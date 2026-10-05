# ES1869 mixer registers

This page is part of the [ES1869 register reference](REGISTERS.md),
generated from `src/esscat.tbl`.

## Mixer registers

Index written to Audio_Base+4h, data at Audio_Base+5h.

### 00h Reset mixer

DS p.58, write-only.

* **7:0 Reset mixer** (`mix.reset`, action, expert). Any write resets the
  mixer registers to their initial values; the record volumes are reset only
  by hardware reset. Software reset does not touch the mixer (DS p.53,
  p.63-64).

### 04h DAC playback volume (SB Pro)

DS p.56, Sound Blaster compatible view.

> SB Pro 3-bit views: bits 7:5 and 3:1 are the top bits of each nibble of
> the extended register

> bits 4 and 0 are cleared by writes and read as 1 (DS p.53)

* **7:5 SB voice volume L** (`sb.voice.l`, level, safe). Sound Blaster Pro
  3-bit view of the Audio 1 play volume 14h, left channel (DS p.53).
* **3:1 SB voice volume R** (`sb.voice.r`, level, safe). Sound Blaster Pro
  3-bit view of the Audio 1 play volume 14h, right channel (DS p.53).

### 0Ah Mic mix volume (SB Pro)

DS p.56, Sound Blaster compatible view.

* **2:1 SB mic mix volume** (`sb.mic`, level, safe). SB Pro view of mic mix
  volume 1Ah: writing 0-3 sets 1Ah to 00h, 55h, AAh or FFh; a read returns
  1Ah bits 3:2 (DS p.53).

### 0Ch ADC source (SB Pro)

DS p.56, Sound Blaster compatible view.

> 0Ch bits 5 and 3 (F1, F0) and 0Eh bit 5 (F2) are SB filter bits that the
> ES1869 ignores (DS p.56)

* **2:1 SB record source** (`sb.adcsrc`, choice, safe). SB Pro view of
  record source 1Ch bits 2:1; bit 0 is cleared by writes and read as 1. Use
  1Ch for the other sources (DS p.53). Values: 0 = Microphone; 1 = CD (Aux);
  2 = Microphone (alt); 3 = Line.

### 0Eh Output control (SB Pro)

DS p.56.

* **1 SB Pro stereo DMA** (`sb.stereo`, bit, expert). 1 = SB Pro stereo DMA
  in Compatibility mode, to the DAC and, for 8-bit data, from the ADC
  (program twice the channel rate). Software reset leaves it set, so clear
  it after the transfer (DS p.44, p.46, p.56).

### 14h Audio 1 play volume

DS p.58.

* **7:4 Audio 1 (wave) volume L** (`mix.a1vol.l`, level, safe, profile,
  driver sets it). Left playback volume of the first audio channel, gated by
  the D1h/D3h speaker commands; reset value 88h (DS p.43, p.55, p.58).
* **3:0 Audio 1 (wave) volume R** (`mix.a1vol.r`, level, safe, profile,
  driver sets it). Right playback volume of the first audio channel, gated
  by the D1h/D3h speaker commands; reset value 88h (DS p.43, p.55, p.58).

### 1Ah Mic mix volume

DS p.58.

* **7:4 Mic volume L** (`mix.micvol.l`, level, safe, profile, driver sets
  it). Left playback volume of the mono Mic input; different L and R values
  pan it. Reset value 00h (DS p.53, p.58).
* **3:0 Mic volume R** (`mix.micvol.r`, level, safe, profile, driver sets
  it). Right playback volume of the mono Mic input; different L and R values
  pan it. Reset value 00h (DS p.53, p.58).

### 1Ch Extended record source

DS p.58.

> the simplified 1Ch table on DS p.53 (x0x mic, 01x CD, 110 line, 111 mixer)
> differs from the register description on p.59, which the catalog follows

* **4 Record mute** (`rec.mute`, bit, safe, profile, driver sets it). 1 =
  mutes the input to the filters for recording; does not affect MONO_OUT (DS
  p.58).
* **2:0 Record source** (`rec.source`, choice, safe, profile, driver sets
  it). Record source in Extended mode. Master volume inputs are the 3-D
  effect outputs before master volume (DS p.59). Values: 0 = Microphone; 1 =
  L Mic, R Master (L+R)/2; 2 = Aux A (CD); 3 = AOUT_L/AOUT_R; 4 = Microphone
  (alt); 5 = Record mixer; 6 = Line; 7 = Master volume inputs.

### 22h Master volume (SB Pro)

DS p.56, Sound Blaster compatible view.

* **7:5 SB master volume L** (`sb.master.l`, level, safe). SB Pro 3-bit
  master volume, left; writes are translated into 60h unless 64h bit 0 is
  set, reads are computed from 60h (DS p.54).
