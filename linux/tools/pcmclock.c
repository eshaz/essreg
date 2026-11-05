// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * pcmclock: any card's sample clock against the machine's clocks. It records
 * and throws the samples away, and for each second writes the earliest stamp of
 * that second's periods, which an interrupt's delay can only make later, with
 * the frames up to it, then the raw, steered and real-time clocks read together.
 *
 *   pcmclock DEVICE RATE SECONDS [PERIOD]
 *
 * prints: second, frames, stamp (raw, ns), raw, steered, real (ns). Frames over
 * the stamps gives the card against the raw clock, and the last three give the
 * raw clock against the steered one, whatever chrony is doing.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long long ns(const struct timespec *t)
{
	return t->tv_sec * 1000000000LL + t->tv_nsec;
}

static long long clock_ns(clockid_t id)
{
	struct timespec t;

	clock_gettime(id, &t);
	return ns(&t);
}

#define CHECK(call)                                                            \
	do {                                                                   \
		int err_ = (call);                                             \
		if (err_ < 0) {                                                \
			fprintf(stderr, "pcmclock: %s: %s\n", #call,           \
				snd_strerror(err_));                           \
			return 2;                                              \
		}                                                              \
	} while (0)

int main(int argc, char **argv)
{
	snd_pcm_uframes_t period, buffer;
	snd_pcm_hw_params_t *hw;
	snd_pcm_sw_params_t *sw;
	snd_pcm_status_t *st;
	unsigned long long total = 0, best_frames = 0;
	long long best = 0, best_stamp = 0, start;
	unsigned int rate, seen;
	long second = -1, secs;
	unsigned long xruns = 0;
	snd_pcm_t *h;
	short *buf;

	if (argc < 4) {
		fprintf(stderr, "usage: pcmclock DEVICE RATE SECONDS [PERIOD]\n");
		return 1;
	}
	rate = atoi(argv[2]);
	secs = atol(argv[3]);
	period = argc > 4 ? strtoul(argv[4], NULL, 0) : 1024;
	buffer = 4 * period;
	CHECK(snd_pcm_open(&h, argv[1], SND_PCM_STREAM_CAPTURE, 0));
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
	CHECK(snd_pcm_sw_params(h, sw));
	fprintf(stderr, "pcmclock: %s at %u Hz, period %lu, buffer %lu\n", argv[1],
		seen, period, buffer);
	buf = malloc(period * 4);
	if (!buf)
		return 2;
	snd_pcm_status_alloca(&st);
	CHECK(snd_pcm_start(h));
	start = clock_ns(CLOCK_MONOTONIC_RAW);
	for (;;) {
		snd_pcm_sframes_t n = snd_pcm_readi(h, buf, period);
		snd_htimestamp_t t;
		long long stamp, late;
		unsigned long long at;
		long now;

		if (n < 0) {
			xruns++;
			fprintf(stderr, "pcmclock: %s at second %ld\n",
				snd_strerror(n), second);
			CHECK(snd_pcm_recover(h, n, 1));
			CHECK(snd_pcm_start(h));
			total += 1ULL << 40; // a mark that the count broke
			continue;
		}
		total += n;
		CHECK(snd_pcm_status(h, st));
		snd_pcm_status_get_htstamp(st, &t);
		stamp = ns(&t);
		at = total + snd_pcm_status_get_avail(st);
		late = stamp - (long long)((double)at * 1e9 / seen);
		now = (stamp - start) / 1000000000LL;
		if (now != second) {
			if (second >= 0) {
				long long raw = clock_ns(CLOCK_MONOTONIC_RAW);
				long long mono = clock_ns(CLOCK_MONOTONIC);
				long long real = clock_ns(CLOCK_REALTIME);

				printf("%ld %llu %lld %lld %lld %lld\n", second,
				       best_frames, best_stamp, raw, mono, real);
				fflush(stdout);
			}
			if (now >= secs)
				break;
			second = now;
			best = late;
			best_frames = at;
			best_stamp = stamp;
		} else if (late < best) {
			best = late;
			best_frames = at;
			best_stamp = stamp;
		}
	}
	snd_pcm_close(h);
	fprintf(stderr, "pcmclock: %ld s, %lu xruns\n", second, xruns);
	return xruns ? 3 : 0;
}
