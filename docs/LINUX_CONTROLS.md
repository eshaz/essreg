# The ALSA controls of snd-es1869

This page lists every ALSA control of the Linux driver, what it does on the
chip, and where to find it. [LINUX.md](LINUX.md) describes the driver, and
[REGISTERS.md](REGISTERS.md) the chip's registers.

Every setting of the driver is one row of `linux/es1869/settings.def`. Each
row is a module parameter, and most rows are a control too. A control and
its parameter hold one value: a write to either reaches the chip and shows
in the other. The lists at the end of this page come from that table.

## Reaching them

**alsamixer** shows every control. F3 is its Playback view, F4 its Capture
view and F5 shows both. Left and right choose an item. Up and down change a
level or step through a list, M turns a switch on or off, and Space sets the
CAPTURE mark in the Capture view.

Three things about alsamixer are worth knowing:
* It cuts the label under each item to 8 characters, so that the Audio 2
  items read alike. The Item line at the top shows the full name.
* It shows a level with a dB scale in dB, and any other number as a
  percentage of its range. Capture DC Offset at 0 shows as 52.
* The bottom of a slider is the lowest value, which isn't always nothing.
  Capture DC Offset at the bottom is -16, and every recording then carries
  -1024 of DC.

**es1869ctl** (`linux/tools/es1869ctl`) is a full-screen panel of the same
settings, by group, with each setting's real value, its dB, what it does and
when it takes effect. It also has what alsamixer has no place for: the
parameters, the running streams and their real rates, and what the ADC is
hearing. It follows alsamixer and the volume buttons at once. A setting with
a control needs only the audio group, and one without needs root.
`es1869ctl --dump` prints every page.

**amixer** takes the full names, such as `amixer -c ES1869 cset name='ADC
Input Capture Enum' Line`.

**A parameter** takes `/sys/module/snd_es1869/parameters/<name>` at runtime,
or the modprobe option of the same name at load.

## The signal path

The playback side, with the register of each level:

```
Audio 2 DAC --- 7Ch ---+
Audio 1 DAC --- 14h ---+
FM ------------ 36h ---+
Line ---------- 3Eh ---+
CD (Aux A) ---- 38h ---+--> 3-D effect --+--> master ---> AOUT ---> jack
Aux (Aux B) --- 3Ah ---+                 |    60h, 62h
Mic ----------- 1Ah ---+                 +--> record source 7
Mono in ------- 6Dh ---+

PC speaker ---- 3Ch ----------------------- the board, after the chip
```

The board mixes the PC speaker in after the chip, so the master doesn't
reach it. With Beep Follows Master on, the driver scales 3Ch by the master
itself, and Beep is its level at full master.

The record side:

```
Mic ---------- 68h ---+
Audio 2 ------ 69h ---+
CD ----------- 6Ah ---+
FM ----------- 6Bh ---+--> record mixer --> record source 5
Aux ---------- 6Ch ---+
Line --------- 6Eh ---+
Mono in ------ 6Fh ---+

8 record sources --> 1Ch --> B4h --> ADC --> BAh, BBh --> Audio 1 records
                     input   level           offset
```

The level of an input on the playback side and its level on the record side
are separate controls: Mic is 1Ah in F3 and 68h in F4, and the two share
only the element's name in alsamixer.

## Choosing the ADC's input

The chip has one stereo ADC, and it hears one of eight record sources. **ADC
Input**, in alsamixer's Capture view (F4), is the list of them:

| Value | ADC Input  | What the ADC hears                             |
|-------|------------|------------------------------------------------|
| 0     | Mic        | The microphone                                 |
| 1     | Mic Master | Left the microphone, right the master's inputs |
| 2     | CD         | Aux A                                          |
| 3     | AOUT       | What the output jack gets, after the master    |
| 4     | Mic1       | The microphone, the data sheet's second value  |
| 5     | Mix        | The record mixer, at the levels in F4          |
| 6     | Line       | Line in                                        |
| 7     | Master     | The 3-D effect's output, before the master     |

Master stands before the master's own level, so that a recording of it
doesn't move with the volume buttons, but the master's mute silences it. Mic
Master's right side is (L+R)/2 of the same point.

Mix is the way to record several inputs at once, or one of them at a level
of its own: the F4 levels of Mic, Line, CD, Aux, FM, PCM (Audio 2) and Mono
all feed it, and each starts at 0.

