; SYSTEM.INI settings of ESFM.DRV, assembled when ESFM_FIX=1.  The code is
; appended to segment 1 (fixed code).
;
; Notes:
;
; [ESFM.DRV] turns each change to ESS's driver on or off, one key each
; (docs/DRIVER_CONFIG.md).  The keys are read once, at the first
; DRV_ENABLE, with GetPrivateProfileInt: a key that isn't there keeps its
; default.  Bank= isn't one of them: it names the bank file, and it's read
; again at every MODM_OPEN (esfmfile.asm).
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
        mov     cx,[si+2]
        or      ax,ax
        jz      .off
        or      [fix_opts],cx
        jmp     .skip
.off:   not     cx
        and     [fix_opts],cx
.skip:  add     si,4
        jmp     .next
.read:  or      word [fix_opts],OPT_READ
        pop     si
.done:  ret
