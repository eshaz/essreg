# SPDX-License-Identifier: GPL-2.0-or-later
#
# The ES1869 driver: snd-es1869, and the ESFM synthesizer snd-es1869-esfm.

snd-es1869-y := main.o hw.o pcm.o mirror.o dual.o mixer.o settings.o \
		esfm.o regs_hwdep.o proc.o resample.o clock.o

obj-$(CONFIG_SND_ES1869) += snd-es1869.o

snd-es1869-esfm-y := esfm_synth.o esfm_gm.o

obj-$(CONFIG_SND_ES1869_ESFM) += snd-es1869-esfm.o
