; Segment 2 of ESFM.DRV: code, 84 bytes, flags 0D40h.
; ESS's driver code, disassembled with tools/ne2asm.py.

WEP:
        mov ax,ds                                       ; 0000
        nop                                             ; 0002
        inc bp                                          ; 0003
        push bp                                         ; 0004
        mov_ bp,sp                                      ; 0005
        push ds                                         ; 0007
        mov ds,ax                                       ; 0008
        mov cx,ds                                       ; 000A
        lar ax,cx                                       ; 000C
        jnz short L2_002E                               ; 000F
        and ax,0x8000                                   ; 0011
        jz short L2_002E                                ; 0014
        mov al,[0x68]                                   ; 0016
        or_ al,al                                       ; 0019
        jz short L2_002E                                ; 001B
        push word [bp+0x6]                              ; 001D
        callf L2_003C, R2_0023, 0xFFFF                  ; 0020 far seg2
        push ax                                         ; 0025
        callf L1_1A50, R2_0029, 0xFFFF                  ; 0026 far seg1
        pop ax                                          ; 002B
        jmp short L2_0031                               ; 002C

L2_002E:
        mov ax,0x1                                      ; 002E

L2_0031:
        sub bp,byte +0x2                                ; 0031
        mov_ sp,bp                                      ; 0034
        pop ds                                          ; 0036
        pop bp                                          ; 0037
        dec bp                                          ; 0038
        retf 0x2                                        ; 0039

L2_003C:
        mov ax,ds                                       ; 003C
        nop                                             ; 003E
        inc bp                                          ; 003F
        push bp                                         ; 0040
        mov_ bp,sp                                      ; 0041
        push ds                                         ; 0043
        mov ds,ax                                       ; 0044
        mov ax,0x1                                      ; 0046
        sub bp,byte +0x2                                ; 0049
        mov_ sp,bp                                      ; 004C
        pop ds                                          ; 004E
        pop bp                                          ; 004F
        dec bp                                          ; 0050
        retf 0x2                                        ; 0051

seg2_data_end:

; relocation table
        dw (seg2_rel_end - seg2_rel_start) / 8
seg2_rel_start:
        reloc 2, 0, R2_0029, 0x0001, 0x0000             ; seg1
        reloc 2, 0, R2_0023, 0x0002, 0x0000             ; seg2
seg2_rel_end:
seg2_end:
