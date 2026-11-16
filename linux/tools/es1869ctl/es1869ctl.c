// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869ctl: snd-es1869's status and settings on one full-screen panel, for
 * what alsamixer can't show: the streams and their real rates, where the ADC
 * gets its input, and the settings that are only module parameters.
 *
 *   es1869ctl                   the panel
 *   es1869ctl --dump            every page as text
 *   es1869ctl --set ID=VALUE    a setting, by its parameter's name
 *   es1869ctl --root DIR        DIR/sys and DIR/proc in place of the machine's,
 *                               with no ALSA control, for the tests
 *
 * Notes:
 *
 * The pages come from the driver's own settings.def, so a new setting shows up
 * by itself, on the page of the row before it. A setting with a control goes
 * through the control, which anyone in the audio group can write, and its
 * events redraw the panel at once, so it follows alsamixer and the volume
 * buttons. A setting without one goes through its parameter, which only root
 * can write. A setting this driver doesn't have reads "not in this driver".
 *
 * /proc/asound/ES1869/es1869 reads every register of the chip, so it is read
 * only on the Status page, and only every 2 s.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <alsa/asoundlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <ncurses.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../test/settab.h"

#define VERSION "0.1"
#define CARD "hw:CARD=ES1869"
#define PARAMS "/sys/module/snd_es1869/parameters"
#define PROC "/proc/asound/ES1869"
#define MAX_LINES 96
#define BAR 16

// what the panel won't write: it hangs the machine at the first recorded byte
static const char *const locked[] = { "piix4_type_f", NULL };

static const struct page {
	const char *title;
	const char *first; // its first row; NULL for Status
} pages[] = {
	{ "Status", NULL },
	{ "Output", "master_volume" },
	{ "Record", "capture_source" },
	{ "DACs", "audio2_oversampling" },
	{ "Mono", "mono_out" },
	{ "3-D", "spatial_switch" },
	{ "Buttons", "hwvol_split" },
	{ "Power", "gpo0" },
	{ "DMA", "dma_single" },
	{ "Rate", "a2_resample" },
	{ "Limits", "a1p_max_rate" },
	{ "Devices", "fmdig_device" },
	{ "FM", "opl3" },
};

#define NPAGES ((int)(sizeof(pages) / sizeof(pages[0])))

static const struct label {
	const char *id, *text;
} labels[] = {
	{ "master_switch", "Master on" },
	{ "capture_source", "ADC Input" },
	{ "capture_volume", "ADC Level (Capture)" },
	{ "capture_switch", "Record on" },
	{ "a2_volume", "Audio 2 (PCM)" },
	{ "a1_volume", "Audio 1 (PCM 1)" },
	{ "a2_capture", "Audio 2 to the record mixer" },
	{ "mic_capture", "Mic to the record mixer" },
	{ "line_capture", "Line to the record mixer" },
	{ "fm_capture", "FM to the record mixer" },
	{ "cd_capture", "CD to the record mixer" },
	{ "aux_capture", "Aux to the record mixer" },
	{ "mono_capture", "Mono to the record mixer" },
};

// what feeds the ADC, for Capture Source's values
static const char *const adc_what[8] = {
	"the microphone",
	"left the mic, right the master's inputs (L+R)/2",
	"Aux A, the CD input",
	"AOUT, what the output jack gets",
	"the microphone, the data sheet's second value for it",
	"the record mixer",
	"line in",
	"the 3-D output, before the master",
};

struct value {
	long v[2];
	char text[64];
	int err;
};

struct line {
	char text[240];
	int row; // its setting, or -1
	int bold;
};

static const char *root = "";
static snd_ctl_t *ctl;
static struct line lines[MAX_LINES];
static int nlines;
static int page, sel, top;
static char message[160];
static char proc_text[32768];
static time_t proc_time;
static unsigned long irq_last[4];
static double irq_rate[4];
static time_t irq_time;

// --- files ---------------------------------------------------------------

static int read_file(const char *path, char *buf, size_t size)
{
	char full[512];
	size_t used = 0;
	int fd;

	snprintf(full, sizeof(full), "%s%s", root, path);
	fd = open(full, O_RDONLY);
	if (fd < 0)
		return -errno;
	while (used + 1 < size) {
		ssize_t n = read(fd, buf + used, size - 1 - used);

		if (n < 0) {
			int err = -errno;

			close(fd);
			return err;
		}
		if (!n)
			break;
		used += n;
	}
	close(fd);
	buf[used] = 0;
	return (int)used;
}

