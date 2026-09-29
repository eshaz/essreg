# Data sheets

This folder holds ESS Technology's data sheets for the ES1869 and two related chips. They are copyright ESS Technology, Inc., and they are included as the references for the register catalog and the other documents.

| File | Chip | Document | Used for |
|---|---|---|---|
| `es1869techmanual.pdf` | ES1869 AudioDrive | SAM0023-122898, 112 pages | the register catalog, cited as `DS p.NN` |
| `es938datasheet.pdf` | ES938 3-D Audio Effects Processor | SAM0054-090497, 12 pages, 1997 | the origin of the 3-D effect ([SPATIALIZER.md](../SPATIALIZER.md)) |
| `es1868datasheet.pdf` | ES1868 AudioDrive | revision B, 72 pages, 1995-1996 | a second reading where the ES1869's text is unclear |

The ES1868 has no 3-D effect, and its mixer registers skip from 4Eh to 64h.

The SHA-256 hashes of the ES938 and ES1868 data sheets are:
* `es938datasheet.pdf`: `e8fcbe00a2814c0a661a58092fbe63c31e63986f3286c8df794f84c3ddc53ad2`
* `es1868datasheet.pdf`: `b85192c1308b7d0272b0871d2984274d1722c69e02fd235ab9afc5f591097a9d`

## ES1869 technical manual

`es1869techmanual.pdf` is ESS Technology's *ES1869 AudioDrive data sheet*, document SAM0023-122898. It has 112 pages and was made with PDFWriter 3.02 on 1999-02-25. It is the same document that was published on [Phil's Computer Lab](https://www.philscomputerlab.com/ess-audiodrive-es1868.html), and its SHA-256 hash is `a876e64279206df048ea48525fba8336b152d6ac65d322c4b6d85fbe959a7ea9`.

The other documents in this repository cite it as `DS p.NN`, where NN is the printed page number, which is also the page number in the PDF.

### Page index

| Topic | Page |
|---|---|
| Pin description (GPO0/GPO1, GPI, MONO_IN/OUT) | 6-8 |
| Mixer schematic block diagram | 11 |
| Digital audio and DMA, Table 2 (Audio 1 controller registers), Table 3 (Audio 2 mixer registers) | 13-15 |
| Interrupts, Tables 4 and 5, interrupt status and mask (Config_Base+6/+7) | 17-18 |
| I2S, wavetable and DSP serial interface, telegaming mode (Figure 9) | 19-21 |
| Modem, IDE CD-ROM, GPIO device, MPU-401, serial EEPROM | 22-24 |
| MONO_IN/MONO_OUT, Spatializer 3-D, hardware and master volume, PC speaker | 25-26 |
| PnP configuration: configuration ports, bypass key | 28 |
| Card-level registers 00h-07h, vendor-defined card-level registers 20h-29h | 29-30 |
| Logical device registers, Table 11, LDN 0-6 | 31-36 |
| I/O ports: Table 12 port summary, port descriptions | 38-42 |
| Programming: identifying the chip (mixer 40h), software reset, modes | 43-44 |
| Extended mode programming, controller register access protocol | 47-51 |
| Programming the mixer, mixer reset, extended access, Tables 18-22 | 52-55 |
| Register types and access, Table 23 (SB compatibility mixer registers) | 56 |
| Table 24, ESS mixer register summary and descriptions | 57-66 |
| Table 25, ESS controller register summary and descriptions | 67-71 |
| Table 26, audio microcontroller command summary | 72-74 |
| Power management: partial and full power-down, self-timed power-down, GPO | 75-78 |
| Appendix A: PnP ROM data example | 88-91 |
| Appendix B: ES689/ES69x digital serial interface | 92 |
| Appendix C: I2S ZV interface | 93-97 |
