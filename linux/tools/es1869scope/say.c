// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * say.c: es1869scope's messages. Notes:
 *
 * Text written to the terminal while the picture holds it in graphics mode
 * isn't seen, so a reason why a view couldn't open, or a stream stopped,
 * used to show only after the program quit, if at all. Each message still
 * goes to stderr as it comes, which the service's log takes, and the last
 * one is drawn across the bottom of the screen for SAY_SHOW_S seconds;
 * those that came while the picture held the terminal are printed again
 * once it is text. A message the same as the last is counted and not
 * printed, so that a stream refused once a second doesn't fill a log, and
 * the count goes out with the next message that differs.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "plot.h"
#include "say.h"

#define SAY_KEPT 8 // the messages kept for after the picture
#define SAY_RGB 0xffcf5a

static char last[SAY_LEN];
static unsigned long seq, repeats;
static time_t last_at;
static unsigned long drawn_seq;
static int drawn_new;
static char kept[SAY_KEPT][SAY_LEN];
static unsigned long nkept;
static int keeping;
static void (*hook)(const char *msg, void *arg);
static void *hook_arg;

// a message already said, again: how many times, before the next one
static void say_repeats(void)
{
	if (!repeats)
		return;
	fprintf(stderr, "es1869scope: the message before came %lu times more\n",
		repeats);
	repeats = 0;
}

void say(const char *fmt, ...)
{
	char msg[SAY_LEN];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (seq && !strcmp(msg, last)) {
		repeats++;
		return;
	}
	say_repeats();
	memcpy(last, msg, sizeof(last));
	last_at = time(NULL);
	seq++;
	fprintf(stderr, "es1869scope: %s\n", msg);
	if (keeping)
		memcpy(kept[nkept++ % SAY_KEPT], msg, sizeof(msg));
	if (hook)
		hook(msg, hook_arg);
}

void say_hook(void (*fn)(const char *msg, void *arg), void *arg)
{
	hook = fn;
	hook_arg = arg;
}

void say_keep(int on)
{
	keeping = on;
	if (on)
		nkept = 0;
}

void say_replay(void)
{
	unsigned long i = nkept > SAY_KEPT ? nkept - SAY_KEPT : 0;

	say_repeats();
	if (!nkept)
		return;
	fprintf(stderr, "es1869scope: while the picture showed:\n");
	for (; i < nkept; i++)
		fprintf(stderr, "  %s\n", kept[i % SAY_KEPT]);
	nkept = 0;
}

int say_bar(const struct canvas *c)
{
	return c->font ? (int)c->font->h + 4 : 0;
}

void say_draw(struct canvas *c)
{
	int h = say_bar(c), y = (int)c->h - h;

	drawn_seq = seq;
	drawn_new = seq && time(NULL) - last_at < SAY_SHOW_S;
	if (!h)
		return;
	fill(c, 0, y, c->w, h, drawn_new ? C_PANEL : C_BACK);
	if (!drawn_new)
		return;
	fill(c, 0, y, c->w, 1, SAY_RGB);
	text(c, 6, y + 3, SAY_RGB, last);
}

int say_due(void)
{
	return seq != drawn_seq ||
	       (drawn_new && time(NULL) - last_at >= SAY_SHOW_S);
}
