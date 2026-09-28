; General MIDI for ESFM.DRV, assembled when ESFM_FIX=1.  The code is
; appended to segment 1 (fixed code).
;
; Notes:
;
; ESS's code plays notes, programs, pitch bend, the pedal and controllers
; 7, 10 and 11.  GM also asks for these, added here:
; 1. controller 1 (modulation) and channel pressure turn on the chip's
;    vibrato on the channel's voices, the larger of the two counts: 1-63
;    the shallow depth, 64-127 the deep one.  The chip's vibrato is about
;    6 Hz, 7 or 14 cents deep
; 2. RPN 0 (pitch bend range) with its cents (controller 38), RPN 1 (fine
;    tuning) and RPN 2 (coarse tuning, not on channel 10, from the next
;    note).  ESS's code only had RPN 0 in semitones.  NRPNs select nothing
; 3. controller 121 resets what RP-015 says: expression, modulation,
;    pressure, the pedal, pitch bend and the RPN selected.  ESS's code
;    also reset volume, pan and the bend range, and left sounding notes
;    bent
; 4. pan (controllers 8 and 10) and the vibrato change the notes that
;    already sound, not only the next ones
; 5. a GM, GM2, GS or XG reset in a long message sets every channel back
;    to the GM defaults and turns its notes off, and the master volume
;    (F0 7F dd 04 01 ll mm F7) turns every voice down (fix_process), on
;    the devices that have FM
; 6. running status as the MIDI spec has it: a real-time byte leaves it
;    alone, and the long-message parser goes on in the next buffer where
;    the last one stopped (fix_status, fix_long_load)
; The GM state of each channel is kept after ESS's fields of the device
; (DEV_GM_* in esfmdev.inc), and chip_reset sets it to the defaults.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

VIB_ON          equ 1           ; DEV_GM_VIB: register 0 bit 6 set
VIB_DEEP        equ 2           ; and register 6 bit 6
RPN_NONE        equ 0x7F7F

MIDIHDR_DATA    equ 0x00
MIDIHDR_LENGTH  equ 0x04

; chip_reset: the GM state of dev (DI) back to the defaults
; keeps DI
fix_gm_init:
        xor     bx,bx
.chan:
        mov     byte [bx+di+DEV_GM_MOD],0
        mov     byte [bx+di+DEV_GM_PRESS],0
        mov     byte [bx+di+DEV_GM_VIB],0
        mov     byte [bx+di+DEV_GM_CENTS],0
        mov     byte [bx+di+DEV_GM_COARSE],0x40
        mov     si,bx
        add     si,si
        add     si,di                   ; dev + 2 * channel
        mov     word [si+DEV_GM_RPN],RPN_NONE
        mov     word [si+DEV_GM_FINE],0x2000
        inc     bx
        cmp     bx,16
        jb      .chan
        mov     byte [di+DEV_GM_MASTER],0
        mov     word [di+DEV_LONG_IDX],0
        mov     word [di+DEV_LONG_MSG],0
        mov     word [di+DEV_LONG_MSG+2],0
        ret

; modm_longdata (seg1): the parser goes on where the last buffer left it,
; so a message split between two buffers, or running status across them,
; isn't lost. SI = dev, BP = modMessage's frame; keeps all but AX
fix_long_load:
        mov     al,[si+DEV_LONG_IDX]
        mov     [bp-0x2],al
        mov     al,[si+DEV_LONG_LEFT]
        mov     [bp-0x15],al
        mov     ax,[si+DEV_LONG_MSG]
        mov     [bp-0x14],ax
        mov     ax,[si+DEV_LONG_MSG+2]
        mov     [bp-0x12],ax
        ret

