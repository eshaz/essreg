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
;    every channel, pedal included, as GM synths do (fix_process).  ESS's
;    parser skips every SysEx byte
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

MIDIHDR_DATA    equ 0x00
MIDIHDR_LENGTH  equ 0x04

; a program change (short_msg, after the program is stored): let go of the
; channel's sustain pedal, as controller 64 with 0 would
; fix_program(dev, channel), near, pops its arguments
fix_program:
        push    bp
        mov     bp,sp
        push    si
        push    di
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

; modMessage_orig, with a look at long messages first: a GM, GS or XG reset
; resets the controllers of every channel before ESS's code plays the rest
; far pascal, the arguments of modMessage
fix_process:
        push    bp
        mov     bp,sp
        cmp     word [bp+0x12],MODM_LONGDATA
        jne     .orig
        push    di
        les     bx,[bp+0x0A]            ; MIDIHDR
        test    byte [es:bx+MIDIHDR_FLAGS],MHDR_PREPARED
        jz      .done                   ; ESS's code refuses it
        mov     cx,[es:bx+MIDIHDR_LENGTH]
        cmp     word [es:bx+MIDIHDR_LENGTH+2],0
        je      .scan
        mov     cx,0xFFFF
.scan:
        les     di,[es:bx+MIDIHDR_DATA]
        call    fix_find_reset
        jne     .done
        call    fix_gm_reset
.done:
        pop     di
.orig:
        pop     bp
        jmp     modMessage_orig

; ZF set if the CX bytes at ES:DI hold a GM, GS or XG reset
;   F0 7E dd 09 01 F7                   GM System On (09 03: GM2)
;   F0 41 dd 42 12 40 00 7F 00 41 F7    GS reset
;   F0 43 1n 4C 00 00 7E 00 F7          XG System On
fix_find_reset:
        push    si
        cld
.next:
        jcxz    .none
        cmp     byte [es:di],0xF0
        jne     .skip
        cmp     cx,6
        jb      .skip
        mov     al,[es:di+1]
        cmp     al,0x7E
        je      .gm
        cmp     al,0x41
        je      .gs
        cmp     al,0x43
        je      .xg
        jmp     .skip
.gm:
        cmp     byte [es:di+3],0x09
        jne     .skip
        mov     al,[es:di+4]
        cmp     al,0x01
        je      .gmon
        cmp     al,0x03
        jne     .skip
.gmon:
        cmp     byte [es:di+5],0xF7
        je      .found
        jmp     .skip
.gs:
        cmp     cx,11
        jb      .skip
        mov     si,fix_gs_tail
        mov     ax,8
        jmp     .tail
.xg:
        cmp     cx,9
        jb      .skip
        mov     al,[es:di+2]
        and     al,0xF0
        cmp     al,0x10
        jne     .skip
        mov     si,fix_xg_tail
        mov     ax,6
.tail:
        ; the bytes after the device number
        push    cx
        push    di
        add     di,3
        mov     cx,ax
        repe    cmpsb
        pop     di
        pop     cx
        je      .found
.skip:
        inc     di
        dec     cx
        jmp     .next
.none:
        or      sp,sp                   ; ZF clear
        jmp     .out
.found:
        xor     ax,ax                   ; ZF set
.out:
        pop     si
        ret

; a GM, GS or XG reset: controller 121 (reset all controllers, the pedal
; up) and 123 (all notes off) on every channel of every open device
fix_gm_reset:
        push    si
        push    di
        mov     si,[device_list]
.dev:
        or      si,si
        jz      .done
        cmp     word [si+DEV_OPEN],0
        je      .next
        xor     di,di
.chan:
        push    si
        push    word 0
        mov     ax,di
        or      ax,0x79B0               ; Bn 79h
        push    ax
        call    short_msg
        push    si
        push    word 0
        mov     ax,di
        or      ax,0x7BB0               ; Bn 7Bh
        push    ax
        call    short_msg
        inc     di
        cmp     di,16
        jb      .chan
.next:
        mov     si,[si+DEV_NEXT]
        jmp     .dev
.done:
        pop     di
        pop     si
        ret
