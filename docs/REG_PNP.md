# ES1869 configuration ports and PnP registers

This page is part of the [ES1869 register reference](REGISTERS.md),
generated from `src/esscat.tbl`.

## Configuration device ports

Ports Config_Base+0h to +7h (Config_Base comes from the mixer 40h
identification sequence or PnP LDN 0).

### Config_Base+05h Status

DS p.39.

> Config_Base+5h is headed "R/W" (DS p.38-39), but no bit is described as
> writable

* **5:4 PnP data source** (`cfg.eeprom`, choice, read-only). EEPROM type
  indicator: internal ROM or 512 x 8-bit serial EEPROM; 01 and 10 are
  reserved (DS p.39). Values: 0 = Internal ROM; 3 = EEPROM 512 x 8.
* **3:2 PnP state** (`cfg.state`, choice, read-only). ES1869 Plug and Play
  operating state (DS p.39). Values: 0 = Wait-for-key; 1 = Sleep; 2 =
  Isolation; 3 = Configure.
* **1 PNPOK** (`cfg.pnpok`, status, read-only). PNPOK bit (DS p.39).
* **0 Reset sequence busy** (`cfg.reset_busy`, status, read-only). Reset
  sequence busy bit (DS p.39).

### Config_Base+06h Interrupt status

DS p.39.

> the interrupt status is read before the Config_Base+7h mask is applied (DS
> p.18)

* **6 GP interrupt** (`cfg.isr.gp`, status, read-only). General-purpose
  interrupt: the GPI input pin (DS p.39).
* **5 Modem interrupt** (`cfg.isr.modem`, status, read-only). Modem
  interrupt: the MMIRQ input pin AND'ed with the inverse of MMIEB (DS p.39).
* **4 CD-ROM interrupt** (`cfg.isr.cdrom`, status, read-only). CD-ROM
  interface interrupt: the CDIRQ input pin (DS p.39).
* **3 MPU-401 interrupt** (`cfg.isr.mpu401`, status, read-only). MPU-401
  receive interrupt request AND'ed with bit 6 of mixer register 64h (DS
  p.39).
* **2 HW volume interrupt** (`cfg.isr.hwvol`, status, read-only). Hardware
  volume interrupt request AND'ed with bit 1 of mixer register 64h (DS
  p.39).
* **1 Audio 2 interrupt** (`cfg.isr.audio2`, status, read-only). Audio 2
  interrupt request AND'ed with bit 6 of mixer register 7Ah (DS p.39).
* **0 Audio 1 interrupt** (`cfg.isr.audio1`, status, read-only). Audio 1
  interrupt request (DS p.39).

### Config_Base+07h Interrupt mask

DS p.39.

> each mask bit is AND'ed with its interrupt source, and 0 forces the source
> to zero without floating the pin

> the mask is all ones after hardware reset (DS p.39)

* **6 GP IRQ mask (1 = on)** (`cfg.imr.gp`, bit, expert). General-purpose
  interrupt mask bit, AND'ed with the GPI interrupt source (DS p.39).
* **5 Modem IRQ mask (1 = on)** (`cfg.imr.modem`, bit, expert). Modem
  interrupt mask bit, AND'ed with the modem interrupt source (DS p.39).
* **4 CD-ROM IRQ mask (1 = on)** (`cfg.imr.cdrom`, bit, expert). CD-ROM
  interface interrupt mask bit, AND'ed with the CD-ROM interrupt source (DS
  p.39).
* **3 MPU-401 IRQ mask (1 = on)** (`cfg.imr.mpu401`, bit, expert). MPU-401
  interrupt mask bit, AND'ed with the MPU-401 interrupt source (DS p.39).
* **2 HW volume IRQ mask (1 = on)** (`cfg.imr.hwvol`, bit, expert). Hardware
  volume interrupt mask bit, AND'ed with the hardware volume interrupt
  source (DS p.39).
