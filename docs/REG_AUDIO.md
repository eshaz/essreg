# ES1869 controller registers and audio ports

This page is part of the [ES1869 register reference](REGISTERS.md),
generated from `src/esscat.tbl`.

## Controller registers

Written with DSP command Axh/Bxh plus the value, read with C0h and the
register after C6h (extended mode). They go through the DSP command channel,
which Windows' driver uses for playback, so essctl reads them only on
request.

### A1h Extended mode sample rate generator

DS p.67, DSP channel.

* **7:0 Audio 1 sample rate** (`a1.rate`, value, expert, driver sets it,
  reset by DSP reset). Rate = 397.7 kHz / (128 - x) if bit 7 = 0, 795.5 kHz
  / (256 - x) if bit 7 = 1. Mixer 71h bit 5 makes it work like 70h: 768 or
  793.8 kHz / (128 - bits 6:0) (DS p.64, p.67).

### A2h Filter divider

DS p.68, DSP channel.

* **7:0 Audio 1 filter clock** (`a1.filter`, value, expert, driver sets it,
  reset by DSP reset). Filter clock = 7.16 MHz / (256 - x); the roll-off is
  clock/82 and belongs at 80-90% of half the sample rate (DS p.68).

### A4h DMA transfer count reload low

DS p.68, DSP channel.

* **7:0 Audio 1 count reload low** (`a1.count.lo`, value, expert, driver
  sets it, reset by DSP reset). Low byte of the two's complement Audio 1 DMA
  transfer counter reload; reset value 00h (DS p.68).

### A5h DMA transfer count reload high

DS p.68, DSP channel.

* **7:0 Audio 1 count reload high** (`a1.count.hi`, value, expert, driver
  sets it, reset by DSP reset). High byte of the two's complement Audio 1
  DMA transfer counter reload; reset value F8h (DS p.68).

### A8h Analog control

DS p.68, DSP channel.

> A8h bit 4 must be written as 1 and bits 7:5 and 2 as 0, which a
> read-modify-write keeps (DS p.68)

* **7:5 A8h bits 7:5 (undocumented)** (`a1.analog.bits7_5`, value, expert,
  driver sets it, reset by DSP reset). Reserved in the data sheet (write 0),
  but ESS's driver writes 111b when it starts recording (F5h or F6h,
  docs/DRIVER_CONFIG.md).
* **3 Record monitor** (`rec.monitor`, bit, caution, driver sets it, reset
  by DSP reset). 1 = enable record monitor, so AOUT_L/R stay live while the
  CODEC records (ADC direction); cleared by software reset (DS p.50, p.51,
  p.68).
* **2 A8h bit 2 (undocumented)** (`a1.analog.bit2`, bit, expert, driver sets
  it, reset by DSP reset). Reserved in the data sheet (write 0), but ESS's
  driver writes 1 when it starts recording (F5h or F6h,
  docs/DRIVER_CONFIG.md).
* **1:0 Audio 1 stereo/mono** (`a1.channels`, choice, expert, driver sets
  it, reset by DSP reset). Operation mode of the first DMA converters: 01 =
  stereo, 10 = mono; 00 and 11 are reserved (DS p.68). Values: 1 = Stereo;
  2 = Mono.

### B1h Legacy audio interrupt control

DS p.68, DSP channel.

> B1h and B2h bit 4 have "no function" (DS p.68-69), but the programming
> steps set it (p.49-50)

* **7 Game compatible IRQ** (`a1.irq.game`, bit, expert, driver sets it,
  reset by DSP reset). Reserved for Compatibility mode; leave zero for
  Extended mode (DS p.68).
* **6 IRQ on DMA counter overflow** (`a1.irq.dma`, bit, expert, driver sets
  it, reset by DSP reset). 1 = interrupt on each overflow of the DMA counter
  in Extended mode DMA (DS p.68).
* **5 IRQ on FIFO half-empty edge** (`a1.irq.fifo`, bit, expert, driver sets
  it, reset by DSP reset). 1 = interrupt on FIFO half-empty transitions
  during Extended mode block I/O to or from the FIFO (DS p.68).
* **3:0 Audio 1 IRQ (decoded)** (`a1.irq.selected`, choice, read-only).
  Read-only decode of the interrupt number selected for the Audio 1
  interrupt (DS p.69). Values: 0 = IRQ 2, 9 or other; 5 = IRQ 5; 10 = IRQ 7;
  15 = IRQ 10.

### B2h DRQ control

DS p.69, DSP channel.

* **7 Game compatible DRQ** (`a1.drq.game`, bit, expert, driver sets it,
  reset by DSP reset). Reserved for Compatibility mode; leave zero for
  Extended mode (DS p.69).
