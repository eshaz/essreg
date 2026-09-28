; The Audio 2 DAC's mode for ES1869.DRV, assembled when ES1869_FIX=1 and
; appended to segment 1 (fixed code).
;
; Notes:
;
; ESS's code reads mixer 71h and ORs in 12h before it writes it back, at
; the wave-out open (1:1157) and at every playback start (6:2DE6): 4x
; oversampling (bit 4) and asynchronous (bit 1).  Here both read 71h
; through a2_mode_read, which sets bits 4 and 3 (the filter bypassed) as
; SYSTEM.INI says, and ESS's OR sets only bit 1.
;
; By default the DAC plays the samples as they are, not oversampled and
; the filter bypassed (docs/AUDIO_PIPELINE.md).  Audio2Oversampling=1
; with Audio2Filter=1 gives ESS's value back.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; a2_mode_read(dev, reg), far pascal, in place of mixer_read(dev, 71h)
a2_mode_read:
        push    bp
        mov     bp,sp
        push    word [bp+8]             ; dev
        push    word [bp+6]             ; the register
        push    cs
        call    mixer_read
        test    byte [es_opts+1],OPT_A2_4X >> 8
        jnz     .over
        and     al,~A2_4X & 0xFF
        test    byte [es_opts+1],OPT_A2_FILTER >> 8
        jnz     .filter
.bypass:
        or      al,A2_BYPASS
        jmp     short .done
.filter:
        and     al,~A2_BYPASS & 0xFF
        jmp     short .done
.over:  or      al,A2_4X
        test    byte [es_opts+1],OPT_A2_FILTER >> 8
        jz      .bypass                 ; bit 3 as it was: ESS's value
.done:  pop     bp
        retf    4