* **1 Audio 2 IRQ mask (1 = on)** (`cfg.imr.audio2`, bit, expert). Audio 2
  interrupt mask bit, AND'ed with the Audio 2 interrupt source (DS p.39).
* **0 Audio 1 IRQ mask (1 = on)** (`cfg.imr.audio1`, bit, expert). Audio 1
  interrupt mask bit, AND'ed with the Audio 1 interrupt source (DS p.39).

## PnP card registers

Index written to Config_Base+0h, data at Config_Base+1h.

### 00h Set RD_DATA port

DS p.29.

> 01h-04h (isolation, configuration control, wake, resource data) and 07h
> (the logical device number) are the PnP protocol's registers, not
> settings, so they're left out (DS p.29-30)

* **7:0 RD_DATA port bits 9:2** (`pnp.rd_data_port`, value, read-only). Bits
  9:2 of the PnP RD_DATA port (bits 1:0 are always one); readable only in
  Configuration mode (DS p.29).

### 05h Status

DS p.30.

* **0 Resource data ready** (`pnp.resource_ready`, status, read-only). 1 =
  ready to read resource data in register 04h; only works in Configuration
  mode (DS p.30).

### 06h Card select number

DS p.30.

* **7:0 Card select number** (`pnp.csn`, value, read-only). PnP card select
  number; reads only work in Configuration mode (DS p.30).

### 20h IRQB, IRQA

DS p.30.

> 20h-26h are loaded from the configuration ROM header after PnP reset

> the ROM header should give unused IRQ pins IRQ 1 and unused DRQ pins DRQ 2
> (DS p.30)

* **7:4 IRQ of pin IRQB** (`pnp.pin.irqb`, level, read-only). IRQ number
  assigned to interrupt pin IRQB (DS p.30).
* **3:0 IRQ of pin IRQA** (`pnp.pin.irqa`, level, read-only). IRQ number
  assigned to interrupt pin IRQA (DS p.30).

### 21h IRQD, IRQC

DS p.30.

* **7:4 IRQ of pin IRQD** (`pnp.pin.irqd`, level, read-only). IRQ number
  assigned to interrupt pin IRQD (DS p.30).
* **3:0 IRQ of pin IRQC** (`pnp.pin.irqc`, level, read-only). IRQ number
  assigned to interrupt pin IRQC (DS p.30).

### 22h IRQF, IRQE

DS p.30.

* **7:4 IRQ of pin IRQF** (`pnp.pin.irqf`, level, read-only). IRQ number
  assigned to interrupt pin IRQF (DS p.30).
* **3:0 IRQ of pin IRQE** (`pnp.pin.irqe`, level, read-only). IRQ number
  assigned to interrupt pin IRQE (DS p.30).

### 23h DRQB, DRQA

DS p.30.

* **7:4 DMA channel of pin DRQB** (`pnp.pin.drqb`, level, read-only). DRQ
  number assigned to DMA request pin DRQB (DS p.30).
* **3:0 DMA channel of pin DRQA** (`pnp.pin.drqa`, level, read-only). DRQ
  number assigned to DMA request pin DRQA (DS p.30).

### 24h DRQD, DRQC

DS p.30.

* **7:4 DMA channel of pin DRQD** (`pnp.pin.drqd`, level, read-only). DRQ
  number assigned to DMA request pin DRQD (DS p.30).
* **3:0 DMA channel of pin DRQC** (`pnp.pin.drqc`, level, read-only). DRQ
  number assigned to DMA request pin DRQC (DS p.30).

### 25h Configuration ROM header 0

DS p.30.

> 25h is headed "(R)" on DS p.30; the DRQ latch bit is tiered EXPERT so a
> write can be tried, the other bits stay read-only

* **7 DRQ latch** (`pnp.drq_latch`, bit, expert). 1 = each audio DRQ stays
  high until the DMA controller answers it, so none is missed; 0 = off. It
  comes from the card's PnP ROM or EEPROM at power-up, and the data sheet
  has 25h as read-only: essctl reads a write back to show whether the chip
  took it (DS p.15, p.30, docs/AUDIO_PIPELINE.md).
