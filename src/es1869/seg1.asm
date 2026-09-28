; segment 1: code, 9832 bytes, flags 0D40h

        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 0000

L1_0010:
        push bp                                         ; 0010
        mov_ bp,sp                                      ; 0011
        push si                                         ; 0013
        mov si,[bp+0xc]                                 ; 0014
        cmp word [si+0xe],byte +0x0                     ; 0017
        jz short L1_0041                                ; 001B
        push word [si+0x4]                              ; 001D
        push word [si+0x2]                              ; 0020
        push word [si+0xe]                              ; 0023
        push word [si+0xa]                              ; 0026
        push word [bp+0xa]                              ; 0029
        push word [si+0x8]                              ; 002C
        push word [si+0x6]                              ; 002F
        push word [bp+0x8]                              ; 0032
        push word [bp+0x6]                              ; 0035
        sub_ ax,ax                                      ; 0038
        push ax                                         ; 003A
        push ax                                         ; 003B
        callp R1_003D, 0xFFFF, 0x0000                   ; 003C MMSYSTEM.DriverCallback

L1_0041:
        pop si                                          ; 0041
        mov_ sp,bp                                      ; 0042
        pop bp                                          ; 0044
        retf 0x8                                        ; 0045

L1_0048:
        push bp                                         ; 0048
        mov_ bp,sp                                      ; 0049
        push si                                         ; 004B
        mov si,[bp+0x6]                                 ; 004C
        mov es,[bp+0x8]                                 ; 004F
        or byte [es:si+0x10],0x1                        ; 0052
        and byte [es:si+0x10],0xef                      ; 0057
        and byte [es:si+0x13],0x7f                      ; 005C
        sub_ ax,ax                                      ; 0061
        mov [es:si+0x1a],ax                             ; 0063
        mov [es:si+0x18],ax                             ; 0067
        push word [es:si+0x1c]                          ; 006B
        mov ax,0x3bd                                    ; 006F
        push ax                                         ; 0072
        push es                                         ; 0073
        push si                                         ; 0074
        push cs                                         ; 0075
        call L1_0010                                    ; 0076
        pop si                                          ; 0079
        mov_ sp,bp                                      ; 007A
        pop bp                                          ; 007C
        retf 0x4                                        ; 007D

L1_0080:
        push bp                                         ; 0080
        mov_ bp,sp                                      ; 0081
        push si                                         ; 0083
        mov si,[bp+0x10]                                ; 0084
        test byte [si+0x2a],0x40                        ; 0087
        jz short L1_00A7                                ; 008B
        mov ax,[bp+0x8]                                 ; 008D
        sub ax,[si+0xf5]                                ; 0090
        add_ ax,ax                                      ; 0094
        add ax,[si+0xf5]                                ; 0096
        mov dx,[si+0xf7]                                ; 009A
        mov [bp+0x8],ax                                 ; 009E
        mov [bp+0xa],dx                                 ; 00A1
        shl word [bp+0x6],1                             ; 00A4

L1_00A7:
        mov al,[bp+0xc]                                 ; 00A7
        and ax,strict word 0x3                          ; 00AA
        sub_ dx,dx                                      ; 00AD
        dec ax                                          ; 00AF
        jz short L1_00C4                                ; 00B0
        dec ax                                          ; 00B2
        dec ax                                          ; 00B3
        jz short L1_00C4                                ; 00B4
        push word [bp+0xa]                              ; 00B6
        push word [bp+0x8]                              ; 00B9
        push word [bp+0x6]                              ; 00BC
        mov ax,0x8080                                   ; 00BF
        jmp short L1_00CF                               ; 00C2

L1_00C4:
        push word [bp+0xa]                              ; 00C4
        push word [bp+0x8]                              ; 00C7
        push word [bp+0x6]                              ; 00CA
        xor_ ax,ax                                      ; 00CD

L1_00CF:
        push ax                                         ; 00CF
        call L1_1C25                                    ; 00D0
        pop si                                          ; 00D3
        mov_ sp,bp                                      ; 00D4
        pop bp                                          ; 00D6
        retf 0xc                                        ; 00D7

L1_00DA:
        push bp                                         ; 00DA
        mov_ bp,sp                                      ; 00DB
        sub sp,byte +0x4                                ; 00DD
        push si                                         ; 00E0
        mov si,[bp+0x6]                                 ; 00E1

L1_00E4:
        mov ax,[si+0xb0]                                ; 00E4
        mov dx,[si+0xb2]                                ; 00E8
        mov_ cx,ax                                      ; 00EC
        mov [bp-0x2],dx                                 ; 00EE
        or_ dx,ax                                       ; 00F1
        jz short L1_0113                                ; 00F3
        les bx,[si+0xb0]                                ; 00F5
        mov ax,[es:bx+0x18]                             ; 00F9
        mov dx,[es:bx+0x1a]                             ; 00FD
        mov [si+0xb0],ax                                ; 0101
        mov [si+0xb2],dx                                ; 0105
        push word [bp-0x2]                              ; 0109
        push cx                                         ; 010C
        push cs                                         ; 010D
        call L1_0048                                    ; 010E
        jmp short L1_00E4                               ; 0111

L1_0113:
        pop si                                          ; 0113
        mov_ sp,bp                                      ; 0114
        pop bp                                          ; 0116
        retf 0x2                                        ; 0117

L1_011A:
        push bp                                         ; 011A
        mov_ bp,sp                                      ; 011B
        sub sp,byte +0x8                                ; 011D
        push di                                         ; 0120
        push si                                         ; 0121
        mov cx,[bp+0x4]                                 ; 0122
        xor_ ax,ax                                      ; 0125
        cwd                                             ; 0127
        mov_ bx,ax                                      ; 0128
        mov_ di,cx                                      ; 012A
        mov [bp-0x2],dx                                 ; 012C
        mov ax,[di+0xb0]                                ; 012F
        mov dx,[di+0xb2]                                ; 0133
        mov_ si,ax                                      ; 0137
        mov [bp-0x6],dx                                 ; 0139
        or_ dx,ax                                       ; 013C
        jz short L1_01B0                                ; 013E

L1_0140:
        mov es,[bp-0x6]                                 ; 0140
        test word [es:si+0x12],0x8000                   ; 0143
        jnz short L1_0168                               ; 0149
        or byte [es:si+0x13],0x80                       ; 014B
        mov_ bx,ax                                      ; 0150
        mov [bp-0x2],es                                 ; 0152
        mov ax,[es:si+0x18]                             ; 0155
        mov dx,[es:si+0x1a]                             ; 0159
        mov_ si,ax                                      ; 015D
        mov [bp-0x6],dx                                 ; 015F
        or_ dx,ax                                       ; 0162
        jnz short L1_0140                               ; 0164
        jmp short L1_01B0                               ; 0166

L1_0168:
        mov ax,[bp-0x2]                                 ; 0168
        or_ ax,bx                                       ; 016B
        jz short L1_017E                                ; 016D
        mov es,[bp-0x2]                                 ; 016F
        sub_ ax,ax                                      ; 0172
        mov [es:bx+0x1a],ax                             ; 0174
        mov [es:bx+0x18],ax                             ; 0178
        jmp short L1_018A                               ; 017C

L1_017E:
        mov_ bx,cx                                      ; 017E
        sub_ ax,ax                                      ; 0180
        mov [bx+0xb2],ax                                ; 0182
        mov [bx+0xb0],ax                                ; 0186

L1_018A:
        mov ax,[bp-0x6]                                 ; 018A
        mov_ cx,si                                      ; 018D
        mov [bp-0x2],ax                                 ; 018F
        or_ ax,si                                       ; 0192
        jz short L1_01B0                                ; 0194
        mov es,[bp-0x6]                                 ; 0196
        mov ax,[es:si+0x18]                             ; 0199
        mov dx,[es:si+0x1a]                             ; 019D
        mov_ si,ax                                      ; 01A1
        mov [bp-0x6],dx                                 ; 01A3
        push word [bp-0x2]                              ; 01A6
        push cx                                         ; 01A9
        push cs                                         ; 01AA
        call L1_0048                                    ; 01AB
        jmp short L1_018A                               ; 01AE

L1_01B0:
        pop si                                          ; 01B0
        pop di                                          ; 01B1
        mov_ sp,bp                                      ; 01B2
        pop bp                                          ; 01B4
        ret 0x2                                         ; 01B5

L1_01B8:
        push bp                                         ; 01B8
        mov_ bp,sp                                      ; 01B9
        sub sp,byte +0x28                               ; 01BB
        push di                                         ; 01BE
        push si                                         ; 01BF
        mov ax,[bp+0x8]                                 ; 01C0
        or ax,[bp+0x6]                                  ; 01C3
        jz short L1_01E4                                ; 01C6
        mov bx,[bp+0x6]                                 ; 01C8
        sub_ ah,ah                                      ; 01CB
        db 0x8A, 0x87, 0x1C, 0x00                       ; 01CD mov al,[bx+0x1c]
        mov [bp+0x6],ax                                 ; 01D1
        mov word [bp+0x8],0x0                           ; 01D4
        mov cl,0x2                                      ; 01D9
        shr word [bp+0xa],cl                            ; 01DB
        cmp word [bp+0xa],byte +0x0                     ; 01DE
        jnz short L1_01E7                               ; 01E2

L1_01E4:
        jmp near L1_04A4                                ; 01E4

L1_01E7:
        mov ax,0x2                                      ; 01E7
        cwd                                             ; 01EA
        push dx                                         ; 01EB
        push ax                                         ; 01EC
        push word [bp+0x8]                              ; 01ED
        push word [bp+0x6]                              ; 01F0
        callf L1_234A, R1_01F6, R1_0274                 ; 01F3 far seg1
        mov cx,0xa                                      ; 01F8
        xor_ bx,bx                                      ; 01FB
        sub_ cx,ax                                      ; 01FD
        sbb_ bx,dx                                      ; 01FF
        mov [bp-0x12],cx                                ; 0201
        mov [bp-0x10],bx                                ; 0204
        mov si,[bp+0xc]                                 ; 0207

L1_020A:
        cmp word [bp+0x4],byte +0x0                     ; 020A
        jz short L1_022C                                ; 020E
        mov es,[bp+0xe]                                 ; 0210
        mov al,[es:si]                                  ; 0213
        sub_ ah,ah                                      ; 0216
        mov ch,[es:si+0x2]                              ; 0218
        sub_ cl,cl                                      ; 021C
        mov_ di,cx                                      ; 021E
        add_ di,ax                                      ; 0220
        mov al,[es:si+0x4]                              ; 0222
        mov ah,[es:si+0x6]                              ; 0226
        jmp short L1_0236                               ; 022A

L1_022C:
        mov es,[bp+0xe]                                 ; 022C
        mov di,[es:si]                                  ; 022F
        mov ax,[es:si+0x2]                              ; 0232

L1_0236:
        mov [bp-0x2],ax                                 ; 0236
        cmp_ ax,di                                      ; 0239
        jnz short L1_024C                               ; 023B
        mov_ ax,di                                      ; 023D
        cwd                                             ; 023F
        and dx,byte +0x3                                ; 0240
        add_ ax,dx                                      ; 0243
        mov cx,0x2                                      ; 0245
        sar ax,cl                                       ; 0248
        jmp short L1_0256                               ; 024A

L1_024C:
        mov_ ax,di                                      ; 024C
        sub ax,[bp-0x2]                                 ; 024E
        cwd                                             ; 0251
        sub_ ax,dx                                      ; 0252
        sar ax,1                                        ; 0254

L1_0256:
        mov [bp-0x4],ax                                 ; 0256
        cwd                                             ; 0259
        mov [bp-0xe],ax                                 ; 025A
        mov [bp-0xc],dx                                 ; 025D
        mov cx,0x86a0                                   ; 0260
        mov bx,0x1                                      ; 0263
        push bx                                         ; 0266
        push cx                                         ; 0267
        mov cx,0x51                                     ; 0268
        xor_ bx,bx                                      ; 026B
        push bx                                         ; 026D
        push cx                                         ; 026E
        push dx                                         ; 026F
        push ax                                         ; 0270
        callf L1_23E4, R1_0274, R1_027B                 ; 0271 far seg1
        push dx                                         ; 0276
        push ax                                         ; 0277
        callf L1_234A, R1_027B, R1_029A                 ; 0278 far seg1
        mov cx,0x3e8                                    ; 027D
        xor_ bx,bx                                      ; 0280
        push bx                                         ; 0282
        push cx                                         ; 0283
        mov cx,0x51                                     ; 0284
        push bx                                         ; 0287
        push cx                                         ; 0288
        push word [0x2e]                                ; 0289
        push word [0x2c]                                ; 028D
        mov [bp-0x16],ax                                ; 0291
        mov [bp-0x14],dx                                ; 0294
        callf L1_23E4, R1_029A, R1_02A1                 ; 0297 far seg1
        push dx                                         ; 029C
        push ax                                         ; 029D
        callf L1_234A, R1_02A1, R1_02CA                 ; 029E far seg1
        mov cx,[bp-0x16]                                ; 02A3
        mov bx,[bp-0x14]                                ; 02A6
        sub_ cx,ax                                      ; 02A9
        sbb_ bx,dx                                      ; 02AB
        mov ax,0x2710                                   ; 02AD
        cwd                                             ; 02B0
        push dx                                         ; 02B1
        push ax                                         ; 02B2
        mov ax,0x1f76                                   ; 02B3
        cwd                                             ; 02B6
        push dx                                         ; 02B7
        push ax                                         ; 02B8
        push word [0x3a]                                ; 02B9
        push word [0x38]                                ; 02BD
        mov [bp-0x1a],cx                                ; 02C1
        mov [bp-0x18],bx                                ; 02C4
        callf L1_23E4, R1_02CA, R1_02D1                 ; 02C7 far seg1
        push dx                                         ; 02CC
        push ax                                         ; 02CD
        callf L1_234A, R1_02D1, R1_02FA                 ; 02CE far seg1
        mov cx,[bp-0x1a]                                ; 02D3
        mov bx,[bp-0x18]                                ; 02D6
        sub_ cx,ax                                      ; 02D9
        sbb_ bx,dx                                      ; 02DB
        mov ax,0x2710                                   ; 02DD
        cwd                                             ; 02E0
        push dx                                         ; 02E1
        push ax                                         ; 02E2
        mov ax,0x464b                                   ; 02E3
        cwd                                             ; 02E6
        push dx                                         ; 02E7
        push ax                                         ; 02E8
        push word [0x3e]                                ; 02E9
        push word [0x3c]                                ; 02ED
        mov [bp-0x1e],cx                                ; 02F1
        mov [bp-0x1c],bx                                ; 02F4
        callf L1_23E4, R1_02FA, R1_0301                 ; 02F7 far seg1
        push dx                                         ; 02FC
        push ax                                         ; 02FD
        callf L1_234A, R1_0301, R1_0355                 ; 02FE far seg1
        add ax,[bp-0x1e]                                ; 0303
        adc dx,[bp-0x1c]                                ; 0306
        mov [0x40],ax                                   ; 0309
        mov [0x42],dx                                   ; 030C
        or_ dx,dx                                       ; 0310
        jl short L1_0329                                ; 0312
        jg short L1_031B                                ; 0314
        cmp ax,0x7fff                                   ; 0316
        jna short L1_0329                               ; 0319

L1_031B:
        mov word [0x40],0x7fff                          ; 031B
        mov word [0x42],0x0                             ; 0321
        jmp short L1_0341                               ; 0327

L1_0329:
        cmp dx,byte -0x1                                ; 0329
        jg short L1_0341                                ; 032C
        jl short L1_0335                                ; 032E
        cmp ax,0x8000                                   ; 0330
        jnc short L1_0341                               ; 0333

L1_0335:
        mov word [0x40],0x8000                          ; 0335
        mov word [0x42],0xffff                          ; 033B

L1_0341:
        mov_ ax,di                                      ; 0341
        mov cx,0xa                                      ; 0343
        cwd                                             ; 0346
        idiv cx                                         ; 0347
        cwd                                             ; 0349
        push dx                                         ; 034A
        push ax                                         ; 034B
        push word [bp-0x10]                             ; 034C
        push word [bp-0x12]                             ; 034F
        callf L1_23E4, R1_0355, R1_036E                 ; 0352 far seg1
        push word [0x42]                                ; 0357
        push word [0x40]                                ; 035B
        push word [bp+0x8]                              ; 035F
        push word [bp+0x6]                              ; 0362
        mov [bp-0x22],ax                                ; 0365
        mov [bp-0x20],dx                                ; 0368
        callf L1_23E4, R1_036E, R1_03A3                 ; 036B far seg1
        add_ ax,ax                                      ; 0370
        adc_ dx,dx                                      ; 0372
        mov_ cx,ax                                      ; 0374
        mov_ bx,dx                                      ; 0376
        add ax,[bp-0x22]                                ; 0378
        adc dx,[bp-0x20]                                ; 037B
        mov [bp-0xa],ax                                 ; 037E
        mov [bp-0x8],dx                                 ; 0381
        mov ax,[bp-0x2]                                 ; 0384
        mov dx,0xa                                      ; 0387
        mov [bp-0x24],dx                                ; 038A
        cwd                                             ; 038D
        idiv word [bp-0x24]                             ; 038E
        cwd                                             ; 0391
        push dx                                         ; 0392
        push ax                                         ; 0393
        push word [bp-0x10]                             ; 0394
        push word [bp-0x12]                             ; 0397
        mov [bp-0x28],cx                                ; 039A
        mov [bp-0x26],bx                                ; 039D
        callf L1_23E4, R1_03A3, 0xFFFF                  ; 03A0 far seg1
        sub ax,[bp-0x28]                                ; 03A5
        sbb dx,[bp-0x26]                                ; 03A8
        mov [bp-0x6],ax                                 ; 03AB
        mov [bp-0x4],dx                                 ; 03AE
        cmp word [bp-0x8],byte +0x0                     ; 03B1
        jl short L1_03CC                                ; 03B5
        jg short L1_03C0                                ; 03B7
        cmp word [bp-0xa],0x7fff                        ; 03B9
        jna short L1_03CC                               ; 03BE

L1_03C0:
        mov word [bp-0xa],0x7fff                        ; 03C0
        mov word [bp-0x8],0x0                           ; 03C5
        jmp short L1_03E5                               ; 03CA

L1_03CC:
        cmp word [bp-0x8],byte -0x1                     ; 03CC
        jg short L1_03E5                                ; 03D0
        jl short L1_03DB                                ; 03D2
        cmp word [bp-0xa],0x8000                        ; 03D4
        jnc short L1_03E5                               ; 03D9

L1_03DB:
        mov word [bp-0xa],0x8000                        ; 03DB
        mov word [bp-0x8],0xffff                        ; 03E0

L1_03E5:
        or_ dx,dx                                       ; 03E5
        jl short L1_03FC                                ; 03E7
        jg short L1_03F0                                ; 03E9
        cmp ax,0x7fff                                   ; 03EB
        jna short L1_03FC                               ; 03EE

L1_03F0:
        mov word [bp-0x6],0x7fff                        ; 03F0
        mov word [bp-0x4],0x0                           ; 03F5
        jmp short L1_0412                               ; 03FA

L1_03FC:
        cmp dx,byte -0x1                                ; 03FC
        jg short L1_0412                                ; 03FF
        jl short L1_0408                                ; 0401
        cmp ax,0x8000                                   ; 0403
        jnc short L1_0412                               ; 0406

L1_0408:
        mov word [bp-0x6],0x8000                        ; 0408
        mov word [bp-0x4],0xffff                        ; 040D

L1_0412:
        mov ax,0xa                                      ; 0412
        cwd                                             ; 0415
        push dx                                         ; 0416
        push ax                                         ; 0417
        push word [bp-0xc]                              ; 0418
        push word [bp-0xe]                              ; 041B
        callf L1_234A, R1_0421, R1_045D                 ; 041E far seg1
        mov [0x2c],ax                                   ; 0423
        mov [0x2e],dx                                   ; 0426
        mov ax,[0x3c]                                   ; 042A
        mov dx,[0x3e]                                   ; 042D
        mov [0x38],ax                                   ; 0431
        mov [0x3a],dx                                   ; 0434
        mov ax,[0x40]                                   ; 0438
        mov dx,[0x42]                                   ; 043B
        mov [0x3c],ax                                   ; 043F
        mov [0x3e],dx                                   ; 0442
        cmp word [bp+0x4],byte +0x0                     ; 0446
        jz short L1_0483                                ; 044A
        mov ax,[bp-0xa]                                 ; 044C
        mov es,[bp+0xe]                                 ; 044F
        mov [es:si],ax                                  ; 0452
        mov dx,[bp-0x8]                                 ; 0455
        mov cl,0x8                                      ; 0458
        callf L1_2422, R1_045D, R1_0472                 ; 045A far seg1
        mov [es:si+0x2],ax                              ; 045F
        mov ax,[bp-0x6]                                 ; 0463
        mov [es:si+0x4],ax                              ; 0466
        mov dx,[bp-0x4]                                 ; 046A
        mov cl,0x8                                      ; 046D
        callf L1_2422, R1_0472, R1_04EA                 ; 046F far seg1
        mov [es:si+0x6],ax                              ; 0474
        cmp word [bp+0xa],byte +0x1                     ; 0478
        jna short L1_049C                               ; 047C
        add si,byte +0x8                                ; 047E
        jmp short L1_049C                               ; 0481

L1_0483:
        mov ax,[bp-0xa]                                 ; 0483
        mov es,[bp+0xe]                                 ; 0486
        mov [es:si],ax                                  ; 0489
        mov ax,[bp-0x6]                                 ; 048C
        mov [es:si+0x2],ax                              ; 048F
        cmp word [bp+0xa],byte +0x1                     ; 0493
        jna short L1_049C                               ; 0497
        add si,byte +0x4                                ; 0499

L1_049C:
        dec word [bp+0xa]                               ; 049C
        jz short L1_04A4                                ; 049F
        jmp near L1_020A                                ; 04A1

L1_04A4:
        pop si                                          ; 04A4
        pop di                                          ; 04A5
        mov_ sp,bp                                      ; 04A6
        pop bp                                          ; 04A8
        ret 0xc                                         ; 04A9
        db 0x90, 0x90                                   ; 04AC

L1_04AE:
        push bp                                         ; 04AE
        mov_ bp,sp                                      ; 04AF
        sub sp,byte +0x28                               ; 04B1
        push di                                         ; 04B4
        push si                                         ; 04B5
        mov ax,[bp+0x8]                                 ; 04B6
        or ax,[bp+0x6]                                  ; 04B9
        jz short L1_04D8                                ; 04BC
        mov bx,[bp+0x6]                                 ; 04BE
        sub_ ah,ah                                      ; 04C1
        db 0x8A, 0x87, 0x1C, 0x00                       ; 04C3 mov al,[bx+0x1c]
        mov [bp+0x6],ax                                 ; 04C7
        mov word [bp+0x8],0x0                           ; 04CA
        shr word [bp+0xa],1                             ; 04CF
        cmp word [bp+0xa],byte +0x0                     ; 04D2
        jnz short L1_04DB                               ; 04D6

L1_04D8:
        jmp near L1_07A8                                ; 04D8

L1_04DB:
        mov ax,0x2                                      ; 04DB
        cwd                                             ; 04DE
        push dx                                         ; 04DF
        push ax                                         ; 04E0
        push word [bp+0x8]                              ; 04E1
        push word [bp+0x6]                              ; 04E4
        callf L1_234A, R1_04EA, R1_0573                 ; 04E7 far seg1
        mov cx,0xa                                      ; 04EC
        xor_ bx,bx                                      ; 04EF
        sub_ cx,ax                                      ; 04F1
        sbb_ bx,dx                                      ; 04F3
        mov [bp-0x12],cx                                ; 04F5
        mov [bp-0x10],bx                                ; 04F8
        mov si,[bp+0xc]                                 ; 04FB

L1_04FE:
        cmp word [bp+0x4],byte +0x0                     ; 04FE
        jz short L1_0517                                ; 0502
        mov es,[bp+0xe]                                 ; 0504
        mov al,[es:si]                                  ; 0507
        xor al,0x7f                                     ; 050A
        not al                                          ; 050C
        cbw                                             ; 050E
        mov_ di,ax                                      ; 050F
        mov al,[es:si+0x2]                              ; 0511
        jmp short L1_0528                               ; 0515

L1_0517:
        mov es,[bp+0xe]                                 ; 0517
        mov al,[es:si]                                  ; 051A
        xor al,0x7f                                     ; 051D
        not al                                          ; 051F
        cbw                                             ; 0521
        mov_ di,ax                                      ; 0522
        mov al,[es:si+0x1]                              ; 0524

L1_0528:
        xor al,0x7f                                     ; 0528
        not al                                          ; 052A
        cbw                                             ; 052C
        mov [bp-0x2],ax                                 ; 052D
        mov cl,0x8                                      ; 0530
        shl di,cl                                       ; 0532
        shl word [bp-0x2],cl                            ; 0534
        cmp [bp-0x2],di                                 ; 0537
        jnz short L1_054B                               ; 053A
        mov_ ax,di                                      ; 053C
        cwd                                             ; 053E
        and dx,byte +0x3                                ; 053F
        add_ ax,dx                                      ; 0542
        mov cx,0x2                                      ; 0544
        sar ax,cl                                       ; 0547
        jmp short L1_0555                               ; 0549

L1_054B:
        mov_ ax,di                                      ; 054B
        sub ax,[bp-0x2]                                 ; 054D
        cwd                                             ; 0550
        sub_ ax,dx                                      ; 0551
        sar ax,1                                        ; 0553

