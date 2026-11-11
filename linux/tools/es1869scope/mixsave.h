/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * mixsave.h: the controls a run of the loop view sets, each one's old value
 * saved in a journal before it changes, and put back at the end of the run
 * where it still holds the run's value.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef MIXSAVE_H
#define MIXSAVE_H

#include <alsa/asoundlib.h>

#define MS_SAVED 16

struct ms_saved {
	char name[48];
	unsigned int count;
	long v[2]; /* what it was before the run */
	long set[2]; /* and what the run set it to */
	int foreign; /* another program's value holds it */
};

struct mixsave {
	snd_ctl_t *ctl;
	struct ms_saved saved[MS_SAVED];
	int nsaved;
	char journal[256];
	int journaled; /* this run wrote the journal, and removes it */
};

/* the run's controls on ctl, with its journal at path */
void ms_init(struct mixsave *m, snd_ctl_t *ctl, const char *path);
/* what a run that ended without putting it back left in the journal */
void ms_apply(struct mixsave *m);
/* a control set for the run, its old value saved first: 0 or -1 */
int ms_set(struct mixsave *m, const char *name, long a, long b);
/* the controls another program changed since the last look, which stay as
 * they set them, and those that hold the run's value again, which the run
 * takes back */
void ms_check(struct mixsave *m);
/* each control as it was before the run, where it holds the run's value */
void ms_restore(struct mixsave *m);

#endif /* MIXSAVE_H */
