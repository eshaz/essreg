; The wave-out messages of the Audio 1 player, assembled when ES1869_FIX=1
; and appended to segment 6 (the wave devices).
;
; Notes:
;
; es_wod_message is ES1869.DRV's wodMessage export, in front of ESS's
; (6:18E4):
; - Audio1Device=1: a second device, "ESS AudioDrive Audio 1", played
;   through the Audio 1 DAC while Audio 2 plays device 0
; - SharedWaveOut=1: a device 0 open that ESS's code refuses as busy
;   (MMSYSERR_ALLOCATED) plays through the Audio 1 DAC instead
; - the player's instances are told from ESS's by their dwUser: the
;   player's block in the low word, which holds its own address, and
;   A1_MAGIC in the high word
; - everything else goes to ESS's code, and with both keys 0 all of it
;
; Audio 1 records or plays, one at a time: the player takes it as its user
; 1, as wave-in takes it as user 2 (wid_acquire), with the same busy
; checks, and a voice recording ESS's driver holds for a playback is held
; for the player too.  Opened, the player sets the CODEC up for DAC
; transfers and turns its DAC on in the mixer (D1h); the first header
; starts the DMA; the close turns the DAC off (D3h).  All DSP commands are
; sent here, at task time.
;
; DualPlayback=1: device 1 also takes 4 channels.  Channels 1-2 play on
; the Audio 1 DAC and 3-4 on the Audio 2 DAC, which the player takes as
; its user 2 and slaves to Audio 1's clock (71h bit 1 clear), so both play
; each frame at the same tick.  Both DMAs run on rings of the same size
; and start together; Audio 1's interrupt serves both.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; far calls and selectors of other segments: each site has its own
; relocation record (a1wave_relocs), in segment 6's table
%assign A1W_NREL 0
%macro fcall 2                          ; segment, target
%assign A1W_NREL A1W_NREL + 1
        callf   %2, ..@A1W_R%[A1W_NREL], 0xFFFF
%xdefine A1W_RSEG%[A1W_NREL] %1
%endmacro
%macro dgroup 1                         ; register: DGROUP's selector
%assign A1W_NREL A1W_NREL + 1
        movsel  %1, ..@A1W_R%[A1W_NREL], 0xFFFF
%xdefine A1W_RSEG%[A1W_NREL] 7
%endmacro
%macro a1wave_relocs 0
%assign i 1
%rep A1W_NREL
        reloc 2, 0, ..@A1W_R%[i], A1W_RSEG%[i], 0x0000
%assign i i + 1
%endrep
%endmacro

; wodMessage(uDevId, msg, dwUser, dwParam1, dwParam2), far pascal: the
; export; [bp+6] dwParam2, [bp+0Ah] dwParam1, [bp+0Eh] dwUser, [bp+12h]
; msg, [bp+14h] uDevId
es_wod_message:
        push    bp
        mov     bp,sp
        push    ds
        push    si
        push    di
        dgroup  ax
        mov     ds,ax
        test    byte [es_opts],OPT_A1_DEVICE | OPT_A1_SHARED
        jz      .ess
        mov     ax,[bp+12h]
        cmp     ax,WODM_GETNUMDEVS
        je      .numdevs
        cmp     ax,WODM_GETDEVCAPS
        je      .caps
        cmp     ax,WODM_OPEN
        je      .open
        cmp     ax,WODM_GETVOLUME
        je      .volume
        cmp     ax,WODM_SETVOLUME
        je      .volume
        cmp     word [bp+10h],A1_MAGIC  ; an instance of the player?
        jne     .ess
        mov     si,[bp+0Eh]
        cmp     [si],si
        jne     .ess                    ; ESS's, its serial number A1A1h
        sub     si,A1_BASE
        jmp     a1_message
.ess:   pop     di
        pop     si
        pop     ds
        pop     bp
        jmp     L6_18E4

; ESS's answer, then device 1 for a devnode that can play it
.numdevs:
        call    a1_ess
        cmp     ax,1
        jne     .done
        test    byte [es_opts],OPT_A1_DEVICE
        jz      .done
        push    word [bp+0Ch]           ; the devnode (dwParam1)
        push    word [bp+0Ah]
        fcall   3, L3_4EAE
        mov     si,ax
        mov     ax,1
        or      si,si
        jz      .ret
        call    a1_capable
        jc      .ret
        inc     ax
        jmp     .ret

.caps:  call    a1_device1              ; the devnode is dwParam2
        jc      .ess
        jnz     .ret
        les     bx,[bp+0Ah]             ; the MDEVICECAPSEX
        call    a1_caps
        xor     ax,ax
        jmp     .ret