L1_0555:
        mov [bp-0x4],ax                                 ; 0555
        cwd                                             ; 0558
        mov [bp-0xe],ax                                 ; 0559
        mov [bp-0xc],dx                                 ; 055C
        mov cx,0x86a0                                   ; 055F
        mov bx,0x1                                      ; 0562
        push bx                                         ; 0565
        push cx                                         ; 0566
        mov cx,0x51                                     ; 0567
        xor_ bx,bx                                      ; 056A
        push bx                                         ; 056C
        push cx                                         ; 056D
        push dx                                         ; 056E
        push ax                                         ; 056F
        callf L1_23E4, R1_0573, R1_057A                 ; 0570 far seg1
        push dx                                         ; 0575
        push ax                                         ; 0576
        callf L1_234A, R1_057A, R1_0599                 ; 0577 far seg1
        mov cx,0x3e8                                    ; 057C
        xor_ bx,bx                                      ; 057F
        push bx                                         ; 0581
        push cx                                         ; 0582
        mov cx,0x51                                     ; 0583
        push bx                                         ; 0586
        push cx                                         ; 0587
        push word [0x46]                                ; 0588
        push word [0x44]                                ; 058C
        mov [bp-0x16],ax                                ; 0590
        mov [bp-0x14],dx                                ; 0593
        callf L1_23E4, R1_0599, R1_05A0                 ; 0596 far seg1
        push dx                                         ; 059B
        push ax                                         ; 059C
        callf L1_234A, R1_05A0, R1_05C9                 ; 059D far seg1
        mov cx,[bp-0x16]                                ; 05A2
        mov bx,[bp-0x14]                                ; 05A5
        sub_ cx,ax                                      ; 05A8
        sbb_ bx,dx                                      ; 05AA
        mov ax,0x2710                                   ; 05AC
        cwd                                             ; 05AF
        push dx                                         ; 05B0
        push ax                                         ; 05B1
        mov ax,0x1f76                                   ; 05B2
        cwd                                             ; 05B5
        push dx                                         ; 05B6
        push ax                                         ; 05B7
        push word [0x52]                                ; 05B8
        push word [0x50]                                ; 05BC
        mov [bp-0x1a],cx                                ; 05C0
        mov [bp-0x18],bx                                ; 05C3
        callf L1_23E4, R1_05C9, R1_05D0                 ; 05C6 far seg1
        push dx                                         ; 05CB
        push ax                                         ; 05CC
        callf L1_234A, R1_05D0, R1_05F9                 ; 05CD far seg1
        mov cx,[bp-0x1a]                                ; 05D2
        mov bx,[bp-0x18]                                ; 05D5
        sub_ cx,ax                                      ; 05D8
        sbb_ bx,dx                                      ; 05DA
        mov ax,0x2710                                   ; 05DC
        cwd                                             ; 05DF
        push dx                                         ; 05E0
        push ax                                         ; 05E1
        mov ax,0x464b                                   ; 05E2
        cwd                                             ; 05E5
        push dx                                         ; 05E6
        push ax                                         ; 05E7
        push word [0x56]                                ; 05E8
        push word [0x54]                                ; 05EC
        mov [bp-0x1e],cx                                ; 05F0
        mov [bp-0x1c],bx                                ; 05F3
        callf L1_23E4, R1_05F9, R1_0600                 ; 05F6 far seg1
        push dx                                         ; 05FB
        push ax                                         ; 05FC
        callf L1_234A, R1_0600, R1_0654                 ; 05FD far seg1
        add ax,[bp-0x1e]                                ; 0602
        adc dx,[bp-0x1c]                                ; 0605
        mov [0x58],ax                                   ; 0608
        mov [0x5a],dx                                   ; 060B
        or_ dx,dx                                       ; 060F
        jl short L1_0628                                ; 0611
        jg short L1_061A                                ; 0613
        cmp ax,0x7fff                                   ; 0615
        jna short L1_0628                               ; 0618

L1_061A:
        mov word [0x58],0x7fff                          ; 061A
        mov word [0x5a],0x0                             ; 0620
        jmp short L1_0640                               ; 0626

L1_0628:
        cmp dx,byte -0x1                                ; 0628
        jg short L1_0640                                ; 062B
        jl short L1_0634                                ; 062D
        cmp ax,0x8000                                   ; 062F
        jnc short L1_0640                               ; 0632

L1_0634:
        mov word [0x58],0x8000                          ; 0634
        mov word [0x5a],0xffff                          ; 063A

L1_0640:
        mov_ ax,di                                      ; 0640
        mov cx,0xa                                      ; 0642
        cwd                                             ; 0645
        idiv cx                                         ; 0646
        cwd                                             ; 0648
        push dx                                         ; 0649
        push ax                                         ; 064A
        push word [bp-0x10]                             ; 064B
        push word [bp-0x12]                             ; 064E
        callf L1_23E4, R1_0654, R1_066D                 ; 0651 far seg1
        push word [0x5a]                                ; 0656
        push word [0x58]                                ; 065A
        push word [bp+0x8]                              ; 065E
        push word [bp+0x6]                              ; 0661
        mov [bp-0x22],ax                                ; 0664
        mov [bp-0x20],dx                                ; 0667
        callf L1_23E4, R1_066D, R1_06A2                 ; 066A far seg1
        add_ ax,ax                                      ; 066F
        adc_ dx,dx                                      ; 0671
        mov_ cx,ax                                      ; 0673
        mov_ bx,dx                                      ; 0675
        add ax,[bp-0x22]                                ; 0677
        adc dx,[bp-0x20]                                ; 067A
        mov [bp-0xa],ax                                 ; 067D
        mov [bp-0x8],dx                                 ; 0680
        mov ax,[bp-0x2]                                 ; 0683
        mov dx,0xa                                      ; 0686
        mov [bp-0x24],dx                                ; 0689
        cwd                                             ; 068C
        idiv word [bp-0x24]                             ; 068D
        cwd                                             ; 0690
        push dx                                         ; 0691
        push ax                                         ; 0692
        push word [bp-0x10]                             ; 0693
        push word [bp-0x12]                             ; 0696
        mov [bp-0x28],cx                                ; 0699
        mov [bp-0x26],bx                                ; 069C
        callf L1_23E4, R1_06A2, R1_0720                 ; 069F far seg1
        sub ax,[bp-0x28]                                ; 06A4
        sbb dx,[bp-0x26]                                ; 06A7
        mov [bp-0x6],ax                                 ; 06AA
        mov [bp-0x4],dx                                 ; 06AD
        cmp word [bp-0x8],byte +0x0                     ; 06B0
        jl short L1_06CB                                ; 06B4
        jg short L1_06BF                                ; 06B6
        cmp word [bp-0xa],0x7fff                        ; 06B8
        jna short L1_06CB                               ; 06BD

L1_06BF:
        mov word [bp-0xa],0x7fff                        ; 06BF
        mov word [bp-0x8],0x0                           ; 06C4
        jmp short L1_06E4                               ; 06C9

L1_06CB:
        cmp word [bp-0x8],byte -0x1                     ; 06CB
        jg short L1_06E4                                ; 06CF
        jl short L1_06DA                                ; 06D1
        cmp word [bp-0xa],0x8000                        ; 06D3
        jnc short L1_06E4                               ; 06D8

L1_06DA:
        mov word [bp-0xa],0x8000                        ; 06DA
        mov word [bp-0x8],0xffff                        ; 06DF

L1_06E4:
        or_ dx,dx                                       ; 06E4
        jl short L1_06FB                                ; 06E6
        jg short L1_06EF                                ; 06E8
        cmp ax,0x7fff                                   ; 06EA
        jna short L1_06FB                               ; 06ED

L1_06EF:
        mov word [bp-0x6],0x7fff                        ; 06EF
        mov word [bp-0x4],0x0                           ; 06F4
        jmp short L1_0711                               ; 06F9

L1_06FB:
        cmp dx,byte -0x1                                ; 06FB
        jg short L1_0711                                ; 06FE
        jl short L1_0707                                ; 0700
        cmp ax,0x8000                                   ; 0702
        jnc short L1_0711                               ; 0705

L1_0707:
        mov word [bp-0x6],0x8000                        ; 0707
        mov word [bp-0x4],0xffff                        ; 070C

L1_0711:
        mov ax,0xa                                      ; 0711
        cwd                                             ; 0714
        push dx                                         ; 0715
        push ax                                         ; 0716
        push word [bp-0xc]                              ; 0717
        push word [bp-0xe]                              ; 071A
        callf L1_234A, R1_0720, R1_0750                 ; 071D far seg1
        mov [0x44],ax                                   ; 0722
        mov [0x46],dx                                   ; 0725
        mov ax,[0x54]                                   ; 0729
        mov dx,[0x56]                                   ; 072C
        mov [0x50],ax                                   ; 0730
        mov [0x52],dx                                   ; 0733
        mov ax,[0x58]                                   ; 0737
        mov dx,[0x5a]                                   ; 073A
        mov [0x54],ax                                   ; 073E
        mov [0x56],dx                                   ; 0741
        mov al,0x8                                      ; 0745
        push ax                                         ; 0747
        lea cx,[bp-0xa]                                 ; 0748
        push ss                                         ; 074B
        push cx                                         ; 074C
        callf L1_230A, R1_0750, R1_075D                 ; 074D far seg1
        mov al,0x8                                      ; 0752
        push ax                                         ; 0754
        lea ax,[bp-0x6]                                 ; 0755
        push ss                                         ; 0758
        push ax                                         ; 0759
        callf L1_230A, R1_075D, R1_01F6                 ; 075A far seg1
        cmp word [bp+0x4],byte +0x0                     ; 075F
        jz short L1_0784                                ; 0763
        mov al,[bp-0xa]                                 ; 0765
        xor al,0x80                                     ; 0768
        mov es,[bp+0xe]                                 ; 076A
        mov [es:si],al                                  ; 076D
        mov al,[bp-0x6]                                 ; 0770
        xor al,0x80                                     ; 0773
        mov [es:si+0x2],al                              ; 0775
        cmp word [bp+0xa],byte +0x1                     ; 0779
        jna short L1_07A0                               ; 077D
        add si,byte +0x4                                ; 077F
        jmp short L1_07A0                               ; 0782

L1_0784:
        mov al,[bp-0xa]                                 ; 0784
        xor al,0x80                                     ; 0787
        mov es,[bp+0xe]                                 ; 0789
        mov [es:si],al                                  ; 078C
        mov al,[bp-0x6]                                 ; 078F
        xor al,0x80                                     ; 0792
        mov [es:si+0x1],al                              ; 0794
        cmp word [bp+0xa],byte +0x1                     ; 0798
        jna short L1_07A0                               ; 079C
        inc si                                          ; 079E
        inc si                                          ; 079F

L1_07A0:
        dec word [bp+0xa]                               ; 07A0
        jz short L1_07A8                                ; 07A3
        jmp near L1_04FE                                ; 07A5

L1_07A8:
        pop si                                          ; 07A8
        pop di                                          ; 07A9
        mov_ sp,bp                                      ; 07AA
        pop bp                                          ; 07AC
        ret 0xc                                         ; 07AD
        db 0x90, 0x90                                   ; 07B0

L1_07B2:
        push bp                                         ; 07B2
        mov_ bp,sp                                      ; 07B3
        sub sp,byte +0xe                                ; 07B5
        push di                                         ; 07B8
        push si                                         ; 07B9
        mov di,[bp+0xe]                                 ; 07BA
        cmp word [bp+0x6],byte +0x0                     ; 07BD
        jz short L1_07D1                                ; 07C1
        mov ax,[di+0xb2]                                ; 07C3
        or ax,[di+0xb0]                                 ; 07C7
        jz short L1_07D1                                ; 07CB
        push di                                         ; 07CD
        call L1_011A                                    ; 07CE

L1_07D1:
        mov ax,[di+0xac]                                ; 07D1
        mov dx,[di+0xae]                                ; 07D5
        mov_ bx,ax                                      ; 07D9
        mov [bp-0xa],dx                                 ; 07DB
        mov word [bp-0xe],0x0                           ; 07DE
        test byte [di+0x2a],0x40                        ; 07E3
        jz short L1_0800                                ; 07E7
        mov ax,[bp+0xa]                                 ; 07E9
        sub ax,[di+0xf5]                                ; 07EC
        add_ ax,ax                                      ; 07F0
        add ax,[di+0xf5]                                ; 07F2
        mov dx,[di+0xf7]                                ; 07F6
        mov [bp+0xa],ax                                 ; 07FA
        mov [bp+0xc],dx                                 ; 07FD

L1_0800:
        mov ax,[bp-0xa]                                 ; 0800
        or_ ax,bx                                       ; 0803
        jnz short L1_080A                               ; 0805
        jmp near L1_0BE4                                ; 0807

L1_080A:
        cmp word [bp+0x8],byte +0x0                     ; 080A
        jnz short L1_0813                               ; 080E
        jmp near L1_0BA9                                ; 0810

L1_0813:
        mov [bp-0xc],bx                                 ; 0813

L1_0816:
        mov ax,[di+0xb6]                                ; 0816
        or ax,[di+0xb4]                                 ; 081A
        jnz short L1_0861                               ; 081E
        mov es,[bp-0xa]                                 ; 0820
        mov ax,[es:bx]                                  ; 0823
        mov dx,[es:bx+0x2]                              ; 0826
        mov [di+0xb4],ax                                ; 082A
        mov [di+0xb6],dx                                ; 082E
        mov ax,[es:bx+0x4]                              ; 0832
        mov dx,[es:bx+0x6]                              ; 0836
        mov [di+0xb8],ax                                ; 083A
        mov [di+0xba],dx                                ; 083E
        test byte [es:bx+0x10],0x4                      ; 0842
        jz short L1_0861                                ; 0847
        mov [di+0xbc],bx                                ; 0849
        mov [di+0xbe],es                                ; 084D
        mov ax,[es:bx+0x14]                             ; 0851
        mov dx,[es:bx+0x16]                             ; 0855
        mov [di+0xc0],ax                                ; 0859
        mov [di+0xc2],dx                                ; 085D

L1_0861:
        cmp word [di+0xc6],byte +0x0                    ; 0861
        jz short L1_0894                                ; 0866
        mov es,[bp-0xa]                                 ; 0868
        test byte [es:bx+0x10],0x4                      ; 086B
        jz short L1_0894                                ; 0870
        mov ax,[di+0xb8]                                ; 0872
        mov dx,[di+0xba]                                ; 0876
        cmp [es:bx+0x4],ax                              ; 087A
        jnz short L1_0894                               ; 087E
        cmp [es:bx+0x6],dx                              ; 0880
        jnz short L1_0894                               ; 0884
        sub_ ax,ax                                      ; 0886
        mov [di+0xc2],ax                                ; 0888
        mov [di+0xc0],ax                                ; 088C
        mov [di+0xc6],ax                                ; 0890

L1_0894:
        mov ax,[di+0xbe]                                ; 0894
        or ax,[di+0xbc]                                 ; 0898
        jz short L1_08B2                                ; 089C
        mov ax,[di+0xc2]                                ; 089E
        or ax,[di+0xc0]                                 ; 08A2
        jnz short L1_08B2                               ; 08A6
        sub_ ax,ax                                      ; 08A8
        mov [di+0xba],ax                                ; 08AA
        mov [di+0xb8],ax                                ; 08AE

L1_08B2:
        mov ax,[di+0xba]                                ; 08B2
        or ax,[di+0xb8]                                 ; 08B6
        jnz short L1_08BF                               ; 08BA
        jmp near L1_0A3A                                ; 08BC

L1_08BF:
        push di                                         ; 08BF
        push word [di+0xb6]                             ; 08C0
        push word [di+0xb4]                             ; 08C4
        mov ax,[bp+0x8]                                 ; 08C8
        sub ax,[bp-0xe]                                 ; 08CB
        sub_ dx,dx                                      ; 08CE
        cmp dx,[di+0xba]                                ; 08D0
        jc short L1_08E2                                ; 08D4
        ja short L1_08DE                                ; 08D6
        cmp ax,[di+0xb8]                                ; 08D8
        jna short L1_08E2                               ; 08DC

L1_08DE:
        mov ax,[di+0xb8]                                ; 08DE

L1_08E2:
        mov_ si,ax                                      ; 08E2
        sub_ ax,ax                                      ; 08E4
        push ax                                         ; 08E6
        push si                                         ; 08E7
        lea ax,[di+0xcc]                                ; 08E8
        push ds                                         ; 08EC
        push ax                                         ; 08ED
        xor_ al,al                                      ; 08EE
        push ax                                         ; 08F0
        callf L1_0E5A, R1_08F4, R1_0421                 ; 08F1 far seg1
        test byte [di+0x2a],0x40                        ; 08F6
        jnz short L1_08FF                               ; 08FA
        jmp near L1_0991                                ; 08FC

L1_08FF:
        push word [bp+0xc]                              ; 08FF
        push word [bp+0xa]                              ; 0902
        push word [di+0xb6]                             ; 0905
        push word [di+0xb4]                             ; 0909
        push si                                         ; 090D
        call L1_1B82                                    ; 090E
        mov [di+0xb4],ax                                ; 0911
        mov [di+0xb6],dx                                ; 0915
        test byte [di+0x2a],0x20                        ; 0919
        jz short L1_0987                                ; 091D
        mov ax,[di+0x107]                               ; 091F
        mov dx,[di+0x109]                               ; 0923
        mov [bp-0x8],ax                                 ; 0927
        mov [bp-0x6],dx                                 ; 092A
        mov ax,[di+0x26]                                ; 092D
        mov dx,[di+0x28]                                ; 0930
        mov_ bx,ax                                      ; 0933
        mov es,dx                                       ; 0935
        mov cx,[es:bx+0x2956]                           ; 0937
        or cx,[es:bx+0x2954]                            ; 093C
        jz short L1_0987                                ; 0941
        test byte [bp-0x8],0x2                          ; 0943
        jz short L1_0987                                ; 0947
        test byte [bp-0x8],0x1                          ; 0949
        jz short L1_096C                                ; 094D
        push word [bp+0xc]                              ; 094F
        push word [bp+0xa]                              ; 0952
        push si                                         ; 0955
        mov cl,0xc                                      ; 0956
        mov ax,[es:bx+0x295c]                           ; 0958
        shr ax,cl                                       ; 095D
        sub_ dx,dx                                      ; 095F
        push dx                                         ; 0961
        push ax                                         ; 0962
        mov ax,0x1                                      ; 0963
        push ax                                         ; 0966
        call L1_01B8                                    ; 0967
        jmp short L1_0987                               ; 096A

L1_096C:
        push word [bp+0xc]                              ; 096C
        push word [bp+0xa]                              ; 096F
        push si                                         ; 0972
        mov cl,0xc                                      ; 0973
        mov ax,[es:bx+0x295c]                           ; 0975
        shr ax,cl                                       ; 097A
        sub_ dx,dx                                      ; 097C
        push dx                                         ; 097E
        push ax                                         ; 097F
        mov ax,0x1                                      ; 0980
        push ax                                         ; 0983
        call L1_04AE                                    ; 0984

L1_0987:
        mov_ ax,si                                      ; 0987
        add_ ax,ax                                      ; 0989
        add [bp+0xa],ax                                 ; 098B
        jmp near L1_0A1A                                ; 098E

L1_0991:
        push word [bp+0xc]                              ; 0991
        push word [bp+0xa]                              ; 0994
        push word [di+0xb6]                             ; 0997
        push word [di+0xb4]                             ; 099B
        push si                                         ; 099F
        call L1_1AF4                                    ; 09A0
        mov [di+0xb4],ax                                ; 09A3
        mov [di+0xb6],dx                                ; 09A7
        test byte [di+0x2a],0x20                        ; 09AB
        jz short L1_0A17                                ; 09AF
        mov ax,[di+0x107]                               ; 09B1
        mov dx,[di+0x109]                               ; 09B5
        mov [bp-0x8],ax                                 ; 09B9
        mov [bp-0x6],dx                                 ; 09BC
        mov ax,[di+0x26]                                ; 09BF
        mov dx,[di+0x28]                                ; 09C2
        mov_ bx,ax                                      ; 09C5
        mov es,dx                                       ; 09C7
        mov cx,[es:bx+0x2956]                           ; 09C9
        or cx,[es:bx+0x2954]                            ; 09CE
        jz short L1_0A17                                ; 09D3
        test byte [bp-0x8],0x2                          ; 09D5
        jz short L1_0A17                                ; 09D9
        test byte [bp-0x8],0x1                          ; 09DB
        jz short L1_09FD                                ; 09DF
        push word [bp+0xc]                              ; 09E1
        push word [bp+0xa]                              ; 09E4
        push si                                         ; 09E7
        mov cl,0xc                                      ; 09E8
        mov ax,[es:bx+0x295c]                           ; 09EA
        shr ax,cl                                       ; 09EF
        sub_ dx,dx                                      ; 09F1
        push dx                                         ; 09F3
        push ax                                         ; 09F4
        xor_ ax,ax                                      ; 09F5
        push ax                                         ; 09F7
        call L1_01B8                                    ; 09F8
        jmp short L1_0A17                               ; 09FB

L1_09FD:
        push word [bp+0xc]                              ; 09FD
        push word [bp+0xa]                              ; 0A00
        push si                                         ; 0A03
        mov cl,0xc                                      ; 0A04
        mov ax,[es:bx+0x295c]                           ; 0A06
        shr ax,cl                                       ; 0A0B
        sub_ dx,dx                                      ; 0A0D
        push dx                                         ; 0A0F
        push ax                                         ; 0A10
        xor_ ax,ax                                      ; 0A11
        push ax                                         ; 0A13
        call L1_04AE                                    ; 0A14

L1_0A17:
        add [bp+0xa],si                                 ; 0A17

L1_0A1A:
        sub_ ax,ax                                      ; 0A1A
        sub [di+0xb8],si                                ; 0A1C
        sbb [di+0xba],ax                                ; 0A20
        add [di+0x49],si                                ; 0A24
        adc [di+0x4b],ax                                ; 0A27
        add [bp-0xe],si                                 ; 0A2A
        les bx,[bp-0xc]                                 ; 0A2D
        mov bx,[es:bx+0x1c]                             ; 0A30
        add [bx+0x10],si                                ; 0A34
        adc [bx+0x12],ax                                ; 0A37

L1_0A3A:
        mov ax,[di+0xba]                                ; 0A3A
        or ax,[di+0xb8]                                 ; 0A3E
        jz short L1_0A4A                                ; 0A42
        mov bx,[bp-0xc]                                 ; 0A44
        jmp near L1_0B84                                ; 0A47

L1_0A4A:
        les bx,[bp-0xc]                                 ; 0A4A
        test byte [es:bx+0x10],0x8                      ; 0A4D
        jnz short L1_0A57                               ; 0A52
        jmp near L1_0AFC                                ; 0A54

L1_0A57:
        mov ax,[di+0xc2]                                ; 0A57
        or ax,[di+0xc0]                                 ; 0A5B
        jnz short L1_0AD8                               ; 0A5F
        mov ax,[di+0xbc]                                ; 0A61
        mov dx,[di+0xbe]                                ; 0A65
        mov_ bx,ax                                      ; 0A69
        mov [bp-0x6],dx                                 ; 0A6B
        mov si,[bp-0xc]                                 ; 0A6E
        mov cx,[es:si+0x18]                             ; 0A71
        mov ax,[es:si+0x1a]                             ; 0A75
        mov [bp-0xc],cx                                 ; 0A79
        mov [bp-0xa],ax                                 ; 0A7C
        cmp_ cx,bx                                      ; 0A7F
        jnz short L1_0A87                               ; 0A81
        cmp_ ax,dx                                      ; 0A83
        jz short L1_0AC3                                ; 0A85

L1_0A87:
        mov cx,[bp+0xe]                                 ; 0A87

L1_0A8A:
        mov es,[bp-0x6]                                 ; 0A8A
        mov_ di,bx                                      ; 0A8D
        mov ax,[es:bx+0x18]                             ; 0A8F
        mov dx,[es:bx+0x1a]                             ; 0A93
        mov_ bx,ax                                      ; 0A97
        mov_ si,cx                                      ; 0A99
        mov [bp-0x6],dx                                 ; 0A9B
        mov ax,[si+0xb0]                                ; 0A9E
        mov dx,[si+0xb2]                                ; 0AA2
        mov [es:di+0x18],ax                             ; 0AA6
        mov [es:di+0x1a],dx                             ; 0AAA
        mov [si+0xb0],di                                ; 0AAE
        mov [si+0xb2],es                                ; 0AB2
        mov ax,[bp-0x6]                                 ; 0AB6
        cmp [bp-0xc],bx                                 ; 0AB9
        jnz short L1_0A8A                               ; 0ABC
        cmp [bp-0xa],ax                                 ; 0ABE
        jnz short L1_0A8A                               ; 0AC1

L1_0AC3:
        mov bx,[bp+0xe]                                 ; 0AC3
        sub_ ax,ax                                      ; 0AC6
        mov [bx+0xbe],ax                                ; 0AC8
        mov [bx+0xbc],ax                                ; 0ACC
        mov bx,[bp-0xc]                                 ; 0AD0

L1_0AD3:
        mov di,[bp+0xe]                                 ; 0AD3
        jmp short L1_0B32                               ; 0AD6

L1_0AD8:
        mov bx,[bp+0xe]                                 ; 0AD8
        sub word [bx+0xc0],byte +0x1                    ; 0ADB
        sbb word [bx+0xc2],byte +0x0                    ; 0AE0
        mov ax,[bx+0xbc]                                ; 0AE5
        mov dx,[bx+0xbe]                                ; 0AE9
        mov [bx+0xac],ax                                ; 0AED
        mov [bx+0xae],dx                                ; 0AF1
        mov_ bx,ax                                      ; 0AF5
        mov [bp-0xa],dx                                 ; 0AF7
        jmp short L1_0AD3                               ; 0AFA

