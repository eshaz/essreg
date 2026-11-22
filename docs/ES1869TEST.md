# es1869test

es1869test is the loopback test suite of snd-es1869, the Linux driver in
[LINUX.md](LINUX.md). It plays and records through the chip, measures what
comes back, and reports each case as PASS, FAIL, KNOWN or SKIP. Its sources
are in `linux/test/`, under the GPL version 3 or later.

## What it needs

* snd-es1869 loaded, with the card's id ES1869. The suite refuses to run
  with another driver, unless `--stock` asks for the comparison with
  snd-es18xx: the cases that need snd-es1869's own devices, controls or
  parameters then skip, and Audio 1 plays the calibration's tone, since
  snd-es18xx's Audio 2 is silent on a 16-bit DMA channel.
* No recording that would hang the machine. On an Intel PIIX4 with Type F
  timing on the card's capture channel (ISA bridge registers 76h and 77h),
  as the Armada's BIOS sets it, the first demand burst recorded hangs the
  machine. snd-es1869 records in single transfers under it and says so in
  its proc file, and turns it off with `piix4_type_f=0` and at its removal;
  snd-es18xx does neither. So the suite reads the bridge first and stops
  with exit code 9 while Type F is on, unless the driver's proc file says it
  records in single transfers. A run with `--stock` therefore needs
  snd-es1869 loaded and removed since the boot, and root to read the bridge.
* For the cases that listen to the output, a second sound card that records,
  and a cable from the ES1869's output to its input. The suite finds the
  first other card with a capture device, or takes `--rec2 DEVICE`. A cable
  into a mono microphone input carries the left side, which is all the cases
  need. The card has to be quiet, since the cases find a tone by its rise
  over the noise, 18 dB and more. A Zoom H2n on USB power, whose own buzz
  reads -32 dBFS against a tone at -23, passes `cal.rec2` and `cal.linear`
  and fails nearly all the rest, which says nothing about the driver. A
  stereo plug in a balanced input hears left minus right, and a tone on both
  sides cancels: `--rec2-left` then holds the right output down.
* Root, for the cases that write the module's parameters (`params.*`),
  suspend the machine (`pm`) or reload the driver (`load`).
* aplaymidi and snd-es1869-esfm with its bank, for `midi.esfm`; WildMidi for
  `midi.soft`.
* For `pm`, a kernel with `/sys/power/pm_test` (CONFIG_PM_DEBUG).

MPD must not be playing, and no program may hold the card's PCM devices: the
suite names them and stops. It never stops MPD itself.

## Building

`make -C linux/test` builds `es1869test` with alsa-lib, and with essreg's
`src/s3dmeas.c` and `src/s3dsim.c`, the 3-D measurement of ess3d and its
simulated effect, so it builds inside essreg's tree. The Armada's kit builds
it with `make CFLAGS_ARCH=-march=pentium2`. Nothing in it needs SSE.

`make -C linux dist` packs `es1869test-VERSION.tar.gz` for a package: the
suite, the Linux essreg (`linux/tools/essreg`), and the files of essreg and
the driver that they build, with the licence and these docs. In the unpacked
tree, `make -C linux/test` and `make -C linux/tools/essreg` build them.

## Running

```
es1869test                 every case but the opt-in ones
es1869test -q              the quick cases, about three minutes
es1869test -c dig,fm       the dig and fm groups
es1869test -c a1rec.start.s16 -v
es1869test -x midi         all but the midi group
es1869test -l              the cases, without running them
es1869test --restore       undo what a cut-off run changed
es1869test --self-test     the analysis, without a sound card
es1869test --sim           the cases on the simulated chip
es1869test -c hwvol -i     the volume buttons, someone pressing them
es1869test -c pm,load --destructive
                           a suspend and resume, and five reloads
es1869test --stock         the comparison with snd-es18xx
```

`-c` and `-x` take a comma list of case ids or groups. `-o DIR` puts the
report, the measurements and the recordings of failed cases in DIR (the
current directory by default). `-v` lists every stream of every case.
`--reload CMD` gives `load` the command that loads the driver again, with
the parameters appended, for a driver that modprobe can't find, such as a
test build; by default it is modprobe.

The report, `es1869test.txt`, has each case with its measurements and the
range each must fall in. `es1869test.tsv` has one row a measurement, for a
comparison of two runs. A failed case's recording is a WAV file named after
the case. Both reports are synced after every case, so a run that a crash
cuts short keeps what it found.

