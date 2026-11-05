// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A small change of rate for Audio 2's ring, and the chip's clock against
 * the system's, with no kernel API, so that they also build on the host.
 *
 * Notes:
 *
 * The chip has one clock input and no trim, so the only way to another rate
 * is to change the samples. Its two clocks are one master divided by 30 and
 * 31: es1869test reads them 251.95 ppm apart to within 0.003 ppm. The 793.8
 * kHz clock is the one that comes out true, and what the chip calls 768 kHz
 * is 768193.5 Hz, so that 48 kHz plays 252 ppm sharp on any crystal. On the
 * Armada the master reads 800 / 481 of the board's 14.318 MHz, the same
 * crystal as the system's PM timer by all appearances.
 *
 * The filter is a Kaiser-windowed sinc (beta 9) of 68 taps, which the table
 * holds at 128 places between two frames. A frame's row is blended from the
 * two places around it, each coefficient, so that the place is exact to 15
 * more bits, where the nearest of 1024 places left a noise of -64 dB at 16
 * kHz. What sets the length is the top of the band: a tone's image comes back
 * right beside it, at the beat, so that the stopband decides the spurs there,
 * not the places. At 44.1 kHz the image of 20 kHz lies at 24.1 kHz, and the
 * filter's edge has to fall between them. 36 taps held up to 18 kHz and then
 * gave -56 dB at 19 kHz and -29 dB at 20 kHz; 68 hold to 20 kHz.
 *
 * MMX has 16-bit coefficients, whose rounding alone would stop it at -84 dB,
 * so the taps have scales by how large they get, from 16 bits at the centre
 * to 24 at the ends. A coefficient of 1 fits in none of them, so the two taps
 * nearest the place hold what they have over 1/2, and rs_half adds the half
 * back. The sums are shifted to one scale from the outside in, and the scales
 * are as fine as the worst signal allows without a sum leaving 32 bits. The
 * first row, whose 1/2 doesn't fit in 16 bits, has a gain of -0.0001 dB.
 *
 * Two things are worked out ahead, since this is the driver's only heavy work.
 * A taps' word read across a cache line costs the Celeron half a sum again, and
 * three frames in four are read that way, so each run has the channels in
 * four copies, a sample apart, and a frame takes the one that lies on 8 bytes.
 * And the outer words of a blend hold from one frame to the next, at the middle
 * of a place 2^-12 of a frame wide: they are small, and their error is under the
 * 16-bit noise at any step, which at 30 ppm leaves them 8 frames unchanged.
 *
 * es1869_rs_run_c() and es1869_rs_run_mmx() give the same bytes.
 *
 * The clock: each interrupt of a stream is a number of the master's ticks at
 * a time on the raw system clock. Interrupts come late, by 15 us rms and at
 * times by 500, but the earliest of a second's worth lies on a line within
 * 1.35 us rms, and the line through those samples has the chip's rate within
 * a few ppb. That is far better than NTP over the internet can give, and the
 * NTP daemon's corrections wander, so the slope of the steered clock against
 * the raw one is fitted apart, over a longer time. The two fits go forward
 * from what they saw and take nothing from the filter, so there is no loop
 * to hunt. The fit is a line by least squares while it grows, and then with a
 * fading memory: for a line through k + 1 samples the gains are those of
 * es1869_fit_add().
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "resample.h"

// the words' scales in bits, outside in and out again
static const u8 rs_scale[ES_RS_TAPS / 4] = {
	24, 24, 23, 21, 20, 19, 18, 17, 16, 17, 18, 19, 20, 21, 23, 24, 24
};

#define RS_FINE 24 // the finest scale, on which a row's sum is taken
#define RS_CENTRE 16 // the centre word's

// --- the table, in integers ----------------------------------------------------

#define Q30 (1LL << 30)
#define RS_PLACES (ES_RS_ROWS / 2 + 1) // the rest are these mirrored
#define RS_SPAN ((s64)ES_RS_ROWS * (ES_RS_TAPS / 2)) // the window's half
#define RS_PI_Q30 3373259426LL
#define RS_ROWS_PI_Q20 42722830LL // 128 / pi
#define RS_BETA_SQ_4 81 // beta squared over 4 is 81 / 4
#define RS_TERMS 18

static s64 q30_mul(s64 a, s64 b)
{
	return (a * b) >> 30;
}