// the text after "key:" in a proc file's lines, to the end of its line
static int field(const char *text, const char *key, char *out, size_t size)
{
	size_t len = strlen(key);
	const char *p = text;

	while (p && *p) {
		if (!strncmp(p, key, len) && p[len] == ':') {
			size_t n = 0;

			p += len + 1;
			while (*p == ' ')
				p++;
			while (p[n] && p[n] != '\n' && n + 1 < size) {
				out[n] = p[n];
				n++;
			}
			out[n] = 0;
			return 0;
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return -ENOENT;
}

// --- the settings --------------------------------------------------------

static const struct et_setting *setting(const char *id)
{
	int i;

	for (i = 0; i < et_nsettings; i++)
		if (!strcmp(et_settings[i].id, id))
			return &et_settings[i];
	return NULL;
}

static int is_locked(const struct et_setting *s)
{
	int i;

	for (i = 0; locked[i]; i++)
		if (!strcmp(s->id, locked[i]))
			return 1;
	return 0;
}

static int enum_count(const struct et_setting *s)
{
	const char *p;
	int n = 1;

	for (p = s->items; *p; p++)
		n += *p == '|';
	return n;
}

static void enum_item(const struct et_setting *s, int item, char *out,
		      size_t size)
{
	const char *p = s->items, *end;

	while (item-- > 0 && (p = strchr(p, '|')))
		p++;
	if (!p) {
		snprintf(out, size, "?");
		return;
	}
	end = strchr(p, '|');
	snprintf(out, size, "%.*s", (int)(end ? (size_t)(end - p) : strlen(p)),
		 p);
}

// the mixer interface first, then the card's, where the driver keeps what
// alsamixer doesn't show
static int ctl_find(const struct et_setting *s, snd_ctl_elem_id_t *id,
		    snd_ctl_elem_info_t *info)
{
	int err;

	if (!ctl || !(s->flags & F_CTL))
		return -ENOENT;
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, s->ctl);
	snd_ctl_elem_id_set_index(id, s->index);
	snd_ctl_elem_info_set_id(info, id);
	err = snd_ctl_elem_info(ctl, info);
	if (err != -ENOENT)
		return err;
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_CARD);
	snd_ctl_elem_info_set_id(info, id);
	return snd_ctl_elem_info(ctl, info);
}

static int ctl_get(const struct et_setting *s, struct value *out)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	int err, ch, count;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	err = ctl_find(s, id, info);
	if (err < 0)
		return err;
	snd_ctl_elem_value_set_id(val, id);
	err = snd_ctl_elem_read(ctl, val);
	if (err < 0)
		return err;
	count = snd_ctl_elem_info_get_count(info);
	for (ch = 0; ch < 2; ch++) {
		int c = ch < count ? ch : 0;

		switch (snd_ctl_elem_info_get_type(info)) {
		case SND_CTL_ELEM_TYPE_BOOLEAN:
			out->v[ch] = snd_ctl_elem_value_get_boolean(val, c);
			break;
		case SND_CTL_ELEM_TYPE_ENUMERATED:
			out->v[ch] = snd_ctl_elem_value_get_enumerated(val, c);
			break;
		default:
			out->v[ch] = snd_ctl_elem_value_get_integer(val, c);
			break;
		}
	}
	return 0;
}

static int ctl_set(const struct et_setting *s, const long v[2])
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	int err, ch, count;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	err = ctl_find(s, id, info);
	if (err < 0)
		return err;
	snd_ctl_elem_value_set_id(val, id);
	count = snd_ctl_elem_info_get_count(info);
	for (ch = 0; ch < count && ch < 2; ch++) {
		switch (snd_ctl_elem_info_get_type(info)) {
		case SND_CTL_ELEM_TYPE_BOOLEAN:
			snd_ctl_elem_value_set_boolean(val, ch, v[ch]);
			break;
		case SND_CTL_ELEM_TYPE_ENUMERATED:
			snd_ctl_elem_value_set_enumerated(val, ch, v[ch]);
			break;
		default:
			snd_ctl_elem_value_set_integer(val, ch, v[ch]);
			break;
		}
	}
	return snd_ctl_elem_write(ctl, val);
}

static int param_get(const struct et_setting *s, struct value *out)
{
	char path[128], buf[128], *end;
	int err;

	snprintf(path, sizeof(path), PARAMS "/%s", s->id);
	err = read_file(path, buf, sizeof(buf));
	if (err < 0)
		return err;
	buf[strcspn(buf, "\n")] = 0;
	if (s->kind == S_STR) {
		snprintf(out->text, sizeof(out->text), "%.63s", buf);
		return 0;
	}
	out->v[0] = strtol(buf, &end, 0);
	out->v[1] = *end == ',' ? strtol(end + 1, NULL, 0) : out->v[0];
	return 0;
}

