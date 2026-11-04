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

From 153.6 kHz on, DMA 5 delivers no more than about 572,000 transfers a
second, and the chip then plays slower instead of skipping: at 198.45 kHz a
441 Hz tone came out at 318 Hz. The rates of the 768 kHz clock come out 252
ppm above those of the 793.8 kHz clock.

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

The system clock isn't disciplined: cron sets it from the network every six
hours and nothing corrects its rate. Two offsets from NTP 1404 s apart put
the Armada's crystal about 30 ppm slow of real time, as chrony's drift file
has it, so that 44.1 kHz plays about 29 ppm flat of real time and 48 kHz
about 223 ppm sharp.

## Rate correction

The chip has one clock input and no trim, so that a stream's rate can only
be changed in its samples. `a2_resample`, the control Audio 2 Rate
Correction, puts Audio 2's stream through a filter on its way to the ring:
* `0`, Off, the default, leaves the samples as the application wrote them.
* `1`, Trim, moves the rate by `a2_trim_ppb`. At 0 the samples stay exact.
* `2`, Clock, plays the stream true to the system's clock, plus
  `a2_trim_ppb`.

It serves S16_LE streams of Audio 2 on a 16-bit DMA channel, which is the
Armada's, with a buffer of at least 256 frames. Any other stream, Audio 1,
the FM and the dual device play as they always do, and so does a pointer
that stands, as under dmix, whose data changes in place. The samples are
then no longer the application's, a choice between the true rate and the
exact samples, so it stays off by default.

The filter is a Kaiser-windowed sinc of 36 taps, blended from 128 places at
each frame, in MMX where the CPU has it. On a 16-bit tone at -1 dBFS and an
offset of 252 ppm its noise and distortion are -86 to -90 dB up to 18 kHz,
and its worst spur is -92 dB. It gives way above that: -56 dB at 19 kHz.
Through the chip's digital path, es1869test reads -88.5 dB from the kernel's
filter. Through the analog path, heard by Audio 1, the output is the same
with and without it, within 0.1 dB: -61.5, -58.7 and -55.8 dB at 1, 10 and
16 kHz, the chip's own floor. It takes about 2.8 % of the Armada's CPU at
44.1 kHz stereo.

A stream's period no longer matches the ring's, so a timer wakes the client
on the stream's own period boundaries, and the chip's interrupts stay as the
net under it. The ring holds as many frames as the buffer, and a step down
makes more ring frames than stream frames, so a fill stops short of a whole
lap.

`a2_resample_dither` rounds to 16 bits with triangular noise. In simulation
it adds 3 dB of noise everywhere, and lowers spurs by up to 13 dB only for a
quiet tone at about 1 ppm, where they lie near -100 dBFS, some 40 dB under
the chip's floor, so it stays off. Dithering the filter's place instead
trades 20 dB of spurs for 5 dB of noise with a nearest-row filter, and
blending the rows beats it on both, so the driver has none.

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

The two are measured apart and nothing feeds back from the filter into them,
so the correction can't hunt. A new stream starts from what the last one
measured.

A correction of the rate is not one of jitter. Jitter is in the DAC's clock
at the moment of each conversion, after all of this, and no change to the
samples reaches it. The filter's own place between two frames is exact to 22
bits, so it adds none. Around a tone heard by Audio 1, the Armada shows 5 ns
rms of timing noise at 10 kHz and 14 ns at 16 kHz, within 2 kHz of it, which
is an upper bound: it holds the path's noise, and the two engines share a
clock.

The rate register itself can be changed under a running stream. A write to
70h takes effect at the next sample, in steps of 13 or 18 of the master's
ticks, 0.55 or 0.76 us, and a burst shorter than a sample works on average.
It isn't used: at best its steps leave about 160 ns rms, some 30 times the
chip's own figure, against the filter's none. Pulses from user space
measured 4 to 5 us. The filter clock (72h) moves a low-pass corner and has
no part in the timing.

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
05 and 09). "Beep Playback Volume" reads 3Ch from the chip, so it follows
the popup, and a resume puts back what it had. At load the driver leaves 3Ch
as the BIOS left it, unless a parameter sets it: the chip's reset value is
4, and on the Armada CMOS 47h is 0, as is the saved state, so the PC speaker
is silent there until something raises it.

The PC speaker comes out on the chip's own pin (PCSPKO), and the board mixes
it in after the chip, so the master, its mute and the volume buttons don't
reach it. ESS's ES1869.DRV scales 3Ch by the master in software and zeroes
it while the master is muted; this driver keeps the two apart, as snd-es18xx
does.

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

## Tools

