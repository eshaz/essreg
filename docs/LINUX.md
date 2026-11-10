# The Linux driver

snd-es1869 is a Linux ALSA driver for the ESS ES1869 AudioDrive. It is built
from what essreg found out about the chip and about ESS's DOS and Windows
drivers, and it exposes every feature of the chip through ALSA: both DACs,
full duplex, every rate of the chip's clocks, the record mixer, the 3-D
effect, the DAC stages, the hardware volume buttons, the music DAC recorded
digitally, OPL3 synthesis and the MPU-401. Every setting is an ALSA control
where it is chip state, a module parameter that can be written at runtime,
and a Kconfig default.

The driver lives in `linux/es1869/`. It is licensed under the GPL version 2
or later, so that it can be linked with the kernel, and its test suite and
tools under the GPL version 3 or later.

## What it does differently from snd-es18xx

snd-es18xx plays Audio 1 but leaves Audio 2 silent on a 16-bit DMA channel,
such as DMA 5 on the Compaq Armada 1500c. The chip takes one byte per DMA
cycle on any channel, and on a 16-bit channel it keeps the low byte of each
word. snd-es18xx sets mixer 78h bit 5, which the data sheet reserves, and
the chip then counts words and plays nothing usable. snd-es1869 does what
ESS's drivers do: it copies each byte of the application's buffer into the
low byte of a word, in a ring twice as long, and the 8237 counts words.

The driver also follows the other measurements in
[ESS_EXTENDED.md](ESS_EXTENDED.md):

* Audio 1's start keeps the DMA channel masked over B8h bit 0 and unmasks it
  at least 32 us and two sample periods later, because the chip loses the
  first byte when the DMA brings it right after the start's first request.
  Without that, 16-bit streams play one byte off.
* Audio 2 starts with a single write of 78h, because a start in two steps
  loses 1 to 3 bytes.
* Audio 1 pauses through B7h bit 7 and then B2h bit 6, which resumed in step
  every time. A pause of Audio 2 is a stop and a fresh start from the paused
  frame, since the chip's own pause resumed in step only half the time.
* A recording turns the codec to the ADC when it is prepared, as the data
  sheet's steps do, keeps it there between recordings, and starts no sooner
  than `a1c_settle_ms` (200 ms) after the turn. The ADC's input jumps when
  the codec turns from the DAC, by up to full scale after a second or so on
  the DAC, and settles with a halving about every 25 ms. ESS's drivers turn
  the codec at the start, and their recordings begin with that thump.
* The silence that alsa-lib asks for past a stream's last frame when it
  drains, and that dmix asks for behind what has played, reaches Audio 2's
  ring of words too. Without it the DMA, which runs on until the next
  period's interrupt, played what the ring held from before. A drain
  silences a period and two FIFOs past the last frame itself, as far as the
  buffer is free, and goes on as the DMA frees more: a drain that began with
  the buffer full, as after a resume, played up to a period of old data.
* A drain's position, on every playback device, stays a FIFO behind the DMA,
  so that the drain ends once the DAC has played the last frame. ESS's
  drivers wait for the FIFO too (DS p.49). A stop at the DMA's own end cut
  32 ms off a sound at 8 kHz on Audio 1.
* A position never steps back, but only a step of a FIFO or so counts as
  one. Over half the buffer in one step is normal with two periods, where a
  client that polls, as JACK does, reads the pointer only at the interrupts.
* What the ring copies depends on whether the application's pointer moved,
  not on how far it is from the DMA, because dmix's pointer stands at 0 and
  the DMA comes near it when the position nears the boundary, after 6 hours.
* Nothing corrects the ADC's offset by itself, as ESS's DCdrift and
  ESSDC.EXE do. On the Armada it is +18 and +21 of 32768, about -64 dBFS,
  which is under half a step of the offset registers, and a recording's
  start, which ESS's correction mostly served, settles before it begins.
  es1869test's `a1rec.dc` measures it, and `Capture DC Offset` corrects a
  card that needs it, in steps of 64.
* Every device takes a whole number of periods in its buffer, two at least.
  Without that the interrupts moved against the buffer at each wrap, and
  speaker-test, which wants two periods, took one of 12000 frames, more than
  half of the 16384 frames that Audio 2's buffer holds at most, and stopped
  with "Setting of hwparams failed".
* Both engines use demand-mode DMA, with which they kept their rates while
  both ran.
* The rates are the measured clocks, 793.8, 396.9 and 768 kHz, not the data
  sheet's 795.5 and 397.7 kHz.
* The interrupt handler takes each source back where it lives, as ESS's
  drivers do, until a pass finds none, and Config_Base+7 lets only those
  sources onto the edge-triggered line, so that no other one holds it high.
  `irq_mode=1` uses the data sheet's way instead: Config_Base+7 cleared on
  entry and restored on exit, so that a source still pending raises a new
  edge. In either mode, more than 20000 calls of the handler in one second
  are taken for a storm, which takes every source off the line until the
  driver loads again, and /proc/asound/card0/es1869 says so.

## The PIIX4's Type F timing

The Armada 1500c's BIOS turns on the Type F DMA timing of its Intel PIIX4
for both of the ES1869's channels (ISA bridge registers 76h and 77h, which
it sets to 8Dh and 89h). Playback works with it, but the first byte a
recording moves into memory hangs the machine, with no message, until a
power cycle. With Type F off for DMA 1, recordings run at their rates.

The driver therefore turns Type F off for Audio 1's channel when it loads,
and says so in the kernel log. It stays off until the next boot, also after
the driver is removed, since that is the safe setting for whichever driver
uses the chip next. `piix4_type_f=1` keeps the BIOS's setting. Audio 2's
channel keeps Type F, because Audio 2 only plays.