.volume:
        call    a1_device1
        jc      .ess
        jnz     .ret
        call    a1_volume
        jmp     .ret

.open:  cmp     word [bp+14h],0
        jne     .open1
        test    byte [es_opts],OPT_A1_SHARED
        jz      .ess
        call    a1_ess                  ; ESS's first
        cmp     ax,MMSYSERR_ALLOCATED
        jne     .done
        test    byte [bp+6],WAVE_FORMAT_QUERY
        jnz     .done
        les     bx,[bp+0Ah]             ; the WAVEOPENDESC
        push    word [es:bx+WOD_DEVNODE+2]
        push    word [es:bx+WOD_DEVNODE]
        fcall   3, L3_4EAE
        mov     si,ax
        mov     ax,MMSYSERR_ALLOCATED
        or      si,si
        jz      .ret
        mov     di,A1F_SHARED
        call    a1_open
        or      ax,ax
        jz      .ret
        mov     ax,MMSYSERR_ALLOCATED   ; ESS's answer stands
        jmp     .ret
.open1: cmp     word [bp+14h],1
        jne     .ess
        test    byte [es_opts],OPT_A1_DEVICE
        jz      .ess
        les     bx,[bp+0Ah]
        push    word [es:bx+WOD_DEVNODE+2]
        push    word [es:bx+WOD_DEVNODE]
        fcall   3, L3_4EAE
        mov     si,ax
        mov     ax,MMSYSERR_BADDEVICEID
        or      si,si
        jz      .ret
        xor     di,di
        call    a1_open
.ret:   xor     dx,dx
.done:  pop     di
        pop     si
        pop     ds
        pop     bp
        retf    10h

; ESS's wodMessage with the same arguments: DX:AX
a1_ess:
        push    word [bp+14h]
        push    word [bp+12h]
        push    word [bp+10h]
        push    word [bp+0Eh]
        push    word [bp+0Ch]
        push    word [bp+0Ah]
        push    word [bp+8]
        push    word [bp+6]
        push    cs
        call    L6_18E4
        ret

; a device-1 message that names its devnode in dwParam2: CF set if it's
; ESS's to answer; else SI = dev and ZF set, or ZF clear and AX = error
a1_device1:
        cmp     word [bp+14h],1
        jne     .ess
        test    byte [es_opts],OPT_A1_DEVICE
        jz      .ess
        push    word [bp+8]
        push    word [bp+6]
        fcall   3, L3_4EAE
        mov     si,ax
        mov     ax,MMSYSERR_BADDEVICEID
        or      si,si
        jz      .err
        call    a1_capable
        jc      .err
        xor     ax,ax                   ; ZF set, CF clear
        ret
.err:   or      ax,ax                   ; ZF and CF clear
        ret
.ess:   stc
        ret

; CF clear if the device can play through Audio 1: enabled, an 8-bit DMA
; channel for it, DMA transfers, and a buffer of two blocks; SI = dev
a1_capable:
        cmp     word [si+DEV_ENABLED],0
        je      .no
        cmp     byte [si+DEV_DMA1],3
        ja      .no
        test    byte [si+DEV_FLAGS+1],NODMA_HI
        jnz     .no
        cmp     word [si+DEV_BUFSIZE],1000h
        jb      .no
        clc
        ret
.no:    stc
        ret

; CF clear if Audio 2 can play the second half of dual playback: its
; buffer holds two blocks; SI = dev
a1_capable2:
        cmp     word [si+DEV_A2_BUFSIZE],1000h
        jb      .no
        clc
        ret
.no:    stc
        ret

; a message to an instance of the player: SI = dev
a1_message:
        mov     ax,MMSYSERR_NOTENABLED
        cmp     word [si+DEV_ENABLED],0
        je      .ret
        mov     ax,[bp+12h]
        mov     bx,a1_close
        cmp     ax,WODM_CLOSE
        je      .call
        mov     bx,a1_write
        cmp     ax,WODM_WRITE
        je      .call
        mov     bx,a1_pause
        cmp     ax,WODM_PAUSE
        je      .call
        mov     bx,a1_restart
        cmp     ax,WODM_RESTART
        je      .call
        mov     bx,a1_reset
        cmp     ax,WODM_RESET
        je      .call
        mov     bx,a1_getpos
        cmp     ax,WODM_GETPOS
        je      .call
        mov     bx,a1_break
        cmp     ax,WODM_BREAKLOOP
        je      .call
        mov     ax,MMSYSERR_NOTSUPPORTED
        jmp     .ret
