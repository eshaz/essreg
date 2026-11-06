// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * mqengine: a Music Quest MIDIEngine parallel port MIDI interface from user
 * space, by port I/O, with no driver and no interrupt. As root. Written for an
 * Opcode MIDIEngine 2port/SE, whose own driver wasn't found: the shape of the
 * protocol comes from the 8Port/SE's Cakewalk driver, and what differs from it
 * was measured on the unit.
 *
 *   mqengine [-p PORT] init              reset the unit and leave it ready
 *   mqengine [-p PORT] look              the lines
 *   mqengine [-p PORT] send OUT BYTE...  to port OUT, GAP us apart
 *   mqengine [-p PORT] recv SECONDS      what comes in
 *   mqengine [-p PORT] sweep OUT         80 00 v for every data byte v
 *
 * Bytes in: status bit 6 says that a byte waits, and the data lines' low two
 * bits show two of its bits on status bits 5 and 3: FFh b7 and b5, FEh b6 and
 * b4, FDh b3 and b1, FCh b2 and b0. A pulse of AUTOFD takes it away.
 *
 * Bytes out: status bit 5 says that the unit is ready, and a strobe sends the
 * byte on the data lines, which the unit reads after the strobe ends, so they
 * have to hold. A byte with bit 5 set doesn't come out, whatever the sequence
 * around it, the control lines or the pace, and it doesn't change what comes
 * after it. That is still open, so this unit can't yet send most notes.
 *
 * The 8Port/SE's driver strobes a port number with SELECTIN off and each byte
 * with SELECTIN on, as this does, but here every port number comes out of A.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <time.h>
#include <unistd.h>

#define DATA 0
#define STATUS 1
#define CONTROL 2

#define S_RX 0x40 // a byte waits
#define S_READY 0x20
#define S_HI 0x20
#define S_LO 0x08

#define C_STROBE 0x01
#define C_NEXT 0x02
#define C_INIT 0x04
#define C_SELECT 0x08
#define C_IDLE (C_INIT | C_SELECT)

#define READY_POLLS 4000 // as the driver's

static unsigned int base = 0x378, gap = 1000;
static unsigned int settle = 30; // us: the driver measures these at its start
static unsigned int hold = 60; // the unit reads the lines after a strobe
static int out_port = -1;

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

static void data(unsigned char v)
{
	outb(v, base + DATA);
}

static void control(unsigned char v)
{
	outb(v, base + CONTROL);
}

static unsigned char status(void)
{
	return inb(base + STATUS);
}

static int ready(void)
{
	int n;

	for (n = 0; n < READY_POLLS; n++)
		if (status() & S_READY)
			return 1;
	return 0;
}

// b on the data lines, strobed with the control lines c
static void strobe(unsigned char b, unsigned char c)
{
	data(b | 0xAA);
	data(b);
	wait_us(settle);
	control(c | C_STROBE);
	wait_us(settle);
	control(C_IDLE);
	wait_us(hold);
	data(b | 0xAA);
	data(0xFF);
	wait_us(settle);
}

static int put(int port, unsigned char b)
{
	if (port != out_port) {
		if (!ready())
			return 0;
		control(C_INIT);
		strobe(port, C_INIT);
		out_port = port;
	}
	if (!ready())
		return 0;
	strobe(b, C_IDLE);
	return 1;
}

// the two bits of the waiting byte that the data lines' value v shows
static unsigned char two(unsigned char v, unsigned char hi, unsigned char lo)
{
	unsigned char s;

	data(v);
	wait_us(settle);
	s = status();
	return (s & S_HI ? hi : 0) | (s & S_LO ? lo : 0);
}

// 1 and the byte that waits, which a pulse of AUTOFD then takes away, or 0
static int get(unsigned char *b)
{
	if (!(status() & S_RX))
		return 0;
	*b = two(0xFD, 0x08, 0x02) | two(0xFC, 0x04, 0x01) |
	     two(0xFE, 0x40, 0x10) | two(0xFF, 0x80, 0x20);
	control(C_IDLE | C_NEXT);
	wait_us(settle);
	control(C_IDLE);
	wait_us(settle);
	return 1;
}

static int init(void)
{
	unsigned char s1, s0, sn;

	control(0);
	data(0);
	usleep(120000);
	control(C_STROBE);
	usleep(2000);
	control(0);
	usleep(2000);
	data(1);
	wait_us(200);
	s1 = status();
	data(0);
	wait_us(200);
	s0 = status();
	control(C_NEXT);
	usleep(1000);
	sn = status();
	data(0);
	wait_us(200);
	control(0);
	usleep(1000);
	printf("data 01: status %02x, want bits 7 and 3 low\n", s1);
	printf("data 00: status %02x, want bit 7 high, bit 3 low\n", s0);
	printf("AUTOFD:  status %02x, want bit 7 low\n", sn);
	data(0xFF);
	control(C_IDLE);
	usleep(120000);
	printf("ready:   status %02x\n", status());
	return 0;
}

static int look(void)
{
	printf("data %02x status %02x control %02x\n", inb(base + DATA), status(),
	       inb(base + CONTROL));
	return 0;
}

static int recv_for(double secs)
{
	double end = now() + secs;
	unsigned long n = 0;
	unsigned char b;

	control(C_IDLE);
	data(0xFF);
	wait_us(settle);
	while (now() < end) {
		if (get(&b)) {
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

static int send(int port, int argc, char **argv)
{
	int i;

	control(C_IDLE);
	data(0xFF);
	for (i = 0; i < argc; i++) {
		if (!put(port, strtoul(argv[i], NULL, 16))) {
			printf("the unit never was ready, byte %d\n", i);
			return 3;
		}
		usleep(gap);
	}
	return 0;
}

static int sweep(int port)
{
	int v, bad = 0;

	control(C_IDLE);
	data(0xFF);
	for (v = 0; v < 128; v++) {
		bad += !put(port, 0x80);
		usleep(gap);
		bad += !put(port, 0x00);
		usleep(gap);
		bad += !put(port, v);
		usleep(gap);
	}
	printf("%d bytes not ready\n", bad);
	return 0;
}

int main(int argc, char **argv)
{
	int o;

	while ((o = getopt(argc, argv, "p:g:s:h:")) != -1) {
		if (o == 'p')
			base = strtoul(optarg, NULL, 0);
		else if (o == 'g')
			gap = strtoul(optarg, NULL, 0);
		else if (o == 's')
			settle = strtoul(optarg, NULL, 0);
		else if (o == 'h')
			hold = strtoul(optarg, NULL, 0);
		else
			return 1;
	}
	if (optind >= argc)
		return 1;
	if (ioperm(base, 3, 1)) {
		perror("mqengine: ioperm");
		return 2;
	}
	if (!strcmp(argv[optind], "init"))
		return init();
	if (!strcmp(argv[optind], "look"))
		return look();
	if (!strcmp(argv[optind], "recv") && optind + 1 < argc)
		return recv_for(atof(argv[optind + 1]));
	if (!strcmp(argv[optind], "sweep") && optind + 1 < argc)
		return sweep(atoi(argv[optind + 1]));
	if (!strcmp(argv[optind], "send") && optind + 2 < argc)
		return send(atoi(argv[optind + 1]), argc - optind - 2,
			    argv + optind + 2);
	if (!strcmp(argv[optind], "ask") && optind + 1 < argc) {
		int r = send(0, argc - optind - 1, argv + optind + 1);

		return r ? r : recv_for(0.3);
	}
	return 1;
}
