/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * es1869test: the loopback test suite of snd-es1869. Its cases play and
 * record through the chip, the cable from its output and a second card,
 * measure what came back, and report each as PASS, FAIL, KNOWN or SKIP.
 *
 * Notes:
 *
 * The machine is reached only through struct et_hal (hal.h): hal_alsa.c on
 * the laptop, hal_sim.c on a simulated ES1869 for the host tests. A run of
 * streams is a batch: the case describes every stream, the HAL starts and
 * feeds them at their times, and the case analyzes what came back.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ET_H
#define ET_H

#include <stdarg.h>
#include <stdint.h>

#define ET_VERSION "0.1"

// the streams' devices
enum et_role {
	ET_A2, // Audio 2, PCM 0 playback subdevice 0
	ET_A1P, // Audio 1 playback, PCM 0 subdevice 1
	ET_A1C, // Audio 1 capture, PCM 0
	ET_FMC, // the music DAC recorded digitally, PCM 1
	ET_DUAL, // both DACs, PCM 2, four channels
	ET_REC2, // the second card's capture, through the cable
	ET_CMD, // a shell command, run at the stream's start
	ET_NROLES
};

enum et_format { ET_U8, ET_S8, ET_S16, ET_U16 };

struct et_fmt {
	enum et_format format;
	unsigned int channels, rate;
};

// et_stream.flags
#define ET_MMAP 0x01 // mmap access, else read/write
#define ET_DMIX 0x02 // through a dmix plugin on the role's device
#define ET_NOSTART 0x04 // the stream starts with the first write
#define ET_STALL 0x08 // the pause is a stall: no transfers, the PCM runs

struct et_stream {
	enum et_role role;
	int capture;
	struct et_fmt fmt;
	unsigned long period, buffer; // frames, 0 for the HAL's defaults
	unsigned int flags;
	void *data; // what to play, or room for the recording
	unsigned long frames; // to play or to record
	const char *cmd; // ET_CMD: the command line
	double start; // seconds after the run's start
	double pause_at, pause_for; // a pause, seconds into the stream
	double timeout; // seconds the stream may take, 0 for its length + 5 s
	double probe_at; // a status reading this long after the start, 0 none

	// results
	int err; // 0, or a negative errno
	unsigned int xruns;
	unsigned long done; // frames moved
	double t_trigger; // when the stream started, from the run's start
	double t_end; // when its last frame moved
	long delay0; // snd_pcm_delay just after the start, in frames
	unsigned long period_got, buffer_got; // what the device took
	double probe_t; // the reading's timestamp, from the run's start
	long probe_delay; // its delay, in frames
	long probe_avail; // its room, in frames
	unsigned long probe_done; // the frames moved by then
	int status; // ET_CMD: the exit status
};

// a control's description
struct et_ctl_info {
	int type; // ET_CTL_BOOL, _INT, _ENUM
	int count; // values
	long min, max;
	int items; // an enum's
};

#define ET_CTL_BOOL 1
#define ET_CTL_INT 2
#define ET_CTL_ENUM 3

// results
enum et_result { ET_PASS, ET_FAIL, ET_KNOWN, ET_SKIP, ET_ERROR };

// et_case.flags: what a case needs, and how it may be selected
#define ET_QUICK 0x0001 // in the quick run
#define ET_CABLE 0x0002 // the cable from the output to the second card
#define ET_SECOND 0x0004 // the second card
#define ET_ROOT 0x0008 // root (parameters, registers)
#define ET_INTER 0x0010 // someone at the machine
#define ET_OPTIN 0x0020 // only when named
#define ET_LOUD 0x0040 // audible on the speakers
#define ET_DESTR 0x0080 // reloads the module or suspends
#define ET_MIDI 0x0100 // aplaymidi and the sequencer
#define ET_LINEAR 0x0200 // a second card that hears levels linearly
#define ET_NEW 0x0400 // snd-es1869's own devices, controls or parameters

struct et_ctx;
struct et_case;

typedef enum et_result (*et_case_fn)(struct et_ctx *c, const struct et_case *k);

struct et_case {
	const char *id; // group.name
	unsigned int flags;
	et_case_fn fn;
	int arg;
	const char *spec; // what it checks
};

struct et_hal;

struct et_ctx {
	struct et_hal *hal;
	const char *outdir; // reports and dumps
	int verbose;
	double max_out; // dB, the most the output may carry
	int stock; // the A/B run on snd-es18xx
	int have_rec2; // the second card hears the left output
	double rec2_gain; // its level for a full-scale tone, dBFS
	char rec2_name[32];
	int dig_exact; // Audio 2 into the music DAC records exactly
	int rec2_linear; // the second card's input is linear (cal.linear)
	// the case running
	const struct et_case *k;
	int measures, fails;
	char why[256]; // the first failure's reason
};

// samples
unsigned int et_fmt_bytes(const struct et_fmt *f); // a frame's
const char *et_fmt_name(enum et_format f);
int et_fmt_parse(const char *s, enum et_format *f);
// channel ch of frames interleaved frames, scaled to s16 (unsigned shifted)
void et_unpack(const struct et_fmt *f, const void *buf, unsigned long frames,
	       unsigned int ch, double *out);
// one sample in s16 scale to the format, rounded and clipped
void et_pack1(const struct et_fmt *f, void *buf, unsigned long frame,
	      unsigned int ch, double v);
void *et_alloc_frames(const struct et_fmt *f, unsigned long frames);

// a run of streams on the HAL; 0, or the first stream's error
int et_run(struct et_ctx *c, struct et_stream *s, int n);

// a measurement: name, value, unit, the range it must be in (lo > hi for
// none); it fails the case when out of range, and goes to the TSV
int et_measure(struct et_ctx *c, const char *name, double v, const char *unit,
	       double lo, double hi);
// a failure with its reason
void et_fail(struct et_ctx *c, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void et_note(struct et_ctx *c, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void et_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// a recording kept for a failure, as a WAV in the output directory
void et_dump(struct et_ctx *c, const char *tag, const struct et_fmt *f,
	     const void *buf, unsigned long frames);

// controls, by name and index, with the journal of the old value
int et_ctl_get(struct et_ctx *c, const char *name, int index, long *v, int n);
int et_ctl_set(struct et_ctx *c, const char *name, int index, long l, long r);
int et_ctl_set_enum(struct et_ctx *c, const char *name, const char *item);
int et_param_get(struct et_ctx *c, const char *id, long *v);
int et_param_set(struct et_ctx *c, const char *id, long v);

// the case table (cases.c)
extern const struct et_case et_cases[];
extern const int et_ncases;

#endif
