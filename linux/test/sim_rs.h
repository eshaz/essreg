/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The driver's rate correction for the simulated chip.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ET_SIM_RS_H
#define ET_SIM_RS_H

// a stretch of a stream at a change of rate: from its frame on, or copied as
// it is with on 0, its line going on
struct sim_rs_part {
	unsigned long from;
	long ppb;
	int on;
	int dither;
	int treble;
	int over; // ring frames to a stream frame, 0 for 1
};

long sim_rs_trim(int item);
short *sim_rs_stream(const short *src, unsigned long frames,
		     unsigned int channels, long ppb, int sign_768,
		     unsigned long *made);
short *sim_rs_parts(const short *src, unsigned long frames,
		    unsigned int channels, const struct sim_rs_part *parts,
		    int n, unsigned long *made);
short *sim_rs_over(const short *src, unsigned long frames, unsigned int channels,
		   long ppb, int over, int treble, unsigned long *made);
// the DAC's share of the last sample, which its correction takes back
double sim_rs_dac_last(void);
// what the filter costs on this CPU, in cycles a frame, by MMX or C, or 0
double sim_rs_cycles(unsigned int channels, int mmx);

#endif /* ET_SIM_RS_H */
