; segment 5: code, 25826 bytes, flags 1D50h

L5_0000:
        push bp                                         ; 0000
        mov_ bp,sp                                      ; 0001
        sub sp,byte +0x4                                ; 0003
        push di                                         ; 0006
        push si                                         ; 0007
        mov si,[bp+0x6]                                 ; 0008
        push si                                         ; 000B
        callf L3_0BEC, R5_000F, R5_0019                 ; 000C far seg3
        or_ ax,ax                                       ; 0011
        jnz short L5_007E                               ; 0013
        push si                                         ; 0015
        callf L3_0C46, R5_0019, 0xFFFF                  ; 0016 far seg3
        or_ ax,ax                                       ; 001B
        jnz short L5_007E                               ; 001D
        cmp [si+0x128],ax                               ; 001F
        jz short L5_0028                                ; 0023
        jmp near L5_00AE                                ; 0025

L5_0028:
        mov ax,0x92a                                    ; 0028
        push ds                                         ; 002B
        push ax                                         ; 002C
        callp R5_002E, 0xFFFF, 0x0000                   ; 002D KERNEL.LoadLibrary
        mov_ di,ax                                      ; 0032
        cmp di,byte +0x20                               ; 0034
        jna short L5_005F                               ; 0037
        push ax                                         ; 0039
        mov ax,0x936                                    ; 003A
        push ds                                         ; 003D
        push ax                                         ; 003E
        callp R5_0040, R5_0053, 0x0000                  ; 003F KERNEL.GetProcAddress
        mov [si+0x12e],ax                               ; 0044
        mov [si+0x130],dx                               ; 0048
        push di                                         ; 004C
        mov ax,0x943                                    ; 004D
        push ds                                         ; 0050
        push ax                                         ; 0051
        callp R5_0053, 0xFFFF, 0x0000                   ; 0052 KERNEL.GetProcAddress
        mov [si+0x12a],ax                               ; 0057
        mov [si+0x12c],dx                               ; 005B

L5_005F:
        cmp di,byte +0x20                               ; 005F
        jna short L5_007E                               ; 0062
        mov ax,[si+0x130]                               ; 0064
        or ax,[si+0x12e]                                ; 0068
        jz short L5_007E                                ; 006C
        mov ax,[si+0x12c]                               ; 006E
        or ax,[si+0x12a]                                ; 0072
        jz short L5_007E                                ; 0076
        mov [si+0x128],di                               ; 0078
        jmp short L5_00AE                               ; 007C

L5_007E:
        les di,[si+0x26]                                ; 007E
        mov_ bx,di                                      ; 0081
        test word [es:di+0x2200],0x8000                 ; 0083
        jnz short L5_00AE                               ; 008A
        or byte [es:bx+0x2201],0x80                     ; 008C
        or byte [es:bx+0x2295],0x80                     ; 0092
        dec word [es:bx+0x266e]                         ; 0098
        dec word [es:bx+0x2674]                         ; 009D
        sub word [es:bx+0x6a2],byte +0x2                ; 00A2
        sbb word [es:bx+0x6a4],byte +0x0                ; 00A8

L5_00AE:
        pop si                                          ; 00AE
        pop di                                          ; 00AF
        mov_ sp,bp                                      ; 00B0
        pop bp                                          ; 00B2
        retf                                            ; 00B3

L5_00B4:
        push bp                                         ; 00B4
        mov_ bp,sp                                      ; 00B5
        push si                                         ; 00B7
        mov si,[bp+0x6]                                 ; 00B8
        cmp word [si+0x128],byte +0x0                   ; 00BB
        jz short L5_00D1                                ; 00C0
        push word [si+0x128]                            ; 00C2
        callp R5_00C7, 0xFFFF, 0x0000                   ; 00C6 KERNEL.FreeLibrary
        mov word [si+0x128],0x0                         ; 00CB

L5_00D1:
        pop si                                          ; 00D1
        mov_ sp,bp                                      ; 00D2
        pop bp                                          ; 00D4
        retf                                            ; 00D5

L5_00D6:
        push bp                                         ; 00D6
        mov_ bp,sp                                      ; 00D7
        sub sp,byte +0x42                               ; 00D9
        push di                                         ; 00DC
        push si                                         ; 00DD
        mov ax,0x2042                                   ; 00DE
        push ax                                         ; 00E1
        mov ax,0x2994                                   ; 00E2
        cwd                                             ; 00E5
        push dx                                         ; 00E6
        push ax                                         ; 00E7
        callp R5_00E9, 0xFFFF, 0x0000                   ; 00E8 KERNEL.GlobalAlloc
        mov_ dx,ax                                      ; 00ED
        sub_ cx,cx                                      ; 00EF
        mov [bp-0xa],cx                                 ; 00F1
        mov [bp-0x8],ax                                 ; 00F4
        or_ dx,cx                                       ; 00F7
        jnz short L5_0101                               ; 00F9
        mov ax,0x7                                      ; 00FB
        jmp near L5_04D0                                ; 00FE

L5_0101:
        mov ax,[bp-0xa]                                 ; 0101
        mov bx,[bp+0x6]                                 ; 0104
        mov dx,[bp-0x8]                                 ; 0107
        mov [bx+0x26],ax                                ; 010A
        mov [bx+0x28],dx                                ; 010D
        mov es,dx                                       ; 0110
        mov_ bx,ax                                      ; 0112
        push ds                                         ; 0114
        lea di,[bx+0x2]                                 ; 0115
        mov si,0x5c64                                   ; 0118
        push cs                                         ; 011B
        pop ds                                          ; 011C
        mov cx,0x33e                                    ; 011D
        rep movsw                                       ; 0120
        pop ds                                          ; 0122
        mov_ bx,ax                                      ; 0123
        push ds                                         ; 0125
        lea di,[bx+0x67e]                               ; 0126
        mov si,0x59cc                                   ; 012A
        push cs                                         ; 012D
        pop ds                                          ; 012E
        mov cx,0x14c                                    ; 012F
        rep movsw                                       ; 0132
        pop ds                                          ; 0134
        mov_ bx,ax                                      ; 0135
        push ds                                         ; 0137
        lea di,[bx+0x916]                               ; 0138
        mov si,0x3d78                                   ; 013C
        push cs                                         ; 013F
        pop ds                                          ; 0140
        mov cx,0xe2a                                    ; 0141
        rep movsw                                       ; 0144
        pop ds                                          ; 0146
        mov word [bp-0x2],0x0                           ; 0147
        add ax,strict word 0x3a                         ; 014C
        mov [bp-0x6],ax                                 ; 014F
        mov [bp-0x4],dx                                 ; 0152
        mov_ si,ax                                      ; 0155
        mov di,[bp-0x2]                                 ; 0157

L5_015A:
        push word [0xbd0]                               ; 015A
        lea ax,[di+0x28]                                ; 015E
        push ax                                         ; 0161
        push word [bp-0x4]                              ; 0162
        push si                                         ; 0165
        mov ax,0x40                                     ; 0166
        push ax                                         ; 0169
        callp R5_016B, R5_0183, 0x0000                  ; 016A USER.LoadString
        push word [0xbd0]                               ; 016F
        lea ax,[di+0x3c]                                ; 0173
        push ax                                         ; 0176
        lea ax,[si-0x10]                                ; 0177
        push word [bp-0x4]                              ; 017A
        push ax                                         ; 017D
        mov ax,0x10                                     ; 017E
        push ax                                         ; 0181
        callp R5_0183, R5_01B2, 0x0000                  ; 0182 USER.LoadString
        add si,0xa6                                     ; 0187
        inc di                                          ; 018B
        cmp di,byte +0xa                                ; 018C
        jc short L5_015A                                ; 018F
        xor_ di,di                                      ; 0191
        mov ax,[bp-0xa]                                 ; 0193
        mov dx,[bp-0x8]                                 ; 0196
        add ax,0x6b6                                    ; 0199
        mov_ si,ax                                      ; 019C
        mov [bp-0x4],dx                                 ; 019E

L5_01A1:
        push word [0xbd0]                               ; 01A1
        lea ax,[di+0x50]                                ; 01A5
        push ax                                         ; 01A8
        push word [bp-0x4]                              ; 01A9
        push si                                         ; 01AC
        mov ax,0x40                                     ; 01AD
        push ax                                         ; 01B0
        callp R5_01B2, R5_01CA, 0x0000                  ; 01B1 USER.LoadString
        push word [0xbd0]                               ; 01B6
        lea ax,[di+0x5a]                                ; 01BA
        push ax                                         ; 01BD
        lea ax,[si-0x10]                                ; 01BE
        push word [bp-0x4]                              ; 01C1
        push ax                                         ; 01C4
        mov ax,0x10                                     ; 01C5
        push ax                                         ; 01C8
        callp R5_01CA, R5_01F9, 0x0000                  ; 01C9 USER.LoadString
        add si,0xa6                                     ; 01CE
        inc di                                          ; 01D2
        cmp di,byte +0x4                                ; 01D3
        jc short L5_01A1                                ; 01D6
        xor_ di,di                                      ; 01D8
        mov ax,[bp-0xa]                                 ; 01DA
        mov dx,[bp-0x8]                                 ; 01DD
        add ax,0x93a                                    ; 01E0
        mov_ si,ax                                      ; 01E3
        mov [bp-0x4],dx                                 ; 01E5

L5_01E8:
        push word [0xbd0]                               ; 01E8
        lea ax,[di+0x64]                                ; 01EC
        push ax                                         ; 01EF
        push word [bp-0x4]                              ; 01F0
        push si                                         ; 01F3
        mov ax,0x40                                     ; 01F4
        push ax                                         ; 01F7
        callp R5_01F9, R5_0212, 0x0000                  ; 01F8 USER.LoadString
        push word [0xbd0]                               ; 01FD
        lea ax,[di+0xc8]                                ; 0201
        push ax                                         ; 0205
        lea ax,[si-0x10]                                ; 0206
        push word [bp-0x4]                              ; 0209
        push ax                                         ; 020C
        mov ax,0x10                                     ; 020D
        push ax                                         ; 0210
        callp R5_0212, 0xFFFF, 0x0000                   ; 0211 USER.LoadString
        add si,0x94                                     ; 0216
        inc di                                          ; 021A
        cmp di,byte +0x31                               ; 021B
        jc short L5_01E8                                ; 021E
        les bx,[bp-0xa]                                 ; 0220
        push ds                                         ; 0223
        lea di,[bx+0x256a]                              ; 0224
        mov si,0x63bc                                   ; 0228
        push cs                                         ; 022B
        pop ds                                          ; 022C
        mov cx,0x93                                     ; 022D
        rep movsw                                       ; 0230
        pop ds                                          ; 0232
        mov bx,[bp-0xa]                                 ; 0233
        push ds                                         ; 0236
        lea di,[bx+0x2690]                              ; 0237
        mov si,0x636c                                   ; 023B
        push cs                                         ; 023E
        pop ds                                          ; 023F
        mov cx,0x28                                     ; 0240
        rep movsw                                       ; 0243
        pop ds                                          ; 0245
        mov bx,[bp-0xa]                                 ; 0246
        push ds                                         ; 0249
        lea di,[bx+0x26e0]                              ; 024A
        mov si,0x6330                                   ; 024E
        push cs                                         ; 0251
        pop ds                                          ; 0252
        mov cx,0x1e                                     ; 0253
        rep movsw                                       ; 0256
        pop ds                                          ; 0258
        mov bx,[bp-0xa]                                 ; 0259
        push ds                                         ; 025C
        lea di,[bx+0x271c]                              ; 025D
        mov si,0x62e0                                   ; 0261
        push cs                                         ; 0264
        pop ds                                          ; 0265
        mov cx,0x28                                     ; 0266
        rep movsw                                       ; 0269
        pop ds                                          ; 026B
        mov bx,[bp-0xa]                                 ; 026C
        mov word [es:bx+0x277c],0x8000                  ; 026F
        mov word [es:bx+0x277e],0x0                     ; 0276
        mov bx,[bp+0x6]                                 ; 027D
        test byte [bx+0x2a],0x80                        ; 0280
        jnz short L5_02C3                               ; 0284
        test byte [bx+0x2a],0x20                        ; 0286
        jnz short L5_02CA                               ; 028A
        mov bx,[bp-0xa]                                 ; 028C
        or byte [es:bx+0x2201],0x80                     ; 028F
        or byte [es:bx+0x2295],0x80                     ; 0295
        or byte [es:bx+0x20d9],0x80                     ; 029B
        or byte [es:bx+0x216d],0x80                     ; 02A1
        dec word [es:bx+0x266e]                         ; 02A7
        dec word [es:bx+0x2674]                         ; 02AC
        dec word [es:bx+0x2662]                         ; 02B1
        dec word [es:bx+0x2668]                         ; 02B6
        sub word [es:bx+0x6a2],byte +0x4                ; 02BB
        jmp short L5_02E9                               ; 02C1

L5_02C3:
        test word [bx+0x2c],0x4                         ; 02C3
        jnz short L5_02EF                               ; 02C8

L5_02CA:
        mov bx,[bp-0xa]                                 ; 02CA
        or byte [es:bx+0x2201],0x80                     ; 02CD
        or byte [es:bx+0x2295],0x80                     ; 02D3
        dec word [es:bx+0x266e]                         ; 02D9
        dec word [es:bx+0x2674]                         ; 02DE
        sub word [es:bx+0x6a2],byte +0x2                ; 02E3

L5_02E9:
        sbb word [es:bx+0x6a4],byte +0x0                ; 02E9

L5_02EF:
        mov bx,[bp+0x6]                                 ; 02EF
        test word [bx+0x2c],0x8                         ; 02F2
        jnz short L5_031F                               ; 02F7
        mov ax,0x8000                                   ; 02F9
        mov bx,[bp-0xa]                                 ; 02FC
        xor_ dx,dx                                      ; 02FF
        mov [es:bx+0x2780],ax                           ; 0301
        mov [es:bx+0x2782],dx                           ; 0306
        mov [es:bx+0x27a0],ax                           ; 030B
        mov [es:bx+0x27a2],dx                           ; 0310
        mov [es:bx+0x27c8],ax                           ; 0315
        mov [es:bx+0x27ca],dx                           ; 031A

L5_031F:
        mov bx,[bp+0x6]                                 ; 031F
        test word [bx+0x2c],0x10                        ; 0322
        jnz short L5_033A                               ; 0327
        mov bx,[bp-0xa]                                 ; 0329
        mov word [es:bx+0x2784],0x8000                  ; 032C
        mov word [es:bx+0x2786],0x0                     ; 0333

L5_033A:
        mov bx,[bp+0x6]                                 ; 033A
        test byte [bx+0x2b],0x1                         ; 033D
        jnz short L5_035D                               ; 0341
        mov bx,[bp-0xa]                                 ; 0343
        or byte [es:bx+0x1fb1],0x80                     ; 0346
        dec word [es:bx+0x2656]                         ; 034C
        sub word [es:bx+0x6a2],byte +0x1                ; 0351
        sbb word [es:bx+0x6a4],byte +0x0                ; 0357

L5_035D:
        mov bx,[bp+0x6]                                 ; 035D
        test byte [bx+0x2b],0x2                         ; 0360
        jnz short L5_0380                               ; 0364
        mov bx,[bp-0xa]                                 ; 0366
        or byte [es:bx+0x2045],0x80                     ; 0369
        dec word [es:bx+0x265c]                         ; 036F
        sub word [es:bx+0x6a2],byte +0x1                ; 0374
        sbb word [es:bx+0x6a4],byte +0x0                ; 037A

L5_0380:
        mov bx,[bp+0x6]                                 ; 0380
        test byte [bx+0x2b],0x40                        ; 0383
        jz short L5_039A                                ; 0387
        mov bx,[bp-0xa]                                 ; 0389
        mov word [es:bx+0x2784],0x8000                  ; 038C
        mov word [es:bx+0x2786],0x0                     ; 0393

L5_039A:
        mov word [bp-0x12],0x2c                         ; 039A
        mov word [bp-0x10],0x0                          ; 039F
        lea ax,[bp-0x42]                                ; 03A4
        mov [bp-0xe],ax                                 ; 03A7
        mov [bp-0xc],ss                                 ; 03AA
        push word [bp+0x6]                              ; 03AD
        lea cx,[bp-0x12]                                ; 03B0
        push ss                                         ; 03B3
        push cx                                         ; 03B4
        callf L6_00E6, R5_03B8, R5_041D                 ; 03B5 far seg6
        les bx,[bp-0xa]                                 ; 03BA
        push ds                                         ; 03BD
        lea di,[bx+0x7a4]                               ; 03BE
        lea si,[bp-0x42]                                ; 03C2
        push ss                                         ; 03C5
        pop ds                                          ; 03C6
        mov cx,0x13                                     ; 03C7
        rep movsw                                       ; 03CA
        pop ds                                          ; 03CC
        les bx,[bp-0xa]                                 ; 03CD
        mov word [es:bx+0x79c],0x2                      ; 03D0
        mov word [es:bx+0x79e],0x0                      ; 03D7
        push ds                                         ; 03DE
        lea di,[bx+0x84a]                               ; 03DF
        lea si,[bp-0x42]                                ; 03E3
        push ss                                         ; 03E6
        pop ds                                          ; 03E7
        mov cx,0x13                                     ; 03E8
        rep movsw                                       ; 03EB
        pop ds                                          ; 03ED
        les bx,[bp-0xa]                                 ; 03EE
        mov word [es:bx+0x842],0x2                      ; 03F1
        mov word [es:bx+0x844],0x0                      ; 03F8
        mov word [bp-0x12],0x30                         ; 03FF
        mov word [bp-0x10],0x0                          ; 0404
        lea ax,[bp-0x42]                                ; 0409
        mov [bp-0xe],ax                                 ; 040C
        mov [bp-0xc],ss                                 ; 040F
        push word [bp+0x6]                              ; 0412
        lea cx,[bp-0x12]                                ; 0415
        push ss                                         ; 0418
        push cx                                         ; 0419
        callf L6_13FE, R5_041D, R5_0464                 ; 041A far seg6
        les bx,[bp-0xa]                                 ; 041F
        push ds                                         ; 0422
        lea di,[bx+0x128]                               ; 0423
        lea si,[bp-0x42]                                ; 0427
        push ss                                         ; 042A
        pop ds                                          ; 042B
        mov cx,0x13                                     ; 042C
        rep movsw                                       ; 042F
        pop ds                                          ; 0431
        les bx,[bp-0xa]                                 ; 0432
        mov word [es:bx+0x120],0x1                      ; 0435
        mov word [es:bx+0x122],0x0                      ; 043C
        mov word [bp-0x12],0x2c                         ; 0443
        mov word [bp-0x10],0x0                          ; 0448
        lea ax,[bp-0x42]                                ; 044D
        mov [bp-0xe],ax                                 ; 0450
        mov [bp-0xc],ss                                 ; 0453
        push word [bp+0x6]                              ; 0456
        xor_ cx,cx                                      ; 0459
        push cx                                         ; 045B
        lea dx,[bp-0x12]                                ; 045C
        push ss                                         ; 045F
        push dx                                         ; 0460
        callf L6_1C74, R5_0464, R5_04AA                 ; 0461 far seg6
        les bx,[bp-0xa]                                 ; 0466
        push ds                                         ; 0469
        lea di,[bx+0x82]                                ; 046A
        lea si,[bp-0x42]                                ; 046E
        push ss                                         ; 0471
        pop ds                                          ; 0472
        mov cx,0x13                                     ; 0473
        rep movsw                                       ; 0476
        pop ds                                          ; 0478
        les bx,[bp-0xa]                                 ; 0479
        mov word [es:bx+0x7a],0x5                       ; 047C
        mov word [es:bx+0x7c],0x0                       ; 0482
        mov word [bp-0x12],0x2c                         ; 0488
        mov word [bp-0x10],0x0                          ; 048D
        lea ax,[bp-0x42]                                ; 0492
        mov [bp-0xe],ax                                 ; 0495
        mov [bp-0xc],ss                                 ; 0498
        push word [bp+0x6]                              ; 049B
        mov cx,0x1                                      ; 049E
        push cx                                         ; 04A1
        lea cx,[bp-0x12]                                ; 04A2
        push ss                                         ; 04A5
        push cx                                         ; 04A6
        callf L6_1C74, R5_04AA, 0xFFFF                  ; 04A7 far seg6
        les bx,[bp-0xa]                                 ; 04AC
        push ds                                         ; 04AF
        lea di,[bx+0x274]                               ; 04B0
        lea si,[bp-0x42]                                ; 04B4
        push ss                                         ; 04B7
        pop ds                                          ; 04B8
        mov cx,0x13                                     ; 04B9
        rep movsw                                       ; 04BC
        pop ds                                          ; 04BE
        les bx,[bp-0xa]                                 ; 04BF
        xor_ ax,ax                                      ; 04C2
        mov word [es:bx+0x26c],0x5                      ; 04C4
        mov [es:bx+0x26e],ax                            ; 04CB

L5_04D0:
        pop si                                          ; 04D0
        pop di                                          ; 04D1
        mov_ sp,bp                                      ; 04D2
        pop bp                                          ; 04D4
        retf 0x2                                        ; 04D5

L5_04D8:
        push bp                                         ; 04D8
        mov_ bp,sp                                      ; 04D9
        push si                                         ; 04DB
        mov si,[bp+0x6]                                 ; 04DC
        push word [si+0x28]                             ; 04DF
        callp R5_04E3, 0xFFFF, 0x0000                   ; 04E2 KERNEL.GlobalFree
        sub_ ax,ax                                      ; 04E7
        mov [si+0x28],ax                                ; 04E9
        mov [si+0x26],ax                                ; 04EC
        pop si                                          ; 04EF
        mov_ sp,bp                                      ; 04F0
        pop bp                                          ; 04F2
        retf 0x2                                        ; 04F3

L5_04F6:
        push bp                                         ; 04F6
        mov_ bp,sp                                      ; 04F7
        push si                                         ; 04F9
        les bx,[bp+0x8]                                 ; 04FA
        mov si,[es:bx]                                  ; 04FD
        or_ si,si                                       ; 0500
        jz short L5_0530                                ; 0502

L5_0504:
        push word [si+0x8]                              ; 0504
        push word [si+0x6]                              ; 0507
        push word [si+0xa]                              ; 050A
        push word [si+0x4]                              ; 050D
        mov ax,0x3d0                                    ; 0510
        push ax                                         ; 0513
        push word [si+0xe]                              ; 0514
        push word [si+0xc]                              ; 0517
        push word [bp+0x6]                              ; 051A
        push word [bp+0x4]                              ; 051D
        sub_ ax,ax                                      ; 0520
        push ax                                         ; 0522
        push ax                                         ; 0523
        callp R5_0525, 0xFFFF, 0x0000                   ; 0524 MMSYSTEM.DriverCallback
        mov si,[si+0x12]                                ; 0529
        or_ si,si                                       ; 052C
        jnz short L5_0504                               ; 052E

L5_0530:
        pop si                                          ; 0530
        mov_ sp,bp                                      ; 0531
        pop bp                                          ; 0533
        ret 0x8                                         ; 0534
        db 0x90                                         ; 0537

L5_0538:
        push bp                                         ; 0538
        mov_ bp,sp                                      ; 0539
        sub sp,byte +0x2c                               ; 053B
        push di                                         ; 053E
        push si                                         ; 053F
        mov bx,[bp+0xe]                                 ; 0540
        mov ax,[bx+0x26]                                ; 0543
        mov dx,[bx+0x28]                                ; 0546
        mov_ si,ax                                      ; 0549
        mov [bp-0x12],dx                                ; 054B
        or_ dx,ax                                       ; 054E
        jnz short L5_0555                               ; 0550
        jmp near L5_08A8                                ; 0552

L5_0555:
        test byte [bp+0x6],0x10                         ; 0555
        jnz short L5_055E                               ; 0559
        jmp near L5_0665                                ; 055B

L5_055E:
        xor_ di,di                                      ; 055E
        mov ax,0x14                                     ; 0560
        mul word [bp+0xc]                               ; 0563
        add_ ax,si                                      ; 0566
        add ax,0x2690                                   ; 0568
        mov cx,[bp-0x12]                                ; 056B
        mov [bp-0x4],ax                                 ; 056E
        mov [bp-0x2],cx                                 ; 0571
        mov [bp-0x14],si                                ; 0574
        mov_ bx,ax                                      ; 0577
        mov [bp-0x10],di                                ; 0579
        mov_ si,di                                      ; 057C
        mov cx,[bp+0xa]                                 ; 057E

L5_0581:
        mov es,[bp-0x2]                                 ; 0581
        cmp [es:bx],cx                                  ; 0584
        jz short L5_0591                                ; 0587
        inc bx                                          ; 0589
        inc bx                                          ; 058A
        inc si                                          ; 058B
        cmp si,byte +0xa                                ; 058C
        jc short L5_0581                                ; 058F

L5_0591:
        mov [bp-0x10],si                                ; 0591
        mov si,[bp-0x14]                                ; 0594
        mov di,[bp+0xc]                                 ; 0597
        mov_ ax,di                                      ; 059A
        mov cx,0xa                                      ; 059C
        mul cx                                          ; 059F
        mov_ bx,ax                                      ; 05A1
        add bx,[bp-0x10]                                ; 05A3
        add_ bx,bx                                      ; 05A6
        add_ bx,bx                                      ; 05A8
        mov ax,[bp-0x12]                                ; 05AA
        mov es,ax                                       ; 05AD
        add_ bx,si                                      ; 05AF
        add bx,0x276c                                   ; 05B1
        mov ax,[es:bx]                                  ; 05B5
        mov dx,[es:bx+0x2]                              ; 05B8
        mov [bp-0xe],ax                                 ; 05BC
        mov [bp-0xc],dx                                 ; 05BF
        test dx,0x800                                   ; 05C2
        jz short L5_05D0                                ; 05C6
        and byte [bp-0xe],0xfe                          ; 05C8
        and byte [bp-0xb],0xf7                          ; 05CC

L5_05D0:
        test byte [bp+0x6],0x1                          ; 05D0
        jz short L5_05EA                                ; 05D4
        cmp word [bp+0x8],byte +0x0                     ; 05D6
        jz short L5_05E3                                ; 05DA
        or byte [es:bx+0x3],0x8                         ; 05DC
        jmp short L5_05FA                               ; 05E1

L5_05E3:
        and byte [es:bx+0x3],0xf7                       ; 05E3
        jmp short L5_05FA                               ; 05E8

L5_05EA:
        cmp word [bp+0x8],byte +0x0                     ; 05EA
        jz short L5_05F6                                ; 05EE
        or byte [es:bx],0x1                             ; 05F0
        jmp short L5_05FA                               ; 05F4

L5_05F6:
        and byte [es:bx],0xfe                           ; 05F6

L5_05FA:
        mov ax,[es:bx]                                  ; 05FA
        mov dx,[es:bx+0x2]                              ; 05FD
        mov [bp-0x4],ax                                 ; 0601
        mov [bp-0x2],dx                                 ; 0604
        test dx,0x800                                   ; 0607
        jz short L5_0615                                ; 060B
        and byte [bp-0x4],0xfe                          ; 060D
        and byte [bp-0x1],0xf7                          ; 0611

L5_0615:
        mov ax,[bp-0xe]                                 ; 0615
        mov dx,[bp-0xc]                                 ; 0618
        cmp [bp-0x4],ax                                 ; 061B
        jnz short L5_0625                               ; 061E
        cmp [bp-0x2],dx                                 ; 0620
        jz short L5_0630                                ; 0623

L5_0625:
        push word [bp-0x12]                             ; 0625
        push si                                         ; 0628
        push word [bp-0x10]                             ; 0629
        push di                                         ; 062C
        call L5_04F6                                    ; 062D

L5_0630:
        mov_ ax,di                                      ; 0630
        mov cx,0xa6                                     ; 0632
        mul cx                                          ; 0635
        mov_ bx,si                                      ; 0637
        mov es,[bp-0x12]                                ; 0639
        add_ bx,ax                                      ; 063C
        add bx,0x68e                                    ; 063E
        mov [bp-0x20],bx                                ; 0642
        mov [bp-0x1e],es                                ; 0645
        mov ax,[es:bx]                                  ; 0648
        mov dx,[es:bx+0x2]                              ; 064B
        mov [bp-0xe],ax                                 ; 064F
        mov [bp-0xc],dx                                 ; 0652
        test dx,0x800                                   ; 0655
        jz short L5_06B9                                ; 0659
        and byte [bp-0xe],0xfe                          ; 065B
        and byte [bp-0xb],0xf7                          ; 065F
        jmp short L5_06B9                               ; 0663

L5_0665:
        mov di,[bp+0xc]                                 ; 0665
        mov_ ax,di                                      ; 0668
        mov cx,0xa6                                     ; 066A
        mul cx                                          ; 066D
        mov_ bx,si                                      ; 066F
        mov es,[bp-0x12]                                ; 0671
        add_ bx,ax                                      ; 0674
        add bx,0x68e                                    ; 0676
        mov [bp-0x20],bx                                ; 067A
        mov [bp-0x1e],es                                ; 067D
        mov ax,[es:bx]                                  ; 0680
        mov dx,[es:bx+0x2]                              ; 0683
        mov [bp-0xe],ax                                 ; 0687
        mov [bp-0xc],dx                                 ; 068A
        test dx,0x800                                   ; 068D
        jz short L5_069B                                ; 0691
        and byte [bp-0xe],0xfe                          ; 0693
        and byte [bp-0xb],0xf7                          ; 0697

L5_069B:
        test byte [bp+0x6],0x1                          ; 069B
        jz short L5_06B9                                ; 069F
        cmp word [bp+0x8],byte +0x0                     ; 06A1
        jz short L5_06B1                                ; 06A5
        mov es,[bp-0x1e]                                ; 06A7
        or byte [es:bx+0x3],0x8                         ; 06AA
        jmp short L5_06B9                               ; 06AF

L5_06B1:
        mov es,[bp-0x1e]                                ; 06B1
        and byte [es:bx+0x3],0xf7                       ; 06B4

L5_06B9:
        les bx,[bp-0x20]                                ; 06B9
        and byte [es:bx],0xfe                           ; 06BC
        test word [es:bx+0x2],0x800                     ; 06C0
        jz short L5_06CB                                ; 06C6
        jmp near L5_086E                                ; 06C8

L5_06CB:
        mov [bp-0xa],di                                 ; 06CB
        and byte [es:bx],0xfe                           ; 06CE
        mov word [bp-0x10],0x0                          ; 06D2
        mov_ ax,di                                      ; 06D7
        mov cx,0x94                                     ; 06D9
        mul cx                                          ; 06DC
        mov_ bx,ax                                      ; 06DE
        mov ax,[bp-0x12]                                ; 06E0
        mov es,ax                                       ; 06E3
        add_ bx,si                                      ; 06E5
        cmp word [es:bx+0x928],byte +0x0                ; 06E7
        jnz short L5_06FA                               ; 06ED
        cmp word [es:bx+0x926],byte +0x0                ; 06EF
        jnz short L5_06FA                               ; 06F5
        jmp near L5_0796                                ; 06F7

L5_06FA:
        mov cl,0x3                                      ; 06FA
        mov_ ax,di                                      ; 06FC
        shl ax,cl                                       ; 06FE
        add_ ax,si                                      ; 0700
        add ax,0x280c                                   ; 0702
        mov [bp-0x24],ax                                ; 0705
        mov [bp-0x22],es                                ; 0708
        mov ax,0x94                                     ; 070B
        mul di                                          ; 070E
        add_ ax,si                                      ; 0710
        add ax,0x926                                    ; 0712
        mov [bp-0x28],ax                                ; 0715
        mov [bp-0x26],es                                ; 0718
        mov bx,[bp-0x10]                                ; 071B
        mov [bp-0x14],si                                ; 071E

L5_0721:
        mov ax,0x1                                      ; 0721
        mov_ cx,bx                                      ; 0724
        shl ax,cl                                       ; 0726
        cwd                                             ; 0728
        les di,[bp-0x24]                                ; 0729
        and ax,[es:di]                                  ; 072C
        and dx,[es:di+0x2]                              ; 072F
        or_ dx,ax                                       ; 0733
        jz short L5_0780                                ; 0735
        mov ax,0xa                                      ; 0737
        mul word [bp+0xc]                               ; 073A
        mov_ cx,ax                                      ; 073D
        mov ax,0xa                                      ; 073F
        mul word [bp-0xa]                               ; 0742
        mov_ di,ax                                      ; 0745
        add_ di,bx                                      ; 0747
        add_ di,di                                      ; 0749
        mov ax,[bp-0x12]                                ; 074B
        add_ di,si                                      ; 074E
        mov es,ax                                       ; 0750
        mov di,[es:di+0x26e0]                           ; 0752
        add_ di,cx                                      ; 0757
        add_ di,di                                      ; 0759
        add_ di,di                                      ; 075B
        add_ di,si                                      ; 075D
        mov ax,[es:di+0x276c]                           ; 075F
        mov dx,[es:di+0x276e]                           ; 0764
        and ax,strict word 0x1                          ; 0769
        and dx,0x800                                    ; 076C
        cmp ax,strict word 0x1                          ; 0770
        jnz short L5_0780                               ; 0773
        or_ dx,dx                                       ; 0775
        jnz short L5_0780                               ; 0777
        les di,[bp-0x20]                                ; 0779
        or byte [es:di],0x1                             ; 077C

L5_0780:
        inc bx                                          ; 0780
        sub_ ax,ax                                      ; 0781
        les di,[bp-0x28]                                ; 0783
        cmp [es:di+0x2],ax                              ; 0786
        ja short L5_0721                                ; 078A
        jc short L5_0793                                ; 078C
        cmp [es:di],bx                                  ; 078E
        ja short L5_0721                                ; 0791

L5_0793:
        mov di,[bp+0xc]                                 ; 0793

L5_0796:
        or_ di,di                                       ; 0796
        jz short L5_079D                                ; 0798
        jmp near L5_086E                                ; 079A

L5_079D:
        mov [bp-0x10],di                                ; 079D
        mov_ ax,di                                      ; 07A0
        mov cx,0x28                                     ; 07A2
        mul cx                                          ; 07A5
        mov_ cx,si                                      ; 07A7
        mov dx,[bp-0x12]                                ; 07A9
        add_ cx,ax                                      ; 07AC
        add cx,0x276c                                   ; 07AE
        mov [bp-0x18],cx                                ; 07B2
        mov [bp-0x16],dx                                ; 07B5
        mov ax,0x94                                     ; 07B8
        mul word [bp-0xa]                               ; 07BB
        mov cx,[bp-0x12]                                ; 07BE
        add_ ax,si                                      ; 07C1
        add ax,0x926                                    ; 07C3
        mov [bp-0x28],ax                                ; 07C6
        mov [bp-0x26],cx                                ; 07C9

L5_07CC:
        mov word [bp-0x6],0x0                           ; 07CC
        les bx,[bp-0x28]                                ; 07D1
        mov ax,[es:bx]                                  ; 07D4
        mov dx,[es:bx+0x2]                              ; 07D7
        mov [bp-0x2c],ax                                ; 07DB
        mov [bp-0x2a],dx                                ; 07DE
        or_ dx,dx                                       ; 07E1
        jnz short L5_07E9                               ; 07E3
        or_ ax,ax                                       ; 07E5
        jz short L5_0827                                ; 07E7

L5_07E9:
        mov ax,0x14                                     ; 07E9
        mul word [bp-0xa]                               ; 07EC
        add_ ax,si                                      ; 07EF
        add ax,0x26e0                                   ; 07F1
        mov cx,[bp-0x12]                                ; 07F4
        mov [bp-0x4],ax                                 ; 07F7
        mov [bp-0x2],cx                                 ; 07FA
        mov bx,[bp-0x6]                                 ; 07FD
        mov_ di,ax                                      ; 0800
        mov [bp-0x14],si                                ; 0802

L5_0805:
        mov ax,[bp-0x10]                                ; 0805
        mov es,[bp-0x2]                                 ; 0808
        cmp [es:di],ax                                  ; 080B
        jz short L5_0821                                ; 080E
        inc di                                          ; 0810
        inc di                                          ; 0811
        inc bx                                          ; 0812
        sub_ ax,ax                                      ; 0813
        cmp [bp-0x2a],ax                                ; 0815
        ja short L5_0805                                ; 0818
        jc short L5_0821                                ; 081A
        cmp [bp-0x2c],bx                                ; 081C
        ja short L5_0805                                ; 081F

L5_0821:
        mov [bp-0x6],bx                                 ; 0821
        mov si,[bp-0x14]                                ; 0824

L5_0827:
        mov ax,[bp-0x6]                                 ; 0827
        sub_ dx,dx                                      ; 082A
        les bx,[bp-0x28]                                ; 082C
        cmp [es:bx],ax                                  ; 082F
        jnz short L5_085B                               ; 0832
        cmp [es:bx+0x2],dx                              ; 0834
        jnz short L5_085B                               ; 0838
        les bx,[bp-0x18]                                ; 083A
        mov ax,[es:bx]                                  ; 083D
        mov dx,[es:bx+0x2]                              ; 0840
        and ax,strict word 0x1                          ; 0844
        and dx,0x800                                    ; 0847
        cmp ax,strict word 0x1                          ; 084B
        jnz short L5_085B                               ; 084E
        or_ dx,dx                                       ; 0850
        jnz short L5_085B                               ; 0852
        les bx,[bp-0x20]                                ; 0854
        or byte [es:bx],0x1                             ; 0857

L5_085B:
        add word [bp-0x18],byte +0x4                    ; 085B
        inc word [bp-0x10]                              ; 085F
        cmp word [bp-0x10],byte +0xa                    ; 0862
        jnc short L5_086B                               ; 0866
        jmp near L5_07CC                                ; 0868

L5_086B:
        mov di,[bp+0xc]                                 ; 086B

L5_086E:
        les bx,[bp-0x20]                                ; 086E
        mov ax,[es:bx]                                  ; 0871
        mov dx,[es:bx+0x2]                              ; 0874
        mov [bp-0x4],ax                                 ; 0878
        mov [bp-0x2],dx                                 ; 087B
        test dx,0x800                                   ; 087E
        jz short L5_088C                                ; 0882
        and byte [bp-0x4],0xfe                          ; 0884
        and byte [bp-0x1],0xf7                          ; 0888

