# ESS driver settings and the registers Windows sets

This page lists:
* the registry values the ES1869's Windows 95 drivers (`ES1869.VXD` and `ES1869.DRV`) read, and what each one does to the chip
* the registers `ES1869.DRV` writes while Windows runs
* the SYSTEM.INI keys that turn the rebuilt drivers' changes off ([section 6](#6-the-rebuilt-drivers-systemini-settings))

It was worked out from the files in `driver/`:
* `ES1869.VXD` from its source in `src/vxd`
* `ES1869.DRV` by disassembling it with `ndisasm`, after resolving its NE relocation chains with `tools/retools/ne.py`

Register meanings are in [REGISTERS.md](REGISTERS.md), and the VxD's programming interface is in [VXD_API.md](VXD_API.md).

**Addresses.** `3:3E8F` is segment 3, offset 3E8Fh of `ES1869.DRV`. Segments are numbered from 1 in the order of the NE segment table:

| Segment | Contents |
|---|---|
| 1 | fixed code: port I/O helpers (1:1228 DSP write, 1:11F6 DSP read, 1:1294 mixer read, 1:12B2 mixer write), interrupt-time wave code |
| 2 | WEP |
| 3 | device enable/disable, registry, configuration dialog |
| 4 | DriverProc (4:02DA), wrappers of the VxD API |
| 5 | mixer: `mxdMessage` 5:1BA4, set-control-details 5:17CA |
| 6 | wave and aux: `widMessage` 6:0FF2, `wodMessage` 6:18E4, `auxMessage` 6:1D1C |
| 7 | data segment (DS) |

`o5:2122` is an address in object 5 of `ES1869.VXD`, as in `src/vxd`.

**Confidence** of each row:
* *verified*: the code was followed from the registry read to the register write, or to a non-register effect that is described.
* *likely*: the read and the flag were followed, but the purpose of the code using the flag is inferred.
* *unknown*: not established.

## Where the settings live

Both drivers use the device's software key, `HKLM\System\CurrentControlSet\Services\Class\Media\<nnnn>\Config`, which OEMSETUP.INF writes as `HKR,Config,...`.

* **ES1869.VXD** reads its values with CONFIGMG `CM_Read_Registry_Value(devnode, "Config", name, REG_BINARY, ..., CM_REGISTRY_SOFTWARE)` when the device starts (o5:0000).
* **ES1869.DRV** gets the key name from CONFIGMG's protected-mode API (INT 2Fh AX=1684h BX=0033h; function 3Dh `CM_Get_DevNode_Key(devnode, NULL, buffer, 100h, CM_REGISTRY_SOFTWARE)` in the wrapper 3:09EA).
  * It appends `\Config` or `\Config\GPO Selections` and opens that under HKEY_LOCAL_MACHINE with KERNEL's `RegOpenKey` and `RegQueryValueEx` (ordinals 217 and 225).
  * Two values go through CONFIGMG instead: "Telegaming Vol" (function 3Eh, 6:1FD6) and "Single Mode DMA" in the configuration dialog (read with 3Eh at 3:44E6, written with 3Fh `CM_Write_Registry_Value` at 3:4549).
* ES1869.DRV's `RegQueryValueEx` calls pass no type pointer, so any value type is accepted.
  * Each value is read into a buffer that already holds the default.
  * A missing value keeps the default, and most likely so does a value longer than the buffer.
  * A shorter one overwrites only the first bytes: a 1-byte "Disable Mic Preamp" of 00 turns the default 1 into 0.
* ES1869.DRV also **writes** the key. Routine 3:0D54 saves the mixer state (section 2.3) as 4-byte REG_BINARY values. It runs when the last of its devices is disabled (DRVM_DISABLE, 3:4D2C; most likely at shutdown) and before an APM suspend (3:4C8A).

## 1. ES1869.VXD settings

* All are read once, when the device starts (o5:0000, called at o5:060E).
* Byte values are read with a length of 1 and dwords with 4.
* The resulting flags go to the device structure (ADI): low word at offset 04h, high word at 13h.

| Value | Size | Flag | Effect | Confidence |
|---|---|---|---|---|
| Disable Warning | byte, nonzero | 0001h | No "device in use" message when a DOS program touches the card while Windows owns it: Show_Contention_Message (o5:0F0F) returns early. ES1869.DRV reads this value too (2.2). | verified |
| Multiple FM Support | byte, nonzero | 0002h | At device start the VxD reports `midi\esfm.drv` present to MMDEVLDR (MMDEVLDR_SetDevicePresence, o5:064B) even when no FM port was assigned; without the flag only if there is one. With the flag clear and no FM alias base (ADI 0Ah = FFFFh), it also sets the FM port to "none" (ADI 08h := FFFFh, o4:0879). | verified in code, purpose likely |
| Do Not Want ES689 | byte, nonzero | 0008h | Keeps the ES689/ES69x serial interface off. o5:1EAD sets mixer 48h bit 4 at start if nobody owns FM (o5:1C8C) and whenever FM is released (o5:10AB), and clears it when Windows' system VM acquires FM (o5:121F); with the flag the bit stays 0. The flag also skips o5:20AE (called from o3:0120), which sends FFh (reset), 3Fh (UART mode) and SysEx F0 00 00 7B 07 F7 to the MPU-401 port. | verified |
| DOS MPU-401 Interrupts | byte, nonzero | 0400h | Tested in Acquire_Resources (o5:11DA). | as given |
| Single Mode DMA | dword, nonzero | clears 10000h | 10000h is set by default and selects demand-mode DMA in the VxD's own Audio 2 path: VDMAD mode 18h instead of 58h (o5:32F2), mixer 78h = 93h instead of 13h (o5:360C). ES1869.DRV reads the value as well (2.2). | verified |
| HwVolume 2-Wire Mode | dword 1-3 | 2000000h | The value (0 if above 3), shifted left by 2, goes into a byte that o5:1EDB writes into mixer 64h bits 3:2 (hardware volume button mode) at device start (o4:086E). | verified |
| HwVolume Count By 3 | dword, nonzero | none | Sets bit 5 of the same byte: mixer 64h bit 5 (count by 3). o5:1EDB only ORs it in and never clears it. ES1869.DRV reads this value too (2.2). | verified |
| Disable Mic Gain | byte, nonzero | 4000000h | At device start (o4:08AE) o5:1CE7 clears mixer 7Dh bit 3, the +26 dB mic preamp. ES1869.DRV rewrites 7Dh later (see "Disable Mic Preamp"). | verified |
| Want Local Powerdown | dword, nonzero | 20000h | When no VM owns the DSP, FM or MPU-401, the VxD powers the digital section down (Audio_Base+7 bits 2 and 3, o5:2122). | as given |

