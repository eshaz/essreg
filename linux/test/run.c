// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869test's runner: the samples' formats, the runs of streams, the
 * measurements and the report, the dumps of failed recordings, and the
 * controls and parameters a case changes, journaled so that the run puts
 * them back.
 *
 * Notes:
 *
 * The report (es1869test.txt) and the measurements (es1869test.tsv) are
 * flushed and synced after every case, so that a run cut short by a crash
 * keeps what it found.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hal.h"
#include "run.h"

static FILE *rep, *tsv;

unsigned int et_fmt_bytes(const struct et_fmt *f)
{
	return f->channels * (f->format == ET_S16 || f->format == ET_U16 ? 2 :
									     1);
}

static const char *const fmt_names[] = { "U8", "S8", "S16_LE", "U16_LE" };

const char *et_fmt_name(enum et_format f)
{
	return fmt_names[f];
}

int et_fmt_parse(const char *s, enum et_format *f)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		if (!strcasecmp(s, fmt_names[i])) {
			*f = i;
			return 0;
		}
	return -EINVAL;
}

void et_unpack(const struct et_fmt *f, const void *buf, unsigned long frames,
	       unsigned int ch, double *out)
{
	const unsigned char *p = buf;
	unsigned int fb = et_fmt_bytes(f);
	unsigned long i;

	for (i = 0; i < frames; i++) {
		const unsigned char *q = p + i * fb;

		switch (f->format) {
		case ET_U8:
			out[i] = (q[ch] - 128) * 256.0;
			break;
		case ET_S8:
			out[i] = (signed char)q[ch] * 256.0;
			break;
		case ET_S16:
			out[i] = (short)(q[2 * ch] | q[2 * ch + 1] << 8);
			break;
		case ET_U16:
			out[i] = (q[2 * ch] | q[2 * ch + 1] << 8) - 32768;
			break;
		}
	}
}

void et_pack1(const struct et_fmt *f, void *buf, unsigned long frame,
	      unsigned int ch, double v)
{
	unsigned char *q = (unsigned char *)buf + frame * et_fmt_bytes(f);
	long x;

	if (f->format == ET_U8 || f->format == ET_S8)
		v /= 256;
	x = lrint(v);
	switch (f->format) {
	case ET_U8:
		x = x < -128 ? -128 : x > 127 ? 127 : x;
		q[ch] = x + 128;
		break;
	case ET_S8:
		x = x < -128 ? -128 : x > 127 ? 127 : x;
		q[ch] = (unsigned char)x;
		break;
	case ET_S16:
	case ET_U16:
		x = x < -32768 ? -32768 : x > 32767 ? 32767 : x;
		if (f->format == ET_U16)
			x += 32768;
		q[2 * ch] = x & 0xFF;
		q[2 * ch + 1] = (x >> 8) & 0xFF;
		break;
	}
}

void *et_alloc_frames(const struct et_fmt *f, unsigned long frames)
{
	void *p = calloc(frames ? frames : 1, et_fmt_bytes(f));

	if (!p) {
		fprintf(stderr, "es1869test: out of memory\n");
		exit(5);
	}
	return p;
}

static const char *const role_names[] = { "a2",	 "a1p",	 "a1c", "fmc",
					  "dual", "rec2", "cmd" };

int et_run(struct et_ctx *c, struct et_stream *s, int n)
{
	int i, err;

	for (i = 0; i < n; i++)
		if (!c->hal->has(c->hal->ctx, s[i].role)) {
			s[i].err = -ENODEV;
			return -ENODEV;
		}
	err = c->hal->run(c->hal->ctx, s, n);
	for (i = 0; i < n && c->verbose; i++)
		et_log("    %s%s: %s, %lu of %lu frames, start %.4f s, end %.4f s, %u xruns, delay %ld, period %lu, buffer %lu\n",
		       role_names[s[i].role], s[i].capture ? " capture" : "",
		       s[i].err ? strerror(-s[i].err) : "ok", s[i].done,
		       s[i].frames, s[i].t_trigger, s[i].t_end, s[i].xruns,
		       s[i].delay0, s[i].period_got, s[i].buffer_got);
	return err;
}

void et_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
	if (rep) {
		va_start(ap, fmt);
		vfprintf(rep, fmt, ap);
		va_end(ap);
	}
}

// --- the baseline ------------------------------------------------------------

// a measurement of an earlier run, from its TSV
struct base {
	char key[128]; // case, a tab, measure
	double v;
};

static struct base *btab;
static int bcount, bcompared, bmoved;

