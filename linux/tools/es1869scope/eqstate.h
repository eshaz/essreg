/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * eqstate.h: what es1869scope -E knows, shared with a viewer: a file in
 * /run that the engine maps and writes under a sequence count and a viewer
 * maps and reads, and a FIFO beside it that takes the viewer's commands.
 * Only 32-bit and smaller fields, so that the i386 engine and a test host
 * of 64 bits lay it out alike.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef EQSTATE_H
#define EQSTATE_H

#include <stdint.h>

#define EQS_DIR "/run/es1869"
#define EQS_PATH EQS_DIR "/eq.state"
#define EQS_CMD EQS_DIR "/eq.cmd"
#define EQS_MAGIC 0x31534545 /* "EES1" */
#define EQS_VERSION 2
#define EQS_WHY 160
#define EQS_POINTS 128 /* the response's points, log-spaced */
#define EQS_BANDS 24
#define EQS_ROWS 64 /* spectrogram rows a source keeps */
#define EQS_BINS 128 /* a row's points across its band */
#define EQS_HIST 64 /* the blocks of history */
#define EQS_ZBINS 33 /* the codes from -16 to 16 */
#define EQS_SIDES 16 /* the DAC correction's taps each side, at a factor */
#define EQS_A1TAPS 34 /* Audio 1's correction: 17 a side */
#define EQS_A1PTAPS 32 /* and its phase taps: 16 a side */
#define EQS_IR 65 /* the impulse response's taps, 32 each side of 0 */
#define EQS_LVALS 33 /* Audio 1's linearity curve's knots, -512 to +512 */
#define EQS_LPER 40 /* its gains and turns: 4 bands by 10 envelope bins */

/* the engine's state, as eqs_state.state */
enum {
	EQS_STARTING,
	EQS_FINDING, /* the music plays; the lag is being found */
	EQS_MEASURING,
	EQS_RESTING, /* nothing plays */
	EQS_A1_RESTS, /* the music plays where Audio 1 can't follow: why says */
	EQS_STOPPED, /* the engine has ended */
};

/* the sources of the spectrogram rows */
enum { EQS_MON, EQS_A1, EQS_SOURCES };

/* a block, as -E's log has it */
struct eqs_block {
	int32_t t; /* seconds since the engine started */
	int32_t counted; /* into the trends and the refit */
	int32_t refitted, trim, moved, held; /* the trim in ppb */
	float dev[EQS_BANDS]; /* the loop's deviation in dB, NaN where none */
	float drift; /* the largest predicted drift, dB */
	float drift_hz;
	float corr20k; /* the correction's change at 20 kHz since the start */
	float chip, tie, applied, resid; /* ppm, us, ppm, ppm */
	float zstep[2]; /* the loop's step at the sign, LSB */
};

struct eqs_state {
	uint32_t magic, version, size;
	uint32_t seq; /* odd while the engine writes */
	int32_t pid;
	uint32_t now_ms; /* the engine's monotonic clock at the last write */
	uint32_t ack; /* the number of the last command done */
	uint32_t watched; /* a viewer asked to watch within 10 s */
	int32_t state;
	char why[EQS_WHY];
	/* the stream and the settings as the engine applied them */
	uint32_t stream_rate, factor, a1_rate, a1_rest, a1c, mode, treble;
	uint32_t hold; /* the refits and the trim held */
	/* the run */
	uint32_t lagged, segments, runs, blocks, restarts;
	int32_t lag;
	float delay; /* frames past the lag */
	float overlay; /* |r| at the lag */
	/* rms and peak over the last 100 ms in dBFS: the monitor's left and
	 * right, then Audio 1's */
	float rms[4], peak[4];
	/* the run's response at points log-spaced from 20 Hz to 20 kHz: gain
	 * in dB against 1 kHz, phase past the delay in degrees, coherence */
	float hz[EQS_POINTS];
	float mag[2][EQS_POINTS], phase[2][EQS_POINTS], coh[2][EQS_POINTS];
	float ir[2][EQS_IR]; /* the path's impulse response, each side's */
	/* the 24 bands: the last block's deviation, the start, the drift */
	float band_hz[EQS_BANDS];
	float band_dev[EQS_BANDS], band_base[EQS_BANDS];
	float band_drift[EQS_BANDS];
	/* the spectrogram: rows of dB against full scale, newest at
	 * row_next - 1, across 0 Hz to row_hz */
	uint32_t row_next[EQS_SOURCES], row_hz[EQS_SOURCES];
	int8_t rows[EQS_SOURCES][EQS_ROWS][EQS_BINS];
	/* the DAC's correction now and at the start, at 1x, 2x and 3x, and
	 * Audio 1's correction */
	int32_t taps[3 * EQS_SIDES], taps0[3 * EQS_SIDES];
	int32_t a1taps[EQS_A1TAPS], a1ptaps[EQS_A1PTAPS];
	/* how often the monitor's codes came to each value from -16 to 16 */
	uint32_t zhist[2][EQS_ZBINS];
	/* the blocks, the newest at hist_next - 1 */
	uint32_t hist_next, hist_count;
	struct eqs_block hist[EQS_HIST];
	/*
	 * Audio 1's linearity (a1lin.h): whether this start of Audio 1 took
	 * the stage, the refits, the frames a side behind the last, the
	 * curve's centre; what the path adds at each knot and what the stage
	 * in use left, LSB; the tables in use; and by band and envelope bin
	 * the gain (dB) and phase (degrees) that the stage left, with the
	 * frames behind each
	 */
	uint32_t lin_on, lin_fits;
	float lin_frames;
	int32_t lin_centre[2];
	float lin_path[2][EQS_LVALS], lin_left[2][EQS_LVALS];
	int32_t lin_curve[2][EQS_LVALS];
	int32_t lin_gain[2][EQS_LPER], lin_turn[2][EQS_LPER];
	float lin_db[2][EQS_LPER], lin_deg[2][EQS_LPER], lin_n[2][EQS_LPER];
};

/*
 * the engine's side: the file made, given the directory's group, mapped and
 * locked for the engine's life, and the FIFO made beside it and opened to
 * read; 0, or -errno
 */
int eqs_create(const char *path, const char *cmd, struct eqs_state **s,
	       int *cmd_fd);
/* a write of it begins and ends: the sequence count odd, then even again */
void eqs_begin(struct eqs_state *s);
void eqs_end(struct eqs_state *s);
/*
 * a command from the FIFO, if a whole line waits: 1 with its word (up to
 * len bytes) and number, 0 if none, -1 for a line that isn't a command
 */
int eqs_command(int fd, char *word, unsigned int len, uint32_t *num);

/*
 * a viewer's side: the file mapped to read, if it is a root-owned regular
 * file of this layout; 0, or -errno (-ESTALE for another layout)
 */
int eqs_map(const char *path, const struct eqs_state **s);
/* the pid of the engine that holds the file's lock, or 0 if none does */
int eqs_alive(const char *path);
/* a copy made whole, between two writes: 0, or -1 if the writes went on */
int eqs_copy(const struct eqs_state *s, struct eqs_state *out);
/* a command to the engine: 0, or -errno (-ENXIO when no engine reads) */
int eqs_send(const char *cmd, const char *word, uint32_t num);

#endif /* EQSTATE_H */
