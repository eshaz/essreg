// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869test's machine on the laptop: the streams through alsa-lib, one
 * thread each, the ES1869 card's controls, the module's parameters in
 * sysfs, the register hwdep and the driver's proc file.
 *
 * Notes:
 *
 * Every stream's thread opens and prepares its device and writes its
 * first buffer of playback, then two barriers give all of them the same
 * clock, so that each starts at its own time after the run's start. The
 * trigger time comes from the driver's timestamp, not from the thread.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#define _GNU_SOURCE // SCHED_IDLE

#include <alsa/asoundlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../es1869/uapi/es1869.h"
#include "hal.h"

#define CARD_ID "ES1869"
#define PARAMS "/sys/module/snd_es1869/parameters/"

struct alsa {
	int card;
	snd_ctl_t *ctl;
	char rec2[64];
	int regs_fd;
	double t0;
	pthread_barrier_t ready, go;
	snd_config_t *dmix_cfg;
};

struct worker {
	struct alsa *a;
	struct et_stream *s;
	pthread_t th;
};

static double mono(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void sleep_until(double t)
{
	struct timespec ts;

	if (t <= mono())
		return;
	ts.tv_sec = (time_t)t;
	ts.tv_nsec = (long)((t - ts.tv_sec) * 1e9);
	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) ==
	       EINTR)
		;
}

static snd_pcm_format_t alsa_format(enum et_format f)
{
	switch (f) {
	case ET_U8:
		return SND_PCM_FORMAT_U8;
	case ET_S8:
		return SND_PCM_FORMAT_S8;
	case ET_U16:
		return SND_PCM_FORMAT_U16_LE;
	default:
		return SND_PCM_FORMAT_S16_LE;
	}
}

static const char *role_dev(struct alsa *a, enum et_role r)
{
	switch (r) {
	case ET_A2:
		return "hw:CARD=" CARD_ID ",DEV=0,SUBDEV=0";
	case ET_A1P:
		return "hw:CARD=" CARD_ID ",DEV=0,SUBDEV=1";
	case ET_A1C:
		return "hw:CARD=" CARD_ID ",DEV=0";
	case ET_FMC:
		return "hw:CARD=" CARD_ID ",DEV=1";
	case ET_DUAL:
		return "hw:CARD=" CARD_ID ",DEV=2";
	case ET_MON:
		return "hw:CARD=" CARD_ID ",DEV=3";
	case ET_REC2:
		return a->rec2[0] ? a->rec2 : NULL;
	default:
		return NULL;
	}
}

// a dmix plugin over the stream's device, with its format as the slave's
static int open_dmix(struct alsa *a, snd_pcm_t **pcm, struct et_stream *s)
{
	char text[512];
	snd_input_t *in;
	snd_config_t *top;
	int err;

	snprintf(text, sizeof(text),
		 "pcm.et_dmix { type dmix ipc_key 186901 ipc_perm 0600 "
		 "slave { pcm \"%s\" format %s rate %u channels %u "
		 "period_size %lu buffer_size %lu } }",
		 role_dev(a, s->role),
		 snd_pcm_format_name(alsa_format(s->fmt.format)), s->fmt.rate,
		 s->fmt.channels, s->period ? s->period : 1024,
		 s->buffer ? s->buffer : 4096);
	err = snd_config_update();
	if (err < 0)
		return err;
	err = snd_config_copy(&top, snd_config);
	if (err < 0)
		return err;
	err = snd_input_buffer_open(&in, text, -1);
	if (err >= 0) {
		err = snd_config_load(top, in);
		snd_input_close(in);
	}
	if (err >= 0)
		err = snd_pcm_open_lconf(pcm, "et_dmix", SND_PCM_STREAM_PLAYBACK,
					 0, top);
	snd_config_delete(top);
	return err;
}

static int setup(struct et_stream *s, snd_pcm_t *pcm)
{
	snd_pcm_hw_params_t *hw;
	snd_pcm_sw_params_t *sw;
	snd_pcm_uframes_t period, buffer;
	unsigned int rate = s->fmt.rate;
	int err;

	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_sw_params_alloca(&sw);
	err = snd_pcm_hw_params_any(pcm, hw);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_set_access(pcm, hw,
					   s->flags & ET_MMAP ?
						   SND_PCM_ACCESS_MMAP_INTERLEAVED :
						   SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err >= 0)
		err = snd_pcm_hw_params_set_format(pcm, hw,
						   alsa_format(s->fmt.format));
	if (err >= 0)
		err = snd_pcm_hw_params_set_channels(pcm, hw, s->fmt.channels);
	if (err >= 0)
		err = snd_pcm_hw_params_set_rate(pcm, hw, rate, 0);
	if (err < 0)
		return err;
	if (s->period) {
		period = s->period;
		err = snd_pcm_hw_params_set_period_size_near(pcm, hw, &period,
							     NULL);
		if (err < 0)
			return err;
	}
	if (s->buffer) {
		buffer = s->buffer;
		err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);
		if (err < 0)
			return err;
	}
	err = snd_pcm_hw_params(pcm, hw);
	if (err < 0)
		return err;
	snd_pcm_hw_params_get_period_size(hw, &period, NULL);
	snd_pcm_hw_params_get_buffer_size(hw, &buffer);
	s->period_got = period;
	s->buffer_got = buffer;

	snd_pcm_sw_params_current(pcm, sw);
	snd_pcm_sw_params_set_tstamp_mode(pcm, sw, SND_PCM_TSTAMP_ENABLE);
	snd_pcm_sw_params_set_tstamp_type(pcm, sw,
					  SND_PCM_TSTAMP_TYPE_MONOTONIC);
	snd_pcm_sw_params_set_avail_min(pcm, sw, period);
	// started by the thread, or by the first write
	snd_pcm_sw_params_set_start_threshold(pcm, sw,
					      s->flags & ET_NOSTART ? 1 :
								      buffer * 2);
	// no underrun, and the core's silence behind the DMA
	if (s->flags & ET_RUNOUT) {
		snd_pcm_uframes_t boundary;

		snd_pcm_sw_params_get_boundary(sw, &boundary);
		snd_pcm_sw_params_set_stop_threshold(pcm, sw, boundary);
		snd_pcm_sw_params_set_silence_threshold(pcm, sw, 0);
		snd_pcm_sw_params_set_silence_size(pcm, sw, boundary);
	}
	err = snd_pcm_sw_params(pcm, sw);
	if (err < 0)
		return err;
	return snd_pcm_prepare(pcm);
}

