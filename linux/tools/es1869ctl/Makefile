# SPDX-License-Identifier: GPL-3.0-or-later
#
# es1869ctl, the status and settings panel of snd-es1869: `make` for this
# machine, `make CFLAGS_ARCH=-march=pentium2` as the Armada's kit builds it.
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>

CC ?= gcc
CFLAGS_ARCH ?=
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -Wall -Wextra -Werror $(CFLAGS_ARCH)
CURSES := $(shell pkg-config --libs ncurses 2>/dev/null || echo -lncurses)
LDLIBS = -lasound $(CURSES)
TEST = ../../test
DRV = ../../es1869

es1869ctl: es1869ctl.o settab.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

es1869ctl.o: es1869ctl.c $(TEST)/settab.h $(DRV)/settings_kinds.h

settab.o: $(TEST)/settab.c $(TEST)/settab.h $(DRV)/settings.def \
	$(DRV)/settings_kinds.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f es1869ctl *.o

.PHONY: clean