int et_baseline_load(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[512];
	int cap = 0;

	if (!f)
		return -errno;
	while (fgets(line, sizeof(line), f)) {
		char *cs = strtok(line, "\t"), *m = strtok(NULL, "\t");
		char *v = strtok(NULL, "\t"), *end;
		double x;

		if (!cs || !m || !v || !strcmp(cs, "case"))
			continue;
		x = strtod(v, &end);
		if (end == v)
			continue;
		if (bcount == cap) {
			cap = cap ? 2 * cap : 512;
			btab = realloc(btab, cap * sizeof(*btab));
			if (!btab)
				abort();
		}
		snprintf(btab[bcount].key, sizeof(btab[bcount].key), "%s\t%s",
			 cs, m);
		btab[bcount++].v = x;
	}
	fclose(f);
	return bcount;
}

void et_baseline_counts(int *compared, int *moved)
{
	*compared = bcompared;
	*moved = bmoved;
}

// what a repeat run on the same machine moves a measurement, by its unit
static double unit_tolerance(const char *unit, double b)
{
	double a = fabs(b);

	if (!strcmp(unit, "dB") || !strcmp(unit, "dBFS"))
		return 1;
	if (!strcmp(unit, "ppm"))
		return fmax(50, 0.1 * a);
	if (!strcmp(unit, "cents"))
		return 3;
	if (!strcmp(unit, "frames"))
		return fmax(2, 0.1 * a);
	if (!strcmp(unit, "s"))
		return fmax(0.01, 0.1 * a);
	if (!strcmp(unit, "ms"))
		return fmax(1, 0.1 * a);
	if (!strcmp(unit, "%"))
		return 0.5;
	if (!strcmp(unit, "Hz"))
		return fmax(0.5, 0.005 * a);
	// counts exactly, other values without a unit within 10 %
	if (!unit[0] || !strncmp(unit, "of ", 3))
		return b == floor(b) ? 0 : 0.1 * a;
	return 0.1 * a;
}

// how far a measurement may move from the baseline's, past the rounding
// of the TSV's 9 digits
static double tolerance(const char *unit, double b)
{
	return 1e-8 * (1 + fabs(b)) + unit_tolerance(unit, b);
}

// the baseline's measurement of the same case and name, or NULL
static const struct base *base_of(struct et_ctx *c, const char *name)
{
	char key[128];
	int i;

	snprintf(key, sizeof(key), "%s\t%s", c->k->id, name);
	for (i = 0; i < bcount; i++)
		if (!strcmp(btab[i].key, key))
			return &btab[i];
	return NULL;
}

int et_measure(struct et_ctx *c, const char *name, double v, const char *unit,
	       double lo, double hi)
{
	int ok = lo > hi || (v >= lo && v <= hi);
	const struct base *b = base_of(c, name);
	int moved = b && fabs(v - b->v) > tolerance(unit, b->v);

	c->measures++;
	if (!ok) {
		c->fails++;
		if (!c->why[0])
			snprintf(c->why, sizeof(c->why),
				 "%s %.6g %s, outside %.6g to %.6g", name, v,
				 unit, lo, hi);
	}
	if (lo > hi)
		et_log("    %-28s %12.6g %s\n", name, v, unit);
	else
		et_log("    %-28s %12.6g %s  (%.6g to %.6g)%s\n", name, v, unit,
		       lo, hi, ok ? "" : "  FAIL");
	bcompared += b != NULL;
	bmoved += moved;
	if (moved)
		et_log("    %-28s was %.6g %s in the baseline\n", "", b->v, unit);
	if (tsv)
		fprintf(tsv, "%s\t%s\t%.9g\t%s\t%.9g\t%.9g\t%s\n", c->k->id,
			name, v, unit, lo, hi,
			!ok ? "fail" : moved ? "moved" : "ok");
	return ok;
}

void et_fail(struct et_ctx *c, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	c->fails++;
	if (!c->why[0])
		snprintf(c->why, sizeof(c->why), "%s", msg);
	et_log("    FAIL: %s\n", msg);
}

void et_note(struct et_ctx *c, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	(void)c;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	et_log("    %s\n", msg);
}

static void put32(FILE *f, unsigned long v)
{
	fputc(v & 0xFF, f);
	fputc(v >> 8 & 0xFF, f);
	fputc(v >> 16 & 0xFF, f);
	fputc(v >> 24 & 0xFF, f);
}

