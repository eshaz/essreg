// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * phaselog: a tone from Audio 2, heard by another card's capture, and its
 * phase there each quarter of a second: the two cards' clocks against each
 * other, to a fraction of a microsecond, with no timer of the machine's in
 * the way.
 *
 * Notes:
 *
 * The tone is 44100 / 16 Hz at the stream's nominal rate, a table of whole
 * cycles, so that the DAC's clock alone sets its pitch. Each block of the
 * capture goes through a Hann window and is mixed down at the pitch it should
 * have on the capture's nominal clock, so the phase's slope is the difference
 * between the two clocks. The raw clock's reading at each second ties the
 * capture's clock to the machine's.
 *
 * A level below 0 plays the two sides in opposition, for a stereo plug in a
 * balanced input, which hears their difference.
 *
 * usage: phaselog CAPTURE PLAY LEVEL BLOCKS   (a line a block to stdout)
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <alsa/asoundlib.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define RATE_IN 48000
#define BLOCK 12000 // a quarter of a second of the capture
#define RATE_OUT 44100
#define TONE (44100.0 / 16)
#define TONE_PERIOD 16

static const char *play_dev;
static volatile int stop;
static int level;
static int opposed;

static void *player(void *arg)
{
	snd_pcm_t *p;
	short buf[2 * TONE_PERIOD * 512];
	int i, err;

	(void)arg;
	for (i = 0; i < TONE_PERIOD * 512; i++) {
		short v = (short)lrint(level * sin(2 * M_PI * (i % TONE_PERIOD) / TONE_PERIOD));

		buf[2 * i] = v;
		buf[2 * i + 1] = opposed ? -v : v;
	}
	if ((err = snd_pcm_open(&p, play_dev, SND_PCM_STREAM_PLAYBACK, 0)) < 0 ||
	    (err = snd_pcm_set_params(p, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
				      2, RATE_OUT, 0, 300000)) < 0) {
		fprintf(stderr, "play: %s\n", snd_strerror(err));
		exit(1);
	}
	while (!stop) {
		snd_pcm_sframes_t n = snd_pcm_writei(p, buf, TONE_PERIOD * 512);

		if (n < 0 && snd_pcm_recover(p, n, 1) < 0)
			break;
	}
	snd_pcm_drop(p);
	snd_pcm_close(p);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "hw:CARD=S3";
	int seconds = argc > 4 ? atoi(argv[4]) : 60, s;
	static short rec[2 * BLOCK];
	static double hann[BLOCK], co[BLOCK], si[BLOCK];
	unsigned long long done = 0;
	snd_pcm_hw_params_t *hw;
	snd_pcm_sw_params_t *sw;
	snd_pcm_uframes_t period = 4800, buffer = 4 * 4800;
	unsigned int rate = RATE_IN;
	snd_pcm_status_t *st;
	pthread_t th;
	snd_pcm_t *c;
	int i, err;

	play_dev = argc > 2 ? argv[2] : "hw:0,0,0";
	level = argc > 3 ? atoi(argv[3]) : 6000;
	if (level < 0) {
		level = -level;
		opposed = 1;
	}
	for (i = 0; i < BLOCK; i++) {
		double a = 2 * M_PI * TONE * i / RATE_IN;

		hann[i] = 0.5 - 0.5 * cos(2 * M_PI * i / BLOCK);
		co[i] = cos(a);
		si[i] = sin(a);
	}
	if ((err = snd_pcm_open(&c, dev, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		fprintf(stderr, "capture: %s\n", snd_strerror(err));
		return 1;
	}
	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_sw_params_alloca(&sw);
	snd_pcm_status_alloca(&st);
	snd_pcm_hw_params_any(c, hw);
	snd_pcm_hw_params_set_access(c, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(c, hw, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_channels(c, hw, 2);
	snd_pcm_hw_params_set_rate(c, hw, rate, 0);
	snd_pcm_hw_params_set_period_size_near(c, hw, &period, NULL);
	snd_pcm_hw_params_set_buffer_size_near(c, hw, &buffer);
	if ((err = snd_pcm_hw_params(c, hw)) < 0) {
		fprintf(stderr, "capture hw: %s\n", snd_strerror(err));
		return 1;
	}
	snd_pcm_sw_params_current(c, sw);
	snd_pcm_sw_params_set_tstamp_mode(c, sw, SND_PCM_TSTAMP_ENABLE);
	snd_pcm_sw_params_set_tstamp_type(c, sw, SND_PCM_TSTAMP_TYPE_MONOTONIC_RAW);
	snd_pcm_sw_params(c, sw);
	pthread_create(&th, NULL, player, NULL);
	sleep(2);
	snd_pcm_start(c);
	printf("# s frames raw avail amp phase then raw steered real\n");
	for (s = 0; s < seconds; s++) {
		unsigned long got = 0;
		double a = 0, b = 0, phase_step;
		struct timespec w, m, r;
		snd_htimestamp_t ts;

		while (got < BLOCK) {
			snd_pcm_sframes_t n = snd_pcm_readi(c, rec + 2 * got, BLOCK - got);

			if (n < 0) {
				fprintf(stderr, "read: %s at %d\n", snd_strerror(n), s);
				if (snd_pcm_recover(c, n, 1) < 0)
					goto out;
				snd_pcm_start(c);
				continue;
			}
			got += n;
		}
		done += got;
		snd_pcm_status(c, st);
		snd_pcm_status_get_htstamp(st, &ts);
		for (i = 0; i < BLOCK; i++) {
			a += rec[2 * i] * hann[i] * co[i];
			b += rec[2 * i] * hann[i] * si[i];
		}
		// each block starts the mixer afresh, so the tone's phase there
		// moves on by the cycles of a block, a fraction, which goes;
		// what is left is the difference of the clocks
		phase_step = 2 * M_PI * fmod(TONE * BLOCK / RATE_IN * s, 1);
		clock_gettime(CLOCK_MONOTONIC_RAW, &w);
		clock_gettime(CLOCK_MONOTONIC, &m);
		clock_gettime(CLOCK_REALTIME, &r);
		printf("%d %llu %ld.%09ld %ld %.2f %.6f %ld.%09ld %ld.%09ld %ld.%09ld\n",
		       s, done, (long)ts.tv_sec, ts.tv_nsec,
		       (long)snd_pcm_status_get_avail(st),
		       2 * hypot(a, b) / (BLOCK / 2.0), atan2(-b, a) - phase_step,
		       (long)w.tv_sec, w.tv_nsec, (long)m.tv_sec, m.tv_nsec,
		       (long)r.tv_sec, r.tv_nsec);
		fflush(stdout);
	}
out:
	stop = 1;
	pthread_join(th, NULL);
	snd_pcm_close(c);
	return 0;
}