* **3:1 SB master volume R** (`sb.master.r`, level, safe). SB Pro 3-bit
  master volume, right; writes are translated into 62h unless 64h bit 0 is
  set, reads are computed from 62h (DS p.54).

### 26h FM volume (SB Pro)

DS p.56, Sound Blaster compatible view.

* **7:5 SB FM volume L** (`sb.fm.l`, level, safe). Sound Blaster Pro 3-bit
  view of the music DAC (FM) volume 36h, left channel (DS p.53).
* **3:1 SB FM volume R** (`sb.fm.r`, level, safe). Sound Blaster Pro 3-bit
  view of the music DAC (FM) volume 36h, right channel (DS p.53).

### 28h AuxA (CD) volume (SB Pro)

DS p.56, Sound Blaster compatible view.

* **7:5 SB CD volume L** (`sb.cd.l`, level, safe). Sound Blaster Pro 3-bit
  view of the AuxA (CD) volume 38h, left channel (DS p.53).
* **3:1 SB CD volume R** (`sb.cd.r`, level, safe). Sound Blaster Pro 3-bit
  view of the AuxA (CD) volume 38h, right channel (DS p.53).

### 2Eh Line volume (SB Pro)

DS p.56, Sound Blaster compatible view.

* **7:5 SB line volume L** (`sb.line.l`, level, safe). Sound Blaster Pro
  3-bit view of the line volume 3Eh, left channel (DS p.53).
* **3:1 SB line volume R** (`sb.line.r`, level, safe). Sound Blaster Pro
  3-bit view of the line volume 3Eh, right channel (DS p.53).

### 32h Master volume

DS p.59, Sound Blaster compatible view.

* **7:4 Master volume L (4-bit)** (`sb.master4.l`, level, safe).
  Backward-compatible 4-bit view of 60h: writes are translated unless 64h
  bit 0 is set, reads are computed from 60h; reset 88h (DS p.54, p.59,
  p.62).
* **3:0 Master volume R (4-bit)** (`sb.master4.r`, level, safe).
  Backward-compatible 4-bit view of 62h: writes are translated unless 64h
  bit 0 is set, reads are computed from 62h; reset 88h (DS p.54, p.59,
  p.62).

### 36h FM volume

DS p.59.

* **7:4 Music DAC (FM) volume L** (`mix.fmvol.l`, level, safe, profile,
  driver sets it). Left playback volume of the music DAC (FM, ES689/ES69x or
  I2S); reset value 88h (DS p.59).
* **3:0 Music DAC (FM) volume R** (`mix.fmvol.r`, level, safe, profile,
  driver sets it). Right playback volume of the music DAC (FM, ES689/ES69x
  or I2S); reset value 88h (DS p.59).

### 38h AuxA (CD) volume

DS p.59.

* **7:4 AuxA (CD) volume L** (`mix.cdvol.l`, level, safe, profile, driver
  sets it). Left playback volume of the AuxA (CD) input; reset value 00h (DS
  p.59).
* **3:0 AuxA (CD) volume R** (`mix.cdvol.r`, level, safe, profile, driver
  sets it). Right playback volume of the AuxA (CD) input; reset value 00h
  (DS p.59).

### 3Ah AuxB volume

DS p.59.

* **7:4 AuxB volume L** (`mix.auxbvol.l`, level, safe, profile, driver sets
  it). Left playback volume of the AuxB input; reset value 00h (DS p.59).
* **3:0 AuxB volume R** (`mix.auxbvol.r`, level, safe, profile, driver sets
  it). Right playback volume of the AuxB input; reset value 00h (DS p.59).

### 3Ch PC speaker volume

DS p.59.

* **2:0 PC speaker volume** (`mix.pcspkvol`, level, safe, profile, driver
  sets it). Volume of the PC speaker input PCSPKI on the PCSPKO pin
  (resistor ladder, see DS p.26); reset value 04h (DS p.59).

### 3Eh Line volume

DS p.59.

* **7:4 Line volume L** (`mix.linevol.l`, level, safe, profile, driver sets
  it). Left playback volume of the line input; reset value 00h (DS p.59).
* **3:0 Line volume R** (`mix.linevol.r`, level, safe, profile, driver sets
  it). Right playback volume of the line input; reset value 00h (DS p.59).

### 40h ES1869 identification

DS p.59, reading changes state.

> the third read is A[11:8], the high bits of the configuration base (DS
> p.59); p.43 prints it as "A[11:0]"

* **7:0 Identification value** (`stat.chipid`, value, read-only). Four
  successive reads return 18h, 69h, then Config_Base bits 11:8 and 7:0;
  every read advances the sequence (DS p.59).

### 42h Serial mode input control