L1_0AFC:
        mov_ cx,bx                                      ; 0AFC
        mov [bp-0x6],es                                 ; 0AFE
        mov ax,[es:bx+0x18]                             ; 0B01
        mov dx,[es:bx+0x1a]                             ; 0B05
        mov_ bx,ax                                      ; 0B09
        mov [bp-0xa],dx                                 ; 0B0B
        mov ax,[di+0xbe]                                ; 0B0E
        or ax,[di+0xbc]                                 ; 0B12
        jnz short L1_0B32                               ; 0B16
        mov ax,[di+0xb0]                                ; 0B18
        mov_ si,cx                                      ; 0B1C
        mov dx,[di+0xb2]                                ; 0B1E
        mov [es:si+0x18],ax                             ; 0B22
        mov [es:si+0x1a],dx                             ; 0B26
        mov [di+0xb0],cx                                ; 0B2A
        mov [di+0xb2],es                                ; 0B2E

L1_0B32:
        mov ax,[bp-0xa]                                 ; 0B32
        or_ ax,bx                                       ; 0B35
        jz short L1_0B97                                ; 0B37
        mov es,[bp-0xa]                                 ; 0B39
        mov ax,[es:bx]                                  ; 0B3C
        mov dx,[es:bx+0x2]                              ; 0B3F
        mov [di+0xb4],ax                                ; 0B43
        mov [di+0xb6],dx                                ; 0B47
        mov ax,[es:bx+0x4]                              ; 0B4B
        mov dx,[es:bx+0x6]                              ; 0B4F
        mov [di+0xb8],ax                                ; 0B53
        mov [di+0xba],dx                                ; 0B57
        mov ax,[di+0xbe]                                ; 0B5B
        or ax,[di+0xbc]                                 ; 0B5F
        jnz short L1_0B84                               ; 0B63
        test byte [es:bx+0x10],0x4                      ; 0B65
        jz short L1_0B84                                ; 0B6A
        mov [di+0xbc],bx                                ; 0B6C
        mov [di+0xbe],es                                ; 0B70
        mov ax,[es:bx+0x14]                             ; 0B74
        mov dx,[es:bx+0x16]                             ; 0B78
        mov [di+0xc0],ax                                ; 0B7C
        mov [di+0xc2],dx                                ; 0B80

L1_0B84:
        mov [bp-0xc],bx                                 ; 0B84
        mov ax,[bp+0x8]                                 ; 0B87
        cmp [bp-0xe],ax                                 ; 0B8A
        jnc short L1_0B92                               ; 0B8D
        jmp near L1_0816                                ; 0B8F

L1_0B92:
        mov di,[bp+0xe]                                 ; 0B92
        jmp short L1_0BA9                               ; 0B95

L1_0B97:
        sub_ ax,ax                                      ; 0B97
        mov [di+0xb6],ax                                ; 0B99
        mov [di+0xb4],ax                                ; 0B9D
        mov [di+0xba],ax                                ; 0BA1
        mov [di+0xb8],ax                                ; 0BA5

L1_0BA9:
        mov ax,[bp-0xa]                                 ; 0BA9
        mov [di+0xac],bx                                ; 0BAC
        mov [di+0xae],ax                                ; 0BB0
        or_ ax,bx                                       ; 0BB4
        jnz short L1_0BD2                               ; 0BB6
        mov ax,[di+0xbe]                                ; 0BB8
        or ax,[di+0xbc]                                 ; 0BBC
        jz short L1_0BD2                                ; 0BC0
        mov ax,[di+0xbc]                                ; 0BC2
        mov dx,[di+0xbe]                                ; 0BC6
        mov [di+0xac],ax                                ; 0BCA
        mov [di+0xae],dx                                ; 0BCE

L1_0BD2:
        cmp word [bp-0xe],byte +0x0                     ; 0BD2
        jnz short L1_0BE4                               ; 0BD6
        cmp word [di+0xe1],byte +0x0                    ; 0BD8
        jnz short L1_0BE4                               ; 0BDD
        push di                                         ; 0BDF
        push cs                                         ; 0BE0
        call L1_00DA                                    ; 0BE1

L1_0BE4:
        mov si,[bp+0x8]                                 ; 0BE4
        sub si,[bp-0xe]                                 ; 0BE7
        jz short L1_0C3F                                ; 0BEA
        test byte [di+0x2a],0x40                        ; 0BEC
        jz short L1_0C0C                                ; 0BF0
        mov ax,[bp+0xa]                                 ; 0BF2
        sub ax,[di+0xf5]                                ; 0BF5
        cwd                                             ; 0BF9
        sub_ ax,dx                                      ; 0BFA
        sar ax,1                                        ; 0BFC
        add ax,[di+0xf5]                                ; 0BFE
        mov dx,[di+0xf7]                                ; 0C02
        mov [bp+0xa],ax                                 ; 0C06
        mov [bp+0xc],dx                                 ; 0C09

L1_0C0C:
        mov ax,[bp+0xa]                                 ; 0C0C
        mov dx,[bp+0xc]                                 ; 0C0F
        mov [di+0xa4],ax                                ; 0C12
        mov [di+0xa6],dx                                ; 0C16
        mov [di+0xa8],si                                ; 0C1A
        push di                                         ; 0C1E
        push word [di+0x109]                            ; 0C1F
        push word [di+0x107]                            ; 0C23
        push dx                                         ; 0C27
        push ax                                         ; 0C28
        push si                                         ; 0C29
        push cs                                         ; 0C2A
        call L1_0080                                    ; 0C2B
        cmp byte [di+0x48],0x0                          ; 0C2E
        jz short L1_0C49                                ; 0C32
        mov_ ax,si                                      ; 0C34
        cwd                                             ; 0C36
        add [di+0x49],ax                                ; 0C37
        adc [di+0x4b],dx                                ; 0C3A
        jmp short L1_0C49                               ; 0C3D

L1_0C3F:
        sub_ ax,ax                                      ; 0C3F
        mov [di+0xa6],ax                                ; 0C41
        mov [di+0xa4],ax                                ; 0C45

L1_0C49:
        mov ax,[bp-0xe]                                 ; 0C49
        pop si                                          ; 0C4C
        pop di                                          ; 0C4D
        mov_ sp,bp                                      ; 0C4E
        pop bp                                          ; 0C50
        retf 0xa                                        ; 0C51
        db 0x90, 0x90                                   ; 0C54

L1_0C56:
        push bp                                         ; 0C56
        mov_ bp,sp                                      ; 0C57
        push si                                         ; 0C59
        mov si,[bp+0x6]                                 ; 0C5A
        mov es,[bp+0x8]                                 ; 0C5D
        test byte [es:si+0x10],0x1                      ; 0C60
        jnz short L1_0C7B                               ; 0C65
        or byte [es:si+0x10],0x1                        ; 0C67
        and byte [es:si+0x10],0xef                      ; 0C6C
        sub_ ax,ax                                      ; 0C71
        mov [es:si+0xa],ax                              ; 0C73
        mov [es:si+0x8],ax                              ; 0C77

L1_0C7B:
        push word [es:si+0x1c]                          ; 0C7B
        mov ax,0x3c0                                    ; 0C7F
        push ax                                         ; 0C82
        push es                                         ; 0C83
        push si                                         ; 0C84
        push cs                                         ; 0C85
        call L1_0010                                    ; 0C86
        pop si                                          ; 0C89
        mov_ sp,bp                                      ; 0C8A
        pop bp                                          ; 0C8C
        retf 0x4                                        ; 0C8D

L1_0C90:
        push bp                                         ; 0C90
        mov_ bp,sp                                      ; 0C91
        sub sp,byte +0x8                                ; 0C93
        push di                                         ; 0C96
        push si                                         ; 0C97
        mov si,[bp+0xa]                                 ; 0C98
        mov ax,[si+0x8e]                                ; 0C9B
        or ax,[si+0x8c]                                 ; 0C9F
        jnz short L1_0CAA                               ; 0CA3
        xor_ ax,ax                                      ; 0CA5
        jmp near L1_0E51                                ; 0CA7

L1_0CAA:
        mov word [bp-0x8],0x0                           ; 0CAA
        cmp byte [si+0x6],0x3                           ; 0CAF
        jna short L1_0CC9                               ; 0CB3
        mov ax,[bp+0x6]                                 ; 0CB5
        sub ax,[si+0x41]                                ; 0CB8
        add_ ax,ax                                      ; 0CBB
        add ax,[si+0x41]                                ; 0CBD
        mov dx,[si+0x43]                                ; 0CC0
        mov [bp+0x6],ax                                 ; 0CC3
        mov [bp+0x8],dx                                 ; 0CC6

L1_0CC9:
        cmp word [bp+0x4],byte +0x0                     ; 0CC9
        jnz short L1_0CD2                               ; 0CCD
        jmp near L1_0E4E                                ; 0CCF

L1_0CD2:
        mov ax,[si+0x92]                                ; 0CD2
        or ax,[si+0x90]                                 ; 0CD6
        jnz short L1_0D09                               ; 0CDA
        les bx,[si+0x8c]                                ; 0CDC
        mov ax,[es:bx]                                  ; 0CE0
        mov dx,[es:bx+0x2]                              ; 0CE3
        mov [si+0x90],ax                                ; 0CE7
        mov [si+0x92],dx                                ; 0CEB
        mov ax,[es:bx+0x4]                              ; 0CEF
        mov dx,[es:bx+0x6]                              ; 0CF3
        mov [si+0x94],ax                                ; 0CF7
        mov [si+0x96],dx                                ; 0CFB
        sub_ ax,ax                                      ; 0CFF
        mov [es:bx+0xa],ax                              ; 0D01
        mov [es:bx+0x8],ax                              ; 0D05

L1_0D09:
        mov ax,[bp+0x4]                                 ; 0D09
        sub ax,[bp-0x8]                                 ; 0D0C
        sub_ dx,dx                                      ; 0D0F
        cmp dx,[si+0x96]                                ; 0D11
        jc short L1_0D23                                ; 0D15
        ja short L1_0D1F                                ; 0D17
        cmp ax,[si+0x94]                                ; 0D19
        jna short L1_0D23                               ; 0D1D

L1_0D1F:
        mov ax,[si+0x94]                                ; 0D1F

L1_0D23:
        mov_ di,ax                                      ; 0D23
        or_ di,ax                                       ; 0D25
        jnz short L1_0D2C                               ; 0D27
        jmp near L1_0DC8                                ; 0D29

L1_0D2C:
        mov [bp-0x2],di                                 ; 0D2C
        cmp byte [si+0x6],0x3                           ; 0D2F
        jna short L1_0D79                               ; 0D33
        mov ax,[si+0x90]                                ; 0D35
        mov dx,[si+0x92]                                ; 0D39
        mov [bp-0x6],ax                                 ; 0D3D
        mov [bp-0x4],dx                                 ; 0D40
        push dx                                         ; 0D43
        push ax                                         ; 0D44
        push word [bp+0x8]                              ; 0D45
        push word [bp+0x6]                              ; 0D48
        push di                                         ; 0D4B
        call L1_1BD5                                    ; 0D4C
        mov [si+0x90],ax                                ; 0D4F
        mov [si+0x92],dx                                ; 0D53
        mov_ ax,di                                      ; 0D57
        add_ ax,di                                      ; 0D59
        add [bp+0x6],ax                                 ; 0D5B
        push si                                         ; 0D5E
        push word [bp-0x4]                              ; 0D5F
        push word [bp-0x6]                              ; 0D62
        sub_ ax,ax                                      ; 0D65
        push ax                                         ; 0D67
        push di                                         ; 0D68
        lea ax,[si+0x98]                                ; 0D69
        push ds                                         ; 0D6D
        push ax                                         ; 0D6E
        mov al,0x1                                      ; 0D6F
        push ax                                         ; 0D71
        callf L1_0E5A, R1_0D75, R1_0D90                 ; 0D72 far seg1
        jmp short L1_0DAF                               ; 0D77

L1_0D79:
        push si                                         ; 0D79
        push word [bp+0x8]                              ; 0D7A
        push word [bp+0x6]                              ; 0D7D
        sub_ ax,ax                                      ; 0D80
        push ax                                         ; 0D82
        push di                                         ; 0D83
        lea ax,[si+0x98]                                ; 0D84
        push ds                                         ; 0D88
        push ax                                         ; 0D89
        mov al,0x1                                      ; 0D8A
        push ax                                         ; 0D8C
        callf L1_0E5A, R1_0D90, R1_08F4                 ; 0D8D far seg1
        push word [si+0x92]                             ; 0D92
        push word [si+0x90]                             ; 0D96
        push word [bp+0x8]                              ; 0D9A
        push word [bp+0x6]                              ; 0D9D
        push di                                         ; 0DA0
        call L1_1B3C                                    ; 0DA1
        mov [si+0x90],ax                                ; 0DA4
        mov [si+0x92],dx                                ; 0DA8
        add [bp+0x6],di                                 ; 0DAC

L1_0DAF:
        sub_ ax,ax                                      ; 0DAF
        sub [si+0x94],di                                ; 0DB1
        sbb [si+0x96],ax                                ; 0DB5
        add [bp-0x8],di                                 ; 0DB9
        les bx,[si+0x8c]                                ; 0DBC
        add [es:bx+0x8],di                              ; 0DC0
        adc [es:bx+0xa],ax                              ; 0DC4

L1_0DC8:
        mov ax,[si+0x96]                                ; 0DC8
        or ax,[si+0x94]                                 ; 0DCC
        jnz short L1_0E2F                               ; 0DD0
        les bx,[si+0x8c]                                ; 0DD2
        mov ax,[es:bx+0x18]                             ; 0DD6
        mov dx,[es:bx+0x1a]                             ; 0DDA
        mov_ di,ax                                      ; 0DDE
        mov [bp-0x2],dx                                 ; 0DE0
        or byte [es:bx+0x10],0x1                        ; 0DE3
        and byte [es:bx+0x10],0xef                      ; 0DE8
        push es                                         ; 0DED
        push bx                                         ; 0DEE
        push cs                                         ; 0DEF
        call L1_0C56                                    ; 0DF0
        mov ax,[bp-0x2]                                 ; 0DF3
        mov [si+0x8c],di                                ; 0DF6
        mov [si+0x8e],ax                                ; 0DFA
        or_ ax,di                                       ; 0DFE
        jz short L1_0E3C                                ; 0E00
        les bx,[si+0x8c]                                ; 0E02
        mov ax,[es:bx]                                  ; 0E06
        mov dx,[es:bx+0x2]                              ; 0E09
        mov [si+0x90],ax                                ; 0E0D
        mov [si+0x92],dx                                ; 0E11
        mov ax,[es:bx+0x4]                              ; 0E15
        mov dx,[es:bx+0x6]                              ; 0E19
        mov [si+0x94],ax                                ; 0E1D
        mov [si+0x96],dx                                ; 0E21
        sub_ ax,ax                                      ; 0E25
        mov [es:bx+0xa],ax                              ; 0E27
        mov [es:bx+0x8],ax                              ; 0E2B

L1_0E2F:
        mov ax,[bp+0x4]                                 ; 0E2F
        cmp [bp-0x8],ax                                 ; 0E32
        jnc short L1_0E3A                               ; 0E35
        jmp near L1_0CD2                                ; 0E37

L1_0E3A:
        jmp short L1_0E4E                               ; 0E3A

L1_0E3C:
        sub_ ax,ax                                      ; 0E3C
        mov [si+0x92],ax                                ; 0E3E
        mov [si+0x90],ax                                ; 0E42
        mov [si+0x96],ax                                ; 0E46
        mov [si+0x94],ax                                ; 0E4A

L1_0E4E:
        mov ax,[bp-0x8]                                 ; 0E4E

L1_0E51:
        pop si                                          ; 0E51
        pop di                                          ; 0E52
        mov_ sp,bp                                      ; 0E53
        pop bp                                          ; 0E55
        ret 0x8                                         ; 0E56
        db 0x90                                         ; 0E59

L1_0E5A:
        push bp                                         ; 0E5A
        mov_ bp,sp                                      ; 0E5B
        sub sp,byte +0x1c                               ; 0E5D
        push di                                         ; 0E60
        push si                                         ; 0E61
        mov ax,[bp+0x10]                                ; 0E62
        mov dx,[bp+0x12]                                ; 0E65
        mov_ si,ax                                      ; 0E68
        mov [bp-0x1a],dx                                ; 0E6A
        mov cl,0x80                                     ; 0E6D
        mov [bp-0x6],cl                                 ; 0E6F
        mov [bp-0x2],cl                                 ; 0E72
        mov [bp-0xb],cl                                 ; 0E75
        mov [bp-0x5],cl                                 ; 0E78
        mov [bp-0x18],ax                                ; 0E7B
        mov [bp-0x16],dx                                ; 0E7E
        xor_ ax,ax                                      ; 0E81
        mov [bp-0x12],ax                                ; 0E83
        mov [bp-0x14],ax                                ; 0E86
        mov [bp-0xe],ax                                 ; 0E89
        mov [bp-0x10],ax                                ; 0E8C
        cmp [bp+0x6],al                                 ; 0E8F
        jnz short L1_0EA3                               ; 0E92
        mov [bp-0x1],al                                 ; 0E94
        mov bx,[bp+0x14]                                ; 0E97
        mov ax,[bx+0x107]                               ; 0E9A
        mov [bp-0xa],ax                                 ; 0E9E
        jmp short L1_0ECF                               ; 0EA1

L1_0EA3:
        mov di,[bp+0x14]                                ; 0EA3
        mov al,[di+0x5d]                                ; 0EA6
        and al,0x3                                      ; 0EA9
        cmp al,0x2                                      ; 0EAB
        jnz short L1_0EBB                               ; 0EAD
        test byte [di+0x6c],0x40                        ; 0EAF
        jz short L1_0EBB                                ; 0EB3
        mov byte [bp-0x1],0x1                           ; 0EB5
        jmp short L1_0EBF                               ; 0EB9

L1_0EBB:
        mov byte [bp-0x1],0x0                           ; 0EBB

L1_0EBF:
        mov ax,[di+0x6b]                                ; 0EBF
        mov [bp-0xa],ax                                 ; 0EC2
        cmp byte [bp-0x1],0x0                           ; 0EC5
        jz short L1_0ECF                                ; 0EC9
        and byte [bp-0xa],0xfe                          ; 0ECB

L1_0ECF:
        mov al,[bp-0xa]                                 ; 0ECF
        and ax,strict word 0x3f                         ; 0ED2
        sub_ dx,dx                                      ; 0ED5
        cmp ax,strict word 0x20                         ; 0ED7
        jnz short L1_0EDF                               ; 0EDA
        jmp near L1_1098                                ; 0EDC

L1_0EDF:
        ja short L1_0F06                                ; 0EDF
        or_ al,al                                       ; 0EE1
        jz short L1_0F0B                                ; 0EE3
        dec al                                          ; 0EE5
        jz short L1_0F66                                ; 0EE7
        dec al                                          ; 0EE9
        jnz short L1_0EF0                               ; 0EEB
        jmp near L1_0FA6                                ; 0EED

L1_0EF0:
        dec al                                          ; 0EF0
        jnz short L1_0EF7                               ; 0EF2
        jmp near L1_1044                                ; 0EF4

L1_0EF7:
        dec al                                          ; 0EF7
        jz short L1_0F03                                ; 0EF9
        sub al,0x4                                      ; 0EFB
        jz short L1_0F03                                ; 0EFD
        sub al,0x8                                      ; 0EFF
        jnz short L1_0F06                               ; 0F01

L1_0F03:
        jmp near L1_1098                                ; 0F03

L1_0F06:
        xor_ ax,ax                                      ; 0F06
        jmp near L1_10E0                                ; 0F08

L1_0F0B:
        mov di,[bp+0xc]                                 ; 0F0B
        cmp byte [bp-0x1],0x0                           ; 0F0E
        jz short L1_0F16                                ; 0F12
        shr di,1                                        ; 0F14

L1_0F16:
        or_ di,di                                       ; 0F16
        jz short L1_0F41                                ; 0F18

L1_0F1A:
        mov es,[bp-0x1a]                                ; 0F1A
        mov_ bx,si                                      ; 0F1D
        inc si                                          ; 0F1F
        mov al,[es:bx]                                  ; 0F20
        mov [bp-0x1],al                                 ; 0F23
        cmp al,[bp-0x2]                                 ; 0F26
        jna short L1_0F30                               ; 0F29
        mov [bp-0x2],al                                 ; 0F2B
        jmp short L1_0F3E                               ; 0F2E

L1_0F30:
        mov al,[bp-0x5]                                 ; 0F30
        cmp [bp-0x1],al                                 ; 0F33
        jnc short L1_0F3E                               ; 0F36
        mov al,[bp-0x1]                                 ; 0F38
        mov [bp-0x5],al                                 ; 0F3B

L1_0F3E:
        dec di                                          ; 0F3E
        jnz short L1_0F1A                               ; 0F3F

L1_0F41:
        mov al,[bp-0x2]                                 ; 0F41
        xor al,0x7f                                     ; 0F44
        not al                                          ; 0F46
        mov_ ah,al                                      ; 0F48
        sub_ al,al                                      ; 0F4A
        mov [bp-0x14],ax                                ; 0F4C
        mov [bp-0x12],ax                                ; 0F4F
        mov al,[bp-0x5]                                 ; 0F52
        xor al,0x7f                                     ; 0F55
        not al                                          ; 0F57
        mov_ ah,al                                      ; 0F59
        sub_ al,al                                      ; 0F5B
        mov [bp-0x10],ax                                ; 0F5D

L1_0F60:
        mov [bp-0xe],ax                                 ; 0F60
        jmp near L1_1098                                ; 0F63

L1_0F66:
        mov ax,[bp+0xc]                                 ; 0F66
        mov dx,[bp+0xe]                                 ; 0F69
        mov cl,0x1                                      ; 0F6C
        callf L1_24F8, R1_0F71, R1_0FB1                 ; 0F6E far seg1
        mov_ di,ax                                      ; 0F73
        or_ di,ax                                       ; 0F75
        jz short L1_0F9B                                ; 0F77
        mov si,[bp-0x18]                                ; 0F79

L1_0F7C:
        mov es,[bp-0x16]                                ; 0F7C
        mov_ bx,si                                      ; 0F7F
        inc si                                          ; 0F81
        inc si                                          ; 0F82
        mov cx,[es:bx]                                  ; 0F83
        cmp cx,[bp-0x14]                                ; 0F86
        jng short L1_0F90                               ; 0F89
        mov [bp-0x14],cx                                ; 0F8B
        jmp short L1_0F98                               ; 0F8E

L1_0F90:
        cmp [bp-0x10],cx                                ; 0F90
        jng short L1_0F98                               ; 0F93
        mov [bp-0x10],cx                                ; 0F95

L1_0F98:
        dec di                                          ; 0F98
        jnz short L1_0F7C                               ; 0F99

L1_0F9B:
        mov ax,[bp-0x14]                                ; 0F9B
        mov [bp-0x12],ax                                ; 0F9E
        mov ax,[bp-0x10]                                ; 0FA1
        jmp short L1_0F60                               ; 0FA4

L1_0FA6:
        mov ax,[bp+0xc]                                 ; 0FA6
        mov dx,[bp+0xe]                                 ; 0FA9
        mov cl,0x1                                      ; 0FAC
        callf L1_24F8, R1_0FB1, R1_104F                 ; 0FAE far seg1
        mov_ di,ax                                      ; 0FB3
        cmp byte [bp-0x1],0x0                           ; 0FB5
        jz short L1_0FBD                                ; 0FB9
        shr di,1                                        ; 0FBB

L1_0FBD:
        or_ di,di                                       ; 0FBD
        jz short L1_100C                                ; 0FBF

L1_0FC1:
        mov es,[bp-0x1a]                                ; 0FC1
        mov_ bx,si                                      ; 0FC4
        inc si                                          ; 0FC6
        mov al,[es:bx]                                  ; 0FC7
        mov [bp-0x1],al                                 ; 0FCA
        cmp al,[bp-0x2]                                 ; 0FCD
        jna short L1_0FD7                               ; 0FD0
        mov [bp-0x2],al                                 ; 0FD2
        jmp short L1_0FE5                               ; 0FD5

L1_0FD7:
        mov al,[bp-0x5]                                 ; 0FD7
        cmp [bp-0x1],al                                 ; 0FDA
        jnc short L1_0FE5                               ; 0FDD
        mov al,[bp-0x1]                                 ; 0FDF
        mov [bp-0x5],al                                 ; 0FE2

L1_0FE5:
        mov es,[bp-0x1a]                                ; 0FE5
        mov_ bx,si                                      ; 0FE8
        inc si                                          ; 0FEA
        mov al,[es:bx]                                  ; 0FEB
        mov [bp-0x1],al                                 ; 0FEE
        cmp al,[bp-0x6]                                 ; 0FF1
        jna short L1_0FFB                               ; 0FF4
        mov [bp-0x6],al                                 ; 0FF6
        jmp short L1_1009                               ; 0FF9

L1_0FFB:
        mov al,[bp-0xb]                                 ; 0FFB
        cmp [bp-0x1],al                                 ; 0FFE
        jnc short L1_1009                               ; 1001
        mov al,[bp-0x1]                                 ; 1003
        mov [bp-0xb],al                                 ; 1006

L1_1009:
        dec di                                          ; 1009
        jnz short L1_0FC1                               ; 100A