// sin(x) up to pi / 2
static s64 q30_sin(s64 x)
{
	static const u8 div[] = { 156, 110, 72, 42, 20, 6 };
	s64 x2 = q30_mul(x, x), v = Q30;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(div); i++)
		v = Q30 - div_s64(q30_mul(x2, v), div[i]);
	return q30_mul(x, v);
}

// the series of I0(beta sqrt(u)) over I0(beta), as a polynomial in u
static void kaiser_series(s64 *c)
{
	s64 t[RS_TERMS];
	u64 sum = 0;
	unsigned int k;

	t[0] = 1LL << 40;
	for (k = 1; k < RS_TERMS; k++)
		t[k] = div_s64(t[k - 1] / 4 * RS_BETA_SQ_4, k * k);
	for (k = 0; k < RS_TERMS; k++)
		sum += t[k];
	for (k = 0; k < RS_TERMS; k++)
		c[k] = div_s64(t[k], (u32)(sum >> 30));
}

static s64 kaiser(const s64 *c, s64 d)
{
	s64 u = Q30 - div_s64((d * d) << 22, (u32)((RS_SPAN * RS_SPAN) >> 8));
	s64 v = c[RS_TERMS - 1];
	int k;

	for (k = RS_TERMS - 2; k >= 0; k--)
		v = c[k] + q30_mul(v, u);
	return v;
}

// one row at the place i of ES_RS_ROWS past a frame. The two taps around the
// place give up 1/2, which makes the gain 1, so that the rest sums to 0, and
// the tap nearest the place takes what the rounding left of that
static void rs_row(s16 *row, unsigned int i, const s64 *series)
{
	s64 s = q30_sin(div_s64(RS_PI_Q30 * i, ES_RS_ROWS));
	s32 taps[ES_RS_TAPS];
	s64 sum = 0;
	unsigned int n;
	int c;

	for (n = 0; n < ES_RS_TAPS; n++) {
		// how far the tap is from the place, in rows
		s64 d = ((s64)n - ES_RS_BEHIND) * ES_RS_ROWS - i;
		unsigned int bits = rs_scale[n / 4];
		s64 h = Q30, v;

		if (d) {
			// sin(pi d / rows) over pi d / rows, the sine's sign
			// from the whole frames between the tap and the place
			h = div_s64(s * RS_ROWS_PI_Q20, d) >> 20;
			if (!((n - ES_RS_BEHIND) & 1))
				h = -h;
		}
		v = q30_mul(h, kaiser(series, d));
		if (n == ES_RS_BEHIND || n == ES_RS_BEHIND + 1)
			v -= Q30 / 2;
		v = (v + (1LL << (29 - bits))) >> (30 - bits);
		taps[n] = v;
		sum += v * (1LL << (RS_FINE - bits));
	}
	c = ES_RS_BEHIND + (i * 2 >= ES_RS_ROWS);
	taps[c] += (-sum + (1 << (RS_FINE - RS_CENTRE - 1))) >>
		   (RS_FINE - RS_CENTRE);
	for (n = 0; n < ES_RS_TAPS; n++)
		row[n] = taps[n] > 32767 ? 32767 :
			 taps[n] < -32768 ? -32768 : taps[n];
}

static s16 rs_up(s16 v)
{
	return v < 32767 ? v + 1 : v;
}

// the filter: for each place the taps, then 2 x the way to the next place's,
// which PMULHW with a 15-bit fraction takes back to the place in between.
// The second half of the places are the first's taps the other way round.
// PMULHW rounds down, by half a unit of each tap on average, a little gain
// lost at the low end, so every other tap has one unit more, the same ones
// at every place: anything that changed with the place would be a spur
void es1869_rs_table(s16 *tab)
{
	s16 end[ES_RS_TAPS];
	s64 series[RS_TERMS];
	unsigned int i, n;

	kaiser_series(series);
	for (i = 0; i < RS_PLACES; i++)
		rs_row(tab + i * ES_RS_STRIDE, i, series);
	for (i = RS_PLACES; i < ES_RS_ROWS; i++)
		for (n = 0; n < ES_RS_TAPS; n++)
			tab[i * ES_RS_STRIDE + n] =
				tab[(ES_RS_ROWS - i) * ES_RS_STRIDE +
				    ES_RS_TAPS - 1 - n];
	for (n = 0; n < ES_RS_TAPS; n++)
		end[n] = tab[ES_RS_TAPS - 1 - n];
	for (n = 0; n < ES_RS_TAPS; n += 2) {
		for (i = 0; i < ES_RS_ROWS; i++)
			tab[i * ES_RS_STRIDE + n] =
				rs_up(tab[i * ES_RS_STRIDE + n]);
		end[n] = rs_up(end[n]);
	}
	for (i = 0; i < ES_RS_ROWS; i++) {
		s16 *row = tab + i * ES_RS_STRIDE;
		const s16 *next = i + 1 < ES_RS_ROWS ? row + ES_RS_STRIDE : end;

		for (n = 0; n < ES_RS_TAPS; n++)
			row[ES_RS_TAPS + n] = 2 * (next[n] - row[n]);
	}
}

