# SPDX-License-Identifier: GPL-3.0-or-later
#
# es1869clock, the clock logger, and es1869scope, the framebuffer display:
# `make` for this machine, `make CFLAGS_ARCH=-march=pentium2` as the
# Armada's kit builds it.
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>

CC ?= gcc
CFLAGS_ARCH ?=
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -Wall -Wextra -Werror $(CFLAGS_ARCH)

all: es1869clock es1869scope

es1869clock: es1869clock.o phase.o
	$(CC) $(LDFLAGS) -o $@ $^ -lasound -lm

es1869scope: es1869scope.o tie.o draw.o fb.o plot.o fft.o spectro.o loop.o \
	     loopview.o eq.o
	$(CC) $(LDFLAGS) -o $@ $^ -lasound -lz -lm

es1869clock.o: es1869clock.c phase.h
phase.o: phase.c phase.h
tie.o: tie.c tie.h
es1869scope.o: es1869scope.c draw.h fb.h plot.h tie.h spectro.h loopview.h
draw.o: draw.c draw.h
fb.o: fb.c fb.h draw.h
plot.o: plot.c plot.h draw.h
fft.o: fft.c fft.h
loop.o: loop.c loop.h tie.h
spectro.o: spectro.c spectro.h fft.h draw.h plot.h
loopview.o: loopview.c loopview.h loop.h draw.h plot.h tie.h eq.h
eq.o: eq.c eq.h

clean:
	rm -f es1869clock es1869scope *.o

.PHONY: all clean
