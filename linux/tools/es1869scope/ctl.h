/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ctl.h: the card's controls by name, for es1869scope's engine and viewer.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef CTL_H
#define CTL_H

#include <alsa/asoundlib.h>

/* a control's first two values, and how many it has (two at most): 0 or -1 */
int ctl_read(snd_ctl_t *h, const char *name, long *v, unsigned int *count);
/* a control written, each of its values from v, the last one repeated */
int ctl_write(snd_ctl_t *h, const char *name, const long *v,
	      unsigned int count);
/* an integer control of count values, all written or all read */
int ctl_write_all(snd_ctl_t *h, const char *name, const long *v,
		  unsigned int count);
int ctl_read_all(snd_ctl_t *h, const char *name, long *v, unsigned int count);
/* an enumerated control's item by its text, or -1 */
int ctl_item(snd_ctl_t *h, const char *name, const char *item);

#endif /* CTL_H */
