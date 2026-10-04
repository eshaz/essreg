; The FM recording device of ES1869.DRV, assembled when ES1869_FIX=1 and
; appended to segment 6 (the wave devices).
;
; Notes:
;
; es_wid_message is ES1869.DRV's widMessage export, in front of ESS's
; (6:0FF2).  With FMRecordDevice=1, wave-in device 1, "ESS AudioDrive FM
; Digital", records the FM synthesizer's samples as the chip makes them.
; Mixer 7Fh bit 4 sends the music DAC's samples to Audio 1's DMA in place
; of the ADC's, at the music DAC's rate of 49716 Hz whatever rate Audio 1
; is programmed for (DS p.65), as esfmrec records them.
;
; So device 1 takes 16-bit stereo at 49716 Hz only, and opens ESS's
; device 0 at 48000 Hz under it, with copies of the program's
; WAVEOPENDESC and format in the device structure.  Every other message
; of device 1 goes to ESS's code as device 0's: the two devices share
; Audio 1, one at a time, and ESS's code refuses the second open.
;
; While device 1 records, fm_route (fmdac.asm) keeps 7Fh on the FM.  ESS's
; DC drift removal (DEV_FLAGS bit 0) is off from the open to the close,
; since it would take the first block's average off every sample.
;
; A DOS program that took the DSP with its first Sound Blaster access
; would keep the recording out until it ends.  So the open first asks the
; extended ES1869.VXD to take the DSP from it (040D, RecordTakesDSP): the
; program goes on with a virtual Sound Blaster, and its FM records.  ESS's
; VxD answers with the carry set, and ESS's open then finds the DSP busy,
; as before.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; widMessage(uDevId, msg, dwUser, dwParam1, dwParam2), far pascal: the
; export; [bp+6] dwParam2, [bp+0Ah] dwParam1, [bp+0Eh] dwUser, [bp+12h]
; msg, [bp+14h] uDevId
es_wid_message:
        push    bp
        mov     bp,sp
        push    ds
        push    si
        push    di
        dgroup  ax
        mov     ds,ax
        test    byte [es_opts],OPT_FM_RECORD
        jz      .ess
        mov     ax,[bp+12h]
        cmp     ax,WIDM_GETNUMDEVS
        je      .numdevs
        cmp     word [bp+14h],1
        jne     .ess
        mov     word [bp+14h],0         ; ESS's device 0 from here on
        cmp     ax,WIDM_GETDEVCAPS
        je      .caps
        cmp     ax,WIDM_OPEN
        je      .open
        ; an open instance: ESS's, and the routing around it
        mov     si,[bp+0Eh]             ; ESS's instance, its device first
        mov     si,[si]
        cmp     ax,WIDM_START
        je      .start
        call    fm_ess
        mov     cx,[bp+12h]
        cmp     cx,WIDM_STOP
        je      .stop
        cmp     cx,WIDM_RESET
        je      .stop
        cmp     cx,WIDM_CLOSE
        jne     .done
        or      ax,ax
        jnz     .done
        mov     cl,0                    ; closed: the routing and DC drift
        call    fm_route_call           ; removal as they were
        test    byte [si+FM_SAVED],FMS_DCDRIFT
        jz      .closed
        or      byte [si+DEV_FLAGS],1
.closed:
        mov     byte [si+FM_STATE],0
        xor     ax,ax
        jmp     .ret
.start: mov     cl,1                    ; the FM's samples from the first
        call    fm_route_call
        call    fm_ess
        or      ax,ax
        jz      .done
.stop:  push    ax
        push    dx
        mov     cl,0
        call    fm_route_call
        pop     dx
        pop     ax
        jmp     .done
.ess:   pop     di
        pop     si
        pop     ds
        pop     bp
        jmp     L6_0FF2

; ESS's answer, then device 1 for the devnode
.numdevs:
        call    fm_ess
        cmp     ax,1
        jne     .done
        push    word [bp+0Ch]           ; the devnode (dwParam1)
        push    word [bp+0Ah]
        fcall   3, L3_4EAE
        or      ax,ax
        mov     ax,1
        jz      .ret
        inc     ax
        jmp     .ret

; ESS's caps of device 0, with device 1's name and format
.caps:  push    word [bp+8]             ; the devnode (dwParam2)
        push    word [bp+6]
        fcall   3, L3_4EAE
        mov     si,ax
        mov     ax,MMSYSERR_BADDEVICEID
        or      si,si
        jz      .ret
        call    fm_ess
        or      ax,ax
        jnz     .done
        les     bx,[bp+0Ah]             ; the MDEVICECAPSEX
        mov     ax,fm_name
        call    caps_rename
        les     bx,[bp+0Ah]
        mov     cx,[es:bx]              ; its size, 0FFFFh if larger
        cmp     word [es:bx+2],0
        je      .size
        mov     cx,0FFFFh
.size:  les     bx,[es:bx+4]
        cmp     cx,CAPS_FORMATS + 4
        jb      .ok
        xor     ax,ax                   ; no standard format: 49716 Hz only
        mov     [es:bx+CAPS_FORMATS],ax
        mov     [es:bx+CAPS_FORMATS+2],ax
        cmp     cx,CAPS_CHANNELS + 2
        jb      .ok
        mov     word [es:bx+CAPS_CHANNELS],2
.ok:    xor     ax,ax
        jmp     .ret

; 16-bit stereo at the music DAC's rate, then ESS's open
.open:  les     bx,[bp+0Ah]             ; the WAVEOPENDESC
        push    word [es:bx+WOD_DEVNODE+2]
        push    word [es:bx+WOD_DEVNODE]
        fcall   3, L3_4EAE
        mov     si,ax
        mov     ax,MMSYSERR_BADDEVICEID
        or      si,si
        jz      .ret
        mov     ax,MMSYSERR_NOTENABLED
        cmp     word [si+DEV_ENABLED],0
        je      .ret
        les     bx,[bp+0Ah]
        les     bx,[es:bx+WOD_FORMAT]
        mov     ax,WAVERR_BADFORMAT
        cmp     word [es:bx+WF_TAG],WAVE_FORMAT_PCM
        jne     .ret
        cmp     word [es:bx+WF_CHANNELS],2
        jne     .ret
        cmp     word [es:bx+WF_RATE],FM_RATE
        jne     .ret
        cmp     word [es:bx+WF_RATE+2],0
        jne     .ret
        cmp     word [es:bx+WF_ALIGN],4
        jne     .ret
        cmp     word [es:bx+WF_BITS],16
        jne     .ret
        xor     ax,ax
        test    byte [bp+6],WAVE_FORMAT_QUERY
        jnz     .ret
        mov     ax,MMSYSERR_ALLOCATED
        test    byte [si+FM_STATE],FMF_OPEN
        jnz     .ret
        call    fm_take_dsp
        call    fm_open
.ret:   xor     dx,dx
.done:  pop     di
        pop     si
        pop     ds
        pop     bp
        retf    10h

; ESS's widMessage with the arguments of the frame: DX:AX
fm_ess:
        push    word [bp+14h]
        push    word [bp+12h]
        push    word [bp+10h]
        push    word [bp+0Eh]
        push    word [bp+0Ch]
        push    word [bp+0Ah]
        push    word [bp+8]
        push    word [bp+6]
        push    cs
        call    L6_0FF2
        ret

; VxD function 040D: a DOS program gives up the DSP for the recording;
; its answer doesn't matter, ESS's open finds out; SI = dev
fm_take_dsp:
        push    si
        mov     ecx,[si+DEV_DEVNODE]
        mov     dx,040Dh
        call    far [0x10]              ; ES1869.VXD's entry point
        pop     si
        ret

; fm_route(dev, CL); SI = dev
fm_route_call:
        push    si
        xor     ch,ch
        push    cx
        fcall   1, fm_route
        ret

; ESS's open of device 0 at 48000 Hz for device 1, with copies of the
; program's WAVEOPENDESC and format, and DC drift removal off; SI = dev
fm_open:
        mov     bx,si
        push    ds
        pop     es
        lea     di,[bx+FM_DESC]
        push    ds
        lds     si,[bp+0Ah]             ; the program's WAVEOPENDESC
        mov     cx,WOD_SIZE / 2
        cld
        rep     movsw
        lds     si,[si - WOD_SIZE + WOD_FORMAT]
        lea     di,[bx+FM_FORMAT]
        mov     cx,WF_SIZE / 2
        rep     movsw
        pop     ds
        mov     si,bx
        mov     word [si+FM_FORMAT+WF_RATE],FM_OPEN_RATE
        mov     word [si+FM_FORMAT+WF_RATE+2],0
        mov     word [si+FM_FORMAT+WF_AVG],(FM_OPEN_RATE * 4) & 0FFFFh
        mov     word [si+FM_FORMAT+WF_AVG+2],(FM_OPEN_RATE * 4) >> 16
        lea     ax,[si+FM_FORMAT]
        mov     [si+FM_DESC+WOD_FORMAT],ax
        mov     [si+FM_DESC+WOD_FORMAT+2],ds
        mov     al,[si+DEV_FLAGS]       ; DC drift removal off
        and     al,FMS_DCDRIFT
        mov     [si+FM_SAVED],al
        and     byte [si+DEV_FLAGS],0FEh
        xor     ax,ax                   ; device 0
        push    ax
        mov     ax,WIDM_OPEN
        push    ax
        push    word [bp+10h]           ; dwUser: where the instance goes
        push    word [bp+0Eh]
        push    ds                      ; the copies
        lea     ax,[si+FM_DESC]
        push    ax
        push    word [bp+8]             ; the flags
        push    word [bp+6]
        push    cs
        call    L6_0FF2
        or      ax,ax
        jnz     .fail
        mov     byte [si+FM_STATE],FMF_OPEN
        ret
.fail:  test    byte [si+FM_SAVED],FMS_DCDRIFT
        jz      .ret
        or      byte [si+DEV_FLAGS],1
.ret:   ret