## 2. ES1869.DRV settings

ES1869.DRV reads 97 value names:
* 16 configuration values and 8 values in `Config\GPO Selections` (2.2)
* 59 mixer-state values (2.3)
* 14 volume maps (2.4)

### 2.1 When the values are read

All four message entry points (mixer, wave in, wave out, aux) pass DRVM_INIT, DRVM_ENABLE and DRVM_DISABLE to the same shared routines. These count the calls, so the work is done once per device.

| When | Routine | Reads |
|---|---|---|
| DRVM_INIT (for example 5:1D15) | 3:43E2 | Disable Warning |
| DRVM_ENABLE, first device (3:4940) | 3:3DEC, called at 3:4B56 | the configuration values, GPO0/GPO1 Show |
| DRVM_ENABLE (3:4BB5) and APM resume (3:4CB9) | 3:4818, which calls 6:1F92 and 3:18EC | Telegaming Vol, then the mixer state, the volume maps and the other GPO values |
| DRV_CONFIGURE ("Use single mode DMA" check box) | 3:4550, 3:44A8, 3:450E | Single Mode DMA (read and written) |

### 2.2 Configuration values

The "Read at" column gives the `RegQueryValueEx` call.

| Value | Read at | Size, default | What ES1869.DRV does | Confidence |
|---|---|---|---|---|
| Disable Warning | 3:447A | 1 byte, 0 | Nonzero sets DS:00C4 bit 0, which suppresses the driver's error message boxes (3:0A8C, 3:0AEC): "The ESS AudioDrive hardware is not responding properly..." (6:0000), "The ES1869.VXD driver is not present...", "The version of ES1869.VXD is out of date..." (3:0B58). | verified |
| DCdrift | 3:3E8F | dword, 1 | Software DC-offset removal on recorded data: the mean of the first recorded block (1:1CC3) is subtracted from the following samples (1:1D0B). Forced off if the Audio 1 DMA channel is above 3. A private widMessage 4488h reads or sets it at run time (6:11FA): dwParam1 points to two dwords, and a nonzero first one reads the setting into the second, a zero one sets it from the second. It only answers the program that has the device open, before it starts recording, and a recording takes the setting when the device is opened (6:1FF4), so it applies from the next open. esfmrec turns it off this way. No register. | verified |
| AGC | 3:3EE1 | dword, 1 | While DC-drift correction is active, an 8-bit recording is captured as 16-bit and converted with an adaptive gain stage (flags 4001h set at 6:2050, conversion at 1:1D6C). Forced off if the Audio 1 DMA channel is above 3. No register. | likely |
| Disable Mic Preamp | 3:3F33 | dword, **1** | Sets DS:00C7. Mixer 7Dh bit 3 (+26 dB) is set when the value is 0 and cleared otherwise: at device enable (3:4884), at every record start (5:38D2) and when Phone Select is muted (5:2EA6/5:2EC0). With Phone Select unmuted the driver clears bit 3 regardless (5:3060). Missing value = preamp off; the INF writes one byte 00 = preamp on. | verified |
| DiscardBlock | 3:3F80 | dword, 1 | Discards the first DMA block of each recording (interrupt code 1:1A28, position correction 6:01FF). No register. | likely |
| HwVolumeStep | 3:3FC8 | dword 1-4, 1 | Out-of-range values become 1. Number of HwVolumeMap entries the master volume moves per hardware volume button event (callback 5:20DE, 5:21AA). | verified |
| HwVolume Count By 3 | 3:402B | dword, 0 | Exactly 1 forces the step to 3; other nonzero values are ignored here. The VxD separately sets mixer 64h bit 5. | verified |
| HwVolumeMap | 3:4076 | 64 bytes (00-3Fh), table at 7:0950 | Used only if exactly 64 bytes are read. Maps the 6-bit master level to mixer 60h/62h bits 5:0, for the Volume Control slider (5:2A8C) as well as for the hardware buttons (callback 5:20DE, sync routine 5:3BAC). Default 00 02 04 06 08 0C ... 3F. | verified |
| Single Mode DMA | 3:40D7 | dword, 0 | 0 sets flag 2Ah bit 15, meaning demand transfers: 8237 demand mode for recording (6:27B0, 6:27F0) and playback (6:2C8C, 6:2CD5), controller B9h = 02h for Audio 1 (6:28BF), mixer 78h = 93h instead of 13h for Audio 2 (6:2DFA). Nonzero gives single transfers, with B9h left as the DSP reset set it. | verified |
| Enable AUXB | 3:411F | dword, 1 | 0: the AuxB source lines are reported disconnected to mixer programs (5:02F2). The AuxB registers are still written. UI only. | verified |
| Enable ES938 | 3:41A1 | dword, see effect | Nonzero, or missing, enables the ES1869's own Spatializer 3-D: 50h-5Ah initialized at enable (3:48AC) and the Spatializer Enable / 3D Effect controls shown. Only an explicit 0 turns it off; the driver then never writes 50h or 52h. The ES938 is an external chip, so the name is historical. | verified |
| 3D Limit | 3:41F9 | dword, 0 | Mixer 50h bit 0, undocumented in the data sheet (3:48D7, 5:3B55). essctl shows it as `fx.3d.limit`, `essreg 3l=` and `ess3d limit` set it. | verified |
| Enable IIS | 3:4247 | dword, 1 | 0: the IIS line is reported disconnected (5:0322). UI only. | verified |
| Enable Software 3D Effect | 3:4293 | dword, 0 | Used only if Enable ES938 = 0. Stereo wave output gets a software 3-D effect in the buffer copy (1:0919, 1:09AB), driven by the Spatializer Enable and 3D Effect controls. No register. | verified |
| ESSWaveTableChip | 3:42DF | dword, 0 | Nonzero sets flag 2Bh bit 6: the driver never sets mixer 7Fh bit 0, which gives the music DAC to I2S. It skips all four places: device enable (3:4BB9), resume (3:4CE3), the FM driver's close (5:2022) and the MPU-401 notification's release (3:4FA8). It still clears the bit when FM opens. The IIS line is reported disconnected (5:0383), and its volume is neither restored (3:2098) nor saved (3:10FA). The Synth volume only goes to 36h while dev+117h is 1 (5:254A, 5:2B79), which the first FM open sets, so until then 36h keeps its reset value. `essctl /i2s=off` sets it. | verified |
| Telegaming Vol | 6:1FD6 (CONFIGMG) | 1 byte REG_BINARY, none | Written to mixer 14h (Audio 1 play volume) right after the mixer reset at enable and resume (6:1FE8). If the read fails, 14h keeps its reset value. A 4-byte value probably fails the 1-byte read. | verified (length: likely) |
| GPO0 Show, GPO1 Show | 3:436C, 3:43B2 (`GPO Selections`) | dword, 0 | Nonzero: the GPO0/GPO1 switch is shown in the mixer. Without it the control is disabled and hidden (5:033D, 5:0360). | verified |
| GPO0 Default, GPO1 Default | 3:3AFD, 3:3C9D | dword, current pin level | If present, or if Show is set, sets the pin: Audio_Base+7 bit 0 or 1 through VxD function 0005 (5:39C0). On Compaq devices GPO1 is inverted. Saved at shutdown if Show is set (3:1874). | verified |
| GPO0/GPO1 LongLabel, Label | 3:3BBA, 3:3BF7, 3:3C27; 3:3D5A, 3:3D97, 3:3DC7 | string, 64 / 16 bytes | Only with Show: LongLabel (or Label if it is missing) becomes the control's long name, Label its short name. | verified |