The stock name of the same setting is Capture Source, which saved mixer
states use. alsa-lib doesn't show a control of that name as a list. It takes
each of its items to the element of the same name, and gives that element a
CAPTURE mark in F4, so Mic, Line and Master have one, and Mic Master, Mic1,
AOUT and Mix are elements of their own that have only that. Those marks
still work, and they and ADC Input always show the same choice.

Capture is the level after the choice, B4h, and it also turns recording on
and off. Its dB scale is the same for every source, -6 to +16.5 dB. The two
microphone sources have 6 dB more in front of that stage, which the scale
leaves out, as it leaves out Mic Boost.

On the Armada the input jack is the microphone input, and it hears one side
of what is plugged into it.

Record Monitor puts the record source on AOUT in place of the playback mix
while Audio 1 records. With AOUT as the source it closes a loop, which
oscillates near 30 Hz at -8 dBFS.

## Saving them

`alsactl store` saves the controls, and udev's `alsactl restore` puts them
back when the card appears. A saved state decides over a parameter, because
it comes later. The parameters and their Kconfig defaults only set what a
card has before any state is restored.

alsactl converts a level to keep its dB if the card's dB range differs from
the saved one, and it does so in a pass before it writes anything, while the
card still has what it had at load. So no control's dB scale here may depend
on another control, and es1869test's `mix.scale` checks that.

## Every setting, by group

The name in bold is what alsamixer shows, and the name after it the
control's own, for amixer. F3 and F4 are its views. The groups are
es1869ctl's pages.

<!-- controls -->

### Output

* **Master** (`Master Playback Volume`, F3): 0 to 63 a channel, default 54.
  Master volume (60h/62h bits 5:0), 63 = 0 dB, 0.75 dB a step. Parameter
  `master_volume`.
* **Master** (`Master Playback Switch`, F3): 0 to 1, default 1. Master on
  (60h/62h bit 6 clear). Parameter `master_switch`.
* **PCM** (`PCM Playback Volume`, F3): 0 to 15 a channel, default 15. Audio
  2's volume (7Ch), 15 = 0 dB. Parameter `a2_volume`.
* **PCM** (`PCM Playback Volume`, F3): 0 to 15 a channel, default 14. Audio
  1's volume (14h), 14 = 0 dB. Parameter `a1_volume`.
* **FM** (`FM Playback Volume`, F3): 0 to 15 a channel, default 8. The music
  DAC's volume (36h), 8 = 0 dB. Parameter `fm_volume`.
* **Line** (`Line Playback Volume`, F3): 0 to 15 a channel, default 0. Line
  in to the output (3Eh), 13 = 0 dB. Parameter `line_volume`.
* **CD** (`CD Playback Volume`, F3): 0 to 15 a channel, default 0. AuxA (CD)
  to the output (38h), 13 = 0 dB. Parameter `cd_volume`.
* **Aux** (`Aux Playback Volume`, F3): 0 to 15 a channel, default 0. AuxB to
  the output (3Ah), 13 = 0 dB. Parameter `aux_volume`.
* **Mic** (`Mic Playback Volume`, F3): 0 to 15 a channel, default 0. The
  microphone to the output (1Ah), 8 = 0 dB. Parameter `mic_volume`.
* **Mono** (`Mono Playback Volume`, F3): 0 to 15 a channel, default 0.
  MONO_IN to the output (6Dh), 13 = 0 dB. Parameter `mono_volume`.
* **Beep** (`Beep Playback Volume`, F3): 0 to 7, default 0. The PC speaker
  (3Ch bits 2:0), 7 = -6 dB, 0 mutes it; the board mixes it in after the
  master, so with beep_follow this is its level at full master. Parameter
  `beep_volume`. Without a parameter at load, the chip keeps its level.
* **Beep Follows Master** (`Beep Follows Master Playback Switch`, F3): 0 to
  1, default 1. 3Ch scaled by the master and 0 while it is muted, as ESS's
  ES1869.DRV does it; 0 keeps the two apart. Parameter `beep_follow`.

### Record

* **Capture Source** (`Capture Source`, F4, a CAPTURE switch on each
  source): 0 Mic, 1 Mic Master, 2 CD, 3 AOUT, 4 Mic1, 5 Mix, 6 Line, 7
  Master, default Mix. The record source (1Ch bits 2:0): 5 the record mixer,
  7 the 3-D output before the master. Parameter `capture_source`.