L5_088C:
        mov ax,[bp-0xe]                                 ; 088C
        mov dx,[bp-0xc]                                 ; 088F
        cmp [bp-0x4],ax                                 ; 0892
        jnz short L5_089C                               ; 0895
        cmp [bp-0x2],dx                                 ; 0897
        jz short L5_08A8                                ; 089A

L5_089C:
        push word [bp-0x12]                             ; 089C
        push si                                         ; 089F
        mov ax,0xffff                                   ; 08A0
        push ax                                         ; 08A3
        push di                                         ; 08A4
        call L5_04F6                                    ; 08A5

L5_08A8:
        pop si                                          ; 08A8
        pop di                                          ; 08A9
        mov_ sp,bp                                      ; 08AA
        pop bp                                          ; 08AC
        retf 0xa                                        ; 08AD

L5_08B0:
        push bp                                         ; 08B0
        mov_ bp,sp                                      ; 08B1
        sub sp,byte +0x4                                ; 08B3
        push si                                         ; 08B6
        cmp word [bp+0x6],byte +0x0                     ; 08B7
        jnz short L5_08C3                               ; 08BB
        cmp word [bp+0x4],byte +0x31                    ; 08BD
        jc short L5_08C8                                ; 08C1

L5_08C3:
        mov ax,0x401                                    ; 08C3
        jmp short L5_08E4                               ; 08C6

L5_08C8:
        mov ax,0x94                                     ; 08C8
        mul word [bp+0x4]                               ; 08CB
        mov_ si,ax                                      ; 08CE
        mov es,[bp+0xa]                                 ; 08D0
        add si,[bp+0x8]                                 ; 08D3
        add si,0x916                                    ; 08D6
        test word [es:si+0xe],0x8000                    ; 08DA
        jnz short L5_08C3                               ; 08E0
        xor_ ax,ax                                      ; 08E2

L5_08E4:
        pop si                                          ; 08E4
        mov_ sp,bp                                      ; 08E5
        pop bp                                          ; 08E7
        ret 0x8                                         ; 08E8
        db 0x90                                         ; 08EB

L5_08EC:
        push bp                                         ; 08EC
        mov_ bp,sp                                      ; 08ED
        sub sp,byte +0x14                               ; 08EF
        push di                                         ; 08F2
        push si                                         ; 08F3
        push word [bp+0xa]                              ; 08F4
        push word [bp+0x8]                              ; 08F7
        les bx,[bp+0x4]                                 ; 08FA
        mov ax,[es:bx+0x4]                              ; 08FD
        mov dx,[es:bx+0x6]                              ; 0901
        mov [bp-0x4],ax                                 ; 0905
        mov [bp-0x2],dx                                 ; 0908
        push dx                                         ; 090B
        push ax                                         ; 090C
        call L5_08B0                                    ; 090D
        mov_ si,ax                                      ; 0910
        or_ si,ax                                       ; 0912
        jz short L5_0919                                ; 0914
        jmp near L5_0A3D                                ; 0916

L5_0919:
        mov si,[bp+0x8]                                 ; 0919
        mov ax,0x94                                     ; 091C
        mul word [bp-0x4]                               ; 091F
        mov cx,[bp+0xa]                                 ; 0922
        add_ ax,si                                      ; 0925
        add ax,0x916                                    ; 0927
        mov [bp-0xe],ax                                 ; 092A
        mov [bp-0xc],cx                                 ; 092D
        les bx,[bp+0x4]                                 ; 0930
        mov bx,[es:bx+0x8]                              ; 0933
        mov_ ax,bx                                      ; 0937
        dec ax                                          ; 0939
        jz short L5_0955                                ; 093A
        mov ax,0x6                                      ; 093C
        mul word [bp-0x4]                               ; 093F
        mov_ di,ax                                      ; 0942
        mov es,cx                                       ; 0944
        add_ di,si                                      ; 0946
        cmp [es:di+0x256e],bx                           ; 0948
        jz short L5_0955                                ; 094D

L5_094F:
        mov ax,0x402                                    ; 094F
        jmp near L5_0A3D                                ; 0952

L5_0955:
        les si,[bp-0xe]                                 ; 0955
        mov ax,[es:si+0xa]                              ; 0958
        sub_ ah,ah                                      ; 095C
        mov_ dx,ax                                      ; 095E
        sub_ cx,cx                                      ; 0960
        cmp dx,byte +0x1                                ; 0962
        jnz short L5_096A                               ; 0965
        jmp near L5_0A3B                                ; 0967

L5_096A:
        cmp ax,strict word 0x2                          ; 096A
        jz short L5_09D4                                ; 096D
        cmp ax,strict word 0x4                          ; 096F
        jz short L5_09D4                                ; 0972
        or_ bx,bx                                       ; 0974
        jnz short L5_097B                               ; 0976
        jmp near L5_0A3B                                ; 0978

L5_097B:
        mov [bp-0x10],bx                                ; 097B
        les bx,[bp+0x4]                                 ; 097E
        mov ax,[es:bx+0x14]                             ; 0981
        mov dx,[es:bx+0x16]                             ; 0985
        mov [bp-0x6],dx                                 ; 0989
        mov_ bx,ax                                      ; 098C

L5_098E:
        mov es,[bp-0x6]                                 ; 098E
        mov_ di,bx                                      ; 0991
        mov [bp-0x2],es                                 ; 0993
        les si,[bp-0xe]                                 ; 0996
        mov ax,[es:si+0x68]                             ; 0999
        mov dx,[es:si+0x6a]                             ; 099D
        mov es,[bp-0x2]                                 ; 09A1
        cmp [es:di+0x2],dx                              ; 09A4
        ja short L5_094F                                ; 09A8
        jc short L5_09B1                                ; 09AA
        cmp [es:di],ax                                  ; 09AC
        ja short L5_094F                                ; 09AF

L5_09B1:
        mov ax,[es:di]                                  ; 09B1
        mov dx,[es:di+0x2]                              ; 09B4
        mov es,[bp-0xc]                                 ; 09B8
        cmp [es:si+0x66],dx                             ; 09BB
        ja short L5_094F                                ; 09BF
        jc short L5_09C9                                ; 09C1
        cmp [es:si+0x64],ax                             ; 09C3
        ja short L5_094F                                ; 09C7

L5_09C9:
        add bx,byte +0x4                                ; 09C9
        inc cx                                          ; 09CC
        cmp [bp-0x10],cx                                ; 09CD
        ja short L5_098E                                ; 09D0
        jmp short L5_0A3B                               ; 09D2

L5_09D4:
        or_ bx,bx                                       ; 09D4
        jz short L5_0A3B                                ; 09D6
        mov [bp-0x10],bx                                ; 09D8
        les si,[bp+0x4]                                 ; 09DB
        mov ax,[es:si+0x14]                             ; 09DE
        mov dx,[es:si+0x16]                             ; 09E2
        mov_ bx,ax                                      ; 09E6
        mov [bp-0x6],dx                                 ; 09E8

L5_09EB:
        mov es,[bp-0x6]                                 ; 09EB
        mov_ di,bx                                      ; 09EE
        mov [bp-0x2],es                                 ; 09F0
        les si,[bp-0xe]                                 ; 09F3
        mov ax,[es:si+0x68]                             ; 09F6
        mov dx,[es:si+0x6a]                             ; 09FA
        mov es,[bp-0x2]                                 ; 09FE
        cmp [es:di+0x2],dx                              ; 0A01
        jng short L5_0A0A                               ; 0A05
        jmp near L5_094F                                ; 0A07

L5_0A0A:
        jl short L5_0A14                                ; 0A0A
        cmp [es:di],ax                                  ; 0A0C
        jna short L5_0A14                               ; 0A0F
        jmp near L5_094F                                ; 0A11

L5_0A14:
        mov ax,[es:di]                                  ; 0A14
        mov dx,[es:di+0x2]                              ; 0A17
        mov es,[bp-0xc]                                 ; 0A1B
        cmp [es:si+0x66],dx                             ; 0A1E
        jng short L5_0A27                               ; 0A22
        jmp near L5_094F                                ; 0A24

L5_0A27:
        jl short L5_0A32                                ; 0A27
        cmp [es:si+0x64],ax                             ; 0A29
        jna short L5_0A32                               ; 0A2D
        jmp near L5_094F                                ; 0A2F

L5_0A32:
        add bx,byte +0x4                                ; 0A32
        inc cx                                          ; 0A35
        cmp [bp-0x10],cx                                ; 0A36
        ja short L5_09EB                                ; 0A39

L5_0A3B:
        xor_ ax,ax                                      ; 0A3B

L5_0A3D:
        pop si                                          ; 0A3D
        pop di                                          ; 0A3E
        mov_ sp,bp                                      ; 0A3F
        pop bp                                          ; 0A41
        ret 0x8                                         ; 0A42
        db 0x90                                         ; 0A45

L5_0A46:
        push bp                                         ; 0A46
        mov_ bp,sp                                      ; 0A47
        push di                                         ; 0A49
        mov di,[bp+0xa]                                 ; 0A4A
        mov bx,[bp+0x6]                                 ; 0A4D
        mov es,[bp+0x8]                                 ; 0A50
        mov ax,[es:bx]                                  ; 0A53
        mov [di+0x12],ax                                ; 0A56
        mov [es:bx],di                                  ; 0A59
        pop di                                          ; 0A5C
        mov_ sp,bp                                      ; 0A5D
        pop bp                                          ; 0A5F
        retf                                            ; 0A60
        db 0x90                                         ; 0A61

L5_0A62:
        push bp                                         ; 0A62
        mov_ bp,sp                                      ; 0A63
        sub sp,byte +0xa                                ; 0A65
        push di                                         ; 0A68
        push si                                         ; 0A69
        mov di,[bp+0x6]                                 ; 0A6A
        mov si,[di+0x10]                                ; 0A6D
        mov ax,[si+0x26]                                ; 0A70
        mov dx,[si+0x28]                                ; 0A73
        mov_ bx,ax                                      ; 0A76
        mov [bp-0x2],dx                                 ; 0A78
        mov es,dx                                       ; 0A7B
        mov_ si,ax                                      ; 0A7D
        cmp word [es:si],byte +0x0                      ; 0A7F
        jz short L5_0AA8                                ; 0A83

L5_0A85:
        mov es,[bp-0x2]                                 ; 0A85
        mov ax,[es:bx]                                  ; 0A88
        cmp_ ax,di                                      ; 0A8B
        jz short L5_0AA0                                ; 0A8D
        mov_ si,ax                                      ; 0A8F
        add si,byte +0x12                               ; 0A91
        mov_ bx,si                                      ; 0A94
        mov [bp-0x2],ds                                 ; 0A96
        cmp word [si],byte +0x0                         ; 0A99
        jnz short L5_0A85                               ; 0A9C
        jmp short L5_0AA8                               ; 0A9E

L5_0AA0:
        mov_ si,ax                                      ; 0AA0
        mov ax,[si+0x12]                                ; 0AA2
        mov [es:bx],ax                                  ; 0AA5

L5_0AA8:
        pop si                                          ; 0AA8
        pop di                                          ; 0AA9
        mov_ sp,bp                                      ; 0AAA
        pop bp                                          ; 0AAC
        retf                                            ; 0AAD

L5_0AAE:
        push bp                                         ; 0AAE
        mov_ bp,sp                                      ; 0AAF
        sub sp,0xae                                     ; 0AB1
        push si                                         ; 0AB5
        mov si,[bp+0x6]                                 ; 0AB6
        mov word [bp-0x2e],0x2e                         ; 0AB9
        mov word [bp-0x2c],0x2a                         ; 0ABE
        mov word [bp-0x2a],0x404                        ; 0AC3
        sub_ ax,ax                                      ; 0AC8
        mov [bp-0x6],ax                                 ; 0ACA
        mov [bp-0x8],ax                                 ; 0ACD
        mov word [bp-0x4],0x4                           ; 0AD0
        mov [bp-0x2],ax                                 ; 0AD5
        push word [0xbd0]                               ; 0AD8
        mov ax,0x17                                     ; 0ADC
        push ax                                         ; 0ADF
        lea ax,[bp-0x6e]                                ; 0AE0
        push ss                                         ; 0AE3
        push ax                                         ; 0AE4
        mov ax,0x40                                     ; 0AE5
        push ax                                         ; 0AE8
        callp R5_0AEA, R5_016B, 0x0000                  ; 0AE9 USER.LoadString
        mov bx,[bp+0xa]                                 ; 0AEE
        push word [bx]                                  ; 0AF1
        lea ax,[bp-0x6e]                                ; 0AF3
        push ss                                         ; 0AF6
        push ax                                         ; 0AF7
        lea ax,[bp-0xae]                                ; 0AF8
        push ss                                         ; 0AFC
        push ax                                         ; 0AFD
        callp R5_0AFF, 0xFFFF, 0x0000                   ; 0AFE USER.wsprintf
        add sp,byte +0xa                                ; 0B03
        lea ax,[bp-0x28]                                ; 0B06
        push ss                                         ; 0B09
        push ax                                         ; 0B0A
        lea ax,[bp-0xae]                                ; 0B0B
        push ss                                         ; 0B0F
        push ax                                         ; 0B10
        mov ax,0x1f                                     ; 0B11
        push ax                                         ; 0B14
        callp R5_0B16, 0xFFFF, 0x0000                   ; 0B15 KERNEL.lstrcpyn
        mov es,[bp+0x8]                                 ; 0B1A
        push word [es:si+0x6]                           ; 0B1D
        push word [es:si+0x4]                           ; 0B21
        lea ax,[bp-0x2e]                                ; 0B25
        push ss                                         ; 0B28
        push ax                                         ; 0B29
        mov ax,[es:si]                                  ; 0B2A
        cmp ax,strict word 0x2e                         ; 0B2D
        jna short L5_0B35                               ; 0B30
        mov ax,0x2e                                     ; 0B32

L5_0B35:
        push ax                                         ; 0B35
        callf L1_1AD3, R5_0B39, 0xFFFF                  ; 0B36 far seg1
        pop si                                          ; 0B3B
        mov_ sp,bp                                      ; 0B3C
        pop bp                                          ; 0B3E
        retf 0x6                                        ; 0B3F

L5_0B42:
        push bp                                         ; 0B42
        mov_ bp,sp                                      ; 0B43
        push di                                         ; 0B45
        push si                                         ; 0B46
        mov ax,0x40                                     ; 0B47
        push ax                                         ; 0B4A
        mov ax,0x14                                     ; 0B4B
        push ax                                         ; 0B4E
        callp R5_0B50, 0xFFFF, 0x0000                   ; 0B4F KERNEL.LocalAlloc
        mov_ si,ax                                      ; 0B54
        or_ si,ax                                       ; 0B56
        jnz short L5_0B5F                               ; 0B58
        mov ax,0x7                                      ; 0B5A
        jmp short L5_0BBC                               ; 0B5D

L5_0B5F:
        mov ax,[bp+0x4]                                 ; 0B5F
        mov dx,[bp+0x6]                                 ; 0B62
        mov [si],ax                                     ; 0B65
        mov [si+0x2],dx                                 ; 0B67
        les bx,[bp+0x8]                                 ; 0B6A
        mov cx,[es:bx]                                  ; 0B6D
        mov [si+0x4],cx                                 ; 0B70
        mov cx,[es:bx+0x6]                              ; 0B73
        mov di,[es:bx+0x8]                              ; 0B77
        mov [si+0x6],cx                                 ; 0B7B
        mov [si+0x8],di                                 ; 0B7E
        and dx,byte +0x7                                ; 0B81
        mov [si+0xa],dx                                 ; 0B84
        mov ax,[es:bx+0xa]                              ; 0B87
        mov dx,[es:bx+0xc]                              ; 0B8B
        mov [si+0xc],ax                                 ; 0B8F
        mov [si+0xe],dx                                 ; 0B92
        mov word [si+0x12],0x0                          ; 0B95
        mov ax,[bp+0x10]                                ; 0B9A
        mov [si+0x10],ax                                ; 0B9D
        push si                                         ; 0BA0
        mov_ bx,ax                                      ; 0BA1
        push word [bx+0x28]                             ; 0BA3
        push word [bx+0x26]                             ; 0BA6
        push cs                                         ; 0BA9
        call L5_0A46                                    ; 0BAA
        add sp,byte +0x6                                ; 0BAD
        les bx,[bp+0xc]                                 ; 0BB0
        xor_ ax,ax                                      ; 0BB3
        mov [es:bx],si                                  ; 0BB5
        mov [es:bx+0x2],ax                              ; 0BB8

L5_0BBC:
        xor_ dx,dx                                      ; 0BBC
        pop si                                          ; 0BBE
        pop di                                          ; 0BBF
        mov_ sp,bp                                      ; 0BC0
        pop bp                                          ; 0BC2
        ret 0xe                                         ; 0BC3

L5_0BC6:
        push bp                                         ; 0BC6
        mov_ bp,sp                                      ; 0BC7
        push si                                         ; 0BC9
        mov si,[bp+0x4]                                 ; 0BCA
        push si                                         ; 0BCD
        push cs                                         ; 0BCE
        call L5_0A62                                    ; 0BCF
        pop bx                                          ; 0BD2
        push si                                         ; 0BD3
        callp R5_0BD5, 0xFFFF, 0x0000                   ; 0BD4 KERNEL.LocalFree
        xor_ ax,ax                                      ; 0BD9
        cwd                                             ; 0BDB
        pop si                                          ; 0BDC
        mov_ sp,bp                                      ; 0BDD
        pop bp                                          ; 0BDF
        ret 0x2                                         ; 0BE0
        db 0x90                                         ; 0BE3

L5_0BE4:
        push bp                                         ; 0BE4
        mov_ bp,sp                                      ; 0BE5
        sub sp,byte +0x26                               ; 0BE7
        push di                                         ; 0BEA
        push si                                         ; 0BEB
        mov si,[bp+0xc]                                 ; 0BEC
        or_ si,si                                       ; 0BEF
        jnz short L5_0BF9                               ; 0BF1
        mov ax,0x5                                      ; 0BF3
        jmp near L5_108C                                ; 0BF6

L5_0BF9:
        mov bx,[si+0x10]                                ; 0BF9
        cmp word [bx+0x1c],byte +0x0                    ; 0BFC
        jnz short L5_0C08                               ; 0C00
        mov ax,0x3                                      ; 0C02
        jmp near L5_108C                                ; 0C05

L5_0C08:
        mov ax,[bx+0x26]                                ; 0C08
        mov dx,[bx+0x28]                                ; 0C0B
        mov [bp-0x16],ax                                ; 0C0E
        mov [bp-0x14],dx                                ; 0C11
        mov di,[bp+0x8]                                 ; 0C14
        mov es,[bp+0xa]                                 ; 0C17
        mov ax,[es:di]                                  ; 0C1A
        mov dx,[es:di+0x2]                              ; 0C1D
        or_ dx,dx                                       ; 0C21
        jnz short L5_0C2A                               ; 0C23
        cmp ax,0xa6                                     ; 0C25
        jna short L5_0C2D                               ; 0C28

L5_0C2A:
        mov ax,0xa6                                     ; 0C2A

L5_0C2D:
        mov [bp-0x18],ax                                ; 0C2D
        mov [es:di],ax                                  ; 0C30
        mov word [es:di+0x2],0x0                        ; 0C33
        sub word [bp-0x18],byte +0x10                   ; 0C39
        mov al,[bp+0x4]                                 ; 0C3D
        and ax,strict word 0xf                          ; 0C40
        sub_ dx,dx                                      ; 0C43
        or_ ax,ax                                       ; 0C45
        jz short L5_0C81                                ; 0C47
        dec ax                                          ; 0C49
        jnz short L5_0C4F                               ; 0C4A
        jmp near L5_0F50                                ; 0C4C

L5_0C4F:
        dec ax                                          ; 0C4F
        jz short L5_0C61                                ; 0C50
        dec ax                                          ; 0C52
        jz short L5_0C9C                                ; 0C53
        dec ax                                          ; 0C55
        jnz short L5_0C5B                               ; 0C56
        jmp near L5_0D9A                                ; 0C58

L5_0C5B:
        mov ax,0x8                                      ; 0C5B
        jmp near L5_108C                                ; 0C5E

L5_0C61:
        mov ax,[es:di+0xc]                              ; 0C61
        mov [es:di+0x4],ax                              ; 0C65
        mov [es:di+0x6],dx                              ; 0C69
        mov ax,[es:di+0xe]                              ; 0C6D
        mov [es:di+0x8],ax                              ; 0C71
        mov [es:di+0xa],dx                              ; 0C75
        cmp ax,strict word 0xffff                       ; 0C79
        jz short L5_0C81                                ; 0C7C
        jmp near L5_0F50                                ; 0C7E

L5_0C81:
        les bx,[bp+0x8]                                 ; 0C81
        cmp word [es:bx+0x6],byte +0x0                  ; 0C84
        jnz short L5_0C95                               ; 0C89
        cmp word [es:bx+0x4],byte +0x3                  ; 0C8B
        ja short L5_0C95                                ; 0C90
        jmp near L5_0F64                                ; 0C92

L5_0C95:
        mov ax,0x400                                    ; 0C95
        cwd                                             ; 0C98
        jmp near L5_108E                                ; 0C99

L5_0C9C:
        xor_ cx,cx                                      ; 0C9C
        mov ax,[bp-0x16]                                ; 0C9E
        mov dx,[bp-0x14]                                ; 0CA1
        add ax,0x696                                    ; 0CA4
        mov_ bx,ax                                      ; 0CA7
        mov [bp-0x2],dx                                 ; 0CA9

L5_0CAC:
        mov es,[bp+0xa]                                 ; 0CAC
        mov ax,[es:di+0x18]                             ; 0CAF
        mov dx,[es:di+0x1a]                             ; 0CB3
        mov es,[bp-0x2]                                 ; 0CB7
        cmp ax,[es:bx]                                  ; 0CBA
        jnz short L5_0CC5                               ; 0CBD
        cmp dx,[es:bx+0x2]                              ; 0CBF
        jz short L5_0CCF                                ; 0CC3

L5_0CC5:
        add bx,0xa6                                     ; 0CC5
        inc cx                                          ; 0CC9
        cmp cx,byte +0x4                                ; 0CCA
        jc short L5_0CAC                                ; 0CCD

L5_0CCF:
        cmp cx,byte +0x4                                ; 0CCF
        jz short L5_0CE3                                ; 0CD2
        les bx,[bp+0x8]                                 ; 0CD4
        mov [es:bx+0x4],cx                              ; 0CD7

L5_0CDB:
        mov word [es:bx+0x6],0x0                        ; 0CDB
        jmp short L5_0C81                               ; 0CE1

L5_0CE3:
        mov word [bp-0xc],0x0                           ; 0CE3
        mov ax,[bp-0x16]                                ; 0CE8
        mov dx,[bp-0x14]                                ; 0CEB
        add ax,0x2690                                   ; 0CEE
        mov [bp-0x10],ax                                ; 0CF1
        mov [bp-0xe],dx                                 ; 0CF4
        mov ax,[bp-0x16]                                ; 0CF7
        add ax,0x69e                                    ; 0CFA
        mov [bp-0xa],ax                                 ; 0CFD
        mov [bp-0x8],dx                                 ; 0D00

L5_0D03:
        mov word [bp-0x6],0x0                           ; 0D03
        les bx,[bp-0xa]                                 ; 0D08
        mov ax,[es:bx]                                  ; 0D0B
        mov dx,[es:bx+0x2]                              ; 0D0E
        mov [bp-0x1c],ax                                ; 0D12
        mov [bp-0x1a],dx                                ; 0D15
        or_ dx,dx                                       ; 0D18
        jnz short L5_0D20                               ; 0D1A
        or_ ax,ax                                       ; 0D1C
        jz short L5_0D6D                                ; 0D1E

L5_0D20:
        mov ax,[bp-0x10]                                ; 0D20
        mov dx,[bp-0xe]                                 ; 0D23
        mov [bp-0x2],dx                                 ; 0D26
        mov_ bx,ax                                      ; 0D29
        mov di,[bp-0x6]                                 ; 0D2B

L5_0D2E:
        les si,[bp+0x8]                                 ; 0D2E
        mov ax,[es:si+0x18]                             ; 0D31
        mov dx,[es:si+0x1a]                             ; 0D35
        mov_ cx,ax                                      ; 0D39
        mov ax,0xa6                                     ; 0D3B
        mov es,[bp-0x2]                                 ; 0D3E
        mov_ si,dx                                      ; 0D41
        mul word [es:bx]                                ; 0D43
        mov_ dx,si                                      ; 0D46
        les si,[bp-0x16]                                ; 0D48
        add_ si,ax                                      ; 0D4B
        cmp [es:si+0x1a],cx                             ; 0D4D
        jnz short L5_0D59                               ; 0D51
        cmp [es:si+0x1c],dx                             ; 0D53
        jz short L5_0D6A                                ; 0D57

L5_0D59:
        inc bx                                          ; 0D59
        inc bx                                          ; 0D5A
        inc di                                          ; 0D5B
        sub_ ax,ax                                      ; 0D5C
        cmp [bp-0x1a],ax                                ; 0D5E
        ja short L5_0D2E                                ; 0D61
        jc short L5_0D6A                                ; 0D63
        cmp [bp-0x1c],di                                ; 0D65
        ja short L5_0D2E                                ; 0D68

L5_0D6A:
        mov [bp-0x6],di                                 ; 0D6A

L5_0D6D:
        mov ax,[bp-0x6]                                 ; 0D6D
        sub_ dx,dx                                      ; 0D70
        cmp ax,[bp-0x1c]                                ; 0D72
        jz short L5_0D7A                                ; 0D75
        jmp near L5_0F30                                ; 0D77

L5_0D7A:
        cmp dx,[bp-0x1a]                                ; 0D7A
        jz short L5_0D82                                ; 0D7D
        jmp near L5_0F30                                ; 0D7F

L5_0D82:
        add word [bp-0x10],byte +0x14                   ; 0D82
        add word [bp-0xa],0xa6                          ; 0D86
        inc word [bp-0xc]                               ; 0D8B
        cmp word [bp-0xc],byte +0x4                     ; 0D8E
        jnc short L5_0D97                               ; 0D92
        jmp near L5_0D03                                ; 0D94

L5_0D97:
        jmp near L5_0FF0                                ; 0D97

L5_0D9A:
        cmp word [es:di+0x80],byte +0x2e                ; 0D9A
        jnz short L5_0DB7                               ; 0DA0
        cmp word [es:di+0x84],0x404                     ; 0DA2
        jnz short L5_0DB7                               ; 0DA9
        mov ax,[es:di+0x78]                             ; 0DAB
        mov dx,[es:di+0x7a]                             ; 0DAF
        or_ dx,dx                                       ; 0DB3
        jz short L5_0DBA                                ; 0DB5

L5_0DB7:
        jmp near L5_0FF0                                ; 0DB7

L5_0DBA:
        dec ax                                          ; 0DBA
        jz short L5_0DCF                                ; 0DBB
        dec ax                                          ; 0DBD
        jz short L5_0DDA                                ; 0DBE
        dec ax                                          ; 0DC0
        jz short L5_0DE2                                ; 0DC1
        dec ax                                          ; 0DC3
        jnz short L5_0DC9                               ; 0DC4
        jmp near L5_0FF0                                ; 0DC6

L5_0DC9:
        dec ax                                          ; 0DC9
        jz short L5_0DEA                                ; 0DCA
        jmp near L5_0FF0                                ; 0DCC

L5_0DCF:
        cmp word [es:di+0x82],byte +0x28                ; 0DCF

L5_0DD5:
        jz short L5_0DF5                                ; 0DD5
        jmp near L5_0FF0                                ; 0DD7

L5_0DDA:
        cmp word [es:di+0x82],byte +0x29                ; 0DDA
        jmp short L5_0DD5                               ; 0DE0

L5_0DE2:
        cmp word [es:di+0x82],byte +0x4                 ; 0DE2
        jmp short L5_0DD5                               ; 0DE8

L5_0DEA:
        cmp word [es:di+0x82],byte +0x3                 ; 0DEA
        jz short L5_0DF5                                ; 0DF0
        jmp near L5_0FF0                                ; 0DF2

L5_0DF5:
        xor_ di,di                                      ; 0DF5
        mov ax,[bp-0x16]                                ; 0DF7
        mov dx,[bp-0x14]                                ; 0DFA
        add ax,0x6f6                                    ; 0DFD
        mov_ si,ax                                      ; 0E00
        mov [bp-0x2],dx                                 ; 0E02

L5_0E05:
        mov es,[bp-0x2]                                 ; 0E05
        mov ax,[es:si]                                  ; 0E08
        mov dx,[es:si+0x2]                              ; 0E0B
        les bx,[bp+0x8]                                 ; 0E0F
        cmp [es:bx+0x78],ax                             ; 0E12
        jnz short L5_0E34                               ; 0E16
        cmp [es:bx+0x7a],dx                             ; 0E18
        jnz short L5_0E34                               ; 0E1C
        lea ax,[bx+0x86]                                ; 0E1E
        push es                                         ; 0E22
        push ax                                         ; 0E23
        lea ax,[si+0xe]                                 ; 0E24
        push word [bp-0x2]                              ; 0E27
        push ax                                         ; 0E2A
        callp R5_0E2C, 0xFFFF, 0x0000                   ; 0E2B USER.lstrcmpi
        or_ ax,ax                                       ; 0E30
        jz short L5_0E3E                                ; 0E32

L5_0E34:
        add si,0xa6                                     ; 0E34
        inc di                                          ; 0E38
        cmp di,byte +0x4                                ; 0E39
        jc short L5_0E05                                ; 0E3C

L5_0E3E:
        cmp di,byte +0x4                                ; 0E3E
        jz short L5_0E4D                                ; 0E41
        les bx,[bp+0x8]                                 ; 0E43
        mov [es:bx+0x4],di                              ; 0E46
        jmp near L5_0CDB                                ; 0E4A

L5_0E4D:
        xor_ ax,ax                                      ; 0E4D
        mov [bp-0xc],ax                                 ; 0E4F
        mov [bp-0xe],ax                                 ; 0E52
        mov ax,[bp-0x16]                                ; 0E55
        mov dx,[bp-0x14]                                ; 0E58
        add ax,0x2690                                   ; 0E5B
        mov [bp-0x12],ax                                ; 0E5E
        mov [bp-0x10],dx                                ; 0E61
        mov ax,[bp-0x16]                                ; 0E64
        add ax,0x69e                                    ; 0E67
        mov [bp-0xa],ax                                 ; 0E6A
        mov [bp-0x8],dx                                 ; 0E6D

L5_0E70:
        xor_ di,di                                      ; 0E70
        mov [bp-0x6],di                                 ; 0E72
        les bx,[bp-0xa]                                 ; 0E75
        cmp [es:bx+0x2],di                              ; 0E78
        jnz short L5_0E83                               ; 0E7C
        cmp [es:bx],di                                  ; 0E7E
        jz short L5_0F01                                ; 0E81

L5_0E83:
        mov ax,[bp-0x12]                                ; 0E83
        mov dx,[bp-0x10]                                ; 0E86
        mov [bp-0x2],dx                                 ; 0E89
        mov_ si,ax                                      ; 0E8C

L5_0E8E:
        mov ax,[bp-0xe]                                 ; 0E8E
        add_ ax,di                                      ; 0E91
        mov [bp-0x1e],ax                                ; 0E93
        les bx,[bp+0x8]                                 ; 0E96
        mov ax,[es:bx+0x78]                             ; 0E99
        mov dx,[es:bx+0x7a]                             ; 0E9D
        mov_ cx,ax                                      ; 0EA1
        mov ax,0xa6                                     ; 0EA3
        mov es,[bp-0x2]                                 ; 0EA6
        mov_ bx,dx                                      ; 0EA9
        mul word [es:si]                                ; 0EAB
        mov_ dx,bx                                      ; 0EAE
        les bx,[bp-0x16]                                ; 0EB0
        add_ bx,ax                                      ; 0EB3
        cmp [es:bx+0x7a],cx                             ; 0EB5
        jnz short L5_0EE9                               ; 0EB9
        cmp [es:bx+0x7c],dx                             ; 0EBB
        jnz short L5_0EE9                               ; 0EBF
        mov ax,[bp+0x8]                                 ; 0EC1
        mov dx,[bp+0xa]                                 ; 0EC4
        add ax,0x86                                     ; 0EC7
        push dx                                         ; 0ECA
        push ax                                         ; 0ECB
        mov ax,0xa6                                     ; 0ECC
        mov es,[bp-0x2]                                 ; 0ECF
        mul word [es:si]                                ; 0ED2
        add ax,[bp-0x16]                                ; 0ED5
        mov dx,[bp-0x14]                                ; 0ED8
        add ax,0x88                                     ; 0EDB
        push dx                                         ; 0EDE
        push ax                                         ; 0EDF
        callp R5_0EE1, R5_0E2C, 0x0000                  ; 0EE0 USER.lstrcmpi
        or_ ax,ax                                       ; 0EE5
        jz short L5_0EFE                                ; 0EE7

L5_0EE9:
        inc si                                          ; 0EE9
        inc si                                          ; 0EEA
        inc di                                          ; 0EEB
        sub_ ax,ax                                      ; 0EEC
        les bx,[bp-0xa]                                 ; 0EEE
        cmp [es:bx+0x2],ax                              ; 0EF1
        ja short L5_0E8E                                ; 0EF5
        jc short L5_0EFE                                ; 0EF7
        cmp [es:bx],di                                  ; 0EF9
        ja short L5_0E8E                                ; 0EFC

L5_0EFE:
        mov [bp-0x6],di                                 ; 0EFE

L5_0F01:
        les bx,[bp-0xa]                                 ; 0F01
        mov ax,[bp-0x6]                                 ; 0F04
        sub_ dx,dx                                      ; 0F07
        cmp ax,[es:bx]                                  ; 0F09
        jnz short L5_0F30                               ; 0F0C
        cmp dx,[es:bx+0x2]                              ; 0F0E
        jnz short L5_0F30                               ; 0F12
        add word [bp-0xe],byte +0xa                     ; 0F14
        add word [bp-0x12],byte +0x14                   ; 0F18
        add word [bp-0xa],0xa6                          ; 0F1C
        inc word [bp-0xc]                               ; 0F21
        cmp word [bp-0xc],byte +0x4                     ; 0F24
        jnc short L5_0F2D                               ; 0F28
        jmp near L5_0E70                                ; 0F2A

L5_0F2D:
        jmp near L5_0FF0                                ; 0F2D

L5_0F30:
        mov di,[bp+0x8]                                 ; 0F30
        mov ax,[bp-0xc]                                 ; 0F33
        mov es,[bp+0xa]                                 ; 0F36
        mov [es:di+0x4],ax                              ; 0F39
        mov word [es:di+0x6],0x0                        ; 0F3D
        mov ax,[bp-0x6]                                 ; 0F43
        mov [es:di+0x8],ax                              ; 0F46
        mov word [es:di+0xa],0x0                        ; 0F4A

L5_0F50:
        cmp word [es:di+0x6],byte +0x0                  ; 0F50
        jz short L5_0F5A                                ; 0F55
        jmp near L5_0C95                                ; 0F57

L5_0F5A:
        cmp word [es:di+0x4],byte +0x3                  ; 0F5A
        jna short L5_0FB6                               ; 0F5F
        jmp near L5_0C95                                ; 0F61

L5_0F64:
        mov ax,[es:bx+0x4]                              ; 0F64
        mov [es:bx+0xc],ax                              ; 0F68
        mov word [es:bx+0xe],0xffff                     ; 0F6C
        mov cx,0xa6                                     ; 0F72
        mul cx                                          ; 0F75
        add ax,[bp-0x16]                                ; 0F77
        mov dx,[bp-0x14]                                ; 0F7A
        add ax,0x68e                                    ; 0F7D
        add bx,byte +0x10                               ; 0F80
        mov cx,[bp-0x18]                                ; 0F83
        mov [bp-0x22],bx                                ; 0F86
        push ds                                         ; 0F89
        mov_ di,bx                                      ; 0F8A
        mov_ si,ax                                      ; 0F8C
        mov ds,dx                                       ; 0F8E
        shr cx,1                                        ; 0F90
        rep movsw                                       ; 0F92
        adc_ cx,cx                                      ; 0F94
        rep movsb                                       ; 0F96
        pop ds                                          ; 0F98
        mov bx,[bp-0x22]                                ; 0F99
        test word [es:bx+0x2],0x800                     ; 0F9C
        jz short L5_0FB1                                ; 0FA2
        mov bx,[bp+0x8]                                 ; 0FA4
        and byte [es:bx+0x10],0xfe                      ; 0FA7
        and byte [es:bx+0x13],0xf7                      ; 0FAC

L5_0FB1:
        xor_ ax,ax                                      ; 0FB1
        jmp near L5_108C                                ; 0FB3

L5_0FB6:
        cmp word [es:di+0xa],byte +0x0                  ; 0FB6
        jnz short L5_0FF0                               ; 0FBB
        cmp word [es:di+0x8],byte +0x9                  ; 0FBD
        ja short L5_0FF0                                ; 0FC2
        mov_ bx,di                                      ; 0FC4
        mov_ si,bx                                      ; 0FC6
        mov bx,[es:bx+0x4]                              ; 0FC8
        mov_ ax,bx                                      ; 0FCC
        mov di,[es:si+0x8]                              ; 0FCE
        mov cx,0xa                                      ; 0FD2
        mul cx                                          ; 0FD5
        mov_ si,ax                                      ; 0FD7
        add_ si,di                                      ; 0FD9
        add_ si,si                                      ; 0FDB
        add si,[bp-0x16]                                ; 0FDD
        mov es,[bp-0x14]                                ; 0FE0
        mov ax,[es:si+0x2690]                           ; 0FE3
        mov [bp-0x2],ax                                 ; 0FE8
        cmp ax,strict word 0xffff                       ; 0FEB
        jnz short L5_0FF6                               ; 0FEE

L5_0FF0:
        mov ax,0x400                                    ; 0FF0
        jmp near L5_108C                                ; 0FF3