* **6:5 Motherboard or card** (`pnp.board`, choice, read-only). ES1869 is on
  the motherboard or an add-on card; bits 6:5 also pick one of the four
  bypass key sequences of p.28 (DS p.30). Values: 0 = Motherboard; 1 = Card.
* **4:3 General-purpose device** (`pnp.gp_device`, choice, read-only).
  General-purpose location: not present, or present as LDN 3-6 using 4, 8 or
  16 addresses (DS p.30). Values: 0 = Not present; 1 = GP, 4 addresses; 2 =
  GP, 8 addresses; 3 = GP, 16 addresses.
* **2 Modem device present** (`pnp.modem_present`, bit, read-only). 1 = the
  modem is LDN 3, 4 or 5; 0 = the modem is not present (DS p.30).
* **1 CD-ROM device present** (`pnp.cdrom_present`, bit, read-only). 1 = the
  CD-ROM is LDN 3 or 4; 0 = the CD-ROM is not present (DS p.31).
* **0 MPU-401 is LDN 3** (`pnp.mpu401_ldn3`, bit, read-only). 1 = MPU-401 is
  LDN 3 and does not share audio interrupt 1 or 2; 0 = it is part of LDN 1
  and shares it (DS p.31).

### 26h Configuration ROM header 1

DS p.31.

> 26h is headed "(R)" on DS p.31, but p.15 has drivers set bits 4:2 to share
> DMA channels, so they are tiered EXPERT

* **4 External DMA mask** (`pnp.ext_dma_mask`, bit, expert). 1 = external
  DMA mask enabled, 0 = disabled; set it while DMA channels are shared (DS
  p.15, p.31).
* **3 Audio 2 DMA mask** (`pnp.a2_dma_mask`, bit, expert). 1 = Audio 2 DMA
  mask enabled, 0 = disabled; set it while DMA channels are shared (DS p.15,
  p.31).
* **2 Audio 1 DMA mask** (`pnp.a1_dma_mask`, bit, expert). 1 = Audio 1 DMA
  mask enabled, 0 = disabled; set it while DMA channels are shared (DS p.15,
  p.31).
* **1 GPO1 pin is DACK** (`pnp.gpo1_is_dack`, bit, read-only). 1 = the GPO1
  pin is an external DACK and GPI an external DRQ; 0 = the GPO1 pin is GPO1
  (DS p.31).
* **0 GPO0 pin is GPCS** (`pnp.gpo0_is_gpcs`, bit, read-only). 1 = the GPO0
  pin is the GPCS chip select; 0 = the GPO0 pin is GPO0 (DS p.31).

### 27h Hardware volume IRQ number

DS p.31.

> 27h and 28h are headed "(R)" on DS p.31, but p.18 assigns these interrupts
> by writing them, so they're tiered EXPERT

* **7:0 HW volume IRQ number** (`pnp.hwvol_irq`, value, expert). IRQ number
  of the hardware volume interrupt, which must be shared with Audio 1 or
  Audio 2; reset to 0 by PnP reset (DS p.31).

### 28h MPU-401 IRQ number

DS p.31.

* **7:0 MPU-401 IRQ number** (`pnp.mpu401_irq`, value, expert). MPU-401 IRQ
  number; the same physical register as register 70h of the MPU-401 device
  (LDN 3) (DS p.31).

### 29h PnP enable

DS p.31.

* **0 PnP enable** (`pnp.enable`, bit, read-only). 1 = PnP enabled; 0 = the
  ES1869 does not respond to any PnP command. Set high by hardware reset (DS
  p.31).

## PnP logical device registers

As the card registers, after selecting the logical device in register 07h;
essctl restores the previous index and device afterwards.

### Logical device 0