L1_100C:
        mov al,[bp-0x2]                                 ; 100C
        xor al,0x7f                                     ; 100F
        not al                                          ; 1011
        mov_ ah,al                                      ; 1013
        sub_ al,al                                      ; 1015
        mov [bp-0x14],ax                                ; 1017
        mov al,[bp-0x5]                                 ; 101A
        xor al,0x7f                                     ; 101D
        not al                                          ; 101F
        mov_ ah,al                                      ; 1021
        sub_ al,al                                      ; 1023
        mov [bp-0x10],ax                                ; 1025
        mov al,[bp-0x6]                                 ; 1028
        xor al,0x7f                                     ; 102B
        not al                                          ; 102D
        mov_ ah,al                                      ; 102F
        sub_ al,al                                      ; 1031
        mov [bp-0x12],ax                                ; 1033
        mov al,[bp-0xb]                                 ; 1036
        xor al,0x7f                                     ; 1039
        not al                                          ; 103B
        mov_ ah,al                                      ; 103D
        sub_ al,al                                      ; 103F
        jmp near L1_0F60                                ; 1041

L1_1044:
        mov ax,[bp+0xc]                                 ; 1044
        mov dx,[bp+0xe]                                 ; 1047
        mov cl,0x2                                      ; 104A
        callf L1_24F8, R1_104F, R1_110B                 ; 104C far seg1
        mov [bp-0x4],ax                                 ; 1051
        or_ ax,ax                                       ; 1054
        jz short L1_1098                                ; 1056
        mov si,[bp-0x18]                                ; 1058

L1_105B:
        mov es,[bp-0x16]                                ; 105B
        mov_ bx,si                                      ; 105E
        inc si                                          ; 1060
        inc si                                          ; 1061
        mov bx,[es:bx]                                  ; 1062
        cmp bx,[bp-0x14]                                ; 1065
        jng short L1_106F                               ; 1068
        mov [bp-0x14],bx                                ; 106A
        jmp short L1_1077                               ; 106D

L1_106F:
        cmp [bp-0x10],bx                                ; 106F
        jng short L1_1077                               ; 1072
        mov [bp-0x10],bx                                ; 1074

L1_1077:
        mov es,[bp-0x16]                                ; 1077
        mov_ bx,si                                      ; 107A
        inc si                                          ; 107C
        inc si                                          ; 107D
        mov bx,[es:bx]                                  ; 107E
        cmp bx,[bp-0x12]                                ; 1081
        jng short L1_108B                               ; 1084
        mov [bp-0x12],bx                                ; 1086
        jmp short L1_1093                               ; 1089

L1_108B:
        cmp [bp-0xe],bx                                 ; 108B
        jng short L1_1093                               ; 108E
        mov [bp-0xe],bx                                 ; 1090

L1_1093:
        dec word [bp-0x4]                               ; 1093
        jnz short L1_105B                               ; 1096

L1_1098:
        mov ax,[bp-0x10]                                ; 1098
        cwd                                             ; 109B
        xor_ ax,dx                                      ; 109C
        sub_ ax,dx                                      ; 109E
        cmp ax,[bp-0x14]                                ; 10A0
        jng short L1_10AA                               ; 10A3
        mov bx,[bp-0x10]                                ; 10A5
        jmp short L1_10AD                               ; 10A8

L1_10AA:
        mov bx,[bp-0x14]                                ; 10AA

L1_10AD:
        mov_ ax,bx                                      ; 10AD
        cwd                                             ; 10AF
        les bx,[bp+0x8]                                 ; 10B0
        mov [es:bx],ax                                  ; 10B3
        mov [es:bx+0x2],dx                              ; 10B6
        mov ax,[bp-0xe]                                 ; 10BA
        cwd                                             ; 10BD
        xor_ ax,dx                                      ; 10BE
        sub_ ax,dx                                      ; 10C0
        cmp ax,[bp-0x12]                                ; 10C2
        jng short L1_10CC                               ; 10C5
        mov bx,[bp-0xe]                                 ; 10C7
        jmp short L1_10CF                               ; 10CA

L1_10CC:
        mov bx,[bp-0x12]                                ; 10CC

L1_10CF:
        mov_ ax,bx                                      ; 10CF
        cwd                                             ; 10D1
        mov bx,[bp+0x8]                                 ; 10D2
        mov [es:bx+0x4],ax                              ; 10D5
        mov [es:bx+0x6],dx                              ; 10D9
        mov ax,0x1                                      ; 10DD

L1_10E0:
        pop si                                          ; 10E0
        pop di                                          ; 10E1
        mov_ sp,bp                                      ; 10E2
        pop bp                                          ; 10E4
        retf 0x10                                       ; 10E5
        db 0x90, 0x90, 0x90, 0x90                       ; 10E8

; position of the running recording, from the VxD (0004, BX=0)
dma_pos_record:
        push bp                                         ; 10EC
        mov_ bp,sp                                      ; 10ED
        push si                                         ; 10EF
        mov si,[bp+0x6]                                 ; 10F0
        cmp byte [si+0x5d],0x0                          ; 10F3
        jz short L1_110F                                ; 10F7
        cmp word [si+0x24],byte +0x0                    ; 10F9
        jz short L1_110F                                ; 10FD
        push word [si+0x16]                             ; 10FF
        push word [si+0x14]                             ; 1102
        xor_ ax,ax                                      ; 1105
        push ax                                         ; 1107
        callf vxd_dma_pos, R1_110B, R1_113A             ; 1108 far seg1
        jmp short L1_1111                               ; 110D

L1_110F:
        xor_ ax,ax                                      ; 110F

L1_1111:
        pop si                                          ; 1111
        mov_ sp,bp                                      ; 1112
        pop bp                                          ; 1114
        retf 0x2                                        ; 1115

; position of the running playback, from the VxD (0004, BX=1)
dma_pos_play:
        push bp                                         ; 1118
        mov_ bp,sp                                      ; 1119
        push si                                         ; 111B
        mov si,[bp+0x6]                                 ; 111C
        cmp byte [si+0x100],0x0                         ; 111F
        jz short L1_113E                                ; 1124
        cmp word [si+0xe1],byte +0x0                    ; 1126
        jz short L1_113E                                ; 112B
        push word [si+0x16]                             ; 112D
        push word [si+0x14]                             ; 1130
        mov ax,0x1                                      ; 1133
        push ax                                         ; 1136
        callf vxd_dma_pos, R1_113A, R1_0D75             ; 1137 far seg1
        jmp short L1_1140                               ; 113C

L1_113E:
        xor_ ax,ax                                      ; 113E

L1_1140:
        pop si                                          ; 1140
        mov_ sp,bp                                      ; 1141
        pop bp                                          ; 1143
        retf 0x2                                        ; 1144
        db 0x90                                         ; 1147

; Audio 2 at wave-out open, close and resume: 71h bits 4 and 1, the 7:00A8 list (70h-78h) to 0, then 70h = 72h = FFh
audio2_init:
        push bp                                         ; 1148
        mov_ bp,sp                                      ; 1149
        push di                                         ; 114B
        push si                                         ; 114C
        mov di,[bp+0x6]                                 ; 114D
        push di                                         ; 1150
        mov al,0x71                                     ; 1151
        push ax                                         ; 1153
        push di                                         ; 1154
        push ax                                         ; 1155
        push cs                                         ; 1156
        call mixer_read                                 ; 1157
%if ES1869_FIX
        or al,0x0A                      ; essreg: filter bypassed, asynchronous
%else
        or al,0x12                                      ; 115A
%endif
        push ax                                         ; 115C
        push cs                                         ; 115D
        call mixer_write                                ; 115E
        xor_ si,si                                      ; 1161
        cmp byte [0xa8],0x0                             ; 1163
        jz short L1_117F                                ; 1168

L1_116A:
        push di                                         ; 116A
        mov al,[si+0xa8]                                ; 116B
        push ax                                         ; 116F
        xor_ al,al                                      ; 1170
        push ax                                         ; 1172
        push cs                                         ; 1173
        call mixer_write                                ; 1174
        inc si                                          ; 1177
        cmp byte [si+0xa8],0x0                          ; 1178
        jnz short L1_116A                               ; 117D

L1_117F:
        push di                                         ; 117F
        mov al,0x70                                     ; 1180
        push ax                                         ; 1182
        mov al,0xff                                     ; 1183
        push ax                                         ; 1185
        push cs                                         ; 1186
        call mixer_write                                ; 1187
        push di                                         ; 118A
        mov al,0x72                                     ; 118B
        push ax                                         ; 118D
        mov al,0xff                                     ; 118E
        push ax                                         ; 1190
        push cs                                         ; 1191
        call mixer_write                                ; 1192
        mov ax,0x1                                      ; 1195
        pop si                                          ; 1198
        pop di                                          ; 1199
        mov_ sp,bp                                      ; 119A
        pop bp                                          ; 119C
        retf 0x2                                        ; 119D

; Audio_Base+6 = 03h then 00h, then command C6h (extended mode), when the DSP is taken for recording
dsp_reset:
        push bp                                         ; 11A0
        mov_ bp,sp                                      ; 11A1
        sub sp,byte +0x4                                ; 11A3
        push di                                         ; 11A6
        mov di,[bp+0x6]                                 ; 11A7
        mov ax,0x3                                      ; 11AA
        mov dx,[di]                                     ; 11AD
        add dx,byte +0x6                                ; 11AF
        out dx,al                                       ; 11B2
        in al,dx                                        ; 11B3
        in al,dx                                        ; 11B4
        in al,dx                                        ; 11B5
        xor_ ax,ax                                      ; 11B6
        out dx,al                                       ; 11B8
        push di                                         ; 11B9
        push cs                                         ; 11BA
        call dsp_read                                   ; 11BB
        cmp al,0xaa                                     ; 11BE
        jnz short L1_11EC                               ; 11C0
        mov bx,[di]                                     ; 11C2
        add bx,byte +0xc                                ; 11C4
        xor_ cx,cx                                      ; 11C7
        mov_ di,cx                                      ; 11C9

L1_11CB:
        xor_ cx,cx                                      ; 11CB

L1_11CD:
        mov_ dx,bx                                      ; 11CD
        in al,dx                                        ; 11CF
        test al,0x80                                    ; 11D0
        jz short L1_11E3                                ; 11D2
        inc cx                                          ; 11D4
        cmp cx,0x7fff                                   ; 11D5
        jl short L1_11CD                                ; 11D9
        inc di                                          ; 11DB
        cmp di,byte +0x14                               ; 11DC
        jl short L1_11CB                                ; 11DF
        jmp short L1_11EC                               ; 11E1

L1_11E3:
        mov ax,0xc6                                     ; 11E3
        out dx,al                                       ; 11E6
        mov ax,0x1                                      ; 11E7
        jmp short L1_11EE                               ; 11EA

L1_11EC:
        xor_ ax,ax                                      ; 11EC

L1_11EE:
        pop di                                          ; 11EE
        mov_ sp,bp                                      ; 11EF
        pop bp                                          ; 11F1
        retf 0x2                                        ; 11F2
        db 0x90                                         ; 11F5

; read a byte from the DSP (Audio_Base+Ah)
dsp_read:
        push bp                                         ; 11F6
        mov_ bp,sp                                      ; 11F7
        sub sp,byte +0x2                                ; 11F9
        push di                                         ; 11FC
        mov di,[bp+0x6]                                 ; 11FD
        mov bx,[di]                                     ; 1200
        add bx,byte +0xe                                ; 1202
        xor_ cx,cx                                      ; 1205

L1_1207:
        mov_ dx,bx                                      ; 1207
        in al,dx                                        ; 1209
        test al,0x80                                    ; 120A
        jnz short L1_1217                               ; 120C
        inc cx                                          ; 120E
        cmp cx,0x3e8                                    ; 120F
        jl short L1_1207                                ; 1213
        jmp short L1_121F                               ; 1215

L1_1217:
        mov dx,[di]                                     ; 1217
        add dx,byte +0xa                                ; 1219
        in al,dx                                        ; 121C
        jmp short L1_1221                               ; 121D

L1_121F:
        mov al,0xff                                     ; 121F

L1_1221:
        pop di                                          ; 1221
        mov_ sp,bp                                      ; 1222
        pop bp                                          ; 1224
        retf 0x2                                        ; 1225

; write a command or data byte to the DSP (Audio_Base+Ch)
dsp_write:
        push bp                                         ; 1228
        mov_ bp,sp                                      ; 1229
        sub sp,byte +0x4                                ; 122B
        push di                                         ; 122E
        mov bx,[bp+0x8]                                 ; 122F
        cmp word [bx+0x20],byte +0x0                    ; 1232
        jnz short L1_128B                               ; 1236
        mov cx,[bx]                                     ; 1238
        add cx,byte +0xc                                ; 123A
        xor_ di,di                                      ; 123D

L1_123F:
        xor_ bx,bx                                      ; 123F

L1_1241:
        mov_ dx,cx                                      ; 1241
        in al,dx                                        ; 1243
        test al,0x80                                    ; 1244
        jz short L1_1278                                ; 1246
        inc bx                                          ; 1248
        cmp bx,0x7fff                                   ; 1249
        jl short L1_1241                                ; 124D
        mov ax,0x1                                      ; 124F
        sub dx,byte +0x6                                ; 1252
        out dx,al                                       ; 1255
        in al,dx                                        ; 1256
        in al,dx                                        ; 1257
        in al,dx                                        ; 1258
        xor_ ax,ax                                      ; 1259
        out dx,al                                       ; 125B
        mov bx,0x3e8                                    ; 125C

L1_125F:
        mov_ dx,cx                                      ; 125F
        inc dx                                          ; 1261
        inc dx                                          ; 1262
        in al,dx                                        ; 1263
        test al,0x80                                    ; 1264
        jz short L1_126D                                ; 1266
        mov_ dx,cx                                      ; 1268
        dec dx                                          ; 126A
        dec dx                                          ; 126B
        in al,dx                                        ; 126C

L1_126D:
        dec bx                                          ; 126D
        jnz short L1_125F                               ; 126E
        inc di                                          ; 1270
        cmp di,byte +0x14                               ; 1271
        jl short L1_123F                                ; 1274
        jmp short L1_1283                               ; 1276

L1_1278:
        mov al,[bp+0x6]                                 ; 1278
        sub_ ah,ah                                      ; 127B
        out dx,al                                       ; 127D
        mov ax,0x1                                      ; 127E
        jmp short L1_128D                               ; 1281

L1_1283:
        mov bx,[bp+0x8]                                 ; 1283
        mov word [bx+0x20],0x1                          ; 1286

L1_128B:
        xor_ ax,ax                                      ; 128B

L1_128D:
        pop di                                          ; 128D
        mov_ sp,bp                                      ; 128E
        pop bp                                          ; 1290
        retf 0x4                                        ; 1291

; mixer_read(dev, reg): index to Audio_Base+4, data from +5
mixer_read:
        push bp                                         ; 1294
        mov_ bp,sp                                      ; 1295
        push si                                         ; 1297
        mov si,[bp+0x8]                                 ; 1298
        mov al,[bp+0x6]                                 ; 129B
        mov cx,[si]                                     ; 129E
        sub_ ah,ah                                      ; 12A0
        mov_ dx,cx                                      ; 12A2
        add dx,byte +0x4                                ; 12A4
        out dx,al                                       ; 12A7
        inc dx                                          ; 12A8
        in al,dx                                        ; 12A9
        pop si                                          ; 12AA
        mov_ sp,bp                                      ; 12AB
        pop bp                                          ; 12AD
        retf 0x4                                        ; 12AE
        db 0x90                                         ; 12B1

; mixer_write(dev, reg, value)
mixer_write:
        push bp                                         ; 12B2
        mov_ bp,sp                                      ; 12B3
        push si                                         ; 12B5
        mov si,[bp+0xa]                                 ; 12B6
        mov al,[bp+0x8]                                 ; 12B9
        mov cx,[si]                                     ; 12BC
        sub_ ah,ah                                      ; 12BE
        mov_ dx,cx                                      ; 12C0
        add dx,byte +0x4                                ; 12C2
        out dx,al                                       ; 12C5
        mov al,[bp+0x6]                                 ; 12C6
        inc dx                                          ; 12C9
        out dx,al                                       ; 12CA
        pop si                                          ; 12CB
        mov_ sp,bp                                      ; 12CC
        pop bp                                          ; 12CE
        retf 0x6                                        ; 12CF

L1_12D2:
        push bp                                         ; 12D2
        mov_ bp,sp                                      ; 12D3
        sub sp,byte +0x8                                ; 12D5
        push si                                         ; 12D8
        mov cl,[bp+0x6]                                 ; 12D9
        and cl,0x3                                      ; 12DC
        mov al,0x10                                     ; 12DF
        shl al,cl                                       ; 12E1
        mov [bp-0x1],al                                 ; 12E3
        cmp byte [bp+0x6],0x3                           ; 12E6
        jna short L1_12F1                               ; 12EA
        mov si,0xd0                                     ; 12EC
        jmp short L1_12F4                               ; 12EF

L1_12F1:
        mov si,0x8                                      ; 12F1

L1_12F4:
        callp R1_12F5, R1_1308, 0x0000                  ; 12F4 MMSYSTEM.timeGetTime
        mov [bp-0x6],ax                                 ; 12F9
        mov [bp-0x4],dx                                 ; 12FC

L1_12FF:
        mov_ dx,si                                      ; 12FF
        in al,dx                                        ; 1301
        test [bp-0x1],al                                ; 1302
        jz short L1_131B                                ; 1305
        callp R1_1308, R1_1325, 0x0000                  ; 1307 MMSYSTEM.timeGetTime
        sub ax,[bp-0x6]                                 ; 130C
        sbb dx,[bp-0x4]                                 ; 130F
        or_ dx,dx                                       ; 1312
        jnz short L1_131B                               ; 1314
        cmp ax,0x3e8                                    ; 1316
        jc short L1_12FF                                ; 1319

L1_131B:
        mov bx,[bp+0x8]                                 ; 131B
        cmp word [bx+0x20],byte +0x0                    ; 131E
        jnz short L1_1344                               ; 1322
        callp R1_1325, 0xFFFF, 0x0000                   ; 1324 MMSYSTEM.timeGetTime
        sub ax,[bp-0x6]                                 ; 1329
        sbb dx,[bp-0x4]                                 ; 132C
        or_ dx,dx                                       ; 132F
        jnz short L1_1338                               ; 1331
        cmp ax,0x3e8                                    ; 1333
        jc short L1_1344                                ; 1336

L1_1338:
        mov bx,[bp+0x8]                                 ; 1338
        xor_ ax,ax                                      ; 133B
        mov word [bx+0x20],0x1                          ; 133D
        jmp short L1_1347                               ; 1342

L1_1344:
        mov ax,0x1                                      ; 1344

L1_1347:
        pop si                                          ; 1347
        mov_ sp,bp                                      ; 1348
        pop bp                                          ; 134A
        ret 0x6                                         ; 134B

L1_134E:
        push bp                                         ; 134E
        mov_ bp,sp                                      ; 134F
        sub sp,byte +0xa                                ; 1351
        push di                                         ; 1354
        push si                                         ; 1355
        mov di,[bp+0xa]                                 ; 1356
        mov si,[di+0x65]                                ; 1359
        cmp word [di+0x24],byte +0x0                    ; 135C
        jnz short L1_1390                               ; 1360
        mov ax,[di+0x3d]                                ; 1362
        sub_ dx,dx                                      ; 1365
        push dx                                         ; 1367
        push ax                                         ; 1368
        push word [si+0x16]                             ; 1369
        push word [si+0x14]                             ; 136C
        callf L1_23E4, R1_1372, R1_13BE                 ; 136F far seg1
        mov [bp-0x4],ax                                 ; 1374
        mov [bp-0x2],dx                                 ; 1377
        mov ax,[bp+0x8]                                 ; 137A
        or ax,[bp+0x6]                                  ; 137D
        jnz short L1_1385                               ; 1380
        jmp near L1_1475                                ; 1382

L1_1385:
        les bx,[bp+0x6]                                 ; 1385
        mov word [es:bx],0x0                            ; 1388
        jmp near L1_1475                                ; 138D

L1_1390:
        mov [bp-0xa],si                                 ; 1390

L1_1393:
        mov bx,[bp-0xa]                                 ; 1393
        mov ax,[bx+0x14]                                ; 1396
        mov dx,[bx+0x16]                                ; 1399
        mov [bp-0x8],ax                                 ; 139C
        mov [bp-0x6],dx                                 ; 139F
        push di                                         ; 13A2
        push cs                                         ; 13A3
        call dma_pos_record                             ; 13A4
        mov_ si,ax                                      ; 13A7
        inc si                                          ; 13A9
        mov ax,[di+0x3d]                                ; 13AA
        sub_ dx,dx                                      ; 13AD
        push dx                                         ; 13AF
        push ax                                         ; 13B0
        mov ax,[bp-0x8]                                 ; 13B1
        mov dx,[bp-0x6]                                 ; 13B4
        and al,0xfe                                     ; 13B7
        push dx                                         ; 13B9
        push ax                                         ; 13BA
        callf L1_23E4, R1_13BE, R1_13F7                 ; 13BB far seg1
        mov cx,[di+0x3d]                                ; 13C0
        add_ cx,cx                                      ; 13C3
        sub_ cx,si                                      ; 13C5
        mov_ si,cx                                      ; 13C7
        add_ ax,cx                                      ; 13C9
        adc dx,byte +0x0                                ; 13CB
        mov [bp-0x4],ax                                 ; 13CE
        mov [bp-0x2],dx                                 ; 13D1
        mov ax,[bp-0x8]                                 ; 13D4
        mov dx,[bp-0x6]                                 ; 13D7
        mov bx,[bp-0xa]                                 ; 13DA
        cmp [bx+0x14],ax                                ; 13DD
        jnz short L1_1393                               ; 13E0
        cmp [bx+0x16],dx                                ; 13E2
        jnz short L1_1393                               ; 13E5
        mov ax,[di+0x3d]                                ; 13E7
        sub_ dx,dx                                      ; 13EA
        push dx                                         ; 13EC
        push ax                                         ; 13ED
        push word [bp-0x6]                              ; 13EE
        push word [bp-0x8]                              ; 13F1
        callf L1_23E4, R1_13F7, R1_14BA                 ; 13F4 far seg1
        cmp dx,[bp-0x2]                                 ; 13F9
        jc short L1_1414                                ; 13FC
        ja short L1_1405                                ; 13FE
        cmp ax,[bp-0x4]                                 ; 1400
        jna short L1_1414                               ; 1403

L1_1405:
        mov ax,[di+0x3d]                                ; 1405
        sub_ dx,dx                                      ; 1408
        add_ ax,ax                                      ; 140A
        adc_ dx,dx                                      ; 140C
        add [bp-0x4],ax                                 ; 140E
        adc [bp-0x2],dx                                 ; 1411

L1_1414:
        mov al,[di+0x5d]                                ; 1414
        and al,0x3                                      ; 1417
        dec al                                          ; 1419
        jnz short L1_143D                               ; 141B
        cmp word [bp-0x2],byte +0x0                     ; 141D
        jnz short L1_1434                               ; 1421
        cmp word [bp-0x4],0x100                         ; 1423
        jnc short L1_1434                               ; 1428
        sub_ ax,ax                                      ; 142A
        mov [bp-0x2],ax                                 ; 142C
        mov [bp-0x4],ax                                 ; 142F
        jmp short L1_143D                               ; 1432

L1_1434:
        sub word [bp-0x4],0x100                         ; 1434
        sbb word [bp-0x2],byte +0x0                     ; 1439

L1_143D:
        mov ax,[di+0xc8]                                ; 143D
        mov dx,[di+0xca]                                ; 1441
        cmp [bp-0x2],dx                                 ; 1445
        ja short L1_1459                                ; 1448
        jc short L1_1451                                ; 144A
        cmp [bp-0x4],ax                                 ; 144C
        jnc short L1_1459                               ; 144F

L1_1451:
        mov [bp-0x4],ax                                 ; 1451
        mov [bp-0x2],dx                                 ; 1454
        jmp short L1_1467                               ; 1457

L1_1459:
        mov ax,[bp-0x4]                                 ; 1459
        mov dx,[bp-0x2]                                 ; 145C
        mov [di+0xc8],ax                                ; 145F
        mov [di+0xca],dx                                ; 1463

L1_1467:
        mov ax,[bp+0x8]                                 ; 1467
        or ax,[bp+0x6]                                  ; 146A
        jz short L1_1475                                ; 146D
        les bx,[bp+0x6]                                 ; 146F
        mov [es:bx],si                                  ; 1472

L1_1475:
        mov ax,[bp-0x4]                                 ; 1475
        mov dx,[bp-0x2]                                 ; 1478
        pop si                                          ; 147B
        pop di                                          ; 147C
        mov_ sp,bp                                      ; 147D
        pop bp                                          ; 147F
        retf 0x6                                        ; 1480
        db 0x90                                         ; 1483

L1_1484:
        push bp                                         ; 1484
        mov_ bp,sp                                      ; 1485
        sub sp,byte +0xa                                ; 1487
        push di                                         ; 148A
        push si                                         ; 148B
        mov di,[bp+0xa]                                 ; 148C
        mov si,[di+0x101]                               ; 148F
        cmp word [di+0xe1],byte +0x0                    ; 1493
        jnz short L1_1502                               ; 1498
        cmp word [di+0xed],byte +0x0                    ; 149A
        jz short L1_14A9                                ; 149F
        mov ax,[di+0x49]                                ; 14A1
        mov dx,[di+0x4b]                                ; 14A4
        jmp short L1_14BC                               ; 14A7

L1_14A9:
        mov ax,[di+0xf1]                                ; 14A9
        sub_ dx,dx                                      ; 14AD
        push dx                                         ; 14AF
        push ax                                         ; 14B0
        push word [si+0x16]                             ; 14B1
        push word [si+0x14]                             ; 14B4
        callf L1_23E4, R1_14BA, R1_1531                 ; 14B7 far seg1

