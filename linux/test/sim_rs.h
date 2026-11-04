/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The driver's rate correction for the simulated chip.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ET_SIM_RS_H
#define ET_SIM_RS_H

long sim_rs_trim(int item);
short *sim_rs_stream(const short *src, unsigned long frames,
		     unsigned int channels, long ppb, int sign_768,
		     unsigned long *made);

#endif /* ET_SIM_RS_H */
