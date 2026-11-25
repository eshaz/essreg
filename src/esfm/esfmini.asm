; SYSTEM.INI settings of ESFM.DRV, assembled when ESFM_FIX=1.  The code is
; appended to segment 1 (fixed code).
;
; Notes:
;
; [ESFM.DRV] turns each change to ESS's driver on or off, one key each
; (docs/DRIVER_CONFIG.md).  The keys are read once, at the first
; DRV_ENABLE or device count, with GetPrivateProfileInt: a key that isn't
; there keeps its default.  Opl3Device=2 also sets OPT_OPL3_DRV: device 1
; is ESFMOPL3.DRV's (esfmopl.asm).  Opl3Headroom= is a number of decibels,
; which fix_opl3_att keeps in the chip's 0.75 dB steps, rounded.  Bank=
; isn't one of them: it names the bank file, and it's read again at every
; MODM_OPEN (esfmfile.asm).
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; fix_opts from SYSTEM.INI (fix_settings), once; DS = DGROUP, keeps SI, DI
fix_read_settings:
        test    word [fix_opts],OPT_READ
        jnz     .done
        push    si
        mov     si,fix_settings
.next:  mov     ax,[si]                 ; the key
        or      ax,ax
        jz      .read
        push    ds                      ; the section
        push    word fix_ini_sect
        push    ds
        push    ax
        xor     ax,ax                   ; the default: the bit as it is
        mov     cx,[si+2]
        test    [fix_opts],cx
        jz      .def
        inc     ax
.def:   push    ax
        push    ds
        push    word fix_ini_file
        kernel  GetPrivateProfileInt
        cmp     word [si],fix_key_opl3
        jne     .bit
        and     word [fix_opts],~OPT_OPL3_DRV
        cmp     ax,2
        jne     .bit
        or      word [fix_opts],OPT_OPL3_DRV
.bit:   mov     cx,[si+2]
        or      ax,ax
        jz      .off
        or      [fix_opts],cx
        jmp     .skip
.off:   not     cx
        and     [fix_opts],cx
.skip:  add     si,4
        jmp     .next
.read:  push    ds                      ; Opl3Headroom=, 0 to 47 dB
        push    word fix_ini_sect
        push    ds
        push    word fix_key_headroom
        push    word OPL3_HEADROOM_DEFAULT
        push    ds
        push    word fix_ini_file
        kernel  GetPrivateProfileInt   ; digits only, unsigned
        cmp     ax,47
        jbe     .steps
        mov     ax,47
.steps: shl     ax,2                    ; dB * 4 / 3, rounded
        inc     ax
        mov     cl,3
        div     cl
        xor     ah,ah
        mov     [fix_opl3_att],ax
        or      word [fix_opts],OPT_READ
        pop     si
.done:  ret