* **6 DRQ for Extended mode DMA** (`a1.drq.ext`, bit, expert, driver sets
  it, reset by DSP reset). 1 = enable DRQ outputs and DACKB inputs for
  Extended mode DMA; 0 = block I/O to or from the FIFO (DS p.69).
* **5 DRQ for game compatible DMA** (`a1.drq.game_dma`, bit, expert, driver
  sets it, reset by DSP reset). Reserved for Compatibility mode; leave zero
  for Extended mode. With bits 5 and 6 low the Audio 1 DRQ is always low (DS
  p.69).
* **3:0 Audio 1 DMA (decoded)** (`a1.drq.selected`, choice, read-only).
  Read-only decode of the DMA channel selected for the first audio DMA
  channel (DS p.69). Values: 0 = Other; 5 = DRQ 0; 10 = DRQ 1; 15 = DRQ 3.

### B4h Record level

DS p.69, DSP channel.

> DS p.67 and p.69 put the RIGHT record level in B4h bits 7:4 and LEFT in
> 3:0, unlike the mixer registers

> ES1869.DRV writes B4h the same way (5:297A)

* **7:4 Record level R** (`rec.level.r`, level, caution, driver sets it,
  reset by DSP reset). Right record level: microphone 0 to +22.5 dB, other
  sources -6 to +16.5 dB, in 1.5 dB steps (DS p.69).
* **3:0 Record level L** (`rec.level.l`, level, caution, driver sets it,
  reset by DSP reset). Left record level: microphone 0 to +22.5 dB, other
  sources -6 to +16.5 dB, in 1.5 dB steps (DS p.69).

### B5h DAC direct access holding low

DS p.69, DSP channel.

* **7:0 DAC holding register low** (`a1.hold.lo`, value, expert, reset by
  DSP reset). Low byte of the DAC direct access holding register, which
  stores 16-bit data for the 8-bit bus (DS p.69).

### B6h DAC direct access holding high

DS p.69, DSP channel.

* **7:0 DAC holding register high** (`a1.hold.hi`, value, expert, reset by
  DSP reset). High byte of the DAC direct access holding register, which
  stores 16-bit data for the 8-bit bus (DS p.69).

### B7h Audio 1 control 1

DS p.69, DSP channel.

> Table 25 (DS p.67) shows B7h bit 0 as a fixed 1 and bit 5 as "Data type
> select", but the catalog follows the descriptions (p.69-70)

> B7h bit 4 must be written as 1 and bit 1 as 0 (DS p.70)

* **7 FIFO to/from CODEC** (`a1.fifo.codec`, bit, expert, driver sets it,
  reset by DSP reset). 1 = connect the first DMA FIFO to the DAC or ADC,
  allowing transfers between the FIFO and the analog circuitry (DS p.69).
* **6 Opposite of bit 3** (`a1.fifo.not_stereo`, bit, expert, driver sets
  it, reset by DSP reset). Reserved function; must be set to the opposite of
  bit 3: high for mono, low for stereo (DS p.70).
* **5 FIFO signed** (`a1.fifo.signed`, bit, expert, driver sets it, reset by
  DSP reset). 1 = first DMA FIFO in two's complement (signed) mode, 0 =
  unsigned (offset 8000h) (DS p.70).
* **3 FIFO stereo** (`a1.fifo.stereo`, bit, expert, driver sets it, reset by
  DSP reset). 1 = first DMA FIFO in stereo mode, 0 = mono; bit 6 must be the
  opposite (DS p.70).
* **2 FIFO 16-bit** (`a1.fifo.16bit`, bit, expert, driver sets it, reset by
  DSP reset). 1 = first DMA FIFO in 16-bit mode, 0 = 8-bit mode (DS p.70).
* **0 Load DAC from holding register** (`a1.dac_load`, action, expert, reset
  by DSP reset). Write 1 to copy the DAC direct access holding register to
  the DAC on the next sample rate clock; the bit then clears (DS p.70).

### B8h Audio 1 control 2

DS p.70, DSP channel.

* **3 CODEC in ADC mode** (`a1.codec_adc`, bit, expert, driver sets it,
  reset by DSP reset). 1 = first DMA converter in ADC mode, 0 = DAC mode (DS
  p.70).
* **2 Audio 1 auto-initialize** (`a1.autoinit`, bit, expert, driver sets it,
  reset by DSP reset). 1 = auto-initialize DMA mode, 0 = normal DMA mode (DS
  p.70).
* **1 Audio 1 DMA read** (`a1.dma_read`, bit, expert, driver sets it, reset
  by DSP reset). 1 = first DMA is a read (ADC operation), 0 = a write (DAC
  operation) (DS p.70).
