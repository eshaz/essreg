// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * essreg's register access (src/esshw.h) on Linux: every register through
 * snd-es1869's register hwdep, "ES1869 registers", and so through the
 * driver's lock and shadows. Reads that change the chip's state (mixer 40h,
 * Audio_Base+6h, +Ah and +Eh) are forced, as essreg means them. Writes need
 * the module's regs_write=1 and root.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "esshw.h"
#include "../../es1869/uapi/es1869.h"

esshw_ctx esshw = { 0x220, 0, 0, 0, ESSHW_SAFE, ESSHW_DIRECT, 0, 0, 0, 0 };

static int fd = -1;

// the hwdep's device node, from /proc/asound/hwdep's "C-D: name" lines
static int dev(void)
{
	char line[128], path[64];
	FILE *f;
	int card, num;

	if (fd >= 0)
		return fd;
	f = fopen("/proc/asound/hwdep", "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (!strstr(line, "ES1869 registers") ||
		    sscanf(line, "%d-%d", &card, &num) != 2)
			continue;
		snprintf(path, sizeof(path), "/dev/snd/hwC%dD%d", card, num);
		fd = open(path, O_RDWR);
		if (fd < 0)
			fd = open(path, O_RDONLY);
		break;
	}
	fclose(f);
	if (fd >= 0) {
		struct es1869_regs_info info;

		if (!ioctl(fd, ES1869_IOCTL_INFO, &info)) {
			esshw.audio_base = info.port;
			esshw.config_base = info.cfg;
		}
	}
	return fd;
}

static int err_of(int e)
{
	switch (e) {
	case ENODEV:
	case ENOENT:
		return -ESSHW_ENODEV;
	case EBUSY:
		return -ESSHW_EBUSY;
	case EINVAL:
		return -ESSHW_EPARAM;
	case ETIMEDOUT:
		return -ESSHW_ETIMEOUT;
	case EIO:
		return -ESSHW_EDESYNC;
	}
	return -ESSHW_EFAIL;
}

static int reg_io(int write, u8 bank, u8 ldn, u8 reg, u8 flags, u8 value)
{
	struct es1869_reg r = { bank, ldn, reg, flags, value, { 0 } };

	if (dev() < 0)
		return -ESSHW_ENODEV;
	if (ioctl(fd, write ? ES1869_IOCTL_WRITE : ES1869_IOCTL_READ, &r))
		return err_of(errno);
	return write ? 0 : r.value;
}

u8 esshw_inb(u16 port)
{
	int v = reg_io(0, ES1869_BANK_APORT, 0, (u8)(port - esshw.audio_base),
		       ES1869_REG_FORCE, 0);

	return v < 0 ? 0xFF : (u8)v;
}

void esshw_outb(u16 port, u8 value)
{
	reg_io(1, ES1869_BANK_APORT, 0, (u8)(port - esshw.audio_base), 0,
	       value);
}

int esshw_mixer_read(u8 reg)
{
	return reg_io(0, ES1869_BANK_MIXER, 0, reg,
		      reg == 0x40 ? ES1869_REG_FORCE : 0, 0);
}

int esshw_mixer_write(u8 reg, u8 value)
{
	return reg_io(1, ES1869_BANK_MIXER, 0, reg, 0, value);
}

int esshw_ctrl_read(u8 reg)
{
	return reg_io(0, ES1869_BANK_CTRL, 0, reg, ES1869_REG_LIVE, 0);
}

int esshw_ctrl_write(u8 reg, u8 value)
{
	return reg_io(1, ES1869_BANK_CTRL, 0, reg, 0, value);
}

int esshw_port_read(u8 offset)
{
	u8 force = offset == 0x06 || offset == 0x0A || offset == 0x0E;

	return reg_io(0, ES1869_BANK_APORT, 0, offset,
		      force ? ES1869_REG_FORCE : 0, 0);
}

int esshw_port_write(u8 offset, u8 value)
{
	return reg_io(1, ES1869_BANK_APORT, 0, offset, 0, value);
}

int esshw_cfg_read(u8 offset)
{
	return reg_io(0, ES1869_BANK_CPORT, 0, offset, 0, 0);
}

int esshw_cfg_write(u8 offset, u8 value)
{
	return reg_io(1, ES1869_BANK_CPORT, 0, offset, 0, value);
}

int esshw_pnp_read(u8 ldn, u8 reg)
{
	return ldn == 0xFF ? reg_io(0, ES1869_BANK_PNPCARD, 0, reg, 0, 0) :
			     reg_io(0, ES1869_BANK_PNPLDN, ldn, reg, 0, 0);
}

int esshw_pnp_write(u8 ldn, u8 reg, u8 value)
{
	return ldn == 0xFF ? reg_io(1, ES1869_BANK_PNPCARD, 0, reg, 0, value) :
			     reg_io(1, ES1869_BANK_PNPLDN, ldn, reg, 0, value);
}

int esshw_mixer_id(u8 id[4])
{
	int i, v;

	for (i = 0; i < 4; i++) {
		v = esshw_mixer_read(0x40);
		if (v < 0)
			return v;
		id[i] = (u8)v;
	}
	return 0;
}

int esshw_detect_config(void)
{
	return dev() < 0 ? -ESSHW_ENODEV : esshw.config_base ? 0 : -ESSHW_ENOCFG;
}

int esshw_dsp_idle(void)
{
	return 1; // the driver serializes the DSP's commands itself
}

const char *esshw_strerror(int err)
{
	static const char *const names[] = {
		"ok", "no ES1869 (snd-es1869 loaded?)", "in use",
		"bad parameter", "busy", "timed out", "no Config_Base",
		"refused (writes need regs_write=1 and root)",
		"the DSP took half a command",
	};

	if (err < 0)
		err = -err;
	return err < (int)(sizeof(names) / sizeof(names[0])) ? names[err] :
							      "error";
}
