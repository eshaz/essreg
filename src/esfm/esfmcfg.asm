; The Settings box of the patch banks, for ESFM.DRV, assembled when
; ESFM_FIX=1.  The code is appended to segment 1 (fixed code).
;
; Notes:
;
; 1. Windows 95 and 98's Multimedia control panel has a Settings button in
;    the properties of each MIDI driver (Devices, MIDI Devices and
;    Instruments), which asks the driver DRV_QUERYCONFIGURE and sends it
;    DRV_CONFIGURE with the panel's window.  ESS's DriverProc answers both
;    with 0, so the button stays gray.
; 2. With BankSettings, the default, both come here: seg3's jt_drvmsg
;    sends them to ESS's DRV_ENABLE code, whose far call goes to
;    fix_drv_enable (esfmfile.asm), so that ESS's segment keeps its
;    length.  That code answers 1 when the call returns DX:AX = 0, and 0
;    otherwise, which is all the two messages need.  The
;    button is there when ESFMCFG.DLL can be found, which essinst puts in
;    the SYSTEM folder, and opens the DLL's box (BankDialog), where the
;    banks of the ESFM device (Bank=) and of the OPL3 device (Opl3Bank=)
;    are chosen; the device asks for its bank file at its next open
;    (esfmfile.asm).  ESFMOPL3.DRV does the same for the OPL3 device.
; 3. The DLL is loaded for the box only, at task time, and freed after it.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

%define KERNEL_OpenFile 74
%define KERNEL_LoadLibrary 95
%define KERNEL_FreeLibrary 96
%define KERNEL_GetProcAddress 50

DRV_CONFIGURE   equ 7
OF_EXIST        equ 0x4000
OFSTRUCT_SIZE   equ 136
CFG_DEVICE      equ 0           ; the device whose box this is: ESFM

; DRV_CONFIGURE and DRV_QUERYCONFIGURE, from fix_drv_enable (far, from
; seg3) with BP = DriverProc's frame ([bp+0xE] the message, [bp+0xA]
; lParam1, for DRV_CONFIGURE the parent window).  DX:AX = 0 makes ESS's
; code answer 1: the query when ESFMCFG.DLL is there, DRV_CONFIGURE when the
; box ended with OK (DRVCNF_OK, or DRVCNF_RESTART, which it doesn't use).
; Otherwise DX:AX = 1 makes it answer 0, as ESS's driver, also with
; BankSettings=0
fix_config:
        push    si
        push    di
        test    word [fix_opts],OPT_SETTINGS
        jz      .zero
        cmp     word [bp+0xE],DRV_CONFIGURE
        je      .run
        sub     sp,OFSTRUCT_SIZE        ; DRV_QUERYCONFIGURE
        mov     di,sp
        push    ds
        push    word fix_cfg_dll
        push    ss
        push    di
        push    word OF_EXIST
        kernel  OpenFile
        add     sp,OFSTRUCT_SIZE
        cmp     ax,-1
        je      .zero
        jmp     .yes
.run:   push    ds
        push    word fix_cfg_dll
        kernel  LoadLibrary
        cmp     ax,32
        jb      .zero
        mov     si,ax                   ; SI = the library
        push    si
        push    ds
        push    word fix_cfg_proc
        kernel  GetProcAddress
        mov     cx,ax
        or      cx,dx
        jz      .free
        mov     [fix_cfg_ptr],ax
        mov     [fix_cfg_ptr+2],dx
        push    word [bp+0xA]           ; the parent window
        push    word CFG_DEVICE
        call    far [fix_cfg_ptr]       ; BankDialog, pascal
        mov     di,ax                   ; DRVCNF_*
        push    si
        kernel  FreeLibrary
        or      di,di                   ; DRVCNF_CANCEL
        jz      .zero
.yes:   xor     ax,ax
        jmp     .out
.free:  push    si
        kernel  FreeLibrary
.zero:  mov     ax,1                    ; ESS's code answers 0
.out:   xor     dx,dx
        pop     di
        pop     si
        retf
