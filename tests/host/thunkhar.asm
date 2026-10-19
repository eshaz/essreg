; thunkhar is a test harness for src/win/vxdcall.asm that tests/test_thunk.py
; runs in a 16-bit CPU emulator.
;
; It calls vxd_raw_call with a fake API entry point that records the
; registers it gets, clobbers BP and returns new values, then calls
; vxd_get_entry (the emulator answers its INT 2Fh). The results are left
; in DATA for the test to read, and `int 3` ends the run.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

        .386

DGROUP group DATA, STACKSEG

DATA segment word public use16 'DATA'
        public  regs, seen, results
regs    dd 11111111h, 22222222h, 33333333h, 44444444h, 55555555h, 66666666h
        dw 0                            ; es (set at run time)
        dw 0                            ; flags
calls   dw 0
seen    db 2*28 dup (0)                 ; per call: the six registers, ES, DS
results dd 0                            ; +0 return value of vxd_raw_call
        dd 0                            ; +4 EBP after the call
        dd 0                            ; +8 ESI
        dd 0                            ; +12 EDI
        dd 0                            ; +16 EBX
        dw 0                            ; +20 FS
        dw 0                            ; +22 GS
        dw 0                            ; +24 DS
        dw 0                            ; +26 SP before - SP after
        dd 0                            ; +28 vxd_get_entry DX:AX
        dd 0                            ; +32 second call: return value
        dd 0                            ; +36 ECX (upper half kept?)
DATA ends

STACKSEG segment para stack use16 'STACK'
        db 512 dup (?)
STACKSEG ends

        extrn   _vxd_raw_call:far
        extrn   _vxd_get_entry:far

HARNESS_TEXT segment word public use16 'CODE'
        assume  cs:HARNESS_TEXT, ds:DGROUP

fake_api proc far
        ; record what arrived in seen[calls]
        pushad                          ; +0 EDI +4 ESI +16 EBX +20 EDX
        push    es                      ; +24 ECX +28 EAX
        push    ds
        mov     bp,sp                   ; [bp] DS, [bp+2] ES, [bp+4] image
        mov     ax,DGROUP
        mov     ds,ax
        mov     bx,calls
        inc     calls
        imul    bx,bx,28
        mov     eax,[bp+4+28]
        mov     dword ptr seen[bx],eax
        mov     eax,[bp+4+16]
        mov     dword ptr seen[bx+4],eax
        mov     eax,[bp+4+24]
        mov     dword ptr seen[bx+8],eax
        mov     eax,[bp+4+20]
        mov     dword ptr seen[bx+12],eax
        mov     eax,[bp+4+4]
        mov     dword ptr seen[bx+16],eax
        mov     eax,[bp+4+0]
        mov     dword ptr seen[bx+20],eax
        mov     ax,[bp+2]
        mov     word ptr seen[bx+24],ax
        mov     ax,[bp]
        mov     word ptr seen[bx+26],ax
        mov     edx,[bp+4+20]           ; incoming EDX decides the carry
        pop     ds
        pop     es
        add     sp,32
        ; return new values, clobber BP, set carry if EDX = 0404h
        mov     ax,DGROUP
        mov     es,ax
        mov     eax,0A1A1A1A1h
        mov     ebx,0B2B2B2B2h
        mov     ecx,0C3C3C3C3h
        mov     esi,0E5E5E5E5h
        mov     edi,0F6F6F6F6h
        mov     bp,1234h
        cmp     dx,0404h
        mov     edx,0D4D4D4D4h
        je      carry
        clc
        ret
carry:  stc
        ret
fake_api endp

start:
        mov     ax,DGROUP
        mov     ds,ax
        mov     word ptr regs+24,ax     ; ES passed in = DS
        mov     ebp,7777BEEFh           ; upper halves must survive
        mov     esi,5A5A0000h
        mov     edi,0A5A50000h
        mov     ebx,3C3C0000h
        mov     ecx,9C9C0000h
        push    1111h
        pop     fs
        push    2222h
        pop     gs
        mov     dx,sp

        push    ds                      ; r
        push    offset regs
        push    cs                      ; entry
        push    offset fake_api
        call    _vxd_raw_call
        add     sp,8

        mov     results,eax
        mov     results+4,ebp
        mov     results+8,esi
        mov     results+12,edi
        mov     results+16,ebx
        mov     word ptr results+20,fs
        mov     word ptr results+22,gs
        mov     word ptr results+24,ds
        sub     dx,sp
        mov     word ptr results+26,dx
        mov     results+36,ecx

        ; second call: DX = 0404h makes the fake API set the carry flag
        mov     dword ptr regs+12,0404h
        push    ds
        push    offset regs
        push    cs
        push    offset fake_api
        call    _vxd_raw_call
        add     sp,8
        mov     results+32,eax

        push    3B07h
        call    _vxd_get_entry
        add     sp,2
        mov     word ptr results+28,ax
        mov     word ptr results+30,dx
        int     3

HARNESS_TEXT ends
        end     start