DS p.59.

> p.25 says 42h bits 7 and 6 select MONO_IN in Serial mode, but none of the
> codes on p.60 is MONO_IN; 46h bit 0 routes it to the left filter

* **7 Serial input override** (`ser.in.override`, bit, caution). 1 = source
  and level below take effect in Serial mode, overriding 1Ch and B4h. Serial
  mode needs DCLK and the SE pin or 48h bit 7 (DS p.59).
* **6:4 Serial record source** (`ser.in.source`, choice, caution). Record
  source used during Serial mode when bit 7 is set (DS p.60). Values: 0 =
  Line; 1 = Aux A (CD); 2 = Microphone; 3 = Master volume inputs; 4 = L Mic,
  R Master (L+R)/2; 5 = AOUT_L/AOUT_R; 6 = Record mixer; 7 = Disconnected
  (muted).
* **3:0 Serial record level** (`ser.in.level`, level, caution). Record level
  in Serial mode: microphone 0 to +22.5 dB, other sources -6 to +16.5 dB, in
  1.5 dB steps (DS p.60).

### 44h Serial mode output control

DS p.60.

* **7 Serial master volume override** (`ser.out.override`, bit, caution).
  1 = master volume during Serial mode comes from bits 3:0 instead of the
  master volume registers (DS p.60).
* **6:4 Serial output select** (`ser.out.select`, choice, caution). Signal
  fed to the master volume stage during Serial mode; the Audio 1 DAC choices
  override record monitor and record mute (DS p.60). Values: 0 = Mute; 1 =
  Normal; 2 = Audio 1 DAC only; 3 = Normal (alt); 4 = Mixer, Audio 1 at 0
  dB; 5 = Mixer, Audio 1 muted.
* **3:0 Serial master volume** (`ser.out.mastervol`, level, caution). Master
  volume during Serial mode when bit 7 is set: 0 = mute, 15 = maximum (0 dB)
  (DS p.60).

### 46h Serial mode analog control

DS p.60.

> the FDXO text calls 48h bit 6 the serial reset, but the 48h description
> puts serial reset in bit 5 (DS p.60)

* **7 Serial analog override** (`ser.an.override`, bit, caution). 1 = bits
  5:0 of this register take effect during Serial mode; 0 = they never take
  effect (DS p.60).
* **6 Music mixer test** (`ser.an.music_test`, bit, expert). Test feature:
  1 = the music DAC mixer inputs are replaced with the AUXB_L and AUXB_R
  inputs (DS p.60).
* **5 Serial left channel ADC** (`ser.an.adc_l`, bit, caution). 1 = the left
  channel combined ADC and DAC is in ADC mode, 0 = DAC mode (DS p.60).
* **4 Serial right channel ADC** (`ser.an.adc_r`, bit, caution). 1 = the
  right channel combined ADC and DAC is in ADC mode, 0 = DAC mode (DS p.60).
* **3 Serial mono mode** (`ser.an.mono`, bit, caution). 1 = mono record,
  mono playback or mono full-duplex modes; 0 = stereo playback or stereo
  record (DS p.60).
* **1 FDXO enable** (`ser.an.fdxo`, bit, caution). ES1868 compatible: in
  Serial mode with bit 7 set and serial reset off, MONO_OUT buffers CIN_R,
  overriding 7Dh bits 2:1 (DS p.60).
* **0 FDXI enable** (`ser.an.fdxi`, bit, caution). In Serial mode with bit 5
  set, the left channel filter input is MONO_IN instead of the selected
  record source (DS p.60).

### 48h Serial mode control

DS p.60.

> the footnote on DS p.55 puts telegaming in 48h bit 0, but the 48h
> description (p.61) and p.20 use bit 1 and reserve bit 0

