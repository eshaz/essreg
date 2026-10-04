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
; the virtual Sound Blaster of the DOS VM whose DSP a recording took
EX_VSB_VM               equ 0x110       ; dword: that VM, 0 if none
EX_VSB_Timer            equ 0x114       ; dword: the time-out of its block
EX_VSB_Due              equ 0x118       ; dword: when the block ends (1/1024 ms),
                                        ; or what's left of it while paused
EX_VSB_Block            equ 0x11C       ; dword: a block's length (1/1024 ms)
EX_VSB_Flags            equ 0x120       ; byte: VSB_*
EX_VSB_Cmd              equ 0x121       ; byte: the command whose operands come
EX_VSB_Need             equ 0x122       ; byte: operands still to come
EX_VSB_Got              equ 0x123       ; byte: operands so far
EX_VSB_Ops              equ 0x124       ; 2 bytes: the operands
EX_VSB_TC               equ 0x126       ; byte: time constant (40h)
EX_VSB_Test             equ 0x127       ; byte: test register (E4h)
EX_VSB_Size             equ 0x128       ; word: block size - 1 (48h)
EX_VSB_Rate             equ 0x12A       ; word: sample rate (41h), 0 if none
EX_VSB_Index            equ 0x12C       ; byte: mixer index
EX_VSB_Count            equ 0x12D       ; byte: bytes in the read queue
EX_VSB_Last             equ 0x12E       ; byte: the last byte read
EX_VSB_Queue            equ 0x130       ; 4 bytes: the read queue
EX_VSB_Id               equ 0x134       ; 2 bytes: the chip's answer to E7h
EX_VSB_Ver              equ 0x136       ; 2 bytes: and to E1h
EX_VSB_Ess              equ 0x138       ; 32 bytes: registers A0h-BFh
EX_VSB_Mix              equ 0x158       ; 64 bytes: mixer registers 00h-3Fh
; the event that gives the program's channel to the chip once it has the
; real DSP back; a new virtual Sound Blaster cancels it, so it isn't cleared
EX_VSB_Event            equ 0x198       ; dword: the event, 0 if none
EX_VSB_Event_VM         equ 0x19C       ; dword: its VM
ESSREG_ADI_SIZE         equ 0x1A0       ; ESS allocated E9h

VSB_RESET               equ 0x01        ; the reset port is at 1
VSB_SPEAKER             equ 0x02        ; the speaker is on (D1h)
VSB_EXT                 equ 0x04        ; Extended mode (C6h)
VSB_RUN                 equ 0x08        ; a transfer runs
VSB_AUTO                equ 0x10        ; it auto-initializes
VSB_PAUSE               equ 0x20        ; it's paused (D0h)
VSB_IRQ                 equ 0x40        ; its interrupt request is set
VSB_NOIRQ               equ 0x80        ; Extended mode without B1h bit 6

EXF_SNAP                equ 0x01        ; EX_Snap holds Windows' mixer
EXF_FM_SOFT             equ 0x02        ; Windows has FM from a port access only
EXF_FM_RESET            equ 0x04        ; the last FM acquisition reset the chip
EXF_DOS_FM              equ 0x08        ; a DOS VM had FM since Windows reset it
EXF_MIDI_CLOSED         equ 0x10        ; Windows' MIDI gave FM back (0103):
                                        ; ES1869.DRV gives I2S the music DAC next

D1_DAC                  equ 0x01        ; 7Fh bit 0 cleared: FM has the music DAC
D1_VOL                  equ 0x02        ; 36h set from 00h to ESSREG_FM_VOLUME

; per-VM node (one per VM and device instance)
NODE_VFM                equ 0x30        ; dword: the VM's virtual FM chip, 0 if none
ESSREG_NODE_SIZE        equ 0x34        ; ESS used 2Eh

%endif
