/*
 * The parts of esfmrec that don't need Windows, so the host tests run
 * them: the WAV header and its repair, the names of a long recording's
 * files, the peak levels and the test tone of /sim.
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

#define FMREC_RATE 49716UL     // the music DAC's rate, 14.31818 MHz / 288
#define FMREC_HEADER 44        // bytes of the WAV header
#define FMREC_PART_SECS 9900UL // a file's most: 2 h 45 min, under 2 GB

// a WAV header for 16-bit stereo PCM at rate, followed by data_bytes
void fmrec_wav_header(u8 *hdr, u32 rate, u32 data_bytes);

// a header esfmrec wrote, with any length in it: set to the whole frames of
// a file of file_size bytes; 0, or -1 if it isn't esfmrec's header
int fmrec_wav_fix(u8 *hdr, u32 file_size, u32 *data_bytes);

// the name of part n of a recording that was named first: part 1 is first
// itself, part 2 of C:\REC\GAME.WAV is C:\REC\GAME_2.WAV, and the name is
// cut to keep 8.3 (LONGNAME.WAV gives LONGNA_2.WAV); 0, or -1 if it
// doesn't fit in size
int fmrec_part_name(const char *first, unsigned n, char *out, unsigned size);

// the highest absolute sample of each channel of 16-bit stereo frames
void fmrec_peaks(const s16 *pcm, u16 frames, u16 *left, u16 *right);

// a peak in tenths of a dB of full scale (-60 is -6.0 dB), -999 for 0
int fmrec_db10(u16 peak);

// the test tone of /sim at FMREC_RATE, 6 dB below full scale: 1 kHz on
// the left, 500 Hz on the right; phase[2] carries on between calls
void fmrec_tone(s16 *pcm, u16 frames, u32 *phase);

#endif /* FMREC_H */
