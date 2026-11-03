# SPDX-License-Identifier: GPL-3.0-or-later
#
# essreg, the ES1869 register utility, on Linux: essreg's own sources from
# src/, with its register access through snd-es1869's register hwdep.

SRC := ../../../src
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -Wall -Wextra -DESS_HOST -Ishim -I$(SRC)

essreg: $(SRC)/main.c $(SRC)/regs.c $(SRC)/debug.c esshw_linux.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f essreg

.PHONY: clean
