// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869test: the loopback test suite of snd-es1869 (docs/ES1869TEST.md).
 *
 * usage: es1869test [-c CASES] [-x CASES] [-q] [-l] [-v] [-o DIR]
 *                   [--rec2 DEVICE] [--rec2-left] [--max-out DB] [-i]
 *                   [--destructive]
 *                   [--reload CMD] [--baseline TSV] [--stock] [--sim[=FAULTS]]
 *                   [--restore] [--self-test] [--version]
 *
 * Notes:
 *
 * Before it changes anything, the suite takes its lock, applies a journal
 * that a cut-off run left, checks that snd-es1869 drives the card, that
 * MPD isn't playing, that nothing holds the card's PCM devices, and that
 * no recording would hang the machine: an Intel PIIX4 with Type F timing on
 * the card's capture channel, which snd-es1869 turns off at load and
 * snd-es18xx doesn't. Every control and parameter it changes goes to the
 * journal first, and the end of each case, or a signal, puts them back.
 * Each case gets the driver's new kernel log lines, and an error among
 * them, or a warning of a fault anywhere, fails it.
 *
 * --baseline TSV compares every measurement with an earlier run's, and
 * reports the ones that moved further than a repeat run moves them.
 *
 * Exit codes: 0 every case passed, 1 a bad option, 2 no driver, 3 busy, 4
 * root needed, 5 the restore failed, 6 cases failed, 7 a signal, 8 the
 * self-test failed, 9 a recording would hang the machine.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "an.h"
#include "hal.h"
#include "run.h"

#define JOURNAL_DIR "/var/lib/es1869test"

int et_fm_note(const char *spec);
int et_gmcheck_write(const char *path);

static struct et_hal *hal;
static const char *only, *except;
static int quick, inter, destructive;

static void usage(void)
{
	fprintf(stderr,
		"usage: es1869test [-c CASES] [-x CASES] [-q] [-l] [-v] [-o DIR]\n"
		"                  [--rec2 DEVICE] [--rec2-left] [--max-out DB] [-i]\n"
		"                  [--destructive]\n"
		"                  [--reload CMD] [--baseline TSV] [--stock] [--sim[=FAULTS]]\n"
		"                  [--restore] [--self-test] [--version]\n"
		"CASES is a comma list of case ids or their groups (a2, dig.fmt).\n");
}

// whether id is in a comma list of ids and groups
static int listed(const char *list, const char *id)
{
	const char *p = list;

	while (p && *p) {
		const char *e = strchr(p, ',');
		size_t n = e ? (size_t)(e - p) : strlen(p);

		if (!strncmp(id, p, n) && (id[n] == 0 || id[n] == '.'))
			return 1;
		p = e ? e + 1 : NULL;
	}
	return 0;
}

static int selected(const struct et_case *k)
{
	if (except && listed(except, k->id))
		return 0;
	if (only && !listed(only, k->id))
		return 0;
	if ((k->flags & ET_OPTIN) && !only)
		return 0;
	if (quick && !(k->flags & ET_QUICK))
		return 0;
	return 1;
}

// why a selected case can't run here, or NULL
static const char *unmet(struct et_ctx *c, const struct et_case *k)
{
	if ((k->flags & (ET_SECOND | ET_CABLE)) && !c->have_rec2)
		return "no second card that hears the output";
	if ((k->flags & ET_LINEAR) && !c->rec2_linear)
		return "the second card's input isn't linear (cal.linear)";
	if ((k->flags & ET_ROOT) && geteuid() && strcmp(hal->name, "simulated"))
		return "needs root";
	if ((k->flags & ET_INTER) && !inter)
		return "needs someone at the machine (-i)";
	if ((k->flags & ET_DESTR) && !destructive)
		return "reloads the driver or suspends (--destructive)";
	if ((k->flags & ET_NEW) && c->stock)
		return "snd-es18xx has no such device, control or parameter";
	if ((k->flags & ET_MIDI) && access("/usr/bin/aplaymidi", X_OK))
		return "no aplaymidi";
	return NULL;
}