// a write as JACK makes it: poll, then the room avail_update gives, in the
// mapped buffer, so that nothing but the interrupts reads the pointer
static snd_pcm_sframes_t polled(struct et_stream *s, snd_pcm_t *pcm,
				unsigned long frames)
{
	unsigned int fb = et_fmt_bytes(&s->fmt);
	const snd_pcm_channel_area_t *areas;
	snd_pcm_uframes_t off, n = frames;
	snd_pcm_sframes_t avail;
	int err;

	if (snd_pcm_state(pcm) == SND_PCM_STATE_RUNNING) {
		err = snd_pcm_wait(pcm, 1000);
		if (err < 0)
			return err;
	}
	avail = snd_pcm_avail_update(pcm);
	if (avail < 0)
		return avail;
	if ((snd_pcm_uframes_t)avail < n)
		n = avail;
	if (!n)
		return -EAGAIN;
	err = snd_pcm_mmap_begin(pcm, &areas, &off, &n);
	if (err < 0)
		return err;
	memcpy((char *)areas[0].addr + off * fb,
	       (char *)s->data + s->done * fb, n * fb);
	return snd_pcm_mmap_commit(pcm, off, n);
}

static snd_pcm_sframes_t xfer(struct et_stream *s, snd_pcm_t *pcm,
			      unsigned long frames)
{
	unsigned int fb = et_fmt_bytes(&s->fmt);
	char *p = (char *)s->data + s->done * fb;

	if (s->flags & ET_POLLED)
		return polled(s, pcm, frames);
	// the monitor gives frames only while Audio 2 plays, and its reads
	// wait for as long: a wait here that the stream's end bounds
	if (s->role == ET_MON) {
		int w = snd_pcm_wait(pcm, 100);

		if (w <= 0)
			return w ? w : -EAGAIN;
	}
	if (s->capture)
		return s->flags & ET_MMAP ? snd_pcm_mmap_readi(pcm, p, frames) :
					    snd_pcm_readi(pcm, p, frames);
	return s->flags & ET_MMAP ? snd_pcm_mmap_writei(pcm, p, frames) :
				    snd_pcm_writei(pcm, p, frames);
}

static double trigger_time(struct alsa *a, snd_pcm_t *pcm)
{
	snd_pcm_status_t *st;
	snd_htimestamp_t ts;

	snd_pcm_status_alloca(&st);
	if (snd_pcm_status(pcm, st) < 0)
		return mono() - a->t0;
	snd_pcm_status_get_trigger_htstamp(st, &ts);
	if (!ts.tv_sec && !ts.tv_nsec)
		return mono() - a->t0;
	return ts.tv_sec + ts.tv_nsec * 1e-9 - a->t0;
}

// where a running stream is, and when: what was written less what is still
// to play, or what was read and what waits to be, at the time of that
// reading of the pointer
static void stamp(struct et_stream *s, snd_pcm_t *pcm)
{
	snd_pcm_status_t *st;
	snd_htimestamp_t ts;
	struct timespec raw, now;

	snd_pcm_status_alloca(&st);
	if (snd_pcm_status(pcm, st) < 0 ||
	    snd_pcm_status_get_state(st) != SND_PCM_STATE_RUNNING)
		return;
	snd_pcm_status_get_htstamp(st, &ts);
	s->stamps[s->stamped].t = ts.tv_sec + ts.tv_nsec * 1e-9;
	clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
	clock_gettime(CLOCK_MONOTONIC, &now);
	s->stamps[s->stamped].raw = s->stamps[s->stamped].t +
		(raw.tv_sec + raw.tv_nsec * 1e-9) -
		(now.tv_sec + now.tv_nsec * 1e-9);
	s->stamps[s->stamped].frame =
		s->capture ? (double)s->done + snd_pcm_status_get_delay(st) :
			     (double)s->done - snd_pcm_status_get_delay(st);
	s->stamped++;
}

