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

; a client's block (16 bytes, LocalAlloc'd by MODM_OPEN, its dwUser)
CLIENT_DEV      equ 0x00E       ; word: its device structure

; ESFM_FIX: queue of messages that arrive while the driver is held
QSIZE           equ 256         ; entries, a power of 2: a GM file's
                                ; first tick is over 100 messages
QENTRY          equ 16
Q_MSG           equ 0           ; word: uMsg (0 = dropped)
Q_USER          equ 2           ; dword: dwUser
Q_DW1           equ 6           ; dword: dwParam1
Q_DW2           equ 10          ; dword: dwParam2

; ESFM_FIX: the bank file (esfmfile.asm)
FIX_PATH        equ 128         ; bytes for Bank= from SYSTEM.INI
BANK_ID         equ 1234        ; resource 256: the built-in bank
BANK_ESS_ID     equ 1235        ; and ESS's bank, for BetterSquareWave=0

; ESFM_FIX: the changes that are on (fix_opts), SYSTEM.INI [ESFM.DRV]
; (esfmini.asm)
OPT_QUEUE       equ 0x0001      ; QueueWhileBusy
OPT_SILENCE     equ 0x0002      ; SilenceOnClose
OPT_PEDAL       equ 0x0004      ; PedalRelease
OPT_VIBRATO     equ 0x0008      ; Vibrato
OPT_TUNING      equ 0x0010      ; Tuning
OPT_CC121       equ 0x0020      ; ResetControllers
OPT_LIVE_PAN    equ 0x0040      ; LivePan
OPT_SYSEX       equ 0x0080      ; SysEx
OPT_RUNNING     equ 0x0100      ; RunningStatus
OPT_SQUARE      equ 0x0200      ; BetterSquareWave: the built-in bank
OPT_SIDES       equ 0x0400      ; TrueSides: left on the left output
OPT_OPL3        equ 0x0800      ; Opl3Device: device 1 on OPL3 mode
OPT_OPL3_DRV    equ 0x1000      ; Opl3Device=2: device 1 is ESFMOPL3.DRV's
OPT_SETTINGS    equ 0x2000      ; BankSettings: the Multimedia control
                                ; panel's Settings box (esfmcfg.asm)
OPT_READ        equ 0x8000      ; SYSTEM.INI was read
OPT_DEFAULT     equ 0x2FFF

; ESFM_FIX: General MIDI state (esfmgm.asm), after ESS's fields.  The fixed
; driver allocates DEV_FIX_SIZE bytes for each device
DEV_GM_MOD      equ 0x311   ; 16 bytes: controller 1 (modulation)
DEV_GM_PRESS    equ 0x321   ; 16 bytes: channel pressure
DEV_GM_VIB      equ 0x331   ; 16 bytes: the vibrato the voices have, VIB_*
DEV_GM_RPN      equ 0x341   ; 16 words: RPN selected, MSB * 256 + LSB
DEV_GM_CENTS    equ 0x361   ; 16 bytes: pitch bend range cents (RPN 0 LSB)
DEV_GM_FINE     equ 0x371   ; 16 words: fine tuning (RPN 1), 2000h = 0 cents
DEV_GM_COARSE   equ 0x391   ; 16 bytes: coarse tuning (RPN 2), 40h = none
DEV_GM_MASTER   equ 0x3A1   ; byte: master volume attenuation, 0.75 dB steps
DEV_GM_OPREGS   equ 0x3A2   ; 2 bytes for each operator (voice * 4 + op):
                            ; the patch's registers 0 and 6
DEV_LONG_IDX    equ 0x432   ; byte: the long-message parser at the end of
DEV_LONG_LEFT   equ 0x433   ; byte: the last buffer, to go on in the next
DEV_LONG_MSG    equ 0x434   ; dword: (seg1 modm_longdata, [bp-2], [bp-15h],
                            ; [bp-14h])

; ESFM_FIX: the OPL3 device, device 1 (esfmopl.asm)
DEV_OPL_CLIENT  equ 0x438   ; word: its client, 0 while it's closed
DEV_OPL_READY   equ 0x43A   ; byte: OPL_*, what device 1 has of the chip
DEV_OPL_STATUS  equ 0x43B   ; byte: running status of its short messages
DEV_OPL_CLOCK   equ 0x43C   ; word: the voices' ages
DEV_OPL_LSTAT   equ 0x43E   ; byte: long messages: the status in force
DEV_OPL_LNEED   equ 0x43F   ; byte: its data bytes, 1 or 2
DEV_OPL_LGOT    equ 0x440   ; byte: the data bytes it has
DEV_OPL_LD1     equ 0x441   ; byte: its first data byte
DEV_OPL_LSYX    equ 0x442   ; byte: bytes of the SysEx so far, 0 = none
DEV_OPL_SYX     equ 0x443   ; OPL_SYX bytes: its start
OPL_SYX         equ 12
DEV_OPL_CHAN    equ 0x44F   ; 16 channels of OCH_SIZE bytes
DEV_OPL_VOICE   equ 0x54F   ; 18 voices of OV_SIZE bytes
DEV_FIX_SIZE    equ 0x66F

; a channel of the OPL3 device
OCH_PROG        equ 0       ; its program
OCH_VOL         equ 1       ; controller 7
OCH_EXPR        equ 2       ; controller 11
OCH_PAN         equ 3       ; register C0h bits 5:4 for its pan
OCH_PEDAL       equ 4       ; 1 while the sustain pedal is down
OCH_MOD         equ 5       ; controller 1
OCH_BEND        equ 6       ; word: pitch bend, 2000h in the middle
OCH_RANGE       equ 8       ; word: bend range, 1/256 semitones
OCH_RPN         equ 10      ; word: the RPN selected, MSB * 256 + LSB
OCH_SEMI        equ 12      ; byte: bend range semitones (RPN 0)
OCH_CENT        equ 13      ; byte: and cents
OCH_SIZE        equ 16

; a voice of the OPL3 device, a 2-operator channel of the OPL3
OV_FLAGS        equ 0       ; OVF_*
OV_CHAN         equ 1       ; its MIDI channel
OV_KEY          equ 2       ; the key of its note on
OV_VEL          equ 3       ; and the velocity
OV_INSTR        equ 4       ; word: instrument * 2 + its voice, whose
                            ; registers it has, FFFFh none
OV_AGE          equ 6       ; word: DEV_OPL_CLOCK at its last key on or off
OV_PITCH        equ 8       ; word: its pitch without the bend, 1/256
                            ; semitones of MIDI notes
OV_B0           equ 10      ; byte: register B0h without the key on
OV_TL           equ 11      ; byte: the carrier's attenuation written
OV_C0           equ 12      ; byte: register C0h written
OV_VIB          equ 13      ; byte: 40h while registers 20h have vibrato
OV_SIZE         equ 16
OVF_ON          equ 1       ; keyed on
OVF_HELD        equ 2       ; its note off waits for the pedal
OVF_SECOND      equ 4       ; the second voice of its instrument
OPL_VOICES      equ 18
OPL_HAS         equ 1       ; DEV_OPL_READY: device 1 has the chip,
                            ; out of native mode, from its open
OPL_ENGINE      equ 2       ; its General MIDI set the chip up