.call:  call    bx
.ret:   xor     dx,dx
        pop     di
        pop     si
        pop     ds
        pop     bp
        retf    10h

; --- the messages: SI = dev, BP = es_wod_message's frame, AX = result ----

; WODM_OPEN: the format as ESS's device 0 takes it, then Audio 1; DI =
; A1F_SHARED for a device 0 open
a1_open:
        les     bx,[bp+0Ah]
        les     bx,[es:bx+WOD_FORMAT]
        mov     ax,WAVERR_BADFORMAT
        cmp     word [es:bx],WAVE_FORMAT_PCM
        jne     .ret
        mov     cx,[es:bx+2]            ; channels
        cmp     cx,4
        jne     .ch12
        or      di,di
        jnz     .ret                    ; 4 on device 1 only
        test    byte [es_opts],OPT_DUAL
        jz      .ret
        mov     cx,FMT_DUAL | FMT_STEREO
        jmp     .ch
.ch12:  dec     cx
        cmp     cx,1
        ja      .ret
        shl     cl,1                    ; FMT_STEREO
.ch:        mov     dx,[es:bx+0Eh]          ; bits
        cmp     dx,8
        je      .bits
        cmp     dx,16
        jne     .ret
        or      cl,FMT_16BIT
.bits:  cmp     word [es:bx+6],0
        jne     .ret
        mov     dx,[es:bx+4]            ; samples per second
        cmp     dx,4000
        jb      .ret
        cmp     dx,49000
        ja      .ret
        xor     ax,ax
        test    byte [bp+6],WAVE_FORMAT_QUERY
        jnz     .ret
        mov     ax,MMSYSERR_NOTENABLED
        cmp     word [si+DEV_ENABLED],0
        je      .ret
        mov     ax,MMSYSERR_BADDEVICEID
        push    cx
        call    a1_capable
        pop     cx
        jc      .ret
        test    cl,FMT_DUAL
        jz      .dead
        push    cx
        call    a1_capable2
        pop     cx
        jc      .ret
.dead:        mov     ax,MMSYSERR_ALLOCATED
        cmp     word [si+DEV_DEAD],0
        jne     .ret
        test    byte [si+A1_STATE],A1F_OPEN
        jnz     .ret
        mov     ax,MMSYSERR_ALLOCATED
        test    cl,FMT_DUAL
        jz      .a1
        test    byte [si+DEV_BUSY],1
        jnz     .ret
        cmp     byte [si+DEV_A2_USER],0 ; Audio 2 free, as wod_acquire finds
        jne     .ret                    ; it (4:0054)
        test    byte [si+DEV_WOD_FLAGS],1
        jnz     .ret
.a1:    push    cx
        push    dx
        call    a1_acquire
        pop     dx
        pop     cx
        mov     ax,MMSYSERR_ALLOCATED
        jc      .ret
        test    cl,FMT_DUAL
        jz      .init
        mov     byte [si+DEV_A2_USER],2
.init:        ; the block from zero, then what the program asked for
        push    cx
        push    di
        push    ds
        pop     es
        lea     di,[si+A1_BASE]
        mov     cx,A1_SIZEOF
        xor     al,al
        cld
        rep     stosb
        pop     di
        pop     cx
        lea     ax,[si+A1_BASE]
        mov     [si+A1_SELF],ax
        mov     word [si+A1_ID],A1_MAGIC
        mov     ax,di
        or      al,A1F_OPEN
        mov     [si+A1_STATE],al
        mov     [si+A1_FMT],cl
        mov     [si+A1_RATE],dx
        mov     ax,1                    ; bytes per frame
        test    cl,FMT_16BIT
        jz      .b8
        shl     ax,1
.b8:    test    cl,FMT_STEREO
        jz      .mono
        shl     ax,1
.mono:  mov     [si+A1_ALIGN],ax
        mov     byte [si+A1_SILENCE],80h
        test    cl,FMT_16BIT
        jz      .s8
        mov     byte [si+A1_SILENCE],0
.s8:    les     bx,[bp+0Ah]
        mov     ax,[es:bx+WOD_HWAVE]
        mov     [si+A1_HWAVE],ax
        mov     ax,[es:bx+WOD_CALLBACK]
        mov     [si+A1_CB],ax
        mov     ax,[es:bx+WOD_CALLBACK+2]
        mov     [si+A1_CB+2],ax
        mov     ax,[es:bx+WOD_INSTANCE]
        mov     [si+A1_CBINST],ax
        mov     ax,[es:bx+WOD_INSTANCE+2]
        mov     [si+A1_CBINST+2],ax
        mov     ax,[bp+6]
        mov     [si+A1_OFLAGS],ax
        mov     ax,[bp+8]
        mov     [si+A1_CBTYPE],ax
        call    a1_setup
        ; *lpdwUser: the block and A1_MAGIC
        les     bx,[bp+0Eh]
        mov     ax,[si+A1_SELF]
        mov     [es:bx],ax
        mov     word [es:bx+2],A1_MAGIC
        mov     ax,WOM_OPEN
        call    a1_notify
        xor     ax,ax
