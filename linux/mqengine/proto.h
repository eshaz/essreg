/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The Opcode MIDIEngine 2Port/SE's protocol on a parallel port, for the kernel
 * driver and linux/tools/mqengine.c. The includer defines mq_out(io, reg, v),
 * mq_in(io, reg), mq_us(n) and mq_ms(n).
 *
 * The data lines' low two bits choose what the status lines show. With FEh,
 * bit 7 low says that a byte waits. With FDh, bit 7 low says that the unit has
 * room for one.
 *
 * Bytes in: status bits 5 and 3 show two of the waiting byte's bits, b7 and b5
 * for FFh, b6 and b4 for FEh, b3 and b1 for FDh, b2 and b0 for FCh. AUTOFD
 * takes the byte away, and while it is on, status bit 7 low says that the byte
 * came from input B.
 *
 * Bytes out: b | CCh and then b on the lines, then STROBE. While STROBE is on,
 * the lines change to b with bit 1 clear and bits 4 and 0 set, and D5 says
 * which output, and it has to hold past the end of STROBE, or the byte goes
 * to B. Bit 5 of the byte itself reaches the unit only as STROBE starts.
 *
 * Between two bytes either way the unit needs time that its flags don't cover,
 * so a caller waits MQ_GAP_US from one to the next.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef MQENGINE_PROTO_H
#define MQENGINE_PROTO_H

#define MQ_DATA 0
#define MQ_STATUS 1
#define MQ_CONTROL 2

#define MQ_S_FLAG 0x80 // low: what the data lines ask, as above
#define MQ_S_HI 0x20
#define MQ_S_LO 0x08
#define MQ_S_ID (MQ_S_FLAG | MQ_S_HI | MQ_S_LO)

#define MQ_D_WAITING 0xFE
#define MQ_D_ROOM 0xFD

#define MQ_C_STROBE 0x01
#define MQ_C_NEXT 0x02 // AUTOFD
#define MQ_C_RUN 0x04 // INIT high
#define MQ_C_SELECT 0x08 // SELECTIN
#define MQ_C_IRQ 0x10 // a byte waiting raises the port's IRQ
#define MQ_C_IDLE (MQ_C_RUN | MQ_C_SELECT)

#ifndef MQ_SETTLE_US
#define MQ_SETTLE_US 2 // the unit kept up with no wait at all, on the Armada
#endif
#define MQ_GAP_US 60 // 30 stayed exact on the Armada, and 20 lost bytes
#define MQ_RESET_MS 110 // two ticks of the PC's timer, as Opcode's driver waits

struct mq_io {
	unsigned long base;
	unsigned char control; // what the control lines hold between accesses
};

// the status lines for the data lines' value v
static inline unsigned char mq_asked(struct mq_io *io, unsigned char v)
{
	mq_out(io, MQ_DATA, v);
	mq_us(MQ_SETTLE_US);
	return mq_in(io, MQ_STATUS);
}

static inline int mq_room(struct mq_io *io)
{
	return !(mq_asked(io, MQ_D_ROOM) & MQ_S_FLAG);
}

static inline int mq_waiting(struct mq_io *io)
{
	return !(mq_asked(io, MQ_D_WAITING) & MQ_S_FLAG);
}

// a byte to output 0 (A) or 1 (B), once mq_room has said yes
static inline void mq_put(struct mq_io *io, unsigned int out, unsigned char b)
{
	unsigned char last = (b & 0xDD) | 0x11 | (out ? 0x20 : 0);

	mq_out(io, MQ_DATA, b | 0xCC);
	mq_out(io, MQ_DATA, b);
	mq_us(MQ_SETTLE_US);
	mq_out(io, MQ_CONTROL, io->control | MQ_C_STROBE);
	mq_us(MQ_SETTLE_US);
	mq_out(io, MQ_DATA, last);
	mq_us(MQ_SETTLE_US);
	mq_out(io, MQ_CONTROL, io->control);
	mq_us(MQ_SETTLE_US);
	mq_out(io, MQ_DATA, last | 0xDD);
}

// two of the waiting byte's bits, for the data lines' value v
static inline unsigned char mq_two(struct mq_io *io, unsigned char v,
				   unsigned char hi, unsigned char lo)
{
	unsigned char s = mq_asked(io, v);

	return (s & MQ_S_HI ? hi : 0) | (s & MQ_S_LO ? lo : 0);
}

// the byte that waits and its input, 0 (A) or 1 (B), or 0 if none does
static inline int mq_get(struct mq_io *io, unsigned char *b, unsigned int *in)
{
	unsigned char s;

	if (!mq_waiting(io))
		return 0;
	*b = mq_two(io, 0xFF, 0x80, 0x20) | mq_two(io, 0xFD, 0x08, 0x02) |
	     mq_two(io, 0xFE, 0x40, 0x10) | mq_two(io, 0xFC, 0x04, 0x01);
	mq_out(io, MQ_CONTROL, io->control | MQ_C_NEXT);
	mq_us(MQ_SETTLE_US);
	s = mq_in(io, MQ_STATUS);
	mq_out(io, MQ_CONTROL, io->control);
	mq_us(MQ_SETTLE_US);
	*in = !(s & MQ_S_FLAG);
	return 1;
}

// the unit reset and left running, or nonzero and three status bytes if the
// port doesn't answer as a 2Port/SE does. Sleeps
static inline int mq_reset(struct mq_io *io, unsigned char *found)
{
	mq_out(io, MQ_CONTROL, MQ_C_SELECT);
	mq_out(io, MQ_DATA, 0);
	mq_ms(MQ_RESET_MS);
	found[0] = mq_asked(io, 3);
	found[1] = mq_asked(io, 0);
	mq_out(io, MQ_CONTROL, MQ_C_SELECT | MQ_C_NEXT);
	mq_ms(1);
	found[2] = mq_in(io, MQ_STATUS);
	mq_out(io, MQ_CONTROL, MQ_C_SELECT);
	mq_ms(1);
	mq_out(io, MQ_DATA, 0xFF);
	io->control = MQ_C_IDLE;
	mq_out(io, MQ_CONTROL, io->control);
	mq_ms(MQ_RESET_MS);
	return (found[0] & MQ_S_ID) != (MQ_S_HI | MQ_S_LO) ||
	       (found[1] & MQ_S_ID) != MQ_S_ID || (found[2] & MQ_S_ID);
}

#endif