* **0 Audio 1 DMA enable** (`a1.dma_enable`, bit, expert, driver sets it,
  reset by DSP reset). First DMA active-low reset: 1 = DMA is allowed to
  proceed, 0 = stops it (DS p.70).

### B9h Audio 1 transfer type

DS p.70, DSP channel.

> the programming steps (DS p.48, p.50) give B9h 00 = single, 01 = demand 2,
> 11 = demand 4, but the catalog follows the register description (p.70)

> B9h: p.70 gives 01 single, 10 demand 2, 11 demand 4 and 00 reserved; the
> programming steps give 00 single, 01 demand 2, 11 demand 4 (p.48, p.50).
> ESS's driver writes 02h for demand transfers (docs/DRIVER_CONFIG.md)

* **1:0 Audio 1 DMA transfer type** (`a1.xfer`, choice, expert, driver sets
  it, reset by DSP reset). DMA transfer type of the first DMA: single, or
  demand with 2 or 4 bytes per request; 00 is reserved (DS p.70). Values:
  1 = Single; 2 = Demand 2; 3 = Demand 4.

### BAh Left channel ADC offset adjust

DS p.70, DSP channel.

> BAh and BBh are only reset by hardware reset, not by software reset (DS
> p.70)

* **5 No mute delay on analog wake** (`pwr.wake_nodelay`, bit, safe,
  profile). 1 = no 100 ms mute of AOUT_L/R when the analog section wakes
  from power-down. A hardware reset clears it, so AOUT is always muted after
  one (DS p.70).
* **4:0 ADC offset L** (`adc.off_l`, signed, safe, profile). Constant added
  to the left ADC output: 64 x bits 3:0, or -64 x (bits 3:0 + 1) when sign
  bit 4 is set (DS p.71).

### BBh Right channel ADC offset adjust

DS p.70, DSP channel.

* **4:0 ADC offset R** (`adc.off_r`, signed, safe, profile). Constant added
  to the right ADC output: 64 x bits 3:0, or -64 x (bits 3:0 + 1) when sign
  bit 4 is set (DS p.71).

### BDh Self-timed power-down

DS p.77, DSP channel, write-only.

> BDh sets self-timed power-down; the data sheet only describes writing it
> (DS p.77)

* **7:0 Self-timed power-down (x 8 s)** (`pwr.self_timed`, level, expert).
  Power down after N x 8 seconds without commands, partially or fully as
  Audio_Base+7h bit 3 says; 0 disables it. The ES1869 then uses the activity
  flags of Audio_Base+6h itself (DS p.77).

## Audio I/O ports

Ports Audio_Base+0h to +Fh.

### Audio_Base+00h FM status

DS p.41, FM port.

> Audio_Base+0h-3h are the FM address and data ports: reading +0h returns
> the FM status, and a write would pick an FM register (DS p.38, p.41)

> Audio_Base+0 is an FM port: ES1869.VXD traps it, and a read from Windows
> while FM is free makes Windows FM's owner, resets the FM and locks DOS
> programs out of FM until a MIDI program opens and closes it
> (docs/VXD_INTERNALS.md), so the stock driver's direct path doesn't read it

* **7 FM timer IRQ** (`stat.fm_irq`, status, read-only). 1 = an FM timer
  overflowed; only a status flag, kept for OPL3 compatibility (DS p.41).
* **6 FM timer 1 overflow** (`stat.fm_timer1`, status, read-only). Overflow
  flag of FM timer 1 (DS p.41).
* **5 FM timer 2 overflow** (`stat.fm_timer2`, status, read-only). Overflow
  flag of FM timer 2 (DS p.41).

### Audio_Base+06h Reset and status flags

DS p.40, reading changes state.

> Audio_Base+6h reads status but writes resets at the same bits

> written bits 7:2 must be 0, so a write must never copy back the status it
> read (DS p.40, p.43)

> DS p.75 lists other sources for the activity flags (flag 2: PnP, joystick,
> MPU-401, ...; flag 1: +4h/+5h), but the catalog follows p.40

* **7 Activity flag 2** (`stat.act2`, status, read-only). Set high by each
  read of this port, then set low by I/O reads or writes to the MPU-401 or
  FM ports (DS p.40).
* **6 Activity flag 1** (`stat.act1`, status, read-only). Set high by each
  read of this port, then set low by I/O reads or writes to Audio_Base+Ch
  and +Eh (DS p.40).
* **5 Activity flag 0** (`stat.act0`, status, read-only). Set high by each
  read of this port, then set low by writes to +2h, +3h, +6h or +Ch, reads
  of +2h, +3h or +Ah, or DMA (DS p.40).
* **4 Serial activity** (`stat.serial_act`, status, read-only). 1 = DSP
  serial mode is on (SE pin, 48h bit 7 or bit 5) or an ES689/ES69x drives
  the FM DAC through MCLK/MSD (DS p.40).