static void stream_body(struct alsa *a, struct et_stream *s, snd_pcm_t *pcm)
{
	double end = a->t0 + s->start +
		     (s->timeout ? s->timeout :
				   (double)s->frames / s->fmt.rate + 5);
	unsigned long pause_frame = s->pause_at > 0 ?
					    (unsigned long)(s->pause_at *
							    s->fmt.rate) :
					    ~0UL;
	snd_pcm_sframes_t n;
	snd_pcm_sframes_t d;
	int restart = 0;

	sleep_until(a->t0 + s->start);
	if (!(s->flags & ET_NOSTART) || s->capture) {
		s->err = snd_pcm_start(pcm);
		if (s->err < 0)
			return;
		s->t_trigger = trigger_time(a, pcm);
		if (snd_pcm_delay(pcm, &d) >= 0)
			s->delay0 = d;
	}
	while (s->done < s->frames && mono() < end) {
		unsigned long want = s->frames - s->done;

		if (s->done >= pause_frame && (s->flags & ET_STALL)) {
			pause_frame = ~0UL;
			sleep_until(mono() + s->pause_for);
			end += s->pause_for;
		} else if (s->done >= pause_frame && (s->flags & ET_RESTART)) {
			// the same open stream again, as a program that stops
			// and starts a recording without closing it
			pause_frame = ~0UL;
			snd_pcm_drop(pcm);
			sleep_until(mono() + s->pause_for);
			s->err = snd_pcm_prepare(pcm);
			if (s->err >= 0)
				s->err = snd_pcm_start(pcm);
			if (s->err < 0)
				return;
			end += s->pause_for;
		} else if (s->done >= pause_frame) {
			pause_frame = ~0UL;
			s->err = snd_pcm_pause(pcm, 1);
			if (s->err < 0)
				return;
			sleep_until(mono() + s->pause_for);
			s->err = snd_pcm_pause(pcm, 0);
			if (s->err < 0)
				return;
		}
		if (want > s->period_got)
			want = s->period_got;
		if (pause_frame != ~0UL && s->done + want > pause_frame)
			want = pause_frame - s->done;
		if (!want)
			continue;
		if (s->probe_at > 0 && !s->probe_t &&
		    mono() >= a->t0 + s->start + s->probe_at) {
			snd_pcm_status_t *st;
			snd_htimestamp_t ts;

			snd_pcm_status_alloca(&st);
			if (snd_pcm_status(pcm, st) >= 0) {
				snd_pcm_status_get_htstamp(st, &ts);
				s->probe_t = ts.tv_sec + ts.tv_nsec * 1e-9 - a->t0;
				s->probe_delay = snd_pcm_status_get_delay(st);
				s->probe_avail = snd_pcm_status_get_avail(st);
				s->probe_done = s->done;
			}
		}
		n = xfer(s, pcm, want);
		if (n == -EAGAIN)
			continue;
		if (n == -EPIPE || n == -ESTRPIPE) {
			s->xruns++;
			if (snd_pcm_recover(pcm, (int)n, 1) < 0) {
				s->err = (int)n;
				return;
			}
			if (s->capture)
				snd_pcm_start(pcm);
			restart = !s->capture;
			continue;
		}
		if (n < 0) {
			s->err = (int)n;
			return;
		}
		s->done += n;
		if (s->stamped < s->max_stamps)
			stamp(s, pcm);
		// a playback that an xrun stopped goes on with its next write
		if (restart && snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED) {
			restart = 0;
			snd_pcm_start(pcm);
		}
		// a stream that its first write started
		if (!s->t_trigger && snd_pcm_state(pcm) == SND_PCM_STATE_RUNNING) {
			s->t_trigger = trigger_time(a, pcm);
			if (snd_pcm_delay(pcm, &d) >= 0)
				s->delay0 = d;
		}
	}
	// playback: what is left in the buffer plays out
	if (!s->capture && s->done == s->frames) {
		if (snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED)
			snd_pcm_start(pcm);
		if (s->flags & ET_RUNOUT) {
			usleep((useconds_t)(1e6 * ((double)s->buffer_got /
						   s->fmt.rate + 0.2)));
			snd_pcm_drop(pcm);
		} else {
			// the drain releases the pause itself
			if (s->flags & ET_PAUSEDRAIN) {
				snd_pcm_pause(pcm, 1);
				usleep(200000);
			}
			snd_pcm_drain(pcm);
		}
	}
	s->t_end = mono() - a->t0;
	if (s->done < s->frames && !s->err)
		s->err = -ETIMEDOUT;
}

static void *stream_thread(void *arg)
{
	struct worker *w = arg;
	struct alsa *a = w->a;
	struct et_stream *s = w->s;
	snd_pcm_t *pcm = NULL;
	const char *dev = role_dev(a, s->role);
	int err;

	if (s->flags & ET_DMIX)
		err = open_dmix(a, &pcm, s);
	else if (!dev)
		err = -ENODEV;
	else
		err = snd_pcm_open(&pcm, dev,
				   s->capture ? SND_PCM_STREAM_CAPTURE :
						SND_PCM_STREAM_PLAYBACK,
				   s->role == ET_MON ? SND_PCM_NONBLOCK : 0);
	if (err >= 0)
		err = setup(s, pcm);
	// the first buffer of playback, so that the start has data
	if (err >= 0 && !s->capture && !(s->flags & ET_NOSTART)) {
		unsigned long first = s->frames < s->buffer_got ?
					      s->frames :
					      s->buffer_got;
		snd_pcm_sframes_t n = xfer(s, pcm, first);

		if (n < 0)
			err = (int)n;
		else
			s->done = n;
	}
	s->err = err < 0 ? err : 0;
	pthread_barrier_wait(&a->ready);
	pthread_barrier_wait(&a->go);
	if (!s->err)
		stream_body(a, s, pcm);
	if (pcm)
		snd_pcm_close(pcm);
	return NULL;
}