.ret:   ret

; WODM_CLOSE: once every header is back
a1_close:
        mov     ax,[si+A1_HEAD]
        or      ax,[si+A1_HEAD+2]
        mov     ax,WAVERR_STILLPLAYING
        jnz     .ret
        call    a1_stop
        mov     al,DSP_A1_OFF
        call    a1_dsp
        mov     al,MX_A2_MODE           ; Audio 1's filter as ESS leaves it,
        call    a1_mixer_read           ; and Audio 2 at its own rate again
        and     al,~A1_BYPASS & 0FFh
        test    byte [si+A1_FMT],FMT_DUAL
        jz      .mode
        or      al,A2_ASYNC
        mov     byte [si+DEV_A2_USER],0
.mode:  mov     ah,al
        mov     al,MX_A2_MODE
        call    a1_mixer_write
        call    a1_release
        mov     ax,WOM_CLOSE
        call    a1_notify
        xor     ax,ax
        mov     [si+A1_SELF],ax
        mov     [si+A1_ID],ax
        mov     [si+A1_STATE],al
.ret:   ret

; WODM_WRITE: ESS's checks and flags (6:199B), then the queue; the first
; header starts the DMA unless paused, and the next go into the ring now
a1_write:
        les     bx,[bp+0Ah]
        mov     al,[es:bx+WH_FLAGS]
        and     ax,1Fh
        mov     [es:bx+WH_FLAGS],ax
        mov     word [es:bx+WH_FLAGS+2],0
        test    al,WHDR_PREPARED
        jnz     .ok
        mov     ax,WAVERR_UNPREPARED
        ret
.ok:    or      al,WHDR_INQUEUE
        and     al,~WHDR_DONE & 0FFh
        mov     [es:bx+WH_FLAGS],al
        xor     ax,ax
        mov     [es:bx+WH_NEXT],ax
        mov     [es:bx+WH_NEXT+2],ax
        mov     [es:bx+WH_END],ax
        mov     ax,[si+A1_SELF]
        mov     [es:bx+WH_OWNER],ax
        push    si
        push    es
        push    bx
        fcall   1, a1_queue
        test    byte [si+A1_STATE],A1F_RUN
        jnz     .more
        test    byte [si+A1_STATE],A1F_PAUSED
        jnz     .done
        call    a1_start
        jmp     .done
.more:  push    si
        fcall   1, a1_topup
.done:  xor     ax,ax
        ret

; WODM_PAUSE: the DMA stops where it is.  In dual playback both DACs have
; to start again together, so it stops for both and a restart starts the
; ring again from what wasn't played (a1_ready)
a1_pause:
        test    byte [si+A1_STATE],A1F_PAUSED
        jnz     .done
        or      byte [si+A1_STATE],A1F_PAUSED
        test    byte [si+A1_STATE],A1F_RUN
        jz      .done
        call    a1_stop
        push    si
        fcall   1, a1_freeze
.done:  xor     ax,ax
        ret

; WODM_RESTART: on from where it stopped, or the first start
a1_restart:
        test    byte [si+A1_STATE],A1F_PAUSED
        jz      .done
        and     byte [si+A1_STATE],~A1F_PAUSED & 0FFh
        mov     ax,[si+A1_HEAD]
        or      ax,[si+A1_HEAD+2]
        jz      .done
        call    a1_start
.done:  xor     ax,ax
        ret

; WODM_RESET: stopped, every header back, the position at zero and not
; paused, as ESS's reset (6:1A32)
a1_reset:
        call    a1_stop
        push    si
        fcall   1, a1_flush
        and     byte [si+A1_STATE],~A1F_PAUSED & 0FFh
        xor     ax,ax
        ret

; WODM_GETPOS: TIME_BYTES, or TIME_SAMPLES for any other, as ESS's
a1_getpos:
        mov     ax,MMSYSERR_INVALPARAM
        cmp     word [bp+8],0
        jne     .size
        cmp     word [bp+6],8
        jb      .ret
.size:  push    si
        fcall   1, a1_played
        les     bx,[bp+0Ah]
        cmp     word [es:bx],TIME_BYTES
        jne     .samples
        test    byte [si+A1_FMT],FMT_DUAL
        jz      .put
        shl     ax,1                    ; the program's bytes: both halves
        rcl     dx,1
        jmp     .put
