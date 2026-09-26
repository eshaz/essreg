/*
 * ESFM stress test. Plays a dense MIDI stream on the ESFM
 * device at interrupt time while essctl sends controller
 * changes, then looks for voices left sounding (stuck notes).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESFMTEST_H
#define ESFMTEST_H

// returns 0 with no stuck notes, 1 with stuck notes, or -1 if the test
// could not run, and writes the report to report
int esfm_stress_test(char *report, unsigned size);

#endif /* ESFMTEST_H */
