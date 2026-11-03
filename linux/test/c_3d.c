// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The 3d group: the 3-D effect (the Spatializer) measured as ess3d
 * measures it, by src/s3dmeas.c. Audio 2 plays the plan's tones at 48 kHz
 * and Audio 1 records them from record source 7, the effect's output
 * before the master's volume, each run with its own values of 50h and
 * 52h-5Ah set through the driver's controls. 3d.quick checks what each
 * control does against the card of docs/SPATIALIZER.md; 3d.full writes the
 * whole report, for comparing the chip with that card.
 *
 * Notes:
 *
 * The mixer is set as ess3d sets it: the other inputs muted, the master at
 * its lowest step, Audio 2 at -4.5 dB, the record level at 0 dB, and the
 * filters of both DACs on, so that no image of a tone folds back onto it
 * at 48 kHz. Each run turns the effect off and on again, which puts the
 * limit's boost back, as ess3d's runs start with it back.
 *
 * The controls set 50h's bits 3:2 only together, so the full plan's run
 * with 50h at 08h (bit 3 without bit 2) can't be set and goes in the report
 * as failed. A run whose windows differ in their halves, where the
 * recording or the playback skipped, is measured once more, and one with
 * no signal twice, as ess3d does.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cases.h"
#include "hal.h"
#include "s3dmeas.h"

#define LEAD 0.35 // seconds the recording runs before the playback
#define F400 3 // the sweeps' 400 Hz
#define F1K 5 // and 1 kHz

struct rep {
	struct et_ctx *c;
	FILE *f;
};

static void out_line(void *ctx, const char *line)
{
	struct rep *r = ctx;

	if (r->f)
		fprintf(r->f, "%s\n", line);
	if (r->c->verbose > 1)
		et_log("    %s\n", line);
}

// a run's registers through the controls: -1 where they can't be set
static int set_regs(struct et_ctx *c, const u8 *reg)
{
	u8 r50 = reg[S3D_R50];
	int err = 0;

	if ((r50 & 0x0C) != 0x0C && (r50 & 0x0C) != 0x04)
		return -1;
	err |= et_ctl_set(c, "3D Control - Switch", 0, 0, 0) < 0;
	err |= et_ctl_set(c, "3D Control - Mono Switch", 0, r50 >> 1 & 1,
			  0) < 0;
	err |= et_ctl_set(c, "3D Control - Limit Switch", 0, r50 & 1, 0) < 0;
	err |= et_ctl_set(c, "3D Control - Level", 0, reg[S3D_R52], 0) < 0;
	err |= et_ctl_set(c, "3D Control - Limit Rise Level", 0, reg[S3D_R54],
			  0) < 0;
	err |= et_ctl_set(c, "3D Control - Limit Fall Level", 0, reg[S3D_R56],
			  0) < 0;
	err |= et_ctl_set(c, "3D Control - Limit Rise Speed", 0,
			  reg[S3D_R58] >> 4, 0) < 0;
	err |= et_ctl_set(c, "3D Control - Limit Fall Speed", 0,
			  reg[S3D_R58] & 15, 0) < 0;
	err |= et_ctl_set(c, "3D Control - Limit Fixed Level", 0,
			  reg[S3D_R5A], 0) < 0;
	if ((r50 & 0x0C) == 0x0C)
		err |= et_ctl_set(c, "3D Control - Switch", 0, 1, 0) < 0;
	return err ? -2 : 0;
}

// the mixer as ess3d sets it
static int set_mixer(struct et_ctx *c)
{
	if (et_mix_quiet(c) < 0 || et_mix_dig(c, 0) < 0 ||
	    et_mix_rec_a2(c, 0) < 0)
		return -1;
	if (et_ctl_set_enum(c, "Capture Source", "Master") < 0)
		return -1;
	et_ctl_set(c, "Master Playback Switch", 0, 1, 1);
	et_ctl_set(c, "Master Playback Volume", 0, 0, 0);
	et_ctl_set(c, "PCM Playback Volume", 0, 12, 12);
	et_ctl_set(c, "Capture Volume", 0, 4, 4);
	et_ctl_set(c, "Audio 2 Oversampling Playback Switch", 0, 0, 0);
	et_ctl_set(c, "Audio 2 Filter Playback Switch", 0, 1, 1);
	return et_ctl_set(c, "Audio 1 Filter Switch", 0, 1, 1) < 0 ? -1 : 0;
}

// one run: S3D_OK and the rest as s3d_end, -1 for a stream's error
static int one_run(struct et_ctx *c, struct s3d_meas *m, int i)
{
	static const struct et_fmt f = { ET_S16, 2, S3D_RATE };
	struct et_stream s[2];
	u32 pn, rn, at, n;
	s16 *play;
	int res = -1;

	pn = s3d_begin(m, i, &rn);
	play = et_alloc_frames(&f, pn);
	for (at = 0; at < pn; at += n) {
		n = pn - at > 32768 ? 32768 : pn - at;
		s3d_fill(m, play + 2 * at, (u16)n);
	}
	s[0] = et_rec(ET_A1C, &f, rn + (u32)(LEAD * S3D_RATE), 0);
	s[1] = et_play(ET_A2, &f, play, pn, LEAD);
	if (!et_run(c, s, 2)) {
		const s16 *rec = s[0].data;

		for (at = 0; at < s[0].done; at += n) {
			n = s[0].done - at > 32768 ? 32768 : s[0].done - at;
			if (s3d_take(m, rec + 2 * at, (u16)n))
				break;
		}
		res = 0;
	}
	free(s[0].data);
	free(play);
	return res;
}

// the plan, every run measured as ess3d measures it, the report in file
static enum et_result measure(struct et_ctx *c, struct s3d_meas *m, int plan,
			      const char *file, int *failed)
{
	struct rep rep = { c, NULL };
	struct s3d_chirp *chirp = NULL;
	char path[512], info[160];
	int i, n;

	snprintf(path, sizeof(path), "%s/%s", c->outdir, file);
	rep.f = fopen(path, "w");
	s3d_init(m, plan, 0, out_line, &rep);
	n = s3d_chirps(m);
	if (n) {
		chirp = calloc(n, sizeof(*chirp));
		if (!chirp)
			abort();
		s3d_chirp_mem(m, chirp, n);
	}
	*failed = 0;
	et_note(c, "%d runs, about %d s", s3d_count(m), s3d_seconds(m));
	for (i = 0; i < s3d_count(m); i++) {
		u8 reg[S3D_REGS];
		int tries = 0, res;
		u32 rn;

		s3d_regs(m, i, reg);
		res = set_regs(c, reg);
		if (res == -2) {
			et_fail(c, "the 3-D controls can't be set");
			break;
		}
		if (res < 0) {
			s3d_begin(m, i, &rn);
			s3d_failed(m, "the controls set 50h bits 3:2 together");
			et_note(c, "run %d (%s): 50h %02Xh has no control", i,
				s3d_name(m, i), reg[S3D_R50]);
			++*failed;
			continue;
		}
		for (;;) {
			res = one_run(c, m, i);
			if (res < 0)
				break;
			if (!i && !tries) {
				snprintf(info, sizeof(info),
					 "es1869test %s on %s, Audio 2 to source 7",
					 ET_VERSION, c->hal->name);
				s3d_header(m, S3D_RATE, S3D_RATE, info);
			}
			if (s3d_disturbed(m) && !tries) {
				et_note(c, "run %d (%s) skipped in %d windows, measured again",
					i, s3d_name(m, i), s3d_disturbed(m));
				tries++;
				continue;
			}
			res = s3d_end(m);
			if ((res & (S3D_NOSIGNAL | S3D_SHORT)) && ++tries < 2)
				continue;
			break;
		}
		if (res < 0 || (res & (S3D_NOSIGNAL | S3D_SHORT))) {
			s3d_failed(m, res < 0 ? "the streams failed" :
				   res & S3D_NOSIGNAL ? "no signal in the recording" :
							 "the recording stopped early");
			et_note(c, "run %d (%s) failed", i, s3d_name(m, i));
			++*failed;
			if (!i)
				break;
		} else if (res & S3D_CLIPPED) {
			et_note(c, "run %d (%s) clipped", i, s3d_name(m, i));
			++*failed;
		}
		if (rep.f)
			fflush(rep.f);
	}
	if (i == s3d_count(m))
		s3d_summary(m);
	if (rep.f)
		fclose(rep.f);
	et_note(c, "the report: %s", path);
	free(chirp);
	return i == s3d_count(m) ? ET_PASS : ET_ERROR;
}

static int find(const struct s3d_meas *m, const char *name)
{
	int i;

	for (i = 0; i < s3d_count(m); i++)
		if (!strcmp(s3d_name(m, i), name) && m->run[i].result == S3D_OK)
			return i;
	return -1;
}

// the boost B at a sweep's frequency, in dB, from S>S against the effect
// off: S>S is 1 + B
static double boost_db(const struct s3d_run *r, int f)
{
	double a = pow(10, r->u.sw.db[1][f] / 20), ph = r->u.sw.ph[0][f] * M_PI / 180;
	double re = a * cos(ph) - 1, im = a * sin(ph);

	return 10 * log10(re * re + im * im + 1e-12);
}

// 3d.quick: the quick plan, and what each control does in it
enum et_result c_3d_quick(struct et_ctx *c, const struct et_case *k)
{
	struct s3d_meas *m = calloc(1, sizeof(*m));
	int ess, lv0, lv20, mod, rat, ratl, again, failed, i, f, p;
	double worst = 0, dtlv;
	long cdb_hi, cdb_lo;
	enum et_result r;