* **7 Serial port enable (SW SE)** (`ser.enable`, bit, caution). 1 = enable
  the DSP serial port (OR'd with the SE pin); synchronized to DCLK, so it
  has no effect while DCLK is not running (DS p.60).
* **6 Serial data signed** (`ser.signed`, bit, caution). 1 = serial data is
  signed two's complement, 0 = unsigned (offset binary) (DS p.60).
* **5 Serial interface reset** (`ser.reset`, bit, caution). 1 = hold the DSP
  serial interface in reset, which resets the left/right flags of stereo
  modes; 0 = release (DS p.60).
* **4 ES689/ES69x interface** (`ser.es689`, bit, caution, driver sets it).
  1 = an ES689/ES69x may use the music DAC while MCLK is seen high at least
  every 20 us; FM volume 36h applies (DS p.61).
* **3 Active-low frame sync** (`ser.sync_low`, bit, caution). 1 = frame sync
  pulses FSR and FSX are active-low, 0 = active-high (DS p.61).
* **2 DSP test mode** (`ser.test_mode`, bit, expert). Test mode: DCLK, FSX
  and FSR become outputs; DCLK runs at 1.5876 MHz and the frame rate comes
  from 4Ah (DS p.61).
* **1 Telegaming mode** (`ser.telegaming`, bit, caution). Telegaming mode:
  in Serial mode Audio 1 DMA plays through the Audio 2 DAC at the 14h
  volume; 0 = Audio 1 unheard (DS p.19, p.61).

### 4Ah FSX/FSR rate control (test)

DS p.61.

* **6:0 Test frame sync divisor** (`ser.fs_divisor`, value, expert). Two's
  complement divisor setting the FSX/FSR frame rate in DSP test mode (48h
  bit 2); 5Ch (-36) gives 44.1 kHz (DS p.61).

### 4Ch Serial mode filter divider

DS p.61.

* **7 Serial filter override** (`ser.filter.override`, bit, caution). 1 =
  during Serial mode the Audio 1 DAC and ADC switched-capacitor filters are
  clocked from DCLK; 0 = no effect (DS p.61).
* **3:0 Serial filter divider** (`ser.filter.divider`, value, caution).
  Two's complement divider of DCLK for the filter clock; the filter -3 dB
  point is about clock/41, e.g. 02h (-14), 0Eh (-2) (DS p.61).

### 4Eh Serial mode format/source/target

DS p.61.

* **7:6 Serial transmit source** (`ser.tx.source`, choice, caution). Source
  of the serial transmit register: none (held at zero), Audio 1 DMA FIFO in
  playback direction, or Audio 1 ADC (DS p.61). Values: 0 = None (zero); 1 =
  Audio 1 DMA FIFO; 2 = Audio 1 ADC.
* **5 Transmit 16-bit** (`ser.tx.16bit`, bit, caution). 1 = transmit length
  is 16 bits, 0 = 8 bits (DS p.61).
* **4 Transmit stereo** (`ser.tx.stereo`, bit, caution). 1 = transmit
  stereo, left and right alternating with left first; 0 = mono (DS p.61).
* **3:2 Serial receive target** (`ser.rx.target`, choice, caution). Target
  of the serial receive register: none (held at zero), Audio 1 DMA FIFO in
  record direction, or Audio 1 DAC (DS p.61). Values: 0 = None (zero); 1 =
  Audio 1 DMA FIFO; 2 = Audio 1 DAC.
* **1 Receive 16-bit** (`ser.rx.16bit`, bit, caution). 1 = receive length is
  16 bits, 0 = 8 bits (DS p.61).
* **0 Receive stereo** (`ser.rx.stereo`, bit, caution). 1 = receive stereo,
  left and right alternating with left first; 0 = mono (DS p.61).

### 50h 3-D enable

DS p.62.

* **3 3-D effect** (`fx.3d.enable`, bit, safe, profile, driver sets it). 1 =
  enable the Spatializer VBX 3-D effect, 0 = bypass it; the effect also
  needs bit 2 (released from reset) (DS p.62).
* **2 3-D released from reset** (`fx.3d.run`, bit, caution, driver sets it).
  Active-low reset of the 3-D effect: 1 = release from reset, 0 = reset (DS
  p.62).
* **1 3-D model (undocumented)** (`fx.3d.mono`, bit, caution, profile,
  driver sets it). Reserved in the data sheet (write 0). Measured: the
  effect makes its S from the mono sum through its band-pass and drops the
  program's own S, a stereo effect made from mono. NetBSD calls it MONO (DS
  p.62, docs/SPATIALIZER.md).
* **0 3-D limit (undocumented)** (`fx.3d.limit`, bit, caution, profile,
  driver sets it). Reserved in the data sheet (write 0); ESS's drivers set
  it from their 3D Limit setting, 0 by default. Measured: holds the S out at
  a level against the M that 54h-5Ah set, moving the boost at the rates in
  58h (DS p.62, docs/SPATIALIZER.md).

### 52h 3-D level

DS p.62.

* **5:0 3-D level** (`fx.3d.level`, level, safe, profile, driver sets it).
  Amount of 3-D effect: 0 = minimum, 3Fh = maximum, in steps of 0.75 dB
  (measured, about +17 dB at 400 Hz at 3Fh); the space control bits of ESS's
  ES938. Reset to zero by hardware reset (DS p.62, docs/SPATIALIZER.md).

### 54h 3-D limit level (undocumented)

Not in the data sheet.

> ESS's Windows 95 and NT drivers write 54h, 56h, 58h and 5Ah with 8Fh, 95h,
> 94h and 80h each time they enable the device with 3-D on, and Linux writes
> them as "recommended values". ESS's Windows 98 driver leaves them alone.
> No data sheet describes them (docs/SPATIALIZER.md).

