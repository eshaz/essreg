; The music DAC and mixer 7Fh while ES1869.DRV's FM recording device
; records, assembled when ES1869_FIX=1 and appended to segment 1.
;
; Notes:
;
; Mixer 7Fh bit 4 sends the music DAC's samples to Audio 1's DMA in
; place of the ADC's, and bit 0 clear keeps the music DAC FM's rather
; than I2S's (DS p.65).  fm_route sets both while device 1 of fmwave.asm
; records, and puts back what they held when it stops.
;
; ESS's code gives the music DAC back to I2S when the FM driver closes
; (5:206D) and when the MPU-401 notification lets it go (3:4FD8).  Those
; two writes come to es_fm_dac, which leaves the FM on the DAC while the
; recording runs and keeps their bit 0 for when it stops.  Everything else
; ESS's code does there still happens.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; mixer_write(dev, reg, value) in place of ESS's at 5:206D and 3:4FD8,
; far pascal: the value kept on FM while device 1 records, and ESS's bit 0
; kept for when it stops
es_fm_dac:
        push    bp
        mov     bp,sp
        mov     bx,[bp+0Ah]             ; the device
        test    byte [bx+FM_STATE],FMF_ROUTED
        jz      .write
        and     byte [bx+FM_SAVED],~FMS_I2S & 0FFh
        test    byte [bp+6],1
        jz      .fm
        or      byte [bx+FM_SAVED],FMS_I2S
.fm:    and     byte [bp+6],0FEh        ; the music DAC stays FM's
        or      byte [bp+6],10h         ; and its samples on Audio 1's DMA
.write: pop     bp
        jmp     mixer_write

; fm_route(dev, how), far pascal: how 1 puts the FM's samples on Audio 1's
; DMA and the music DAC on FM, and keeps what 7Fh held; 0 puts that back;
; 2 does 1 again after the mixer reset of an APM resume
fm_route:
        push    bp
        mov     bp,sp
        push    si
        mov     si,[bp+8]               ; the device
        cmp     byte [bp+6],1
        je      .on
        test    byte [si+FM_STATE],FMF_ROUTED
        jz      .done
        cmp     byte [bp+6],2
        je      .again
        ; off: bit 4 as it was, and the music DAC FM's while FM has it,
        ; else bit 0 as ESS's code last left it
        and     byte [si+FM_STATE],~FMF_ROUTED & 0FFh
        call    .read
        and     al,0EEh
        test    byte [si+FM_SAVED],FMS_DREC
        jz      .dac
        or      al,10h
.dac:   cmp     byte [si+DEV_FM_DAC],0
        jne     .write
        test    byte [si+FM_SAVED],FMS_I2S
        jz      .write
        or      al,1
        jmp     short .write
.on:    test    byte [si+FM_STATE],FMF_ROUTED
        jnz     .done
        and     byte [si+FM_SAVED],FMS_DCDRIFT
        call    .read
        test    al,1
        jz      .bit4
        or      byte [si+FM_SAVED],FMS_I2S
.bit4:  test    al,10h
        jz      .route
        or      byte [si+FM_SAVED],FMS_DREC
.route: or      byte [si+FM_STATE],FMF_ROUTED
.again: call    .read
        and     al,0FEh
        or      al,10h
.write: push    si
        mov     ah,0
        mov     cx,MX_DREC
        push    cx
        push    ax
        push    cs
        call    mixer_write
.done:  pop     si
        pop     bp
        retf    4
.read:  push    si
        mov     ax,MX_DREC
        push    ax
        push    cs
        call    mixer_read
        ret