static void *cmd_thread(void *arg)
{
	struct worker *w = arg;
	struct alsa *a = w->a;
	struct et_stream *s = w->s;
	double end;
	pid_t pid;
	int st;

	pthread_barrier_wait(&a->ready);
	pthread_barrier_wait(&a->go);
	sleep_until(a->t0 + s->start);
	s->t_trigger = mono() - a->t0;
	pid = fork();
	if (pid < 0) {
		s->err = -errno;
		return NULL;
	}
	if (!pid) {
		int fd = open("/dev/null", O_RDWR);

		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		execl("/bin/sh", "sh", "-c", s->cmd, (char *)NULL);
		_exit(127);
	}
	end = mono() + (s->timeout ? s->timeout : 60);
	for (;;) {
		pid_t r = waitpid(pid, &st, WNOHANG);

		if (r == pid)
			break;
		if (mono() > end) {
			kill(pid, SIGTERM);
			waitpid(pid, &st, 0);
			s->err = -ETIMEDOUT;
			break;
		}
		sleep_until(mono() + 0.02);
	}
	s->t_end = mono() - a->t0;
	s->status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
	return NULL;
}

static int alsa_ctl_set(void *ctx, const char *name, int index, const long *v,
			int n);

// a control written at its time, as alsamixer writes it
static void *ctl_thread(void *arg)
{
	struct worker *w = arg;
	struct alsa *a = w->a;
	struct et_stream *s = w->s;

	pthread_barrier_wait(&a->ready);
	pthread_barrier_wait(&a->go);
	sleep_until(a->t0 + s->start);
	s->t_trigger = mono() - a->t0;
	s->err = alsa_ctl_set(a, s->cmd, 0, &s->value, 1);
	if (s->err > 0)
		s->err = 0;
	s->t_end = mono() - a->t0;
	return NULL;
}

static int alsa_run(void *ctx, struct et_stream *s, int n)
{
	struct alsa *a = ctx;
	struct worker *w = calloc(n, sizeof(*w));
	int i, err = 0;

	if (!w)
		return -ENOMEM;
	pthread_barrier_init(&a->ready, NULL, n + 1);
	pthread_barrier_init(&a->go, NULL, n + 1);
	for (i = 0; i < n; i++) {
		w[i].a = a;
		w[i].s = &s[i];
		s[i].err = 0;
		s[i].xruns = 0;
		s[i].done = 0;
		s[i].t_trigger = s[i].t_end = 0;
		s[i].stamped = 0;
		if (pthread_create(&w[i].th, NULL,
				   s[i].role == ET_CMD ? cmd_thread :
				   s[i].role == ET_CTL ? ctl_thread :
							 stream_thread,
				   &w[i])) {
			// no thread: the barriers can't be met, so stop here
			fprintf(stderr, "es1869test: no thread\n");
			exit(5);
		}
	}
	pthread_barrier_wait(&a->ready);
	a->t0 = mono() + 0.02;
	pthread_barrier_wait(&a->go);
	for (i = 0; i < n; i++)
		pthread_join(w[i].th, NULL);
	pthread_barrier_destroy(&a->ready);
	pthread_barrier_destroy(&a->go);
	free(w);
	for (i = 0; i < n && !err; i++)
		err = s[i].err;
	return err;
}

static int alsa_has(void *ctx, enum et_role r)
{
	struct alsa *a = ctx;
	snd_pcm_info_t *info;

	if (r == ET_CMD || r == ET_CTL)
		return 1;
	if (r == ET_REC2)
		return a->rec2[0] != 0;
	if (!a->ctl)
		return 0;
	snd_pcm_info_alloca(&info);
	snd_pcm_info_set_device(info, r == ET_FMC  ? 1 :
				      r == ET_DUAL ? 2 :
				      r == ET_MON  ? 3 :
						     0);
	snd_pcm_info_set_subdevice(info, r == ET_A1P ? 1 : 0);
	snd_pcm_info_set_stream(info,
				r == ET_A1C || r == ET_FMC || r == ET_MON ?
					SND_PCM_STREAM_CAPTURE :
					SND_PCM_STREAM_PLAYBACK);
	return snd_ctl_pcm_info(a->ctl, info) >= 0;
}

static const char *alsa_rec2(void *ctx)
{
	struct alsa *a = ctx;

	return a->rec2[0] ? a->rec2 : NULL;
}

static int elem(struct alsa *a, const char *name, int index,
		snd_ctl_elem_id_t *id, snd_ctl_elem_info_t *info)
{
	if (!a->ctl)
		return -ENODEV;
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, name);
	snd_ctl_elem_id_set_index(id, index);
	snd_ctl_elem_info_set_id(info, id);
	return snd_ctl_elem_info(a->ctl, info);
}

