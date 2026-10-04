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
; The Audio 2 DAC's mode goes into mixer 71h at every start and resume,
; right after ESS's mixer reset (es_restore_mixer), so that the chip holds
; it from the start and not only from the first playback.
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

; far calls into segment 1: each site has its own relocation record
; (settings_relocs), in segment 3's table
%assign SET_NREL 0
%macro callf1 1                         ; the target
%assign SET_NREL SET_NREL + 1
        callf   %1, ..@SET_R%[SET_NREL], 0xFFFF
%endmacro

; in place of restore_mixer_state(dev) in hw_init (3:4897), far pascal:
; ESS's levels back, then 71h with the Audio 2 DAC's mode from SYSTEM.INI
; while the DSP is still held; ESS's setting leaves 71h as the reset left it
es_restore_mixer:
        push    bp
        mov     bp,sp
        push    word [bp+6]
        push    cs
        call    restore_mixer_state
        mov     al,[es_opts+1]
        and     al,(OPT_A2_4X | OPT_A2_FILTER) >> 8
        cmp     al,(OPT_A2_4X | OPT_A2_FILTER) >> 8
        je      .done
        push    word [bp+6]             ; mixer_write(dev, 71h, its mode)
        mov     ax,MX_A2_MODE
        push    ax
        push    word [bp+6]
        push    ax
        callf1  a2_mode_read
        push    ax
        callf1  mixer_write
.done:  pop     bp
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

; the relocation records, for the relocation table of segment 3: the
; import, and the far calls into segment 1
%macro settings_relocs 0
        reloc 3, 1, ..@ES_I_PPINT, 0x0001, 0x007F       ; KERNEL.GetPrivateProfileInt
%assign i 1
%rep SET_NREL
        reloc 2, 0, ..@SET_R%[i], 0x0001, 0x0000
%assign i i + 1
%endrep
%endmacro