static int param_set(const struct et_setting *s, const long v[2],
		     const char *text)
{
	char path[512], buf[128];
	int fd, n, err = 0;

	snprintf(path, sizeof(path), "%s" PARAMS "/%s", root, s->id);
	if (s->kind == S_STR)
		n = snprintf(buf, sizeof(buf), "%s", text);
	else if (et_setting_count(s) == 2)
		n = snprintf(buf, sizeof(buf), "%ld,%ld", v[0], v[1]);
	else
		n = snprintf(buf, sizeof(buf), "%ld", v[0]);
	fd = open(path, O_WRONLY | (*root ? O_TRUNC : 0));
	if (fd < 0)
		return -errno;
	if (write(fd, buf, n) != n)
		err = -errno;
	if (close(fd) < 0 && !err)
		err = -errno;
	return err;
}

static void get_value(const struct et_setting *s, struct value *out)
{
	memset(out, 0, sizeof(*out));
	out->err = ctl_get(s, out);
	if (out->err < 0)
		out->err = param_get(s, out);
}

static long val(const char *id, int ch)
{
	const struct et_setting *s = setting(id);
	struct value v;

	if (!s)
		return 0;
	get_value(s, &v);
	return v.err < 0 ? 0 : v.v[ch];
}

static int set_value(const struct et_setting *s, const long v[2],
		     const char *text)
{
	int err;

	if (is_locked(s))
		return -EPERM;
	if (s->kind != S_STR) {
		err = ctl_set(s, v);
		if (err != -ENOENT)
			return err;
	}
	return param_set(s, v, text);
}

// a value typed in: "on" or "off", an enum's text or number, "l,r" or one
// number for both sides
static int parse_value(const struct et_setting *s, const char *text, long v[2])
{
	char *end;
	int i, n;

	if (s->kind == S_STR)
		return 0;
	if (s->kind == S_BOOL || s->kind == S_MASTERSW) {
		if (!strcasecmp(text, "on") || !strcmp(text, "1") ||
		    !strcasecmp(text, "y"))
			v[0] = v[1] = 1;
		else if (!strcasecmp(text, "off") || !strcmp(text, "0") ||
			 !strcasecmp(text, "n"))
			v[0] = v[1] = 0;
		else
			return -EINVAL;
		return 0;
	}
	if (s->kind == S_ENUM) {
		n = enum_count(s);
		for (i = 0; i < n; i++) {
			char item[48];

			enum_item(s, i, item, sizeof(item));
			if (!strcasecmp(item, text)) {
				v[0] = v[1] = i;
				return 0;
			}
		}
	}
	v[0] = strtol(text, &end, 0);
	if (end == text)
		return -EINVAL;
	v[1] = v[0];
	if (*end == ',')
		v[1] = strtol(end + 1, &end, 0);
	if (*end)
		return -EINVAL;
	for (i = 0; i < 2; i++)
		if (v[i] < s->min || v[i] > s->max)
			return -ERANGE;
	return 0;
}

// --- text ----------------------------------------------------------------

static void label_of(const struct et_setting *s, char *out, size_t size)
{
	static const char *const ends[] = {
		" Playback Volume", " Playback Switch", " Capture Volume",
		" Capture Switch",  " Capture Enum",	" Switch",
	};
	unsigned int i;
	size_t n;

	for (i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
		if (!strcmp(labels[i].id, s->id)) {
			snprintf(out, size, "%s", labels[i].text);
			return;
		}
	if (!(s->flags & F_CTL)) {
		snprintf(out, size, "%s", s->id);
		return;
	}
	snprintf(out, size, "%s", s->ctl);
	n = strlen(out);
	for (i = 0; i < sizeof(ends) / sizeof(ends[0]); i++) {
		size_t e = strlen(ends[i]);

		if (n > e && !strcmp(out + n - e, ends[i])) {
			out[n - e] = 0;
			return;
		}
	}
}

// where alsamixer shows a control, by the rules of alsa-lib's simple mixer
static const char *view_of(const struct et_setting *s)
{
	const char *n = s->ctl;
	size_t len = strlen(n);

#define ENDS(x) (len >= sizeof(x) - 1 && !strcmp(n + len - (sizeof(x) - 1), x))
	if (!strcmp(n, "Capture Source"))
		return "F4 as ADC Input, or CAPTURE on each source";
	if (ENDS("Capture Volume") || ENDS("Capture Switch"))
		return "F4";
	if (s->kind == S_INT && !ENDS(" Playback Volume") && !ENDS(" Volume"))
		return "F3 and F4";
	if (s->kind == S_SMAG)
		return "F3 and F4";
	return "F3";
#undef ENDS
}

static void when_of(const struct et_setting *s, char *out, size_t size)
{
	if (s->flags & F_IDLE)
		snprintf(out, size, "refused while Audio 2 is open");
	else if (s->flags & F_IDLE_A1)
		snprintf(out, size, "refused while Audio 1 is open");
	else if (s->flags & F_NEXT)
		snprintf(out, size, "takes effect at the next stream");
	else if (s->flags & F_OPEN)
		snprintf(out, size, "takes effect at the next open");
	else
		snprintf(out, size, "takes effect at once");
}

static void bar(char *out, long v, long min, long max)
{
	long n = max > min ? (v - min) * BAR / (max - min) : 0;
	int i;

	for (i = 0; i < BAR; i++)
		out[i] = i < n ? '#' : '.';
	out[BAR] = 0;
}

static void db_text(const struct et_setting *s, long v, char *out, size_t size)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	long cdb;

	out[0] = 0;
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	if (s->tlv == T_NONE || ctl_find(s, id, info) < 0 ||
	    snd_ctl_convert_to_dB(ctl, id, v, &cdb) < 0)
		return;
	if (cdb == SND_CTL_TLV_DB_GAIN_MUTE)
		snprintf(out, size, "mute");
	else
		snprintf(out, size, "%+.1f dB", cdb / 100.0);
}

