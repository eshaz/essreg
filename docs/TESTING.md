# Testing

## Automated tests

```
python3 tests/run_tests.py
```

* Needs Python 3, gcc and NASM, plus the `unicorn` Python module for the CPU emulator tests.
* **Python tests** (`tests/test_*.py`):
  * LE and NE tooling, and the byte-identical rebuilds of `ES1869.VXD` and `ESFM.DRV`
  * `ESFM.DRV` in a CPU emulator with an FM chip model and simulated interrupts: the hanging notes of ESS's driver, and the fixed driver (`test_esfmdrv`, [ESFM_MIDI.md](ESFM_MIDI.md))
  * the fixed driver's bank file in the same emulator, with `SYSTEM.INI`, files with dates and the global heap simulated
  * the register API of the rebuilt VxD, run in a CPU emulator against a simulated ES1869
  * `esfmpat` on copies of `ESFM.DRV`
  * that the generated documentation is current
* **C tests** (`tests/host/t_*.c`), built with gcc against a simulated ES1869 (`src/simhw.c`):
  * the port protocols (`t_esshw`)
  * the VxD API wrappers (`t_vxdapi`)
  * the register catalog (`t_esscat`)
  * profiles (`t_profile`)
  * patch banks (`t_esfm`)
  * a port trace proving that the refactored essreg talks to the chip exactly like the original did (`t_trace`)

**With Open Watcom v2** (`OW2=/path/to/open-watcom`), the tests also:
* run essctl's 16-bit VxD call thunk in a CPU emulator
* build every program and check `essctl.exe`'s NE header: Windows 4.0, one data segment, discardable code, imports, resources (`test_ow2build`)

To compile everything with Open Watcom on Linux:

```
tools/ow2build.sh /path/to/open-watcom     # binaries in out/ow2/
```

**Under Wine.** `tests/test_wine.py` runs `essctl.exe` as a 16-bit Windows program (`ESSREG_WINE=1`, needs 32-bit Wine and Xvfb):
* profile save/load against the simulated chip
* the ESFM live load against the real `ESFM.DRV`
* the ESFM voice table of `essctl /dump`, with ESS's driver and the fixed one
* the fixed `ESFM.DRV` loading the bank file named in `SYSTEM.INI` when the device is opened, and `essctl /load` naming it there

## On the hardware

* None of the above touches a real card.
* Before you start, keep copies of `C:\WINDOWS\SYSTEM\ES1869.VXD` and `ESFM.DRV`.
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
2. Toggle *Mic +26 dB preamp* and speak into the microphone with the Windows mixer's mic monitor on.
3. *ADC offset & power*:
   * click *Read controller registers*, set *ADC offset L* to +3 and read again
   * start and stop a WAV playback, then read again. Does the value survive? (BAh and BBh should survive a DSP reset.)
4. **Profiles.**
   1. File > *Save profile* to `C:\ESS\MY.INI`.
   2. Change a few values, then File > *Load profile*: they come back.
   3. Put `essctl /load C:\ESS\MY.INI` in the StartUp group and restart Windows. The settings are back, and `ESSCTL.LOG` next to essctl.exe says "... settings applied".

### C. DOS box contention

1. Start a DOS game that uses the card (for example Doom's setup with sound test) in a window. While it plays, press F5 in essctl.
   * The rows say "in use by DOS". There's no Windows "device in use" message and no sound glitch in the game.
2. Quit the game and press F5: the values come back.
3. Start the game again after using essctl. It must get sound: essctl gives the DSP back when it took it.

### D. essreg (DOS)

1. In real DOS (not Windows), run `essreg a` and `essreg 3=40 m=1`: same results as the original essreg.
2. `essreg x=1 a1s` uses the protected protocol (C6h, polling Audio_Base+Ch). Check that the reported Audio 1 sample rate matches the default mode.

### E. The extended driver

1. Install `build\ES1869.VXD` as in [VXD_INTERNALS.md](VXD_INTERNALS.md#installing-the-extended-driver) and restart.
2. Windows sounds, MIDI, a DirectSound game and a DOS game in a window all work like before.
3. essctl's title bar says "VxD register API 1.00", and the owners line (bottom right) shows the DSP/FM/MPU owners.
4. With a DOS game playing, change *Audio 2 volume* or *Master volume* in essctl. The change applies right away and the game keeps its sound. That's the point of the register API!
5. Put the original driver back if anything misbehaves, and note what.

### F. ESFM patch banks

1. Play a MIDI file in Media Player. In essctl, ESFM > *Load patch bank* `esfm_patch_banks\bnk_com_better_square_wave.bin`: the square-wave instruments change within a note or two.
2. ESFM > *Restore original bank*: the sound goes back.
3. Save a profile with a bank loaded. Its `[ESFM] Bank=` line reloads the bank on `essctl /load`.
4. `esfmpat C:\WINDOWS\SYSTEM\ESFM.DRV bank.bin` with a bank larger than 8288 bytes:
   * it reports "Bank moved"
   * after a restart, MIDI plays with the new bank
   * `ESFM.BAK` is the original

### G. ESFM hanging notes

See [ESFM_MIDI.md](ESFM_MIDI.md).

1. With ESS's `ESFM.DRV`: essctl, *ESFM patch bank* page, *Stress test*. Expected: voices left sounding (more likely on a fast machine).
2. Play the MIDI files or games that hang notes, with the ESFM page open. A voice marked STUCK, or one that keeps "playing" after the music stops, is a hanging note.
3. Install `build\ESFM.DRV` from DOS (see [ESFM_MIDI.md](ESFM_MIDI.md#installing)) and restart.
4. The page says "Fixed driver". Run the stress test again: no voice left sounding. The "queued while busy" count is what ESS's driver would have dropped.
5. Play the same music again: no hanging notes. MIDI, the patch bank and *Load bank* work like before.
6. Play music that uses the sustain pedal. When a song changes a channel's program with the pedal down, the page lists "pedal let up by a program change". After the music stops, no channel should keep its pedal down.

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

### H. Expert mode (last)

Only with nothing playing:
1. Options > *Expert mode*.
2. *Status & interrupts* > *DSP software reset*. The next WAV playback still works, because ES1869.DRV reprograms the DSP.
3. In the raw register editor, write the value a register already has. Nothing should change.

Report the essctl version, the access path in the title bar, and `ESSCTL.LOG` with any problem.
