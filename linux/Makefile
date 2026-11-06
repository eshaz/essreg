# SPDX-License-Identifier: GPL-2.0-or-later
#
# The ES1869 driver built outside the kernel tree, against a configured
# tree (KDIR, by default the running kernel's), and its kernel patch.
#
#   make                  the modules, in es1869/
#   make clean
#   make mqengine         the MIDIEngine's module, in mqengine/
#   make patch            es1869-driver.patch: the driver as sound/isa/es1869,
#                         and mqengine-driver.patch: sound/drivers/mqengine
#   make deploy HOST=...  build on that machine over SSH and copy the result
#   make dist             es1869test-VERSION.tar.gz: the test suite and the
#                         Linux essreg with the essreg files they build
#
# A kernel built with the driver's Kconfig gets its CONFIG_SND_ES1869_*
# defaults from there; here they come from es1869/oot_config.h.

KDIR ?= /lib/modules/$(shell uname -r)/build
ARCH ?= $(shell uname -m | sed -e 's/i.86/i386/' -e 's/x86_64/x86/')

modules:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) M=$(CURDIR)/es1869 \
		CONFIG_SND_ES1869=m CONFIG_SND_ES1869_ESFM=m modules

mqengine:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) M=$(CURDIR)/mqengine \
		CONFIG_SND_MQENGINE=m modules

clean:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) M=$(CURDIR)/es1869 clean
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) M=$(CURDIR)/mqengine clean

# against a kernel source tree, as KSRC (by default the build tree)
KSRC ?= $(KDIR)
patch:
	python3 ../tools/es1869patch.py --ksrc $(KSRC) -o es1869-driver.patch
	python3 ../tools/mqenginepatch.py --ksrc $(KSRC) -o mqengine-driver.patch

# the sources to HOST over SSH, built there against its running kernel
deploy:
	@test -n "$(HOST)" || { echo "make deploy HOST=user@machine"; exit 1; }
	tar -C es1869 --exclude='*.o' --exclude='*.ko' --exclude='*.mod*' \
		--exclude='.*.cmd' --exclude=modules.order \
		--exclude=Module.symvers -cf - . | \
		ssh $(HOST) 'mkdir -p es1869 && tar -C es1869 -xf - && \
		make -C /lib/modules/$$(uname -r)/build M=$$HOME/es1869 \
		CONFIG_SND_ES1869=m CONFIG_SND_ES1869_ESFM=m modules'

# a package's sources: make -C linux/test and make -C linux/tools/essreg
# in the unpacked tree
VERSION := $(shell sed -n 's/.*ET_VERSION "\(.*\)".*/\1/p' test/et.h)
DIST := es1869test-$(VERSION)
DIST_FILES := LICENSE docs/ES1869TEST.md docs/LINUX.md \
	linux/test/Makefile $(wildcard test/*.c test/*.h test/baseline/*) \
	linux/tools/essreg/Makefile linux/tools/essreg/esshw_linux.c \
	linux/tools/essreg/shim/i86.h \
	linux/tools/es1869ctl/Makefile linux/tools/es1869ctl/es1869ctl.c \
	linux/tools/a2stall.c \
	linux/es1869/settings.def linux/es1869/settings_kinds.h \
	linux/es1869/resample.c linux/es1869/resample.h \
	linux/es1869/hw_compat.h \
	linux/es1869/uapi/es1869.h \
	src/esstypes.h src/s3dmeas.c src/s3dmeas.h src/s3dsim.c src/s3dsim.h \
	src/main.c src/regs.c src/regs.h src/debug.c src/debug.h src/esshw.h
dist:
	tar -C .. --transform 's,^,$(DIST)/,' -czf $(DIST).tar.gz \
		$(patsubst test/%,linux/test/%,$(DIST_FILES))

.PHONY: modules clean patch deploy dist
