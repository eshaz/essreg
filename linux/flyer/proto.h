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
 * output. Room shows again once none of them waits behind a byte being sent:
 * 960 us after four into an idle unit, and every 1.28 ms while one output is
 * kept busy, when it has 320 us of sending left. It keeps seven bytes waiting
 * behind those being sent, and an eighth makes the queue look empty and loses
 * them: of nine bytes in a row into an idle unit only the first goes out.
 *
 * Status bit 3 at idle says a byte waits on some input, and with that input's
 * read line on it says whether it is that one. The byte comes two bits at a
 * time on status bits 5 and 4, low bits first, one pair each time the read line
 * comes back on. Each input is a ring of 16 that holds 15: a 16th byte makes it
 * look empty, so at full speed it has to be read within 4.8 ms.
 *
 * Status bit 6 is the port's interrupt. It is up for 240 us every 16.4 ms
 * whatever is happening, and while a byte waits it stays up but for a drop of
 * 16 us every 256 us, so one that is missed comes again. Room to send raises
 * nothing of its own. The port passes the line on only with control bit 4 set,
 * which no line carries to the unit, so the caller chooses it in fl_io's irq
 * and every control write here keeps it.
 *
 * MOTU's driver times everything by the length of a port access, about a
 * microsecond on a parallel port of the time: a rest is three accesses of the
 * idle control, and six on port A.
 *
 * The unit has no supply of its own and runs off the data lines, so they go
 * back to FFh after every byte. With them low for a second it loses bytes and
 * reads FFh until it has been fed again. A port that is off reads FFh too, and
 * a unit that reads so has neither room nor bytes.
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
#define FL_S_GONE 0xFF /* a port that is off, or a unit without its supply */

#define FL_C_IRQ 0x10
#define FL_C_IDLE 0x14
#define FL_C_COMMAND 0x11

#define FL_BURST 4 /* bytes for each time FL_S_ROOM is seen */
#define FL_RING 15 /* bytes an input holds */
#define FL_FEED_MS 50 /* of FFh on the data lines before the first access */
#define FL_INIT 0x07 /* the command MOTU's driver sends at every open */
#define FL_PULSE_POLLS 8000 /* about 5 us apart, over two 16.4 ms pulses */

struct fl_io {
	unsigned long base;
	unsigned char irq; /* FL_C_IRQ to let the port interrupt, or 0 */
};

static inline unsigned char fl_strobe(unsigned int port)
{
	return port ? 0x10 : 0x15;
}

static inline unsigned char fl_reader(unsigned int port)
{
	return port ? 0x1C : 0x16;
}

static inline void fl_ctl(struct fl_io *io, unsigned char c)
{
	fl_out(io, FL_CONTROL, (c & ~FL_C_IRQ) | io->irq);
}

static inline void fl_rest(struct fl_io *io, unsigned int port, unsigned int n)
{
	for (n *= port ? 3 : 6; n; n--)
		fl_ctl(io, FL_C_IDLE);
}

static inline int fl_room(struct fl_io *io)
{
	unsigned char s = fl_in(io, FL_STATUS);

	return s != FL_S_GONE && (s & FL_S_ROOM);
}

/*
 * The bytes that may go now: what is left of the last sight of room, or
 * FL_BURST once room shows again. The flag is read only when none is left,
 * so that no sight of it counts twice.
 */
static inline unsigned int fl_credit(struct fl_io *io, unsigned int left)
{
	if (left)
		return left;
	return fl_room(io) ? FL_BURST : 0;
}

/* whether a byte waits on some input */
static inline int fl_any(struct fl_io *io)
{
	unsigned char s = fl_in(io, FL_STATUS);

	return s != FL_S_GONE && (s & FL_S_BYTE);
}

/* whether one waits on this input: if so the read line stays on for fl_get() */
static inline int fl_waiting(struct fl_io *io, unsigned int in)
{
	if (!fl_any(io))
		return 0;
	fl_ctl(io, fl_reader(in));
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
			fl_ctl(io, fl_reader(in));
		v |= ((fl_in(io, FL_STATUS) & FL_S_PAIR) >> 4) << n;
		fl_rest(io, in, 1);
	}
	return v;
}

static inline void fl_put(struct fl_io *io, unsigned int out, unsigned char b)
{
	fl_out(io, FL_DATA, b);
	fl_ctl(io, fl_strobe(out));
	fl_rest(io, out, 1);
	fl_out(io, FL_DATA, 0xFF);
	fl_ctl(io, FL_C_IDLE);
}

static inline void fl_command(struct fl_io *io, unsigned char cmd)
{
	int n;

	fl_rest(io, 0, 4);
	fl_out(io, FL_DATA, cmd);
	fl_rest(io, 0, 8);
	for (n = 0; n < 8; n++)
		fl_ctl(io, FL_C_COMMAND);
	fl_rest(io, 0, 10);
	fl_out(io, FL_DATA, 0xFF);
	fl_ctl(io, FL_C_IDLE);
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
		fl_ctl(io, 0x1F);
	fl_ctl(io, 0x1E);
	fl_ctl(io, 0x1E);
	s = fl_in(io, FL_STATUS);
	fl_out(io, FL_DATA, 0xFE);
	fl_rest(io, 0, 1);
	fl_out(io, FL_DATA, 0xFF);
	fl_ctl(io, FL_C_IDLE);
	return (s & FL_S_PAIR) >> 4;
}

/*
 * The 16.4 ms pulse, which needs no byte sent and nothing else on a port makes.
 * The polls come close enough together to see a drop of 16 us.
 */
static inline int fl_alive(struct fl_io *io)
{
	unsigned char was = fl_in(io, FL_STATUS) & FL_S_PULSE;
	int n;

	for (n = 0; n < FL_PULSE_POLLS; n++) {
		if ((fl_in(io, FL_STATUS) & FL_S_PULSE) != was)
			return 1;
		fl_us(4);
	}
	return 0;
}

/* the supply first, then what MOTU's driver sends at open, which clears the rings */
static inline void fl_start(struct fl_io *io)
{
	fl_ctl(io, FL_C_IDLE);
	fl_out(io, FL_DATA, 0xFF);
	fl_ms(FL_FEED_MS);
	fl_command(io, FL_INIT);
}

#endif /* FLYER_PROTO_H */
