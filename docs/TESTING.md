# Testing

## Automated tests

```
python3 tests/run_tests.py
```

* Needs Python 3, gcc and NASM, plus the `unicorn` Python module for the CPU emulator tests.
* On Claude Code on the web, [`.claude/hooks/session-start.sh`](../.claude/hooks/session-start.sh) installs all of it, Open Watcom and Wine included, and sets `OW2` and `ESSREG_WINE=1`.
* **Python tests** (`tests/test_*.py`):
  * LE and NE tooling, and the byte-identical rebuilds of `ES1869.VXD` and `ESFM.DRV`
  * `ESFM.DRV` in a CPU emulator with an FM chip model and simulated interrupts: the hanging notes of ESS's driver, and the fixed driver (`test_esfmdrv`, [ESFM_MIDI.md](ESFM_MIDI.md))
  * the fixed driver's bank file in the same emulator, with `SYSTEM.INI`, files with dates and the global heap simulated
  * the register API of the rebuilt VxD, run in a CPU emulator against a simulated ES1869, and its refusals and checks (`test_vxdext`)
  * that the extended VxD keeps ESS's code where it was, byte for byte apart from the hooks, and that Windows' own sound (ES1869.DRV's calls around a wave device, a mixer change, DirectSound taking the DSP) makes the same port accesses as with ESS's driver, apart from the Audio 2 mode (`test_vxdext`)
  * `ES1869.DRV` rebuilt from `src/es1869`: ESS's driver byte for byte, and in the changed build ESS's code at its addresses, apart from the listed instructions. Its writes of the Audio 2 mode run in the CPU emulator inside ESS's code, for both builds (`test_es1869drv`)
  * DOS boxes and the rebuilt VxD in the same emulator, with an FM chip model after ESFMu, VMs and per-VM port trapping (`test_vxddos`): FM detection whoever has FM, the virtual FM chip and its hand-over, the music DAC, Windows' mixer around a DOS game, and the reset when Windows uses the card again. The stock driver runs the same steps where it differs.
  * `esfmpat` on copies of `ESFM.DRV`
  * that the generated documentation is current
* **C tests** (`tests/host/t_*.c`), built with gcc against a simulated ES1869 (`src/simhw.c`):
  * the port protocols (`t_esshw`)
  * the VxD API wrappers (`t_vxdapi`)
  * the register catalog (`t_esscat`)
  * profiles (`t_profile`)
  * ess3d's command line and 3-D register changes (`t_ess3d`)
  * essreg's register functions that the original didn't have, like the 3-D limit (`t_regs`)
  * patch banks (`t_esfm`)
  * esfmrec's WAV header and its repair, the names of a long recording's files, the levels and the test tone (`t_fmrec`)
  * a port trace proving that the refactored essreg talks to the chip exactly like the original did (`t_trace`)

**With Open Watcom v2** (`OW2=/path/to/open-watcom`), the tests also:
* run essctl's 16-bit VxD call thunk in a CPU emulator
* build every program and check the NE headers of `essctl.exe` and `ess3d.exe`: Windows 4.0, one data segment, discardable code, imports, exports, resources (`test_ow2build`)

To compile everything with Open Watcom on Linux:

```
tools/ow2build.sh /path/to/open-watcom     # binaries in out/ow2/
```

**Under Wine.** `tests/test_wine.py` runs `essctl.exe`, `ess3d.exe` and `esfmrec.exe` as 16-bit Windows programs (`ESSREG_WINE=1`, needs 32-bit Wine and Xvfb):
* profile save/load against the simulated chip
* the ESFM live load against the real `ESFM.DRV`
* the ESFM voice table of `essctl /dump`, with ESS's driver and the fixed one
* the fixed `ESFM.DRV` loading the bank file named in `SYSTEM.INI` when the device is opened, and `essctl /load` naming it there
* ess3d's commands against the simulated chip, a bad command and no card, read from its `/log=` file
  * *Note: Wine drops the exit code of a 16-bit Windows program (its process always exits with 0), so the log is what the test checks.*
* ess3d's box: a second ess3d hands its setting to the box of the first one and exits
* ess3d's tray icon: a second `ess3d tray` opens the panel of the first one, and `ess3d exit` closes it
  * *Note: Wine shows no tray icon for a 16-bit program, since its 16-bit and 32-bit icon handles differ. Windows 9x has one kind, so the icon only shows there.*
* esfmrec's recording of the test tone, `/raw` and `/split=`
* esfmrec ended by force (Wine's `taskkill /f`): the header it saved every 5 s holds the samples, and the next start repairs it
* esfmrec on a full disk, made with a file size limit: it stops, and the file stays playable

**Screenshots.** `tools/wineshot.sh` runs essctl under Wine on a virtual screen, to click through it and see the pages:

```
tools/wineshot.sh start out/ow2 essctl.exe /sim
tools/wineshot.sh click 60 216          # ESFM patch bank
tools/wineshot.sh shot esfm.png
tools/wineshot.sh stop
```

It shows ess3d's box too, kept up with a long `/t=`:

```
tools/wineshot.sh start out/ow2 ess3d.exe /sim /t=20000 on level 40
tools/wineshot.sh shot ess3d.png
tools/wineshot.sh stop
```

And the tray's panel: `run` starts a second program on the same desktop, here the `ess3d tray` that opens the panel of the first one.

```
tools/wineshot.sh start out/ow2 ess3d.exe /sim tray
tools/wineshot.sh run ess3d.exe /sim tray
tools/wineshot.sh shot panel.png
tools/wineshot.sh stop
```

## On the hardware

* None of the above touches a real card.
* Before you start, keep copies of `C:\WINDOWS\SYSTEM\ES1869.VXD`, `ES1869.DRV` and `ESFM.DRV`.
* Run the steps in this order and stop at the first surprise. Each step says what to expect.

### A. Reading, with the stock driver

1. Start `essctl`.
   * The title bar says "Direct I/O at 220h (stock ES1869.VXD 4.04)".
   * *Device information* shows the same resources as Device Manager (I/O, IRQ, DMA) and "Mixer 40h ID: 18h 69h ... (ES1869)".
2. Walk through the pages.
   * Values show up, and there's no "sound device in use" box.
   * Play a WAV file in the Sound Recorder while essctl is open: it plays normally.
3. Run `essreg r=before.txt` in a DOS box and compare it with `essctl /dump dump.txt`: the mixer values must agree.

### B. Changing settings

1. *3-D, mic, MONO, I2S*: switch *3-D effect* on and move *3-D level*. Music playing in Windows changes audibly.
   * Click the slider's arrows: the level goes up or down by one, and the text field follows.
   * Type 20 in the text field and press Enter: the slider moves to 20. Type 99: a beep, and the text field goes back to 20.
2. Toggle *Mic +26 dB preamp* and speak into the microphone with the Windows mixer's mic monitor on.
3. *ADC offset & power*:
   * click *Read controller registers*, set *ADC offset L* to +3 and read again
   * start and stop a WAV playback, then read again. Does the value survive? (BAh and BBh should survive a DSP reset.)
4. **Profiles.**
   1. File > *Save profile* to `C:\ESS\MY.INI`.
   2. Change a few values, then File > *Load profile*: they come back.
   3. Put `essctl /load C:\ESS\MY.INI` in the StartUp group and restart Windows. The settings are back, and `ESSCTL.LOG` next to essctl.exe says "... settings applied".
   4. With the stock driver, start a DOS game with sound in a window, then File > *Save profile* over `MY.INI`: essctl says the device is in use, and `MY.INI` is unchanged (its date too).

5. **The music DAC.**
   1. Play a MIDI file in the Media Player and stop it. *3-D, mic, MONO, I2S* shows *I2S drives music DAC* on (ESS's driver gives the DAC back to I2S), and *Device information* says "I2S has it while no MIDI program is open".
   2. Options > *FM keeps the music DAC*. The check box on the page goes off, and *Device information* says "FM keeps it".
   3. Restart Windows. Play and stop a MIDI file again: *I2S drives music DAC* stays off.
   4. Start a DOS game that plays FM music in a window. The music plays, and follows *Music DAC (FM) volume* on the *Output mixer* page.
   5. `essctl /i2s=on` and restart Windows to go back.

### C. DOS box contention

1. Start a DOS game that uses the card (for example Doom's setup with sound test) in a window. While it plays, press F5 in essctl.
   * The rows say "in use by DOS". There's no Windows "device in use" message and no sound glitch in the game.
2. Quit the game and press F5: the values come back.
3. Start the game again after using essctl. It must get sound: essctl gives the DSP back when it took it.

### D. essreg (DOS)

1. In real DOS (not Windows), run `essreg a` and `essreg 3=40 m=1`: same results as the original essreg.
   * `essreg 3=99` says "use 0 to 63", and changes nothing.
   * In a Windows DOS box while a WAV file plays: essreg says the ES1869 doesn't answer.
2. `essreg x=1 a1s` uses the protected protocol (C6h, polling Audio_Base+Ch). Check that the reported Audio 1 sample rate matches the default mode.

### E. The extended driver

1. Install `build\ES1869.VXD` as in [VXD_INTERNALS.md](VXD_INTERNALS.md#installing-the-extended-driver) and restart.
2. Windows sounds, MIDI, a DirectSound game and a DOS game in a window all work like before.
3. essctl's title bar says "VxD register API 1.10", and the owners line (bottom right) shows the DSP/FM/MPU owners.
4. With a DOS game playing, change *Audio 2 volume* or *Master volume* in essctl. The change applies right away and the game keeps its sound. That's the point of the register API!
5. Put the original driver back if anything misbehaves, and note what.
6. **Long playback.** Play a stream or a long MP3 for 10 minutes in Winamp, once with the DirectSound output and once with waveOut, with essctl, the tray and DOS boxes closed. Listen for skips.
   * If it skips, restart with `ES1869.ORG` in place and play the same stream. Skips with both drivers come from the card's DMA, see [AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#finding-out-on-the-card). Skips with the extended driver only are this repository's, so note which output and how often.

### E2. DOS boxes with the extended driver

Use an FM-only DOS game or player (AdLib music, no Sound Blaster), and one that uses the Sound Blaster too.
1. **FM while nothing else has it.** Run the FM game in a DOS box, without `1869opl3`. It detects the AdLib or OPL3 and its music plays: the driver gave it the music DAC and an FM volume. Quit it: essctl's *3-D, mic, MONO, I2S* page shows the music DAC back as before.
2. **FM while Windows' MIDI has it.** Open a MIDI file in the Media Player and pause it. Start the FM game.
   * It must detect the FM synthesizer and run, silently.
   * Close the Media Player: the game's music starts within a moment, with its instruments.
3. **Two DOS boxes.** Start the FM game in two DOS boxes. The second one detects FM and runs silently. Quit the first one: the second one's music starts.
4. **Windows' mixer after a DOS game.** In the Windows volume control and essctl, note the 3-D effect, the record source (Recording) and the wave volume. Play the Sound Blaster game (most reset the mixer), then quit it.
   * Everything is as it was. With ESS's driver the 3-D effect, the record source and the wave volume stayed as the game left them.
5. **The reset.** Kill the FM game in the middle of a note (close the DOS box window, or Ctrl+Alt+Del). If a note keeps sounding, move a slider in the tray's volume control, or play any Windows sound: the note stops.
6. **A game that runs other programs** (a menu that starts the game, a cutscene player): its music keeps its instruments after the other program ends.
7. **Recording a DOS game.** With esfmrec recording, play the FM game: the recording has its music (esfmrec keeps 7Fh bit 4 on; the driver changes only bit 0 and 36h).
8. Note anything that differs, and the game.

### E3. The Audio 2 DAC

See [AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter).

1. With ESS's `ES1869.DRV`, play music at 44.1 kHz in Winamp with the waveOut output, and open essctl on *Audio 2 channel*. F5: *Audio 2 4x oversampling* is on.
   * Switch it off and *Audio 2 filter bypass* on while the music plays, and back. Note what changes.
   * Stop and play again: the oversampling is on again.
2. Install `build\ES1869.DRV` as in the [README](../README.md#es1869drv-with-the-audio-2-dac-unfiltered) and restart. Play the same music: F5 shows the oversampling off and the filter bypassed, and it sounds like the setting of step 1.
3. Winamp's DirectSound output: the same with the extended `ES1869.VXD` (E). With ESS's, the oversampling is on.
4. Play Windows sounds at 11 and 22 kHz, like those in `C:\WINDOWS\MEDIA`, and music at 44.1 and 48 kHz: each plays at its pitch and speed.
5. If the computer has a standby: pause Winamp, Start > *Shut Down* > *Stand by*, wake the computer and play on. The music keeps its pitch, and F5 shows the same setting.
6. Record the Wave output with each setting, as in [Measuring the DAC on the card](AUDIO_PIPELINE.md#measuring-the-dac-on-the-card), and keep both files.
7. Sound Recorder records, a MIDI file plays, and a DOS game in a window has its sound, as before.

### F. ESFM patch banks

1. Play a MIDI file in Media Player. In essctl, ESFM > *Load patch bank* `esfm_patch_banks\bnk_com_better_square_wave.bin`: the square-wave instruments change within a note or two.
2. ESFM > *Restore original bank*: the sound goes back.
3. Save a profile with a bank loaded. Its `[ESFM] Bank=` line reloads the bank on `essctl /load`.
4. `esfmpat C:\WINDOWS\SYSTEM\ESFM.DRV bank.bin` with a bank larger than 8288 bytes:
   * in a DOS box, it refuses: Windows has the driver loaded
   * from MS-DOS mode, it reports "Bank moved", and no `ESFM.$$$` is left
   * after a restart, MIDI plays with the new bank
   * `ESFM.BAK` is the original

### G. ESFM hanging notes

See [ESFM_MIDI.md](ESFM_MIDI.md).

1. With ESS's `ESFM.DRV`: essctl, *ESFM patch bank* page, *Stress test*. Expected: voices left sounding (more likely on a fast machine).
2. Play the MIDI files or games that hang notes, with the ESFM page open. A voice marked STUCK, or one that keeps "playing" after the music stops, is a hanging note.
3. Install `build\ESFM.DRV` from DOS (see [ESFM_MIDI.md](ESFM_MIDI.md#installing)) and restart.
4. The page says "Fixed driver". Run the stress test again: no voice left sounding. The "queued while busy" count is what ESS's driver would have dropped.
5. Play the same music again: no hanging notes. MIDI, the patch bank and *Load bank* work like before.
6. Play music that uses the sustain pedal. After the music stops, no voice stays *held by pedal*.

### G2. ESFM bank file

With `build\ESFM.DRV` installed. See [ESFM_BANK.md](ESFM_BANK.md#bank-file-buildesfmdrv).

1. Copy `esfm_patch_banks\bnk_NT4.bin` to `C:\BANKS\TEST.BIN`. Add to `SYSTEM.INI`:
   ```
   [ESFM.DRV]
   Bank=C:\BANKS\TEST.BIN
   ```
2. Play a MIDI file. The *ESFM patch bank* page says `Bank file: C:\BANKS\TEST.BIN, 8288 bytes`, with the file's date, and the instruments sound like the NT4 bank, not the driver's own.
3. Close the player and play the file again, without changing the bank file. The page counts one more check and no new load.
4. Close the player. Copy `esfm_patch_banks\bnk_com.bin` over `C:\BANKS\TEST.BIN` and give it the current date in an MS-DOS prompt: `cd C:\BANKS`, then `copy /b TEST.BIN +,,`. Play again: ESS's original sounds, and one more load.
5. Change the file (step 4) while music plays: the sound doesn't change until the player closes and opens the device again.
6. Delete `C:\BANKS\TEST.BIN` and play: the page says "cannot read", and the last bank keeps playing. Copy a bank back with a new date: it loads the next time a program opens the device.
7. ESFM > *Restore original bank*: the `Bank=` line is gone from `SYSTEM.INI` and the driver's own bank plays. ESFM > *Load patch bank* writes it again.

### G3. General MIDI

With `build\ESFM.DRV` installed. See [ESFM_GM.md](ESFM_GM.md).

1. Play `build\GMCHECK.MID` in Media Player. It plays a square lead, one feature at a time:

   | Time | What to hear |
   |---|---|
   | 0.5-4.5 s | Modulation: C4, then a slight vibrato at 1.5 s, a deeper one at 2.5 s, none again at 3.5 s |
   | 5-8.5 s | Channel pressure: E4, vibrato from 6 s to 7 s |
   | 9-13 s | Pan: G4 in the middle, left at 10 s, right at 11 s, middle at 12 s |
   | 13.5-17.5 s | Fine tuning: C4, a quarter tone sharp at 14.5 s, a quarter tone flat at 15.5 s, in tune at 16.5 s |
   | 18-20.8 s | Coarse tuning: C4, C5, C4 |
   | 21.5-25.5 s | Bend range 12: C4 glides up an octave from 22 s to 23 s, then back down at 24 s |
   | 26-30 s | Master volume: a C chord, quiet at 27 s, louder at 28 s, full at 29 s |
   | 30.5-34 s | Reset all controllers: C4 on the left, sharp, with vibrato. At 32.5 s the vibrato stops and it comes back in tune, still on the left |

2. The same file with ESS's `ESFM.DRV`: no vibrato, the G4 stays in the middle, no quarter tones, C4 three times, the chord doesn't get quieter, and the last C4 stays sharp to the end.
3. Play GM MIDI files and games. They sound as before, with vibrato where the music uses the modulation wheel or channel pressure.

### H. ess3d

With the stock driver, and again with the extended driver (E) if it's installed. Copy `build\ess3d.exe` to `C:\ESSREG`.

1. Play music, and open essctl on *3-D, mic, MONO, I2S*. From Start > *Run*:
   * `C:\ESSREG\ess3d.exe on level 40`: a box at the bottom of the screen says "3-D on, level 40 of 63" for 1.5 s, and the music changes. F5 in essctl shows *3-D effect* on and *3-D level* 40.
   * `ess3d off`, `ess3d toggle`, `ess3d up`, `ess3d down 8`, `ess3d level 50%`: the box shows each new setting, and essctl agrees after F5.
   * `ess3d reset`: the same setting, and the music keeps its 3-D sound.
   * `ess3d hold`: the box says "held in reset". Is the music silent, or does it play without 3-D? Note which. `ess3d on` brings the effect back.
2. Make a desktop shortcut to `C:\ESSREG\ESS3D.EXE toggle` with a Shortcut key, as in the [README](../README.md#putting-ess3d-on-a-key). With Notepad in front, type a few letters and press the key:
   * the box shows, Notepad's title bar stays active and typing still goes to Notepad
   * press the key again while the box is up: the same box shows the new setting. No second box, no taskbar button, and Notepad keeps the focus.
3. The same in a game, in a window and full screen: the game keeps the focus and its sound. The box may not show over a full-screen game.
4. With the stock driver, play a DOS game with sound in a window and press the key: the box at the bottom says the audio device is in use by another program, goes away by itself after 3 s, and the game's sound goes on. Press the key five times: still one box, and Ctrl+Alt+Del lists no ess3d after it's gone. With the extended driver, the key works while the game plays.
5. `ess3d bogus` shows the usage in a message box. `ess3d /q bogus` shows nothing, and `ESS3D.LOG` next to ess3d.exe says "unknown command: bogus".
6. Change 3-D in the Windows mixer, then `ess3d show`: it shows the mixer's setting. Restart Windows and `ess3d show`: the driver's own setting is back.
7. **The tray icon.** `C:\ESSREG\ESS3D.EXE tray` from Start > *Run*:
   * A *3D* icon shows next to the clock, green if 3-D is on, gray if it's off. Point at it: the tooltip shows the setting.
   * Right-click it: a panel opens above the icon with *3-D effect*, *3-D released from reset*, *3-D limit*, *3-D level* and the registers 54h-5Ah. A left click opens it too.
   * Switch *3-D effect* on and click the level slider's arrows: the icon turns green, the music changes, and the text field counts in steps of one. Type 30 in the level's text field and press Enter: the slider moves.
   * Click the desktop: the panel closes. Open it again and click the icon: it closes. Open it again and press Esc: it closes.
   * `ess3d off` on a key: the icon turns gray. Change *3-D effect* in essctl: the icon follows.
   * With the extended driver, change 3-D in the Windows mixer: the icon follows within 3 seconds.
   * *Driver defaults*: 3-D on, level 63, the limit off, and 54h-5Ah 8Fh, 95h, 94h and 80h.
   * A second `ess3d tray` opens the panel. *Close tray icon* removes the icon. `ess3d tray`, then `ess3d exit`: it goes too.
   * Put a shortcut to `C:\ESSREG\ESS3D.EXE tray` in the StartUp folder and restart Windows: the icon is there after the start.
   * If Explorer restarts (after a crash, or ended with Ctrl+Alt+Del), the icon comes back with the taskbar.
   * Start `ess3d tray` twice at once (two shortcuts, or the StartUp folder and a key right after a restart): one icon.
8. **The undocumented settings** ([SPATIALIZER.md](SPATIALIZER.md#finding-out-on-the-card)). No document says what they do, so note everything:
   * Before Windows starts, from a cold boot to DOS: `essreg r=boot.txt`. Note 50h, 52h and 54h-5Ah: the chip's own reset values.
   * In essctl's Expert mode, write FFh to each of 54h, 56h, 58h and 5Ah on the *Raw registers* page and read it back, then 00h. Note which bits stay.
   * With music playing, with a wide stereo image and with a mono voice: `ess3d mono toggle` and `ess3d limit toggle` at level 63, then `ess3d reg 54 00`, `ess3d reg 54 FF` and the same for 56, 58 and 5A. `ess3d defaults` puts ESS's values back.

### I. esfmrec

Copy `build\esfmrec.exe` to `C:\ESSREG`.

1. Play a MIDI file in the Media Player and start `C:\ESSREG\esfmrec.exe`.
   * The window counts up, and the peaks follow the music.
   * After a minute the rate line says "measured" within a few Hz of 49,716 Hz.
   * *Stop*, then play `FMREC001.WAV` (on another computer if Windows can't play 49,716 Hz): the music at its pitch and speed, without clicks or gaps.
2. Record again, and meanwhile:
   * play a WAV file: it plays
   * move the Synth or FM volume in the Windows mixer: the level in the file doesn't change, since the samples are digital
   * open Sound Recorder and record: it says the device is in use.
3. A DOS game with FM (AdLib) music, in a window: its music records.
4. A DOS game with Sound Blaster sound:
   * start the game and its sound, then esfmrec: esfmrec says a DOS program is playing Sound Blaster sound
   * quit, start esfmrec, then the game: the game gets no digital sound, or a message that the device is in use, and its FM music records.
5. From Start > *Run*, `C:\ESSREG\esfmrec.exe /t=10 /q /log=REC.LOG`: it records 10 s and exits. `REC.LOG` says "10.0 s".
6. After esfmrec: Sound Recorder records the microphone as before, and essctl shows *Music DAC digital record* off.
7. **Ended by force.** Record for a minute, then Ctrl+Alt+Del > *End Task* on esfmrec.
   * essctl shows *Music DAC digital record* still on, and Sound Recorder records the FM instead of the microphone.
   * Start esfmrec again: a box says it put the chip's settings back, and that the file is repaired with its length. The file plays to the end.
   * Sound Recorder records the microphone again.
8. `esfmrec /split=60` for three minutes: `FMREC00n.WAV` and the next two, one minute each, playing on without a gap.
9. Press Enter and Esc while it records: it goes on.

### J. Expert mode (last)

Only with nothing playing:
1. Options > *Expert mode*.
2. *Status & interrupts* > *DSP software reset*. The next WAV playback still works, because ES1869.DRV reprograms the DSP.
3. In the raw register editor, write the value a register already has. Nothing should change.
4. If sounds skip: *Plug and Play* > *DRQ latch*. Switch it on and note whether essctl reads it back on or says the chip returns 0. Then follow [AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#finding-out-on-the-card).

Report the essctl version, the access path in the title bar, and `ESSCTL.LOG` (or `ESS3D.LOG`) with any problem.