static void row_text(const struct et_setting *s, char *out, size_t size)
{
	char name[48], item[48], b[BAR + 1], db[24], text[100];
	struct value v;
	int two = et_setting_count(s) == 2;

	get_value(s, &v);
	label_of(s, name, sizeof(name));
	if (v.err < 0) {
		snprintf(out, size, "%-34.47s not in this driver", name);
		return;
	}
	switch (s->kind) {
	case S_BOOL:
		snprintf(text, sizeof(text), "[%s]", v.v[0] ? "on" : "off");
		break;
	case S_MASTERSW:
		snprintf(text, sizeof(text), "[%s]",
			 v.v[0] && v.v[1] ? "on" :
			 v.v[0]		  ? "left on" :
			 v.v[1]		  ? "right on" :
					    "off");
		break;
	case S_ENUM:
		enum_item(s, v.v[0], item, sizeof(item));
		snprintf(text, sizeof(text), "< %s >", item);
		break;
	case S_STR:
		snprintf(text, sizeof(text), "%s", v.text);
		break;
	default:
		bar(b, v.v[0], s->min, s->max);
		db_text(s, v.v[0], db, sizeof(db));
		if (two)
			snprintf(text, sizeof(text), "[%s] %ld,%ld %s", b,
				 v.v[0], v.v[1], db);
		else
			snprintf(text, sizeof(text), "[%s] %ld %s", b, v.v[0],
				 db);
		break;
	}
	snprintf(out, size, "%-34.47s %.99s%s", name, text,
		 s->kind != S_STR && (v.v[0] != s->def ||
				      (two && v.v[1] != s->def)) ?
			 " *" :
			 "");
}

// --- the pages -----------------------------------------------------------