* **7:0 3-D limit level** (`fx.3d.reg54`, value, caution, profile, driver
  sets it). Undocumented; ESS's drivers and Linux write 8Fh. Measured with
  the limit on: bits 6:0 add to 56h's to raise the level it holds, +5.5 dB
  over the M at 8Fh, +9.8 at FFh; bit 7 clear stops the boost coming back
  (docs/SPATIALIZER.md).

### 56h 3-D limit level 2 (undocumented)

Not in the data sheet.

* **7:0 3-D limit level 2** (`fx.3d.reg56`, value, caution, profile, driver
  sets it). Undocumented; ESS's drivers and Linux write 95h. Measured with
  the limit on: bits 6:0 add to 54h's to set the level it holds; bit 7 clear
  with 54h's lowers that level about 12 dB. Nothing with the limit off
  (docs/SPATIALIZER.md).

### 58h 3-D limit speed (undocumented)

Not in the data sheet.

* **7:0 3-D limit speed** (`fx.3d.reg58`, value, caution, profile, driver
  sets it). Undocumented; ESS's drivers and Linux write 94h. Measured with
  the limit on: the boost falls at 257/(n+1) dB a second, n = bits 3:0, and
  rises h+1 times slower, h = bits 7:4: 52 and 5.2 dB a second at 94h
  (docs/SPATIALIZER.md).

### 5Ah 3-D limit trim (undocumented)

Not in the data sheet.

* **7:0 3-D limit trim** (`fx.3d.reg5a`, value, caution, profile, driver
  sets it). Undocumented; ESS's drivers and Linux write 80h. Measured with
  the limit on: a fine trim of the level it holds, by the byte less 80h,
  from -1.2 dB at 00h to +1.0 dB at FFh. Nothing with the limit off
  (docs/SPATIALIZER.md).

### 60h Left master volume and mute

DS p.62.

* **6 Master mute L** (`master.mute.l`, bit, safe, profile, driver sets it).
  1 = mute the left master output (DS p.62).
* **5:0 Master volume L** (`master.vol.l`, level, safe, profile, driver sets
  it). Left master volume presented to the analog output, 0.75 dB per step
  up to 63 (0 dB); 36h after hardware reset (DS p.25, p.59, p.62).

### 61h Left hardware volume counter

DS p.62.

> 61h and 63h are only separate registers in split mode (64h bit 7 = 1),
> otherwise they're combined with 60h and 62h (DS p.62)

* **6 HW volume mute L** (`hwvol.mute.l`, bit, caution). Left mute kept by
  the hardware volume controls in split mode; host software copies it to 60h
  (DS p.62).
* **5:0 HW volume counter L** (`hwvol.count.l`, level, caution). Left
  hardware volume counter in split mode; host software reads it and updates
  master volume 60h (DS p.62).

### 62h Right master volume and mute

DS p.62.

* **6 Master mute R** (`master.mute.r`, bit, safe, profile, driver sets it).
  1 = mute the right master output (DS p.62).
* **5:0 Master volume R** (`master.vol.r`, level, safe, profile, driver sets
  it). Right master volume presented to the analog output, 0.75 dB per step
  up to 63 (0 dB); 36h after hardware reset (DS p.25, p.59, p.62).

### 63h Right hardware volume counter

DS p.62.

* **6 HW volume mute R** (`hwvol.mute.r`, bit, caution). Right mute kept by
  the hardware volume controls in split mode; host software copies it to 62h
  (DS p.62).
* **5:0 HW volume counter R** (`hwvol.count.r`, level, caution). Right
  hardware volume counter in split mode; host software reads it and updates
  master volume 62h (DS p.62).

### 64h Master volume control

DS p.62.

> DS p.17 says the hardware volume interrupt is polled at 64h bit 3, but the
> 64h description (p.62) uses bit 4

> ES1869.VXD rewrites 64h: bits 5 and 3:2 at device start, bits 1:0 in
> HwVol_Int_Enable/Disable, all of it in Save/Restore_DOS_Mixer

* **7 Split mode** (`hwvol.split`, bit, caution). 1 = split the HW volume
  counters 61h/63h from 60h/62h; host software then updates the master
  volume. 0 = slaved together (DS p.62).
* **6 MPU-401 IRQ mask (1 = on)** (`irq.mpu401.mask`, bit, expert). AND'ed
  with the MPU-401 interrupt request; if low, the request stays low. Cleared
  by hardware reset (DS p.62).
* **5 Count by 3** (`hwvol.count_by_3`, bit, caution, driver sets it). 1 =
  each push of Up or Down changes the volume by 3 (2.25 dB), 0 = by 1;
  cleared by hardware reset (DS p.25, p.62).
