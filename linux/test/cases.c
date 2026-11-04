// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869test's cases, in the order they run: the calibration, the probes,
 * then each path through the chip. An id is group.name; the flags say what
 * a case needs (et.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "et.h"

#define CASE(id, flags, fn, arg, spec) { id, flags, fn, arg, spec },
#define FN(name) enum et_result name(struct et_ctx *c, const struct et_case *k);

FN(c_cal_rec2)
FN(c_probe_card)
FN(c_probe_controls)
FN(c_probe_params)
FN(c_params_rw)
FN(c_params_bad)
FN(c_dig_fmt)
FN(c_dig_start)
FN(c_dig_pause)
FN(c_dig_access)
FN(c_dig_long)
FN(c_dig_drain)
FN(c_dig_drain_two)
FN(c_dig_two)
FN(c_dig_runout)
FN(c_dig_trim)
FN(c_dig_trim_exact)
FN(c_dig_clock)
FN(c_fm_idle)
FN(c_fm_esfm_sides)
FN(c_fm_opl3_sides)
FN(c_fm_pitch)
FN(c_a1rec_fmt)
FN(c_a1rec_dc)
FN(c_a1rec_rate)
FN(c_a1rec_start)
FN(c_a1rec_settle0)
FN(c_a1play_fmt)
FN(c_a1play_rate)
FN(c_a1play_start)
FN(c_a1play_drain)
FN(c_midi_esfm)
FN(c_midi_soft)
FN(c_midi_opl3)
FN(c_buf_delay)
FN(c_buf_drain)
FN(c_buf_xrun)
FN(c_cpu)
FN(c_mix)
FN(c_dual_map)
FN(c_dual_align)
FN(c_both_restart)
FN(c_rate_max)
FN(c_latency_min)
FN(c_music_aplay)
FN(c_midi_mpu)
FN(c_clock)
FN(c_dac_images)
FN(c_dac_corner)
FN(c_a2_rate)
FN(c_mix_cable)
FN(c_3d_quick)
FN(c_3d_full)
FN(c_gpo_out)
FN(c_beep_out)
FN(c_hwvol_buttons)
FN(c_pm_suspend)
FN(c_pm_stream)
FN(c_pm_a1)
FN(c_load_cycle)
FN(c_stress_reopen)
FN(c_stress_all)

#define Q ET_QUICK
#define R ET_SECOND
#define N ET_NEW
#define DESTR (ET_OPTIN | ET_DESTR | ET_ROOT | N)

const struct et_case et_cases[] = {
	CASE("cal.rec2", Q, c_cal_rec2, 0,
	     "the second card hears the left output through the cable")
	CASE("cal.linear", R, c_mix_cable, 0,
	     "the second card's level, a tone 6 dB a step at fixed volumes")

	CASE("clock.families", 0, c_clock, 0,
	     "the 768 kHz clock against the 793.8 kHz one")
	CASE("clock.fm", N, c_clock, 1,
	     "the FM's clock against the 793.8 kHz one")
	CASE("clock.system", N, c_clock, 2,
	     "the 793.8 kHz clock against the system's")

	CASE("probe.card", Q | N, c_probe_card, 0,
	     "the PCM devices, the proc file, the register hwdep, settings.def")
	CASE("probe.controls", Q | N, c_probe_controls, 0,
	     "every control as settings.def has it, its parameter agreeing")
	CASE("probe.params", Q | N, c_probe_params, 0,
	     "every parameter readable and in its range")
	CASE("params.rw", Q | ET_ROOT | N, c_params_rw, 0,
	     "a parameter written at runtime moves its control, and back")
	CASE("params.bad", ET_ROOT | N, c_params_bad, 0,
	     "bad values give ERANGE or EINVAL and change nothing")

	CASE("dig.fmt.s16", Q | N, c_dig_fmt, 0,
	     "Audio 2, 16-bit stereo, recorded exactly through the music DAC")
	CASE("dig.fmt.s16m", N, c_dig_fmt, 1, "the same, 16-bit mono")
	CASE("dig.fmt.u8", N, c_dig_fmt, 2, "the same, 8-bit unsigned stereo")
	CASE("dig.fmt.u8m", N, c_dig_fmt, 3, "the same, 8-bit unsigned mono")
	CASE("dig.fmt.s8", N, c_dig_fmt, 4, "the same, 8-bit signed stereo")
	CASE("dig.fmt.u16", N, c_dig_fmt, 5, "the same, 16-bit unsigned stereo")
	CASE("dig.start", Q | N, c_dig_start, 0,
	     "twenty starts of Audio 2, none losing or adding a frame")
	CASE("dig.pause", Q | N, c_dig_pause, 0,
	     "a pause resumes on the next frame")
	CASE("dig.mmap", N, c_dig_access, ET_MMAP,
	     "every frame through mmap")
	CASE("dig.dmix", N, c_dig_access, ET_DMIX,
	     "every frame through dmix")
	CASE("dig.drain.full", N, c_dig_drain, 0,
	     "a drain that begins with the buffer nearly full: idle frames after it")
	CASE("dig.drain.two", N, c_dig_drain_two, 0,
	     "a drain with two periods in the buffer: idle frames after it")
	CASE("dig.two", N, c_dig_two, 0,
	     "two periods in the buffer, polled as JACK does it, every frame")
	CASE("dig.trim", N, c_dig_trim, 0,
	     "the rate correction: a2_trim_ppb moves the samples' pitch, cleanly")
	CASE("dig.trim.mono", N, c_dig_trim, 1, "the same in mono")
	CASE("dig.trim.two", N, c_dig_trim, 2,
	     "the same with two periods in the buffer, polled as JACK does it")
	CASE("dig.trim.down", N, c_dig_trim, 3, "the same, down by as much")
	CASE("dig.trim.48", N, c_dig_trim, 4, "the same at 48 kHz")
	CASE("dig.trim.exact", N, c_dig_trim_exact, 0,
	     "the rate correction with nothing to correct: every frame as it was")
	CASE("dig.clock", N, c_dig_clock, 0,
	     "the Clock mode at 48 kHz: the samples follow the clock it measures")
	CASE("dig.runout", N, c_dig_runout, 0,
	     "an application that stops writing: the core's silence reaches the ring")
	CASE("dig.long", N, c_dig_long, 0,
	     "half a minute, every frame")

	CASE("fm.idle", Q | N, c_fm_idle, 0,
	     "PCM 1 reads +128 with the FM silent")
	CASE("fm.esfm.sides", Q | N, c_fm_esfm_sides, 0,
	     "native register 6: bit 5 on the left, bit 4 on the right")
	CASE("fm.opl3.sides", Q | N, c_fm_opl3_sides, 0,
	     "OPL3 channel A on the left with fm_sides, mirrored without")
	CASE("fm.pitch", N, c_fm_pitch, 0,
	     "a native note's pitch, exactly, and its sides alike")

	CASE("a1rec.fmt.s16", Q, c_a1rec_fmt, 0,
	     "Audio 1 records Audio 2's tones: 16-bit stereo")
	CASE("a1rec.fmt.s16m", 0, c_a1rec_fmt, 1, "the same, 16-bit mono")
	CASE("a1rec.fmt.u8", 0, c_a1rec_fmt, 2, "the same, 8-bit unsigned")
	CASE("a1rec.fmt.u8m", 0, c_a1rec_fmt, 3, "the same, 8-bit mono")
	CASE("a1rec.fmt.s8", 0, c_a1rec_fmt, 4, "the same, 8-bit signed")
	CASE("a1rec.fmt.u16", 0, c_a1rec_fmt, 5, "the same, 16-bit unsigned")
	CASE("a1rec.rate.8000", 0, c_a1rec_rate, 0, "16-bit stereo at 8000 Hz")
	CASE("a1rec.rate.11025", 0, c_a1rec_rate, 1, "at 11025 Hz")
	CASE("a1rec.rate.16000", 0, c_a1rec_rate, 2, "at 16000 Hz")
	CASE("a1rec.rate.22050", 0, c_a1rec_rate, 3, "at 22050 Hz")
	CASE("a1rec.rate.32000", 0, c_a1rec_rate, 4, "at 32000 Hz")
	CASE("a1rec.rate.44100", 0, c_a1rec_rate, 5, "at 44100 Hz")
	CASE("a1rec.rate.48000", Q, c_a1rec_rate, 6, "at 48000 Hz")
	CASE("a1rec.start.s16", Q, c_a1rec_start, 0,
	     "ten 16-bit stereo recordings, each the tone from its first frame")
	CASE("a1rec.start.s16m", 0, c_a1rec_start, 1, "the same, 16-bit mono")
	CASE("a1rec.start.u8", 0, c_a1rec_start, 2, "the same, 8-bit stereo")
	CASE("a1rec.start.8000", 0, c_a1rec_start, 3, "the same, at 8000 Hz")
	CASE("a1rec.start.48000", 0, c_a1rec_start, 4, "the same, at 48000 Hz")
	CASE("a1rec.dc", N, c_a1rec_dc, 0,
	     "the ADC's offset with a silent source, and a step of Capture DC Offset")
	CASE("a1rec.settle0", ET_ROOT | N, c_a1rec_settle0, 0,
	     "a1c_settle_ms=0, ESS's start: the input's offset at the start")

	CASE("a1play.fmt.s16", Q | R, c_a1play_fmt, 0,
	     "Audio 1 plays through the cable: 16-bit stereo")
	CASE("a1play.fmt.s16m", R, c_a1play_fmt, 1, "the same, 16-bit mono")
	CASE("a1play.fmt.u8", R, c_a1play_fmt, 2, "the same, 8-bit unsigned")
	CASE("a1play.fmt.u8m", R, c_a1play_fmt, 3, "the same, 8-bit mono")
	CASE("a1play.fmt.s8", R, c_a1play_fmt, 4, "the same, 8-bit signed")
	CASE("a1play.fmt.u16", R, c_a1play_fmt, 5, "the same, 16-bit unsigned")
	CASE("a1play.rate.8000", R, c_a1play_rate, 0, "16-bit stereo, 8000 Hz")
	CASE("a1play.rate.11025", R, c_a1play_rate, 1, "at 11025 Hz")
	CASE("a1play.rate.22050", R, c_a1play_rate, 3, "at 22050 Hz")
	CASE("a1play.rate.48000", R, c_a1play_rate, 6, "at 48000 Hz")
	CASE("a1play.start", Q | R, c_a1play_start, 0,
	     "twenty starts in step: 16-bit clean, 8-bit with its sides")

	CASE("a1play.drain", R, c_a1play_drain, 0,
	     "a drain ending on a period's end plays Audio 1's FIFO out")
	CASE("a1play.pausedrain", R, c_a1play_drain, ET_PAUSEDRAIN,
	     "a drain out of a pause plays what was queued")
	CASE("buf.delay", Q | N, c_buf_delay, 0,
	     "Audio 2's delay counts its FIFO; its first frame plays at once")
	CASE("buf.drain", 0, c_buf_drain, 0,
	     "a drain ends as the last frame plays")
	CASE("buf.xrun.play", 0, c_buf_xrun, 0,
	     "a stalled writer: an xrun, then every frame")
	CASE("buf.xrun.rec", 0, c_buf_xrun, 1,
	     "a stalled reader: an xrun, then every frame")

	CASE("mix.a2", Q, c_mix, 0,
	     "Audio 2's volume (7Ch) on its dB scale, through source 7")
	CASE("mix.a2rec", 0, c_mix, 1,
	     "Audio 2 into the record mixer (69h) on its dB scale")
	CASE("mix.capture", 0, c_mix, 2,
	     "the recording level (B4h) on the line inputs' dB scale")
	CASE("mix.fmrec", N, c_mix, 5,
	     "the music DAC into the record mixer (6Bh) on its dB scale")
	CASE("mix.master", 0, c_mix, 3,
	     "the master on its dB scale, through record source 3 (AOUT)")
	CASE("mix.a1", R | ET_LINEAR, c_mix, 4,
	     "Audio 1's volume (14h) on its dB scale, through a linear cable")

	CASE("dac.images", N, c_dac_images, 0,
	     "Audio 2's image at 22.05 kHz, with each DAC stage")

	CASE("dac.corner.11025", N, c_dac_corner, 0,
	     "Audio 2's filter corner at 11025 Hz, from the driver's 72h")
	CASE("dac.corner.22050", N, c_dac_corner, 1, "the same at 22050 Hz")
	CASE("dac.corner.32000", N, c_dac_corner, 2, "the same at 32000 Hz")
	CASE("a2.rate.8000", 0, c_a2_rate, 0,
	     "Audio 2 at 8000 Hz on its clock, heard by Audio 1 at 48 kHz")
	CASE("a2.rate.11025", 0, c_a2_rate, 1, "at 11025 Hz")
	CASE("a2.rate.16000", 0, c_a2_rate, 2, "at 16000 Hz")
	CASE("a2.rate.22050", 0, c_a2_rate, 3, "at 22050 Hz")
	CASE("a2.rate.32000", 0, c_a2_rate, 4, "at 32000 Hz")
	CASE("a2.rate.44100", 0, c_a2_rate, 5, "at 44100 Hz")
	CASE("a2.rate.48000", 0, c_a2_rate, 6, "at 48000 Hz")
	CASE("a2.rate.88200", 0, c_a2_rate, 7, "at 88200 Hz")
	CASE("a2.rate.96000", 0, c_a2_rate, 8, "at 96000 Hz")

	CASE("3d.quick", N, c_3d_quick, 0,
	     "the 3-D controls: the boost, its level's scale, the model, the limit")
	CASE("3d.full", ET_OPTIN | N, c_3d_full, 0,
	     "ess3d's full plan, the report for docs/SPATIALIZER.md's card")

	CASE("dual.map", R | ET_ROOT | N, c_dual_map, 0,
	     "the dual device: channel 1 on Audio 1, channel 3 on Audio 2, one clock")
	CASE("dual.align", R | ET_ROOT | N, c_dual_align, 0,
	     "the two DACs begin within half a frame of each other")
	CASE("dual.align.22050", R | ET_ROOT | N, c_dual_align, 1,
	     "the same at 22050 Hz")
	CASE("dual.align.48000", R | ET_ROOT | N, c_dual_align, 2,
	     "the same at 48000 Hz")

	CASE("both.a1play", R | N, c_both_restart, 0,
	     "Audio 2 plays on while Audio 1 starts and stops eight times")
	CASE("both.a1rec", R | N, c_both_restart, 1,
	     "Audio 2 plays on while Audio 1 starts and stops eight recordings")
	CASE("mix.monitor", R | N, c_both_restart, 2,
	     "the record monitor puts the record source on AOUT while Audio 1 records")

	CASE("latency.min.a2", 0, c_latency_min, 0,
	     "Audio 2's smallest period of four without an xrun")
	CASE("latency.min.a1", 0, c_latency_min, 1,
	     "Audio 1's smallest period of four without an xrun")
	CASE("music.aplay", N, c_music_aplay, 0,
	     "aplay plays a file on Audio 2, every frame on the dig path")
	CASE("rate.max.a2", ET_OPTIN | ET_ROOT | N, c_rate_max, 0,
	     "Audio 2's highest clean rate, with rate_limit=0")
	CASE("rate.max.a1c", ET_OPTIN | ET_ROOT | N, c_rate_max, 1,
	     "Audio 1 recording's highest clean rate, with rate_limit=0")
	CASE("rate.max.a1p", ET_OPTIN | ET_ROOT | R | N, c_rate_max, 2,
	     "Audio 1 playback's highest clean rate, with rate_limit=0")

	CASE("cpu.a2", N, c_cpu, 1,
	     "the CPU while Audio 2 plays: under 1 %")
	CASE("cpu.a1", N, c_cpu, 2,
	     "the CPU while Audio 1 plays: under 1 %")
	CASE("cpu.both", N, c_cpu, 3,
	     "the CPU while both play: under 2 %")

	CASE("beep.out", R | ET_ROOT, c_beep_out, 0,
	     "the PC speaker's levels, heard by the second card")
	CASE("gpo.out", R | ET_OPTIN, c_gpo_out, 0,
	     "what GPO0 and GPO1 do to the line output")
	CASE("hwvol.buttons", ET_INTER, c_hwvol_buttons, 0,
	     "the volume buttons move the master and its control, or the counters")
	CASE("pm.suspend", DESTR, c_pm_suspend, 0,
	     "a suspend and resume keep every setting, and the engines work")
	CASE("pm.stream", DESTR, c_pm_stream, 0,
	     "a stream through a suspend goes on once prepared again")
	CASE("pm.a1", DESTR | R, c_pm_a1, 0,
	     "a suspend with Audio 1 playing: it goes on, on the mixer")
	CASE("load.cycle", DESTR, c_load_cycle, 0,
	     "five reloads: the card the same, a parameter at load in force")
	CASE("stress.reopen", ET_OPTIN, c_stress_reopen, 0,
	     "200 open and close cycles over the devices, the dig path after")
	CASE("stress.all", ET_OPTIN | N, c_stress_all, 0,
	     "a minute of the dig path, exact, with every interface read at once")

	CASE("midi.mpu", 0, c_midi_mpu, 0,
	     "amidi sends a note out of the MPU-401")
	CASE("midi.esfm", ET_MIDI | N, c_midi_esfm, 0,
	     "GMCHECK.MID on the ESFM synth, every step on PCM 1")
	CASE("midi.opl3", ET_MIDI | N, c_midi_opl3, 0,
	     "the OPL3 synth with sbiload's bank: notes, pitch and pan on PCM 1")
	CASE("midi.soft", ET_MIDI | R, c_midi_soft, 0,
	     "WildMidi on Audio 1, heard by the second card")
};

const int et_ncases = sizeof(et_cases) / sizeof(et_cases[0]);