L1_14BC:
        mov [bp-0x4],ax                                 ; 14BC
        mov [bp-0x2],dx                                 ; 14BF
        mov ax,[di+0x10b]                               ; 14C2
        mov dx,[di+0x10d]                               ; 14C6
        cmp [bp-0x2],dx                                 ; 14CA
        ja short L1_14DE                                ; 14CD
        jc short L1_14D6                                ; 14CF
        cmp [bp-0x4],ax                                 ; 14D1
        jnc short L1_14DE                               ; 14D4

L1_14D6:
        mov [bp-0x4],ax                                 ; 14D6
        mov [bp-0x2],dx                                 ; 14D9
        jmp short L1_14EC                               ; 14DC

L1_14DE:
        mov ax,[bp-0x4]                                 ; 14DE
        mov dx,[bp-0x2]                                 ; 14E1
        mov [di+0x10b],ax                               ; 14E4
        mov [di+0x10d],dx                               ; 14E8

L1_14EC:
        mov ax,[bp+0x8]                                 ; 14EC
        or ax,[bp+0x6]                                  ; 14EF
        jnz short L1_14F7                               ; 14F2
        jmp near L1_15F3                                ; 14F4

L1_14F7:
        les bx,[bp+0x6]                                 ; 14F7
        mov word [es:bx],0x0                            ; 14FA
        jmp near L1_15F3                                ; 14FF

L1_1502:
        mov [bp-0xa],si                                 ; 1502

L1_1505:
        mov bx,[bp-0xa]                                 ; 1505
        mov ax,[bx+0x14]                                ; 1508
        mov dx,[bx+0x16]                                ; 150B
        mov [bp-0x8],ax                                 ; 150E
        mov [bp-0x6],dx                                 ; 1511
        push di                                         ; 1514
        push cs                                         ; 1515
        call dma_pos_play                               ; 1516
        mov_ si,ax                                      ; 1519
        inc si                                          ; 151B
        mov ax,[di+0xf1]                                ; 151C
        sub_ dx,dx                                      ; 1520
        push dx                                         ; 1522
        push ax                                         ; 1523
        mov ax,[bp-0x8]                                 ; 1524
        mov dx,[bp-0x6]                                 ; 1527
        and al,0xfe                                     ; 152A
        push dx                                         ; 152C
        push ax                                         ; 152D
        callf L1_23E4, R1_1531, R1_157D                 ; 152E far seg1
        mov cx,[di+0xf1]                                ; 1533
        add_ cx,cx                                      ; 1537
        sub_ cx,si                                      ; 1539
        mov_ si,cx                                      ; 153B
        add_ ax,cx                                      ; 153D
        adc dx,byte +0x0                                ; 153F
        mov [bp-0x4],ax                                 ; 1542
        mov [bp-0x2],dx                                 ; 1545
        mov ax,[bp-0x8]                                 ; 1548
        mov dx,[bp-0x6]                                 ; 154B
        mov bx,[bp-0xa]                                 ; 154E
        cmp [bx+0x14],ax                                ; 1551
        jnz short L1_1505                               ; 1554
        cmp [bx+0x16],dx                                ; 1556
        jnz short L1_1505                               ; 1559
        cmp word [di+0xed],byte +0x0                    ; 155B
        jz short L1_1570                                ; 1560
        mov ax,[di+0x49]                                ; 1562
        mov dx,[di+0x4b]                                ; 1565
        mov [bp-0x4],ax                                 ; 1568
        mov [bp-0x2],dx                                 ; 156B
        jmp short L1_159B                               ; 156E

L1_1570:
        push dx                                         ; 1570
        push ax                                         ; 1571
        mov ax,[di+0xf1]                                ; 1572
        sub_ dx,dx                                      ; 1576
        push dx                                         ; 1578
        push ax                                         ; 1579
        callf L1_23E4, R1_157D, R1_0F71                 ; 157A far seg1
        cmp dx,[bp-0x2]                                 ; 157F
        jc short L1_159B                                ; 1582
        ja short L1_158B                                ; 1584
        cmp ax,[bp-0x4]                                 ; 1586
        jna short L1_159B                               ; 1589

L1_158B:
        mov ax,[di+0xf1]                                ; 158B
        sub_ dx,dx                                      ; 158F
        add_ ax,ax                                      ; 1591
        adc_ dx,dx                                      ; 1593
        add [bp-0x4],ax                                 ; 1595
        adc [bp-0x2],dx                                 ; 1598

L1_159B:
        cmp word [bp-0x2],byte +0x0                     ; 159B
        jnz short L1_15B2                               ; 159F
        cmp word [bp-0x4],0x100                         ; 15A1
        jnc short L1_15B2                               ; 15A6
        sub_ ax,ax                                      ; 15A8
        mov [bp-0x2],ax                                 ; 15AA
        mov [bp-0x4],ax                                 ; 15AD
        jmp short L1_15BB                               ; 15B0

L1_15B2:
        sub word [bp-0x4],0x100                         ; 15B2
        sbb word [bp-0x2],byte +0x0                     ; 15B7

L1_15BB:
        mov ax,[di+0x10b]                               ; 15BB
        mov dx,[di+0x10d]                               ; 15BF
        cmp [bp-0x2],dx                                 ; 15C3
        ja short L1_15D7                                ; 15C6
        jc short L1_15CF                                ; 15C8
        cmp [bp-0x4],ax                                 ; 15CA
        jnc short L1_15D7                               ; 15CD

L1_15CF:
        mov [bp-0x4],ax                                 ; 15CF
        mov [bp-0x2],dx                                 ; 15D2
        jmp short L1_15E5                               ; 15D5

L1_15D7:
        mov ax,[bp-0x4]                                 ; 15D7
        mov dx,[bp-0x2]                                 ; 15DA
        mov [di+0x10b],ax                               ; 15DD
        mov [di+0x10d],dx                               ; 15E1

L1_15E5:
        mov ax,[bp+0x8]                                 ; 15E5
        or ax,[bp+0x6]                                  ; 15E8
        jz short L1_15F3                                ; 15EB
        les bx,[bp+0x6]                                 ; 15ED
        mov [es:bx],si                                  ; 15F0

L1_15F3:
        mov ax,[bp-0x4]                                 ; 15F3
        mov dx,[bp-0x2]                                 ; 15F6
        pop si                                          ; 15F9
        pop di                                          ; 15FA
        mov_ sp,bp                                      ; 15FB
        pop bp                                          ; 15FD
        retf 0x6                                        ; 15FE
        db 0x90                                         ; 1601

L1_1602:
        push bp                                         ; 1602
        mov_ bp,sp                                      ; 1603
        sub sp,byte +0x2                                ; 1605
        push si                                         ; 1608
        mov si,[bp+0x6]                                 ; 1609
        cmp word [si+0x1c],byte +0x0                    ; 160C
        jz short L1_1618                                ; 1610
        cmp word [si+0x24],byte +0x0                    ; 1612
        jnz short L1_161B                               ; 1616

L1_1618:
        jmp near L1_16D4                                ; 1618

L1_161B:
        test byte [si+0x6b],0x3c                        ; 161B
        jz short L1_1626                                ; 161F
        push si                                         ; 1621
        mov al,0xd0                                     ; 1622
        jmp short L1_164C                               ; 1624

L1_1626:
        push si                                         ; 1626
        mov al,0xc0                                     ; 1627
        push ax                                         ; 1629
        push cs                                         ; 162A
        call dsp_write                                  ; 162B
        push si                                         ; 162E
        mov al,0xb8                                     ; 162F
        push ax                                         ; 1631
        push cs                                         ; 1632
        call dsp_write                                  ; 1633
        push si                                         ; 1636
        push cs                                         ; 1637
        call dsp_read                                   ; 1638
        and al,0xfb                                     ; 163B
        mov [bp-0x1],al                                 ; 163D
        push si                                         ; 1640
        mov al,0xb8                                     ; 1641
        push ax                                         ; 1643
        push cs                                         ; 1644
        call dsp_write                                  ; 1645
        push si                                         ; 1648
        mov al,[bp-0x1]                                 ; 1649

L1_164C:
        push ax                                         ; 164C
        push cs                                         ; 164D
        call dsp_write                                  ; 164E
        test byte [si+0x2b],0x8                         ; 1651
        jnz short L1_1662                               ; 1655
        push si                                         ; 1657
        mov al,[si+0x6]                                 ; 1658
        push ax                                         ; 165B
        xor_ al,al                                      ; 165C
        push ax                                         ; 165E
        call L1_12D2                                    ; 165F

L1_1662:
        test byte [si+0x6b],0x3c                        ; 1662
        jnz short L1_169F                               ; 1666
        push si                                         ; 1668
        mov al,0xc0                                     ; 1669
        push ax                                         ; 166B
        push cs                                         ; 166C
        call dsp_write                                  ; 166D
        push si                                         ; 1670
        mov al,0xb8                                     ; 1671
        push ax                                         ; 1673
        push cs                                         ; 1674
        call dsp_write                                  ; 1675
        push si                                         ; 1678
        push cs                                         ; 1679
        call dsp_read                                   ; 167A
        and al,0x30                                     ; 167D
        mov [bp-0x1],al                                 ; 167F
        push si                                         ; 1682
        mov al,0xb8                                     ; 1683
        push ax                                         ; 1685
        push cs                                         ; 1686
        call dsp_write                                  ; 1687
        push si                                         ; 168A
        mov al,[bp-0x1]                                 ; 168B
        push ax                                         ; 168E
        push cs                                         ; 168F
        call dsp_write                                  ; 1690
        mov ax,0x2                                      ; 1693
        mov dx,[si]                                     ; 1696
        add dx,byte +0x6                                ; 1698
        out dx,al                                       ; 169B
        xor_ ax,ax                                      ; 169C
        out dx,al                                       ; 169E

L1_169F:
        test byte [si+0x2b],0x8                         ; 169F
        jz short L1_16B8                                ; 16A3
        push word [si+0x16]                             ; 16A5
        push word [si+0x14]                             ; 16A8
        xor_ ax,ax                                      ; 16AB
        push ax                                         ; 16AD
        mov al,0x1                                      ; 16AE
        push ax                                         ; 16B0
        callf vxd_pio_buffer, R1_16B4, R1_1791          ; 16B1 far seg1
        jmp short L1_16C3                               ; 16B6

L1_16B8:
        mov al,[si+0x36]                                ; 16B8
        sub_ ah,ah                                      ; 16BB
        mov dl,[si+0x31]                                ; 16BD
        sub_ dh,dh                                      ; 16C0
        out dx,al                                       ; 16C2

L1_16C3:
        mov dx,[si]                                     ; 16C3
        add dx,byte +0xa                                ; 16C5
        in al,dx                                        ; 16C8
        mov dx,[si]                                     ; 16C9
        add dx,byte +0xe                                ; 16CB
        in al,dx                                        ; 16CE
        mov word [si+0x24],0x0                          ; 16CF

L1_16D4:
        pop si                                          ; 16D4
        mov_ sp,bp                                      ; 16D5
        pop bp                                          ; 16D7
        retf 0x2                                        ; 16D8
        db 0x90                                         ; 16DB

; stop playback: 78h bit 4 off, the DMA channel masked, 78h = 0, 7Ah bit 7 cleared, 7Ch = 0
audio2_stop:
        push bp                                         ; 16DC
        mov_ bp,sp                                      ; 16DD
        push si                                         ; 16DF
        mov si,[bp+0x6]                                 ; 16E0
        cmp word [si+0x1c],byte +0x0                    ; 16E3
        jz short L1_174D                                ; 16E7
        cmp word [si+0xe1],byte +0x0                    ; 16E9
        jz short L1_174D                                ; 16EE
        push si                                         ; 16F0
        mov al,0x78                                     ; 16F1
        push ax                                         ; 16F3
        push si                                         ; 16F4
        push ax                                         ; 16F5
        push cs                                         ; 16F6
        call mixer_read                                 ; 16F7
        and al,0xef                                     ; 16FA
        push ax                                         ; 16FC
        push cs                                         ; 16FD
        call mixer_write                                ; 16FE
        test byte [si+0x2b],0x8                         ; 1701
        jnz short L1_1713                               ; 1705
        push si                                         ; 1707
        mov al,[si+0xd4]                                ; 1708
        push ax                                         ; 170C
        mov al,0x1                                      ; 170D
        push ax                                         ; 170F
        call L1_12D2                                    ; 1710

L1_1713:
        push si                                         ; 1713
        mov al,0x78                                     ; 1714
        push ax                                         ; 1716
        xor_ al,al                                      ; 1717
        push ax                                         ; 1719
        push cs                                         ; 171A
        call mixer_write                                ; 171B
        mov al,[si+0xea]                                ; 171E
        sub_ ah,ah                                      ; 1722
        mov dl,[si+0xe5]                                ; 1724
        sub_ dh,dh                                      ; 1728
        out dx,al                                       ; 172A
        push si                                         ; 172B
        mov al,0x7a                                     ; 172C
        push ax                                         ; 172E
        push si                                         ; 172F
        push ax                                         ; 1730
        push cs                                         ; 1731
        call mixer_read                                 ; 1732
        and al,0x7f                                     ; 1735
        push ax                                         ; 1737
        push cs                                         ; 1738
        call mixer_write                                ; 1739
        push si                                         ; 173C
        mov al,0x7c                                     ; 173D
        push ax                                         ; 173F
        xor_ al,al                                      ; 1740
        push ax                                         ; 1742
        push cs                                         ; 1743
        call mixer_write                                ; 1744
        mov word [si+0xe1],0x0                          ; 1747

L1_174D:
        pop si                                          ; 174D
        mov_ sp,bp                                      ; 174E
        pop bp                                          ; 1750
        retf 0x2                                        ; 1751

; playback interrupt service: refill the Audio 2 DMA buffer from the wave-out queue (+101h). Audio 2 users 1 and 2, Audio 1 users 3 and 4
isr_play:
        push bp                                         ; 1754
        mov_ bp,sp                                      ; 1755
        sub sp,byte +0x4                                ; 1757
        push di                                         ; 175A
        push si                                         ; 175B
        mov si,[bp+0x6]                                 ; 175C
        cmp word [si+0xe1],byte +0x0                    ; 175F
        jnz short L1_1769                               ; 1764
        jmp near L1_190E                                ; 1766

L1_1769:
        sub_ ax,ax                                      ; 1769
        mov [si+0xa6],ax                                ; 176B
        mov [si+0xa4],ax                                ; 176F
        mov bx,[si+0x101]                               ; 1773
        add word [bx+0x14],byte +0x1                    ; 1777
        adc [bx+0x16],ax                                ; 177B
        xor byte [si+0x2c],0x1                          ; 177E
        cmp [si+0xed],ax                                ; 1782
        jz short L1_1796                                ; 1786
        push si                                         ; 1788
        push cs                                         ; 1789
        call audio2_stop                                ; 178A
        push si                                         ; 178D
        callf L1_00DA, R1_1791, R1_1811                 ; 178E far seg1
        jmp near L1_190E                                ; 1793

L1_1796:
        mov [bp-0x2],bx                                 ; 1796
        push si                                         ; 1799
        push cs                                         ; 179A
        call dma_pos_play                               ; 179B
        mov_ di,ax                                      ; 179E
        inc di                                          ; 17A0
        mov ax,[si+0xf1]                                ; 17A1
        add_ ax,ax                                      ; 17A5
        mov_ cx,ax                                      ; 17A7
        sub_ ax,di                                      ; 17A9
        mov_ di,ax                                      ; 17AB
        mov ax,[si+0xf3]                                ; 17AD
        dec ax                                          ; 17B1
        not ax                                          ; 17B2
        and_ di,ax                                      ; 17B4
        cmp_ di,cx                                      ; 17B6
        jnz short L1_17BC                               ; 17B8
        xor_ di,di                                      ; 17BA

L1_17BC:
        cmp [si+0xdf],di                                ; 17BC
        jnc short L1_17CC                               ; 17C0
        mov_ ax,di                                      ; 17C2
        sub ax,[si+0xdf]                                ; 17C4
        sub_ dx,dx                                      ; 17C8
        jmp short L1_17DC                               ; 17CA

L1_17CC:
        mov ax,[si+0xf1]                                ; 17CC
        add_ ax,ax                                      ; 17D0
        sub ax,[si+0xdf]                                ; 17D2
        sub_ dx,dx                                      ; 17D6
        add_ ax,di                                      ; 17D8
        adc_ dx,dx                                      ; 17DA

L1_17DC:
        add [si+0x4d],ax                                ; 17DC
        adc [si+0x4f],dx                                ; 17DF
        cmp [si+0xdf],di                                ; 17E2
        jnc short L1_1844                               ; 17E6
        mov_ ax,di                                      ; 17E8
        sub ax,[si+0xdf]                                ; 17EA
        mov [bp-0x4],ax                                 ; 17EE
        or_ ax,ax                                       ; 17F1
        jnz short L1_17F8                               ; 17F3
        jmp near L1_190A                                ; 17F5

L1_17F8:
        push si                                         ; 17F8
        mov ax,[si+0xf5]                                ; 17F9
        mov dx,[si+0xf7]                                ; 17FD
        add ax,[si+0xdf]                                ; 1801
        push dx                                         ; 1805
        push ax                                         ; 1806
        push word [bp-0x4]                              ; 1807
        mov ax,0x1                                      ; 180A
        push ax                                         ; 180D
        callf L1_07B2, R1_1811, R1_1871                 ; 180E far seg1
        or_ ax,ax                                       ; 1813
        jz short L1_181A                                ; 1815
        jmp near L1_190A                                ; 1817

L1_181A:
        mov ax,[si+0x4d]                                ; 181A
        mov dx,[si+0x4f]                                ; 181D
        add ax,[si+0xf1]                                ; 1820
        adc dx,byte +0x0                                ; 1824
        mov bx,[bp-0x2]                                 ; 1827
        cmp dx,[bx+0x12]                                ; 182A
        jc short L1_183B                                ; 182D
        ja short L1_1836                                ; 182F
        cmp ax,[bx+0x10]                                ; 1831
        jc short L1_183B                                ; 1834

L1_1836:
        mov ax,0x1                                      ; 1836
        jmp short L1_183D                               ; 1839

L1_183B:
        xor_ ax,ax                                      ; 183B

L1_183D:
        mov [si+0xed],ax                                ; 183D
        jmp near L1_190A                                ; 1841

L1_1844:
        mov ax,[si+0xf1]                                ; 1844
        add_ ax,ax                                      ; 1848
        sub ax,[si+0xdf]                                ; 184A
        mov [bp-0x4],ax                                 ; 184E
        or_ ax,ax                                       ; 1851
        jnz short L1_1858                               ; 1853
        jmp near L1_190A                                ; 1855

L1_1858:
        push si                                         ; 1858
        mov ax,[si+0xf5]                                ; 1859
        mov dx,[si+0xf7]                                ; 185D
        add ax,[si+0xdf]                                ; 1861
        push dx                                         ; 1865
        push ax                                         ; 1866
        push word [bp-0x4]                              ; 1867
        mov ax,0x1                                      ; 186A
        push ax                                         ; 186D
        callf L1_07B2, R1_1871, R1_188C                 ; 186E far seg1
        or_ ax,ax                                       ; 1873
        jnz short L1_18BB                               ; 1875
        push si                                         ; 1877
        push word [si+0x109]                            ; 1878
        push word [si+0x107]                            ; 187C
        push word [si+0xf7]                             ; 1880
        push word [si+0xf5]                             ; 1884
        push di                                         ; 1888
        callf L1_0080, R1_188C, R1_18D0                 ; 1889 far seg1
        mov ax,[si+0x4d]                                ; 188E
        mov dx,[si+0x4f]                                ; 1891
        add ax,[si+0xf1]                                ; 1894
        adc dx,byte +0x0                                ; 1898
        mov bx,[bp-0x2]                                 ; 189B
        cmp dx,[bx+0x12]                                ; 189E
        jc short L1_18AF                                ; 18A1
        ja short L1_18AA                                ; 18A3
        cmp ax,[bx+0x10]                                ; 18A5
        jc short L1_18AF                                ; 18A8

L1_18AA:
        mov ax,0x1                                      ; 18AA
        jmp short L1_18B1                               ; 18AD

L1_18AF:
        xor_ ax,ax                                      ; 18AF

L1_18B1:
        mov [si+0xed],ax                                ; 18B1
        mov byte [si+0x48],0x0                          ; 18B5
        jmp short L1_190A                               ; 18B9

L1_18BB:
        or_ di,di                                       ; 18BB
        jz short L1_190A                                ; 18BD
        push si                                         ; 18BF
        push word [si+0xf7]                             ; 18C0
        push word [si+0xf5]                             ; 18C4
        push di                                         ; 18C8
        mov ax,0x1                                      ; 18C9
        push ax                                         ; 18CC
        callf L1_07B2, R1_18D0, R1_1372                 ; 18CD far seg1
        or_ ax,ax                                       ; 18D2
        jnz short L1_190A                               ; 18D4
        mov ax,[si+0xf1]                                ; 18D6
        add_ ax,ax                                      ; 18DA
        sub_ dx,dx                                      ; 18DC
        add ax,[si+0x4d]                                ; 18DE
        adc dx,[si+0x4f]                                ; 18E1
        mov bx,[bp-0x2]                                 ; 18E4
        cmp dx,[bx+0x12]                                ; 18E7
        jc short L1_18F8                                ; 18EA
        ja short L1_18F3                                ; 18EC
        cmp ax,[bx+0x10]                                ; 18EE
        jc short L1_18F8                                ; 18F1

L1_18F3:
        mov ax,0x1                                      ; 18F3
        jmp short L1_18FA                               ; 18F6

L1_18F8:
        xor_ ax,ax                                      ; 18F8

L1_18FA:
        mov [si+0xed],ax                                ; 18FA
        mov byte [si+0x48],0x0                          ; 18FE
        sub_ ax,ax                                      ; 1902
        sub [si+0x49],di                                ; 1904
        sbb [si+0x4b],ax                                ; 1907

L1_190A:
        mov [si+0xdf],di                                ; 190A

L1_190E:
        pop si                                          ; 190E
        pop di                                          ; 190F
        mov_ sp,bp                                      ; 1910
        pop bp                                          ; 1912
        retf 0x2                                        ; 1913

; copy captured bytes from the DMA buffer to the wave-in queue
record_copy:
        push bp                                         ; 1916
        mov_ bp,sp                                      ; 1917
        mov bx,[bp+0xc]                                 ; 1919
        test byte [bx+0x2a],0x2                         ; 191C
        jz short L1_1950                                ; 1920
        and byte [bx+0x2a],0xfd                         ; 1922
        push word [bp+0xa]                              ; 1926
        push word [bp+0x8]                              ; 1929
        push word [bp+0x6]                              ; 192C
        mov al,[bx+0x6b]                                ; 192F
        and ax,strict word 0x1                          ; 1932
        cmp ax,strict word 0x1                          ; 1935
        sbb_ ax,ax                                      ; 1938
        inc ax                                          ; 193A
        push ax                                         ; 193B
        call dc_mean                                    ; 193C
        mov bx,[bp+0xc]                                 ; 193F
        mov [bx+0xa0],ax                                ; 1942
        test byte [bx+0x6c],0x40                        ; 1946
        jz short L1_1950                                ; 194A
        push ax                                         ; 194C
        call L1_1D44                                    ; 194D

L1_1950:
        mov bx,[bp+0xc]                                 ; 1950
        test byte [bx+0x6c],0x40                        ; 1953
        jz short L1_197A                                ; 1957
        push word [bp+0xa]                              ; 1959
        push word [bp+0x8]                              ; 195C
        push word [bp+0x6]                              ; 195F
        push word [bx+0xa0]                             ; 1962
        call agc_convert                                ; 1966
        push word [bp+0xc]                              ; 1969
        push word [bp+0xa]                              ; 196C
        push word [bp+0x8]                              ; 196F
        mov ax,[bp+0x6]                                 ; 1972
        shr ax,1                                        ; 1975
        push ax                                         ; 1977
        jmp short L1_19AA                               ; 1978

L1_197A:
        cmp word [bx+0xa0],byte +0x0                    ; 197A
        jz short L1_199E                                ; 197F
        push word [bp+0xa]                              ; 1981
        push word [bp+0x8]                              ; 1984
        push word [bp+0x6]                              ; 1987
        mov al,[bx+0x6b]                                ; 198A
        and ax,strict word 0x1                          ; 198D
        cmp ax,strict word 0x1                          ; 1990
        sbb_ ax,ax                                      ; 1993
        inc ax                                          ; 1995
        push ax                                         ; 1996
        push word [bx+0xa0]                             ; 1997
        call dc_remove                                  ; 199B

L1_199E:
        push word [bp+0xc]                              ; 199E
        push word [bp+0xa]                              ; 19A1
        push word [bp+0x8]                              ; 19A4
        push word [bp+0x6]                              ; 19A7

L1_19AA:
        call L1_0C90                                    ; 19AA
        mov_ sp,bp                                      ; 19AD
        pop bp                                          ; 19AF
        retf 0x8                                        ; 19B0
        db 0x90                                         ; 19B3

; recording interrupt service: what's new in the Audio 1 DMA buffer to the wave-in queue. Audio 1 users 1 and 2
isr_record:
        push bp                                         ; 19B4
        mov_ bp,sp                                      ; 19B5
        sub sp,byte +0x2                                ; 19B7
        push si                                         ; 19BA
        mov si,[bp+0x6]                                 ; 19BB
        test byte [si+0x6b],0x3c                        ; 19BE
        jz short L1_19E4                                ; 19C2
        push si                                         ; 19C4
        mov al,[si+0x71]                                ; 19C5
        push ax                                         ; 19C8
        push cs                                         ; 19C9
        call dsp_write                                  ; 19CA
        push si                                         ; 19CD
        mov al,[si+0x3d]                                ; 19CE
        dec al                                          ; 19D1
        push ax                                         ; 19D3
        push cs                                         ; 19D4
        call dsp_write                                  ; 19D5
        push si                                         ; 19D8
        mov ax,[si+0x3d]                                ; 19D9
        dec ax                                          ; 19DC
        mov_ al,ah                                      ; 19DD
        push ax                                         ; 19DF
        push cs                                         ; 19E0
        call dsp_write                                  ; 19E1

