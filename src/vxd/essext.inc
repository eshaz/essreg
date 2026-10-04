; Layout constants of the ES1869.VXD extension (src/vxd/essext.asm), for
; the few places in ESS's code that change with ESSREG_EXT=1.
;
; Notes:
;
; The extension keeps its per-device state after ESS's E9h bytes of the
; ADI, and a pointer to a VM's virtual FM chip after ESS's 2Eh bytes of the
; per-VM node (see L1_0479). Both blocks are allocated zeroed.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

%ifndef ESSEXT_INC
%define ESSEXT_INC

; ADI fields after ESS's
EX_Flags                equ 0xEC        ; byte: EXF_*
EX_D1                   equ 0xED        ; byte: D1_*, what the DOS FM owner changed
EX_Snap                 equ 0xF0        ; SNAP_COUNT bytes: Windows' mixer
ESSREG_ADI_SIZE         equ 0x110       ; ESS allocated E9h

EXF_SNAP                equ 0x01        ; EX_Snap holds Windows' mixer
EXF_FM_SOFT             equ 0x02        ; Windows has FM from a port access only
EXF_FM_RESET            equ 0x04        ; the last FM acquisition reset the chip
EXF_DOS_FM              equ 0x08        ; a DOS VM had FM since Windows reset it
EXF_MIDI_CLOSED         equ 0x10        ; Windows' MIDI gave FM back (0103):
                                        ; ES1869.DRV gives I2S the music DAC next
EXF_A1_BYPASS           equ 0x20        ; a DOS program's Audio 1 filter bypassed

D1_DAC                  equ 0x01        ; 7Fh bit 0 cleared: FM has the music DAC
D1_VOL                  equ 0x02        ; 36h set from 00h to ESSREG_FM_VOLUME

; per-VM node (one per VM and device instance)
NODE_VFM                equ 0x30        ; dword: the VM's virtual FM chip, 0 if none
ESSREG_NODE_SIZE        equ 0x34        ; ESS used 2Eh

%endif
