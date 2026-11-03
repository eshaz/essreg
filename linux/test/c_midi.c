// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The midi group: GMCHECK.MID, the General MIDI check of tools/gmcheck.py,
 * written by the suite and played through the ESFM synth by aplaymidi,
 * with PCM 1 recording the FM exactly; and WildMidi on a DAC, heard by the
 * second card.
 *
 * Notes:
 *
 * The file's steps (docs/TESTING.md, G3): modulation 0.5-4.5 s, channel
 * pressure 5-8.5 s, pan 9-13 s, fine tuning 13.5-17.5 s, coarse tuning
 * 18-20.8 s, a bend range of 12 21.5-25.5 s, the master volume 26-30 s and
 * controller 121 30.5-34 s. ESS's notes play about 12 cents flat, so the
 * pitches are measured against the file's first note, within 10 cents:
 * program 81 is two voices a few cents apart, which beat, so the full
 * vibrato measured 11 to 22 cents from run to run on the Armada, and only
 * its floor of 10 cents, against 0.2 without it, is checked.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cases.h"
#include "hal.h"

// --- GMCHECK.MID -------------------------------------------------------------

struct mev {
	double t;
	int seq;
	unsigned char b[12];
	int n;
};

static struct mev evs[160];
static int nev;

static void at(double t, int n, ...)
{
	va_list ap;
	int i;

	va_start(ap, n);
	evs[nev].t = t;
	evs[nev].seq = nev;
	evs[nev].n = n;
	for (i = 0; i < n; i++)
		evs[nev].b[i] = (unsigned char)va_arg(ap, int);
	va_end(ap);
	nev++;
}

static void note(double on, double off, int key)
{
	at(on, 3, 0x90, key, 100);
	at(off, 3, 0x80, key, 0);
}

static void cc(double t, int num, int val)
{
	at(t, 3, 0xB0, num, val);
}

static void rpn(double t, int num, int msb)
{
	cc(t, 101, num >> 7);
	cc(t, 100, num & 0x7F);
	cc(t, 6, msb);
	cc(t, 101, 0x7F);
	cc(t, 100, 0x7F);
}

static void bend(double t, int v)
{
	at(t, 3, 0xE0, v & 0x7F, v >> 7);
}

static void master(double t, int v)
{
	at(t, 8, 0xF0, 0x7F, 0x7F, 0x04, 0x01, v & 0x7F, v >> 7, 0xF7);
}

static int by_time(const void *a, const void *b)
{
	const struct mev *x = a, *y = b;

	if (x->t != y->t)
		return x->t < y->t ? -1 : 1;
	return x->seq - y->seq;
}

static void events(void)
{
	int i, key;

	nev = 0;
	at(0.0, 6, 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7);
	at(0.1, 2, 0xC0, 80);
	note(0.5, 4.5, 60);
	cc(1.5, 1, 40);
	cc(2.5, 1, 127);
	cc(3.5, 1, 0);
	note(5.0, 8.5, 64);
	at(6.0, 2, 0xD0, 90);
	at(7.0, 2, 0xD0, 0);
	note(9.0, 13.0, 67);
	cc(10.0, 10, 0);
	cc(11.0, 10, 127);
	cc(12.0, 10, 64);
	note(13.5, 17.5, 60);
	rpn(14.5, 1, 0x60);
	rpn(15.5, 1, 0x20);
	rpn(16.5, 1, 0x40);
	note(18.0, 18.8, 60);
	rpn(19.0, 2, 0x40 + 12);
	note(19.0, 19.8, 60);
	rpn(20.0, 2, 0x40);
	note(20.0, 20.8, 60);
	rpn(21.5, 0, 12);
	note(21.5, 25.5, 60);
	for (i = 1; i <= 16; i++)
		bend(22.0 + i / 16.0, 0x2000 + i * 0x200 - (i == 16));
	bend(24.0, 0x2000);
	rpn(25.5, 0, 2);
	for (key = 60; key <= 67; key += key == 60 ? 4 : 3)
		note(26.0, 30.0, key);
	master(27.0, 0x1000);
	master(28.0, 0x2000);
	master(29.0, 0x3FFF);
	cc(30.5, 10, 0);
	cc(30.5, 1, 127);
	bend(30.5, 0x3000);
	note(30.5, 34.0, 60);
	cc(32.5, 121, 0);
	cc(34.0, 10, 64);
	at(34.5, 6, 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7);
	qsort(evs, nev, sizeof(evs[0]), by_time);
}