L1_19E4:
        mov bx,[si+0x65]                                ; 19E4
        add word [bx+0x14],byte +0x1                    ; 19E7
        adc word [bx+0x16],byte +0x0                    ; 19EB
        push si                                         ; 19EF
        push cs                                         ; 19F0
        call dma_pos_record                             ; 19F1
        mov [bp-0x2],ax                                 ; 19F4
        cmp ax,strict word 0xffff                       ; 19F7
        jnz short L1_1A01                               ; 19FA
        mov word [bp-0x2],0x0                           ; 19FC

L1_1A01:
        mov ax,[si+0x3d]                                ; 1A01
        add_ ax,ax                                      ; 1A04
        sub ax,[bp-0x2]                                 ; 1A06
        mov [bp-0x2],ax                                 ; 1A09
        test byte [si+0x6b],0x3c                        ; 1A0C
        jz short L1_1A1F                                ; 1A10
        sub_ dx,dx                                      ; 1A12
        div word [si+0x3f]                              ; 1A14
        mul word [si+0x3f]                              ; 1A17
        mov [bp-0x2],ax                                 ; 1A1A
        jmp short L1_1A28                               ; 1A1D

L1_1A1F:
        mov ax,[si+0x3f]                                ; 1A1F
        dec ax                                          ; 1A22
        not ax                                          ; 1A23
        and [bp-0x2],ax                                 ; 1A25

L1_1A28:
        test byte [si+0x2a],0x10                        ; 1A28
        jnz short L1_1A34                               ; 1A2C
        or byte [si+0x2a],0x10                          ; 1A2E
        jmp short L1_1A80                               ; 1A32

L1_1A34:
        mov ax,[si+0x22]                                ; 1A34
        cmp [bp-0x2],ax                                 ; 1A37
        jna short L1_1A52                               ; 1A3A
        mov cx,[bp-0x2]                                 ; 1A3C
        sub_ cx,ax                                      ; 1A3F
        jz short L1_1A80                                ; 1A41
        push si                                         ; 1A43
        mov ax,[si+0x41]                                ; 1A44
        mov dx,[si+0x43]                                ; 1A47
        add ax,[si+0x22]                                ; 1A4A
        push dx                                         ; 1A4D
        push ax                                         ; 1A4E
        push cx                                         ; 1A4F
        jmp short L1_1A7C                               ; 1A50

L1_1A52:
        mov cx,[si+0x3d]                                ; 1A52
        add_ cx,cx                                      ; 1A55
        sub_ cx,ax                                      ; 1A57
        jz short L1_1A6C                                ; 1A59
        push si                                         ; 1A5B
        mov ax,[si+0x41]                                ; 1A5C
        mov dx,[si+0x43]                                ; 1A5F
        add ax,[si+0x22]                                ; 1A62
        push dx                                         ; 1A65
        push ax                                         ; 1A66
        push cx                                         ; 1A67
        push cs                                         ; 1A68
        call record_copy                                ; 1A69

L1_1A6C:
        cmp word [bp-0x2],byte +0x0                     ; 1A6C
        jz short L1_1A80                                ; 1A70
        push si                                         ; 1A72
        push word [si+0x43]                             ; 1A73
        push word [si+0x41]                             ; 1A76
        push word [bp-0x2]                              ; 1A79

L1_1A7C:
        push cs                                         ; 1A7C
        call record_copy                                ; 1A7D

L1_1A80:
        mov ax,[bp-0x2]                                 ; 1A80
        mov [si+0x22],ax                                ; 1A83
        pop si                                          ; 1A86
        mov_ sp,bp                                      ; 1A87
        pop bp                                          ; 1A89
        retf 0x2                                        ; 1A8A
        db 0x90                                         ; 1A8D
        retf 0x2                                        ; 1A8E
        db 0x90, 0x00, 0x00                             ; 1A91

; VxD API 0004: DMA position of the running transfer
vxd_dma_pos:
        push bp                                         ; 1A94
        mov_ bp,sp                                      ; 1A95
        push bx                                         ; 1A97
        push ecx                                        ; 1A98
        mov bx,[bp+0x6]                                 ; 1A9A
        mov ecx,[bp+0x8]                                ; 1A9D
        mov dx,0x4                                      ; 1AA1
        call far [0x10]                                 ; 1AA4
        pop ecx                                         ; 1AA8
        pop bx                                          ; 1AAA
        mov_ sp,bp                                      ; 1AAB
        pop bp                                          ; 1AAD
        retf 0x6                                        ; 1AAE

; VxD API 0007: PIO emulation buffer
vxd_pio_buffer:
        push bp                                         ; 1AB1
        mov_ bp,sp                                      ; 1AB2
        push bx                                         ; 1AB4
        push ecx                                        ; 1AB5
        mov ax,[bp+0x8]                                 ; 1AB7
        mov bl,[bp+0x6]                                 ; 1ABA
        xor_ bh,bh                                      ; 1ABD
        mov ecx,[bp+0xa]                                ; 1ABF
        mov dx,0x7                                      ; 1AC3
        call far [0x10]                                 ; 1AC6
        pop ecx                                         ; 1ACA
        pop bx                                          ; 1ACC
        mov_ sp,bp                                      ; 1ACD
        pop bp                                          ; 1ACF
        retf 0x8                                        ; 1AD0

L1_1AD3:
        push bp                                         ; 1AD3
        mov_ bp,sp                                      ; 1AD4
        push si                                         ; 1AD6
        push di                                         ; 1AD7
        push ds                                         ; 1AD8
        cld                                             ; 1AD9
        lds si,[bp+0x8]                                 ; 1ADA
        les di,[bp+0xc]                                 ; 1ADD
        mov cx,[bp+0x6]                                 ; 1AE0
        shr cx,1                                        ; 1AE3
        rep movsw                                       ; 1AE5
        adc_ cl,cl                                      ; 1AE7
        rep movsb                                       ; 1AE9
        pop ds                                          ; 1AEB
        pop di                                          ; 1AEC
        pop si                                          ; 1AED
        mov_ sp,bp                                      ; 1AEE
        pop bp                                          ; 1AF0
        retf 0xa                                        ; 1AF1

L1_1AF4:
        push bp                                         ; 1AF4
        mov_ bp,sp                                      ; 1AF5
        push si                                         ; 1AF7
        push di                                         ; 1AF8
        push ds                                         ; 1AF9
        lds si,[bp+0x6]                                 ; 1AFA
        mov cx,[bp+0x4]                                 ; 1AFD
        jcxz L1_1B2F                                    ; 1B00
        cld                                             ; 1B02
        les di,[bp+0xa]                                 ; 1B03
        mov_ ax,si                                      ; 1B06
        add_ ax,cx                                      ; 1B08
        sbb_ bx,bx                                      ; 1B0A
        and_ ax,bx                                      ; 1B0C
        sub_ cx,ax                                      ; 1B0E

L1_1B10:
        jcxz L1_1B2F                                    ; 1B10
        shr cx,1                                        ; 1B12
        rep movsw                                       ; 1B14
        adc_ cl,cl                                      ; 1B16
        rep movsb                                       ; 1B18
        or_ si,si                                       ; 1B1A
        jnz short L1_1B2F                               ; 1B1C
        mov_ cx,ax                                      ; 1B1E
        mov ax,ds                                       ; 1B20
        db 0x05                                         ; 1B22 add ax,0xffff
R1_1B23: dw 0xFFFF                                      ; KERNEL.__AHINCR
        jcxz L1_1B2B                                    ; 1B25
        mov ds,ax                                       ; 1B27
        jmp short L1_1B10                               ; 1B29

L1_1B2B:
        mov_ dx,ax                                      ; 1B2B
        jmp short L1_1B31                               ; 1B2D

L1_1B2F:
        mov dx,ds                                       ; 1B2F

L1_1B31:
        mov_ ax,si                                      ; 1B31
        pop ds                                          ; 1B33
        pop di                                          ; 1B34
        pop si                                          ; 1B35
        mov_ sp,bp                                      ; 1B36
        pop bp                                          ; 1B38
        ret 0xa                                         ; 1B39

L1_1B3C:
        push bp                                         ; 1B3C
        mov_ bp,sp                                      ; 1B3D
        push si                                         ; 1B3F
        push di                                         ; 1B40
        push ds                                         ; 1B41
        cld                                             ; 1B42
        lds si,[bp+0x6]                                 ; 1B43
        les di,[bp+0xa]                                 ; 1B46
        mov cx,[bp+0x4]                                 ; 1B49
        mov_ ax,di                                      ; 1B4C
        add_ ax,cx                                      ; 1B4E
        sbb_ bx,bx                                      ; 1B50
        and_ ax,bx                                      ; 1B52
        sub_ cx,ax                                      ; 1B54

L1_1B56:
        jcxz L1_1B75                                    ; 1B56
        shr cx,1                                        ; 1B58
        rep movsw                                       ; 1B5A
        adc_ cl,cl                                      ; 1B5C
        rep movsb                                       ; 1B5E
        or_ di,di                                       ; 1B60
        jnz short L1_1B75                               ; 1B62
        mov_ cx,ax                                      ; 1B64
        mov ax,es                                       ; 1B66
        db 0x05                                         ; 1B68 add ax,0x1b23
R1_1B69: dw R1_1B23                                     ; KERNEL.__AHINCR
        jcxz L1_1B71                                    ; 1B6B
        mov es,ax                                       ; 1B6D
        jmp short L1_1B56                               ; 1B6F

L1_1B71:
        mov_ dx,ax                                      ; 1B71
        jmp short L1_1B77                               ; 1B73

L1_1B75:
        mov dx,es                                       ; 1B75

L1_1B77:
        mov_ ax,di                                      ; 1B77
        pop ds                                          ; 1B79
        pop di                                          ; 1B7A
        pop si                                          ; 1B7B
        mov_ sp,bp                                      ; 1B7C
        pop bp                                          ; 1B7E
        ret 0xa                                         ; 1B7F

L1_1B82:
        push bp                                         ; 1B82
        mov_ bp,sp                                      ; 1B83
        push si                                         ; 1B85
        push di                                         ; 1B86
        push ds                                         ; 1B87
        lds si,[bp+0x6]                                 ; 1B88
        mov cx,[bp+0x4]                                 ; 1B8B
        jcxz L1_1BC8                                    ; 1B8E
        cld                                             ; 1B90
        les di,[bp+0xa]                                 ; 1B91
        mov_ ax,si                                      ; 1B94
        add_ ax,cx                                      ; 1B96
        sbb_ bx,bx                                      ; 1B98
        and_ ax,bx                                      ; 1B9A
        sub_ cx,ax                                      ; 1B9C

L1_1B9E:
        jcxz L1_1BC8                                    ; 1B9E
        shr cx,1                                        ; 1BA0
        push ax                                         ; 1BA2
        jz short L1_1BAC                                ; 1BA3

L1_1BA5:
        lodsw                                           ; 1BA5
        stosw                                           ; 1BA6
        mov_ al,ah                                      ; 1BA7
        stosw                                           ; 1BA9
        loop L1_1BA5                                    ; 1BAA

L1_1BAC:
        adc_ cl,cl                                      ; 1BAC
        jz short L1_1BB2                                ; 1BAE
        lodsb                                           ; 1BB0
        stosw                                           ; 1BB1

L1_1BB2:
        pop ax                                          ; 1BB2
        or_ si,si                                       ; 1BB3
        jnz short L1_1BC8                               ; 1BB5
        mov_ cx,ax                                      ; 1BB7
        mov ax,ds                                       ; 1BB9
        db 0x05                                         ; 1BBB add ax,0x1b69
R1_1BBC: dw R1_1B69                                     ; KERNEL.__AHINCR
        jcxz L1_1BC4                                    ; 1BBE
        mov ds,ax                                       ; 1BC0
        jmp short L1_1B9E                               ; 1BC2

L1_1BC4:
        mov_ dx,ax                                      ; 1BC4
        jmp short L1_1BCA                               ; 1BC6

L1_1BC8:
        mov dx,ds                                       ; 1BC8

L1_1BCA:
        mov_ ax,si                                      ; 1BCA
        pop ds                                          ; 1BCC
        pop di                                          ; 1BCD
        pop si                                          ; 1BCE
        mov_ sp,bp                                      ; 1BCF
        pop bp                                          ; 1BD1
        ret 0xa                                         ; 1BD2

L1_1BD5:
        push bp                                         ; 1BD5
        mov_ bp,sp                                      ; 1BD6
        push si                                         ; 1BD8
        push di                                         ; 1BD9
        push ds                                         ; 1BDA
        cld                                             ; 1BDB
        lds si,[bp+0x6]                                 ; 1BDC
        les di,[bp+0xa]                                 ; 1BDF
        mov cx,[bp+0x4]                                 ; 1BE2
        mov_ ax,di                                      ; 1BE5
        add_ ax,cx                                      ; 1BE7
        sbb_ bx,bx                                      ; 1BE9
        and_ ax,bx                                      ; 1BEB
        sub_ cx,ax                                      ; 1BED

L1_1BEF:
        jcxz L1_1C18                                    ; 1BEF
        shr cx,1                                        ; 1BF1
        push ax                                         ; 1BF3
        jz short L1_1BFC                                ; 1BF4

L1_1BF6:
        lodsw                                           ; 1BF6
        stosb                                           ; 1BF7
        lodsw                                           ; 1BF8
        stosb                                           ; 1BF9
        loop L1_1BF6                                    ; 1BFA

L1_1BFC:
        adc_ cl,cl                                      ; 1BFC
        jz short L1_1C02                                ; 1BFE
        lodsw                                           ; 1C00
        stosb                                           ; 1C01

L1_1C02:
        pop ax                                          ; 1C02
        or_ di,di                                       ; 1C03
        jnz short L1_1C18                               ; 1C05
        mov_ cx,ax                                      ; 1C07
        mov ax,es                                       ; 1C09
        db 0x05                                         ; 1C0B add ax,0x1bbc
R1_1C0C: dw R1_1BBC                                     ; KERNEL.__AHINCR
        jcxz L1_1C14                                    ; 1C0E
        mov es,ax                                       ; 1C10
        jmp short L1_1BEF                               ; 1C12

L1_1C14:
        mov_ dx,ax                                      ; 1C14
        jmp short L1_1C1A                               ; 1C16

L1_1C18:
        mov dx,es                                       ; 1C18

L1_1C1A:
        mov_ ax,di                                      ; 1C1A
        pop ds                                          ; 1C1C
        pop di                                          ; 1C1D
        pop si                                          ; 1C1E
        mov_ sp,bp                                      ; 1C1F
        pop bp                                          ; 1C21
        ret 0xa                                         ; 1C22

L1_1C25:
        push bp                                         ; 1C25
        mov_ bp,sp                                      ; 1C26
        push di                                         ; 1C28
        cld                                             ; 1C29
        les di,[bp+0x8]                                 ; 1C2A
        mov cx,[bp+0x6]                                 ; 1C2D
        jcxz L1_1C3D                                    ; 1C30
        shr cx,1                                        ; 1C32
        mov ax,[bp+0x4]                                 ; 1C34
        rep stosw                                       ; 1C37
        adc_ cl,cl                                      ; 1C39
        rep stosb                                       ; 1C3B

L1_1C3D:
        pop di                                          ; 1C3D
        mov_ sp,bp                                      ; 1C3E
        pop bp                                          ; 1C40
        ret 0x8                                         ; 1C41
        push bp                                         ; 1C44
        mov_ bp,sp                                      ; 1C45
        push si                                         ; 1C47
        push di                                         ; 1C48
        push ds                                         ; 1C49
        cld                                             ; 1C4A
        les di,[bp+0xa]                                 ; 1C4B
        mov cx,[bp+0x8]                                 ; 1C4E
        lds si,[bp+0x4]                                 ; 1C51
        jcxz L1_1C75                                    ; 1C54
        lodsb                                           ; 1C56
        xor_ ah,ah                                      ; 1C57
        mov_ bx,cx                                      ; 1C59
        mov_ dx,si                                      ; 1C5B

L1_1C5D:
        mov_ cx,ax                                      ; 1C5D
        cmp_ bx,ax                                      ; 1C5F
        jnc short L1_1C65                               ; 1C61
        mov_ cx,bx                                      ; 1C63

L1_1C65:
        sub_ bx,cx                                      ; 1C65
        mov_ si,dx                                      ; 1C67
        shr cx,1                                        ; 1C69
        rep movsw                                       ; 1C6B
        adc_ cl,cl                                      ; 1C6D
        rep movsb                                       ; 1C6F
        or_ bx,bx                                       ; 1C71
        jnz short L1_1C5D                               ; 1C73

L1_1C75:
        pop ds                                          ; 1C75
        pop di                                          ; 1C76
        pop si                                          ; 1C77
        mov_ sp,bp                                      ; 1C78
        pop bp                                          ; 1C7A
        ret 0xa                                         ; 1C7B
        push bp                                         ; 1C7E
        mov_ bp,sp                                      ; 1C7F
        push si                                         ; 1C81
        push di                                         ; 1C82
        push ds                                         ; 1C83
        cld                                             ; 1C84
        les di,[bp+0xa]                                 ; 1C85
        mov cx,[bp+0x8]                                 ; 1C88
        lds si,[bp+0x4]                                 ; 1C8B
        jcxz L1_1CBA                                    ; 1C8E
        lodsb                                           ; 1C90
        xor_ ah,ah                                      ; 1C91
        mov_ bx,cx                                      ; 1C93
        mov_ dx,si                                      ; 1C95

L1_1C97:
        mov_ cx,ax                                      ; 1C97
        cmp_ bx,ax                                      ; 1C99
        jnc short L1_1C9F                               ; 1C9B
        mov_ cx,bx                                      ; 1C9D

L1_1C9F:
        sub_ bx,cx                                      ; 1C9F
        mov_ si,dx                                      ; 1CA1
        shr cx,1                                        ; 1CA3
        push ax                                         ; 1CA5
        jz short L1_1CAF                                ; 1CA6

L1_1CA8:
        lodsw                                           ; 1CA8
        stosw                                           ; 1CA9
        mov_ al,ah                                      ; 1CAA
        stosw                                           ; 1CAC
        loop L1_1CA8                                    ; 1CAD

L1_1CAF:
        adc_ cl,cl                                      ; 1CAF
        jz short L1_1CB5                                ; 1CB1
        lodsb                                           ; 1CB3
        stosw                                           ; 1CB4

L1_1CB5:
        pop ax                                          ; 1CB5
        or_ bx,bx                                       ; 1CB6
        jnz short L1_1C97                               ; 1CB8

L1_1CBA:
        pop ds                                          ; 1CBA
        pop di                                          ; 1CBB
        pop si                                          ; 1CBC
        mov_ sp,bp                                      ; 1CBD
        pop bp                                          ; 1CBF
        ret 0xa                                         ; 1CC0

; DCdrift: mean of the first recorded block
dc_mean:
        push bp                                         ; 1CC3
        mov_ bp,sp                                      ; 1CC4
        push si                                         ; 1CC6
        push di                                         ; 1CC7
        push ds                                         ; 1CC8
        cld                                             ; 1CC9
        xor_ ax,ax                                      ; 1CCA
        xor_ bx,bx                                      ; 1CCC
        xor_ di,di                                      ; 1CCE
        lds si,[bp+0x8]                                 ; 1CD0
        mov cx,[bp+0x6]                                 ; 1CD3
        jcxz L1_1D02                                    ; 1CD6
        cmp word [bp+0x4],byte +0x0                     ; 1CD8
        jz short L1_1CED                                ; 1CDC
        shr cx,1                                        ; 1CDE
        jcxz L1_1D02                                    ; 1CE0
        push cx                                         ; 1CE2

L1_1CE3:
        lodsw                                           ; 1CE3
        cwd                                             ; 1CE4
        add_ bx,ax                                      ; 1CE5
        adc_ di,dx                                      ; 1CE7
        loop L1_1CE3                                    ; 1CE9
        jmp short L1_1CF9                               ; 1CEB

L1_1CED:
        push cx                                         ; 1CED

L1_1CEE:
        lodsb                                           ; 1CEE
        xor al,0x80                                     ; 1CEF
        cbw                                             ; 1CF1
        cwd                                             ; 1CF2
        add_ bx,ax                                      ; 1CF3
        adc_ di,dx                                      ; 1CF5
        loop L1_1CEE                                    ; 1CF7

L1_1CF9:
        mov_ ax,bx                                      ; 1CF9
        mov_ dx,di                                      ; 1CFB
        pop cx                                          ; 1CFD
        idiv cx                                         ; 1CFE
        neg ax                                          ; 1D00

L1_1D02:
        pop ds                                          ; 1D02
        pop di                                          ; 1D03
        pop si                                          ; 1D04
        mov_ sp,bp                                      ; 1D05
        pop bp                                          ; 1D07
        ret 0x8                                         ; 1D08

; DCdrift: subtract it from the following samples
dc_remove:
        push bp                                         ; 1D0B
        mov_ bp,sp                                      ; 1D0C
        push si                                         ; 1D0E
        push di                                         ; 1D0F
        push ds                                         ; 1D10
        cld                                             ; 1D11
        lds si,[bp+0xa]                                 ; 1D12
        les di,[bp+0xa]                                 ; 1D15
        mov cx,[bp+0x8]                                 ; 1D18
        mov dx,[bp+0x4]                                 ; 1D1B
        jcxz L1_1D38                                    ; 1D1E
        cmp word [bp+0x6],byte +0x0                     ; 1D20
        jz short L1_1D32                                ; 1D24
        shr cx,1                                        ; 1D26
        jcxz L1_1D38                                    ; 1D28

L1_1D2A:
        lodsw                                           ; 1D2A
        add_ ax,dx                                      ; 1D2B
        stosw                                           ; 1D2D
        loop L1_1D2A                                    ; 1D2E
        jmp short L1_1D38                               ; 1D30

L1_1D32:
        lodsb                                           ; 1D32
        add_ al,dl                                      ; 1D33
        stosb                                           ; 1D35
        loop L1_1D32                                    ; 1D36

L1_1D38:
        pop ds                                          ; 1D38
        pop di                                          ; 1D39
        pop si                                          ; 1D3A
        mov_ sp,bp                                      ; 1D3B
        pop bp                                          ; 1D3D
        ret 0xa                                         ; 1D3E
        db 0x00, 0x00, 0x00                             ; 1D41

L1_1D44:
        push bp                                         ; 1D44
        mov_ bp,sp                                      ; 1D45
        mov byte [0x110],0x5                            ; 1D47
        mov word [0x111],0x72a                          ; 1D4C
        mov byte [0x113],0x4                            ; 1D52
        mov ax,[bp+0x4]                                 ; 1D57
        test ah,0x80                                    ; 1D5A
        mov ax,0x8000                                   ; 1D5D
        jnz short L1_1D63                               ; 1D60
        dec ax                                          ; 1D62

L1_1D63:
        mov [0x114],ax                                  ; 1D63
        mov_ sp,bp                                      ; 1D66
        pop bp                                          ; 1D68
        ret 0x2                                         ; 1D69

; 16-bit recording converted with an adaptive gain stage
agc_convert:
        push bp                                         ; 1D6C
        mov_ bp,sp                                      ; 1D6D
        push si                                         ; 1D6F
        push di                                         ; 1D70
        push ds                                         ; 1D71
        cld                                             ; 1D72
        push bp                                         ; 1D73
        les di,[bp+0x8]                                 ; 1D74
        mov_ si,di                                      ; 1D77
        mov ax,[bp+0x6]                                 ; 1D79
        shr ax,1                                        ; 1D7C
        jnz short L1_1D83                               ; 1D7E
        jmp near L1_1E5A                                ; 1D80

L1_1D83:
        mov [0x116],ax                                  ; 1D83
        mov ax,[bp+0x4]                                 ; 1D86
        mov [0x118],ax                                  ; 1D89
        mov cl,[0x110]                                  ; 1D8C
        mov bx,[0x111]                                  ; 1D90
        mov bp,[bx-0x4]                                 ; 1D94
        mov dx,[bx-0x2]                                 ; 1D97
        dec byte [0x113]                                ; 1D9A
        jnz short L1_1E09                               ; 1D9E
        mov byte [0x113],0x4                            ; 1DA0

L1_1DA5:
        db 0x26, 0xAD                                   ; 1DA5 es lodsw
        add ax,[0x118]                                  ; 1DA7
        jno short L1_1DB0                               ; 1DAB
        mov ax,[0x114]                                  ; 1DAD

L1_1DB0:
        sar ax,cl                                       ; 1DB0
        cmp_ ax,dx                                      ; 1DB2
        jg short L1_1E2D                                ; 1DB4
        sub_ ax,bp                                      ; 1DB6
        jl short L1_1E31                                ; 1DB8
        xchg ax,si                                      ; 1DBA
        mov ch,[bx+si]                                  ; 1DBB
        xchg ax,si                                      ; 1DBD
        mov_ al,ch                                      ; 1DBE
        stosb                                           ; 1DC0
        cmp word [0x116],byte +0x1                      ; 1DC1
        jz short L1_1DDC                                ; 1DC6
        mov al,[es:si-0x1]                              ; 1DC8
        xor al,[es:si+0x1]                              ; 1DCC
        js short L1_1DDC                                ; 1DD0
        dec word [0x116]                                ; 1DD2
        jnz short L1_1DA5                               ; 1DD6
        inc word [0x116]                                ; 1DD8