; and keeps it at the end of the buffer; keeps all but AX
fix_long_save:
        push    si
        mov     si,[bp-0x10]            ; dev
        mov     al,[bp-0x2]
        mov     [si+DEV_LONG_IDX],al
        mov     al,[bp-0x15]
        mov     [si+DEV_LONG_LEFT],al
        mov     ax,[bp-0x14]
        mov     [si+DEV_LONG_MSG],ax
        mov     ax,[bp-0x12]
        mov     [si+DEV_LONG_MSG+2],ax
        pop     si
        ret

; short message status byte AL (seg1 modm_data), SI = dev: a real-time
; byte (F8h-FFh) changes nothing, a system common one (F0h-F7h) ends the
; running status and a channel one starts it, for the next long message
; too, as if all the bytes came one after the other. ESS's code made any
; status byte the running status. Keeps all but AX
fix_status:
        cmp     al,0xF8
        jae     .keep
        xor     ah,ah
        cmp     al,0xF0
        jb      .channel
        xor     al,al
        mov     [si+DEV_LONG_IDX],ax    ; and DEV_LONG_LEFT: no message
        jmp     .set
.channel:
        ; a long message goes on with its data bytes, one for Cn and Dn
        mov     ah,2
        cmp     al,0xC0
        jb      .left
        cmp     al,0xE0
        jae     .left
        mov     ah,1
.left:
        mov     byte [si+DEV_LONG_IDX],1
        mov     [si+DEV_LONG_LEFT],ah
        xor     ah,ah
.set:
        mov     [si+DEV_LONG_MSG],ax
        mov     word [si+DEV_LONG_MSG+2],0
        mov     [running_status],al
.keep:
        ret

; a controller (short_msg, frame of short_msg): the ones GM adds or that
; work differently here.  CF set when handled, clear for ESS's code
; keeps SI, DI
fix_control:
        push    si
        push    di
        mov     si,[bp+0x8]             ; dev
        mov     bl,[bp-0x3]
        xor     bh,bh
        mov     di,bx                   ; channel
        mov     al,[bp-0x2]             ; controller
        mov     ah,[bp-0x1]             ; value
        cmp     al,1
        je      .mod
        cmp     al,6
        je      .data_msb
        cmp     al,8
        je      .pan
        cmp     al,10
        je      .pan
        cmp     al,38
        je      .data_lsb
        cmp     al,98
        je      .nrpn
        cmp     al,99
        je      .nrpn
        cmp     al,100
        je      .rpn_lsb
        cmp     al,101
        je      .rpn_msb
        cmp     al,121
        je      .reset
        clc
        jmp     .out

.mod:
        mov     [bx+si+DEV_GM_MOD],ah
        call    fix_vibrato
        jmp     .done

.pan:
        ; as ESS's code: above 50h right, below 30h left, else both
        mov     al,0x20
        cmp     ah,0x50
        ja      .setpan
        mov     al,0x10
        cmp     ah,0x30
        jb      .setpan
        mov     al,0x30
.setpan:
        cmp     [bx+si+DEV_CHAN_PAN],al
        je      .done                   ; the three positions: often the same
        mov     [bx+si+DEV_CHAN_PAN],al
        call    fix_refresh
        jmp     .done

.nrpn:
        add     bx,bx
        mov     word [bx+si+DEV_GM_RPN],RPN_NONE
        jmp     .done
.rpn_lsb:
        add     bx,bx
        mov     [bx+si+DEV_GM_RPN],ah
        jmp     .done
.rpn_msb:
        add     bx,bx
        mov     [bx+si+DEV_GM_RPN+1],ah
        jmp     .done

.data_msb:
        add     bx,bx
        mov     dx,[bx+si+DEV_GM_RPN]
        cmp     dx,0x0001
        je      .fine_msb
        mov     bx,di
        cmp     dx,0x0002
        je      .coarse
        cmp     dx,0x0000
        jne     .done
        ; RPN 0: the bend range, the MSB sets the cents to 0
        mov     [bx+si+DEV_RPN_DATA],ah
        mov     byte [bx+si+DEV_GM_CENTS],0
        jmp     .bend