	(void)k;
	if (!m)
		abort();
	if (set_mixer(c) < 0) {
		free(m);
		return ET_ERROR;
	}
	r = measure(c, m, S3D_PLAN_QUICK, "3d.quick.txt", &failed);
	if (r != ET_PASS) {
		et_fail(c, "run 0, with the effect off, had no signal");
		free(m);
		return ET_FAIL;
	}
	et_measure(c, "runs that failed or clipped", failed, "", 0, 0);
	ess = find(m, "ess");
	lv0 = find(m, "52=00");
	lv20 = find(m, "52=20");
	mod = find(m, "model");
	rat = find(m, "ratio");
	ratl = find(m, "ratio limit");
	again = find(m, "off again");
	if (ess < 0 || lv0 < 0 || lv20 < 0 || mod < 0 || rat < 0 || ratl < 0 ||
	    again < 0) {
		et_fail(c, "a run the checks need didn't measure");
		free(m);
		return ET_FAIL;
	}

	// the switch and the level at 63: the boost, the M as it is
	et_measure(c, "S>S at 400 Hz, ESS's setting", m->run[ess].u.sw.db[1][F400],
		   "dB", 12, 24);
	for (f = 0; f < S3D_FREQS; f++)
		if (fabs(m->run[ess].u.sw.db[0][f]) > fabs(worst))
			worst = m->run[ess].u.sw.db[0][f];
	et_measure(c, "M>M with ESS's setting, furthest from 0 dB", worst, "dB",
		   -1, 1);
	// the level's scale: the boost from 63 to 32
	if (c->hal->ctl_db(c->hal->ctx, "3D Control - Level", 0, 63, &cdb_hi) < 0 ||
	    c->hal->ctl_db(c->hal->ctx, "3D Control - Level", 0, 32, &cdb_lo) < 0) {
		et_fail(c, "'3D Control - Level' has no dB scale");
	} else {
		dtlv = (cdb_hi - cdb_lo) / 100.0;
		et_measure(c, "the boost at 400 Hz from level 63 to 32, against the scale",
			   boost_db(&m->run[ess], F400) -
				   boost_db(&m->run[lv20], F400) - dtlv,
			   "dB", -2, 2);
	}
	et_measure(c, "S>S at 400 Hz, level 0", m->run[lv0].u.sw.db[1][F400], "dB",
		   -1, 1);
	// 50h bit 1: the S made from the M
	et_measure(c, "M>S at 400 Hz with the model", m->run[mod].u.sw.db[2][F400],
		   "dB", 8, 24);
	et_measure(c, "S>S at 400 Hz with the model", m->run[mod].u.sw.db[1][F400],
		   "dB", -100, -12);
	// 50h bit 0: the S out held at S/M +6 dB, not at -24 dB
	et_measure(c, "the limit's hold at 1 kHz, S/M +6 dB",
		   m->run[rat].u.ra.ss[4] - m->run[ratl].u.ra.ss[4], "dB", 6, 40);
	et_measure(c, "the limit's hold at 1 kHz, S/M -24 dB",
		   m->run[rat].u.ra.ss[0] - m->run[ratl].u.ra.ss[0], "dB", -1.5,
		   1.5);
	// 54h-5Ah do nothing with the limit off
	worst = 0;
	for (i = 0; i < s3d_count(m); i++) {
		const struct s3d_run *x = &m->run[i];

		if (x->kind != S3D_SWEEP || x->result != S3D_OK ||
		    strlen(x->name) != 5 || x->name[0] != '5' ||
		    x->name[1] < '4' || x->name[2] != '=')
			continue;
		for (p = 0; p < 2; p++)
			for (f = F400 - 1; f <= F1K + 2; f++) {
				double d = x->u.sw.db[p][f] -
					   m->run[ess].u.sw.db[p][f];

				if (fabs(d) > fabs(worst))
					worst = d;
			}
	}
	et_measure(c, "54h-5Ah at their ends with the limit off, furthest from ESS's",
		   worst, "dB", -0.5, 0.5);
	// the path at the end against the start
	worst = 0;
	for (p = 0; p < 2; p++)
		for (f = 1; f < S3D_FREQS - 1; f++)
			if (fabs(m->run[again].u.sw.db[p][f]) > fabs(worst))
				worst = m->run[again].u.sw.db[p][f];
	et_measure(c, "the path at the end against the start", worst, "dB",
		   -0.5, 0.5);
	free(m);
	return ET_PASS;
}

// 3d.full: the full plan, its report for docs/SPATIALIZER.md's comparison
enum et_result c_3d_full(struct et_ctx *c, const struct et_case *k)
{
	struct s3d_meas *m = calloc(1, sizeof(*m));
	enum et_result r;
	int failed;

	(void)k;
	if (!m)
		abort();
	if (set_mixer(c) < 0) {
		free(m);
		return ET_ERROR;
	}
	r = measure(c, m, S3D_PLAN_FULL, "3d.full.txt", &failed);
	free(m);
	if (r != ET_PASS) {
		et_fail(c, "run 0, with the effect off, had no signal");
		return ET_FAIL;
	}
	// the one run that has no control: 50h at 08h
	et_measure(c, "runs that failed or clipped", failed, "", 0, 1);
	return ET_PASS;
}