**Compaq.** A device whose ID contains CPQB023, CPQB0AB, CPQB0AC or CPQB0AD (CONFIGMG function 07h, 3:0B92-3:0CA0) is treated as a Compaq.
* On a Compaq, Treble and Bass go to `SetEqzrCtrls` in CPQVAPI.DLL (5:0000, 5:3A74), except on CPQB0AB and CPQB0AC, where they're hidden.
* There's no ES1869 register for treble and bass.

### 2.3 Mixer state

* These values hold the state of ES1869.DRV's mixer controls.
* They're restored at every enable and resume (3:18EC) through the driver's own set-control-details path (5:17CA), and saved back when the device is disabled and at suspend (3:0D54).
* The number in the Control column is the control ID.
* Volumes are the 16-bit mixer value (0-FFFFh) in the low word of a dword. Pairs such as Left/RightMasterVol are one value per channel.

The conversions:
* 4-bit registers: nibble = map[value >> 12], left channel in bits 7:4, right in 3:0. The map is the volume map of section 2.4.
* Master: 60h/62h bits 5:0 = HwVolumeMap[value >> 10]; bit 6 = Master Mute.
* PC speaker: 3Ch = (value x max(master left, master right) / FFFFh) >> 13, and 0 while the master is muted.
* A muted or deselected source gets 00h in its register.
* Bitmasks: bit i is source i of the destination, in the order given below.