static const char *const result_names[] = { "PASS", "FAIL", "KNOWN", "SKIP",
					    "ERROR" };

// the kernel's lines from the case: the driver's, as notes, and a failure
// for an error of the driver's or a fault anywhere
static void klog_check(struct et_ctx *c)
{
	static char buf[8192];
	char *line, *next;
	int n, shown = 0;

	if (!hal->klog)
		return;
	n = hal->klog(hal->ctx, buf, sizeof(buf));
	for (line = buf; n > 0 && *line; line = next) {
		int level = line[0] - '0';
		const char *msg = line + 2;

		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		else
			next = line + strlen(line);
		if (shown++ < 8)
			et_note(c, "kernel: %.200s", msg);
		if (level <= 3 || et_klog_fault(msg))
			et_fail(c, "the kernel logged: %.160s", msg);
	}
	if (shown > 8)
		et_note(c, "kernel: %d more lines", shown - 8);
}

static enum et_result run_case(struct et_ctx *c, const struct et_case *k)
{
	const char *why = unmet(c, k);
	enum et_result r;
	double t0;

	c->k = k;
	c->measures = c->fails = 0;
	c->why[0] = 0;
	et_log("case %s: %s\n", k->id, k->spec);
	if (why) {
		et_log("  SKIP: %s\n", why);
		return ET_SKIP;
	}
	t0 = hal->now(hal->ctx);
	r = k->fn(c, k);
	klog_check(c);
	if ((r == ET_PASS || r == ET_KNOWN) && c->fails)
		r = ET_FAIL;
	// what the case changed goes back, so that the next starts afresh
	if (et_journal_restore(hal) && r != ET_ERROR) {
		snprintf(c->why, sizeof(c->why), "its changes didn't go back");
		r = ET_ERROR;
	}
	et_log("  %s (%.1f s)%s%s\n", result_names[r], hal->now(hal->ctx) - t0,
	       r == ET_FAIL || r == ET_ERROR || r == ET_KNOWN ? ": " : "",
	       r == ET_FAIL || r == ET_ERROR || r == ET_KNOWN ? c->why : "");
	et_report_sync();
	return r;
}

// --- safety ------------------------------------------------------------------

static int lock_fd = -1;

static int take_lock(void)
{
	const char *paths[] = { "/run/lock/es1869test.lock",
				"/tmp/es1869test.lock" };
	unsigned int i;

	for (i = 0; i < 2; i++) {
		lock_fd = open(paths[i], O_RDWR | O_CREAT, 0644);
		if (lock_fd >= 0)
			break;
	}
	if (lock_fd < 0)
		return 0;
	return flock(lock_fd, LOCK_EX | LOCK_NB) == 0;
}

static int mpd_playing(void)
{
	char line[256];
	FILE *p;
	int playing = 0;

	if (access("/usr/bin/mpc", X_OK))
		return 0;
	p = popen("mpc status 2>/dev/null", "r");
	if (!p)
		return 0;
	while (fgets(line, sizeof(line), p))
		if (strstr(line, "[playing]"))
			playing = 1;
	pclose(p);
	return playing;
}

