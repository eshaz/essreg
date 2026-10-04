# ESS driver settings and the registers Windows sets

This page describes the registry values read by the ES1869's Windows 95
drivers, `ES1869.VXD` and `ES1869.DRV`, and what each value does to the
chip. It also lists the registers that `ES1869.DRV` writes while Windows
runs, and the SYSTEM.INI keys that turn off the changes in the rebuilt
drivers ([section 6](#6-the-rebuilt-drivers-systemini-settings)).

The findings come from ESS's files in `driver/`. For `ES1869.VXD` they rest
on its source in `src/vxd`, and for `ES1869.DRV` on a disassembly made with
`ndisasm` after `tools/retools/ne.py` had resolved the driver's NE
relocation chains. [REGISTERS.md](REGISTERS.md) explains what the registers
mean, and [VXD_API.md](VXD_API.md) documents the VxD's programming
interface.

An address such as `3:3E8F` means segment 3, offset 3E8Fh, of `ES1869.DRV`.
The segments are numbered from 1 in the order of the NE segment table:
* **Segment 1** holds the fixed code: the port I/O helpers (DSP write
  1:1228, DSP read 1:11F6, mixer read 1:1294, mixer write 1:12B2) and the
  wave code that runs at interrupt time.
* **Segment 2** holds the WEP.
* **Segment 3** holds the code for device enable and disable, the registry
  and the configuration dialog.
* **Segment 4** holds `DriverProc` (4:02DA) and the wrappers for the VxD
  API.
* **Segment 5** holds the mixer: `mxdMessage` 5:1BA4 and set-control-details
  5:17CA.
* **Segment 6** holds wave and aux: `widMessage` 6:0FF2, `wodMessage` 6:18E4
  and `auxMessage` 6:1D1C.
* **Segment 7** is the data segment (DS).

An address such as `o5:2122` is an offset in object 5 of `ES1869.VXD`,
numbered as in `src/vxd`.

The confidence given for the values in sections 1 and 2 uses these terms:
* *verified* means that the code was followed from the registry read to the
  register write, or to the effect other than a register write that the
  entry describes.
* *likely* means that the read and the flag were followed, but the purpose
  of the code that uses the flag is inferred.

## Where the settings live

Both drivers read their settings from the device's software key,
`HKLM\System\CurrentControlSet\Services\Class\Media\<nnnn>\Config`, which
the OEMSETUP.INF file writes as `HKR,Config,...`.

ES1869.VXD reads its values with CONFIGMG's `CM_Read_Registry_Value(devnode,
"Config", name, REG_BINARY, ..., CM_REGISTRY_SOFTWARE)` when the device
starts (o5:0000).

ES1869.DRV gets the name of the key from CONFIGMG's protected-mode API (INT
2Fh AX=1684h BX=0033h) with function 3Dh, `CM_Get_DevNode_Key(devnode, NULL,
buffer, 100h, CM_REGISTRY_SOFTWARE)`, in the wrapper 3:09EA. The driver then
appends `\Config` or `\Config\GPO Selections`, opens that key under
HKEY_LOCAL_MACHINE with KERNEL's `RegOpenKey` (ordinal 217), and reads the
values with `RegQueryValueEx` (ordinal 225). Two values go through CONFIGMG
instead: "Telegaming Vol" (function 3Eh, 6:1FD6), and "Single Mode DMA" in
the configuration dialog, which is read with function 3Eh at 3:44E6 and
written with function 3Fh, `CM_Write_Registry_Value`, at 3:4549.

ES1869.DRV's `RegQueryValueEx` calls pass no pointer for the type, so the
driver accepts a value of any type. It reads each value into a buffer that
already holds the default. A missing value leaves the default in place, and
a value longer than the buffer most likely does too. A shorter value
overwrites only the first bytes, so a 1-byte "Disable Mic Preamp" of 00
turns the default 1 into 0.

ES1869.DRV also writes to the key. Routine 3:0D54 saves the mixer state
(section 2.3) as 4-byte REG_BINARY values when the last of the driver's
devices is disabled (DRVM_DISABLE, 3:4D2C), which most likely happens at
shutdown, and before an APM suspend (3:4C8A).

## 1. ES1869.VXD settings

The VxD reads all of these values once, when the device starts (o5:0000,
called at o5:060E). It reads a byte value with a length of 1 and a dword
with a length of 4, and it puts the resulting flags in the device structure
(the ADI), with the low word at offset 04h and the high word at 13h.

The parentheses after each value give its size and the values that have an
effect, the flag it sets and the confidence:
* **Disable Warning** (nonzero byte, flag 0001h, verified).
  Show_Contention_Message (o5:0F0F) returns early, so no "device in use"
  message appears when a DOS program touches the card while Windows owns it.
  ES1869.DRV reads this value too (section 2.2).
* **Multiple FM Support** (nonzero byte, flag 0002h, verified in code,
  purpose likely). At device start, the VxD reports `midi\esfm.drv` as
  present to MMDEVLDR (MMDEVLDR_SetDevicePresence, o5:064B) even when no FM
  port was assigned. Without the flag, it does so only when there is an FM
  port. With the flag clear and no FM alias base (ADI 0Ah = FFFFh), the VxD
  also sets the FM port to "none" (ADI 08h := FFFFh, o4:0879).
* **Do Not Want ES689** (nonzero byte, flag 0008h, verified). The flag keeps
  the ES689/ES69x serial interface off. o5:1EAD sets mixer 48h bit 4 at
  start if nobody owns FM (o5:1C8C) and whenever FM is released (o5:10AB),
  and it clears the bit when Windows' system VM acquires FM (o5:121F). With
  the flag, the bit stays 0. The flag also skips o5:20AE (called from
  o3:0120), which sends FFh (reset), 3Fh (UART mode) and the SysEx F0 00 00
  7B 07 F7 to the MPU-401 port.
* **DOS MPU-401 Interrupts** (nonzero byte, flag 0400h, verified). When a
  DOS program acquires the DSP while the MPU-401 shares the audio interrupt
  (flag 2000h), Acquire_Resources masks the MPU-401 interrupt by clearing
  mixer 64h bit 6 (o5:11DA, o1:09B8). With the flag, the MPU-401 interrupt
  stays on for the DOS program.
* **Single Mode DMA** (nonzero dword, clears flag 10000h, verified). Flag
  10000h is set by default and selects demand-mode DMA for the VxD's own
  Audio 2 path: VDMAD mode 18h instead of 58h (o5:32F2), and mixer 78h = 93h
  instead of 13h (o5:360C). ES1869.DRV reads the value as well (section
  2.2).
* **HwVolume 2-Wire Mode** (dword 1-3, flag 2000000h, verified). The value,
  or 0 if it is above 3, is shifted left by 2 into a byte that o5:1EDB
  writes to mixer 64h bits 3:2 (the hardware volume button mode) at device
  start (o4:086E).
* **HwVolume Count By 3** (nonzero dword, no flag, verified). The value sets
  bit 5 of the same byte, which becomes mixer 64h bit 5 (count by 3).
  o5:1EDB only ORs the bit in and never clears it. ES1869.DRV reads this
  value too (section 2.2).
* **Disable Mic Gain** (nonzero byte, flag 4000000h, verified). At device
  start (o4:08AE), o5:1CE7 clears mixer 7Dh bit 3, the +26 dB mic preamp.
  ES1869.DRV rewrites 7Dh later (see "Disable Mic Preamp" in section 2.2).
* **Want Local Powerdown** (nonzero dword, flag 20000h, verified). When no
  VM owns the DSP, FM or MPU-401, the VxD powers the digital section down
  (Audio_Base+7 bits 2 and 3, o5:2122).

## 2. ES1869.DRV settings

ES1869.DRV reads 97 value names: 16 configuration values and 8 values in
`Config\GPO Selections` (section 2.2), 59 mixer-state values (section 2.3)
and 14 volume maps (section 2.4).

### 2.1 When the values are read

All four message entry points, for the mixer, wave input, wave output and
aux, pass DRVM_INIT, DRVM_ENABLE and DRVM_DISABLE to the same shared
routines. These routines count the calls, so the work is done only once for
each device.
* **DRVM_INIT** (for example 5:1D15). Routine 3:43E2 reads Disable Warning.
* **DRVM_ENABLE for the first device** (3:4940). Routine 3:3DEC, called at
  3:4B56, reads the configuration values and GPO0/GPO1 Show.
* **DRVM_ENABLE and APM resume** (3:4BB5 and 3:4CB9). Routine 3:4818, which
  calls 6:1F92 and 3:18EC, reads Telegaming Vol, then the mixer state, the
  volume maps and the other GPO values.
* **DRV_CONFIGURE** (the "Use single mode DMA" check box). The code at
  3:4550, 3:44A8 and 3:450E reads and writes Single Mode DMA.

### 2.2 Configuration values

The parentheses after each value give the address of its `RegQueryValueEx`
call, its size and default, and the confidence:
* **Disable Warning** (read at 3:447A, 1 byte, default 0, verified). A
  nonzero value sets DS:00C4 bit 0, which suppresses the driver's error
  message boxes (3:0A8C, 3:0AEC): "The ESS AudioDrive hardware is not
  responding properly..." (6:0000), "The ES1869.VXD driver is not
  present..." and "The version of ES1869.VXD is out of date..." (3:0B58).
* **DCdrift** (read at 3:3E8F, dword, default 1, verified). The driver
  removes the DC offset from recorded data in software: the mean of the
  first recorded block (1:1CC3) is subtracted from the samples that follow
  (1:1D0B). It is forced off when the Audio 1 DMA channel is above 3. The
  private widMessage 4488h reads or sets the setting at run time (6:11FA).
  Its dwParam1 points to two dwords: a nonzero first dword reads the setting
  into the second, and a zero one sets the setting from the second. The
  driver answers only the program that has the device open, and only before
  it starts recording. A recording takes the setting when the device is
  opened (6:1FF4), so a change applies from the next open. esfmrec turns
  DC-drift removal off this way. No register is written.
* **AGC** (read at 3:3EE1, dword, default 1, likely). While DC-drift
  correction is active, an 8-bit recording is captured as 16-bit and
  converted by an adaptive gain stage (flags 4001h set at 6:2050, the
  conversion at 1:1D6C). It is forced off when the Audio 1 DMA channel is
  above 3. No register is written.
* **Disable Mic Preamp** (read at 3:3F33, dword, default **1**, verified).
  The value sets DS:00C7. The driver sets mixer 7Dh bit 3 (+26 dB) when the
  value is 0 and clears it otherwise, at device enable (3:4884), at every
  record start (5:38D2) and when Phone Select is muted (5:2EA6, 5:2EC0).
  With Phone Select unmuted, the driver clears bit 3 regardless (5:3060). A
  missing value leaves the preamp off, and the INF writes a single byte of
  00, which turns it on.
* **DiscardBlock** (read at 3:3F80, dword, default 1, likely). The driver
  discards the first DMA block of each recording (the interrupt code at
  1:1A28, the position correction at 6:01FF). No register is written.
* **HwVolumeStep** (read at 3:3FC8, dword 1-4, default 1, verified). The
  value is the number of HwVolumeMap entries that the master volume moves
  for each hardware volume button event (callback 5:20DE, 5:21AA). Values
  out of range become 1.
* **HwVolume Count By 3** (read at 3:402B, dword, default 0, verified). A
  value of exactly 1 forces the step to 3, and ES1869.DRV ignores other
  nonzero values. The VxD sets mixer 64h bit 5 separately (section 1).
* **HwVolumeMap** (read at 3:4076, 64 bytes of 00-3Fh, default in the table
  at 7:0950, verified). The driver uses it only if exactly 64 bytes are
  read. It maps the 6-bit master level to mixer 60h/62h bits 5:0, both for
  the Volume Control slider (5:2A8C) and for the hardware buttons (callback
  5:20DE, sync routine 5:3BAC). The default is 00 02 04 06 08 0C ... 3F.
* **Single Mode DMA** (read at 3:40D7, dword, default 0, verified). A value
  of 0 sets flag 2Ah bit 15, which means demand transfers: 8237 demand mode
  for recording (6:27B0, 6:27F0) and playback (6:2C8C, 6:2CD5), controller
  B9h = 02h for Audio 1 (6:28BF), and mixer 78h = 93h instead of 13h for
  Audio 2 (6:2DFA). A nonzero value gives single transfers and leaves B9h as
  the DSP reset set it.
* **Enable AUXB** (read at 3:411F, dword, default 1, verified). With 0, the
  AuxB source lines are reported to mixer programs as disconnected (5:02F2),
  but the AuxB registers are still written. This affects only the user
  interface.
* **Enable ES938** (read at 3:41A1, dword, on by default, verified). A
  nonzero or missing value turns on the ES1869's own Spatializer 3-D: the
  driver initializes 50h-5Ah at enable (3:48AC) and shows the Spatializer
  Enable and 3D Effect controls. Only an explicit 0 turns it off, and the
  driver then never writes 50h or 52h. The ES938 is an external chip, so the
  name is historical.
* **3D Limit** (read at 3:41F9, dword, default 0, verified). The value sets
  mixer 50h bit 0, which the data sheet doesn't document (3:48D7, 5:3B55).
  essctl shows it as `fx.3d.limit`, and `essreg 3l=` and `ess3d limit` set
  it.
* **Enable IIS** (read at 3:4247, dword, default 1, verified). With 0, the
  IIS line is reported as disconnected (5:0322). This affects only the user
  interface.
* **Enable Software 3D Effect** (read at 3:4293, dword, default 0,
  verified). The driver uses it only if Enable ES938 is 0. Stereo wave
  output then gets a software 3-D effect in the buffer copy (1:0919,
  1:09AB), which the Spatializer Enable and 3D Effect controls drive. No
  register is written.
* **ESSWaveTableChip** (read at 3:42DF, dword, default 0, verified). A
  nonzero value sets flag 2Bh bit 6, and the driver then never sets mixer
  7Fh bit 0, the bit that gives the music DAC to I2S. It skips all four
  places that set it: device enable (3:4BB9), resume (3:4CE3), the FM
  driver's close (5:2022) and the release in the MPU-401 notification
  (3:4FA8). It still clears the bit when FM opens. The IIS line is reported
  as disconnected (5:0383), and its volume is neither restored (3:2098) nor
  saved (3:10FA). The Synth volume goes to 36h only while dev+117h is 1
  (5:254A, 5:2B79), which the first FM open sets, so until then 36h keeps
  its reset value. `essctl /i2s=off` sets this value.
* **Telegaming Vol** (read at 6:1FD6 through CONFIGMG, 1 byte REG_BINARY, no
  default, verified except for the length, which is likely). The value is
  written to mixer 14h, the Audio 1 play volume, right after the mixer reset
  at enable and resume (6:1FE8). If the read fails, 14h keeps its reset
  value. A 4-byte value probably fails the 1-byte read.
* **GPO0 Show, GPO1 Show** (read from `GPO Selections` at 3:436C and 3:43B2,
  dword, default 0, verified). With a nonzero value, the GPO0 or GPO1 switch
  appears in the mixer. Otherwise the control is disabled and hidden
  (5:033D, 5:0360).
* **GPO0 Default, GPO1 Default** (read at 3:3AFD and 3:3C9D, dword, by
  default the current pin level, verified). If the value is present, or if
  Show is set, the driver sets the pin, Audio_Base+7 bit 0 or 1, through VxD
  function 0005 (5:39C0). GPO1 is inverted on Compaq devices. The value is
  saved at shutdown if Show is set (3:1874).
* **GPO0/GPO1 LongLabel, Label** (read at 3:3BBA, 3:3BF7 and 3:3C27 for GPO0
  and at 3:3D5A, 3:3D97 and 3:3DC7 for GPO1, strings, verified). The driver
  uses them only with Show. LongLabel, or Label if LongLabel is missing,
  becomes the control's long name, and Label becomes its short name. The
  size is 64 bytes for the long name and 16 for the short name.

The driver treats a device as a Compaq when its ID contains CPQB023,
CPQB0AB, CPQB0AC or CPQB0AD (CONFIGMG function 07h, 3:0B92-3:0CA0). On a
Compaq, it passes Treble and Bass to `SetEqzrCtrls` in CPQVAPI.DLL (5:0000,
5:3A74), except on CPQB0AB and CPQB0AC, where it hides these controls. The
ES1869 has no register for treble and bass.

### 2.3 Mixer state

These values hold the state of ES1869.DRV's mixer controls. The driver
restores them at every enable and resume (3:18EC) through its own
set-control-details path (5:17CA), and saves them back when the device is
disabled and at suspend (3:0D54). The number after a control's name is its
control ID. A volume is the 16-bit mixer value (0-FFFFh) in the low word of
a dword, and a pair such as Left/RightMasterVol has one value for each
channel.

The driver converts the values into register settings as follows:
* A 4-bit register gets the nibble map[value >> 12], with the left channel
  in bits 7:4 and the right channel in bits 3:0. The map is the matching
  volume map from section 2.4.
* The master volume sets 60h/62h bits 5:0 to HwVolumeMap[value >> 10], and
  bit 6 holds Master Mute.
* The PC speaker gets 3Ch = (value x max(master left, master right) /
  FFFFh) >> 13, or 0 while the master is muted.
* A muted or deselected source gets 00h in its register.
* In a bitmask, bit i stands for source i of the destination, in the order
  listed after the values.

The parentheses after each value give the address where it is read and its
default, and every entry is verified:
* **Mute** (read at 3:1998, default 0). Master Mute (33) sets 60h/62h bit 6
  and forces 3Ch to 0.
* **MutesOut** (read at 3:1A20, default 04h, the Mic bit). The source mutes
  (25-32) set playback source i's register to 00h when bit i is set.
* **Mixer:Output** (read at 3:1AF7, default 7Bh). Master Output Sources (0)
  sets playback source i's register to 00h when bit i is clear.
* **Left/RightMasterVol** (read at 3:1BD3, default 8000h). Master Volume
  (11) sets 60h/62h bits 5:0 through HwVolumeMap, and it also rescales 3Ch.
* **Left/RightLineInVol** (read at 3:1C8A, default 8000h). Line-In Volume
  (3) sets 3Eh.
* **Left/RightDACVol** (read at 3:1D41, default 8000h). Wave Output Volume
  (4) sets 7Ch, the Audio 2 DAC volume. The driver also keeps the byte at
  dev+124h and writes it to 7Ch at every playback start (6:2D6D) without the
  WaveVolumeOutMap.
* **Left/RightMicVol** (read at 3:1DF8, default 8000h). Microphone Volume
  (5) sets 1Ah.
* **Left/RightCDAudioVol** (read at 3:1EAF, default 8000h). CD Audio Volume
  (6) sets 38h.
* **Left/RightSynthVol** (read at 3:1F66, default 8000h). FM Synthesis
  Volume (7) is passed to the FM driver's callback (5:2467). It is written
  to 36h only while the music DAC belongs to FM or an ES689 (dev+117h = 1,
  see 7Fh in section 4).
* **Left/RightAuxBVol** (read at 3:201D, default 8000h). AuxB Volume (8)
  sets 3Ah.
* **Left/RightIISVol** (read at 3:20DD, default 8000h). IIS Volume (9) sets
  36h while dev+117h = 0, which is the normal case while no FM client is
  open.
* **PCspeakerVol** (read at 3:218F, default 8000h). PC Speaker Volume (10),
  which is mono, sets 3Ch bits 2:0, as described above.
* **MonitorWave** (read at 3:2215, default 0). Recording Input Monitor (37)
  sets controller A8h bit 3, at each record start (6:2A3C) and while
  recording (5:38F4).
* **Mixer:Wave** (read at 3:229D, default 02h, the Mic bit). Recording Input
  Sources (1) sets 1Ch bits 2:0 to 5 at record start (record mixer, 5:35AC),
  and it gives the selected sources their record volume and the others 00h
  (5:365B). Bit 1 (Mic) also drives the Phone destination's Mic Select.
* **Left/RightWaveMasterVol** (read at 3:23FD, default 8000h). Mixer Input
  Level (18) sets controller B4h at record start, with the nibbles swapped:
  right in 7:4, left in 3:0 (5:297A).
* **Left/RightWaveLineVol** (read at 3:24B4, default 8000h). Line-In Input
  Level (12) sets 6Eh at record start.
* **Left/RightWaveMicVol** (read at 3:256B, default 8000h). Microphone Input
  Level (13), which is linked to Phone "Mic Vol" (47), is written to 68h at
  once, and 68h gets 00h if Mic is not a recording source.
* **Left/RightWaveCDAudioVol** (read at 3:266E, default 8000h). CD Audio
  Input Level (14) sets 6Ah at record start.
* **Left/RightWaveAuxBVol** (read at 3:2725, default 8000h). Control 15, on
  the AuxB line, sets 6Ch (AuxB record) at record start. The control is
  named "FM Synthesis Input Level" but belongs to the AuxB line.
* **Left/RightWaveSynthVol** (read at 3:27DC, default 8000h). Control 16, on
  the Synthesizer line, sets 6Bh (music DAC record) at record start. The
  control is named "AuxB Input Level".
* **Left/RightWaveDACVol** (read at 3:2893, default 8000h). Wave Input Level
  (17) sets 69h (Audio 2 record) at record start.
* **MonitorVoice, Mixer:Voice, Left/RightVoiceMasterVol,
  Left/RightVoiceLineVol, ...VoiceMicVol, ...VoiceCDAudioVol,
  ...VoiceAuxBVol, ...VoiceDACVol** (read at 3:2943-3:2E71, defaults 0, 02h
  and 8000h). These values belong to the "Voice Commands" destination (38,
  2, 24, 19-23). They drive the same registers as the Wave* set (A8h bit 3,
  1Ch, B4h, 6Eh, 68h, 6Ah, 6Ch, 69h), and the driver uses them when the wave
  input instance marked by the private widMessage 4093h records.
* **Left/RightMonoInPhone** (read at 3:2F00, default 8000h). Phone Vol (48)
  sets 6Dh and 6Fh, both to the same value, without a map (5:2B38, 5:2B4A).
* **MonoInPhoneMute** (read at 3:2FBE, default not muted). Only bit 0 of the
  value counts. When Phone Select (46) is muted, the driver sets 6Dh = 6Fh =
  00h and 7Dh bits 2:1 = 00 (MONO_OUT off), and 7Dh bit 3 comes from Disable
  Mic Preamp. When it is unmuted, 6Dh/6Fh come from Phone Vol, 7Dh bits
  2:1 = 11 (record mono mix), and bit 3 = 0.
* **3D Effect Enable**, falling back to SpatializerEnable (read at 3:3063,
  3:33BE and 3:34D4, default 1, or 0 for software 3-D). Spatializer Enable
  (41) sets 50h to 0Ch (on) or 04h (off), with bit 0 = 3D Limit (5:3B50).
* **3D Effect**, falling back to SpatializerEffect (read at 3:30FD, 3:32D0,
  3:331A and 3:343C, default FFFFFFFFh, or BFFFBFFFh for software 3-D). 3D
  Effect (42) sets 52h to the low word >> 10 (5:3B90). The high word is the
  right channel and is ignored.
* **Treble, Bass** (read at 3:319D and 3:3237, default 7FFF7FFFh). Treble
  (43) and Bass (44) work only on a Compaq, through CPQVAPI.DLL, and write
  no register.

The bits of the source bitmasks stand for these sources:
* Playback (Mixer:Output and MutesOut): bit 0 Line-In, 1 Wave, 2 Microphone,
  3 CD Audio, 4 Synthesizer, 5 AuxB, 6 IIS, 7 PC Speaker.
* Recording (Mixer:Wave): 0 Line-In, 1 Microphone, 2 CD Audio, 3 AuxB, 4
  Synthesizer, 5 Wave.
* Voice (Mixer:Voice): 0 Line-In, 1 Microphone, 2 CD Audio, 3 AuxB, 4 Wave.

The INF writes "Mixer:Output" as 5Bh, which turns off Mic, AuxB and the PC
speaker, and "MutesOut" as 44h, which mutes Mic and IIS.

The driver's set-control-details rejects disabled controls (5:08B0), so the
3-D values take effect only if their controls exist, which is the case with
Enable ES938 (the default) or Enable Software 3D Effect. Treble and Bass
exist only on a Compaq that has CPQVAPI.DLL.

### 2.4 Volume maps

Each map must be exactly 16 bytes long (checked at 3:356D and in similar
places). Entry n is the 4-bit register value for slider step n. The entries
must be 00-0Fh: the driver ORs the right channel's entry in without a mask,
so a larger value would corrupt the left nibble (for example at 5:2697). The
defaults at 7:0990-7:0A6F are the identity, 00, 01 ... 0F. The maps apply
only in the set-control-details path (see Left/RightDACVol in section 2.3).

| Map                 | Table  | Used for    |
|---------------------|--------|-------------|
| LineInVolumeOutMap  | 7:0A00 | 3Eh         |
| WaveVolumeOutMap    | 7:0A10 | 7Ch         |
| MicVolumeOutMap     | 7:0A20 | 1Ah         |
| CDAudioVolumeOutMap | 7:0A30 | 38h         |
| SynthVolumeOutMap   | 7:0A40 | 36h (Synth) |
| AuxBVolumeOutMap    | 7:0A50 | 3Ah         |
| IISVolumeOutMap     | 7:0A60 | 36h (IIS)   |
| LineInVolumeInMap   | 7:0990 | 6Eh         |
| WaveVolumeInMap     | 7:09A0 | 69h         |
| MicVolumeInMap      | 7:09B0 | 68h         |
| CDAudioVolumeInMap  | 7:09C0 | 6Ah         |
| SynthVolumeInMap    | 7:09D0 | 6Bh         |
| AuxBVolumeInMap     | 7:09E0 | 6Ch         |
| IISVolumeInMap      | 7:09F0 | none        |

IISVolumeInMap is read but never used, because there is no IIS record
source.

### 2.5 Names nobody reads

ES1869.DRV contains the value names StartupMuteMsg, MIDIInPersistence,
MonoInMicMute, LeftMonoInMic, RightMonoInMic and Do Not Want ES938, but its
code never refers to them. OEMSETUP.INF and `ess_windows_regs.txt` also have
a "Telegaming" value, which neither driver reads. Of the two telegaming
values, only "Telegaming Vol" is read, and nothing in either driver sets
telegaming mode (mixer 48h bit 1).

ESSDC.EXE, the "DC Drift daemon", is a 16-bit Windows program that the INF
starts from the Run key. It uses no registry value names. It checks once
whether mixer 64h bit 5 accepts a write, and puts the bit back. Every 10
minutes, beginning 10 seconds after it starts, it makes a short recording
through ES1869.DRV (2 or 8 KB, 16-bit stereo) and writes the ADC offset
registers, controller BAh and BBh, with DSP commands (`adc.off_l` and
`adc.off_r` in essctl).

## 3. VxD calls made by ES1869.DRV

ES1869.DRV stores the AUDDRV entry point (INT 2Fh AX=1684h BX=3B07h) at
DS:0010 (3:0091), and it passes the devnode in ECX. Each function below goes
by its number in DX, and the parentheses give the address of the far call
through that entry point and the wrapper, which is the routine that contains
the call:
* **0000** (call 3:00CC, wrapper 3:00C4). DRV_ENABLE (3:5013) reads the
  version. Without the VxD, the driver sets error 2, and with a version
  below 0404h, error 3. The message box appears at DRVM_INIT unless Disable
  Warning is set.
* **0001** (call 3:0109, wrapper 3:00D9 with AX=1 and C9h bytes).
  DRVM_ENABLE (3:4971) copies the ADI: Audio_Base, the MPU-401 port and IRQ,
  the audio IRQ, both DMA channels and the no-DMA flag.
* **0002** (BX=1, call 4:0010, wrapper 4:0000). This acquires the DSP,
  counted in dev+2Eh. It is called from wave output open 4:0054 (6:15AF in
  WODM_OPEN), wave input open 4:00E0 (6:059E, 6:0807), enable and resume
  (3:482B, 3:4B14, 3:4BC6, 3:4CC4), disable (3:4E16), every mixer register
  change (5:2A5D, 5:2984, 5:2E66, 5:303F, 5:35E8, 5:38A5, 5:3936, 5:3B10),
  and the FM and MPU-401 notifications (5:1F7A, 5:2052, 3:4F59, 3:4FB5).
* **0003** (BX=1, call 4:0041, wrapper 4:0023). This releases the DSP. It is
  called from the matching releases: wave output close 4:009F (6:1829), wave
  input close 4:011A (6:0DF1) and the error paths, and after each mixer
  change. The call is made only when the count drops to 0, so a mixer change
  while no wave device is open costs a VxD acquire and release, and the
  release resets the DSP.
* **0004** (call 1:1AA4, wrapper 1:1A94). The code at 1:10EC (BX=0,
  recording) and 1:1118 (BX=1, playback) reads the DMA position of the
  running transfer.
* **0005** (call 4:015E, wrapper 4:014A). This handles the GPO pins. The
  restore reads them (3:3AA0, 3:3C40), the GPO controls read them (5:39DC)
  and write them (5:3A64), and the private auxMessage 4687h (6:1E65) passes
  the caller's values.
* **0006** (call 4:0199, wrapper 4:0187). This sets the hardware volume
  callback, which is 5:20DE at enable (3:4B9B) and 0 at disable (3:4DD2).
* **0007** (call 1:1AC6, wrapper 1:1AB1). This handles the PIO emulation
  buffer. Record start 6:2791 and record stop 1:16B1 call it, in the no-DMA
  mode only.
* **000A** (call 4:01B9, wrapper 4:01A7). Enable (3:4BAE) calls it with
  EAX = a 16:16 pointer to 3:4C3C, and disable (3:4DE1) with EAX = 0. The
  driver passes 3:4C3C, which marks the DSP busy or free, as if to register
  it. In this VxD, 000Ah only returns a flag byte and ignores EAX, so
  nothing is registered.
* **0009** (call 3:00D4, wrapper 3:00D1). Nothing calls the wrapper, so it
  is dead code.
* **0200** (call 3:478C, wrapper 3:4758). At enable, the driver registers
  the notification clients: 3:4B7F ("ESSFMMXD", callback 5:1D6E) and 3:4C23
  ("ESMPUISR", callback 3:4EE6, only with an MPU-401 port and no MPU-401
  IRQ). The FM driver uses ESSFMMXD to take over the Synth volume and the
  music DAC.
* **0201** (call 3:47BD, wrapper 3:4794). At disable (3:4DB8, 3:4DFF), the
  driver unregisters them.

## 4. Registers ES1869.DRV writes

ES1869.DRV writes a mixer register through 1:12B2, which puts the index in
Audio_Base+4h and the data in +5h. It writes a controller register with DSP
commands, sending the register and then the value. For a read-modify-write,
it first reads the register back by sending C0h and the register number,
then reading the value. A register tool should expect Windows to overwrite
every register in the lists below, and the parentheses after each register
give the addresses of the writes.

At every device enable and APM resume, routine 3:4818 and then the
mixer-state restore write these registers:
* **mixer 00h** (6:1FA1). The driver writes 00h, the mixer reset, which
  returns the mixer registers to their reset values. According to the data
  sheet, the record volumes are reset only by a hardware reset.
* **14h** (6:1FE8). The driver writes Telegaming Vol, only if the value
  exists.
* **50h, 52h, 54h, 56h, 58h, 5Ah** (3:48C0-3:4936). The driver writes these
  only with Enable ES938. It sets 50h to 00h and then to 0Ch with bit 0 = 3D
  Limit, 52h to 3Fh, and 54h-5Ah to 8Fh, 95h, 94h and 80h (undocumented,
  `fx.3d.reg54` to `fx.3d.reg5a` in essctl).
* **7Dh** (3:485B, 3:4884). The driver writes 06h (MONO_OUT = record mono
  mix, MONO_IN direct off), then sets bit 3 from Disable Mic Preamp.
* **1Ch** (3:4890). The driver writes 05h (record mixer, record mute off).
* **From the restore** (3:18EC). The mixer-state restore writes 60h, 62h,
  3Eh, 7Ch, 1Ah, 38h, 36h, 3Ah, 3Ch, 68h, 6Dh, 6Fh, 7Dh bits 3:1, 50h, 52h
  and Audio_Base+7 bits 1:0.
* **7Fh bit 0** (3:4BDD at enable, 3:4CDA-3:4CF7 at resume). The driver sets
  it unless ESSWaveTableChip is set. At resume, it clears the bit instead if
  FM or an ES689 had the music DAC (dev+117h).

Through the mixer API, MXDM_SETCONTROLDETAILS (5:17CA) and the per-control
handlers at 5:2BA1 write the registers below. Every change acquires the DSP
before the write and releases it afterwards.
* **3Eh, 7Ch, 1Ah, 38h, 36h, 3Ah, 3Ch** (5:2B8C). The playback volumes and
  mutes and Master Output Sources write them. 36h gets 00h when muted, at
  5:256E, and 3Ch is also written at 5:265E.
* **60h, 62h** (5:2AAC, 5:2B8C). Master Volume and Master Mute write them.
* **68h, 69h, 6Ah, 6Bh, 6Ch, 6Eh** (5:28D0 for 68h, 5:2B8C). The record
  levels write them, 68h at once and the others only while recording.
* **6Dh, 6Fh** (5:2B38, 5:2B4A). Phone Vol and Phone Select write them.
* **1Ch bits 2:0** (5:3628). The driver writes 5 while recording.
* **7Dh bits 3:1** (5:2E85, 5:2EA6, 5:2EC0, 5:3060, 5:38D2). Phone Select
  and the record start write them.
* **50h, 52h** (5:3B50, 5:3B61-5:3B90). Spatializer Enable and 3D Effect
  write them.
* **controller B4h** (5:29B3 with C6h, 5:29BE, 5:29D1). Mixer Input Level
  writes it, with the nibbles swapped.
* **controller A8h bit 3** (5:3951-5:399C). The record monitor writes it
  while recording.
* **Audio_Base+7 bits 1:0** (through VxD function 0005). GPO0 and GPO1 write
  them.
* **7Fh bit 0** (cleared at 5:1FA3, set at 5:206D). The FM driver opening or
  closing writes it (ESSFMMXD messages), and so do 3:4F7F and 3:4FD8
  (ESMPUISR). 3:4E39 clears it at disable.

At every playback start, 5:3BAC (called at 6:2D5A) reads 60h and 62h back.
If the hardware buttons or a DOS program changed them, it updates the Master
Volume and Mute controls, which rewrites 60h and 62h.

During playback on Audio 2, the driver writes these registers:
* **71h** (1:115E, 6:2DEE). The driver sets bits 4 and 1 (4x oversampling,
  asynchronous) and keeps the other bits. At both places, `build/ES1869.DRV`
  reads 71h through `a2_mode_read` (1:1157, 6:2DE6), which by default clears
  bit 4 and sets bit 3, for no oversampling and the filter bypassed
  ([AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter),
  section 6.2).
* **70h, 72h, 74h, 76h, 78h** (1:1174-1:1192). The driver writes 00h to them
  at wave output open, close and resume (the list "prtvx" at 7:00A8), then
  sets 70h = 72h = FFh.
* **70h, 72h** (6:2599, 6:25D0). The driver writes the sample rate and
  filter.
* **7Ch** (6:2D6D). The driver writes the wave volume at the start.
* **DSP D3h** (6:2D78). The command turns the speaker off, which switches
  off Audio 1's path through 14h.
* **74h, 76h** (6:2D98, 6:2DA7). They get minus the block length.
* **7Ah** (6:2DD7). The driver writes 40h, plus 05h for 16-bit signed and
  02h for stereo.
* **78h** (6:2E0C, 6:2E69). The driver writes 13h (single) or 93h (demand 4)
  at the start, and 00h at a pause.
* **78h, 7Ah, 7Ch** (1:16FE, 1:171B, 1:1739, 1:1744). At the stop, the
  driver clears 78h bit 4 and then writes 00h to it, clears 7Ah bit 7, and
  sets **7Ch = 00h**, so the Wave volume reads 00h while nothing plays.
* **7Ah bit 7** (3:0300). The interrupt handler clears it, and it saves and
  restores the mixer index (3:02EF, 3:0307).

For recording on Audio 1, ES1869.DRV also resets the DSP itself when it
takes the DSP, at 6:05A8, 6:0811, 6:0D5E and 6:0DEB. The reset routine
(1:11A0) writes 03h and then 00h to Audio_Base+6 and then sends command C6h.
This reset clears all the controller registers. During recording, the driver
writes these registers:
* **71h bit 5** (6:22D3-6:22EA). The driver sets it only for 48000 Hz.
* **A1h, A2h** (6:22F3, 6:236C). The driver writes the sample rate and
  filter, or instead the DSP 40h time constant for some formats (6:214B).
* **Audio_Base+6** (6:28AC, 1:169B). The driver writes 02h, then 00h, a FIFO
  reset, at the start and at the stop.
* **B9h** (6:28BF). The driver writes 02h (demand 2), only without Single
  Mode DMA.
* **A4h, A5h** (6:28DE, 6:28F5). They get minus the block length.
* **A8h** (6:291D, 6:2A9C). The driver writes F5h (stereo) or F6h (mono),
  then sets bit 3 to the record monitor. Bits 7:5 and 2 are written as 1,
  although the data sheet says to write 0 (`a1.analog.bits7_5` and
  `a1.analog.bit2` in essctl).
* **B1h, B2h** (6:2934-6:2963, 6:296E-6:299D). The driver ORs them with 50h.
* **B7h** (6:29C1). The driver writes 90h, or 98h for stereo, plus 24h for
  16-bit.
* **B8h** (6:29D8-6:2A09). The driver writes B8h & 30h, ORed with 0Fh (ADC,
  auto-initialize, DMA read, enable).
* **1Ch, 68h, 69h, 6Ah, 6Bh, 6Ch, 6Eh, B4h, 7Dh bit 3** (5:35AC, 5:365B).
  The driver writes the record source and record mix, as section 2.3
  describes.
* **B8h** (1:1626-1:1690). At the stop, the driver clears bit 2 and then
  writes B8h & 30h, or sends DSP D0h for some formats.

Apart from the mixer reset, ES1869.DRV never writes the SB Pro views between
04h and 2Eh, 32h, 42h-4Eh, 64h, 65h, 66h, BAh, BBh, 7Fh bits 7:1 or
Audio_Base+7 bits 7:2. The VxD writes some of them: 48h bit 4, and 64h bits
5 and 3:2 at start and bits 1:0 for the hardware volume interrupt.

## 5. How to change a setting

Put the value in the device's software key,
`HKLM\System\CurrentControlSet\Services\Class\Media\<nnnn>\Config`, where
the ES1869's instance takes the place of `<nnnn>`. That instance is the one
whose `Driver` value is `es1869.vxd`. GPO values go in the key's subkey
`GPO Selections`.

Use binary values, as OEMSETUP.INF's `HKR,Config,"name",01,...` lines do
(flag 01 is REG_BINARY), because the CONFIGMG reads ask for REG_BINARY: all
of the VxD's reads, and ES1869.DRV's read of "Telegaming Vol". Each kind of
value has its own form:
* A dword is 4 bytes, low byte first (`01,00,00,00`).
* A byte value should be exactly one byte. The byte values are Disable
  Warning, Multiple FM Support, Do Not Want ES689, DOS MPU-401 Interrupts,
  Disable Mic Gain and Telegaming Vol.
* A map must have exactly 16 bytes, and HwVolumeMap exactly 64.
* The GPO labels are strings (`HKR,"Config\GPO Selections","GPO0
  Label",,"text"`).

The settings take effect when Windows starts again, because the VxD and
ES1869.DRV read them only when the device starts.

ES1869.DRV overwrites the mixer-state values of section 2.3 and GPO0/GPO1
Default when it disables the device, which most likely happens at shutdown,
and at suspend. Changes you make to them in the registry while Windows runs
are therefore lost. Set them with the Volume Control instead, or edit them
while the driver isn't running, for example in Safe Mode. The values of
sections 1 and 2.2 are only read, never written, except Single Mode DMA,
which the driver's own Settings dialog writes (its "Use single mode DMA"
check box, at Control Panel > *Multimedia* > *Advanced* > the ES1869 audio
device > *Properties* > *Settings*).