static void add(int row, int bold, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

static void add(int row, int bold, const char *fmt, ...)
{
	struct line *l;
	va_list ap;

	if (nlines >= MAX_LINES)
		return;
	l = &lines[nlines++];
	l->row = row;
	l->bold = bold;
	va_start(ap, fmt);
	vsnprintf(l->text, sizeof(l->text), fmt, ap);
	va_end(ap);
}

// a stream's line, from its proc files
static void stream(const char *title, const char *dir)
{
	char path[128], hw[1024], st[1024], format[32], ch[16], rate[32],
		period[16];
	char buffer[16], state[32], pid[16], comm[64], note[64];
	long r;

	snprintf(path, sizeof(path), PROC "/%s/hw_params", dir);
	if (read_file(path, hw, sizeof(hw)) < 0 || !strncmp(hw, "closed", 6)) {
		add(-1, 0, "  %-11s closed", title);
		return;
	}
	if (field(hw, "format", format, sizeof(format)) ||
	    field(hw, "channels", ch, sizeof(ch)) ||
	    field(hw, "rate", rate, sizeof(rate)) ||
	    field(hw, "period_size", period, sizeof(period)) ||
	    field(hw, "buffer_size", buffer, sizeof(buffer))) {
		add(-1, 0, "  %-11s open, not set up yet", title);
		return;
	}
	r = strtol(rate, NULL, 10);
	note[0] = 0;
	// the chip's two families, and the FM's own clock (docs/LINUX.md)
	if (r == 49716)
		snprintf(note, sizeof(note), ", really %.1f Hz",
			 49716 * 1.00208182);
	else if (r && 768000 % r == 0)
		snprintf(note, sizeof(note), ", really %.2f Hz",
			 r * (768193.548 / 768000));
	snprintf(path, sizeof(path), PROC "/%s/status", dir);
	st[0] = 0;
	read_file(path, st, sizeof(st));
	if (field(st, "state", state, sizeof(state)))
		snprintf(state, sizeof(state), "open");
	comm[0] = 0;
	if (!field(st, "owner_pid", pid, sizeof(pid))) {
		char cpath[64];

		snprintf(cpath, sizeof(cpath), "/proc/%s/comm", pid);
		if (read_file(cpath, comm, sizeof(comm)) > 0)
			comm[strcspn(comm, "\n")] = 0;
	}
	add(-1, 0, "  %-11s %s %s, %s ch, %ld Hz%s, period %s of %s %s", title,
	    state, format, ch, r, note, period, buffer, comm);
}

static void routing(void)
{
	static const struct {
		const char *id, *name;
	} rec[] = {
		{ "mic_capture", "Mic" },   { "line_capture", "Line" },
		{ "cd_capture", "CD" },	    { "aux_capture", "Aux" },
		{ "fm_capture", "FM" },	    { "a2_capture", "Audio 2" },
		{ "mono_capture", "Mono" },
	};
	static const struct {
		const char *id, *name;
	} out[] = {
		{ "a2_volume", "Audio 2" }, { "a1_volume", "Audio 1" },
		{ "fm_volume", "FM" },	    { "line_volume", "Line" },
		{ "cd_volume", "CD" },	    { "aux_volume", "Aux" },
		{ "mic_volume", "Mic" },    { "mono_volume", "Mono" },
	};
	const struct et_setting *cv = setting("capture_volume");
	long src = val("capture_source", 0) & 7, off = val("adc_offset", 0);
	char item[32], db[24], more[160];
	size_t used = 0;
	unsigned int i;

	enum_item(setting("capture_source"), src, item, sizeof(item));
	add(-1, 1, "Routing");
	add(-1, 0, "  ADC input   %s: %s", item, adc_what[src]);
	if (src == 5) {
		more[0] = 0;
		for (i = 0; i < sizeof(rec) / sizeof(rec[0]); i++)
			if (val(rec[i].id, 0) || val(rec[i].id, 1))
				used += snprintf(more + used,
						 sizeof(more) - used, "%s %s",
						 used ? "," : "", rec[i].name);
		add(-1, 0, "  Into it    %s",
		    used ? more : " nothing: every level is 0");
	}
	db_text(cv, val("capture_volume", 0), db, sizeof(db));
	add(-1, 0, "  ADC level   %ld,%ld%s%s%s%s, boost %s%s",
	    val("capture_volume", 0), val("capture_volume", 1), *db ? " " : "",
	    db, src == 0 || src == 4 ? " and 6 dB on the mic's path" : "",
	    val("capture_switch", 0) ? "" : ", record muted",
	    val("mic_boost", 0) ? "on" : "off",
	    val("record_monitor", 0) ? ", monitor on" : "");
	if (off || val("adc_offset", 1))
		add(-1, 0,
		    "  DC offset   %ld,%ld: recordings carry %ld,%ld of DC",
		    off, val("adc_offset", 1), off * 64,
		    val("adc_offset", 1) * 64);
	used = 0;
	more[0] = 0;
	for (i = 0; i < sizeof(out) / sizeof(out[0]); i++)
		if (val(out[i].id, 0) || val(out[i].id, 1))
			used += snprintf(more + used, sizeof(more) - used,
					 "%s %s", used ? "," : "", out[i].name);
	db_text(setting("master_volume"), val("master_volume", 0), db,
		sizeof(db));
	add(-1, 0, "  Output      Master%s%s%s,%s", *db ? " " : "", db,
	    val("master_switch", 0) || val("master_switch", 1) ? "" : " muted",
	    used ? more : " nothing up");
	if (val("spatial_switch", 0))
		add(-1, 0, "  3-D         on, level %ld",
		    val("spatial_level", 0));
}

static void status_page(void)
{
	static const char *const keys[] = { "interrupts",	"Audio 2 DAC",
					    "DSP busy",		"stuck sources",
					    "Audio 2 ring",	"clock" };
	time_t now = time(NULL);
	unsigned long irq[4];
	unsigned int i;
	const char *p, *q;

	if (!proc_time || now - proc_time >= 2) {
		if (read_file(PROC "/es1869", proc_text, sizeof(proc_text)) < 0)
			snprintf(proc_text, sizeof(proc_text),
				 "the driver's status can't be read\n");
		proc_time = now;
	}
	add(-1, 1, "Streams");
	stream("Audio 2", "pcm0p/sub0");
	stream("Audio 1", "pcm0p/sub1");
	stream("Recording", "pcm0c/sub0");
	stream("FM digital", "pcm1c/sub0");
	stream("Dual", "pcm2p/sub0");
	add(-1, 0, " ");
	routing();
	add(-1, 0, " ");
	add(-1, 1, "Driver");
	// the four lines of what it found
	p = proc_text;
	for (i = 0; i < 4 && p; i++) {
		const char *e = strchr(p, '\n');

		add(-1, 0, "  %.*s", e ? (int)(e - p) : (int)strlen(p), p);
		p = e ? e + 1 : NULL;
	}
	q = strstr(proc_text, "interrupts:");
	if (q &&
	    sscanf(q,
		   "interrupts: Audio 1 %lu, Audio 2 %lu, buttons %lu, MPU-401 %lu",
		   &irq[0], &irq[1], &irq[2], &irq[3]) == 4) {
		if (irq_time && now > irq_time)
			for (i = 0; i < 4; i++)
				irq_rate[i] = (double)(irq[i] - irq_last[i]) /
					      (now - irq_time);
		if (now > irq_time) {
			memcpy(irq_last, irq, sizeof(irq));
			irq_time = now;
		}
		add(-1, 0,
		    "  Interrupts a second: Audio 1 %.0f, Audio 2 %.0f, buttons %.0f, MPU-401 %.0f",
		    irq_rate[0], irq_rate[1], irq_rate[2], irq_rate[3]);
	}
	for (p = proc_text; p && *p;) {
		const char *e = strchr(p, '\n');

		for (i = 1; i < sizeof(keys) / sizeof(keys[0]); i++)
			if (!strncmp(p, keys[i], strlen(keys[i])))
				add(-1, 0, "  %.*s",
				    e ? (int)(e - p) : (int)strlen(p), p);
		p = e ? e + 1 : NULL;
	}
}

static int row_index(const char *id)
{
	int i;

	for (i = 0; i < et_nsettings; i++)
		if (!strcmp(et_settings[i].id, id))
			return i;
	return -1;
}

// a page's rows: from its first to the next page's
static void settings_page(int n)
{
	int first = row_index(pages[n].first), end, i;

	end = n + 1 < NPAGES ? row_index(pages[n + 1].first) : et_nsettings;
	for (i = first; i >= 0 && i < end; i++) {
		char text[200];

		row_text(&et_settings[i], text, sizeof(text));
		add(i, 0, "%s", text);
	}
}

static void build(int n)
{
	nlines = 0;
	if (pages[n].first)
		settings_page(n);
	else
		status_page();
}

// --- the screen ----------------------------------------------------------

// text at y, wrapped at the screen's width on a space, on two lines at most
static void wrapped(int y, const char *text)
{
	int w = COLS > 2 ? COLS - 2 : 1, n = strlen(text), cut = n;

	if (n > w) {
		cut = w;
		while (cut > w / 2 && text[cut] != ' ')
			cut--;
		if (text[cut] != ' ')
			cut = w;
	}
	mvprintw(y, 1, "%.*s", cut, text);
	if (n > cut)
		mvprintw(y + 1, 1, "%.*s", w, text + cut + (text[cut] == ' '));
}

static void info_lines(const struct et_setting *s, int y)
{
	char when[64], text[240];

	when_of(s, when, sizeof(when));
	wrapped(y, s->desc);
	if (is_locked(s))
		snprintf(
			text, sizeof(text),
			"the panel won't write this: a module parameter, if you mean it");
	else if (s->flags & F_CTL)
		snprintf(text, sizeof(text), "\"%s\", alsamixer %s; %s", s->ctl,
			 view_of(s), when);
	else
		snprintf(text, sizeof(text), "parameter only, needs root; %s",
			 when);
	mvprintw(y + 2, 1, "%.*s", COLS > 2 ? COLS - 2 : 1, text);
}

static int rows_on_page(void)
{
	int i, n = 0;

	for (i = 0; i < nlines; i++)
		n += lines[i].row >= 0;
	return n;
}

static void draw(void)
{
	int i, x = 1, y, body = LINES - 7;
	int sel_line = -1, seen = 0;

	erase();
	for (i = 0; i < NPAGES; i++) {
		if (i == page)
			attron(A_REVERSE);
		mvprintw(0, x, "%s", pages[i].title);
		attroff(A_REVERSE);
		x += strlen(pages[i].title) + 1;
	}
	mvhline(1, 0, ACS_HLINE, COLS);
	build(page);
	for (i = 0; i < nlines; i++)
		if (lines[i].row >= 0 && seen++ == sel)
			sel_line = i;
	if (sel_line >= 0) {
		if (sel_line < top)
			top = sel_line;
		if (sel_line >= top + body)
			top = sel_line - body + 1;
	} else if (top > nlines - body) {
		top = nlines > body ? nlines - body : 0;
	}
	for (i = top, y = 2; i < nlines && y < 2 + body; i++, y++) {
		int attr = i == sel_line ? A_REVERSE :
			   lines[i].bold ? A_BOLD :
					   0;

		attron(attr);
		mvprintw(y, 1, "%.*s", COLS > 2 ? COLS - 2 : 1, lines[i].text);
		attroff(attr);
	}
	mvhline(LINES - 5, 0, ACS_HLINE, COLS);
	if (sel_line >= 0)
		info_lines(&et_settings[lines[sel_line].row], LINES - 4);
	else
		wrapped(LINES - 4,
			"Up and down scroll this page. On the other pages they choose a "
			"setting, and left and right change it.");
	if (message[0]) {
		attron(A_BOLD);
		mvprintw(LINES - 1, 1, "%.*s", COLS > 2 ? COLS - 2 : 1,
			 message);
		attroff(A_BOLD);
	} else {
		mvprintw(
			LINES - 1, 1, "%.*s", COLS > 2 ? COLS - 2 : 1,
			"Tab page  arrows  Space toggle  Enter type  d default  s save  ? help  q quit");
	}
	refresh();
}

static void help(void)
{
	static const char *const text[] = {
		"es1869ctl: the ES1869's status and settings",
		"",
		"Tab, Shift-Tab      next and previous page",
		"1 to 9              jump to a page",
		"Up, Down            choose a setting",
		"Left, Right         change it by a step, or to the next choice",
		"PgUp, PgDn          change it by a bigger step",
		"Home, End           its lowest and highest value",
		"Space               turn a switch on or off",
		"Enter               type a value: l,r for the two sides of a level",
		"d                   back to the default",
		"s                   save the mixer as alsactl does (needs root)",
		"q                   quit",
		"",
		"A * marks a value that isn't the driver's default.",
		"The bottom lines say what the chosen setting does, where alsamixer",
		"shows it, and when it takes effect. A change made in alsamixer or",
		"with the volume buttons shows here at once.",
		"",
		"Any key goes back.",
	};
	unsigned int i;

	erase();
	for (i = 0; i < sizeof(text) / sizeof(text[0]); i++)
		mvprintw(i + 1, 2, "%s", text[i]);
	refresh();
	timeout(-1);
	getch();
	timeout(0);
}

static void report(const struct et_setting *s, int err)
{
	char name[48];

	label_of(s, name, sizeof(name));
	if (!err)
		message[0] = 0;
	else if (err == -EBUSY)
		snprintf(message, sizeof(message),
			 "%s can't change while the engine is open", name);
	else if (err == -EACCES)
		snprintf(
			message, sizeof(message),
			"%s is a parameter only: run this as root to change it",
			name);
	else if (err == -EPERM)
		snprintf(message, sizeof(message), "the panel won't write %s",
			 name);
	else
		snprintf(message, sizeof(message), "%s: %s", name,
			 strerror(-err));
}

static const struct et_setting *selected(void)
{
	int i, seen = 0;

	for (i = 0; i < nlines; i++)
		if (lines[i].row >= 0 && seen++ == sel)
			return &et_settings[lines[i].row];
	return NULL;
}

static void step(const struct et_setting *s, long by)
{
	struct value v;
	int ch, count = et_setting_count(s);
	long n[2];

	get_value(s, &v);
	if (v.err < 0)
		return;
	if (s->kind == S_BOOL || s->kind == S_MASTERSW) {
		n[0] = n[1] = !(v.v[0] || v.v[1]);
	} else if (s->kind == S_STR) {
		return;
	} else {
		long lo = s->min,
		     hi = s->kind == S_ENUM ? enum_count(s) - 1 : s->max;

		for (ch = 0; ch < 2; ch++) {
			n[ch] = v.v[ch < count ? ch : 0] + by;
			if (s->kind == S_ENUM)
				n[ch] = n[ch] > hi ? lo :
					n[ch] < lo ? hi :
						     n[ch];
			else
				n[ch] = n[ch] > hi ? hi :
					n[ch] < lo ? lo :
						     n[ch];
		}
	}
	report(s, set_value(s, n, ""));
}

static void set_to(const struct et_setting *s, long to)
{
	long n[2] = { to, to };

	if (s->kind != S_STR)
		report(s, set_value(s, n, ""));
}

static void type_value(const struct et_setting *s)
{
	char buf[64], name[48];
	long v[2] = { 0, 0 };
	int err;

	label_of(s, name, sizeof(name));
	move(LINES - 1, 0);
	clrtoeol();
	mvprintw(LINES - 1, 1, "%s: ", name);
	timeout(-1);
	echo();
	curs_set(1);
	buf[0] = 0;
	getnstr(buf, sizeof(buf) - 1);
	noecho();
	curs_set(0);
	timeout(0);
	if (!buf[0])
		return;
	err = parse_value(s, buf, v);
	report(s, err ? err : set_value(s, v, buf));
}

static void save(void)
{
	int err = system("alsactl store ES1869 >/dev/null 2>&1");

	snprintf(message, sizeof(message), "%s",
		 err ? "alsactl store failed: it needs root" :
		       "the mixer is saved");
}

static int key(int k)
{
	const struct et_setting *s = selected();
	long big = s ? (s->max - s->min) / 16 : 1;
	int n = rows_on_page();

	if (big < 1)
		big = 1;
	if (k != ERR)
		message[0] = 0;
	switch (k) {
	case 'q':
	case 'Q':
	case 27:
		return 0;
	case '\t':
		page = (page + 1) % NPAGES;
		sel = top = 0;
		break;
	case KEY_BTAB:
		page = (page + NPAGES - 1) % NPAGES;
		sel = top = 0;
		break;
	case KEY_UP:
	case 'k':
		if (sel > 0)
			sel--;
		else if (!n && top > 0)
			top--;
		break;
	case KEY_DOWN:
	case 'j':
		if (sel + 1 < n)
			sel++;
		else if (!n)
			top++;
		break;
	case KEY_LEFT:
	case 'h':
		if (s)
			step(s, -1);
		break;
	case KEY_RIGHT:
	case 'l':
		if (s)
			step(s, 1);
		break;
	case KEY_PPAGE:
		if (s)
			step(s, big);
		break;
	case KEY_NPAGE:
		if (s)
			step(s, -big);
		break;
	case KEY_HOME:
		if (s)
			set_to(s, s->min);
		break;
	case KEY_END:
		if (s)
			set_to(s,
			       s->kind == S_ENUM ? enum_count(s) - 1 : s->max);
		break;
	case ' ':
		if (s && (s->kind == S_BOOL || s->kind == S_MASTERSW))
			step(s, 1);
		break;
	case '\n':
	case KEY_ENTER:
		if (s)
			type_value(s);
		break;
	case 'd':
		if (s)
			set_to(s, s->def);
		break;
	case 's':
		save();
		break;
	case '?':
	case KEY_F(1):
		help();
		break;
	default:
		if (k >= '1' && k <= '9' && k - '1' < NPAGES) {
			page = k - '1';
			sel = top = 0;
		}
		break;
	}
	return 1;
}

static void panel(void)
{
	struct pollfd fds[8];
	int nfds = 1, running = 1;

	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	set_escdelay(25);
	timeout(0);
	fds[0].fd = 0;
	fds[0].events = POLLIN;
	if (ctl) {
		snd_ctl_subscribe_events(ctl, 1);
		nfds += snd_ctl_poll_descriptors(ctl, fds + 1, 7);
	}
	while (running) {
		int k;

		draw();
		poll(fds, nfds, 1000);
		if (ctl) {
			snd_ctl_event_t *ev;

			snd_ctl_event_alloca(&ev);
			while (snd_ctl_read(ctl, ev) > 0)
				;
		}
		while (running && (k = getch()) != ERR)
			running = key(k);
	}
	endwin();
}

// --- the rest ------------------------------------------------------------

static void dump(void)
{
	int n, i;

	for (n = 0; n < NPAGES; n++) {
		printf("== %s\n", pages[n].title);
		build(n);
		for (i = 0; i < nlines; i++)
			printf("%s\n", lines[i].text);
		printf("\n");
	}
}

static int set_one(const char *arg)
{
	const char *eq = strchr(arg, '=');
	const struct et_setting *s;
	char id[64];
	long v[2] = { 0, 0 };
	int err;

	if (!eq) {
		fprintf(stderr, "es1869ctl: %s: not ID=VALUE\n", arg);
		return 1;
	}
	snprintf(id, sizeof(id), "%.*s", (int)(eq - arg), arg);
	s = setting(id);
	if (!s) {
		fprintf(stderr, "es1869ctl: no setting %s\n", id);
		return 1;
	}
	err = parse_value(s, eq + 1, v);
	if (!err)
		err = set_value(s, v, eq + 1);
	if (err) {
		report(s, err);
		fprintf(stderr, "es1869ctl: %s\n", message);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "dump", 0, NULL, 'd' }, { "set", 1, NULL, 's' },
		{ "root", 1, NULL, 'r' }, { "version", 0, NULL, 'V' },
		{ "help", 0, NULL, 'h' }, { NULL, 0, NULL, 0 },
	};
	const char *sets[32];
	int nsets = 0, want_dump = 0, opt, i, bad = 0;

	while ((opt = getopt_long(argc, argv, "", opts, NULL)) != -1) {
		switch (opt) {
		case 'd':
			want_dump = 1;
			break;
		case 's':
			if (nsets < 32)
				sets[nsets++] = optarg;
			break;
		case 'r':
			root = optarg;
			break;
		case 'V':
			printf("es1869ctl %s\n", VERSION);
			return 0;
		default:
			fprintf(stderr,
				"usage: es1869ctl [--dump] [--set ID=VALUE]... [--root DIR]\n");
			return opt == 'h' ? 0 : 1;
		}
	}
	if (!*root && snd_ctl_open(&ctl, CARD, SND_CTL_NONBLOCK) < 0) {
		fprintf(stderr,
			"es1869ctl: no ES1869 card: is snd-es1869 loaded?\n");
		return 2;
	}
	for (i = 0; i < nsets; i++)
		bad |= set_one(sets[i]);
	if (want_dump)
		dump();
	else if (!nsets)
		panel();
	if (ctl)
		snd_ctl_close(ctl);
	return bad;
}