#### 30h Config device activate

DS p.32.

> LDN 0: configuration device

> only relocate it with the bypass key, since writing its 60h is unreliable
> (DS p.29)

* **0 Config device active** (`pnp.ldn0.active`, bit, read-only). Activate
  bit of the configuration device (DS p.32).

#### 31h Config device I/O range check

DS p.32.

* **1 Config device range check** (`pnp.ldn0.range_check`, bit, read-only).
  1 = the device's I/O range answers with the pattern of bit 0, to find
  conflicts with another device (DS p.32).
* **0 Config device range check 55h** (`pnp.ldn0.range_pattern`, bit,
  read-only). Range check pattern: 1 = 55h, 0 = AAh (DS p.32).

#### 60h Config device I/O base 11:8

DS p.32.

* **3:0 Config_Base bits 11:8** (`pnp.ldn0.io.hi`, value, read-only). Bits
  11:8 of Config_Base (eight ports); zero disables the device (DS p.31-32).

#### 61h Config device I/O base 7:3

DS p.32.

* **7:0 Config_Base bits 7:0** (`pnp.ldn0.io.lo`, value, read-only). Bits
  7:3 of Config_Base; bits 2:0 are 0 (DS p.32).

#### 74h Config device DMA channel select 0

DS p.32.

* **2:0 DMA channel 0** (`pnp.ldn0.dma0`, level, read-only). DMA channel in
  use for DMA 0; returns 4 (no DMA channel selected) (DS p.32).

#### 75h Config device DMA channel select 1

DS p.32.

* **2:0 DMA channel 1** (`pnp.ldn0.dma1`, level, read-only). DMA channel in
  use for DMA 1; returns 4 (no DMA channel selected) (DS p.32).

### Logical device 1

#### 30h Audio device activate

DS p.33.

> LDN 1: audio device (audio, FM and MPU-401)

* **0 Audio device active** (`pnp.ldn1.active`, bit, expert). 1 = activate
  the audio device; 0 after reset or a PnP configuration reset (DS p.33).

#### 31h Audio device I/O range check

DS p.33.

* **1 Audio device range check** (`pnp.ldn1.range_check`, bit, read-only).
  1 = the device's I/O range answers with the pattern of bit 0, to find
  conflicts with another device (DS p.33).
* **0 Audio device range check 55h** (`pnp.ldn1.range_pattern`, bit,
  read-only). Range check pattern: 1 = 55h, 0 = AAh (DS p.33).

#### 60h Audio processor I/O base 11:8

DS p.33.

* **3:0 Audio_Base bits 11:8** (`pnp.ldn1.io.hi`, value, expert). Bits 11:8
  of Audio_Base (sixteen ports); zero makes the device inaccessible (DS
  p.31, p.33).

#### 61h Audio processor I/O base 7:4

DS p.33.

* **7:0 Audio_Base bits 7:0** (`pnp.ldn1.io.lo`, value, expert). Bits 7:4 of
  Audio_Base; bits 3:0 are 0 (DS p.33).

#### 62h FM alias I/O base 11:8

DS p.33.

* **3:0 FM alias base bits 11:8** (`pnp.ldn1.fm.hi`, value, expert). Bits
  11:8 of the FM alias base (four ports); zero makes it inaccessible (DS
  p.31, p.33).

#### 63h FM alias I/O base 7:2

DS p.33.

* **7:0 FM alias base bits 7:0** (`pnp.ldn1.fm.lo`, value, expert). Bits 7:2
  of the FM alias base; bits 1:0 are 0 (DS p.33).

#### 64h MPU-401 I/O base 11:8

DS p.33.

* **3:0 MPU-401 base bits 11:8** (`pnp.ldn1.mpu.hi`, value, expert). Bits
  11:8 of the MPU-401 base (two ports); zero makes it inaccessible. The
  MPU-401 may also be reached through LDN 3 (DS p.31, p.33).

