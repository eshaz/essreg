// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * flyer: a parallel port MIDI interface from user space, by port I/O, with no
 * driver and no interrupt: a first look at MOTU's PC-MIDI Flyer on the protocol
 * of its MIDI Timepiece AV, which snd-mtpav speaks. As root.
 *
 *   flyer [-p PORT] look              the registers, and what each control bit
 *                                     does to the status
 *   flyer [-p PORT] raw TOKEN...      d XX: data, c XX: control, s: read status,
 *                                     r: read data, x: read control, e XX: ECR,
 *                                     E: read ECR, u N: wait N us
 *   flyer [-p PORT] init              0xF5 p 0xFE for ports 0 to 7, as
 *                                     snd-mtpav's scan does
 *   flyer [-p PORT] send OUT BYTE...  OUT 0 for all, 1 for A, 2 for B
 *   flyer [-p PORT] recv SECONDS      what comes in, by port
 *   flyer [-p PORT] watch SECONDS     each change of the status lines, reading
 *                                     no byte
 *   flyer [-p PORT] trace BYTE...     each byte, then the status lines' changes
 *                                     for 2 ms: a byte going out on MIDI holds
 *                                     the unit busy for about 320 us
 *   flyer [-p PORT] ask BYTE...       the bytes as they are, then whatever the
 *                                     unit sends back in 300 ms
 *   flyer [-p PORT] rate OUT N        N bytes as fast as the unit takes them: a
 *                                     unit that sends them takes 3125 a second
 *   flyer [-p PORT] sweep             a note after 0xF5 p for all 256 p: the
 *                                     note says p, the velocity its top bit
 *   flyer [-p PORT] try [US]          every idle state of the control lines and
 *                                     each line as the strobe, a note a pattern,
 *                                     with the data lines high between bytes
 *
 * Bytes out go on the data lines, latched by a falling edge of control bit 2
 * once status bit 7 (ready) is up. Status bit 3 says that a byte waits, and it
 * comes back two bits at a time on status bits 5 and 4, low bits first, each
 * pair on control bit 3. A byte 0xF5 and a port number says where the bytes
 * after it belong, in both directions.
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
#define ECR 0x402 // past the port's base, on an ECP port: the mode in bits 7:5

#define S_READY 0x80
#define S_ACK 0x40
#define S_IN1 0x20
#define S_IN0 0x10
#define S_BYTE 0x08

#define C_WRITE 0x04
#define C_READ 0x08
#define C_INT 0x10

#define READY_POLLS 10000 // of 10 us, as snd-mtpav's

static unsigned int base = 0x378;

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

static int ready(void)
{
	int n;

	for (n = 0; n < READY_POLLS; n++) {
		if (inb(base + STATUS) & S_READY)
			return 1;
		wait_us(10);
	}
	return 0;
}

// one byte, with what snd-mtpav does around it; 0 if the unit never was ready
static int put(unsigned char b)
{
	unsigned char c;

	if (!ready())
		return 0;
	c = inb(base + CONTROL) & 0x1C;
	outb(b, base + DATA);
	outb(c & ~C_WRITE, base + CONTROL);
	outb(c | C_WRITE, base + CONTROL);
	return 1;
}

static int get(void)
{
	unsigned char c = inb(base + CONTROL) & 0x1C;
	unsigned char v = 0;
	int i;

	for (i = 0; i < 4; i++) {
		unsigned char s;

		outb(c | C_READ, base + CONTROL);
		s = inb(base + STATUS);
		outb(c & ~C_READ, base + CONTROL);
		v |= ((s & (S_IN0 | S_IN1)) >> 4) << (i * 2);
	}
	return v;
}

static void show(const char *what)
{
	printf("%-14s data %02x status %02x control %02x ecr %02x\n", what,
	       inb(base + DATA), inb(base + STATUS), inb(base + CONTROL),
	       inb(base + ECR));
}

static int look(void)
{
	unsigned char c = inb(base + CONTROL);
	int bit;

	show("as found");
	for (bit = 0; bit < 5; bit++) {
		char name[32];

		outb(c ^ (1 << bit), base + CONTROL);
		wait_us(100);
		snprintf(name, sizeof(name), "control bit %d", bit);
		show(name);
		outb(c, base + CONTROL);
		wait_us(100);
	}
	// bit 5 turns the data lines around where the port can
	outb(c | 0x20, base + CONTROL);
	wait_us(100);
	show("data lines in");
	outb(c, base + CONTROL);
	outb(0x55, base + DATA);
	wait_us(10);
	show("data 55h");
	outb(0xAA, base + DATA);
	wait_us(10);
	show("data AAh");
	outb(0, base + DATA);
	show("as left");
	return 0;
}

