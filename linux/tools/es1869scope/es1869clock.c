// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869clock: the ES1869's clock against the machine's, real time and two
 * other cards' crystals, logged a line at a time for es1869scope and for
 * reading later.
 *
 *   es1869clock [-o LOG] [-t SECONDS] [-l DB] [-V LEVEL] [-I] [-e PLAY]
 *               [-a CAPTURE|none] [-A NAME] [-p PLAY|none] [-c CAPTURE]
 *               [-B NAME]
 *
 * Notes:
 *
 * Three comparisons run together:
 * - The chip against the raw clock: each interrupt the driver stamps for its
 *   Clock mode, from /proc/asound/ES1869/es1869_irqs where the driver has
 *   that file (-I leaves them out of the log).
 * - The raw clock against UTC, as chronyd steers the system clock: the three
 *   clocks and adjtimex each second, and chronyc's tracking each minute.
 * - The crystals against each other: a pilot of 19600 Hz from Audio 1 (-e),
 *   which -a records, by default the Focusrite through the cable from the
 *   ES1869's front out (pair -A, ef), and one of 12000 Hz from -p, by default
 *   the Focusrite's outputs 3 and 4, which -c records, by default the VX
 *   (pair -B, fv). Each pilot's phase, in blocks of 0.1 s of its capture, is
 *   a time error in nanoseconds, and two pairs that share a card give each
 *   crystal's own noise (tie_hat in tie.c). -p none leaves the second pair
 *   out: -a plughw:CARD=VXPocket,DEV=0 -A ev -p none times the ES1869
 *   against the VX alone, and -a none -p none plays the ES1869's pilot for
 *   its interrupts and records nothing.
 *
 * A stream with GIVE_UP errors in ten seconds is closed for the rest of the
 * run, so that a sick USB device isn't sent a prepare after every one.
 *
 * The ES1869's pilot is -l dBFS, -30 by default, and -V sets Audio 1's volume
 * (0 to 15) for the run, which a saved mixer may hold at 0, putting the old
 * value back at the end. A pilot is a table of whole
 * cycles at its DAC's nominal rate, so that its pitch is that DAC's clock.
 *
 * The log's lines, a letter and then fields:
 *   s RAW REAL BOOT FREQ_PPB TICK STATUS ESTERR_US   each second
 *   k MASTER_PPB STEERED_PPB STEP_PPB                the driver's clock lines
 *   i SEQ SRC TICKS RAW                              each stamped interrupt
 *   p PAIR RAW X_NS AMP                              each pilot block
 *   t CHRONYC_TRACKING_CSV                           each minute
 *   w TEXT                                           what went wrong
 * RAW, REAL and BOOT are the clocks in ns. A block's RAW is its middle, on
 * its capture's line against the raw clock. PAIR is ef (the ES1869 against
 * the Focusrite) or fv (the Focusrite against the VX), and X_NS grows while
 * the first runs ahead of the second.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <alsa/asoundlib.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <sys/timex.h>
#include <time.h>
#include <unistd.h>

#include "phase.h"

#define BLOCK_S 0.1
#define MAX_FDS 16

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

static long long ns_of(clockid_t id)
{
	struct timespec t;

	clock_gettime(id, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static FILE *out;

static void logw(const char *fmt, ...)
{
	va_list ap;

	fputs("w ", out);
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
	va_start(ap, fmt);
	fputs("es1869clock: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

// a stream of the three cards
struct stream {
	const char *name;
	const char *dev;
	snd_pcm_stream_t dir;
	unsigned int channels, rate;
	snd_pcm_uframes_t period, buffer;
	snd_pcm_t *pcm;
	int nfds, fd0; // its poll descriptors in the table
	// playback: a table of whole cycles of the pilot, on some channels
	short *table;
	unsigned int table_frames, table_at;
	// capture: a block for the pilot's phase, and the line of its frames
	// against the raw clock
	short *block;
	unsigned int block_frames, block_at;
	unsigned int pilot_channel;
	struct ph_est ph;
	const char *pair;
	unsigned long long frames;
	long long first_raw, best_raw;
	unsigned long long first_frames, best_frames;
	int have_line;
	// errors in the last ten seconds, and whether it was given up
	unsigned int errors;
	long long errors_since;
	int dead;
};

#define GIVE_UP 20 // errors in ten seconds: the stream goes

// an error of a stream: recovered, or, when they come too often, the stream
// closed for the rest of the run, so that a sick device isn't hammered
static void trouble(struct stream *s, int err)
{
	long long now = ns_of(CLOCK_MONOTONIC);

	if (now - s->errors_since > 10000000000LL) {
		s->errors_since = now;
		s->errors = 0;
	}
	logw("%s: %s", s->name, snd_strerror(err));
	if (++s->errors >= GIVE_UP) {
		logw("%s: %u errors in ten seconds, closed", s->name,
		     s->errors);
		snd_pcm_drop(s->pcm);
		s->dead = 1;
		return;
	}
	snd_pcm_recover(s->pcm, err, 1);
}

static int open_stream(struct stream *s)
{
	snd_pcm_hw_params_t *hw;
	snd_pcm_sw_params_t *sw;
	unsigned int rate = s->rate;
	int err;

	err = snd_pcm_open(&s->pcm, s->dev, s->dir, SND_PCM_NONBLOCK);
	if (err < 0)
		goto fail;
	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_hw_params_any(s->pcm, hw);
	err = snd_pcm_hw_params_set_access(s->pcm, hw,
					   SND_PCM_ACCESS_RW_INTERLEAVED);
	if (!err)
		err = snd_pcm_hw_params_set_format(s->pcm, hw,
						   SND_PCM_FORMAT_S16_LE);
	if (!err)
		err = snd_pcm_hw_params_set_channels(s->pcm, hw, s->channels);
	if (!err)
		err = snd_pcm_hw_params_set_rate_resample(s->pcm, hw, 0);
	if (!err)
		err = snd_pcm_hw_params_set_rate(s->pcm, hw, rate, 0);
	if (!err)
		err = snd_pcm_hw_params_set_period_size_near(s->pcm, hw,
							     &s->period, NULL);
	s->buffer = 8 * s->period;
	if (!err)
		err = snd_pcm_hw_params_set_buffer_size_near(s->pcm, hw,
							     &s->buffer);
	if (!err)
		err = snd_pcm_hw_params(s->pcm, hw);
	if (err < 0)
		goto fail;
	snd_pcm_sw_params_alloca(&sw);
	snd_pcm_sw_params_current(s->pcm, sw);
	snd_pcm_sw_params_set_avail_min(s->pcm, sw, s->period);
	snd_pcm_sw_params_set_tstamp_mode(s->pcm, sw, SND_PCM_TSTAMP_ENABLE);
	snd_pcm_sw_params_set_tstamp_type(s->pcm, sw,
					  SND_PCM_TSTAMP_TYPE_MONOTONIC_RAW);
	if (s->dir == SND_PCM_STREAM_PLAYBACK)
		snd_pcm_sw_params_set_start_threshold(s->pcm, sw, s->buffer);
	err = snd_pcm_sw_params(s->pcm, sw);
	if (err < 0)
		goto fail;
	fprintf(out, "# %s: %s, %u Hz, %u channels, period %lu, buffer %lu\n",
		s->name, s->dev, s->rate, s->channels, s->period, s->buffer);
	return 0;
fail:
	logw("%s: %s: %s", s->name, s->dev, snd_strerror(err));
	return err;
}

// a pilot of cycles whole cycles in frames frames on the channels in mask
static int make_table(struct stream *s, unsigned int frames,
		      unsigned int cycles, double dbfs, unsigned int mask)
{
	double a = 32767 * pow(10, dbfs / 20);
	unsigned int k, c;

	s->table = calloc((size_t)frames * s->channels, sizeof(short));
	if (!s->table)
		return -ENOMEM;
	s->table_frames = frames;
	for (k = 0; k < frames; k++)
		for (c = 0; c < s->channels; c++)
			if (mask & (1u << c))
				s->table[k * s->channels + c] =
					(short)lrint(a * sin(2 * M_PI * k *
							     cycles / frames));
	return 0;
}

static void feed(struct stream *s)
{
	short buf[4096];
	snd_pcm_sframes_t room = snd_pcm_avail_update(s->pcm);
	unsigned int max = sizeof(buf) / sizeof(buf[0]) / s->channels;

	if (room < 0) {
		trouble(s, room);
		return;
	}
	while (room > 0) {
		unsigned int n = max, k;
		snd_pcm_sframes_t w;

		if (room < (snd_pcm_sframes_t)max)
			n = (unsigned int)room;
		for (k = 0; k < n; k++) {
			memcpy(buf + k * s->channels,
			       s->table + s->table_at * s->channels,
			       s->channels * sizeof(short));
			s->table_at = (s->table_at + 1) % s->table_frames;
		}
		w = snd_pcm_writei(s->pcm, buf, n);
		if (w < 0) {
			trouble(s, w);
			return;
		}
		if ((unsigned int)w < n)
			// the frames not taken come again from the table's place
			s->table_at = (s->table_at + s->table_frames -
				       (n - w) % s->table_frames) %
				      s->table_frames;
		room -= w;
	}
}

// the capture's frames against the raw clock: the earliest stamp for its
// frame count over the run, which an interrupt's delay can only make later
static void line_point(struct stream *s)
{
	snd_pcm_status_t *st;
	snd_htimestamp_t t;
	unsigned long long at;
	long long raw;

	snd_pcm_status_alloca(&st);
	if (snd_pcm_status(s->pcm, st) < 0)
		return;
	snd_pcm_status_get_htstamp(st, &t);
	raw = t.tv_sec * 1000000000LL + t.tv_nsec;
	if (!raw)
		return;
	at = s->frames + snd_pcm_status_get_avail(st);
	if (!s->have_line) {
		s->first_raw = s->best_raw = raw;
		s->first_frames = s->best_frames = at;
		s->have_line = 1;
		return;
	}
	// against the nominal rate, the point that sits earliest
	if (raw - (long long)((at - s->best_frames) * 1e9 / s->rate) <
	    s->best_raw) {
		s->best_raw = raw;
		s->best_frames = at;
	}
}

static long long frame_raw(const struct stream *s, double frame)
{
	double since = (frame - s->best_frames) / s->rate;

	return s->best_raw + (long long)(since * 1e9);
}

// a full block, which ended at frame end of the capture
static void block_done(struct stream *s, unsigned long long end)
{
	const short *first = s->block + s->pilot_channel;
	double mid = end - s->block_frames / 2.0, amp, x;

	x = ph_block(&s->ph, first, s->channels, s->block_frames, &amp);
	if (s->have_line)
		fprintf(out, "p %s %lld %.3f %.1f\n", s->pair,
			frame_raw(s, mid), x * 1e9, amp);
	s->block_at = 0;
}

static void take(struct stream *s)
{
	short buf[4096];
	unsigned int max = sizeof(buf) / sizeof(buf[0]) / s->channels;

	for (;;) {
		snd_pcm_sframes_t r = snd_pcm_readi(s->pcm, buf, max);
		unsigned int k = 0;

		if (r == -EAGAIN)
			break;
		if (r < 0) {
			trouble(s, r);
			if (s->dead)
				break;
			snd_pcm_start(s->pcm);
			// the phase can't bridge a gap: start the pair afresh
			ph_init(&s->ph, s->ph.f, s->ph.rate);
			s->block_at = 0;
			fprintf(out, "w %s restarts\n", s->pair);
			break;
		}
		while (k < (unsigned int)r) {
			unsigned int n = s->block_frames - s->block_at;

			if (n > r - k)
				n = r - k;
			memcpy(s->block + s->block_at * s->channels,
			       buf + k * s->channels,
			       n * s->channels * sizeof(short));
			s->block_at += n;
			k += n;
			if (s->block_at == s->block_frames)
				block_done(s, s->frames + k);
		}
		s->frames += r;
		line_point(s);
	}
}

// --- Audio 1's volume ----------------------------------------------------------

// The pilot leaves the chip through Audio 1's volume ("PCM Playback Volume",
// index 1), which a saved mixer may hold at 0: -V sets it for the run and the
// old value goes back at the end, and into the log in case the end is a crash
static snd_ctl_t *mixer;
static snd_ctl_elem_value_t *a1_old;

static void a1_id(snd_ctl_elem_value_t *v)
{
	snd_ctl_elem_value_set_interface(v, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_value_set_name(v, "PCM Playback Volume");
	snd_ctl_elem_value_set_index(v, 1);
}

static int a1_volume(int level)
{
	snd_ctl_elem_value_t *v;
	int err;

	err = snd_ctl_open(&mixer, "hw:CARD=ES1869", 0);
	if (err < 0)
		return err;
	if (snd_ctl_elem_value_malloc(&a1_old) || snd_ctl_elem_value_malloc(&v))
		return -ENOMEM;
	a1_id(a1_old);
	err = snd_ctl_elem_read(mixer, a1_old);
	if (err < 0)
		return err;
	fprintf(out, "# Audio 1's volume was %ld,%ld, now %d,%d\n",
		snd_ctl_elem_value_get_integer(a1_old, 0),
		snd_ctl_elem_value_get_integer(a1_old, 1), level, level);
	a1_id(v);
	snd_ctl_elem_value_set_integer(v, 0, level);
	snd_ctl_elem_value_set_integer(v, 1, level);
	err = snd_ctl_elem_write(mixer, v);
	snd_ctl_elem_value_free(v);
	return err < 0 ? err : 0;
}

static void a1_restore(void)
{
	if (!mixer || !a1_old)
		return;
	snd_ctl_elem_write(mixer, a1_old);
	snd_ctl_close(mixer);
	mixer = NULL;
}

// --- once a second -----------------------------------------------------------

static void system_line(void)
{
	struct timex tx = { 0 };
	long long raw, real, boot;

	raw = ns_of(CLOCK_MONOTONIC_RAW);
	real = ns_of(CLOCK_REALTIME);
	boot = ns_of(CLOCK_BOOTTIME);
	adjtimex(&tx);
	fprintf(out, "s %lld %lld %lld %.3f %ld %d %ld\n", raw, real, boot,
		tx.freq * 1000.0 / 65536, tx.tick, tx.status, tx.esterror);
}

// the driver's clock lines, as /proc shows them; - where it has none
static void driver_line(void)
{
	FILE *f = fopen("/proc/asound/ES1869/es1869", "r");
	char line[256];
	long long master = 0, steered = 0, step = 0;
	int have = 0;

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "clock: the master %lld ppb", &master) == 1)
			have |= 1;
		else if (sscanf(line, "clock: the steered clock %lld ppb",
				&steered) == 1)
			have |= 2;
		else if (sscanf(line, "clock: Audio 2's stream takes %lld ppb",
				&step) == 1)
			have |= 4;
	}
	fclose(f);
	if (!have)
		return;
	fprintf(out, "k ");
	if (have & 1)
		fprintf(out, "%lld ", master);
	else
		fprintf(out, "- ");
	if (have & 2)
		fprintf(out, "%lld ", steered);
	else
		fprintf(out, "- ");
	if (have & 4)
		fprintf(out, "%lld\n", step);
	else
		fprintf(out, "-\n");
}

// the interrupts the driver stamped since the last read
static void irq_lines(unsigned long long *last)
{
	FILE *f = fopen("/proc/asound/ES1869/es1869_irqs", "r");
	char line[128];

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		unsigned long long seq, raw;
		unsigned int src, ticks;
		int got = sscanf(line, "%llu %u %u %llu", &seq, &src, &ticks,
				 &raw);

		if (got != 4)
			continue;
		if (*last && seq <= *last)
			continue;
		if (*last && seq != *last + 1)
			fprintf(out, "w irqs %llu lost\n", seq - *last - 1);
		fprintf(out, "i %llu %u %u %llu\n", seq, src, ticks, raw);
		*last = seq;
	}
	fclose(f);
}

// chronyc's tracking, read as it comes, since the Celeron takes longer to
// run chronyc than a pilot's buffer lasts
static FILE *chrony;
static char chrony_buf[512];
static size_t chrony_len;

static void chrony_start(void)
{
	if (chrony)
		return;
	chrony = popen("chronyc -c tracking 2>/dev/null", "r");
	chrony_len = 0;
	if (chrony)
		fcntl(fileno(chrony), F_SETFL, O_NONBLOCK);
}

static void chrony_poll(void)
{
	ssize_t r;

	if (!chrony)
		return;
	r = read(fileno(chrony), chrony_buf + chrony_len,
		 sizeof(chrony_buf) - 1 - chrony_len);
	if (r > 0) {
		chrony_len += r;
		if (chrony_len < sizeof(chrony_buf) - 1)
			return;
	} else if (r < 0 && errno == EAGAIN) {
		return;
	}
	chrony_buf[chrony_len] = 0;
	chrony_buf[strcspn(chrony_buf, "\n")] = 0;
	if (chrony_len)
		fprintf(out, "t %s\n", chrony_buf);
	pclose(chrony);
	chrony = NULL;
}

// --- the run -----------------------------------------------------------------

static void usage(void)
{
	fprintf(stderr,
		"usage: es1869clock [-o LOG] [-t SECONDS] [-l DB] [-V LEVEL] [-I] [-e PLAY]\n"
		"                   [-a CAPTURE|none] [-A NAME] [-p PLAY|none] [-c CAPTURE] [-B NAME]\n");
}

// a capture that hears a pilot of f Hz, for the pair name
static int capture_init(struct stream *s, const char *name, double f)
{
	s->dir = SND_PCM_STREAM_CAPTURE;
	s->channels = 2;
	s->rate = 48000;
	s->period = 1200;
	s->pair = name;
	s->block_frames = (unsigned int)(BLOCK_S * s->rate);
	s->block = calloc((size_t)s->block_frames * s->channels, sizeof(short));
	ph_init(&s->ph, f, s->rate);
	return s->block ? 0 : -ENOMEM;
}

int main(int argc, char **argv)
{
	const char *log = "/var/tmp/es1869clock.log";
	struct stream es = { .name = "ES1869 pilot",
			     .dev = "hw:CARD=ES1869,DEV=0,SUBDEV=1",
			     .dir = SND_PCM_STREAM_PLAYBACK,
			     .channels = 2,
			     .rate = 44100,
			     .period = 1024 };
	struct stream ec = { .name = "ES1869 pilot capture",
			     .dev = "plughw:CARD=USB,DEV=0" };
	struct stream sp = { .name = "second pilot",
			     .dev = "plughw:CARD=USB,DEV=0",
			     .dir = SND_PCM_STREAM_PLAYBACK,
			     .channels = 4,
			     .rate = 48000,
			     .period = 1200 };
	struct stream sc = { .name = "second pilot capture",
			     .dev = "plughw:CARD=VXPocket,DEV=0" };
	const char *ename = "ef", *sname = "fv";
	struct stream *all[4];
	struct pollfd fds[MAX_FDS];
	struct itimerspec tick = { { 1, 0 }, { 1, 0 } };
	double level = -30, secs = 0;
	unsigned long long irq_last = 0;
	long long t_end = 0;
	int o, i, nfds, tfd, irqs = 1, first = 1, second = 1, ns, volume = -1;
	unsigned long seconds = 0;

	while ((o = getopt(argc, argv, "o:t:e:a:A:p:c:B:l:V:I")) != -1) {
		switch (o) {
		case 'o':
			log = optarg;
			break;
		case 't':
			secs = atof(optarg);
			break;
		case 'e':
			es.dev = optarg;
			break;
		case 'a':
			if (!strcmp(optarg, "none"))
				first = 0;
			else
				ec.dev = optarg;
			break;
		case 'A':
			ename = optarg;
			break;
		case 'p':
			if (!strcmp(optarg, "none"))
				second = 0;
			else
				sp.dev = optarg;
			break;
		case 'c':
			sc.dev = optarg;
			break;
		case 'B':
			sname = optarg;
			break;
		case 'l':
			level = atof(optarg);
			break;
		case 'I':
			irqs = 0;
			break;
		case 'V':
			volume = atoi(optarg);
			break;
		default:
			usage();
			return 1;
		}
	}
	out = fopen(log, "a");
	if (!out) {
		perror(log);
		return 2;
	}
	setvbuf(out, NULL, _IOFBF, 1 << 16);
	fprintf(out, "# es1869clock 1, raw %lld, real %lld\n",
		ns_of(CLOCK_MONOTONIC_RAW), ns_of(CLOCK_REALTIME));
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);

	// 19600 Hz is 4 cycles in 9 frames at 44100, 12000 Hz 1 in 4 at 48000,
	// the second on a four-channel player's last two
	all[0] = &es;
	ns = 1;
	if (make_table(&es, 9, 4, level, 3))
		return 2;
	if (first) {
		all[ns++] = &ec;
		if (capture_init(&ec, ename, 19600))
			return 2;
	}
	if (second) {
		all[ns++] = &sp;
		all[ns++] = &sc;
		if (make_table(&sp, 4, 1, -20, 12) ||
		    capture_init(&sc, sname, 12000))
			return 2;
	}
	if (volume >= 0 && a1_volume(volume) < 0)
		logw("Audio 1's volume: no control to set");
	for (i = 0; i < ns; i++)
		if (open_stream(all[i])) {
			a1_restore();
			return 3;
		}
	for (i = 0; i < ns; i++)
		if (all[i]->dir == SND_PCM_STREAM_PLAYBACK)
			feed(all[i]);
		else
			snd_pcm_start(all[i]->pcm);

	tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	if (tfd < 0 || timerfd_settime(tfd, 0, &tick, NULL)) {
		perror("timerfd");
		return 2;
	}
	nfds = 0;
	fds[nfds].fd = tfd;
	fds[nfds++].events = POLLIN;
	for (i = 0; i < ns; i++) {
		struct stream *s = all[i];
		int n = snd_pcm_poll_descriptors_count(s->pcm);

		if (n < 1 || nfds + n > MAX_FDS)
			return 2;
		s->fd0 = nfds;
		s->nfds = snd_pcm_poll_descriptors(s->pcm, fds + nfds, n);
		nfds += s->nfds;
	}
	if (secs > 0)
		t_end = ns_of(CLOCK_MONOTONIC) + (long long)(secs * 1e9);
	fflush(out);

	while (!stop) {
		if (poll(fds, nfds, 1000) < 0 && errno != EINTR)
			break;
		for (i = 0; i < ns; i++) {
			struct stream *s = all[i];
			struct pollfd *own = fds + s->fd0;
			unsigned short ev;
			int k;

			if (s->dead)
				continue;
			if (snd_pcm_poll_descriptors_revents(s->pcm, own,
							     s->nfds, &ev) < 0)
				continue;
			if (s->dir == SND_PCM_STREAM_PLAYBACK &&
			    (ev & (POLLOUT | POLLERR)))
				feed(s);
			else if (s->dir == SND_PCM_STREAM_CAPTURE &&
				 (ev & (POLLIN | POLLERR)))
				take(s);
			// a stream given up is out of the poll
			for (k = 0; s->dead && k < s->nfds; k++)
				own[k].fd = -1;
		}
		chrony_poll();
		if (fds[0].revents & POLLIN) {
			unsigned long long n;

			if (read(tfd, &n, sizeof(n)) < 0)
				n = 0;
			system_line();
			driver_line();
			if (irqs)
				irq_lines(&irq_last);
			if (seconds++ % 60 == 0)
				chrony_start();
			// a capture with no frames after five seconds hears nothing
			for (i = 0; seconds == 5 && i < ns; i++)
				if (all[i]->dir == SND_PCM_STREAM_CAPTURE &&
				    !all[i]->frames)
					logw("%s: no frames in five seconds",
					     all[i]->name);
			fflush(out);
			if (seconds % 60 == 0)
				fsync(fileno(out));
		}
		if (t_end && ns_of(CLOCK_MONOTONIC) >= t_end)
			break;
	}
	for (i = 0; i < ns; i++)
		snd_pcm_close(all[i]->pcm);
	a1_restore();
	fprintf(out, "# es1869clock ends, raw %lld\n",
		ns_of(CLOCK_MONOTONIC_RAW));
	fflush(out);
	fsync(fileno(out));
	fclose(out);
	return 0;
}
