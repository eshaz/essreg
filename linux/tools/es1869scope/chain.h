/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * chain.h: es1869scope attached to es1869eq: the engine's state (eqstate.h)
 * drawn, and the settings that change the sound switched as the user at the
 * console, with no audio device opened.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef CHAIN_H
#define CHAIN_H

#include "draw.h"

/* the keys beyond a byte that the viewer takes */
#define CHAIN_UP 0x100
#define CHAIN_DOWN 0x101

/* the views: as es1869scope's, and the chain, the default */
enum { CHAIN_CLOCK = 1, CHAIN_SPECTRO, CHAIN_LOOP, CHAIN_TIME, CHAIN_VIEW };

struct chain;

/*
 * attached to the engine's state at path, if its engine runs (alive 0: also
 * a state that an engine left, as tests draw), with the card's controls of
 * ctl (NULL: none, as without the card): NULL, with the reason said
 */
struct chain *chain_open(const char *path, const char *ctl, int alive);
/* the switches as they were at attach again, unless kept, and the end */
void chain_close(struct chain *w);
/* the view to show */
void chain_view(struct chain *w, int view);
/* the chain's stage to show, from 0, the stream */
void chain_select(struct chain *w, int stage);
/*
 * a key, -1 for none, and the state looked at again: 1 if the canvas
 * changed, 0 if not, -1 to quit
 */
int chain_step(struct chain *w, struct canvas *c, int key);
/* the whole view again */
void chain_frame(struct chain *w, struct canvas *c);

#endif /* CHAIN_H */