static int vlq(unsigned char *p, unsigned long v)
{
	unsigned char t[5];
	int n = 0, i;

	do {
		t[n++] = v & 0x7F;
		v >>= 7;
	} while (v);
	for (i = 0; i < n; i++)
		p[i] = t[n - 1 - i] | (i < n - 1 ? 0x80 : 0);
	return n;
}

// midi.opl3's file: the kernel's OPL3 synth sets a voice's sides at its
// note-on only, so each pan comes before its note
static void opl3_events(void)
{
	nev = 0;
	at(0.1, 2, 0xC0, 0);
	note(0.5, 2.0, 60);
	cc(2.5, 10, 0);
	note(3.0, 4.0, 67);
	cc(4.5, 10, 127);
	note(5.0, 6.0, 67);
	cc(6.5, 10, 64);
	note(7.0, 8.0, 67);
	qsort(evs, nev, sizeof(evs[0]), by_time);
}

// a format 0 file of the events, at 480 ticks a beat and 120 beats a
// minute
static int smf_write(const char *path, const char *name)
{
	unsigned char trk[4096], *p = trk;
	unsigned long last = 0, len;
	size_t nlen = strlen(name);
	FILE *f;
	int i;

	p += vlq(p, 0);
	*p++ = 0xFF;
	*p++ = 0x03;
	*p++ = (unsigned char)nlen;
	memcpy(p, name, nlen);
	p += nlen;
	p += vlq(p, 0);
	memcpy(p, "\xFF\x51\x03\x07\xA1\x20", 6);
	p += 6;
	for (i = 0; i < nev; i++) {
		unsigned long tick = lrint(evs[i].t * 2 * 480);

		p += vlq(p, tick - last);
		last = tick;
		if (evs[i].b[0] == 0xF0) {
			*p++ = 0xF0;
			p += vlq(p, evs[i].n - 1);
			memcpy(p, evs[i].b + 1, evs[i].n - 1);
			p += evs[i].n - 1;
		} else {
			memcpy(p, evs[i].b, evs[i].n);
			p += evs[i].n;
		}
	}
	p += vlq(p, 960);
	memcpy(p, "\xFF\x2F\x00", 3);
	p += 3;
	len = p - trk;
	f = fopen(path, "wb");
	if (!f)
		return -1;
	fwrite("MThd\0\0\0\x06\0\0\0\x01\x01\xE0MTrk", 1, 18, f);
	fputc(len >> 24, f);
	fputc(len >> 16 & 0xFF, f);
	fputc(len >> 8 & 0xFF, f);
	fputc(len & 0xFF, f);
	fwrite(trk, 1, len, f);
	return fclose(f);
}

// the file tools/gmcheck.py writes, byte for byte
int et_gmcheck_write(const char *path)
{
	events();
	return smf_write(path, "GM check for ESFM.DRV");
}

// --- the recording -----------------------------------------------------------

struct rec {
	double *ch[2];
	long n;
	double off; // file time of MIDI time 0
};

static const double *win(const struct rec *r, int ch, double t, long *len,
			 double secs)
{
	long a = lrint((t + r->off) * ET_FM_RATE);

	*len = lrint(secs * ET_FM_RATE);
	if (a < 0 || a + *len > r->n)
		return NULL;
	return r->ch[ch] + a;
}

static double level(const struct rec *r, int ch, double t, double secs)
{
	long n;
	const double *x = win(r, ch, t, &n, secs);

	return x ? an_rms(x, n, 1) : 0;
}

static double pitch(const struct rec *r, int ch, double t, double secs,
		    double f0)
{
	struct an_fit fit;
	long n;
	const double *x = win(r, ch, t, &n, secs);

	if (!x || an_fit_sine(x, n, ET_FM_RATE, f0, 1, &fit))
		return 0;
	return fit.f;
}

static double cents(double f, double ref)
{
	return f > 0 && ref > 0 ? 1200 * log2(f / ref) : 1e9;
}