* **4 HW volume IRQ request** (`hwvol.irq`, status, read-only). Read-only
  interrupt request from a hardware volume event; any write to 66h clears it
  (DS p.62, p.63).
* **3:2 HW volume button mode** (`hwvol.mode`, choice, caution, driver sets
  it). Button mode: normal 3-wire; 2-wire (Up and Down low together = Mute);
  2-wire with 10 us debounce and no auto-increment; or disabled (DS p.63).
  Values: 0 = Three-wire; 1 = Two-wire; 2 = Two-wire, fast debounce; 3 =
  Disabled.
* **1 HW volume IRQ mask (1 = on)** (`hwvol.irq_mask`, bit, expert, driver
  sets it). AND'ed with the hardware volume interrupt request before it is
  OR'd into the Audio 1 interrupt; cleared by hardware reset (DS p.63).
* **0 Disable SB Pro master emulation** (`master.sbpro_off`, bit, caution,
  driver sets it). 0 = writes to 22h/32h and mixer reset update 60h/62h; 1 =
  22h/32h are in effect read-only. Cleared by hardware reset (DS p.63).

### 65h Opamp calibration control

DS p.63.

* **0 Calibrate op-amps** (`pwr.opamp_cal`, action, caution). Writing 1
  starts op-amp calibration (about 200 ms, AOUT_L/R and MONO_OUT muted);
  reads 1 while it runs. Also runs after hardware reset (DS p.63).

### 66h Clear hardware volume interrupt request

DS p.63, write-only.

> Table 24 (DS p.57) says write-only; the register description on p.63 heads
> it "(66h, R/W)"

* **7:0 Clear HW volume IRQ** (`hwvol.irq_clear`, action, expert). Any write
  to this register resets the hardware volume interrupt request (DS p.63).

### 68h Mic record volume

DS p.63.

* **7:4 Mic record volume L** (`rec.micvol.l`, level, safe, profile, driver
  sets it). Left record mixer volume of the mic input; cleared by hardware
  reset but not by mixer reset (DS p.63).
* **3:0 Mic record volume R** (`rec.micvol.r`, level, safe, profile, driver
  sets it). Right record mixer volume of the mic input; cleared by hardware
  reset but not by mixer reset (DS p.63).

### 69h Audio 2 record volume

DS p.63.

* **7:4 Audio 2 record volume L** (`rec.a2vol.l`, level, safe, profile,
  driver sets it). Left record mixer volume of the second audio channel;
  cleared by hardware reset but not by mixer reset (DS p.63).
* **3:0 Audio 2 record volume R** (`rec.a2vol.r`, level, safe, profile,
  driver sets it). Right record mixer volume of the second audio channel;
  cleared by hardware reset but not by mixer reset (DS p.63).

### 6Ah AuxA (CD) record volume

DS p.63.

* **7:4 CD record volume L** (`rec.cdvol.l`, level, safe, profile, driver
  sets it). Left record mixer volume of the AuxA (CD) input; cleared by
  hardware reset but not by mixer reset (DS p.63).
* **3:0 CD record volume R** (`rec.cdvol.r`, level, safe, profile, driver
  sets it). Right record mixer volume of the AuxA (CD) input; cleared by
  hardware reset but not by mixer reset (DS p.63).

### 6Bh Music DAC record volume

DS p.63.

* **7:4 Music DAC record volume L** (`rec.fmvol.l`, level, safe, profile,
  driver sets it). Left record mixer volume of the music DAC (FM or
  wavetable); cleared by hardware reset but not by mixer reset (DS p.63).
* **3:0 Music DAC record volume R** (`rec.fmvol.r`, level, safe, profile,
  driver sets it). Right record mixer volume of the music DAC (FM or
  wavetable); cleared by hardware reset but not by mixer reset (DS p.63).

### 6Ch AuxB record volume

DS p.63.

* **7:4 AuxB record volume L** (`rec.auxbvol.l`, level, safe, profile,
  driver sets it). Left record mixer volume of the AuxB input; cleared by
  hardware reset but not by mixer reset (DS p.63).
* **3:0 AuxB record volume R** (`rec.auxbvol.r`, level, safe, profile,
  driver sets it). Right record mixer volume of the AuxB input; cleared by
  hardware reset but not by mixer reset (DS p.63).

### 6Dh Mono_In play mix

DS p.63.

* **7:4 MONO_IN volume L** (`mix.monoinvol.l`, level, safe, profile, driver
  sets it). Left playback mixer volume of the MONO_IN input (DS p.63).
* **3:0 MONO_IN volume R** (`mix.monoinvol.r`, level, safe, profile, driver
  sets it). Right playback mixer volume of the MONO_IN input (DS p.63).