#### 65h MPU-401 I/O base 7:2

DS p.33.

* **7:0 MPU-401 base bits 7:0** (`pnp.ldn1.mpu.lo`, value, expert). Bits 7:2
  of the MPU-401 base; bits 1:0 are 0 (DS p.33).

#### 70h Audio interrupt channel 1 select

DS p.33.

* **3:0 Audio 1 IRQ** (`pnp.ldn1.irq1`, level, expert). Interrupt used for
  the channel 1 IRQ, which is the Audio 1 interrupt (DS p.17, p.33).

#### 71h Audio interrupt type select 1

DS p.33.

* **7:0 IRQ channel 1 type** (`pnp.ldn1.irq1_type`, value, read-only).
  Interrupt request type select 1; returns 2 (low-to-high transition) (DS
  p.33).

#### 72h Audio interrupt channel 2 select

DS p.33.

* **3:0 Audio 2 IRQ** (`pnp.ldn1.irq2`, level, expert). Interrupt used for
  the channel 2 IRQ, which is the optional Audio 2 interrupt (DS p.17,
  p.33).

#### 73h Audio interrupt type select 2

DS p.34.

* **7:0 IRQ channel 2 type** (`pnp.ldn1.irq2_type`, value, read-only).
  Interrupt request type select 2; returns 2 (low-to-high transition) (DS
  p.34).

#### 74h Audio DMA channel 1 select

DS p.34.

* **2:0 DMA channel 1** (`pnp.ldn1.dma1`, level, expert). DMA channel in use
  for the channel 1 DRQ; 4 = no DMA channel selected (DS p.34).

#### 75h Audio DMA channel 2 select

DS p.34.

* **2:0 DMA channel 2** (`pnp.ldn1.dma2`, level, expert). DMA channel in use
  for the channel 2 DRQ; 4 = no DMA channel selected (DS p.34).

### Logical device 2

#### 30h Joystick activate

DS p.34.

> LDN 2: joystick device

* **0 Joystick active** (`pnp.ldn2.active`, bit, expert). 1 = activate the
  joystick device; 0 after reset or a PnP configuration reset (DS p.34).

#### 31h Joystick I/O range check

DS p.34.

* **1 Joystick range check** (`pnp.ldn2.range_check`, bit, read-only). 1 =
  the device's I/O range answers with the pattern of bit 0, to find
  conflicts with another device (DS p.34).
* **0 Joystick range check 55h** (`pnp.ldn2.range_pattern`, bit, read-only).
  Range check pattern: 1 = 55h, 0 = AAh (DS p.34).

#### 60h Joystick I/O base 11:8

DS p.34.

* **3:0 Joystick port bits 11:8** (`pnp.ldn2.io.hi`, value, expert). Bits
  11:8 of the joystick port (one location); zero disables the device (DS
  p.31, p.34).

#### 61h Joystick I/O base 7:0

DS p.34.

* **7:0 Joystick port bits 7:0** (`pnp.ldn2.io.lo`, value, expert). Bits 7:0
  of the joystick port (DS p.34).

### MPU-401 device (optional, LDN 3)

#### 30h MPU-401 device activate

DS p.34.

> MPU-401 device (optional): LDN 3 when card register 25h bit 0 is set,
> otherwise the MPU-401 is part of LDN 1

* **0 MPU-401 device active** (`pnp.mpu401.active`, bit, expert). 1 =
  activate the stand-alone MPU-401 device; 0 after reset or a PnP
  configuration reset (DS p.34).

#### 31h MPU-401 device I/O range check

DS p.34.

* **1 MPU-401 device range check** (`pnp.mpu401.range_check`, bit,
  read-only). 1 = the device's I/O range answers with the pattern of bit 0,
  to find conflicts with another device (DS p.34).
* **0 MPU-401 device range check 55h** (`pnp.mpu401.range_pattern`, bit,
  read-only). Range check pattern: 1 = 55h, 0 = AAh (DS p.34).

