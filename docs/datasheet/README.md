# ES1869 technical manual

* `es1869techmanual.pdf` is the ESS Technology **ES1869 AudioDrive data sheet**, document SAM0023-122898 (112 pages, PDFWriter 3.02, created 1999-02-25).
* It's the same document that was published on [Phil's Computer Lab](https://www.philscomputerlab.com/ess-audiodrive-es1868.html).
* Copyright ESS Technology, Inc. It's included here as the reference for the register catalog.
* SHA-256: `a876e64279206df048ea48525fba8336b152d6ac65d322c4b6d85fbe959a7ea9`
* The other docs in this repository cite it as `DS p.NN`, where NN is the printed page number. The printed page number is also the PDF page number.

## Page index

| Topic | Page |
|---|---|
| Pin description (GPO0/GPO1, GPI, MONO_IN/OUT) | 6-8 |
| Mixer schematic block diagram | 11 |
| Digital audio, DMA, Table 2 (Audio 1 controller regs), Table 3 (Audio 2 mixer regs) | 13-15 |
| Interrupts, Table 4/5, interrupt status and mask (Config_Base+6/+7) | 17-18 |
| I2S / wavetable / DSP serial interface, telegaming mode (Figure 9) | 19-21 |
| Modem, IDE CD-ROM, GPIO device, MPU-401, serial EEPROM | 22-24 |
| MONO_IN/MONO_OUT, Spatializer 3-D, hardware and master volume, PC speaker | 25-26 |
| PnP configuration: config ports, bypass key | 28 |
| Card-level registers 00h-07h, vendor-defined card-level registers 20h-29h | 29-30 |
| Logical device registers, Table 11, LDN 0-6 | 31-36 |
| I/O ports: Table 12 port summary, port descriptions | 38-42 |
| Programming: identifying the chip (mixer 40h), software reset, modes | 43-44 |
| Extended mode programming, controller register access protocol | 47-51 |
| Programming the mixer, mixer reset, extended access, Tables 18-22 | 52-55 |
| Register types and access, Table 23 (SB compatibility mixer registers) | 56 |
| Table 24 ESS mixer register summary and descriptions | 57-66 |
| Table 25 ESS controller register summary and descriptions | 67-71 |
| Table 26 audio microcontroller command summary | 72-74 |
| Power management, partial/full power-down, self-timed power-down, GPO | 75-78 |
| Appendix A: PnP ROM data example | 88-91 |
| Appendix B: ES689/ES69x digital serial interface | 92 |
| Appendix C: I2S ZV interface | 93-97 |