.samples:
        mov     word [es:bx],TIME_SAMPLES
        mov     cx,[si+A1_ALIGN]
.frame: shr     cx,1
        jz      .put
        shr     dx,1
        rcr     ax,1
        jmp     .frame
.put:   mov     [es:bx+2],ax
        mov     [es:bx+4],dx
        xor     ax,ax
.ret:   ret

; WODM_BREAKLOOP
a1_break:
        push    si
        fcall   1, a1_breakloop
        xor     ax,ax
        ret

; WODM_GETDEVCAPS of device 1: ESS's device 0 caps (6:13FE) with the
; Audio 1 name; ES:BX = the MDEVICECAPSEX
a1_caps:
        push    bp
        mov     bp,sp
        sub     sp,30h                  ; WAVEOUTCAPS
        push    es
        push    bx
        push    ss
        pop     es
        lea     di,[bp-30h]
        cld
        mov     ax,2Eh                  ; ESS
        stosw
        mov     ax,28h
        stosw
        mov     ax,0404h
        stosw
        push    si
        mov     si,a1_name
.name:  lodsb
        or      al,al
        jz      .base
        stosb
        jmp     .name
.base:  pop     si
        mov     dx,[si+DEV_BASE]        ; Audio_Base, as ESS's %X
        mov     cx,4
        xor     bx,bx                   ; digits so far
.hex:   rol     dx,4
        mov     al,dl
        and     al,0Fh
        jnz     .digit
        or      bx,bx
        jnz     .digit
        cmp     cx,1
        jne     .next                   ; no leading zero
.digit: add     al,'0'
        cmp     al,'9'
        jbe     .put
        add     al,'A' - '9' - 1
.put:   stosb
        inc     bx
.next:  loop    .hex
        mov     ax,')'                  ; and the NUL
        stosw
        lea     cx,[bp-30h+6+32]
        sub     cx,di
        xor     al,al
        rep     stosb                   ; the rest of szPname
        mov     ax,0FFFh                ; every standard format
        stosw
        xor     ax,ax
        stosw
        mov     ax,2
        stosw
        mov     ax,2Ch                  ; volume, left and right, sample accurate
        stosw
        xor     ax,ax
        stosw
        pop     bx
        pop     es
        push    word [es:bx+6]          ; pCaps
        push    word [es:bx+4]
        lea     ax,[bp-30h]
        push    ss
        push    ax
        mov     ax,[es:bx]              ; its size
        cmp     word [es:bx+2],0
        jne     .max
        cmp     ax,30h
        jbe     .n
.max:   mov     ax,30h
.n:     push    ax
        fcall   1, L1_1AD3
        mov     sp,bp
        pop     bp
        ret

; WODM_GETVOLUME, WODM_SETVOLUME of device 1: mixer 14h, the Audio 1
; play volume, 4 bits a side
a1_volume:
        cmp     word [bp+12h],WODM_SETVOLUME
        je      .set
        mov     al,MX_A1_VOLUME
        call    a1_mixer_read
        mov     cl,al
        and     ax,0Fh
        mov     dx,1111h
        mul     dx
        mov     dx,ax                   ; right
        mov     al,cl
        shr     al,4
        and     ax,0Fh
        mov     cx,1111h
        push    dx
        mul     cx                      ; left
        pop     dx
        les     bx,[bp+0Ah]
        mov     [es:bx],ax
        mov     [es:bx+2],dx
        xor     ax,ax
        ret
.set:   mov     ah,[bp+0Bh]             ; left: bits 15:12 of the low word
        and     ah,0F0h
        mov     al,[bp+0Dh]             ; right: of the high word
        shr     al,4
        or      ah,al
        mov     al,MX_A1_VOLUME
        call    a1_mixer_write
        xor     ax,ax
        ret

; WOM_OPEN, WOM_CLOSE in AX, through ESS's callback (1:0010); SI = dev
a1_notify:
        push    word [si+A1_SELF]
        push    ax
        xor     ax,ax
        push    ax
        push    ax
        fcall   1, L1_0010
        ret

; --- the hardware, at task time: SI = dev -----------------------------

; Audio 1 taken as wid_acquire takes it (4:00E0), after a voice recording
; is held as for ESS's wave-out (6:1595): CF set if busy
a1_acquire:
        test    byte [si+DEV_BUSY],1
        jnz     .busy
        xor     cx,cx                   ; no voice recording held here
        mov     al,[si+DEV_A1_USER]
        and     al,3
        cmp     al,2
        jne     .free
        mov     ax,[si+DEV_VOICE]
        or      ax,[si+DEV_VOICE+2]
        jz      .free
        push    si
        call    L6_0470                 ; the voice recording held
        or      ax,ax
        jz      .busy
        mov     cx,1
.free:  cmp     byte [si+DEV_A1_USER],0
        jne     .back
        test    byte [si+DEV_WID_FLAGS],1
        jnz     .back
        push    cx
        push    si
        push    word [si+DEV_BASE]
        mov     ax,1
        push    ax
        fcall   4, vxd_acquire
        pop     cx
        or      ax,ax
        jnz     .back
        mov     byte [si+DEV_A1_USER],1
        clc
        ret
.back:  jcxz    .busy
        call    a1_voice                ; not held for nothing
.busy:  stc
        ret

; Audio 1 given back
a1_release:
        mov     byte [si+DEV_A1_USER],0
        push    si
        push    word [si+DEV_BASE]
        mov     ax,1
        push    ax
        fcall   4, vxd_release
; a voice recording held goes on, unless ESS's wave-out still plays: its
; close does it then (6:1831)
a1_voice:
        cmp     word [si+DEV_VOICE_SUSP],0
        je      .done
        cmp     byte [si+DEV_A2_USER],0
        jne     .done
        push    si
        call    L6_0566
.done:  ret

; the CODEC set up for DAC transfers, as the data sheet (DS p.48) without
; its reset: a reset would stop Audio 2 too
a1_setup:
        mov     al,DSP_EXTENDED
        call    a1_dsp
        ; the rate and filter clock, with ESS's playback filter table
        push    si
        push    word [si+A1_RATE]
        mov     al,[si+A1_FMT]
        and     ax,FMT_STEREO
        shr     ax,1
        inc     ax
        push    ax                      ; channels
        xor     ax,ax
        push    ax                      ; playback
        push    ax
        mov     al,[si+A1_FMT]
        push    ax
        push    cs
        call    L6_1FF4
        mov     ax,[si+DEV_BLOCK]
        cmp     ax,3FFCh                ; the ring stays under 32 KB
        jbe     .block
        mov     ax,3FFCh
.block: test    byte [si+A1_FMT],FMT_DUAL
        jz      .blk
        mov     cx,[si+DEV_A2_BUFSIZE]  ; and in Audio 2's buffer
        shr     cx,1
        and     cl,0FCh
        cmp     ax,cx
        jbe     .blk
        mov     ax,cx
.blk:   mov     [si+A1_BLOCK],ax
        shl     ax,1
        mov     [si+A1_RING],ax
        mov     ax,[si+A1_BLOCK]
        shr     ax,3                    ; 1/128 s
        mov     cx,[si+A1_ALIGN]
        dec     cx
        not     cx
        and     ax,cx
        jnz     .guard
        mov     ax,[si+A1_ALIGN]
.guard: mov     [si+A1_GUARD],ax
        ; A8h bits 1:0: 10 mono, 01 stereo
        mov     al,CR_ANALOG
        call    a1_reg_read
        and     al,0FCh
        or      al,2
        test    byte [si+A1_FMT],FMT_STEREO
        jz      .an
        xor     al,3
.an:    mov     ah,al
        mov     al,CR_ANALOG
        call    a1_reg_write
        ; demand transfers as ESS records
        test    byte [si+DEV_FLAGS+1],DEMAND_HI
        jz      .fmt
        mov     ax,(2 << 8) | CR_XFER
        call    a1_reg_write
        ; B6h and B7h as Linux es18xx: unsigned 8-bit, signed 16-bit
.fmt:   mov     ax,(80h << 8) | CR_DAC_INIT
        mov     cx,(51h << 8) | CR_FIFO
        mov     dx,(0D0h << 8) | CR_FIFO        ; 90h, mono
        test    byte [si+A1_FMT],FMT_16BIT
        jz      .fifo
        mov     ax,(00h << 8) | CR_DAC_INIT
        mov     cx,(71h << 8) | CR_FIFO
        mov     dx,(0F4h << 8) | CR_FIFO        ; 90h, signed, 16-bit, mono
.fifo:  test    byte [si+A1_FMT],FMT_STEREO
        jz      .fifo2
        xor     dh,48h                  ; stereo
.fifo2: push    dx
        push    cx
        call    a1_reg_write
        pop     ax
        call    a1_reg_write
        pop     ax
        call    a1_reg_write
        ; its interrupt and DMA request on (bits 6 and 4)
        mov     al,CR_IRQ
        call    a1_reg_or50
        mov     al,CR_DRQ
        call    a1_reg_or50
        ; the filter: bypassed but with Audio1Filter=1 (71h bit 2)
        mov     al,MX_A2_MODE
        call    a1_mixer_read
        or      al,A1_BYPASS
        test    byte [es_opts],OPT_A1_FILTER
        jz      .byp
        and     al,~A1_BYPASS & 0FFh
.byp:   mov     ah,al
        mov     al,MX_A2_MODE
        call    a1_mixer_write
        ; the DAC into the mixer
        mov     al,DSP_A1_ON
        call    a1_dsp
        test    byte [si+A1_FMT],FMT_DUAL
        jz      .done
        ; Audio 2 slaved to Audio 1's clock and filter clock, without 4x
        ; oversampling, its filter as Audio 1's
        mov     al,MX_A2_MODE
        call    a1_mixer_read
        and     al,~(A2_4X | A2_ASYNC | A2_BYPASS) & 0FFh
        test    al,A1_BYPASS
        jz      .a2f
        or      al,A2_BYPASS
.a2f:   mov     ah,al
        mov     al,MX_A2_MODE
        call    a1_mixer_write
        ; its format as ESS's playback start writes it (6:2DB6), no interrupt
        mov     ah,FMT_STEREO
        test    byte [si+A1_FMT],FMT_16BIT
        jz      .a2c
        or      ah,05h                  ; 16-bit, signed
.a2c:   mov     al,MX_A2_CONTROL2
        call    a1_mixer_write
        mov     ax,[si+A1_BLOCK]
        neg     ax
        push    ax
        mov     ah,al
        mov     al,MX_A2_COUNT_LO
        call    a1_mixer_write
        pop     ax
        mov     al,MX_A2_COUNT_HI
        call    a1_mixer_write
        mov     ah,[si+DEV_WAVE_VOL]
        mov     al,MX_A2_VOLUME
        call    a1_mixer_write
.done:  ret

; the DMA started on the ring, from what wasn't played (a1_ready); in
; dual playback Audio 2's too, both at once
a1_start:
        push    si
        fcall   1, a1_ready
        ; DAC direction, auto-initialize, not going yet
        mov     al,CR_CONTROL
        call    a1_reg_read
        and     al,30h
        or      al,04h
        push    ax
        mov     ah,al
        mov     al,CR_CONTROL
        call    a1_reg_write
        ; the FIFO empty
        mov     dx,[si+DEV_BASE]
        add     dx,6
        mov     al,2
        out     dx,al
        xor     al,al
        out     dx,al
        ; an interrupt each block
        mov     ax,[si+A1_BLOCK]
        neg     ax
        push    ax
        mov     ah,al
        mov     al,CR_COUNT_LO
        call    a1_reg_write
        pop     ax
        mov     al,CR_COUNT_HI
        call    a1_reg_write
        ; the 8237, as ESS's record start (6:2799), in the playback mode
        pushf
        cli
        xor     dh,dh
        mov     dl,[si+DEV_DMA_MASK]
        mov     al,[si+DEV_DMA_OFF]
        out     dx,al
        mov     dl,[si+DEV_DMA_FF]
        xor     al,al
        out     dx,al
        mov     al,[si+DEV_DMA_PLAY]
        test    byte [si+DEV_FLAGS+1],DEMAND_HI
        jz      .mode
        and     al,3Fh
.mode:  mov     dl,[si+DEV_DMA_MODE]
        out     dx,al
        mov     ax,[si+DEV_BUF_PHYS]
        mov     dl,[si+DEV_DMA_ADDR]
        out     dx,al
        mov     al,ah
        out     dx,al
        mov     al,[si+DEV_BUF_PAGE]
        mov     dl,[si+DEV_DMA_PAGE]
        out     dx,al
        mov     ax,[si+A1_RING]
        dec     ax
        mov     dl,[si+DEV_DMA_COUNT]
        out     dx,al
        mov     al,ah
        out     dx,al
        mov     al,[si+DEV_DMA_ON]
        mov     dl,[si+DEV_DMA_MASK]
        out     dx,al
        or      byte [si+A1_STATE],A1F_RUN
        popf
        test    byte [si+A1_FMT],FMT_DUAL
        jnz     .dual
        ; go
        pop     ax
        or      al,1
        mov     ah,al
        mov     al,CR_CONTROL
        jmp     a1_reg_write
        ; Audio 2's DMA on its ring, into its FIFO but not to the DAC yet