L5_0FF6:
        mov [bp-0x6],di                                 ; 0FF6
        mov [bp-0xc],bx                                 ; 0FF9
        mov_ cx,bx                                      ; 0FFC
        les bx,[bp+0x8]                                 ; 0FFE
        mov [es:bx+0xc],cx                              ; 1001
        mov [es:bx+0xe],di                              ; 1005
        mov ax,0xa6                                     ; 1009
        mul word [bp-0x2]                               ; 100C
        add ax,[bp-0x16]                                ; 100F
        mov dx,[bp-0x14]                                ; 1012
        add ax,strict word 0x12                         ; 1015
        add bx,byte +0x10                               ; 1018
        mov cx,[bp-0x18]                                ; 101B
        mov [bp-0x26],bx                                ; 101E
        mov [bp-0x24],es                                ; 1021
        push ds                                         ; 1024
        mov_ di,bx                                      ; 1025
        mov_ si,ax                                      ; 1027
        mov ds,dx                                       ; 1029
        shr cx,1                                        ; 102B
        rep movsw                                       ; 102D
        adc_ cx,cx                                      ; 102F
        rep movsb                                       ; 1031
        pop ds                                          ; 1033
        mov ax,0xa                                      ; 1034
        mul word [bp-0xc]                               ; 1037
        mov_ si,ax                                      ; 103A
        add si,[bp-0x6]                                 ; 103C
        mov [bp-0x1e],si                                ; 103F
        add_ si,si                                      ; 1042
        add_ si,si                                      ; 1044
        les bx,[bp-0x16]                                ; 1046
        mov ax,[es:bx+si+0x276c]                        ; 1049
        mov dx,[es:bx+si+0x276e]                        ; 104E
        les bx,[bp-0x26]                                ; 1053
        or [es:bx],ax                                   ; 1056
        or [es:bx+0x2],dx                               ; 1059
        test word [es:bx+0x2],0x800                     ; 105D
        jz short L5_1072                                ; 1063
        les bx,[bp+0x8]                                 ; 1065
        and byte [es:bx+0x10],0xfe                      ; 1068
        and byte [es:bx+0x13],0xf7                      ; 106D

L5_1072:
        mov si,[bp-0x1e]                                ; 1072
        add_ si,si                                      ; 1075
        les bx,[bp-0x16]                                ; 1077
        mov ax,[es:bx+si+0x271c]                        ; 107A
        les bx,[bp+0x8]                                 ; 107F
        mov [es:bx+0x24],ax                             ; 1082
        xor_ ax,ax                                      ; 1086
        mov [es:bx+0x26],ax                             ; 1088

L5_108C:
        xor_ dx,dx                                      ; 108C

L5_108E:
        pop si                                          ; 108E
        pop di                                          ; 108F
        mov_ sp,bp                                      ; 1090
        pop bp                                          ; 1092
        ret 0xa                                         ; 1093
        db 0x90, 0x90                                   ; 1096

L5_1098:
        push bp                                         ; 1098
        mov_ bp,sp                                      ; 1099
        sub sp,byte +0x1c                               ; 109B
        push di                                         ; 109E
        push si                                         ; 109F
        mov si,[bp+0xc]                                 ; 10A0
        or_ si,si                                       ; 10A3
        jnz short L5_10AD                               ; 10A5
        mov ax,0x5                                      ; 10A7
        jmp near L5_1357                                ; 10AA

L5_10AD:
        mov bx,[si+0x10]                                ; 10AD
        cmp word [bx+0x1c],byte +0x0                    ; 10B0
        jnz short L5_10BC                               ; 10B4
        mov ax,0x3                                      ; 10B6
        jmp near L5_1357                                ; 10B9

L5_10BC:
        mov ax,[bx+0x26]                                ; 10BC
        mov dx,[bx+0x28]                                ; 10BF
        mov_ cx,ax                                      ; 10C2
        mov [bp-0x12],dx                                ; 10C4
        mov di,[bp+0x8]                                 ; 10C7
        mov es,[bp+0xa]                                 ; 10CA
        mov ax,[es:di+0x14]                             ; 10CD
        mov dx,[es:di+0x16]                             ; 10D1
        mov [bp-0x18],ax                                ; 10D5
        mov [bp-0x16],dx                                ; 10D8
        mov al,[bp+0x4]                                 ; 10DB
        and ax,strict word 0xf                          ; 10DE
        sub_ dx,dx                                      ; 10E1
        or_ ax,ax                                       ; 10E3
        jz short L5_10F9                                ; 10E5
        dec ax                                          ; 10E7
        jnz short L5_10ED                               ; 10E8
        jmp near L5_11F5                                ; 10EA

L5_10ED:
        dec ax                                          ; 10ED
        jnz short L5_10F3                               ; 10EE
        jmp near L5_128A                                ; 10F0

L5_10F3:
        mov ax,0x8                                      ; 10F3
        jmp near L5_1357                                ; 10F6

L5_10F9:
        mov ax,[es:di+0x4]                              ; 10F9
        mov [bp-0x10],ax                                ; 10FD
        cmp ax,strict word 0x3                          ; 1100
        jna short L5_1108                               ; 1103
        jmp near L5_12AA                                ; 1105

L5_1108:
        mov [bp-0x14],cx                                ; 1108
        mov_ bx,di                                      ; 110B
        mov_ di,cx                                      ; 110D
        mov ax,[es:bx+0x6]                              ; 110F
        mov_ si,ax                                      ; 1113
        cmp si,byte -0x1                                ; 1115
        jnz short L5_113B                               ; 1118
        mov ax,0xa6                                     ; 111A
        mul word [bp-0x10]                              ; 111D
        mov_ bx,ax                                      ; 1120
        mov ax,[bp-0x12]                                ; 1122
        add_ bx,cx                                      ; 1125
        mov es,ax                                       ; 1127
        mov ax,[es:bx+0x6a2]                            ; 1129
        mov dx,[es:bx+0x6a4]                            ; 112E
        mov [bp-0x4],ax                                 ; 1133
        mov [bp-0x2],dx                                 ; 1136
        jmp short L5_1163                               ; 1139

L5_113B:
        cmp si,byte +0x9                                ; 113B
        jna short L5_1143                               ; 113E
        jmp near L5_12AA                                ; 1140

L5_1143:
        mov ax,0xa                                      ; 1143
        mul word [bp-0x10]                              ; 1146
        mov_ bx,ax                                      ; 1149
        add_ bx,si                                      ; 114B
        add_ bx,bx                                      ; 114D
        mov ax,[bp-0x12]                                ; 114F
        add_ bx,di                                      ; 1152
        mov es,ax                                       ; 1154
        mov ax,[es:bx+0x271c]                           ; 1156
        mov [bp-0x4],ax                                 ; 115B
        mov word [bp-0x2],0x0                           ; 115E

L5_1163:
        mov ax,[bp-0x4]                                 ; 1163
        mov dx,[bp-0x2]                                 ; 1166
        les bx,[bp+0x8]                                 ; 1169
        cmp [es:bx+0xc],ax                              ; 116C
        jnz short L5_1178                               ; 1170
        cmp [es:bx+0xe],dx                              ; 1172
        jz short L5_117E                                ; 1176

L5_1178:
        mov ax,0xb                                      ; 1178
        jmp near L5_1357                                ; 117B

L5_117E:
        mov [bp-0xe],si                                 ; 117E
        mov_ ax,di                                      ; 1181
        mov dx,[bp-0x12]                                ; 1183
        add ax,0x916                                    ; 1186
        mov [bp-0x4],ax                                 ; 1189
        mov [bp-0x2],dx                                 ; 118C
        lea ax,[di+0x256a]                              ; 118F
        mov [bp-0x8],ax                                 ; 1193
        mov [bp-0x6],dx                                 ; 1196
        mov word [bp-0xc],0x31                          ; 1199

L5_119E:
        les bx,[bp-0x8]                                 ; 119E
        mov ax,[es:bx]                                  ; 11A1
        cmp [bp-0x10],ax                                ; 11A4
        jnz short L5_11D5                               ; 11A7
        mov ax,[bp-0xe]                                 ; 11A9
        cmp [es:bx+0x2],ax                              ; 11AC
        jnz short L5_11D5                               ; 11B0
        les bx,[bp-0x4]                                 ; 11B2
        mov dx,es                                       ; 11B5
        mov cx,[es:bx]                                  ; 11B7
        push ds                                         ; 11BA
        mov_ si,bx                                      ; 11BB
        mov ds,dx                                       ; 11BD
        les di,[bp-0x18]                                ; 11BF
        shr cx,1                                        ; 11C2
        rep movsw                                       ; 11C4
        adc_ cx,cx                                      ; 11C6
        rep movsb                                       ; 11C8
        pop ds                                          ; 11CA
        les bx,[bp+0x8]                                 ; 11CB
        mov ax,[es:bx+0x10]                             ; 11CE
        add [bp-0x18],ax                                ; 11D2

L5_11D5:
        add word [bp-0x4],0x94                          ; 11D5
        add word [bp-0x8],byte +0x6                     ; 11DA
        dec word [bp-0xc]                               ; 11DE
        jnz short L5_119E                               ; 11E1
        les bx,[bp+0x8]                                 ; 11E3
        mov word [es:bx+0x10],0x94                      ; 11E6
        mov word [es:bx+0x12],0x0                       ; 11EC
        jmp near L5_1355                                ; 11F2

L5_11F5:
        mov [bp-0x14],cx                                ; 11F5
        push word [bp-0x12]                             ; 11F8
        push cx                                         ; 11FB
        mov_ bx,di                                      ; 11FC
        push word [es:bx+0xa]                           ; 11FE
        push word [es:bx+0x8]                           ; 1202
        call L5_08B0                                    ; 1206
        mov_ si,ax                                      ; 1209
        or_ si,ax                                       ; 120B
        jz short L5_1214                                ; 120D
        sub_ dx,dx                                      ; 120F
        jmp near L5_1359                                ; 1211

L5_1214:
        mov ax,0x94                                     ; 1214
        les bx,[bp+0x8]                                 ; 1217
        mul word [es:bx+0x8]                            ; 121A
        mov_ si,ax                                      ; 121E
        les bx,[bp-0x14]                                ; 1220
        mov ax,[es:bx+si+0x916]                         ; 1223
        mov dx,[es:bx+si+0x918]                         ; 1228
        les bx,[bp+0x8]                                 ; 122D
        mov [es:bx+0x10],ax                             ; 1230
        mov [es:bx+0x12],dx                             ; 1234
        mov ax,[es:bx+0x8]                              ; 1238
        mov cx,0x94                                     ; 123C
        mov_ di,ax                                      ; 123F
        mul cx                                          ; 1241
        add ax,[bp-0x14]                                ; 1243
        mov dx,[bp-0x12]                                ; 1246
        add ax,0x916                                    ; 1249
        mov cx,[es:bx+0x10]                             ; 124C
        mov_ bx,di                                      ; 1250
        push ds                                         ; 1252
        mov_ si,ax                                      ; 1253
        mov ds,dx                                       ; 1255
        les di,[bp-0x18]                                ; 1257
        shr cx,1                                        ; 125A
        rep movsw                                       ; 125C
        adc_ cx,cx                                      ; 125E
        rep movsb                                       ; 1260
        pop ds                                          ; 1262
        mov_ ax,bx                                      ; 1263
        mov cx,0x6                                      ; 1265
        mul cx                                          ; 1268
        mov_ bx,ax                                      ; 126A
        add bx,[bp-0x14]                                ; 126C
        mov es,[bp-0x12]                                ; 126F
        mov ax,[es:bx+0x256c]                           ; 1272
        mov cx,[es:bx+0x256a]                           ; 1277
        les bx,[bp+0x8]                                 ; 127C
        mov [es:bx+0x4],cx                              ; 127F
        mov [es:bx+0x6],ax                              ; 1283
        jmp near L5_1355                                ; 1287

L5_128A:
        mov [bp-0x14],cx                                ; 128A
        mov ax,[es:di+0x4]                              ; 128D
        mov [bp-0x10],ax                                ; 1291
        cmp ax,strict word 0x3                          ; 1294
        ja short L5_12AA                                ; 1297
        mov ax,[es:di+0x6]                              ; 1299
        mov [bp-0xe],ax                                 ; 129D
        cmp ax,strict word 0xffff                       ; 12A0
        jz short L5_12B0                                ; 12A3
        cmp ax,strict word 0x9                          ; 12A5
        jna short L5_12B0                               ; 12A8

L5_12AA:
        mov ax,0x400                                    ; 12AA
        jmp near L5_1357                                ; 12AD

L5_12B0:
        xor_ si,si                                      ; 12B0
        mov_ ax,cx                                      ; 12B2
        mov dx,[bp-0x12]                                ; 12B4
        add ax,0x91e                                    ; 12B7
        mov_ cx,ax                                      ; 12BA
        mov [bp-0x6],dx                                 ; 12BC
        mov ax,[bp-0x14]                                ; 12BF
        add ax,0x256a                                   ; 12C2
        mov [bp-0x4],ax                                 ; 12C5
        mov [bp-0x2],dx                                 ; 12C8
        mov_ bx,ax                                      ; 12CB
        mov_ di,si                                      ; 12CD

L5_12CF:
        mov ax,[bp-0x10]                                ; 12CF
        mov es,[bp-0x2]                                 ; 12D2
        cmp [es:bx],ax                                  ; 12D5
        jnz short L5_12FE                               ; 12D8
        mov ax,[bp-0xe]                                 ; 12DA
        cmp [es:bx+0x2],ax                              ; 12DD
        jnz short L5_12FE                               ; 12E1
        mov_ si,cx                                      ; 12E3
        mov es,[bp-0x6]                                 ; 12E5
        mov ax,[es:si]                                  ; 12E8
        mov dx,[es:si+0x2]                              ; 12EB
        les si,[bp+0x8]                                 ; 12EF
        cmp [es:si+0x8],ax                              ; 12F2
        jnz short L5_12FE                               ; 12F6
        cmp [es:si+0xa],dx                              ; 12F8
        jz short L5_130B                                ; 12FC

L5_12FE:
        add cx,0x94                                     ; 12FE
        add bx,byte +0x6                                ; 1302
        inc di                                          ; 1305
        cmp di,byte +0x31                               ; 1306
        jc short L5_12CF                                ; 1309

L5_130B:
        cmp di,byte +0x31                               ; 130B
        jnz short L5_1315                               ; 130E
        mov ax,0x401                                    ; 1310
        jmp short L5_1357                               ; 1313

L5_1315:
        mov ax,0x94                                     ; 1315
        mul di                                          ; 1318
        mov_ si,ax                                      ; 131A
        les bx,[bp-0x14]                                ; 131C
        mov ax,[es:bx+si+0x916]                         ; 131F
        mov dx,[es:bx+si+0x918]                         ; 1324
        les bx,[bp+0x8]                                 ; 1329
        mov [es:bx+0x10],ax                             ; 132C
        mov [es:bx+0x12],dx                             ; 1330
        add si,[bp-0x14]                                ; 1334
        mov cx,[bp-0x12]                                ; 1337
        add si,0x916                                    ; 133A
        mov [bp-0x1a],cx                                ; 133E
        mov_ cx,ax                                      ; 1341
        mov dx,[bp-0x1a]                                ; 1343
        push ds                                         ; 1346
        mov ds,dx                                       ; 1347
        les di,[bp-0x18]                                ; 1349
        shr cx,1                                        ; 134C
        rep movsw                                       ; 134E
        adc_ cx,cx                                      ; 1350
        rep movsb                                       ; 1352
        pop ds                                          ; 1354

L5_1355:
        xor_ ax,ax                                      ; 1355

L5_1357:
        xor_ dx,dx                                      ; 1357

L5_1359:
        pop si                                          ; 1359
        pop di                                          ; 135A
        mov_ sp,bp                                      ; 135B
        pop bp                                          ; 135D
        ret 0xa                                         ; 135E
        db 0x90                                         ; 1361

L5_1362:
        push bp                                         ; 1362
        mov_ bp,sp                                      ; 1363
        sub sp,byte +0x30                               ; 1365
        push di                                         ; 1368
        push si                                         ; 1369
        mov bx,[bp+0xe]                                 ; 136A
        mov ax,[bx+0x26]                                ; 136D
        mov dx,[bx+0x28]                                ; 1370
        mov_ si,ax                                      ; 1373
        mov [bp-0xe],dx                                 ; 1375
        cmp word [bx+0x1c],byte +0x0                    ; 1378
        jnz short L5_1384                               ; 137C
        mov ax,0x3                                      ; 137E
        jmp near L5_17C0                                ; 1381

L5_1384:
        mov [bp-0x10],si                                ; 1384
        mov_ di,si                                      ; 1387
        push word [bp-0xe]                              ; 1389
        push si                                         ; 138C
        les bx,[bp+0xa]                                 ; 138D
        mov ax,[es:bx+0x4]                              ; 1390
        mov dx,[es:bx+0x6]                              ; 1394
        mov [bp-0xa],ax                                 ; 1398
        mov [bp-0x8],dx                                 ; 139B
        push dx                                         ; 139E
        push ax                                         ; 139F
        call L5_08B0                                    ; 13A0
        mov_ si,ax                                      ; 13A3
        or_ si,ax                                       ; 13A5
        jz short L5_13AC                                ; 13A7
        jmp near L5_17C0                                ; 13A9

L5_13AC:
        mov_ si,di                                      ; 13AC
        mov ax,0x94                                     ; 13AE
        mul word [bp-0xa]                               ; 13B1
        mov_ bx,ax                                      ; 13B4
        mov ax,[bp-0xe]                                 ; 13B6
        mov es,ax                                       ; 13B9
        add_ bx,di                                      ; 13BB
        mov [bp-0x1a],bx                                ; 13BD
        mov [bp-0x18],es                                ; 13C0
        les bx,[bp+0xa]                                 ; 13C3
        mov ax,[es:bx+0xc]                              ; 13C6
        mov dx,[es:bx+0xe]                              ; 13CA
        les bx,[bp-0x1a]                                ; 13CE
        cmp [es:bx+0x926],ax                            ; 13D1
        jnz short L5_13DF                               ; 13D6
        cmp [es:bx+0x928],dx                            ; 13D8
        jz short L5_13E5                                ; 13DD

L5_13DF:
        mov ax,0xb                                      ; 13DF
        jmp near L5_17C0                                ; 13E2

L5_13E5:
        mov ax,0x6                                      ; 13E5
        mul word [bp-0xa]                               ; 13E8
        mov_ di,ax                                      ; 13EB
        mov_ bx,si                                      ; 13ED
        mov es,[bp-0xe]                                 ; 13EF
        mov ax,[es:bx+di+0x256e]                        ; 13F2
        mov [bp-0xc],ax                                 ; 13F7
        les bx,[bp-0x1a]                                ; 13FA
        mov ax,[es:bx+0x91e]                            ; 13FD
        mov dx,[es:bx+0x920]                            ; 1402
        mov [bp-0x4],ax                                 ; 1407
        mov [bp-0x2],dx                                 ; 140A
        mov al,[bp+0x6]                                 ; 140D
        and ax,strict word 0xf                          ; 1410
        sub_ dx,dx                                      ; 1413
        or_ ax,ax                                       ; 1415
        jz short L5_1425                                ; 1417
        dec ax                                          ; 1419
        jnz short L5_141F                               ; 141A
        jmp near L5_16D6                                ; 141C

L5_141F:
        mov ax,0x8                                      ; 141F
        jmp near L5_17C0                                ; 1422

L5_1425:
        mov ax,[bp-0x4]                                 ; 1425
        mov dx,[bp-0x2]                                 ; 1428
        cmp ax,strict word 0x1                          ; 142B
        jnz short L5_1442                               ; 142E
        sub dx,0x1002                                   ; 1430
        jnz short L5_1439                               ; 1434
        jmp near L5_14BD                                ; 1436

L5_1439:
        sub dx,0x60ff                                   ; 1439
        jnz short L5_1442                               ; 143D
        jmp near L5_14DF                                ; 143F

L5_1442:
        mov bx,[bp-0xc]                                 ; 1442
        mov si,[bp+0xa]                                 ; 1445
        sub_ ax,ax                                      ; 1448
        mov es,[bp+0xc]                                 ; 144A
        cmp [es:si+0x8],bx                              ; 144D
        jz short L5_1456                                ; 1451
        jmp near L5_1588                                ; 1453

L5_1456:
        cmp [es:si+0xa],ax                              ; 1456
        jz short L5_145F                                ; 145A
        jmp near L5_1588                                ; 145C

L5_145F:
        mov ax,[es:si+0x14]                             ; 145F
        mov dx,[es:si+0x16]                             ; 1463
        mov [bp-0x16],ax                                ; 1467
        mov [bp-0x14],dx                                ; 146A
        mov word [bp-0x12],0x0                          ; 146D
        or_ bx,bx                                       ; 1472
        jz short L5_14B7                                ; 1474
        mov cl,0x3                                      ; 1476
        mov ax,[bp-0xa]                                 ; 1478
        shl ax,cl                                       ; 147B
        add ax,[bp-0x10]                                ; 147D
        mov dx,[bp-0xe]                                 ; 1480
        add ax,0x280c                                   ; 1483
        mov [bp-0x4],ax                                 ; 1486
        mov [bp-0x2],dx                                 ; 1489
        mov [bp-0x6],bx                                 ; 148C
        mov_ bx,ax                                      ; 148F
        mov cx,[bp-0x6]                                 ; 1491

L5_1494:
        mov es,[bp-0x2]                                 ; 1494
        mov ax,[es:bx]                                  ; 1497
        mov dx,[es:bx+0x2]                              ; 149A
        les di,[bp-0x16]                                ; 149E
        mov [es:di],ax                                  ; 14A1
        mov [es:di+0x2],dx                              ; 14A4
        mov es,[bp+0xc]                                 ; 14A8
        mov ax,[es:si+0x10]                             ; 14AB
        add [bp-0x16],ax                                ; 14AF
        add bx,byte +0x4                                ; 14B2
        loop L5_1494                                    ; 14B5

L5_14B7:
        mov di,[bp+0xa]                                 ; 14B7
        jmp near L5_16BA                                ; 14BA

L5_14BD:
        push word [bp+0xe]                              ; 14BD
        push word [bp-0x8]                              ; 14C0
        push word [bp-0xa]                              ; 14C3
        mov cl,0x3                                      ; 14C6
        mov ax,[bp-0xa]                                 ; 14C8
        shl ax,cl                                       ; 14CB
        mov dx,[bp-0xe]                                 ; 14CD
        add_ ax,si                                      ; 14D0
        add ax,0x280c                                   ; 14D2
        push dx                                         ; 14D5
        push ax                                         ; 14D6
        callf L6_25E4, R5_14DA, R5_03B8                 ; 14D7 far seg6
        jmp near L5_1442                                ; 14DC

L5_14DF:
        mov ax,[bp-0xc]                                 ; 14DF
        sub_ dx,dx                                      ; 14E2
        les bx,[bp+0xa]                                 ; 14E4
        cmp [es:bx+0x8],ax                              ; 14E7
        jnz short L5_14F3                               ; 14EB
        cmp [es:bx+0xa],dx                              ; 14ED
        jz short L5_14F6                                ; 14F1

L5_14F3:
        jmp near L5_13DF                                ; 14F3

L5_14F6:
        mov_ si,bx                                      ; 14F6
        mov ax,[es:si+0x14]                             ; 14F8
        mov dx,[es:si+0x16]                             ; 14FC
        mov [bp-0x4],ax                                 ; 1500
        mov [bp-0x2],dx                                 ; 1503
        xor_ bx,bx                                      ; 1506
        cmp [es:si+0xe],bx                              ; 1508
        jnz short L5_1514                               ; 150C
        cmp [es:si+0xc],bx                              ; 150E
        jz short L5_156D                                ; 1512

L5_1514:
        mov cl,0x3                                      ; 1514
        mov ax,[bp-0xa]                                 ; 1516
        shl ax,cl                                       ; 1519
        add ax,[bp-0x10]                                ; 151B
        mov dx,[bp-0xe]                                 ; 151E
        add ax,0x280c                                   ; 1521
        mov [bp-0x1e],ax                                ; 1524
        mov [bp-0x1c],dx                                ; 1527

L5_152A:
        mov ax,0x1                                      ; 152A
        mov_ cx,bx                                      ; 152D
        shl ax,cl                                       ; 152F
        cwd                                             ; 1531
        les di,[bp-0x1e]                                ; 1532
        test [es:di+0x2],dx                             ; 1535
        ja short L5_1540                                ; 1539
        test [es:di],ax                                 ; 153B
        jna short L5_1545                               ; 153E

L5_1540:
        mov ax,0x1                                      ; 1540
        jmp short L5_1547                               ; 1543

L5_1545:
        xor_ ax,ax                                      ; 1545

L5_1547:
        cwd                                             ; 1547
        les di,[bp-0x4]                                 ; 1548
        mov [es:di],ax                                  ; 154B
        mov [es:di+0x2],dx                              ; 154E
        mov es,[bp+0xc]                                 ; 1552
        mov ax,[es:si+0x10]                             ; 1555
        add [bp-0x4],ax                                 ; 1559
        inc bx                                          ; 155C
        sub_ ax,ax                                      ; 155D
        cmp [es:si+0xe],ax                              ; 155F
        ja short L5_152A                                ; 1563
        jc short L5_156D                                ; 1565
        cmp [es:si+0xc],bx                              ; 1567
        ja short L5_152A                                ; 156B

L5_156D:
        mov word [es:si+0x10],0x4                       ; 156D
        mov word [es:si+0x12],0x0                       ; 1573
        mov word [es:si+0x8],0x1                        ; 1579
        mov word [es:si+0xa],0x0                        ; 157F
        jmp near L5_17BE                                ; 1585

L5_1588:
        mov_ di,si                                      ; 1588
        cmp word [es:di+0x8],byte +0x1                  ; 158A
        jnz short L5_1597                               ; 158F
        cmp [es:di+0xa],ax                              ; 1591
        jz short L5_159A                                ; 1595

L5_1597:
        jmp near L5_13DF                                ; 1597

L5_159A:
        mov ax,[bp-0x2]                                 ; 159A
        and ax,0xf000                                   ; 159D
        mov_ cx,ax                                      ; 15A0
        mov_ dx,ax                                      ; 15A2
        sub_ ax,ax                                      ; 15A4
        sub dx,0x1000                                   ; 15A6
        test dx,0xfff                                   ; 15AA
        jz short L5_15B3                                ; 15AE
        jmp near L5_16BA                                ; 15B0

L5_15B3:
        mov cl,0xb                                      ; 15B3
        shr dx,cl                                       ; 15B5
        cmp dx,byte +0xa                                ; 15B7
        jna short L5_15BF                               ; 15BA
        jmp near L5_16BA                                ; 15BC

L5_15BF:
        mov_ bx,dx                                      ; 15BF
        jmp [cs:bx+JT_mixer_class]                      ; 15C1

; ((control type & F000h) - 1000h) >> 11: the control class
JT_mixer_class:
        dw L5_15D2                                      ; 15C6
        dw L5_1640                                      ; 15C8
        dw L5_15D2                                      ; 15CA
        dw L5_1677                                      ; 15CC
        dw L5_1677                                      ; 15CE
        dw L5_15D2                                      ; 15D0

L5_15D2:
        mov ax,[es:di+0x14]                             ; 15D2
        mov dx,[es:di+0x16]                             ; 15D6
        mov [bp-0x16],ax                                ; 15DA
        mov [bp-0x14],dx                                ; 15DD
        mov si,[bp-0x10]                                ; 15E0
        mov cl,0x3                                      ; 15E3
        mov bx,[bp-0xa]                                 ; 15E5
        shl bx,cl                                       ; 15E8
        mov ax,[bp-0xe]                                 ; 15EA
        add_ bx,si                                      ; 15ED
        mov es,ax                                       ; 15EF
        mov ax,[es:bx+0x2810]                           ; 15F1
        mov dx,[es:bx+0x2812]                           ; 15F6
        mov [bp-0x2a],ax                                ; 15FB
        mov [bp-0x28],dx                                ; 15FE
        mov ax,[es:bx+0x280c]                           ; 1601
        mov dx,[es:bx+0x280e]                           ; 1606
        mov [bp-0x22],ax                                ; 160B
        mov [bp-0x20],dx                                ; 160E
        cwd                                             ; 1611
        xor_ ax,dx                                      ; 1612
        sub_ ax,dx                                      ; 1614
        mov_ cx,ax                                      ; 1616
        mov ax,[bp-0x2a]                                ; 1618
        mov dx,[bp-0x28]                                ; 161B
        mov [bp-0x26],ax                                ; 161E
        mov [bp-0x24],dx                                ; 1621
        cwd                                             ; 1624
        xor_ ax,dx                                      ; 1625
        sub_ ax,dx                                      ; 1627
        cmp_ cx,ax                                      ; 1629
        jng short L5_1635                               ; 162B
        mov ax,[bp-0x22]                                ; 162D
        mov dx,[bp-0x20]                                ; 1630
        jmp short L5_163B                               ; 1633

L5_1635:
        mov ax,[bp-0x26]                                ; 1635
        mov dx,[bp-0x24]                                ; 1638

L5_163B:
        les bx,[bp-0x16]                                ; 163B
        jmp short L5_16B3                               ; 163E

L5_1640:
        mov si,[bp-0x10]                                ; 1640
        mov cl,0x3                                      ; 1643
        mov bx,[bp-0xa]                                 ; 1645
        shl bx,cl                                       ; 1648
        mov ax,[bp-0xe]                                 ; 164A
        add_ bx,si                                      ; 164D
        mov es,ax                                       ; 164F
        mov ax,[es:bx+0x280c]                           ; 1651
        mov dx,[es:bx+0x280e]                           ; 1656
        add ax,[es:bx+0x2810]                           ; 165B
        adc dx,[es:bx+0x2812]                           ; 1660
        or_ dx,dx                                       ; 1665
        jnz short L5_166D                               ; 1667
        or_ ax,ax                                       ; 1669
        jz short L5_1672                                ; 166B

L5_166D:
        mov ax,0x1                                      ; 166D
        jmp short L5_1674                               ; 1670

L5_1672:
        xor_ ax,ax                                      ; 1672

L5_1674:
        cwd                                             ; 1674
        jmp short L5_16AC                               ; 1675

L5_1677:
        mov si,[bp-0x10]                                ; 1677
        mov cl,0x3                                      ; 167A
        mov bx,[bp-0xa]                                 ; 167C
        shl bx,cl                                       ; 167F
        mov ax,[bp-0xe]                                 ; 1681
        add_ bx,si                                      ; 1684
        mov es,ax                                       ; 1686
        mov ax,[es:bx+0x280c]                           ; 1688
        mov dx,[es:bx+0x280e]                           ; 168D
        cmp dx,[es:bx+0x2812]                           ; 1692
        ja short L5_16AC                                ; 1697
        jc short L5_16A2                                ; 1699
        cmp ax,[es:bx+0x2810]                           ; 169B
        jnc short L5_16AC                               ; 16A0

L5_16A2:
        mov dx,[es:bx+0x2812]                           ; 16A2
        mov ax,[es:bx+0x2810]                           ; 16A7

L5_16AC:
        mov es,[bp+0xc]                                 ; 16AC
        les bx,[es:di+0x14]                             ; 16AF

L5_16B3:
        mov [es:bx],ax                                  ; 16B3
        mov [es:bx+0x2],dx                              ; 16B6

L5_16BA:
        mov es,[bp+0xc]                                 ; 16BA
        mov word [es:di+0x10],0x4                       ; 16BD
        mov word [es:di+0x12],0x0                       ; 16C3
        sub_ ax,ax                                      ; 16C9
        mov [es:di+0xe],ax                              ; 16CB
        mov [es:di+0xc],ax                              ; 16CF
        jmp near L5_17BE                                ; 16D3

L5_16D6:
        mov ax,[bp-0x4]                                 ; 16D6
        mov dx,[bp-0x2]                                 ; 16D9
        sub ax,strict word 0x1                          ; 16DC
        sbb dx,0x7101                                   ; 16DF
        or_ ax,dx                                       ; 16E3
        jz short L5_16ED                                ; 16E5
        mov ax,0x401                                    ; 16E7
        jmp near L5_17C0                                ; 16EA

L5_16ED:
        mov di,[bp+0xa]                                 ; 16ED
        mov es,[bp+0xc]                                 ; 16F0
        mov ax,[es:di+0x14]                             ; 16F3
        mov dx,[es:di+0x16]                             ; 16F7
        mov [bp-0x14],ax                                ; 16FB
        mov [bp-0x12],dx                                ; 16FE
        xor_ si,si                                      ; 1701
        cmp [es:di+0xc],si                              ; 1703
        jg short L5_170C                                ; 1707
        jmp near L5_179B                                ; 1709

L5_170C:
        mov ax,0xa                                      ; 170C
        mul word [bp-0xa]                               ; 170F
        mov [bp-0x2c],ax                                ; 1712
        mov [bp-0x2],si                                 ; 1715
        mov si,[bp-0x10]                                ; 1718
        mov di,[bp-0x2]                                 ; 171B

L5_171E:
        mov bx,[bp-0x2c]                                ; 171E
        add_ bx,di                                      ; 1721
        add_ bx,bx                                      ; 1723
        mov ax,[bp-0xe]                                 ; 1725
        add_ bx,si                                      ; 1728
        mov es,ax                                       ; 172A
        mov cx,[es:bx+0x26e0]                           ; 172C
        mov dx,[bp-0xa]                                 ; 1731
        les bx,[bp-0x14]                                ; 1734
        mov [es:bx],dx                                  ; 1737
        mov [es:bx+0x2],cx                              ; 173A
        mov ax,0xa6                                     ; 173E
        mov_ bx,cx                                      ; 1741
        add bx,[bp-0x2c]                                ; 1743
        add_ bx,bx                                      ; 1746
        mov cx,[bp-0xe]                                 ; 1748
        add_ bx,si                                      ; 174B
        mov es,cx                                       ; 174D
        mul word [es:bx+0x2690]                         ; 174F
        add_ ax,si                                      ; 1754
        mov_ bx,ax                                      ; 1756
        mov_ dx,cx                                      ; 1758
        mov cx,[es:bx+0x1a]                             ; 175A
        mov ax,[es:bx+0x1c]                             ; 175E
        mov [bp-0x30],bx                                ; 1762
        les bx,[bp-0x14]                                ; 1765
        mov [es:bx+0x4],cx                              ; 1768
        mov [es:bx+0x6],ax                              ; 176C
        lea ax,[bx+0x8]                                 ; 1770
        push es                                         ; 1773
        push ax                                         ; 1774
        mov ax,[bp-0x30]                                ; 1775
        add ax,strict word 0x3a                         ; 1778
        push dx                                         ; 177B
        push ax                                         ; 177C
        callp R5_177E, 0xFFFF, 0x0000                   ; 177D KERNEL.lstrcpy
        les bx,[bp+0xa]                                 ; 1782
        mov ax,[es:bx+0x10]                             ; 1785
        add [bp-0x14],ax                                ; 1789
        inc di                                          ; 178C
        cmp [es:bx+0xc],di                              ; 178D
        jg short L5_171E                                ; 1791
        mov [bp-0x2],di                                 ; 1793
        mov_ di,bx                                      ; 1796
        mov si,[bp-0x2]                                 ; 1798

L5_179B:
        mov word [es:di+0x10],0x48                      ; 179B
        mov word [es:di+0x12],0x0                       ; 17A1
        mov word [es:di+0x8],0x1                        ; 17A7
        mov word [es:di+0xa],0x0                        ; 17AD
        mov_ ax,si                                      ; 17B3
        cwd                                             ; 17B5
        mov [es:di+0xc],si                              ; 17B6
        mov [es:di+0xe],dx                              ; 17BA

L5_17BE:
        xor_ ax,ax                                      ; 17BE

L5_17C0:
        pop si                                          ; 17C0
        pop di                                          ; 17C1
        mov_ sp,bp                                      ; 17C2
        pop bp                                          ; 17C4
        retf 0xa                                        ; 17C5
        db 0x90, 0x90                                   ; 17C8

mxd_set_control_details:
        push bp                                         ; 17CA
        mov_ bp,sp                                      ; 17CB
        sub sp,byte +0x2c                               ; 17CD
        push di                                         ; 17D0
        push si                                         ; 17D1
        test byte [bp+0x6],0xf                          ; 17D2
        jz short L5_17DE                                ; 17D6
        mov ax,0x8                                      ; 17D8
        jmp near L5_1B9B                                ; 17DB

L5_17DE:
        mov bx,[bp+0xe]                                 ; 17DE
        mov ax,[bx+0x26]                                ; 17E1
        mov dx,[bx+0x28]                                ; 17E4
        mov_ di,ax                                      ; 17E7
        mov [bp-0x18],dx                                ; 17E9
        cmp word [bx+0x1c],byte +0x0                    ; 17EC
        jnz short L5_17F8                               ; 17F0
        mov ax,0x3                                      ; 17F2
        jmp near L5_1B9B                                ; 17F5

L5_17F8:
        mov [bp-0x1a],di                                ; 17F8
        mov_ si,di                                      ; 17FB
        push word [bp-0x18]                             ; 17FD
        push di                                         ; 1800
        les bx,[bp+0xa]                                 ; 1801
        mov ax,[es:bx+0x4]                              ; 1804
        mov dx,[es:bx+0x6]                              ; 1808
        mov [bp-0x12],ax                                ; 180C
        push dx                                         ; 180F
        push ax                                         ; 1810
        call L5_08B0                                    ; 1811
        mov_ di,ax                                      ; 1814
        or_ di,ax                                       ; 1816
        jz short L5_181F                                ; 1818

L5_181A:
        mov_ ax,di                                      ; 181A
        jmp near L5_1B9B                                ; 181C

L5_181F:
        mov_ di,si                                      ; 181F
        mov ax,0x94                                     ; 1821
        mul word [bp-0x12]                              ; 1824
        mov_ bx,ax                                      ; 1827
        mov ax,[bp-0x18]                                ; 1829
        mov es,ax                                       ; 182C
        add_ bx,si                                      ; 182E
        mov [bp-0x22],bx                                ; 1830
        mov [bp-0x20],es                                ; 1833
        les bx,[bp+0xa]                                 ; 1836
        mov ax,[es:bx+0xc]                              ; 1839
        mov dx,[es:bx+0xe]                              ; 183D
        les bx,[bp-0x22]                                ; 1841
        cmp [es:bx+0x926],ax                            ; 1844
        jnz short L5_1852                               ; 1849
        cmp [es:bx+0x928],dx                            ; 184B
        jz short L5_1858                                ; 1850

L5_1852:
        mov ax,0xb                                      ; 1852
        jmp near L5_1B9B                                ; 1855