#### 60h MPU-401 device I/O base 11:8

DS p.35.

* **3:0 MPU-401 port bits 11:8** (`pnp.mpu401.io.hi`, value, expert). Bits
  11:8 of the MPU-401 base (two locations); zero disables the device (DS
  p.32, p.35).

#### 61h MPU-401 device I/O base 7:0

DS p.35.

* **7:0 MPU-401 port bits 7:0** (`pnp.mpu401.io.lo`, value, expert). Bits
  7:0 of the MPU-401 base (DS p.35).

#### 70h MPU-401 interrupt select

DS p.35.

* **3:0 MPU-401 IRQ** (`pnp.mpu401.irq`, level, expert). Interrupt used for
  the MPU-401 IRQ; when the MPU-401 is its own device, the same physical
  register as card register 28h (DS p.17-18, p.31, p.35).

#### 71h MPU-401 interrupt type select

DS p.35.

* **7:0 MPU-401 IRQ type** (`pnp.mpu401.irq_type`, value, read-only).
  Interrupt request type select 0; returns 2 (low-to-high transition) (DS
  p.35).

### CD-ROM device (optional, LDN 3 or 4)

#### 30h CD-ROM activate

DS p.35.

> CD-ROM device (optional, card register 25h bit 1): LDN 3 or 4

* **0 CD-ROM device active** (`pnp.cdrom.active`, bit, expert). 1 = activate
  the CD-ROM device; 0 after reset or a PnP configuration reset (DS p.35).

#### 31h CD-ROM I/O range check

DS p.35.

* **1 CD-ROM range check** (`pnp.cdrom.range_check`, bit, read-only). 1 =
  the device's I/O range answers with the pattern of bit 0, to find
  conflicts with another device (DS p.35).
* **0 CD-ROM range check 55h** (`pnp.cdrom.range_pattern`, bit, read-only).
  Range check pattern: 1 = 55h, 0 = AAh (DS p.35).

#### 60h CD-ROM I/O decoder 0 base 11:8

DS p.35.

* **3:0 CD-ROM range 1 bits 11:8** (`pnp.cdrom.io.hi`, value, expert). Bits
  11:8 of the first CD-ROM address range (eight locations); zero makes it
  inaccessible (DS p.32, p.35).

#### 61h CD-ROM I/O decoder 0 base 7:0

DS p.35.

* **7:0 CD-ROM range 1 bits 7:0** (`pnp.cdrom.io.lo`, value, expert). Bits
  7:0 of the first CD-ROM address range (DS p.35).

#### 62h CD-ROM I/O decoder 1 base 11:8

DS p.35.

* **3:0 CD-ROM range 2 bits 11:8** (`pnp.cdrom.io2.hi`, value, expert). Bits
  11:8 of the second CD-ROM address range (two locations); zero makes it
  inaccessible (DS p.32, p.35).

#### 63h CD-ROM I/O decoder 1 base 7:0

DS p.35.

* **7:0 CD-ROM range 2 bits 7:0** (`pnp.cdrom.io2.lo`, value, expert). Bits
  7:0 of the second CD-ROM address range (DS p.35).

#### 70h CD-ROM interrupt select

DS p.35.

* **3:0 CD-ROM IRQ** (`pnp.cdrom.irq`, level, expert). Interrupt used for
  the CD-ROM IRQ (DS p.35).

#### 71h CD-ROM interrupt type select

DS p.35.

* **7:0 CD-ROM IRQ type** (`pnp.cdrom.irq_type`, value, read-only).
  Interrupt request type select 0; returns 2 (low-to-high transition) (DS
  p.35).

#### 74h CD-ROM DMA channel select

DS p.35.

* **2:0 CD-ROM DMA channel** (`pnp.cdrom.dma`, level, expert). DMA channel
  in use for the CD-ROM DRQ; 4 = no DMA channel selected (DS p.35).

