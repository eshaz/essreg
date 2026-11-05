// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * clockcmp: the machine's other clocks against the raw system clock, a line
 * of readings at each tick of the RTC, for a look at which of them are
 * oscillators of their own, and how they move.
 *
 * Each counter is read between two readings of the raw clock, and again if
 * those lie too far apart, so that a reading's time is known to a few us:
 *   the RTC's second, its own 32.768 kHz crystal, by reading it through the
 *   kernel around the tick, which waits out the update, to about 100 us;
 *   the TSC, the CPU's clock;
 *   an RTL8169's timer (TCTR), which counts the PCI bus, if the machine has one;
 *   the UHCI host's frame number, at the moment it changes, if it has one;
 *   the video chip's retrace, at the moment it starts, every 16 s.
 * adjtimex's frequency and the temperature come along. tools/clockan.py reads
 * the log.
 *
 * usage: gcc -O2 -o clockcmp clockcmp.c; clockcmp seconds  (as root; to stdout)
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/timex.h>
#include <time.h>
#include <unistd.h>

#define TCTR 0x48 // an RTL8169's timer, from its I/O base
#define FRNUM 6 // a UHCI host's frame number, from its I/O base
#define VGA_STATUS 0x3DA
#define NEAR_NS 30000

static unsigned int nic_tctr, usb_frnum;

// the I/O base of a PCI device's BAR, or 0
static unsigned int pci_io(const char *dir, int bar)
{
	char path[300], line[128];
	unsigned long long start = 0, end, flags = 0;
	FILE *f;
	int i;

	snprintf(path, sizeof(path), "%s/resource", dir);
	f = fopen(path, "r");
	if (!f)
		return 0;
	for (i = 0; i <= bar && fgets(line, sizeof(line), f); i++)
		sscanf(line, "%llx %llx %llx", &start, &end, &flags);
	fclose(f);
	return flags & 0x100 ? (unsigned int)start : 0;
}

static unsigned long pci_attr(const char *dir, const char *name)
{
	char path[300], line[32] = "";
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "r");
	if (!f)
		return 0;
	if (!fgets(line, sizeof(line), f))
		line[0] = 0;
	fclose(f);
	return strtoul(line, NULL, 0);
}

// where the two counters are on this machine
static void pci_find(void)
{
	DIR *d = opendir("/sys/bus/pci/devices");
	struct dirent *e;
	char dir[280];

	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(dir, sizeof(dir), "/sys/bus/pci/devices/%s", e->d_name);
		if (pci_attr(dir, "vendor") == 0x10EC &&
		    pci_attr(dir, "device") == 0x8169 && !nic_tctr)
			nic_tctr = pci_io(dir, 0) + TCTR;
		if (pci_attr(dir, "class") == 0x0C0300 && !usb_frnum)
			usb_frnum = pci_io(dir, 4) + FRNUM;
	}
	if (d)
		closedir(d);
}

static long long raw(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC_RAW, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static long long mono(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static uint64_t tsc(void)
{
	uint32_t a, d;

	__asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
	return (uint64_t)d << 32 | a;
}

// a counter between two readings of the raw clock: their middle
#define BRACKET(at, v, expr) do { \
	int tries_; \
	for (tries_ = 0; tries_ < 5; tries_++) { \
		long long a_ = raw(); \
		v = (expr); \
		long long b_ = raw(); \
		at = (a_ + b_) / 2; \
		if (b_ - a_ < NEAR_NS) \
			break; \
	} \
} while (0)

// the raw time at which the frame number changes
static long long usb_edge(unsigned int *frame)
{
	unsigned int f, g;
	long long a, b;
	int n;

	*frame = 0xFFFF;
	if (!usb_frnum)
		return 0;
	f = inw(usb_frnum) & 0x7FF;
	for (n = 0; n < 40000; n++) {
		a = raw();
		g = inw(usb_frnum) & 0x7FF;
		b = raw();
		if (g != f) {
			*frame = g;
			return (a + b) / 2;
		}
	}
	return 0;
}

// the raw time at which the vertical retrace begins
static long long vga_edge(void)
{
	long long a, b;
	int n;

	for (n = 0; n < 200000; n++)
		if (!(inb(VGA_STATUS) & 8))
			break;
	for (n = 0; n < 200000; n++) {
		a = raw();
		if (inb(VGA_STATUS) & 8) {
			b = raw();
			return (a + b) / 2;
		}
	}
	return 0;
}

// the raw time of the RTC's next tick: read it until the second changes, from
// a little before that: the interrupt's way there goes through a work queue,
// which scatters it by hundreds of us
static long long rtc_edge(int fd, int *last)
{
	struct rtc_time tm;
	long long t;

	if (ioctl(fd, RTC_RD_TIME, &tm))
		return 0;
	if (*last >= 0 && tm.tm_sec != *last) {
		// the tick came first: the kernel set the RTC, or a late wake
		*last = tm.tm_sec;
		return 0;
	}
	*last = tm.tm_sec;
	for (;;) {
		if (ioctl(fd, RTC_RD_TIME, &tm))
			return 0;
		t = raw();
		if (tm.tm_sec != *last) {
			*last = tm.tm_sec;
			return t;
		}
	}
}

static int temperature(void)
{
	int fd = open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY);
	char buf[16] = "";
	int n;

	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	return n > 0 ? atoi(buf) : 0;
}

int main(int argc, char **argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60, i;
	struct sched_param sp = { .sched_priority = 30 };
	int fd = open("/dev/rtc0", O_RDONLY);
	long long t_vga = 0;
	int temp = 0, sec = -1;
	long freq = 0;

	if (fd < 0 || iopl(3)) {
		perror("clockcmp: needs root and /dev/rtc0");
		return 1;
	}
	pci_find();
	if (sched_setscheduler(0, SCHED_FIFO, &sp))
		perror("clockcmp: no real-time priority");
	printf("# n rtc_raw mono tsc_at tsc nic_at nic usb_at usb vga_at freq temp\n");
	for (i = 0; i < seconds; i++) {
		long long t_rtc, m, t_tsc, t_nic, t_usb;
		uint64_t c;
		uint32_t nic;
		unsigned int frame;

		t_rtc = rtc_edge(fd, &sec);
		if (!t_rtc)
			t_rtc = rtc_edge(fd, &sec);
		m = mono();
		BRACKET(t_tsc, c, tsc());
		BRACKET(t_nic, nic, nic_tctr ? inl(nic_tctr) : 0);
		t_usb = usb_edge(&frame);
		t_vga = 0;
		if (i % 16 == 3)
			t_vga = vga_edge();
		if (i % 64 == 5) {
			struct timex tx;

			memset(&tx, 0, sizeof(tx));
			adjtimex(&tx);
			freq = tx.freq;
			temp = temperature();
		}
		printf("%d %lld %lld %lld %llu %lld %u %lld %u %lld %ld %d\n", i,
		       t_rtc, m, t_tsc, (unsigned long long)c, t_nic, nic, t_usb,
		       frame, t_vga, freq, temp);
		fflush(stdout);
		// asleep until 5 ms before the next tick
		{
			long long left = t_rtc + 995000000LL - raw();

			if (left > 0) {
				struct timespec w = { left / 1000000000,
						      left % 1000000000 };

				nanosleep(&w, NULL);
			}
		}
	}
	return 0;
}
