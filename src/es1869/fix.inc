; Constants of the changes to ES1869.DRV, assembled when ES1869_FIX=1.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

%ifndef FIX_INC
%define FIX_INC

; es_opts: the changes that are on, from SYSTEM.INI [ES1869.DRV]
; (settings.asm); the Audio 2 bits are those of ES1869.VXD's ESSREG_Opts
OPT_A2_4X       equ 0x0100      ; Audio2Oversampling: ESS's 4x
OPT_A2_FILTER   equ 0x0200      ; Audio2Filter: the filter in use
OPT_READ        equ 0x8000      ; SYSTEM.INI was read
OPT_DEFAULT     equ 0x0000

; mixer 71h, the Audio 2 mode
A2_4X           equ 0x10        ; 4x oversampling (the filter bypassed too)
A2_BYPASS       equ 0x08        ; the switched-capacitor filter bypassed
A2_ASYNC        equ 0x02        ; Audio 2 at its own rate

%endif