* **ADC Input** (`ADC Input Capture Enum`, F4): the same value as Capture
  Source, as a plain list.
* **Capture** (`Capture Volume`, F4): 0 to 15 a channel, default 4. The
  record level (B4h, right in bits 7:4), 1.5 dB a step. Parameter
  `capture_volume`.
* **Capture** (`Capture Switch`, F4): 0 to 1, default 1. The record source
  on (1Ch bit 4 clear). Parameter `capture_switch`.
* **Record Monitor** (`Record Monitor`, F3): 0 to 1, default 0. AOUT carries
  the record source in place of the playback mix while Audio 1 records (A8h
  bit 3). Parameter `record_monitor`.
* **PCM** (`PCM Capture Volume`, F4): 0 to 15 a channel, default 0. Audio 2
  into the record mixer (69h). Parameter `a2_capture`.
* **Mic** (`Mic Capture Volume`, F4): 0 to 15 a channel, default 0. The
  microphone into the record mixer (68h). Parameter `mic_capture`.
* **Line** (`Line Capture Volume`, F4): 0 to 15 a channel, default 0. Line
  in into the record mixer (6Eh). Parameter `line_capture`.
* **FM** (`FM Capture Volume`, F4): 0 to 15 a channel, default 0. The music
  DAC into the record mixer (6Bh). Parameter `fm_capture`.
* **CD** (`CD Capture Volume`, F4): 0 to 15 a channel, default 0. AuxA (CD)
  into the record mixer (6Ah). Parameter `cd_capture`.
* **Aux** (`Aux Capture Volume`, F4): 0 to 15 a channel, default 0. AuxB
  into the record mixer (6Ch). Parameter `aux_capture`.
* **Mono** (`Mono Capture Volume`, F4): 0 to 15 a channel, default 0.
  MONO_IN into the record mixer (6Fh). Parameter `mono_capture`.
* **Mic Boost (+26dB)** (`Mic Boost (+26dB)`, F3): 0 to 1, default 1. The
  microphone's +26 dB preamp (7Dh bit 3), on after a hardware reset.
  Parameter `mic_boost`.
* **Capture DC Offset** (`Capture DC Offset`, F3 and F4): -16 to 15,
  default 0. The ADC's offsets (BAh/BBh bits 4:0), 64 a step of the 16-bit
  sample. Parameter `adc_offset`.
* **Analog Wake No Mute** (`Analog Wake No Mute Switch`, F3): 0 to 1,
  default 0. No 100 ms AOUT mute when the analog section wakes (BAh bit 5).
  Parameter `wake_no_mute`.

### DACs

* **Audio 2 Oversampling** (`Audio 2 Oversampling Playback Switch`, F3): 0
  to 1, default 0. Audio 2's 4x oversampling (71h bit 4), which also
  bypasses its filter. Parameter `audio2_oversampling`.
* **Audio 2 Filter** (`Audio 2 Filter Playback Switch`, F3): 0 to 1,
  default 0. Audio 2's switched-capacitor filter in use (71h bit 3 clear).
  Parameter `audio2_filter`.
* **Audio 1 Filter** (`Audio 1 Filter Switch`, F3): 0 to 1, default 0. Audio
  1's filter in use (71h bit 2 clear), also the ADC's anti-alias filter.
  Parameter `audio1_filter`.
* **Audio 2 Own Clock** (`Audio 2 Own Clock Switch`, F3): 0 to 1, default 1.
  Audio 2 at its own rate (71h bit 1), else slaved to Audio 1's. Parameter
  `audio2_own_clock`. A write fails with EBUSY while Audio 2 is open.
* **Audio 2 To FM** (`Audio 2 To FM Playback Switch`, F3): 0 to 1,
  default 0. Audio 2 slaved to the FM (71h bit 0): mixed into the music DAC
  at 49716 Hz, its sides swapped. Parameter `audio2_to_fm`. A write fails
  with EBUSY while Audio 2 is open.
* **Video** (`Video Playback Switch`, F3): 0 to 1, default 0. The music DAC
  driven by I2S (7Fh bit 0), which silences FM. Parameter `video_switch`.
* **ES689 Input** (`ES689 Input Switch`, F3): 0 to 1, default 1. The ES689
  wavetable interface (48h bit 4). Parameter `es689`.