.coarse:
        mov     [bx+si+DEV_GM_COARSE],ah
        jmp     .done
.fine_msb:
        xor     al,al
        shr     ax,1                    ; value * 128
        mov     [bx+si+DEV_GM_FINE],ax
        jmp     .bend

.data_lsb:
        add     bx,bx
        mov     dx,[bx+si+DEV_GM_RPN]
        cmp     dx,0x0001
        je      .fine_lsb
        cmp     dx,0x0000
        jne     .done
        mov     bx,di
        mov     [bx+si+DEV_GM_CENTS],ah
        jmp     .bend
.fine_lsb:
        mov     dx,[bx+si+DEV_GM_FINE]
        and     dx,0x3F80
        or      dl,ah
        mov     [bx+si+DEV_GM_FINE],dx

.bend:
        ; the notes that sound follow the new range or tuning
        ; pitch_bend(dev, channel, the channel's bend)
        mov     bx,di
        add     bx,bx
        push    si
        push    di
        push    word [bx+si+DEV_BEND]
        call    pitch_bend
        jmp     .done

.reset:
        call    fix_ctl_reset
.done:
        stc
.out:
        pop     di
        pop     si
        ret

; controller 121, reset all controllers, as RP-015 says: expression 127,
; modulation and pressure 0, the pedal up, pitch bend centered and no RPN
; selected.  Volume, pan, the bend range and the tuning stay
; SI = dev, DI = channel
fix_ctl_reset:
        ; ESS's sustain lets go of the voices the pedal holds
        push    word 0
        push    di
        push    si
        push    cs
        call    sustain
        add     sp,6
        mov     bx,di
        mov     byte [bx+si+DEV_CHAN_EXPR],0x7F
        mov     byte [bx+si+DEV_GM_MOD],0
        mov     byte [bx+si+DEV_GM_PRESS],0
        and     byte [bx+si+DEV_CHAN_FLAGS],0xF9 ; ESS's RPN 0 bits
        add     bx,bx
        mov     word [bx+si+DEV_GM_RPN],RPN_NONE
        ; update_volume(dev, channel): the expression of the notes that sound
        push    si
        push    di
        call    update_volume
        call    fix_vibrato
        ; pitch_bend(dev, channel, 2000h): centered, the notes that sound too
        push    si
        push    di
        push    word 0x2000
        call    pitch_bend
        ret

; channel pressure (short_msg): fix_pressure(dev, channel, value)
; near, pops its arguments
fix_pressure:
        push    bp
        mov     bp,sp
        push    si
        push    di
        mov     si,[bp+0x4]             ; dev
        mov     di,[bp+0x6]             ; channel
        and     di,0x0F
        mov     al,[bp+0x8]
        and     al,0x7F
        mov     bx,di
        mov     [bx+si+DEV_GM_PRESS],al
        call    fix_vibrato
        pop     di
        pop     si
        pop     bp
        ret     6

; the channel's vibrato from its modulation and pressure, and on the notes
; that sound if it changed
; SI = dev, DI = channel
fix_vibrato:
        mov     bx,di
        mov     al,[bx+si+DEV_GM_MOD]
        cmp     al,[bx+si+DEV_GM_PRESS]
        jae     .max
        mov     al,[bx+si+DEV_GM_PRESS]
.max:
        xor     ah,ah
        or      al,al
        jz      .set
        mov     ah,VIB_ON
        cmp     al,64
        jb      .set
        mov     ah,VIB_ON | VIB_DEEP
.set:
        cmp     [bx+si+DEV_GM_VIB],ah
        je      .same
        mov     [bx+si+DEV_GM_VIB],ah
        call    fix_refresh
.same:
        ret

; write registers 0 and 6 of every operator of the channel's keyed voices
; again: the patch's bytes with the channel's pan and vibrato
; SI = dev, DI = channel
fix_refresh:
        push    bp
        mov     bp,sp
        sub     sp,6
        ; [bp-2] operator (voice * 4 + op), [bp-4] voice entry, [bp-6] ops left
        mov     word [bp-2],0
        lea     ax,[si+DEV_VOICES]
        mov     [bp-4],ax
.voice:
        mov     bx,[bp-4]
        test    byte [bx+VOICE_FLAGS],1
        jz      .skip
        mov     al,[bx+VOICE_CHANNEL]
        xor     ah,ah
        cmp     ax,di
        jne     .skip
        mov     word [bp-6],4
.op:
        mov     bx,[bp-2]
        add     bx,bx
        add     bx,si
        mov     al,[bx+DEV_GM_OPREGS]
        call    fix_vib0
        push    si
        mov     dx,[bp-2]
        shl     dx,3                    ; operator * 8: its register 0
        push    dx
        push    ax
        push    cs
        call    fm_write
        mov     bx,[bp-2]
        add     bx,bx
        add     bx,si
        mov     al,[bx+DEV_GM_OPREGS+1]
        call    fix_vib6
        push    si
        mov     dx,[bp-2]
        shl     dx,3
        add     dx,6
        push    dx
        push    ax
        push    cs
        call    fm_write
        inc     word [bp-2]
        dec     word [bp-6]
        jnz     .op
        jmp     .next
.skip:
        add     word [bp-2],4
.next:
        add     word [bp-4],VOICE_SIZE
        cmp     word [bp-2],NUM_VOICES * 4
        jb      .voice
        mov     sp,bp
        pop     bp
        ret

; register 0 of an operator (AL, the patch's byte) with the channel's
; vibrato
; SI = dev, DI = channel
fix_vib0:
        mov     bx,di
        test    byte [bx+si+DEV_GM_VIB],VIB_ON
        jz      .out
        or      al,0x40
.out:
        ret

; register 6 of an operator (AL, the patch's byte) with the channel's pan,
; as program_operator sets it, and vibrato depth
; SI = dev, DI = channel
fix_vib6:
        mov     bx,di
        test    al,0x30
        jz      .depth
        mov     ah,[bx+si+DEV_CHAN_PAN]
        cmp     ah,0x30
        je      .depth
        and     al,0xCF
        or      al,ah
.depth:
        test    byte [bx+si+DEV_GM_VIB],VIB_DEEP
        jz      .out
        or      al,0x40
.out:
        ret

; program_operator's frame: BX = dev + 2 * (voice * 4 + op), SI = dev,
; DI = channel
fix_op_slot:
        mov     si,[bp+0x6]             ; dev
        mov     di,[bp+0x14]
        and     di,0x0F                 ; channel
        mov     bx,[bp+0x18]            ; voice
        shl     bx,2
        add     bx,[bp+0x16]            ; operator
        add     bx,bx
        add     bx,si
        ret

; program_operator, register 0: keep the patch's byte (AL) for fix_refresh
; and add the vibrato
; keeps every register but AX
fix_op_reg0:
        push    bx
        push    si
        push    di
        call    fix_op_slot
        mov     [bx+DEV_GM_OPREGS],al
        call    fix_vib0
        pop     di
        pop     si
        pop     bx
        ret

; program_operator, register 6: keep the patch's byte (ES:BX+6) for
; fix_refresh and add the vibrato depth to AL, which has the pan
; keeps every register but AX
fix_op_reg6:
        push    cx
        mov     cl,[es:bx+0x6]
        push    bx
        push    si
        push    di
        call    fix_op_slot
        mov     [bx+DEV_GM_OPREGS+1],cl
        mov     bx,di
        test    byte [bx+si+DEV_GM_VIB],VIB_DEEP
        jz      .out
        or      al,0x40
.out:
        pop     di
        pop     si
        pop     bx
        pop     cx
        ret

; program_operator: the coarse tuning of the channel moves the note, but
; not on channel 10 (drums)
; frame of program_operator, uses AX, BX
fix_coarse:
        mov     bx,[bp+0x14]
        and     bx,0x0F
        cmp     bx,9
        je      .out
        add     bx,[bp+0x6]
        mov     al,[bx+DEV_GM_COARSE]
        sub     al,0x40
        cbw
        add     [bp+0xA],ax
.out:
        ret

; calc_level, after the volume and expression: the master volume turns
; down the operators they turn down (level mode 1-3)
; frame of calc_level, DI = dev, SI = attenuation so far
fix_master:
        cmp     word [bp+0x6],0
        je      .out
        mov     al,[di+DEV_GM_MASTER]
        xor     ah,ah
        add     si,ax
.out:
        ret

; calc_pitch with the bend range cents and the fine tuning
; fix_calc_pitch(dev, channel, range, bend, freq), near, pops its arguments
; the pitch offset is (bend - 2000h) * range in cents / 3200 plus the fine
; tuning, in 1/256 semitones, up to calc_pitch's +-24 semitones.  With a
; range of 24, calc_pitch's bend of 2000h + offset * 4 / 3 (rounded up)
; gives that offset back
fix_calc_pitch:
        push    bp
        mov     bp,sp
        push    si
        push    di
        mov     si,[bp+0x4]             ; dev
        mov     di,[bp+0x6]
        and     di,0x0F                 ; channel
        ; the range in cents, up to 12700 so that the division fits
        mov     al,[bp+0x8]
        mov     ah,100
        mul     ah
        mov     bx,di
        mov     cl,[bx+si+DEV_GM_CENTS]
        xor     ch,ch
        add     cx,ax
        cmp     cx,12700
        jbe     .range
        mov     cx,12700
.range:
        mov     ax,[bp+0xA]             ; bend
        cmp     ax,0x3F80
        jb      .bend
        mov     ax,0x4000               ; the top of the wheel: the whole range
.bend:
        sub     ax,0x2000
        imul    cx
        mov     cx,3200
        idiv    cx
        ; the fine tuning, -256..+255
        add     bx,bx
        mov     cx,[bx+si+DEV_GM_FINE]
        sub     cx,0x2000
        sar     cx,5
        add     ax,cx
        cmp     ax,24 * 256 - 1
        jle     .high
        mov     ax,24 * 256 - 1
.high:
        cmp     ax,-24 * 256
        jge     .low
        mov     ax,-24 * 256
.low:
        shl     ax,2
        or      ax,ax
        js      .div
        add     ax,2
.div:
        cwd
        mov     cx,3
        idiv    cx
        add     ax,0x2000
        ; calc_pitch(24, bend, freq)
        push    word [bp+0xC]
        push    ax
        mov     ax,24
        push    ax
        call    calc_pitch
        pop     di
        pop     si
        pop     bp
        ret     0xA

; modMessage_orig, with a look at long messages first: GM, GS and XG resets
; and the master volume happen before ESS's code plays the rest
; far pascal, the arguments of modMessage
fix_process:
        push    bp
        mov     bp,sp
        cmp     word [bp+0x12],MODM_LONGDATA
        jne     .orig
        push    si
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
.next:
        call    fix_find_sysex
        or      ax,ax
        jz      .done
        push    es
        push    cx
        push    di
        cmp     ax,1
        jne     .volume
        call    fix_gm_reset
        jmp     .found
.volume:
        call    fix_master_volume
.found:
        pop     di
        pop     cx
        pop     es
        jmp     .next
.done:
        pop     di
        pop     si
.orig:
        pop     bp
        jmp     modMessage_orig

; the next reset or master volume in the CX bytes at ES:DI
; AX = 0 none, 1 a reset, 2 the master volume (DX = 0-3FFFh), with ES:DI
; and CX moved past its F0
;   F0 7E dd 09 01 F7                   GM System On (09 03: GM2)
;   F0 41 dd 42 12 40 00 7F 00 41 F7    GS reset
;   F0 43 1n 4C 00 00 7E 00 F7          XG System On
;   F0 7F dd 04 01 ll mm F7             master volume
fix_find_sysex:
        push    si
        cld
.next:
        or      cx,cx
        jz      .none
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
        cmp     al,0x7F
        je      .master
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
        jne     .skip
        jmp     .reset
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
        jne     .skip
.reset:
        mov     ax,1
        jmp     .found
.master:
        cmp     cx,8
        jb      .skip
        cmp     word [es:di+3],0x0104   ; 04 01
        jne     .skip
        cmp     byte [es:di+7],0xF7
        jne     .skip
        mov     dl,[es:di+5]            ; LSB
        mov     al,[es:di+6]            ; MSB
        and     dx,0x7F
        and     ax,0x7F
        shl     ax,7
        or      dx,ax
        mov     ax,2
.found:
        inc     di
        dec     cx
        jmp     .out
.skip:
        inc     di
        dec     cx
        jmp     .next
.none:
        xor     ax,ax
.out:
        pop     si
        ret

; a GM, GS or XG reset: every channel of every open device back to the GM
; defaults, with its notes off
fix_gm_reset:
        push    si
        push    di
        mov     si,[device_list]
.dev:
        or      si,si
        jz      .done
        call    fix_has_fm
        jz      .nextdev
        xor     di,di
.chan:
        ; controllers 121 (reset all controllers, the pedal up) and 123 (all
        ; notes off)
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
        ; and what controller 121 keeps: program 0, volume 100, pan center,
        ; bend range 2 semitones, no tuning
        mov     bx,di
        mov     byte [bx+si+DEV_PROGRAM],0
        mov     byte [bx+si+DEV_CHAN_VOLUME],100
        mov     byte [bx+si+DEV_CHAN_VOLCURVE],4
        mov     byte [bx+si+DEV_CHAN_PAN],0x30
        mov     byte [bx+si+DEV_RPN_DATA],2
        mov     byte [bx+si+DEV_GM_CENTS],0
        mov     byte [bx+si+DEV_GM_COARSE],0x40
        add     bx,bx
        mov     word [bx+si+DEV_GM_FINE],0x2000
        inc     di
        cmp     di,16
        jb      .chan
        mov     byte [si+DEV_GM_MASTER],0
.nextdev:
        mov     si,[si+DEV_NEXT]
        jmp     .dev
.done:
        pop     di
        pop     si
        ret

; the master volume (DX, 0-3FFFh) on every open device, the notes that
; sound too
fix_master_volume:
        push    si
        mov     bx,dx
        shr     bx,9
        mov     al,[cs:bx+fix_master_curve]
        mov     si,[device_list]
.dev:
        or      si,si
        jz      .done
        call    fix_has_fm
        jz      .next
        mov     [si+DEV_GM_MASTER],al
        push    ax
        ; update_volume(dev, FFh): every channel
        push    si
        mov     cx,0xFF
        push    cx
        call    update_volume
        pop     ax
.next:
        mov     si,[si+DEV_NEXT]
        jmp     .dev
.done:
        pop     si
        ret

; ZF clear if device SI is open and has FM: not while it's suspended, when
; FM isn't Windows' and ES1869.VXD traps the ports (ESS's code refuses the
; messages then)
fix_has_fm:
        cmp     word [si+DEV_OPEN],0
        je      .out
        cmp     word [si+DEV_ACTIVE],0
        je      .out
        test    byte [si+DEV_FLAGS],4
        jnz     .none
        or      sp,sp
.out:
        ret
.none:
        cmp     ax,ax                   ; ZF set
        ret

; attenuation in 0.75 dB steps for the master volume / 512: 40 log10(v / 127)
; dB, as GM's volume curve
fix_master_curve:
        db 63, 63, 57, 49, 44, 40, 36, 33, 30, 27, 25, 23, 21, 19, 18, 16
        db 15, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 2, 1, 0
