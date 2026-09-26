/*
 * esfmtest.h -- ESFM stress test: a dense MIDI stream played on the ESFM
 * device at interrupt time while essctl sends controller changes, then a
 * look for voices left sounding (stuck notes).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#ifndef ESFMTEST_H
#define ESFMTEST_H

/* 0: no stuck note, 1: stuck notes, -1: the test could not run; the
 * report is in report */
int esfm_stress_test(char *report, unsigned size);

#endif /* ESFMTEST_H */