`linux/tools/essreg` builds essreg, the register utility, from its own
sources in `src/`, with its register access through the driver's register
hwdep instead of the ports: `make -C linux/tools/essreg`, then `essreg a`
for every value or `essreg r=regs.txt` for a dump. Reads work as root;
writes also need the module's `regs_write=1`.

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
  master. Without a parameter at load, the chip keeps its level.
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
* `filter_corner`: 100 to 1000, default 875. The filters' corner below 44.1
  kHz, in thousandths of half the rate; ESS's 875. It takes effect at the
  next prepare.
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
* `pwr_save`: 0 to 3600, default 0. Seconds of idleness before a power-down,
  0 never.
* `pwr_full`: 0 to 1, default 0. Power down fully (the inputs go silent)
  instead of partly.
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
* `a1c_settle_ms`: 0 to 1000, default 200. How long a recording waits for
  the ADC's input to settle after the codec turns to it (ms); 0 starts at
  once, as ESS's drivers do, and the first 100 ms then carry the input's
  offset decaying.
* `a2_lead_ms`: 4 to 500, default 40. How far ahead Audio 2's ring is filled
  on DMA 5 (ms).
* `a2_tick_us`: 500 to 20000, default 2000. The ring's refill period on DMA
  5 while a copy is behind (us); under dmix a quarter of a2_lead_ms, if that
  is longer.
* `a2_pause`: 0 to 1, default 0. Pause on an 8-bit Audio 2 channel through
  78h, which may resume off step. It takes effect at the next prepare.
* `a2_resample` ("Audio 2 Rate Correction"): 0 Off, 1 Trim, 2 Clock, default
  Off. Audio 2's 16-bit streams on DMA 5 through a filter that corrects
  their rate: 1 by a2_trim_ppb, 2 also to the system's clock, as the chip's
  measures against it; 0 leaves the samples as they are. It takes effect at
  the next prepare.
* `a2_trim_ppb`: -2000000 to 2000000, default 0. What the rate correction
  adds to the stream's rate (ppb). It takes effect at the next prepare.
* `a2_resample_dither`: 0 to 1, default 0. The rate correction's rounding to
  16 bits with triangular noise of 1 LSB: 3 dB more noise for spurs at about
  -100 dBFS. It takes effect at the next prepare.
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
* `rate_limit`: 0 to 1, default 1. 0 lifts the rate limits above, for
  es1869test's rate.max. It takes effect at the next prepare.
* `fmdig_device`: 0 to 1, default 1. PCM device 1, the music DAC recorded
  digitally, opens.
* `dual_playback`: 0 to 1, default 0. PCM device 2, both DACs as four
  channels, opens.
* `irq_sources`: 0 to 15, default 15. The sources the line carries: 1 Audio
  1, 2 Audio 2, 4 the buttons, 8 the MPU-401.
* `irq_mode`: 0 to 1, default 0. 0 the sources polled where they live, as
  ESS's drivers do; 1 through Config_Base+6/+7.
* `irq_trace`: 0 to 2, default 0. For debugging: 1 logs the streams' steps
  and each interrupt's sources, 2 also every port write.
* `regs_write`: 0 to 1, default 0. The register hwdep writes (CAP_SYS_RAWIO
  too).
* `opl3`: 0 to 1, default 1. The OPL3 hwdep and synth open.
* `esfm`: 0 to 1, default 1. The ESFM hwdep and synth open.
* `fm_sides`: 0 to 1, default 1. OPL3 channel A and the ESFM synth's pan
  left on the left output; 0 the chip's mirror.
* `esfm_delay`: 0 to 32, default 6. Status reads after each ESFM address or
  data write.
* `esfm_bank`: default "es1869/esfm-gm.bin". The GM bank's firmware file; a
  write loads it.
* `esfm_square`: 0 to 1, default 0. The bank with the better square wave
  (es1869/esfm-gm-square.bin).
* `esfm_silence_on_close`: 0 to 1, default 1. Every voice off when the
  synth's last user goes, the pedal's too; 0 is ESS's all notes off, though
  the FM's return to the OPL3 mode stops them right after.
* `esfm_pedal_release`: 0 to 1, default 1. A program change releases the
  sustain pedal.
* `esfm_vibrato`: 0 to 1, default 1. Vibrato from the modulation wheel and
  channel pressure.
* `esfm_tuning`: 0 to 1, default 1. RPN 1 and 2, fine and coarse tuning.
* `esfm_reset_controllers`: 0 to 1, default 1. Controller 121 as RP-015 has
  it; 0 ESS's reset.
* `esfm_live_pan`: 0 to 1, default 1. A pan change moves sounding notes too.
* `esfm_sysex`: 0 to 1, default 1. The GM, GM2, GS and XG resets and the
  master volume.

<!-- /settings -->
