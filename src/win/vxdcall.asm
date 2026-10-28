; Far calls into a VxD's protected mode API from 16-bit Windows
; (Watcom large model, __cdecl).
;
; Usage:
;   void __far * __far __cdecl vxd_get_entry(unsigned device_id);
;     INT 2Fh AX=1684h, returns the PM API entry point of a VxD or 0:0
;
;   int __far __cdecl vxd_raw_call(void __far *entry, vxd_regs __far *r);
;     loads EAX, EBX, ECX, EDX, ESI, EDI and ES from *r, calls entry,
;     stores the registers and FLAGS back into *r and returns 1 if the
;     carry flag was set, else 0
;     every other register is preserved, including the upper halves of
;     the 32-bit ones
;
; vxd_regs (src/vxdapi.h):
;   +0 eax  +4 ebx  +8 ecx  +12 edx  +16 esi  +20 edi  +24 es  +26 flags
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

        .386

VXDCALL_TEXT segment word public use16 'CODE'
        assume  cs:VXDCALL_TEXT, ds:nothing, es:nothing

        public  _vxd_get_entry
        public  _vxd_raw_call

_vxd_get_entry proc far
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    es
        mov     bx,[bp+6]               ; device id
        xor     di,di
        mov     es,di
        mov     ax,1684h
        int     2Fh
        mov     ax,di                   ; DX:AX = ES:DI
        mov     dx,es
        pop     es
        pop     di
        pop     si
        pop     bp
        ret
_vxd_get_entry endp

; frame after the prologue (BP = B):
;   [B+10] r         [B+6] entry      [B+2] return address
;   [B-2] ds  [B-4] es  [B-6] fs  [B-8] gs  [B-40..B-9] pushad image
;   (EAX of the pushad image at B-12)
_vxd_raw_call proc far
        push    bp
        mov     bp,sp
        push    ds
        push    es
        push    fs
        push    gs
        pushad

        lds     si,[bp+10]              ; DS:SI = r
        mov     eax,[si]
        mov     ebx,[si+4]
        mov     ecx,[si+8]
        mov     edx,[si+12]
        mov     edi,[si+20]
        mov     es,[si+24]
        mov     esi,[si+16]
        mov     ds,[bp-2]               ; back to the caller's DS
        call    dword ptr [bp+6]

        ; the API may change BP, so address the frame from SP (= B-40)
        pushf
        push    es
        push    esi
        mov     bp,sp                   ; BP = B-48
        lds     si,[bp+58]              ; r (B+10)
        mov     [si],eax
        mov     [si+4],ebx
        mov     [si+8],ecx
        mov     [si+12],edx
        mov     [si+20],edi
        mov     eax,[bp]                ; ESI
        mov     [si+16],eax
        mov     ax,[bp+4]               ; ES
        mov     [si+24],ax
        mov     ax,[bp+6]               ; FLAGS
        mov     [si+26],ax
        and     eax,1                   ; carry flag = return value
        mov     [bp+36],eax             ; EAX of the pushad image (B-12)
        add     sp,8

        popad
        pop     gs
        pop     fs
        pop     es
        pop     ds
        pop     bp
        ret
_vxd_raw_call endp

VXDCALL_TEXT ends
        end
