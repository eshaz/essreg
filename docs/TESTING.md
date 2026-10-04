# Testing

## Automated tests

```
python3 tests/run_tests.py
```

The tests need Python 3, gcc and NASM, and the CPU emulator tests also need
the `unicorn` Python module. On Claude Code on the web,
[`.claude/hooks/session-start.sh`](../.claude/hooks/session-start.sh)
installs all of this, including Open Watcom and Wine, and sets `OW2` and
`ESSREG_WINE=1`.

### Python tests

The Python tests, in `tests/test_*.py`, cover the following:
* The LE and NE tooling is tested, and so are the stock rebuilds of
  `ES1869.VXD` and `ESFM.DRV`, which must be byte-identical to ESS's
  drivers.
* `test_esfmdrv` runs `ESFM.DRV` in a CPU emulator, with a model of the FM
  chip and simulated interrupts. It reproduces the hanging notes of ESS's
  driver and tests the fixed driver against them
  ([ESFM_MIDI.md](ESFM_MIDI.md)).
* The fixed driver's bank file is tested in the same emulator, which
  simulates `SYSTEM.INI`, files with their dates, and the global heap.
* `test_vxdext` runs the register API of the rebuilt VxD in a CPU emulator
  against a simulated ES1869, including the API's refusals and checks. It
  also checks that the extended VxD keeps ESS's code where it was, byte for
  byte apart from the hooks, and that Windows' own sound makes the same port
  accesses as with ESS's driver, apart from the Audio 2 mode. For that, the
  test runs ES1869.DRV's calls around a wave device, a mixer change, and
  DirectSound taking the DSP.
* `test_es1869drv` rebuilds `ES1869.DRV` from `src/es1869`. The stock build
  must be ESS's driver byte for byte, and the changed build must keep ESS's
  code at its addresses, apart from the instructions the test lists. For
  both builds, the code that writes the Audio 2 mode runs in the CPU
  emulator inside ESS's code.
* `test_dualwav` checks what each mode of `tools/dualwav.py` puts on the
  Audio 2 DAC next to the Audio 1 DAC's part, and the tool's delays, gains
  and input formats.
* `test_a1play` runs the Audio 1 player of `ES1869.DRV` in the CPU emulator
  with a model of the DSP, the mixer and the DMA controller
  (`tests/drvemu.py`), and sends the player's interrupts through ESS's own
  handler. It checks that the DMA takes what the program wrote and that
  headers come back once they have played. It also covers underruns, loops,
  pause, reset and close, who gets Audio 1, device 0 falling back to it,
  dual playback on both DACs in step (with Audio 2 on an 8-bit or a 16-bit
  channel), the resume and the last disable, and it checks that with the
  player's keys at 0, the driver gives ESS's answers.
* `test_vxddos` runs DOS boxes and the rebuilt VxD in the same emulator,
  with VMs, per-VM port trapping and a model of the FM chip that follows
  ESFMu. It covers FM detection no matter who has FM, the virtual FM chip
  and its hand-over, the music DAC, Windows' mixer around a DOS game, and
  the reset when Windows uses the card again. Where ESS's driver behaves
  differently, the test runs the same steps on it too.
* The `SYSTEM.INI` settings of all three drivers are read with emulated
  profile calls. The tests check each key and its default, that each key is
  read once, and that each change, when it is turned off, does what ESS's
  driver does. With every key at 0, the VxD (`test_vxdini`) and `ESFM.DRV`
  (`SettingsTest` in `test_esfmdrv`) make the same port accesses as ESS's
  drivers, step by step.
* Other tests run `esfmpat` on copies of `ESFM.DRV` and check that the
  generated documentation is current.

### C tests

The C tests, `tests/host/t_*.c`, are built with gcc against a simulated
ES1869 (`src/simhw.c`). They test:
* the port protocols (`t_esshw`)
* the VxD API wrappers (`t_vxdapi`)
* the register catalog (`t_esscat`)
* profiles (`t_profile`)
* ess3d's command line and 3-D register changes (`t_ess3d`)
* essreg's register functions that the original didn't have, such as the 3-D
  limit (`t_regs`)
* patch banks (`t_esfm`)
* esfmrec's WAV header and its repair, the names of a long recording's
  files, the levels and the test tone (`t_fmrec`)
* what essctl reads from the data segment of ESS's `ES1869.DRV` and of the
  rebuilt one (`t_wavestat`)
* a port trace, which proves that the refactored essreg talks to the chip
  exactly as the original did (`t_trace`)

### With Open Watcom v2

When `OW2=/path/to/open-watcom` points to Open Watcom v2, the tests also run
essctl's 16-bit VxD call thunk in a CPU emulator. They build every program
and check the NE headers of `essctl.exe`, `ess3d.exe` and `esfmrec.exe` for
Windows 4.0, one data segment, discardable code, and the imports, exports
and resources (`test_ow2build`).

To compile everything with Open Watcom on Linux, run:

```
tools/ow2build.sh /path/to/open-watcom     # binaries in out/ow2/
```

### Under Wine

With `ESSREG_WINE=1`, `tests/test_wine.py` runs `essctl.exe`, `ess3d.exe`
and `esfmrec.exe` as 16-bit Windows programs under Wine. It needs 32-bit
Wine and Xvfb, and covers these cases:
* essctl saves and loads a profile against the simulated chip. A save keeps
  the file's other sections, and a save onto a full disk, which the test
  simulates with a file size limit, leaves the old file as it was.
* `essctl /i2s=off` sets `ESSWaveTableChip` in the ES1869's registry key and
  clears mixer 7Fh bit 0, and `/i2s=on` sets ESS's default again.
* essctl loads a bank live into the real `ESFM.DRV`.
* `essctl /dump` writes the ESFM voice table, with ESS's driver and with the
  fixed one.
* `essctl /dump` writes the wave driver lines with no `ES1869.DRV` loaded,
  with ESS's and with the rebuilt one. `ES1869.DRV` doesn't load without VDS
  (INT 4Bh), which Wine doesn't have, so the test's `drvhold.exe` answers
  the version call while the driver loads.
* The fixed `ESFM.DRV` loads the bank file named in `SYSTEM.INI` when the
  device is opened, and `essctl /load` writes the bank's name there.
* The fixed `ESFM.DRV` reads its `[ESFM.DRV]` switches from `SYSTEM.INI`,
  and with `BetterSquareWave=0` it plays ESS's bank.
* ess3d runs its commands against the simulated chip, and the test also
  tries a bad command and a run without a card. It reads the results from
  ess3d's `/log=` file, because Wine drops the exit code of a 16-bit Windows
  program, whose process always exits with 0.
* A second ess3d hands its setting to the box of the first one and exits.
* A second `ess3d tray` opens the panel of the first one, and `ess3d exit`
  closes the tray icon. Wine shows no tray icon for a 16-bit program,
  because its 16-bit and 32-bit icon handles differ. Windows 9x has only one
  kind, so the icon shows only there.
* esfmrec records the test tone, also with `/raw` and `/split=`.
* esfmrec is ended by force with Wine's `taskkill /f`. The header it saved
  every 5 s holds the samples, and its next start repairs the header.
* esfmrec records onto a full disk, which the test simulates with a file
  size limit. It stops, and the file stays playable.

### Screenshots

`tools/wineshot.sh` runs essctl under Wine on a virtual screen, so that you
can click through it and see the pages:

```
tools/wineshot.sh start out/ow2 essctl.exe /sim
tools/wineshot.sh click 60 216          # ESFM patch bank
tools/wineshot.sh shot esfm.png
tools/wineshot.sh stop
```

It also shows ess3d's box, which a long `/t=` keeps on the screen:

```
tools/wineshot.sh start out/ow2 ess3d.exe /sim /t=20000 on level 40
tools/wineshot.sh shot ess3d.png
tools/wineshot.sh stop
```

For the tray's panel, `run` starts a second program on the same desktop,
here the `ess3d tray` that opens the panel of the first one:

```
tools/wineshot.sh start out/ow2 ess3d.exe /sim tray
tools/wineshot.sh run ess3d.exe /sim tray
tools/wineshot.sh shot panel.png
tools/wineshot.sh stop
```

## On the hardware

None of the tests above touches a real card. Before you start, keep copies
of `C:\WINDOWS\SYSTEM\ES1869.VXD`, `ES1869.DRV` and `ESFM.DRV`. Run the
sections in this order, except E4, which needs the driver that G installs,
and stop at the first surprise. Each step says what you should see.

### A. Reading, with the stock driver

1. Start `essctl`. The title bar says "Direct I/O at 220h (stock ES1869.VXD
   4.04)", and the *Device information* page shows the same resources as
   Device Manager (I/O, IRQ and DMA) and "Mixer 40h ID: 18h 69h ...
   (ES1869)".
2. Walk through the pages. Each one shows its values, and no "sound device
   in use" box appears. Then play a WAV file in Sound Recorder while essctl
   is open. It plays normally.
3. In a DOS box, run `essreg r=before.txt`, and compare the file with the
   one that `essctl /dump dump.txt` writes. The mixer values must agree.

### B. Changing settings

1. On the *3-D, mic, MONO, I2S* page, switch *3-D effect* on and move *3-D
   level*. Music playing in Windows changes audibly.
   * Click the slider's arrows. The level goes up or down by one, and the
     text field follows.
   * Type 20 in the text field and press Enter, and the slider moves to 20.
     Then type 99 and press Enter. You hear a beep, and the text field goes
     back to 20.
2. Turn on the microphone monitor in the Windows mixer, then speak into the
   microphone and toggle *Mic +26 dB preamp*. The microphone is much louder
   with the preamp on.
