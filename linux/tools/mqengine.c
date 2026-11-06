// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * mqengine: an Opcode MIDIEngine 2port/SE, a Music Quest design, on the parallel
 * port from user space, by port I/O, with no driver and no interrupt. As root.
 *
 *   mqengine [-p PORT] init              reset the unit and leave it ready
 *   mqengine [-p PORT] look              the lines
 *   mqengine [-p PORT] send OUT BYTE...  to OUT, 0 for A and 1 for B
 *   mqengine [-p PORT] recv SECONDS      what comes in, by input
 *   mqengine [-p PORT] sweep OUT         80 00 v for every data byte v
 *   mqengine [-p PORT] loop OUT N        a sysex of N bytes, reading both
 *                                        inputs, with a cable from that output
 *                                        to its input
 *
 * The driver is linux/mqengine/mqengine.c, whose proto.h this shares. This is
 * for looking at a unit without it, so don't run the two together.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <time.h>
#include <unistd.h>

static unsigned int base = 0x378, gap = 1000;
static unsigned int settle = 2; // us: see the driver's header

#define READY_POLLS 2000 // as Opcode's driver

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

#define mq_out(io, reg, v) outb(v, (io)->base + (reg))
#define mq_in(io, reg) inb((io)->base + (reg))
#define mq_us(n) wait_us(n)
#define mq_ms(n) usleep((n) * 1000)
#define MQ_SETTLE_US settle
#include "../mqengine/proto.h"

static struct mq_io io;

// room for a byte, as Opcode's driver waits for it
static int put(int out, unsigned char b)
{
	int n;

	for (n = 0; n < READY_POLLS; n++) {
		if (mq_room(&io)) {
			mq_put(&io, out, b);
			wait_us(MQ_GAP_US);
			return 1;
		}
	}
	return 0;
}

static int init(void)
{
	unsigned char found[3];
	int bad = mq_reset(&io, found);

	printf("data 03: status %02x, wants 28 of A8\n", found[0] & MQ_S_ID);
	printf("data 00: status %02x, wants A8 of A8\n", found[1] & MQ_S_ID);
	printf("AUTOFD:  status %02x, wants 00 of A8\n", found[2] & MQ_S_ID);
	return bad ? 3 : 0;
}

static int look(void)
{
	printf("data %02x status %02x control %02x\n", mq_in(&io, MQ_DATA),
	       mq_in(&io, MQ_STATUS), mq_in(&io, MQ_CONTROL));
	return 0;
}

static int recv_for(double secs)
{
	double end = now() + secs;
	unsigned long n = 0;
	unsigned int in;
	unsigned char b;
	int was = -1;

	while (now() < end) {
		if (mq_get(&io, &b, &in)) {
			wait_us(MQ_GAP_US);
			if ((int)in != was)
				printf("[%c] ", 'A' + in);
			was = in;
			printf("%02x ", b);
			fflush(stdout);
			n++;
		} else {
			usleep(300);
		}
	}
	printf("\n%lu bytes\n", n);
	return 0;
}

static int send(int out, int argc, char **argv)
{
	int i;

	for (i = 0; i < argc; i++) {
		if (!put(out, strtoul(argv[i], NULL, 16))) {
			printf("the unit never had room, byte %d\n", i);
			return 3;
		}
		usleep(gap);
	}
	return 0;
}

// the byte of a test sysex
static unsigned char nth(int i, int n)
{
	return !i ? 0xF0 : i == n - 1 ? 0xF7 : i % 128;
}

// a sysex of n bytes to OUT with both inputs read throughout, for a cable from
// an output to an input: what came back, and what came in on the other
static int loop(int out, int n)
{
	static unsigned char got[2][8192];
	unsigned int in;
	unsigned char b;
	int count[2] = { 0, 0 }, i = 0, wrong = 0, k;
	double start = now(), end = 0;

	if (n > 8192)
		n = 8192;
	while (i < n || now() < end) {
		if (mq_get(&io, &b, &in)) {
			wait_us(MQ_GAP_US);
			if (count[in] < 8192)
				got[in][count[in]++] = b;
		} else if (i < n && mq_room(&io)) {
			mq_put(&io, out, nth(i, n));
			if (++i == n)
				end = now() + 0.25;
		}
	}
	for (k = 0; k < count[out] && k < n; k++)
		wrong += got[out][k] != nth(k, n);
	printf("%d bytes to %c in %.0f ms: in A %d, in B %d, %d wrong%s\n", n,
	       'A' + out, (end - 0.25 - start) * 1e3, count[0], count[1], wrong,
	       count[out] == n && !wrong ? ", exact" : "");
	return 0;
}

static int sweep(int out)
{
	int v, bad = 0;

	for (v = 0; v < 128; v++) {
		bad += !put(out, 0x80);
		usleep(gap);
		bad += !put(out, 0x00);
		usleep(gap);
		bad += !put(out, v);
		usleep(gap);
	}
	printf("%d bytes found no room\n", bad);
	return 0;
}

int main(int argc, char **argv)
{
	int o;

	while ((o = getopt(argc, argv, "p:g:s:")) != -1) {
		if (o == 'p')
			base = strtoul(optarg, NULL, 0);
		else if (o == 'g')
			gap = strtoul(optarg, NULL, 0);
		else if (o == 's')
			settle = strtoul(optarg, NULL, 0);
		else
			return 1;
	}
	if (optind >= argc)
		return 1;
	if (ioperm(base, 3, 1)) {
		perror("mqengine: ioperm");
		return 2;
	}
	io.base = base;
	io.control = MQ_C_IDLE;
	if (!strcmp(argv[optind], "init"))
		return init();
	if (!strcmp(argv[optind], "look"))
		return look();
	if (!strcmp(argv[optind], "recv") && optind + 1 < argc)
		return recv_for(atof(argv[optind + 1]));
	if (!strcmp(argv[optind], "sweep") && optind + 1 < argc)
		return sweep(atoi(argv[optind + 1]));
	if (!strcmp(argv[optind], "loop") && optind + 2 < argc)
		return loop(atoi(argv[optind + 1]), atoi(argv[optind + 2]));
	if (!strcmp(argv[optind], "send") && optind + 2 < argc)
		return send(atoi(argv[optind + 1]), argc - optind - 2,
			    argv + optind + 2);
	return 1;
}