* **Filter Corner** (`Filter Corner`, F3 and F4): 100 to 1000, default 875.
  The filters' corner below 44.1 kHz, in thousandths of half the rate;
  ESS's 875. Parameter `filter_corner`. It takes effect at the next prepare.

Parameters only:

* `a8_play_bits`: 0 to 244, default 0. A8h's bits beside its channels and
  monitor in playback: ESS's player keeps 00h. It takes effect at the next
  prepare.
* `a8_rec_bits`: 0 to 244, default 244. A8h's bits beside its channels and
  monitor in a recording: ESS's F4h (F5h/F6h). It takes effect at the next
  prepare.

### Mono

* **Mono Output Source** (`Mono Output Source`, F3): 0 Mute, 1 CIN_R, 2
  Audio 2 Right, 3 Record Mix, default Record Mix. What MONO_OUT carries
  (7Dh bits 2:1); ESS's 3. Parameter `mono_out`.
* **Mono Input Direct** (`Mono Input Direct Playback Switch`, F3): 0 to 1,
  default 0. MONO_IN straight to the output at unity gain (7Dh bit 0).
  Parameter `mono_in_direct`.

### 3-D

* **3D Control - Switch** (`3D Control - Switch`, F3): 0 to 1, default 0.
  The effect on (50h bits 3:2), out of reset as it turns on. Parameter
  `spatial_switch`.
* **3D Control - Level** (`3D Control - Level`, F3 and F4): 0 to 63,
  default 0. The effect's boost (52h), +16.8 dB near 450 Hz at 63, 0.75 dB a
  step. Parameter `spatial_level`.
* **3D Control - Mono** (`3D Control - Mono Switch`, F3): 0 to 1, default 0.
  Stereo made from the mono sum (50h bit 1). Parameter `spatial_mono`.
* **3D Control - Limit** (`3D Control - Limit Switch`, F3): 0 to 1,
  default 0. The automatic width limit (50h bit 0). Parameter
  `spatial_limit`.
* **3D Control - Limit Rise Level** (`3D Control - Limit Rise Level`, F3 and
  F4): 0 to 255, default 143. The limit's rise level (54h); ESS's 8Fh.
  Parameter `spatial_rise_level`.
* **3D Control - Limit Fall Level** (`3D Control - Limit Fall Level`, F3 and
  F4): 0 to 255, default 149. The limit's fall level (56h); ESS's 95h.
  Parameter `spatial_fall_level`.
* **3D Control - Limit Rise Speed** (`3D Control - Limit Rise Speed`, F3 and
  F4): 0 to 15, default 9. The limit's rise speed (58h bits 7:4); ESS's 9.
  Parameter `spatial_rise_speed`.
* **3D Control - Limit Fall Speed** (`3D Control - Limit Fall Speed`, F3 and
  F4): 0 to 15, default 4. The limit's fall speed (58h bits 3:0); ESS's 4.
  Parameter `spatial_fall_speed`.
* **3D Control - Limit Fixed Level** (`3D Control - Limit Fixed Level`, F3
  and F4): 0 to 255, default 128. The limit's fixed level (5Ah); ESS's 80h.
  Parameter `spatial_fixed_level`.

### Buttons

* **Hardware Master Volume Split** (`Hardware Master Volume Split`, F3): 0
  to 1, default 0. The buttons move 61h/63h instead of the master (64h bit
  7). Parameter `hwvol_split`.
* **Hardware Volume Mode** (`Hardware Volume Mode`, F3): 0 Three-wire, 1
  Two-wire, 2 Two-wire Fast, 3 Disabled, default Three-wire. The buttons'
  wiring and debounce (64h bits 3:2). Parameter `hwvol_mode`.
* **Hardware Volume Count By 3** (`Hardware Volume Count By 3 Switch`, F3):
  0 to 1, default 0. 2.25 dB a push instead of 0.75 dB (64h bit 5).
  Parameter `hwvol_count3`.
* **Hardware Master** (`Hardware Master Playback Volume` and
  `Hardware Master Playback Switch`, F3): 61h and 63h, the counters the
  buttons move in split mode.

### Power

* **GPO0** (`GPO0 Switch`, F3): 0 to 1, default 0. The GPO0 pin
  (Audio_Base+7 bit 0), low after a hardware reset. Parameter `gpo0`.
  Without a parameter at load, the chip keeps its level.
* **GPO1** (`GPO1 Switch`, F3): 0 to 1, default 1. The GPO1 pin
  (Audio_Base+7 bit 1), high after a hardware reset. Parameter `gpo1`.
  Without a parameter at load, the chip keeps its level.