// the vibrato, peak to peak in cents: the pitch in 40 ms fits every
// 10 ms, its trend taken off, and the strongest swing between 4 and 8 Hz,
// where the chip's vibrato runs (about 6 Hz), away from the slow beat of
// a patch's two voices
static double vibrato(const struct rec *r, int ch, double t, double secs,
		      double f0)
{
	enum { MAX = 400 };
	double c[MAX], tt[MAX], sx = 0, sy = 0, sxx = 0, sxy = 0, best = 0;
	double slope, mean, fv, end = t + secs;
	int n = 0, i;

	for (; t + 0.04 <= end && n < MAX; t += 0.01) {
		double v = cents(pitch(r, ch, t, 0.04, f0), f0);

		if (v > 1e8)
			continue;
		tt[n] = t;
		c[n++] = v;
	}
	if (n < 10)
		return 0;
	for (i = 0; i < n; i++) {
		sx += tt[i];
		sy += c[i];
		sxx += tt[i] * tt[i];
		sxy += tt[i] * c[i];
	}
	slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
	mean = sy / n;
	for (i = 0; i < n; i++)
		c[i] -= mean + slope * (tt[i] - sx / n);
	for (fv = 4.0; fv <= 8.0; fv += 0.05) {
		double a = 0, b = 0, amp;

		for (i = 0; i < n; i++) {
			a += c[i] * sin(2 * M_PI * fv * tt[i]);
			b += c[i] * cos(2 * M_PI * fv * tt[i]);
		}
		amp = 2 * hypot(a, b) / n;
		best = amp > best ? amp : best;
	}
	return 2 * best;
}

static void side(struct et_ctx *c, const struct rec *r, double t,
		 const char *what, int want)
{
	char name[64];
	double l = level(r, 0, t, 0.6), rr = level(r, 1, t, 0.6);

	snprintf(name, sizeof(name), "%s, left", what);
	et_measure(c, name, want & 1 ? an_dbfs(l) : l, want & 1 ? "dBFS" : "rms",
		   want & 1 ? -50 : 0, want & 1 ? 0 : 0);
	snprintf(name, sizeof(name), "%s, right", what);
	et_measure(c, name, want & 2 ? an_dbfs(rr) : rr,
		   want & 2 ? "dBFS" : "rms", want & 2 ? -50 : 0,
		   want & 2 ? 0 : 0);
}

static int has_port(const char *name)
{
	char line[256];
	FILE *p = popen("aplaymidi -l 2>/dev/null", "r");
	int found = 0;

	if (!p)
		return 0;
	while (fgets(line, sizeof(line), p))
		found |= strstr(line, name) != NULL;
	pclose(p);
	return found;
}

// midi.esfm: GMCHECK.MID on the ESFM synth, every step on PCM 1
enum et_result c_midi_esfm(struct et_ctx *c, const struct et_case *k)
{
	char path[512], cmd[700];
	struct et_stream s[2];
	struct rec r;
	double ref, f, x;
	long sides = 1, first;
	int ch;

	(void)k;
	if (!has_port("ES1869 ESFM synth")) {
		et_note(c, "no ESFM synth: load snd-es1869-esfm");
		return ET_SKIP;
	}
	snprintf(path, sizeof(path), "%s/GMCHECK.MID", c->outdir);
	if (et_gmcheck_write(path)) {
		et_fail(c, "can't write %s", path);
		return ET_ERROR;
	}
	et_param_get(c, "fm_sides", &sides);
	et_ctl_set(c, "Video Playback Switch", 0, 0, 0);
	et_mix_dig(c, 0);
	snprintf(cmd, sizeof(cmd), "aplaymidi -p 'ES1869 ESFM synth' '%s'",
		 path);
	s[0] = et_rec(ET_FMC, &et_fm_fmt, 37 * ET_FM_RATE, 0);
	memset(&s[1], 0, sizeof(s[1]));
	s[1].role = ET_CMD;
	s[1].cmd = cmd;
	s[1].start = 0.5;
	s[1].timeout = 45;
	if (et_run(c, s, 2) || s[1].status) {
		et_fail(c, "aplaymidi: exit %d", s[1].status);
		free(s[0].data);
		return ET_ERROR;
	}
	r.n = s[0].done;
	for (ch = 0; ch < 2; ch++) {
		long i;

		r.ch[ch] = et_chan(&et_fm_fmt, s[0].data, r.n, ch);
		for (i = 0; i < r.n; i++)
			r.ch[ch][i] -= ET_FM_IDLE;
	}
	first = an_onset(r.ch[0], r.n, 2);
	x = first >= 0 ? first : r.n;
	if (an_onset(r.ch[1], r.n, 2) >= 0 && an_onset(r.ch[1], r.n, 2) < x)
		x = an_onset(r.ch[1], r.n, 2);
	if (x >= r.n) {
		et_fail(c, "the synth made no sound");
		goto out;
	}
	r.off = x / ET_FM_RATE - 0.5;
	et_measure(c, "MIDI time 0 in the recording", r.off, "s", 0, 1.5);

	// the reference: the first note, in tune before the vibrato
	ref = pitch(&r, 1, 0.8, 0.6, 259.8);
	et_measure(c, "C4, against A440", cents(ref, 261.626), "cents", -20,
		   0);
	et_measure(c, "modulation 0: vibrato", vibrato(&r, 1, 0.8, 0.6, ref),
		   "cents", 0, 4);
	et_measure(c, "modulation 40: vibrato", vibrato(&r, 1, 1.7, 0.7, ref),
		   "cents", 5, 22);
	et_measure(c, "modulation 127: vibrato", vibrato(&r, 1, 2.7, 0.7, ref),
		   "cents", 10, 45);
	// program 81's pitch wavers about 4.1 s for a moment, vibrato or not
	et_measure(c, "modulation 0 again", vibrato(&r, 1, 3.6, 0.4, ref),
		   "cents", 0, 4);
	f = pitch(&r, 1, 5.3, 0.6, ref * 1.26);
	et_measure(c, "pressure 90: vibrato", vibrato(&r, 1, 6.1, 0.8, f),
		   "cents", 10, 45);
	et_measure(c, "pressure 0: vibrato", vibrato(&r, 1, 7.6, 0.7, f),
		   "cents", 0, 4);

	side(c, &r, 9.2, "pan 64", 3);
	side(c, &r, 10.2, "pan 0", sides ? 1 : 2);
	side(c, &r, 11.2, "pan 127", sides ? 2 : 1);
	side(c, &r, 12.2, "pan 64 again", 3);

	et_measure(c, "RPN 1 +50 cents",
		   cents(pitch(&r, 1, 14.8, 0.6, ref * 1.03), ref), "cents", 40,
		   60);
	et_measure(c, "RPN 1 -50 cents",
		   cents(pitch(&r, 1, 15.8, 0.6, ref * 0.97), ref), "cents", -60,
		   -40);
	et_measure(c, "RPN 1 in tune",
		   cents(pitch(&r, 1, 16.8, 0.6, ref), ref), "cents", -10, 10);
	et_measure(c, "RPN 2 +12",
		   cents(pitch(&r, 1, 19.2, 0.5, ref * 2), ref), "cents", 1190,
		   1210);
	et_measure(c, "RPN 2 0", cents(pitch(&r, 1, 20.2, 0.5, ref), ref),
		   "cents", -10, 10);
	et_measure(c, "bend range 12, full",
		   cents(pitch(&r, 1, 23.2, 0.6, ref * 2), ref), "cents", 1190,
		   1210);
	et_measure(c, "bend back to center",
		   cents(pitch(&r, 1, 24.2, 0.6, ref), ref), "cents", -10, 10);

	// ESS's master volume turns down only the operators that the patch
	// scales, so it takes program 81's chord down a few dB
	x = level(&r, 1, 26.2, 0.6);
	f = level(&r, 1, 29.1, 0.6);
	et_measure(c, "master 1000h against 3FFFh",
		   20 * log10(level(&r, 1, 27.2, 0.6) / f), "dB", -40, -1);
	et_measure(c, "master 3FFFh against before", 20 * log10(f / x), "dB",
		   -3, 3);

	side(c, &r, 31.0, "controller 121, before", sides ? 1 : 2);
	ch = sides ? 0 : 1;
	f = pitch(&r, ch, 30.8, 0.6, ref * 1.06);
	et_measure(c, "bend +1 semitone", cents(f, ref), "cents", 90, 110);
	et_measure(c, "vibrato before 121", vibrato(&r, ch, 31.0, 1.0, f),
		   "cents", 10, 45);
	et_measure(c, "after 121: in tune",
		   cents(pitch(&r, ch, 33.0, 0.6, ref), ref), "cents", -10, 10);
	et_measure(c, "after 121: vibrato", vibrato(&r, ch, 33.0, 0.8, ref),
		   "cents", 0, 4);
	side(c, &r, 33.1, "controller 121, after", sides ? 1 : 2);
out:
	if (c->fails)
		et_dump(c, "fm", &et_fm_fmt, s[0].data, s[0].done);
	free(r.ch[0]);
	free(r.ch[1]);
	free(s[0].data);
	return ET_PASS;
}

// midi.opl3: the kernel's OPL3 synth with sbiload's patches, PCM 1
// recording the FM: the notes there, the first in tune, and the pan of each
// on the side fm_sides gives
enum et_result c_midi_opl3(struct et_ctx *c, const struct et_case *k)
{
	char path[512], cmd[700];
	struct et_stream s[2];
	struct rec r;
	long sides = 1, first;
	double ref;
	int ch, st;

	(void)k;
	if (access("/usr/bin/sbiload", X_OK)) {
		et_note(c, "no sbiload: install alsa-tools");
		return ET_SKIP;
	}
	if (!has_port("OPL3 FM synth")) {
		et_note(c, "no OPL3 synth: load snd-opl3-synth");
		return ET_SKIP;
	}
	// the four-operator bank from /usr/share/sounds/opl3, through the
	// OPL3 hwdep
	st = system("sbiload >/dev/null 2>&1");
	if (st) {
		et_fail(c, "sbiload: exit %d", st);
		return ET_FAIL;
	}
	snprintf(path, sizeof(path), "%s/OPL3CHK.MID", c->outdir);
	opl3_events();
	if (smf_write(path, "OPL3 check")) {
		et_fail(c, "can't write %s", path);
		return ET_ERROR;
	}
	et_param_get(c, "fm_sides", &sides);
	et_ctl_set(c, "Video Playback Switch", 0, 0, 0);
	et_mix_dig(c, 0);
	snprintf(cmd, sizeof(cmd), "aplaymidi -p 'OPL3 FM synth' '%s'", path);
	s[0] = et_rec(ET_FMC, &et_fm_fmt, 11 * ET_FM_RATE, 0);
	memset(&s[1], 0, sizeof(s[1]));
	s[1].role = ET_CMD;
	s[1].cmd = cmd;
	s[1].start = 0.5;
	s[1].timeout = 20;
	if (et_run(c, s, 2) || s[1].status) {
		et_fail(c, "aplaymidi: exit %d", s[1].status);
		free(s[0].data);
		return ET_ERROR;
	}
	r.n = s[0].done;
	for (ch = 0; ch < 2; ch++) {
		long i;

		r.ch[ch] = et_chan(&et_fm_fmt, s[0].data, r.n, ch);
		for (i = 0; i < r.n; i++)
			r.ch[ch][i] -= ET_FM_IDLE;
	}
	first = an_onset(r.ch[0], r.n, 2);
	if (an_onset(r.ch[1], r.n, 2) >= 0 &&
	    (first < 0 || an_onset(r.ch[1], r.n, 2) < first))
		first = an_onset(r.ch[1], r.n, 2);
	if (first < 0) {
		et_fail(c, "the synth made no sound");
		goto out;
	}
	r.off = (double)first / ET_FM_RATE - 0.5;
	et_measure(c, "MIDI time 0 in the recording", r.off, "s", 0, 1.5);
	ref = pitch(&r, 0, 0.8, 0.6, 261.6);
	et_measure(c, "C4, against A440", cents(ref, 261.626), "cents", -25, 25);
	side(c, &r, 1.0, "pan 64", 3);
	side(c, &r, 3.3, "pan 0", sides ? 1 : 2);
	side(c, &r, 5.3, "pan 127", sides ? 2 : 1);
	side(c, &r, 7.3, "pan 64 again", 3);
out:
	if (c->fails)
		et_dump(c, "fm", &et_fm_fmt, s[0].data, s[0].done);
	free(r.ch[0]);
	free(r.ch[1]);
	free(s[0].data);
	return ET_PASS;
}

// midi.soft: WildMidi plays GMCHECK.MID on Audio 1, heard by the second card
enum et_result c_midi_soft(struct et_ctx *c, const struct et_case *k)
{
	char path[512], cmd[700];
	struct et_stream s[2];
	double *x, quiet, loud;

	(void)k;
	if (access("/usr/bin/wildmidi", X_OK) ||
	    !strcmp(c->hal->name, "simulated")) {
		et_note(c, "no wildmidi");
		return ET_SKIP;
	}
	snprintf(path, sizeof(path), "%s/GMCHECK.MID", c->outdir);
	if (et_gmcheck_write(path))
		return ET_ERROR;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, 12, 0);
	snprintf(cmd, sizeof(cmd),
		 "wildmidi -d plughw:CARD=ES1869,DEV=0,SUBDEV=1 '%s'", path);
	s[0] = et_rec(ET_REC2, &et_rec2_fmt, 12 * 48000, 0);
	memset(&s[1], 0, sizeof(s[1]));
	s[1].role = ET_CMD;
	s[1].cmd = cmd;
	s[1].start = 0.3;
	s[1].timeout = 11;
	et_run(c, s, 2);
	x = et_chan(&et_rec2_fmt, s[0].data, s[0].done, 0);
	quiet = et_noise_floor(x, s[0].done);
	loud = an_rms(x + 48000 * 2, 48000 * 2, 1);
	et_measure(c, "music over the noise", 20 * log10((loud + 1) / (quiet + 1)),
		   "dB", 10, 200);
	free(x);
	free(s[0].data);
	return ET_PASS;
}
