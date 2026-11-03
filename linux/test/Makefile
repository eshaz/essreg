# SPDX-License-Identifier: GPL-3.0-or-later
#
# es1869test, the loopback test suite of snd-es1869: `make` for this
# machine, `make CFLAGS_ARCH=-march=pentium2` as the Armada's kit builds it.
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>

CC ?= gcc
CFLAGS_ARCH ?=
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -Wall -Wextra -Werror -fno-math-errno $(CFLAGS_ARCH)
# essreg's 3-D measurement and its simulated effect, as ess3d has them
SRC = ../../src
CPPFLAGS += -DESS_HOST -I$(SRC)
LDLIBS = -lasound -lpthread -lm

OBJS = main.o run.o cases.o c_util.o c_probe.o c_dig.o c_fm.o c_a1.o c_midi.o \
	c_buf.o c_mix.o c_dual.o c_rate.o c_3d.o c_sys.o \
	an.o settab.o fmnote.o hal_alsa.o hal_sim.o
SHARED = s3dmeas.o s3dsim.o

es1869test: $(OBJS) $(SHARED)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(SHARED) $(LDLIBS)

$(OBJS): et.h hal.h an.h cases.h run.h settab.h \
	../es1869/settings.def ../es1869/settings_kinds.h

$(SHARED): %.o: $(SRC)/%.c $(SRC)/%.h $(SRC)/esstypes.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

c_3d.o hal_sim.o: $(SRC)/s3dmeas.h $(SRC)/s3dsim.h

clean:
	rm -f es1869test $(OBJS) $(SHARED)

.PHONY: clean