* **Self-Timed Power Down** (`Self-Timed Power Down`, F3 and F4): 0 to 255,
  default 0. Partly power down after this many 8 s of no access (BDh), 0
  never. Parameter `self_power_down`.
* **GPO Power Down** (`GPO Power Down`, F3 and F4): 0 to 255, default 0. The
  GPOs at power-down (DSP CFh, read with CEh). Parameter `gpo_power_down`.
* **Power Save Seconds** (`Power Save Seconds`, F3 and F4): 0 to 3600,
  default 0. Seconds of idleness before a power-down, 0 never. Parameter
  `pwr_save`.
* **Full Power Down** (`Full Power Down Switch`, F3): 0 to 1, default 0.
  Power down fully (the inputs go silent) instead of partly. Parameter
  `pwr_full`.

### DMA

* **Single Mode DMA** (`Single Mode DMA Switch`, F3): 0 to 1, default 0.
  Single transfers instead of demand transfers (8237, B9h, 78h). Parameter
  `dma_single`. It takes effect at the next prepare.
* **Audio 1 DMA Burst** (`Audio 1 DMA Burst`, F3): 0 Type 0, 1 Type 1, 2
  Type 2, default Type 2. Audio 1's demand transfers (B9h); ESS's 2.
  Parameter `a1_burst`. It takes effect at the next prepare.
* **Audio 2 DMA Burst** (`Audio 2 DMA Burst`, F3): 0 Single, 1 Demand 2, 2
  Demand 4, 3 Demand 8, default Demand 4. Audio 2's transfers (78h bits
  7:6); ESS's demand 4. Parameter `a2_burst`. It takes effect at the next
  prepare.
* **DRQ Latch** (`DRQ Latch Switch`, F3): 0 to 1, default 1. Each DRQ held
  until its DACK (card register 25h bit 7). Parameter `drq_latch`.
* **ADC Settle Milliseconds** (`ADC Settle Milliseconds`, F3 and F4): 0 to
  1000, default 200. How long a recording waits for the ADC's input to
  settle after the codec turns to it (ms); 0 starts at once, as ESS's
  drivers do, and the first 100 ms then carry the input's offset decaying.
  Parameter `a1c_settle_ms`.

Parameters only:

* `piix4_type_f`: 0 to 1, default 0. 1 keeps an Intel PIIX4's Type F DMA
  timing on Audio 1's channel as the BIOS set it; 0 turns it off, since with
  it a recording's first transfer hung the Armada. A write fails with EBUSY
  while Audio 1 is open.
* `a1_settle_us`: 20 to 2000, default 32. Audio 1's start held masked at
  least this long (us).
* `a1_settle_frames`: 0 to 16, default 2. And at least this many sample
  periods.
* `a1_settle_mode`: 0 to 1, default 0. 0 a timer unmasks the channel, 1 the
  start waits for it.
* `a2_lead_ms`: 4 to 500, default 40. How far ahead Audio 2's ring is filled
  on DMA 5 (ms).
* `a2_tick_us`: 500 to 20000, default 2000. The ring's refill period on DMA
  5 while a copy is behind (us); under dmix a quarter of a2_lead_ms, if that
  is longer.
* `a2_pause`: 0 to 1, default 0. Pause on an 8-bit Audio 2 channel through
  78h, which may resume off step. It takes effect at the next prepare.

### Rate

* **Audio 2 Rate Correction** (`Audio 2 Rate Correction`, F3): 0 Off, 1
  Trim, 2 Clock, default Clock. Audio 2's 16-bit streams on DMA 5 through a
  filter that corrects their rate, also while one plays: Trim by a2_trim,
  Clock also to the system's clock, as the chip's measures against it, which
  is true time with chronyd running; Off leaves the samples as they are.
  Parameter `a2_resample`.
* **Audio 2 Rate Trim** (`Audio 2 Rate Trim`, F3): 0 -500 ppm, 1 -200 ppm, 2
  -100 ppm, 3 -50 ppm, 4 -20 ppm, 5 -10 ppm, 6 -5 ppm, 7 -2 ppm, 8 -1 ppm, 9
  -0.5 ppm, 10 -0.2 ppm, 11 -0.1 ppm, 12 0, 13 +0.1 ppm, 14 +0.2 ppm, 15
  +0.5 ppm, 16 +1 ppm, 17 +2 ppm, 18 +5 ppm, 19 +10 ppm, 20 +20 ppm, 21 +50
  ppm, 22 +100 ppm, 23 +200 ppm, 24 +500 ppm, default 0. What the rate
  correction adds to the stream's rate. Parameter `a2_trim`.
