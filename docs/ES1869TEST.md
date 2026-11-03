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
  as the Armada's BIOS sets it, the first byte recorded hangs the machine.
  snd-es1869 turns that timing off at load and snd-es18xx doesn't, so the
  suite reads the bridge first and stops with exit code 9 while it is on. A
  run with `--stock` therefore needs snd-es1869 loaded once since the boot,
  and root to read the bridge.
* For the cases that listen to the output, a second sound card that records,
  and a cable from the ES1869's output to its input. The suite finds the
  first other card with a capture device, or takes `--rec2 DEVICE`. A cable
  into a mono microphone input carries the left side, which is all the cases
  need.
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

## The paths

The cases reach the chip four ways:
* **dig.** With Audio 2 slaved to the FM (mixer 71h bit 0), the music DAC
  takes Audio 2's samples, one a frame at the FM's 49716 Hz whatever rate
  the stream has, and PCM device 1 records them exactly, with left and right
  swapped and the FM's idle level of +128 added. Every byte that Audio 2's
  DMA brings is checked, including the ring of words on a 16-bit channel.
* **fmdig.** PCM device 1 records the FM itself, exactly.
* **The record mixer.** Audio 1 records Audio 2's output through record
  source 5 and 69h, with the master muted, for the recording cases.
* **The cable.** The second card hears the left output, for Audio 1's
  playback and the cases that need the analog output.

## The groups

* `cal`: the second card hears the left output, its level and its clock, and
  whether it hears levels linearly: a tone falling 6 dB a step at fixed
  volumes. A microphone input with its own gain control, as on the Armada's
  Play! 3, doesn't, and then the cases that measure levels through the cable
  skip.
* `clock`: the chip's clocks against each other, and the 793.8 kHz one
  against the system's.
* `dac`: Audio 2's image at 22.05 kHz with each of its DAC stages, KNOWN
  with the stages off; and the corner of its switched-capacitor filter at
  11025, 22050 and 32000 Hz (`dac.corner`), whose clock the driver sets from
  the rate (72h): six tones from 0.15 to 0.46 of the rate with the filter in
  use and without, and the -3 dB point of their ratio.
* `a2`: Audio 2 at each standard rate from 8 to 96 kHz (`a2.rate`), heard
  through the record mixer by Audio 1 at 48 kHz with both filters on: the
  pitch on the clock the driver chose, which a wrong divisor or clock puts
  thousands of ppm off, and THD+N below the first image.
* `probe`: the devices, the proc file, the register hwdep, and every control
  and parameter against settings.def.
* `params`: a parameter written at runtime moves its control and back, and
  bad values are refused.
* `dig`: Audio 2's formats, starts, a pause, mmap, dmix and half a minute,
  frame by frame, and `dig.drain.full`, a pattern of one whole buffer whose
  drain begins with the buffer full, followed only by idle frames.
* `fm`: PCM 1's idle level, which output each FM side bit reaches with
  `fm_sides` on and off, and a note's pitch.
* `a1rec`: Audio 1's recordings: formats, rates, sides, pitch, and the
  frames at their start.
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
* `mix`: each volume's steps against its dB scale: Audio 2's through the
  output before the master, 69h, B4h and the music DAC's 6Bh through the
  record mixer, the master through record source 3 (AOUT) with the record
  monitor off, Audio 1's through a linear cable, and a stereo control's
  sides apart. `mix.monitor` turns the record monitor on with a silent
  record source and arecord recording eight times, and the output falls
  silent in each recording, since the monitor puts the record source on AOUT
  in place of the playback mix.
* `dual`: the four-channel device's channels on their DACs and one clock,
  and when each DAC began, from the phase of its own tone, at three rates.
* `latency`: the smallest period of four that each engine plays without an
  xrun.
* `music`: aplay plays a file on Audio 2, checked frame by frame.
* `cpu`: the driver's share of the CPU while each engine plays 44.1 kHz
  16-bit stereo, from its own cycle counts, under 1 % for one engine and 2 %
  for both.
* `midi`: GMCHECK.MID on the ESFM synth, every step on PCM 1; WildMidi on a
  DAC; a note out of the MPU-401.
* `3d`: the 3-D effect measured as ess3d measures it (src/s3dmeas.c), tones
  on Audio 2 recorded from record source 7, with each run's registers set
  through the driver's controls. `3d.quick` checks the boost with ESS's
  setting, the level's dB scale from 63 to 32, the level at 0, the model
  (50h bit 1), the limit (bit 0) at two S/M ratios, 54h-5Ah with the limit
  off, and the path at the end against the start, against the card of
  [SPATIALIZER.md](SPATIALIZER.md). `3d.full` (opt-in) runs ess3d's full
  plan for the comparison with that card. Each writes ess3d's report,
  `3d.quick.txt` or `3d.full.txt`, in the output directory.
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
  again, the pattern's last second in order. Every device of the machine
  goes through it: on the Armada both cases passed, but its CardBus network
  card (an RTL8169) didn't come back until it was taken out and put back.
  The socket finds the card again as it resumes, and the card's PHY driver
  can't be requested then, since the kernel refuses usermode helpers during
  a suspend and resume with EBUSY, which libphy doesn't accept as it does
  ENOENT (the Armada kit's patch 0008 does). So run `pm` at the machine, not
  over its network.
* `load` (opt-in, `--destructive`): five reloads of the driver, the card
  complete after each, a parameter given at load in force and gone at the
  next, every setting put back after udev's alsactl restore, and the engines
  working after the last.
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

The dual device's two DACs began 1 to 4.5 frames apart on the Armada, not
the same at every rate or every start, so no fixed skew puts them in step;
`dual.align` reports that as KNOWN within 6 frames, and the driver keeps
`dual_playback` off by default.

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
