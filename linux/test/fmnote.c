// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869test --fm-note: a sine on the FM for a while, through the ESFM
 * hwdep (the native mode, register 6's output bits as given) or the OPL3
 * hwdep (OPL3 mode, C0h's channels A and B). The fm cases run it as a
 * command while PCM 1 records.
 *
 * usage: es1869test --fm-note esfm:OUT:FNUM:BLOCK:SECONDS
 *        es1869test --fm-note opl3:L|R|LR:FNUM:BLOCK:SECONDS
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <sound/asound_fm.h>

#include "../es1869/uapi/es1869.h"

static int open_hwdep(const char *title)
{
	char line[128], path[32];
	FILE *f = fopen("/proc/asound/hwdep", "r");
	int c, d, fd = -1;

	if (!f)
		return -ENODEV;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%d-%d:", &c, &d) == 2 && strstr(line, title)) {
			snprintf(path, sizeof(path), "/dev/snd/hwC%dD%d", c, d);
			fd = open(path, O_RDWR);
			if (fd < 0)
				fd = -errno;
			break;
		}
	fclose(f);
	return fd;
}

static void wait_s(double s)
{
	struct timespec ts = { (time_t)s, (long)((s - (time_t)s) * 1e9) };

	nanosleep(&ts, NULL);
}

static int esfm_write(int fd, const struct es1869_esfm_reg *r, int n)
{
	struct es1869_esfm_regs regs = { .count = n,
					 .regs = (uintptr_t)r };

	return ioctl(fd, ES1869_ESFM_IOCTL_WRITE, &regs);
}

static int esfm_note(int out, int fnum, int block, double secs)
{
	struct es1869_esfm_reg r[20];
	int fd = open_hwdep("ES1869 ESFM"), n = 0, op;

	if (fd < 0)
		return 2;
	ioctl(fd, ES1869_ESFM_IOCTL_RESET);
	// operator 0 of voice 0 alone: a sustained sine
	r[n++] = (struct es1869_esfm_reg){ 0x00, 0x21, 0 };
	r[n++] = (struct es1869_esfm_reg){ 0x01, 0x00, 0 };
	r[n++] = (struct es1869_esfm_reg){ 0x02, 0xF0, 0 };
	r[n++] = (struct es1869_esfm_reg){ 0x03, 0x05, 0 };
	r[n++] = (struct es1869_esfm_reg){ 0x04, fnum & 0xFF, 0 };
	r[n++] = (struct es1869_esfm_reg){ 0x05, block << 2 | fnum >> 8, 0 };
	r[n++] = (struct es1869_esfm_reg){ 0x06, out & 0x30, 0 };
	r[n++] = (struct es1869_esfm_reg){ 0x07, 0xE0, 0 };
	for (op = 1; op < 4; op++) {
		r[n++] = (struct es1869_esfm_reg){ op * 8 + 1, 0x3F, 0 };
		r[n++] = (struct es1869_esfm_reg){ op * 8 + 6, 0x00, 0 };
		r[n++] = (struct es1869_esfm_reg){ op * 8 + 7, 0x00, 0 };
	}
	r[n++] = (struct es1869_esfm_reg){ 0x240, 0x01, 0 };
	if (esfm_write(fd, r, n) < 0) {
		close(fd);
		return 3;
	}
	wait_s(secs);
	r[0] = (struct es1869_esfm_reg){ 0x240, 0x00, 0 };
	esfm_write(fd, r, 1);
	wait_s(0.2);
	close(fd);
	return 0;
}

static int opl3_note(int left, int right, int fnum, int block, double secs)
{
	struct snd_dm_fm_voice v;
	struct snd_dm_fm_note note;
	int fd = open_hwdep("OPL3"), mode = SNDRV_DM_FM_MODE_OPL3, op;

	if (fd < 0)
		return 2;
	if (ioctl(fd, SNDRV_DM_FM_IOCTL_RESET) < 0 ||
	    ioctl(fd, SNDRV_DM_FM_IOCTL_SET_MODE, mode) < 0) {
		close(fd);
		return 3;
	}
	for (op = 0; op < 2; op++) {
		memset(&v, 0, sizeof(v));
		v.op = op;
		v.voice = 0;
		v.do_sustain = 1;
		v.harmonic = 1;
		v.volume = op ? 63 : 0; // the modulator silent
		v.attack = 15;
		v.release = 5;
		v.left = left;
		v.right = right;
		if (ioctl(fd, SNDRV_DM_FM_IOCTL_SET_VOICE, &v) < 0) {
			close(fd);
			return 3;
		}
	}
	memset(&note, 0, sizeof(note));
	note.voice = 0;
	note.octave = block;
	note.fnum = fnum;
	note.key_on = 1;
	ioctl(fd, SNDRV_DM_FM_IOCTL_PLAY_NOTE, &note);
	wait_s(secs);
	note.key_on = 0;
	ioctl(fd, SNDRV_DM_FM_IOCTL_PLAY_NOTE, &note);
	wait_s(0.2);
	close(fd);
	return 0;
}

int et_fm_note(const char *spec)
{
	char mode[8], side[8];
	int fnum, block;
	double secs;

	if (sscanf(spec, "%7[^:]:%7[^:]:%i:%i:%lf", mode, side, &fnum, &block,
		   &secs) != 5)
		return 1;
	if (!strcmp(mode, "esfm"))
		return esfm_note((int)strtol(side, NULL, 0), fnum, block, secs);
	if (!strcmp(mode, "opl3"))
		return opl3_note(strchr(side, 'L') != NULL,
				 strchr(side, 'R') != NULL, fnum, block, secs);
	return 1;
}