The BIOS sets both registers where it builds the ES1869's PnP node: at every
POST, and whenever the PnP BIOS is asked for the boot configuration, which
Linux never does with PnP ACPI. Its SMM keeps both across a suspend to RAM.
The driver writes its setting again at every resume, after whatever the BIOS
or ACPI put back (celeronix-6a's BIOS notes, chapter 03).

## Devices

The card keeps snd-es18xx's id, ES1869, its PCM layout and its control
names, so that saved mixer states, `hw:0,0,1` in configuration files and
programs that set the master still work.

* PCM device 0, playback subdevice 0: Audio 2. On a 16-bit DMA channel it
  plays from the ring of words described above.
* PCM device 0, playback subdevice 1: Audio 1.
* PCM device 0, capture: Audio 1. Audio 1 plays or records, not both, so
  opening one while the other runs fails with EBUSY. Playback on Audio 2 and
  recording on Audio 1 run at once.
* PCM device 1, capture: the music DAC recorded digitally on Audio 1, as
  16-bit stereo at 49716 Hz (mixer 7Fh bit 4).
* PCM device 2, playback: both DACs as one four-channel device, channels 1
  and 2 on Audio 1 and 3 and 4 on Audio 2, on Audio 1's clock and from one
  start; on the Armada the two DACs began 1 to 4.5 frames apart at 44.1 kHz
  and from -15 to +15 at 22050 Hz, not the same at every rate or every start
  (es1869test's dual.align). It opens with `dual_playback=1`, and needs both
  engines free and Audio 2 on a 16-bit DMA channel.
* PCM device 3, capture: the monitor, the frames Audio 2's DAC took, read
  back from what its DMA reads, as 16-bit stereo at the DAC's rate. It is
  the reference for a recording of the analog loop on Audio 1, takes no
  engine, and follows Audio 2's stream, with an xrun at each break in it
  ([LINUX_MONITOR.md](LINUX_MONITOR.md)).
* hwdep 0 and the sequencer's "OPL3 FM" synth: the FM in its OPL3 mode.
* hwdep 1, "ES1869 ESFM": the FM in its native ESFM mode, for programs that
  write its registers themselves (ioctls in `uapi/es1869.h`).
* snd-es1869-esfm's sequencer client "ES1869 ESFM synth": General MIDI on
  the native ESFM, with ESS's GM bank as the firmware file
  `es1869/esfm-gm.bin`. Without the file, every program plays a sine.
* One of the OPL3 hwdep, the OPL3 synth, the ESFM hwdep and the ESFM synth
  has the FM at a time; the others get EBUSY until it lets go.
* hwdep 2, "ES1869 regs": every register of the chip through the driver's
  lock, for essreg's register utility and the test suite.
* The MPU-401 as raw MIDI device 0.
* `/proc/asound/card0/es1869`: the resources, the engines' owners, the
  counters, every setting with its default, and a register dump.

## Rates

The driver offers every rate of the chip's clocks: 793.8 kHz and 768 kHz
divided by 1 to 128 on both DACs, and 396.9 kHz divided by 1 to 128 on
Audio 1. The lowest rate is 3101 Hz on Audio 1, and on Audio 2 when its own
clock is off and it follows Audio 1's. The highest rates are what
es1869test's rate.max measured on the Armada (`a2_max_rate`, `a1c_max_rate`
and `a1p_max_rate`), and `rate_limit=0` lifts them:
* Audio 2 up to 96 kHz. With Audio 1 recording at 44.1 kHz at the same time,
  it stayed clean to 99.225 kHz in periods of about 220 frames, the driver's
  default, and to 132.3 kHz (793.8 kHz / 6) in longer ones.
* Audio 1's recording up to 52.92 kHz.
* Audio 1's playback up to 48 kHz, which the second card's microphone input
  can't judge above.

What limits the top is the ISA bus. Audio 2 takes one byte a DMA cycle, and
on silence DMA 5 moves at most about 770,000 bytes a second with the chip's
demand of 8 (`a2_burst=3`), 757,000 with 4 and 740,000 with 2. Past that the
chip plays slower instead of skipping, as 16-bit stereo does at 198.45 kHz,
which asks for 794,000. A real stream has less room. With the Play! 3 on the
same bridge, 16-bit stereo was clean at 132.3 kHz, showed stalls as
sidebands at -34 dB at 158.76 kHz, and ran 0.65 % flat at 192 kHz. Audio 1
shares the bus: 88.2 kHz held while it recorded, with one take of eight
stalling, and 132.3 kHz broke while it recorded at 52.92 kHz. Mono and 8-bit
stereo have more room, and 198.45 kHz runs clean in both on silence. The
rates of the 768 kHz clock come out 252 ppm above those of the 793.8 kHz
clock.

The mixer's dB scales are the data sheet's. Measured on the Armada through
the chip's own record paths, Audio 2's playback level (7Ch) and record level
(69h, muted only at 0) follow Audio 2's table, the music DAC's record level
(6Bh) the microphone's, within 0.6 dB, and the record level (B4h) its 1.5 dB
steps from -6 dB, down to 0 without a mute, within 0.1 dB. The master's and
Audio 1's levels can be heard only through the cable, and the Play! 3's
microphone input that it ends in hears a 6 dB step as 7 to 10 dB. The master
is measured through AOUT, record source 3, within 0.33 dB of the table, and
Audio 1's level (14h) follows the table unmeasured.

The chip's clocks are one master clock divided. The 768 kHz clock and the
793.8 kHz one stand exactly 30 to 31 apart: es1869test reads 251.955 and
251.952 ppm, and 31 / 30 is 251.953. The FM's stays 2081.82 ppm fast of
49716 Hz on them, to 0.003 ppm over 90 minutes. PCM device 1 says 49716 Hz,
the music DAC's nominal rate, and so records the FM about 0.2 % faster than
it says.

The 793.8 kHz clock is the true one, and what the chip calls 768 kHz is
768193.5 Hz. So 44.1 kHz plays at 44100.04 Hz by the Armada's raw system
clock, 0.86 to 0.88 ppm fast in three runs, and 48 kHz at 48012.1 Hz, 252.9
ppm sharp on any crystal. A line through the earliest interrupt of each
second leaves 1.4 to 4 us rms, and its slope moved by about 0.02 ppm while
the laptop compiled and warmed up. An earlier reading here, of 178 to 247
ppm slow, was the test's: it timed a stream from its start through the end
of its drain.

Those readings point to one crystal under the chip and the system timer. A
master of 800 / 481 of the board's 14.318 MHz gives +0.873 ppm against the
kernel's count of the PM timer, which is what the three runs read, and two
crystals would hardly agree that well while the laptop's clock is off by 30
ppm. The FM's clock fits no such small ratio of either. On one crystal the
difference between the chip and the system can't drift, so that a warming
crystal shows only against NTP.

On the Armada chronyd runs as a service and steers the system clock; without
it, cron sets the clock from the network every six hours and nothing
corrects its rate. Two offsets from NTP 1404 s apart put the Armada's
crystal about 30 ppm slow of real time, as chrony's drift file has it, so
that 44.1 kHz plays about 29 ppm flat of real time and 48 kHz about 223 ppm
sharp, unless the Clock mode below corrects it.

## Rate correction

The chip has one clock input and no trim, so that a stream's rate can only
be changed in its samples. `a2_resample`, the control Audio 2 Rate
Correction, puts Audio 2's stream through a filter on its way to the ring:
* `0`, Off, leaves the samples as the application wrote them.
* `1`, Trim, moves the rate by `a2_trim`, the control Audio 2 Rate Trim, a
  list from -500 to +500 ppm that alsamixer steps through, plus
  `a2_trim_ppb` for a value in between. At 0 the samples stay exact.
* `2`, Clock, the default, plays the stream true to the system's clock, plus
  the same trim. With chronyd running that is real time.

The controls show up in alsamixer's Playback view (F3) with Audio 2 Rate
Dither, and all of them take effect while a stream plays. Turning the
correction off under a stream leaves out less than a frame.

It serves S16_LE streams of Audio 2 on a 16-bit DMA channel, which is the
Armada's, with a buffer of at least 256 frames. Any other stream, Audio 1,
the FM and the dual device play as they always do, and so does a pointer
that stands, as under dmix, whose data changes in place. The samples are
then no longer the application's, a choice between the true rate and the
exact samples. The driver takes the true rate by default; Off gives an
application its samples back exactly, and es1869test turns it off for every
case but the correction's own.

The filter is a Kaiser-windowed sinc of 68 taps, blended from 128 places at
each frame, in MMX where the CPU has it. What sets its length is the top of
the band: a tone's image comes back right beside it, at the beat, and at
44.1 kHz the image of 20 kHz lies at 24.1 kHz, so the filter's edge has to
fall between them. A first version of 36 taps held up to 18 kHz and then
gave -56 dB at 19 kHz and -29 dB at 20 kHz.

These are measures of the driver's code, at -1 dBFS on a 16-bit stream, and
none of them moves with the step from 0.1 to 1000 ppm:

| Measure                       | Result            | A perfect filter |
|-------------------------------|-------------------|------------------|
| THD+N, 20 Hz to 18 kHz        | -90.3 to -93.1 dB | -94.5 dB         |
| THD+N, 19 and 20 kHz          | -89.0 to -89.4 dB | -94.5 dB         |
| Worst spur to 20 kHz          | -94.6 dB          |                  |
| Level to 20 kHz               | within 0.0004 dB  |                  |
| Level at 21 kHz               | -0.36 dB          |                  |
| 19 and 20 kHz together (CCIF) | -102 dBFS         |                  |
| 60 Hz under 7 kHz (SMPTE)     | -122 dBFS         |                  |
| 500 tones with music's slope  | 0.42 LSB rms left | 0.41 LSB         |

A perfect filter still rounds twice, its input and its output, which is its
-94.5 dB. `tools/rsquality.py` gives these numbers and more on the host, and
`tests/test_es1869rs.py` holds the filter to them.

On the chip, the dig path reads every frame that the DMA took, and
es1869test compares them with the filter's C code on the same stream
(`rs.exact`): not one of about 176,000 differs, in stereo, in mono, or with
a client polling two periods. Tones through that path (`rs.band`) read the
same as on the host within 0.01 dB. Through the analog path, heard by Audio
1, the filter takes nothing that the chip can show: with it and without it
in turn, THD+N reads -61.6, -52.7 and -43.7 dB at 1, 10 and 16 kHz, within
0.06 dB, and the level stays within 0.002 dB.

It takes 3.1 % of the Armada's 400 MHz Celeron at 44.1 kHz stereo and 3.8 %
at 48 kHz, by the driver's own cycle counts, against 0.3 % with it off. A
frame costs 190 cycles in MMX, 119 in mono, and 625 in C where the kernel
can't take the FPU. Two things took a fifth off: each channel is kept in
four copies a sample apart, so that a frame's taps lie on 8 bytes and no
word crosses a cache line, and the 32 outer taps are blended at the middle
of a place 2^-12 of a frame wide and held while the fraction stays there,
which is about 8 frames at 30 ppm. That is a pure function of the fraction,
so the C and the MMX code still make the same bytes however a stream is cut
up. A fill holds interrupts off for about 80 us, and for 125 us at the worst
seen, while the machine's own worst timer delay is over a millisecond with
the filter or without it.

`a2_oversample` ("Audio 2 Output Rate") clocks the DAC at two or three times
a 16-bit stream's rate, through the same filter, with or without a change of
rate on top. The chip's rate register, 70h, holds a divisor of 793.8 kHz or,
with bit 7, of 768 kHz, and the driver writes the divisor of the higher
rate: a 44.1 kHz stream plays at 88.2 or 132.3 kHz, and a 48 kHz one at 96
kHz. It applies where a divisor of the stream's own gives the rate exactly,
so 48 kHz takes 2x and no more, and up to 132.3 kHz. The DAC then holds each
sample for a half or a third as long, so that its images move up by as much.

A write takes effect at once under a playing stream, which stops and starts
afresh as at a pause's release, and that costs what a release costs. A
paused stream takes it at its release, and a drain goes on as it is. The
line "Audio 2 DAC" in `/proc/asound/ES1869/es1869`, and es1869ctl's Status
page, show the rate in force while a stream runs. 70h keeps it after the
stream.

The ring keeps its size. It holds two or three laps of the buffer where it
has the room, and otherwise a lap as long as the buffer, as with MPD's 16384
frames, which is a half or a third of the buffer at the DAC's rate. The fill
then goes a lap ahead at most, and a timer tops it up four times a lap.
Audio 2's transfers turn to Demand 8, which leaves the bus the most. 3x
takes up so much of the ISA bus that Audio 1 can't record at the same time.
A client whose pointer stands, as under dmix, gets each frame written again
without the filter, which takes the room for all the laps, so there the rate
stays as low as the ring allows. At 2x every frame of the DMA matches the
filter's C code on the chip. The driver takes 6.1 % of the Celeron at 44.1
kHz stereo at 2x, and 9.7 % at 3x.

Measured through a VX Pocket on the Armada, steady playback is as clean at
2x and 3x as at 1x: THD+N stays within 0.5 dB at 997 Hz and 10 kHz, and the
noise of a silent stream is the same. What the higher rates give up is
margin. The chip's 64-byte FIFO lasts 360 us at 1x of 44.1 kHz stereo, 180
us at 2x and 120 us at 3x. When the DMA can't reach the chip for that long,
the DAC stops until it can, and the sound comes that much later: nothing is
lost, and the stream's position doesn't move.

`linux/tools/a2stall.c` finds those stops from the DMA's own position, with
no second card. At 3x it found none in 30 to 40 s each of quiet, of bursts
of disk writes, of a disk read at full speed and of a CPU hog. The filter is
never what runs short: its fill stayed at least 11,572 of the ring's 16,384
frames ahead of the DMA throughout, 87 ms, which /proc shows as the lead.
What does stop the DAC is a VX Pocket capturing at 48 kHz, by PIO through
the CardBus bridge: 2 stops in 40 s at 3x and at 2x, of up to 314 us, and
none at 1x or with the capture at 8 kHz. So that second card can show stops
that the DAC doesn't have without it. The IDE channel's PCI latency timer
stays at 40h: 20h changes nothing, and 10h gave 202 stops in 40 s.

The latency is the client's, since the fill only goes as far as what it
wrote. At 3x under bursts of disk writes, a buffer of 1024 frames, 23 ms,
ran clean with 908 ring frames of lead, and one of 512 frames, 12 ms, came
late three times, by about 5 ms each.

With its stages off, the DAC holds each sample for a whole sample period,
and each sample keeps about 6 % of the one before
([AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter)).
At 44.1 kHz that takes 1.2 dB off 10 kHz, 4.2 dB off 20 kHz and 5 dB off
22.05 kHz, and with the DAC at 2x or 3x 1.4 or 0.65 dB off 22.05 kHz.
`a2_treble` ("Audio 2 Treble Correction") gives that back through a table of
the filter for each rate of the DAC, with or without a change of rate. Its
16 taps a side, which `tools/es1869treble.py` fits to that model, go on the
filter's sinc before the window, so a frame costs what it did. They hold the
response within 0.05 dB up to 22.05 kHz at 1x and within 0.015 dB at 2x. The
first version had 7 taps a side and left 0.75 dB at 22.05 kHz, because a
short symmetric filter is flat at half the rate, where the hold still falls.
The gain reaches 4.95 dB at 22.05 kHz, so a stream that is that loud up
there clips, and the correction only fits a DAC with its stages off
(`audio2_oversampling=0`, `audio2_filter=0`). To follow a new measurement of
the DAC, change `LAST` in the tool and run it.

Where the filter moves the samples, at 1x with a change of rate and at 2x
and 3x, the top of the band is the filter's own edge, which no correction
moves without letting images through: 0.4 dB down at 21 kHz, 1.8 dB at 21.5
kHz and 5.5 dB at 22 kHz, with a tone's image 51 dB down at 20.5 kHz and 27
dB at 21 kHz. Only at 1x with no change of rate do the samples keep their
places and the response its 0.05 dB up to 22.05 kHz, which `rs.treble` reads
back from the chip's data path. With the first version, a 44.1 kHz stream at
the jack stayed within -0.12 and +0.21 dB of the DAC at 132.3 kHz up to 19.4
kHz, through a recorder.

A stream's period no longer matches the ring's, so a timer wakes the client
on the stream's own period boundaries, and the chip's interrupts stay as the
net under it. The ring holds as many frames as the buffer, and a step down
makes more ring frames than stream frames, so a fill stops short of a whole
lap.

`a2_resample_dither` rounds to 16 bits with triangular noise. It doesn't
pay. Across tones from -60 to -96 dBFS and steps of 0.1, 1 and 30 ppm it
adds 3 to 6.6 dB of noise, and the worst spur moves by 2 dB either way, but
for one case of 6.5 dB at -60 dBFS and 0.1 ppm, near -110 dBFS, some 50 dB
under the chip's floor. On the chip, at -80 dBFS and 1 ppm, it adds 6.2 dB
of noise and leaves the worst spur at -106 dBFS (`rs.dither`). The switch
stays, off by default, for anyone who wants to hear that for themselves.
Dithering the filter's place instead trades 20 dB of spurs for 5 dB of noise
with a nearest-row filter, and blending the rows beats it on both, so the
driver has none.

The Clock mode follows a chain of two measures, which
`/proc/asound/card0/es1869` shows:
* The chip against the raw system clock. Every stream's interrupts are a
  known count of the chip's master clock, and a line through the earliest of
  each second gives its rate, whichever stream runs: Audio 2 while it plays,
  Audio 1, playing or recording, otherwise. It leaves 1.4 to 4 us rms, so
  that a few minutes give it to a few ppb.
* The raw clock against the clock NTP steers, over about a quarter of an
  hour, so that the daemon's corrections of its own offset don't reach the
  pitch. This needs `chronyd` running as a service; `chronyd -q` from cron
  leaves the kernel's frequency at 0, and the stream then locks to the
  laptop's own crystal.

`/proc/asound/card0/es1869_irqs` lists the last 512 interrupts that feed the
first, for es1869clock ([ES1869SCOPE.md](ES1869SCOPE.md)).

The two are measured apart and nothing feeds back from the filter into them,
so the correction can't hunt. A new stream starts from what the last one
measured. The very first stream after a load has no slopes yet, and takes
about a minute to settle within 0.2 ppm.

Over half an hour with chronyd running, a stream in the Clock mode held to
the steered clock within 0.08 ppm at 44.1 kHz, with the CPU under full load,
and 0.05 ppm at 48 kHz, about 0.2 ms an hour. The same streams with the
correction off ran 28.9 ppm slow and 223 ppm fast. Over a single minute a
stream follows the driver's slope within 0.3 ppm, since both fits are still
moving.

The machine's other clocks, and how chrony's frequency follows the board's
temperature, are in [CLOCK_CORRECTION.md](CLOCK_CORRECTION.md).

Audio 2's DAC puts its codes from 0 up about 8 LSB higher than those below.
With `a2_zstep` on, each code moves about that gap as it goes to the ring,
by `a2_zstep_l` and `a2_zstep_r`, 2048 on the Armada as the VX measured it
on the line out
([LOOP_TRANSFORM.md](LOOP_TRANSFORM.md#the-dacs-gap-at-its-sign)).

A correction of the rate is not one of jitter. Jitter is in the DAC's clock
at the moment of each conversion, after all of this, and no change to the
samples reaches it. The filter's own place between two frames is exact to 22
bits, so it adds none. Around a tone heard by Audio 1, the Armada shows 5 ns
rms of timing noise at 10 kHz and 14 ns at 16 kHz, within 2 kHz of it, which
is an upper bound: it holds the path's noise, and the two engines share a
clock.

The rate register itself can be changed under a running stream. A write to
70h takes effect at the next sample, in steps of 13 or 18 of the master's
ticks, 0.55 or 0.76 us, and a burst shorter than a sample works on average:
100 ms a second on the other clock read +2466.9 ppm of a predicted +2466.8.
So the rate could be held by pulse width with the samples left exact, but
the steps stay whole periods, however the pulses are timed. Tuned pulses in
the kernel, with interrupts off, of a sample's length, 40 a second, measured
0.9 to 1.8 us rms of timing noise and THD+N of -48, -21 and -11 dB at 1, 10
and 16 kHz, where plain playback and the filter both read 13 to 21 ns and
-61, -52 and -46 dB in the same session. Perfect pulses would still leave
about 220 ns, so the driver doesn't use them. Nor can the clock make a rate
that no divisor gives, such as 176.4 kHz from 793.8 kHz / 4.5, by turning
between /4 and /5: that takes a write a sample, and a write of index and
data costs 3.1 us of the ISA bus and more with the lock around it, where a
sample lasts 5.67 us. The filter clock (72h) moves a low-pass corner and has
no part in the timing:
[AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-filters-measured) has what it does
under a stream.

## CPU and latency

The driver keeps its own work small, since the Armada's Celeron has little
to spare. With both DACs playing 44.1 kHz 16-bit stereo, the driver's
interrupt handler and Audio 2's ring took about 0.4 % of the CPU, and the
whole machine, with both players, 1.8 %. `/proc/asound/card0/es1869` shows
what each part costs, in TSC cycles, on its `cost:` line:

* The interrupt handler reads Config_Base+6, which shows every pending
  source in one access, and takes back only those; it polls each source
  where it lives, as ESS's drivers do, only when that read finds nothing,
  and counts the times polling then finds one (none on the Armada).
* Audio 2's ring is copied when the application writes, two bytes to a
  store. The write's own call copies up to 1 KB, with interrupts off, about
  18 us, and a work item copies the rest in pieces of that size, with
  interrupts on between them, so that a whole period handed over at once, as
  MPD does through plughw, doesn't hold them off for 140 us. The timer runs
  under the work item while a copy is behind, and every 10 ms under dmix,
  whose data changes in place.
* Each position is two readings of the 8237's count that agree within 32
  transfers, so that a reading the low byte's wrap tore is never used, and
  the pointer never steps back.

The chip's FIFOs, 256 bytes on Audio 1 and 64 on Audio 2, are in the delay
that `snd_pcm_delay` reports. Small periods cost an interrupt each, about 60
us on the Armada; MPD's default buffer of 500 ms keeps the interrupt rate
low.

## Known limits

Measured with es1869test, frame by frame where the music DAC records Audio 2
([ES1869TEST.md](ES1869TEST.md)):
* Audio 2's stop clears its FIFO, so a pause's release, which starts the
  engine afresh from the paused frame less a FIFO, can play a few frames
  again (4 on the Armada).
* Under dmix on a 16-bit channel, the ring of words copies the window ahead
  of the DMA every quarter of `a2_lead_ms`, 10 ms by default, and dmix mixes
  a stream in at the DMA's position, so a stream can lose up to that much of
  its start (8 ms on the Armada). A smaller `a2_lead_ms` shortens it, for
  more of the ring's timer.

The Armada's BIOS reaches the chip at run time in one place: the Fn-key
volume popup, an SMI even in ACPI mode, reads the PC speaker's volume, mixer
3Ch, and writes it while the popup is up, from Computer Setup's speaker
volume (CMOS 47h, 0 to 100, as 0 to 7). It saves and restores the mixer's
index register around that, and sends the DSP nothing, so the driver's index
and data accesses and its DSP commands are safe. The BIOS's only DSP
commands outside S3 and POST, C6h and then B4h at 22h, sit in an SMI that
nothing calls, and its other writes of 3Ch come only from Computer Setup's
int 15h call, which Linux never makes (celeronix-6a's BIOS notes, chapters
05 and 09). At load the driver takes the PC speaker's level from 3Ch as the
BIOS left it, unless a parameter sets it: the chip's reset value is 4, and
on the Armada CMOS 47h is 0, so the PC speaker is silent there until
something raises it.

The PC speaker comes out on the chip's own pin (PCSPKO), and the board mixes
it in after the chip, so the master, its mute and the volume buttons don't
reach it. So the driver does what ESS's ES1869.DRV does, with `beep_follow`,
on by default: Beep Playback Volume is the level at full master, and 3Ch
gets that level times the louder side of the master, plus 1, over 64, or 0
while both sides are muted. The buttons move it too, and a change from the
Fn keys lasts until the next look at the chip. When the driver unloads it
leaves the level itself in 3Ch, for the next load to find. With
`beep_follow=0` the two stay apart, as in snd-es18xx: the control reads 3Ch
from the chip, so that it follows the popup, and a resume puts back what it
had.

## Power

With `pwr_save` set to a number of seconds, the chip powers down when
nothing has used it for that long: partly, with the mixer, the inputs, the
MPU-401 and the volume buttons still on, or fully with `pwr_full=1`. The
next program that opens a device wakes it, which after a full power-down
takes 100 ms and puts every register back. The OPL3 synth has no open, so
its first events after a full power-down start the wake and are lost.
Suspend and resume put the chip back as it was: the streams go on after a
prepare, the FM stays in the mode its user chose, and an open MIDI port goes
back to its UART mode. If the chip doesn't come back, its devices fail and
the programs that had them open can close them.

## MIDI

MIDI files play these ways on the laptop:

* WildMidi synthesizes them with the freepats patches:
  `wildmidi -d plughw:0,0,1 song.mid`. A script must give it a stdin
  (`< /dev/null`), or it waits for its keys and plays nothing. MPD's
  wildmidi decoder plays them on whichever output MPD uses.
* The OPL3 synth (`aplaymidi -l` lists it as "OPL3 FM synth") plays them on
  the chip's FM once `sbiload` from alsa-tools has loaded patches into it
  through the OPL3 hwdep, which it finds by itself: plain `sbiload` loads
  the four-operator bank, std.o3 and drums.o3 from /usr/share/sounds/opl3,
  and `sbiload -2 std.sb drums.sb` the two-operator one. Then
  `aplaymidi -p 17:0 song.mid` plays on the synth's port.
* The ESFM synth plays them on the chip's native ESFM with ESS's GM bank:
  `aplaymidi -p "ES1869 ESFM synth" song.mid`. The bank is the firmware file
  `es1869/esfm-gm.bin`, ESS's `bnk_com.bin`.
* PCM device 1 records what the FM plays, digitally and exactly: `arecord -D
  hw:ES1869,1 -f S16_LE -r 49716 -c 2 fm.wav`. With FM idle it reads a
  constant +128.

The chip plays the FM's two sides the other way round from an OPL3: bit 4 of
C0h-C8h, an OPL3's channel A and its left, sounds on the right output, and
so does bit 4 of the native mode's register 6, which ESS's driver uses for
the left. With `fm_sides=1`, the default, the driver swaps the two bits in
what the OPL3 synth and hwdep write, and the ESFM synth pans left with bit
5, so that a channel panned left sounds on the left. The ESFM hwdep writes
its registers as it gets them. With `fm_sides=0` both synths play as the
chip has it, mirrored, which is what ESS's drivers do.

The ESFM synth follows the rebuilt ESFM.DRV, which `tests/test_esfmgm.py`
checks message by message in the emulator, and it played `build/GMCHECK.MID`
on the Armada with every step as [TESTING.md](TESTING.md#g3-general-midi)
describes it. Like ESS's driver, it plays 6 to 9 cents flat of A4 = 440 Hz
([ESFM_ENGINE.md](ESFM_ENGINE.md#6-pitch)).

The MPU-401 is raw MIDI device 0, in UART mode, and a byte coming in raises
the chip's interrupt. The driver opens and sends through it, but no byte has
yet been seen to cross a cable in either direction on the Armada.

### A parallel port MIDI interface

The Armada's parallel port is off in its BIOS, and an interface that takes
its power from the port stays dead until it is on. ACPI turns it on from
Linux, until the next boot, on PNP0401's node, found with `grep -l PNP0401
/sys/bus/pnp/devices/*/id`:

```
echo disable > resources
echo clear > resources
echo "set io 0x378-0x37f io 0x778-0x77a irq 7 dma 3" > resources
echo activate > resources
```

The node reads active with no resources while the port is off, and the
kernel ignores `clear` and `set` on an active node, so `disable` comes
first. `activate` fills in a DMA channel of its own if none is set, and DMA
3 is free. parport_pc must stay out of it: its own probe activates the port
with its own choice of resources, which is why the laptop blacklists it, and
it can't share the port with snd-mtpav.

An Opcode MIDIEngine 2Port/SE, a Music Quest design with its own power, has
a driver of its own, snd-mqengine, in `linux/mqengine/`. Its two inputs and
two outputs are one raw MIDI device, subdevice 0 for A and 1 for B, and the
sequencer's client "MIDIEngine 2Port/SE" has a port for each.
`linux/tools/mqengine-up.sh` turns the parallel port on and loads the
module, and with `off` takes both away:

```
make -C linux mqengine
sudo linux/tools/mqengine-up.sh linux/mqengine/snd-mqengine.ko
aplaymidi -p "MIDIEngine 2Port/SE:0" song.mid
sudo linux/tools/mqengine-up.sh off
```

Where the module is part of the kernel's build, `mqengine-up.sh` alone loads
it. Its parameters are `port`, 0x378 by default, `irq`, 7 by default or -1
to poll at each timer tick, `index` and `id`. It takes the port itself, as
snd-mtpav does, so parport_pc, snd-mtpav and snd-flyer have to stay out of
it, and it refuses to load unless the port answers as the unit does. It
handles no suspend, so unload it and load it again after one.

On the Armada, 1024 bytes came back exact through a cable from output B to
input B, by interrupt and polled, also with 1024 bytes coming in on input A
at the same time, and every event of a 37 s MIDI file came back through the
sequencer. Output A held 3,857 bytes in a row exact. That was read through a
MIDISPORT 2x2, which holds bytes that reach it with nothing reading and
hands them over ahead of newer ones, so an output is best tested through a
cable to the unit's own input.

The protocol, in `linux/mqengine/proto.h`, comes from Opcode's MQ2PRTSE.DRV.
D5 at the end of STROBE is the output, whatever bit 5 of the byte is. From
one byte to the next, either way, the unit needs time that its flags don't
cover: a gap of 30 us stayed exact and 20 us lost bytes, so the driver
leaves 60 us. Its output buffer holds about 64 bytes, and it raises its
interrupt once for each byte in either direction. `linux/tools/mqengine.c`
reaches the unit without the driver, by port I/O as root.

A MOTU PC-MIDI Flyer, two inputs and two outputs that take their power from
the port, has a driver of its own too, snd-flyer in `linux/flyer/`, which
`linux/tools/flyer-up.sh` loads in the same way. [FLYER.md](FLYER.md)
describes it, the unit's protocol and its tests.

## Tools

`linux/tools/es1869ctl` is a full-screen panel of the driver's settings and
status, with what alsamixer can't show: the streams and their real rates,
what the ADC is hearing, and the parameters.
[LINUX_CONTROLS.md](LINUX_CONTROLS.md) lists every control, what it does on
the chip, and how to reach it. `make -C linux/tools/es1869ctl` builds it,
with alsa-lib and ncurses.

`linux/tools/essreg` builds essreg, the register utility, from its own
sources in `src/`, with its register access through the driver's register
hwdep instead of the ports: `make -C linux/tools/essreg`, then `essreg a`
for every value or `essreg r=regs.txt` for a dump. Reads work as root;
writes also need the module's `regs_write=1`.

`linux/tools/a2stall.c` plays silence and finds each stop of Audio 2's DAC
from its DMA position against the raw clock, with no second card:
`a2stall 44100 40 4096 16384`, the rate, the seconds, the period and the
buffer.

`linux/tools/clockcmp.c` compares the machine's clocks with the raw one.
`linux/tools/pcmclock.c` follows any card's sample clock against the raw,
steered and real-time clocks, and `linux/tools/phaselog.c` reads a tone from
Audio 2 at another card's capture, which sets the two cards' clocks against
each other with no timer of the machine's in the way. `linux/tools/modlab.c`
moves a rate or a filter clock under a running stream, through the register
hwdep's ES1869_IOCTL_PATTERN, which writes a table of 70h, 72h, A1h or A2h
at even times of the cycle counter, with `regs_write=1`. The cycle counter
is on the chip's crystal, so a pattern stays in phase with the samples.
`linux/tools/iotime.c` times the chip's ports. A port takes 1.53 us on the
ISA bus, the chip's or the game port's, against 0.72 us for one the bridge
answers itself. Index and data take 3.1 us as two OUTs and 2.3 us as one
16-bit OUT at the index, which reaches both ports and can't be split by an
interrupt, and data alone 1.6 us.

`linux/tools/es1869scope` holds es1869clock, which logs the chip's clock
against the machine's, real time and other cards' crystals, and es1869scope,
which draws the log on the framebuffer: [ES1869SCOPE.md](ES1869SCOPE.md).

## The test suite

es1869test, in `linux/test/`, plays and records through the chip and checks
what came back: every frame of Audio 2's DMA through the music DAC, the FM,
Audio 1's recordings and, through a cable to a second card, its playback,
the delay, the parameters, the 3-D effect's controls, the MIDI synth, and,
when asked, the volume buttons, a suspend and resume, reloads of the driver
and a stress run. With `--stock` it runs what applies to snd-es18xx, for the
comparison. [ES1869TEST.md](ES1869TEST.md) says how to build and run it.

On the Armada, `es1869test --stock` on snd-es18xx passed the chip's clocks,
a drain, the recovery from an xrun, the shortest periods and the MPU-401,
and failed every recording of Audio 2: Audio 1 heard its 441 Hz tone at -58
to -76 dBFS where snd-es1869 records it at -17 dBFS, since Audio 2 plays
nothing on DMA 5. snd-es18xx's volumes have no dB scales, so the cases of
the mixer's steps failed too. The run skipped the cases that need
snd-es1869's own devices, controls or parameters. Through the cable,
snd-es18xx's Audio 1 played every format and rate, and 20 starts in step, so
the start race that snd-es1869's settle avoids didn't show there.

## Loading it

On the Armada, the chip's resources are those that DOS and Windows use too:

    modprobe snd-es1869 isapnp=0 port=0x220 irq=5 dma1=1 dma2=5 \
    fm_port=0x388 mpu_port=0x330

Without `isapnp=0` the driver takes the ESS1869 device that the BIOS
describes through ACPI or the PnP BIOS. In legacy mode the resources the
parameters leave out come from the chip's own PnP registers, and those they
give go into them. With no DMA channel for Audio 2 (75h at 4), Audio 1 runs
alone. snd-es18xx claims the same devices, so blacklist it.

The Armada's BIOS keeps the chip's resources in CMOS, as Computer Setup's
Audio page sets them, and programs them into the chip at POST. Either DMA
channel can be 0, 1, 5 or none (DMA 3 belongs to the ECP port), and Audio 2
has DMA 5 by default. With Audio 2 on DMA 0, an 8-bit channel, and `dma2=0`,
Audio 2 plays straight from its buffer, without the ring of words and its
copy.

`linux/config/` holds the Armada's configuration, which its deployment uses:

* `armada1500c-modprobe.conf`, for `/etc/modprobe.d/`: the resources above,
  index 0, snd-es18xx blacklisted, the three DAC stages off, with what 1
  does for each, and the volume buttons in 2-wire mode (`hwvol_mode=1`): the
  Armada has only the two, and both pressed together are the mute.
* `armada1500c.kconfig`: the lines for the kernel's configuration, the
  module and its defaults.
* `armada1500c-asound.state`: the `state.ES1869` block for
  `/var/lib/alsa/asound.state`, the laptop's saved mixer restored onto the
  driver's controls, with Audio 2's volume at 0 dB, the DAC stages off and
  the buttons in 2-wire mode. alsactl restores it at boot, after the module
  has loaded, so it decides over the parameters.

## Settings

Each setting below is a module parameter. It can be given to modprobe, on
the kernel command line as `snd_es1869.<name>=<value>`, and at runtime
through `/sys/module/snd_es1869/parameters/<name>`. A write at runtime goes
to the chip at once, unless the setting says otherwise below, and the
control of the same setting announces it. A stereo value is written as
`left,right`, or as one value for both. An enum takes its number or its
text. Where a setting has a control, alsamixer and amixer change it too;
alsactl restores the controls at boot, after the module has loaded, so a
saved state wins over a parameter. The defaults are the Kconfig options
`CONFIG_SND_ES1869_DEFAULT_<NAME>`.

<!-- settings -->

* `master_volume` ("Master Playback Volume"): 0 to 63 a channel, default 54.
  Master volume (60h/62h bits 5:0), 63 = 0 dB, 0.75 dB a step.
* `master_switch` ("Master Playback Switch"): 0 to 1, default 1. Master on
  (60h/62h bit 6 clear).
* `a2_volume` ("PCM Playback Volume"): 0 to 15 a channel, default 15. Audio
  2's volume (7Ch), 15 = 0 dB.
* `a1_volume` ("PCM Playback Volume"): 0 to 15 a channel, default 14. Audio
  1's volume (14h), 14 = 0 dB.
* `fm_volume` ("FM Playback Volume"): 0 to 15 a channel, default 8. The
  music DAC's volume (36h), 8 = 0 dB.
* `line_volume` ("Line Playback Volume"): 0 to 15 a channel, default 0. Line
  in to the output (3Eh), 13 = 0 dB.
* `cd_volume` ("CD Playback Volume"): 0 to 15 a channel, default 0. AuxA
  (CD) to the output (38h), 13 = 0 dB.
* `aux_volume` ("Aux Playback Volume"): 0 to 15 a channel, default 0. AuxB
  to the output (3Ah), 13 = 0 dB.
* `mic_volume` ("Mic Playback Volume"): 0 to 15 a channel, default 0. The
  microphone to the output (1Ah), 8 = 0 dB.
* `mono_volume` ("Mono Playback Volume"): 0 to 15 a channel, default 0.
  MONO_IN to the output (6Dh), 13 = 0 dB.
* `beep_volume` ("Beep Playback Volume"): 0 to 7, default 0. The PC speaker
  (3Ch bits 2:0), 7 = -6 dB, 0 mutes it; the board mixes it in after the
  master, so with beep_follow this is its level at full master. Without a
  parameter at load, the chip keeps its level.
* `beep_follow` ("Beep Follows Master Playback Switch"): 0 to 1, default 1.
  3Ch scaled by the master and 0 while it is muted, as ESS's ES1869.DRV does
  it; 0 keeps the two apart.
* `capture_source` ("Capture Source"): 0 Mic, 1 Mic Master, 2 CD, 3 AOUT, 4
  Mic1, 5 Mix, 6 Line, 7 Master, default Mix. The record source (1Ch bits
  2:0): 5 the record mixer, 7 the 3-D output before the master.
* `capture_volume` ("Capture Volume"): 0 to 15 a channel, default 4. The
  record level (B4h, right in bits 7:4), 1.5 dB a step.
* `capture_switch` ("Capture Switch"): 0 to 1, default 1. The record source
  on (1Ch bit 4 clear).
* `record_monitor` ("Record Monitor"): 0 to 1, default 0. AOUT carries the
  record source in place of the playback mix while Audio 1 records (A8h bit
  3).
* `a2_capture` ("PCM Capture Volume"): 0 to 15 a channel, default 0. Audio 2
  into the record mixer (69h).
* `mic_capture` ("Mic Capture Volume"): 0 to 15 a channel, default 0. The
  microphone into the record mixer (68h).
* `line_capture` ("Line Capture Volume"): 0 to 15 a channel, default 0. Line
  in into the record mixer (6Eh).
* `fm_capture` ("FM Capture Volume"): 0 to 15 a channel, default 0. The
  music DAC into the record mixer (6Bh).
* `cd_capture` ("CD Capture Volume"): 0 to 15 a channel, default 0. AuxA
  (CD) into the record mixer (6Ah).
* `aux_capture` ("Aux Capture Volume"): 0 to 15 a channel, default 0. AuxB
  into the record mixer (6Ch).
* `mono_capture` ("Mono Capture Volume"): 0 to 15 a channel, default 0.
  MONO_IN into the record mixer (6Fh).
* `mic_boost` ("Mic Boost (+26dB)"): 0 to 1, default 1. The microphone's +26
  dB preamp (7Dh bit 3), on after a hardware reset.
* `adc_offset` ("Capture DC Offset"): -16 to 15, default 0. The ADC's
  offsets (BAh/BBh bits 4:0), 64 a step of the 16-bit sample.
* `wake_no_mute` ("Analog Wake No Mute Switch"): 0 to 1, default 0. No 100
  ms AOUT mute when the analog section wakes (BAh bit 5).
* `audio2_oversampling` ("Audio 2 Oversampling Playback Switch"): 0 to 1,
  default 0. Audio 2's 4x oversampling (71h bit 4), which also bypasses its
  filter.
* `audio2_filter` ("Audio 2 Filter Playback Switch"): 0 to 1, default 0.
  Audio 2's switched-capacitor filter in use (71h bit 3 clear).
* `audio1_filter` ("Audio 1 Filter Switch"): 0 to 1, default 0. Audio 1's
  filter in use (71h bit 2 clear), also the ADC's anti-alias filter.
* `audio2_own_clock` ("Audio 2 Own Clock Switch"): 0 to 1, default 1. Audio
  2 at its own rate (71h bit 1), else slaved to Audio 1's. A write fails
  with EBUSY while Audio 2 is open.
* `audio2_to_fm` ("Audio 2 To FM Playback Switch"): 0 to 1, default 0. Audio
  2 slaved to the FM (71h bit 0): mixed into the music DAC at 49716 Hz, its
  sides swapped. A write fails with EBUSY while Audio 2 is open.
* `video_switch` ("Video Playback Switch"): 0 to 1, default 0. The music DAC
  driven by I2S (7Fh bit 0), which silences FM.
* `es689` ("ES689 Input Switch"): 0 to 1, default 1. The ES689 wavetable
  interface (48h bit 4).
* `a8_play_bits`: 0 to 244, default 0. A8h's bits beside its channels and
  monitor in playback: ESS's player keeps 00h. It takes effect at the next
  prepare.
* `a8_rec_bits`: 0 to 244, default 244. A8h's bits beside its channels and
  monitor in a recording: ESS's F4h (F5h/F6h). It takes effect at the next
  prepare.
* `filter_corner` ("Filter Corner"): 100 to 1000, default 875. The filters'
  corner below 44.1 kHz, in thousandths of half the rate; ESS's 875. It
  takes effect at the next prepare.
* `mono_out` ("Mono Output Source"): 0 Mute, 1 CIN_R, 2 Audio 2 Right, 3
  Record Mix, default Record Mix. What MONO_OUT carries (7Dh bits 2:1);
  ESS's 3.
* `mono_in_direct` ("Mono Input Direct Playback Switch"): 0 to 1, default 0.
  MONO_IN straight to the output at unity gain (7Dh bit 0).
* `spatial_switch` ("3D Control - Switch"): 0 to 1, default 0. The effect on
  (50h bits 3:2), out of reset as it turns on.
* `spatial_level` ("3D Control - Level"): 0 to 63, default 0. The effect's
  boost (52h), +16.8 dB near 450 Hz at 63, 0.75 dB a step.
* `spatial_mono` ("3D Control - Mono Switch"): 0 to 1, default 0. Stereo
  made from the mono sum (50h bit 1).
* `spatial_limit` ("3D Control - Limit Switch"): 0 to 1, default 0. The
  automatic width limit (50h bit 0).
* `spatial_rise_level` ("3D Control - Limit Rise Level"): 0 to 255,
  default 143. The limit's rise level (54h); ESS's 8Fh.
* `spatial_fall_level` ("3D Control - Limit Fall Level"): 0 to 255,
  default 149. The limit's fall level (56h); ESS's 95h.
* `spatial_rise_speed` ("3D Control - Limit Rise Speed"): 0 to 15,
  default 9. The limit's rise speed (58h bits 7:4); ESS's 9.
* `spatial_fall_speed` ("3D Control - Limit Fall Speed"): 0 to 15,
  default 4. The limit's fall speed (58h bits 3:0); ESS's 4.
* `spatial_fixed_level` ("3D Control - Limit Fixed Level"): 0 to 255,
  default 128. The limit's fixed level (5Ah); ESS's 80h.
* `hwvol_split` ("Hardware Master Volume Split"): 0 to 1, default 0. The
  buttons move 61h/63h instead of the master (64h bit 7).
* `hwvol_mode` ("Hardware Volume Mode"): 0 Three-wire, 1 Two-wire, 2
  Two-wire Fast, 3 Disabled, default Three-wire. The buttons' wiring and
  debounce (64h bits 3:2).
* `hwvol_count3` ("Hardware Volume Count By 3 Switch"): 0 to 1, default 0.
  2.25 dB a push instead of 0.75 dB (64h bit 5).
* `gpo0` ("GPO0 Switch"): 0 to 1, default 0. The GPO0 pin (Audio_Base+7 bit
  0), low after a hardware reset. Without a parameter at load, the chip
  keeps its level.
* `gpo1` ("GPO1 Switch"): 0 to 1, default 1. The GPO1 pin (Audio_Base+7 bit
  1), high after a hardware reset. Without a parameter at load, the chip
  keeps its level.
* `self_power_down` ("Self-Timed Power Down"): 0 to 255, default 0. Partly
  power down after this many 8 s of no access (BDh), 0 never.
* `gpo_power_down` ("GPO Power Down"): 0 to 255, default 0. The GPOs at
  power-down (DSP CFh, read with CEh).
* `pwr_save` ("Power Save Seconds"): 0 to 3600, default 0. Seconds of
  idleness before a power-down, 0 never.
* `pwr_full` ("Full Power Down Switch"): 0 to 1, default 0. Power down fully
  (the inputs go silent) instead of partly.
* `dma_single` ("Single Mode DMA Switch"): 0 to 1, default 0. Single
  transfers instead of demand transfers (8237, B9h, 78h). It takes effect at
  the next prepare.
* `a1_burst` ("Audio 1 DMA Burst"): 0 Type 0, 1 Type 1, 2 Type 2, default
  Type 2. Audio 1's demand transfers (B9h); ESS's 2. It takes effect at the
  next prepare.
* `a2_burst` ("Audio 2 DMA Burst"): 0 Single, 1 Demand 2, 2 Demand 4, 3
  Demand 8, default Demand 4. Audio 2's transfers (78h bits 7:6); ESS's
  demand 4. It takes effect at the next prepare.
* `piix4_type_f`: 0 to 1, default 0. 1 keeps an Intel PIIX4's Type F DMA
  timing on Audio 1's channel as the BIOS set it; 0 turns it off, since with
  it a recording's first transfer hung the Armada. A write fails with EBUSY
  while Audio 1 is open.
* `drq_latch` ("DRQ Latch Switch"): 0 to 1, default 1. Each DRQ held until
  its DACK (card register 25h bit 7).
* `a1_settle_us`: 20 to 2000, default 32. Audio 1's start held masked at
  least this long (us).
* `a1_settle_frames`: 0 to 16, default 2. And at least this many sample
  periods.
* `a1_settle_mode`: 0 to 1, default 0. 0 a timer unmasks the channel, 1 the
  start waits for it.
* `a1c_settle_ms` ("ADC Settle Milliseconds"): 0 to 1000, default 200. How
  long a recording waits for the ADC's input to settle after the codec turns
  to it (ms); 0 starts at once, as ESS's drivers do, and the first 100 ms
  then carry the input's offset decaying.
* `a2_lead_ms`: 4 to 500, default 40. How far ahead Audio 2's ring is filled
  on DMA 5 (ms).
* `a2_tick_us`: 500 to 20000, default 2000. The ring's refill period on DMA
  5 while a copy is behind (us); under dmix a quarter of a2_lead_ms, if that
  is longer.
* `a2_pause`: 0 to 1, default 0. Pause on an 8-bit Audio 2 channel through
  78h, which may resume off step. It takes effect at the next prepare.
* `a2_resample` ("Audio 2 Rate Correction"): 0 Off, 1 Trim, 2 Clock, default
  Clock. Audio 2's 16-bit streams on DMA 5 through a filter that corrects
  their rate, also while one plays: Trim by a2_trim, Clock also to the
  system's clock, as the chip's measures against it, which is true time with
  chronyd running; Off leaves the samples as they are.
* `a2_trim` ("Audio 2 Rate Trim"): 0 -500 ppm, 1 -200 ppm, 2 -100 ppm, 3 -50
  ppm, 4 -20 ppm, 5 -10 ppm, 6 -5 ppm, 7 -2 ppm, 8 -1 ppm, 9 -0.5 ppm, 10
  -0.2 ppm, 11 -0.1 ppm, 12 0, 13 +0.1 ppm, 14 +0.2 ppm, 15 +0.5 ppm, 16 +1
  ppm, 17 +2 ppm, 18 +5 ppm, 19 +10 ppm, 20 +20 ppm, 21 +50 ppm, 22 +100
  ppm, 23 +200 ppm, 24 +500 ppm, default 0. What the rate correction adds to
  the stream's rate.
* `a2_trim_ppb`: -2000000 to 2000000, default 0. More of the same in ppb,
  added to a2_trim.
* `a2_resample_dither` ("Audio 2 Rate Dither Playback Switch"): 0 to 1,
  default 0. The rate correction's rounding to 16 bits with triangular noise
  of 1 LSB: 3 dB more noise for spurs at about -100 dBFS.
* `a2_zstep` ("Audio 2 Zero Step Playback Switch"): 0 to 1, default 0. The
  DAC's gap at its sign taken back, each code moved about it.
* `a2_zstep_l` ("Audio 2 Zero Step Left"): 0 to 8192, default 0. The left
  DAC's gap in 1/256 LSB, as a recorder on the line out measures it: 2048 on
  the Armada.
* `a2_zstep_r` ("Audio 2 Zero Step Right"): 0 to 8192, default 0. The right
  DAC's.
* `a2_oversample` ("Audio 2 Output Rate"): 0 1x, 1 2x, 2 3x, default 1x. The
  DAC clocked at this many times a 16-bit stream's rate on DMA 5 (70h),
  through the rate filter, where a divisor of the stream's own gives that
  exactly: no images, less droop, more of the ISA bus and CPU. A playing
  stream takes it at once, restarting as at a pause. 3x can't run while
  Audio 1 records.
* `a2_treble` ("Audio 2 Treble Correction Playback Switch"): 0 to 1,
  default 0. Audio 2 through the filter with the DAC's own response taken
  back, at 1x, 2x and 3x: within 0.05 dB up to half the stream's rate, or up
  to the filter's edge at 20 kHz where it moves the samples, with the DAC's
  stages off, for 5 dB less headroom at the top.
* `a1c_filter` ("Audio 1 Correction Capture Switch"): 0 to 1, default 0.
  Audio 1's 16-bit recordings through a filter of 33 taps a channel that
  takes back the record path's own response, with the taps of "Audio 1
  Correction Capture Taps" (es1869scope -v 3 -C measures and loads them):
  the frames come 16 later, and the FM recording stays exact. It takes
  effect at the next prepare.
* `a1c_linear` ("Audio 1 Linearity Capture Switch"): 0 to 1, default 0.
  Audio 1's 16-bit recordings, after the correction filter, through a stage
  that takes back what the record path does to a sample's size: a curve
  about each side's centre, then a gain and a turn of the phase by the
  analytic signal's envelope and instantaneous frequency, from "Audio 1
  Linearity Capture Curve", "Gain" and "Turn" (es1869scope -C and -E measure
  and load them); the frames come 16 later, as through the filter. It takes
  effect at the next prepare.
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
* `rate_limit` ("Rate Limit Switch"): 0 to 1, default 1. 0 lifts the rate
  limits above, for es1869test's rate.max. It takes effect at the next
  prepare.
* `fmdig_device` ("FM Digital Capture Switch"): 0 to 1, default 1. PCM
  device 1, the music DAC recorded digitally, opens. It takes effect at the
  next open.
* `dual_playback` ("Dual Playback Switch"): 0 to 1, default 0. PCM device 2,
  both DACs as four channels, opens. It takes effect at the next open.
* `monitor_device` ("Audio 2 Monitor Capture Switch"): 0 to 1, default 1.
  PCM device 3, the frames Audio 2's DAC took, opens. It takes effect at the
  next open.
* `irq_sources`: 0 to 15, default 15. The sources the line carries: 1 Audio
  1, 2 Audio 2, 4 the buttons, 8 the MPU-401.
* `irq_mode`: 0 to 1, default 0. 0 the sources polled where they live, as
  ESS's drivers do; 1 through Config_Base+6/+7.
* `irq_trace`: 0 to 2, default 0. For debugging: 1 logs the streams' steps
  and each interrupt's sources, 2 also every port write.
* `regs_write`: 0 to 1, default 0. The register hwdep writes (CAP_SYS_RAWIO
  too).
* `opl3` ("OPL3 Switch"): 0 to 1, default 1. The OPL3 hwdep and synth open.
* `esfm` ("ESFM Switch"): 0 to 1, default 1. The ESFM hwdep and synth open.
  It takes effect at the next open.
* `fm_sides` ("FM Sides Switch"): 0 to 1, default 1. OPL3 channel A and the
  ESFM synth's pan left on the left output; 0 the chip's mirror.
* `esfm_delay`: 0 to 32, default 6. Status reads after each ESFM address or
  data write.
* `esfm_bank`: default "es1869/esfm-gm.bin". The GM bank's firmware file; a
  write loads it.
* `esfm_square` ("ESFM Better Square Switch"): 0 to 1, default 0. The bank
  with the better square wave (es1869/esfm-gm-square.bin).
* `esfm_silence_on_close` ("ESFM Silence On Close Switch"): 0 to 1,
  default 1. Every voice off when the synth's last user goes, the pedal's
  too; 0 is ESS's all notes off, though the FM's return to the OPL3 mode
  stops them right after. It takes effect at the next open.
* `esfm_pedal_release` ("ESFM Pedal Release Switch"): 0 to 1, default 1. A
  program change releases the sustain pedal. It takes effect at the next
  open.
* `esfm_vibrato` ("ESFM Vibrato Switch"): 0 to 1, default 1. Vibrato from
  the modulation wheel and channel pressure. It takes effect at the next
  open.
* `esfm_tuning` ("ESFM Tuning Switch"): 0 to 1, default 1. RPN 1 and 2, fine
  and coarse tuning. It takes effect at the next open.
* `esfm_reset_controllers` ("ESFM Reset Controllers Switch"): 0 to 1,
  default 1. Controller 121 as RP-015 has it; 0 ESS's reset. It takes effect
  at the next open.
* `esfm_live_pan` ("ESFM Live Pan Switch"): 0 to 1, default 1. A pan change
  moves sounding notes too. It takes effect at the next open.
* `esfm_sysex` ("ESFM SysEx Switch"): 0 to 1, default 1. The GM, GM2, GS and
  XG resets and the master volume. It takes effect at the next open.

<!-- /settings -->
