/*
 * The parts of esfmrec that don't need Windows, so the host tests run
 * them: the WAV header, the peak levels and the test tone of /sim.
 *
 * Notes:
 *
 * The music DAC runs at the FM synthesizer's rate, 14.31818 MHz / 288 =
 * 49,716 Hz. Mixer 7Fh bit 4 records its data on Audio 1 at that rate,
 * whatever A1h says (DS p.65). esfmrec saves those samples as they come,
 * so the WAV file says 49,716 Hz.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef FMREC_H
#define FMREC_H

#include "esstypes.h"

#define FMREC_RATE 49716UL // the music DAC's rate, 14.31818 MHz / 288
#define FMREC_HEADER 44    // bytes of the WAV header

// a WAV header for 16-bit stereo PCM at rate, followed by data_bytes
void fmrec_wav_header(u8 *hdr, u32 rate, u32 data_bytes);

// the highest absolute sample of each channel of 16-bit stereo frames
void fmrec_peaks(const s16 *pcm, u16 frames, u16 *left, u16 *right);

// a peak in tenths of a dB of full scale (-60 is -6.0 dB), -999 for 0
int fmrec_db10(u16 peak);

// the test tone of /sim at FMREC_RATE, 6 dB below full scale: 1 kHz on
// the left, 500 Hz on the right; phase[2] carries on between calls
void fmrec_tone(s16 *pcm, u16 frames, u32 *phase);

#endif /* FMREC_H */
