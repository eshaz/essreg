# The MIDI suite

No free test suite checks a General MIDI synthesizer by machine. The MIDI
Manufacturers Association publishes the specification, its Developer
Guidelines and RP-015 (Response to Reset All Controllers), not tests.
FluidSynth's tests and the MIDI files of `jazz-soft/test-midi-files` check
other things, a synthesizer's samples and what a listener hears. So
[`tests/test_midisuite.py`](../tests/test_midisuite.py) is essreg's own. It
runs `build/ESFM.DRV` in the CPU emulator of `tests/esfmemu.py`, sends each
message to device 0, ESS's synthesizer, or to device 1, the OPL3 device, and
checks both against [`tests/midiref.py`](../tests/midiref.py), a reference
in Python of what the specification and the drivers' documented changes ask
for: each channel's controllers, the notes that may sound, with the sustain
pedal, all notes off, all sound off, reset all controllers, the resets and
the master volume, and the notes that `MpcSongs` leaves out.
* `SongTest` plays the 20 MIDI files of Windows 3.0, 98 and XP in
  [third_party/windows_midi](../third_party/windows_midi/README.md),
  `digital_recording/GROOVE.MID` and `build/GMCHECK.MID`, and random songs.
  After every message no voice may sound a note that the reference has
  ended, and on device 1 every note on that the device can play sounds and
  every voice's carrier has the reference's level. The 15 songs that hold
  two arrangements lose their base-level copy, the same notes as in the
  reference, and the others lose nothing. Each song plays its first 30
  seconds, and with `ESSREG_MIDI_FULL=1` all of it, about 4 minutes.
* `HistoryTest` plays random streams, then starts a new song with a GM
  reset, `MODM_RESET`, or a close and an open, and plays a probe note, whose
  voice registers must be those of a device that played nothing before. A
  level, a pitch, a pan or an instrument's registers left over from the last
  song fails it.
* `RulesTest` checks the rules one at a time: a note on with velocity 0, the
  pedal, all notes off and all sound off, a program change, RP-015's reset,
  the GM, GM2, GS and XG resets, the master volume, and the volume curve of
  the Developer Guidelines, -4.8 dB at volume 96, -11.9 at 64, -23.9 at 32
  and -36.0 at 16, on device 1 within a step of 0.75 dB.

A broken driver fails it: the suite catches a pedal that doesn't let the
notes go, a level without the expression, a carrier's level kept from the
last instrument, and the base-level copies playing. A song added to
`third_party/windows_midi` plays in `SongTest`, and a song that holds two
arrangements goes into `TWO` in the test.
