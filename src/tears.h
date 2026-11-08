/*
 * Torn readings of a DMA count, found in a series of readings of it, for
 * sb16test, essprobe and esswchk.
 *
 * Notes:
 *
 * The 8237 gives a channel's count a byte at a time. When a byte comes
 * between the reads of the low and the high byte, the reading is 256 off,
 * its position 256 ahead (Linux's driver measured it on the laptop's
 * ES1869). The fronts and the drivers read until two readings agree
 * within 32, so a game never sees such a reading through them; a front
 * without that rule handed a torn reading of an 8-bit channel to a game
 * halved, 128 of its words off.
 *
 * tears_add() takes positions that rise with time, readings taken close
 * together. A reading whose neighbours agree within TEARS_NEAR and that
 * sits TEARS_NEAR or more from their mean is torn, and is counted by how
 * far off it is. Steps back past a slack, and steps forward past a limit,
 * between the readings that aren't torn are counted too, so a reader's
 * own fault shows as well. The positions wrap at the ring's size, which
 * should be well over 2 * 384.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef TEARS_H
#define TEARS_H

#include "esstypes.h"

#define TEARS_NEAR 96 // neighbours this close agree; this far off is torn

struct tears {
  u32 ring;       // positions run 0 to ring - 1, then wrap
  u16 slack;      // a step back of more than this is counted
  u16 jump;       // a step forward of more than this is counted
  u16 prev[2];    // the last two readings
  u8 have;        // how many of them there are
  u8 prev_torn;   // prev[0] was torn
  u32 readings;   // taken so far
  u32 torn128;    // off by 96 to 191: a front's torn byte count, in words
  u32 torn256;    // off by 192 to 383: the 8237's own count torn
  u32 torn_other; // off by more
  u32 back;       // steps back past slack, torn readings left out
  u32 jumps;      // steps forward past jump, torn readings left out
  s32 worst;      // the torn reading farthest off, signed, 0 if none
};

void tears_init(struct tears *t, u32 ring, u16 slack, u16 jump);

// n more positions, each taken after the last
void tears_add(struct tears *t, const u16 *pos, unsigned n);

// a pause in the readings: the next one has no neighbour before it
void tears_break(struct tears *t);

// the torn readings of all sizes
u32 tears_torn(const struct tears *t);

#endif /* TEARS_H */
