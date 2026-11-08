// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * flyer: MOTU's PC-MIDI Flyer on the parallel port from user space, by port
 * I/O, with no driver and no interrupt. As root. A port is a or b.
 *
 *   flyer [-p PORT] look              the lines at each control state
 *   flyer [-p PORT] raw TOKEN...      d XX: data, c XX: control, s: read status,
 *                                     x: read control, u N: wait N us
 *   flyer [-p PORT] id                the model the unit says it is
 *   flyer [-p PORT] init              the supply, then MOTU's open command
 *   flyer [-p PORT] cmd XX            a byte on the command channel
 *   flyer [-p PORT] send OUT BYTE...  to OUT, one every -g us
 *   flyer [-p PORT] recv SECONDS      what comes in, by input
 *   flyer [-p PORT] watch SECONDS     each change of the status lines, reading
 *                                     no byte
 *   flyer [-p PORT] trace OUT N       the status lines after each of N bytes
 *   flyer [-p PORT] lat OUT           how long a byte takes to come back
 *   flyer [-p PORT] loop OUT N        a sysex of N bytes to a, b or ab, reading
 *                                     both inputs, for a cable from that output
 *                                     to an input
 *   flyer [-p PORT] fill OUT N        N bytes, then what the unit held of them
 *   flyer [-p PORT] depth OUT STEP    bytes the unit takes before room falls
 *   flyer [-p PORT] pace OUT N        N bursts, with each change of the lines
 *   flyer [-p PORT] flood OUT N       N bytes with no look at the room flag
 *   flyer [-p PORT] alive             whether the unit's pulse is there
 *
 * -g US spaces the bytes of send and loop; 0, the default for loop, leaves it
 * to the unit's room flag, FL_BURST bytes each time. -i 0 leaves control bit 4,
 * the port's interrupt enable, clear in the protocol's writes, as the driver
 * does while no input is open.
 *
 * The protocol is in linux/flyer/proto.h. This is for looking at a unit without
 * the driver, so don't run the two together.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <time.h>
#include <unistd.h>

static unsigned int base = 0x378, gap, irq_bit = 1;

static void wait_us(unsigned int n);

#define fl_out(io, reg, v) outb(v, (io)->base + (reg))
#define fl_in(io, reg) inb((io)->base + (reg))
#define fl_ms(n) usleep((n) * 1000)
#define fl_us(n) wait_us(n)
#include "../flyer/proto.h"

#define ROOM_S 0.05 // a full FIFO drains in 3 ms

static struct fl_io io;

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static void wait_us(unsigned int n)
{
	double end = now() + n * 1e-6;

	while (now() < end)
		;
}

static int port_of(const char *s)
{
	return s[0] == 'b' || s[0] == 'B' || s[0] == '1';
}

static void show(const char *what, unsigned char c)
{
	outb(c, base + FL_CONTROL);
	wait_us(20);
	printf("%-10s control %02x status %02x\n", what, inb(base + FL_CONTROL),
	       inb(base + FL_STATUS));
}

// each output's strobe sends FFh, which comes back round a cable
static int look(void)
{
	outb(0xFF, base + FL_DATA);
	wait_us(20);
	show("idle", FL_C_IDLE);
	show("write A", fl_strobe(0));
	show("read A", fl_reader(0));
	show("write B", fl_strobe(1));
	show("read B", fl_reader(1));
	show("command", FL_C_COMMAND);
	show("both read", 0x1E);
	show("idle", FL_C_IDLE);
	return 0;
}

static int raw(int argc, char **argv)
{
	int i;

	for (i = 0; i < argc; i++) {
		const char *t = argv[i];

		if (!strcmp(t, "s")) {
			printf("status %02x\n", inb(base + FL_STATUS));
		} else if (!strcmp(t, "x")) {
			printf("control %02x\n", inb(base + FL_CONTROL));
		} else if (i + 1 < argc && !strcmp(t, "d")) {
			outb(strtoul(argv[++i], NULL, 16), base + FL_DATA);
		} else if (i + 1 < argc && !strcmp(t, "c")) {
			outb(strtoul(argv[++i], NULL, 16), base + FL_CONTROL);
		} else if (i + 1 < argc && !strcmp(t, "u")) {
			wait_us(strtoul(argv[++i], NULL, 0));
		} else {
			fprintf(stderr, "flyer: what is %s?\n", t);
			return 1;
		}
	}
	return 0;
}

