// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * mixsave.c: the controls a run of the loop view sets, for es1869scope.
 *
 * Notes:
 *
 * Before a control changes, its old value goes to the journal, a line a
 * control, and the end of the run puts it back. A run that ends without
 * doing so, killed or crashed, leaves the journal, and the next run puts
 * that back once it holds the devices.
 *
 * Another program's change stays as they set it: the control leaves the
 * journal, and the end of the run doesn't put it back. Once the control
 * holds the run's value again, the run takes it back. A viewer attached to
 * the engine, es1869eq, puts back at its exit what it found at attach,
 * which is the engine's value, and the engine's stop then puts back what
 * was there before the engine. Without that, the engine's own settings
 * stayed after it, and the next shutdown saved them.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ctl.h"
#include "mixsave.h"
#include "say.h"

void ms_init(struct mixsave *m, snd_ctl_t *ctl, const char *path)
{
	memset(m, 0, sizeof(*m));
	m->ctl = ctl;
	snprintf(m->journal, sizeof(m->journal), "%s", path);
}

// a control's value against what the run set: 1 it holds it, 0 another's,
// -1 unread
static int ms_holds(struct mixsave *m, const struct ms_saved *s)
{
	long cur[2];
	unsigned int n;

	if (ctl_read(m->ctl, s->name, cur, &n))
		return -1;
	return cur[0] == s->set[0] && (s->count < 2 || cur[1] == s->set[1]);
}

// what the run holds changed, before it changes anything more
static void ms_write(struct mixsave *m)
{
	char tmp[sizeof(m->journal) + 8];
	FILE *f;
	int i;

	snprintf(tmp, sizeof(tmp), "%s.new", m->journal);
	f = fopen(tmp, "w");
	if (!f)
		return;
	for (i = 0; i < m->nsaved; i++)
		if (!m->saved[i].foreign)
			fprintf(f, "%u %ld %ld %s\n", m->saved[i].count,
				m->saved[i].v[0], m->saved[i].v[1],
				m->saved[i].name);
	if (fflush(f) || fsync(fileno(f))) {
		fclose(f);
		unlink(tmp);
		return;
	}
	if (!fclose(f) && !rename(tmp, m->journal))
		m->journaled = 1;
}

void ms_apply(struct mixsave *m)
{
	FILE *f = fopen(m->journal, "r");
	char line[128];

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		long val[2];
		unsigned int count;
		int at = 0;

		line[strcspn(line, "\n")] = 0;
		if (sscanf(line, "%u %ld %ld %n", &count, &val[0], &val[1],
			   &at) >= 3 &&
		    at)
			ctl_write(m->ctl, line + at, val, count);
	}
	fclose(f);
	unlink(m->journal);
	say("put back the mixer an earlier run left");
}

int ms_set(struct mixsave *m, const char *name, long a, long b)
{
	long old[2], val[2] = { a, b };
	struct ms_saved *s;
	unsigned int count;
	int i;

	if (ctl_read(m->ctl, name, old, &count) < 0)
		return -1;
	for (i = 0; i < m->nsaved; i++)
		if (!strcmp(m->saved[i].name, name))
			break;
	s = &m->saved[i];
	if (i == m->nsaved) {
		if (m->nsaved == MS_SAVED)
			return -1;
		m->nsaved++;
		snprintf(s->name, sizeof(s->name), "%s", name);
		s->count = count;
		s->v[0] = old[0];
		s->v[1] = count > 1 ? old[1] : old[0];
		s->foreign = 0;
		ms_write(m);
	} else if (s->foreign) {
		// set again by the run: its own, with its old value
		s->foreign = 0;
		ms_write(m);
	}
	s->set[0] = a;
	s->set[1] = b;
	return ctl_write(m->ctl, name, val, 2);
}

void ms_check(struct mixsave *m)
{
	int i, moved = 0;

	for (i = 0; i < m->nsaved; i++) {
		struct ms_saved *s = &m->saved[i];
		int held = ms_holds(m, s);

		if (held < 0 || held == !s->foreign)
			continue;
		s->foreign = !held;
		moved = 1;
		if (s->foreign)
			say("\"%s\" was changed by another program: it stays "
			    "as they set it",
			    s->name);
		else
			say("\"%s\" holds this run's value again: its end puts "
			    "back what was there before",
			    s->name);
	}
	if (moved)
		ms_write(m);
}

void ms_restore(struct mixsave *m)
{
	int i;

	for (i = m->nsaved - 1; i >= 0; i--)
		if (ms_holds(m, &m->saved[i]) == 1)
			ctl_write(m->ctl, m->saved[i].name, m->saved[i].v,
				  m->saved[i].count);
	m->nsaved = 0;
	// only the journal this run wrote: another run's is its own
	if (m->journaled)
		unlink(m->journal);
	m->journaled = 0;
}
