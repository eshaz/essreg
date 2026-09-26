; Macros for the reassemblable NE source that tools/ne2asm.py generates.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

%ifndef NE16_INC
%define NE16_INC

; far call/jump into another segment: target offset, label of the selector
; word (a relocation site), chain link (next site or 0xFFFF)
%macro callf 3
        db      0x9A
        dw      %1
%2:     dw      %3
%endmacro
%macro jmpf 3
        db      0xEA
        dw      %1
%2:     dw      %3
%endmacro

; mov r16, selector: register, label of the selector word, chain link
%macro movsel 3
        db      0xB8 + REG_%1
%2:     dw      %3
%endmacro

; far call through a relocated far pointer (an import): label of the
; pointer, chain link, selector word
%macro callp 3
        db      0x9A
%1:     dw      %2, %3
%endmacro

; relocation record: type, flags, first site, target words
%macro reloc 5
        db      %1, %2
        dw      %3, %4, %5
%endmacro

; reg, reg forms with the "load" opcode (reg field = destination)
%define REG_ax 0
%define REG_cx 1
%define REG_dx 2
%define REG_bx 3
%define REG_sp 4
%define REG_bp 5
%define REG_si 6
%define REG_di 7
%define REG_al 0
%define REG_cl 1
%define REG_dl 2
%define REG_bl 3
%define REG_ah 4
%define REG_ch 5
%define REG_dh 6
%define REG_bh 7
%define WID_ax 16
%define WID_cx 16
%define WID_dx 16
%define WID_bx 16
%define WID_sp 16
%define WID_bp 16
%define WID_si 16
%define WID_di 16
%define WID_al 8
%define WID_cl 8
%define WID_dl 8
%define WID_bl 8
%define WID_ah 8
%define WID_ch 8
%define WID_dh 8
%define WID_bh 8
%macro _rr_load 3                       ; 16-bit opcode, dst, src
%if WID_%2 == 16
        db      %1, 0xC0 | (REG_%2 << 3) | REG_%3
%else
        db      (%1) - 1, 0xC0 | (REG_%2 << 3) | REG_%3
%endif
%endmacro
%macro add_ 2
        _rr_load 0x03, %1, %2
%endmacro
%macro or_ 2
        _rr_load 0x0B, %1, %2
%endmacro
%macro adc_ 2
        _rr_load 0x13, %1, %2
%endmacro
%macro sbb_ 2
        _rr_load 0x1B, %1, %2
%endmacro
%macro and_ 2
        _rr_load 0x23, %1, %2
%endmacro
%macro sub_ 2
        _rr_load 0x2B, %1, %2
%endmacro
%macro xor_ 2
        _rr_load 0x33, %1, %2
%endmacro
%macro cmp_ 2
        _rr_load 0x3B, %1, %2
%endmacro
%macro mov_ 2
        _rr_load 0x8B, %1, %2
%endmacro

%endif
