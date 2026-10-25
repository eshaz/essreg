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
EX_VSB_Flags2           equ 0x12F       ; byte: VSB2_*
EX_VSB_Queue            equ 0x130       ; 4 bytes: the read queue
EX_VSB_Id               equ 0x134       ; 2 bytes: the chip's answer to E7h
EX_VSB_Ver              equ 0x136       ; 2 bytes: and to E1h
EX_VSB_Ess              equ 0x138       ; 32 bytes: registers A0h-BFh
EX_VSB_Mix              equ 0x158       ; 64 bytes: mixer registers 00h-3Fh
; the event that gives the program's channel to the chip once it has the
; real DSP back; a new virtual Sound Blaster cancels it, so it isn't cleared
EX_VSB_Event            equ 0x198       ; dword: the event, 0 if none
EX_VSB_Event_VM         equ 0x19C       ; dword: its VM
; a DOS program's FM on its own clock (ESSREG_FMQ_*)
EX_FMQ_VM               equ 0x1A0       ; dword: the VM whose FM plays from
                                        ; the queue, 0 if none
EX_FMQ_Blk              equ 0x1A4       ; dword: its queue and clock (FMQ_*)
EX_FMQ_Direct           equ 0x1A8       ; dword: a VM whose program plays
                                        ; directly, its writes off its ticks
EX_VSB_Ops3             equ 0x1AC       ; byte: the third operand of a Sound
                                        ; Blaster 16 transfer command
; the Sound Blaster 16 front of the DOS VM that owns the DSP (ESSREG_S16_*),
; the same as esssb16.com's
EX_S16_Flags            equ 0x1AD       ; byte: S16_*
EX_S16_State            equ 0x1AE       ; byte: S16S_*, the DSP's state
EX_S16_Need             equ 0x1AF       ; byte: operands still to come
EX_S16_VM               equ 0x1B0       ; dword: the VM it serves, 0 if none
EX_S16_Last             equ 0x1B4       ; dword: the VM it served last, whose
                                        ; settings stay for its return
EX_S16_Got              equ 0x1B8       ; byte: operands so far
EX_S16_Ops              equ 0x1B9       ; 3 bytes: the operands
EX_S16_Index            equ 0x1BC       ; byte: the mixer index the VM wrote,
                                        ; FFh for the chip's (not yet seen)
EX_S16_Chan             equ 0x1BD       ; byte: the 16-bit DMA channel (5-7)
                                        ; the front carries over, 0 if none
EX_S16_Cmd              equ 0x1BE       ; byte: the last command
EX_S16_Prev             equ 0x1BF       ; byte: the one before it (FDh)
EX_S16_Aptr             equ 0x1C0       ; dword: the answer's bytes after the
                                        ; latch
EX_S16_Alen             equ 0x1C4       ; byte: how many
EX_S16_Latch            equ 0x1C5       ; byte: the byte the read port gives
EX_S16_Abuf             equ 0x1C6       ; byte: a one-byte answer
EX_S16_Held             equ 0x1C7       ; byte: a byte written while an
                                        ; answer blocks
EX_S16_RateV            equ 0x1C8       ; byte: the rate generator's value
EX_S16_Test             equ 0x1C9       ; byte: E4h's, which a reset keeps
EX_S16_Block8           equ 0x1CA       ; word: the 8-bit block, less one
                                        ; (48h, Cxh)
EX_S16_Xbits            equ 0x1CC       ; byte: the transfer's bits (BL of
                                        ; ESSREG_S16_Xfer)
EX_S16_DMA              equ 0x1CD       ; byte: 1 while Audio 1's channel runs
                                        ; the 16-bit channel's programming
EX_S16_Stale            equ 0x1CE       ; byte: the view's levels a write of
                                        ; the chip's own registers changed
                                        ; (bit 0 master, then
                                        ; ESSREG_S16_Aliases)
EX_S16_Sel              equ 0x1CF       ; byte: the 16-bit channel the program
                                        ; picked (mixer 81h), 0 for none
EX_S16_Mix              equ 0x1D0       ; 24 bytes: the Sound Blaster 16's
                                        ; mixer registers 30h-47h
EX_S16_Ctrl             equ 0x1E8       ; 32 bytes: the chip's registers
                                        ; A0h-BFh as the front last wrote or
                                        ; read them
EX_S16_Known            equ 0x208       ; dword: bit n set while EX_S16_Ctrl+n
                                        ; holds the chip's A0h+n
ESSREG_ADI_SIZE         equ 0x20C       ; ESS allocated E9h

