; ESFMOPL3.DRV, the OPL3 device of ESFM.DRV as a MIDI driver of its own, so
; that Windows 95 and 98 list it in the Multimedia control panel.  Built by
; tools/build_esfm.py with tools/nelink.py and esfmopl3.json.
;
; Notes:
;
; 1. Windows 95 and 98 load the MIDI drivers of a card from the card's
;    software key (Class\MEDIA\nnnn\Drivers\midi\<driver>), and keep the
;    driver's entry in the MIDI list (MediaResources\midi\<driver><nnnn>)
;    active at every start.  ESS's INF puts ESFM.DRV and ESSMPU.DRV there,
;    and essinst adds this driver the same way, because ESFM.DRV's second
;    device has no entry of its own that Windows keeps.
; 2. The driver has one device, the OPL3 device.  Every message to it goes
;    on to ESFM.DRV's modMessage as device 1's, with its arguments as they
;    came, the card's devnode among them, and its result comes back.  With
;    Opl3Device=2 in [ESFM.DRV] of SYSTEM.INI, which essinst writes with
;    the driver, ESFM.DRV counts its own device only, so a program sees the
;    OPL3 device once.  With any other value this driver has no device,
;    nor when ESFM.DRV counted device 1 itself, which it does for the
;    session if this driver wasn't loaded at its first count.
; 3. ESFM.DRV's modMessage is looked up by name (GetModuleHandle and
;    GetProcAddress) at task time: at the device count, the caps and the
;    open.  The messages that may come at interrupt time (data, long data,
;    reset, close) only follow the pointer, in fixed memory.  Before
;    ESFM.DRV is loaded, the count is 1 and the rest MMSYSERR_NODRIVER.
; 4. MMSYSTEM tells each driver of the card's devnode coming and going
;    (DRVM_INIT, DRVM_ENABLE, DRVM_DISABLE, DRVM_EXIT).  ESFM.DRV hears of
;    it itself, so this driver answers them and passes none on, and it
;    passes on only the messages of a device (below DRVM_INIT).
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

        bits 16
%include "ne16.inc"

DRV_LOAD        equ 1
DRV_ENABLE      equ 2
DRV_FREE        equ 6
DRV_QUERYCONFIGURE equ 8
DRV_INSTALL     equ 9
DRV_REMOVE      equ 10
DRV_POWER       equ 15
DRVCNF_OK       equ 1
MODM_GETNUMDEVS equ 1
MODM_GETDEVCAPS equ 2
MODM_OPEN       equ 3
DRVM_INIT       equ 0x64
DRVM_ENABLE     equ 0x67
MMSYSERR_BADDEVICEID equ 2
MMSYSERR_NODRIVER equ 6
MMSYSERR_NOTSUPPORTED equ 8
OPL3_DEVICE     equ 1               ; ESFM.DRV's device that this one is

; module references, in the order of esfmopl3.json
KERNEL          equ 1
USER            equ 2

%define KERNEL_GetModuleHandle 47
%define KERNEL_GetProcAddress 50
%define KERNEL_GetPrivateProfileInt 127
%define USER_DefDriverProc 255

; far call to an import: each call site gets a relocation record
%assign NIMP 0
%macro import 2
%assign NIMP NIMP + 1
        callp   ..@IMP%[NIMP], 0xFFFF, 0x0000
%xdefine IMOD%[NIMP] %1
%xdefine IORD%[NIMP] %2
%endmacro
%macro kernel 1
        import  KERNEL, KERNEL_%1
%endmacro
%macro user 1
        import  USER, USER_%1
%endmacro

; mov r16, DGROUP's selector: each site gets a relocation record
%assign NSEL 0
%macro dgroup 1
%assign NSEL NSEL + 1
        movsel  %1, ..@SEL%[NSEL], 0xFFFF
%endmacro

; the arguments of DriverProc and modMessage, after push bp
DP_LP2          equ 0x06
DP_LP1          equ 0x0A
DP_MSG          equ 0x0E
DP_HDRV         equ 0x10
DP_ID           equ 0x12
MM_DW2          equ 0x06
MM_DW1          equ 0x0A
MM_USER         equ 0x0E
MM_MSG          equ 0x12
MM_DEV          equ 0x14

        section seg1 progbits start=0 vstart=0 align=1

; the DLL's start: DS = DGROUP, DI = hInstance; AX = 1
LibEntry:
        mov     ax,1
        retf

; WEP(nParam)
WEP:
        mov     ax,1
        retf    2

; DriverProc(dwDriverID, hDriver, uMsg, lParam1, lParam2)
DriverProc:
        push    bp
        mov     bp,sp
        push    ds
        dgroup  ax
        mov     ds,ax
        mov     ax,[bp+DP_MSG]
        cmp     ax,DRV_LOAD
        jb      .def
        cmp     ax,DRV_FREE
        ja      .other
        cmp     ax,DRV_ENABLE
        ja      .one
        ; DRV_LOAD and DRV_ENABLE: ESFM.DRV may come again, and the setting
        mov     word [esfm_msg],0
        mov     word [esfm_msg+2],0
        call    read_setting
.one:   mov     ax,1
        xor     dx,dx
        jmp     .out
.other: cmp     ax,DRV_QUERYCONFIGURE
        je      .zero
        cmp     ax,DRV_INSTALL
        je      .one
        cmp     ax,DRV_REMOVE
        je      .one
        cmp     ax,DRV_POWER
        je      .one