* **Audio 2 Rate Dither** (`Audio 2 Rate Dither Playback Switch`, F3): 0 to
  1, default 0. The rate correction's rounding to 16 bits with triangular
  noise of 1 LSB: 3 dB more noise for spurs at about -100 dBFS. Parameter
  `a2_resample_dither`.
* **Audio 2 Output Rate** (`Audio 2 Output Rate`, F3): 0 1x, 1 2x, 2 3x,
  default 1x. The DAC clocked at this many times a 16-bit stream's rate on
  DMA 5 (70h), through the rate filter, where a divisor of the stream's own
  gives that exactly: no images, less droop, more of the ISA bus and CPU. A
  playing stream takes it at once, restarting as at a pause. 3x can't run
  while Audio 1 records. Parameter `a2_oversample`.
* **Audio 2 Treble Correction** (`Audio 2 Treble Correction Playback
  Switch`, F3): 0 to 1, default 0. Audio 2 through the filter with the DAC's
  own response taken back, at 1x, 2x and 3x: within 0.05 dB up to half the
  stream's rate, or up to the filter's edge at 20 kHz where it moves the
  samples, with the DAC's stages off, for 5 dB less headroom at the top.
  Parameter `a2_treble`.
* **Audio 2 Treble Correction Taps** (`Audio 2 Treble Correction Taps`): the
  DAC's correction that a2_treble folds into the rate filter, 16 side taps
  at each of 1x, 2x and 3x in Q30, as `tools/es1869treble.py` fits them;
  treble.h's until others are loaded, for a DAC measured apart. A playing
  stream takes them within milliseconds, between the filter's runs, with no
  break.
* **Audio 1 Correction** (`Audio 1 Correction Capture Switch`, F4): 0 to 1,
  default 0. Audio 1's 16-bit recordings through a filter of 33 taps a
  channel that takes back the record path's own response, with the taps of
  "Audio 1 Correction Capture Taps" (es1869scope -v 3 -C measures and loads
  them): the frames come 16 later, and the FM recording stays exact.
  Parameter `a1c_filter`. It takes effect at the next prepare.
* **Audio 1 Correction Taps** (`Audio 1 Correction Capture Taps`): the
  correction filter's taps, 17 for each channel, the left's first, each the
  centre then outward, in units of 1/16384; the identity until a calibration
  (`es1869scope -C`) loads its own. A recording under way takes them at its
  next pass.

Parameters only:

* `a2_trim_ppb`: -2000000 to 2000000, default 0. More of the same in ppb,
  added to a2_trim.

### Limits

* **Rate Limit** (`Rate Limit Switch`, F3): 0 to 1, default 1. 0 lifts the
  rate limits above, for es1869test's rate.max. Parameter `rate_limit`. It
  takes effect at the next prepare.

Parameters only:

* `a1p_max_rate`: 3101 to 793800, default 48000. Audio 1 playback's highest
  rate (Hz). It takes effect at the next prepare.
* `a1c_max_rate`: 3101 to 793800, default 52920. Audio 1 recording's highest
  rate (Hz): the ADC delivers nothing from 56700. It takes effect at the
  next prepare.
* `a2_max_rate`: 6000 to 793800, default 96000. Audio 2's highest rate (Hz):
  clean to 99225 with Audio 1 recording, 132300 alone. It takes effect at
  the next prepare.
* `a1_max_bps`: 12404 to 3175200, default 211680. Audio 1's highest DMA byte
  rate. It takes effect at the next prepare.
* `a2_max_bps`: 12404 to 3175200, default 384000. Audio 2's highest DMA byte
  rate. It takes effect at the next prepare.

### Devices

* **FM Digital** (`FM Digital Capture Switch`, F4): 0 to 1, default 1. PCM
  device 1, the music DAC recorded digitally, opens. Parameter
  `fmdig_device`. It takes effect at the next open.
* **Dual** (`Dual Playback Switch`, F3): 0 to 1, default 0. PCM device 2,
  both DACs as four channels, opens. Parameter `dual_playback`. It takes
  effect at the next open.
