; Device structure of ESFM.DRV.  There's one per ES1869 devnode,
; LocalAlloc'd in DGROUP (311h bytes, zeroed) and linked from device_list.
; The offsets are the ones the code in seg1.asm and seg3.asm uses.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

DEV_VXD_ENTRY   equ 0x000   ; dword: ES1869.VXD PM API entry point
DEV_FM_COUNT    equ 0x004   ; dword: FM acquire count (0102/0103)
DEV_FM_INFO     equ 0x008   ; word: from 0101
DEV_FM_PORT     equ 0x00A   ; word: FM_Base
DEV_DEVNODE     equ 0x00C   ; dword
DEV_REFS        equ 0x010   ; word: driver messages 62h/63h
DEV_ENABLED     equ 0x012   ; word
DEV_ACTIVE      equ 0x014   ; word: FM acquired and the chip set up
DEV_OPEN        equ 0x016   ; word: a client has the device open
DEV_FREE1       equ 0x018   ; word: find_voices result (FFh = none)
DEV_FREE2       equ 0x01A   ; word: second free voice
DEV_CLOCK       equ 0x01C   ; dword: note-on counter (voice age)
DEV_PROGRAM     equ 0x020   ; 16 bytes: program of each channel
DEV_RPN_DATA    equ 0x030   ; 16 bytes: pitch bend range (RPN 0) of each channel
DEV_CHAN_FLAGS  equ 0x040   ; 16 bytes: bit 0 sustain pedal down, bits 1-2 RPN 0 selected
DEV_BEND        equ 0x050   ; 16 words: pitch bend of each channel (2000h = center)
DEV_VOICES      equ 0x070   ; 18 voices of VOICE_SIZE bytes
DEV_CHAN_VOLCURVE equ 0x2C2 ; 16 bytes: vol_curve entry for CC 7
DEV_CHAN_PAN    equ 0x2D2   ; 16 bytes: 10h left, 30h center, 20h right
DEV_CHAN_VOLUME equ 0x2E2   ; 16 bytes: CC 7
DEV_CHAN_EXPR   equ 0x2F2   ; 16 bytes: CC 11
DEV_MIDI_VOLUME equ 0x302   ; dword: midiOutSetVolume
DEV_NOTIFY      equ 0x306   ; dword: handle from 0200
DEV_MIXER_CB    equ 0x30A   ; far pointer: volume callback set through vxd_notify
DEV_FLAGS       equ 0x30E   ; byte: bit 2 suspended
DEV_NEXT        equ 0x30F   ; word: next device structure
DEV_SIZE        equ 0x311

; one entry of the voice table
VOICE_SIZE      equ 0x21
VOICE_FLAGS     equ 0x00    ; 1 keyed on, 2 keyed off, 4 note off held by the
                            ; sustain pedal, 8 second voice of a patch
VOICE_CLOCK     equ 0x01    ; dword: DEV_CLOCK at key on
VOICE_CHANNEL   equ 0x05
VOICE_NOTE      equ 0x06
VOICE_HDR3      equ 0x07    ; byte 3 of the patch voice header
VOICE_HDR0      equ 0x18    ; byte 0 of the patch voice header
NUM_VOICES      equ 18

; ESFM_FIX: queue of messages that arrive while the driver is held
QSIZE           equ 64          ; entries, a power of 2
QENTRY          equ 16
Q_MSG           equ 0           ; word: uMsg (0 = dropped)
Q_USER          equ 2           ; dword: dwUser
Q_DW1           equ 6           ; dword: dwParam1
Q_DW2           equ 10          ; dword: dwParam2