3. Open the *DACs and ADC* page. It has three sections, each under a
   heading: Audio 1, the ADC, and Audio 2. Every value shows as soon as the
   page opens, the controller registers too, and there is no button to read
   them.
   * Set *ADC offset L* to +3. Then start and stop a WAV playback, press F5,
     and check whether the value survives. According to the data sheet, BAh
     and BBh survive a DSP reset.
4. Test the profiles.
   1. Choose File > *Save profile* and save to `C:\ESS\MY.INI`.
   2. Change a few values, then choose File > *Load profile*. The values
      come back.
   3. Put `essctl /load C:\ESS\MY.INI` in the StartUp group and restart
      Windows. The settings are back, and `ESSCTL.LOG` next to essctl.exe
      says "... settings applied".
   4. With ESS's VxD, start a DOS game with sound in a window, then save
      over `MY.INI` with File > *Save profile*. essctl says that the device
      is in use, and `MY.INI` is unchanged, down to its date.
5. Test who gets the music DAC.
   1. Play a MIDI file in Media Player and stop it. The *3-D, mic, MONO,
      I2S* page shows *I2S drives music DAC* on, because ESS's driver gives
      the DAC back to I2S. *Device information* says "I2S has it while no
      MIDI program is open".
   2. Choose Options > *FM keeps the music DAC*. The check box on the page
      goes off, and *Device information* says "FM keeps it".
   3. Restart Windows, then play and stop a MIDI file again. *I2S drives
      music DAC* stays off.
   4. Start a DOS game that plays FM music in a window. The music plays, and
      it follows *Music DAC (FM) volume* on the *Output mixer* page.
   5. To go back, run `essctl /i2s=on` and restart Windows.

### C. DOS box contention

1. Start a DOS game that uses the card in a window, for example Doom's setup
   with its sound test. While it plays, press F5 in essctl. The rows say "in
   use by DOS", no Windows "device in use" message appears, and the game's
   sound doesn't glitch.
2. Quit the game and press F5. The values come back.
3. Start the game again after using essctl. It still gets sound, because
   essctl gives the DSP back whenever it has taken it.

### D. essreg (DOS)

1. In real DOS, not in Windows, run `essreg a` and `essreg 3=40 m=1`. The
   results are the same as with the original essreg.
   * `essreg 3=99` says "use 0 to 63" and changes nothing.
   * In a Windows DOS box while a WAV file plays, essreg says that the
     ES1869 doesn't answer.
2. Run `essreg x=1 a1s`, which uses the safe DSP protocol (C6h, polling
   Audio_Base+Ch). Check that the Audio 1 sample rate it reports matches the
   one essreg reports in its default mode, without `x=1`.

### E0. Installing with essinst

