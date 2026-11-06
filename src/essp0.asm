; essprobe's window on the DMA controllers under EMM386: a handler that
; EMM386 calls at ring 0 for a trapped read of one port (int 2Fh 4A15h),
; which does the accesses of a request list on the real ports.
;
; Notes:
;
; EMM386 traps a V86 program's accesses of the DMA controllers and
; programs the 8237 itself, so what reaches the 8237, and when, can only
; be seen past it: at ring 0 no access is trapped. essprobe copies this
; block into memory of its own, registers the trap for Audio_Base+Dh,
; which the chip doesn't decode, through the table here, and each read of
; that port runs the list once. EMM386 calls the handler with a 16-bit
; far call, CS and DS selectors of the block as long as the DI of the
; 4A15h call, interrupts off and the direction flag set, DX = the port,
; CL = the direction (0 in, else out) and AL = the byte. AL comes back,
; and CF set tells EMM386 to do the access itself; every other register
; comes back as it was. The block belongs to DOS while the trap is in, so
; that a program that ends without removing it leaves no handler in freed
; memory, and it passes every access unless it is armed; a later run finds
; it by its signature and removes the trap (4A15h with BX = 1 and the
; handle in SI). Built by tools/build_essp0.py into src/essp0.h.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

        bits    16
        org     0

ESSP0_NREQ      equ 32          ; the request list's entries
ESSP0_ARMED     equ 0x5A        ; armed while the program runs
ESSP0_DONE      equ 0xA5        ; the read's answer when the list ran
ESSP0_IN        equ 1           ; an entry: in, its byte comes back
ESSP0_OUT       equ 2           ; out of its byte; 0 ends the list

signature:      db "ESSP0", 0   ; a later run's search
handle:         dw 0            ; the trap's handle, from 4A15h
armed:          db 0            ; ESSP0_ARMED, else every access passes
count:          db 0            ; the entries the last read ran
table:          dw 0            ; the dispatch table: the port, filled in
                dw handler      ; by the program, and its handler
req:            times ESSP0_NREQ * 4 db 0   ; kind, port, byte

handler:
        cmp     byte [armed],ESSP0_ARMED
        jne     .pass
        push    cx
        push    dx
        push    si
        mov     si,req
        xor     cx,cx
.next:  cmp     cl,ESSP0_NREQ
        jae     .done
        mov     al,[si]
        mov     dx,[si+1]
        cmp     al,ESSP0_IN
        je      .in
        cmp     al,ESSP0_OUT
        jne     .done
        mov     al,[si+3]
        out     dx,al
        jmp     short .step
.in:    in      al,dx
        mov     [si+3],al
.step:  add     si,4
        inc     cx
        jmp     short .next
.done:  mov     [count],cl
        pop     si
        pop     dx
        pop     cx
        mov     al,ESSP0_DONE
        clc
        retf
.pass:  stc
        retf

ESSP0_SIZE      equ $
