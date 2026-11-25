# Windows MIDI files

These are the MIDI files that came with Windows 3.0's Multimedia Extensions,
Windows 98 and Windows XP.
[`tests/test_midisuite.py`](../../tests/test_midisuite.py) plays them
through both MIDI devices of `build/ESFM.DRV` in the CPU emulator
([TESTING.md](../../docs/MIDI_SUITE.md)). The owner of this repository added
them as test data. They belong to Microsoft, and most of the Windows 3.0
files name Passport Designs in their text, with copyrights of 1990 and 1991.

* `win30` has the files of Windows 3.0's Multimedia Extensions.
* `win98` has `CANYON.MID` and `PASSPORT.MID` of Windows 98's Media folder,
  General MIDI versions of the Windows 3.0 songs, and its classics, the
  `.rmi` files, standard MIDI files in a RIFF file.
* `winxp` has three songs of Windows XP.

## Two arrangements in one file

The files of Windows 3.0, and the classics of Windows 98, follow Microsoft's
authoring guidelines for the Multimedia PC. Each holds its song twice, once
for each kind of synthesizer that Windows 3.1's MIDI Mapper played on:
* the extended arrangement, for a synthesizer of 16 notes or more, on
  channels 1 to 10, with its drums on channel 10;
* the base-level arrangement, for one of 6 melodic notes and 3 drum notes,
  such as an AdLib, on channels 13 to 16, with its drums on channel 16.

Channels 11 and 12 stay unused, though two of the Windows 3.0 files name a
part on channel 11 in their text. The MIDI Mapper of Windows 95 and 98 sends
all 16 channels to one device, which then plays both arrangements, and the
drums of channel 16 as notes of a melodic instrument. `MpcSongs` under
`[ESFM.DRV]` in `SYSTEM.INI` leaves out the base-level copy
([ESFM_GM.md](../../docs/ESFM_GM.md#songs-with-two-arrangements)).

The channels with notes:
* `win30`: every file has the extended arrangement on channels 1 to 10 and
  the base-level one on 13 to 16. `BRNDNBRG.MID` and `SEASONS.MID` have no
  drums, and so nothing on channels 10 and 16.
* `win98`: the classics have the extended arrangement on channels 1 to 10
  and a copy on 13 to 15 that plays the extended parts' notes on fewer
  channels, without drums. *In the Hall of the Mountain King* has drums on
  channel 10 only. `CANYON.MID` and `PASSPORT.MID` are General MIDI, on
  channels 1 to 10.
* `winxp`: General MIDI, with parts on channels 11 to 16 as well.

## The files

The size in bytes and the SHA-256 of each:
* `win30/ALMONDS.MID`: 49889,
  `5326fe480594f857cff276ffb84db19a97e75113fc7c3ada42593f3ef30997ff`
* `win30/BRNDNBRG.MID`: 19645,
  `e4ede6bfa3aa9797487ac7523538be9f7ec4cf45f52b418cabddd8658f7fa450`
* `win30/CANYON.MID`: 33876,
  `39ad41b8310bd7ce7e00accc017bb9cdb23e3d3d581478f295f215495931005c`
* `win30/DREAMY.MID`: 26785,
  `6cf8526787f958425cbd5eb4486e13dc31d3dd276c62aa8c84ce9c1bbac37079`
* `win30/METBLAST.MID`: 40106,
  `e9bcf12a91cddbbeea834a39727fa193a1086ccac6f09d482b4ec977f973cf40`
* `win30/PASSPORT.MID`: 40287,
  `f7b1b01ff82cc98214fc8266a0a6aab5ea1736154305f35fdadbfe609962d1aa`
* `win30/SEASONS.MID`: 25997,
  `e86e158b4aeda31133fc452fa1e25b8dfe3e39896cdda9a9579a28012a89a4c1`
* `win30/SOMEBAND.MID`: 28025,
  `c584c7bfbfc501fa0235c1159f5214c5eb91a2da4c8b7aa6880f0d371ae6d80d`
* `win30/STOMP.MID`: 33378,
  `0564ef0a68e890e0e8ce85c933fb874e3a1f630bc44189a322aef79313c49a87`
* `win98/Bach's Brandenburg Concerto No. 3.rmi`: 144902,
  `68035e3bd4cd21fc69d1c4189971df91b8a2e45f0dfef3a0a6c034761e8f08ce`
* `win98/Beethoven's 5th Symphony.rmi`: 92466,
  `18aa196c443ed04bf07125ecd0b1c3bb8b96030cf105a5f7ad59411419a7d5ea`
* `win98/Beethoven's Fur Elise.rmi`: 21312,
  `9fd47212a8f3ad2d26fe63bc8ef063584303d01a063328a0286bacbabf71fb47`
* `win98/CANYON.MID`: 20861,
  `a50eabf9175535d0d1ddad647b4a90b2474fb63a8976648edc1835445fbc521e`
* `win98/Dance of the Sugar-Plum Fairy.rmi`: 20906,
  `73558ce9b2de296942121a87eaf71399119e6461c4fb2c86514eecae218b8f16`
* `win98/Debussy's Claire de Lune.rmi`: 27940,
  `3793c951990d0ddfc8d7faa56bc9fc9a94590990944e0ae8ea28acdbee3697ad`
* `win98/In the Hall of the Mountain King.rmi`: 38444,
  `6d9e816b33a1b2f4d30344fe40174ce0685c4b452d12708547e61eefa467def4`
* `win98/PASSPORT.MID`: 23165,
  `ec49a2f312673c24e51feac054c34eb37a46192462c6086ad2dd4c90edcb91fe`
* `winxp/201 - Flourish.mid`: 24253,
  `5921b5e73febe6e1df1b99c76271eb8c693075280ed5e6ee663c6d0fe1758953`
* `winxp/202 - One Stop.mid`: 40075,
  `679054db4e301027fef6a80c7b5d3e0f4f159e7be38dc4392b56d264f77a8632`
* `winxp/203 - Town.mid`: 22097,
  `184030546d71f7323b3c58be5692b5692cc46469df060b2e670a5a3ade90c4af`