static void put16(FILE *f, unsigned int v)
{
	fputc(v & 0xFF, f);
	fputc(v >> 8 & 0xFF, f);
}

void et_dump(struct et_ctx *c, const char *tag, const struct et_fmt *f,
	     const void *buf, unsigned long frames)
{
	char path[512];
	unsigned int fb = et_fmt_bytes(f), bps = fb / f->channels;
	const unsigned char *p = buf;
	unsigned long i, len = frames * fb;
	FILE *w;

	snprintf(path, sizeof(path), "%s/%s-%s.wav", c->outdir, c->k->id, tag);
	w = fopen(path, "wb");
	if (!w)
		return;
	fwrite("RIFF", 1, 4, w);
	put32(w, 36 + len);
	fwrite("WAVEfmt ", 1, 8, w);
	put32(w, 16);
	put16(w, 1);
	put16(w, f->channels);
	put32(w, f->rate);
	put32(w, f->rate * fb);
	put16(w, fb);
	put16(w, bps * 8);
	fwrite("data", 1, 4, w);
	put32(w, len);
	// WAV's 8-bit samples are unsigned and its 16-bit ones signed
	for (i = 0; i < len; i++) {
		unsigned char b = p[i];

		if ((f->format == ET_S8) ||
		    (f->format == ET_U16 && (i % 2) == 1))
			b ^= 0x80;
		fputc(b, w);
	}
	fclose(w);
	et_log("    dump: %s\n", path);
}

// --- the journal -------------------------------------------------------------

#define JMAX 512

static struct jent {
	char kind; // 'c' a control, 'p' a parameter
	char name[48];
	int index;
	long v[2];
	char sval[64];
} jtab[JMAX];
static int jcount;
static char jpath[256];

void et_journal_open(const char *path)
{
	snprintf(jpath, sizeof(jpath), "%s", path);
}

static int jfind(char kind, const char *name, int index)
{
	int i;

	for (i = 0; i < jcount; i++)
		if (jtab[i].kind == kind && jtab[i].index == index &&
		    !strcmp(jtab[i].name, name))
			return i;
	return -1;
}

// the journal file, synced, so that --restore can undo a cut-off run
static void jsave(void)
{
	char tmp[300];
	FILE *f;
	int i;

	if (!jpath[0])
		return;
	snprintf(tmp, sizeof(tmp), "%s.new", jpath);
	f = fopen(tmp, "w");
	if (!f)
		return;
	for (i = 0; i < jcount; i++)
		if (jtab[i].kind == 'c')
			fprintf(f, "ctl\t%s\t%d\t%ld\t%ld\n", jtab[i].name,
				jtab[i].index, jtab[i].v[0], jtab[i].v[1]);
		else
			fprintf(f, "param\t%s\t%s\n", jtab[i].name,
				jtab[i].sval);
	fflush(f);
	fsync(fileno(f));
	fclose(f);
	rename(tmp, jpath);
}

static void jadd_ctl(struct et_ctx *c, const char *name, int index)
{
	long v[2] = { 0, 0 };
	int n;

	if (jfind('c', name, index) >= 0 || jcount == JMAX)
		return;
	n = c->hal->ctl_get(c->hal->ctx, name, index, v, 2);
	if (n <= 0)
		return;
	if (n == 1)
		v[1] = v[0];
	jtab[jcount].kind = 'c';
	snprintf(jtab[jcount].name, sizeof(jtab[jcount].name), "%s", name);
	jtab[jcount].index = index;
	jtab[jcount].v[0] = v[0];
	jtab[jcount].v[1] = v[1];
	jcount++;
	jsave();
}

static void jadd_param(struct et_ctx *c, const char *id)
{
	char val[64];

	if (jfind('p', id, 0) >= 0 || jcount == JMAX)
		return;
	if (c->hal->param_get(c->hal->ctx, id, val, sizeof(val)) < 0)
		return;
	jtab[jcount].kind = 'p';
	snprintf(jtab[jcount].name, sizeof(jtab[jcount].name), "%s", id);
	jtab[jcount].index = 0;
	snprintf(jtab[jcount].sval, sizeof(jtab[jcount].sval), "%s", val);
	jcount++;
	jsave();
}