### 6Eh Line record volume

DS p.64.

* **7:4 Line record volume L** (`rec.linevol.l`, level, safe, profile,
  driver sets it). Left record mixer volume of the line input; cleared by
  hardware reset but not by mixer reset (DS p.64).
* **3:0 Line record volume R** (`rec.linevol.r`, level, safe, profile,
  driver sets it). Right record mixer volume of the line input; cleared by
  hardware reset but not by mixer reset (DS p.64).

### 6Fh Mono_In record volume

DS p.64.

* **7:4 MONO_IN record volume L** (`rec.monoinvol.l`, level, safe, profile,
  driver sets it). Left record mixer volume of the MONO_IN input; cleared by
  hardware reset but not by mixer reset (DS p.64).
* **3:0 MONO_IN record volume R** (`rec.monoinvol.r`, level, safe, profile,
  driver sets it). Right record mixer volume of the MONO_IN input; cleared
  by hardware reset but not by mixer reset (DS p.64).

### 70h Audio 2 sample rate

DS p.64.

* **7:0 Audio 2 sample rate** (`a2.rate`, value, expert, driver sets it).
  Bit 7 picks the 768 kHz (1) or 793.8 kHz (0) clock; rate = clock / (128 -
  bits 6:0), e.g. F0h = 48 kHz, 6Eh = 44.1 kHz (DS p.64).

### 71h Audio 2 mode

DS p.64.

* **5 New A1h rate mode** (`a2.new_a1`, bit, caution, driver sets it). 1 =
  A1h behaves like 70h, giving accurate rates that divide 48 kHz; 0 = A1h
  behaves as in earlier AudioDrive chips (DS p.64).
* **4 Audio 2 4x oversampling** (`a2.oversample4x`, bit, caution, driver
  sets it). 1 = the Audio 2 DAC is in 4x oversampling mode, which always
  bypasses its switched-capacitor filter (DS p.64).
* **3 Audio 2 filter bypass** (`a2.scf_bypass`, bit, caution, driver sets
  it). 1 = bypass the switched-capacitor filter of the Audio 2 DAC (DS
  p.64).
* **2 Audio 1 filter bypass** (`a1.scf_bypass`, bit, caution, driver sets
  it). 1 = bypass the switched-capacitor filter of the Audio 1 CODEC, which
  is in the ADC's input path too (DS p.25, p.64).
* **1 Audio 2 asynchronous** (`a2.async`, bit, expert, driver sets it). 1 =
  the Audio 2 DAC runs at its own rate (70h, 72h); 0 = it is slaved to the
  Audio 1 sample and filter rate (DS p.64).
* **0 Audio 2 mixed into FM** (`a2.fm_mix`, bit, caution). 1 = Audio 2 DMA
  is slaved to the FM synthesizer sample rate and digitally mixed into the
  FM output (DS p.64).

### 72h Audio 2 filter clock rate

DS p.64.

* **7:0 Audio 2 filter clock** (`a2.filter`, value, expert, driver sets it).
  Filter clock divider of the Audio 2 switched-capacitor filter in
  asynchronous mode, programmed like A2h; reset to zero (DS p.64).

### 74h Audio 2 transfer count reload low

DS p.64.

* **7:0 Audio 2 count reload low** (`a2.count.lo`, value, expert, driver
  sets it). Low byte of the two's complement Audio 2 DMA transfer count
  reload (DS p.64).

### 76h Audio 2 transfer count reload high

DS p.64.

* **7:0 Audio 2 count reload high** (`a2.count.hi`, value, expert, driver
  sets it). High byte of the two's complement Audio 2 DMA transfer count
  reload (DS p.64).

### 78h Audio 2 control 1

DS p.64.

> Table 24 (DS p.58) names 78h bit 0 "Enable full-duplex mode", but the
> description (p.65) calls it "Enable FIFO to 2nd chan DAC"

> the ES1868 data sheet says a hardware or software reset clears 78h; the
> ES1869's says that only of 7Ah (DS p.65)

* **7:6 Audio 2 DMA transfer type** (`a2.xfer`, choice, expert, driver sets
  it). Single transfer (1 DACK per DRQ) or demand transfer with 2, 4 or 8
  DACKs per DRQ (DS p.65). Values: 0 = Single; 1 = Demand 2; 2 = Demand 4;
  3 = Demand 8.
* **4 Audio 2 auto-initialize** (`a2.autoinit`, bit, expert, driver sets
  it). 1 = the counter reloads at zero and DMA continues; 0 = normal mode,
  DMA stops and bit 1 is cleared (DS p.65).
* **1 Audio 2 DMA enable** (`a2.dma_enable`, bit, expert, driver sets it).
  1 = Audio 2 DMA writes into the 32-word Audio 2 FIFO; cleared at terminal
  count unless auto-initialize (DS p.65).