// --- a run ---------------------------------------------------------------------

// a stream's frames into a channel each
void es1869_rs_split(s16 *l, s16 *r, const void *src, u32 frames, bool stereo)
{
	const s16 *s = src;
	u32 i;

	if (!stereo) {
		memcpy(l, s, 2 * frames);
		return;
	}
	for (i = 0; i < frames; i++) {
		l[i] = s[2 * i];
		r[i] = s[2 * i + 1];
	}
}

#ifdef ES1869_RS_MMX
static inline void rs_copy_word(s16 *to, const s16 *from)
{
	asm volatile("movq (%1),%%mm0\n\t"
		     "movq %%mm0,(%0)\n\t"
		     : : "r" (to), "r" (from) : "memory");
}
#else
static inline void rs_copy_word(s16 *to, const s16 *from)
{
	memcpy(to, from, 8);
}
#endif

// the channels of a run of n frames a sample, two and three on, in copies that
// lie on 8 bytes, each frame's taps in one of them. MMX is the quick way here,
// and the caller holds the FPU for it
void es1869_rs_shift(struct es1869_rs *rs, struct es1869_rs_job *j, u32 n,
		     bool stereo)
{
	u32 want = (n + ES_RS_TAPS + 4) / 4, c, k, i;

	for (c = 0; c < (stereo ? 2 : 1); c++) {
		j->ch[c] = rs->ch[c];
		for (k = 0; k < 3; k++) {
			const s16 *from = rs->ch[c] + k + 1;
			s16 *to = rs->odd[c][k];

			j->odd[c][k] = to;
			for (i = 0; i < want; i++, from += 4, to += 4)
				rs_copy_word(to, from);
		}
	}
}

// the 15 bits that follow the row's place
static inline u32 rs_blend_bits(u32 frac)
{
	return (frac >> 10) & 0x7FFF;
}

#define RS_HALF (1 << 13) // the 1/2 that the taps around the place gave up
#define RS_HELD (1U << 20) // the width in frac of a place of the outer words
#define RS_INNER 16 // the taps that take every frame's own place start here
#define RS_OUTER 52 // and end here

static inline const s16 *rs_place(const s16 *tab, u32 frac)
{
	return tab + (frac >> 25) * ES_RS_STRIDE;
}

// the rounding of a sample: without dither one half, with it a triangle of
// 1 LSB each way, from the difference of two uniform values, one frame's
// from the last's, which shifts the noise up the band
static inline void rs_round(struct es1869_rs_job *j, s32 *rnd)
{
	u32 x = j->seed;
	s32 l, r;

	if (!j->dither) {
		rnd[0] = rnd[1] = 1 << 13;
		return;
	}
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	j->seed = x;
	l = x & 0x3FFF;
	r = (x >> 14) & 0x3FFF;
	rnd[0] = (1 << 13) + l - j->last[0];
	rnd[1] = (1 << 13) + r - j->last[1];
	j->last[0] = l;
	j->last[1] = r;
}

static inline void rs_next(struct es1869_rs_job *j, u32 *i)
{
	u32 f = j->frac + (u32)j->step;

	*i += 1;
	if (j->step >= 0) {
		if (f < j->frac)
			*i += 1;
	} else if (f > j->frac) {
		*i -= 1;
	}
	j->frac = f;
}

// where the outer words' blend stands for a frame at frac
static inline u32 rs_held(u32 frac)
{
	return (frac & ~(RS_HELD - 1)) | (RS_HELD / 2);
}

static void rs_blend_c(s16 *c, const s16 *row, s32 f, unsigned int from,
		       unsigned int to)
{
	unsigned int i;

	for (i = from; i < to; i++)
		c[i] = row[i] + ((row[ES_RS_TAPS + i] * f) >> 16);
}

#define RS_WORD_C(a, b, w) do { \
	a += x[4 * (w)] * c[4 * (w)] + x[4 * (w) + 1] * c[4 * (w) + 1]; \
	b += x[4 * (w) + 2] * c[4 * (w) + 2] + x[4 * (w) + 3] * c[4 * (w) + 3]; \
} while (0)