A setting made with essctl or essreg is undone by the mixer reset at the
next start or resume, and by the writes in section 4. An `essctl /load` in
the StartUp group runs after the driver's start-up writes, but the playback,
recording and mixer writes of section 4 still happen after it.

## 6. The rebuilt drivers' SYSTEM.INI settings

The drivers in `build/` change ESS's drivers in the ways that this
repository describes. Each change has a key in `SYSTEM.INI`
(`C:\WINDOWS\SYSTEM.INI`), and a key set to `0` gives back what ESS's driver
does in that place.

Each driver reads its section once, when it starts:
* `ES1869.VXD` reads it when Windows loads the VxD
  (`Sys_Dynamic_Device_Init`).
* `ES1869.DRV` reads it at its first enable, right after it reads ESS's
  registry values.
* `ESFM.DRV` reads it at its first `DRV_ENABLE`.

A change therefore takes effect when Windows starts again. `ESFM.DRV`'s
`Bank=` is the exception, because it isn't a switch: it names a bank file,
and the driver reads the key again every time a program opens the MIDI
device ([ESFM_BANK.md](ESFM_BANK.md#bank-file-buildesfmdrv)).

The VxD can read SYSTEM.INI only while Windows starts, because VMM's profile
services are gone afterwards. A VxD that is loaded later, for a card found
while Windows runs, keeps the defaults. essctl's *Device information* page
tells you which applies: "VxD settings: from SYSTEM.INI" or "the defaults".

When the section or a key is missing, the default applies. A value is read
the way `GetPrivateProfileInt` reads it, as its leading digits, or 0 if
there are none. Write `1` or `0`, because `yes` and `on` read as 0.

For example, these settings give ESS's DOS box behavior, ESS's wave devices
and ESS's modes of both DACs, while keeping the register API:

```
[ES1869.VXD]
VirtualFM=0
DosTakesFM=0
DosKeepsFM=0
DosFMAudible=0
DosMixerRestore=0
ResetDosFM=0
RecordTakesDSP=0

[ES1869.DRV]
Audio1Device=0
SharedWaveOut=0
FMRecordDevice=0
Audio2Oversampling=1
Audio2Filter=1
Audio1Filter=1
```

### 6.1 [ES1869.VXD]

[VXD_INTERNALS.md](VXD_INTERNALS.md#dos-boxes) describes the DOS box
changes. Each key below is followed by its default and by what the key does
at 1, and the sentence that begins "With 0" says what happens at 0, as in
ESS's driver:
* **RegisterAPI** (default 1). The VxD offers the register API, functions
  0400-040D ([VXD_API.md](VXD_API.md)), which essctl, `ess3d`, `esfmrec` and
  ES1869.DRV's FM recording device use. With 0, group 4 fails with CF set,
  and the programs go through the ports as they do with ESS's driver
  ([VXD_API.md](VXD_API.md#ownership-and-port-trapping)).
* **VirtualFM** (default 1). A DOS program that can't have the FM chip gets
  a virtual one, so its FM detection succeeds. With 0, every FM read returns
  FFh while Windows' MIDI or another DOS box has FM, and ESS's "in use"
  message appears.
* **DosTakesFM** (default 1). Windows keeps the FM chip it got from a port
  access only until a DOS program wants it. With 0, Windows keeps it until a
  MIDI program opens and closes.
* **DosKeepsFM** (default 1). A DOS program that takes back the FM chip it
  had last finds it as it left it. With 0, the chip is reset under it.
* **DosFMAudible** (default 1). A DOS FM owner gets the music DAC (7Fh
  bit 0) and, if 36h was 00h, FM volume FFh. With 0, an FM-only DOS program
  can be silent while no Windows MIDI program is open.
* **DosMixerRestore** (default 1). Windows' mixer, 30 registers, comes back
  after a DOS program. With 0, ESS's 11 registers come back.
* **ResetDosFM** (default 1). Windows' next sound, level change or MIDI open
  resets what a DOS program left on the FM chip. With 0, notes that a DOS
  program left keep sounding.
* **RecordTakesDSP** (default 1). A recording of the FM, through esfmrec or
  ES1869.DRV's FM recording device, takes the DSP from a DOS program, which
  goes on with a silent virtual Sound Blaster
  ([VXD_INTERNALS.md](VXD_INTERNALS.md#a-recording-takes-the-dsp)). With 0,
  a DOS program keeps the DSP from its first Sound Blaster access until it
  ends, and the recording is refused until then.

### 6.2 [ES1869.DRV]

[AUDIO1.md](AUDIO1.md#the-audio-1-player-in-buildes1869drv) describes the
Audio 1 player, and [AUDIO1.md](AUDIO1.md#the-fm-recording-device) the FM
recording device. Each key below is followed by its default and by what the
key does at 1, and the sentence that begins "With 0" says what happens at 0:
* **Audio1Device** (default 1). The driver adds a second wave output device,
  "ESS AudioDrive Audio 1", which plays through the Audio 1 DAC, and names
  device 0 after its DAC, "ESS AudioDrive Audio 2", also as the target of
  the mixer's Wave line, by which Windows finds the mixer of device 0. With
  0, there is one device, "ESS AudioDrive Playback", as with ESS's driver.
* **SharedWaveOut** (default 1). A program that opens device 0 while another
  plays there gets the Audio 1 DAC. With 0, the program gets
  `MMSYSERR_ALLOCATED`, as with ESS's driver.
* **Audio1Filter** (default 0). The Audio 1 CODEC's switched-capacitor
  filter smooths the DAC's steps and keeps high frequencies out of the ADC
  (71h bit 2 clear), as with ESS's driver. With 0, the filter is bypassed at
  all times, for playback and recording (71h bit 2 set)
  ([AUDIO1.md](AUDIO1.md#the-filter-of-the-audio-1-codec)).
* **DualPlayback** (default 1). Device 1 also accepts 4 channels, with 1-2
  on the Audio 1 DAC and 3-4 on the Audio 2 DAC, from one clock
  ([AUDIO1.md](AUDIO1.md#dual-playback)). With 0, a 4-channel format is
  refused (`WAVERR_BADFORMAT`).
* **FMRecordDevice** (default 1). The driver adds a second recording device,
  "ESS AudioDrive FM Digital", which records the FM synthesizer's samples at
  49,716 Hz, 16-bit stereo, through mixer 7Fh bit 4. With 0, there is one
  recording device, as with ESS's driver.
* **Audio2Oversampling** (default 0). The Audio 2 DAC uses ESS's 4x
  oversampling (mixer 71h bit 4), which also bypasses the filter. With 0,
  the DAC plays the samples as they are.
* **Audio2Filter** (default 0). Without 4x oversampling, the
  switched-capacitor filter smooths the DAC's steps (71h bit 3 clear). With
  0, the filter is bypassed (71h bit 3 set).

With `Audio1Device=0` and `SharedWaveOut=0`, the driver answers every wave
output message as ESS's driver does, and `SettingsTest` in
`tests/test_a1play.py` compares the two. `Audio1Filter` and `DualPlayback`
then have no effect. With `FMRecordDevice=0`, it answers every wave input
message as ESS's driver does.

With `Audio2Oversampling=1`, `Audio2Filter=1` and `Audio1Filter=1`, 71h gets
ESS's value: bits 4 and 1 set, and bits 3 and 2 as they were. ES1869.DRV
applies these three keys to Windows' wave output, at the open (1:1157) and
at every playback start (6:2DE6), and with any other setting it also writes
the mode right after the mixer reset of every start and resume (3:4897).
ES1869.VXD reads the same three keys from this section. It applies them to
DirectSound, and to a DOS program when the program takes the DSP and when it
starts a DMA transfer.
[AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter)
explains why the defaults sound better.

essctl's *Device information* page shows the settings the driver read and
what each channel does, for example "Wave driver: the rebuilt ES1869.DRV,
settings from SYSTEM.INI, off: ...", "Audio 1: records the FM digitally" and
"Audio 2: plays the first device".

### 6.3 [ESFM.DRV]

[ESFM_MIDI.md](ESFM_MIDI.md#the-fix-buildesfmdrv) and
[ESFM_GM.md](ESFM_GM.md) describe the changes. Each key below is followed by
its default and by what the key does at 1, and the sentence that begins
"With 0" says what happens at 0, as in ESS's driver:
* **QueueWhileBusy** (default 1). A message that arrives while the driver is
  busy waits in a queue. Open, close and the chip reset hold the driver, so
  no register write is split. With 0, the message is refused with
  `MIDIERR_NOTREADY`, and a note off lost that way leaves the note sounding.
* **SilenceOnClose** (default 1). Closing the device or a power suspend keys
  off every voice and lets every sustain pedal up. With 0, notes that the
  pedal holds keep sounding.
* **PedalRelease** (default 1). A program change lets the channel's sustain
  pedal up. With 0, the pedal stays down.
* **Vibrato** (default 1). Modulation (controller 1) and channel pressure
  turn on the chip's vibrato. With 0, both are ignored.
* **Tuning** (default 1). The driver handles RPN 0 with cents, RPN 1 (fine
  tuning) and RPN 2 (coarse tuning). With 0, it handles RPN 0 in semitones
  only, and RPN 1 and 2 are ignored.
* **ResetControllers** (default 1). Controller 121 resets what RP-015 lists,
  and keeps volume, pan and the bend range. With 0, it does ESS's reset,
  which also resets volume, pan and the bend range and leaves sounding notes
  bent.
* **LivePan** (default 1). Pan (controllers 8 and 10) moves the notes that
  are sounding. With 0, controller 10 pans the next notes, and 8 is ignored.
* **SysEx** (default 1). The driver acts on GM, GM2, GS and XG resets and
  the GM master volume in long messages. With 0, it ignores them.
* **RunningStatus** (default 1). The driver handles running status as the
  MIDI specification defines it, across long-message buffers and around
  real-time bytes. With 0, each long message starts from the last one's data
  bytes, and a real-time byte becomes the running status.
* **BetterSquareWave** (default 1). The built-in bank is
  `esfm_patch_banks/bnk_com_better_square_wave.bin`. With 0, the built-in
  bank is ESS's own `bnk_com.bin`, which the driver also carries (resource
  1235).

With every key at 0, the driver writes to the chip's ports exactly as ESS's
driver does, which `SettingsTest` in `tests/test_esfmdrv.py` checks by
playing a song's worth of messages through both drivers. essctl's *ESFM
patch bank* page lists the changes that are off and shows which bank is the
driver's own, and ESFM > *Restore original bank* puts that bank back.