static int alsa_ctl_info(void *ctx, const char *name, int index,
			 struct et_ctl_info *out)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	int err;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	err = elem(ctx, name, index, id, info);
	if (err < 0)
		return err;
	memset(out, 0, sizeof(*out));
	out->count = snd_ctl_elem_info_get_count(info);
	switch (snd_ctl_elem_info_get_type(info)) {
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		out->type = ET_CTL_BOOL;
		out->max = 1;
		break;
	case SND_CTL_ELEM_TYPE_INTEGER:
		out->type = ET_CTL_INT;
		out->min = snd_ctl_elem_info_get_min(info);
		out->max = snd_ctl_elem_info_get_max(info);
		break;
	case SND_CTL_ELEM_TYPE_ENUMERATED:
		out->type = ET_CTL_ENUM;
		out->items = snd_ctl_elem_info_get_items(info);
		out->max = out->items - 1;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int alsa_ctl_get(void *ctx, const char *name, int index, long *v,
			int n)
{
	struct alsa *a = ctx;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_type_t type;
	int err, i, count;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	err = elem(a, name, index, id, info);
	if (err < 0)
		return err;
	type = snd_ctl_elem_info_get_type(info);
	count = snd_ctl_elem_info_get_count(info);
	snd_ctl_elem_value_set_id(val, id);
	err = snd_ctl_elem_read(a->ctl, val);
	if (err < 0)
		return err;
	for (i = 0; i < count && i < n; i++)
		v[i] = type == SND_CTL_ELEM_TYPE_BOOLEAN ?
			       snd_ctl_elem_value_get_boolean(val, i) :
		       type == SND_CTL_ELEM_TYPE_ENUMERATED ?
			       (long)snd_ctl_elem_value_get_enumerated(val, i) :
			       snd_ctl_elem_value_get_integer(val, i);
	return i;
}

static int alsa_ctl_set(void *ctx, const char *name, int index, const long *v,
			int n)
{
	struct alsa *a = ctx;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_type_t type;
	int err, i, count;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	err = elem(a, name, index, id, info);
	if (err < 0)
		return err;
	type = snd_ctl_elem_info_get_type(info);
	count = snd_ctl_elem_info_get_count(info);
	snd_ctl_elem_value_set_id(val, id);
	for (i = 0; i < count; i++) {
		long x = v[i < n ? i : n - 1];

		if (type == SND_CTL_ELEM_TYPE_BOOLEAN)
			snd_ctl_elem_value_set_boolean(val, i, x);
		else if (type == SND_CTL_ELEM_TYPE_ENUMERATED)
			snd_ctl_elem_value_set_enumerated(val, i, x);
		else
			snd_ctl_elem_value_set_integer(val, i, x);
	}
	err = snd_ctl_elem_write(a->ctl, val);
	return err < 0 ? err : count;
}

static int alsa_ctl_enum_name(void *ctx, const char *name, int index,
			      int item, char *buf, int len)
{
	struct alsa *a = ctx;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	int err;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	err = elem(a, name, index, id, info);
	if (err < 0)
		return err;
	snd_ctl_elem_info_set_item(info, item);
	err = snd_ctl_elem_info(a->ctl, info);
	if (err < 0)
		return err;
	snprintf(buf, len, "%s", snd_ctl_elem_info_get_item_name(info));
	return 0;
}

static int alsa_ctl_db(void *ctx, const char *name, int index, long value,
		       long *cdb)
{
	struct alsa *a = ctx;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	int err;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	err = elem(a, name, index, id, info);
	if (err < 0)
		return err;
	snd_ctl_elem_info_get_id(info, id);
	return snd_ctl_convert_to_dB(a->ctl, id, value, cdb);
}

// as alsamixer asks: an enum that isn't a capture enum, a playback switch or a
// playback volume is in its Playback view, and a capture enum, a capture
// switch or a capture volume in its Capture view
static int alsa_selem(void *ctx, const char *name, int *views)
{
	snd_mixer_t *m;
	snd_mixer_elem_t *e;
	int err;

	(void)ctx;
	err = snd_mixer_open(&m, 0);
	if (err < 0)
		return err;
	err = snd_mixer_attach(m, "hw:CARD=" CARD_ID);
	if (err >= 0)
		err = snd_mixer_selem_register(m, NULL, NULL);
	if (err >= 0)
		err = snd_mixer_load(m);
	if (err >= 0)
		err = -ENOENT;
	for (e = err == -ENOENT ? snd_mixer_first_elem(m) : NULL; e;
	     e = snd_mixer_elem_next(e)) {
		if (strcmp(snd_mixer_selem_get_name(e), name))
			continue;
		if (snd_mixer_selem_is_enumerated(e))
			*views = snd_mixer_selem_is_enum_capture(e) ?
					 ET_VIEW_CAPTURE :
					 ET_VIEW_PLAYBACK;
		else
			*views =
				(snd_mixer_selem_has_playback_switch(e) ||
						 snd_mixer_selem_has_playback_volume(
							 e) ?
					 ET_VIEW_PLAYBACK :
					 0) |
				(snd_mixer_selem_has_capture_switch(e) ||
						 snd_mixer_selem_has_capture_volume(
							 e) ?
					 ET_VIEW_CAPTURE :
					 0);
		err = 0;
		break;
	}
	snd_mixer_close(m);
	return err;
}

static int alsa_ctl_nth(void *ctx, int nth, char *name, int len, int *index)
{
	struct alsa *a = ctx;
	snd_ctl_elem_list_t *list;
	int err, count;

	if (!a->ctl)
		return -ENODEV;
	snd_ctl_elem_list_alloca(&list);
	err = snd_ctl_elem_list(a->ctl, list);
	if (err < 0)
		return err;
	count = snd_ctl_elem_list_get_count(list);
	if (nth >= count)
		return -ENOENT;
	err = snd_ctl_elem_list_alloc_space(list, count);
	if (err < 0)
		return err;
	err = snd_ctl_elem_list(a->ctl, list);
	if (err >= 0) {
		snprintf(name, len, "%s", snd_ctl_elem_list_get_name(list, nth));
		*index = snd_ctl_elem_list_get_index(list, nth);
	}
	snd_ctl_elem_list_free_space(list);
	return err < 0 ? err : 0;
}

static int alsa_param_get(void *ctx, const char *id, char *buf, int len)
{
	char path[128];
	int fd, n;

	(void)ctx;
	snprintf(path, sizeof(path), PARAMS "%s", id);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;
	n = read(fd, buf, len - 1);
	close(fd);
	if (n < 0)
		return -errno;
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
		n--;
	buf[n] = 0;
	return n;
}

static int alsa_param_set(void *ctx, const char *id, const char *val)
{
	char path[128];
	int fd, n, err = 0;

	(void)ctx;
	snprintf(path, sizeof(path), PARAMS "%s", id);
	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -errno;
	n = write(fd, val, strlen(val));
	if (n < 0)
		err = -errno;
	close(fd);
	return err;
}

static int alsa_reg_read(void *ctx, int bank, int reg, int *val)
{
	struct alsa *a = ctx;
	struct es1869_reg r;

	if (a->regs_fd < 0)
		return -ENODEV;
	memset(&r, 0, sizeof(r));
	r.bank = bank;
	r.reg = reg;
	if (ioctl(a->regs_fd, ES1869_IOCTL_READ, &r) < 0)
		return -errno;
	*val = r.value;
	return 0;
}

static int alsa_info(void *ctx, uint32_t *hash, uint32_t *irqs)
{
	struct alsa *a = ctx;
	struct es1869_regs_info info;
	int i;

	if (a->regs_fd < 0)
		return -ENODEV;
	if (ioctl(a->regs_fd, ES1869_IOCTL_INFO, &info) < 0)
		return -errno;
	*hash = info.settings_hash;
	for (i = 0; irqs && i < 4; i++)
		irqs[i] = info.irqs[i];
	return 0;
}

static int alsa_proc(void *ctx, char *buf, int len)
{
	struct alsa *a = ctx;
	char path[64];
	int fd, n, got = 0;

	snprintf(path, sizeof(path), "/proc/asound/card%d/es1869", a->card);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;
	while (got < len - 1 && (n = read(fd, buf + got, len - 1 - got)) > 0)
		got += n;
	close(fd);
	buf[got] = 0;
	return got;
}

static int alsa_cpu(void *ctx, double *busy, double *total)
{
	unsigned long long v[8] = { 0 };
	FILE *f = fopen("/proc/stat", "r");
	long hz = sysconf(_SC_CLK_TCK);
	int i, n;

	(void)ctx;
	if (!f)
		return -errno;
	n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0],
		   &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
	fclose(f);
	if (n < 5)
		return -EIO;
	*total = 0;
	for (i = 0; i < n; i++)
		*total += v[i];
	// idle and iowait aren't busy
	*busy = (*total - v[3] - v[4]) / hz;
	*total /= hz;
	return 0;
}