VSB_RESET               equ 0x01        ; the reset port is at 1
VSB_SPEAKER             equ 0x02        ; the speaker is on (D1h)
VSB_EXT                 equ 0x04        ; Extended mode (C6h)
VSB_RUN                 equ 0x08        ; a transfer runs
VSB_AUTO                equ 0x10        ; it auto-initializes
VSB_PAUSE               equ 0x20        ; it's paused (D0h)
VSB_IRQ                 equ 0x40        ; its interrupt request is set
VSB_NOIRQ               equ 0x80        ; Extended mode without B1h bit 6

VSB2_MININT             equ 0x01        ; VTD gives it 1 ms timer interrupts
VSB2_ESS                equ 0x02        ; the program knows the chip (S16_ESS):
                                        ; Bxh and Cxh are the chip's commands
VSB2_16                 equ 0x04        ; its transfer is 16-bit (mixer 82h)
VSB2_STEREO             equ 0x08        ; and stereo (two samples a frame)

; the Sound Blaster 16 front (EX_S16_Flags), as esssb16.com's F_*; S16_AUTO
; and S16_16 are the bits of ESSREG_S16_Xfer's BL
S16_ESS                 equ 0x01        ; the program knows the chip (C6h and a
                                        ; register): every access is the chip's
S16_EXT                 equ 0x02        ; the chip is in Extended mode, its DAC on
S16_AUTO                equ 0x04        ; the transfer auto-initializes
S16_RUN                 equ 0x08        ; a transfer of the front's is on Audio 1
S16_16                  equ 0x10        ; it is 16-bit (the 16-bit interrupt)
S16_LAST                equ 0x20        ; a single-cycle block is queued: the
                                        ; auto-initialize ends at the next
                                        ; acknowledge
S16_PAUSE               equ 0x40        ; it is paused
S16_RESET               equ 0x80        ; the reset port is at 1

; the front's DSP (EX_S16_State), as esssb16.com's S_*
S16S_FULL               equ 0x01        ; the read latch has a byte
S16S_HELD               equ 0x02        ; a written byte waits for the answer's
                                        ; end
S16S_UART               equ 0x04        ; MIDI UART: the DSP is the chip's until
                                        ; a reset
S16S_MIDI               equ 0x08        ; MIDI input: the next write ends it
S16S_COMP               equ 0x10        ; the chip is in Compatibility mode
S16S_WARM               equ 0x20        ; the next reset is a warm boot
S16S_SPKR               equ 0x40        ; D1h's flag, for D8h
S16S_T16                equ 0x80        ; the chip's processor interrupt is the
                                        ; 16-bit one (F3h)

; the queue of a DOS program's FM writes and the clock of its ticks, a
; block allocated while its FM plays from the queue
FMQ_Head                equ 0x00        ; dword: the oldest entry
FMQ_Count               equ 0x04        ; dword: entries queued
FMQ_Timer               equ 0x08        ; dword: the time-out of the next
                                        ; entry, 0 if none
FMQ_Last                equ 0x0C        ; dword: when the last tick came (us)
FMQ_V                   equ 0x10        ; dword: this tick on the clock (us)
FMQ_T                   equ 0x14        ; dword: a tick (1/256 us)
FMQ_Slew                equ 0x18        ; dword: us added to each tick of
                                        ; this window
FMQ_BaseV               equ 0x1C        ; dword: the clock where the rate's
                                        ; baseline starts
FMQ_Ticks               equ 0x20        ; dword: ticks since the clock locked
FMQ_BaseK               equ 0x24        ; dword: ticks where the baseline starts
FMQ_Min                 equ 0x28        ; dword: the least lag of this window
FMQ_Frac                equ 0x2C        ; byte: 1/256 us the clock is behind
FMQ_Flags               equ 0x2D        ; byte: FMQF_*
FMQ_Win                 equ 0x2E        ; byte: ticks in this window
FMQ_Low                 equ 0x2F        ; byte: of them, held back to the delay
FMQ_High                equ 0x30        ; byte: of them, held to the real time
FMQ_NIval               equ 0x31        ; byte: intervals before it locks
FMQ_IPos                equ 0x32        ; byte: where the next interval goes
FMQ_Writes              equ 0x33        ; byte: data writes in this check
FMQ_Late                equ 0x34        ; byte: of them, long after a tick
FMQ_Ivals               equ 0x40        ; 16 dwords: the last intervals (us)
FMQ_Queue               equ 0x80        ; FMQ_LEN entries of 8 bytes: when it's
                                        ; due (us), the port's offset, the value
FMQ_LEN                 equ 1024
FMQ_SIZE                equ FMQ_Queue + FMQ_LEN * 8

FMQF_LOCKED             equ 0x01        ; the clock follows the ticks
FMQF_LAST               equ 0x02        ; FMQ_Last holds a tick
FMQF_MININT             equ 0x04        ; VTD gives 1 ms timer interrupts

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
