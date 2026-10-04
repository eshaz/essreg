# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test the FM recording device of build/ES1869.DRV (src/es1869/fmwave.asm,
fmdac.asm): wave-in device 1, "ESS AudioDrive FM Digital", which records
the FM synthesizer through mixer 7Fh bit 4 at 49716 Hz, and the playback
device 0 named after the Audio 2 DAC.

The driver runs in tests/drvemu.py against a model of the chip.
"""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_a1play import (A1Case, HAVE_UNICORN, OPT_A1_DEVICE,  # noqa: E402
                         OPT_A1_SHARED, OPT_DUAL, OPT_FM_RECORD,
                         MMSYSERR_BADDEVICEID, MMSYSERR_ALLOCATED,
                         WAVERR_BADFORMAT)

if HAVE_UNICORN:
    import drvemu
    from drvemu import (WIDM_GETNUMDEVS, WIDM_GETDEVCAPS, WIDM_CLOSE,
                        WIDM_ADDBUFFER, WIDM_START, WIDM_STOP, WIDM_RESET,
                        WIM_OPEN, WODM_GETDEVCAPS, DEVNODE)

MXDM_GETLINEINFO, MIXER_GETLINEINFOF_TARGETTYPE = 5, 4
TARGET_WAVEOUT, TARGET_WAVEIN = 1, 2
MIXERR_INVALLINE = 1024
FM_STATE = 0x132 + 0x54
FMF_OPEN, FMF_ROUTED = 1, 2
DEV_FLAGS, DEV_FM_DAC = 0x2A, 0x117
WAVE_FORMAT_QUERY = 1


class FMRecordTest(A1Case):
    def caps(self, e, dev, wave_in=True, size=0x30):
        buf = e.alloc(b"\xEE" * 0x30)
        ex = e.alloc(struct.pack("<II", size, buf))
        call = e.wid if wave_in else e.wod
        msg = WIDM_GETDEVCAPS if wave_in else WODM_GETDEVCAPS
        self.assertEqual(call(dev, msg, 0, ex, DEVNODE), 0)
        return e.rd(e.far_lin(buf), 0x30)

    def recording(self, e, mix7f=0x01):
        """device 1 open, a buffer queued and started"""
        e.chip.mixer[0x7F] = mix7f
        r, user = e.open_in()
        self.assertEqual(r, 0)
        self.assertEqual(e.wid(1, WIDM_ADDBUFFER, user,
                               e.header(b"\0" * 4096), 0x20), 0)
        self.assertEqual(e.wid(1, WIDM_START, user), 0)
        return user

    def test_two_devices(self):
        self.assertEqual(self.emu().wid(0, WIDM_GETNUMDEVS, 0, DEVNODE), 2)
        e = self.emu(opts=OPT_A1_DEVICE | OPT_A1_SHARED | OPT_DUAL)
        self.assertEqual(e.wid(0, WIDM_GETNUMDEVS, 0, DEVNODE), 1)
        e = self.emu(stock=True)
        self.assertEqual(e.wid(0, WIDM_GETNUMDEVS, 0, DEVNODE), 1)

    def test_caps(self):
        e = self.emu()
        caps = self.caps(e, 1)
        self.assertEqual(caps[6:38], b"ESS AudioDrive FM Digital (220)\0")
        # no standard format: 49716 Hz only, in stereo
        self.assertEqual(struct.unpack_from("<IH", caps, 0x26), (0, 2))
        # device 0 is ESS's recording device, as it was: ESS's code copies
        # the name's 32 bytes from its stack, what follows the NUL too
        ess = self.caps(self.emu(stock=True), 0)
        dev0 = self.caps(e, 0)
        self.assertEqual(dev0[:6] + dev0[38:], ess[:6] + ess[38:])
        self.assertEqual(dev0[6:38].split(b"\0")[0],
                         b"ESS AudioDrive Record (220)")

    def test_caps_short_buffer(self):
        e = self.emu()
        caps = self.caps(e, 1, size=12)
        self.assertEqual(caps[6:12], b"ESS Au")
        self.assertEqual(caps[12:16], b"\xEE" * 4)
        # room for the formats but not the channels
        caps = self.caps(e, 1, size=0x2A)
        self.assertEqual(caps[0x26:0x2C], b"\0\0\0\0\xEE\xEE")

    def test_device_2_is_ess_answer(self):
        e = self.emu()
        self.assertEqual(e.open_in(dev_id=2)[0], MMSYSERR_BADDEVICEID)

    def test_only_the_music_dac_format(self):
        e = self.emu()
        for kw in (dict(rate=44100), dict(rate=48000), dict(channels=1),
                   dict(bits=8)):
            with self.subTest(**kw):
                self.assertEqual(e.open_in(**kw)[0], WAVERR_BADFORMAT)
        self.assertEqual(e.open_in(flags=WAVE_FORMAT_QUERY)[0], 0)
        self.assertEqual(e.dev8(FM_STATE), 0)
        self.assertEqual(e.chip.log, [])

    def test_open_and_close(self):
        e = self.emu()
        e.set_dev8(DEV_FLAGS, e.dev8(DEV_FLAGS) | 1)    # DCdrift on
        r, user = e.open_in()
        self.assertEqual(r, 0)
        self.assertEqual(e.dev8(FM_STATE), FMF_OPEN)
        # DC drift removal off, ESS's recording at 48 kHz, and the program
        # told with its own instance
        self.assertEqual(e.dev8(DEV_FLAGS) & 1, 0)
        self.assertIn(("reg", 0xA1, 0xF0), e.chip.log)
        self.assertTrue(e.chip.mixer[0x71] & 0x20)
        self.assertEqual(e.callbacks[-1][:2], (WIM_OPEN, 0x33334444))
        self.assertEqual(e.wid(1, WIDM_CLOSE, user), 0)
        self.assertEqual(e.dev8(FM_STATE), 0)
        self.assertEqual(e.dev8(DEV_FLAGS) & 1, 1)

    def test_recording_routes_the_fm(self):
        e = self.emu()
        user = self.recording(e, mix7f=0x01)
        # the FM's samples on Audio 1's DMA, the music DAC FM's
        self.assertEqual(e.chip.mixer[0x7F], 0x10)
        self.assertEqual(e.dev8(FM_STATE), FMF_OPEN | FMF_ROUTED)
        self.assertEqual(e.wid(1, WIDM_STOP, user), 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x01)
        self.assertEqual(e.wid(1, WIDM_START, user), 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x10)
        self.assertEqual(e.wid(1, WIDM_RESET, user), 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x01)
        self.assertEqual(e.wid(1, WIDM_CLOSE, user), 0)
        self.assertEqual(e.dev8(FM_STATE), 0)

    def test_routed_before_the_dma_starts(self):
        # 7Fh first, so the buffer starts with the FM's samples
        e = self.emu()
        e.chip.mixer[0x7F] = 0x01
        r, user = e.open_in()
        self.assertEqual(r, 0)
        self.assertEqual(e.wid(1, WIDM_ADDBUFFER, user,
                               e.header(b"\0" * 4096), 0x20), 0)
        e.chip.log.clear()
        self.assertEqual(e.wid(1, WIDM_START, user), 0)
        starts = [i for i, x in enumerate(e.chip.log)
                  if x[:2] == ("reg", 0xB8) and x[2] & 1]
        self.assertTrue(starts)
        self.assertLess(e.chip.log.index(("mixer", 0x7F, 0x10)), starts[0])

    def test_close_while_routed(self):
        e = self.emu()
        user = self.recording(e, mix7f=0x00)
        self.assertEqual(e.wid(1, WIDM_RESET, user), 0)
        self.assertEqual(e.wid(1, WIDM_START, user), 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x10)
        self.assertEqual(e.wid(1, WIDM_CLOSE, user), 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x00)
        self.assertEqual(e.dev8(FM_STATE), 0)

    def test_fm_driver_lets_go_meanwhile(self):
        # ESS's FM close gives the music DAC to I2S through es_fm_dac: kept
        # on FM while recording, then I2S's once the recording stops
        e = self.emu()
        e.set_dev8(DEV_FM_DAC, 1)
        user = self.recording(e, mix7f=0x00)
        e.call_name("es_fm_dac", drvemu.DEV_OFF, 0x7F, 0x01)
        self.assertEqual(e.chip.mixer[0x7F], 0x10)
        e.set_dev8(DEV_FM_DAC, 0)
        self.assertEqual(e.wid(1, WIDM_STOP, user), 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x01)

    def test_fm_driver_opens_meanwhile(self):
        e = self.emu()
        user = self.recording(e, mix7f=0x01)
        e.set_dev8(DEV_FM_DAC, 1)
        self.assertEqual(e.wid(1, WIDM_STOP, user), 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x00)

    def test_fm_driver_opens_and_closes_meanwhile(self):
        # ESS's close reads the routed 7Fh and sets bit 0: I2S's once the
        # recording stops, as ESS's driver would have left it
        e = self.emu()
        user = self.recording(e, mix7f=0x00)
        e.set_dev8(DEV_FM_DAC, 1)
        e.call_name("es_fm_dac", drvemu.DEV_OFF, 0x7F, 0x11)
        e.set_dev8(DEV_FM_DAC, 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x10)
        self.assertEqual(e.wid(1, WIDM_STOP, user), 0)
        self.assertEqual(e.chip.mixer[0x7F], 0x01)

    def test_es_fm_dac_without_a_recording(self):
        e = self.emu()
        e.call_name("es_fm_dac", drvemu.DEV_OFF, 0x7F, 0x01)
        self.assertEqual(e.chip.mixer[0x7F], 0x01)

    def test_one_recording_at_a_time(self):
        e = self.emu()
        r, user = e.open_in()
        self.assertEqual(r, 0)
        self.assertEqual(e.open_in()[0], MMSYSERR_ALLOCATED)
        self.assertEqual(e.open_in(dev_id=0, rate=22050)[0],
                         MMSYSERR_ALLOCATED)
        # the Audio 1 player can't have Audio 1 either
        self.assertEqual(e.open(dev_id=1)[0], MMSYSERR_ALLOCATED)
        self.assertEqual(e.wid(1, WIDM_CLOSE, user), 0)
        # and device 1 waits for a recording on device 0
        r, user = e.open_in(dev_id=0, rate=22050)
        self.assertEqual(r, 0)
        e.set_dev8(DEV_FLAGS, e.dev8(DEV_FLAGS) | 1)
        self.assertEqual(e.open_in()[0], MMSYSERR_ALLOCATED)
        self.assertEqual(e.dev8(FM_STATE), 0)
        self.assertEqual(e.dev8(DEV_FLAGS) & 1, 1)

    def test_resume_routes_again(self):
        e = self.emu()
        self.recording(e, mix7f=0x01)
        e.chip.mixer[0x7F] = 0x01       # the mixer reset, then ESS's bit 0
        e.call_name("es_wid_resume", drvemu.DEV_OFF)
        self.assertEqual(e.chip.mixer[0x7F], 0x10)

    def test_last_disable_puts_7f_back(self):
        e = self.emu()
        self.recording(e, mix7f=0x01)
        e.call_name("a1_disable", drvemu.DEV_OFF)
        self.assertEqual(e.chip.mixer[0x7F], 0x01)
        self.assertEqual(e.dev8(FM_STATE), FMF_OPEN)

    def test_off_is_ess(self):
        opts = OPT_A1_DEVICE | OPT_A1_SHARED | OPT_DUAL
        self.assertFalse(opts & OPT_FM_RECORD)
        for stock in (True, False):
            e = self.emu(opts=None if stock else opts, stock=stock)
            with self.subTest(stock=stock):
                self.assertEqual(e.open_in()[0], MMSYSERR_BADDEVICEID)


class Device0NameTest(A1Case):
    def caps(self, e, size=0x30):
        buf = e.alloc(b"\0" * 0x30)
        ex = e.alloc(struct.pack("<II", size, buf))
        self.assertEqual(e.wod(0, WODM_GETDEVCAPS, 0, ex, DEVNODE), 0)
        return e.rd(e.far_lin(buf), 0x30)

    def test_named_by_its_dac(self):
        ess = self.caps(self.emu(stock=True))
        self.assertIn(b"ESS AudioDrive Playback (220)", ess)
        named = self.caps(self.emu())
        self.assertEqual(named[6:38],
                         b"ESS AudioDrive Audio 2 (220)".ljust(32, b"\0"))
        self.assertEqual(named[:6] + named[38:], ess[:6] + ess[38:])

    def test_one_device_keeps_ess_name(self):
        ess = self.caps(self.emu(stock=True))
        e = self.emu(opts=OPT_A1_SHARED | OPT_DUAL | OPT_FM_RECORD)
        self.assertEqual(self.caps(e), ess)
        # nor when Audio 1 can't play: a 16-bit DMA channel
        e = self.emu()
        e.set_dev8(0x06, 5)
        self.assertEqual(self.caps(e), ess)


class MixerTargetTest(A1Case):
    """Windows finds the mixer of a wave device by the device's caps:
    mixerGetID asks each mixer for the line whose target has them
    (MIXER_GETLINEINFOF_TARGETTYPE), and ESS's mixer compares the names,
    which it takes at the first DRVM_ENABLE"""

    def caps(self, e, dev, wave_in=False):
        """wMid, wPid, vDriverVersion and szPname of a device"""
        buf = e.alloc(b"\0" * 0x30)
        ex = e.alloc(struct.pack("<II", 0x30, buf))
        call = e.wid if wave_in else e.wod
        msg = WIDM_GETDEVCAPS if wave_in else WODM_GETDEVCAPS
        self.assertEqual(call(dev, msg, 0, ex, DEVNODE), 0)
        return e.rd(e.far_lin(buf), 38)

    def line_for(self, e, ttype, caps):
        """the answer and the short name of the line whose target has caps"""
        line = e.alloc(struct.pack("<I", 0xA6).ljust(0x78, b"\0") +
                       struct.pack("<II", ttype, 0) + caps)
        r = e.mxd(0, MXDM_GETLINEINFO, e.mixer_instance(), line,
                  MIXER_GETLINEINFOF_TARGETTYPE)
        return r, e.rd(e.far_lin(line) + 0x28, 16).split(b"\0")[0]

    def enabled(self, **kw):
        e = self.emu(**kw)
        e.mixer_init()
        return e

    def test_wave_line_names_device_0(self):
        for kw in ({"stock": True}, {},
                   {"opts": OPT_A1_SHARED | OPT_DUAL | OPT_FM_RECORD}):
            with self.subTest(**kw):
                e = self.enabled(**kw)
                self.assertEqual(
                    self.line_for(e, TARGET_WAVEOUT, self.caps(e, 0)),
                    (0, b"Wave"))

    def test_audio_1_on_a_16_bit_channel(self):
        # device 0 keeps ESS's name there, and so does the line
        e = self.emu()
        e.set_dev8(0x06, 5)
        e.mixer_init()
        caps = self.caps(e, 0)
        self.assertIn(b"ESS AudioDrive Playback (220)", caps)
        self.assertEqual(self.line_for(e, TARGET_WAVEOUT, caps), (0, b"Wave"))

    def test_ess_name_gone(self):
        ess = self.caps(self.enabled(stock=True), 0)
        self.assertEqual(self.line_for(self.enabled(), TARGET_WAVEOUT, ess)[0],
                         MIXERR_INVALLINE)

    def test_record_line(self):
        for stock in (True, False):
            e = self.enabled(stock=stock)
            self.assertEqual(
                self.line_for(e, TARGET_WAVEIN, self.caps(e, 0, True)),
                (0, b"Rec"))

    def test_no_line_for_the_new_devices(self):
        # ESS's mixer has none for Audio 1 or the FM recording, so Windows
        # finds no mixer for them (docs/AUDIO1.md)
        e = self.enabled()
        self.assertEqual(
            self.line_for(e, TARGET_WAVEOUT, self.caps(e, 1))[0],
            MIXERR_INVALLINE)
        self.assertEqual(
            self.line_for(e, TARGET_WAVEIN, self.caps(e, 1, True))[0],
            MIXERR_INVALLINE)


if __name__ == "__main__":
    unittest.main()
