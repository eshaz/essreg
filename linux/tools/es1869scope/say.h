/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * say.h: es1869scope's messages. Each goes to stderr as it comes, the last
 * is kept for the screen, and those that came while the picture held the
 * terminal are printed again once it is text. From the main thread only.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef SAY_H
#define SAY_H

#include "draw.h"

#define SAY_LEN 200
#define SAY_SHOW_S 15 /* how long the screen shows a message */

/* a message, without the program's name or a newline */
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* each message also to fn, as the log of -E takes them */
void say_hook(void (*fn)(const char *msg, void *arg), void *arg);
/* while the picture holds the terminal: keep the messages for after */
void say_keep(int on);
/* the messages kept, printed again on stderr */
void say_replay(void);
/* the height of the bar across the bottom that the views leave free */
int say_bar(const struct canvas *c);
/* the bar: the last message while it is new, else empty */
void say_draw(struct canvas *c);
/* whether the bar wants drawing again: a new message, or the one it shows
 * grown old */
int say_due(void);

#endif /* SAY_H */