L5_1858:
        mov ax,0x6                                      ; 1858
        mul word [bp-0x12]                              ; 185B
        mov_ si,ax                                      ; 185E
        mov_ bx,di                                      ; 1860
        mov es,[bp-0x18]                                ; 1862
        mov ax,[es:bx+si+0x256e]                        ; 1865
        mov [bp-0xa],ax                                 ; 186A
        les bx,[bp-0x22]                                ; 186D
        mov ax,[es:bx+0x91e]                            ; 1870
        mov dx,[es:bx+0x920]                            ; 1875
        sub ax,strict word 0x1                          ; 187A
        sbb dx,0x7101                                   ; 187D
        or_ ax,dx                                       ; 1881
        jnz short L5_1888                               ; 1883
        jmp near L5_192D                                ; 1885

L5_1888:
        push word [bp-0x18]                             ; 1888
        push di                                         ; 188B
        push word [bp+0xc]                              ; 188C
        push word [bp+0xa]                              ; 188F
        call L5_08EC                                    ; 1892
        mov_ si,ax                                      ; 1895
        or_ si,ax                                       ; 1897
        jz short L5_189E                                ; 1899
        jmp near L5_1B9B                                ; 189B

L5_189E:
        les bx,[bp+0xa]                                 ; 189E
        mov ax,[es:bx+0x14]                             ; 18A1
        mov dx,[es:bx+0x16]                             ; 18A5
        mov [bp-0xe],ax                                 ; 18A9
        mov [bp-0xc],dx                                 ; 18AC
        mov word [bp-0x8],0x0                           ; 18AF
        mov bx,[bp-0xa]                                 ; 18B4
        or_ bx,bx                                       ; 18B7
        jz short L5_190C                                ; 18B9
        mov cl,0x3                                      ; 18BB
        mov ax,[bp-0x12]                                ; 18BD
        shl ax,cl                                       ; 18C0
        add_ ax,di                                      ; 18C2
        add ax,0x280c                                   ; 18C4
        mov cx,[bp-0x18]                                ; 18C7
        mov [bp-0x4],ax                                 ; 18CA
        mov [bp-0x2],cx                                 ; 18CD
        mov [bp-0x6],bx                                 ; 18D0
        mov_ bx,ax                                      ; 18D3
        mov di,[bp+0xa]                                 ; 18D5
        mov cx,[bp-0x6]                                 ; 18D8

L5_18DB:
        les si,[bp-0xe]                                 ; 18DB
        mov ax,[es:si]                                  ; 18DE
        mov dx,[es:si+0x2]                              ; 18E1
        mov es,[bp-0x2]                                 ; 18E5
        mov [es:bx],ax                                  ; 18E8
        mov [es:bx+0x2],dx                              ; 18EB
        mov es,[bp+0xc]                                 ; 18EF
        cmp word [es:di+0x8],byte +0x1                  ; 18F2
        jnz short L5_1900                               ; 18F7
        cmp word [es:di+0xa],byte +0x0                  ; 18F9
        jz short L5_1907                                ; 18FE

L5_1900:
        mov ax,[es:di+0x10]                             ; 1900
        add [bp-0xe],ax                                 ; 1904

L5_1907:
        add bx,byte +0x4                                ; 1907
        loop L5_18DB                                    ; 190A

L5_190C:
        push word [bp+0xe]                              ; 190C
        push word [bp+0xc]                              ; 190F
        push word [bp+0xa]                              ; 1912
        mov bx,[bp-0x12]                                ; 1915
        add_ bx,bx                                      ; 1918
        add_ bx,bx                                      ; 191A
        call far [cs:bx+0x2ba1]                         ; 191C
        mov_ di,ax                                      ; 1921
        or_ di,ax                                       ; 1923
        jnz short L5_192A                               ; 1925
        jmp near L5_1B5B                                ; 1927

L5_192A:
        jmp near L5_181A                                ; 192A

L5_192D:
        mov ax,[bp-0xa]                                 ; 192D
        sub_ dx,dx                                      ; 1930
        les bx,[bp+0xa]                                 ; 1932
        cmp [es:bx+0x8],ax                              ; 1935
        jz short L5_193E                                ; 1939
        jmp near L5_1852                                ; 193B

L5_193E:
        cmp [es:bx+0xa],dx                              ; 193E
        jz short L5_1947                                ; 1942
        jmp near L5_1852                                ; 1944

L5_1947:
        mov ax,[bp-0x12]                                ; 1947
        mov [bp-0x14],ax                                ; 194A
        mov [bp-0x16],dx                                ; 194D
        or_ ax,ax                                       ; 1950
        jz short L5_197B                                ; 1952
        mov bx,[bp+0xe]                                 ; 1954
        mov al,[bx+0x5d]                                ; 1957
        and al,0x3                                      ; 195A
        cmp al,0x2                                      ; 195C
        jnz short L5_197B                               ; 195E
        mov ax,[bx+0x78]                                ; 1960
        mov dx,[bx+0x7a]                                ; 1963
        cmp [bx+0x65],ax                                ; 1966
        jnz short L5_1970                               ; 1969
        cmp [bx+0x67],dx                                ; 196B
        jz short L5_1975                                ; 196E

L5_1970:
        mov ax,0x1                                      ; 1970
        jmp short L5_1978                               ; 1973

L5_1975:
        mov ax,0x2                                      ; 1975

L5_1978:
        mov [bp-0x16],ax                                ; 1978

L5_197B:
        mov di,[bp-0x1a]                                ; 197B
        mov cl,0x3                                      ; 197E
        mov bx,[bp-0x12]                                ; 1980
        shl bx,cl                                       ; 1983
        mov ax,[bp-0x18]                                ; 1985
        mov es,ax                                       ; 1988
        add_ bx,di                                      ; 198A
        add bx,0x280c                                   ; 198C
        mov [bp-0x26],bx                                ; 1990
        mov [bp-0x24],es                                ; 1993
        mov ax,[es:bx]                                  ; 1996
        mov dx,[es:bx+0x2]                              ; 1999
        mov [bp-0xe],ax                                 ; 199D
        mov [bp-0xc],dx                                 ; 19A0
        sub_ ax,ax                                      ; 19A3
        mov [bp-0x8],ax                                 ; 19A5
        mov [bp-0xa],ax                                 ; 19A8
        les bx,[bp+0xa]                                 ; 19AB
        mov ax,[es:bx+0x14]                             ; 19AE
        mov dx,[es:bx+0x16]                             ; 19B2
        mov [bp-0x1e],ax                                ; 19B6
        mov [bp-0x1c],dx                                ; 19B9
        mov word [bp-0x6],0x0                           ; 19BC
        les bx,[bp-0x22]                                ; 19C1
        cmp word [es:bx+0x928],byte +0x0                ; 19C4
        jnz short L5_19D4                               ; 19CA
        cmp word [es:bx+0x926],byte +0x0                ; 19CC
        jz short L5_1A17                                ; 19D2

L5_19D4:
        mov bx,[bp-0x6]                                 ; 19D4
        mov si,[bp+0xa]                                 ; 19D7

L5_19DA:
        les di,[bp-0x1e]                                ; 19DA
        mov ax,[es:di+0x2]                              ; 19DD
        or ax,[es:di]                                   ; 19E1
        jz short L5_19F4                                ; 19E4
        mov ax,0x1                                      ; 19E6
        mov_ cx,bx                                      ; 19E9
        shl ax,cl                                       ; 19EB
        cwd                                             ; 19ED
        or [bp-0xa],ax                                  ; 19EE
        or [bp-0x8],dx                                  ; 19F1

L5_19F4:
        mov es,[bp+0xc]                                 ; 19F4
        mov ax,[es:si+0x10]                             ; 19F7
        add [bp-0x1e],ax                                ; 19FB
        inc bx                                          ; 19FE
        sub_ ax,ax                                      ; 19FF
        les di,[bp-0x22]                                ; 1A01
        cmp [es:di+0x928],ax                            ; 1A04
        ja short L5_19DA                                ; 1A09
        jc short L5_1A14                                ; 1A0B
        cmp [es:di+0x926],bx                            ; 1A0D
        ja short L5_19DA                                ; 1A12

L5_1A14:
        mov di,[bp-0x1a]                                ; 1A14

L5_1A17:
        mov ax,[bp-0xa]                                 ; 1A17
        mov dx,[bp-0x8]                                 ; 1A1A
        les bx,[bp-0x26]                                ; 1A1D
        mov [es:bx],ax                                  ; 1A20
        mov [es:bx+0x2],dx                              ; 1A23
        mov word [bp-0x6],0x0                           ; 1A27
        les bx,[bp-0x22]                                ; 1A2C
        cmp word [es:bx+0x928],byte +0x0                ; 1A2F
        jnz short L5_1A42                               ; 1A35
        cmp word [es:bx+0x926],byte +0x0                ; 1A37
        jnz short L5_1A42                               ; 1A3D
        jmp near L5_190C                                ; 1A3F

L5_1A42:
        mov ax,0x14                                     ; 1A42
        mul word [bp-0x12]                              ; 1A45
        add_ ax,di                                      ; 1A48
        add ax,0x26e0                                   ; 1A4A
        mov cx,[bp-0x18]                                ; 1A4D
        mov [bp-0x4],ax                                 ; 1A50
        mov [bp-0x2],cx                                 ; 1A53
        mov si,[bp-0x14]                                ; 1A56
        mov di,[bp-0x6]                                 ; 1A59

L5_1A5C:
        or_ si,si                                       ; 1A5C
        jnz short L5_1A8D                               ; 1A5E
        les bx,[bp-0x4]                                 ; 1A60
        mov_ ax,si                                      ; 1A63
        mov cx,0xa                                      ; 1A65
        mul cx                                          ; 1A68
        mov cx,[es:bx]                                  ; 1A6A
        mov_ bx,ax                                      ; 1A6D
        add_ bx,cx                                      ; 1A6F
        add_ bx,bx                                      ; 1A71
        add bx,[bp-0x1a]                                ; 1A73
        mov es,[bp-0x18]                                ; 1A76
        mov ax,[es:bx+0x2690]                           ; 1A79
        mov [bp-0x28],ax                                ; 1A7E
        dec ax                                          ; 1A81
        jz short L5_1A8A                                ; 1A82
        cmp word [bp-0x28],byte +0x4                    ; 1A84
        jnz short L5_1A8D                               ; 1A88

L5_1A8A:
        jmp near L5_1B35                                ; 1A8A

L5_1A8D:
        mov ax,0x1                                      ; 1A8D
        mov_ cx,di                                      ; 1A90
        shl ax,cl                                       ; 1A92
        cwd                                             ; 1A94
        mov [bp-0x2c],ax                                ; 1A95
        mov [bp-0x2a],dx                                ; 1A98
        and ax,[bp-0xe]                                 ; 1A9B
        and dx,[bp-0xc]                                 ; 1A9E
        or_ dx,ax                                       ; 1AA1
        jz short L5_1AE2                                ; 1AA3
        mov ax,[bp-0xa]                                 ; 1AA5
        mov dx,[bp-0x8]                                 ; 1AA8
        and ax,[bp-0x2c]                                ; 1AAB
        and dx,[bp-0x2a]                                ; 1AAE
        or_ dx,ax                                       ; 1AB1
        jnz short L5_1AE2                               ; 1AB3
        push word [bp+0xe]                              ; 1AB5
        push si                                         ; 1AB8
        les bx,[bp-0x4]                                 ; 1AB9
        mov_ ax,si                                      ; 1ABC
        mov cx,0xa                                      ; 1ABE
        mul cx                                          ; 1AC1
        mov cx,[es:bx]                                  ; 1AC3
        mov_ bx,ax                                      ; 1AC6
        add_ bx,cx                                      ; 1AC8
        add_ bx,bx                                      ; 1ACA
        mov es,[bp-0x18]                                ; 1ACC
        add bx,[bp-0x1a]                                ; 1ACF
        push word [es:bx+0x2690]                        ; 1AD2
        xor_ ax,ax                                      ; 1AD7
        push ax                                         ; 1AD9
        mov ax,0x10                                     ; 1ADA
        push ax                                         ; 1ADD
        push cs                                         ; 1ADE
        call L5_0538                                    ; 1ADF

L5_1AE2:
        mov ax,[bp-0xe]                                 ; 1AE2
        mov dx,[bp-0xc]                                 ; 1AE5
        and ax,[bp-0x2c]                                ; 1AE8
        and dx,[bp-0x2a]                                ; 1AEB
        or_ dx,ax                                       ; 1AEE
        jnz short L5_1B35                               ; 1AF0
        mov ax,[bp-0xa]                                 ; 1AF2
        mov dx,[bp-0x8]                                 ; 1AF5
        and ax,[bp-0x2c]                                ; 1AF8
        and dx,[bp-0x2a]                                ; 1AFB
        or_ dx,ax                                       ; 1AFE
        jz short L5_1B35                                ; 1B00
        cmp [bp-0x16],si                                ; 1B02
        jnz short L5_1B35                               ; 1B05
        push word [bp+0xe]                              ; 1B07
        push si                                         ; 1B0A
        les bx,[bp-0x4]                                 ; 1B0B
        mov_ ax,si                                      ; 1B0E
        mov cx,0xa                                      ; 1B10
        mul cx                                          ; 1B13
        mov cx,[es:bx]                                  ; 1B15
        mov_ bx,ax                                      ; 1B18
        add_ bx,cx                                      ; 1B1A
        add_ bx,bx                                      ; 1B1C
        mov es,[bp-0x18]                                ; 1B1E
        add bx,[bp-0x1a]                                ; 1B21
        push word [es:bx+0x2690]                        ; 1B24
        mov ax,0x1                                      ; 1B29
        push ax                                         ; 1B2C
        mov ax,0x10                                     ; 1B2D
        push ax                                         ; 1B30
        push cs                                         ; 1B31
        call L5_0538                                    ; 1B32

L5_1B35:
        add word [bp-0x4],byte +0x2                     ; 1B35
        inc di                                          ; 1B39
        sub_ ax,ax                                      ; 1B3A
        les bx,[bp-0x22]                                ; 1B3C
        cmp [es:bx+0x928],ax                            ; 1B3F
        jna short L5_1B49                               ; 1B44
        jmp near L5_1A5C                                ; 1B46

L5_1B49:
        jnc short L5_1B4E                               ; 1B49
        jmp near L5_190C                                ; 1B4B

L5_1B4E:
        cmp [es:bx+0x926],di                            ; 1B4E
        jna short L5_1B58                               ; 1B53
        jmp near L5_1A5C                                ; 1B55

L5_1B58:
        jmp near L5_190C                                ; 1B58

L5_1B5B:
        les bx,[bp-0x1a]                                ; 1B5B
        mov si,[es:bx]                                  ; 1B5E
        or_ si,si                                       ; 1B61
        jz short L5_1B99                                ; 1B63
        mov di,[bp+0xa]                                 ; 1B65

L5_1B68:
        push word [si+0x8]                              ; 1B68
        push word [si+0x6]                              ; 1B6B
        push word [si+0xa]                              ; 1B6E
        push word [si+0x4]                              ; 1B71
        mov ax,0x3d1                                    ; 1B74
        push ax                                         ; 1B77
        push word [si+0xe]                              ; 1B78
        push word [si+0xc]                              ; 1B7B
        mov es,[bp+0xc]                                 ; 1B7E
        push word [es:di+0x6]                           ; 1B81
        push word [es:di+0x4]                           ; 1B85
        sub_ ax,ax                                      ; 1B89
        push ax                                         ; 1B8B
        push ax                                         ; 1B8C
        callp R5_1B8E, R5_0525, 0x0000                  ; 1B8D MMSYSTEM.DriverCallback
        mov si,[si+0x12]                                ; 1B92
        or_ si,si                                       ; 1B95
        jnz short L5_1B68                               ; 1B97

L5_1B99:
        xor_ ax,ax                                      ; 1B99

L5_1B9B:
        pop si                                          ; 1B9B
        pop di                                          ; 1B9C
        mov_ sp,bp                                      ; 1B9D
        pop bp                                          ; 1B9F
        retf 0xa                                        ; 1BA0
        db 0x90                                         ; 1BA3

L5_1BA4:
        push bp                                         ; 1BA4
        mov_ bp,sp                                      ; 1BA5
        push di                                         ; 1BA7
        push si                                         ; 1BA8
        push ds                                         ; 1BA9
        movsel ax, R5_1BAB, R5_1D78                     ; 1BAA seg7
        mov ds,ax                                       ; 1BAD
        mov di,[bp+0x12]                                ; 1BAF
        mov_ ax,di                                      ; 1BB2
        cmp ax,strict word 0x67                         ; 1BB4
        jnz short L5_1BBC                               ; 1BB7
        jmp near L5_1D58                                ; 1BB9

L5_1BBC:
        ja short L5_1BE8                                ; 1BBC
        dec al                                          ; 1BBE
        jnz short L5_1BC5                               ; 1BC0
        jmp near L5_1C69                                ; 1BC2

L5_1BC5:
        dec al                                          ; 1BC5
        jnz short L5_1BCC                               ; 1BC7
        jmp near L5_1C9E                                ; 1BC9

L5_1BCC:
        dec al                                          ; 1BCC
        jnz short L5_1BD3                               ; 1BCE
        jmp near L5_1CCE                                ; 1BD0

L5_1BD3:
        sub al,0x61                                     ; 1BD3
        jnz short L5_1BDA                               ; 1BD5
        jmp near L5_1D15                                ; 1BD7

L5_1BDA:
        dec al                                          ; 1BDA
        jnz short L5_1BE1                               ; 1BDC
        jmp near L5_1D3E                                ; 1BDE

L5_1BE1:
        dec al                                          ; 1BE1
        jnz short L5_1BE8                               ; 1BE3
        jmp near L5_1D4B                                ; 1BE5

L5_1BE8:
        cmp word [bp+0x14],byte +0x0                    ; 1BE8
        jz short L5_1BF4                                ; 1BEC

L5_1BEE:
        mov ax,0x2                                      ; 1BEE
        jmp near L5_1C93                                ; 1BF1

L5_1BF4:
        mov si,[bp+0xe]                                 ; 1BF4
        mov_ ax,di                                      ; 1BF7
        mov cx,[si+0x10]                                ; 1BF9
        sub ax,strict word 0x4                          ; 1BFC
        jz short L5_1C13                                ; 1BFF
        dec ax                                          ; 1C01
        jz short L5_1C1A                                ; 1C02
        dec ax                                          ; 1C04
        jz short L5_1C2D                                ; 1C05
        dec ax                                          ; 1C07
        jz short L5_1C40                                ; 1C08
        dec ax                                          ; 1C0A
        jz short L5_1C56                                ; 1C0B
        mov ax,0x8                                      ; 1C0D
        jmp near L5_1C93                                ; 1C10

L5_1C13:
        push si                                         ; 1C13
        call L5_0BC6                                    ; 1C14
        jmp near L5_1D63                                ; 1C17

L5_1C1A:
        push si                                         ; 1C1A
        push word [bp+0xc]                              ; 1C1B
        push word [bp+0xa]                              ; 1C1E
        push word [bp+0x8]                              ; 1C21
        push word [bp+0x6]                              ; 1C24
        call L5_0BE4                                    ; 1C27
        jmp near L5_1D63                                ; 1C2A

L5_1C2D:
        push si                                         ; 1C2D
        push word [bp+0xc]                              ; 1C2E
        push word [bp+0xa]                              ; 1C31
        push word [bp+0x8]                              ; 1C34
        push word [bp+0x6]                              ; 1C37
        call L5_1098                                    ; 1C3A
        jmp near L5_1D63                                ; 1C3D

L5_1C40:
        push cx                                         ; 1C40
        push word [bp+0xc]                              ; 1C41
        push word [bp+0xa]                              ; 1C44
        push word [bp+0x8]                              ; 1C47
        push word [bp+0x6]                              ; 1C4A
        push cs                                         ; 1C4D
        call L5_1362                                    ; 1C4E

L5_1C51:
        sub_ dx,dx                                      ; 1C51
        jmp near L5_1D63                                ; 1C53

L5_1C56:
        push cx                                         ; 1C56
        push word [bp+0xc]                              ; 1C57
        push word [bp+0xa]                              ; 1C5A
        push word [bp+0x8]                              ; 1C5D
        push word [bp+0x6]                              ; 1C60
        push cs                                         ; 1C63
        call mxd_set_control_details                    ; 1C64
        jmp short L5_1C51                               ; 1C67

L5_1C69:
        push word [bp+0xc]                              ; 1C69
        push word [bp+0xa]                              ; 1C6C
        callf L3_4EAE, R5_1C72, R5_1CB0                 ; 1C6F far seg3
        mov_ si,ax                                      ; 1C74
        or_ si,ax                                       ; 1C76
        jnz short L5_1C82                               ; 1C78
        xor_ ax,ax                                      ; 1C7A
        mov dx,0xb                                      ; 1C7C
        jmp near L5_1D63                                ; 1C7F

L5_1C82:
        mov ax,[bp+0xc]                                 ; 1C82
        or ax,[bp+0xa]                                  ; 1C85
        jz short L5_1C98                                ; 1C88
        cmp word [si+0x1c],byte +0x0                    ; 1C8A
        jz short L5_1C98                                ; 1C8E
        mov ax,0x1                                      ; 1C90

L5_1C93:
        xor_ dx,dx                                      ; 1C93
        jmp near L5_1D63                                ; 1C95

L5_1C98:
        xor_ ax,ax                                      ; 1C98
        cwd                                             ; 1C9A
        jmp near L5_1D63                                ; 1C9B

L5_1C9E:
        cmp word [bp+0x14],byte +0x0                    ; 1C9E
        jz short L5_1CA7                                ; 1CA2
        jmp near L5_1BEE                                ; 1CA4

L5_1CA7:
        push word [bp+0x8]                              ; 1CA7
        push word [bp+0x6]                              ; 1CAA
        callf L3_4EAE, R5_1CB0, R5_1CE7                 ; 1CAD far seg3
        mov_ si,ax                                      ; 1CB2
        or_ si,ax                                       ; 1CB4
        jnz short L5_1CBB                               ; 1CB6
        jmp near L5_1BEE                                ; 1CB8

L5_1CBB:
        cmp word [si+0x1c],byte +0x0                    ; 1CBB
        jz short L5_1CF8                                ; 1CBF
        push si                                         ; 1CC1
        push word [bp+0xc]                              ; 1CC2
        push word [bp+0xa]                              ; 1CC5
        push cs                                         ; 1CC8
        call L5_0AAE                                    ; 1CC9
        jmp short L5_1D2C                               ; 1CCC

L5_1CCE:
        cmp word [bp+0x14],byte +0x0                    ; 1CCE
        jnz short L5_1CEF                               ; 1CD2
        mov cx,[bp+0xc]                                 ; 1CD4
        mov bx,[bp+0xa]                                 ; 1CD7
        mov es,cx                                       ; 1CDA
        push word [es:bx+0x10]                          ; 1CDC
        push word [es:bx+0xe]                           ; 1CE0
        callf L3_4EAE, R5_1CE7, R5_1D1E                 ; 1CE4 far seg3
        mov_ si,ax                                      ; 1CE9
        or_ si,ax                                       ; 1CEB
        jnz short L5_1CF2                               ; 1CED

L5_1CEF:
        jmp near L5_1BEE                                ; 1CEF

L5_1CF2:
        cmp word [si+0x1c],byte +0x0                    ; 1CF2
        jnz short L5_1CFD                               ; 1CF6

L5_1CF8:
        mov ax,0x3                                      ; 1CF8
        jmp short L5_1C93                               ; 1CFB

L5_1CFD:
        push si                                         ; 1CFD
        push word [bp+0x10]                             ; 1CFE
        push word [bp+0xe]                              ; 1D01
        push word [bp+0xc]                              ; 1D04
        push word [bp+0xa]                              ; 1D07
        push word [bp+0x8]                              ; 1D0A
        push word [bp+0x6]                              ; 1D0D
        call L5_0B42                                    ; 1D10
        jmp short L5_1D63                               ; 1D13

L5_1D15:
        push word [bp+0x8]                              ; 1D15
        push word [bp+0x6]                              ; 1D18
        callf L3_43E2, R5_1D1E, R5_1D2A                 ; 1D1B far seg3
        cmp word [0xc8],byte +0x0                       ; 1D20
        jz short L5_1D31                                ; 1D25
        callf vxd_check, R5_1D2A, R5_1D3A               ; 1D27 far seg3

L5_1D2C:
        xor_ ax,ax                                      ; 1D2C
        jmp near L5_1C93                                ; 1D2E

L5_1D31:
        push word [bp+0x8]                              ; 1D31
        push word [bp+0x6]                              ; 1D34
        callf L3_47C4, R5_1D3A, R5_1D47                 ; 1D37 far seg3
        jmp short L5_1D63                               ; 1D3C

L5_1D3E:
        push word [bp+0x8]                              ; 1D3E
        push word [bp+0x6]                              ; 1D41
        callf L3_4E68, R5_1D47, R5_1D54                 ; 1D44 far seg3
        jmp short L5_1D63                               ; 1D49

L5_1D4B:
        push word [bp+0x8]                              ; 1D4B
        push word [bp+0x6]                              ; 1D4E
        callf L3_4D2C, R5_1D54, R5_1D61                 ; 1D51 far seg3
        jmp short L5_1D63                               ; 1D56

L5_1D58:
        push word [bp+0x8]                              ; 1D58
        push word [bp+0x6]                              ; 1D5B
        callf L3_4940, R5_1D61, R5_1D85                 ; 1D5E far seg3

L5_1D63:
        pop ds                                          ; 1D63
        pop si                                          ; 1D64
        pop di                                          ; 1D65
        mov_ sp,bp                                      ; 1D66
        pop bp                                          ; 1D68
        retf 0x10                                       ; 1D69
        db 0x90, 0x90                                   ; 1D6C

; "ESSFMMXD" pipe callback
fm_mixer_callback:
        push bp                                         ; 1D6E
        mov_ bp,sp                                      ; 1D6F
        sub sp,byte +0x44                               ; 1D71
        push di                                         ; 1D74
        push si                                         ; 1D75
        push ds                                         ; 1D76
        movsel ax, R5_1D78, 0xFFFF                      ; 1D77 seg7
        mov ds,ax                                       ; 1D7A
        push word [bp+0xc]                              ; 1D7C
        push word [bp+0xa]                              ; 1D7F
        callf L3_4EAE, R5_1D85, R5_000F                 ; 1D82 far seg3
        mov [bp-0x2],ax                                 ; 1D87
        or_ ax,ax                                       ; 1D8A
        jnz short L5_1D94                               ; 1D8C
        mov ax,0xfffe                                   ; 1D8E
        jmp near L5_20D4                                ; 1D91

L5_1D94:
        mov ax,[bp+0xe]                                 ; 1D94
        mov dx,[bp+0x10]                                ; 1D97
        dec dx                                          ; 1D9A
        jnz short L5_1DA4                               ; 1D9B
        or_ ax,ax                                       ; 1D9D
        jz short L5_1DD4                                ; 1D9F
        jmp near L5_20D2                                ; 1DA1

L5_1DA4:
        dec dx                                          ; 1DA4
        jnz short L5_1DB1                               ; 1DA5
        dec ax                                          ; 1DA7
        dec ax                                          ; 1DA8
        jnz short L5_1DAE                               ; 1DA9
        jmp near L5_1E6E                                ; 1DAB

L5_1DAE:
        jmp near L5_20D2                                ; 1DAE

L5_1DB1:
        dec dx                                          ; 1DB1
        jnz short L5_1DC4                               ; 1DB2
        or_ ax,ax                                       ; 1DB4
        jnz short L5_1DBB                               ; 1DB6
        jmp near L5_1EDF                                ; 1DB8

L5_1DBB:
        dec ax                                          ; 1DBB
        jnz short L5_1DC1                               ; 1DBC
        jmp near L5_1F3C                                ; 1DBE

L5_1DC1:
        jmp near L5_20D2                                ; 1DC1

L5_1DC4:
        sub ax,strict word 0x0                          ; 1DC4
        sbb dx,byte +0x1                                ; 1DC7
        or_ ax,dx                                       ; 1DCA
        jnz short L5_1DD1                               ; 1DCC
        jmp near L5_20C5                                ; 1DCE

L5_1DD1:
        jmp near L5_20D2                                ; 1DD1

L5_1DD4:
        mov bx,[bp-0x2]                                 ; 1DD4
        mov ax,[bx+0x26]                                ; 1DD7
        mov dx,[bx+0x28]                                ; 1DDA
        mov [bp-0x6],ax                                 ; 1DDD
        mov [bp-0x4],dx                                 ; 1DE0
        mov ax,[bp+0x6]                                 ; 1DE3
        mov dx,[bp+0x8]                                 ; 1DE6
        mov [bx+0x113],ax                               ; 1DE9
        mov [bx+0x115],dx                               ; 1DED
        push word [bp+0x14]                             ; 1DF1
        push word [bp+0x12]                             ; 1DF4
        mov cx,0x1                                      ; 1DF7
        mov si,0x2                                      ; 1DFA
        push si                                         ; 1DFD
        push cx                                         ; 1DFE
        push word [bp+0xc]                              ; 1DFF
        push word [bp+0xa]                              ; 1E02
        sub_ cx,cx                                      ; 1E05
        push cx                                         ; 1E07
        push cx                                         ; 1E08
        mov [bp-0x42],dx                                ; 1E09
        mov [bp-0x44],ax                                ; 1E0C
        call far [bp-0x44]                              ; 1E0F
        mov word [bp-0xe],0x32                          ; 1E12
        mov word [bp-0xc],0x0                           ; 1E17
        lea ax,[bp-0x40]                                ; 1E1C
        mov [bp-0xa],ax                                 ; 1E1F
        mov [bp-0x8],ss                                 ; 1E22
        push word [bp+0x14]                             ; 1E25
        push word [bp+0x12]                             ; 1E28
        xor_ cx,cx                                      ; 1E2B
        push si                                         ; 1E2D
        push cx                                         ; 1E2E
        push word [bp+0xc]                              ; 1E2F
        push word [bp+0xa]                              ; 1E32
        lea cx,[bp-0xe]                                 ; 1E35
        push ss                                         ; 1E38
        push cx                                         ; 1E39
        mov bx,[bp-0x2]                                 ; 1E3A
        call far [bx+0x113]                             ; 1E3D
        les bx,[bp-0x6]                                 ; 1E41
        push ds                                         ; 1E44
        lea di,[bx+0x31a]                               ; 1E45
        lea si,[bp-0x40]                                ; 1E49
        push ss                                         ; 1E4C
        pop ds                                          ; 1E4D
        mov cx,0x13                                     ; 1E4E
        rep movsw                                       ; 1E51
        pop ds                                          ; 1E53
        les bx,[bp-0x6]                                 ; 1E54
        mov word [es:bx+0x312],0x3                      ; 1E57
        mov word [es:bx+0x314],0x0                      ; 1E5E
        and byte [es:bx+0x277d],0x7f                    ; 1E65
        jmp near L5_20D2                                ; 1E6B

L5_1E6E:
        mov bx,[bp-0x2]                                 ; 1E6E
        mov ax,[bx+0x26]                                ; 1E71
        mov dx,[bx+0x28]                                ; 1E74
        mov_ si,ax                                      ; 1E77
        mov [bp-0x4],dx                                 ; 1E79
        mov ax,[bp+0x6]                                 ; 1E7C
        mov [bp-0xe],ax                                 ; 1E7F
        mov word [bp-0xc],0x0                           ; 1E82
        mov ax,[bp+0x8]                                 ; 1E87
        mov [bp-0xa],ax                                 ; 1E8A
        mov word [bp-0x8],0x0                           ; 1E8D
        mov word [bp-0x22],0x7                          ; 1E92
        mov word [bp-0x20],0x0                          ; 1E97
        mov word [bp-0x26],0x18                         ; 1E9C
        mov word [bp-0x24],0x0                          ; 1EA1
        mov es,dx                                       ; 1EA6
        mov ax,[es:si+0x2598]                           ; 1EA8
        mov [bp-0x1e],ax                                ; 1EAD
        mov word [bp-0x1c],0x0                          ; 1EB0
        sub_ ax,ax                                      ; 1EB5
        mov [bp-0x18],ax                                ; 1EB7
        mov [bp-0x1a],ax                                ; 1EBA
        mov word [bp-0x16],0x4                          ; 1EBD
        mov [bp-0x14],ax                                ; 1EC2
        lea ax,[bp-0xe]                                 ; 1EC5
        mov [bp-0x12],ax                                ; 1EC8
        mov [bp-0x10],ss                                ; 1ECB
        push bx                                         ; 1ECE

L5_1ECF:
        lea ax,[bp-0x26]                                ; 1ECF
        push ss                                         ; 1ED2
        push ax                                         ; 1ED3
        sub_ ax,ax                                      ; 1ED4
        push ax                                         ; 1ED6
        push ax                                         ; 1ED7
        push cs                                         ; 1ED8
        call mxd_set_control_details                    ; 1ED9
        jmp near L5_20D2                                ; 1EDC

L5_1EDF:
        mov ax,[bp+0x6]                                 ; 1EDF
        sub_ dx,dx                                      ; 1EE2
        mov si,[bp-0x2]                                 ; 1EE4
        les si,[si+0x26]                                ; 1EE7
        mov [bp-0x4],es                                 ; 1EEA
        mov [es:si+0x2844],ax                           ; 1EED
        mov [es:si+0x2846],dx                           ; 1EF2
        mov [es:si+0x2848],ax                           ; 1EF7
        mov [es:si+0x284a],dx                           ; 1EFC
        mov_ bx,si                                      ; 1F01
        mov si,[es:bx]                                  ; 1F03
        or_ si,si                                       ; 1F06
        jnz short L5_1F0D                               ; 1F08
        jmp near L5_20D2                                ; 1F0A

L5_1F0D:
        push word [si+0x8]                              ; 1F0D
        push word [si+0x6]                              ; 1F10
        push word [si+0xa]                              ; 1F13
        push word [si+0x4]                              ; 1F16
        mov ax,0x3d1                                    ; 1F19
        push ax                                         ; 1F1C
        push word [si+0xe]                              ; 1F1D
        push word [si+0xc]                              ; 1F20
        mov ax,0x7                                      ; 1F23
        cwd                                             ; 1F26
        push dx                                         ; 1F27
        push ax                                         ; 1F28
        sub_ ax,ax                                      ; 1F29
        push ax                                         ; 1F2B
        push ax                                         ; 1F2C
        callp R5_1F2E, R5_1B8E, 0x0000                  ; 1F2D MMSYSTEM.DriverCallback
        mov si,[si+0x12]                                ; 1F32
        or_ si,si                                       ; 1F35
        jnz short L5_1F0D                               ; 1F37
        jmp near L5_20D2                                ; 1F39

L5_1F3C:
        push word [bp-0x2]                              ; 1F3C
        xor_ ax,ax                                      ; 1F3F
        push ax                                         ; 1F41
        mov ax,0x4                                      ; 1F42
        push ax                                         ; 1F45
        push word [bp+0x6]                              ; 1F46
        mov ax,0x10                                     ; 1F49
        push ax                                         ; 1F4C
        push cs                                         ; 1F4D
        call L5_0538                                    ; 1F4E
        mov ax,[bp+0x8]                                 ; 1F51
        or ax,[bp+0x6]                                  ; 1F54
        jnz short L5_1F5C                               ; 1F57
        jmp near L5_201F                                ; 1F59

L5_1F5C:
        mov bx,[bp-0x2]                                 ; 1F5C
        mov ax,[bx+0x26]                                ; 1F5F
        mov dx,[bx+0x28]                                ; 1F62
        mov_ si,ax                                      ; 1F65
        mov [bp-0x4],dx                                 ; 1F67
        push bx                                         ; 1F6A
        callf sync_hw_volume, R5_1F6E, R5_1F8F          ; 1F6B far seg5
        mov bx,[bp-0x2]                                 ; 1F70
        push bx                                         ; 1F73
        push word [bx]                                  ; 1F74
        mov ax,0x1                                      ; 1F76
        push ax                                         ; 1F79
        callf vxd_acquire, R5_1F7D, R5_1FBA             ; 1F7A far seg4
        or_ ax,ax                                       ; 1F7F
        jnz short L5_1FBC                               ; 1F81
        push word [bp-0x2]                              ; 1F83
        mov ax,0x7                                      ; 1F86
        cwd                                             ; 1F89
        push dx                                         ; 1F8A
        push ax                                         ; 1F8B
        callf L5_3D72, R5_1F8F, R5_203E                 ; 1F8C far seg5
        push word [bp-0x2]                              ; 1F91
        mov al,0x7f                                     ; 1F94
        push ax                                         ; 1F96
        push word [bp-0x2]                              ; 1F97
        push ax                                         ; 1F9A
        callf mixer_read, R5_1F9E, R5_1FA6              ; 1F9B far seg1
        and al,0xfe                                     ; 1FA0
        push ax                                         ; 1FA2
        callf mixer_write, R5_1FA6, R5_2068             ; 1FA3 far seg1
        mov bx,[bp-0x2]                                 ; 1FA8
        mov byte [bx+0x117],0x1                         ; 1FAB
        push bx                                         ; 1FB0
        push word [bx]                                  ; 1FB1
        mov ax,0x1                                      ; 1FB3
        push ax                                         ; 1FB6
        callf vxd_release, R5_1FBA, R5_2055             ; 1FB7 far seg4

L5_1FBC:
        mov es,[bp-0x4]                                 ; 1FBC
        mov ax,[es:si+0x2844]                           ; 1FBF
        mov dx,[es:si+0x2846]                           ; 1FC4
        mov [bp-0xe],ax                                 ; 1FC9
        mov [bp-0xc],dx                                 ; 1FCC
        mov ax,[es:si+0x2848]                           ; 1FCF
        mov dx,[es:si+0x284a]                           ; 1FD4
        mov [bp-0xa],ax                                 ; 1FD9
        mov [bp-0x8],dx                                 ; 1FDC
        mov word [bp-0x22],0x7                          ; 1FDF
        mov word [bp-0x20],0x0                          ; 1FE4
        mov word [bp-0x26],0x18                         ; 1FE9
        mov word [bp-0x24],0x0                          ; 1FEE
        mov ax,[es:si+0x2598]                           ; 1FF3

L5_1FF8:
        mov [bp-0x1e],ax                                ; 1FF8
        mov word [bp-0x1c],0x0                          ; 1FFB
        sub_ ax,ax                                      ; 2000
        mov [bp-0x18],ax                                ; 2002
        mov [bp-0x1a],ax                                ; 2005
        mov word [bp-0x16],0x4                          ; 2008
        mov [bp-0x14],ax                                ; 200D
        lea ax,[bp-0xe]                                 ; 2010
        mov [bp-0x12],ax                                ; 2013
        mov [bp-0x10],ss                                ; 2016
        push word [bp-0x2]                              ; 2019
        jmp near L5_1ECF                                ; 201C