`--baseline TSV` compares each measurement with the same case's in an
earlier run's TSV, such as the Armada's in `linux/test/baseline/`. A
measurement that moved further than a repeat run moves it, by its unit (1
dB, 3 cents, 50 ppm or a tenth, 2 frames, a count not at all), gets the
earlier value in the report and `moved` in the TSV, and the report ends with
how many moved. A move doesn't fail the case: the case's own ranges decide
that.

The exit code is 0 when every case passed or was KNOWN, 1 for a bad option,
2 without the driver, 3 when the card or the lock is busy or MPD plays, 4
when root is needed, 5 when the restore failed, 6 when a case failed, 7
after a signal, 8 when the self-test failed and 9 when a recording would
hang the machine.

Each case gets the kernel's new log lines that name the driver, in its
report, and an error among them fails it, as does a warning of a fault
anywhere in the kernel: a WARN, a BUG, an oops, a hung task or an interrupt
that nobody handled. Reading the log needs root.

## The journal

Every control and parameter that a case changes goes to the journal first,
`/var/lib/es1869test/restore` as root, and the end of the run puts it back.
A signal (SIGINT, SIGTERM or SIGHUP) restores and exits with 7. A run cut
short another way, by a crash or a reboot, leaves the journal, and the next
run, or `es1869test --restore`, applies it before anything else.

Each case starts with `a2_resample` at 0, Off, through the journal, so that
Audio 2 plays its samples as written at the chip's own pitch whatever the
driver's default. The rate correction's own cases set it themselves.

## The paths

The cases reach the chip four ways:
* **dig.** With Audio 2 slaved to the FM (mixer 71h bit 0), the music DAC
  takes Audio 2's samples, one a frame at the FM's 49,612.5 Hz whatever rate
  the stream has, and PCM device 1 records them exactly, with left and right
  swapped and the FM's idle level of +128 added. Every byte that Audio 2's
  DMA brings is checked, including the ring of words on a 16-bit channel.
* **fmdig.** PCM device 1 records the FM itself, exactly.
* **The record mixer.** Audio 1 records Audio 2's output through record
  source 5 and 69h, with the master muted, for the recording cases.
* **The cable.** The second card hears the left output, for Audio 1's
  playback and the cases that need the analog output.

PCM device 3, the monitor, reads back what Audio 2's DMA took from the
driver's side, without an engine of its own, and the `mon` group holds it
against the stream, the filter's C code and the dig path.

## The groups

* `cal`: the second card hears the left output, its level and its clock, and
  whether it hears levels linearly: a tone falling 6 dB a step at fixed
  volumes. A microphone input with its own gain control, as on the Armada's
  Play! 3, doesn't, and then the cases that measure levels through the cable
  skip.
* `clock`: the chip's clocks against each other, the FM's within 100 ppm of
  PCM 1's 49,613 Hz in `clock.fm`, and in `clock.system` the driver's line
  through 30 s of a stream's interrupts, the master clock against the raw
  system clock and the clock NTP steers against that.
* `dac`: Audio 2's image at 22.05 kHz with each of its DAC stages, KNOWN
  with the stages off; and the corner of its switched-capacitor filter at
  11025, 22050 and 32000 Hz (`dac.corner`), whose clock the driver sets from
  the rate (72h): six tones from 0.15 to 0.46 of the rate with the filter in
  use and without, and the -3 dB point of their ratio. `dac.fold` plays 16
  kHz at 48 kHz with the DAC's stages off, heard at the same rate, six
  starts with the ADC's filter off and six with it on: off, the DAC's image
  at 32 kHz folds back onto the tone in a phase that each start draws.
* `a2`: Audio 2 at each standard rate from 8 to 96 kHz (`a2.rate`), heard
  through the record mixer by Audio 1 at 48 kHz with both filters on: the
  pitch on the clock the driver chose, which a wrong divisor or clock puts
  thousands of ppm off, and THD+N below the first image.
* `probe`: the devices, the proc file, the register hwdep, and every control
  and parameter against settings.def. `probe.alsamixer` checks that
  alsamixer shows the mixer's controls and Audio 2's output rate, rate
  correction and treble correction, and none of the chip's workings, which
  stay controls on the card interface.
* `params`: a parameter written at runtime moves its control and back, and
  bad values are refused.
