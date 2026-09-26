; vxd.inc -- macros and structures shared by the ES1869.VXD source.

%ifndef VXD_INC
%define VXD_INC

; Dynamic link to a VxD service: int 20h followed by (device << 16 | service).
; VMM patches the six bytes into a direct call on first use.
%macro VxDCall 1
        int     0x20
        dd      %1
%endmacro

; Same, but the service is entered with a jump (bit 15 of the service number).
%macro VxDJmp 1
        int     0x20
        dd      (%1) | 0x8000
%endmacro

%include "services.inc"

; Register-register instructions in the "load" encoding (op reg, r/m), which
; MASM prefers and NASM does not emit by default.  "mov_ ebp,esp" assembles to
; 8B EC where "mov ebp,esp" would give 89 E5; both do the same thing.
%define REG_eax 0
%define REG_ecx 1
%define REG_edx 2
%define REG_ebx 3
%define REG_esp 4
%define REG_ebp 5
%define REG_esi 6
%define REG_edi 7
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
%define WID_eax 32
%define WID_ecx 32
%define WID_edx 32
%define WID_ebx 32
%define WID_esp 32
%define WID_ebp 32
%define WID_esi 32
%define WID_edi 32
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

%macro _rr_load 3                       ; opcode (32-bit form), dst, src
%if WID_%2 == 32
        db      %1, 0xC0 | (REG_%2 << 3) | REG_%3
%elif WID_%2 == 16
        db      0x66, %1, 0xC0 | (REG_%2 << 3) | REG_%3
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

%include "ctlmsg.inc"

; Client_Reg_Struc offsets (EBP points to it in V86/PM API procedures)
Client_EDI      equ 0x00
Client_ESI      equ 0x04
Client_EBP      equ 0x08
Client_EBX      equ 0x10
Client_EDX      equ 0x14
Client_ECX      equ 0x18
Client_EAX      equ 0x1C
Client_Error    equ 0x20
Client_EIP      equ 0x24
Client_CS       equ 0x28
Client_EFlags   equ 0x2C
Client_ESP      equ 0x30
Client_SS       equ 0x34
Client_ES       equ 0x38
Client_DS       equ 0x3C
Client_FS       equ 0x40
Client_GS       equ 0x44
CF_Mask         equ 0x0001

%endif