* **3 Digital section up (0 = down)** (`stat.digital_up`, status,
  read-only). 0 = the digital section is powered down (power modes 0 and 1);
  analog power follows Audio_Base+7h bit 3 (DS p.40).
* **2 MIDI mode** (`stat.midi_mode`, status, read-only). 1 = processing MIDI
  command 30h, 31h, 34h or 35h and monitoring serial input; powering down
  may lose data (DS p.40).
* **1 Reset Audio 1 FIFO** (`ctl.fifo_reset`, pulse, expert). Write 1 then 0
  to hold and release the FIFO reset (no function in Compatibility mode). A
  read returns the FIFO reset bit (DS p.40).
* **0 DSP software reset** (`ctl.sw_reset`, pulse, expert). Write 1 then 0
  to reset the ES1869, then read AAh from Audio_Base+Ah (p.43). A read
  returns the software reset bit (DS p.40).

### Audio_Base+07h Power management

DS p.40.

> the bit 3 heading reads "Analog power-down" but its description sets or
> clears Analog_Stays_On (DS p.40)

> GPO0/GPO1 are CAUTION because boards may wire them to an amplifier mute or
> other circuitry

* **7 Suspend request** (`pwr.suspend`, pulse, expert). Pulse high then low
  to request suspend: the ES1869 uploads its context for saving; resuming
  needs a hardware reset (DS p.76).
* **6 GPI pin** (`pwr.gpi`, status, read-only). Read-only status of the GPI
  input pin (DS p.40).
* **5 Hold FM in reset** (`pwr.fm_reset`, bit, caution). 1 = hold the FM
  synthesizer in reset, 0 = release it from reset (DS p.40).
* **3 Analog stays on** (`pwr.analog_on`, bit, caution, profile). 1 = set
  Analog_Stays_On: a power-down request then only partially powers down and
  the analog inputs stay audible (DS p.75).
* **2 Power-down request** (`pwr.pdn_request`, pulse, expert). Pulse high
  then low to request power-down: partial if bit 3 is set, full if it is
  clear (DS p.75).
* **1 GPO1 output** (`pwr.gpo1`, bit, caution, profile, driver sets it).
  Level of the GPO1 output pin; set high by hardware reset (DS p.41).
* **0 GPO0 output** (`pwr.gpo0`, bit, caution, profile, driver sets it).
  Level of the GPO0 output pin, for example an amplifier mute; cleared by
  hardware reset (DS p.41).

### Audio_Base+0Ch DSP status

DS p.41.

> Audio_Base+Ch is only read here, since a write sends a command to the DSP
> (DS p.41)

> reading Audio_Base+Ch clears activity flag 1 and wakes a partial
> power-down, as the DSP accesses do anyway (DS p.40, p.75)

* **7 DSP busy** (`stat.dsp_busy`, status, read-only). 1 = write buffer not
  available or ES1869 busy; poll until 0 before writing a command (DS p.41).
* **6 DSP read data available** (`stat.dsp_data`, status, read-only). 1 =
  data available in the read buffer (same flag as Audio_Base+Eh bit 7);
  cleared by reading Audio_Base+Ah (DS p.41).
* **5 FIFO full** (`stat.fifo_full`, status, read-only). 1 = Extended mode
  FIFO full (256 bytes loaded) (DS p.41).
* **4 FIFO empty** (`stat.fifo_empty`, status, read-only). 1 = Extended mode
  FIFO empty (0 bytes loaded) (DS p.41).
* **3 FIFO half empty** (`stat.fifo_half`, status, read-only). 1 = FIFO half
  empty, an Extended mode flag: set with 0-127 bytes in the FIFO for DAC and
  128-256 bytes for ADC (DS p.41, p.51).
* **2 DSP interrupt request** (`stat.irq_dsp`, status, read-only). 1 = the
  ES1869 processor generated an interrupt request, e.g. Compatibility mode
  DMA complete (DS p.41).
* **1 FIFO half-empty IRQ** (`stat.irq_fifo`, status, read-only). 1 =
  interrupt request from a FIFO half-empty flag change, used by programmed
  I/O in Extended mode (DS p.41).
* **0 DMA counter IRQ** (`stat.irq_dma`, status, read-only). 1 = interrupt
  request from a DMA counter overflow in Extended mode (DS p.41).

### Audio_Base+0Eh Read buffer status

DS p.41, reading changes state.

> reading Audio_Base+Eh clears any audio interrupt request, so it's only
> read on request (DS p.41)

* **7 Read buffer data available** (`stat.read_buffer`, status, read-only).
  1 = data available in the read buffer; reading Audio_Base+Ah clears it (DS
  p.41).