// the watch: one thread counts in what idle time it gets, and one wakes from
// a timer at a real-time priority every millisecond
static struct {
	pthread_t spin, timer;
	volatile int stop;
	volatile unsigned long long count;
	double t0, late;
	int timed;
	unsigned long long slot[4096];
	int slots;
} watching;

static void *spin_thread(void *arg)
{
	struct sched_param p = { 0 };

	(void)arg;
	sched_setscheduler(0, SCHED_IDLE, &p);
	while (!watching.stop)
		watching.count++;
	return NULL;
}

static void *timer_thread(void *arg)
{
	struct sched_param p = { .sched_priority = 40 };
	struct timespec next;
	int n = 0;

	(void)arg;
	watching.timed = !sched_setscheduler(0, SCHED_FIFO, &p);
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (!watching.stop) {
		double late;

		next.tv_nsec += 1000000;
		if (next.tv_nsec >= 1000000000) {
			next.tv_nsec -= 1000000000;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		late = mono() - (next.tv_sec + next.tv_nsec * 1e-9);
		if (++n > 20 && late > watching.late)
			watching.late = late;
		if (!(n % 250) && watching.slots < 4096)
			watching.slot[watching.slots++] = watching.count;
	}
	return NULL;
}

static int alsa_watch(void *ctx, int on, double *spins, double *late_us,
		      double *trace, int max)
{
	int i;

	(void)ctx;
	if (on) {
		memset(&watching, 0, sizeof(watching));
		watching.t0 = mono();
		if (pthread_create(&watching.spin, NULL, spin_thread, NULL))
			return -EAGAIN;
		if (pthread_create(&watching.timer, NULL, timer_thread, NULL)) {
			watching.stop = 1;
			pthread_join(watching.spin, NULL);
			return -EAGAIN;
		}
		return 0;
	}
	*spins = watching.count / (mono() - watching.t0);
	watching.stop = 1;
	pthread_join(watching.spin, NULL);
	pthread_join(watching.timer, NULL);
	*late_us = watching.timed ? watching.late * 1e6 : -1;
	for (i = 0; i < max; i++)
		trace[i] = i < watching.slots && i ?
				   (double)(watching.slot[i] - watching.slot[i - 1]) :
				   0;
	return 0;
}

static double alsa_cpu_hz(void *ctx)
{
	char line[128];
	FILE *f = fopen("/proc/cpuinfo", "r");
	double mhz = 0;

	(void)ctx;
	while (f && fgets(line, sizeof(line), f))
		if (sscanf(line, "cpu MHz : %lf", &mhz) == 1)
			break;
	if (f)
		fclose(f);
	return mhz * 1e6;
}

static double alsa_now(void *ctx)
{
	(void)ctx;
	return mono();
}

static void alsa_sleep(void *ctx, double s)
{
	(void)ctx;
	sleep_until(mono() + s);
}

// the register hwdep, by its name in /proc/asound/hwdep
static int open_regs(int card)
{
	char line[128], path[32];
	FILE *f = fopen("/proc/asound/hwdep", "r");
	int c, d, fd = -1;

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%d-%d:", &c, &d) == 2 && c == card &&
		    strstr(line, "ES1869 reg")) {
			snprintf(path, sizeof(path), "/dev/snd/hwC%dD%d", c, d);
			fd = open(path, O_RDONLY);
			break;
		}
	fclose(f);
	return fd;
}