| Value(s) | Read at | Default | Control | Register effect | Confidence |
|---|---|---|---|---|---|
| Mute | 3:1998 | 0 | Master Mute (33) | 60h/62h bit 6; 3Ch forced to 0 | verified |
| MutesOut | 3:1A20 | 04h (Mic) | source mutes 25-32 | bit i set: register of playback source i = 00h | verified |
| Mixer:Output | 3:1AF7 | 7Bh | Master Output Sources (0) | bit i clear: register of playback source i = 00h | verified |
| Left/RightMasterVol | 3:1BD3 | 8000h | Master Volume (11) | 60h/62h bits 5:0 via HwVolumeMap; also rescales 3Ch | verified |
| Left/RightLineInVol | 3:1C8A | 8000h | Line-In Volume (3) | 3Eh | verified |
| Left/RightDACVol | 3:1D41 | 8000h | Wave Output Volume (4) | 7Ch, the Audio 2 DAC volume. The byte is also kept at dev+124h and written to 7Ch at every playback start (6:2D6D), without the WaveVolumeOutMap. | verified |
| Left/RightMicVol | 3:1DF8 | 8000h | Microphone Volume (5) | 1Ah | verified |
| Left/RightCDAudioVol | 3:1EAF | 8000h | CD Audio Volume (6) | 38h | verified |
| Left/RightSynthVol | 3:1F66 | 8000h | FM Synthesis Volume (7) | Passed to the FM driver's callback (5:2467). Written to 36h only while the music DAC belongs to FM or an ES689 (dev+117h = 1, see 7Fh in section 4). | verified |
| Left/RightAuxBVol | 3:201D | 8000h | AuxB Volume (8) | 3Ah | verified |
| Left/RightIISVol | 3:20DD | 8000h | IIS Volume (9) | 36h while dev+117h = 0, which is the normal case with no FM client open | verified |
| PCspeakerVol | 3:218F | 8000h | PC Speaker Volume (10), mono | 3Ch bits 2:0, see above | verified |
| MonitorWave | 3:2215 | 0 | Recording Input Monitor (37) | controller A8h bit 3, at each record start (6:2A3C) and while recording (5:38F4) | verified |
| Mixer:Wave | 3:229D | 02h (Mic) | Recording Input Sources (1) | At record start: 1Ch bits 2:0 = 5 (record mixer, 5:35AC); selected sources get their record volume, others 00h (5:365B). Bit 1 (Mic) also drives the Phone destination's Mic Select. | verified |
| Left/RightWaveMasterVol | 3:23FD | 8000h | Mixer Input Level (18) | controller B4h at record start, nibbles swapped: right in 7:4, left in 3:0 (5:297A) | verified |
| Left/RightWaveLineVol | 3:24B4 | 8000h | Line-In Input Level (12) | 6Eh at record start | verified |
| Left/RightWaveMicVol | 3:256B | 8000h | Microphone Input Level (13), linked to Phone "Mic Vol" (47) | 68h, written at once; 00h if Mic is not a recording source | verified |
| Left/RightWaveCDAudioVol | 3:266E | 8000h | CD Audio Input Level (14) | 6Ah at record start | verified |
| Left/RightWaveAuxBVol | 3:2725 | 8000h | control 15, on the AuxB line | 6Ch (AuxB record) at record start. The control is named "FM Synthesis Input Level" but belongs to the AuxB line. | verified |
| Left/RightWaveSynthVol | 3:27DC | 8000h | control 16, on the Synthesizer line | 6Bh (music DAC record) at record start. Named "AuxB Input Level". | verified |
| Left/RightWaveDACVol | 3:2893 | 8000h | Wave Input Level (17) | 69h (Audio 2 record) at record start | verified |
| MonitorVoice, Mixer:Voice, Left/RightVoiceMasterVol, Left/RightVoiceLineVol, ...VoiceMicVol, ...VoiceCDAudioVol, ...VoiceAuxBVol, ...VoiceDACVol | 3:2943-3:2E71 | 0, 02h, 8000h | "Voice Commands" destination (38, 2, 24, 19-23) | Same registers as the Wave* set (A8h bit 3, 1Ch, B4h, 6Eh, 68h, 6Ah, 6Ch, 69h), used when the wave-in instance marked by the private widMessage 4093h records | verified |
| Left/RightMonoInPhone | 3:2F00 | 8000h | Phone Vol (48) | 6Dh and 6Fh, same value, no map (5:2B38, 5:2B4A) | verified |
| MonoInPhoneMute | 3:2FBE | bit 0 counts; missing: not muted | Phone Select (46) | Muted: 6Dh = 6Fh = 00h, 7Dh bits 2:1 = 00 (MONO_OUT off), 7Dh bit 3 from Disable Mic Preamp. Unmuted: 6Dh/6Fh from Phone Vol, 7Dh bits 2:1 = 11 (record mono mix), bit 3 = 0. | verified |
| 3D Effect Enable (fallback SpatializerEnable) | 3:3063, 3:33BE, 3:34D4 | 1 (0 for software 3-D) | Spatializer Enable (41) | 50h = 0Ch (on) or 04h (off), bit 0 = 3D Limit (5:3B50) | verified |
| 3D Effect (fallback SpatializerEffect) | 3:30FD, 3:32D0, 3:331A, 3:343C | FFFFFFFFh (BFFFBFFFh for software 3-D) | 3D Effect (42) | 52h = low word >> 10 (5:3B90); the high word is the right channel and is ignored | verified |
| Treble, Bass | 3:319D, 3:3237 | 7FFF7FFFh | Treble (43), Bass (44) | Compaq only: CPQVAPI.DLL; no register | verified |

Source order:
* **Playback** (Mixer:Output, MutesOut): bit 0 Line-In, 1 Wave, 2 Microphone, 3 CD Audio, 4 Synthesizer, 5 AuxB, 6 IIS, 7 PC Speaker.
* **Recording** (Mixer:Wave): 0 Line-In, 1 Microphone, 2 CD Audio, 3 AuxB, 4 Synthesizer, 5 Wave.
* **Voice** (Mixer:Voice): 0 Line-In, 1 Microphone, 2 CD Audio, 3 AuxB, 4 Wave.
* The INF's "Mixer:Output" 5Bh turns off Mic, AuxB and PC speaker. Its "MutesOut" 44h mutes Mic and IIS.