* `dig`: Audio 2's formats, starts, a pause, mmap, dmix and half a minute,
  frame by frame, and `dig.drain.full`, a pattern of one whole buffer whose
  drain begins with the buffer full, followed only by idle frames, and
  `dig.drain.two`, the same with two periods in the buffer. `dig.two` plays
  two periods in the buffer, polled as JACK does it, so that nothing but the
  interrupts reads the pointer: a driver that took a step of over half the
  buffer for a step back lost a buffer there, with no xrun. `dig.runout`
  stops writing, with no stop threshold and the core's silence behind the
  DMA: only idle frames after the pattern. `dig.typef` (opt-in, root) sets
  `piix4_type_f=1`, the PIIX4's Type F timing for Audio 1's channel, which
  records the dig path, mutes the master, and checks every frame of a second
  of the pattern: the driver records in single transfers there, since a
  demand burst under Type F hung the Armada. Without a PIIX4 that names the
  channel, it skips.
* `dig.trim`: a tone of 997 Hz through the rate correction with
  `a2_trim_ppb` at 1000 ppm, read back as the DMA took it, one frame for
  one: the pitch has moved by the step within 0.1 ppm, with THD+N under -75
  dB and no glitches. `dig.trim.mono` is the same in mono, `dig.trim.down`
  the other way, `dig.trim.48` at 48 kHz, and `dig.trim.two` with two
  periods in the buffer, polled as JACK does it, which the timer on the
  period boundaries has to wake. `dig.trim.exact`, with nothing to correct,
  gets every frame back as it went in. `dig.clock` plays 48 kHz in the Clock
  mode: the samples go down by the 251.95 ppm of the 768 kHz clock and by
  what the driver measures of the master, within 3 ppm.
* `rs`: the rate correction, in depth.
  * `rs.exact` plays noise and a sweep up the band through the filter, reads
    them back on the dig path, and compares every frame with what the same
    filter's C code makes here. The kernel runs MMX, under real interrupts,
    in chunks that the fills cut as they come, so a frame that differs by 1
    fails it. `.down`, `.mono`, `.30`, `.48` and `.two` are the other steps,
    formats and a client polling two periods.
  * `rs.over.2` and `.3` do the same with `a2_oversample`, with the ring at
    2x from 44.1 kHz and 3x from 22.05 kHz, since the dig path's recording
    takes as much of the bus as the stream. `rs.over.drain` and
    `rs.over.runout` are those two cases at 2x, and `cpu.over.2` and `.3`
    read the driver's share of the CPU there.
  * `rs.band` measures tones up to 20 kHz at 30 ppm, the step that the
    Armada's crystal takes, and `rs.band.48` at 48 kHz's 252 ppm: the level
    within 0.01 dB, THD+N under -85 dB and the worst spur under -90 dB. The
    top of the band is where a short filter shows.
  * `rs.treble` turns on `a2_treble` and reads tones up to 22 kHz with no
    change of rate: each comes up by what the DAC's hold and its share of
    the last sample take from it, within 0.1 dB, with the same THD+N limit.
    `.30` does it at a step of 30 ppm up to 20 kHz, since above that the
    filter's own edge takes the top of the band.
  * `rs.imd.ccif` plays 19 and 20 kHz and `rs.imd.smpte` 60 Hz under 7 kHz,
    and reads what falls between the tones, under -92 dBFS.
  * `rs.quiet` keeps a tone's level at -60 dBFS within 0.1 dB, over the
    noise of 16-bit rounding.
  * `rs.dither` reads what `a2_resample_dither` does at -80 dBFS and 1 ppm,
    where rounding is at its most regular: the noise it adds and the spurs
    it takes.
  * `rs.live` writes the mixer's controls under a playing stream, as a mixer
    program does: Off, Trim up and down, Clock and Off again, each read from
    the tone's pitch, with no xrun.
  * `rs.pause` goes on in step within a FIFO, `rs.xrun` recovers from an
    underrun at the same pitch, `rs.drain` plays every frame, and
    `rs.runout` hears silence, not the ring's old frames, after a client
    stops writing with no stop threshold.
  * `rs.clock.44` and `rs.clock.48` take a line through a minute of readings
    of the stream's position against the clock that NTP steers, off the dig
    path, in the Off mode, the control, and in Clock, within 0.1 ppm.
    `rs.clock.long44` and `rs.clock.long48` (opt-in) take half an hour.
  * `rs.rates` does the same at eight rates on every clock of the chip,
    within 2 ppm.
  * `rs.bench` gives the filter's cycles a frame on this CPU, MMX and C.
  * `rs.analog` plays a tone through the DAC and the ADC with the filter and
    without it, in turn, since the level moves between sessions: the filter
    moves THD+N by under 1.5 dB and the level by under 0.1 dB.