// the first other card with a capture device, unless named
static void find_rec2(struct alsa *a, const char *want)
{
	snd_pcm_info_t *info;
	int card = -1;

	if (want) {
		snprintf(a->rec2, sizeof(a->rec2), "%s", want);
		return;
	}
	snd_pcm_info_alloca(&info);
	while (snd_card_next(&card) >= 0 && card >= 0) {
		char name[16], *id;
		snd_ctl_t *ctl;

		if (card == a->card)
			continue;
		snprintf(name, sizeof(name), "hw:%d", card);
		if (snd_ctl_open(&ctl, name, 0) < 0)
			continue;
		snd_pcm_info_set_device(info, 0);
		snd_pcm_info_set_subdevice(info, 0);
		snd_pcm_info_set_stream(info, SND_PCM_STREAM_CAPTURE);
		if (snd_ctl_pcm_info(ctl, info) >= 0 &&
		    snd_card_get_name(card, &id) >= 0) {
			snd_ctl_card_info_t *ci;

			snd_ctl_card_info_alloca(&ci);
			snd_ctl_card_info(ctl, ci);
			snprintf(a->rec2, sizeof(a->rec2), "hw:CARD=%s,DEV=0",
				 snd_ctl_card_info_get_id(ci));
			free(id);
			snd_ctl_close(ctl);
			return;
		}
		snd_ctl_close(ctl);
	}
}

// --- the machine: the buttons, a suspend, a reload, the kernel's log ---------

static const char *reload_cmd;
static int kmsg_fd = -1;

void et_hal_alsa_reload_cmd(const char *cmd)
{
	reload_cmd = cmd;
}

static void alsa_ask_press(void *ctx, int button)
{
	static const char *const names[] = {
		"Volume Up", "Volume Down",
		"Mute, or Volume Up and Down together",
		"the suspend button, wait for it to sleep, then wake it",
	};

	(void)ctx;
	printf("\a  >>> press %s on the machine, once\n", names[button]);
	fflush(stdout);
}

static int put(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY), err = 0;

	if (fd < 0)
		return -errno;
	if (write(fd, val, strlen(val)) < 0)
		err = -errno;
	if (close(fd) < 0 && !err)
		err = -errno;
	return err;
}

// the devices suspended, held for pm_test's 5 s and resumed, without the
// platform's sleep: the drivers' suspend and resume, and nothing that
// needs the BIOS
int et_pc_tone(unsigned int hz, unsigned int ms)
{
	int fd = open("/dev/tty0", O_WRONLY), err;

	if (fd < 0)
		fd = open("/dev/console", O_WRONLY);
	if (fd < 0)
		return -1;
	err = ioctl(fd, KDMKTONE, (ms << 16) | (1193180 / hz));
	close(fd);
	usleep(ms * 1000);
	return err;
}

int et_pm_test_suspend(void)
{
	int err;

	if (access("/sys/power/pm_test", W_OK))
		return errno == ENOENT ? -ENOTSUP : -errno;
	err = put("/sys/power/pm_test", "devices");
	if (!err)
		err = put("/sys/power/state", "freeze");
	put("/sys/power/pm_test", "none");
	return err;
}

static int alsa_suspend(void *ctx)
{
	(void)ctx;
	return et_pm_test_suspend();
}

// the card's control and register hwdep again, within secs
static int find_card(struct alsa *a, double secs)
{
	double end = mono() + secs;
	char name[32];

	while (mono() < end) {
		a->card = snd_card_get_index(CARD_ID);
		if (a->card >= 0) {
			snprintf(name, sizeof(name), "hw:%d", a->card);
			if (snd_ctl_open(&a->ctl, name, 0) >= 0) {
				a->regs_fd = open_regs(a->card);
				return 0;
			}
		}
		sleep_until(mono() + 0.2);
	}
	return -ENODEV;
}

static int alsa_reload(void *ctx, const char *params)
{
	struct alsa *a = ctx;
	char cmd[700];
	int st, err = 0;

	if (a->ctl)
		snd_ctl_close(a->ctl);
	a->ctl = NULL;
	if (a->regs_fd >= 0)
		close(a->regs_fd);
	a->regs_fd = -1;
	// the synth module goes and comes back with the driver
	if (reload_cmd)
		snprintf(cmd, sizeof(cmd), "%s %s", reload_cmd, params);
	else
		snprintf(cmd, sizeof(cmd),
			 "esfm=0; grep -q '^snd_es1869_esfm ' /proc/modules && "
			 "{ esfm=1; rmmod snd_es1869_esfm || exit 1; }; "
			 "rmmod snd_es1869 && modprobe snd-es1869 %s && "
			 "{ [ $esfm = 0 ] || modprobe snd-es1869-esfm; }",
			 params);
	st = system(cmd);
	if (st == -1 || !WIFEXITED(st) || WEXITSTATUS(st))
		err = -EIO;
	// udev's alsactl restore of the new card, before the suite goes on
	st = system("udevadm settle -t 10 >/dev/null 2>&1");
	(void)st;
	st = find_card(a, 10);
	return err ? err : st;
}