* **Audio 2 Monitor** (`Audio 2 Monitor Capture Switch`, F4): 0 to 1,
  default 1. PCM device 3, the frames Audio 2's DAC took, opens. Parameter
  `monitor_device`. It takes effect at the next open.

Parameters only:

* `irq_sources`: 0 to 15, default 15. The sources the line carries: 1 Audio
  1, 2 Audio 2, 4 the buttons, 8 the MPU-401.
* `irq_mode`: 0 to 1, default 0. 0 the sources polled where they live, as
  ESS's drivers do; 1 through Config_Base+6/+7.
* `irq_trace`: 0 to 2, default 0. For debugging: 1 logs the streams' steps
  and each interrupt's sources, 2 also every port write.
* `regs_write`: 0 to 1, default 0. The register hwdep writes (CAP_SYS_RAWIO
  too).

### FM

* **OPL3** (`OPL3 Switch`, F3): 0 to 1, default 1. The OPL3 hwdep and synth
  open. Parameter `opl3`.
* **ESFM** (`ESFM Switch`, F3): 0 to 1, default 1. The ESFM hwdep and synth
  open. Parameter `esfm`. It takes effect at the next open.
* **FM Sides** (`FM Sides Switch`, F3): 0 to 1, default 1. OPL3 channel A
  and the ESFM synth's pan left on the left output; 0 the chip's mirror.
  Parameter `fm_sides`.
* **ESFM Better Square** (`ESFM Better Square Switch`, F3): 0 to 1,
  default 0. The bank with the better square wave
  (es1869/esfm-gm-square.bin). Parameter `esfm_square`.
* **ESFM Silence On Close** (`ESFM Silence On Close Switch`, F3): 0 to 1,
  default 1. Every voice off when the synth's last user goes, the pedal's
  too; 0 is ESS's all notes off, though the FM's return to the OPL3 mode
  stops them right after. Parameter `esfm_silence_on_close`. It takes effect
  at the next open.
* **ESFM Pedal Release** (`ESFM Pedal Release Switch`, F3): 0 to 1,
  default 1. A program change releases the sustain pedal. Parameter
  `esfm_pedal_release`. It takes effect at the next open.
* **ESFM Vibrato** (`ESFM Vibrato Switch`, F3): 0 to 1, default 1. Vibrato
  from the modulation wheel and channel pressure. Parameter `esfm_vibrato`.
  It takes effect at the next open.
* **ESFM Tuning** (`ESFM Tuning Switch`, F3): 0 to 1, default 1. RPN 1 and
  2, fine and coarse tuning. Parameter `esfm_tuning`. It takes effect at the
  next open.
* **ESFM Reset Controllers** (`ESFM Reset Controllers Switch`, F3): 0 to 1,
  default 1. Controller 121 as RP-015 has it; 0 ESS's reset. Parameter
  `esfm_reset_controllers`. It takes effect at the next open.
* **ESFM Live Pan** (`ESFM Live Pan Switch`, F3): 0 to 1, default 1. A pan
  change moves sounding notes too. Parameter `esfm_live_pan`. It takes
  effect at the next open.
* **ESFM SysEx** (`ESFM SysEx Switch`, F3): 0 to 1, default 1. The GM, GM2,
  GS and XG resets and the master volume. Parameter `esfm_sysex`. It takes
  effect at the next open.

Parameters only:

* `esfm_delay`: 0 to 32, default 6. Status reads after each ESFM address or
  data write.
* `esfm_bank`: default "es1869/esfm-gm.bin". The GM bank's firmware file; a
  write loads it.

<!-- /controls -->

## What has no control

A control is open to anyone in the audio group and sits in a mixer among the
volumes, so these stay parameters:
* The machine's safety: `piix4_type_f`, which hangs the Armada at the first
  recorded byte, and `regs_write`, which opens the chip's registers.
* The driver's inner timing, which no client has a use for: the
  `a1_settle_*` settings, `a2_lead_ms`, `a2_tick_us`, `a8_play_bits`,
  `a8_rec_bits` and `esfm_delay`.
* Numbers too wide for a slider: the rate limits and `a2_trim_ppb`. Rate
  Limit turns all the limits off, and Audio 2 Rate Trim has the steps of the
  trim.
* Debugging: `irq_sources`, `irq_mode` and `irq_trace`.
* `a2_pause`, which only matters on an 8-bit Audio 2 channel, and
  `esfm_bank`, a file name.