L5_201F:
        mov bx,[bp-0x2]                                 ; 201F
        test byte [bx+0x2b],0x40                        ; 2022
        jz short L5_202B                                ; 2026
        jmp near L5_20D2                                ; 2028

L5_202B:
        mov si,[bx+0x26]                                ; 202B
        mov dx,[bx+0x28]                                ; 202E
        mov [bp-0x4],dx                                 ; 2031
        push bx                                         ; 2034
        mov ax,0x9                                      ; 2035
        cwd                                             ; 2038
        push dx                                         ; 2039
        push ax                                         ; 203A
        callf L5_3D72, R5_203E, R5_2046                 ; 203B far seg5
        push word [bp-0x2]                              ; 2040
        callf sync_hw_volume, R5_2046, 0xFFFF           ; 2043 far seg5
        mov bx,[bp-0x2]                                 ; 2048
        push bx                                         ; 204B
        push word [bx]                                  ; 204C
        mov ax,0x1                                      ; 204E
        push ax                                         ; 2051
        callf vxd_acquire, R5_2055, R5_2084             ; 2052 far seg4
        or_ ax,ax                                       ; 2057
        jnz short L5_2086                               ; 2059
        push word [bp-0x2]                              ; 205B
        mov al,0x7f                                     ; 205E
        push ax                                         ; 2060
        push word [bp-0x2]                              ; 2061
        push ax                                         ; 2064
        callf mixer_read, R5_2068, R5_2070              ; 2065 far seg1
        or al,0x1                                       ; 206A
        push ax                                         ; 206C
        callf mixer_write, R5_2070, R5_0B39             ; 206D far seg1
        mov bx,[bp-0x2]                                 ; 2072
        mov byte [bx+0x117],0x0                         ; 2075
        push bx                                         ; 207A
        push word [bx]                                  ; 207B
        mov ax,0x1                                      ; 207D
        push ax                                         ; 2080
        callf vxd_release, R5_2084, 0xFFFF              ; 2081 far seg4

L5_2086:
        mov es,[bp-0x4]                                 ; 2086
        mov ax,[es:si+0x2854]                           ; 2089
        mov dx,[es:si+0x2856]                           ; 208E
        mov [bp-0xe],ax                                 ; 2093
        mov [bp-0xc],dx                                 ; 2096
        mov ax,[es:si+0x2858]                           ; 2099
        mov dx,[es:si+0x285a]                           ; 209E
        mov [bp-0xa],ax                                 ; 20A3
        mov [bp-0x8],dx                                 ; 20A6
        mov word [bp-0x22],0x9                          ; 20A9
        mov word [bp-0x20],0x0                          ; 20AE
        mov word [bp-0x26],0x18                         ; 20B3
        mov word [bp-0x24],0x0                          ; 20B8
        mov ax,[es:si+0x25a4]                           ; 20BD
        jmp near L5_1FF8                                ; 20C2

L5_20C5:
        mov bx,[bp-0x2]                                 ; 20C5
        sub_ ax,ax                                      ; 20C8
        mov [bx+0x115],ax                               ; 20CA
        mov [bx+0x113],ax                               ; 20CE

L5_20D2:
        xor_ ax,ax                                      ; 20D2

L5_20D4:
        cwd                                             ; 20D4
        pop ds                                          ; 20D5
        pop si                                          ; 20D6
        pop di                                          ; 20D7
        mov_ sp,bp                                      ; 20D8
        pop bp                                          ; 20DA
        retf 0x10                                       ; 20DB

; the hardware volume buttons
hwvol_callback:
        push bp                                         ; 20DE
        mov_ bp,sp                                      ; 20DF
        sub sp,byte +0x38                               ; 20E1
        push di                                         ; 20E4
        push si                                         ; 20E5
        push ds                                         ; 20E6
        movsel ax, R5_20E8, R5_1BAB                     ; 20E7 seg7
        mov ds,ax                                       ; 20EA
        push word [bp+0xc]                              ; 20EC
        push word [bp+0xa]                              ; 20EF
        callf L3_4EAE, R5_20F5, R5_1C72                 ; 20F2 far seg3
        mov_ si,ax                                      ; 20F7
        or_ si,ax                                       ; 20F9
        jnz short L5_2100                               ; 20FB
        jmp near L5_2355                                ; 20FD

L5_2100:
        mov ax,[si+0x26]                                ; 2100
        mov dx,[si+0x28]                                ; 2103
        mov_ di,ax                                      ; 2106
        mov [bp-0xe],dx                                 ; 2108
        mov al,[si+0x120]                               ; 210B
        mov [bp-0x2],al                                 ; 210F
        mov ax,[bp+0x8]                                 ; 2112
        or ax,[bp+0x6]                                  ; 2115
        jz short L5_211E                                ; 2118
        mov al,0x1                                      ; 211A
        jmp short L5_2120                               ; 211C

L5_211E:
        xor_ al,al                                      ; 211E

L5_2120:
        mov [si+0x120],al                               ; 2120
        sub_ ah,ah                                      ; 2124
        mov [bp-0x18],ax                                ; 2126
        mov word [bp-0x16],0x0                          ; 2129
        mov word [bp-0x34],0x21                         ; 212E
        mov word [bp-0x32],0x0                          ; 2133
        mov word [bp-0x38],0x18                         ; 2138
        mov word [bp-0x36],0x0                          ; 213D
        mov es,[bp-0xe]                                 ; 2142
        mov ax,[es:di+0x2634]                           ; 2145
        mov [bp-0x30],ax                                ; 214A
        mov word [bp-0x2e],0x0                          ; 214D
        sub_ ax,ax                                      ; 2152
        mov [bp-0x2a],ax                                ; 2154
        mov [bp-0x2c],ax                                ; 2157
        mov word [bp-0x28],0x4                          ; 215A
        mov [bp-0x26],ax                                ; 215F
        lea ax,[bp-0x18]                                ; 2162
        mov [bp-0x24],ax                                ; 2165
        mov [bp-0x22],ss                                ; 2168
        push si                                         ; 216B
        lea ax,[bp-0x38]                                ; 216C
        push ss                                         ; 216F
        push ax                                         ; 2170
        sub_ ax,ax                                      ; 2171
        push ax                                         ; 2173
        push ax                                         ; 2174
        push cs                                         ; 2175
        call mxd_set_control_details                    ; 2176
        mov ax,[bp+0xe]                                 ; 2179
        mov dx,[bp+0x10]                                ; 217C
        cmp dx,[bp+0x14]                                ; 217F
        ja short L5_2191                                ; 2182
        jc short L5_218B                                ; 2184
        cmp ax,[bp+0x12]                                ; 2186
        jnc short L5_2191                               ; 2189

L5_218B:
        mov dx,[bp+0x14]                                ; 218B
        mov ax,[bp+0x12]                                ; 218E

L5_2191:
        mov cl,0xa                                      ; 2191
        callf L1_24F8, R5_2196, R5_2289                 ; 2193 far seg1
        and al,0x3f                                     ; 2198
        mov [bp-0x1],al                                 ; 219A
        cmp al,[si+0x122]                               ; 219D
        jna short L5_21C0                               ; 21A1
        cmp byte [si+0x121],0x3f                        ; 21A3
        jnc short L5_21C0                               ; 21A8

L5_21AA:
        mov al,[si+0x123]                               ; 21AA
        add [si+0x121],al                               ; 21AE
        cmp byte [si+0x121],0x3f                        ; 21B2
        jna short L5_2218                               ; 21B7
        mov byte [si+0x121],0x3f                        ; 21B9
        jmp short L5_2218                               ; 21BE

L5_21C0:
        mov al,[si+0x122]                               ; 21C0
        cmp [bp-0x1],al                                 ; 21C4
        jnc short L5_21D0                               ; 21C7
        cmp byte [si+0x121],0x0                         ; 21C9
        jnz short L5_2204                               ; 21CE

L5_21D0:
        mov ax,[bp+0x8]                                 ; 21D0
        or ax,[bp+0x6]                                  ; 21D3
        jz short L5_21DD                                ; 21D6
        mov ax,0x1                                      ; 21D8
        jmp short L5_21DF                               ; 21DB

L5_21DD:
        xor_ ax,ax                                      ; 21DD

L5_21DF:
        mov cl,[bp-0x2]                                 ; 21DF
        sub_ ch,ch                                      ; 21E2
        cmp_ ax,cx                                      ; 21E4
        jnz short L5_2218                               ; 21E6
        cmp byte [si+0x122],0x3f                        ; 21E8
        jnz short L5_21F6                               ; 21ED
        cmp byte [si+0x121],0x3f                        ; 21EF
        jc short L5_21AA                                ; 21F4

L5_21F6:
        cmp byte [si+0x122],0x0                         ; 21F6
        jnz short L5_2218                               ; 21FB
        cmp byte [si+0x121],0x0                         ; 21FD
        jz short L5_2218                                ; 2202

L5_2204:
        mov al,[si+0x123]                               ; 2204
        sub [si+0x121],al                               ; 2208
        test byte [si+0x121],0x80                       ; 220C
        jz short L5_2218                                ; 2211
        mov byte [si+0x121],0x0                         ; 2213

L5_2218:
        mov es,[bp-0xe]                                 ; 2218
        mov ax,[es:di+0x2864]                           ; 221B
        mov dx,[es:di+0x2866]                           ; 2220
        mov_ cx,ax                                      ; 2225
        mov_ bx,dx                                      ; 2227
        cmp dx,[es:di+0x286a]                           ; 2229
        jc short L5_2243                                ; 222E
        ja short L5_2239                                ; 2230
        cmp ax,[es:di+0x2868]                           ; 2232
        jna short L5_2243                               ; 2237

L5_2239:
        mov dx,[es:di+0x286a]                           ; 2239
        mov ax,[es:di+0x2868]                           ; 223E

L5_2243:
        mov [bp-0x14],ax                                ; 2243
        mov [bp-0x12],dx                                ; 2246
        cmp bx,[es:di+0x286a]                           ; 2249
        ja short L5_2263                                ; 224E
        jc short L5_2259                                ; 2250
        cmp cx,[es:di+0x2868]                           ; 2252
        jnc short L5_2263                               ; 2257

L5_2259:
        mov bx,[es:di+0x286a]                           ; 2259
        mov cx,[es:di+0x2868]                           ; 225E

L5_2263:
        mov [bp-0x4],cx                                 ; 2263
        mov [bp-0x2],bx                                 ; 2266
        cmp byte [si+0x121],0x3f                        ; 2269
        jnz short L5_227C                               ; 226E
        mov word [bp-0x8],0xffff                        ; 2270
        mov word [bp-0x6],0x0                           ; 2275
        jmp short L5_2291                               ; 227A

L5_227C:
        mov al,[si+0x121]                               ; 227C
        sub_ ah,ah                                      ; 2280
        sub_ dx,dx                                      ; 2282
        mov cl,0xa                                      ; 2284
        callf L1_2416, R5_2289, R5_22AE                 ; 2286 far seg1
        mov [bp-0x8],ax                                 ; 228B
        mov [bp-0x6],dx                                 ; 228E

L5_2291:
        mov ax,[bp-0x2]                                 ; 2291
        or ax,[bp-0x4]                                  ; 2294
        jz short L5_22B9                                ; 2297
        push word [bp-0x2]                              ; 2299
        push word [bp-0x4]                              ; 229C
        push word [bp-0x12]                             ; 229F
        push word [bp-0x14]                             ; 22A2
        push word [bp-0x6]                              ; 22A5
        push word [bp-0x8]                              ; 22A8
        callf L1_23E4, R5_22AE, R5_22B5                 ; 22AB far seg1
        push dx                                         ; 22B0
        push ax                                         ; 22B1
        callf L1_242E, R5_22B5, R5_1F9E                 ; 22B2 far seg1
        jmp short L5_22BC                               ; 22B7

L5_22B9:
        mov ax,[bp-0x8]                                 ; 22B9

L5_22BC:
        mov [bp-0xc],ax                                 ; 22BC
        mov word [bp-0xa],0x0                           ; 22BF
        mov es,[bp-0xe]                                 ; 22C4
        mov ax,[es:di+0x2868]                           ; 22C7
        mov dx,[es:di+0x286a]                           ; 22CC
        cmp [es:di+0x2866],dx                           ; 22D1
        jc short L5_22F5                                ; 22D6
        ja short L5_22E1                                ; 22D8
        cmp [es:di+0x2864],ax                           ; 22DA
        jc short L5_22F5                                ; 22DF

L5_22E1:
        mov ax,[bp-0x8]                                 ; 22E1
        mov dx,[bp-0x6]                                 ; 22E4
        mov [bp-0x20],ax                                ; 22E7
        mov [bp-0x1e],dx                                ; 22EA
        mov ax,[bp-0xc]                                 ; 22ED
        mov dx,[bp-0xa]                                 ; 22F0
        jmp short L5_2307                               ; 22F3

L5_22F5:
        mov ax,[bp-0xc]                                 ; 22F5
        mov dx,[bp-0xa]                                 ; 22F8
        mov [bp-0x20],ax                                ; 22FB
        mov [bp-0x1e],dx                                ; 22FE
        mov ax,[bp-0x8]                                 ; 2301
        mov dx,[bp-0x6]                                 ; 2304

L5_2307:
        mov [bp-0x1c],ax                                ; 2307
        mov [bp-0x1a],dx                                ; 230A
        mov word [bp-0x34],0xb                          ; 230D
        mov word [bp-0x32],0x0                          ; 2312
        mov word [bp-0x38],0x18                         ; 2317
        mov word [bp-0x36],0x0                          ; 231C
        mov ax,[es:di+0x25b0]                           ; 2321
        mov [bp-0x30],ax                                ; 2326
        mov word [bp-0x2e],0x0                          ; 2329
        sub_ ax,ax                                      ; 232E
        mov [bp-0x2a],ax                                ; 2330
        mov [bp-0x2c],ax                                ; 2333
        mov word [bp-0x28],0x4                          ; 2336
        mov [bp-0x26],ax                                ; 233B
        lea ax,[bp-0x20]                                ; 233E
        mov [bp-0x24],ax                                ; 2341
        mov [bp-0x22],ss                                ; 2344
        push si                                         ; 2347
        lea ax,[bp-0x38]                                ; 2348
        push ss                                         ; 234B
        push ax                                         ; 234C
        sub_ ax,ax                                      ; 234D
        push ax                                         ; 234F
        push ax                                         ; 2350
        push cs                                         ; 2351
        call mxd_set_control_details                    ; 2352

L5_2355:
        pop ds                                          ; 2355
        pop si                                          ; 2356
        pop di                                          ; 2357
        mov_ sp,bp                                      ; 2358
        pop bp                                          ; 235A
        retf 0x10                                       ; 235B

; write one control's registers (05h, 1Ah, 32h-3Eh, 68h-6Eh, 7Ch)
mixer_set_control:
        push bp                                         ; 235E
        mov_ bp,sp                                      ; 235F
        sub sp,byte +0x3e                               ; 2361
        push di                                         ; 2364
        push si                                         ; 2365
        mov bx,[bp+0xa]                                 ; 2366
        mov ax,[bx+0x26]                                ; 2369
        mov dx,[bx+0x28]                                ; 236C
        mov [bp-0xc],ax                                 ; 236F
        mov [bp-0xa],dx                                 ; 2372
        les bx,[bp+0x6]                                 ; 2375
        mov ax,[es:bx+0x4]                              ; 2378
        mov dx,[es:bx+0x6]                              ; 237C
        mov [bp-0x8],ax                                 ; 2380
        mov [bp-0x6],dx                                 ; 2383
        cmp word [es:bx+0x8],byte +0x2                  ; 2386
        jnz short L5_239E                               ; 238B
        cmp word [es:bx+0xa],byte +0x0                  ; 238D
        jnz short L5_239E                               ; 2392
        les bx,[es:bx+0x14]                             ; 2394
        mov al,[es:bx+0x5]                              ; 2398
        jmp short L5_23AD                               ; 239C

L5_239E:
        mov cl,0x3                                      ; 239E
        mov si,[bp-0x8]                                 ; 23A0
        shl si,cl                                       ; 23A3
        les bx,[bp-0xc]                                 ; 23A5
        mov al,[es:bx+si+0x2811]                        ; 23A8

L5_23AD:
        mov cl,0x4                                      ; 23AD
        shr al,cl                                       ; 23AF
        sub_ ah,ah                                      ; 23B1
        mov [bp-0xe],ax                                 ; 23B3
        les bx,[bp+0x6]                                 ; 23B6
        les bx,[es:bx+0x14]                             ; 23B9
        mov al,[es:bx+0x1]                              ; 23BD
        and al,0xf0                                     ; 23C1
        or al,[bp-0xe]                                  ; 23C3
        mov [bp-0x4],al                                 ; 23C6
        cmp word [bp-0x8],byte +0x7                     ; 23C9
        jz short L5_23D2                                ; 23CD
        jmp near L5_246B                                ; 23CF

L5_23D2:
        cmp word [bp-0x6],byte +0x0                     ; 23D2
        jnz short L5_23E5                               ; 23D6
        mov si,[bp+0xa]                                 ; 23D8
        mov ax,[si+0x115]                               ; 23DB
        or ax,[si+0x113]                                ; 23DF
        jnz short L5_23E8                               ; 23E3

L5_23E5:
        jmp near L5_246B                                ; 23E5

L5_23E8:
        mov ax,es                                       ; 23E8
        les si,[bp+0x6]                                 ; 23EA
        mov [bp-0x1a],bx                                ; 23ED
        mov [bp-0x18],ax                                ; 23F0
        cmp word [es:si+0x8],byte +0x2                  ; 23F3
        jnz short L5_2409                               ; 23F8
        cmp word [es:si+0xa],byte +0x0                  ; 23FA
        jnz short L5_2409                               ; 23FF
        mov es,ax                                       ; 2401
        mov ax,[es:bx+0x4]                              ; 2403
        jmp short L5_2418                               ; 2407

L5_2409:
        mov cl,0x3                                      ; 2409
        mov si,[bp-0x8]                                 ; 240B
        shl si,cl                                       ; 240E
        les bx,[bp-0xc]                                 ; 2410
        mov ax,[es:bx+si+0x2810]                        ; 2413

L5_2418:
        mov_ cx,ax                                      ; 2418
        mov_ dx,ax                                      ; 241A
        sub_ ax,ax                                      ; 241C
        mov [bp-0x16],ax                                ; 241E
        mov [bp-0x14],dx                                ; 2421
        les bx,[bp+0x6]                                 ; 2424
        les bx,[es:bx+0x14]                             ; 2427
        mov ax,[es:bx]                                  ; 242B
        mov [bp-0x12],ax                                ; 242E
        mov [bp-0x10],dx                                ; 2431
        les bx,[bp-0xc]                                 ; 2434
        mov ax,[es:bx+0x28f6]                           ; 2437
        or ax,[es:bx+0x28f4]                            ; 243C
        jz short L5_244B                                ; 2441
        sub_ ax,ax                                      ; 2443
        mov [bp-0x10],ax                                ; 2445
        mov [bp-0x12],ax                                ; 2448

L5_244B:
        mov bx,[bp+0xa]                                 ; 244B
        push word [bx+0x111]                            ; 244E
        push word [bx+0x10f]                            ; 2452
        mov dx,0x2                                      ; 2456
        push dx                                         ; 2459
        push dx                                         ; 245A
        push word [bx+0x16]                             ; 245B
        push word [bx+0x14]                             ; 245E
        push word [bp-0x10]                             ; 2461
        push word [bp-0x12]                             ; 2464
        call far [bx+0x113]                             ; 2467

L5_246B:
        mov ax,[bp-0x8]                                 ; 246B
        mov dx,[bp-0x6]                                 ; 246E
        sub ax,strict word 0x3                          ; 2471
        sbb dx,byte +0x0                                ; 2474
        jnz short L5_2481                               ; 2477
        cmp ax,strict word 0x8                          ; 2479
        ja short L5_2481                                ; 247C
        jmp near L5_2525                                ; 247E

L5_2481:
        cmp word [bp-0x8],byte +0xa                     ; 2481
        jnz short L5_248D                               ; 2485
        cmp word [bp-0x6],byte +0x0                     ; 2487
        jz short L5_249C                                ; 248B

L5_248D:
        cmp word [bp-0x8],byte +0xb                     ; 248D
        jnz short L5_2499                               ; 2491
        cmp word [bp-0x6],byte +0x0                     ; 2493
        jz short L5_249C                                ; 2497

L5_2499:
        jmp near L5_2672                                ; 2499

L5_249C:
        cmp word [bp-0x8],byte +0xa                     ; 249C
        jnz short L5_24A8                               ; 24A0
        cmp word [bp-0x6],byte +0x0                     ; 24A2
        jz short L5_24AB                                ; 24A6

L5_24A8:
        jmp near L5_25A3                                ; 24A8

L5_24AB:
        les bx,[bp+0x6]                                 ; 24AB
        les bx,[es:bx+0x14]                             ; 24AE
        mov ax,[es:bx]                                  ; 24B2
        mov dx,[es:bx+0x2]                              ; 24B5
        mov [bp-0x1a],ax                                ; 24B9
        mov [bp-0x18],dx                                ; 24BC
        les bx,[bp-0xc]                                 ; 24BF
        mov cx,[es:bx+0x2864]                           ; 24C2
        mov si,[es:bx+0x2866]                           ; 24C7
        cmp si,[es:bx+0x286a]                           ; 24CC
        ja short L5_24E6                                ; 24D1
        jc short L5_24DC                                ; 24D3
        cmp cx,[es:bx+0x2868]                           ; 24D5
        jnc short L5_24E6                               ; 24DA

L5_24DC:
        mov si,[es:bx+0x286a]                           ; 24DC
        mov cx,[es:bx+0x2868]                           ; 24E1

L5_24E6:
        mov [bp-0x16],cx                                ; 24E6
        mov [bp-0x14],si                                ; 24E9
        mov di,0xffff                                   ; 24EC
        xor_ bx,bx                                      ; 24EF
        push bx                                         ; 24F1
        push di                                         ; 24F2
        push dx                                         ; 24F3
        push ax                                         ; 24F4
        push si                                         ; 24F5
        push cx                                         ; 24F6
        callf L1_23E4, R5_24FA, R5_2501                 ; 24F7 far seg1
        push dx                                         ; 24FC
        push ax                                         ; 24FD
        callf L1_242E, R5_2501, R5_2571                 ; 24FE far seg1
        mov_ al,ah                                      ; 2503
        mov cl,0x5                                      ; 2505
        shr al,cl                                       ; 2507
        mov [bp-0x4],al                                 ; 2509
        les bx,[bp-0xc]                                 ; 250C
        mov ax,[es:bx+0x2916]                           ; 250F
        or ax,[es:bx+0x2914]                            ; 2514
        jnz short L5_251E                               ; 2519
        jmp near L5_2672                                ; 251B

L5_251E:
        mov byte [bp-0x4],0x0                           ; 251E
        jmp near L5_2672                                ; 2522

L5_2525:
        mov cl,0x3                                      ; 2525
        mov si,[bp-0x8]                                 ; 2527
        shl si,cl                                       ; 252A
        les bx,[bp-0xc]                                 ; 252C
        mov ax,[es:bx+si+0x28be]                        ; 252F
        or ax,[es:bx+si+0x28bc]                         ; 2534
        jz short L5_2578                                ; 2539

L5_253B:
        cmp word [bp-0x8],byte +0x7                     ; 253B
        jnz short L5_2551                               ; 253F
        cmp word [bp-0x6],byte +0x0                     ; 2541
        jnz short L5_2551                               ; 2545
        mov bx,[bp+0xa]                                 ; 2547
        cmp byte [bx+0x117],0x0                         ; 254A
        jnz short L5_2567                               ; 254F

L5_2551:
        cmp word [bp-0x8],byte +0x9                     ; 2551
        jnz short L5_2573                               ; 2555
        cmp word [bp-0x6],byte +0x0                     ; 2557
        jnz short L5_2573                               ; 255B
        mov bx,[bp+0xa]                                 ; 255D
        cmp byte [bx+0x117],0x0                         ; 2560
        jnz short L5_2573                               ; 2565

L5_2567:
        push bx                                         ; 2567
        mov al,0x36                                     ; 2568
        push ax                                         ; 256A
        xor_ al,al                                      ; 256B
        push ax                                         ; 256D
        callf mixer_write, R5_2571, R5_262F             ; 256E far seg1

L5_2573:
        xor_ ax,ax                                      ; 2573
        jmp near L5_2B99                                ; 2575

L5_2578:
        cmp word [bp-0x8],byte +0xb                     ; 2578
        jnz short L5_2584                               ; 257C
        cmp word [bp-0x6],byte +0x0                     ; 257E
        jz short L5_259E                                ; 2582

L5_2584:
        mov ax,0x1                                      ; 2584
        mov cl,[bp-0x8]                                 ; 2587
        sub cl,0x3                                      ; 258A
        shl ax,cl                                       ; 258D
        cwd                                             ; 258F
        and ax,[es:bx+0x280c]                           ; 2590
        and dx,[es:bx+0x280e]                           ; 2595
        or_ dx,ax                                       ; 259A
        jz short L5_25A1                                ; 259C

L5_259E:
        jmp near L5_2481                                ; 259E

L5_25A1:
        jmp short L5_253B                               ; 25A1

L5_25A3:
        les bx,[bp-0xc]                                 ; 25A3
        mov ax,[es:bx+0x290e]                           ; 25A6
        or ax,[es:bx+0x290c]                            ; 25AB
        jz short L5_25B5                                ; 25B0
        jmp near L5_2672                                ; 25B2

L5_25B5:
        mov ax,[es:bx+0x285c]                           ; 25B5
        mov dx,[es:bx+0x285e]                           ; 25BA
        mov [bp-0x1a],ax                                ; 25BF
        mov [bp-0x18],dx                                ; 25C2
        les bx,[bp+0x6]                                 ; 25C5
        cmp word [es:bx+0x8],byte +0x2                  ; 25C8
        jnz short L5_25E4                               ; 25CD
        cmp word [es:bx+0xa],byte +0x0                  ; 25CF
        jnz short L5_25E4                               ; 25D4
        les bx,[es:bx+0x14]                             ; 25D6
        mov ax,[es:bx+0x4]                              ; 25DA
        mov dx,[es:bx+0x6]                              ; 25DE
        jmp short L5_25F1                               ; 25E2

L5_25E4:
        les bx,[bp-0xc]                                 ; 25E4
        mov ax,[es:bx+0x2868]                           ; 25E7
        mov dx,[es:bx+0x286a]                           ; 25EC

L5_25F1:
        mov [bp-0x1e],ax                                ; 25F1
        mov [bp-0x1c],dx                                ; 25F4
        les bx,[bp+0x6]                                 ; 25F7
        les bx,[es:bx+0x14]                             ; 25FA
        mov ax,[es:bx]                                  ; 25FE
        mov dx,[es:bx+0x2]                              ; 2601
        cmp dx,[bp-0x1c]                                ; 2605
        ja short L5_2617                                ; 2608
        jc short L5_2611                                ; 260A
        cmp ax,[bp-0x1e]                                ; 260C
        jnc short L5_2617                               ; 260F

L5_2611:
        mov dx,[bp-0x1c]                                ; 2611
        mov ax,[bp-0x1e]                                ; 2614

L5_2617:
        mov [bp-0x16],ax                                ; 2617
        mov [bp-0x14],dx                                ; 261A
        mov cx,0xffff                                   ; 261D
        xor_ bx,bx                                      ; 2620
        push bx                                         ; 2622
        push cx                                         ; 2623
        push word [bp-0x18]                             ; 2624
        push word [bp-0x1a]                             ; 2627
        push dx                                         ; 262A
        push ax                                         ; 262B
        callf L1_23E4, R5_262F, R5_2636                 ; 262C far seg1
        push dx                                         ; 2631
        push ax                                         ; 2632
        callf L1_242E, R5_2636, R5_2661                 ; 2633 far seg1
        mov_ al,ah                                      ; 2638
        mov cl,0x5                                      ; 263A
        shr al,cl                                       ; 263C
        mov [bp-0x12],al                                ; 263E
        mov bx,[bp+0xa]                                 ; 2641
        push bx                                         ; 2644
        push word [bx]                                  ; 2645
        mov ax,0x1                                      ; 2647
        push ax                                         ; 264A
        callf vxd_acquire, R5_264E, R5_2670             ; 264B far seg4
        or_ ax,ax                                       ; 2650
        jnz short L5_2672                               ; 2652
        push word [bp+0xa]                              ; 2654
        mov al,0x3c                                     ; 2657
        push ax                                         ; 2659
        mov al,[bp-0x12]                                ; 265A
        push ax                                         ; 265D
        callf mixer_write, R5_2661, R5_2196             ; 265E far seg1
        mov bx,[bp+0xa]                                 ; 2663
        push bx                                         ; 2666
        push word [bx]                                  ; 2667
        mov ax,0x1                                      ; 2669
        push ax                                         ; 266C
        callf vxd_release, R5_2670, R5_1F7D             ; 266D far seg4

L5_2672:
        mov ax,[bp-0x8]                                 ; 2672
        mov dx,[bp-0x6]                                 ; 2675
        jmp near L5_29E3                                ; 2678

L5_267B:
        mov cl,0x4                                      ; 267B
        mov si,[bp+0xa]                                 ; 267D
        mov bl,[bp-0x4]                                 ; 2680
        mov [si+0x124],bl                               ; 2683
        shr bl,cl                                       ; 2687
        sub_ bh,bh                                      ; 2689
        mov al,[bx+0xa10]                               ; 268B
        shl al,cl                                       ; 268F
        mov bl,[bp-0x4]                                 ; 2691
        and bx,byte +0xf                                ; 2694
        or al,[bx+0xa10]                                ; 2697
        mov [bp-0x4],al                                 ; 269B
        mov byte [bp-0x2],0x7c                          ; 269E
        jmp near L5_2A53                                ; 26A2

L5_26A5:
        mov cl,0x4                                      ; 26A5
        mov bl,[bp-0x4]                                 ; 26A7
        shr bl,cl                                       ; 26AA
        sub_ bh,bh                                      ; 26AC
        mov al,[bx+0xa40]                               ; 26AE
        shl al,cl                                       ; 26B2
        mov bl,[bp-0x4]                                 ; 26B4
        and bx,byte +0xf                                ; 26B7
        or al,[bx+0xa40]                                ; 26BA

L5_26BE:
        mov [bp-0x4],al                                 ; 26BE
        mov byte [bp-0x2],0x36                          ; 26C1
        jmp near L5_2A53                                ; 26C5

L5_26C8:
        mov byte [bp-0x2],0x32                          ; 26C8
        jmp near L5_2A53                                ; 26CC

L5_26CF:
        mov cl,0x4                                      ; 26CF
        mov bl,[bp-0x4]                                 ; 26D1
        shr bl,cl                                       ; 26D4
        sub_ bh,bh                                      ; 26D6
        mov al,[bx+0xa00]                               ; 26D8
        shl al,cl                                       ; 26DC
        mov bl,[bp-0x4]                                 ; 26DE
        and bx,byte +0xf                                ; 26E1
        or al,[bx+0xa00]                                ; 26E4
        mov [bp-0x4],al                                 ; 26E8
        mov byte [bp-0x2],0x3e                          ; 26EB
        jmp near L5_2A53                                ; 26EF

L5_26F2:
        mov cl,0x4                                      ; 26F2
        mov bl,[bp-0x4]                                 ; 26F4
        shr bl,cl                                       ; 26F7
        sub_ bh,bh                                      ; 26F9
        mov al,[bx+0xa20]                               ; 26FB
        shl al,cl                                       ; 26FF
        mov bl,[bp-0x4]                                 ; 2701
        and bx,byte +0xf                                ; 2704
        or al,[bx+0xa20]                                ; 2707
        mov [bp-0x4],al                                 ; 270B
        mov byte [bp-0x2],0x1a                          ; 270E
        jmp near L5_2A53                                ; 2712

L5_2715:
        mov cl,0x4                                      ; 2715
        mov bl,[bp-0x4]                                 ; 2717
        shr bl,cl                                       ; 271A
        sub_ bh,bh                                      ; 271C
        mov al,[bx+0xa30]                               ; 271E
        shl al,cl                                       ; 2722
        mov bl,[bp-0x4]                                 ; 2724
        and bx,byte +0xf                                ; 2727
        or al,[bx+0xa30]                                ; 272A
        mov [bp-0x4],al                                 ; 272E
        mov byte [bp-0x2],0x38                          ; 2731
        jmp near L5_2A53                                ; 2735

L5_2738:
        mov cl,0x4                                      ; 2738
        mov bl,[bp-0x4]                                 ; 273A
        shr bl,cl                                       ; 273D
        sub_ bh,bh                                      ; 273F
        mov al,[bx+0xa50]                               ; 2741
        shl al,cl                                       ; 2745
        mov bl,[bp-0x4]                                 ; 2747
        and bx,byte +0xf                                ; 274A
        or al,[bx+0xa50]                                ; 274D
        mov [bp-0x4],al                                 ; 2751
        mov byte [bp-0x2],0x3a                          ; 2754
        jmp near L5_2A53                                ; 2758

L5_275B:
        mov cl,0x4                                      ; 275B
        mov bl,[bp-0x4]                                 ; 275D
        shr bl,cl                                       ; 2760
        sub_ bh,bh                                      ; 2762
        mov al,[bx+0xa60]                               ; 2764
        shl al,cl                                       ; 2768
        mov bl,[bp-0x4]                                 ; 276A
        and bx,byte +0xf                                ; 276D
        or al,[bx+0xa60]                                ; 2770
        jmp near L5_26BE                                ; 2774

L5_2777:
        mov byte [bp-0x2],0x6d                          ; 2777
        jmp near L5_2A53                                ; 277B

L5_277E:
        mov byte [bp-0x2],0x3c                          ; 277E
        jmp near L5_2A53                                ; 2782

L5_2785:
        mov cl,0x4                                      ; 2785
        mov bl,[bp-0x4]                                 ; 2787
        shr bl,cl                                       ; 278A
        sub_ bh,bh                                      ; 278C
        mov al,[bx+0x990]                               ; 278E
        shl al,cl                                       ; 2792
        mov bl,[bp-0x4]                                 ; 2794
        and bx,byte +0xf                                ; 2797
        or al,[bx+0x990]                                ; 279A
        mov [bp-0x4],al                                 ; 279E
        mov byte [bp-0x2],0x6e                          ; 27A1
        jmp near L5_2A53                                ; 27A5

L5_27A8:
        mov cl,0x4                                      ; 27A8
        mov bl,[bp-0x4]                                 ; 27AA
        shr bl,cl                                       ; 27AD
        sub_ bh,bh                                      ; 27AF
        mov al,[bx+0x9b0]                               ; 27B1
        shl al,cl                                       ; 27B5
        mov bl,[bp-0x4]                                 ; 27B7
        and bx,byte +0xf                                ; 27BA
        or al,[bx+0x9b0]                                ; 27BD
        mov [bp-0x4],al                                 ; 27C1
        mov byte [bp-0x2],0x68                          ; 27C4
        jmp near L5_2A53                                ; 27C8

L5_27CB:
        mov cl,0x4                                      ; 27CB
        mov bl,[bp-0x4]                                 ; 27CD
        shr bl,cl                                       ; 27D0
        sub_ bh,bh                                      ; 27D2
        mov al,[bx+0x9b0]                               ; 27D4
        shl al,cl                                       ; 27D8
        mov bl,[bp-0x4]                                 ; 27DA
        and bx,byte +0xf                                ; 27DD
        or al,[bx+0x9b0]                                ; 27E0
        mov [bp-0x4],al                                 ; 27E4
        mov byte [bp-0x2],0x68                          ; 27E7
        cmp byte [0xa70],0x0                            ; 27EB
        jz short L5_27F5                                ; 27F0
        jmp near L5_2898                                ; 27F2

L5_27F5:
        mov cl,0x3                                      ; 27F5
        mov bx,[bp-0x8]                                 ; 27F7
        shl bx,cl                                       ; 27FA
        add bx,[bp-0xc]                                 ; 27FC
        mov es,[bp-0xa]                                 ; 27FF
        mov ax,[es:bx+0x280c]                           ; 2802
        mov dx,[es:bx+0x280e]                           ; 2807
        mov [bp-0x3e],ax                                ; 280C
        mov [bp-0x3c],dx                                ; 280F
        mov ax,[es:bx+0x2810]                           ; 2812
        mov dx,[es:bx+0x2812]                           ; 2817
        mov [bp-0x3a],ax                                ; 281C
        mov [bp-0x38],dx                                ; 281F
        cmp word [bp-0x8],byte +0xd                     ; 2822
        jnz short L5_2833                               ; 2826
        cmp word [bp-0x6],byte +0x0                     ; 2828
        jnz short L5_2833                               ; 282C
        mov ax,0x2f                                     ; 282E
        jmp short L5_2836                               ; 2831

L5_2833:
        mov ax,0xd                                      ; 2833

L5_2836:
        cwd                                             ; 2836
        mov [bp-0x8],ax                                 ; 2837
        mov [bp-0x6],dx                                 ; 283A
        mov [bp-0x32],ax                                ; 283D
        mov [bp-0x30],dx                                ; 2840
        mov word [bp-0x36],0x18                         ; 2843
        mov word [bp-0x34],0x0                          ; 2848
        mov cx,0x6                                      ; 284D
        mul cx                                          ; 2850
        mov bx,[bp-0xc]                                 ; 2852
        mov_ si,ax                                      ; 2855
        mov ax,[es:bx+si+0x256e]                        ; 2857
        mov [bp-0x2e],ax                                ; 285C
        mov word [bp-0x2c],0x0                          ; 285F
        sub_ ax,ax                                      ; 2864
        mov [bp-0x28],ax                                ; 2866
        mov [bp-0x2a],ax                                ; 2869
        mov word [bp-0x26],0x4                          ; 286C
        mov [bp-0x24],ax                                ; 2871
        lea ax,[bp-0x3e]                                ; 2874
        mov [bp-0x22],ax                                ; 2877
        mov [bp-0x20],ss                                ; 287A
        mov byte [0xa70],0x1                            ; 287D
        push word [bp+0xa]                              ; 2882
        lea ax,[bp-0x36]                                ; 2885
        push ss                                         ; 2888
        push ax                                         ; 2889
        sub_ ax,ax                                      ; 288A
        push ax                                         ; 288C
        push ax                                         ; 288D
        callf mxd_set_control_details, R5_2891, R5_1F6E ; 288E far seg5
        mov byte [0xa70],0x0                            ; 2893