Also:
* The 3-D values take effect only if their controls exist, that is with Enable ES938 (the default) or Enable Software 3D Effect. The driver's set-control-details rejects disabled controls (5:08B0).
* Treble and Bass exist only on a Compaq that has CPQVAPI.DLL.

### 2.4 Volume maps

* Each map must be exactly 16 bytes (checked at 3:356D and similar).
* Entry n is the 4-bit register value used for slider step n.
* Entries must be 00-0Fh. The right-channel entry is ORed in unmasked, so a larger value would corrupt the left nibble (for example 5:2697).
* The defaults at 7:0990-7:0A6F are the identity 00, 01 ... 0F.
* The maps apply in the set-control-details path only (see Left/RightDACVol).

| Map | Table | Used for |
|---|---|---|
| LineInVolumeOutMap, WaveVolumeOutMap, MicVolumeOutMap, CDAudioVolumeOutMap, SynthVolumeOutMap, AuxBVolumeOutMap, IISVolumeOutMap | 7:0A00, 0A10, 0A20, 0A30, 0A40, 0A50, 0A60 | 3Eh, 7Ch, 1Ah, 38h, 36h (Synth), 3Ah, 36h (IIS) |
| LineInVolumeInMap, WaveVolumeInMap, MicVolumeInMap, CDAudioVolumeInMap, SynthVolumeInMap, AuxBVolumeInMap | 7:0990, 09A0, 09B0, 09C0, 09D0, 09E0 | 6Eh, 69h, 68h, 6Ah, 6Bh, 6Ch |
| IISVolumeInMap | 7:09F0 | read but never used (there is no IIS record source) |

### 2.5 Names nobody reads

* In ES1869.DRV but never referenced: StartupMuteMsg, MIDIInPersistence, MonoInMicMute, LeftMonoInMic, RightMonoInMic, Do Not Want ES938.
* In OEMSETUP.INF / `ess_windows_regs.txt` but read by neither driver: "Telegaming". Only "Telegaming Vol" is read. Nothing in either driver sets telegaming mode (mixer 48h bit 1).
* ESSDC.EXE, the "DC Drift daemon", is a 16-bit Windows program the INF starts from the Run key. It uses no registry value names.
  * Once, it checks whether mixer 64h bit 5 takes a write, and puts it back.
  * 10 seconds after it starts, then every 10 minutes, it makes a short recording through ES1869.DRV (2 or 8 KB, 16-bit stereo) and writes the ADC offset registers, controller BAh and BBh, with DSP commands (`adc.off_l` and `adc.off_r` in essctl).

## 3. VxD calls made by ES1869.DRV

ES1869.DRV stores the AUDDRV entry point (INT 2Fh AX=1684h BX=3B07h) at DS:0010 (3:0091) and passes the devnode in ECX.