* `fm`: PCM 1's idle level, which output each FM side bit reaches with
  `fm_sides` on and off, and a note's pitch, all with the output muted.
* `a1rec`: Audio 1's recordings: formats, rates, sides, pitch, and the
  frames at their start. `a1rec.dc` measures the ADC's offset from a source
  that nothing drives, which should stay under half a step of the offset
  registers, and what a step of `Capture DC Offset` moves it by. Its first
  recording takes 5 s and counts the last second, since the input settles
  slowly from a loud recording before it. `a1rec.correct` (root) records
  four tones through the record mixer with the correction filter
  (`a1c_filter`) off, then on with known taps, a lift of the top on the left
  and -1 dB on the right. Audio 2 runs at 2x with its treble correction, so
  that no image folds back and moves a tone. Each tone has to change by the
  taps' own response within 0.15 dB. `a1rec.yield` records through the
  filter at periods of 4096 frames, and no piece of its work may hold the
  CPU over 1 ms, the longest piece the driver's /proc page reports. A whole
  period at once took the Armada's Celeron about 5 ms, and the VX Pocket's
  programmed I/O, starved that long, played a gap each period: a recording
  of it came out 6 % long. `a1rec.restart` records a tone eight times, each
  dropped, prepared and started again on its open stream after 0.4 s, at
  once or 2, 10 or 50 ms later, and the tone's share of the recording after
  the restart has to stay within 1 dB of its share before. A recording begun
  again with its channel unmasked before the start could come back a byte
  off, as noise, the tone's share 30 dB down: es1869scope -C's second run
  did now and then.
* `a1play`: Audio 1's playback through the cable: formats, rates and twenty
  starts, measured late in each tone, with THD+N over 600 Hz: the second
  card's clock wanders while it records, which leaves sidebands within 20 Hz
  of the tone and read as -2 to -12 dB, while a stream a byte off is
  broadband. A run is repeated up to twice while it misses its bar.
* `both` (with the second card): Audio 2 plays a tone for 14 s while aplay
  or arecord starts and stops Audio 1 eight times, and no 50 ms of Audio 2
  on the output falls more than 6 dB under its median, so that no start or
  stop of Audio 1 silences Audio 2, with the record monitor off. The dips
  over 3 dB go in the report with their times.
* `buf`: Audio 2's delay, its FIFO's term exact and its timing within the
  trigger timestamps' few milliseconds, a drain, and the xruns of a stalled
  reader and writer.
* `dac.over`: that Audio 2 Output Rate clocks the DAC up, which the `dig`
  path can't show, since it runs on the FM's clock. Tones at 22050 Hz on
  793.8 kHz's divisors, and at 24000 Hz on 768 kHz's, go to the second card
  at each setting: 70h has to hold the divisor, a tone's image has to leave
  the second card's band, and the droop at 9 kHz has to shrink by what the
  DAC's response at the new rate says. `dac.over.live` moves the rate under
  a playing stream with MPD's buffer: 15 kHz has to step with it, in tune,
  with no xrun, each reading the middle of four pieces, since with the
  second card capturing the DAC can stop about 5 s into a take.
  `dac.over.quality` takes THD+N at 997 Hz and 10 kHz, as the median of four
  pieces of a take, and the quietest of three takes of a silent stream, at
  each rate, each within 1 dB of the same chain at 1x, since the second
  card's own distortion sets the numbers. A stall of the DAC spoils one
  piece, and those are counted apart, since they are the bus's and not the
  rate filter's.
* `mix`: each volume's steps against its dB scale: Audio 2's through the
  output before the master, 69h, B4h and the music DAC's 6Bh through the
  record mixer, the master through record source 3 (AOUT) with the record
  monitor off, Audio 1's through a linear cable, and a stereo control's
  sides apart. `mix.monitor` turns the record monitor on with a silent
  record source and arecord recording eight times, and the output falls
  silent in each recording, since the monitor puts the record source on AOUT
  in place of the playback mix. `mix.scale` reads every level's dB range at
  all eight record sources: none may move, since alsactl's restore converts
  a level whose range differs from the saved one. `mix.adc` sets each source
  through the ADC Input list and through Capture Source, and reads it back
  through the other and from 1Ch.
