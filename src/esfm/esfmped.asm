; Sustain pedal fixes for ESFM.DRV, assembled when ESFM_FIX=1.  The code is
; appended to segment 1 (fixed code).
;
; Notes:
;
; The ESS driver follows the MIDI spec for controller 64: a note off while
; the pedal is down leaves the voice keyed on until the pedal goes up.  An
; FM voice with a sustaining envelope doesn't fade, so a pedal a song
; leaves down holds its notes until something lets the pedal up:
; 1. a program change lets go of the channel's pedal (fix_program).  The
;    MIDI spec keeps it down, but a pedal that is still down when a channel
;    gets another instrument is left over from the part before
; 2. a GM, GS or XG reset in a long message resets the controllers of
;    every channel, pedal included, as GM synths do (fix_gm_reset in
;    esfmgm.asm).  ESS's parser skips every SysEx byte
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; a program change (short_msg, after the program is stored): let go of the
; channel's sustain pedal, as controller 64 with 0 would (not with
; PedalRelease=0)
; fix_program(dev, channel), near, pops its arguments
fix_program:
        push    bp
        mov     bp,sp
        push    si
        push    di
        test    word [fix_opts],OPT_PEDAL
        jz      .out
        mov     si,[bp+0x4]             ; dev
        mov     di,[bp+0x6]             ; channel
        and     di,0x0F
        mov     bx,di
        test    byte [bx+si+DEV_CHAN_FLAGS],1
        jz      .out
        push    word 0                  ; pedal up
        push    di
        push    si
        push    cs
        call    sustain
        add     sp,6
.out:
        pop     di
        pop     si
        pop     bp
        ret     4