See the [README](../README.md#essinstexe). Start with ESS's drivers.

1. Copy `build\essinst.exe`, `build\ES1869.VXD`, `build\ES1869.DRV` and
   `build\ESFM.DRV` to one folder, such as `C:\ESSREG`, and run
   `essinst.exe`. It lists each driver as "ESS's driver, kept as" its `.ORG`
   file. Press OK.
2. Windows restarts. Afterwards, `C:\WINDOWS\SYSTEM` holds `ES1869VX.ORG`,
   `ES1869.ORG` and `ESFM.ORG`, and essctl's *Device information* page names
   the rebuilt drivers. `C:\WINDOWS\ESSINST.LOG` lists each step.
3. Run essinst again. It says that the rebuilt drivers are installed
   already.
4. Run `essinst /restore` and press OK. After the restart, essctl names
   ESS's drivers again, as the next sections expect.
5. Note anything that differs, such as a message during the restart.

To install one driver alone in the next sections, put `essinst.exe` and that
driver in a folder of their own.

### E. The extended driver

1. Install `build\ES1869.VXD` with essinst, or by hand as described in
   [VXD_INTERNALS.md](VXD_INTERNALS.md#installing-the-extended-driver), and
   restart Windows.
2. Try Windows sounds, MIDI, a DirectSound game and a DOS game in a window.
   They all work as before.
3. Start essctl. Its title bar says "VxD register API 1.13", and the owners
   line at the bottom right shows who owns the DSP, FM and the MPU.
4. While a DOS game plays, change *Audio 2 volume* or *Master volume* in
   essctl. The change applies at once, and the game keeps its sound. This is
   what the register API is for.
5. If anything misbehaves, put the original driver back, and note what went
   wrong.
6. Test long playback. With essctl, ess3d's tray icon and all DOS boxes
   closed, play a stream or a long MP3 in Winamp for 10 minutes, once with
   the DirectSound output and once with waveOut, and listen for skips.
   * If it skips, put ESS's driver back with `essinst /restore`, restart
     Windows and play the same stream. Skips with both drivers come from the
     card's DMA, as described in
     [AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#finding-out-on-the-card). Skips
     that happen only with the extended driver come from this repository, so
     note which output skipped and how often.

### E2. DOS boxes with the extended driver

These steps use an FM-only DOS game or player, one with AdLib music and no
Sound Blaster sound, and a second game that uses the Sound Blaster too.

1. While nothing else has FM, run the FM game in a DOS box without
   `1869opl3`. It detects the AdLib or OPL3, and its music plays, because
   the driver gave it the music DAC and an FM volume. When you quit it,
   essctl's *3-D, mic, MONO, I2S* page shows the music DAC back as it was
   before.
2. Now let Windows' MIDI have FM. Open a MIDI file in Media Player and pause
   it, then start the FM game.
   * The game detects the FM synthesizer and runs, silently.
   * Close Media Player. Within a moment the game's music starts, with its
     own instruments.
3. Start the FM game in two DOS boxes. The second one detects FM and runs
   silently. When you quit the first one, the second one's music starts.
4. Check Windows' mixer after a DOS game. In the Windows volume control and
   in essctl, note the 3-D effect, the record source (Recording) and the
   wave volume. Play the Sound Blaster game, then quit it. Most such games
   reset the mixer, but afterwards everything is as it was. With ESS's
   driver, the 3-D effect, the record source and the wave volume stay as the
   game left them.
5. To test the FM reset, kill the FM game in the middle of a note, by
   closing the DOS box window or with Ctrl+Alt+Del. If a note keeps
   sounding, move a slider in the tray's volume control or play any Windows
   sound, and the note stops.
6. Play a game that runs other programs, such as a menu that starts the game
   or a cutscene player. Its music keeps its instruments after the other
   program ends.
7. Start a recording in esfmrec and play the FM game. The recording has the
   game's music, because esfmrec keeps 7Fh bit 4 on, and the driver changes
   only 7Fh bit 0 and 36h. With the Sound Blaster game, start esfmrec while
   the game runs, as in [section I](#i-esfmrec).
8. Test the FM's tempo. Play the FM game in a window, with music that has a
   steady beat, and make Windows busy meanwhile: record with esfmrec, copy a
   large folder, or drag a window around.
   * The music keeps its tempo, and its notes keep their places. A short
     delay, 30 ms, is all that differs from the game's picture.
   * With `DosFMDelay=0` in `[ES1869.VXD]` and Windows restarted, the same
     test makes the music stop and hurry, which is what the setting fixes.
   * A game with DOS/4GW plays as with `DosFMDelay=0`. Note whether its
     tempo wavers.
9. Note anything that differs, and in which game.

### E3. The Audio 2 DAC

See
[AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter).

1. With ESS's `ES1869.DRV`, play music at 44.1 kHz in Winamp with the
   waveOut output, open essctl on the *DACs and ADC* page and press F5.
   *Audio 2 4x oversampling* is on.
   * While the music plays, switch the oversampling off and *Audio 2 filter
     bypass* on, then back again. Note what changes.
   * Stop the music and play it again. The oversampling is on again.
2. Install `build\ES1869.DRV` as described in the
   [README](../README.md#es1869drv-with-four-wave-devices-and-unfiltered-playback)
   and restart Windows. Play the same music. F5 shows the oversampling off
   and both filters bypassed (*Audio 2 filter bypass* and *Audio 1 filter
   bypass* on), and the music sounds like the setting you tried in step 1.
   Restart Windows once more and open essctl before any sound plays. F5
   already shows the same setting.
   * Record a few seconds in Sound Recorder. F5 still shows both filters
     bypassed while it records and after it stops.
3. Switch Winamp to its DirectSound output. With the extended `ES1869.VXD`
   from section E, F5 shows the same setting. With ESS's VxD, the
   oversampling is on.
4. With the extended VxD, play a DOS game with Sound Blaster sound in a
   window. While its sound plays, F5 shows *Audio 1 filter bypass* on. Quit
   the game and play a Windows sound, and it stays on.
5. Play Windows sounds at 11 and 22 kHz, such as those in
   `C:\WINDOWS\MEDIA`, and music at 44.1 and 48 kHz. Each plays at its own
   pitch and speed.
6. If the computer has a standby mode, pause Winamp, choose Start > *Shut
   Down* > *Stand by*, then wake the computer and play on. The music keeps
   its pitch, and F5 shows the same setting.
7. Record the Wave output with each setting, as described in [Measuring the
   DAC on the card](AUDIO_PIPELINE.md#measuring-the-dac-on-the-card), and
   keep both files.
8. Check that Sound Recorder records, a MIDI file plays and a DOS game in a
   window has its sound, as before.

### E3b. The Audio 1 device

See [AUDIO1.md](AUDIO1.md#the-audio-1-player-in-buildes1869drv). These steps
need `build\ES1869.DRV` installed.

1. Open Control Panel > *Multimedia* > *Audio*. The playback list has "ESS
   AudioDrive Audio 2 (220)" and "ESS AudioDrive Audio 1 (220)", and the
   recording list has "ESS AudioDrive Record (220)" and "ESS AudioDrive FM
   Digital (220)". The preferred playback device is "ESS AudioDrive Audio 2
   (220)" when it was "ESS AudioDrive Playback (220)" before the install.
   * With it preferred, the speaker button next to the playback list opens
     the Volume Control with ESS's mixer, and so does the volume icon on the
     taskbar.
2. Play music in Winamp on the first device, and a WAV file in Media Player
   with *Audio 1* as the preferred device. Both play, each at its own pitch,
   and neither skips.
   * On essctl's *DACs and ADC* page, the Audio 1 section shows the DAC
     direction in B8h: *CODEC in ADC mode* and *Audio 1 DMA read* are off.
     *Audio 1 DMA enable*, B8h bit 0, turns on within a second of Media
     Player's start, with no key pressed.
   * essctl's *Device information* page says "Audio 1: plays the Audio 1
     device" and "Audio 2: plays the first device". After both stop, both
     channels are "free".
3. With the music playing on Audio 1, start a recording in Sound Recorder.
   It says the device is in use. Stop the music and record again, and the
   recording works.
4. Stop using *Audio 1* as the preferred device, play the music on the first
   device, and start a second player on the same device. The second player
   now plays on Audio 1.
5. Play a short Windows sound on Audio 1 many times, which opens the device
   each time. There are no clicks at the start or the end, and no sound is
   left looping after the last one.
6. In Media Player on Audio 1, pause and play on, and seek. Playback picks
   up where it was, and the position display runs at the right speed.
7. Check the volume of Audio 1. essctl's *Audio 1 (wave) volume* (mixer 14h)
   sets it, and so does a program's own volume control on device 1.
8. If the computer has a standby mode, play something on Audio 1, put the
   computer on standby and wake it. The sound goes on.
9. Set `SharedWaveOut=0` and `Audio1Device=0` in the `[ES1869.DRV]` section
   of `SYSTEM.INI`, and restart Windows. There is one playback device, and a
   second player on it says the device is in use, as with ESS's driver.

### E3d. The FM recording device

See [AUDIO1.md](AUDIO1.md#the-fm-recording-device). These steps need
`build\ES1869.DRV` installed, and a sound editor that lets you type in the
sample rate, such as Cool Edit or GoldWave.

1. Play a MIDI file in Media Player. In the editor, record from "ESS
   AudioDrive FM Digital (220)" at 49,716 Hz, 16-bit stereo, for a minute.
   * The recording has the music, at its pitch and speed, without clicks or
     gaps.
   * Moving the Synth or FM volume in the Windows mixer doesn't change the
     recording's level, because the samples are digital.
   * essctl's *Device information* page says "Audio 1: records the FM
     digitally", and *Music DAC digital record* on the *3-D, mic, MONO, I2S*
     page is on.
2. Stop the recording. *Music DAC digital record* is off again, and the
   music DAC is as it was before.
3. Record again, and close Media Player while the recording runs. The
   recording goes on, and the music DAC stays with FM until the recording
   stops.
4. Try 44,100 Hz on the FM device. The editor reports that the format isn't
   supported.
5. While the FM device records, start a recording in Sound Recorder, and
   play a WAV file on *Audio 1*. Both say that the device is in use. The
   other way round, the FM device can't start while Sound Recorder records.
6. If the computer has a standby mode, put it on standby while the FM device
   records, then wake it. The recording goes on with the music.
7. Set `FMRecordDevice=0` in the `[ES1869.DRV]` section of `SYSTEM.INI`, and
   restart Windows. The recording list has only "ESS AudioDrive Record
   (220)".

### E3c. Dual playback

See [AUDIO1.md](AUDIO1.md#dual-playback). Make the test files on any
computer with Python 3, and copy them over:

```
python3 tools/dualwav.py same --tone 1000 same1k.wav
python3 tools/dualwav.py invert --tone 1000 null1k.wav
python3 tools/dualwav.py same music.wav music4.wav
```

1. In essctl, set *Audio 1 (wave) volume* (14h) and the Wave volume (7Ch) to
   the same level, a few steps below the top.
2. Play `same1k.wav` in Media Player. If Media Player refuses the format,
   make *Audio 1* the preferred playback device. You hear one tone, louder
   than a stereo 1 kHz file. On essctl's *DACs and ADC* page, F5 shows 71h
   bit 1 clear, and 78h shows the DMA and FIFO bits.
3. Play `null1k.wav`. It is much quieter than `same1k.wav`. Make it again
   with other `--delay2` and `--gain2` values to find a quieter one, and
   note the best values.
4. Play `music4.wav`, then the stereo `music.wav` on the first device at the
   same loudness, and note what differs.
5. While a dual file plays, start a Windows sound and a recording. Both say
   the device is in use, and both work once the file has finished.
6. Pause the null file in Media Player and play on. It stays as quiet as
   before.

### E4. SYSTEM.INI settings

See
[DRIVER_CONFIG.md](DRIVER_CONFIG.md#6-the-rebuilt-drivers-systemini-settings).
These steps need all three rebuilt drivers, so run them after G, which
installs `build\ESFM.DRV`.

1. Start with no `[ES1869.VXD]`, `[ES1869.DRV]` or `[ESFM.DRV]` switches in
   `SYSTEM.INI`. essctl's *Device information* page says "VxD settings: from
   SYSTEM.INI" and "Wave driver: the rebuilt ES1869.DRV, settings from
   SYSTEM.INI" with nothing off, and the *ESFM patch bank* page says
   "Settings: from SYSTEM.INI, the square-wave bank".
   * If the page says "the defaults (loaded after Windows started)" instead,
     the VxD couldn't read `SYSTEM.INI` on this computer. Note it.
2. Add every key with the value 0: the keys of the example in
   DRIVER_CONFIG.md, `DualPlayback=0` and all of section 6.3, but set
   `Audio2Oversampling=1`, `Audio2Filter=1` and `Audio1Filter=1`. Keep
   `RegisterAPI`, which essctl needs to read the VxD's settings. Restart
   Windows.
   * Both pages list every change as off. The "Wave driver" line has "off:
     Audio1Device, SharedWaveOut, DualPlayback, FMRecordDevice", and the
     ESFM page says "ESS's bank".
   * In E2 step 2, the FM game doesn't detect FM while Media Player has it,
     as with ESS's driver.
   * In E3, F5 shows the 4x oversampling on, and in E3 step 4 the DOS game's
     sound leaves *Audio 1 filter bypass* off.
   * In G, the stress test reports refused messages, as ESS's driver does.
3. Take the lines out and restart Windows. Every change is back.
4. Try the keys one at a time, for example `[ESFM.DRV] BetterSquareWave=0`
   alone. With that key, the square-wave instruments sound as with ESS's
   driver, and ESFM > *Restore original bank* puts ESS's bank back.

### F. ESFM patch banks

1. Play a MIDI file in Media Player. In essctl, choose ESFM > *Load patch
   bank* and pick `esfm_patch_banks\bnk_com_better_square_wave.bin`. The
   square-wave instruments change within a note or two.
2. Choose ESFM > *Restore original bank*. The sound goes back to what it
   was.
3. Save a profile while a bank is loaded. The profile's `[ESFM] Bank=` line
   reloads the bank on `essctl /load`.
4. Run `esfmpat C:\WINDOWS\SYSTEM\ESFM.DRV bank.bin` with a bank larger than
   8288 bytes.
   * In a DOS box, it refuses, because Windows has the driver loaded.
   * In MS-DOS mode, it reports "Bank moved", and no `ESFM.$$$` is left
     behind.
   * After a restart, MIDI plays with the new bank.
   * `ESFM.BAK` is the original driver.

### G. ESFM hanging notes

See [ESFM_MIDI.md](ESFM_MIDI.md).

1. With ESS's `ESFM.DRV`, open essctl's *ESFM patch bank* page and click
   *Stress test*. Voices are left sounding, which is more likely on a fast
   machine.
2. Play the MIDI files or games that hang notes, with the ESFM page open. A
   voice marked STUCK, or one that keeps "playing" after the music stops, is
   a hanging note.
3. Install `build\ESFM.DRV` with essinst, or from DOS as described in
   [ESFM_MIDI.md](ESFM_MIDI.md#installing), and restart Windows.
4. The page now says "Fixed driver". Run the stress test again. No voice is
   left sounding, and the "queued while busy" count shows how many messages
   ESS's driver would have dropped.
5. Play the same music again. No notes hang, and MIDI, the patch bank and
   *Load bank* work as before.
6. Play music that uses the sustain pedal. After the music stops, no voice
   stays *held by pedal*.

### G2. ESFM bank file

These steps need `build\ESFM.DRV` installed. See
[ESFM_BANK.md](ESFM_BANK.md#bank-file-buildesfmdrv).

1. Copy `esfm_patch_banks\bnk_NT4.bin` to `C:\BANKS\TEST.BIN`, and add these
   lines to `SYSTEM.INI`:
   ```
   [ESFM.DRV]
   Bank=C:\BANKS\TEST.BIN
   ```
2. Play a MIDI file. The *ESFM patch bank* page says `Bank file:
   C:\BANKS\TEST.BIN, 8288 bytes`, followed by the file's date, and the
   instruments sound like the NT4 bank, not the driver's own.
3. Close the player and play the file again without changing the bank file.
   The page counts one more check and no new load.
4. Close the player. Copy `esfm_patch_banks\bnk_com.bin` over
   `C:\BANKS\TEST.BIN`, and give the file the current date in an MS-DOS
   prompt with `cd C:\BANKS`, then `copy /b TEST.BIN +,,`. Play again. ESS's
   original bank sounds, and the page counts one more load.
5. Change the file as in step 4 while music plays. The sound doesn't change
   until the player closes the device and opens it again.
6. Delete `C:\BANKS\TEST.BIN` and play. The page says "cannot read", and the
   last bank keeps playing. Copy a bank back with a new date, and it loads
   the next time a program opens the device.
7. Choose ESFM > *Restore original bank*. The `Bank=` line is gone from
   `SYSTEM.INI`, and the driver's own bank plays. ESFM > *Load patch bank*
   writes the line again.

### G3. General MIDI

These steps need `build\ESFM.DRV` installed. See [ESFM_GM.md](ESFM_GM.md).

1. Play `build\GMCHECK.MID` in Media Player. It plays a square lead and
   tries one feature at a time:
   * 0.5-4.5 s, modulation: C4, then a slight vibrato at 1.5 s, a deeper one
     at 2.5 s, none again at 3.5 s
   * 5-8.5 s, channel pressure: E4, vibrato from 6 s to 7 s
   * 9-13 s, pan: G4 in the middle, left at 10 s, right at 11 s, middle at
     12 s
   * 13.5-17.5 s, fine tuning: C4, a quarter tone sharp at 14.5 s, a quarter
     tone flat at 15.5 s, in tune at 16.5 s
   * 18-20.8 s, coarse tuning: C4, C5, C4
   * 21.5-25.5 s, bend range 12: C4 glides up an octave from 22 s to 23 s,
     then back down at 24 s
   * 26-30 s, master volume: a C chord, quiet at 27 s, louder at 28 s, full
     at 29 s
   * 30.5-34 s, reset all controllers: C4 on the left, sharp, with vibrato.
     At 32.5 s the vibrato stops and it comes back in tune, still on the
     left

2. Play the same file with ESS's `ESFM.DRV`. There is no vibrato, the G4
   stays in the middle, there are no quarter tones, C4 plays three times,
   the chord doesn't get quieter, and the last C4 stays sharp to the end.
3. Play GM MIDI files and games. They sound as before, with vibrato where
   the music uses the modulation wheel or channel pressure.

### H. ess3d

Run these steps with ESS's VxD, and again with the extended driver (E) if
it's installed. First copy `build\ess3d.exe` to `C:\ESSREG`.

1. Play music, and open essctl on the *3-D, mic, MONO, I2S* page. Then run
   these commands from Start > *Run*:
   * `C:\ESSREG\ess3d.exe on level 40` shows a box at the bottom of the
     screen that says "3-D on, level 40 of 63" for 1.5 s, and the music
     changes. In essctl, F5 shows *3-D effect* on and *3-D level* 40.
   * After each of `ess3d off`, `ess3d toggle`, `ess3d up`, `ess3d down 8`
     and `ess3d level 50%`, the box shows the new setting, and essctl agrees
     after F5.
   * `ess3d reset` shows the same setting, and the music keeps its 3-D
     sound.
   * `ess3d hold` shows "held in reset". Note whether the music goes silent
     or plays on without 3-D. `ess3d on` brings the effect back.
2. Make a desktop shortcut to `C:\ESSREG\ESS3D.EXE toggle` with a Shortcut
   key, as described in the [README](../README.md#putting-ess3d-on-a-key).
   With Notepad in front, type a few letters and press the key.
   * The box shows, Notepad's title bar stays active, and typing still goes
     to Notepad.
   * Press the key again while the box is up. The same box shows the new
     setting. No second box or taskbar button appears, and Notepad keeps the
     focus.
3. Do the same in a game, both in a window and full screen. The game keeps
   the focus and its sound, although the box may not show over a full-screen
   game.
4. With ESS's VxD, play a DOS game with sound in a window and press the key.
   The box at the bottom says that the audio device is in use by another
   program and goes away by itself after 3 s, and the game's sound goes on.
   Press the key five times. There is still only one box, and after it has
   gone, Ctrl+Alt+Del lists no ess3d. With the extended driver, the key
   works while the game plays.
5. Run `ess3d bogus`, which shows the usage in a message box.
   `ess3d /q bogus` shows nothing, and `ESS3D.LOG` next to ess3d.exe says
   "unknown command: bogus".
6. Change 3-D in the Windows mixer, then run `ess3d show`. It shows the
   mixer's setting. Restart Windows and run `ess3d show` again, and the
   driver's own setting is back.
7. Start the tray icon with `C:\ESSREG\ESS3D.EXE tray` from Start > *Run*.
   * A *3D* icon appears next to the clock, green if 3-D is on and gray if
     it's off. When you point at it, the tooltip shows the setting.
   * Right-click the icon. A panel opens above it with *3-D effect*, *3-D
     released from reset*, *3-D mono (undocumented)*, *3-D limit
     (undocumented)*, a *Reset* button, *3-D level* and the registers
     54h-5Ah. A left click opens the panel too.
   * Switch *3-D effect* on and click the arrows of the level slider. The
     icon turns green, the music changes, and the text field counts in steps
     of one. Type 30 in the level's text field and press Enter, and the
     slider moves.
   * The panel closes when you click the desktop, when you click the icon
     again, and when you press Esc. Try each one.
   * Run `ess3d off` from a shortcut key, and the icon turns gray. Change
     *3-D effect* in essctl, and the icon follows.
   * With the extended driver, change 3-D in the Windows mixer. The icon
     follows within 3 seconds.
   * Click *Driver defaults*. The panel shows 3-D on, level 63, the limit
     off, and 8Fh, 95h, 94h and 80h in 54h-5Ah.
   * A second `ess3d tray` opens the panel. *Close tray icon* removes the
     icon. Start it again with `ess3d tray`, then run `ess3d exit`, and the
     icon goes too.
   * Put a shortcut to `C:\ESSREG\ESS3D.EXE tray` in the StartUp folder and
     restart Windows. The icon is there after the start.
   * If Explorer restarts, after a crash or when it's ended with
     Ctrl+Alt+Del, the icon comes back with the taskbar.
   * Start `ess3d tray` twice at once, with two shortcuts, or with the
     StartUp folder and a key right after a restart. Only one icon appears.
8. Try the undocumented settings
   ([SPATIALIZER.md](SPATIALIZER.md#finding-out-on-the-card)). No document
   says what they do, so note everything.
   * Boot to DOS from a cold start and, before Windows starts, run
     `essreg r=boot.txt`. Note 50h, 52h and 54h-5Ah, which hold the chip's
     own reset values.
   * In essctl's Expert mode, write FFh to each of 54h, 56h, 58h and 5Ah on
     the *Raw registers* page and read it back, then do the same with 00h.
     Note which bits stay.
   * With music playing, once with a wide stereo image and once with a mono
     voice, try `ess3d mono toggle` and `ess3d limit toggle` at level 63,
     then `ess3d reg 54 00`, `ess3d reg 54 FF` and the same for 56, 58 and
     5A. `ess3d defaults` puts ESS's values back.

### I. esfmrec

Copy `build\esfmrec.exe` to `C:\ESSREG`.

1. Play a MIDI file in Media Player and start `C:\ESSREG\esfmrec.exe`.
   * The window counts up, and the peaks follow the music.
   * After a minute, the rate line says "measured" within a few Hz of 49,716
     Hz.
   * Click *Stop*, then play `FMREC001.WAV`, on another computer if Windows
     can't play 49,716 Hz. The music plays at its pitch and speed, without
     clicks or gaps.
2. Record again, and while esfmrec records, try the following.
   * Play a WAV file. It plays as usual.
   * Move the Synth or FM volume in the Windows mixer. The level in the file
     doesn't change, because the samples are digital.
   * Open Sound Recorder and record. It says the device is in use.
3. Play a DOS game with FM (AdLib) music in a window. Its music is recorded.
4. Try a DOS game with FM music and Sound Blaster sound in a window, with
   the extended `ES1869.VXD` from section E.
   * Start the game and its sound, then start esfmrec. The recording starts
     and has the game's FM music. The game runs on, and its Sound Blaster
     sound goes silent meanwhile.
   * Stop esfmrec. The game's next sound effect plays. A sound that plays
     all the time, such as the sound of an engine, stays silent until the
     game stops it or you quit the game, which you should note.
   * Do the same with the *ESS AudioDrive FM Digital* device of
     `build\ES1869.DRV`, in Sound Recorder or another recording program. It
     also records while the game has the Sound Blaster.
   * With `RecordTakesDSP=0` in `[ES1869.VXD]`, or with ESS's VxD, esfmrec
     says that a DOS program has the Sound Blaster until it ends.
   * Quit the game, start esfmrec, and then start the game. The game gets no
     digital sound, or a message that the device is in use, and its FM music
     is recorded.
5. From Start > *Run*, run `C:\ESSREG\esfmrec.exe /t=10 /q /log=REC.LOG`. It
   records for 10 s and exits, and `REC.LOG` says "10.0 s".
6. After esfmrec has finished, Sound Recorder records the microphone as
   before, and essctl shows *Music DAC digital record* off.
7. End esfmrec by force. Record for a minute, then choose Ctrl+Alt+Del >
   *End Task* on esfmrec.
   * essctl shows *Music DAC digital record* still on, and Sound Recorder
     records the FM instead of the microphone.
   * Start esfmrec again. A box says that esfmrec put the chip's settings
     back and that the file is repaired, and gives the file's length. The
     file plays to the end.
   * Sound Recorder records the microphone again.
8. Run `esfmrec /split=60` for three minutes. It writes `FMREC00n.WAV` and
   the next two files, one minute each, and they play on from one to the
   next without a gap.
9. Press Enter and Esc while esfmrec records. It goes on recording.

### J. Expert mode (last)

Run these steps only while nothing is playing.

1. Choose Options > *Expert mode*.
2. On the *Status & interrupts* page, click *DSP software reset*. The next
   WAV playback still works, because ES1869.DRV reprograms the DSP.
3. In the raw register editor, write the value that a register already has.
   Nothing changes.
4. If sounds skip, find *DRQ latch* on the *Plug and Play* page. Switch it
   on, and note whether essctl reads it back as on or says that the chip
   returns 0. Then follow
   [AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#finding-out-on-the-card).

When you report a problem, include the essctl version, the access path in
the title bar, and `ESSCTL.LOG` or `ESS3D.LOG`.