### Modem device (optional, LDN 3, 4 or 5)

#### 30h Modem activate

DS p.36.

> modem device (optional, card register 25h bit 2): LDN 3, 4 or 5

* **0 Modem device active** (`pnp.modem.active`, bit, expert). 1 = activate
  the modem device; 0 after reset or a PnP configuration reset (DS p.36).

#### 31h Modem I/O range check

DS p.36.

* **1 Modem range check** (`pnp.modem.range_check`, bit, read-only). 1 = the
  device's I/O range answers with the pattern of bit 0, to find conflicts
  with another device (DS p.36).
* **0 Modem range check 55h** (`pnp.modem.range_pattern`, bit, read-only).
  Range check pattern: 1 = 55h, 0 = AAh (DS p.36).

#### 60h Modem I/O base 11:8

DS p.36.

* **3:0 Modem base bits 11:8** (`pnp.modem.io.hi`, value, expert). Bits 11:8
  of the modem address range (eight locations); zero makes it inaccessible
  (DS p.32, p.36).

#### 61h Modem I/O base 7:0

DS p.36.

* **7:0 Modem base bits 7:0** (`pnp.modem.io.lo`, value, expert). Bits 7:0
  of the modem address range (DS p.36).

#### 70h Modem interrupt select

DS p.36.

* **3:0 Modem IRQ** (`pnp.modem.irq`, level, expert). Interrupt used for the
  modem IRQ (DS p.36).

#### 71h Modem interrupt type select

DS p.36.

* **7:0 Modem IRQ type** (`pnp.modem.irq_type`, value, read-only). Interrupt
  request type select 0; returns 2 (low-to-high transition) (DS p.36).

#### 74h Modem DMA channel select

DS p.36.

* **2:0 Modem DMA channel** (`pnp.modem.dma`, level, expert). DMA channel in
  use for the modem DRQ; 4 = no DMA channel selected (DS p.36).

### General-purpose device (optional, LDN 3 to 6)

#### 30h GP device activate

DS p.36.

> general-purpose device (optional, card register 25h bits 4:3): LDN 3, 4, 5
> or 6

* **0 GP device active** (`pnp.gp.active`, bit, expert). 1 = activate the
  general-purpose device; 0 after reset or a PnP configuration reset (DS
  p.36).

#### 31h GP device I/O range check

DS p.37.

* **1 GP device range check** (`pnp.gp.range_check`, bit, read-only). 1 =
  the device's I/O range answers with the pattern of bit 0, to find
  conflicts with another device (DS p.37).
* **0 GP device range check 55h** (`pnp.gp.range_pattern`, bit, read-only).
  Range check pattern: 1 = 55h, 0 = AAh (DS p.37).

#### 60h GP device I/O base 11:8

DS p.37.

* **3:0 GP base bits 11:8** (`pnp.gp.io.hi`, value, expert). Bits 11:8 of
  the general-purpose address range (4, 8 or 16 locations); zero makes it
  inaccessible (DS p.32, p.37).

#### 61h GP device I/O base 7:0

DS p.37.

* **7:0 GP base bits 7:0** (`pnp.gp.io.lo`, value, expert). Bits 7:0 of the
  general-purpose address range (DS p.37).

#### 70h GP device interrupt select

DS p.37.

* **3:0 GP device IRQ** (`pnp.gp.irq`, level, expert). Interrupt used for
  the general-purpose device IRQ (DS p.37).

#### 71h GP device interrupt type select

DS p.37.

* **7:0 GP device IRQ type** (`pnp.gp.irq_type`, value, read-only).
  Interrupt request type select 0; returns 2 (low-to-high transition) (DS
  p.37).

#### 74h GP device DMA channel select

DS p.37.

* **2:0 GP device DMA channel** (`pnp.gp.dma`, level, expert). DMA channel
  in use for the general-purpose device DRQ; 4 = no DMA channel selected (DS
  p.37).