L1_1DDC:
        cmp bx,0x72a                                    ; 1DDC
        jz short L1_1DE8                                ; 1DE0
        add bx,0x204                                    ; 1DE2
        jmp short L1_1DF9                               ; 1DE6

L1_1DE8:
        cmp cl,0x5                                      ; 1DE8
        jnz short L1_1DF4                               ; 1DEB
        mov byte [0x113],0xff                           ; 1DED
        jmp short L1_1E25                               ; 1DF2

L1_1DF4:
        dec cl                                          ; 1DF4
        mov bx,0x11e                                    ; 1DF6

L1_1DF9:
        mov [0x111],bx                                  ; 1DF9
        mov [0x110],cl                                  ; 1DFD
        mov bp,[bx-0x4]                                 ; 1E01
        mov dx,[bx-0x2]                                 ; 1E04
        jmp short L1_1E25                               ; 1E07

L1_1E09:
        db 0x26, 0xAD                                   ; 1E09 es lodsw
        add ax,[0x118]                                  ; 1E0B
        jno short L1_1E14                               ; 1E0F
        mov ax,[0x114]                                  ; 1E11

L1_1E14:
        sar ax,cl                                       ; 1E14
        cmp_ ax,dx                                      ; 1E16
        jg short L1_1E2D                                ; 1E18
        sub_ ax,bp                                      ; 1E1A
        jl short L1_1E31                                ; 1E1C
        xchg ax,si                                      ; 1E1E
        mov ch,[bx+si]                                  ; 1E1F
        xchg ax,si                                      ; 1E21
        mov_ al,ch                                      ; 1E22
        stosb                                           ; 1E24

L1_1E25:
        dec word [0x116]                                ; 1E25
        jnz short L1_1E09                               ; 1E29
        jmp short L1_1E5A                               ; 1E2B

L1_1E2D:
        mov al,0xff                                     ; 1E2D
        jmp short L1_1E33                               ; 1E2F

L1_1E31:
        mov al,0x0                                      ; 1E31

L1_1E33:
        stosb                                           ; 1E33
        mov byte [0x113],0x4                            ; 1E34
        cmp bx,0x11e                                    ; 1E39
        jz short L1_1E45                                ; 1E3D
        sub bx,0x204                                    ; 1E3F
        jmp short L1_1E4A                               ; 1E43

L1_1E45:
        inc cl                                          ; 1E45
        mov bx,0x72a                                    ; 1E47

L1_1E4A:
        mov [0x111],bx                                  ; 1E4A
        mov [0x110],cl                                  ; 1E4E
        mov bp,[bx-0x4]                                 ; 1E52
        mov dx,[bx-0x2]                                 ; 1E55
        jmp short L1_1E25                               ; 1E58

L1_1E5A:
        pop bp                                          ; 1E5A
        pop ds                                          ; 1E5B
        pop di                                          ; 1E5C
        pop si                                          ; 1E5D
        mov_ sp,bp                                      ; 1E5E
        pop bp                                          ; 1E60
        ret 0x8                                         ; 1E61

L1_1E64:
        mov ax,ds                                       ; 1E64
        nop                                             ; 1E66
        inc bp                                          ; 1E67
        push bp                                         ; 1E68
        mov_ bp,sp                                      ; 1E69
        push ds                                         ; 1E6B
        mov ds,ax                                       ; 1E6C
        push word [0xa76]                               ; 1E6E
        push word [0xa78]                               ; 1E72
        push word [0xa7a]                               ; 1E76
        push word [0xa7e]                               ; 1E7A
        push word [0xa7c]                               ; 1E7E
        callf L3_503E, R1_1E85, 0xFFFF                  ; 1E82 far seg3
        sub bp,byte +0x2                                ; 1E87
        mov_ sp,bp                                      ; 1E8A
        pop ds                                          ; 1E8C
        pop bp                                          ; 1E8D
        dec bp                                          ; 1E8E
        retf                                            ; 1E8F
R1_1E90: dw 0xFFFF                                      ; 1E90 KERNEL.__WINFLAGS
        push ax                                         ; 1E92
        push bx                                         ; 1E93
        push cx                                         ; 1E94
        push dx                                         ; 1E95
        push es                                         ; 1E96
        db 0xB8                                         ; 1E97 mov ax,0x1e90
R1_1E98: dw R1_1E90                                     ; KERNEL.__WINFLAGS
        or_ ax,ax                                       ; 1E9A
        jns short L1_1EB0                               ; 1E9C
        pop es                                          ; 1E9E
        pop dx                                          ; 1E9F
        pop cx                                          ; 1EA0
        pop bx                                          ; 1EA1
        pop ax                                          ; 1EA2
        callp R1_1EA4, 0xFFFB, 0x0000                   ; 1EA3 KERNEL.InitTask
        jmp short L1_1EAE                               ; 1EA8
        db 0x90, 0x33, 0xC0, 0xCB                       ; 1EAA

L1_1EAE:
        jmp short L1_1ED4                               ; 1EAE

L1_1EB0:
        nop                                             ; 1EB0
        nop                                             ; 1EB1
        jmp short L1_1EC2                               ; 1EB2
        db 0x57, 0x9A                                   ; 1EB4
R1_1EB6: dw 0xFFFF, 0x0000                              ; 1EB6 KERNEL.GetModuleUsage
        db 0x48, 0x74, 0x05, 0x40, 0x83, 0xC4, 0x0A, 0xCB ; 1EBA

L1_1EC2:
        pop es                                          ; 1EC2
        pop dx                                          ; 1EC3
        pop cx                                          ; 1EC4
        pop bx                                          ; 1EC5
        pop ax                                          ; 1EC6
        jmp short L1_1ED4                               ; 1EC7
        db 0x43, 0x44, 0x44, 0x01, 0x00, 0x16, 0x00, 0x1E, 0x00, 0x42, 0x00 ; 1EC9

L1_1ED4:
        mov ax,ds                                       ; 1ED4
        nop                                             ; 1ED6
        inc bp                                          ; 1ED7
        push bp                                         ; 1ED8
        mov_ bp,sp                                      ; 1ED9
        push ds                                         ; 1EDB
        mov ds,ax                                       ; 1EDC
        push di                                         ; 1EDE
        push si                                         ; 1EDF
        mov [0xa76],di                                  ; 1EE0
        mov [0xa78],ds                                  ; 1EE4
        mov [0xa7a],cx                                  ; 1EE8
        mov [0xa7c],bx                                  ; 1EEC
        mov [0xa7e],si                                  ; 1EF0
        jcxz L1_1F04                                    ; 1EF4
        push ds                                         ; 1EF6
        xor_ ax,ax                                      ; 1EF7
        push ax                                         ; 1EF9
        push cx                                         ; 1EFA
        callp R1_1EFC, 0xFFFF, 0x0000                   ; 1EFB KERNEL.LocalInit
        or_ ax,ax                                       ; 1F00
        jz short L1_1F5C                                ; 1F02

L1_1F04:
        callp R1_1F05, 0xFFFF, 0x0000                   ; 1F04 KERNEL.GetVersion
        xchg al,ah                                      ; 1F09
        mov [0xa9c],ax                                  ; 1F0B
        mov ah,0x30                                     ; 1F0E
        test word [cs:0x1e90],0x1                       ; 1F10
        jz short L1_1F20                                ; 1F17
        callp R1_1F1A, 0xFFFF, 0x0000                   ; 1F19 KERNEL.DOS3Call
        jmp short L1_1F22                               ; 1F1E

L1_1F20:
        int 0x21                                        ; 1F20

L1_1F22:
        mov [0xaa0],ax                                  ; 1F22
        xchg al,ah                                      ; 1F25
        mov [0xa9e],ax                                  ; 1F27
        test word [cs:0x1e90],0x1                       ; 1F2A
        jnz short L1_1F38                               ; 1F31
        mov al,0x0                                      ; 1F33
        mov [0xaa3],al                                  ; 1F35

L1_1F38:
        callf L1_1F7E, R1_1F3B, R1_16B4                 ; 1F38 far seg1
        callf L1_20D4, R1_1F40, R1_1F3B                 ; 1F3D far seg1
        inc byte [0xa80]                                ; 1F42
        push word [0xac0]                               ; 1F46
        push word [0xabe]                               ; 1F4A
        push word [0xabc]                               ; 1F4E
        callf L1_1E64, R1_1F55, R1_1F40                 ; 1F52 far seg1
        add sp,byte +0x6                                ; 1F57
        pop si                                          ; 1F5A
        pop di                                          ; 1F5B

L1_1F5C:
        sub bp,byte +0x2                                ; 1F5C
        mov_ sp,bp                                      ; 1F5F
        pop ds                                          ; 1F61
        pop bp                                          ; 1F62
        dec bp                                          ; 1F63
        retf                                            ; 1F64
        mov ax,ds                                       ; 1F65
        nop                                             ; 1F67
        inc bp                                          ; 1F68
        push bp                                         ; 1F69
        mov_ bp,sp                                      ; 1F6A
        push ds                                         ; 1F6C
        mov ds,ax                                       ; 1F6D
        mov ax,0x1                                      ; 1F6F
        sub bp,byte +0x2                                ; 1F72
        mov_ sp,bp                                      ; 1F75
        pop ds                                          ; 1F77
        pop bp                                          ; 1F78
        dec bp                                          ; 1F79
        retf 0xa                                        ; 1F7A
        db 0x00                                         ; 1F7D

L1_1F7E:
        mov ax,ds                                       ; 1F7E
        nop                                             ; 1F80
        inc bp                                          ; 1F81
        push bp                                         ; 1F82
        mov_ bp,sp                                      ; 1F83
        push ds                                         ; 1F85
        mov ds,ax                                       ; 1F86
        mov cx,[0xad8]                                  ; 1F88
        jcxz L1_1FA2                                    ; 1F8C
        xor_ si,si                                      ; 1F8E
        mov ax,[0xada]                                  ; 1F90
        mov dx,[0xadc]                                  ; 1F93
        xor_ bx,bx                                      ; 1F97
        call far [0xad6]                                ; 1F99
        jnc short L1_1FA2                               ; 1F9D
        jmp near L1_22B0                                ; 1F9F

L1_1FA2:
        mov si,0xae2                                    ; 1FA2
        mov di,0xae2                                    ; 1FA5
        call L1_204E                                    ; 1FA8
        mov si,0xae2                                    ; 1FAB
        mov di,0xae2                                    ; 1FAE
        call L1_204E                                    ; 1FB1
        mov si,0xae2                                    ; 1FB4
        mov di,0xae2                                    ; 1FB7
        call L1_204E                                    ; 1FBA
        sub bp,byte +0x2                                ; 1FBD
        mov_ sp,bp                                      ; 1FC0
        pop ds                                          ; 1FC2
        pop bp                                          ; 1FC3
        dec bp                                          ; 1FC4
        retf                                            ; 1FC5

L1_1FC6:
        mov ax,ds                                       ; 1FC6
        nop                                             ; 1FC8
        inc bp                                          ; 1FC9
        push bp                                         ; 1FCA
        mov_ bp,sp                                      ; 1FCB
        push ds                                         ; 1FCD
        mov ds,ax                                       ; 1FCE
        push si                                         ; 1FD0
        push di                                         ; 1FD1
        mov cx,0x100                                    ; 1FD2
        jmp short L1_1FE6                               ; 1FD5
        mov ax,ds                                       ; 1FD7
        nop                                             ; 1FD9
        inc bp                                          ; 1FDA
        push bp                                         ; 1FDB
        mov_ bp,sp                                      ; 1FDC
        push ds                                         ; 1FDE
        mov ds,ax                                       ; 1FDF
        push si                                         ; 1FE1
        push di                                         ; 1FE2
        mov cx,0x101                                    ; 1FE3

L1_1FE6:
        mov [0xac9],ch                                  ; 1FE6
        push cx                                         ; 1FEA
        or_ cl,cl                                       ; 1FEB
        jnz short L1_2001                               ; 1FED
        mov si,0xbc8                                    ; 1FEF
        mov di,0xbc8                                    ; 1FF2
        call L1_204E                                    ; 1FF5
        mov si,0xae2                                    ; 1FF8
        mov di,0xae2                                    ; 1FFB
        call L1_204E                                    ; 1FFE

L1_2001:
        mov si,0xae2                                    ; 2001
        mov di,0xae2                                    ; 2004
        call L1_204E                                    ; 2007
        mov si,0xae2                                    ; 200A
        mov di,0xae2                                    ; 200D
        call L1_204E                                    ; 2010
        call L1_2308                                    ; 2013
        call L1_2308                                    ; 2016
        pop ax                                          ; 2019
        pop di                                          ; 201A
        pop si                                          ; 201B
        sub bp,byte +0x2                                ; 201C
        mov_ sp,bp                                      ; 201F
        pop ds                                          ; 2021
        pop bp                                          ; 2022
        dec bp                                          ; 2023
        retf                                            ; 2024
        mov cx,[0xad8]                                  ; 2025
        jcxz L1_2032                                    ; 2029
        mov bx,0x2                                      ; 202B
        call far [0xad6]                                ; 202E

L1_2032:
        push ds                                         ; 2032
        lds dx,[0xa8a]                                  ; 2033
        mov ax,0x2500                                   ; 2037
        test word [cs:0x1e90],0x1                       ; 203A
        jz short L1_204A                                ; 2041
        callp R1_2044, R1_1F1A, 0x0000                  ; 2043 KERNEL.DOS3Call
        jmp short L1_204C                               ; 2048

L1_204A:
        int 0x21                                        ; 204A

L1_204C:
        pop ds                                          ; 204C
        ret                                             ; 204D

L1_204E:
        cmp_ si,di                                      ; 204E
        jnc short L1_2060                               ; 2050
        sub di,byte +0x4                                ; 2052
        mov ax,[di]                                     ; 2055
        or ax,[di+0x2]                                  ; 2057
        jz short L1_204E                                ; 205A
        call far [di]                                   ; 205C
        jmp short L1_204E                               ; 205E

L1_2060:
        ret                                             ; 2060
        db 0x00                                         ; 2061

L1_2062:
        mov ax,ds                                       ; 2062
        nop                                             ; 2064
        inc bp                                          ; 2065
        push bp                                         ; 2066
        mov_ bp,sp                                      ; 2067
        push ds                                         ; 2069
        mov ds,ax                                       ; 206A
        mov ax,0xfc                                     ; 206C
        push ax                                         ; 206F
        push cs                                         ; 2070
        call L1_20BD                                    ; 2071
        mov ax,0xff                                     ; 2074
        push ax                                         ; 2077
        push cs                                         ; 2078
        call L1_20BD                                    ; 2079
        sub bp,byte +0x2                                ; 207C
        mov_ sp,bp                                      ; 207F
        pop ds                                          ; 2081
        pop bp                                          ; 2082
        dec bp                                          ; 2083
        retf                                            ; 2084
        db 0x00                                         ; 2085

L1_2086:
        mov ax,ds                                       ; 2086
        nop                                             ; 2088
        inc bp                                          ; 2089
        push bp                                         ; 208A
        mov_ bp,sp                                      ; 208B
        push ds                                         ; 208D
        mov ds,ax                                       ; 208E
        push si                                         ; 2090
        push di                                         ; 2091
        push ds                                         ; 2092
        pop es                                          ; 2093
        mov dx,[bp+0x6]                                 ; 2094
        mov si,0xaea                                    ; 2097

L1_209A:
        lodsw                                           ; 209A
        cmp_ ax,dx                                      ; 209B
        jz short L1_20AF                                ; 209D
        inc ax                                          ; 209F
        xchg ax,si                                      ; 20A0
        jz short L1_20AF                                ; 20A1
        xchg ax,di                                      ; 20A3
        xor_ ax,ax                                      ; 20A4
        mov cx,0xffff                                   ; 20A6
        repne scasb                                     ; 20A9
        mov_ si,di                                      ; 20AB
        jmp short L1_209A                               ; 20AD

L1_20AF:
        xchg ax,si                                      ; 20AF
        pop di                                          ; 20B0
        pop si                                          ; 20B1
        sub bp,byte +0x2                                ; 20B2
        mov_ sp,bp                                      ; 20B5
        pop ds                                          ; 20B7
        pop bp                                          ; 20B8
        dec bp                                          ; 20B9
        retf 0x2                                        ; 20BA

L1_20BD:
        mov ax,ds                                       ; 20BD
        nop                                             ; 20BF
        inc bp                                          ; 20C0
        push bp                                         ; 20C1
        mov_ bp,sp                                      ; 20C2
        push ds                                         ; 20C4
        mov ds,ax                                       ; 20C5
        push di                                         ; 20C7
        pop di                                          ; 20C8
        sub bp,byte +0x2                                ; 20C9
        mov_ sp,bp                                      ; 20CC
        pop ds                                          ; 20CE
        pop bp                                          ; 20CF
        dec bp                                          ; 20D0
        retf 0x2                                        ; 20D1

L1_20D4:
        mov ax,ds                                       ; 20D4
        nop                                             ; 20D6
        inc bp                                          ; 20D7
        push bp                                         ; 20D8
        mov_ bp,sp                                      ; 20D9
        push ds                                         ; 20DB
        mov ds,ax                                       ; 20DC
        push ds                                         ; 20DE
        callp R1_20E0, 0xFFFF, 0x0000                   ; 20DF KERNEL.GetDOSEnvironment
        or_ ax,ax                                       ; 20E4
        jz short L1_20EB                                ; 20E6
        mov dx,0x0                                      ; 20E8

L1_20EB:
        mov_ bx,dx                                      ; 20EB
        mov es,dx                                       ; 20ED
        xor_ ax,ax                                      ; 20EF
        xor_ si,si                                      ; 20F1
        xor_ di,di                                      ; 20F3
        mov cx,0xffff                                   ; 20F5
        or_ bx,bx                                       ; 20F8
        jz short L1_210A                                ; 20FA
        cmp byte [es:0x0],0x0                           ; 20FC
        jz short L1_210A                                ; 2102

L1_2104:
        repne scasb                                     ; 2104
        inc si                                          ; 2106
        scasb                                           ; 2107
        jnz short L1_2104                               ; 2108

L1_210A:
        mov_ ax,di                                      ; 210A
        inc ax                                          ; 210C
        and al,0xfe                                     ; 210D
        inc si                                          ; 210F
        mov_ di,si                                      ; 2110
        shl si,1                                        ; 2112
        mov cx,0x9                                      ; 2114
        call L1_22B6                                    ; 2117
        push ax                                         ; 211A
        mov_ ax,si                                      ; 211B
        call L1_22B6                                    ; 211D
        mov [0xac0],ax                                  ; 2120
        push es                                         ; 2123
        push ds                                         ; 2124
        pop es                                          ; 2125
        pop ds                                          ; 2126
        mov_ cx,di                                      ; 2127
        mov_ bx,ax                                      ; 2129
        xor_ si,si                                      ; 212B
        pop di                                          ; 212D
        dec cx                                          ; 212E
        jcxz L1_213E                                    ; 212F

L1_2131:
        mov [es:bx],di                                  ; 2131
        inc bx                                          ; 2134
        inc bx                                          ; 2135

L1_2136:
        lodsb                                           ; 2136
        stosb                                           ; 2137
        or_ al,al                                       ; 2138
        jnz short L1_2136                               ; 213A
        loop L1_2131                                    ; 213C

L1_213E:
        mov [es:bx],cx                                  ; 213E
        pop ds                                          ; 2141
        sub bp,byte +0x2                                ; 2142
        mov_ sp,bp                                      ; 2145
        pop ds                                          ; 2147
        pop bp                                          ; 2148
        dec bp                                          ; 2149
        retf                                            ; 214A
        db 0x00                                         ; 214B
        callf L1_22E4, R1_214F, R1_1F55                 ; 214C far seg1
        mov ds,ax                                       ; 2151
        mov ax,0x3                                      ; 2153

L1_2156:
        push ax                                         ; 2156
        push ax                                         ; 2157
        push cs                                         ; 2158
        call L1_2062                                    ; 2159
        push cs                                         ; 215C
        call L1_20BD                                    ; 215D
        push cs                                         ; 2160
        call L1_2086                                    ; 2161
        xor_ bx,bx                                      ; 2164
        or_ ax,ax                                       ; 2166
        jz short L1_2187                                ; 2168
        mov_ di,ax                                      ; 216A
        mov ax,0x9                                      ; 216C
        cmp byte [di],0x4d                              ; 216F
        jnz short L1_2177                               ; 2172
        mov ax,0xf                                      ; 2174

L1_2177:
        add_ di,ax                                      ; 2177
        push di                                         ; 2179
        push ds                                         ; 217A
        pop es                                          ; 217B
        mov al,0xd                                      ; 217C
        mov cx,0x22                                     ; 217E
        repne scasb                                     ; 2181
        mov [di-0x1],bl                                 ; 2183
        pop ax                                          ; 2186

L1_2187:
        push bx                                         ; 2187
        push ds                                         ; 2188
        push ax                                         ; 2189
        callp R1_218B, 0xFFFF, 0x0000                   ; 218A KERNEL.FatalAppExit
        mov ax,0xff                                     ; 218F
        push ax                                         ; 2192
        callp R1_2194, 0xFFFF, 0x0000                   ; 2193 KERNEL.FatalExit
        push cx                                         ; 2198
        push di                                         ; 2199
        test byte [bx+0x2],0x1                          ; 219A
        jz short L1_2208                                ; 219E
        call L1_228F                                    ; 21A0
        mov_ di,si                                      ; 21A3
        mov ax,[si]                                     ; 21A5
        test al,0x1                                     ; 21A7
        jz short L1_21AE                                ; 21A9
        sub_ cx,ax                                      ; 21AB
        dec cx                                          ; 21AD

L1_21AE:
        inc cx                                          ; 21AE
        inc cx                                          ; 21AF
        mov si,[bx+0x4]                                 ; 21B0
        or_ si,si                                       ; 21B3
        jz short L1_2208                                ; 21B5
        add_ cx,si                                      ; 21B7
        jnc short L1_21C4                               ; 21B9
        xor_ ax,ax                                      ; 21BB
        mov dx,0xfff0                                   ; 21BD
        jcxz L1_21F7                                    ; 21C0
        jmp short L1_2208                               ; 21C2

L1_21C4:
        callf L1_22E4, R1_21C7, R1_214F                 ; 21C4 far seg1
        mov es,ax                                       ; 21C9
        mov ax,[es:0xace]                               ; 21CB
        cmp ax,0x1000                                   ; 21CF
        jz short L1_21EA                                ; 21D2
        mov dx,0x8000                                   ; 21D4

L1_21D7:
        cmp_ dx,ax                                      ; 21D7
        jc short L1_21E1                                ; 21D9
        shr dx,1                                        ; 21DB
        jnz short L1_21D7                               ; 21DD
        jmp short L1_2203                               ; 21DF

L1_21E1:
        cmp dx,byte +0x8                                ; 21E1
        jc short L1_2203                                ; 21E4
        shl dx,1                                        ; 21E6
        mov_ ax,dx                                      ; 21E8

L1_21EA:
        dec ax                                          ; 21EA
        mov_ dx,ax                                      ; 21EB
        add_ ax,cx                                      ; 21ED
        jnc short L1_21F3                               ; 21EF
        xor_ ax,ax                                      ; 21F1

L1_21F3:
        not dx                                          ; 21F3
        and_ ax,dx                                      ; 21F5

L1_21F7:
        push dx                                         ; 21F7
        call L1_2229                                    ; 21F8
        pop dx                                          ; 21FB
        jnc short L1_220B                               ; 21FC
        cmp dx,byte -0x10                               ; 21FE
        jz short L1_2208                                ; 2201

L1_2203:
        mov ax,0x10                                     ; 2203
        jmp short L1_21EA                               ; 2206

L1_2208:
        stc                                             ; 2208
        jmp short L1_2226                               ; 2209

L1_220B:
        mov_ dx,ax                                      ; 220B
        sub dx,[bx+0x4]                                 ; 220D
        mov [bx+0x4],ax                                 ; 2210
        mov [bx+0xa],di                                 ; 2213
        mov si,[bx+0xc]                                 ; 2216
        dec dx                                          ; 2219
        mov [si],dx                                     ; 221A
        inc dx                                          ; 221C
        add_ si,dx                                      ; 221D
        mov word [si],0xfffe                            ; 221F
        mov [bx+0xc],si                                 ; 2223

L1_2226:
        pop di                                          ; 2226
        pop cx                                          ; 2227
        ret                                             ; 2228

L1_2229:
        mov_ dx,ax                                      ; 2229
        test byte [bx+0x2],0x4                          ; 222B
        jz short L1_2233                                ; 222F
        jmp short L1_2284                               ; 2231

L1_2233:
        push dx                                         ; 2233
        push cx                                         ; 2234
        push bx                                         ; 2235
        mov si,[bx+0x6]                                 ; 2236
        mov bx,[cs:0x1e90]                              ; 2239
        xor_ cx,cx                                      ; 223E
        or_ dx,dx                                       ; 2240
        jnz short L1_224B                               ; 2242
        test bx,0x10                                    ; 2244
        jnz short L1_228A                               ; 2248
        inc cx                                          ; 224A

L1_224B:
        mov ax,0x2002                                   ; 224B
        test bx,0x1                                     ; 224E
        jnz short L1_2257                               ; 2252
        mov ax,0x2020                                   ; 2254

