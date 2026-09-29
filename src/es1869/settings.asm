; SYSTEM.INI settings of ES1869.DRV, assembled when ES1869_FIX=1 and
; appended to segment 3.
;
; Notes:
;
; [ES1869.DRV] turns the driver's changes on and off, one key each
; (docs/DRIVER_CONFIG.md).  It's read once, at the first DRVM_ENABLE,
; right after ESS's own configuration (read_config), with
; GetPrivateProfileInt: a key that isn't there keeps its default.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; in place of read_config(dev) at the first DRVM_ENABLE (3:4B56), far
; pascal: ESS's configuration, then SYSTEM.INI
es_read_config:
        push    bp
        mov     bp,sp
        push    word [bp+6]
        push    cs
        call    read_config
        push    cs
        call    es_read_ini
        pop     bp
        retf    2

; es_opts from SYSTEM.INI (es_settings), far, once
es_read_ini:
        test    byte [es_opts+1],OPT_READ >> 8
        jnz     .done
        push    si
        mov     si,es_settings
.next:  mov     ax,[si]                 ; the key
        or      ax,ax
        jz      .read
        push    ds                      ; the section
        push    word es_section
        push    ds
        push    ax
        xor     ax,ax                   ; the default: the bit as it is
        mov     cx,[si+2]
        test    [es_opts],cx
        jz      .def
        inc     ax
.def:   push    ax
        push    ds
        push    word es_ini_file
        callp   ..@ES_I_PPINT, 0xFFFF, 0x0000   ; KERNEL.GetPrivateProfileInt
        mov     cx,[si+2]
        or      ax,ax
        jz      .off
        or      [es_opts],cx
        jmp     short .skip
.off:   not     cx
        and     [es_opts],cx
.skip:  add     si,byte 4
        jmp     short .next
.read:  or      word [es_opts],OPT_READ
        pop     si
.done:  retf

; the import's relocation record, for the relocation table of segment 3
%macro settings_relocs 0
        reloc 3, 1, ..@ES_I_PPINT, 0x0001, 0x007F       ; KERNEL.GetPrivateProfileInt
%endmacro