| DX | Call | Wrapper | Called from | Purpose |
|---|---|---|---|---|
| 0000 | 3:00CC | 3:00C4 | DRV_ENABLE (3:5013) | Version. No VxD sets error 2, a version below 0404h error 3; the message box appears at DRVM_INIT unless Disable Warning is set. |
| 0001 | 3:0109 | 3:00D9 (AX=1, C9h bytes) | DRVM_ENABLE (3:4971) | Copy the ADI: Audio_Base, MPU-401 port and IRQ, audio IRQ, both DMA channels, the no-DMA flag. |
| 0002 (BX=1) | 4:0010 | 4:0000 | wave-out open 4:0054 (6:15AF in WODM_OPEN), wave-in open 4:00E0 (6:059E, 6:0807), enable and resume (3:482B, 3:4B14, 3:4BC6, 3:4CC4), disable (3:4E16), every mixer register change (5:2A5D, 5:2984, 5:2E66, 5:303F, 5:35E8, 5:38A5, 5:3936, 5:3B10), FM and MPU notifications (5:1F7A, 5:2052, 3:4F59, 3:4FB5) | Acquire the DSP. Counted in dev+2Eh. |
| 0003 (BX=1) | 4:0041 | 4:0023 | the matching releases: wave-out close 4:009F (6:1829), wave-in close 4:011A (6:0DF1), error paths, after each mixer change | Release the DSP. Called only when the count drops to 0, so a mixer change while no wave device is open costs a VxD acquire plus release, and the release resets the DSP. |
| 0004 | 1:1AA4 | 1:1A94 | 1:10EC (BX=0, recording), 1:1118 (BX=1, playback) | DMA position of the running transfer |
| 0005 | 4:015E | 4:014A | restore (3:3AA0, 3:3C40, read), GPO controls (5:39DC read, 5:3A64 write), private auxMessage 4687h (6:1E65, passes the caller's values) | GPO pins |
| 0006 | 4:0199 | 4:0187 | enable (3:4B9B, callback 5:20DE), disable (3:4DD2, 0) | Hardware volume callback |
| 0007 | 1:1AC6 | 1:1AB1 | record start 6:2791, record stop 1:16B1, no-DMA mode only | PIO emulation buffer |
| 000A | 4:01B9 | 4:01A7 | enable (3:4BAE, EAX = 16:16 pointer to 3:4C3C), disable (3:4DE1, EAX = 0) | Seemingly meant to register 3:4C3C, which marks the DSP busy or free. In this VxD 000Ah only returns a flag byte and ignores EAX, so nothing is registered. |
| 0009 | 3:00D4 | 3:00D1 | nowhere | dead code |
| 0200 | 3:478C | 3:4758 | enable: 3:4B7F ("ESSFMMXD", callback 5:1D6E); 3:4C23 ("ESMPUISR", callback 3:4EE6, only with an MPU-401 port and no MPU IRQ) | Register notification clients. The FM driver uses ESSFMMXD to take over the Synth volume and the music DAC. |
| 0201 | 3:47BD | 3:4794 | disable (3:4DB8, 3:4DFF) | Unregister them |

## 4. Registers ES1869.DRV writes

* Mixer registers go through 1:12B2 (index to Audio_Base+4h, data to +5h).
* Controller registers go through DSP commands: the register then the value, with read-modify-write as C0h, register, read.
* A register tool should expect Windows to overwrite everything below.

**At every device enable and APM resume** (3:4818, then the restore):

| Register | Address | Value |
|---|---|---|
| mixer 00h | 6:1FA1 | 00h: mixer reset. Mixer registers return to their reset values; per the data sheet the record volumes are reset only by a hardware reset. |
| 14h | 6:1FE8 | Telegaming Vol, only if the value exists |
| 50h, 52h, 54h, 56h, 58h, 5Ah | 3:48C0-3:4936 | 50h = 00h then 0Ch with bit 0 = 3D Limit; 52h = 3Fh; 54h-5Ah = 8Fh, 95h, 94h, 80h (undocumented, `fx.3d.reg54` to `fx.3d.reg5a` in essctl). Only with Enable ES938. |
| 7Dh | 3:485B, 3:4884 | 06h (MONO_OUT = record mono mix, MONO_IN direct off), then bit 3 from Disable Mic Preamp |
| 1Ch | 3:4890 | 05h (record mixer, record mute off) |
| from the restore | 3:18EC | 60h, 62h, 3Eh, 7Ch, 1Ah, 38h, 36h, 3Ah, 3Ch, 68h, 6Dh, 6Fh, 7Dh bits 3:1, 50h, 52h, Audio_Base+7 bits 1:0 |
| 7Fh bit 0 | 3:4BDD (enable), 3:4CDA-3:4CF7 (resume) | set, unless ESSWaveTableChip; at resume cleared instead if FM or an ES689 had the music DAC (dev+117h) |

**Mixer API** (MXDM_SETCONTROLDETAILS 5:17CA, per-control handlers at 5:2BA1). Every change acquires and releases the DSP around the write.

| Register | Address | From |
|---|---|---|
| 3Eh, 7Ch, 1Ah, 38h, 36h, 3Ah, 3Ch | 5:2B8C (36h = 00h when muted at 5:256E; 3Ch also at 5:265E) | playback volumes, mutes, Master Output Sources |
| 60h, 62h | 5:2AAC, 5:2B8C | Master Volume, Master Mute |
| 68h, 69h, 6Ah, 6Bh, 6Ch, 6Eh | 5:28D0 (68h), 5:2B8C | record levels: 68h at once, the others only while recording |
| 6Dh, 6Fh | 5:2B38, 5:2B4A | Phone Vol, Phone Select |
| 1Ch bits 2:0 | 5:3628 | 5 while recording |
| 7Dh bits 3:1 | 5:2E85, 5:2EA6, 5:2EC0, 5:3060, 5:38D2 | Phone Select, record start |
| 50h, 52h | 5:3B50, 5:3B61-5:3B90 | Spatializer Enable, 3D Effect |
| controller B4h | 5:29B3 (C6h), 5:29BE, 5:29D1 | Mixer Input Level, nibbles swapped |
| controller A8h bit 3 | 5:3951-5:399C | record monitor, while recording |
| Audio_Base+7 bits 1:0 | via VxD 0005 | GPO0, GPO1 |
| 7Fh bit 0 | 5:1FA3 (clear), 5:206D (set) | FM driver opens or closes (ESSFMMXD messages); also 3:4F7F / 3:4FD8 (ESMPUISR) and 3:4E39 (clear at disable) |

At every playback start, 5:3BAC (6:2D5A) reads 60h and 62h back. If they changed, from the hardware buttons or a DOS program, it updates the Master Volume and Mute controls and so rewrites 60h and 62h.

**Playback** (Audio 2):

| Register | Address | Value |
|---|---|---|
| 71h | 1:115E, 6:2DEE | bits 4 and 1 set (4x oversampling, asynchronous); other bits kept. `build/ES1869.DRV` reads 71h through `a2_mode_read` at both (1:1157, 6:2DE6), which clears bit 4 and sets bit 3 by default: no oversampling, the filter bypassed ([AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter), section 6.2) |
| 70h, 72h, 74h, 76h, 78h | 1:1174-1:1192 | 00h at wave-out open, close and resume (list "prtvx" at 7:00A8), then 70h = 72h = FFh |
| 70h, 72h | 6:2599, 6:25D0 | sample rate and filter |
| 7Ch | 6:2D6D | wave volume, at start |
| DSP D3h | 6:2D78 | speaker off (gates Audio 1's 14h path) |
| 74h, 76h | 6:2D98, 6:2DA7 | minus the block length |
| 7Ah | 6:2DD7 | 40h, plus 05h for 16-bit signed and 02h for stereo |
| 78h | 6:2E0C, 6:2E69 | 13h (single) or 93h (demand 4) at start; 00h at pause |
| 78h, 7Ah, 7Ch | 1:16FE, 1:171B, 1:1739, 1:1744 | at stop: 78h bit 4 cleared then 00h, 7Ah bit 7 cleared, **7Ch = 00h**, so the Wave volume reads 00h while nothing plays |
| 7Ah bit 7 | 3:0300 | cleared by the interrupt handler, which saves and restores the mixer index (3:02EF, 3:0307) |

**Recording** (Audio 1):
* ES1869.DRV also resets the DSP itself (1:11A0: Audio_Base+6 = 03h then 00h, then command C6h) when it takes the DSP for recording: 6:05A8, 6:0811, 6:0D5E, 6:0DEB.
* That clears all controller registers.

| Register | Address | Value |
|---|---|---|
| 71h bit 5 | 6:22D3-6:22EA | set only for 48000 Hz |
| A1h, A2h | 6:22F3, 6:236C | sample rate and filter; DSP 40h time constant instead for some formats (6:214B) |
| Audio_Base+6 | 6:28AC, 1:169B | 02h then 00h: FIFO reset at start and stop |
| B9h | 6:28BF | 02h (demand 2), only without Single Mode DMA |
| A4h, A5h | 6:28DE, 6:28F5 | minus the block length |
| A8h | 6:291D, 6:2A9C | F5h (stereo) or F6h (mono), then bit 3 = record monitor. Bits 7:5 and 2 are written as 1 although the data sheet says to write 0 (`a1.analog.bits7_5`, `a1.analog.bit2` in essctl). |
| B1h, B2h | 6:2934-6:2963, 6:296E-6:299D | ORed with 50h |
| B7h | 6:29C1 | 90h, or 98h for stereo, plus 24h for 16-bit |
| B8h | 6:29D8-6:2A09 | (B8h & 30h) or 0Fh: ADC, auto-initialize, DMA read, enable |
| 1Ch, 68h, 69h, 6Ah, 6Bh, 6Ch, 6Eh, B4h, 7Dh bit 3 | 5:35AC, 5:365B | record source and record mix, see 2.3 |
| B8h | 1:1626-1:1690 | at stop: bit 2 cleared, then B8h & 30h (or DSP D0h for some formats) |

**Never written by ES1869.DRV** (apart from the mixer reset): the SB Pro views 04h-2Eh, 32h, 42h-4Eh (the VxD writes 48h bit 4), 64h (the VxD writes bits 5 and 3:2 at start and bits 1:0 for the hardware volume interrupt), 65h, 66h, BAh, BBh, 7Fh bits 7:1, Audio_Base+7 bits 7:2.

## 5. How to change a setting

* Put the value in the device's software key, `HKLM\System\CurrentControlSet\Services\Class\Media\<nnnn>\Config`, where `<nnnn>` is the ES1869's instance (the one whose `Driver` value is `es1869.vxd`). GPO values go in its subkey `GPO Selections`.
* Use binary values, as in OEMSETUP.INF's `HKR,Config,"name",01,...` lines (flag 01 = REG_BINARY).
  * The CONFIGMG reads (the VxD's, and the one for "Telegaming Vol") ask for REG_BINARY.
  * Dwords are 4 bytes, low byte first (`01,00,00,00`).
  * Byte values (Disable Warning, Multiple FM Support, Do Not Want ES689, DOS MPU-401 Interrupts, Disable Mic Gain, Telegaming Vol) should be exactly one byte.
  * Maps must have exactly 16 bytes, HwVolumeMap exactly 64.
  * The GPO labels are strings (`HKR,"Config\GPO Selections","GPO0 Label",,"text"`).
* Settings take effect when Windows starts again: the VxD and ES1869.DRV read them only when the device starts.
* **The mixer-state values of 2.3 and GPO0/GPO1 Default are overwritten** by ES1869.DRV when it disables the device (most likely at shutdown) and at suspend.
  * Registry edits to them made while Windows runs are lost.
  * Set them with the Volume Control instead, or edit them while the driver isn't running, for example in Safe Mode.
* The values of sections 1 and 2.2 are only read, never written, except Single Mode DMA. The driver's own Settings dialog writes that one ("Use single mode DMA": Multimedia control panel, Advanced, the ES1869 audio device, Properties, Settings).
* A setting made with essctl or essreg is undone by the mixer reset at the next start or resume, and by the writes in section 4.
  * An `essctl /load` in the StartUp group runs after the driver's start-up writes, but the playback, recording and mixer writes of section 4 still apply afterwards.

## 6. The rebuilt drivers' SYSTEM.INI settings

The drivers in `build/` change ESS's in the ways this repository describes. Each change has a key in `SYSTEM.INI` (`C:\WINDOWS\SYSTEM.INI`), and `0` gives back what ESS's driver does there.

* A driver reads its section **once, when it starts**:
  * `ES1869.VXD` when Windows loads it (`Sys_Dynamic_Device_Init`)
  * `ES1869.DRV` at its first enable, right after ESS's registry values
  * `ESFM.DRV` at its first `DRV_ENABLE`. Its `Bank=` isn't a switch: it names a bank file, and it's read again every time a program opens the MIDI device ([ESFM_BANK.md](ESFM_BANK.md#bank-file-buildesfmdrv)).
* A change takes effect when Windows starts again.
* Without the section or a key, the default applies.
* A value is read as `GetPrivateProfileInt` reads it: its leading digits, 0 without any. Write `1` or `0`: `yes` and `on` read as 0.

For example, ESS's DOS box behavior, one wave device and ESS's Audio 2 mode, with the register API kept:

```
[ES1869.VXD]
VirtualFM=0
DosTakesFM=0
DosKeepsFM=0
DosFMAudible=0
DosMixerRestore=0
ResetDosFM=0

[ES1869.DRV]
Audio1Device=0
SharedWaveOut=0
Audio2Oversampling=1
Audio2Filter=1
```

*Note: the VxD can read SYSTEM.INI only while Windows starts: VMM's profile services are gone afterwards. A VxD loaded later, for a card found while Windows runs, keeps the defaults. essctl's Device information page says which: "VxD settings: from SYSTEM.INI" or "the defaults".*

### 6.1 [ES1869.VXD]

The DOS box changes are described in [VXD_INTERNALS.md](VXD_INTERNALS.md#dos-boxes).

| Key | Default | 1 | 0, as ESS's driver |
|---|---|---|---|
| RegisterAPI | 1 | The register API, functions 0400-040C ([VXD_API.md](VXD_API.md)), which essctl, `ess3d` and `esfmrec` use | Group 4 fails with CF set. The programs go through the ports as with ESS's driver ([VXD_API.md](VXD_API.md#ownership-and-port-trapping)) |
| VirtualFM | 1 | A DOS program that can't have the FM chip gets a virtual one, so its FM detection succeeds | Every FM read returns FFh while Windows' MIDI or another DOS box has FM, with ESS's "in use" message |
| DosTakesFM | 1 | Windows keeps FM it got from a port access only until a DOS program wants it | Windows keeps it until a MIDI program opens and closes |
| DosKeepsFM | 1 | A DOS program taking back the FM chip it had last finds it as it left it | The chip is reset under it |
| DosFMAudible | 1 | A DOS FM owner gets the music DAC (7Fh bit 0) and, if 36h was 00h, FM volume FFh | An FM-only DOS program can be silent while no Windows MIDI program is open |
| DosMixerRestore | 1 | Windows' mixer, 30 registers, comes back after a DOS program | ESS's 11 registers come back |
| ResetDosFM | 1 | Windows' next sound, level change or MIDI open resets FM a DOS program left | Notes a DOS program left keep sounding |

### 6.2 [ES1869.DRV]

The Audio 1 player is described in [AUDIO1.md](AUDIO1.md#the-audio-1-player-in-buildes1869drv).

| Key | Default | 1 | 0 |
|---|---|---|---|
| Audio1Device | 1 | A second wave-out device, "ESS AudioDrive Audio 1", played through the Audio 1 DAC | One device, as ESS's driver |
| SharedWaveOut | 1 | A program opening device 0 while another plays there gets the Audio 1 DAC | It gets `MMSYSERR_ALLOCATED`, as with ESS's driver |
| Audio1Filter | 0 | While the Audio 1 player plays, the CODEC's switched-capacitor filter smooths the DAC's steps | The filter is bypassed while it plays (71h bit 2 set, and clear again at its close) |
| Audio2Oversampling | 0 | ESS's 4x oversampling on the Audio 2 DAC (mixer 71h bit 4), which also bypasses the filter | The DAC plays the samples as they are |
| Audio2Filter | 0 | Without 4x oversampling, the switched-capacitor filter smooths the DAC's steps (71h bit 3 clear) | The filter is bypassed (71h bit 3 set) |

* With `Audio1Device=0` and `SharedWaveOut=0` the driver answers every wave-out message as ESS's does: `SettingsTest` in `tests/test_a1play.py` compares them. `Audio1Filter` then does nothing.
* `Audio2Oversampling=1` with `Audio2Filter=1` writes ESS's value, 71h bits 4 and 1 set and bit 3 as it was.
* ES1869.DRV applies them to Windows' wave output, at the open (1:1157) and at every playback start (6:2DE6). ES1869.VXD reads the same two keys for DirectSound and for a DOS program taking the DSP.
* Why the defaults sound better: [AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter).

### 6.3 [ESFM.DRV]

The changes are described in [ESFM_MIDI.md](ESFM_MIDI.md#the-fix-buildesfmdrv) and [ESFM_GM.md](ESFM_GM.md).

| Key | Default | 1 | 0, as ESS's driver |
|---|---|---|---|
| QueueWhileBusy | 1 | A message that comes while the driver is busy waits in a queue. Open, close and the chip reset hold the driver, so no register write is split | The message is refused with `MIDIERR_NOTREADY`, and a note off lost that way leaves the note sounding |
| SilenceOnClose | 1 | Closing the device or a power suspend keys off every voice and lets every sustain pedal up | Notes the pedal holds keep sounding |
| PedalRelease | 1 | A program change lets the channel's sustain pedal up | The pedal stays down |
| Vibrato | 1 | Modulation (controller 1) and channel pressure turn on the chip's vibrato | Both are ignored |
| Tuning | 1 | RPN 0 with cents, RPN 1 (fine tuning) and RPN 2 (coarse tuning) | RPN 0 in semitones only, RPN 1 and 2 ignored |
| ResetControllers | 1 | Controller 121 resets what RP-015 lists, and keeps volume, pan and the bend range | ESS's reset: volume, pan and the bend range too, and sounding notes stay bent |
| LivePan | 1 | Pan (controllers 8 and 10) moves the notes that sound | Controller 10 pans the next notes, 8 is ignored |
| SysEx | 1 | GM, GM2, GS and XG resets and the GM master volume in long messages | Ignored |
| RunningStatus | 1 | Running status as the MIDI spec has it, across long-message buffers and around real-time bytes | Each long message starts from the last one's data bytes, and a real-time byte becomes the running status |
| BetterSquareWave | 1 | The built-in bank is `esfm_patch_banks/bnk_com_better_square_wave.bin` | ESS's `bnk_com.bin`, which the driver also carries (resource 1235) |

* With every key at 0, the driver writes the chip's ports exactly as ESS's does: `SettingsTest` in `tests/test_esfmdrv.py` plays a song's worth of messages through both.
* essctl's *ESFM patch bank* page lists the changes that are off and which bank is the driver's own. ESFM > *Restore original bank* puts that bank back.
