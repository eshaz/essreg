# Data sheets

This folder holds ESS Technology's data sheets for the ES1869 and two
related chips. They are copyright ESS Technology, Inc., and they are
included as the references for the register catalog and the other documents.
The parentheses below give each data sheet's document number or revision and
its number of pages:

* **`es1869techmanual.pdf`** is the data sheet of the ES1869 AudioDrive
  (SAM0023-122898, 112 pages). It is used for the register catalog, which
  cites it as `DS p.NN`.
* **`es938datasheet.pdf`** is the data sheet of the ES938 3-D Audio Effects
  Processor (SAM0054-090497, 12 pages, 1997). It is used to explain the
  origin of the 3-D effect ([SPATIALIZER.md](../SPATIALIZER.md)).
* **`es1868datasheet.pdf`** is the data sheet of the ES1868 AudioDrive
  (revision B, 72 pages, 1995-1996). It is used as a second reading where
  the ES1869's text is unclear.

The ES1868 has no 3-D effect, and its mixer registers skip from 4Eh to 64h.

The SHA-256 hashes of the ES938 and ES1868 data sheets are:
* `es938datasheet.pdf`:
  `e8fcbe00a2814c0a661a58092fbe63c31e63986f3286c8df794f84c3ddc53ad2`
* `es1868datasheet.pdf`:
  `b85192c1308b7d0272b0871d2984274d1722c69e02fd235ab9afc5f591097a9d`

## ES1869 technical manual

`es1869techmanual.pdf` is ESS Technology's *ES1869 AudioDrive data sheet*,
document SAM0023-122898. It has 112 pages and was made with PDFWriter 3.02
on 1999-02-25. It is the same document that was published on [Phil's
Computer Lab](https://www.philscomputerlab.com/ess-audiodrive-es1868.html),
and its SHA-256 hash is
`a876e64279206df048ea48525fba8336b152d6ac65d322c4b6d85fbe959a7ea9`.

The other documents in this repository cite it as `DS p.NN`, where NN is the
printed page number, which is also the page number in the PDF.

### Page index

* Pages 6-8: Pin description (GPO0/GPO1, GPI, MONO_IN/OUT)
* Page 11: Mixer schematic block diagram
* Pages 13-15: Digital audio and DMA, Table 2 (Audio 1 controller
  registers), Table 3 (Audio 2 mixer registers)
* Pages 17-18: Interrupts, Tables 4 and 5, interrupt status and mask
  (Config_Base+6/+7)
* Pages 19-21: I2S, wavetable and DSP serial interface, telegaming mode
  (Figure 9)
* Pages 22-24: Modem, IDE CD-ROM, GPIO device, MPU-401, serial EEPROM
* Pages 25-26: MONO_IN/MONO_OUT, Spatializer 3-D, hardware and master
  volume, PC speaker
* Page 28: PnP configuration: configuration ports, bypass key
* Pages 29-30: Card-level registers 00h-07h, vendor-defined card-level
  registers 20h-29h
* Pages 31-36: Logical device registers, Table 11, LDN 0-6
* Pages 38-42: I/O ports: Table 12 port summary, port descriptions
* Pages 43-44: Programming: identifying the chip (mixer 40h), software
  reset, modes
* Pages 47-51: Extended mode programming, controller register access
  protocol
* Pages 52-55: Programming the mixer, mixer reset, extended access, Tables
  18-22
* Page 56: Register types and access, Table 23 (SB compatibility mixer
  registers)
* Pages 57-66: Table 24, ESS mixer register summary and descriptions
* Pages 67-71: Table 25, ESS controller register summary and descriptions
* Pages 72-74: Table 26, audio microcontroller command summary
* Pages 75-78: Power management: partial and full power-down, self-timed
  power-down, GPO
* Pages 88-91: Appendix A: PnP ROM data example
* Page 92: Appendix B: ES689/ES69x digital serial interface
* Pages 93-97: Appendix C: I2S ZV interface