L5_2898:
        mov bx,[bp+0xa]                                 ; 2898
        push bx                                         ; 289B
        push word [bx]                                  ; 289C
        mov ax,0x1                                      ; 289E
        push ax                                         ; 28A1
        callf vxd_acquire, R5_28A5, R5_28E9             ; 28A2 far seg4
        or_ ax,ax                                       ; 28A7
        jz short L5_28AE                                ; 28A9
        jmp near L5_2573                                ; 28AB

L5_28AE:
        mov ax,[0xa6]                                   ; 28AE
        inc word [0xa6]                                 ; 28B1
        or_ ax,ax                                       ; 28B5
        jnz short L5_28BA                               ; 28B7
        cli                                             ; 28B9

L5_28BA:
        cmp byte [0xc6],0x0                             ; 28BA
        jnz short L5_28C5                               ; 28BF
        mov byte [bp-0x4],0x0                           ; 28C1

L5_28C5:
        push word [bp+0xa]                              ; 28C5
        mov al,[bp-0x2]                                 ; 28C8
        push ax                                         ; 28CB
        mov al,[bp-0x4]                                 ; 28CC
        push ax                                         ; 28CF
        callf mixer_write, R5_28D3, R5_29B6             ; 28D0 far seg1
        dec word [0xa6]                                 ; 28D5
        jnz short L5_28DC                               ; 28D9
        sti                                             ; 28DB

L5_28DC:
        mov bx,[bp+0xa]                                 ; 28DC
        push bx                                         ; 28DF
        push word [bx]                                  ; 28E0
        mov ax,0x1                                      ; 28E2
        push ax                                         ; 28E5
        callf vxd_release, R5_28E9, R5_2987             ; 28E6 far seg4
        jmp near L5_2573                                ; 28EB

L5_28EE:
        mov cl,0x4                                      ; 28EE
        mov bl,[bp-0x4]                                 ; 28F0
        shr bl,cl                                       ; 28F3
        sub_ bh,bh                                      ; 28F5
        mov al,[bx+0x9c0]                               ; 28F7
        shl al,cl                                       ; 28FB
        mov bl,[bp-0x4]                                 ; 28FD
        and bx,byte +0xf                                ; 2900
        or al,[bx+0x9c0]                                ; 2903
        mov [bp-0x4],al                                 ; 2907
        mov byte [bp-0x2],0x6a                          ; 290A
        jmp near L5_2A53                                ; 290E

L5_2911:
        mov cl,0x4                                      ; 2911
        mov bl,[bp-0x4]                                 ; 2913
        shr bl,cl                                       ; 2916
        sub_ bh,bh                                      ; 2918
        mov al,[bx+0x9e0]                               ; 291A
        shl al,cl                                       ; 291E
        mov bl,[bp-0x4]                                 ; 2920
        and bx,byte +0xf                                ; 2923
        or al,[bx+0x9e0]                                ; 2926
        mov [bp-0x4],al                                 ; 292A
        mov byte [bp-0x2],0x6c                          ; 292D
        jmp near L5_2A53                                ; 2931

L5_2934:
        mov cl,0x4                                      ; 2934
        mov bl,[bp-0x4]                                 ; 2936
        shr bl,cl                                       ; 2939
        sub_ bh,bh                                      ; 293B
        mov al,[bx+0x9d0]                               ; 293D
        shl al,cl                                       ; 2941
        mov bl,[bp-0x4]                                 ; 2943
        and bx,byte +0xf                                ; 2946
        or al,[bx+0x9d0]                                ; 2949
        mov [bp-0x4],al                                 ; 294D
        mov byte [bp-0x2],0x6b                          ; 2950
        jmp near L5_2A53                                ; 2954

L5_2957:
        mov cl,0x4                                      ; 2957
        mov bl,[bp-0x4]                                 ; 2959
        shr bl,cl                                       ; 295C
        sub_ bh,bh                                      ; 295E
        mov al,[bx+0x9a0]                               ; 2960
        shl al,cl                                       ; 2964
        mov bl,[bp-0x4]                                 ; 2966
        and bx,byte +0xf                                ; 2969
        or al,[bx+0x9a0]                                ; 296C
        mov [bp-0x4],al                                 ; 2970
        mov byte [bp-0x2],0x69                          ; 2973
        jmp near L5_2A53                                ; 2977

L5_297A:
        mov bx,[bp+0xa]                                 ; 297A
        push bx                                         ; 297D
        push word [bx]                                  ; 297E
        mov ax,0x1                                      ; 2980
        push ax                                         ; 2983
        callf vxd_acquire, R5_2987, R5_2A60             ; 2984 far seg4
        or_ ax,ax                                       ; 2989
        jz short L5_2990                                ; 298B
        jmp near L5_2573                                ; 298D

L5_2990:
        mov byte [bp-0x3e],0x0                          ; 2990
        mov ax,[0xa6]                                   ; 2994
        inc word [0xa6]                                 ; 2997
        or_ ax,ax                                       ; 299B
        jnz short L5_29A0                               ; 299D
        cli                                             ; 299F

L5_29A0:
        mov cl,0x4                                      ; 29A0
        mov al,[bp-0x4]                                 ; 29A2
        shl al,cl                                       ; 29A5
        mov [bp-0x3e],al                                ; 29A7
        push word [bp+0xa]                              ; 29AA
        mov dl,0xc6                                     ; 29AD
        push dx                                         ; 29AF
        mov [bp-0x36],ax                                ; 29B0
        callf dsp_write, R5_29B6, R5_29C1               ; 29B3 far seg1
        push word [bp+0xa]                              ; 29B8
        mov al,0xb4                                     ; 29BB
        push ax                                         ; 29BD
        callf dsp_write, R5_29C1, R5_29D4               ; 29BE far seg1
        push word [bp+0xa]                              ; 29C3
        mov cl,0x4                                      ; 29C6
        mov al,[bp-0x4]                                 ; 29C8
        shr al,cl                                       ; 29CB
        or al,[bp-0x36]                                 ; 29CD
        push ax                                         ; 29D0
        callf dsp_write, R5_29D4, R5_2AAF               ; 29D1 far seg1
        dec word [0xa6]                                 ; 29D6
        jz short L5_29DF                                ; 29DA
        jmp near L5_28DC                                ; 29DC

L5_29DF:
        sti                                             ; 29DF
        jmp near L5_28DC                                ; 29E0

L5_29E3:
        or_ dx,dx                                       ; 29E3
        jnz short L5_2A53                               ; 29E5
        sub ax,strict word 0x3                          ; 29E7
        cmp ax,strict word 0x2d                         ; 29EA
        ja short L5_2A53                                ; 29ED
        add_ ax,ax                                      ; 29EF
        xchg ax,bx                                      ; 29F1
        jmp [cs:bx+JT5_29F7]                            ; 29F2
JT5_29F7:
        dw L5_26CF                                      ; 29F7
        dw L5_267B                                      ; 29F9
        dw L5_26F2                                      ; 29FB
        dw L5_2715                                      ; 29FD
        dw L5_26A5                                      ; 29FF
        dw L5_2738                                      ; 2A01
        dw L5_275B                                      ; 2A03
        dw L5_277E                                      ; 2A05
        dw L5_26C8                                      ; 2A07
        dw L5_2785                                      ; 2A09
        dw L5_27CB                                      ; 2A0B
        dw L5_28EE                                      ; 2A0D
        dw L5_2911                                      ; 2A0F
        dw L5_2934                                      ; 2A11
        dw L5_2957                                      ; 2A13
        dw L5_297A                                      ; 2A15
        dw L5_2785                                      ; 2A17
        dw L5_27A8                                      ; 2A19
        dw L5_28EE                                      ; 2A1B
        dw L5_2911                                      ; 2A1D
        dw L5_2957                                      ; 2A1F
        dw L5_2A53                                      ; 2A21
        dw L5_2A53                                      ; 2A23
        dw L5_2A53                                      ; 2A25
        dw L5_2A53                                      ; 2A27
        dw L5_2A53                                      ; 2A29
        dw L5_2A53                                      ; 2A2B
        dw L5_2A53                                      ; 2A2D
        dw L5_2A53                                      ; 2A2F
        dw L5_2A53                                      ; 2A31
        dw L5_2A53                                      ; 2A33
        dw L5_2A53                                      ; 2A35
        dw L5_2A53                                      ; 2A37
        dw L5_2A53                                      ; 2A39
        dw L5_2A53                                      ; 2A3B
        dw L5_2A53                                      ; 2A3D
        dw L5_2A53                                      ; 2A3F
        dw L5_2A53                                      ; 2A41
        dw L5_2A53                                      ; 2A43
        dw L5_2A53                                      ; 2A45
        dw L5_2A53                                      ; 2A47
        dw L5_2A53                                      ; 2A49
        dw L5_2A53                                      ; 2A4B
        dw L5_2A53                                      ; 2A4D
        dw L5_27CB                                      ; 2A4F
        dw L5_2777                                      ; 2A51

L5_2A53:
        mov bx,[bp+0xa]                                 ; 2A53
        push bx                                         ; 2A56
        push word [bx]                                  ; 2A57
        mov ax,0x1                                      ; 2A59
        push ax                                         ; 2A5C
        callf vxd_acquire, R5_2A60, R5_264E             ; 2A5D far seg4
        or_ ax,ax                                       ; 2A62
        jz short L5_2A69                                ; 2A64
        jmp near L5_2573                                ; 2A66

L5_2A69:
        cmp byte [bp-0x2],0x32                          ; 2A69
        jz short L5_2A72                                ; 2A6D
        jmp near L5_2B22                                ; 2A6F

L5_2A72:
        les bx,[bp+0x6]                                 ; 2A72
        les bx,[es:bx+0x14]                             ; 2A75
        mov al,[es:bx+0x1]                              ; 2A79
        shr al,1                                        ; 2A7D
        mov bx,[bp+0xa]                                 ; 2A7F
        shr al,1                                        ; 2A82
        mov [bx+0x121],al                               ; 2A84
        mov_ bl,al                                      ; 2A88
        sub_ bh,bh                                      ; 2A8A
        mov al,[bx+0x950]                               ; 2A8C
        mov bx,[bp+0xa]                                 ; 2A90
        mov [bx+0x122],al                               ; 2A93
        cmp byte [bx+0x120],0x1                         ; 2A97
        cmc                                             ; 2A9C
        sbb_ cl,cl                                      ; 2A9D
        and cl,0x40                                     ; 2A9F
        or_ al,cl                                       ; 2AA2
        mov [bp-0x4],al                                 ; 2AA4
        push bx                                         ; 2AA7
        mov cl,0x60                                     ; 2AA8
        push cx                                         ; 2AAA
        push ax                                         ; 2AAB
        callf mixer_write, R5_2AAF, R5_24FA             ; 2AAC far seg1
        les bx,[bp+0x6]                                 ; 2AB1
        cmp word [es:bx+0x8],byte +0x2                  ; 2AB4
        jnz short L5_2ACC                               ; 2AB9
        cmp word [es:bx+0xa],byte +0x0                  ; 2ABB
        jnz short L5_2ACC                               ; 2AC0
        les bx,[es:bx+0x14]                             ; 2AC2
        mov al,[es:bx+0x5]                              ; 2AC6
        jmp short L5_2ADB                               ; 2ACA

L5_2ACC:
        mov cl,0x3                                      ; 2ACC
        mov si,[bp-0x8]                                 ; 2ACE
        shl si,cl                                       ; 2AD1
        les bx,[bp-0xc]                                 ; 2AD3
        mov al,[es:bx+si+0x2811]                        ; 2AD6

L5_2ADB:
        shr al,1                                        ; 2ADB
        shr al,1                                        ; 2ADD
        mov [bp-0x4],al                                 ; 2ADF
        mov bx,[bp+0xa]                                 ; 2AE2
        cmp [bx+0x121],al                               ; 2AE5
        jnc short L5_2AFE                               ; 2AE9
        mov [bx+0x121],al                               ; 2AEB
        mov_ bl,al                                      ; 2AEF
        sub_ bh,bh                                      ; 2AF1
        mov al,[bx+0x950]                               ; 2AF3
        mov bx,[bp+0xa]                                 ; 2AF7
        mov [bx+0x122],al                               ; 2AFA

L5_2AFE:
        mov bl,[bp-0x4]                                 ; 2AFE
        sub_ bh,bh                                      ; 2B01
        mov al,[bx+0x950]                               ; 2B03
        mov [bp-0x4],al                                 ; 2B07
        push word [bp+0xa]                              ; 2B0A
        mov al,0x62                                     ; 2B0D
        push ax                                         ; 2B0F
        mov bx,[bp+0xa]                                 ; 2B10
        cmp byte [bx+0x120],0x1                         ; 2B13
        cmc                                             ; 2B18
        sbb_ al,al                                      ; 2B19
        and al,0x40                                     ; 2B1B
        or [bp-0x4],al                                  ; 2B1D
        jmp short L5_2B88                               ; 2B20

L5_2B22:
        cmp word [bp-0x8],byte +0x30                    ; 2B22
        jnz short L5_2B4F                               ; 2B26
        cmp [bp-0x6],ax                                 ; 2B28
        jnz short L5_2B4F                               ; 2B2B
        push word [bp+0xa]                              ; 2B2D
        mov al,[bp-0x2]                                 ; 2B30
        push ax                                         ; 2B33
        mov cl,[bp-0x4]                                 ; 2B34
        push cx                                         ; 2B37
        callf mixer_write, R5_2B3B, R5_2B4D             ; 2B38 far seg1
        push word [bp+0xa]                              ; 2B3D
        mov al,[bp-0x2]                                 ; 2B40
        add al,0x2                                      ; 2B43
        push ax                                         ; 2B45
        mov al,[bp-0x4]                                 ; 2B46
        push ax                                         ; 2B49
        callf mixer_write, R5_2B4D, R5_2B8F             ; 2B4A far seg1

L5_2B4F:
        cmp word [bp-0x8],byte +0x9                     ; 2B4F
        jnz short L5_2B6A                               ; 2B53
        cmp word [bp-0x6],byte +0x0                     ; 2B55
        jnz short L5_2B6A                               ; 2B59
        mov bx,[bp+0xa]                                 ; 2B5B
        cmp byte [bx+0x117],0x0                         ; 2B5E
        jz short L5_2B68                                ; 2B63
        jmp near L5_28DC                                ; 2B65

L5_2B68:
        jmp short L5_2B83                               ; 2B68

L5_2B6A:
        cmp word [bp-0x8],byte +0x7                     ; 2B6A
        jnz short L5_2B94                               ; 2B6E
        cmp word [bp-0x6],byte +0x0                     ; 2B70
        jnz short L5_2B94                               ; 2B74
        mov bx,[bp+0xa]                                 ; 2B76
        cmp byte [bx+0x117],0x0                         ; 2B79
        jnz short L5_2B83                               ; 2B7E
        jmp near L5_28DC                                ; 2B80

L5_2B83:
        push bx                                         ; 2B83

L5_2B84:
        mov al,[bp-0x2]                                 ; 2B84
        push ax                                         ; 2B87

L5_2B88:
        mov al,[bp-0x4]                                 ; 2B88
        push ax                                         ; 2B8B
        callf mixer_write, R5_2B8F, R5_28D3             ; 2B8C far seg1
        jmp near L5_28DC                                ; 2B91

L5_2B94:
        push word [bp+0xa]                              ; 2B94
        jmp short L5_2B84                               ; 2B97

L5_2B99:
        pop si                                          ; 2B99
        pop di                                          ; 2B9A
        mov_ sp,bp                                      ; 2B9B
        pop bp                                          ; 2B9D
        retf 0x6                                        ; 2B9E

; per-control handler table (near pointers into this segment)
control_handlers:
        db 0xFC, 0x32                                   ; 2BA1
R5_2BA3: dw R5_2BA7                                     ; 2BA3 seg5
        db 0xFC, 0x32                                   ; 2BA5
R5_2BA7: dw R5_2BAB                                     ; 2BA7 seg5
        db 0xFC, 0x32                                   ; 2BA9
R5_2BAB: dw R5_2BAF                                     ; 2BAB seg5
        db 0x5E, 0x23                                   ; 2BAD
R5_2BAF: dw R5_2BB3                                     ; 2BAF seg5
        db 0x5E, 0x23                                   ; 2BB1
R5_2BB3: dw R5_2BB7                                     ; 2BB3 seg5
        db 0x5E, 0x23                                   ; 2BB5
R5_2BB7: dw R5_2BBB                                     ; 2BB7 seg5
        db 0x5E, 0x23                                   ; 2BB9
R5_2BBB: dw R5_2BBF                                     ; 2BBB seg5
        db 0x5E, 0x23                                   ; 2BBD
R5_2BBF: dw R5_2BC3                                     ; 2BBF seg5
        db 0x5E, 0x23                                   ; 2BC1
R5_2BC3: dw R5_2BC7                                     ; 2BC3 seg5
        db 0x5E, 0x23                                   ; 2BC5
R5_2BC7: dw R5_2BCB                                     ; 2BC7 seg5
        db 0x5E, 0x23                                   ; 2BC9
R5_2BCB: dw R5_2BCF                                     ; 2BCB seg5
        db 0x5E, 0x23                                   ; 2BCD
R5_2BCF: dw R5_2BD3                                     ; 2BCF seg5
        db 0x76, 0x31                                   ; 2BD1
R5_2BD3: dw R5_2BD7                                     ; 2BD3 seg5
        db 0x5E, 0x23                                   ; 2BD5
R5_2BD7: dw R5_2BDB                                     ; 2BD7 seg5
        db 0x76, 0x31                                   ; 2BD9
R5_2BDB: dw R5_2BDF                                     ; 2BDB seg5
        db 0x76, 0x31                                   ; 2BDD
R5_2BDF: dw R5_2BE3                                     ; 2BDF seg5
        db 0x76, 0x31                                   ; 2BE1
R5_2BE3: dw R5_2BE7                                     ; 2BE3 seg5
        db 0x76, 0x31                                   ; 2BE5
R5_2BE7: dw R5_2BEB                                     ; 2BE7 seg5
        db 0x76, 0x31                                   ; 2BE9
R5_2BEB: dw R5_2BEF                                     ; 2BEB seg5
        db 0x76, 0x31                                   ; 2BED
R5_2BEF: dw R5_2BF3                                     ; 2BEF seg5
        db 0x76, 0x31                                   ; 2BF1
R5_2BF3: dw R5_2BF7                                     ; 2BF3 seg5
        db 0x76, 0x31                                   ; 2BF5
R5_2BF7: dw R5_2BFB                                     ; 2BF7 seg5
        db 0x76, 0x31                                   ; 2BF9
R5_2BFB: dw R5_2BFF                                     ; 2BFB seg5
        db 0x76, 0x31                                   ; 2BFD
R5_2BFF: dw R5_2C03                                     ; 2BFF seg5
        db 0x76, 0x31                                   ; 2C01
R5_2C03: dw R5_2C07                                     ; 2C03 seg5
        db 0x66, 0x2C                                   ; 2C05
R5_2C07: dw R5_2C0B                                     ; 2C07 seg5
        db 0x66, 0x2C                                   ; 2C09
R5_2C0B: dw R5_2C0F                                     ; 2C0B seg5
        db 0x66, 0x2C                                   ; 2C0D
R5_2C0F: dw R5_2C13                                     ; 2C0F seg5
        db 0x66, 0x2C                                   ; 2C11
R5_2C13: dw R5_2C17                                     ; 2C13 seg5
        db 0x66, 0x2C                                   ; 2C15
R5_2C17: dw R5_2C1B                                     ; 2C17 seg5
        db 0x66, 0x2C                                   ; 2C19
R5_2C1B: dw R5_2C1F                                     ; 2C1B seg5
        db 0x66, 0x2C                                   ; 2C1D
R5_2C1F: dw R5_2C23                                     ; 2C1F seg5
        db 0x66, 0x2C                                   ; 2C21
R5_2C23: dw R5_2C27                                     ; 2C23 seg5
        db 0x66, 0x2C                                   ; 2C25
R5_2C27: dw R5_2C2B                                     ; 2C27 seg5
        db 0xEE, 0x38                                   ; 2C29
R5_2C2B: dw R5_2C2F                                     ; 2C2B seg5
        db 0xEE, 0x38                                   ; 2C2D
R5_2C2F: dw R5_2C33                                     ; 2C2F seg5
        db 0xEE, 0x38                                   ; 2C31
R5_2C33: dw R5_2C37                                     ; 2C33 seg5
        db 0xF4, 0x38                                   ; 2C35
R5_2C37: dw R5_2C3B                                     ; 2C37 seg5
        db 0xF4, 0x38                                   ; 2C39
R5_2C3B: dw R5_2C3F                                     ; 2C3B seg5
        db 0xC0, 0x39                                   ; 2C3D
R5_2C3F: dw R5_2C43                                     ; 2C3F seg5
        db 0xC0, 0x39                                   ; 2C41
R5_2C43: dw R5_2C47                                     ; 2C43 seg5
        db 0xE6, 0x3A                                   ; 2C45
R5_2C47: dw R5_2C4B                                     ; 2C47 seg5
        db 0xE6, 0x3A                                   ; 2C49
R5_2C4B: dw R5_2C4F                                     ; 2C4B seg5
        db 0xE6, 0x3A                                   ; 2C4D
R5_2C4F: dw R5_2C53                                     ; 2C4F seg5
        db 0xE6, 0x3A                                   ; 2C51
R5_2C53: dw R5_2C57                                     ; 2C53 seg5
        db 0x66, 0x2C                                   ; 2C55
R5_2C57: dw R5_2C5B                                     ; 2C57 seg5
        db 0x66, 0x2C                                   ; 2C59
R5_2C5B: dw R5_2C5F                                     ; 2C5B seg5
        db 0x5E, 0x23                                   ; 2C5D
R5_2C5F: dw R5_2C63                                     ; 2C5F seg5
        db 0x5E, 0x23                                   ; 2C61
R5_2C63: dw R5_3D12                                     ; 2C63 seg5
        db 0x90                                         ; 2C65
        push bp                                         ; 2C66
        mov_ bp,sp                                      ; 2C67
        sub sp,strict word 0x56                         ; 2C69
        push di                                         ; 2C6D
        push si                                         ; 2C6E
        mov bx,[bp+0xa]                                 ; 2C6F
        mov ax,[bx+0x26]                                ; 2C72
        mov dx,[bx+0x28]                                ; 2C75
        mov [bp-0x2c],ax                                ; 2C78
        mov [bp-0x2a],dx                                ; 2C7B
        les bx,[bp+0x6]                                 ; 2C7E
        mov ax,[es:bx+0x4]                              ; 2C81
        mov dx,[es:bx+0x6]                              ; 2C85
        mov [bp-0x4],ax                                 ; 2C89
        mov [bp-0x2],dx                                 ; 2C8C
        les bx,[bp+0x6]                                 ; 2C8F
        les bx,[es:bx+0x14]                             ; 2C92
        mov ax,[es:bx]                                  ; 2C96
        mov dx,[es:bx+0x2]                              ; 2C99
        mov [bp-0x8],ax                                 ; 2C9D
        mov [bp-0x6],dx                                 ; 2CA0
        cmp word [bp-0x4],byte +0x21                    ; 2CA3
        jz short L5_2CAC                                ; 2CA7
        jmp near L5_2CF4                                ; 2CA9

L5_2CAC:
        cmp word [bp-0x2],byte +0x0                     ; 2CAC
        jz short L5_2CB5                                ; 2CB0
        jmp near L5_2CF4                                ; 2CB2

L5_2CB5:
        mov ax,[bp-0x6]                                 ; 2CB5
        or ax,[bp-0x8]                                  ; 2CB8
        jnz short L5_2CC0                               ; 2CBB
        jmp near L5_2CC5                                ; 2CBD

L5_2CC0:
        mov al,0x1                                      ; 2CC0
        jmp near L5_2CC7                                ; 2CC2

L5_2CC5:
        mov al,0x0                                      ; 2CC5

L5_2CC7:
        mov bx,[bp+0xa]                                 ; 2CC7
        mov [bx+0x120],al                               ; 2CCA
        push word [bp+0xa]                              ; 2CCE
        mov ax,0x0                                      ; 2CD1
        push ax                                         ; 2CD4
        mov ax,0xffff                                   ; 2CD5
        push ax                                         ; 2CD8
        mov cl,0x3                                      ; 2CD9
        mov si,[bp-0x4]                                 ; 2CDB
        shl si,cl                                       ; 2CDE
        les bx,[bp-0x2c]                                ; 2CE0
        push word [es:bx+si+0x280c]                     ; 2CE3
        mov ax,0x1                                      ; 2CE8
        push ax                                         ; 2CEB
        callf L5_0538, R5_2CEF, R5_2D1E                 ; 2CEC far seg5
        jmp near L5_2D20                                ; 2CF1

L5_2CF4:
        push word [bp+0xa]                              ; 2CF4
        mov ax,0x0                                      ; 2CF7
        push ax                                         ; 2CFA
        mov si,[bp-0x4]                                 ; 2CFB
        shl si,1                                        ; 2CFE
        les bx,[bp-0x2c]                                ; 2D00
        push word [es:bx+si+0x265e]                     ; 2D03
        mov cl,0x3                                      ; 2D08
        mov si,[bp-0x4]                                 ; 2D0A
        shl si,cl                                       ; 2D0D
        les bx,[bp-0x2c]                                ; 2D0F
        push word [es:bx+si+0x280c]                     ; 2D12
        mov ax,0x11                                     ; 2D17
        push ax                                         ; 2D1A
        callf L5_0538, R5_2D1E, R5_2E41                 ; 2D1B far seg5

L5_2D20:
        mov cl,0x3                                      ; 2D20
        mov si,[bp-0x4]                                 ; 2D22
        shl si,cl                                       ; 2D25
        les bx,[bp-0x2c]                                ; 2D27
        sub_ ax,ax                                      ; 2D2A
        mov [es:bx+si+0x280e],ax                        ; 2D2C
        mov [es:bx+si+0x280c],ax                        ; 2D31
        mov ax,[bp-0x6]                                 ; 2D36
        or ax,[bp-0x8]                                  ; 2D39
        jnz short L5_2D41                               ; 2D3C
        jmp near L5_2ED9                                ; 2D3E

L5_2D41:
        cmp word [bp-0x4],byte +0x21                    ; 2D41
        jz short L5_2D4A                                ; 2D45
        jmp near L5_2D53                                ; 2D47

L5_2D4A:
        cmp word [bp-0x2],byte +0x0                     ; 2D4A
        jnz short L5_2D53                               ; 2D4E
        jmp near L5_2ED9                                ; 2D50

L5_2D53:
        sub_ ax,ax                                      ; 2D53
        mov [bp-0x26],ax                                ; 2D55
        mov [bp-0x28],ax                                ; 2D58
        sub_ ax,ax                                      ; 2D5B
        mov [bp-0x22],ax                                ; 2D5D
        mov [bp-0x24],ax                                ; 2D60
        cmp word [bp-0x4],byte +0x2d                    ; 2D63
        jz short L5_2D6C                                ; 2D67
        jmp near L5_2E48                                ; 2D69

L5_2D6C:
        cmp word [bp-0x2],byte +0x0                     ; 2D6C
        jz short L5_2D75                                ; 2D70
        jmp near L5_2E48                                ; 2D72

L5_2D75:
        mov byte [0xc6],0x0                             ; 2D75
        mov word [bp-0x1c],0x1                          ; 2D7A
        mov word [bp-0x1a],0x0                          ; 2D7F
        mov word [bp-0x20],0x18                         ; 2D84
        mov word [bp-0x1e],0x0                          ; 2D89
        mov word [bp-0x18],0x1                          ; 2D8E
        mov word [bp-0x16],0x0                          ; 2D93
        mov ax,0x94                                     ; 2D98
        mul word [bp-0x1c]                              ; 2D9B
        mov_ si,ax                                      ; 2D9E
        les bx,[bp-0x2c]                                ; 2DA0
        mov ax,[es:bx+si+0x926]                         ; 2DA3
        mov dx,[es:bx+si+0x928]                         ; 2DA8
        mov [bp-0x14],ax                                ; 2DAD
        mov [bp-0x12],dx                                ; 2DB0
        mov word [bp-0x10],0x4                          ; 2DB3
        mov word [bp-0xe],0x0                           ; 2DB8
        lea ax,[bp-0x54]                                ; 2DBD
        mov [bp-0xc],ax                                 ; 2DC0
        mov [bp-0xa],ss                                 ; 2DC3
        mov byte [bp-0x56],0x0                          ; 2DC6
        jmp near L5_2DD0                                ; 2DCA

L5_2DCD:
        inc byte [bp-0x56]                              ; 2DCD

L5_2DD0:
        mov al,[bp-0x56]                                ; 2DD0
        sub_ ah,ah                                      ; 2DD3
        cmp ax,[bp-0x14]                                ; 2DD5
        jc short L5_2DDD                                ; 2DD8
        jmp near L5_2E1B                                ; 2DDA

L5_2DDD:
        mov ax,0x1                                      ; 2DDD
        mov cl,[bp-0x56]                                ; 2DE0
        shl ax,cl                                       ; 2DE3
        cwd                                             ; 2DE5
        les bx,[bp-0x2c]                                ; 2DE6
        test [es:bx+0x2816],dx                          ; 2DE9
        jna short L5_2DF3                               ; 2DEE
        jmp near L5_2DFD                                ; 2DF0

L5_2DF3:
        test [es:bx+0x2814],ax                          ; 2DF3
        ja short L5_2DFD                                ; 2DF8
        jmp near L5_2E03                                ; 2DFA

L5_2DFD:
        mov ax,0x1                                      ; 2DFD
        jmp near L5_2E06                                ; 2E00

L5_2E03:
        mov ax,0x0                                      ; 2E03

L5_2E06:
        cwd                                             ; 2E06
        mov si,[bp-0x56]                                ; 2E07
        and si,0xff                                     ; 2E0A
        shl si,1                                        ; 2E0E
        shl si,1                                        ; 2E10
        mov [bp+si-0x54],ax                             ; 2E12
        mov [bp+si-0x52],dx                             ; 2E15
        jmp near L5_2DCD                                ; 2E18

L5_2E1B:
        sub_ ax,ax                                      ; 2E1B
        mov [bp-0x4e],ax                                ; 2E1D
        mov [bp-0x50],ax                                ; 2E20
        cmp byte [0xa71],0x0                            ; 2E23
        jz short L5_2E2D                                ; 2E28
        jmp near L5_2E48                                ; 2E2A

L5_2E2D:
        mov byte [0xa71],0x1                            ; 2E2D
        push word [bp+0xa]                              ; 2E32
        lea ax,[bp-0x20]                                ; 2E35
        push ss                                         ; 2E38
        push ax                                         ; 2E39
        sub_ ax,ax                                      ; 2E3A
        push ax                                         ; 2E3C
        push ax                                         ; 2E3D
        callf mxd_set_control_details, R5_2E41, R5_2FB9 ; 2E3E far seg5
        mov byte [0xa71],0x0                            ; 2E43

L5_2E48:
        cmp word [bp-0x4],byte +0x2e                    ; 2E48
        jz short L5_2E51                                ; 2E4C
        jmp near L5_2ED6                                ; 2E4E

L5_2E51:
        cmp word [bp-0x2],byte +0x0                     ; 2E51
        jz short L5_2E5A                                ; 2E55
        jmp near L5_2ED6                                ; 2E57

L5_2E5A:
        push word [bp+0xa]                              ; 2E5A
        mov bx,[bp+0xa]                                 ; 2E5D
        push word [bx]                                  ; 2E60
        mov ax,0x1                                      ; 2E62
        push ax                                         ; 2E65
        callf vxd_acquire, R5_2E69, R5_2ED4             ; 2E66 far seg4
        cmp ax,strict word 0x0                          ; 2E6B
        jz short L5_2E73                                ; 2E6E
        jmp near L5_2ED6                                ; 2E70

L5_2E73:
        push word [bp+0xa]                              ; 2E73
        mov al,0x7d                                     ; 2E76
        push ax                                         ; 2E78
        push word [bp+0xa]                              ; 2E79
        push ax                                         ; 2E7C
        callf mixer_read, R5_2E80, R5_2E88              ; 2E7D far seg1
        and al,0xf9                                     ; 2E82
        push ax                                         ; 2E84
        callf mixer_write, R5_2E88, R5_2EA1             ; 2E85 far seg1
        cmp byte [0xc7],0x0                             ; 2E8A
        jz short L5_2E94                                ; 2E8F
        jmp near L5_2EAE                                ; 2E91

L5_2E94:
        push word [bp+0xa]                              ; 2E94
        mov al,0x7d                                     ; 2E97
        push ax                                         ; 2E99
        push word [bp+0xa]                              ; 2E9A
        push ax                                         ; 2E9D
        callf mixer_read, R5_2EA1, R5_2EA9              ; 2E9E far seg1
        or al,0x8                                       ; 2EA3
        push ax                                         ; 2EA5
        callf mixer_write, R5_2EA9, R5_2EBB             ; 2EA6 far seg1
        jmp near L5_2EC5                                ; 2EAB

L5_2EAE:
        push word [bp+0xa]                              ; 2EAE
        mov al,0x7d                                     ; 2EB1
        push ax                                         ; 2EB3
        push word [bp+0xa]                              ; 2EB4
        push ax                                         ; 2EB7
        callf mixer_read, R5_2EBB, R5_2EC3              ; 2EB8 far seg1
        and al,0xf7                                     ; 2EBD
        push ax                                         ; 2EBF
        callf mixer_write, R5_2EC3, R5_2B3B             ; 2EC0 far seg1

L5_2EC5:
        push word [bp+0xa]                              ; 2EC5
        mov bx,[bp+0xa]                                 ; 2EC8
        push word [bx]                                  ; 2ECB
        mov ax,0x1                                      ; 2ECD
        push ax                                         ; 2ED0
        callf vxd_release, R5_2ED4, R5_28A5             ; 2ED1 far seg4

L5_2ED6:
        jmp near L5_30AB                                ; 2ED6

L5_2ED9:
        cmp word [bp-0x4],byte +0x2d                    ; 2ED9
        jz short L5_2EE2                                ; 2EDD
        jmp near L5_2FC0                                ; 2EDF

L5_2EE2:
        cmp word [bp-0x2],byte +0x0                     ; 2EE2
        jz short L5_2EEB                                ; 2EE6
        jmp near L5_2FC0                                ; 2EE8

L5_2EEB:
        mov byte [0xc6],0x1                             ; 2EEB
        mov word [bp-0x1c],0x1                          ; 2EF0
        mov word [bp-0x1a],0x0                          ; 2EF5
        mov word [bp-0x20],0x18                         ; 2EFA
        mov word [bp-0x1e],0x0                          ; 2EFF
        mov word [bp-0x18],0x1                          ; 2F04
        mov word [bp-0x16],0x0                          ; 2F09
        mov ax,0x94                                     ; 2F0E
        mul word [bp-0x1c]                              ; 2F11
        mov_ si,ax                                      ; 2F14
        les bx,[bp-0x2c]                                ; 2F16
        mov ax,[es:bx+si+0x926]                         ; 2F19
        mov dx,[es:bx+si+0x928]                         ; 2F1E
        mov [bp-0x14],ax                                ; 2F23
        mov [bp-0x12],dx                                ; 2F26
        mov word [bp-0x10],0x4                          ; 2F29
        mov word [bp-0xe],0x0                           ; 2F2E
        lea ax,[bp-0x54]                                ; 2F33
        mov [bp-0xc],ax                                 ; 2F36
        mov [bp-0xa],ss                                 ; 2F39
        mov byte [bp-0x56],0x0                          ; 2F3C
        jmp near L5_2F46                                ; 2F40

L5_2F43:
        inc byte [bp-0x56]                              ; 2F43

L5_2F46:
        mov al,[bp-0x56]                                ; 2F46
        sub_ ah,ah                                      ; 2F49
        cmp ax,[bp-0x14]                                ; 2F4B
        jc short L5_2F53                                ; 2F4E
        jmp near L5_2F91                                ; 2F50

L5_2F53:
        mov ax,0x1                                      ; 2F53
        mov cl,[bp-0x56]                                ; 2F56
        shl ax,cl                                       ; 2F59
        cwd                                             ; 2F5B
        les bx,[bp-0x2c]                                ; 2F5C
        test [es:bx+0x2816],dx                          ; 2F5F
        jna short L5_2F69                               ; 2F64
        jmp near L5_2F73                                ; 2F66

L5_2F69:
        test [es:bx+0x2814],ax                          ; 2F69
        ja short L5_2F73                                ; 2F6E
        jmp near L5_2F79                                ; 2F70

L5_2F73:
        mov ax,0x1                                      ; 2F73
        jmp near L5_2F7C                                ; 2F76

L5_2F79:
        mov ax,0x0                                      ; 2F79

L5_2F7C:
        cwd                                             ; 2F7C
        mov si,[bp-0x56]                                ; 2F7D
        and si,0xff                                     ; 2F80
        shl si,1                                        ; 2F84
        shl si,1                                        ; 2F86
        mov [bp+si-0x54],ax                             ; 2F88
        mov [bp+si-0x52],dx                             ; 2F8B
        jmp near L5_2F43                                ; 2F8E

L5_2F91:
        mov word [bp-0x50],0x1                          ; 2F91
        mov word [bp-0x4e],0x0                          ; 2F96
        cmp byte [0xa71],0x0                            ; 2F9B
        jz short L5_2FA5                                ; 2FA0
        jmp near L5_2FC0                                ; 2FA2

L5_2FA5:
        mov byte [0xa71],0x1                            ; 2FA5
        push word [bp+0xa]                              ; 2FAA
        lea ax,[bp-0x20]                                ; 2FAD
        push ss                                         ; 2FB0
        push ax                                         ; 2FB1
        sub_ ax,ax                                      ; 2FB2
        push ax                                         ; 2FB4
        push ax                                         ; 2FB5
        callf mxd_set_control_details, R5_2FB9, R5_2891 ; 2FB6 far seg5
        mov byte [0xa71],0x0                            ; 2FBB