#define RS_SHIFT_C(a, b, n) do { \
	a >>= n; \
	b >>= n; \
} while (0)

// the taps' sum on the scale of 14 bits, the shifts per MMX lane as there
static s32 rs_sum_c(const s16 *x, const s16 *c)
{
	s32 a = 0, b = 0, p = 0, q = 0;

	RS_WORD_C(a, b, 0);
	RS_WORD_C(a, b, 16);
	RS_WORD_C(a, b, 1);
	RS_WORD_C(a, b, 15);
	RS_SHIFT_C(a, b, 1);
	RS_WORD_C(a, b, 2);
	RS_WORD_C(a, b, 14);
	RS_SHIFT_C(a, b, 2);
	RS_WORD_C(a, b, 3);
	RS_WORD_C(a, b, 13);
	RS_SHIFT_C(a, b, 1);
	RS_WORD_C(a, b, 4);
	RS_WORD_C(a, b, 12);
	RS_SHIFT_C(a, b, 1);
	RS_WORD_C(a, b, 5);
	RS_WORD_C(a, b, 11);
	RS_SHIFT_C(a, b, 1);
	RS_WORD_C(a, b, 6);
	RS_WORD_C(a, b, 10);
	RS_SHIFT_C(a, b, 2);
	// 17 bits don't fit under the sum so far: down to its 16 first
	RS_WORD_C(p, q, 7);
	RS_WORD_C(p, q, 9);
	RS_SHIFT_C(p, q, 1);
	a += p;
	b += q;
	RS_SHIFT_C(a, b, 2);
	p = 0;
	q = 0;
	RS_WORD_C(p, q, 8);
	RS_SHIFT_C(p, q, 2);
	a += p + x[ES_RS_BEHIND] * RS_HALF;
	b += q + x[ES_RS_BEHIND + 1] * RS_HALF;
	return a + b;
}