.def:   push    word [bp+DP_ID+2]
        push    word [bp+DP_ID]
        push    word [bp+DP_HDRV]
        push    word [bp+DP_MSG]
        push    word [bp+DP_LP1+2]
        push    word [bp+DP_LP1]
        push    word [bp+DP_LP2+2]
        push    word [bp+DP_LP2]
        user    DefDriverProc
        jmp     .out
.zero:  xor     ax,ax
        cwd
.out:   pop     ds
        pop     bp
        retf    0x10

; [ESFM.DRV] Opl3Device of SYSTEM.INI, once: 2 gives this driver its
; device.  DS = DGROUP
read_setting:
        cmp     word [read],0
        jne     .done
        push    ds
        push    word s_sect
        push    ds
        push    word s_key
        mov     ax,1
        push    ax
        push    ds
        push    word s_file
        kernel  GetPrivateProfileInt
        xor     cx,cx
        cmp     ax,2
        jne     .set
        inc     cx
.set:   mov     [devs],cx
        mov     word [read],1
.done:  ret

; esfm_msg = ESFM.DRV's modMessage, if it's loaded; at task time only.
; DS = DGROUP
find_esfm:
        mov     ax,[esfm_msg]
        or      ax,[esfm_msg+2]
        jnz     .done
        push    ds
        push    word s_esfm
        kernel  GetModuleHandle
        or      ax,ax
        jz      .done
        push    ax
        push    ds
        push    word s_modmsg
        kernel  GetProcAddress
        mov     [esfm_msg],ax
        mov     [esfm_msg+2],dx
.done:  ret

; modMessage(uDeviceID, uMsg, dwUser, dwParam1, dwParam2), in fixed code:
; the data, long data, reset and close may come at interrupt time
modMessage:
        push    bp
        mov     bp,sp
        push    ds
        dgroup  ax
        mov     ds,ax
        mov     ax,[bp+MM_MSG]
        cmp     ax,MODM_GETNUMDEVS
        je      .count
        cmp     ax,DRVM_INIT
        jb      .dev
        cmp     ax,DRVM_ENABLE
        ja      .notsup
        xor     ax,ax                   ; the devnode's news: ESFM.DRV's
        cwd
        jmp     .out
.dev:   cmp     word [bp+MM_DEV],0
        jne     .baddev
        cmp     word [devs],0
        je      .baddev
        cmp     ax,MODM_GETDEVCAPS
        je      .find
        cmp     ax,MODM_OPEN
        jne     .pass
.find:  call    find_esfm
.pass:  mov     ax,[esfm_msg]
        or      ax,[esfm_msg+2]
        jz      .nodrv
        mov     ax,OPL3_DEVICE
        call    forward
        jmp     .out
.count: call    read_setting
        xor     ax,ax
        cwd
        cmp     word [devs],0
        je      .out
        call    find_esfm
        mov     ax,[esfm_msg]
        or      ax,[esfm_msg+2]
        jz      .one                    ; ESFM.DRV comes later
        xor     ax,ax                   ; its count of this card's devices:
        call    forward                 ; 1 leaves device 1 to this driver
        or      dx,dx
        jnz     .none
        cmp     ax,1
        jne     .none
.one:   mov     ax,1
        xor     dx,dx
        jmp     .out
.none:  xor     ax,ax
        cwd
        jmp     .out
.baddev:
        mov     ax,MMSYSERR_BADDEVICEID
        xor     dx,dx
        jmp     .out
.notsup:
        mov     ax,MMSYSERR_NOTSUPPORTED
        xor     dx,dx
        jmp     .out
.nodrv: mov     ax,MMSYSERR_NODRIVER
        xor     dx,dx
.out:   pop     ds
        pop     bp
        retf    0x10

; ESFM.DRV's modMessage with this call's arguments, as device AX: DX:AX is
; its result.  BP = modMessage's frame, DS = DGROUP
forward:
        push    ax
        push    word [bp+MM_MSG]
        push    word [bp+MM_USER+2]
        push    word [bp+MM_USER]
        push    word [bp+MM_DW1+2]
        push    word [bp+MM_DW1]
        push    word [bp+MM_DW2+2]
        push    word [bp+MM_DW2]
        call    far [esfm_msg]
        ret

seg1_data_end:

; relocation table
        dw (seg1_rel_end - seg1_rel_start) / 8
seg1_rel_start:
%assign i 1
%rep NSEL
        reloc 2, 0, ..@SEL%[i], 0x0002, 0x0000          ; seg2
%assign i i + 1
%endrep
%assign i 1
%rep NIMP
        reloc 3, 1, ..@IMP%[i], IMOD%[i], IORD%[i]
%assign i i + 1
%endrep
seg1_rel_end:
seg1_end:

        section seg2 progbits follows=seg1 vstart=0 align=1

; DGROUP: Windows' instance data first
        times 16 db 0
esfm_msg:       dd 0            ; ESFM.DRV's modMessage, 0 until found
devs:           dw 0            ; this driver's devices: 1 with Opl3Device=2
read:           dw 0            ; SYSTEM.INI was read
s_esfm:         db 'ESFM', 0
s_modmsg:       db 'modMessage', 0
s_sect:         db 'ESFM.DRV', 0
s_key:          db 'Opl3Device', 0
s_file:         db 'SYSTEM.INI', 0

seg2_data_end:
seg2_end:

        section link progbits follows=seg2 vstart=0 align=1
        db 'NELINK', 1, 0
        dw seg1_data_end, seg1_end
        dw seg2_data_end, seg2_end
        dw WEP
        dw DriverProc
        dw modMessage
        dw LibEntry