// everything the run changed back, last change first; the failures
int et_journal_restore(struct et_hal *h)
{
	int i, bad = 0;

	for (i = jcount - 1; i >= 0; i--) {
		int err;

		if (jtab[i].kind == 'c')
			err = h->ctl_set(h->ctx, jtab[i].name, jtab[i].index,
					 jtab[i].v, 2);
		else
			err = h->param_set(h->ctx, jtab[i].name, jtab[i].sval);
		if (err < 0) {
			fprintf(stderr, "es1869test: can't restore %s %s: %s\n",
				jtab[i].kind == 'c' ? "control" : "parameter",
				jtab[i].name, strerror(-err));
			bad++;
		}
	}
	jcount = 0;
	if (jpath[0] && !bad)
		unlink(jpath);
	return bad;
}

// a journal left by a run that didn't finish, applied; the failures, or
// -1 for none to apply
int et_journal_replay(struct et_hal *h, const char *path)
{
	char line[256];
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	jcount = 0;
	while (fgets(line, sizeof(line), f) && jcount < JMAX) {
		struct jent *e = &jtab[jcount];
		char *t = strtok(line, "\t\n");

		if (!t)
			continue;
		memset(e, 0, sizeof(*e));
		if (!strcmp(t, "ctl")) {
			char *name = strtok(NULL, "\t\n");
			char *idx = strtok(NULL, "\t\n");
			char *l = strtok(NULL, "\t\n");
			char *r = strtok(NULL, "\t\n");

			if (!name || !idx || !l || !r)
				continue;
			e->kind = 'c';
			snprintf(e->name, sizeof(e->name), "%s", name);
			e->index = atoi(idx);
			e->v[0] = atol(l);
			e->v[1] = atol(r);
			jcount++;
		} else if (!strcmp(t, "param")) {
			char *id = strtok(NULL, "\t\n");
			char *val = strtok(NULL, "\n");

			if (!id)
				continue;
			e->kind = 'p';
			snprintf(e->name, sizeof(e->name), "%s", id);
			snprintf(e->sval, sizeof(e->sval), "%s",
				 val ? val : "");
			jcount++;
		}
	}
	fclose(f);
	snprintf(jpath, sizeof(jpath), "%s", path);
	return et_journal_restore(h);
}

int et_ctl_get(struct et_ctx *c, const char *name, int index, long *v, int n)
{
	return c->hal->ctl_get(c->hal->ctx, name, index, v, n);
}

int et_ctl_set(struct et_ctx *c, const char *name, int index, long l, long r)
{
	long v[2] = { l, r };
	int err;

	jadd_ctl(c, name, index);
	err = c->hal->ctl_set(c->hal->ctx, name, index, v, 2);
	if (err < 0)
		et_note(c, "control '%s' %d: %s", name, index, strerror(-err));
	return err;
}

int et_ctl_set_enum(struct et_ctx *c, const char *name, const char *item)
{
	struct et_ctl_info info;
	char buf[64];
	int i, err;

	err = c->hal->ctl_info(c->hal->ctx, name, 0, &info);
	if (err < 0)
		return err;
	for (i = 0; i < info.items; i++) {
		if (c->hal->ctl_enum_name(c->hal->ctx, name, 0, i, buf,
					  sizeof(buf)) < 0)
			continue;
		if (!strcmp(buf, item))
			return et_ctl_set(c, name, 0, i, i);
	}
	return -ENOENT;
}

int et_param_get(struct et_ctx *c, const char *id, long *v)
{
	char buf[64];
	int err = c->hal->param_get(c->hal->ctx, id, buf, sizeof(buf));

	if (err < 0)
		return err;
	*v = buf[0] == 'Y' ? 1 : buf[0] == 'N' ? 0 : strtol(buf, NULL, 0);
	return 0;
}

int et_param_set(struct et_ctx *c, const char *id, long v)
{
	char buf[32];

	jadd_param(c, id);
	snprintf(buf, sizeof(buf), "%ld", v);
	return c->hal->param_set(c->hal->ctx, id, buf);
}

// --- the report --------------------------------------------------------------

int et_report_open(const char *dir)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/es1869test.txt", dir);
	rep = fopen(path, "w");
	snprintf(path, sizeof(path), "%s/es1869test.tsv", dir);
	tsv = fopen(path, "w");
	if (!rep || !tsv)
		return -errno;
	fprintf(tsv, "case\tmeasure\tvalue\tunit\tlo\thi\tresult\n");
	return 0;
}

void et_report_sync(void)
{
	if (rep) {
		fflush(rep);
		fsync(fileno(rep));
	}
	if (tsv) {
		fflush(tsv);
		fsync(fileno(tsv));
	}
}

void et_report_close(void)
{
	et_report_sync();
	if (rep)
		fclose(rep);
	if (tsv)
		fclose(tsv);
	rep = tsv = NULL;
}