// the processes that hold one of the card's PCM devices, named; the count
static int pcm_holders(int card)
{
	char prefix[32], path[600], link[300], comm[64];
	struct dirent *d;
	DIR *proc = opendir("/proc");
	int n = 0;

	if (!proc)
		return 0;
	snprintf(prefix, sizeof(prefix), "/dev/snd/pcmC%dD", card);
	while ((d = readdir(proc))) {
		struct dirent *e;
		DIR *fds;
		int held = 0;

		if (d->d_name[0] < '0' || d->d_name[0] > '9' ||
		    atoi(d->d_name) == getpid())
			continue;
		snprintf(path, sizeof(path), "/proc/%.64s/fd", d->d_name);
		fds = opendir(path);
		if (!fds)
			continue;
		while (!held && (e = readdir(fds))) {
			ssize_t len;

			snprintf(path, sizeof(path), "/proc/%.64s/fd/%.64s",
				 d->d_name, e->d_name);
			len = readlink(path, link, sizeof(link) - 1);
			if (len > 0) {
				link[len] = 0;
				held = !strncmp(link, prefix, strlen(prefix));
			}
		}
		closedir(fds);
		if (held) {
			FILE *f;

			snprintf(path, sizeof(path), "/proc/%.64s/comm",
				 d->d_name);
			f = fopen(path, "r");
			comm[0] = 0;
			if (f) {
				if (fgets(comm, sizeof(comm), f))
					comm[strcspn(comm, "\n")] = 0;
				fclose(f);
			}
			fprintf(stderr, "es1869test: %s (%s) holds the card\n",
				comm, d->d_name);
			n++;
		}
	}
	closedir(proc);
	return n;
}

// the card's number, from /proc/asound/ES1869 -> cardN
static int card_number(void)
{
	char link[64];
	ssize_t n = readlink("/proc/asound/ES1869", link, sizeof(link) - 1);

	if (n <= 4)
		return -1;
	link[n] = 0;
	return atoi(link + 4);
}

static const char *journal_path(void)
{
	static char path[300];
	const char *home = getenv("HOME");

	if (!mkdir(JOURNAL_DIR, 0755) || errno == EEXIST)
		if (!access(JOURNAL_DIR, W_OK))
			return JOURNAL_DIR "/restore";
	snprintf(path, sizeof(path), "%s/.es1869test.restore",
		 home ? home : "/tmp");
	return path;
}

// signals restore what the run changed, from a thread of their own
static void *signal_thread(void *arg)
{
	sigset_t *set = arg;
	int sig;

	if (sigwait(set, &sig))
		return NULL;
	fprintf(stderr, "\nes1869test: signal %d, restoring\n", sig);
	et_report_sync();
	_exit(et_journal_restore(hal) ? 5 : 7);
}

// --- the helpers ---------------------------------------------------------------

// stress.all's load: every control, its dB scale, the proc file, the
// register hwdep's info and the mixer's registers, for secs seconds, with
// a moment between rounds
static int hammer(double secs)
{
	struct et_hal *h = et_hal_alsa(NULL);
	char name[64], buf[4096];
	double end;
	uint32_t hash;
	long v[2], cdb;
	int nth, index, val, reg;

	if (!h)
		return 2;
	end = h->now(h->ctx) + secs;
	while (h->now(h->ctx) < end) {
		for (nth = 0;
		     !h->ctl_nth(h->ctx, nth, name, sizeof(name), &index);
		     nth++) {
			struct et_ctl_info info;

			if (h->ctl_info(h->ctx, name, index, &info) < 0)
				continue;
			h->ctl_get(h->ctx, name, index, v, 2);
			if (info.type == ET_CTL_INT)
				h->ctl_db(h->ctx, name, index, info.min, &cdb);
		}
		h->proc(h->ctx, buf, sizeof(buf));
		h->info(h->ctx, &hash, NULL);
		// the mixer's registers without side effects
		for (reg = 0x50; reg <= 0x7D; reg++)
			if (reg != 0x61 && reg != 0x63 && reg != 0x66)
				h->reg_read(h->ctx, 0, reg, &val);
		h->sleep(h->ctx, 0.005);
	}
	return 0;
}

// --- the self-test -----------------------------------------------------------

static int self_test(void)
{
	enum { N = 8820 };
	static double x[N], y[N];
	struct an_fit fit;
	double corr;
	long i, lag;
	int bad = 0;

	for (i = 0; i < N; i++)
		x[i] = 12000 * sin(2 * M_PI * 441.25 * i / 44100 + 0.5) + 50;
	if (an_fit_sine(x, N, 44100, 440, 1, &fit) ||
	    fabs(fit.f - 441.25) > 1e-3 || fabs(fit.amp - 12000) > 1 ||
	    fabs(fit.dc - 50) > 0.5)
		bad++;
	for (i = 0; i < N; i++)
		y[i] = i >= 37 ? x[i - 37] : 0;
	lag = an_lag(x, N, y, N, 200, &corr);
	if (lag != 37)
		bad++;
	x[N / 2] = 0;
	if (an_glitches(x, N, 44100, 441.25, 256, 20, NULL, 0) != 1)
		bad++;
	printf("es1869test self-test: %s\n", bad ? "FAILED" : "ok");
	return bad ? 8 : 0;
}

// --- main --------------------------------------------------------------------

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "rec2", 1, NULL, 'r' },    { "max-out", 1, NULL, 'm' },
		{ "rec2-left", 0, NULL, 'Y' },
		{ "destructive", 0, NULL, 'D' }, { "stock", 0, NULL, 'S' },
		{ "sim", 2, NULL, 's' },     { "restore", 0, NULL, 'R' },
		{ "self-test", 0, NULL, 'T' }, { "version", 0, NULL, 'V' },
		{ "list", 0, NULL, 'l' },    { "fm-note", 1, NULL, 'F' },
		{ "gmcheck", 1, NULL, 'G' }, { "reload", 1, NULL, 'L' },
		{ "suspend", 0, NULL, 'P' }, { "hammer", 1, NULL, 'H' },
		{ "tone", 1, NULL, 'W' },
		{ "baseline", 1, NULL, 'B' },
		{ NULL, 0, NULL, 0 },
	};
	struct et_ctx c = { .outdir = ".", .max_out = -20 };
	const char *rec2 = NULL;
	int sim = -1, restore = 0, list = 0, need_rec2 = 0, opt, i;
	int compared, moved;
	int counts[5] = { 0 };
	struct utsname un;
	sigset_t set;
	pthread_t sth;
	time_t now;

	while ((opt = getopt_long(argc, argv, "c:x:qlvo:i", opts, NULL)) != -1)
		switch (opt) {
		case 'c':
			only = optarg;
			break;
		case 'x':
			except = optarg;
			break;
		case 'q':
			quick = 1;
			break;
		case 'l':
			list = 1;
			break;
		case 'v':
			c.verbose = 1;
			break;
		case 'o':
			c.outdir = optarg;
			break;
		case 'i':
			inter = 1;
			break;
		case 'r':
			rec2 = optarg;
			break;
		case 'Y':
			c.rec2_left = 1;
			break;
		case 'm':
			c.max_out = atof(optarg);
			break;
		case 'D':
			destructive = 1;
			break;
		case 'S':
			c.stock = 1;
			break;
		case 's':
			sim = optarg ? (int)strtol(optarg, NULL, 0) : 0;
			break;
		case 'R':
			restore = 1;
			break;
		case 'T':
			return self_test();
		case 'V':
			printf("es1869test %s\n", ET_VERSION);
			return 0;
		case 'F':
			return et_fm_note(optarg);
		case 'G':
			return et_gmcheck_write(optarg) ? 1 : 0;
		case 'L':
			et_hal_alsa_reload_cmd(optarg);
			break;
		case 'B':
			if (et_baseline_load(optarg) < 0) {
				fprintf(stderr, "es1869test: can't read %s\n",
					optarg);
				return 1;
			}
			break;
		case 'P':
			return et_pm_test_suspend() ? 1 : 0;
		case 'H':
			return hammer(atof(optarg));
		case 'W':
			return et_pc_tone(1000, atoi(optarg)) ? 1 : 0;
		default:
			usage();
			return 1;
		}
	if (optind < argc) {
		usage();
		return 1;
	}
	if (list) {
		for (i = 0; i < et_ncases; i++)
			if (selected(&et_cases[i]))
				printf("%-24s %s\n", et_cases[i].id,
				       et_cases[i].spec);
		return 0;
	}

	if (sim >= 0) {
		hal = et_hal_sim(sim);
	} else {
		if (!take_lock()) {
			fprintf(stderr, "es1869test: another run holds the lock\n");
			return 3;
		}
		if (access("/sys/module/snd_es1869", F_OK) &&
		    !(c.stock && !access("/sys/module/snd_es18xx", F_OK))) {
			fprintf(stderr, "es1869test: snd-es1869 isn't loaded\n");
			return 2;
		}
		hal = et_hal_alsa(rec2);
	}
	if (!hal) {
		fprintf(stderr, "es1869test: no ES1869 card\n");
		return 2;
	}
	c.hal = hal;
	if (sim < 0) {
		const char *jp = journal_path();
		int r = et_journal_replay(hal, jp);

		if (r > 0) {
			fprintf(stderr, "es1869test: the last run's journal didn't apply\n");
			return 5;
		}
		if (r == 0)
			printf("es1869test: restored what a cut-off run changed\n");
		if (restore)
			return 0;
		if (mpd_playing()) {
			fprintf(stderr, "es1869test: MPD is playing; stop it first\n");
			return 3;
		}
		r = hal->type_f(hal->ctx);
		if (r == 1 || (r < 0 && c.stock)) {
			fprintf(stderr,
				r == 1 ? "es1869test: the PIIX4 runs the card's capture channel with Type F timing, and the first byte recorded would hang the machine: load snd-es1869 with piix4_type_f=0 once since boot\n" :
					 "es1869test: can't read the PIIX4's Type F timing without root, and snd-es18xx leaves it on\n");
			return r == 1 ? 9 : 4;
		}
		if (pcm_holders(card_number()))
			return 3;
		et_journal_open(jp);
	} else if (restore) {
		return 0;
	}

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGHUP);
	pthread_sigmask(SIG_BLOCK, &set, NULL);
	pthread_create(&sth, NULL, signal_thread, &set);

	if (et_report_open(c.outdir) < 0) {
		fprintf(stderr, "es1869test: can't write the report in %s\n",
			c.outdir);
		return 1;
	}
	// the kernel's log from here on
	if (hal->klog)
		hal->klog(hal->ctx, NULL, 0);
	now = time(NULL);
	uname(&un);
	et_log("es1869test %s on %s %s, %s", ET_VERSION, un.nodename,
	       un.release, ctime(&now));
	et_log("machine: %s%s\n", hal->name, c.stock ? ", stock driver" : "");

	for (i = 0; i < et_ncases; i++)
		if (selected(&et_cases[i]) &&
		    (et_cases[i].flags & (ET_SECOND | ET_CABLE)))
			need_rec2 = 1;
	for (i = 0; i < et_ncases; i++) {
		const struct et_case *k = &et_cases[i];
		int cal = !strncmp(k->id, "cal.", 4);

		// the calibration runs whenever a case needs what it finds
		if (!selected(k) && !(cal && need_rec2))
			continue;
		if (cal && except && listed(except, k->id))
			continue;
		counts[run_case(&c, k)]++;
	}
	et_log("passed %d, failed %d, known %d, skipped %d, errors %d\n",
	       counts[ET_PASS], counts[ET_FAIL], counts[ET_KNOWN],
	       counts[ET_SKIP], counts[ET_ERROR]);
	et_baseline_counts(&compared, &moved);
	if (compared)
		et_log("against the baseline: %d of %d measurements moved\n",
		       moved, compared);
	if (et_journal_restore(hal)) {
		et_report_close();
		return 5;
	}
	et_report_close();
	return counts[ET_FAIL] || counts[ET_ERROR] ? 6 : 0;
}
