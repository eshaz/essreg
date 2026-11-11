# SPDX-License-Identifier: GPL-3.0-or-later
#
# es1869clock, the clock logger, and es1869scope, the framebuffer display:
# `make` for this machine, `make CFLAGS_ARCH=-march=pentium2` as the
# Armada's kit builds it. `make install` puts the programs in PREFIX's bin,
# and `make install-services` the OpenRC services (es1869cal, es1869eq) and
# the rotation of es1869eq's log in /etc, pointed at the programs there; a
# package passes DESTDIR and PREFIX=/usr.
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>

CC ?= gcc
INSTALL ?= install
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SYSCONFDIR ?= /etc
CONFIG := ../../config
CFLAGS_ARCH ?=
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -Wall -Wextra -Werror $(CFLAGS_ARCH)

all: es1869clock es1869scope

es1869clock: es1869clock.o phase.o
	$(CC) $(LDFLAGS) -o $@ $^ -lasound -lm

es1869scope: es1869scope.o tie.o draw.o fb.o plot.o fft.o spectro.o loop.o \
	     loopview.o eq.o zstep.o say.o eqstate.o ctl.o chain.o settab.o \
	     a1lin.o capfilt.o mixsave.o
	$(CC) $(LDFLAGS) -o $@ $^ -lasound -lz -lm -lpthread

es1869clock.o: es1869clock.c phase.h
phase.o: phase.c phase.h
tie.o: tie.c tie.h
es1869scope.o: es1869scope.c draw.h fb.h plot.h tie.h spectro.h loopview.h \
	       say.h eqstate.h
draw.o: draw.c draw.h
fb.o: fb.c fb.h draw.h
plot.o: plot.c plot.h draw.h
fft.o: fft.c fft.h
loop.o: loop.c loop.h tie.h
spectro.o: spectro.c spectro.h fft.h draw.h plot.h say.h
loopview.o: loopview.c loopview.h loop.h draw.h plot.h tie.h eq.h zstep.h \
	    say.h eqstate.h fft.h ctl.h a1lin.h mixsave.h \
	    ../../es1869/capfilt.h
	$(CC) $(CFLAGS) -I../../es1869 -c -o $@ $<
a1lin.o: a1lin.c a1lin.h ../../es1869/capfilt.h
	$(CC) $(CFLAGS) -I../../es1869 -c -o $@ $<
capfilt.o: ../../es1869/capfilt.c ../../es1869/capfilt.h \
	   ../../es1869/hw_compat.h
	$(CC) $(CFLAGS) -I../../es1869 -c -o $@ $<
eq.o: eq.c eq.h
mixsave.o: mixsave.c mixsave.h ctl.h say.h
zstep.o: zstep.c zstep.h
say.o: say.c say.h draw.h plot.h
eqstate.o: eqstate.c eqstate.h
ctl.o: ctl.c ctl.h
chain.o: chain.c chain.h ctl.h eq.h eqstate.h plot.h say.h spectro.h \
	 ../../test/settab.h
settab.o: ../../test/settab.c ../../test/settab.h ../../es1869/settings.def \
	  ../../es1869/settings_kinds.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 es1869clock es1869scope $(DESTDIR)$(BINDIR)

install-services:
	$(INSTALL) -d $(DESTDIR)$(SYSCONFDIR)/init.d \
		$(DESTDIR)$(SYSCONFDIR)/logrotate.d
	for s in es1869cal es1869eq; do \
		sed 's,/usr/local/bin/es1869scope,$(BINDIR)/es1869scope,' \
			$(CONFIG)/$$s.initd > $(DESTDIR)$(SYSCONFDIR)/init.d/$$s && \
		chmod 0755 $(DESTDIR)$(SYSCONFDIR)/init.d/$$s || exit 1; \
	done
	$(INSTALL) -m 0644 $(CONFIG)/es1869eq.logrotate \
		$(DESTDIR)$(SYSCONFDIR)/logrotate.d/es1869eq

clean:
	rm -f es1869clock es1869scope *.o

.PHONY: all install install-services clean