static int raw(int argc, char **argv)
{
	int i;

	for (i = 0; i < argc; i++) {
		const char *t = argv[i];

		if (!strcmp(t, "s")) {
			printf("status %02x\n", inb(base + STATUS));
		} else if (!strcmp(t, "r")) {
			printf("data %02x\n", inb(base + DATA));
		} else if (!strcmp(t, "x")) {
			printf("control %02x\n", inb(base + CONTROL));
		} else if (!strcmp(t, "E")) {
			printf("ecr %02x\n", inb(base + ECR));
		} else if (i + 1 < argc && !strcmp(t, "e")) {
			outb(strtoul(argv[++i], NULL, 16), base + ECR);
		} else if (i + 1 < argc && !strcmp(t, "d")) {
			outb(strtoul(argv[++i], NULL, 16), base + DATA);
		} else if (i + 1 < argc && !strcmp(t, "c")) {
			outb(strtoul(argv[++i], NULL, 16), base + CONTROL);
		} else if (i + 1 < argc && !strcmp(t, "u")) {
			wait_us(strtoul(argv[++i], NULL, 0));
		} else {
			fprintf(stderr, "flyer: what is %s?\n", t);
			return 1;
		}
	}
	return 0;
}

static int init(void)
{
	int p;

	outb(C_WRITE, base + CONTROL);
	for (p = 0; p < 8; p++)
		if (!put(0xF5) || !put(p) || !put(0xFE)) {
			printf("not ready at port %d: status %02x\n", p,
			       inb(base + STATUS));
			return 1;
		}
	printf("all ready: status %02x\n", inb(base + STATUS));
	return 0;
}

static int send(int argc, char **argv)
{
	int i;

	outb(C_WRITE, base + CONTROL);
	if (!put(0xF5) || !put(strtoul(argv[0], NULL, 0))) {
		printf("not ready: status %02x\n", inb(base + STATUS));
		return 1;
	}
	for (i = 1; i < argc; i++)
		if (!put(strtoul(argv[i], NULL, 16))) {
			printf("not ready after %d bytes: status %02x\n", i - 1,
			       inb(base + STATUS));
			return 1;
		}
	printf("sent %d bytes: status %02x\n", argc - 1, inb(base + STATUS));
	return 0;
}

// a byte with this strobe from this idle state, then the data lines high, which
// may be what feeds the unit
static void pulse(unsigned char idle, unsigned char line, unsigned char b,
		  unsigned int us)
{
	outb(b, base + DATA);
	wait_us(us);
	outb(idle ^ line, base + CONTROL);
	wait_us(us);
	outb(idle, base + CONTROL);
	wait_us(us);
	outb(0xFF, base + DATA);
	wait_us(320); // a MIDI byte
}

// the note says the idle state and the strobe, the velocity the port selected
static int try_all(unsigned int us)
{
	static const unsigned char lines[] = { 1, 2, 4, 8 };
	int idle, l, p;

	outb(0xFF, base + DATA);
	for (idle = 0; idle < 16; idle++)
		for (l = 0; l < 4; l++)
			for (p = 0; p < 3; p++) {
				outb(idle, base + CONTROL);
				wait_us(us);
				if (p) {
					pulse(idle, lines[l], 0xF5, us);
					pulse(idle, lines[l], p, us);
				}
				pulse(idle, lines[l], 0x90, us);
				pulse(idle, lines[l], idle | l << 4, us);
				pulse(idle, lines[l], 1 + p, us);
				wait_us(2000);
			}
	outb(C_WRITE, base + CONTROL);
	printf("done: status %02x\n", inb(base + STATUS));
	return 0;
}

static int trace(int argc, char **argv)
{
	int i;

	outb(C_WRITE, base + CONTROL);
	for (i = 0; i < argc; i++) {
		double t0, end;
		int last = -1;

		if (!put(strtoul(argv[i], NULL, 16))) {
			printf("%s: not ready, status %02x\n", argv[i],
			       inb(base + STATUS));
			continue;
		}
		t0 = now();
		end = t0 + 0.002;
		printf("%s:", argv[i]);
		while (now() < end) {
			int s = inb(base + STATUS) & 0xF8;

			if (s != last) {
				printf(" %.0f:%02x", (now() - t0) * 1e6, s);
				last = s;
			}
		}
		printf("\n");
	}
	return 0;
}

static int ask(int argc, char **argv)
{
	double end;
	int i, n = 0;

	outb(C_WRITE, base + CONTROL);
	while (inb(base + STATUS) & S_BYTE)
		get();
	for (i = 0; i < argc; i++)
		put(strtoul(argv[i], NULL, 16));
	outb(0xFF, base + DATA);
	end = now() + 0.3;
	printf("back:");
	while (now() < end)
		if (inb(base + STATUS) & S_BYTE) {
			printf(" %02x", get());
			n++;
		}
	printf(" (%d)\n", n);
	return 0;
}

static int rate(int out, int n)
{
	double t0;
	int i;

	outb(C_WRITE, base + CONTROL);
	if (!put(0xF5) || !put(out))
		return 1;
	t0 = now();
	for (i = 0; i < n; i++)
		if (!put(0xF8))
			break;
	printf("port %d: %d of %d bytes in %.1f ms, %.0f a second, status %02x\n",
	       out, i, n, (now() - t0) * 1e3, i / (now() - t0),
	       inb(base + STATUS));
	return 0;
}

static int sweep(void)
{
	int p;

	outb(C_WRITE, base + CONTROL);
	for (p = 0; p < 256; p++) {
		put(0xF5);
		put(p);
		put(0x90);
		put(p & 0x7F);
		put(1 + (p >> 7));
		outb(0xFF, base + DATA);
		wait_us(1500);
	}
	printf("swept: status %02x\n", inb(base + STATUS));
	return 0;
}

static int watch(double secs)
{
	double start = now(), end = start + secs;
	int last = -1;
	unsigned long n = 0;

	while (now() < end) {
		int s = inb(base + STATUS) & 0xF8;

		if (s != last) {
			if (n++ < 200)
				printf("%9.5f status %02x\n", now() - start, s);
			last = s;
		}
	}
	printf("%lu changes\n", n);
	return 0;
}

static int recv_for(double secs)
{
	double start = now(), end = start + secs;
	int state = 0, port = -1;
	unsigned long n = 0;

	outb(C_WRITE, base + CONTROL);
	while (now() < end) {
		int b;

		if (!(inb(base + STATUS) & S_BYTE)) {
			usleep(100);
			continue;
		}
		b = get();
		n++;
		if (b >= 0xF8) {
			printf("%9.4f port %d: %02x\n", now() - start, port, b);
		} else if (state) {
			port = b;
			state = 0;
			printf("%9.4f port now %d\n", now() - start, port);
		} else if (b == 0xF5) {
			state = 1;
		} else {
			printf("%9.4f port %d: %02x\n", now() - start, port, b);
		}
		fflush(stdout);
	}
	printf("%lu bytes in %.1f s, status %02x\n", n, secs, inb(base + STATUS));
	return 0;
}

int main(int argc, char **argv)
{
	int a = 1;

	if (a + 1 < argc && !strcmp(argv[a], "-p")) {
		base = strtoul(argv[a + 1], NULL, 16);
		a += 2;
	}
	if (a >= argc) {
		fprintf(stderr, "usage: flyer [-p PORT] look | raw TOKEN... | init"
				" | send OUT BYTE... | recv SECONDS | watch SECONDS"
				" | trace BYTE... | ask BYTE... | rate OUT N | sweep"
				" | try [US]\n");
		return 1;
	}
	if (ioperm(base, 3, 1) || ioperm(base + ECR, 1, 1)) {
		perror("flyer: ioperm");
		return 2;
	}
	if (!strcmp(argv[a], "look"))
		return look();
	if (!strcmp(argv[a], "raw"))
		return raw(argc - a - 1, argv + a + 1);
	if (!strcmp(argv[a], "init"))
		return init();
	if (!strcmp(argv[a], "send") && argc - a >= 2)
		return send(argc - a - 1, argv + a + 1);
	if (!strcmp(argv[a], "ask") && argc - a >= 2)
		return ask(argc - a - 1, argv + a + 1);
	if (!strcmp(argv[a], "rate") && argc - a >= 3)
		return rate(atoi(argv[a + 1]), atoi(argv[a + 2]));
	if (!strcmp(argv[a], "sweep"))
		return sweep();
	if (!strcmp(argv[a], "trace") && argc - a >= 2)
		return trace(argc - a - 1, argv + a + 1);
	if (!strcmp(argv[a], "watch") && argc - a >= 2)
		return watch(atof(argv[a + 1]));
	if (!strcmp(argv[a], "try"))
		return try_all(a + 1 < argc ? strtoul(argv[a + 1], NULL, 0) : 20);
	if (!strcmp(argv[a], "recv") && argc - a >= 2)
		return recv_for(atof(argv[a + 1]));
	fprintf(stderr, "flyer: what is %s?\n", argv[a]);
	return 1;
}