static int has_room(void)
{
	double end = now() + ROOM_S;

	while (now() < end)
		if (fl_room(&io))
			return 1;
	return 0;
}

static int send(int out, int argc, char **argv)
{
	int i;

	for (i = 0; i < argc; i++) {
		if (!has_room()) {
			printf("the unit never had room, byte %d\n", i);
			return 3;
		}
		fl_put(&io, out, strtoul(argv[i], NULL, 16));
		wait_us(gap);
	}
	return 0;
}

static int recv_for(double secs)
{
	double end = now() + secs;
	unsigned long n = 0;
	int was = -1, in;

	while (now() < end) {
		if (!fl_any(&io)) {
			usleep(100);
			continue;
		}
		for (in = 0; in < 2; in++) {
			unsigned char b;

			if (!fl_waiting(&io, in))
				continue;
			b = fl_get(&io, in);
			if (in != was)
				printf("[%c] ", 'A' + in);
			was = in;
			printf("%02x ", b);
			fflush(stdout);
			n++;
		}
	}
	printf("\n%lu bytes\n", n);
	return 0;
}

static int watch(double secs)
{
	double start = now(), end = start + secs;
	unsigned long n = 0;
	int last = -1;

	while (now() < end) {
		int s = inb(base + FL_STATUS) & 0xF8;

		if (s != last) {
			if (n++ < 200)
				printf("%9.5f status %02x\n", now() - start, s);
			last = s;
		}
	}
	printf("%lu changes\n", n);
	return 0;
}

static void drain(void)
{
	double end = now() + 0.01;
	int in;

	while (now() < end)
		for (in = 0; in < 2; in++)
			if (fl_waiting(&io, in))
				fl_get(&io, in);
}

// how long a byte takes from its strobe to the input: a MIDI cable takes 320 us
// for the byte alone
static int lat(int out)
{
	double t[2][16];
	int n[2] = { 0, 0 }, i, in;

	drain();
	for (i = 0; i < 16; i++) {
		double t0, end;

		fl_put(&io, out, 0x10 + i);
		t0 = now();
		end = t0 + 0.02;
		while (now() < end)
			for (in = 0; in < 2; in++)
				if (fl_waiting(&io, in)) {
					fl_get(&io, in);
					t[in][n[in]++ & 15] = now() - t0;
					end = 0;
				}
	}
	for (in = 0; in < 2; in++) {
		double lo = 1, hi = 0;

		for (i = 0; i < n[in] && i < 16; i++) {
			lo = t[in][i] < lo ? t[in][i] : lo;
			hi = t[in][i] > hi ? t[in][i] : hi;
		}
		if (n[in])
			printf("%c to %c: %d of 16 back, %.0f to %.0f us\n", 'A' + out,
			       'A' + in, n[in], lo * 1e6, hi * 1e6);
		else
			printf("%c to %c: none back\n", 'A' + out, 'A' + in);
	}
	return 0;
}

// the status lines' changes after each byte, for 1 ms, reading no byte until the
// end of each: when the unit raises the line that interrupts, status bit 6
static int trace(int out, int n)
{
	int i, in;

	drain();
	for (i = 0; i < n; i++) {
		double t0, end;
		int last = -1;

		fl_put(&io, out, 0x20 + i);
		t0 = now();
		end = t0 + 0.001;
		printf("byte %d:", i);
		while (now() < end) {
			int s = inb(base + FL_STATUS) & 0xF8;

			if (s != last) {
				printf(" %.0f:%02x", (now() - t0) * 1e6, s);
				last = s;
			}
		}
		printf("\n");
		for (in = 0; in < 2; in++)
			while (fl_waiting(&io, in))
				printf("  read %c: %02x, status %02x\n", 'A' + in,
				       fl_get(&io, in), inb(base + FL_STATUS));
	}
	return 0;
}

// the byte of a test sysex
static unsigned char nth(int i, int n)
{
	return !i ? 0xF0 : i == n - 1 ? 0xF7 : i % 128;
}

// bytes with no read until they are all out: how many the unit held, and which
static int fill(int out, int n)
{
	int i, in, count = 0;
	double start = now();

	drain();
	for (i = 0; i < n; i++) {
		if (!has_room()) {
			printf("no room at byte %d\n", i);
			break;
		}
		fl_put(&io, out, i & 0x7F);
	}
	usleep(20000);
	printf("%d bytes in %.0f ms, back:", n, (now() - start) * 1e3);
	for (in = 0; in < 2; in++)
		while (fl_waiting(&io, in)) {
			int b = fl_get(&io, in);

			if (count++ < 12)
				printf(" %02x", b);
		}
	printf(" ... %d in all\n", count);
	return 0;
}

// bursts of FL_BURST bytes on the room flag, with each change of the interrupt
// line and the flag: whether a rise of the line comes with the room
static int pace(int out, int bursts)
{
	static double at[4096];
	static unsigned char st[4096];
	int n = 0, i, k, in, last = -1, bytes = 0;
	double t0;

	drain();
	t0 = now();
	for (i = 0; i < bursts;) {
		int s = inb(base + FL_STATUS);

		if ((s & 0xC8) != last && n < 4096) {
			at[n] = now() - t0;
			st[n++] = s;
			last = s & 0xC8;
		}
		for (in = 0; in < 2; in++)
			if (s & FL_S_BYTE && fl_waiting(&io, in)) {
				fl_get(&io, in);
				bytes++;
			}
		if (s & FL_S_ROOM) {
			if (n < 4096) {
				at[n] = now() - t0;
				st[n++] = 1;
			}
			for (k = 0; k < FL_BURST; k++)
				fl_put(&io, out, 0x10 + k);
			i++;
		}
	}
	for (i = 0; i < n; i++) {
		if (st[i] == 1)
			printf("%7.0f burst\n", at[i] * 1e6);
		else
			printf("%7.0f %02x\n", at[i] * 1e6, st[i]);
	}
	printf("%d back\n", bytes);
	return 0;
}

// n bytes in a row with no look at the room flag, then what came back
static int flood(int out, int n)
{
	int i, in, count = 0;

	drain();
	while (!fl_room(&io))
		;
	for (i = 0; i < n; i++)
		fl_put(&io, out, 0x20 + i);
	usleep(10000);
	printf("%d in a row, back:", n);
	for (in = 0; in < 2; in++)
		while (fl_waiting(&io, in)) {
			printf(" %02x", fl_get(&io, in));
			count++;
		}
	printf(" (%d)\n", count);
	return 0;
}

// how many bytes the unit takes before its room flag falls, checked every step
// bytes, then how long the flag stays down
static int depth(int out, int step)
{
	int n = 0, i;
	double t0;

	drain();
	while (fl_room(&io) && n < 1024)
		for (i = 0; i < step; i++)
			fl_put(&io, out, 0x10 + n++ % 64);
	t0 = now();
	while (!fl_room(&io) && now() < t0 + 0.05)
		;
	printf("%d bytes, %d at a time, then %.0f us without room\n", n, step,
	       (now() - t0) * 1e6);
	usleep(10000);
	drain();
	return 0;
}

// a sysex of n bytes to OUT, a, b or ab, with both inputs read throughout: what
// came back on each, against what was sent
static int loop(const char *to, int n)
{
	static unsigned char got[2][8192];
	int count[2] = { 0, 0 }, i = 0, k, in, wrong[2] = { 0, 0 }, full = 0, o;
	int use[2] = { !!strchr(to, 'a'), !!strchr(to, 'b') };
	double start = now(), end = 0;

	if (n > 8192)
		n = 8192;
	drain();
	while (i < n || now() < end) {
		for (in = 0; in < 2; in++)
			if (fl_waiting(&io, in)) {
				unsigned char b = fl_get(&io, in);

				if (count[in] < 8192)
					got[in][count[in]++] = b;
			}
		if (i >= n)
			continue;
		if (gap) {
			for (o = 0; o < 2; o++)
				if (use[o])
					fl_put(&io, o, nth(i, n));
			i++;
			wait_us(gap);
		} else if (fl_room(&io)) {
			for (k = 0; k < FL_BURST && i < n; i++)
				for (o = 0; o < 2; o++)
					if (use[o]) {
						fl_put(&io, o, nth(i, n));
						k++;
					}
		} else {
			full++;
		}
		if (i == n)
			end = now() + 0.25;
	}
	for (in = 0; in < 2; in++)
		for (k = 0; k < count[in] && k < n; k++)
			wrong[in] += got[in][k] != nth(k, n);
	printf("%d bytes to %s in %.0f ms: in A %d (%d wrong), in B %d (%d wrong),"
	       " %d polls found no room\n",
	       n, to, (end - 0.25 - start) * 1e3, count[0], wrong[0], count[1],
	       wrong[1], full);
	for (in = 0; in < 2; in++)
		if (wrong[in] || (count[in] && count[in] != n)) {
			for (k = 0; k < count[in] && k < 32; k++)
				printf("%02x ", got[in][k]);
			printf("\n");
		}
	return 0;
}

int main(int argc, char **argv)
{
	const char *c;
	int o;

	while ((o = getopt(argc, argv, "p:g:i:")) != -1) {
		if (o == 'p')
			base = strtoul(optarg, NULL, 0);
		else if (o == 'g')
			gap = strtoul(optarg, NULL, 0);
		else if (o == 'i')
			irq_bit = atoi(optarg);
		else
			return 1;
	}
	if (optind >= argc) {
		fprintf(stderr, "usage: flyer [-p PORT] [-g US] [-i 0|1] look"
				" | raw TOKEN..."
				" | id | init | cmd XX | send OUT BYTE..."
				" | recv SECONDS | watch SECONDS | trace OUT N"
				" | lat OUT | loop OUT N | fill OUT N"
				" | depth OUT STEP | pace OUT N | flood OUT N"
				" | alive\n");
		return 1;
	}
	if (ioperm(base, 3, 1)) {
		perror("flyer: ioperm");
		return 2;
	}
	io.base = base;
	io.irq = irq_bit ? FL_C_IRQ : 0;
	argc -= optind;
	argv += optind;
	c = argv[0];
	if (!strcmp(c, "look"))
		return look();
	if (!strcmp(c, "raw"))
		return raw(argc - 1, argv + 1);
	if (!strcmp(c, "id")) {
		unsigned char before = inb(base + FL_STATUS);
		int v = fl_id(&io);

		printf("status before %02x, id %d, status after %02x\n", before, v,
		       inb(base + FL_STATUS));
		return 0;
	}
	if (!strcmp(c, "init")) {
		fl_start(&io);
		printf("status %02x\n", inb(base + FL_STATUS));
		return 0;
	}
	if (!strcmp(c, "cmd") && argc >= 2) {
		fl_command(&io, strtoul(argv[1], NULL, 16));
		printf("status %02x\n", inb(base + FL_STATUS));
		return 0;
	}
	if (!strcmp(c, "send") && argc >= 3)
		return send(port_of(argv[1]), argc - 2, argv + 2);
	if (!strcmp(c, "recv") && argc >= 2)
		return recv_for(atof(argv[1]));
	if (!strcmp(c, "watch") && argc >= 2)
		return watch(atof(argv[1]));
	if (!strcmp(c, "trace") && argc >= 3)
		return trace(port_of(argv[1]), atoi(argv[2]));
	if (!strcmp(c, "lat") && argc >= 2)
		return lat(port_of(argv[1]));
	if (!strcmp(c, "loop") && argc >= 3)
		return loop(argv[1], atoi(argv[2]));
	if (!strcmp(c, "flood") && argc >= 3)
		return flood(port_of(argv[1]), atoi(argv[2]));
	if (!strcmp(c, "alive")) {
		printf("%s\n", fl_alive(&io) ? "alive" : "no pulse");
		return 0;
	}
	if (!strcmp(c, "pace") && argc >= 3)
		return pace(port_of(argv[1]), atoi(argv[2]));
	if (!strcmp(c, "depth") && argc >= 3)
		return depth(port_of(argv[1]), atoi(argv[2]));
	if (!strcmp(c, "fill") && argc >= 3)
		return fill(port_of(argv[1]), atoi(argv[2]));
	fprintf(stderr, "flyer: what is %s?\n", c);
	return 1;
}
