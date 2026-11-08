/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * proto.h: MOTU's PC-MIDI Flyer on a parallel port, with no kernel API, shared
 * by the driver, by linux/tools/flyer.c and by tests/test_flyer.py. The
 * includer defines fl_out(io, reg, v), fl_in(io, reg), fl_us(n) and fl_ms(n).
 *
 * The unit has two ports, A and B, each an input and an output, and no byte
 * says which one it belongs to: the control lines do. Each output has a strobe
 * and each input a read line, and all of them idle high.
 *
 *            write   read
 *   port A   15h     16h     nSTROBE, nAUTOFD
 *   port B   10h     1Ch     nINIT, nSELECTIN
 *   command  11h             nSTROBE and nINIT together
 *
 * Status bit 7 says the unit has room, and four bytes may then go, to either
 * output. It keeps seven and room comes back with three left, which is 960 us
 * of sending on one output and 480 us with both running. Eight bytes in a row
 * make the queue look empty.
 *
 * Status bit 3 at idle says a byte waits on some input, and with that input's
 * read line on it says whether it is that one. The byte comes two bits at a
 * time on status bits 5 and 4, low bits first, one pair each time the read line
 * comes back on. Each input is a ring of 16 that holds 15: a 16th byte makes it
 * look empty, so at full speed it has to be read within 4.8 ms.
 *
 * Status bit 6 is the port's interrupt. It rises every 16.4 ms whatever is
 * happening, and while a byte waits it drops for 16 us every 256 us, so one
 * that is missed comes again. Room to send raises nothing.
 *
 * MOTU's driver times everything by the length of a port access, about a
 * microsecond on a parallel port of the time: a rest is three accesses of the
 * idle control, and six on port A.
 *
 * The unit has no supply of its own and runs off the data lines, so they go
 * back to FFh after every byte. With them low for a second it loses bytes and
 * reads FFh until it has been fed again.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef FLYER_PROTO_H
#define FLYER_PROTO_H

#define FL_DATA 0
#define FL_STATUS 1
#define FL_CONTROL 2

#define FL_S_ROOM 0x80
#define FL_S_PULSE 0x40
#define FL_S_PAIR 0x30
#define FL_S_BYTE 0x08

#define FL_C_IRQ 0x10
#define FL_C_IDLE 0x14
#define FL_C_COMMAND 0x11

#define FL_BURST 4 /* bytes for each time FL_S_ROOM is seen */
#define FL_RING 15 /* bytes an input holds */
#define FL_FEED_MS 50 /* of FFh on the data lines before the first access */
#define FL_INIT 0x07 /* the command MOTU's driver sends at every open */
#define FL_PULSE_POLLS 240 /* 100 us each, over two of the 16.4 ms pulses */

struct fl_io {
	unsigned long base;
};

static inline unsigned char fl_strobe(unsigned int port)
{
	return port ? 0x10 : 0x15;
}

static inline unsigned char fl_reader(unsigned int port)
{
	return port ? 0x1C : 0x16;
}

static inline void fl_rest(struct fl_io *io, unsigned int port, unsigned int n)
{
	for (n *= port ? 3 : 6; n; n--)
		fl_out(io, FL_CONTROL, FL_C_IDLE);
}

static inline int fl_room(struct fl_io *io)
{
	return !!(fl_in(io, FL_STATUS) & FL_S_ROOM);
}

/* whether a byte waits on some input */
static inline int fl_any(struct fl_io *io)
{
	return !!(fl_in(io, FL_STATUS) & FL_S_BYTE);
}

/* whether one waits on this input: if so the read line stays on for fl_get() */
static inline int fl_waiting(struct fl_io *io, unsigned int in)
{
	if (!fl_any(io))
		return 0;
	fl_out(io, FL_CONTROL, fl_reader(in));
	if (fl_in(io, FL_STATUS) & FL_S_BYTE)
		return 1;
	fl_rest(io, in, 1);
	return 0;
}

static inline unsigned char fl_get(struct fl_io *io, unsigned int in)
{
	unsigned char v = 0;
	int n;

	for (n = 0; n < 8; n += 2) {
		if (n)
			fl_out(io, FL_CONTROL, fl_reader(in));
		v |= ((fl_in(io, FL_STATUS) & FL_S_PAIR) >> 4) << n;
		fl_rest(io, in, 1);
	}
	return v;
}

static inline void fl_put(struct fl_io *io, unsigned int out, unsigned char b)
{
	fl_out(io, FL_DATA, b);
	fl_out(io, FL_CONTROL, fl_strobe(out));
	fl_rest(io, out, 1);
	fl_out(io, FL_DATA, 0xFF);
	fl_out(io, FL_CONTROL, FL_C_IDLE);
}

static inline void fl_command(struct fl_io *io, unsigned char cmd)
{
	int n;

	fl_rest(io, 0, 4);
	fl_out(io, FL_DATA, cmd);
	fl_rest(io, 0, 8);
	for (n = 0; n < 8; n++)
		fl_out(io, FL_CONTROL, FL_C_COMMAND);
	fl_rest(io, 0, 10);
	fl_out(io, FL_DATA, 0xFF);
	fl_out(io, FL_CONTROL, FL_C_IDLE);
}

/*
 * The model the unit says it is, in status bits 5 and 4 with both read lines
 * on: 2 on a Flyer. A byte that waits makes it 0, as does a port with nothing
 * on it, and the question sends a byte 11h out of output A.
 */
static inline int fl_id(struct fl_io *io)
{
	unsigned char s;
	int n;

	if (fl_any(io))
		return 0;
	fl_rest(io, 0, 1);
	if (fl_in(io, FL_STATUS) & FL_S_PAIR)
		return 0;
	fl_rest(io, 0, 1);
	fl_out(io, FL_DATA, 0x11);
	for (n = 0; n < 200; n++)
		fl_out(io, FL_CONTROL, 0x1F);
	fl_out(io, FL_CONTROL, 0x1E);
	fl_out(io, FL_CONTROL, 0x1E);
	s = fl_in(io, FL_STATUS);
	fl_out(io, FL_DATA, 0xFE);
	fl_rest(io, 0, 1);
	fl_out(io, FL_DATA, 0xFF);
	fl_out(io, FL_CONTROL, FL_C_IDLE);
	return (s & FL_S_PAIR) >> 4;
}

/* the 16.4 ms pulse, which needs no byte sent and nothing else on a port makes */
static inline int fl_alive(struct fl_io *io)
{
	unsigned char was = fl_in(io, FL_STATUS) & FL_S_PULSE;
	int n;

	for (n = 0; n < FL_PULSE_POLLS; n++) {
		if ((fl_in(io, FL_STATUS) & FL_S_PULSE) != was)
			return 1;
		fl_us(100);
	}
	return 0;
}

/* the supply first, then what MOTU's driver sends at open, which clears the rings */
static inline void fl_start(struct fl_io *io)
{
	fl_out(io, FL_CONTROL, FL_C_IDLE);
	fl_out(io, FL_DATA, 0xFF);
	fl_ms(FL_FEED_MS);
	fl_command(io, FL_INIT);
}

#endif /* FLYER_PROTO_H */