L5_2FC0:
        cmp word [bp-0x4],byte +0x2d                    ; 2FC0
        jz short L5_2FC9                                ; 2FC4
        jmp near L5_2FFB                                ; 2FC6

L5_2FC9:
        cmp word [bp-0x2],byte +0x0                     ; 2FC9
        jz short L5_2FD2                                ; 2FCD
        jmp near L5_2FFB                                ; 2FCF

L5_2FD2:
        les bx,[bp-0x2c]                                ; 2FD2
        mov ax,[es:bx+0x2984]                           ; 2FD5
        mov dx,[es:bx+0x2986]                           ; 2FDA
        mov [bp-0x28],ax                                ; 2FDF
        mov [bp-0x26],dx                                ; 2FE2
        les bx,[bp-0x2c]                                ; 2FE5
        mov ax,[es:bx+0x2988]                           ; 2FE8
        mov dx,[es:bx+0x298a]                           ; 2FED
        mov [bp-0x24],ax                                ; 2FF2
        mov [bp-0x22],dx                                ; 2FF5
        jmp near L5_30AB                                ; 2FF8

L5_2FFB:
        cmp word [bp-0x4],byte +0x2e                    ; 2FFB
        jz short L5_3004                                ; 2FFF
        jmp near L5_3079                                ; 3001

L5_3004:
        cmp word [bp-0x2],byte +0x0                     ; 3004
        jz short L5_300D                                ; 3008
        jmp near L5_3079                                ; 300A

L5_300D:
        les bx,[bp-0x2c]                                ; 300D
        mov ax,[es:bx+0x298c]                           ; 3010
        mov dx,[es:bx+0x298e]                           ; 3015
        mov [bp-0x28],ax                                ; 301A
        mov [bp-0x26],dx                                ; 301D
        les bx,[bp-0x2c]                                ; 3020
        mov ax,[es:bx+0x2990]                           ; 3023
        mov dx,[es:bx+0x2992]                           ; 3028
        mov [bp-0x24],ax                                ; 302D
        mov [bp-0x22],dx                                ; 3030
        push word [bp+0xa]                              ; 3033
        mov bx,[bp+0xa]                                 ; 3036
        push word [bx]                                  ; 3039
        mov ax,0x1                                      ; 303B
        push ax                                         ; 303E
        callf vxd_acquire, R5_3042, R5_3074             ; 303F far seg4
        cmp ax,strict word 0x0                          ; 3044
        jz short L5_304C                                ; 3047
        jmp near L5_3076                                ; 3049

L5_304C:
        push word [bp+0xa]                              ; 304C
        mov al,0x7d                                     ; 304F
        push ax                                         ; 3051
        push word [bp+0xa]                              ; 3052
        push ax                                         ; 3055
        callf mixer_read, R5_3059, R5_3063              ; 3056 far seg1
        and al,0xf7                                     ; 305B
        or al,0x6                                       ; 305D
        push ax                                         ; 305F
        callf mixer_write, R5_3063, R5_2E80             ; 3060 far seg1
        push word [bp+0xa]                              ; 3065
        mov bx,[bp+0xa]                                 ; 3068
        push word [bx]                                  ; 306B
        mov ax,0x1                                      ; 306D
        push ax                                         ; 3070
        callf vxd_release, R5_3074, R5_2E69             ; 3071 far seg4

L5_3076:
        jmp near L5_30AB                                ; 3076

L5_3079:
        mov cl,0x3                                      ; 3079
        mov si,[bp-0x4]                                 ; 307B
        shl si,cl                                       ; 307E
        les bx,[bp-0x2c]                                ; 3080
        mov ax,[es:bx+si+0x275c]                        ; 3083
        mov dx,[es:bx+si+0x275e]                        ; 3088
        mov [bp-0x28],ax                                ; 308D
        mov [bp-0x26],dx                                ; 3090
        mov si,[bp-0x4]                                 ; 3093
        shl si,cl                                       ; 3096
        les bx,[bp-0x2c]                                ; 3098
        mov ax,[es:bx+si+0x2760]                        ; 309B
        mov dx,[es:bx+si+0x2762]                        ; 30A0
        mov [bp-0x24],ax                                ; 30A5
        mov [bp-0x22],dx                                ; 30A8

L5_30AB:
        cmp word [bp-0x4],byte +0x2d                    ; 30AB
        jz short L5_30B4                                ; 30AF
        jmp near L5_30CA                                ; 30B1

L5_30B4:
        cmp word [bp-0x2],byte +0x0                     ; 30B4
        jz short L5_30BD                                ; 30B8
        jmp near L5_30CA                                ; 30BA

L5_30BD:
        mov word [bp-0x1c],0x2f                         ; 30BD
        mov word [bp-0x1a],0x0                          ; 30C2
        jmp near L5_30FB                                ; 30C7

L5_30CA:
        cmp word [bp-0x4],byte +0x2e                    ; 30CA
        jz short L5_30D3                                ; 30CE
        jmp near L5_30E9                                ; 30D0

L5_30D3:
        cmp word [bp-0x2],byte +0x0                     ; 30D3
        jz short L5_30DC                                ; 30D7
        jmp near L5_30E9                                ; 30D9

L5_30DC:
        mov word [bp-0x1c],0x30                         ; 30DC
        mov word [bp-0x1a],0x0                          ; 30E1
        jmp near L5_30FB                                ; 30E6

L5_30E9:
        mov ax,[bp-0x4]                                 ; 30E9
        mov dx,[bp-0x2]                                 ; 30EC
        sub ax,strict word 0x16                         ; 30EF
        sbb dx,byte +0x0                                ; 30F2
        mov [bp-0x1c],ax                                ; 30F5
        mov [bp-0x1a],dx                                ; 30F8

L5_30FB:
        mov word [bp-0x20],0x18                         ; 30FB
        mov word [bp-0x1e],0x0                          ; 3100
        mov ax,0x6                                      ; 3105
        mul word [bp-0x1c]                              ; 3108
        mov_ si,ax                                      ; 310B
        les bx,[bp-0x2c]                                ; 310D
        mov ax,[es:bx+si+0x256e]                        ; 3110
        mov [bp-0x18],ax                                ; 3115
        mov word [bp-0x16],0x0                          ; 3118
        sub_ ax,ax                                      ; 311D
        mov [bp-0x12],ax                                ; 311F
        mov [bp-0x14],ax                                ; 3122
        mov word [bp-0x10],0x4                          ; 3125
        mov word [bp-0xe],0x0                           ; 312A
        lea ax,[bp-0x28]                                ; 312F
        mov [bp-0xc],ax                                 ; 3132
        mov [bp-0xa],ss                                 ; 3135
        push word [bp+0xa]                              ; 3138
        lea ax,[bp-0x20]                                ; 313B
        push ss                                         ; 313E
        push ax                                         ; 313F
        callf mixer_set_control, R5_3143, R5_32EF       ; 3140 far seg5
        mov ax,[bp-0x6]                                 ; 3145
        or ax,[bp-0x8]                                  ; 3148
        jnz short L5_3150                               ; 314B
        jmp near L5_3168                                ; 314D

L5_3150:
        mov cl,0x3                                      ; 3150
        mov si,[bp-0x4]                                 ; 3152
        shl si,cl                                       ; 3155
        les bx,[bp-0x2c]                                ; 3157
        mov word [es:bx+si+0x280c],0x1                  ; 315A
        mov word [es:bx+si+0x280e],0x0                  ; 3161

L5_3168:
        mov ax,0x0                                      ; 3168
        jmp near L5_316E                                ; 316B

L5_316E:
        pop si                                          ; 316E
        pop di                                          ; 316F
        mov_ sp,bp                                      ; 3170
        pop bp                                          ; 3172
        retf 0x6                                        ; 3173
        push bp                                         ; 3176
        mov_ bp,sp                                      ; 3177
        sub sp,strict word 0x12                         ; 3179
        push di                                         ; 317D
        push si                                         ; 317E
        mov bx,[bp+0xa]                                 ; 317F
        mov ax,[bx+0x26]                                ; 3182
        mov dx,[bx+0x28]                                ; 3185
        mov [bp-0x12],ax                                ; 3188
        mov [bp-0x10],dx                                ; 318B
        les bx,[bp+0x6]                                 ; 318E
        mov ax,[es:bx+0x4]                              ; 3191
        mov dx,[es:bx+0x6]                              ; 3195
        mov [bp-0xa],ax                                 ; 3199
        mov [bp-0x8],dx                                 ; 319C
        mov bx,[bp+0xa]                                 ; 319F
        mov ax,[bx+0x67]                                ; 31A2
        or ax,[bx+0x65]                                 ; 31A5
        jz short L5_31AD                                ; 31A8
        jmp near L5_31B3                                ; 31AA

L5_31AD:
        mov ax,0x0                                      ; 31AD
        jmp near L5_32F4                                ; 31B0

L5_31B3:
        mov bx,[bp+0xa]                                 ; 31B3
        mov al,[bx+0x5d]                                ; 31B6
        and al,0x3                                      ; 31B9
        cmp al,0x2                                      ; 31BB
        jnz short L5_31C2                               ; 31BD
        jmp near L5_31C8                                ; 31BF

L5_31C2:
        mov ax,0x0                                      ; 31C2
        jmp near L5_32F4                                ; 31C5

L5_31C8:
        mov bx,[bp+0xa]                                 ; 31C8
        mov ax,[bx+0x78]                                ; 31CB
        mov dx,[bx+0x7a]                                ; 31CE
        mov bx,[bp+0xa]                                 ; 31D1
        cmp [bx+0x65],ax                                ; 31D4
        jz short L5_31DC                                ; 31D7
        jmp near L5_31E4                                ; 31D9

L5_31DC:
        cmp [bx+0x67],dx                                ; 31DC
        jnz short L5_31E4                               ; 31DF
        jmp near L5_31F6                                ; 31E1

L5_31E4:
        mov word [bp-0x6],0x1                           ; 31E4
        mov word [bp-0xe],0x1                           ; 31E9
        mov word [bp-0xc],0x0                           ; 31EE
        jmp near L5_3205                                ; 31F3

L5_31F6:
        mov word [bp-0x6],0x2                           ; 31F6
        mov word [bp-0xe],0x2                           ; 31FB
        mov word [bp-0xc],0x0                           ; 3200

L5_3205:
        mov word [bp-0x2],0x0                           ; 3205
        mov word [bp-0x4],0x0                           ; 320A
        jmp near L5_3215                                ; 320F

L5_3212:
        inc word [bp-0x4]                               ; 3212

L5_3215:
        mov ax,0x94                                     ; 3215
        mul word [bp-0xe]                               ; 3218
        mov_ si,ax                                      ; 321B
        les bx,[bp-0x12]                                ; 321D
        mov ax,[bp-0x4]                                 ; 3220
        sub_ dx,dx                                      ; 3223
        cmp [es:bx+si+0x928],dx                         ; 3225
        jnc short L5_322F                               ; 322A
        jmp near L5_32AE                                ; 322C

L5_322F:
        jna short L5_3234                               ; 322F
        jmp near L5_323E                                ; 3231

L5_3234:
        cmp [es:bx+si+0x926],ax                         ; 3234
        ja short L5_323E                                ; 3239
        jmp near L5_32AE                                ; 323B

L5_323E:
        mov ax,0x1                                      ; 323E
        mov cl,[bp-0x4]                                 ; 3241
        shl ax,cl                                       ; 3244
        cwd                                             ; 3246
        mov cl,0x3                                      ; 3247
        mov si,[bp-0xe]                                 ; 3249
        shl si,cl                                       ; 324C
        les bx,[bp-0x12]                                ; 324E
        and ax,[es:bx+si+0x280c]                        ; 3251
        and dx,[es:bx+si+0x280e]                        ; 3256
        or_ dx,ax                                       ; 325B
        jnz short L5_3262                               ; 325D
        jmp near L5_32AB                                ; 325F

L5_3262:
        mov ax,0x6                                      ; 3262
        mul word [bp-0xa]                               ; 3265
        mov_ si,ax                                      ; 3268
        les bx,[bp-0x12]                                ; 326A
        mov ax,[bp-0x6]                                 ; 326D
        cmp [es:bx+si+0x256a],ax                        ; 3270
        jz short L5_327A                                ; 3275
        jmp near L5_32AB                                ; 3277

L5_327A:
        mov ax,0x6                                      ; 327A
        mul word [bp-0xa]                               ; 327D
        mov_ si,ax                                      ; 3280
        les bx,[bp-0x12]                                ; 3282
        mov ax,[es:bx+si+0x256c]                        ; 3285
        mov_ cx,ax                                      ; 328A
        mov ax,0xa                                      ; 328C
        mul word [bp-0x6]                               ; 328F
        mov_ si,ax                                      ; 3292
        add si,[bp-0x4]                                 ; 3294
        shl si,1                                        ; 3297
        les bx,[bp-0x12]                                ; 3299
        cmp [es:bx+si+0x26e0],cx                        ; 329C
        jz short L5_32A6                                ; 32A1
        jmp near L5_32AB                                ; 32A3

L5_32A6:
        mov word [bp-0x2],0x1                           ; 32A6

L5_32AB:
        jmp near L5_3212                                ; 32AB

L5_32AE:
        cmp word [bp-0x2],byte +0x0                     ; 32AE
        jz short L5_32B7                                ; 32B2
        jmp near L5_32E9                                ; 32B4

L5_32B7:
        cmp word [bp-0xe],byte +0x1                     ; 32B7
        jz short L5_32C0                                ; 32BB
        jmp near L5_32CF                                ; 32BD

L5_32C0:
        cmp word [bp-0xc],byte +0x0                     ; 32C0
        jz short L5_32C9                                ; 32C4
        jmp near L5_32CF                                ; 32C6

L5_32C9:
        mov ax,0x12                                     ; 32C9
        jmp near L5_32D2                                ; 32CC

L5_32CF:
        mov ax,0x18                                     ; 32CF

L5_32D2:
        cwd                                             ; 32D2
        cmp ax,[bp-0xa]                                 ; 32D3
        jz short L5_32DB                                ; 32D6
        jmp near L5_32E3                                ; 32D8

L5_32DB:
        cmp dx,[bp-0x8]                                 ; 32DB
        jnz short L5_32E3                               ; 32DE
        jmp near L5_32E9                                ; 32E0

L5_32E3:
        mov ax,0x0                                      ; 32E3
        jmp near L5_32F4                                ; 32E6

L5_32E9:
        push word [bp+0xa]                              ; 32E9
        callf L5_365B, R5_32EF, R5_3336                 ; 32EC far seg5
        jmp near L5_32F4                                ; 32F1

L5_32F4:
        pop si                                          ; 32F4
        pop di                                          ; 32F5
        mov_ sp,bp                                      ; 32F6
        pop bp                                          ; 32F8
        retf 0x6                                        ; 32F9
        push bp                                         ; 32FC
        mov_ bp,sp                                      ; 32FD
        sub sp,strict word 0x24                         ; 32FF
        push di                                         ; 3303
        push si                                         ; 3304
        mov bx,[bp+0xa]                                 ; 3305
        mov ax,[bx+0x26]                                ; 3308
        mov dx,[bx+0x28]                                ; 330B
        mov [bp-0x8],ax                                 ; 330E
        mov [bp-0x6],dx                                 ; 3311
        les bx,[bp+0x6]                                 ; 3314
        mov ax,[es:bx+0x4]                              ; 3317
        mov dx,[es:bx+0x6]                              ; 331B
        mov [bp-0x4],ax                                 ; 331F
        mov [bp-0x2],dx                                 ; 3322
        mov ax,[bp-0x2]                                 ; 3325
        or ax,[bp-0x4]                                  ; 3328
        jz short L5_3330                                ; 332B
        jmp near L5_333E                                ; 332D

L5_3330:
        push word [bp+0xa]                              ; 3330
        callf L5_341A, R5_3336, R5_2CEF                 ; 3333 far seg5
        mov ax,0x0                                      ; 3338
        jmp near L5_3412                                ; 333B

L5_333E:
        cmp word [bp-0x4],byte +0x1                     ; 333E
        jz short L5_3347                                ; 3342
        jmp near L5_33DB                                ; 3344

L5_3347:
        cmp word [bp-0x2],byte +0x0                     ; 3347
        jz short L5_3350                                ; 334B
        jmp near L5_33DB                                ; 334D

L5_3350:
        les bx,[bp-0x8]                                 ; 3350
        test byte [es:bx+0x2814],0x2                    ; 3353
        jnz short L5_335E                               ; 3359
        jmp near L5_336E                                ; 335B

L5_335E:
        sub_ ax,ax                                      ; 335E
        mov [bp-0xa],ax                                 ; 3360
        mov [bp-0xc],ax                                 ; 3363
        mov byte [0xc6],0x1                             ; 3366
        jmp near L5_337D                                ; 336B

L5_336E:
        mov word [bp-0xc],0x1                           ; 336E
        mov word [bp-0xa],0x0                           ; 3373
        mov byte [0xc6],0x0                             ; 3378

L5_337D:
        mov word [bp-0x20],0x2d                         ; 337D
        mov word [bp-0x1e],0x0                          ; 3382
        mov word [bp-0x24],0x18                         ; 3387
        mov word [bp-0x22],0x0                          ; 338C
        mov word [bp-0x1c],0x1                          ; 3391
        mov word [bp-0x1a],0x0                          ; 3396
        sub_ ax,ax                                      ; 339B
        mov [bp-0x16],ax                                ; 339D
        mov [bp-0x18],ax                                ; 33A0
        mov word [bp-0x14],0x4                          ; 33A3
        mov word [bp-0x12],0x0                          ; 33A8
        lea ax,[bp-0xc]                                 ; 33AD
        mov [bp-0x10],ax                                ; 33B0
        mov [bp-0xe],ss                                 ; 33B3
        cmp byte [0xa72],0x0                            ; 33B6
        jz short L5_33C0                                ; 33BB
        jmp near L5_33DB                                ; 33BD

L5_33C0:
        mov byte [0xa72],0x1                            ; 33C0
        push word [bp+0xa]                              ; 33C5
        lea ax,[bp-0x24]                                ; 33C8
        push ss                                         ; 33CB
        push ax                                         ; 33CC
        sub_ ax,ax                                      ; 33CD
        push ax                                         ; 33CF
        push ax                                         ; 33D0
        callf mxd_set_control_details, R5_33D4, R5_3402 ; 33D1 far seg5
        mov byte [0xa72],0x0                            ; 33D6

L5_33DB:
        mov bx,[bp+0xa]                                 ; 33DB
        cmp word [bx+0x74],byte +0x0                    ; 33DE
        jnz short L5_33E7                               ; 33E2
        jmp near L5_33F6                                ; 33E4

L5_33E7:
        mov bx,[bp+0xa]                                 ; 33E7
        mov al,[bx+0x5d]                                ; 33EA
        and al,0x3                                      ; 33ED
        cmp al,0x2                                      ; 33EF
        jnz short L5_33F6                               ; 33F1
        jmp near L5_33FC                                ; 33F3

L5_33F6:
        mov ax,0x0                                      ; 33F6
        jmp near L5_3412                                ; 33F9

L5_33FC:
        push word [bp+0xa]                              ; 33FC
        callf L5_35AC, R5_3402, R5_340A                 ; 33FF far seg5
        push word [bp+0xa]                              ; 3404
        callf L5_365B, R5_340A, R5_356C                 ; 3407 far seg5
        mov ax,0x0                                      ; 340C
        jmp near L5_3412                                ; 340F

L5_3412:
        pop si                                          ; 3412
        pop di                                          ; 3413
        mov_ sp,bp                                      ; 3414
        pop bp                                          ; 3416
        retf 0x6                                        ; 3417

L5_341A:
        push bp                                         ; 341A
        mov_ bp,sp                                      ; 341B
        sub sp,strict word 0x2e                         ; 341D
        push di                                         ; 3421
        push si                                         ; 3422
        mov bx,[bp+0x6]                                 ; 3423
        mov ax,[bx+0x26]                                ; 3426
        mov dx,[bx+0x28]                                ; 3429
        mov [bp-0x2e],ax                                ; 342C
        mov [bp-0x2c],dx                                ; 342F
        les bx,[bp-0x2e]                                ; 3432
        mov ax,[es:bx+0x280c]                           ; 3435
        mov dx,[es:bx+0x280e]                           ; 343A
        mov [bp-0x4],ax                                 ; 343F
        mov [bp-0x2],dx                                 ; 3442
        les bx,[bp-0x2e]                                ; 3445
        mov word [es:bx+0x280c],0xffff                  ; 3448
        mov word [es:bx+0x280e],0xffff                  ; 344F
        mov word [bp-0xa],0x0                           ; 3456
        jmp near L5_3461                                ; 345B

L5_345E:
        inc word [bp-0xa]                               ; 345E

L5_3461:
        mov ax,[bp-0xa]                                 ; 3461
        sub_ dx,dx                                      ; 3464
        les bx,[bp-0x2e]                                ; 3466
        cmp [es:bx+0x928],dx                            ; 3469
        jnc short L5_3473                               ; 346E
        jmp near L5_358B                                ; 3470

L5_3473:
        jna short L5_3478                               ; 3473
        jmp near L5_3482                                ; 3475

L5_3478:
        cmp [es:bx+0x926],ax                            ; 3478
        ja short L5_3482                                ; 347D
        jmp near L5_358B                                ; 347F

L5_3482:
        mov cl,0x3                                      ; 3482
        mov si,[bp-0xa]                                 ; 3484
        shl si,cl                                       ; 3487
        les bx,[bp-0x2e]                                ; 3489
        mov ax,[es:bx+si+0x28d4]                        ; 348C
        mov dx,[es:bx+si+0x28d6]                        ; 3491
        mov [bp-0x8],ax                                 ; 3496
        mov [bp-0x6],dx                                 ; 3499
        mov si,[bp-0xa]                                 ; 349C
        shl si,cl                                       ; 349F
        les bx,[bp-0x2e]                                ; 34A1
        sub_ ax,ax                                      ; 34A4
        mov [es:bx+si+0x28d6],ax                        ; 34A6
        mov [es:bx+si+0x28d4],ax                        ; 34AB
        mov ax,0x1                                      ; 34B0
        mov cl,[bp-0xa]                                 ; 34B3
        shl ax,cl                                       ; 34B6
        cwd                                             ; 34B8
        and ax,[bp-0x4]                                 ; 34B9
        and dx,[bp-0x2]                                 ; 34BC
        or_ dx,ax                                       ; 34BF
        jnz short L5_34C6                               ; 34C1
        jmp near L5_34D1                                ; 34C3

L5_34C6:
        mov ax,[bp-0x6]                                 ; 34C6
        or ax,[bp-0x8]                                  ; 34C9
        jnz short L5_34D1                               ; 34CC
        jmp near L5_34E4                                ; 34CE

L5_34D1:
        sub_ ax,ax                                      ; 34D1
        mov [bp-0x28],ax                                ; 34D3
        mov [bp-0x2a],ax                                ; 34D6
        sub_ ax,ax                                      ; 34D9
        mov [bp-0x24],ax                                ; 34DB
        mov [bp-0x26],ax                                ; 34DE
        jmp near L5_3516                                ; 34E1

L5_34E4:
        mov cl,0x3                                      ; 34E4
        mov si,[bp-0xa]                                 ; 34E6
        shl si,cl                                       ; 34E9
        les bx,[bp-0x2e]                                ; 34EB
        mov ax,[es:bx+si+0x2824]                        ; 34EE
        mov dx,[es:bx+si+0x2826]                        ; 34F3
        mov [bp-0x2a],ax                                ; 34F8
        mov [bp-0x28],dx                                ; 34FB
        mov si,[bp-0xa]                                 ; 34FE
        shl si,cl                                       ; 3501
        les bx,[bp-0x2e]                                ; 3503
        mov ax,[es:bx+si+0x2828]                        ; 3506
        mov dx,[es:bx+si+0x282a]                        ; 350B
        mov [bp-0x26],ax                                ; 3510
        mov [bp-0x24],dx                                ; 3513

L5_3516:
        mov ax,[bp-0xa]                                 ; 3516
        add ax,strict word 0x3                          ; 3519
        mov [bp-0x1e],ax                                ; 351C
        mov word [bp-0x1c],0x0                          ; 351F
        mov word [bp-0x22],0x18                         ; 3524
        mov word [bp-0x20],0x0                          ; 3529
        mov ax,0x6                                      ; 352E
        mul word [bp-0x1e]                              ; 3531
        mov_ si,ax                                      ; 3534
        les bx,[bp-0x2e]                                ; 3536
        mov ax,[es:bx+si+0x256e]                        ; 3539
        mov [bp-0x1a],ax                                ; 353E
        mov word [bp-0x18],0x0                          ; 3541
        sub_ ax,ax                                      ; 3546
        mov [bp-0x14],ax                                ; 3548
        mov [bp-0x16],ax                                ; 354B
        mov word [bp-0x12],0x4                          ; 354E
        mov word [bp-0x10],0x0                          ; 3553
        lea ax,[bp-0x2a]                                ; 3558
        mov [bp-0xe],ax                                 ; 355B
        mov [bp-0xc],ss                                 ; 355E
        push word [bp+0x6]                              ; 3561
        lea ax,[bp-0x22]                                ; 3564
        push ss                                         ; 3567
        push ax                                         ; 3568
        callf mixer_set_control, R5_356C, R5_3143       ; 3569 far seg5
        mov ax,[bp-0x8]                                 ; 356E
        mov dx,[bp-0x6]                                 ; 3571
        mov cl,0x3                                      ; 3574
        mov si,[bp-0xa]                                 ; 3576
        shl si,cl                                       ; 3579
        les bx,[bp-0x2e]                                ; 357B
        mov [es:bx+si+0x28d4],ax                        ; 357E
        mov [es:bx+si+0x28d6],dx                        ; 3583
        jmp near L5_345E                                ; 3588

L5_358B:
        mov ax,[bp-0x4]                                 ; 358B
        mov dx,[bp-0x2]                                 ; 358E
        les bx,[bp-0x2e]                                ; 3591
        mov [es:bx+0x280c],ax                           ; 3594
        mov [es:bx+0x280e],dx                           ; 3599
        mov ax,0x0                                      ; 359E
        jmp near L5_35A4                                ; 35A1

L5_35A4:
        pop si                                          ; 35A4
        pop di                                          ; 35A5
        mov_ sp,bp                                      ; 35A6
        pop bp                                          ; 35A8
        retf 0x2                                        ; 35A9

L5_35AC:
        push bp                                         ; 35AC
        mov_ bp,sp                                      ; 35AD
        sub sp,strict word 0x4                          ; 35AF
        push di                                         ; 35B3
        push si                                         ; 35B4
        mov bx,[bp+0x6]                                 ; 35B5
        mov ax,[bx+0x67]                                ; 35B8
        or ax,[bx+0x65]                                 ; 35BB
        jnz short L5_35C3                               ; 35BE
        jmp near L5_35D2                                ; 35C0

L5_35C3:
        mov bx,[bp+0x6]                                 ; 35C3
        mov al,[bx+0x5d]                                ; 35C6
        and al,0x3                                      ; 35C9
        cmp al,0x2                                      ; 35CB
        jnz short L5_35D2                               ; 35CD
        jmp near L5_35D8                                ; 35CF

L5_35D2:
        mov ax,0x0                                      ; 35D2
        jmp near L5_3653                                ; 35D5

L5_35D8:
        mov byte [bp-0x2],0x5                           ; 35D8
        push word [bp+0x6]                              ; 35DC
        mov bx,[bp+0x6]                                 ; 35DF
        push word [bx]                                  ; 35E2
        mov ax,0x1                                      ; 35E4
        push ax                                         ; 35E7
        callf vxd_acquire, R5_35EB, R5_364B             ; 35E8 far seg4
        cmp ax,strict word 0x0                          ; 35ED
        jz short L5_35F5                                ; 35F0
        jmp near L5_364D                                ; 35F2

L5_35F5:
        mov ax,[0xa6]                                   ; 35F5
        inc word [0xa6]                                 ; 35F8
        cmp ax,strict word 0x0                          ; 35FC
        jz short L5_3604                                ; 35FF
        jmp near L5_3605                                ; 3601

L5_3604:
        cli                                             ; 3604

L5_3605:
        push word [bp+0x6]                              ; 3605
        mov al,0x1c                                     ; 3608
        push ax                                         ; 360A
        callf mixer_read, R5_360E, R5_362B              ; 360B far seg1
        mov [bp-0x4],al                                 ; 3610
        mov al,[bp-0x4]                                 ; 3613
        and al,0xf8                                     ; 3616
        or al,[bp-0x2]                                  ; 3618
        mov [bp-0x4],al                                 ; 361B
        push word [bp+0x6]                              ; 361E
        mov al,0x1c                                     ; 3621
        push ax                                         ; 3623
        mov al,[bp-0x4]                                 ; 3624
        push ax                                         ; 3627
        callf mixer_write, R5_362B, R5_3059             ; 3628 far seg1
        dec word [0xa6]                                 ; 362D
        cmp word [0xa6],byte +0x0                       ; 3631
        jz short L5_363B                                ; 3636
        jmp near L5_363C                                ; 3638

L5_363B:
        sti                                             ; 363B

L5_363C:
        push word [bp+0x6]                              ; 363C
        mov bx,[bp+0x6]                                 ; 363F
        push word [bx]                                  ; 3642
        mov ax,0x1                                      ; 3644
        push ax                                         ; 3647
        callf vxd_release, R5_364B, R5_3042             ; 3648 far seg4

L5_364D:
        mov ax,0x0                                      ; 364D
        jmp near L5_3653                                ; 3650

L5_3653:
        pop si                                          ; 3653
        pop di                                          ; 3654
        mov_ sp,bp                                      ; 3655
        pop bp                                          ; 3657
        retf 0x2                                        ; 3658

L5_365B:
        push bp                                         ; 365B
        mov_ bp,sp                                      ; 365C
        sub sp,byte +0x34                               ; 365E
        push di                                         ; 3661
        push si                                         ; 3662
        mov si,[bp+0x6]                                 ; 3663
        mov ax,[si+0x26]                                ; 3666
        mov dx,[si+0x28]                                ; 3669
        mov_ di,ax                                      ; 366C
        mov [bp-0xe],dx                                 ; 366E
        mov ax,[si+0x67]                                ; 3671
        or ax,[si+0x65]                                 ; 3674
        jz short L5_3682                                ; 3677
        mov al,[si+0x5d]                                ; 3679
        and al,0x3                                      ; 367C
        cmp al,0x2                                      ; 367E
        jz short L5_3685                                ; 3680

L5_3682:
        jmp near L5_38E3                                ; 3682

L5_3685:
        mov ax,[si+0x78]                                ; 3685
        mov dx,[si+0x7a]                                ; 3688
        cmp [si+0x65],ax                                ; 368B
        jnz short L5_3695                               ; 368E
        cmp [si+0x67],dx                                ; 3690
        jz short L5_369C                                ; 3693

L5_3695:
        mov word [bp-0xa],0x1                           ; 3695
        jmp short L5_36A1                               ; 369A

L5_369C:
        mov word [bp-0xa],0x2                           ; 369C

L5_36A1:
        mov word [bp-0x8],0x0                           ; 36A1
        mov cl,0x3                                      ; 36A6
        mov bx,[bp-0xa]                                 ; 36A8
        shl bx,cl                                       ; 36AB
        mov ax,[bp-0xe]                                 ; 36AD
        add_ bx,di                                      ; 36B0
        mov es,ax                                       ; 36B2
        mov ax,[es:bx+0x280c]                           ; 36B4
        mov dx,[es:bx+0x280e]                           ; 36B9
        mov [bp-0x14],ax                                ; 36BE
        mov [bp-0x12],dx                                ; 36C1
        mov word [es:bx+0x280c],0xffff                  ; 36C4
        mov word [es:bx+0x280e],0xffff                  ; 36CB
        mov ax,[si+0x78]                                ; 36D2
        mov dx,[si+0x7a]                                ; 36D5
        cmp [si+0x65],ax                                ; 36D8
        jnz short L5_36E2                               ; 36DB
        cmp [si+0x67],dx                                ; 36DD
        jz short L5_36F3                                ; 36E0

L5_36E2:
        mov word [bp-0xa],0xc                           ; 36E2
        mov word [bp-0x8],0x0                           ; 36E7
        mov word [bp-0x6],0x1                           ; 36EC
        jmp short L5_3702                               ; 36F1

L5_36F3:
        mov word [bp-0xa],0x13                          ; 36F3
        mov word [bp-0x8],0x0                           ; 36F8
        mov word [bp-0x6],0x2                           ; 36FD

L5_3702:
        mov ax,0x94                                     ; 3702
        mul word [bp-0x6]                               ; 3705
        mov_ bx,ax                                      ; 3708
        add_ bx,di                                      ; 370A
        mov ax,[es:bx+0x926]                            ; 370C
        mov [bp-0xc],ax                                 ; 3711
        mov word [bp-0x6],0x0                           ; 3714
        or_ ax,ax                                       ; 3719
        jnz short L5_3720                               ; 371B
        jmp near L5_37E6                                ; 371D

L5_3720:
        mov cl,0x3                                      ; 3720
        mov ax,[bp-0xa]                                 ; 3722
        shl ax,cl                                       ; 3725
        add_ ax,di                                      ; 3727
        add ax,0x280c                                   ; 3729
        mov [bp-0x4],ax                                 ; 372C
        mov [bp-0x2],es                                 ; 372F
        mov [bp-0x10],di                                ; 3732
        mov_ di,ax                                      ; 3735
        mov si,[bp-0x6]                                 ; 3737

L5_373A:
        mov ax,0x1                                      ; 373A
        mov_ cx,si                                      ; 373D
        shl ax,cl                                       ; 373F
        cwd                                             ; 3741
        and ax,[bp-0x14]                                ; 3742
        and dx,[bp-0x12]                                ; 3745
        or_ dx,ax                                       ; 3748
        jnz short L5_375C                               ; 374A
        sub_ ax,ax                                      ; 374C
        mov [bp-0x1a],ax                                ; 374E
        mov [bp-0x1c],ax                                ; 3751
        mov [bp-0x16],ax                                ; 3754
        mov [bp-0x18],ax                                ; 3757
        jmp short L5_377A                               ; 375A

L5_375C:
        mov es,[bp-0x2]                                 ; 375C
        mov ax,[es:di]                                  ; 375F
        mov dx,[es:di+0x2]                              ; 3762
        mov [bp-0x1c],ax                                ; 3766
        mov [bp-0x1a],dx                                ; 3769
        mov ax,[es:di+0x4]                              ; 376C
        mov dx,[es:di+0x6]                              ; 3770
        mov [bp-0x18],ax                                ; 3774
        mov [bp-0x16],dx                                ; 3777

L5_377A:
        mov_ ax,si                                      ; 377A
        sub_ dx,dx                                      ; 377C
        add ax,[bp-0xa]                                 ; 377E
        adc dx,[bp-0x8]                                 ; 3781
        mov [bp-0x30],ax                                ; 3784
        mov [bp-0x2e],dx                                ; 3787
        mov word [bp-0x34],0x18                         ; 378A
        mov word [bp-0x32],0x0                          ; 378F
        mov ax,0x6                                      ; 3794
        mul word [bp-0x30]                              ; 3797
        mov_ bx,ax                                      ; 379A
        add bx,[bp-0x10]                                ; 379C
        mov es,[bp-0xe]                                 ; 379F
        mov ax,[es:bx+0x256e]                           ; 37A2
        mov [bp-0x2c],ax                                ; 37A7
        mov word [bp-0x2a],0x0                          ; 37AA
        sub_ ax,ax                                      ; 37AF
        mov [bp-0x26],ax                                ; 37B1
        mov [bp-0x28],ax                                ; 37B4
        mov word [bp-0x24],0x4                          ; 37B7
        mov [bp-0x22],ax                                ; 37BC
        lea ax,[bp-0x1c]                                ; 37BF
        mov [bp-0x20],ax                                ; 37C2
        mov [bp-0x1e],ss                                ; 37C5
        push word [bp+0x6]                              ; 37C8
        lea ax,[bp-0x34]                                ; 37CB
        push ss                                         ; 37CE
        push ax                                         ; 37CF
        push cs                                         ; 37D0
        call mixer_set_control                          ; 37D1
        add di,byte +0x8                                ; 37D4
        inc si                                          ; 37D7
        cmp [bp-0xc],si                                 ; 37D8
        jna short L5_37E0                               ; 37DB
        jmp near L5_373A                                ; 37DD

L5_37E0:
        mov si,[bp+0x6]                                 ; 37E0
        mov di,[bp-0x10]                                ; 37E3

L5_37E6:
        mov ax,[si+0x78]                                ; 37E6
        mov dx,[si+0x7a]                                ; 37E9
        cmp [si+0x65],ax                                ; 37EC
        jnz short L5_37F6                               ; 37EF
        cmp [si+0x67],dx                                ; 37F1
        jz short L5_37FD                                ; 37F4

L5_37F6:
        mov word [bp-0xa],0x1                           ; 37F6
        jmp short L5_3802                               ; 37FB

L5_37FD:
        mov word [bp-0xa],0x2                           ; 37FD

L5_3802:
        mov word [bp-0x8],0x0                           ; 3802
        mov cl,0x3                                      ; 3807
        mov bx,[bp-0xa]                                 ; 3809
        shl bx,cl                                       ; 380C
        mov ax,[bp-0xe]                                 ; 380E
        mov es,ax                                       ; 3811
        add_ bx,di                                      ; 3813
        mov ax,[bp-0x14]                                ; 3815
        mov dx,[bp-0x12]                                ; 3818
        mov [es:bx+0x280c],ax                           ; 381B
        mov [es:bx+0x280e],dx                           ; 3820
        mov ax,[si+0x78]                                ; 3825
        mov dx,[si+0x7a]                                ; 3828
        cmp [si+0x65],ax                                ; 382B
        jnz short L5_3835                               ; 382E
        cmp [si+0x67],dx                                ; 3830
        jz short L5_383C                                ; 3833

L5_3835:
        mov word [bp-0xa],0x12                          ; 3835
        jmp short L5_3841                               ; 383A

L5_383C:
        mov word [bp-0xa],0x18                          ; 383C