int et_klog_fault(const char *msg)
{
	static const char *const faults[] = {
		"WARNING:", "BUG:", "Oops", "general protection",
		"nobody cared", "blocked for more than", "Call Trace",
	};
	unsigned int i;

	for (i = 0; i < sizeof(faults) / sizeof(faults[0]); i++)
		if (strstr(msg, faults[i]))
			return 1;
	return 0;
}

// /dev/kmsg's records since the last call, each "level message"
static int alsa_klog(void *ctx, char *buf, int len)
{
	char rec[1024];
	int n = 0, at = 0;

	(void)ctx;
	if (kmsg_fd < 0) {
		kmsg_fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
		if (kmsg_fd < 0)
			return -errno;
		lseek(kmsg_fd, 0, SEEK_END);
	}
	if (len)
		buf[0] = 0;
	for (;;) {
		ssize_t got = read(kmsg_fd, rec, sizeof(rec) - 1);
		char *msg, *nl;
		int pri;

		if (got < 0 && errno == EPIPE)
			continue;
		if (got <= 0)
			break;
		rec[got] = 0;
		msg = strchr(rec, ';');
		if (!msg || sscanf(rec, "%d", &pri) != 1)
			continue;
		msg++;
		nl = strchr(msg, '\n');
		if (nl)
			*nl = 0;
		if (!strstr(msg, "es1869") && !et_klog_fault(msg))
			continue;
		if (at < len)
			at += snprintf(buf + at, len - at, "%d %.300s\n",
				       pri & 7, msg);
		n++;
	}
	return n;
}

// the card's capture channel: the first of the driver's dma1, or -1
static int capture_dma(void)
{
	static const char *const files[] = {
		"/sys/module/snd_es1869/parameters/dma1",
		"/sys/module/snd_es18xx/parameters/dma1",
	};
	unsigned int i;
	int ch = -1;

	for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		FILE *f = fopen(files[i], "r");

		if (!f)
			continue;
		if (fscanf(f, "%d", &ch) != 1)
			ch = -1;
		fclose(f);
		if (ch >= 0)
			break;
	}
	return ch;
}

static unsigned int sysfs_hex(const char *dir, const char *file)
{
	char path[320];
	unsigned int v = 0;
	FILE *f;

	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%.64s/%s", dir,
		 file);
	f = fopen(path, "r");
	if (!f)
		return 0;
	if (fscanf(f, "%x", &v) != 1)
		v = 0;
	fclose(f);
	return v;
}

// the PIIX4 ISA bridge's 76h and 77h: bit 7 Type F, bits 2:0 its channel
static int alsa_type_f(void *ctx)
{
	DIR *d = opendir("/sys/bus/pci/devices");
	struct dirent *e;
	int ch = capture_dma(), found = 0, err = 0;

	(void)ctx;
	if (!d)
		return -errno;
	while ((e = readdir(d))) {
		unsigned char cfg[2];
		char path[320];
		int fd, i;

		if (e->d_name[0] == '.' || sysfs_hex(e->d_name, "vendor") != 0x8086 ||
		    sysfs_hex(e->d_name, "device") != 0x7110)
			continue;
		snprintf(path, sizeof(path), "/sys/bus/pci/devices/%.64s/config",
			 e->d_name);
		fd = open(path, O_RDONLY);
		// past the first 64 bytes only for root
		if (fd < 0 || pread(fd, cfg, 2, 0x76) != 2) {
			err = -EACCES;
			if (fd >= 0)
				close(fd);
			continue;
		}
		close(fd);
		for (i = 0; i < 2; i++)
			if ((cfg[i] & 0x80) &&
			    (ch < 0 ? (cfg[i] & 7) < 4 : (cfg[i] & 7) == ch))
				found = 1;
	}
	closedir(d);
	return found ? 1 : err;
}

struct et_hal *et_hal_alsa(const char *rec2)
{
	static struct alsa a;
	static struct et_hal h = {
		.name = "alsa",
		.ctx = &a,
		.run = alsa_run,
		.has = alsa_has,
		.rec2 = alsa_rec2,
		.ctl_info = alsa_ctl_info,
		.ctl_get = alsa_ctl_get,
		.ctl_set = alsa_ctl_set,
		.selem = alsa_selem,
		.ctl_enum_name = alsa_ctl_enum_name,
		.ctl_db = alsa_ctl_db,
		.ctl_nth = alsa_ctl_nth,
		.param_get = alsa_param_get,
		.param_set = alsa_param_set,
		.reg_read = alsa_reg_read,
		.info = alsa_info,
		.proc = alsa_proc,
		.cpu = alsa_cpu,
		.watch = alsa_watch,
		.cpu_hz = alsa_cpu_hz,
		.now = alsa_now,
		.sleep = alsa_sleep,
		.ask_press = alsa_ask_press,
		.suspend = alsa_suspend,
		.reload = alsa_reload,
		.klog = alsa_klog,
		.type_f = alsa_type_f,
	};
	char name[32];

	a.card = snd_card_get_index(CARD_ID);
	a.regs_fd = -1;
	if (a.card < 0)
		return NULL;
	snprintf(name, sizeof(name), "hw:%d", a.card);
	if (snd_ctl_open(&a.ctl, name, 0) < 0)
		return NULL;
	a.regs_fd = open_regs(a.card);
	find_rec2(&a, rec2);
	return &h;
}
