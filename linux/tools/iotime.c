// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * iotime: what an access of the chip's mixer ports costs, in cycles, one
 * instruction at a time, and whether one 16-bit OUT sets the index and the data.
 * Only the register 72h moves, and it ends on the value it found. Run it with
 * no stream open: the ports are reached around the driver.
 *
 * usage: iotime [BASE]  (as root, default 0x220)
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>
#include <unistd.h>

static inline unsigned long long tsc(void)
{
	unsigned int a, d;

	__asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
	return ((unsigned long long)d << 32) | a;
}

static double mhz = 400.014154;

#define LOOP(name, code) do { \
	unsigned long long best = ~0ULL, t; \
	int pass, i; \
	for (pass = 0; pass < 200; pass++) { \
		t = tsc(); \
		for (i = 0; i < 64; i++) { code; } \
		t = tsc() - t; \
		if (t < best) \
			best = t; \
	} \
	printf("%-34s %8.1f cycles %6.3f us\n", name, best / 64.0, best / 64.0 / mhz); \
} while (0)

int main(int argc, char **argv)
{
	unsigned int base = argc > 1 ? strtoul(argv[1], NULL, 0) : 0x220;
	unsigned int ix = base + 4, data = base + 5;
	unsigned char found, a, b, got;
	volatile unsigned char sink;

	if (iopl(3)) {
		perror("iopl");
		return 1;
	}
	outb(0x72, ix);
	found = inb(data);
	a = found;
	b = found == 0xFD ? 0xFC : 0xFD;
	printf("72h found %02x; bridge recovery as set by the BIOS\n", found);
	LOOP("rdtsc alone", (void)0);
	LOOP("out index", outb(0x72, ix));
	outb(0x72, ix);
	LOOP("out data, same value", outb(a, data));
	LOOP("in data", sink = inb(data));
	LOOP("out index, out data", (outb(0x72, ix), outb(a, data)));
	LOOP("out data, turned", (outb(b, data), outb(a, data)));
	LOOP("outw index + data, turned", (outw(0x72 | b << 8, ix), outw(0x72 | a << 8, ix)));
	LOOP("in 3DAh (the video chip, on PCI)", sink = inb(0x3DA));
	LOOP("in 61h (inside the bridge)", sink = inb(0x61));
	LOOP("in 80h (ISA, nothing there)", sink = inb(0x80));
	LOOP("in 201h (game port, ISA)", sink = inb(0x201));
	LOOP("in 388h (FM status, the chip)", sink = inb(0x388));
	LOOP("in +Ch (DSP status, the chip)", sink = inb(base + 0xC));
	LOOP("in Config_Base+6 (the chip)", sink = inb(0x256));
	(void)sink;
	// does one 16-bit OUT reach both ports?
	outw(0x72 | b << 8, ix);
	outb(0x72, ix);
	got = inb(data);
	printf("outw wrote %02x, reads %02x: %s\n", b, got, got == b ? "both ports" : "NOT both");
	outw(0x72 | a << 8, ix);
	outb(0x72, ix);
	got = inb(data);
	printf("outw wrote %02x, reads %02x: %s\n", a, got, got == a ? "both ports" : "NOT both");
	outb(0x72, ix);
	outb(found, data);
	return 0;
}