static inline s32 rs_clip(s32 v)
{
	v >>= 14;
	return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

u32 es1869_rs_run_c(struct es1869_rs_job *j)
{
	s16 c[ES_RS_TAPS];
	u32 i = 0, o = 0, held = 0;
	u8 *d = j->dst;

	while (i < j->frames && o < j->room) {
		u32 at = rs_held(j->frac);
		s32 rnd[2], v;

		if (at != held) {
			const s16 *row = rs_place(j->tab, at);
			u32 f = rs_blend_bits(at);

			held = at;
			rs_blend_c(c, row, f, 0, RS_INNER);
			rs_blend_c(c, row, f, RS_OUTER, ES_RS_TAPS);
		}
		rs_blend_c(c, rs_place(j->tab, j->frac), rs_blend_bits(j->frac),
			   RS_INNER, RS_OUTER);
		rs_round(j, rnd);
		v = rs_clip(rs_sum_c(j->ch[0] + i, c) + rnd[0]);
		d[0] = v;
		d[1] = 0;
		d[2] = v >> 8;
		d[3] = 0;
		d += 4;
		if (j->stereo) {
			v = rs_clip(rs_sum_c(j->ch[1] + i, c) + rnd[1]);
			d[0] = v;
			d[1] = 0;
			d[2] = v >> 8;
			d[3] = 0;
			d += 4;
		}
		o++;
		rs_next(j, &i);
	}
	j->used = i;
	return o;
}

#ifdef ES1869_RS_MMX

// the compiler has no use for the MMX registers (the kernel's -mno-mmx; on
// the host it has SSE), so they aren't clobbers, as in lib/raid6/mmx.c, and
// they hold their values from one asm statement to the next

#define RS_BLEND(w, s) \
	"movq " s "(%1),%%mm0\n\t" \
	"pmulhw %%mm7,%%mm0\n\t" \
	"paddw " w "(%1),%%mm0\n\t" \
	"movq %%mm0," w "(%0)\n\t"

// the row of the place: mm7 the fraction in each lane
static inline void rs_blend_inner(s16 *c, const s16 *row, u32 f)
{
	asm volatile("movd %2,%%mm7\n\t"
		     "punpckldq %%mm7,%%mm7\n\t"
		     RS_BLEND("32", "168") RS_BLEND("40", "176")
		     RS_BLEND("48", "184") RS_BLEND("56", "192")
		     RS_BLEND("64", "200") RS_BLEND("72", "208")
		     RS_BLEND("80", "216") RS_BLEND("88", "224")
		     RS_BLEND("96", "232")
		     : : "r" (c), "r" (row), "r" (f | f << 16) : "memory");
}

static inline void rs_blend_outer(s16 *c, const s16 *row, u32 f)
{
	asm volatile("movd %2,%%mm7\n\t"
		     "punpckldq %%mm7,%%mm7\n\t"
		     RS_BLEND("0", "136") RS_BLEND("8", "144")
		     RS_BLEND("16", "152") RS_BLEND("24", "160")
		     RS_BLEND("104", "240") RS_BLEND("112", "248")
		     RS_BLEND("120", "256") RS_BLEND("128", "264")
		     : : "r" (c), "r" (row), "r" (f | f << 16) : "memory");
}

#define RS_WORD(w) \
	"movq " w "(%0),%%mm1\n\t" \
	"pmaddwd " w "(%1),%%mm1\n\t" \
	"paddd %%mm1,%%mm0\n\t"

// the half for the taps around the place, the second and third of their word
static const u64 rs_half __aligned(8) = (u64)RS_HALF << 16 | (u64)RS_HALF << 32;

// the taps' sum into mm0, its low 32 bits
static inline void rs_sum_mmx(const s16 *x, const s16 *c)
{
	asm volatile("pxor %%mm0,%%mm0\n\t"
		     RS_WORD("0") RS_WORD("128") RS_WORD("8") RS_WORD("120")
		     "psrad $1,%%mm0\n\t"
		     RS_WORD("16") RS_WORD("112")
		     "psrad $2,%%mm0\n\t"
		     RS_WORD("24") RS_WORD("104")
		     "psrad $1,%%mm0\n\t"
		     RS_WORD("32") RS_WORD("96")
		     "psrad $1,%%mm0\n\t"
		     RS_WORD("40") RS_WORD("88")
		     "psrad $1,%%mm0\n\t"
		     RS_WORD("48") RS_WORD("80")
		     "psrad $2,%%mm0\n\t"
		     "movq 56(%0),%%mm1\n\t"
		     "pmaddwd 56(%1),%%mm1\n\t"
		     "movq 72(%0),%%mm2\n\t"
		     "pmaddwd 72(%1),%%mm2\n\t"
		     "paddd %%mm2,%%mm1\n\t"
		     "psrad $1,%%mm1\n\t"
		     "paddd %%mm1,%%mm0\n\t"
		     "psrad $2,%%mm0\n\t"
		     "movq 64(%0),%%mm2\n\t"
		     "movq %%mm2,%%mm1\n\t"
		     "pmaddwd 64(%1),%%mm1\n\t"
		     "pmaddwd %2,%%mm2\n\t"
		     "psrad $2,%%mm1\n\t"
		     "paddd %%mm1,%%mm0\n\t"
		     "paddd %%mm2,%%mm0\n\t"
		     "movq %%mm0,%%mm1\n\t"
		     "psrlq $32,%%mm1\n\t"
		     "paddd %%mm1,%%mm0\n\t"
		     : : "r" (x), "r" (c), "m" (rs_half)
		     : "memory");
}

// mm0 and the channel before it, in mm5, to words of the ring
static inline void rs_out_stereo(u8 *d, const s32 *rnd)
{
	asm volatile("punpckldq %%mm0,%%mm5\n\t"
		     "paddd %1,%%mm5\n\t"
		     "psrad $14,%%mm5\n\t"
		     "packssdw %%mm5,%%mm5\n\t"
		     "pxor %%mm1,%%mm1\n\t"
		     "punpcklbw %%mm1,%%mm5\n\t"
		     "movq %%mm5,%0\n\t"
		     : "=m" (*(u64 *)d) : "m" (*(const u64 *)rnd));
}

static inline void rs_out_mono(u8 *d, const s32 *rnd)
{
	asm volatile("paddd %1,%%mm0\n\t"
		     "psrad $14,%%mm0\n\t"
		     "packssdw %%mm0,%%mm0\n\t"
		     "pxor %%mm1,%%mm1\n\t"
		     "punpcklbw %%mm1,%%mm0\n\t"
		     "movd %%mm0,%0\n\t"
		     : "=m" (*(u32 *)d) : "m" (*(const u32 *)rnd));
}

// a frame's taps on 8 bytes: the copy of the channel that has them there
static inline const s16 *rs_taps(const struct es1869_rs_job *j, unsigned int c,
				 u32 i)
{
	u32 k = i & 3;

	return (k ? j->odd[c][k - 1] : j->ch[c]) + i - k;
}

// the caller holds the FPU, as the kernel has it (kernel_fpu_begin())
u32 es1869_rs_run_mmx(struct es1869_rs_job *j)
{
	s16 c[ES_RS_TAPS] __aligned(8);
	u32 i = 0, o = 0, held = 0;
	u8 *d = j->dst;

	while (i < j->frames && o < j->room) {
		u32 at = rs_held(j->frac);
		s32 rnd[2] __aligned(8);

		if (at != held) {
			held = at;
			rs_blend_outer(c, rs_place(j->tab, at), rs_blend_bits(at));
		}
		rs_blend_inner(c, rs_place(j->tab, j->frac),
			       rs_blend_bits(j->frac));
		rs_round(j, rnd);
		rs_sum_mmx(rs_taps(j, 0, i), c);
		if (j->stereo) {
			asm volatile("movq %%mm0,%%mm5" : : );
			rs_sum_mmx(rs_taps(j, 1, i), c);
			rs_out_stereo(d, rnd);
			d += 8;
		} else {
			rs_out_mono(d, rnd);
			d += 4;
		}
		o++;
		rs_next(j, &i);
	}
	asm volatile("emms");
	j->used = i;
	return o;
}

#endif /* ES1869_RS_MMX */

// --- the line through the ring ---------------------------------------------------

#define RS_SINCE (1U << 30) // since this far, a stream's history is all there

// a stream's start, or a pause's release, at the ring's first frame
void es1869_rs_start(struct es1869_rs *rs, unsigned long pos)
{
	rs->frac = 0;
	rs->w = 0;
	rs->pos = pos;
}

// the line moved d ring frames along: the stream's frame there is d of them
// and the steps over them
void es1869_rs_seek(struct es1869_rs *rs, const struct es1869_rs_stream *s,
		    long d)
{
	unsigned long b = s->buffer, bound = s->boundary;
	s64 q = (s64)rs->frac + (s64)d * rs->step;
	long carry = (long)(q >> 32), dm = d % (long)b, cm = carry % (long)b;

	rs->frac = (u32)q;
	rs->w = (rs->w + b + dm) % b;
	*s->rot = (*s->rot + b + cm) % b;
	rs->pos = (rs->pos + bound + (d + carry) % (long)bound) % bound;
	if (d < 0 && (u32)-d >= rs->since) {
		rs->since = 0;
	} else {
		s64 v = (s64)rs->since + d;

		rs->since = v > RS_SINCE ? RS_SINCE : v;
	}
}

// the channels around the n frames from pos, into the filter's window: what
// comes before the stream's start or after the last frame written counts
// as silence
static void rs_window(struct es1869_rs *rs, const struct es1869_rs_stream *s,
		      u32 n)
{
	unsigned long b = s->buffer, bound = s->boundary;
	unsigned int fb = s->stereo ? 4 : 2;
	u32 want = n + ES_RS_TAPS + 1;
	u32 head = rs->since < ES_RS_BEHIND ? ES_RS_BEHIND - rs->since : 0;
	unsigned long ahead = (s->appl + bound - rs->pos) % bound;
	unsigned long a;
	u32 end, j = head;

	if (ahead > b)
		ahead = 0;
	end = want < ES_RS_BEHIND + ahead ? want : ES_RS_BEHIND + ahead;
	if (head) {
		memset(rs->ch[0], 0, 2 * head);
		memset(rs->ch[1], 0, 2 * head);
	}
	a = (rs->pos % b + b + head - ES_RS_BEHIND) % b;
	while (j < end) {
		u32 piece = end - j < b - a ? end - j : b - a;

		es1869_rs_split(rs->ch[0] + j, rs->ch[1] + j,
				(const u8 *)s->src + a * fb, piece, s->stereo);
		j += piece;
		a = (a + piece) % b;
	}
	if (j < want) {
		memset(rs->ch[0] + j, 0, 2 * (want - j));
		memset(rs->ch[1] + j, 0, 2 * (want - j));
	}
}

// up to a chunk of frames through the filter into the ring; the frames gone
// through
static u32 rs_chunk(struct es1869_rs *rs, const struct es1869_rs_stream *s,
		    u32 n)
{
	unsigned long b = s->buffer;
	struct es1869_rs_job j = {
		.tab = rs->tab,
		.dst = s->ring + 2 * rs->w * (s->stereo ? 4 : 2),
		.frames = n,
		.room = b - rs->w,
		.frac = rs->frac,
		.step = rs->step,
		.stereo = s->stereo,
		.dither = rs->dither,
		.seed = rs->seed,
		.last = { rs->last[0], rs->last[1] },
	};
	u32 out;

	rs_window(rs, s, n);
#ifdef ES1869_RS_MMX
	if (s->mmx) {
		es1869_rs_shift(rs, &j, n, s->stereo);
		out = es1869_rs_run_mmx(&j);
	} else
#endif
	{
		j.ch[0] = rs->ch[0];
		j.ch[1] = rs->ch[1];
		out = es1869_rs_run_c(&j);
	}
	rs->frac = j.frac;
	rs->seed = j.seed;
	rs->last[0] = j.last[0];
	rs->last[1] = j.last[1];
	*s->rot = (*s->rot + b + j.used % b - out % b) % b;
	rs->w = (rs->w + out) % b;
	rs->pos = (rs->pos + j.used) % s->boundary;
	rs->since = rs->since + j.used > RS_SINCE ? RS_SINCE : rs->since + j.used;
	return j.used;
}

// a fill's frames through the filter, from where the line stands: a plan
// that begins elsewhere moves it first
void es1869_rs_fill(struct es1869_rs *rs, const struct es1869_rs_stream *s,
		    unsigned long start, unsigned long frames)
{
	unsigned long bound = s->boundary;
	long d = (long)((start + bound - rs->pos) % bound);

	if ((unsigned long)d > bound / 2)
		d -= (long)bound;
	if (d < -1 || d > 1)
		es1869_rs_seek(rs, s, d);
	while (frames) {
		u32 n = frames < ES_RS_CHUNK ? frames : ES_RS_CHUNK;
		u32 used = rs_chunk(rs, s, n);

		if (!used)
			break;
		frames -= used < frames ? used : frames;
	}
}

// how far a fill may go, up to the stream's frame to, with the DMA at hw. The
// ring holds as many frames as the buffer, but a step down makes more ring
// frames than stream frames, so that a full buffer's last frames would land
// on what the DMA has yet to play, and those are the frames made as if
// silence followed. A 256th of the buffer covers any step the settings allow
unsigned long es1869_rs_limit(unsigned long to, unsigned long hw,
			      unsigned long buffer, unsigned long boundary)
{
	unsigned long most = buffer - ES_RS_ROOM - buffer / 256;
	unsigned long ahead = (to + boundary - hw) % boundary;

	if (ahead <= most || ahead > buffer + ES_RS_ROOM)
		return to;
	return (hw + most) % boundary;
}

// how far along the line a ring frame a behind the next to be written lies
// from the frame there would be with no step, in whole frames
long es1869_rs_behind(const struct es1869_rs *rs, unsigned long a)
{
	s64 q = (s64)rs->frac - (s64)a * rs->step;

	return (long)(q >> 32);
}

// --- the clock -----------------------------------------------------------------

#define FIT_FRAC 40 // the fit's slope, in bits
#define FIT_LIMIT 500000 // a sample further off the line in ns stands out
#define FIT_ODD 3 // and this many in a row move the line: a slip, not a rate
#define CLOCK_NS_NUM 500000 // ns to a tick: this over CLOCK_NS_DEN
#define CLOCK_NS_DEN 11907

// a clock's ticks to a frame at the rate num / den
u32 es1869_clock_tpf(u32 num, u32 den)
{
	return den * (num == 768000 ? 31 : num == 396900 ? 60 : 30);
}

// a clock's rate over what it is called, as the step that plays it true on
// a master of ES_CLOCK_HZ, 32 bits
s32 es1869_clock_base(u32 num)
{
	return num == 768000 ?
		-(s32)div_s64(6000LL << 32, ES_CLOCK_HZ) : 0;
}

// ppb to the step's 32 bits
s32 es1869_clock_q32(s32 ppb)
{
	return div_s64((s64)ppb << 32, 1000000000);
}

// a2_trim's items in ppb: 0 in the middle, and 0.1, 0.2, 0.5 ppm and so on
// to 500 either side of it
s32 es1869_trim_ppb(unsigned int item)
{
	static const u8 digit[] = { 1, 2, 5 };
	int n = (int)item - ES_TRIM_ZERO, a = n < 0 ? -n : n;
	s32 v = 100;
	int i;

	if (!n)
		return 0;
	for (i = 0; i < (a - 1) / 3; i++)
		v *= 10;
	v *= digit[(a - 1) % 3];
	return n < 0 ? -v : v;
}

// the fit's slope in ppb
s64 es1869_clock_ppb(s64 rate)
{
	return (rate * 1000000000) >> FIT_FRAC;
}

static void fit_init(struct es1869_fit *f, u32 kmax)
{
	memset(f, 0, sizeof(*f));
	f->kmax = kmax;
}

// the sample dx of the time on, over dt of the reference. The gains are a
// least-squares line's until kmax, and a line that forgets from there
static void fit_add(struct es1869_fit *f, s64 dx, s64 dt)
{
	s64 pred, e;
	u32 k, q;

	if (dt <= 0)
		return;
	f->x += dx - dt;
	pred = f->line + ((f->rate * dt) >> FIT_FRAC);
	e = f->x - pred;
	if (e > FIT_LIMIT || e < -FIT_LIMIT) {
		if (++f->odd < FIT_ODD) {
			f->line = pred;
			return;
		}
		f->line = f->x;
		f->odd = 0;
		return;
	}
	f->odd = 0;
	k = f->k + 1 > f->kmax ? f->kmax : f->k + 1;
	q = (k + 1) * (k + 2);
	f->line = pred + div_s64(e * 2 * (2 * k + 1), q);
	f->rate += div_s64(div64_s64(e * (1LL << FIT_FRAC), dt) * 6, q);
	f->k = k;
}

void es1869_clock_init(struct es1869_clock *c)
{
	memset(c, 0, sizeof(*c));
	fit_init(&c->chip, ES_FIT_CHIP);
	fit_init(&c->system, ES_FIT_SYSTEM);
	c->ns_q16 = div_u64((u64)CLOCK_NS_NUM << 16, CLOCK_NS_DEN);
}

// an engine stopped or another took over: the slopes stay, the rest starts
// again from the next interrupt
void es1869_clock_stretch(struct es1869_clock *c)
{
	c->ticks = 0;
	c->have = false;
	c->fed = false;
	c->timed = false;
}

// the next group of interrupts, measured from this one, which isn't in it:
// it may be the last sample, and two samples need time between them
static void clock_group(struct es1869_clock *c, u64 raw)
{
	c->g_ticks = c->ticks;
	c->g_raw = raw;
	c->g_best = S64_MAX;
}

// a stream's interrupt, ticks on from the last, at raw ns (0 for one that
// can't be timed). True when it closed a sample, so that the caller reads
// the system's two clocks. The earliest of about a second's interrupts is
// the sample, by where the line has it, since all of them come late
bool es1869_clock_irq(struct es1869_clock *c, u32 ticks, u64 raw)
{
	u64 dt, dc;
	s64 r;

	c->ticks += ticks;
	if (!raw)
		return false;
	if (!c->have) {
		c->have = true;
		clock_group(c, raw);
		return false;
	}
	dt = raw - c->g_raw;
	dc = c->ticks - c->g_ticks;
	r = dt - ((dc * c->ns_q16) >> 16);
	if (r < c->g_best) {
		c->g_best = r;
		c->b_ticks = c->ticks;
		c->b_raw = raw;
	}
	if (dt < ES_CLOCK_GROUP_NS)
		return false;
	if (c->fed) {
		s64 ns = div_u64((c->b_ticks - c->l_ticks) * CLOCK_NS_NUM,
				 CLOCK_NS_DEN);

		fit_add(&c->chip, ns, c->b_raw - c->l_raw);
		c->samples++;
		c->ns_q16 = div_u64((u64)CLOCK_NS_NUM << 16, CLOCK_NS_DEN) -
			((((s64)c->ns_q16 * c->chip.rate) >> FIT_FRAC));
	} else {
		c->fed = true;
		c->chip.line = c->chip.x;
		c->chip.odd = 0;
	}
	c->l_ticks = c->b_ticks;
	c->l_raw = c->b_raw;
	clock_group(c, raw);
	return true;
}

// the raw clock and the steered one, read together
void es1869_clock_system(struct es1869_clock *c, u64 raw, u64 mono)
{
	if (c->timed) {
		fit_add(&c->system, mono - c->u_mono, raw - c->u_raw);
	} else {
		c->timed = true;
		c->system.line = c->system.x;
		c->system.odd = 0;
	}
	c->u_raw = raw;
	c->u_mono = mono;
}

// what the step takes for the chip's clock against the steered one
s32 es1869_clock_offset(const struct es1869_clock *c)
{
	return -((c->chip.rate - c->system.rate) >> (FIT_FRAC - 32));
}