* **0 Audio 2 FIFO to DAC** (`a2.fifo_enable`, bit, expert, driver sets it).
  1 = data moves from the FIFO to the Audio 2 DAC (or to the DSP serial port
  or the FM mix) (DS p.65).

### 7Ah Audio 2 control 2

DS p.65.

> DS p.49 and p.53 say 7Ah bits 4:3 pick the playback or record mixer as the
> Mixer record source, but the 7Ah description (p.65) reserves bits 5:3

> 7Ah is reset to zero by hardware or software reset (DS p.65), so it's
> FF_VOLATILE

> ES1869.DRV also writes 7Ah for every playback

* **7 Audio 2 IRQ latch** (`a2.irq`, bit, expert, driver sets it, reset by
  DSP reset). Set when the DMA counter rolls over to zero or when 1 is
  written; write 0 to clear it (DS p.65).
* **6 Audio 2 IRQ mask (1 = on)** (`a2.irq_mask`, bit, expert, driver sets
  it, reset by DSP reset). AND'ed with bit 7 to produce the Audio 2
  interrupt request (DS p.65).
* **2 Audio 2 signed data** (`a2.signed`, bit, expert, driver sets it, reset
  by DSP reset). 1 = data is signed two's complement, 0 = unsigned (DS
  p.65).
* **1 Audio 2 stereo** (`a2.stereo`, bit, expert, driver sets it, reset by
  DSP reset). 1 = stereo data, 0 = mono. With the DSP serial interface,
  stereo is for Interleave mode (DS p.52, p.65).
* **0 Audio 2 16-bit** (`a2.16bit`, bit, expert, driver sets it, reset by
  DSP reset). 1 = 16-bit samples, 0 = 8-bit samples (DS p.65).

### 7Ch Audio 2 DAC mixer volume

DS p.65.

> DS p.51 says a software reset sets 7Ch to zero, but the 7Ch description
> (p.65) only names hardware reset, so no FF_VOLATILE

* **7:4 Audio 2 volume L** (`mix.a2vol.l`, level, safe, profile, driver sets
  it). Left playback mixer volume of the Audio 2 DAC; reset to zero by
  hardware reset (DS p.65).
* **3:0 Audio 2 volume R** (`mix.a2vol.r`, level, safe, profile, driver sets
  it). Right playback mixer volume of the Audio 2 DAC; reset to zero by
  hardware reset (DS p.65).

### 7Dh Mic preamp, MONO_IN and MONO_OUT

DS p.65.

* **3 Mic +26 dB preamp** (`fx.mic.boost`, bit, safe, profile, driver sets
  it). 1 = +26 dB microphone preamp gain, 0 = 0 dB; on after hardware reset,
  which sets 7Dh to 08h (DS p.65).
* **2:1 MONO_OUT source** (`fx.monoout.source`, choice, safe, profile,
  driver sets it). Source of the MONO_OUT pin; overridden in Serial mode by
  the FDXO setting of 46h (DS p.60, p.65). Values: 0 = Mute (CMR); 1 = CIN_R
  pin; 2 = Audio 2 DAC right; 3 = Record level mono mix.
* **0 MONO_IN to line out** (`fx.monoin.direct`, bit, safe, profile, driver
  sets it). 1 = MONO_IN is mixed at unity gain with AOUT_L/R after the
  playback mixer, 3-D effect and master volume (DS p.65).

### 7Fh I2S interface

DS p.65.

* **4 Music DAC digital record** (`rec.music_digital`, bit, safe, profile).
  1 = record the music DAC data (FM, ES689/ES69x or I2S) digitally on Audio
  1 in stereo, at the music DAC rate instead of A1h (DS p.65).
* **3 I2S data activity** (`fx.i2s.data_seen`, bit, caution). Latched high
  if IISDATA has been high at least once since it was last cleared by
  software (DS p.65).
* **2 I2S clock activity** (`fx.i2s.clock_seen`, bit, caution). Latched high
  if IISCLK and IISLR have been high at the same time since it was last
  cleared by software (DS p.66).
* **1 MODE pin** (`fx.i2s.mode_pin`, status, read-only). Read-only state of
  the MODE input pin, which must be high for the I2S serial interface to be
  enabled (DS p.66).
* **0 I2S drives music DAC** (`fx.i2s.enable`, bit, safe, profile, driver
  sets it). 1 = the I2S serial interface takes control of the music DAC; 0 =
  the FM synthesizer or ES689/ES69x interface uses it. ESS's driver sets it
  while no program has the MIDI synthesizer open, unless Options > FM keeps
  the music DAC is on (DS p.66).