L1_2257:
        push si                                         ; 2257
        push cx                                         ; 2258
        push dx                                         ; 2259
        push ax                                         ; 225A
        callp R1_225C, 0xFFFF, 0x0000                   ; 225B KERNEL.GlobalReAlloc
        or_ ax,ax                                       ; 2260
        jz short L1_228A                                ; 2262
        cmp_ ax,si                                      ; 2264
        jnz short L1_2284                               ; 2266
        push si                                         ; 2268
        callp R1_226A, 0xFFFF, 0x0000                   ; 2269 KERNEL.GlobalSize
        or_ dx,ax                                       ; 226E
        jz short L1_2284                                ; 2270
        pop bx                                          ; 2272
        pop cx                                          ; 2273
        pop dx                                          ; 2274
        mov_ ax,dx                                      ; 2275
        test byte [bx+0x2],0x4                          ; 2277
        jz short L1_2281                                ; 227B
        dec dx                                          ; 227D
        mov [bx-0x2],dx                                 ; 227E

L1_2281:
        clc                                             ; 2281
        jmp short L1_228E                               ; 2282

L1_2284:
        mov ax,0x12                                     ; 2284
        jmp near L1_2156                                ; 2287

L1_228A:
        pop bx                                          ; 228A
        pop cx                                          ; 228B
        pop dx                                          ; 228C
        stc                                             ; 228D

L1_228E:
        ret                                             ; 228E

L1_228F:
        push di                                         ; 228F
        mov si,[bx+0xa]                                 ; 2290
        cmp si,[bx+0xc]                                 ; 2293
        jnz short L1_229B                               ; 2296
        mov si,[bx+0x8]                                 ; 2298

L1_229B:
        lodsw                                           ; 229B
        cmp ax,byte -0x2                                ; 229C
        jz short L1_22A9                                ; 229F
        mov_ di,si                                      ; 22A1
        and al,0xfe                                     ; 22A3
        add_ si,ax                                      ; 22A5
        jmp short L1_229B                               ; 22A7

L1_22A9:
        dec di                                          ; 22A9
        dec di                                          ; 22AA
        mov_ si,di                                      ; 22AB
        pop di                                          ; 22AD
        ret                                             ; 22AE
        db 0x00                                         ; 22AF

L1_22B0:
        mov ax,0x2                                      ; 22B0
        jmp near L1_2156                                ; 22B3

L1_22B6:
        push bp                                         ; 22B6
        mov_ bp,sp                                      ; 22B7
        push bx                                         ; 22B9
        push es                                         ; 22BA
        push cx                                         ; 22BB
        mov cx,0x1000                                   ; 22BC
        xchg cx,[0xace]                                 ; 22BF
        push cx                                         ; 22C3
        push ax                                         ; 22C4
        callf L1_2570, R1_22C8, R1_21C7                 ; 22C5 far seg1
        pop bx                                          ; 22CA
        pop word [0xace]                                ; 22CB
        pop cx                                          ; 22CF
        mov dx,ds                                       ; 22D0
        or_ ax,ax                                       ; 22D2
        jz short L1_22DA                                ; 22D4
        pop es                                          ; 22D6
        pop bx                                          ; 22D7
        jmp short L1_22DF                               ; 22D8

L1_22DA:
        mov_ ax,cx                                      ; 22DA
        jmp near L1_2156                                ; 22DC

L1_22DF:
        mov_ sp,bp                                      ; 22DF
        pop bp                                          ; 22E1
        ret                                             ; 22E2
        db 0x00                                         ; 22E3

L1_22E4:
        cmp byte [cs:0x22f4],0xb8                       ; 22E4
        jz short L1_22EF                                ; 22EA
        mov ax,ss                                       ; 22EC
        retf                                            ; 22EE

L1_22EF:
        mov ax,[cs:0x22f5]                              ; 22EF
        retf                                            ; 22F3

L1_22F4:
        mov ax,ds                                       ; 22F4
        nop                                             ; 22F6
        inc bp                                          ; 22F7
        push bp                                         ; 22F8
        mov_ bp,sp                                      ; 22F9
        push ds                                         ; 22FB
        mov ds,ax                                       ; 22FC
        xor_ ax,ax                                      ; 22FE
        lea sp,[bp-0x2]                                 ; 2300
        pop ds                                          ; 2303
        pop bp                                          ; 2304
        dec bp                                          ; 2305
        retf                                            ; 2306
        db 0x90                                         ; 2307

L1_2308:
        ret                                             ; 2308
        db 0x00                                         ; 2309

L1_230A:
        push bp                                         ; 230A
        mov_ bp,sp                                      ; 230B
        push ds                                         ; 230D
        push bx                                         ; 230E
        lds bx,[bp+0x6]                                 ; 230F
        mov ax,[bx]                                     ; 2312
        mov dx,[bx+0x2]                                 ; 2314
        mov cx,[bp+0xa]                                 ; 2317
        push cs                                         ; 231A
        call L1_2422                                    ; 231B
        mov [bx],ax                                     ; 231E
        mov [bx+0x2],dx                                 ; 2320
        pop bx                                          ; 2323
        pop ds                                          ; 2324
        pop bp                                          ; 2325
        retf 0x6                                        ; 2326
        db 0x00                                         ; 2329

L1_232A:
        push bp                                         ; 232A
        mov_ bp,sp                                      ; 232B
        push ds                                         ; 232D
        push bx                                         ; 232E
        lds bx,[bp+0x6]                                 ; 232F
        mov ax,[bx]                                     ; 2332
        mov dx,[bx+0x2]                                 ; 2334
        mov cx,[bp+0xa]                                 ; 2337
        push cs                                         ; 233A
        call L1_24F8                                    ; 233B
        mov [bx],ax                                     ; 233E
        mov [bx+0x2],dx                                 ; 2340
        pop bx                                          ; 2343
        pop ds                                          ; 2344
        pop bp                                          ; 2345
        retf 0x6                                        ; 2346
        db 0x00                                         ; 2349

L1_234A:
        push bp                                         ; 234A
        mov_ bp,sp                                      ; 234B
        push di                                         ; 234D
        push si                                         ; 234E
        push bx                                         ; 234F
        xor_ di,di                                      ; 2350
        mov ax,[bp+0x8]                                 ; 2352
        or_ ax,ax                                       ; 2355
        jnl short L1_236A                               ; 2357
        inc di                                          ; 2359
        mov dx,[bp+0x6]                                 ; 235A
        neg ax                                          ; 235D
        neg dx                                          ; 235F
        sbb ax,byte +0x0                                ; 2361
        mov [bp+0x8],ax                                 ; 2364
        mov [bp+0x6],dx                                 ; 2367

L1_236A:
        mov ax,[bp+0xc]                                 ; 236A
        or_ ax,ax                                       ; 236D
        jnl short L1_2382                               ; 236F
        inc di                                          ; 2371
        mov dx,[bp+0xa]                                 ; 2372
        neg ax                                          ; 2375
        neg dx                                          ; 2377
        sbb ax,byte +0x0                                ; 2379
        mov [bp+0xc],ax                                 ; 237C
        mov [bp+0xa],dx                                 ; 237F

L1_2382:
        or_ ax,ax                                       ; 2382
        jnz short L1_239B                               ; 2384
        mov cx,[bp+0xa]                                 ; 2386
        mov ax,[bp+0x8]                                 ; 2389
        xor_ dx,dx                                      ; 238C
        div cx                                          ; 238E
        mov_ bx,ax                                      ; 2390
        mov ax,[bp+0x6]                                 ; 2392
        div cx                                          ; 2395
        mov_ dx,bx                                      ; 2397
        jmp short L1_23D3                               ; 2399

L1_239B:
        mov_ bx,ax                                      ; 239B
        mov cx,[bp+0xa]                                 ; 239D
        mov dx,[bp+0x8]                                 ; 23A0
        mov ax,[bp+0x6]                                 ; 23A3

L1_23A6:
        shr bx,1                                        ; 23A6
        rcr cx,1                                        ; 23A8
        shr dx,1                                        ; 23AA
        rcr ax,1                                        ; 23AC
        or_ bx,bx                                       ; 23AE
        jnz short L1_23A6                               ; 23B0
        div cx                                          ; 23B2
        mov_ si,ax                                      ; 23B4
        mul word [bp+0xc]                               ; 23B6
        xchg ax,cx                                      ; 23B9
        mov ax,[bp+0xa]                                 ; 23BA
        mul si                                          ; 23BD
        add_ dx,cx                                      ; 23BF
        jc short L1_23CF                                ; 23C1
        cmp dx,[bp+0x8]                                 ; 23C3
        ja short L1_23CF                                ; 23C6
        jc short L1_23D0                                ; 23C8
        cmp ax,[bp+0x6]                                 ; 23CA
        jna short L1_23D0                               ; 23CD

L1_23CF:
        dec si                                          ; 23CF

L1_23D0:
        xor_ dx,dx                                      ; 23D0
        xchg ax,si                                      ; 23D2

L1_23D3:
        dec di                                          ; 23D3
        jnz short L1_23DD                               ; 23D4
        neg dx                                          ; 23D6
        neg ax                                          ; 23D8
        sbb dx,byte +0x0                                ; 23DA

L1_23DD:
        pop bx                                          ; 23DD
        pop si                                          ; 23DE
        pop di                                          ; 23DF
        pop bp                                          ; 23E0
        retf 0x8                                        ; 23E1

L1_23E4:
        push bp                                         ; 23E4
        mov_ bp,sp                                      ; 23E5
        mov ax,[bp+0x8]                                 ; 23E7
        mov cx,[bp+0xc]                                 ; 23EA
        or_ cx,ax                                       ; 23ED
        mov cx,[bp+0xa]                                 ; 23EF
        jnz short L1_23FD                               ; 23F2
        mov ax,[bp+0x6]                                 ; 23F4
        mul cx                                          ; 23F7
        pop bp                                          ; 23F9
        retf 0x8                                        ; 23FA

L1_23FD:
        push bx                                         ; 23FD
        mul cx                                          ; 23FE
        mov_ bx,ax                                      ; 2400
        mov ax,[bp+0x6]                                 ; 2402
        mul word [bp+0xc]                               ; 2405
        add_ bx,ax                                      ; 2408
        mov ax,[bp+0x6]                                 ; 240A
        mul cx                                          ; 240D
        add_ dx,bx                                      ; 240F
        pop bx                                          ; 2411
        pop bp                                          ; 2412
        retf 0x8                                        ; 2413

L1_2416:
        xor_ ch,ch                                      ; 2416
        jcxz L1_2420                                    ; 2418

L1_241A:
        shl ax,1                                        ; 241A
        rcl dx,1                                        ; 241C
        loop L1_241A                                    ; 241E

L1_2420:
        retf                                            ; 2420
        db 0x00                                         ; 2421

L1_2422:
        xor_ ch,ch                                      ; 2422
        jcxz L1_242C                                    ; 2424

L1_2426:
        sar dx,1                                        ; 2426
        rcr ax,1                                        ; 2428
        loop L1_2426                                    ; 242A

L1_242C:
        retf                                            ; 242C
        db 0x00                                         ; 242D

L1_242E:
        push bp                                         ; 242E
        mov_ bp,sp                                      ; 242F
        push bx                                         ; 2431
        push si                                         ; 2432
        mov ax,[bp+0xc]                                 ; 2433
        or_ ax,ax                                       ; 2436
        jnz short L1_244F                               ; 2438
        mov cx,[bp+0xa]                                 ; 243A
        mov ax,[bp+0x8]                                 ; 243D
        xor_ dx,dx                                      ; 2440
        div cx                                          ; 2442
        mov_ bx,ax                                      ; 2444
        mov ax,[bp+0x6]                                 ; 2446
        div cx                                          ; 2449
        mov_ dx,bx                                      ; 244B
        jmp short L1_2487                               ; 244D

L1_244F:
        mov_ cx,ax                                      ; 244F
        mov bx,[bp+0xa]                                 ; 2451
        mov dx,[bp+0x8]                                 ; 2454
        mov ax,[bp+0x6]                                 ; 2457

L1_245A:
        shr cx,1                                        ; 245A
        rcr bx,1                                        ; 245C
        shr dx,1                                        ; 245E
        rcr ax,1                                        ; 2460
        or_ cx,cx                                       ; 2462
        jnz short L1_245A                               ; 2464
        div bx                                          ; 2466
        mov_ si,ax                                      ; 2468
        mul word [bp+0xc]                               ; 246A
        xchg ax,cx                                      ; 246D
        mov ax,[bp+0xa]                                 ; 246E
        mul si                                          ; 2471
        add_ dx,cx                                      ; 2473
        jc short L1_2483                                ; 2475
        cmp dx,[bp+0x8]                                 ; 2477
        ja short L1_2483                                ; 247A
        jc short L1_2484                                ; 247C
        cmp ax,[bp+0x6]                                 ; 247E
        jna short L1_2484                               ; 2481

L1_2483:
        dec si                                          ; 2483

L1_2484:
        xor_ dx,dx                                      ; 2484
        xchg ax,si                                      ; 2486

L1_2487:
        pop si                                          ; 2487
        pop bx                                          ; 2488
        pop bp                                          ; 2489
        retf 0x8                                        ; 248A
        db 0x00                                         ; 248D

L1_248E:
        push bp                                         ; 248E
        mov_ bp,sp                                      ; 248F
        push bx                                         ; 2491
        mov ax,[bp+0xc]                                 ; 2492
        or_ ax,ax                                       ; 2495
        jnz short L1_24AE                               ; 2497
        mov cx,[bp+0xa]                                 ; 2499
        mov ax,[bp+0x8]                                 ; 249C
        xor_ dx,dx                                      ; 249F
        div cx                                          ; 24A1
        mov ax,[bp+0x6]                                 ; 24A3
        div cx                                          ; 24A6
        mov_ ax,dx                                      ; 24A8
        xor_ dx,dx                                      ; 24AA
        jmp short L1_24F3                               ; 24AC

L1_24AE:
        mov_ cx,ax                                      ; 24AE
        mov bx,[bp+0xa]                                 ; 24B0
        mov dx,[bp+0x8]                                 ; 24B3
        mov ax,[bp+0x6]                                 ; 24B6

L1_24B9:
        shr cx,1                                        ; 24B9
        rcr bx,1                                        ; 24BB
        shr dx,1                                        ; 24BD
        rcr ax,1                                        ; 24BF
        or_ cx,cx                                       ; 24C1
        jnz short L1_24B9                               ; 24C3
        div bx                                          ; 24C5
        mov_ cx,ax                                      ; 24C7
        mul word [bp+0xc]                               ; 24C9
        xchg ax,cx                                      ; 24CC
        mul word [bp+0xa]                               ; 24CD
        add_ dx,cx                                      ; 24D0
        jc short L1_24E0                                ; 24D2
        cmp dx,[bp+0x8]                                 ; 24D4
        ja short L1_24E0                                ; 24D7
        jc short L1_24E6                                ; 24D9
        cmp ax,[bp+0x6]                                 ; 24DB
        jna short L1_24E6                               ; 24DE

L1_24E0:
        sub ax,[bp+0xa]                                 ; 24E0
        sbb dx,[bp+0xc]                                 ; 24E3

L1_24E6:
        sub ax,[bp+0x6]                                 ; 24E6
        sbb dx,[bp+0x8]                                 ; 24E9
        neg dx                                          ; 24EC
        neg ax                                          ; 24EE
        sbb dx,byte +0x0                                ; 24F0

L1_24F3:
        pop bx                                          ; 24F3
        pop bp                                          ; 24F4
        retf 0x8                                        ; 24F5

L1_24F8:
        xor_ ch,ch                                      ; 24F8
        jcxz L1_2502                                    ; 24FA

L1_24FC:
        shr dx,1                                        ; 24FC
        rcr ax,1                                        ; 24FE
        loop L1_24FC                                    ; 2500

L1_2502:
        retf                                            ; 2502
        db 0x00                                         ; 2503

L1_2504:
        push bp                                         ; 2504
        mov_ bp,sp                                      ; 2505
        sub sp,byte +0x2                                ; 2507
        push si                                         ; 250A
        push di                                         ; 250B
        push ds                                         ; 250C
        les di,[bp+0xa]                                 ; 250D
        push es                                         ; 2510
        pop ds                                          ; 2511
        xor_ ax,ax                                      ; 2512
        mov cx,0xffff                                   ; 2514
        repne scasb                                     ; 2517
        not cx                                          ; 2519
        jnz short L1_251E                               ; 251B
        dec cx                                          ; 251D

L1_251E:
        jcxz L1_255E                                    ; 251E
        dec cx                                          ; 2520
        mov [bp-0x2],cx                                 ; 2521
        les di,[bp+0x6]                                 ; 2524
        mov_ bx,di                                      ; 2527
        xor_ ax,ax                                      ; 2529
        mov cx,0xffff                                   ; 252B
        repne scasb                                     ; 252E
        not cx                                          ; 2530
        jnz short L1_2535                               ; 2532
        dec cx                                          ; 2534

L1_2535:
        mov_ dx,cx                                      ; 2535
        sub dx,[bp-0x2]                                 ; 2537
        jna short L1_2565                               ; 253A
        mov_ di,bx                                      ; 253C

L1_253E:
        mov si,[bp+0xa]                                 ; 253E
        lodsb                                           ; 2541
        mov_ di,bx                                      ; 2542
        mov_ cx,dx                                      ; 2544
        repne scasb                                     ; 2546
        jnz short L1_2565                               ; 2548
        mov_ dx,cx                                      ; 254A
        mov_ bx,di                                      ; 254C
        mov cx,[bp-0x2]                                 ; 254E
        jcxz L1_2557                                    ; 2551
        repe cmpsb                                      ; 2553
        jnz short L1_253E                               ; 2555

L1_2557:
        lea ax,[bx-0x1]                                 ; 2557
        mov dx,es                                       ; 255A
        jmp short L1_2568                               ; 255C

L1_255E:
        les ax,[bp+0x6]                                 ; 255E
        mov dx,es                                       ; 2561
        jmp short L1_2568                               ; 2563

L1_2565:
        xor_ ax,ax                                      ; 2565
        cwd                                             ; 2567

L1_2568:
        pop ds                                          ; 2568
        pop di                                          ; 2569
        pop si                                          ; 256A
        mov_ sp,bp                                      ; 256B
        pop bp                                          ; 256D
        retf                                            ; 256E
        db 0x00                                         ; 256F

L1_2570:
        inc bp                                          ; 2570
        push bp                                         ; 2571
        mov_ bp,sp                                      ; 2572
        push ds                                         ; 2574
        sub sp,byte +0x2                                ; 2575
        cmp word [bp+0x6],byte +0x0                     ; 2578
        jnz short L1_2583                               ; 257C
        mov word [bp+0x6],0x1                           ; 257E

L1_2583:
        mov ax,0xffff                                   ; 2583
        push ax                                         ; 2586
        callp R1_2588, R1_261D, 0x0000                  ; 2587 KERNEL.LockSegment
        mov ax,0x20                                     ; 258C
        push ax                                         ; 258F
        push word [bp+0x6]                              ; 2590
        callp R1_2594, 0xFFFF, 0x0000                   ; 2593 KERNEL.LocalAlloc
        mov [bp-0x4],ax                                 ; 2598
        mov ax,0xffff                                   ; 259B
        push ax                                         ; 259E
        callp R1_25A0, R1_2645, 0x0000                  ; 259F KERNEL.UnlockSegment
        cmp word [bp-0x4],byte +0x0                     ; 25A4
        jnz short L1_25C1                               ; 25A8
        mov ax,[0xad2]                                  ; 25AA
        or ax,[0xad0]                                   ; 25AD
        jz short L1_25C1                                ; 25B1
        push word [bp+0x6]                              ; 25B3
        call far [0xad0]                                ; 25B6
        add sp,byte +0x2                                ; 25BA
        or_ ax,ax                                       ; 25BD
        jnz short L1_2583                               ; 25BF

L1_25C1:
        mov ax,[bp-0x4]                                 ; 25C1
        lea sp,[bp-0x2]                                 ; 25C4
        pop ds                                          ; 25C7
        pop bp                                          ; 25C8
        dec bp                                          ; 25C9
        retf                                            ; 25CA
        db 0x90                                         ; 25CB

L1_25CC:
        inc bp                                          ; 25CC
        push bp                                         ; 25CD
        mov_ bp,sp                                      ; 25CE
        push ds                                         ; 25D0
        cmp word [bp+0x6],byte +0x0                     ; 25D1
        jz short L1_25DF                                ; 25D5
        push word [bp+0x6]                              ; 25D7
        callp R1_25DB, 0xFFFF, 0x0000                   ; 25DA KERNEL.LocalFree

L1_25DF:
        lea sp,[bp-0x2]                                 ; 25DF
        pop ds                                          ; 25E2
        pop bp                                          ; 25E3
        dec bp                                          ; 25E4
        retf                                            ; 25E5
        inc bp                                          ; 25E6
        push bp                                         ; 25E7
        mov_ bp,sp                                      ; 25E8
        push ds                                         ; 25EA
        sub sp,byte +0x4                                ; 25EB
        cmp word [bp+0x6],byte +0x0                     ; 25EE
        jnz short L1_2602                               ; 25F2
        push word [bp+0x8]                              ; 25F4
        callf L1_2570, R1_25FA, R1_260E                 ; 25F7 far seg1
        add sp,byte +0x2                                ; 25FC
        jmp short L1_264C                               ; 25FF
        db 0x90                                         ; 2601

L1_2602:
        cmp word [bp+0x8],byte +0x0                     ; 2602
        jnz short L1_2618                               ; 2606
        push word [bp+0x6]                              ; 2608
        callf L1_25CC, R1_260E, R1_22C8                 ; 260B far seg1
        add sp,byte +0x2                                ; 2610
        xor_ ax,ax                                      ; 2613
        jmp short L1_264C                               ; 2615
        db 0x90                                         ; 2617

L1_2618:
        mov ax,0xffff                                   ; 2618
        push ax                                         ; 261B
        callp R1_261D, 0xFFFF, 0x0000                   ; 261C KERNEL.LockSegment
        push word [bp+0x6]                              ; 2621
        cmp word [bp+0x8],byte +0x0                     ; 2624
        jz short L1_2630                                ; 2628
        mov ax,[bp+0x8]                                 ; 262A
        jmp short L1_2633                               ; 262D
        db 0x90                                         ; 262F

L1_2630:
        mov ax,0x1                                      ; 2630

L1_2633:
        push ax                                         ; 2633
        mov ax,0x62                                     ; 2634
        push ax                                         ; 2637
        callp R1_2639, 0xFFFF, 0x0000                   ; 2638 KERNEL.LocalReAlloc
        mov [bp-0x4],ax                                 ; 263D
        mov ax,0xffff                                   ; 2640
        push ax                                         ; 2643
        callp R1_2645, 0xFFFF, 0x0000                   ; 2644 KERNEL.UnlockSegment
        mov ax,[bp-0x4]                                 ; 2649

L1_264C:
        lea sp,[bp-0x2]                                 ; 264C
        pop ds                                          ; 264F
        pop bp                                          ; 2650
        dec bp                                          ; 2651
        retf                                            ; 2652
        db 0x90                                         ; 2653
        inc bp                                          ; 2654
        push bp                                         ; 2655
        mov_ bp,sp                                      ; 2656
        push ds                                         ; 2658
        push word [bp+0x6]                              ; 2659
        callp R1_265D, 0xFFFF, 0x0000                   ; 265C KERNEL.LocalSize
        lea sp,[bp-0x2]                                 ; 2661
        pop ds                                          ; 2664
        pop bp                                          ; 2665
        dec bp                                          ; 2666
        retf                                            ; 2667

seg1_data_end:

; relocation table
        dw (seg1_rel_end - seg1_rel_start) / 8
seg1_rel_start:
        reloc 2, 0, R1_25FA, 0x0001, 0x0000             ; seg1
        reloc 3, 1, R1_2194, 0x0001, 0x0001             ; KERNEL.FatalExit
        reloc 3, 1, R1_1F05, 0x0001, 0x0003             ; KERNEL.GetVersion
        reloc 3, 1, R1_20E0, 0x0001, 0x0083             ; KERNEL.GetDOSEnvironment
        reloc 2, 0, R1_1E85, 0x0003, 0x0000             ; seg3
        reloc 3, 1, R1_1EFC, 0x0001, 0x0004             ; KERNEL.LocalInit
        reloc 3, 1, R1_2594, 0x0001, 0x0005             ; KERNEL.LocalAlloc
        reloc 3, 1, R1_2639, 0x0001, 0x0006             ; KERNEL.LocalReAlloc
        reloc 3, 1, R1_25DB, 0x0001, 0x0007             ; KERNEL.LocalFree
        reloc 3, 1, R1_218B, 0x0001, 0x0089             ; KERNEL.FatalAppExit
        reloc 5, 1, R1_1C0C, 0x0001, 0x0072             ; KERNEL.__AHINCR
        reloc 3, 1, R1_265D, 0x0001, 0x000A             ; KERNEL.LocalSize
        reloc 3, 1, R1_225C, 0x0001, 0x0010             ; KERNEL.GlobalReAlloc
        reloc 3, 1, R1_226A, 0x0001, 0x0014             ; KERNEL.GlobalSize
        reloc 3, 1, R1_2588, 0x0001, 0x0017             ; KERNEL.LockSegment
        reloc 3, 1, R1_25A0, 0x0001, 0x0018             ; KERNEL.UnlockSegment
        reloc 3, 1, R1_003D, 0x0003, 0x001F             ; MMSYSTEM.DriverCallback
        reloc 3, 1, R1_1EB6, 0x0001, 0x0030             ; KERNEL.GetModuleUsage
        reloc 5, 1, R1_1E98, 0x0001, 0x00B2             ; KERNEL.__WINFLAGS
        reloc 3, 5, R1_1EA4, 0x0001, 0x005B             ; KERNEL.InitTask
        reloc 3, 1, R1_2044, 0x0001, 0x0066             ; KERNEL.DOS3Call
        reloc 3, 1, R1_12F5, 0x0003, 0x025F             ; MMSYSTEM.timeGetTime
seg1_rel_end:
seg1_end:
