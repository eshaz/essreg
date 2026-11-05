// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * modlab: a rate or filter register moved under a stream, in time with the
 * cycle counter, through the register hwdep's pattern. Ends on what it found.
 *
 * usage: modlab DEV m|c HZ SECONDS REG:VAL[*N],... [BURSTS GAP_MS]
 *   m mixer (70, 72), c controller (A1, A2); HZ the steps a second
 *   REG:VAL*N is N steps of that entry, in hex
 *   with BURSTS, that many of SECONDS each with no other task running, GAP_MS
 *   between them for the streams' clients, the pattern in phase across them
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/types.h>

#include "../es1869/uapi/es1869.h"

static __u16 tab[ES1869_PATTERN_MAX];

int main(int argc, char **argv)
{
	double mhz = getenv("TSC_MHZ") ? atof(getenv("TSC_MHZ")) : 400.014154;
	const char *dev, *s;
	struct es1869_reg_pattern p;
	int fd, bank, regs[8], nregs = 0, found[8], i, err = 0, b, bursts, gap;
	unsigned long writes = 0, skipped = 0, late = 0, longest = 0;
	double hz, secs;
	char *e;

	if (argc < 6) {
		fprintf(stderr, "usage: modlab DEV m|c HZ SECONDS REG:VAL[*N],...\n");
		return 1;
	}
	dev = argv[1];
	bank = argv[2][0] == 'c' ? ES1869_BANK_CTRL : ES1869_BANK_MIXER;
	hz = atof(argv[3]);
	secs = atof(argv[4]);
	bursts = argc > 6 ? atoi(argv[6]) : 0;
	gap = argc > 7 ? atoi(argv[7]) : 60;
	memset(&p, 0, sizeof(p));
	for (s = argv[5]; *s;) {
		long r = strtol(s, &e, 16), v, n = 1;

		if (*e != ':')
			return 2;
		v = strtol(e + 1, &e, 16);
		if (*e == '*')
			n = strtol(e + 1, &e, 10);
		while (n-- > 0) {
			if (p.len >= ES1869_PATTERN_MAX)
				return 3;
			tab[p.len++] = r << 8 | v;
		}
		for (i = 0; i < nregs && regs[i] != r; i++)
			;
		if (i == nregs)
			regs[nregs++] = r;
		s = *e ? e + 1 : e;
	}
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return 1;
	}
	for (i = 0; i < nregs; i++) {
		struct es1869_reg r = { .bank = bank, .reg = regs[i] };

		if (ioctl(fd, ES1869_IOCTL_READ, &r)) {
			perror("read");
			return 1;
		}
		found[i] = r.value;
	}
	p.table = (uintptr_t)tab;
	p.step = (__u64)(mhz * 1e6 / hz * 65536.0 + 0.5);
	p.count = (__u32)(secs * hz);
	p.bank = bank;
	if (bursts)
		p.flags = ES1869_PATTERN_ALONE;
	for (b = 0; b < (bursts ? bursts : 1) && !err; b++) {
		err = ioctl(fd, ES1869_IOCTL_PATTERN, &p);
		if (err) {
			fprintf(stderr, "pattern: %s\n", strerror(errno));
			break;
		}
		writes += p.writes;
		skipped += p.skipped;
		if (p.late > late)
			late = p.late;
		if (p.longest > longest)
			longest = p.longest;
		if (b + 1 < bursts) {
			// the same phase after the gap: whole steps later
			__u64 steps = (__u64)(gap * 1e-3 * hz) + 1;

			usleep(gap * 1000);
			p.base += steps * p.step >> 16;
			p.offset = (p.offset + steps) % p.len;
		}
	}
	for (i = 0; i < nregs; i++) {
		struct es1869_reg r = { .bank = bank, .reg = regs[i], .value = found[i] };

		if (ioctl(fd, ES1869_IOCTL_WRITE, &r))
			perror("write");
	}
	printf("%d entries at %.3f Hz: %lu writes, %lu steps skipped, latest %.2f us, longest %.2f us\n",
	       p.len, hz, writes, skipped, late / mhz, longest / mhz);
	return err ? 1 : 0;
}