.dual:  call    a1_dma2
        mov     ah,13h                  ; auto-initialize, DMA, FIFO to DAC
        test    byte [si+DEV_FLAGS+1],DEMAND_HI
        jz      .xfer
        mov     ah,93h                  ; 4-byte demand transfers, as ESS
.xfer:  push    ax
        and     ah,~1 & 0FFh
        mov     al,MX_A2_CONTROL1
        call    a1_mixer_write
        ; both go, as close together as the ports allow
        pop     bx
        pop     ax
        pushf
        cli
        push    bx
        or      al,1
        mov     ah,al
        mov     al,CR_CONTROL
        call    a1_reg_write
        pop     ax
        mov     al,MX_A2_CONTROL1
        call    a1_mixer_write
        popf
        ret

; Audio 2's 8237 channel on its ring, as ESS's playback start programs it
; (6:2C6E), a 16-bit channel in words
a1_dma2:
        pushf
        cli
        xor     dh,dh
        mov     dl,[si+DEV_A2_MASK]
        mov     al,[si+DEV_A2_OFF]
        out     dx,al
        mov     dl,[si+DEV_A2_FF]
        xor     al,al
        out     dx,al
        mov     al,[si+DEV_A2_PLAY]
        test    byte [si+DEV_FLAGS+1],DEMAND_HI
        jz      .mode
        and     al,3Fh
.mode:  mov     dl,[si+DEV_A2_MODE]
        out     dx,al
        mov     ax,[si+DEV_A2_PHYS]
        mov     cx,[si+A1_RING]
        mov     bl,[si+DEV_A2_PHYS_PG]
        cmp     byte [si+DEV_A2_DMA],3
        jbe     .addr
        shr     bl,1                    ; a word address, 17 bits
        rcr     ax,1
        shl     bl,1
        shr     cx,1
.addr:  mov     dl,[si+DEV_A2_ADDR]
        out     dx,al
        mov     al,ah
        out     dx,al
        mov     al,bl
        mov     dl,[si+DEV_A2_PAGE]
        out     dx,al
        mov     ax,cx
        dec     ax
        mov     dl,[si+DEV_A2_COUNT]
        out     dx,al
        mov     al,ah
        out     dx,al
        mov     al,[si+DEV_A2_ON]
        mov     dl,[si+DEV_A2_MASK]
        out     dx,al
        popf
        ret

; the DMA stopped, if it runs
a1_stop:
        push    si
        fcall   1, a1_hwstop
        ret

; the DMA channel masked and the FIFO empty
a1_dma_off:
        xor     dh,dh
        mov     dl,[si+DEV_DMA_MASK]
        mov     al,[si+DEV_DMA_OFF]
        out     dx,al
        mov     dx,[si+DEV_BASE]
        add     dx,6
        mov     al,2
        out     dx,al
        xor     al,al
        out     dx,al
        ret

; es_wid_resume(dev), far pascal, in place of ESS's wave-in resume at an
; APM resume (3:4D1B): that, then the player set up again on the chip
; hw_init reset, and started again if it played
es_wid_resume:
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    word [bp+6]
        push    cs
        call    L6_0D40
        push    ax
        mov     si,[bp+6]
        test    byte [si+A1_STATE],A1F_OPEN
        jz      .done
        mov     al,[si+A1_STATE]
        push    ax
        and     byte [si+A1_STATE],~A1F_RUN & 0FFh
        call    a1_dma_off
        call    a1_setup
        pop     ax
        test    al,A1F_RUN
        jz      .done                   ; paused: WODM_RESTART starts it
        call    a1_start
.done:  pop     ax
        pop     di
        pop     si
        pop     bp
        retf    2

; --- DSP and mixer access: SI = dev -----------------------------------

; command AL
a1_dsp:
        push    si
        push    ax
        fcall   1, dsp_write
        ret

; controller register AL = AH
a1_reg_write:
        push    ax
        call    a1_dsp
        pop     ax
        mov     al,ah
        jmp     a1_dsp

; controller register AL, to AL
a1_reg_read:
        push    ax
        mov     al,DSP_READ_REG
        call    a1_dsp
        pop     ax
        call    a1_dsp
        push    si
        fcall   1, dsp_read
        ret

; controller register AL: bits 6 and 4 set
a1_reg_or50:
        push    ax
        call    a1_reg_read
        or      al,50h
        mov     ah,al
        pop     bx
        mov     al,bl
        jmp     a1_reg_write

; mixer register AL, to AL
a1_mixer_read:
        push    si
        push    ax
        fcall   1, mixer_read
        ret

; mixer register AL = AH
a1_mixer_write:
        push    si
        push    ax
        mov     al,ah
        push    ax
        fcall   1, mixer_write
        ret