* `a1play.drain`: a second of tone at 8 kHz in 8 bits, whose last frame ends
  a period, drained: it lasts the second, within the second card's clock,
  where a stop at the DMA's end cuts the 32 ms that the chip's FIFO holds.
  `a1play.pausedrain` pauses for 200 ms before that drain, and the buffer
  that was queued still plays. `a1play.click` reads the largest sample just
  after a pause's release, against the tone's peak, six times.
* `dual`: the four-channel device's channels on their DACs and one clock,
  and when each DAC began, from the phase of its own tone, at three rates.
  `dual.a1c` plays the device's tones before and after a recording through
  Audio 1's correction filter, and each has to come back within 0.1 dB. A
  driver that left the filter on took the dual stream through it, four
  channels in its two rows, and wrote over its own state; the Armada froze
  two cases later on 2026-11-09.
* `mon`: the monitor, PCM device 3
  ([LINUX_MONITOR.md](LINUX_MONITOR.md#tests)). A pattern played on Audio 2
  comes back exactly, in 16-bit stereo and in 8-bit mono, with timestamps at
  the ring's rate; through the rate correction, and with the ring at 2x,
  every frame is the filter's C code's; in the Clock mode the monitor and
  the dig path's PCM 1 agree frame for frame; a pause is one xrun with the
  frames in order on both sides; and beside it Audio 1 records the analog
  loop, on the same clock to a frame, with the delay between the two by
  their timestamps.
* `latency`: the smallest period of four that each engine plays without an
  xrun.
* `music`: aplay plays a file on Audio 2, checked frame by frame.
* `cpu`: the driver's share of the CPU while each engine plays 44.1 kHz
  16-bit stereo, from its own cycle counts of the interrupts and of the
  ring's ticks and copies, under 1 % for one engine and 2 % for both.
  `cpu.rs` turns the filter off and on every 4 s under one stream, with a
  watch running: what a thread of idle priority counts, which time in the
  idle loop can't hide from, gives the filter's part of the whole machine,
  and a real-time timer every millisecond how late the machine can be. With
  them the longest a fill held interrupts off, under 350 us, and the
  driver's own cycles in each mode, at 44.1 and 48 kHz, in mono and at 96
  kHz.
* `midi`: GMCHECK.MID on the ESFM synth, every step on PCM 1, with ESS's
  bank and the engine's changes on, whatever the machine keeps (the better
  square wave's program 81, which the file plays, has a vibrato of its own);
  WildMidi on a DAC; a note out of the MPU-401. `midi.mpu.loop` sends 240
  bytes and reads them back through a cable from MIDI out to MIDI in, and
  skips with no cable.
* `3d`: the 3-D effect measured as ess3d measures it (src/s3dmeas.c), tones
  on Audio 2 recorded from record source 7, with each run's registers set
  through the driver's controls. `3d.quick` checks the boost with ESS's
  setting, the level's dB scale from 63 to 32, the level at 0, the model
  (50h bit 1), the limit (bit 0) at two S/M ratios, 54h-5Ah with the limit
  off, and the path at the end against the start, against the card of
  [SPATIALIZER.md](SPATIALIZER.md). `3d.full` (opt-in) runs ess3d's full
  plan for the comparison with that card. Each writes ess3d's report,
  `3d.quick.txt` or `3d.full.txt`, in the output directory.
* `beep`: a console tone at the PC speaker's levels 0, 4 and 7, heard by the
  second card: KNOWN where it doesn't hear that pin, which the board mixes
  in after the chip. It needs root, and the pcspkr module. `beep.follow`
  moves the master and reads 3Ch, which has to follow it and go to 0 when
  both sides are muted, while the level stays what was set, also after
  reading it, which goes through the chip.
* `gpo`: a tone through the output at each state of GPO0 and GPO1, heard by
  the second card: KNOWN where neither pin changes the line output.
* `hwvol` (with `-i`): the volume buttons, each pressed when asked, move the
  master and its control by a step, Mute turns it off and on, and with the
  split the counters move instead of the master. The case keeps the driver's
  `hwvol_mode`: on the Armada, which has only the two buttons, it passes
  with `hwvol_mode=1` (2-wire), Mute being both pressed together.
* `pm` (opt-in, `--destructive`): the machine's devices suspended and
  resumed through pm_test, every mixer register and B4h the same after it,
  and every engine working; a stream through a suspend goes on once prepared
  again, the pattern's last second in order; and a tone on Audio 1 is heard
  after the resume at its level before it (`pm.a1`, through the second
  card). Every device of the machine goes through it: on the Armada both
  cases passed, but its CardBus network card (an RTL8169) didn't come back
  until it was taken out and put back. The socket finds the card again as it
  resumes, and the card's PHY driver can't be requested then, since the
  kernel refuses usermode helpers during a suspend and resume with EBUSY,
  which libphy doesn't accept as it does ENOENT (the Armada kit's patch 0008
  does). So run `pm` at the machine, not over its network. `pm.fm` plays a
  note through the OPL3 and through the native ESFM before and after a
  suspend. `pm.real` (with `-i`) is a real suspend to RAM: someone presses
  the suspend button while a stream plays and wakes the machine again, and
  the stream goes on and every engine works.
* `pwr`: `pwr_save` puts the idle chip down, partly (`pwr.partial`) or fully
  (`pwr.full`), as the proc file says, and Audio 2's next stream wakes it,
  every frame exact. `pwr.self` does the same after the chip's own
  Self-Timed Power Down.
* `load` (opt-in, `--destructive`): five reloads of the driver, the card
  complete after each, a parameter given at load in force and gone at the
  next, every setting put back after udev's alsactl restore, and the engines
  working after the last. `load.fm` loads with `opl3=0` and with `esfm=0`,
  each of which takes one way to the FM away and leaves the other.
* `stress` (opt-in): 200 open and close cycles over the devices, and a
  minute of the dig path, exact, while the suite's helper reads every
  control, its dB scale, the proc file and the mixer's registers.
* `rate` (opt-in, `-c rate`): each engine's highest clean rate with the
  driver's limits lifted, for the Kconfig defaults.

`a1play.start` plays twenty starts and takes two minutes. The `midi` cases
write GMCHECK.MID, the same file as `tools/gmcheck.py`, into the output
directory.

## KNOWN results

Audio 2's interrupt comes when its DMA has fetched a period's last byte, up
to a FIFO of 64 bytes before that byte plays, and a stop clears the FIFO. So
the end of a drain loses the frames still in the FIFO, and the release of a
pause, which starts the engine afresh, can only estimate where to go on, and
may play a few frames again. The dig cases report both as KNOWN while they
stay within a FIFO (16 frames of 16-bit stereo), with the counts in the
report, and as a FAIL beyond it. On the Armada a drain lost 1 to 15 frames,
and a release played 4 frames again.

Under dmix, the ring of words on a 16-bit channel copies the window ahead of
the DMA every quarter of `a2_lead_ms`, 10 ms by default, and dmix mixes a
stream in at the DMA's position, so a stream can lose up to that much of its
start. `dig.dmix` reports it as KNOWN within that bound.

Without the driver's settle (`a1c_settle_ms=0`), a recording starts with the
ADC's input offset decaying, as with ESS's drivers; `a1rec.settle0` measures
it and reports it as KNOWN.

A release of Audio 1's pause clicks: the largest sample just after it reads
up to about 3 times the tone's peak through the second card. `a1play.click`
reports that as KNOWN.

The dual device's two DACs began 1 to 4.5 frames apart on the Armada at 44.1
kHz and from -15 to +15 at 22050 Hz, on the installed driver and a test
build alike, not the same at every rate or every start, so no fixed skew
puts them in step; `dual.align` reports that as KNOWN within 20 frames, and
the driver keeps `dual_playback` off by default.

## The simulated chip

`--sim` runs the cases on a model of the chip with the cable and a second
card, in virtual time, and `--sim=FAULTS` adds faults (the `SIM_*` values in
`linux/test/hal.h`): a byte-late Audio 1, a silent Audio 2, a missing FIFO
in the delay, the data sheet's clocks, the FM's sides ignored, a resume that
loses a setting, a reload that ignores its parameters, a master control that
misses the buttons, an error in the kernel's log and others. Its record
source 7 goes through src/s3dsim.c's 3-D effect. Every case passes on the
healthy model, and each fault fails the cases that look for it.
`tests/host/t_etan.c` checks the analysis on known signals.