L5_3841:
        mov word [bp-0x8],0x0                           ; 3841
        mov word [bp-0x30],0x12                         ; 3846
        mov word [bp-0x2e],0x0                          ; 384B
        mov word [bp-0x34],0x18                         ; 3850
        mov word [bp-0x32],0x0                          ; 3855
        mov ax,0x6                                      ; 385A
        mul word [bp-0x30]                              ; 385D
        mov_ bx,ax                                      ; 3860
        add_ bx,di                                      ; 3862
        mov cx,[es:bx+0x256e]                           ; 3864
        mov [bp-0x2c],cx                                ; 3869
        mov word [bp-0x2a],0x0                          ; 386C
        sub_ cx,cx                                      ; 3871
        mov [bp-0x26],cx                                ; 3873
        mov [bp-0x28],cx                                ; 3876
        mov word [bp-0x24],0x4                          ; 3879
        mov [bp-0x22],cx                                ; 387E
        mov cl,0x3                                      ; 3881
        mov dx,[bp-0xa]                                 ; 3883
        shl dx,cl                                       ; 3886
        add_ dx,di                                      ; 3888
        add dx,0x280c                                   ; 388A
        mov [bp-0x20],dx                                ; 388E
        mov [bp-0x1e],es                                ; 3891
        push si                                         ; 3894
        lea ax,[bp-0x34]                                ; 3895
        push ss                                         ; 3898
        push ax                                         ; 3899
        push cs                                         ; 389A
        call mixer_set_control                          ; 389B
        push si                                         ; 389E
        push word [si]                                  ; 389F
        mov ax,0x1                                      ; 38A1
        push ax                                         ; 38A4
        callf vxd_acquire, R5_38A8, R5_38E1             ; 38A5 far seg4
        or_ ax,ax                                       ; 38AA
        jnz short L5_38E3                               ; 38AC
        cmp byte [0xc7],0x1                             ; 38AE
        jnz short L5_38C4                               ; 38B3
        push si                                         ; 38B5
        mov al,0x7d                                     ; 38B6
        push ax                                         ; 38B8
        push si                                         ; 38B9
        push ax                                         ; 38BA
        callf mixer_read, R5_38BE, R5_38CD              ; 38BB far seg1
        and al,0xf7                                     ; 38C0
        jmp short L5_38D1                               ; 38C2

L5_38C4:
        push si                                         ; 38C4
        mov al,0x7d                                     ; 38C5
        push ax                                         ; 38C7
        push si                                         ; 38C8
        push ax                                         ; 38C9
        callf mixer_read, R5_38CD, R5_38D5              ; 38CA far seg1
        or al,0x8                                       ; 38CF

L5_38D1:
        push ax                                         ; 38D1
        callf mixer_write, R5_38D5, R5_3954             ; 38D2 far seg1
        push si                                         ; 38D7
        push word [si]                                  ; 38D8
        mov ax,0x1                                      ; 38DA
        push ax                                         ; 38DD
        callf vxd_release, R5_38E1, R5_3939             ; 38DE far seg4

L5_38E3:
        xor_ ax,ax                                      ; 38E3
        pop si                                          ; 38E5
        pop di                                          ; 38E6
        mov_ sp,bp                                      ; 38E7
        pop bp                                          ; 38E9
        retf 0x2                                        ; 38EA
        db 0x90                                         ; 38ED
        mov ax,0x401                                    ; 38EE
        retf 0x6                                        ; 38F1
        push bp                                         ; 38F4
        mov_ bp,sp                                      ; 38F5
        mov bx,[bp+0xa]                                 ; 38F7
        sub sp,byte +0x4                                ; 38FA
        mov al,[bx+0x5d]                                ; 38FD
        and al,0x3                                      ; 3900
        cmp al,0x2                                      ; 3902
        jz short L5_390B                                ; 3904

L5_3906:
        xor_ ax,ax                                      ; 3906
        jmp near L5_39BA                                ; 3908

L5_390B:
        mov ax,[bx+0x78]                                ; 390B
        mov dx,[bx+0x7a]                                ; 390E
        cmp [bx+0x65],ax                                ; 3911
        jnz short L5_391B                               ; 3914
        cmp [bx+0x67],dx                                ; 3916
        jz short L5_3920                                ; 3919

L5_391B:
        mov ax,0x25                                     ; 391B
        jmp short L5_3923                               ; 391E

L5_3920:
        mov ax,0x26                                     ; 3920

L5_3923:
        les bx,[bp+0x6]                                 ; 3923
        cmp ax,[es:bx+0x4]                              ; 3926
        jnz short L5_3906                               ; 392A
        mov bx,[bp+0xa]                                 ; 392C
        push bx                                         ; 392F
        push word [bx]                                  ; 3930
        mov ax,0x1                                      ; 3932
        push ax                                         ; 3935
        callf vxd_acquire, R5_3939, R5_39B5             ; 3936 far seg4
        or_ ax,ax                                       ; 393B
        jnz short L5_3906                               ; 393D
        mov ax,[0xa6]                                   ; 393F
        inc word [0xa6]                                 ; 3942
        or_ ax,ax                                       ; 3946
        jnz short L5_394B                               ; 3948
        cli                                             ; 394A

L5_394B:
        push word [bp+0xa]                              ; 394B
        mov al,0xc0                                     ; 394E
        push ax                                         ; 3950
        callf dsp_write, R5_3954, R5_395F               ; 3951 far seg1
        push word [bp+0xa]                              ; 3956
        mov al,0xa8                                     ; 3959
        push ax                                         ; 395B
        callf dsp_write, R5_395F, R5_3980               ; 395C far seg1
        les bx,[bp+0x6]                                 ; 3961
        les bx,[es:bx+0x14]                             ; 3964
        mov ax,[es:bx+0x2]                              ; 3968
        or ax,[es:bx]                                   ; 396C
        jz short L5_3975                                ; 396F
        mov al,0x8                                      ; 3971
        jmp short L5_3977                               ; 3973

L5_3975:
        xor_ al,al                                      ; 3975

L5_3977:
        push word [bp+0xa]                              ; 3977
        mov [bp-0x4],ax                                 ; 397A
        callf dsp_read, R5_3980, R5_3993                ; 397D far seg1
        and al,0xf7                                     ; 3982
        or al,[bp-0x4]                                  ; 3984
        mov [bp-0x2],al                                 ; 3987
        push word [bp+0xa]                              ; 398A
        mov al,0xa8                                     ; 398D
        push ax                                         ; 398F
        callf dsp_write, R5_3993, R5_399F               ; 3990 far seg1
        push word [bp+0xa]                              ; 3995
        mov al,[bp-0x2]                                 ; 3998
        push ax                                         ; 399B
        callf dsp_write, R5_399F, R5_360E               ; 399C far seg1
        dec word [0xa6]                                 ; 39A1
        jnz short L5_39A8                               ; 39A5
        sti                                             ; 39A7

L5_39A8:
        mov bx,[bp+0xa]                                 ; 39A8
        push bx                                         ; 39AB
        push word [bx]                                  ; 39AC
        mov ax,0x1                                      ; 39AE
        push ax                                         ; 39B1
        callf vxd_release, R5_39B5, R5_39DF             ; 39B2 far seg4
        jmp near L5_3906                                ; 39B7

L5_39BA:
        mov_ sp,bp                                      ; 39BA
        pop bp                                          ; 39BC
        retf 0x6                                        ; 39BD

L5_39C0:
        push bp                                         ; 39C0
        mov_ bp,sp                                      ; 39C1
        sub sp,byte +0x8                                ; 39C3
        push di                                         ; 39C6
        push si                                         ; 39C7
        mov di,[bp+0xa]                                 ; 39C8
        push word [di+0x16]                             ; 39CB
        push word [di+0x14]                             ; 39CE
        mov ax,0x1                                      ; 39D1
        cwd                                             ; 39D4
        push dx                                         ; 39D5
        push ax                                         ; 39D6
        lea ax,[bp-0x8]                                 ; 39D7
        push ss                                         ; 39DA
        push ax                                         ; 39DB
        callf vxd_0005, R5_39DF, R5_3A67                ; 39DC far seg4
        or_ ax,ax                                       ; 39E1
        jz short L5_39E8                                ; 39E3
        jmp near L5_3A69                                ; 39E5

L5_39E8:
        mov bx,[bp+0x6]                                 ; 39E8
        mov es,[bp+0x8]                                 ; 39EB
        cmp word [es:bx+0x4],byte +0x27                 ; 39EE
        jnz short L5_3A00                               ; 39F3
        cmp [es:bx+0x6],ax                              ; 39F5
        jnz short L5_3A00                               ; 39F9
        mov ax,0x1                                      ; 39FB
        jmp short L5_3A03                               ; 39FE

L5_3A00:
        mov ax,0x2                                      ; 3A00

L5_3A03:
        cwd                                             ; 3A03
        mov [bp-0x4],ax                                 ; 3A04
        mov [bp-0x2],dx                                 ; 3A07
        test word [di+0x2c],0x4                         ; 3A0A
        jz short L5_3A3A                                ; 3A0F
        cmp word [es:bx+0x4],byte +0x28                 ; 3A11
        jnz short L5_3A3A                               ; 3A16
        cmp word [es:bx+0x6],byte +0x0                  ; 3A18
        jnz short L5_3A3A                               ; 3A1D
        les si,[es:bx+0x14]                             ; 3A1F
        mov ax,[es:si+0x2]                              ; 3A23
        or ax,[es:si]                                   ; 3A27
        jnz short L5_3A47                               ; 3A2A

L5_3A2C:
        mov ax,[bp-0x4]                                 ; 3A2C
        mov dx,[bp-0x2]                                 ; 3A2F
        or [bp-0x8],ax                                  ; 3A32
        or [bp-0x6],dx                                  ; 3A35
        jmp short L5_3A54                               ; 3A38

L5_3A3A:
        les si,[es:bx+0x14]                             ; 3A3A
        mov ax,[es:si+0x2]                              ; 3A3E
        or ax,[es:si]                                   ; 3A42
        jnz short L5_3A2C                               ; 3A45

L5_3A47:
        mov ax,[bp-0x4]                                 ; 3A47
        not ax                                          ; 3A4A
        not dx                                          ; 3A4C
        and [bp-0x8],ax                                 ; 3A4E
        and [bp-0x6],dx                                 ; 3A51

L5_3A54:
        push word [di+0x16]                             ; 3A54
        push word [di+0x14]                             ; 3A57
        sub_ ax,ax                                      ; 3A5A
        push ax                                         ; 3A5C
        push ax                                         ; 3A5D
        push word [bp-0x6]                              ; 3A5E
        push word [bp-0x8]                              ; 3A61
        callf vxd_0005, R5_3A67, R5_3B13                ; 3A64 far seg4

L5_3A69:
        xor_ ax,ax                                      ; 3A69
        pop si                                          ; 3A6B
        pop di                                          ; 3A6C
        mov_ sp,bp                                      ; 3A6D
        pop bp                                          ; 3A6F
        retf 0x6                                        ; 3A70
        db 0x90                                         ; 3A73

L5_3A74:
        push bp                                         ; 3A74
        mov_ bp,sp                                      ; 3A75
        sub sp,byte +0x26                               ; 3A77
        push si                                         ; 3A7A
        mov bx,[bp+0xa]                                 ; 3A7B
        cmp word [bx+0x128],byte +0x0                   ; 3A7E
        jz short L5_3ADD                                ; 3A83
        mov si,[bp+0x6]                                 ; 3A85
        mov es,[bp+0x8]                                 ; 3A88
        mov ax,[es:si+0x4]                              ; 3A8B
        mov dx,[es:si+0x6]                              ; 3A8F
        mov [bp-0x6],ax                                 ; 3A93
        mov [bp-0x4],dx                                 ; 3A96
        les bx,[es:si+0x14]                             ; 3A99
        mov ax,[es:bx]                                  ; 3A9D
        mov [bp-0x2],ax                                 ; 3AA0
        lea ax,[bp-0x26]                                ; 3AA3
        push ss                                         ; 3AA6
        push ax                                         ; 3AA7
        mov bx,[bp+0xa]                                 ; 3AA8
        call far [bx+0x12a]                             ; 3AAB
        mov ax,[bp-0x6]                                 ; 3AAF
        mov dx,[bp-0x4]                                 ; 3AB2
        or_ dx,dx                                       ; 3AB5
        jnz short L5_3ADD                               ; 3AB7
        sub ax,strict word 0x2b                         ; 3AB9
        jz short L5_3AC3                                ; 3ABC
        dec ax                                          ; 3ABE
        jz short L5_3ACB                                ; 3ABF
        jmp short L5_3ADD                               ; 3AC1

L5_3AC3:
        mov ax,[bp-0x2]                                 ; 3AC3
        mov [bp-0x24],ax                                ; 3AC6
        jmp short L5_3AD1                               ; 3AC9

L5_3ACB:
        mov ax,[bp-0x2]                                 ; 3ACB
        mov [bp-0x26],ax                                ; 3ACE

L5_3AD1:
        lea ax,[bp-0x26]                                ; 3AD1
        push ss                                         ; 3AD4
        push ax                                         ; 3AD5
        mov bx,[bp+0xa]                                 ; 3AD6
        call far [bx+0x12e]                             ; 3AD9

L5_3ADD:
        xor_ ax,ax                                      ; 3ADD
        pop si                                          ; 3ADF
        mov_ sp,bp                                      ; 3AE0
        pop bp                                          ; 3AE2
        retf 0x6                                        ; 3AE3
        push bp                                         ; 3AE6
        mov_ bp,sp                                      ; 3AE7
        push di                                         ; 3AE9
        push si                                         ; 3AEA
        mov si,[bp+0xa]                                 ; 3AEB
        test word [si+0x2c],0x4                         ; 3AEE
        jz short L5_3B00                                ; 3AF3
        push si                                         ; 3AF5
        push word [bp+0x8]                              ; 3AF6
        push word [bp+0x6]                              ; 3AF9
        push cs                                         ; 3AFC
        call L5_3A74                                    ; 3AFD

L5_3B00:
        test byte [si+0x2a],0x20                        ; 3B00
        jz short L5_3B09                                ; 3B04
        jmp near L5_3BA1                                ; 3B06

L5_3B09:
        push si                                         ; 3B09
        push word [si]                                  ; 3B0A
        mov ax,0x1                                      ; 3B0C
        push ax                                         ; 3B0F
        callf vxd_acquire, R5_3B13, R5_35EB             ; 3B10 far seg4
        or_ ax,ax                                       ; 3B15
        jz short L5_3B1C                                ; 3B17
        jmp near L5_3BA1                                ; 3B19

L5_3B1C:
        mov di,[bp+0x6]                                 ; 3B1C
        mov es,[bp+0x8]                                 ; 3B1F
        mov ax,[es:di+0x4]                              ; 3B22
        mov dx,[es:di+0x6]                              ; 3B26
        or_ dx,dx                                       ; 3B2A
        jnz short L5_3B95                               ; 3B2C
        sub ax,strict word 0x29                         ; 3B2E
        jz short L5_3B38                                ; 3B31
        dec ax                                          ; 3B33
        jz short L5_3B79                                ; 3B34
        jmp short L5_3B95                               ; 3B36

L5_3B38:
        push si                                         ; 3B38
        mov al,0x50                                     ; 3B39
        push ax                                         ; 3B3B
        les bx,[es:di+0x14]                             ; 3B3C
        mov ax,[es:bx+0x2]                              ; 3B40
        or ax,[es:bx]                                   ; 3B44
        jz short L5_3B4D                                ; 3B47
        mov al,0xc                                      ; 3B49
        jmp short L5_3B4F                               ; 3B4B

L5_3B4D:
        mov al,0x4                                      ; 3B4D

L5_3B4F:
        push ax                                         ; 3B4F
        callf mixer_write, R5_3B53, R5_3B64             ; 3B50 far seg1
        test byte [si+0x2b],0x4                         ; 3B55
        jz short L5_3B6A                                ; 3B59
        push si                                         ; 3B5B
        mov al,0x50                                     ; 3B5C
        push ax                                         ; 3B5E
        push si                                         ; 3B5F
        push ax                                         ; 3B60
        callf mixer_read, R5_3B64, R5_3B73              ; 3B61 far seg1
        or al,0x1                                       ; 3B66
        jmp short L5_3B8F                               ; 3B68

L5_3B6A:
        push si                                         ; 3B6A
        mov al,0x50                                     ; 3B6B
        push ax                                         ; 3B6D
        push si                                         ; 3B6E
        push ax                                         ; 3B6F
        callf mixer_read, R5_3B73, R5_3B8D              ; 3B70 far seg1
        and al,0xfe                                     ; 3B75
        jmp short L5_3B8F                               ; 3B77

L5_3B79:
        push si                                         ; 3B79
        mov al,0x52                                     ; 3B7A
        push ax                                         ; 3B7C
        les bx,[es:di+0x14]                             ; 3B7D
        mov ax,[es:bx]                                  ; 3B81
        mov dx,[es:bx+0x2]                              ; 3B84
        mov cl,0xa                                      ; 3B88
        callf L1_24F8, R5_3B8D, R5_3B93                 ; 3B8A far seg1

L5_3B8F:
        push ax                                         ; 3B8F
        callf mixer_write, R5_3B93, R5_3BC9             ; 3B90 far seg1

L5_3B95:
        push si                                         ; 3B95
        push word [si]                                  ; 3B96
        mov ax,0x1                                      ; 3B98
        push ax                                         ; 3B9B
        callf vxd_release, R5_3B9F, R5_38A8             ; 3B9C far seg4

L5_3BA1:
        xor_ ax,ax                                      ; 3BA1
        pop si                                          ; 3BA3
        pop di                                          ; 3BA4
        mov_ sp,bp                                      ; 3BA5
        pop bp                                          ; 3BA7
        retf 0x6                                        ; 3BA8
        db 0x90                                         ; 3BAB

; reads 60h and 62h back at every playback start
sync_hw_volume:
        push bp                                         ; 3BAC
        mov_ bp,sp                                      ; 3BAD
        sub sp,byte +0x2a                               ; 3BAF
        push di                                         ; 3BB2
        push si                                         ; 3BB3
        mov di,[bp+0x6]                                 ; 3BB4
        mov ax,[di+0x26]                                ; 3BB7
        mov dx,[di+0x28]                                ; 3BBA
        mov_ si,ax                                      ; 3BBD
        mov [bp-0x4],dx                                 ; 3BBF
        push di                                         ; 3BC2
        mov al,0x60                                     ; 3BC3
        push ax                                         ; 3BC5
        callf mixer_read, R5_3BC9, R5_3C31              ; 3BC6 far seg1
        mov [bp-0x1],al                                 ; 3BCB
        mov es,[bp-0x4]                                 ; 3BCE
        mov bx,[es:si+0x2864]                           ; 3BD1
        mov cl,0xa                                      ; 3BD6
        shr bx,cl                                       ; 3BD8
        sub_ bh,bh                                      ; 3BDA
        mov al,[bx+0x950]                               ; 3BDC
        mov [bp-0x2],al                                 ; 3BE0
        mov al,[bp-0x1]                                 ; 3BE3
        and al,0x40                                     ; 3BE6
        cmp al,0x1                                      ; 3BE8
        sbb_ ax,ax                                      ; 3BEA
        inc ax                                          ; 3BEC
        cwd                                             ; 3BED
        mov [bp-0xa],ax                                 ; 3BEE
        mov [bp-0x8],dx                                 ; 3BF1
        mov al,[bp-0x2]                                 ; 3BF4
        and byte [bp-0x1],0x3f                          ; 3BF7
        cmp [bp-0x1],al                                 ; 3BFB
        jz short L5_3C35                                ; 3BFE
        mov [bp-0x2],bh                                 ; 3C00
        mov al,[0x950]                                  ; 3C03
        cmp [bp-0x1],al                                 ; 3C06
        jc short L5_3C25                                ; 3C09
        mov [bp-0x6],si                                 ; 3C0B

L5_3C0E:
        cmp byte [bp-0x2],0x3f                          ; 3C0E
        jnc short L5_3C25                               ; 3C12
        inc byte [bp-0x2]                               ; 3C14
        mov bl,[bp-0x2]                                 ; 3C17
        sub_ bh,bh                                      ; 3C1A
        mov al,[bp-0x1]                                 ; 3C1C
        cmp [bx+0x950],al                               ; 3C1F
        jna short L5_3C0E                               ; 3C23

L5_3C25:
        mov al,[bp-0x2]                                 ; 3C25
        sub_ ah,ah                                      ; 3C28
        sub_ dx,dx                                      ; 3C2A
        mov cl,0xa                                      ; 3C2C
        callf L1_2416, R5_3C31, R5_3C4C                 ; 3C2E far seg1
        jmp short L5_3C3F                               ; 3C33

L5_3C35:
        mov ax,[es:si+0x2864]                           ; 3C35
        mov dx,[es:si+0x2866]                           ; 3C3A

L5_3C3F:
        mov [bp-0x12],ax                                ; 3C3F
        mov [bp-0x10],dx                                ; 3C42
        push di                                         ; 3C45
        mov al,0x62                                     ; 3C46
        push ax                                         ; 3C48
        callf mixer_read, R5_3C4C, R5_3CB4              ; 3C49 far seg1
        mov [bp-0x1],al                                 ; 3C4E
        mov es,[bp-0x4]                                 ; 3C51
        mov bx,[es:si+0x2868]                           ; 3C54
        mov cl,0xa                                      ; 3C59
        shr bx,cl                                       ; 3C5B
        sub_ bh,bh                                      ; 3C5D
        mov al,[bx+0x950]                               ; 3C5F
        mov [bp-0x2],al                                 ; 3C63
        mov al,[bp-0x1]                                 ; 3C66
        and al,0x40                                     ; 3C69
        cmp al,0x1                                      ; 3C6B
        sbb_ ax,ax                                      ; 3C6D
        inc ax                                          ; 3C6F
        cwd                                             ; 3C70
        or [bp-0xa],ax                                  ; 3C71
        or [bp-0x8],dx                                  ; 3C74
        mov al,[bp-0x2]                                 ; 3C77
        and byte [bp-0x1],0x3f                          ; 3C7A
        cmp [bp-0x1],al                                 ; 3C7E
        jz short L5_3CB8                                ; 3C81
        mov [bp-0x2],bh                                 ; 3C83
        mov al,[0x950]                                  ; 3C86
        cmp [bp-0x1],al                                 ; 3C89
        jc short L5_3CA8                                ; 3C8C
        mov [bp-0x6],si                                 ; 3C8E

L5_3C91:
        cmp byte [bp-0x2],0x3f                          ; 3C91
        jnc short L5_3CA8                               ; 3C95
        inc byte [bp-0x2]                               ; 3C97
        mov bl,[bp-0x2]                                 ; 3C9A
        sub_ bh,bh                                      ; 3C9D
        mov al,[bp-0x1]                                 ; 3C9F
        cmp [bx+0x950],al                               ; 3CA2
        jna short L5_3C91                               ; 3CA6

L5_3CA8:
        mov al,[bp-0x2]                                 ; 3CA8
        sub_ ah,ah                                      ; 3CAB
        sub_ dx,dx                                      ; 3CAD
        mov cl,0xa                                      ; 3CAF
        callf L1_2416, R5_3CB4, R5_38BE                 ; 3CB1 far seg1
        jmp short L5_3CC2                               ; 3CB6

L5_3CB8:
        mov ax,[es:si+0x2868]                           ; 3CB8
        mov dx,[es:si+0x286a]                           ; 3CBD

L5_3CC2:
        mov [bp-0xe],ax                                 ; 3CC2
        mov [bp-0xc],dx                                 ; 3CC5
        mov word [bp-0x26],0x21                         ; 3CC8
        mov word [bp-0x24],0x0                          ; 3CCD
        mov word [bp-0x2a],0x18                         ; 3CD2
        mov word [bp-0x28],0x0                          ; 3CD7
        mov es,[bp-0x4]                                 ; 3CDC
        mov ax,[es:si+0x2634]                           ; 3CDF
        mov [bp-0x22],ax                                ; 3CE4
        mov word [bp-0x20],0x0                          ; 3CE7
        sub_ ax,ax                                      ; 3CEC
        mov [bp-0x1c],ax                                ; 3CEE
        mov [bp-0x1e],ax                                ; 3CF1
        mov word [bp-0x1a],0x4                          ; 3CF4
        mov [bp-0x18],ax                                ; 3CF9
        lea ax,[bp-0xa]                                 ; 3CFC
        mov [bp-0x16],ax                                ; 3CFF
        mov [bp-0x14],ss                                ; 3D02
        push di                                         ; 3D05
        lea cx,[bp-0x2a]                                ; 3D06
        push ss                                         ; 3D09
        push cx                                         ; 3D0A
        sub_ cx,cx                                      ; 3D0B
        push cx                                         ; 3D0D
        push cx                                         ; 3D0E
        callf mxd_set_control_details, R5_3D12, R5_3D66 ; 3D0F far seg5
        mov ax,[bp-0x8]                                 ; 3D14
        or ax,[bp-0xa]                                  ; 3D17
        jnz short L5_3D68                               ; 3D1A
        mov word [bp-0x26],0xb                          ; 3D1C
        mov word [bp-0x24],0x0                          ; 3D21
        mov word [bp-0x2a],0x18                         ; 3D26
        mov word [bp-0x28],0x0                          ; 3D2B
        mov es,[bp-0x4]                                 ; 3D30
        mov ax,[es:si+0x25b0]                           ; 3D33
        mov [bp-0x22],ax                                ; 3D38
        mov word [bp-0x20],0x0                          ; 3D3B
        sub_ ax,ax                                      ; 3D40
        mov [bp-0x1c],ax                                ; 3D42
        mov [bp-0x1e],ax                                ; 3D45
        mov word [bp-0x1a],0x4                          ; 3D48
        mov [bp-0x18],ax                                ; 3D4D
        lea ax,[bp-0x12]                                ; 3D50
        mov [bp-0x16],ax                                ; 3D53
        mov [bp-0x14],ss                                ; 3D56
        push di                                         ; 3D59
        lea ax,[bp-0x2a]                                ; 3D5A
        push ss                                         ; 3D5D
        push ax                                         ; 3D5E
        sub_ ax,ax                                      ; 3D5F
        push ax                                         ; 3D61
        push ax                                         ; 3D62
        callf mxd_set_control_details, R5_3D66, R5_33D4 ; 3D63 far seg5

L5_3D68:
        xor_ ax,ax                                      ; 3D68
        pop si                                          ; 3D6A
        pop di                                          ; 3D6B
        mov_ sp,bp                                      ; 3D6C
        pop bp                                          ; 3D6E
        retf 0x2                                        ; 3D6F

L5_3D72:
        xor_ ax,ax                                      ; 3D72
        retf 0x6                                        ; 3D74
        db 0x90, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x71, 0x03, 0x00, 0x00 ; 3D77
        db 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3D87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3D97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3DA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3DB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3DC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3DD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00 ; 3DE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3DF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01 ; 3E07
        db 0x71, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3E17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3E27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3E37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3E47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3E57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00 ; 3E67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3E77
        db 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3E87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00 ; 3E97
        db 0x00, 0x01, 0x00, 0x01, 0x71, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3EA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3EB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3EC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3ED7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3EE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3EF7
        db 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F07
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 3F27
        db 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3F97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3FA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3FB7
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00 ; 3FC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3FD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3FE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 3FF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4007
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4017
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4027
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 ; 4037
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4047
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03 ; 4057
        db 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4067
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4077
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4087
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4097
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 40A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00 ; 40B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 40C7
        db 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 40D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00 ; 40E7
        db 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 40F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4107
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4117
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4127
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4137
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4147
        db 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4157
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4167
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 4177
        db 0x00, 0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4187
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4197
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 41A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 41B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 41C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 41D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 41E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 41F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4207
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00 ; 4217
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4227
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4237
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4247
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4257
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4267
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4277
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 ; 4287
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4297
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03 ; 42A7
        db 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 42B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 42C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 42D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 42E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 42F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00 ; 4307
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4317
        db 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4327
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00 ; 4337
        db 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4347
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4357
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4367
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4377
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4387
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4397
        db 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 43A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 43B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 43C7
        db 0x00, 0x0B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 43D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 43E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 43F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4407
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4417
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4427
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4437
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4447
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4457
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00 ; 4467
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4477
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4487
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4497
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 44A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 44B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 44C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 ; 44D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 44E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03 ; 44F7
        db 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4507
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4517
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4527
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4537
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4547
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00 ; 4557
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4567
        db 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4577
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00 ; 4587
        db 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4597
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 45A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 45B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 45C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 45D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 45E7
        db 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 45F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4607
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 4617
        db 0x00, 0x0F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4627
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4637
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4647
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4657
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4667
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4677
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4687
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4697
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 46A7
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00 ; 46B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 46C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 46D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 46E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 46F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4707
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4717
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 ; 4727
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4737
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03 ; 4747
        db 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4757
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4767
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4777
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4787
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4797
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00 ; 47A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 47B7
        db 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 47C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00 ; 47D7
        db 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 47E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 47F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4807
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4817
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4827
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4837
        db 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4847
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4857
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 4867
        db 0x00, 0x13, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4877
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4887
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4897
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 48A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 48B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 48C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 48D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 48E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 48F7
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00 ; 4907
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4917
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4927
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4937
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4947
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4957
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4967
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 ; 4977
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4987
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03 ; 4997
        db 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 49A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 49B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 49C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 49D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 49E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00 ; 49F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A07
        db 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00 ; 4A27
        db 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A87
        db 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4A97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4AA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 4AB7
        db 0x00, 0x17, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4AC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4AD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4AE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4AF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B07
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B47
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00 ; 4B57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4B97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4BA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4BB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 ; 4BC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4BD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01 ; 4BE7
        db 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4BF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4C07
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4C17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4C27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4C37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 ; 4C47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4C57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4C67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00 ; 4C77
        db 0x00, 0x02, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4C87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4C97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4CA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4CB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4CC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4CD7
        db 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4CE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4CF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 4D07
        db 0x00, 0x1B, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4D97
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00 ; 4DA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4DB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4DC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4DD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4DE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4DF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4E07
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4E17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4E27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01 ; 4E37
        db 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4E47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4E57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4E67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4E77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4E87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 ; 4E97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4EA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4EB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00 ; 4EC7
        db 0x00, 0x02, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4ED7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4EE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4EF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F07
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F27
        db 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 4F57
        db 0x00, 0x1F, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4F97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4FA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4FB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4FC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4FD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 4FE7
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00 ; 4FF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5007
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5017
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5027
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5037
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5047
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5057
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5067
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5077
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01 ; 5087
        db 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5097
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 50A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 50B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 50C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 50D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 ; 50E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 50F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5107
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00 ; 5117
        db 0x00, 0x01, 0x00, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5127
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5137
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5147
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5157
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5167
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF ; 5177
        db 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5187
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5197
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 51A7
        db 0x00, 0x23, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 51B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 51C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 51D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 51E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 51F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5207
        db 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5217
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5227
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5237
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x10, 0x00, 0x00, 0x00 ; 5247
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5257
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5267
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5277
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5287
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5297
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00 ; 52A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 52B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 52C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01 ; 52D7
        db 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 52E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 52F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5307
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5317
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5327
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 ; 5337
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5347
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5357
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00 ; 5367
        db 0x00, 0x01, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5377
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5387
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5397
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 53A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 53B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 53C7
        db 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 53D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 53E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 53F7
        db 0x00, 0x27, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5407
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5417
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5427
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5437
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5447
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5457
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5467
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5477
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5487
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00 ; 5497
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 54A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 54B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 54C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 54D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 54E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 54F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5507
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5517
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01 ; 5527
        db 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5537
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5547
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5557
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5567
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5577
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 ; 5587
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5597
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 55A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x00 ; 55B7
        db 0x00, 0x00, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 55C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 55D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 55E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 55F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5607
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5617
        db 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5627
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5637
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 5647
        db 0x00, 0x2B, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5657
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5667
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5677
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5687
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5697
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 56A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 56B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 56C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 56D7
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00 ; 56E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 56F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5707
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5717
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5727
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5737
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5747
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 ; 5757
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5767
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x2D, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01 ; 5777
        db 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5787
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5797
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 57A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 57B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 57C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 ; 57D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 57E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 57F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x00 ; 5807
        db 0x00, 0x02, 0x00, 0x01, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5817
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5827
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5837
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5847
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5857
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5867
        db 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5877
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5887
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00 ; 5897
        db 0x00, 0x2F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 58A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 58B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 58C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 58D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 58E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 58F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5907
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5917
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5927
        db 0x00, 0x94, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00 ; 5937
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5947
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5957
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5967
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5977
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5987
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5997
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 ; 59A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 59B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 59C7
        db 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00 ; 59D7
        db 0x00, 0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 59E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 59F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5A07
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5A17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5A27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5A37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5A47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5A57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x01 ; 5A67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5A77
        db 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04 ; 5A87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5A97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5AA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5AB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5AC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5AD7
        db 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5AE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5AF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5B07
        db 0x00, 0xA6, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF ; 5B17
        db 0xFF, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00 ; 5B27
        db 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5B37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5B47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5B57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5B67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5B77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5B87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5B97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5BA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00 ; 5BB7
        db 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 ; 5BC7
        db 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5BD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5BE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5BF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5C07
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5C17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5C27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5C37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5C47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00 ; 5C57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00 ; 5C67
        db 0x80, 0x00, 0x00, 0x00, 0x00, 0x09, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5C77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5C87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5C97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5CA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5CB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5CC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5CD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5CE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5CF7
        db 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF ; 5D07
        db 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x08, 0x10, 0x00, 0x00, 0x02 ; 5D17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5D27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5D37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5D47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5D57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5D67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 ; 5D77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5D87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5D97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5DA7
        db 0x00, 0x02, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00 ; 5DB7
        db 0x00, 0x03, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5DC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5DD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5DE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5DF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5E07
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5E17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5E27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5E37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6 ; 5E47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 ; 5E57
        db 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x05, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 ; 5E67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5E77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5E87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5E97
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5EA7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5EB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5EC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5ED7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5EE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00 ; 5EF7
        db 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x04, 0x10, 0x00 ; 5F07
        db 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5F17
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5F27
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5F37
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5F47
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5F57
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5F67
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5F77
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5F87
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00 ; 5F97
        db 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, 0x00 ; 5FA7
        db 0x00, 0x00, 0x00, 0x09, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5FB7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5FC7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5FD7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5FE7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 5FF7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6007
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6017
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6027
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6037
        db 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF ; 6047
        db 0xFF, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x09, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00 ; 6057
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6067
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6077
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6087
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6097
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 60A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 60B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 60C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 60D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07 ; 60E7
        db 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03 ; 60F7
        db 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6107
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6117
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6127
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6137
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6147
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6157
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6167
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6177
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00 ; 6187
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00 ; 6197
        db 0x80, 0x00, 0x00, 0x00, 0x00, 0x03, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 61A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 61B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 61C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 61D7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 61E7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 61F7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6207
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6217
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6227
        db 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xFF ; 6237
        db 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x09, 0x10, 0x00, 0x00, 0x02 ; 6247
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6257
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6267
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6277
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6287
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6297
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 62A7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 62B7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 62C7
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x02, 0x00, 0x02 ; 62D7
        db 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01 ; 62E7
        db 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 62F7
        db 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6307
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 6317
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03 ; 6327
        db 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01 ; 6337
        db 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF ; 6347
        db 0xFF, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF ; 6357
        db 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05 ; 6367
        db 0x00, 0x06, 0x00, 0x07, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x05 ; 6377
        db 0x00, 0x04, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x02 ; 6387
        db 0x00, 0x03, 0x00, 0x05, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF ; 6397
        db 0xFF, 0x08, 0x00, 0x09, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF ; 63A7
        db 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x01, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0x01 ; 63B7
        db 0x00, 0x02, 0x00, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01 ; 63C7
        db 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00 ; 63D7
        db 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06, 0x00, 0x02 ; 63E7
        db 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0x01, 0x00, 0x00 ; 63F7
        db 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x01 ; 6407
        db 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00, 0x02 ; 6417
        db 0x00, 0x01, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x01 ; 6427
        db 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x00, 0x02, 0x00, 0x02 ; 6437
        db 0x00, 0x04, 0x00, 0x02, 0x00, 0x02, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 ; 6447
        db 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03 ; 6457
        db 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00 ; 6467
        db 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x01 ; 6477
        db 0x00, 0x01, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0x02, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x01 ; 6487
        db 0x00, 0x02, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0x01, 0x00, 0x02, 0x00, 0xFF, 0xFF, 0x01, 0x00, 0x00 ; 6497
        db 0x00, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x01 ; 64A7
        db 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0xFF ; 64B7
        db 0xFF, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x03 ; 64C7
        db 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00 ; 64D7

seg5_data_end:

; relocation table
        dw (seg5_rel_end - seg5_rel_start) / 8
seg5_rel_start:
        reloc 2, 0, R5_3B53, 0x0001, 0x0000             ; seg1
        reloc 2, 0, R5_20F5, 0x0003, 0x0000             ; seg3
        reloc 3, 1, R5_0B50, 0x0001, 0x0005             ; KERNEL.LocalAlloc
        reloc 3, 1, R5_0BD5, 0x0001, 0x0007             ; KERNEL.LocalFree
        reloc 2, 0, R5_3B9F, 0x0004, 0x0000             ; seg4
        reloc 2, 0, R5_2BA3, 0x0005, 0x0000             ; seg5
        reloc 3, 1, R5_00E9, 0x0001, 0x000F             ; KERNEL.GlobalAlloc
        reloc 2, 0, R5_14DA, 0x0006, 0x0000             ; seg6
        reloc 3, 1, R5_04E3, 0x0001, 0x0011             ; KERNEL.GlobalFree
        reloc 2, 0, R5_20E8, 0x0007, 0x0000             ; seg7
        reloc 3, 1, R5_0AFF, 0x0002, 0x01A4             ; USER.wsprintf
        reloc 3, 1, R5_1F2E, 0x0003, 0x001F             ; MMSYSTEM.DriverCallback
        reloc 3, 1, R5_0040, 0x0001, 0x0032             ; KERNEL.GetProcAddress
        reloc 3, 1, R5_0AEA, 0x0002, 0x00B0             ; USER.LoadString
        reloc 3, 1, R5_177E, 0x0001, 0x0058             ; KERNEL.lstrcpy
        reloc 3, 1, R5_002E, 0x0001, 0x005F             ; KERNEL.LoadLibrary
        reloc 3, 1, R5_0EE1, 0x0002, 0x01D7             ; USER.lstrcmpi
        reloc 3, 1, R5_00C7, 0x0001, 0x0060             ; KERNEL.FreeLibrary
        reloc 3, 1, R5_0B16, 0x0001, 0x0161             ; KERNEL.lstrcpyn
seg5_rel_end:
seg5_end:
