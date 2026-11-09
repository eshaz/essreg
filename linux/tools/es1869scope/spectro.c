// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * spectro.c: the spectrogram view (spectro.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "spectro.h"

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fft.h"
#include "plot.h"
#include "say.h"

#define DB_FLOOR -1000 // tenths of a dB at the colour map's black

struct spectro {
	struct fft fft;
	int16_t *x, *z, *db;
	unsigned int fill; // samples in x
	unsigned int rate, channels;
	snd_pcm_t *pcm;
	int16_t *wav; // a file's samples, mono
	size_t wav_n, wav_at;
	// the waterfall's place on the canvas, and its next row
	int ax, ay, aw, ah, row;
	int sx, sy, sw, sh; // the spectrum strip under it
	uint32_t map[256];
	double us; // an FFT's time, averaged
	unsigned long blocks;
	char name[96];
};

// magma: black, purple, a red-orange, a pale yellow
static void make_map(uint32_t *map)
{
	static const int stops[5][3] = { { 0, 0, 4 },
					 { 59, 15, 112 },
					 { 181, 54, 122 },
					 { 251, 135, 97 },
					 { 252, 253, 191 } };
	int i, k;

	for (i = 0; i < 256; i++) {
		double t = i / 255.0 * 4;
		int s = t >= 4 ? 3 : (int)t;
		double f = t - s;
		uint32_t rgb = 0;

		for (k = 0; k < 3; k++)
			rgb = rgb << 8 |
			      (uint32_t)(stops[s][k] +
					 f * (stops[s + 1][k] - stops[s][k]));
		map[i] = rgb;
	}
}

// a WAV file's 16-bit samples, mixed to one channel
static int load_wav(struct spectro *s, const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned char h[12], ck[8];
	unsigned int ch = 0, bits = 0;

	if (!f || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) ||
	    memcmp(h + 8, "WAVE", 4))
		goto bad;
	while (fread(ck, 1, 8, f) == 8) {
		uint32_t len = ck[4] | ck[5] << 8 | ck[6] << 16 |
			       (uint32_t)ck[7] << 24;

		if (!memcmp(ck, "fmt ", 4)) {
			unsigned char fm[16];

			if (len < 16 || fread(fm, 1, 16, f) != 16)
				goto bad;
			ch = fm[2] | fm[3] << 8;
			s->rate = fm[4] | fm[5] << 8 | fm[6] << 16;
			bits = fm[14] | fm[15] << 8;
			fseek(f, len - 16, SEEK_CUR);
		} else if (!memcmp(ck, "data", 4)) {
			size_t i, n;
			int16_t *raw;

			if (bits != 16 || !ch || ch > 2)
				goto bad;
			n = len / 2;
			raw = malloc(n * 2);
			if (!raw || fread(raw, 2, n, f) != n) {
				free(raw);
				goto bad;
			}
			s->wav_n = n / ch;
			for (i = 0; i < s->wav_n; i++)
				raw[i] = ch == 1 ? raw[i] :
						   (int16_t)((raw[2 * i] +
							      raw[2 * i + 1]) /
							     2);
			s->wav = raw;
			fclose(f);
			snprintf(s->name, sizeof(s->name), "%.80s", path);
			return 0;
		} else {
			fseek(f, len + (len & 1), SEEK_CUR);
		}
	}
bad:
	if (f)
		fclose(f);
	say("%s: not a 16-bit WAV", path);
	return -1;
}

static int open_capture(struct spectro *s, const char *dev, unsigned int rate)
{
	int err;

	err = snd_pcm_open(&s->pcm, dev, SND_PCM_STREAM_CAPTURE,
			   SND_PCM_NONBLOCK);
	if (!err)
		err = snd_pcm_set_params(s->pcm, SND_PCM_FORMAT_S16_LE,
					 SND_PCM_ACCESS_RW_INTERLEAVED, 2, rate,
					 1, 200000);
	if (err < 0) {
		say("%s: %s", dev, snd_strerror(err));
		if (s->pcm)
			snd_pcm_close(s->pcm);
		s->pcm = NULL;
		return -1;
	}
	s->rate = rate;
	s->channels = 2;
	snprintf(s->name, sizeof(s->name), "%.80s", dev);
	return snd_pcm_start(s->pcm) < 0 ? -1 : 0;
}

struct spectro *spectro_open(const char *dev, unsigned int rate,
			     const char *wav, unsigned int log2n, int mmx)
{
	struct spectro *s = calloc(1, sizeof(*s));
	unsigned int n = 1u << log2n;

	if (!s || fft_init(&s->fft, log2n, mmx))
		goto bad;
	s->x = calloc(n, sizeof(*s->x));
	s->z = calloc(2 * n, sizeof(*s->z));
	s->db = calloc(n / 2, sizeof(*s->db));
	if (!s->x || !s->z || !s->db)
		goto bad;
	make_map(s->map);
	if (wav ? load_wav(s, wav) : open_capture(s, dev, rate))
		goto bad;
	return s;
bad:
	spectro_close(s);
	return NULL;
}

void spectro_close(struct spectro *s)
{
	if (!s)
		return;
	if (s->pcm)
		snd_pcm_close(s->pcm);
	fft_free(&s->fft);
	free(s->x);
	free(s->z);
	free(s->db);
	free(s->wav);
	free(s);
}

static void status_line(struct spectro *s, struct canvas *c)
{
	fill(c, 0, 0, c->w, c->font->h + 6, C_BACK);
	textf(c, 6, 2, C_TEXT,
	      "es1869scope  spectrogram  %s, %u Hz, %u-point FFT in %s, %.0f us",
	      s->name, s->rate, s->fft.n, s->fft.mmx ? "MMX" : "C", s->us);
}

void spectro_frame(struct spectro *s, struct canvas *c)
{
	int fh = c->font->h, fw = c->font->w, k;
	char buf[24];

	fill(c, 0, 0, c->w, c->h, C_BACK);
	s->ax = 6 * fw;
	s->ay = 2 * fh + 12;
	s->aw = c->w - s->ax - 8;
	s->sh = c->h / 6;
	s->ah = c->h - s->ay - s->sh - 2 * fh - 10;
	s->sx = s->ax;
	s->sw = s->aw;
	s->sy = s->ay + s->ah + fh + 6;
	s->row = 0;
	status_line(s, c);
	// the frequency along the top, every 5 kHz
	for (k = 0; k * 5000 <= (int)s->rate / 2; k++) {
		int x = s->ax + (int)((long)k * 5000 * 2 * s->aw / s->rate);

		si(k * 5000.0, "Hz", buf, sizeof(buf));
		text(c, x - (int)strlen(buf) * fw / 2, s->ay - fh - 2, C_DIM,
		     buf);
		fill(c, x, s->ay - 2, 1, 2, C_AXIS);
	}
	box(c, s->ax - 1, s->ay - 1, s->aw + 2, s->ah + 2, C_AXIS);
	fill(c, s->ax, s->ay, s->aw, s->ah, 0);
	box(c, s->sx - 1, s->sy - 1, s->sw + 2, s->sh + 2, C_AXIS);
	text(c, 6, s->sy, C_DIM, "0 dB");
	text(c, 6, s->sy + s->sh - fh, C_DIM, "-100");
}

// a block's FFT as one row, the loudest bin of each pixel's share
static void row(struct spectro *s, struct canvas *c)
{
	unsigned int bins = s->fft.n / 2;
	uint32_t *p = c->px + (size_t)(s->ay + s->row) * c->w + s->ax;
	int x, y;

	for (x = 0; x < s->aw; x++) {
		unsigned int b0 = (unsigned int)x * bins / s->aw;
		unsigned int b1 = (unsigned int)(x + 1) * bins / s->aw, b;
		int v = s->db[b0];

		for (b = b0 + 1; b < b1; b++)
			if (s->db[b] > v)
				v = s->db[b];
		v = (v - DB_FLOOR) * 255 / -DB_FLOOR;
		p[x] = s->map[v < 0 ? 0 : v > 255 ? 255 : v];
	}
	c->dirty[s->ay + s->row] = 1;
	s->row = (s->row + 1) % s->ah;
	// the line that marks now, on the next row
	fill(c, s->ax, s->ay + s->row, s->aw, 1, C_TEXT);
	// the latest spectrum in the strip under it, every fourth block
	if (s->blocks % 4)
		return;
	fill(c, s->sx, s->sy, s->sw, s->sh, C_PANEL);
	for (x = 0; x < s->sw; x++) {
		int v = s->db[(unsigned int)x * bins / s->sw];

		y = (v - DB_FLOOR) * s->sh / -DB_FLOOR;
		y = y < 0 ? 0 : y >= s->sh ? s->sh - 1 : y;
		fill(c, s->sx + x, s->sy + s->sh - 1 - y, 1, y + 1, 0x5aa9ff);
	}
}

static void block(struct spectro *s, struct canvas *c)
{
	struct timespec a, b;

	clock_gettime(CLOCK_MONOTONIC, &a);
	fft_run(&s->fft, s->x, s->z);
	clock_gettime(CLOCK_MONOTONIC, &b);
	fft_db(&s->fft, s->z, s->db);
	s->us = s->us * 0.9 + 0.1 * ((b.tv_sec - a.tv_sec) * 1e6 +
				     (b.tv_nsec - a.tv_nsec) / 1e3);
	s->blocks++;
	row(s, c);
	s->fill = 0;
}

int spectro_step(struct spectro *s, struct canvas *c)
{
	int16_t buf[2 * 1024];
	int rows = 0;

	if (s->wav) {
		while (s->wav_at < s->wav_n) {
			s->x[s->fill++] = s->wav[s->wav_at++];
			if (s->fill == s->fft.n) {
				block(s, c);
				rows++;
			}
		}
		status_line(s, c);
		return rows;
	}
	for (;;) {
		snd_pcm_sframes_t r = snd_pcm_readi(s->pcm, buf, 1024);
		snd_pcm_sframes_t i;

		if (r == -EAGAIN)
			break;
		if (r < 0) {
			if (snd_pcm_recover(s->pcm, r, 1) < 0 ||
			    snd_pcm_start(s->pcm) < 0)
				return -1;
			continue;
		}
		for (i = 0; i < r; i++) {
			s->x[s->fill++] =
				(int16_t)((buf[2 * i] + buf[2 * i + 1]) / 2);
			if (s->fill == s->fft.n) {
				block(s, c);
				rows++;
			}
		}
	}
	if (rows)
		status_line(s, c);
	return rows;
}
