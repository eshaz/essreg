// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The Linux driver's chip core on the host, for tests/test_es1869hw.py: a
 * context the tests make, and its shadows and counters.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdlib.h>

#include "hw.h"

struct es1869_hw *lx_new(const struct es1869_io *io, u16 port, u16 cfg,
			 u16 fm)
{
	struct es1869_hw *hw = calloc(1, sizeof(*hw));

	if (!hw)
		return NULL;
	hw->io = io;
	hw->port = port;
	hw->cfg = cfg;
	hw->fm = fm;
	hw->fm_delay = 2;
	return hw;
}

void lx_free(struct es1869_hw *hw)
{
	free(hw);
}

u8 lx_mix(const struct es1869_hw *hw, u8 reg)
{
	return hw->mix[reg & 0x7F];
}

u8 lx_ctrl(const struct es1869_hw *hw, u8 reg)
{
	return hw->ctrl[(reg - 0xA0) & 0x1F];
}

void lx_set_imr(struct es1869_hw *hw, u8 imr)
{
	hw->imr = imr;
}

u8 lx_imr(const struct es1869_hw *hw)
{
	return hw->imr;
}

void lx_set_mpu(struct es1869_hw *hw, u16 port)
{
	hw->mpu = port;
}

void lx_set_port7(struct es1869_hw *hw, u8 val)
{
	hw->port7 = val;
}

int lx_desync(const struct es1869_hw *hw)
{
	return hw->desync;
}

const struct es1869_stats *lx_stats(const struct es1869_hw *hw)
{
	return &hw->stats;
}
