// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * a2stall: the stops of Audio 2's DAC, from its DMA position against the raw
 * clock. The DMA moves only as the DAC takes data, so when the chip's FIFO runs
 * dry the position falls behind the clock by the length of the stop, and stays
 * behind. It plays silence, so that nothing else has to listen, and needs no
 * second card, whose own traffic on the bus is part of what it measures.
 *
 *   a2stall [-d DEVICE] RATE SECONDS [PERIOD [BUFFER]]
 *
 * prints each stop with its time and length, then the count, the longest and
 * the sum. Readings every few ms show a stop of more than about two frames.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <alsa/asoundlib.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define SETTLE 2.0 // seconds a start takes to settle
#define NEED 8 // readings that have to agree on a stop
#define MIN_FRAMES 2.0 // the smallest stop told from a reading's own step
#define FOLLOW (1.0 / 256) // of a reading, for the slow drift of the two clocks

#define CHECK(call)                                                            \
	do {                                                                   \
		int err_ = (call);                                             \
		if (err_ < 0) {                                                \
			fprintf(stderr, "a2stall: %s: %s\n", #call,            \
				snd_strerror(err_));                           \
			return 2;                                              \
		}                                                              \
	} while (0)

static double seconds(const snd_htimestamp_t *t)
{
	return t->tv_sec + t->tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
	const char *dev = "hw:CARD=ES1869,DEV=0,SUBDEV=0";
	snd_pcm_uframes_t period = 4096, buffer = 16384;
	snd_pcm_hw_params_t *hw;
	snd_pcm_sw_params_t *sw;
	snd_pcm_status_t *st;
	double t0 = 0, base = 0, sum = 0, longest = 0, secs;
	unsigned long long written = 0, xruns = 0;
	unsigned int rate, seen;
	int have = 0, opt, stops = 0, over = 0;
	short *zero;
	snd_pcm_t *h;

	while ((opt = getopt(argc, argv, "d:")) != -1) {
		if (opt != 'd')
			return 1;
		dev = optarg;
	}
	if (argc - optind < 2) {
		fprintf(stderr, "usage: a2stall [-d DEVICE] RATE SECONDS [PERIOD [BUFFER]]\n");
		return 1;
	}
	rate = strtoul(argv[optind], NULL, 0);
	secs = strtod(argv[optind + 1], NULL);
	if (argc - optind > 2)
		period = strtoul(argv[optind + 2], NULL, 0);
	if (argc - optind > 3)
		buffer = strtoul(argv[optind + 3], NULL, 0);
	CHECK(snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0));
	snd_pcm_hw_params_alloca(&hw);
	CHECK(snd_pcm_hw_params_any(h, hw));
	CHECK(snd_pcm_hw_params_set_access(h, hw, SND_PCM_ACCESS_RW_INTERLEAVED));
	CHECK(snd_pcm_hw_params_set_format(h, hw, SND_PCM_FORMAT_S16_LE));
	CHECK(snd_pcm_hw_params_set_channels(h, hw, 2));
	seen = rate;
	CHECK(snd_pcm_hw_params_set_rate_near(h, hw, &seen, NULL));
	CHECK(snd_pcm_hw_params_set_period_size_near(h, hw, &period, NULL));
	CHECK(snd_pcm_hw_params_set_buffer_size_near(h, hw, &buffer));
	CHECK(snd_pcm_hw_params(h, hw));
	snd_pcm_sw_params_alloca(&sw);
	CHECK(snd_pcm_sw_params_current(h, sw));
	CHECK(snd_pcm_sw_params_set_tstamp_mode(h, sw, SND_PCM_TSTAMP_ENABLE));
	CHECK(snd_pcm_sw_params_set_tstamp_type(h, sw,
						SND_PCM_TSTAMP_TYPE_MONOTONIC_RAW));
	CHECK(snd_pcm_sw_params_set_start_threshold(h, sw, buffer / 2));
	CHECK(snd_pcm_sw_params(h, sw));
	zero = calloc(period, 4);
	if (!zero)
		return 2;
	snd_pcm_status_alloca(&st);
	printf("a2stall: %u Hz, period %lu, buffer %lu\n", seen, period, buffer);
	for (;;) {
		snd_pcm_sframes_t room, n;
		snd_htimestamp_t t;
		double now, frame, lag;

		CHECK(snd_pcm_status(h, st));
		room = snd_pcm_status_get_avail(st);
		if (snd_pcm_status_get_state(st) == SND_PCM_STATE_XRUN) {
			xruns++;
			CHECK(snd_pcm_prepare(h));
			have = 0;
			continue;
		}
		// the position goes with the frames written when it was read, so
		// that comes before this round's write
		frame = (double)written - snd_pcm_status_get_delay(st);
		while (room >= (snd_pcm_sframes_t)period) {
			n = snd_pcm_writei(h, zero, period);
			if (n < 0) {
				CHECK(snd_pcm_recover(h, n, 1));
				break;
			}
			written += n;
			room -= n;
		}
		if (snd_pcm_status_get_state(st) != SND_PCM_STATE_RUNNING) {
			usleep(1000);
			continue;
		}
		snd_pcm_status_get_htstamp(st, &t);
		now = seconds(&t);
		if (!have) {
			t0 = now;
			have = 1;
		}
		if (now - t0 > secs)
			break;
		// what the clock says was played, less what the DMA took
		lag = (now - t0) * seen - frame;
		if (now - t0 < SETTLE) {
			base = lag;
		} else if (lag - base > MIN_FRAMES) {
			if (++over >= NEED) {
				double us = (lag - base) * 1e6 / seen;

				printf("%8.3f s: %6.0f us\n", now - t0, us);
				fflush(stdout);
				stops++;
				sum += us;
				if (us > longest)
					longest = us;
				base = lag;
				over = 0;
			}
		} else {
			over = 0;
			base += (lag - base) * FOLLOW;
		}
		usleep(3000);
	}
	snd_pcm_drop(h);
	snd_pcm_close(h);
	printf("stops %d, longest %.0f us, sum %.0f us, xruns %llu, in %.0f s\n",
	       stops, longest, sum, xruns, secs);
	return xruns ? 3 : 0;
}
