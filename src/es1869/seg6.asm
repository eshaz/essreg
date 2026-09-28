; segment 6: code, 12330 bytes, flags 1D50h

L6_0000:
        cmp word [0x16],byte +0x0                       ; 0000
        jnz short L6_001C                               ; 0005
        mov word [0x16],0x1                             ; 0007
        mov ax,0x2                                      ; 000D
        push ax                                         ; 0010
        xor_ ax,ax                                      ; 0011
        push ax                                         ; 0013
        callf L3_0AEC, R6_0017, 0xFFFF                  ; 0014 far seg3
        add sp,byte +0x4                                ; 0019

L6_001C:
        retf                                            ; 001C
        db 0x90                                         ; 001D

L6_001E:
        push bp                                         ; 001E
        mov_ bp,sp                                      ; 001F
        sub sp,byte +0x4                                ; 0021
        push di                                         ; 0024
        push si                                         ; 0025
        mov si,[bp+0x4]                                 ; 0026
        mov ax,[bp+0x6]                                 ; 0029
        or_ ax,si                                       ; 002C
        jz short L6_0053                                ; 002E

L6_0030:
        mov es,[bp+0x6]                                 ; 0030
        mov ax,[es:si+0x18]                             ; 0033
        mov dx,[es:si+0x1a]                             ; 0037
        mov_ di,ax                                      ; 003B
        mov [bp-0x2],dx                                 ; 003D
        push es                                         ; 0040
        push si                                         ; 0041
        callf L1_0C56, R6_0045, R6_00DC                 ; 0042 far seg1
        mov ax,[bp-0x2]                                 ; 0047
        mov_ si,di                                      ; 004A
        mov [bp+0x6],ax                                 ; 004C
        or_ ax,di                                       ; 004F
        jnz short L6_0030                               ; 0051

L6_0053:
        pop si                                          ; 0053
        pop di                                          ; 0054
        mov_ sp,bp                                      ; 0055
        pop bp                                          ; 0057
        ret 0x4                                         ; 0058
        db 0x90                                         ; 005B

L6_005C:
        push bp                                         ; 005C
        mov_ bp,sp                                      ; 005D
        sub sp,byte +0x6                                ; 005F
        push si                                         ; 0062
        mov ax,[0xa6]                                   ; 0063
        inc word [0xa6]                                 ; 0066
        or_ ax,ax                                       ; 006A
        jnz short L6_006F                               ; 006C
        cli                                             ; 006E

L6_006F:
        mov bx,[bp+0x6]                                 ; 006F
        mov ax,[bx+0x8e]                                ; 0072
        or ax,[bx+0x8c]                                 ; 0076
        jz short L6_00BC                                ; 007A
        mov ax,[bx+0x8c]                                ; 007C
        mov dx,[bx+0x8e]                                ; 0080
        mov [bp-0x4],ax                                 ; 0084
        mov [bp-0x2],dx                                 ; 0087
        mov_ si,ax                                      ; 008A
        mov es,dx                                       ; 008C
        mov ax,[es:si+0x18]                             ; 008E
        mov dx,[es:si+0x1a]                             ; 0092
        mov [bx+0x8c],ax                                ; 0096
        mov [bx+0x8e],dx                                ; 009A
        or byte [es:si+0x10],0x1                        ; 009E
        and byte [es:si+0x10],0xef                      ; 00A3
        sub_ ax,ax                                      ; 00A8
        mov [bx+0x96],ax                                ; 00AA
        mov [bx+0x94],ax                                ; 00AE
        mov [bx+0x92],ax                                ; 00B2
        mov [bx+0x90],ax                                ; 00B6
        jmp short L6_00C4                               ; 00BA

L6_00BC:
        sub_ ax,ax                                      ; 00BC
        mov [bp-0x2],ax                                 ; 00BE
        mov [bp-0x4],ax                                 ; 00C1

L6_00C4:
        dec word [0xa6]                                 ; 00C4
        jnz short L6_00CB                               ; 00C8
        sti                                             ; 00CA

L6_00CB:
        mov ax,[bp-0x2]                                 ; 00CB
        or ax,[bp-0x4]                                  ; 00CE
        jz short L6_00DE                                ; 00D1
        push word [bp-0x2]                              ; 00D3
        push word [bp-0x4]                              ; 00D6
        callf L1_0C56, R6_00DC, R6_0170                 ; 00D9 far seg1

L6_00DE:
        pop si                                          ; 00DE
        mov_ sp,bp                                      ; 00DF
        pop bp                                          ; 00E1
        retf 0x2                                        ; 00E2
        db 0x90                                         ; 00E5

L6_00E6:
        push bp                                         ; 00E6
        mov_ bp,sp                                      ; 00E7
        sub sp,0xac                                     ; 00E9
        push si                                         ; 00ED
        mov si,[bp+0x6]                                 ; 00EE
        mov word [bp-0x2c],0x2e                         ; 00F1
        mov word [bp-0x2a],0x29                         ; 00F6
        mov word [bp-0x28],0x404                        ; 00FB
        mov word [bp-0x6],0xfff                         ; 0100
        mov word [bp-0x4],0x0                           ; 0105
        mov word [bp-0x2],0x2                           ; 010A
        push word [0xbd0]                               ; 010F
        mov ax,0x10                                     ; 0113
        push ax                                         ; 0116
        lea ax,[bp-0x6c]                                ; 0117
        push ss                                         ; 011A
        push ax                                         ; 011B
        mov ax,0x40                                     ; 011C
        push ax                                         ; 011F
        callp R6_0121, 0xFFFF, 0x0000                   ; 0120 USER.LoadString
        mov bx,[bp+0xa]                                 ; 0125
        push word [bx]                                  ; 0128
        lea ax,[bp-0x6c]                                ; 012A
        push ss                                         ; 012D
        push ax                                         ; 012E
        lea ax,[bp-0xac]                                ; 012F
        push ss                                         ; 0133
        push ax                                         ; 0134
        callp R6_0136, 0xFFFF, 0x0000                   ; 0135 USER.wsprintf
        add sp,byte +0xa                                ; 013A
        lea ax,[bp-0x26]                                ; 013D
        push ss                                         ; 0140
        push ax                                         ; 0141
        lea ax,[bp-0xac]                                ; 0142
        push ss                                         ; 0146
        push ax                                         ; 0147
        mov ax,0x1f                                     ; 0148
        push ax                                         ; 014B
        callp R6_014D, 0xFFFF, 0x0000                   ; 014C KERNEL.lstrcpyn
        mov es,[bp+0x8]                                 ; 0151
        push word [es:si+0x6]                           ; 0154
        push word [es:si+0x4]                           ; 0158
        lea ax,[bp-0x2c]                                ; 015C
        push ss                                         ; 015F
        push ax                                         ; 0160
        mov ax,[es:si]                                  ; 0161
        cmp ax,strict word 0x2c                         ; 0164
        jna short L6_016C                               ; 0167
        mov ax,0x2c                                     ; 0169

L6_016C:
        push ax                                         ; 016C
        callf L1_1AD3, R6_0170, R6_01EB                 ; 016D far seg1
        pop si                                          ; 0172
        mov_ sp,bp                                      ; 0173
        pop bp                                          ; 0175
        retf 0x6                                        ; 0176
        db 0x90                                         ; 0179

L6_017A:
        push bp                                         ; 017A
        mov_ bp,sp                                      ; 017B
        sub sp,byte +0xe                                ; 017D
        push di                                         ; 0180
        push si                                         ; 0181
        mov ax,[bp+0xc]                                 ; 0182
        mov_ bx,ax                                      ; 0185
        mov [bp-0x2],ax                                 ; 0187
        mov ax,[bx]                                     ; 018A
        mov [bp-0xc],ax                                 ; 018C
        mov_ bx,ax                                      ; 018F
        mov cx,[bp+0xc]                                 ; 0191
        mov dx,[bp+0xe]                                 ; 0194
        cmp [bx+0x65],cx                                ; 0197
        jnz short L6_01A1                               ; 019A
        cmp [bx+0x67],dx                                ; 019C
        jz short L6_01B9                                ; 019F

L6_01A1:
        cmp [bx+0x78],cx                                ; 01A1
        jnz short L6_01B1                               ; 01A4
        cmp [bx+0x7a],dx                                ; 01A6
        jnz short L6_01B1                               ; 01A9
        cmp word [bx+0x76],byte +0x0                    ; 01AB
        jnz short L6_01B9                               ; 01AF

L6_01B1:
        mov ax,0xb                                      ; 01B1

L6_01B4:
        xor_ dx,dx                                      ; 01B4
        jmp near L6_0392                                ; 01B6

L6_01B9:
        cmp word [bp+0x6],byte +0x0                     ; 01B9
        jnz short L6_01C5                               ; 01BD
        cmp word [bp+0x4],byte +0x8                     ; 01BF
        jc short L6_01B1                                ; 01C3

L6_01C5:
        mov ax,[bp+0xc]                                 ; 01C5
        mov dx,[bp+0xe]                                 ; 01C8
        mov bx,[bp-0xc]                                 ; 01CB
        cmp [bx+0x65],ax                                ; 01CE
        jnz short L6_023E                               ; 01D1
        cmp [bx+0x67],dx                                ; 01D3
        jnz short L6_023E                               ; 01D6
        test byte [bx+0x5d],0x3                         ; 01D8
        jnz short L6_01E3                               ; 01DC
        mov ax,0x1                                      ; 01DE
        jmp short L6_01B4                               ; 01E1

L6_01E3:
        push bx                                         ; 01E3
        sub_ ax,ax                                      ; 01E4
        push ax                                         ; 01E6
        push ax                                         ; 01E7
        callf L1_134E, R6_01EB, R6_0273                 ; 01E8 far seg1
        mov [bp-0x6],ax                                 ; 01ED
        mov [bp-0x4],dx                                 ; 01F0
        mov bx,[bp-0xc]                                 ; 01F3
        mov al,[bx+0x5d]                                ; 01F6
        and al,0x3                                      ; 01F9
        cmp al,0x2                                      ; 01FB
        jnz short L6_0226                               ; 01FD
        test byte [bx+0x2a],0x8                         ; 01FF
        jz short L6_0226                                ; 0203
        mov ax,[bx+0x3d]                                ; 0205
        sub_ dx,dx                                      ; 0208
        cmp dx,[bp-0x4]                                 ; 020A
        jc short L6_0220                                ; 020D
        ja short L6_0216                                ; 020F
        cmp ax,[bp-0x6]                                 ; 0211
        jna short L6_0220                               ; 0214

L6_0216:
        sub_ ax,ax                                      ; 0216
        mov [bp-0x4],ax                                 ; 0218
        mov [bp-0x6],ax                                 ; 021B
        jmp short L6_0226                               ; 021E

L6_0220:
        sub [bp-0x6],ax                                 ; 0220
        sbb [bp-0x4],dx                                 ; 0223

L6_0226:
        mov bx,[bp-0xc]                                 ; 0226
        mov ax,[bx+0x6b]                                ; 0229
        mov [bp-0xa],ax                                 ; 022C
        mov cl,[bx+0x5d]                                ; 022F
        and cl,0x3                                      ; 0232
        cmp cl,0x2                                      ; 0235
        jnz short L6_02B4                               ; 0238
        mov_ cx,ax                                      ; 023A
        jmp short L6_02A3                               ; 023C

L6_023E:
        mov ax,[bx+0x78]                                ; 023E
        mov dx,[bx+0x7a]                                ; 0241
        add bx,byte +0x7c                               ; 0244
        mov [bp-0xe],bx                                 ; 0247
        cmp [bx],ax                                     ; 024A
        jz short L6_0251                                ; 024C
        jmp near L6_01B1                                ; 024E

L6_0251:
        cmp [bx+0x2],dx                                 ; 0251
        jz short L6_0259                                ; 0254
        jmp near L6_01B1                                ; 0256

L6_0259:
        mov bx,[bp-0x2]                                 ; 0259
        push word [bx+0x16]                             ; 025C
        push word [bx+0x14]                             ; 025F
        mov bx,[bp-0xe]                                 ; 0262
        sub_ dx,dx                                      ; 0265
        mov ax,[bx+0xe]                                 ; 0267
        push dx                                         ; 026A
        push ax                                         ; 026B
        mov_ si,ax                                      ; 026C
        mov_ di,dx                                      ; 026E
        callf L1_23E4, R6_0273, R6_0339                 ; 0270 far seg1
        mov [bp-0x6],ax                                 ; 0275
        mov [bp-0x4],dx                                 ; 0278
        mov bx,[bp-0xc]                                 ; 027B
        test byte [bx+0x2a],0x8                         ; 027E
        jz short L6_0298                                ; 0282
        cmp_ dx,di                                      ; 0284
        jc short L6_0298                                ; 0286
        ja short L6_028E                                ; 0288
        cmp_ ax,si                                      ; 028A
        jc short L6_0298                                ; 028C

L6_028E:
        sub_ ax,si                                      ; 028E
        sbb_ dx,di                                      ; 0290
        mov [bp-0x6],ax                                 ; 0292
        mov [bp-0x4],dx                                 ; 0295

L6_0298:
        mov bx,[bp-0xe]                                 ; 0298
        mov ax,[bx+0x6]                                 ; 029B
        mov_ cx,ax                                      ; 029E
        mov [bp-0xa],ax                                 ; 02A0

L6_02A3:
        test ah,0x40                                    ; 02A3
        jz short L6_02B4                                ; 02A6
        and cl,0xfe                                     ; 02A8
        mov [bp-0xa],cx                                 ; 02AB
        shr word [bp-0x4],1                             ; 02AE
        rcr word [bp-0x6],1                             ; 02B1

L6_02B4:
        mov ax,[bp-0x6]                                 ; 02B4
        mov dx,[bp-0x4]                                 ; 02B7
        mov bx,[bp-0x2]                                 ; 02BA
        cmp [bx+0x12],dx                                ; 02BD
        ja short L6_02D5                                ; 02C0
        jc short L6_02C9                                ; 02C2
        cmp [bx+0x10],ax                                ; 02C4
        jnc short L6_02D5                               ; 02C7

L6_02C9:
        mov ax,[bx+0x10]                                ; 02C9
        mov dx,[bx+0x12]                                ; 02CC
        mov [bp-0x6],ax                                 ; 02CF
        mov [bp-0x4],dx                                 ; 02D2

L6_02D5:
        les bx,[bp+0x8]                                 ; 02D5
        cmp word [es:bx],byte +0x4                      ; 02D8
        jnz short L6_02E8                               ; 02DC

L6_02DE:
        mov [es:bx+0x2],ax                              ; 02DE
        mov [es:bx+0x4],dx                              ; 02E2
        jmp short L6_0326                               ; 02E6

L6_02E8:
        mov word [es:bx],0x2                            ; 02E8
        mov [es:bx+0x2],ax                              ; 02ED
        mov [es:bx+0x4],dx                              ; 02F1
        mov al,[bp-0xa]                                 ; 02F5
        and ax,strict word 0x3f                         ; 02F8
        sub_ dx,dx                                      ; 02FB
        cmp ax,strict word 0x20                         ; 02FD
        jz short L6_036A                                ; 0300
        ja short L6_0326                                ; 0302
        sub al,0x1                                      ; 0304
        jc short L6_0326                                ; 0306
        sub al,0x1                                      ; 0308
        jna short L6_031E                               ; 030A
        dec al                                          ; 030C
        jz short L6_032B                                ; 030E
        dec al                                          ; 0310
        jz short L6_033D                                ; 0312
        sub al,0x4                                      ; 0314
        jz short L6_033D                                ; 0316
        sub al,0x8                                      ; 0318
        jz short L6_033D                                ; 031A
        jmp short L6_0326                               ; 031C

L6_031E:
        shr word [es:bx+0x4],1                          ; 031E
        rcr word [es:bx+0x2],1                          ; 0322

L6_0326:
        xor_ ax,ax                                      ; 0326
        jmp near L6_01B4                                ; 0328

L6_032B:
        mov al,0x2                                      ; 032B
        push ax                                         ; 032D
        mov_ ax,bx                                      ; 032E
        mov dx,es                                       ; 0330
        inc ax                                          ; 0332
        inc ax                                          ; 0333
        push es                                         ; 0334
        push ax                                         ; 0335
        callf L1_232A, R6_0339, R6_035B                 ; 0336 far seg1
        jmp short L6_0326                               ; 033B

L6_033D:
        mov ax,0x13                                     ; 033D
        cwd                                             ; 0340
        push dx                                         ; 0341
        push ax                                         ; 0342
        mov bx,[bp-0xc]                                 ; 0343
        sub_ dx,dx                                      ; 0346
        mov al,[bx+0x73]                                ; 0348
        push dx                                         ; 034B
        push ax                                         ; 034C
        mov bx,[bp+0x8]                                 ; 034D
        push word [es:bx+0x4]                           ; 0350
        push word [es:bx+0x2]                           ; 0354
        callf L1_242E, R6_035B, R6_0362                 ; 0358 far seg1
        push dx                                         ; 035D
        push ax                                         ; 035E
        callf L1_23E4, R6_0362, R6_037B                 ; 035F far seg1
        les bx,[bp+0x8]                                 ; 0364
        jmp near L6_02DE                                ; 0367

L6_036A:
        mov ax,[es:bx+0x2]                              ; 036A
        mov dx,[es:bx+0x4]                              ; 036E
        mov cl,0x8                                      ; 0372
        mov_ si,ax                                      ; 0374
        mov_ di,dx                                      ; 0376
        callf L1_24F8, R6_037B, 0xFFFF                  ; 0378 far seg1
        add_ si,si                                      ; 037D
        adc_ di,di                                      ; 037F
        sub_ si,ax                                      ; 0381
        sbb_ di,dx                                      ; 0383
        les bx,[bp+0x8]                                 ; 0385
        mov [es:bx+0x2],si                              ; 0388
        mov [es:bx+0x4],di                              ; 038C
        jmp short L6_0326                               ; 0390

L6_0392:
        pop si                                          ; 0392
        pop di                                          ; 0393
        mov_ sp,bp                                      ; 0394
        pop bp                                          ; 0396
        ret 0xc                                         ; 0397

L6_039A:
        push bp                                         ; 039A
        mov_ bp,sp                                      ; 039B
        sub sp,byte +0xc                                ; 039D
        mov ax,[bp+0xc]                                 ; 03A0
        mov_ bx,ax                                      ; 03A3
        mov [bp-0x2],ax                                 ; 03A5
        mov ax,[bx]                                     ; 03A8
        mov [bp-0xc],ax                                 ; 03AA
        mov_ bx,ax                                      ; 03AD
        mov ax,[bp+0xc]                                 ; 03AF
        mov dx,[bp+0xe]                                 ; 03B2
        cmp [bx+0x101],ax                               ; 03B5
        jnz short L6_03C1                               ; 03B9
        cmp [bx+0x103],dx                               ; 03BB
        jz short L6_03C9                                ; 03BF

L6_03C1:
        mov ax,0xb                                      ; 03C1

L6_03C4:
        xor_ dx,dx                                      ; 03C4
        jmp near L6_0469                                ; 03C6

L6_03C9:
        cmp word [bp+0x6],byte +0x0                     ; 03C9
        jnz short L6_03D5                               ; 03CD
        cmp word [bp+0x4],byte +0x8                     ; 03CF
        jc short L6_03C1                                ; 03D3

L6_03D5:
        test byte [bx+0x100],0x3                        ; 03D5
        jnz short L6_03E1                               ; 03DA
        mov ax,0x1                                      ; 03DC
        jmp short L6_03C4                               ; 03DF

L6_03E1:
        push bx                                         ; 03E1
        sub_ ax,ax                                      ; 03E2
        push ax                                         ; 03E4
        push ax                                         ; 03E5
        callf L1_1484, R6_03E9, R6_0465                 ; 03E6 far seg1
        mov [bp-0x6],ax                                 ; 03EB
        mov bx,[bp-0xc]                                 ; 03EE
        mov ax,[bx+0x107]                               ; 03F1
        mov [bp-0xa],ax                                 ; 03F5
        mov ax,[bp-0x6]                                 ; 03F8
        mov bx,[bp-0x2]                                 ; 03FB
        cmp [bx+0x12],dx                                ; 03FE
        ja short L6_0413                                ; 0401
        jc short L6_040A                                ; 0403
        cmp [bx+0x10],ax                                ; 0405
        jnc short L6_0413                               ; 0408

L6_040A:
        mov ax,[bx+0x10]                                ; 040A
        mov dx,[bx+0x12]                                ; 040D
        mov [bp-0x6],ax                                 ; 0410

L6_0413:
        les bx,[bp+0x8]                                 ; 0413
        cmp word [es:bx],byte +0x4                      ; 0416
        jnz short L6_0426                               ; 041A
        mov [es:bx+0x2],ax                              ; 041C
        mov [es:bx+0x4],dx                              ; 0420
        jmp short L6_0452                               ; 0424

L6_0426:
        mov word [es:bx],0x2                            ; 0426
        mov [es:bx+0x2],ax                              ; 042B
        mov [es:bx+0x4],dx                              ; 042F
        mov al,[bp-0xa]                                 ; 0433
        and ax,strict word 0x3                          ; 0436
        sub_ dx,dx                                      ; 0439
        sub ax,strict word 0x1                          ; 043B
        jc short L6_0452                                ; 043E
        sub ax,strict word 0x1                          ; 0440
        jna short L6_044A                               ; 0443
        dec ax                                          ; 0445
        jz short L6_0457                                ; 0446
        jmp short L6_0452                               ; 0448

L6_044A:
        shr word [es:bx+0x4],1                          ; 044A
        rcr word [es:bx+0x2],1                          ; 044E

L6_0452:
        xor_ ax,ax                                      ; 0452
        jmp near L6_03C4                                ; 0454

L6_0457:
        mov al,0x2                                      ; 0457
        push ax                                         ; 0459
        mov_ ax,bx                                      ; 045A
        mov dx,es                                       ; 045C
        inc ax                                          ; 045E
        inc ax                                          ; 045F
        push es                                         ; 0460
        push ax                                         ; 0461
        callf L1_232A, R6_0465, R6_05AB                 ; 0462 far seg1
        jmp short L6_0452                               ; 0467

L6_0469:
        mov_ sp,bp                                      ; 0469
        pop bp                                          ; 046B
        ret 0xc                                         ; 046C
        db 0x90                                         ; 046F

L6_0470:
        push bp                                         ; 0470
        mov_ bp,sp                                      ; 0471
        sub sp,byte +0x8                                ; 0473
        push di                                         ; 0476
        push si                                         ; 0477
        mov di,[bp+0x4]                                 ; 0478
        cmp word [di+0x76],byte +0x0                    ; 047B
        jz short L6_0486                                ; 047F
        xor_ ax,ax                                      ; 0481
        jmp near L6_055E                                ; 0483

L6_0486:
        mov ax,[di+0x26]                                ; 0486
        mov dx,[di+0x28]                                ; 0489
        mov_ si,ax                                      ; 048C
        mov [bp-0x6],dx                                 ; 048E
        mov ax,[di+0x74]                                ; 0491
        lea bx,[di+0x7c]                                ; 0494
        mov [bx+0x4],ax                                 ; 0497
        push di                                         ; 049A
        callf L6_2AB6, R6_049E, R6_05E8                 ; 049B far seg6
        mov ax,[di+0x78]                                ; 04A0
        mov dx,[di+0x7a]                                ; 04A3
        mov [di+0x7c],ax                                ; 04A6
        mov [di+0x7e],dx                                ; 04A9
        mov ax,[di+0x6b]                                ; 04AC
        mov dx,[di+0x6d]                                ; 04AF
        lea bx,[di+0x7c]                                ; 04B2
        mov [bx+0x6],ax                                 ; 04B5
        mov [bx+0x8],dx                                 ; 04B8
        mov ax,[di+0x8c]                                ; 04BB
        mov dx,[di+0x8e]                                ; 04BF
        mov [bx+0xa],ax                                 ; 04C3
        mov [bx+0xc],dx                                 ; 04C6
        mov ax,[di+0x3d]                                ; 04C9
        mov [bx+0xe],ax                                 ; 04CC
        mov word [di+0x76],0x1                          ; 04CF
        sub_ ax,ax                                      ; 04D4
        mov [bp-0x2],ax                                 ; 04D6
        mov [bp-0x4],ax                                 ; 04D9
        mov es,[bp-0x6]                                 ; 04DC
        cmp [es:si+0xa50],ax                            ; 04DF
        jnz short L6_04ED                               ; 04E4
        cmp [es:si+0xa4e],ax                            ; 04E6
        jz short L6_054B                                ; 04EB

L6_04ED:
        mov ax,0x1                                      ; 04ED
        mov cl,[bp-0x4]                                 ; 04F0
        shl ax,cl                                       ; 04F3
        cwd                                             ; 04F5
        and ax,[es:si+0x281c]                           ; 04F6
        and dx,[es:si+0x281e]                           ; 04FB
        or_ dx,ax                                       ; 0500
        jz short L6_052A                                ; 0502
        push di                                         ; 0504
        mov ax,0x2                                      ; 0505
        push ax                                         ; 0508
        mov bx,[bp-0x4]                                 ; 0509
        add_ bx,bx                                      ; 050C
        add_ bx,si                                      ; 050E
        mov bx,[es:bx+0x2708]                           ; 0510
        add_ bx,bx                                      ; 0515
        add_ bx,si                                      ; 0517
        push word [es:bx+0x26b8]                        ; 0519
        xor_ ax,ax                                      ; 051E
        push ax                                         ; 0520
        mov ax,0x10                                     ; 0521
        push ax                                         ; 0524
        callf L5_0538, R6_0528, R6_0652                 ; 0525 far seg5

L6_052A:
        add word [bp-0x4],byte +0x1                     ; 052A
        adc word [bp-0x2],byte +0x0                     ; 052E
        mov ax,[bp-0x4]                                 ; 0532
        mov dx,[bp-0x2]                                 ; 0535
        mov es,[bp-0x6]                                 ; 0538
        cmp [es:si+0xa50],dx                            ; 053B
        ja short L6_04ED                                ; 0540
        jc short L6_054B                                ; 0542
        cmp [es:si+0xa4e],ax                            ; 0544
        ja short L6_04ED                                ; 0549

L6_054B:
        sub_ ax,ax                                      ; 054B
        mov [di+0x8e],ax                                ; 054D
        mov [di+0x8c],ax                                ; 0551
        push di                                         ; 0555
        callf wid_release, R6_0559, R6_05A1             ; 0556 far seg4
        mov ax,0x1                                      ; 055B

L6_055E:
        pop si                                          ; 055E
        pop di                                          ; 055F
        mov_ sp,bp                                      ; 0560
        pop bp                                          ; 0562
        ret 0x2                                         ; 0563

L6_0566:
        push bp                                         ; 0566
        mov_ bp,sp                                      ; 0567
        sub sp,byte +0x8                                ; 0569
        push di                                         ; 056C
        push si                                         ; 056D
        mov di,[bp+0x4]                                 ; 056E
        mov ax,[di+0x26]                                ; 0571
        mov dx,[di+0x28]                                ; 0574
        mov [bp-0x8],ax                                 ; 0577
        mov [bp-0x6],dx                                 ; 057A
        cmp word [di+0x76],byte +0x0                    ; 057D
        jnz short L6_0588                               ; 0581

L6_0583:
        xor_ ax,ax                                      ; 0583
        jmp near L6_0678                                ; 0585

L6_0588:
        mov ax,[di+0x78]                                ; 0588
        mov dx,[di+0x7a]                                ; 058B
        lea si,[di+0x7c]                                ; 058E
        cmp [si],ax                                     ; 0591
        jnz short L6_0583                               ; 0593
        cmp [si+0x2],dx                                 ; 0595
        jnz short L6_0583                               ; 0598
        mov [bp-0x2],si                                 ; 059A
        push di                                         ; 059D
        callf wid_acquire, R6_05A1, 0xFFFF              ; 059E far seg4
        or_ ax,ax                                       ; 05A3
        jnz short L6_0583                               ; 05A5
        push di                                         ; 05A7
        callf dsp_reset, R6_05AB, R6_0045               ; 05A8 far seg1
        mov ax,[di+0x78]                                ; 05AD
        mov dx,[di+0x7a]                                ; 05B0
        mov [di+0x65],ax                                ; 05B3
        mov [di+0x67],dx                                ; 05B6
        mov ax,[si+0xa]                                 ; 05B9
        mov dx,[si+0xc]                                 ; 05BC
        mov [di+0x8c],ax                                ; 05BF
        mov [di+0x8e],dx                                ; 05C3
        mov ax,[si+0x6]                                 ; 05C7
        mov dx,[si+0x8]                                 ; 05CA
        mov [di+0x6b],ax                                ; 05CD
        mov [di+0x6d],dx                                ; 05D0
        mov bx,[si]                                     ; 05D3
        mov cx,[bx+0x1c]                                ; 05D5
        mov [di+0x69],cx                                ; 05D8
        push di                                         ; 05DB
        push cx                                         ; 05DC
        push word [bx+0x1a]                             ; 05DD
        mov cl,0x1                                      ; 05E0
        push cx                                         ; 05E2
        push dx                                         ; 05E3
        push ax                                         ; 05E4
        callf L6_1FF4, R6_05E8, R6_05F4                 ; 05E5 far seg6
        cmp word [si+0x4],byte +0x0                     ; 05EA
        jz short L6_05F6                                ; 05EE
        push di                                         ; 05F0
        callf L6_26EA, R6_05F4, 0xFFFF                  ; 05F1 far seg6

L6_05F6:
        mov word [di+0x76],0x0                          ; 05F6
        sub_ ax,ax                                      ; 05FB
        mov [bp-0x2],ax                                 ; 05FD
        mov [bp-0x4],ax                                 ; 0600
        les bx,[bp-0x8]                                 ; 0603
        cmp [es:bx+0xa50],ax                            ; 0606
        jnz short L6_0614                               ; 060B
        cmp [es:bx+0xa4e],ax                            ; 060D
        jz short L6_0675                                ; 0612

L6_0614:
        mov_ si,bx                                      ; 0614

L6_0616:
        mov ax,0x1                                      ; 0616
        mov cl,[bp-0x4]                                 ; 0619
        shl ax,cl                                       ; 061C
        cwd                                             ; 061E
        and ax,[es:si+0x281c]                           ; 061F
        and dx,[es:si+0x281e]                           ; 0624
        or_ dx,ax                                       ; 0629
        jz short L6_0654                                ; 062B
        push di                                         ; 062D
        mov ax,0x2                                      ; 062E
        push ax                                         ; 0631
        mov bx,[bp-0x4]                                 ; 0632
        add_ bx,bx                                      ; 0635
        add_ bx,si                                      ; 0637
        mov bx,[es:bx+0x2708]                           ; 0639
        add_ bx,bx                                      ; 063E
        add_ bx,si                                      ; 0640
        push word [es:bx+0x26b8]                        ; 0642
        mov ax,0x1                                      ; 0647
        push ax                                         ; 064A
        mov ax,0x10                                     ; 064B
        push ax                                         ; 064E
        callf L5_0538, R6_0652, 0xFFFF                  ; 064F far seg5

L6_0654:
        add word [bp-0x4],byte +0x1                     ; 0654
        adc word [bp-0x2],byte +0x0                     ; 0658
        mov ax,[bp-0x4]                                 ; 065C
        mov dx,[bp-0x2]                                 ; 065F
        mov es,[bp-0x6]                                 ; 0662
        cmp [es:si+0xa50],dx                            ; 0665
        ja short L6_0616                                ; 066A
        jc short L6_0675                                ; 066C
        cmp [es:si+0xa4e],ax                            ; 066E
        ja short L6_0616                                ; 0673

L6_0675:
        mov ax,0x1                                      ; 0675

L6_0678:
        pop si                                          ; 0678
        pop di                                          ; 0679
        mov_ sp,bp                                      ; 067A
        pop bp                                          ; 067C
        ret 0x2                                         ; 067D

L6_0680:
        push bp                                         ; 0680
        mov_ bp,sp                                      ; 0681
        sub sp,byte +0x12                               ; 0683
        push di                                         ; 0686
        push si                                         ; 0687
        mov word [bp-0xc],0x0                           ; 0688
        cmp word [bp+0x4],byte +0x0                     ; 068D
        jnz short L6_0699                               ; 0691
        cmp word [bp+0x6],byte +0x1                     ; 0693
        jz short L6_06AD                                ; 0697

L6_0699:
        cmp word [bp+0x4],byte +0x0                     ; 0699
        jnz short L6_06A5                               ; 069D
        cmp word [bp+0x6],byte +0x4                     ; 069F
        jz short L6_06AD                                ; 06A3

L6_06A5:
        mov word [0x18],0x0                             ; 06A5
        jmp short L6_06B7                               ; 06AB

L6_06AD:
        mov word [0x1a],0x0                             ; 06AD
        inc word [0x18]                                 ; 06B3

L6_06B7:
        mov byte [bp-0x5],0x1                           ; 06B7
        les bx,[bp+0x8]                                 ; 06BB
        mov ax,[es:bx+0x2]                              ; 06BE
        mov dx,[es:bx+0x4]                              ; 06C2
        mov_ si,ax                                      ; 06C6
        mov [bp-0xe],dx                                 ; 06C8
        mov_ di,ax                                      ; 06CB
        mov [bp-0x2],dx                                 ; 06CD
        mov_ bx,ax                                      ; 06D0
        mov es,dx                                       ; 06D2
        mov ax,[es:bx]                                  ; 06D4
        dec ax                                          ; 06D7
        jz short L6_06E9                                ; 06D8
        sub ax,strict word 0x60                         ; 06DA
        jz short L6_075A                                ; 06DD
        mov byte [bp-0x5],0x0                           ; 06DF

L6_06E3:
        mov [bp-0x10],si                                ; 06E3
        jmp near L6_07AC                                ; 06E6

L6_06E9:
        mov es,dx                                       ; 06E9
        mov ax,[es:di+0xe]                              ; 06EB
        mov [bp-0x12],ax                                ; 06EF
        cmp ax,strict word 0x10                         ; 06F2
        jz short L6_0700                                ; 06F5
        cmp ax,strict word 0x8                          ; 06F7
        jz short L6_0700                                ; 06FA
        mov byte [bp-0x5],0x0                           ; 06FC

L6_0700:
        mov es,[bp-0xe]                                 ; 0700
        cmp word [es:si+0x2],byte +0x1                  ; 0703
        jz short L6_0715                                ; 0708
        cmp word [es:si+0x2],byte +0x2                  ; 070A
        jz short L6_0715                                ; 070F
        mov byte [bp-0x5],0x0                           ; 0711

L6_0715:
        cmp word [es:si+0x6],byte +0x0                  ; 0715
        jnz short L6_0724                               ; 071A
        cmp word [es:si+0x4],0xfa0                      ; 071C
        jc short L6_0733                                ; 0722

L6_0724:
        cmp word [es:si+0x6],byte +0x0                  ; 0724
        jnz short L6_0733                               ; 0729
        cmp word [es:si+0x4],0xbf68                     ; 072B
        jna short L6_0737                               ; 0731

L6_0733:
        mov byte [bp-0x5],0x0                           ; 0733

L6_0737:
        cmp word [bp-0x12],byte +0x10                   ; 0737
        jnz short L6_0742                               ; 073B
        mov ax,0x1                                      ; 073D
        jmp short L6_0744                               ; 0740

L6_0742:
        xor_ ax,ax                                      ; 0742

L6_0744:
        mov [bp-0xc],ax                                 ; 0744
        cmp word [es:si+0x2],byte +0x2                  ; 0747
        jnz short L6_0753                               ; 074C
        mov ax,0x2                                      ; 074E
        jmp short L6_0755                               ; 0751

L6_0753:
        xor_ ax,ax                                      ; 0753

L6_0755:
        or [bp-0xc],ax                                  ; 0755
        jmp short L6_06E3                               ; 0758

L6_075A:
        mov bx,[bp+0x10]                                ; 075A
        test byte [bx+0x2b],0x8                         ; 075D
        jz short L6_0767                                ; 0761
        mov byte [bp-0x5],0x0                           ; 0763

L6_0767:
        mov es,dx                                       ; 0767
        cmp word [es:di+0xe],byte +0x4                  ; 0769
        jz short L6_0774                                ; 076E
        mov byte [bp-0x5],0x0                           ; 0770

L6_0774:
        mov es,[bp-0xe]                                 ; 0774
        cmp word [es:si+0x2],byte +0x1                  ; 0777
        jz short L6_0782                                ; 077C
        mov byte [bp-0x5],0x0                           ; 077E

L6_0782:
        cmp word [es:si+0x6],byte +0x0                  ; 0782
        jnz short L6_0791                               ; 0787
        cmp word [es:si+0x4],0xfa0                      ; 0789
        jc short L6_07B9                                ; 078F

L6_0791:
        mov [bp-0x10],si                                ; 0791
        cmp word [es:si+0x6],byte +0x0                  ; 0794
        jnz short L6_07A3                               ; 0799
        cmp word [es:si+0x4],0x2bf2                     ; 079B
        jna short L6_07A7                               ; 07A1

L6_07A3:
        mov byte [bp-0x5],0x0                           ; 07A3

L6_07A7:
        mov word [bp-0xc],0x4                           ; 07A7

L6_07AC:
        cmp byte [bp-0x5],0x0                           ; 07AC
        jnz short L6_07BE                               ; 07B0
        mov ax,0x20                                     ; 07B2
        cwd                                             ; 07B5
        jmp near L6_0A0E                                ; 07B6

L6_07B9:
        mov [bp-0x10],si                                ; 07B9
        jmp short L6_07A3                               ; 07BC

L6_07BE:
        test byte [bp+0x4],0x1                          ; 07BE
        jz short L6_07C7                                ; 07C2
        jmp near L6_0A0A                                ; 07C4

L6_07C7:
        mov di,[bp+0x10]                                ; 07C7
        cmp word [di+0x20],byte +0x0                    ; 07CA
        jz short L6_07D6                                ; 07CE
        push cs                                         ; 07D0
        call L6_0000                                    ; 07D1
        jmp short L6_0820                               ; 07D4

L6_07D6:
        mov ax,[di+0x26]                                ; 07D6
        mov dx,[di+0x28]                                ; 07D9
        mov_ si,ax                                      ; 07DC
        mov [bp-0x6],dx                                 ; 07DE
        mov al,[di+0x5d]                                ; 07E1
        and al,0x3                                      ; 07E4
        cmp al,0x2                                      ; 07E6
        jnz short L6_07FA                               ; 07E8
        mov ax,[di+0x7a]                                ; 07EA
        or ax,[di+0x78]                                 ; 07ED
        jz short L6_07FA                                ; 07F0
        push di                                         ; 07F2
        call L6_0470                                    ; 07F3
        or_ ax,ax                                       ; 07F6
        jz short L6_0820                                ; 07F8

L6_07FA:
        mov al,[di+0x5d]                                ; 07FA
        and al,0x3                                      ; 07FD
        cmp al,0x2                                      ; 07FF
        jz short L6_0820                                ; 0801
        mov [bp-0x8],si                                 ; 0803
        push di                                         ; 0806
        callf wid_acquire, R6_080A, R6_081E             ; 0807 far seg4
        or_ ax,ax                                       ; 080C
        jnz short L6_0820                               ; 080E
        push di                                         ; 0810
        callf dsp_reset, R6_0814, R6_0A08               ; 0811 far seg1
        or_ ax,ax                                       ; 0816
        jnz short L6_0826                               ; 0818
        push di                                         ; 081A
        callf wid_release, R6_081E, R6_083E             ; 081B far seg4

L6_0820:
        mov ax,0x4                                      ; 0820
        jmp near L6_0A0C                                ; 0823

L6_0826:
        mov ax,0x40                                     ; 0826
        push ax                                         ; 0829
        mov ax,0x28                                     ; 082A
        push ax                                         ; 082D
        callp R6_082F, 0xFFFF, 0x0000                   ; 082E KERNEL.LocalAlloc
        mov [bp-0xa],ax                                 ; 0833
        or_ ax,ax                                       ; 0836
        jnz short L6_0846                               ; 0838
        push di                                         ; 083A
        callf wid_release, R6_083E, R6_0559             ; 083B far seg4
        mov ax,0x7                                      ; 0840
        jmp near L6_0A0C                                ; 0843

L6_0846:
        mov_ bx,di                                      ; 0846
        sub_ ax,ax                                      ; 0848
        mov [bx+0xca],ax                                ; 084A
        mov [bx+0xc8],ax                                ; 084E
        mov [bx+0x8e],ax                                ; 0852
        mov [bx+0x8c],ax                                ; 0856
        les si,[bp+0x8]                                 ; 085A
        mov di,[bp-0xa]                                 ; 085D
        mov ax,[es:si+0x6]                              ; 0860
        mov dx,[es:si+0x8]                              ; 0864
        mov [di+0x2],ax                                 ; 0868
        mov [di+0x4],dx                                 ; 086B
        mov ax,[es:si+0xa]                              ; 086E
        mov dx,[es:si+0xc]                              ; 0872
        mov [di+0x6],ax                                 ; 0876
        mov [di+0x8],dx                                 ; 0879
        mov ax,[es:si]                                  ; 087C
        mov [di+0xa],ax                                 ; 087F
        mov ax,[bp+0x4]                                 ; 0882
        mov dx,[bp+0x6]                                 ; 0885
        mov [di+0xc],ax                                 ; 0888
        mov [di+0xe],dx                                 ; 088B
        sub_ ax,ax                                      ; 088E
        mov [di+0x12],ax                                ; 0890
        mov [di+0x10],ax                                ; 0893
        mov [di+0x16],ax                                ; 0896
        mov [di+0x14],ax                                ; 0899
        mov ax,[bp-0x10]                                ; 089C
        mov dx,[bp-0xe]                                 ; 089F
        push ds                                         ; 08A2
        add di,byte +0x18                               ; 08A3
        mov_ si,ax                                      ; 08A6
        push ds                                         ; 08A8
        pop es                                          ; 08A9
        mov ds,dx                                       ; 08AA
        mov cx,0x8                                      ; 08AC
        rep movsw                                       ; 08AF
        pop ds                                          ; 08B1
        mov si,[bp-0xa]                                 ; 08B2
        mov [si],bx                                     ; 08B5
        mov ax,[si+0x1c]                                ; 08B7
        mov [bx+0x69],ax                                ; 08BA
        push bx                                         ; 08BD
        push ax                                         ; 08BE
        push word [si+0x1a]                             ; 08BF
        mov al,0x1                                      ; 08C2
        push ax                                         ; 08C4
        mov ax,[bp-0xc]                                 ; 08C5
        sub_ dx,dx                                      ; 08C8
        push dx                                         ; 08CA
        push ax                                         ; 08CB
        callf L6_1FF4, R6_08CF, R6_0A57                 ; 08CC far seg6
        mov bx,[bp+0x10]                                ; 08D1
        mov ax,[0x14]                                   ; 08D4
        mov [bx+0x65],si                                ; 08D7
        mov [bx+0x67],ax                                ; 08DA
        inc word [0x14]                                 ; 08DD
        mov cx,[bp+0xe]                                 ; 08E1
        mov bx,[bp+0xc]                                 ; 08E4
        mov es,cx                                       ; 08E7
        mov [es:bx],si                                  ; 08E9
        mov [es:bx+0x2],ax                              ; 08EC
        cmp word [0x18],byte +0x2                       ; 08F0
        jz short L6_08FA                                ; 08F5
        jmp near L6_097F                                ; 08F7

L6_08FA:
        sub_ ax,ax                                      ; 08FA
        mov [bp-0x2],ax                                 ; 08FC
        mov [bp-0x4],ax                                 ; 08FF
        les bx,[bp-0x8]                                 ; 0902
        cmp [es:bx+0xa50],ax                            ; 0905
        jnz short L6_0916                               ; 090A
        cmp [es:bx+0xa4e],ax                            ; 090C
        jnz short L6_0916                               ; 0911
        jmp near L6_09FA                                ; 0913

L6_0916:
        mov_ si,bx                                      ; 0916
        mov di,[bp+0x10]                                ; 0918

L6_091B:
        mov ax,0x1                                      ; 091B
        mov cl,[bp-0x4]                                 ; 091E
        shl ax,cl                                       ; 0921
        cwd                                             ; 0923
        and ax,[es:si+0x281c]                           ; 0924
        and dx,[es:si+0x281e]                           ; 0929
        or_ dx,ax                                       ; 092E
        jz short L6_0959                                ; 0930
        push di                                         ; 0932
        mov ax,0x2                                      ; 0933
        push ax                                         ; 0936
        mov bx,[bp-0x4]                                 ; 0937
        add_ bx,bx                                      ; 093A
        add_ bx,si                                      ; 093C
        mov bx,[es:bx+0x2708]                           ; 093E
        add_ bx,bx                                      ; 0943
        add_ bx,si                                      ; 0945
        push word [es:bx+0x26b8]                        ; 0947
        mov ax,0x1                                      ; 094C
        push ax                                         ; 094F
        mov ax,0x10                                     ; 0950
        push ax                                         ; 0953
        callf L5_0538, R6_0957, R6_09D7                 ; 0954 far seg5

L6_0959:
        add word [bp-0x4],byte +0x1                     ; 0959
        adc word [bp-0x2],byte +0x0                     ; 095D
        mov ax,[bp-0x4]                                 ; 0961
        mov dx,[bp-0x2]                                 ; 0964
        mov es,[bp-0x6]                                 ; 0967
        cmp [es:si+0xa50],dx                            ; 096A
        ja short L6_091B                                ; 096F
        jnc short L6_0976                               ; 0971
        jmp near L6_09FA                                ; 0973

L6_0976:
        cmp [es:si+0xa4e],ax                            ; 0976
        ja short L6_091B                                ; 097B
        jmp short L6_09FA                               ; 097D

L6_097F:
        sub_ ax,ax                                      ; 097F
        mov [bp-0x2],ax                                 ; 0981
        mov [bp-0x4],ax                                 ; 0984
        mov si,[bp-0x8]                                 ; 0987
        mov es,[bp-0x6]                                 ; 098A
        cmp [es:si+0x9bc],ax                            ; 098D
        jnz short L6_099B                               ; 0992
        cmp [es:si+0x9ba],ax                            ; 0994
        jz short L6_09FA                                ; 0999

L6_099B:
        mov di,[bp+0x10]                                ; 099B

L6_099E:
        mov ax,0x1                                      ; 099E
        mov cl,[bp-0x4]                                 ; 09A1
        shl ax,cl                                       ; 09A4
        cwd                                             ; 09A6
        and ax,[es:si+0x2814]                           ; 09A7
        and dx,[es:si+0x2816]                           ; 09AC
        or_ dx,ax                                       ; 09B1
        jz short L6_09D9                                ; 09B3
        push di                                         ; 09B5
        mov ax,0x1                                      ; 09B6
        push ax                                         ; 09B9
        mov bx,[bp-0x4]                                 ; 09BA
        add_ bx,bx                                      ; 09BD
        add_ bx,si                                      ; 09BF
        mov bx,[es:bx+0x26f4]                           ; 09C1
        add_ bx,bx                                      ; 09C6
        add_ bx,si                                      ; 09C8
        push word [es:bx+0x26a4]                        ; 09CA
        push ax                                         ; 09CF
        mov ax,0x10                                     ; 09D0
        push ax                                         ; 09D3
        callf L5_0538, R6_09D7, R6_0528                 ; 09D4 far seg5

L6_09D9:
        add word [bp-0x4],byte +0x1                     ; 09D9
        adc word [bp-0x2],byte +0x0                     ; 09DD
        mov ax,[bp-0x4]                                 ; 09E1
        mov dx,[bp-0x2]                                 ; 09E4
        mov es,[bp-0x6]                                 ; 09E7
        cmp [es:si+0x9bc],dx                            ; 09EA
        ja short L6_099E                                ; 09EF
        jc short L6_09FA                                ; 09F1
        cmp [es:si+0x9ba],ax                            ; 09F3
        ja short L6_099E                                ; 09F8

L6_09FA:
        push word [bp-0xa]                              ; 09FA
        mov ax,0x3be                                    ; 09FD
        push ax                                         ; 0A00
        sub_ ax,ax                                      ; 0A01
        push ax                                         ; 0A03
        push ax                                         ; 0A04
        callf L1_0010, R6_0A08, R6_03E9                 ; 0A05 far seg1

L6_0A0A:
        xor_ ax,ax                                      ; 0A0A

L6_0A0C:
        xor_ dx,dx                                      ; 0A0C

L6_0A0E:
        pop si                                          ; 0A0E
        pop di                                          ; 0A0F
        mov_ sp,bp                                      ; 0A10
        pop bp                                          ; 0A12
        ret 0xe                                         ; 0A13

L6_0A16:
        push bp                                         ; 0A16
        mov_ bp,sp                                      ; 0A17
        push si                                         ; 0A19
        mov ax,[bp+0x4]                                 ; 0A1A
        mov_ bx,ax                                      ; 0A1D
        mov dx,[bp+0x6]                                 ; 0A1F
        mov si,[bx]                                     ; 0A22
        cmp [si+0x65],ax                                ; 0A24
        jnz short L6_0A2E                               ; 0A27
        cmp [si+0x67],dx                                ; 0A29
        jz short L6_0A43                                ; 0A2C

L6_0A2E:
        mov ax,[si+0x78]                                ; 0A2E
        mov dx,[si+0x7a]                                ; 0A31
        cmp_ bx,ax                                      ; 0A34
        jnz short L6_0A6D                               ; 0A36
        cmp [bp+0x6],dx                                 ; 0A38
        jnz short L6_0A6D                               ; 0A3B
        cmp word [si+0x76],byte +0x0                    ; 0A3D
        jz short L6_0A6D                                ; 0A41

L6_0A43:
        mov ax,[si+0x65]                                ; 0A43
        mov dx,[si+0x67]                                ; 0A46
        cmp [bp+0x4],ax                                 ; 0A49
        jnz short L6_0A5B                               ; 0A4C
        cmp [bp+0x6],dx                                 ; 0A4E
        jnz short L6_0A5B                               ; 0A51
        push si                                         ; 0A53
        callf L6_26EA, R6_0A57, R6_0AC3                 ; 0A54 far seg6
        jmp short L6_0A77                               ; 0A59

L6_0A5B:
        mov ax,[si+0x78]                                ; 0A5B
        mov dx,[si+0x7a]                                ; 0A5E
        lea bx,[si+0x7c]                                ; 0A61
        cmp [bx],ax                                     ; 0A64
        jnz short L6_0A6D                               ; 0A66
        cmp [bx+0x2],dx                                 ; 0A68
        jz short L6_0A72                                ; 0A6B

L6_0A6D:
        mov ax,0xb                                      ; 0A6D
        jmp short L6_0A79                               ; 0A70

L6_0A72:
        mov word [bx+0x4],0x1                           ; 0A72

L6_0A77:
        xor_ ax,ax                                      ; 0A77

L6_0A79:
        xor_ dx,dx                                      ; 0A79
        pop si                                          ; 0A7B
        mov_ sp,bp                                      ; 0A7C
        pop bp                                          ; 0A7E
        ret 0x4                                         ; 0A7F

L6_0A82:
        push bp                                         ; 0A82
        mov_ bp,sp                                      ; 0A83
        push si                                         ; 0A85
        mov ax,[bp+0x4]                                 ; 0A86
        mov_ bx,ax                                      ; 0A89
        mov dx,[bp+0x6]                                 ; 0A8B
        mov si,[bx]                                     ; 0A8E
        cmp [si+0x65],ax                                ; 0A90
        jnz short L6_0A9A                               ; 0A93
        cmp [si+0x67],dx                                ; 0A95
        jz short L6_0AAF                                ; 0A98

L6_0A9A:
        mov ax,[si+0x78]                                ; 0A9A
        mov dx,[si+0x7a]                                ; 0A9D
        cmp_ bx,ax                                      ; 0AA0
        jnz short L6_0AD9                               ; 0AA2
        cmp [bp+0x6],dx                                 ; 0AA4
        jnz short L6_0AD9                               ; 0AA7
        cmp word [si+0x76],byte +0x0                    ; 0AA9
        jz short L6_0AD9                                ; 0AAD

L6_0AAF:
        mov ax,[si+0x65]                                ; 0AAF
        mov dx,[si+0x67]                                ; 0AB2
        cmp [bp+0x4],ax                                 ; 0AB5
        jnz short L6_0AC7                               ; 0AB8
        cmp [bp+0x6],dx                                 ; 0ABA
        jnz short L6_0AC7                               ; 0ABD
        push si                                         ; 0ABF
        callf L6_2AB6, R6_0AC3, R6_049E                 ; 0AC0 far seg6
        jmp short L6_0AE3                               ; 0AC5

L6_0AC7:
        mov ax,[si+0x78]                                ; 0AC7
        mov dx,[si+0x7a]                                ; 0ACA
        lea bx,[si+0x7c]                                ; 0ACD
        cmp [bx],ax                                     ; 0AD0
        jnz short L6_0AD9                               ; 0AD2
        cmp [bx+0x2],dx                                 ; 0AD4
        jz short L6_0ADE                                ; 0AD7

L6_0AD9:
        mov ax,0xb                                      ; 0AD9
        jmp short L6_0AE5                               ; 0ADC

L6_0ADE:
        mov word [bx+0x4],0x0                           ; 0ADE

L6_0AE3:
        xor_ ax,ax                                      ; 0AE3

L6_0AE5:
        xor_ dx,dx                                      ; 0AE5
        pop si                                          ; 0AE7
        mov_ sp,bp                                      ; 0AE8
        pop bp                                          ; 0AEA
        ret 0x4                                         ; 0AEB

L6_0AEE:
        push bp                                         ; 0AEE
        mov_ bp,sp                                      ; 0AEF
        sub sp,byte +0x6                                ; 0AF1
        push si                                         ; 0AF4
        mov ax,[bp+0x8]                                 ; 0AF5
        mov_ bx,ax                                      ; 0AF8
        mov [bp-0x4],ax                                 ; 0AFA
        mov ax,[bx]                                     ; 0AFD
        mov [bp-0x2],ax                                 ; 0AFF
        mov_ bx,ax                                      ; 0B02
        mov cx,[bp+0x8]                                 ; 0B04
        mov dx,[bp+0xa]                                 ; 0B07
        cmp [bx+0x65],cx                                ; 0B0A
        jnz short L6_0B14                               ; 0B0D
        cmp [bx+0x67],dx                                ; 0B0F
        jz short L6_0B2C                                ; 0B12

L6_0B14:
        cmp [bx+0x78],cx                                ; 0B14
        jnz short L6_0B24                               ; 0B17
        cmp [bx+0x7a],dx                                ; 0B19
        jnz short L6_0B24                               ; 0B1C
        cmp word [bx+0x76],byte +0x0                    ; 0B1E
        jnz short L6_0B2C                               ; 0B22

L6_0B24:
        mov ax,0xb                                      ; 0B24

L6_0B27:
        xor_ dx,dx                                      ; 0B27
        jmp near L6_0BC6                                ; 0B29

L6_0B2C:
        les bx,[bp+0x4]                                 ; 0B2C
        test byte [es:bx+0x10],0x2                      ; 0B2F
        jnz short L6_0B3B                               ; 0B34
        mov ax,0x22                                     ; 0B36
        jmp short L6_0B27                               ; 0B39

L6_0B3B:
        mov ax,[bp-0x4]                                 ; 0B3B
        mov [es:bx+0x1c],ax                             ; 0B3E
        mov [es:bx+0x1e],ds                             ; 0B42
        or byte [es:bx+0x10],0x10                       ; 0B46
        and byte [es:bx+0x10],0xfe                      ; 0B4B
        sub_ ax,ax                                      ; 0B50
        mov [es:bx+0xa],ax                              ; 0B52
        mov [es:bx+0x8],ax                              ; 0B56
        mov ax,[bp+0x8]                                 ; 0B5A
        mov dx,[bp+0xa]                                 ; 0B5D
        mov bx,[bp-0x2]                                 ; 0B60
        cmp [bx+0x65],ax                                ; 0B63
        jnz short L6_0BA4                               ; 0B66
        cmp [bx+0x67],dx                                ; 0B68
        jnz short L6_0BA4                               ; 0B6B
        mov ax,[0xa6]                                   ; 0B6D
        inc word [0xa6]                                 ; 0B70
        or_ ax,ax                                       ; 0B74
        jnz short L6_0B79                               ; 0B76
        cli                                             ; 0B78

L6_0B79:
        mov bx,[bp-0x2]                                 ; 0B79
        push bx                                         ; 0B7C
        push word [bx+0x8e]                             ; 0B7D
        push word [bx+0x8c]                             ; 0B81
        push word [bp+0x6]                              ; 0B85
        push word [bp+0x4]                              ; 0B88
        callf L6_2AE2, R6_0B8E, R6_0BB9                 ; 0B8B far seg6
        mov bx,[bp-0x2]                                 ; 0B90
        mov [bx+0x8c],ax                                ; 0B93
        mov [bx+0x8e],dx                                ; 0B97
        dec word [0xa6]                                 ; 0B9B
        jnz short L6_0BC1                               ; 0B9F
        sti                                             ; 0BA1
        jmp short L6_0BC1                               ; 0BA2

L6_0BA4:
        lea ax,[bx+0x7c]                                ; 0BA4
        push bx                                         ; 0BA7
        mov_ bx,ax                                      ; 0BA8
        push word [bx+0xc]                              ; 0BAA
        push word [bx+0xa]                              ; 0BAD
        push es                                         ; 0BB0
        push word [bp+0x4]                              ; 0BB1
        mov_ si,ax                                      ; 0BB4
        callf L6_2AE2, R6_0BB9, R6_0C22                 ; 0BB6 far seg6
        mov [si+0xa],ax                                 ; 0BBB
        mov [si+0xc],dx                                 ; 0BBE

L6_0BC1:
        xor_ ax,ax                                      ; 0BC1
        jmp near L6_0B27                                ; 0BC3

L6_0BC6:
        pop si                                          ; 0BC6
        mov_ sp,bp                                      ; 0BC7
        pop bp                                          ; 0BC9
        ret 0x8                                         ; 0BCA
        db 0x90                                         ; 0BCD

L6_0BCE:
        push bp                                         ; 0BCE
        mov_ bp,sp                                      ; 0BCF
        sub sp,byte +0xa                                ; 0BD1
        mov ax,[bp+0x4]                                 ; 0BD4
        mov_ bx,ax                                      ; 0BD7
        mov [bp-0x4],ax                                 ; 0BD9
        mov ax,[bx]                                     ; 0BDC
        mov [bp-0x2],ax                                 ; 0BDE
        mov_ bx,ax                                      ; 0BE1
        mov cx,[bp+0x4]                                 ; 0BE3
        mov dx,[bp+0x6]                                 ; 0BE6
        cmp [bx+0x65],cx                                ; 0BE9
        jnz short L6_0BF3                               ; 0BEC
        cmp [bx+0x67],dx                                ; 0BEE
        jz short L6_0C0B                                ; 0BF1

L6_0BF3:
        cmp [bx+0x78],cx                                ; 0BF3
        jnz short L6_0C03                               ; 0BF6
        cmp [bx+0x7a],dx                                ; 0BF8
        jnz short L6_0C03                               ; 0BFB
        cmp word [bx+0x76],byte +0x0                    ; 0BFD
        jnz short L6_0C0B                               ; 0C01

L6_0C03:
        mov ax,0xb                                      ; 0C03

L6_0C06:
        xor_ dx,dx                                      ; 0C06
        jmp near L6_0CA5                                ; 0C08

L6_0C0B:
        mov ax,[bp+0x4]                                 ; 0C0B
        mov dx,[bp+0x6]                                 ; 0C0E
        mov bx,[bp-0x2]                                 ; 0C11
        cmp [bx+0x65],ax                                ; 0C14
        jnz short L6_0C5D                               ; 0C17
        cmp [bx+0x67],dx                                ; 0C19
        jnz short L6_0C5D                               ; 0C1C
        push bx                                         ; 0C1E
        callf L6_2AB6, R6_0C22, R6_0D25                 ; 0C1F far seg6
        mov ax,[0xa6]                                   ; 0C24
        inc word [0xa6]                                 ; 0C27
        or_ ax,ax                                       ; 0C2B
        jnz short L6_0C30                               ; 0C2D
        cli                                             ; 0C2F

L6_0C30:
        mov bx,[bp-0x2]                                 ; 0C30
        mov ax,[bx+0x8c]                                ; 0C33
        mov dx,[bx+0x8e]                                ; 0C37
        mov [bp-0x8],ax                                 ; 0C3B
        mov [bp-0x6],dx                                 ; 0C3E
        sub_ ax,ax                                      ; 0C41
        mov [bx+0x8e],ax                                ; 0C43
        mov [bx+0x8c],ax                                ; 0C47
        dec word [0xa6]                                 ; 0C4B
        jnz short L6_0C52                               ; 0C4F
        sti                                             ; 0C51

L6_0C52:
        push word [bp-0x6]                              ; 0C52
        push word [bp-0x8]                              ; 0C55
        call L6_001E                                    ; 0C58
        jmp short L6_0C86                               ; 0C5B

L6_0C5D:
        mov ax,[bx+0x78]                                ; 0C5D
        mov dx,[bx+0x7a]                                ; 0C60
        add bx,byte +0x7c                               ; 0C63
        mov [bp-0xa],bx                                 ; 0C66
        cmp [bx],ax                                     ; 0C69
        jnz short L6_0C03                               ; 0C6B
        cmp [bx+0x2],dx                                 ; 0C6D
        jnz short L6_0C03                               ; 0C70
        push word [bx+0xc]                              ; 0C72
        push word [bx+0xa]                              ; 0C75
        call L6_001E                                    ; 0C78
        mov bx,[bp-0xa]                                 ; 0C7B
        sub_ ax,ax                                      ; 0C7E
        mov [bx+0xc],ax                                 ; 0C80
        mov [bx+0xa],ax                                 ; 0C83

L6_0C86:
        mov bx,[bp-0x4]                                 ; 0C86
        sub_ ax,ax                                      ; 0C89
        mov [bx+0x12],ax                                ; 0C8B
        mov [bx+0x10],ax                                ; 0C8E
        mov [bx+0x16],ax                                ; 0C91
        mov [bx+0x14],ax                                ; 0C94
        mov bx,[bp-0x2]                                 ; 0C97
        mov [bx+0xca],ax                                ; 0C9A
        mov [bx+0xc8],ax                                ; 0C9E
        jmp near L6_0C06                                ; 0CA2

L6_0CA5:
        mov_ sp,bp                                      ; 0CA5
        pop bp                                          ; 0CA7
        ret 0x4                                         ; 0CA8
        db 0x90                                         ; 0CAB

L6_0CAC:
        push bp                                         ; 0CAC
        mov_ bp,sp                                      ; 0CAD
        sub sp,byte +0x2                                ; 0CAF
        push di                                         ; 0CB2
        push si                                         ; 0CB3
        mov di,[bp+0x4]                                 ; 0CB4
        mov ax,[di+0x78]                                ; 0CB7
        mov dx,[di+0x7a]                                ; 0CBA
        lea si,[di+0x7c]                                ; 0CBD
        cmp [si],ax                                     ; 0CC0
        jnz short L6_0CC9                               ; 0CC2
        cmp [si+0x2],dx                                 ; 0CC4
        jz short L6_0CCE                                ; 0CC7

L6_0CC9:
        mov ax,0xb                                      ; 0CC9
        jmp short L6_0D03                               ; 0CCC

L6_0CCE:
        mov ax,[si+0xc]                                 ; 0CCE
        or ax,[si+0xa]                                  ; 0CD1
        jz short L6_0CDB                                ; 0CD4
        mov ax,0x21                                     ; 0CD6
        jmp short L6_0D03                               ; 0CD9

L6_0CDB:
        sub_ ax,ax                                      ; 0CDB
        mov [di+0x7a],ax                                ; 0CDD
        mov [di+0x78],ax                                ; 0CE0
        mov [di+0x76],ax                                ; 0CE3
        mov ax,[si]                                     ; 0CE6
        mov [bp-0x2],ax                                 ; 0CE8
        push ax                                         ; 0CEB
        mov ax,0x3bf                                    ; 0CEC
        push ax                                         ; 0CEF
        sub_ ax,ax                                      ; 0CF0
        push ax                                         ; 0CF2
        push ax                                         ; 0CF3
        callf L1_0010, R6_0CF7, R6_0D61                 ; 0CF4 far seg1
        push word [bp-0x2]                              ; 0CF9
        callp R6_0CFD, 0xFFFF, 0x0000                   ; 0CFC KERNEL.LocalFree
        xor_ ax,ax                                      ; 0D01

L6_0D03:
        xor_ dx,dx                                      ; 0D03
        pop si                                          ; 0D05
        pop di                                          ; 0D06
        mov_ sp,bp                                      ; 0D07
        pop bp                                          ; 0D09
        ret 0x2                                         ; 0D0A
        db 0x90                                         ; 0D0D

L6_0D0E:
        push bp                                         ; 0D0E
        mov_ bp,sp                                      ; 0D0F
        push si                                         ; 0D11
        mov si,[bp+0x6]                                 ; 0D12
        test byte [si+0x6f],0x1                         ; 0D15
        jz short L6_0D35                                ; 0D19
        cmp word [si+0x74],byte +0x0                    ; 0D1B
        jz short L6_0D2B                                ; 0D1F
        push si                                         ; 0D21
        callf L6_2AB6, R6_0D25, R6_0D90                 ; 0D22 far seg6
        or byte [si+0x6f],0x8                           ; 0D27

L6_0D2B:
        push si                                         ; 0D2B
        callf wid_release, R6_0D2F, R6_0D51             ; 0D2C far seg4
        or byte [si+0x6f],0x4                           ; 0D31

L6_0D35:
        mov ax,0x1                                      ; 0D35
        pop si                                          ; 0D38
        mov_ sp,bp                                      ; 0D39
        pop bp                                          ; 0D3B
        retf 0x2                                        ; 0D3C
        db 0x90                                         ; 0D3F

L6_0D40:
        push bp                                         ; 0D40
        mov_ bp,sp                                      ; 0D41
        push si                                         ; 0D43
        mov si,[bp+0x6]                                 ; 0D44
        test byte [si+0x6f],0x4                         ; 0D47
        jz short L6_0DA2                                ; 0D4B
        push si                                         ; 0D4D
        callf wid_acquire, R6_0D51, R6_0DF4             ; 0D4E far seg4
        or_ ax,ax                                       ; 0D53
        jnz short L6_0D9E                               ; 0D55
        test byte [si+0x6f],0x8                         ; 0D57
        jz short L6_0D98                                ; 0D5B
        push si                                         ; 0D5D
        callf dsp_reset, R6_0D61, R6_0DEE               ; 0D5E far seg1
        or_ ax,ax                                       ; 0D63
        jz short L6_0D98                                ; 0D65
        test byte [si+0x6c],0x40                        ; 0D67
        jz short L6_0D72                                ; 0D6B
        and word [si+0x6b],0xbffe                       ; 0D6D

L6_0D72:
        push si                                         ; 0D72
        push word [si+0x69]                             ; 0D73
        mov al,[si+0x6b]                                ; 0D76
        and ax,strict word 0x2                          ; 0D79
        cmp ax,strict word 0x1                          ; 0D7C
        sbb_ ax,ax                                      ; 0D7F
        inc ax                                          ; 0D81
        inc ax                                          ; 0D82
        push ax                                         ; 0D83
        mov al,0x1                                      ; 0D84
        push ax                                         ; 0D86
        push word [si+0x6d]                             ; 0D87
        push word [si+0x6b]                             ; 0D8A
        callf L6_1FF4, R6_0D90, R6_0D96                 ; 0D8D far seg6
        push si                                         ; 0D92
        callf L6_26EA, R6_0D96, R6_0DE8                 ; 0D93 far seg6

L6_0D98:
        and byte [si+0x6f],0xf3                         ; 0D98
        jmp short L6_0DA2                               ; 0D9C

L6_0D9E:
        xor_ ax,ax                                      ; 0D9E
        jmp short L6_0DA5                               ; 0DA0

L6_0DA2:
        mov ax,0x1                                      ; 0DA2

L6_0DA5:
        pop si                                          ; 0DA5
        mov_ sp,bp                                      ; 0DA6
        pop bp                                          ; 0DA8
        retf 0x2                                        ; 0DA9

L6_0DAC:
        push bp                                         ; 0DAC
        mov_ bp,sp                                      ; 0DAD
        sub sp,byte +0xa                                ; 0DAF
        push di                                         ; 0DB2
        push si                                         ; 0DB3
        mov bx,[bp+0x4]                                 ; 0DB4
        mov di,[bx]                                     ; 0DB7
        mov ax,[di+0x26]                                ; 0DB9
        mov dx,[di+0x28]                                ; 0DBC
        mov_ si,ax                                      ; 0DBF
        mov [bp-0x6],dx                                 ; 0DC1
        mov dx,[bp+0x6]                                 ; 0DC4
        cmp [di+0x65],bx                                ; 0DC7
        jnz short L6_0DD1                               ; 0DCA
        cmp [di+0x67],dx                                ; 0DCC
        jz short L6_0DD4                                ; 0DCF

L6_0DD1:
        jmp near L6_0FC8                                ; 0DD1

L6_0DD4:
        mov ax,[di+0x8e]                                ; 0DD4
        or ax,[di+0x8c]                                 ; 0DD8
        jz short L6_0DE4                                ; 0DDC
        mov ax,0x21                                     ; 0DDE
        jmp near L6_0FE6                                ; 0DE1

L6_0DE4:
        push di                                         ; 0DE4
        callf L6_2AB6, R6_0DE8, R6_08CF                 ; 0DE5 far seg6
        push di                                         ; 0DEA
        callf dsp_reset, R6_0DEE, R6_0814               ; 0DEB far seg1
        push di                                         ; 0DF0
        callf wid_release, R6_0DF4, R6_080A             ; 0DF1 far seg4
        sub_ ax,ax                                      ; 0DF6
        mov [di+0x67],ax                                ; 0DF8
        mov [di+0x65],ax                                ; 0DFB
        mov ax,[di+0x78]                                ; 0DFE
        mov dx,[di+0x7a]                                ; 0E01
        cmp [bp+0x4],ax                                 ; 0E04
        jnz short L6_0E0E                               ; 0E07
        cmp [bp+0x6],dx                                 ; 0E09
        jz short L6_0E11                                ; 0E0C

L6_0E0E:
        jmp near L6_0E9C                                ; 0E0E

L6_0E11:
        mov word [di+0x76],0x0                          ; 0E11
        sub_ ax,ax                                      ; 0E16
        mov [di+0x7a],ax                                ; 0E18
        mov [di+0x78],ax                                ; 0E1B
        mov [bp-0x2],ax                                 ; 0E1E
        mov [bp-0x4],ax                                 ; 0E21
        mov es,[bp-0x6]                                 ; 0E24
        cmp [es:si+0xa50],ax                            ; 0E27
        jnz short L6_0E38                               ; 0E2C
        cmp [es:si+0xa4e],ax                            ; 0E2E
        jnz short L6_0E38                               ; 0E33
        jmp near L6_0FA2                                ; 0E35

L6_0E38:
        mov ax,0x1                                      ; 0E38
        mov cl,[bp-0x4]                                 ; 0E3B
        shl ax,cl                                       ; 0E3E
        cwd                                             ; 0E40
        and ax,[es:si+0x281c]                           ; 0E41
        and dx,[es:si+0x281e]                           ; 0E46
        or_ dx,ax                                       ; 0E4B
        jz short L6_0E75                                ; 0E4D
        push di                                         ; 0E4F
        mov ax,0x2                                      ; 0E50
        push ax                                         ; 0E53
        mov bx,[bp-0x4]                                 ; 0E54
        add_ bx,bx                                      ; 0E57
        add_ bx,si                                      ; 0E59
        mov bx,[es:bx+0x2708]                           ; 0E5B
        add_ bx,bx                                      ; 0E60
        add_ bx,si                                      ; 0E62
        push word [es:bx+0x26b8]                        ; 0E64
        xor_ ax,ax                                      ; 0E69
        push ax                                         ; 0E6B
        mov ax,0x10                                     ; 0E6C
        push ax                                         ; 0E6F
        callf L5_0538, R6_0E73, R6_0957                 ; 0E70 far seg5

L6_0E75:
        add word [bp-0x4],byte +0x1                     ; 0E75
        adc word [bp-0x2],byte +0x0                     ; 0E79
        mov ax,[bp-0x4]                                 ; 0E7D
        mov dx,[bp-0x2]                                 ; 0E80
        mov es,[bp-0x6]                                 ; 0E83
        cmp [es:si+0xa50],dx                            ; 0E86
        ja short L6_0E38                                ; 0E8B
        jnc short L6_0E92                               ; 0E8D
        jmp near L6_0FA2                                ; 0E8F

L6_0E92:
        cmp [es:si+0xa4e],ax                            ; 0E92
        ja short L6_0E38                                ; 0E97
        jmp near L6_0FA2                                ; 0E99

L6_0E9C:
        cmp word [0x18],byte +0x2                       ; 0E9C
        jz short L6_0EA6                                ; 0EA1
        jmp near L6_0F2B                                ; 0EA3

L6_0EA6:
        mov word [0x18],0x0                             ; 0EA6
        sub_ ax,ax                                      ; 0EAC
        mov [bp-0x2],ax                                 ; 0EAE
        mov [bp-0x4],ax                                 ; 0EB1
        mov es,[bp-0x6]                                 ; 0EB4
        cmp [es:si+0xa50],ax                            ; 0EB7
        jnz short L6_0EC8                               ; 0EBC
        cmp [es:si+0xa4e],ax                            ; 0EBE
        jnz short L6_0EC8                               ; 0EC3
        jmp near L6_0FA2                                ; 0EC5

L6_0EC8:
        mov ax,0x1                                      ; 0EC8
        mov cl,[bp-0x4]                                 ; 0ECB
        shl ax,cl                                       ; 0ECE
        cwd                                             ; 0ED0
        and ax,[es:si+0x281c]                           ; 0ED1
        and dx,[es:si+0x281e]                           ; 0ED6
        or_ dx,ax                                       ; 0EDB
        jz short L6_0F05                                ; 0EDD
        push di                                         ; 0EDF
        mov ax,0x2                                      ; 0EE0
        push ax                                         ; 0EE3
        mov bx,[bp-0x4]                                 ; 0EE4
        add_ bx,bx                                      ; 0EE7
        add_ bx,si                                      ; 0EE9
        mov bx,[es:bx+0x2708]                           ; 0EEB
        add_ bx,bx                                      ; 0EF0
        add_ bx,si                                      ; 0EF2
        push word [es:bx+0x26b8]                        ; 0EF4
        xor_ ax,ax                                      ; 0EF9
        push ax                                         ; 0EFB
        mov ax,0x10                                     ; 0EFC
        push ax                                         ; 0EFF
        callf L5_0538, R6_0F03, R6_0F7F                 ; 0F00 far seg5

L6_0F05:
        add word [bp-0x4],byte +0x1                     ; 0F05
        adc word [bp-0x2],byte +0x0                     ; 0F09
        mov ax,[bp-0x4]                                 ; 0F0D
        mov dx,[bp-0x2]                                 ; 0F10
        mov es,[bp-0x6]                                 ; 0F13
        cmp [es:si+0xa50],dx                            ; 0F16
        ja short L6_0EC8                                ; 0F1B
        jnc short L6_0F22                               ; 0F1D
        jmp near L6_0FA2                                ; 0F1F

L6_0F22:
        cmp [es:si+0xa4e],ax                            ; 0F22
        ja short L6_0EC8                                ; 0F27
        jmp short L6_0FA2                               ; 0F29

L6_0F2B:
        sub_ ax,ax                                      ; 0F2B
        mov [bp-0x2],ax                                 ; 0F2D
        mov [bp-0x4],ax                                 ; 0F30
        mov es,[bp-0x6]                                 ; 0F33
        cmp [es:si+0x9bc],ax                            ; 0F36
        jnz short L6_0F44                               ; 0F3B
        cmp [es:si+0x9ba],ax                            ; 0F3D
        jz short L6_0FA2                                ; 0F42

L6_0F44:
        mov ax,0x1                                      ; 0F44
        mov cl,[bp-0x4]                                 ; 0F47
        shl ax,cl                                       ; 0F4A
        cwd                                             ; 0F4C
        and ax,[es:si+0x2814]                           ; 0F4D
        and dx,[es:si+0x2816]                           ; 0F52
        or_ dx,ax                                       ; 0F57
        jz short L6_0F81                                ; 0F59
        push di                                         ; 0F5B
        mov ax,0x1                                      ; 0F5C
        push ax                                         ; 0F5F
        mov bx,[bp-0x4]                                 ; 0F60
        add_ bx,bx                                      ; 0F63
        add_ bx,si                                      ; 0F65
        mov bx,[es:bx+0x26f4]                           ; 0F67
        add_ bx,bx                                      ; 0F6C
        add_ bx,si                                      ; 0F6E
        push word [es:bx+0x26a4]                        ; 0F70
        xor_ ax,ax                                      ; 0F75
        push ax                                         ; 0F77
        mov ax,0x10                                     ; 0F78
        push ax                                         ; 0F7B
        callf L5_0538, R6_0F7F, R6_1151                 ; 0F7C far seg5

L6_0F81:
        add word [bp-0x4],byte +0x1                     ; 0F81
        adc word [bp-0x2],byte +0x0                     ; 0F85
        mov ax,[bp-0x4]                                 ; 0F89
        mov dx,[bp-0x2]                                 ; 0F8C
        mov es,[bp-0x6]                                 ; 0F8F
        cmp [es:si+0x9bc],dx                            ; 0F92
        ja short L6_0F44                                ; 0F97
        jc short L6_0FA2                                ; 0F99
        cmp [es:si+0x9ba],ax                            ; 0F9B
        ja short L6_0F44                                ; 0FA0

L6_0FA2:
        push word [bp+0x4]                              ; 0FA2
        mov ax,0x3bf                                    ; 0FA5
        push ax                                         ; 0FA8
        sub_ ax,ax                                      ; 0FA9
        push ax                                         ; 0FAB
        push ax                                         ; 0FAC
        callf L1_0010, R6_0FB0, R6_0CF7                 ; 0FAD far seg1
        push word [bp+0x4]                              ; 0FB2
        callp R6_0FB6, R6_0CFD, 0x0000                  ; 0FB5 KERNEL.LocalFree
        cmp word [di+0x76],byte +0x0                    ; 0FBA
        jz short L6_0FC4                                ; 0FBE
        push di                                         ; 0FC0
        call L6_0566                                    ; 0FC1

L6_0FC4:
        xor_ ax,ax                                      ; 0FC4
        jmp short L6_0FE6                               ; 0FC6

L6_0FC8:
        mov ax,[di+0x78]                                ; 0FC8
        mov dx,[di+0x7a]                                ; 0FCB
        cmp_ bx,ax                                      ; 0FCE
        jnz short L6_0FE3                               ; 0FD0
        cmp [bp+0x6],dx                                 ; 0FD2
        jnz short L6_0FE3                               ; 0FD5
        cmp word [di+0x76],byte +0x0                    ; 0FD7
        jz short L6_0FE3                                ; 0FDB
        push di                                         ; 0FDD
        call L6_0CAC                                    ; 0FDE
        jmp short L6_0FE8                               ; 0FE1

L6_0FE3:
        mov ax,0xb                                      ; 0FE3

L6_0FE6:
        xor_ dx,dx                                      ; 0FE6

L6_0FE8:
        pop si                                          ; 0FE8
        pop di                                          ; 0FE9
        mov_ sp,bp                                      ; 0FEA
        pop bp                                          ; 0FEC
        ret 0x4                                         ; 0FED
        db 0x90, 0x90                                   ; 0FF0

L6_0FF2:
        push bp                                         ; 0FF2
        mov_ bp,sp                                      ; 0FF3
        sub sp,byte +0xa                                ; 0FF5
        push di                                         ; 0FF8
        push si                                         ; 0FF9
        push ds                                         ; 0FFA
        movsel ax, R6_0FFC, 0xFFFF                      ; 0FFB seg7
        mov ds,ax                                       ; 0FFE
        mov cx,[bp+0x12]                                ; 1000
        mov_ ax,cx                                      ; 1003
        cmp ax,strict word 0x67                         ; 1005
        jnz short L6_100D                               ; 1008
        jmp near L6_1351                                ; 100A

L6_100D:
        ja short L6_1039                                ; 100D
        sub al,0x32                                     ; 100F
        jnz short L6_1016                               ; 1011
        jmp near L6_125F                                ; 1013

L6_1016:
        dec al                                          ; 1016
        jnz short L6_101D                               ; 1018
        jmp near L6_1294                                ; 101A

L6_101D:
        dec al                                          ; 101D
        jnz short L6_1024                               ; 101F
        jmp near L6_12C5                                ; 1021

L6_1024:
        sub al,0x30                                     ; 1024
        jnz short L6_102B                               ; 1026
        jmp near L6_1310                                ; 1028

L6_102B:
        dec al                                          ; 102B
        jnz short L6_1032                               ; 102D
        jmp near L6_1337                                ; 102F

L6_1032:
        dec al                                          ; 1032
        jnz short L6_1039                               ; 1034
        jmp near L6_1344                                ; 1036

L6_1039:
        cmp word [bp+0x14],byte +0x0                    ; 1039
        jz short L6_1045                                ; 103D

L6_103F:
        mov ax,0x2                                      ; 103F
        jmp near L6_1289                                ; 1042

L6_1045:
        mov bx,[bp+0xe]                                 ; 1045
        mov di,[bx]                                     ; 1048
        cmp word [di+0x1c],byte +0x0                    ; 104A
        jnz short L6_1053                               ; 104E
        jmp near L6_125A                                ; 1050

L6_1053:
        test byte [di+0x6f],0x4                         ; 1053
        jz short L6_105C                                ; 1057
        jmp near L6_125A                                ; 1059

L6_105C:
        mov_ ax,cx                                      ; 105C
        sub ax,strict word 0x35                         ; 105E
        jz short L6_1087                                ; 1061
        sub ax,strict word 0x3                          ; 1063
        jz short L6_1091                                ; 1066
        dec ax                                          ; 1068
        jz short L6_10A1                                ; 1069
        dec ax                                          ; 106B
        jz short L6_10AB                                ; 106C
        dec ax                                          ; 106E
        jz short L6_10B5                                ; 106F
        dec ax                                          ; 1071
        jz short L6_10BF                                ; 1072
        sub ax,0x4057                                   ; 1074
        jz short L6_10D5                                ; 1077
        sub ax,0x3f5                                    ; 1079
        jnz short L6_1081                               ; 107C
        jmp near L6_11FA                                ; 107E

L6_1081:
        mov ax,0x8                                      ; 1081
        jmp near L6_1289                                ; 1084

L6_1087:
        push word [bp+0x10]                             ; 1087
        push bx                                         ; 108A
        call L6_0DAC                                    ; 108B
        jmp near L6_135C                                ; 108E

L6_1091:
        push word [bp+0x10]                             ; 1091
        push bx                                         ; 1094
        push word [bp+0xc]                              ; 1095
        push word [bp+0xa]                              ; 1098
        call L6_0AEE                                    ; 109B
        jmp near L6_135C                                ; 109E

L6_10A1:
        push word [bp+0x10]                             ; 10A1
        push bx                                         ; 10A4
        call L6_0A16                                    ; 10A5
        jmp near L6_135C                                ; 10A8

L6_10AB:
        push word [bp+0x10]                             ; 10AB
        push bx                                         ; 10AE
        call L6_0A82                                    ; 10AF
        jmp near L6_135C                                ; 10B2

L6_10B5:
        push word [bp+0x10]                             ; 10B5
        push bx                                         ; 10B8
        call L6_0BCE                                    ; 10B9
        jmp near L6_135C                                ; 10BC

L6_10BF:
        push word [bp+0x10]                             ; 10BF
        push bx                                         ; 10C2
        push word [bp+0xc]                              ; 10C3
        push word [bp+0xa]                              ; 10C6
        push word [bp+0x8]                              ; 10C9
        push word [bp+0x6]                              ; 10CC
        call L6_017A                                    ; 10CF
        jmp near L6_135C                                ; 10D2

L6_10D5:
        mov ax,[di+0x26]                                ; 10D5
        mov dx,[di+0x28]                                ; 10D8
        mov_ si,ax                                      ; 10DB
        mov [bp-0x8],dx                                 ; 10DD
        mov ax,[di+0x65]                                ; 10E0
        mov dx,[di+0x67]                                ; 10E3
        cmp_ bx,ax                                      ; 10E6
        jnz short L6_10EF                               ; 10E8
        cmp [bp+0x10],dx                                ; 10EA
        jz short L6_10F5                                ; 10ED

L6_10EF:
        mov ax,0x4                                      ; 10EF
        jmp near L6_1289                                ; 10F2

L6_10F5:
        mov ax,[di+0x7a]                                ; 10F5
        or ax,[di+0x78]                                 ; 10F8
        jnz short L6_10EF                               ; 10FB
        sub_ ax,ax                                      ; 10FD
        mov [bp-0x2],ax                                 ; 10FF
        mov [bp-0x4],ax                                 ; 1102
        mov es,[bp-0x8]                                 ; 1105
        cmp [es:si+0x9bc],ax                            ; 1108
        jnz short L6_1116                               ; 110D
        cmp [es:si+0x9ba],ax                            ; 110F
        jz short L6_1174                                ; 1114

L6_1116:
        mov ax,0x1                                      ; 1116
        mov cl,[bp-0x4]                                 ; 1119
        shl ax,cl                                       ; 111C
        cwd                                             ; 111E
        and ax,[es:si+0x2814]                           ; 111F
        and dx,[es:si+0x2816]                           ; 1124
        or_ dx,ax                                       ; 1129
        jz short L6_1153                                ; 112B
        push di                                         ; 112D
        mov ax,0x1                                      ; 112E
        push ax                                         ; 1131
        mov bx,[bp-0x4]                                 ; 1132
        add_ bx,bx                                      ; 1135
        add_ bx,si                                      ; 1137
        mov bx,[es:bx+0x26f4]                           ; 1139
        add_ bx,bx                                      ; 113E
        add_ bx,si                                      ; 1140
        push word [es:bx+0x26a4]                        ; 1142
        xor_ ax,ax                                      ; 1147
        push ax                                         ; 1149
        mov ax,0x10                                     ; 114A
        push ax                                         ; 114D
        callf L5_0538, R6_1151, R6_11D2                 ; 114E far seg5

L6_1153:
        add word [bp-0x4],byte +0x1                     ; 1153
        adc word [bp-0x2],byte +0x0                     ; 1157
        mov ax,[bp-0x4]                                 ; 115B
        mov dx,[bp-0x2]                                 ; 115E
        mov es,[bp-0x8]                                 ; 1161
        cmp [es:si+0x9bc],dx                            ; 1164
        ja short L6_1116                                ; 1169
        jc short L6_1174                                ; 116B
        cmp [es:si+0x9ba],ax                            ; 116D
        ja short L6_1116                                ; 1172

L6_1174:
        mov ax,[bp+0xe]                                 ; 1174
        mov dx,[bp+0x10]                                ; 1177
        mov [di+0x78],ax                                ; 117A
        mov [di+0x7a],dx                                ; 117D
        sub_ ax,ax                                      ; 1180
        mov [bp-0x2],ax                                 ; 1182
        mov [bp-0x4],ax                                 ; 1185
        cmp [es:si+0xa50],ax                            ; 1188
        jnz short L6_1196                               ; 118D
        cmp [es:si+0xa4e],ax                            ; 118F
        jz short L6_11F5                                ; 1194

L6_1196:
        mov ax,0x1                                      ; 1196
        mov cl,[bp-0x4]                                 ; 1199
        shl ax,cl                                       ; 119C
        cwd                                             ; 119E
        and ax,[es:si+0x281c]                           ; 119F
        and dx,[es:si+0x281e]                           ; 11A4
        or_ dx,ax                                       ; 11A9
        jz short L6_11D4                                ; 11AB
        push di                                         ; 11AD
        mov ax,0x2                                      ; 11AE
        push ax                                         ; 11B1
        mov bx,[bp-0x4]                                 ; 11B2
        add_ bx,bx                                      ; 11B5
        add_ bx,si                                      ; 11B7
        mov bx,[es:bx+0x2708]                           ; 11B9
        add_ bx,bx                                      ; 11BE
        add_ bx,si                                      ; 11C0
        push word [es:bx+0x26b8]                        ; 11C2
        mov ax,0x1                                      ; 11C7
        push ax                                         ; 11CA
        mov ax,0x10                                     ; 11CB
        push ax                                         ; 11CE
        callf L5_0538, R6_11D2, R6_0E73                 ; 11CF far seg5

L6_11D4:
        add word [bp-0x4],byte +0x1                     ; 11D4
        adc word [bp-0x2],byte +0x0                     ; 11D8
        mov ax,[bp-0x4]                                 ; 11DC
        mov dx,[bp-0x2]                                 ; 11DF
        mov es,[bp-0x8]                                 ; 11E2
        cmp [es:si+0xa50],dx                            ; 11E5
        ja short L6_1196                                ; 11EA
        jc short L6_11F5                                ; 11EC
        cmp [es:si+0xa4e],ax                            ; 11EE
        ja short L6_1196                                ; 11F3

L6_11F5:
        xor_ ax,ax                                      ; 11F5
        jmp near L6_1289                                ; 11F7

L6_11FA:
        mov ax,[di+0x65]                                ; 11FA
        mov dx,[di+0x67]                                ; 11FD
        cmp_ bx,ax                                      ; 1200
        jnz short L6_120F                               ; 1202
        cmp [bp+0x10],dx                                ; 1204
        jnz short L6_120F                               ; 1207
        cmp word [di+0x74],byte +0x0                    ; 1209
        jz short L6_1212                                ; 120D

L6_120F:
        jmp near L6_10EF                                ; 120F

L6_1212:
        mov cx,[bp+0xc]                                 ; 1212
        mov bx,[bp+0xa]                                 ; 1215
        mov es,cx                                       ; 1218
        mov ax,[es:bx+0x2]                              ; 121A
        or ax,[es:bx]                                   ; 121E
        jz short L6_123A                                ; 1221
        mov al,[di+0x2a]                                ; 1223
        and ax,strict word 0x1                          ; 1226
        cmp ax,strict word 0x1                          ; 1229
        sbb_ ax,ax                                      ; 122C
        inc ax                                          ; 122E
        cwd                                             ; 122F
        mov [es:bx+0x4],ax                              ; 1230
        mov [es:bx+0x6],dx                              ; 1234
        jmp short L6_11F5                               ; 1238

L6_123A:
        mov ax,[es:bx+0x6]                              ; 123A
        or ax,[es:bx+0x4]                               ; 123E
        jz short L6_124A                                ; 1242
        or byte [di+0x2a],0x1                           ; 1244
        jmp short L6_124E                               ; 1248

L6_124A:
        and byte [di+0x2a],0xfe                         ; 124A

L6_124E:
        cmp byte [di+0x6],0x3                           ; 124E
        jna short L6_11F5                               ; 1252
        and byte [di+0x2a],0xfe                         ; 1254
        jmp short L6_11F5                               ; 1258

L6_125A:
        mov ax,0x3                                      ; 125A
        jmp short L6_1289                               ; 125D

L6_125F:
        push word [bp+0xc]                              ; 125F
        push word [bp+0xa]                              ; 1262
        callf L3_4EAE, R6_1268, R6_0017                 ; 1265 far seg3
        mov_ si,ax                                      ; 126A
        or_ si,ax                                       ; 126C
        jnz short L6_1278                               ; 126E
        xor_ ax,ax                                      ; 1270
        mov dx,0xb                                      ; 1272
        jmp near L6_135C                                ; 1275

L6_1278:
        mov ax,[bp+0xc]                                 ; 1278
        or ax,[bp+0xa]                                  ; 127B
        jz short L6_128E                                ; 127E
        cmp word [si+0x1c],byte +0x0                    ; 1280
        jz short L6_128E                                ; 1284
        mov ax,0x1                                      ; 1286

L6_1289:
        xor_ dx,dx                                      ; 1289
        jmp near L6_135C                                ; 128B

L6_128E:
        xor_ ax,ax                                      ; 128E
        cwd                                             ; 1290
        jmp near L6_135C                                ; 1291

L6_1294:
        cmp word [bp+0x14],byte +0x0                    ; 1294
        jz short L6_129D                                ; 1298
        jmp near L6_103F                                ; 129A

L6_129D:
        push word [bp+0x8]                              ; 129D
        push word [bp+0x6]                              ; 12A0
        callf L3_4EAE, R6_12A6, R6_12DE                 ; 12A3 far seg3
        mov_ si,ax                                      ; 12A8
        or_ si,ax                                       ; 12AA
        jnz short L6_12B1                               ; 12AC
        jmp near L6_103F                                ; 12AE

L6_12B1:
        cmp word [si+0x1c],byte +0x0                    ; 12B1
        jz short L6_125A                                ; 12B5
        push si                                         ; 12B7
        push word [bp+0xc]                              ; 12B8
        push word [bp+0xa]                              ; 12BB
        push cs                                         ; 12BE
        call L6_00E6                                    ; 12BF
        jmp near L6_11F5                                ; 12C2

L6_12C5:
        cmp word [bp+0x14],byte +0x0                    ; 12C5
        jnz short L6_12E6                               ; 12C9
        mov cx,[bp+0xc]                                 ; 12CB
        mov bx,[bp+0xa]                                 ; 12CE
        mov es,cx                                       ; 12D1
        push word [es:bx+0x12]                          ; 12D3
        push word [es:bx+0x10]                          ; 12D7
        callf L3_4EAE, R6_12DE, R6_1319                 ; 12DB far seg3
        mov_ si,ax                                      ; 12E0
        or_ si,ax                                       ; 12E2
        jnz short L6_12E9                               ; 12E4

L6_12E6:
        jmp near L6_103F                                ; 12E6

L6_12E9:
        cmp word [si+0x1c],byte +0x0                    ; 12E9
        jz short L6_12F5                                ; 12ED
        test byte [si+0x6f],0x4                         ; 12EF
        jz short L6_12F8                                ; 12F3

L6_12F5:
        jmp near L6_125A                                ; 12F5

L6_12F8:
        push si                                         ; 12F8
        push word [bp+0x10]                             ; 12F9
        push word [bp+0xe]                              ; 12FC
        push word [bp+0xc]                              ; 12FF
        push word [bp+0xa]                              ; 1302
        push word [bp+0x8]                              ; 1305
        push word [bp+0x6]                              ; 1308
        call L6_0680                                    ; 130B
        jmp short L6_135C                               ; 130E

L6_1310:
        push word [bp+0x8]                              ; 1310
        push word [bp+0x6]                              ; 1313
        callf L3_43E2, R6_1319, R6_1325                 ; 1316 far seg3
        cmp word [0xc8],byte +0x0                       ; 131B
        jz short L6_132A                                ; 1320
        callf vxd_check, R6_1325, R6_1333               ; 1322 far seg3
        jmp near L6_11F5                                ; 1327

L6_132A:
        push word [bp+0x8]                              ; 132A
        push word [bp+0x6]                              ; 132D
        callf L3_47C4, R6_1333, R6_1340                 ; 1330 far seg3
        jmp short L6_135C                               ; 1335

L6_1337:
        push word [bp+0x8]                              ; 1337
        push word [bp+0x6]                              ; 133A
        callf L3_4E68, R6_1340, R6_134D                 ; 133D far seg3
        jmp short L6_135C                               ; 1342

L6_1344:
        push word [bp+0x8]                              ; 1344
        push word [bp+0x6]                              ; 1347
        callf L3_4D2C, R6_134D, R6_135A                 ; 134A far seg3
        jmp short L6_135C                               ; 134F

L6_1351:
        push word [bp+0x8]                              ; 1351
        push word [bp+0x6]                              ; 1354
        callf L3_4940, R6_135A, R6_1268                 ; 1357 far seg3

L6_135C:
        pop ds                                          ; 135C
        pop si                                          ; 135D
        pop di                                          ; 135E
        mov_ sp,bp                                      ; 135F
        pop bp                                          ; 1361
        retf 0x10                                       ; 1362
        db 0x90                                         ; 1365

L6_1366:
        push bp                                         ; 1366
        mov_ bp,sp                                      ; 1367
        sub sp,byte +0x8                                ; 1369
        push di                                         ; 136C
        push si                                         ; 136D
        mov di,[bp+0x4]                                 ; 136E
        push di                                         ; 1371
        callf L1_00DA, R6_1375, R6_13E7                 ; 1372 far seg1
        mov ax,[di+0xbe]                                ; 1377
        or ax,[di+0xbc]                                 ; 137B
        jz short L6_1387                                ; 137F
        les si,[di+0xbc]                                ; 1381
        jmp short L6_138B                               ; 1385

L6_1387:
        les si,[di+0xac]                                ; 1387

L6_138B:
        sub_ ax,ax                                      ; 138B
        mov [di+0xae],ax                                ; 138D
        mov [di+0xac],ax                                ; 1391
        mov [di+0xbe],ax                                ; 1395
        mov [di+0xbc],ax                                ; 1399
        mov [di+0xb6],ax                                ; 139D
        mov [di+0xb4],ax                                ; 13A1
        mov [di+0xba],ax                                ; 13A5
        mov [di+0xb8],ax                                ; 13A9
        mov [di+0xc2],ax                                ; 13AD
        mov [di+0xc0],ax                                ; 13B1
        mov [di+0x4b],ax                                ; 13B5
        mov [di+0x49],ax                                ; 13B8
        mov [di+0x4f],ax                                ; 13BB
        mov [di+0x4d],ax                                ; 13BE
        mov byte [di+0x48],0x1                          ; 13C1
        mov [di+0xe1],ax                                ; 13C5
        mov ax,es                                       ; 13C9
        or_ ax,si                                       ; 13CB
        jz short L6_13F5                                ; 13CD
        mov [bp-0x6],es                                 ; 13CF

L6_13D2:
        mov es,[bp-0x6]                                 ; 13D2
        mov ax,[es:si+0x18]                             ; 13D5
        mov dx,[es:si+0x1a]                             ; 13D9
        mov_ di,ax                                      ; 13DD
        mov [bp-0x2],dx                                 ; 13DF
        push es                                         ; 13E2
        push si                                         ; 13E3
        callf L1_0048, R6_13E7, R6_1492                 ; 13E4 far seg1
        mov ax,[bp-0x2]                                 ; 13E9
        mov_ si,di                                      ; 13EC
        mov [bp-0x6],ax                                 ; 13EE
        or_ ax,di                                       ; 13F1
        jnz short L6_13D2                               ; 13F3

L6_13F5:
        pop si                                          ; 13F5
        pop di                                          ; 13F6
        mov_ sp,bp                                      ; 13F7
        pop bp                                          ; 13F9
        ret 0x2                                         ; 13FA
        db 0x90                                         ; 13FD

L6_13FE:
        push bp                                         ; 13FE
        mov_ bp,sp                                      ; 13FF
        sub sp,0xb0                                     ; 1401
        push si                                         ; 1405
        mov si,[bp+0x6]                                 ; 1406
        mov word [bp-0x30],0x2e                         ; 1409
        mov word [bp-0x2e],0x28                         ; 140E
        mov word [bp-0x2c],0x404                        ; 1413
        mov word [bp-0xa],0xfff                         ; 1418
        mov word [bp-0x8],0x0                           ; 141D
        mov word [bp-0x6],0x2                           ; 1422
        mov word [bp-0x4],0x2c                          ; 1427
        mov word [bp-0x2],0x0                           ; 142C
        push word [0xbd0]                               ; 1431
        mov ax,0x11                                     ; 1435
        push ax                                         ; 1438
        lea ax,[bp-0x70]                                ; 1439
        push ss                                         ; 143C
        push ax                                         ; 143D
        mov ax,0x40                                     ; 143E
        push ax                                         ; 1441
        callp R6_1443, R6_0121, 0x0000                  ; 1442 USER.LoadString
        mov bx,[bp+0xa]                                 ; 1447
        push word [bx]                                  ; 144A
        lea ax,[bp-0x70]                                ; 144C
        push ss                                         ; 144F
        push ax                                         ; 1450
        lea ax,[bp-0xb0]                                ; 1451
        push ss                                         ; 1455
        push ax                                         ; 1456
        callp R6_1458, R6_0136, 0x0000                  ; 1457 USER.wsprintf
        add sp,byte +0xa                                ; 145C
        lea ax,[bp-0x2a]                                ; 145F
        push ss                                         ; 1462
        push ax                                         ; 1463
        lea ax,[bp-0xb0]                                ; 1464
        push ss                                         ; 1468
        push ax                                         ; 1469
        mov ax,0x1f                                     ; 146A
        push ax                                         ; 146D
        callp R6_146F, R6_014D, 0x0000                  ; 146E KERNEL.lstrcpyn
        mov es,[bp+0x8]                                 ; 1473
        push word [es:si+0x6]                           ; 1476
        push word [es:si+0x4]                           ; 147A
        lea ax,[bp-0x30]                                ; 147E
        push ss                                         ; 1481
        push ax                                         ; 1482
        mov ax,[es:si]                                  ; 1483
        cmp ax,strict word 0x30                         ; 1486
        jna short L6_148E                               ; 1489
        mov ax,0x30                                     ; 148B

L6_148E:
        push ax                                         ; 148E
        callf L1_1AD3, R6_1492, R6_15BC                 ; 148F far seg1
        pop si                                          ; 1494
        mov_ sp,bp                                      ; 1495
        pop bp                                          ; 1497
        retf 0x6                                        ; 1498
        db 0x90                                         ; 149B

; WODM_OPEN: format check (PCM, 8/16 bit, 1/2 channels, 4000-49000 Hz), busy check, audio2_init
wod_open:
        push bp                                         ; 149C
        mov_ bp,sp                                      ; 149D
        sub sp,byte +0x10                               ; 149F
        push di                                         ; 14A2
        push si                                         ; 14A3
        sub_ ax,ax                                      ; 14A4
        mov [bp-0x8],ax                                 ; 14A6
        mov [bp-0xa],ax                                 ; 14A9
        mov byte [bp-0x5],0x1                           ; 14AC
        cmp [bp+0x4],ax                                 ; 14B0
        jnz short L6_14BB                               ; 14B3
        cmp word [bp+0x6],byte +0x1                     ; 14B5
        jz short L6_14CB                                ; 14B9

L6_14BB:
        cmp [bp+0x4],ax                                 ; 14BB
        jnz short L6_14C6                               ; 14BE
        cmp word [bp+0x6],byte +0x4                     ; 14C0
        jz short L6_14CB                                ; 14C4

L6_14C6:
        mov [0x1a],ax                                   ; 14C6
        jmp short L6_14D5                               ; 14C9

L6_14CB:
        mov word [0x18],0x0                             ; 14CB
        inc word [0x1a]                                 ; 14D1

L6_14D5:
        les bx,[bp+0x8]                                 ; 14D5
        mov ax,[es:bx+0x2]                              ; 14D8
        mov dx,[es:bx+0x4]                              ; 14DC
        mov_ di,ax                                      ; 14E0
        mov [bp-0xc],dx                                 ; 14E2
        mov_ si,ax                                      ; 14E5
        mov [bp-0x2],dx                                 ; 14E7
        mov_ bx,ax                                      ; 14EA
        mov es,dx                                       ; 14EC
        mov ax,[es:bx]                                  ; 14EE
        dec ax                                          ; 14F1
        jz short L6_14FA                                ; 14F2
        mov byte [bp-0x5],0x0                           ; 14F4
        jmp short L6_1571                               ; 14F8

L6_14FA:
        mov es,dx                                       ; 14FA
        mov ax,[es:si+0xe]                              ; 14FC
        mov [bp-0x10],ax                                ; 1500
        cmp ax,strict word 0x10                         ; 1503
        jz short L6_1511                                ; 1506
        cmp ax,strict word 0x8                          ; 1508
        jz short L6_1511                                ; 150B
        mov byte [bp-0x5],0x0                           ; 150D

L6_1511:
        mov es,[bp-0xc]                                 ; 1511
        cmp word [es:di+0x2],byte +0x1                  ; 1514
        jz short L6_1526                                ; 1519
        cmp word [es:di+0x2],byte +0x2                  ; 151B
        jz short L6_1526                                ; 1520
        mov byte [bp-0x5],0x0                           ; 1522

L6_1526:
        cmp word [es:di+0x6],byte +0x0                  ; 1526
        jnz short L6_1535                               ; 152B
        cmp word [es:di+0x4],0xfa0                      ; 152D
        jc short L6_1544                                ; 1533

L6_1535:
        cmp word [es:di+0x6],byte +0x0                  ; 1535
        jnz short L6_1544                               ; 153A
        cmp word [es:di+0x4],0xbf68                     ; 153C
        jna short L6_1548                               ; 1542

L6_1544:
        mov byte [bp-0x5],0x0                           ; 1544

L6_1548:
        cmp word [bp-0x10],byte +0x10                   ; 1548
        jnz short L6_1553                               ; 154C
        mov ax,0x1                                      ; 154E
        jmp short L6_1555                               ; 1551

L6_1553:
        xor_ ax,ax                                      ; 1553

L6_1555:
        cwd                                             ; 1555
        mov [bp-0xa],ax                                 ; 1556
        mov [bp-0x8],dx                                 ; 1559
        cmp word [es:di+0x2],byte +0x2                  ; 155C
        jnz short L6_1568                               ; 1561
        mov ax,0x2                                      ; 1563
        jmp short L6_156A                               ; 1566

L6_1568:
        xor_ ax,ax                                      ; 1568

L6_156A:
        cwd                                             ; 156A
        or [bp-0xa],ax                                  ; 156B
        or [bp-0x8],dx                                  ; 156E

L6_1571:
        cmp byte [bp-0x5],0x0                           ; 1571
        jnz short L6_157D                               ; 1575
        mov ax,0x20                                     ; 1577
        jmp near L6_1759                                ; 157A

L6_157D:
        test byte [bp+0x4],0x1                          ; 157D
        jz short L6_1586                                ; 1581
        jmp near L6_1757                                ; 1583

L6_1586:
        mov si,[bp+0x10]                                ; 1586
        cmp word [si+0x20],byte +0x0                    ; 1589
        jz short L6_1595                                ; 158D
        push cs                                         ; 158F
        call L6_0000                                    ; 1590
        jmp short L6_15D2                               ; 1593

L6_1595:
        mov al,[si+0x5d]                                ; 1595
        and al,0x3                                      ; 1598
        cmp al,0x2                                      ; 159A
        jnz short L6_15AE                               ; 159C
        mov ax,[si+0x7a]                                ; 159E
        or ax,[si+0x78]                                 ; 15A1
        jz short L6_15AE                                ; 15A4
        push si                                         ; 15A6
        call L6_0470                                    ; 15A7
        or_ ax,ax                                       ; 15AA
        jz short L6_15D2                                ; 15AC

L6_15AE:
        push si                                         ; 15AE
        callf wod_acquire, R6_15B2, R6_15C6             ; 15AF far seg4
        or_ ax,ax                                       ; 15B4
        jnz short L6_15D2                               ; 15B6
        push si                                         ; 15B8
        callf audio2_init, R6_15BC, R6_0FB0             ; 15B9 far seg1
        or_ ax,ax                                       ; 15BE
        jnz short L6_15D8                               ; 15C0
        push si                                         ; 15C2
        callf wod_release, R6_15C6, R6_15F0             ; 15C3 far seg4
        cmp word [si+0x76],byte +0x0                    ; 15C8
        jz short L6_15D2                                ; 15CC
        push si                                         ; 15CE
        call L6_0566                                    ; 15CF

L6_15D2:
        mov ax,0x4                                      ; 15D2
        jmp near L6_1759                                ; 15D5

L6_15D8:
        mov ax,0x40                                     ; 15D8
        push ax                                         ; 15DB
        mov ax,0x28                                     ; 15DC
        push ax                                         ; 15DF
        callp R6_15E1, R6_082F, 0x0000                  ; 15E0 KERNEL.LocalAlloc
        mov [bp-0x2],ax                                 ; 15E5
        or_ ax,ax                                       ; 15E8
        jnz short L6_1602                               ; 15EA
        push si                                         ; 15EC
        callf wod_release, R6_15F0, R6_0D2F             ; 15ED far seg4
        cmp word [si+0x76],byte +0x0                    ; 15F2
        jz short L6_15FC                                ; 15F6
        push si                                         ; 15F8
        call L6_0566                                    ; 15F9

L6_15FC:
        mov ax,0x7                                      ; 15FC
        jmp near L6_1759                                ; 15FF

L6_1602:
        les bx,[bp+0x8]                                 ; 1602
        mov_ si,ax                                      ; 1605
        mov ax,[es:bx+0x6]                              ; 1607
        mov dx,[es:bx+0x8]                              ; 160B
        mov [si+0x2],ax                                 ; 160F
        mov [si+0x4],dx                                 ; 1612
        mov ax,[es:bx+0xa]                              ; 1615
        mov dx,[es:bx+0xc]                              ; 1619
        mov [si+0x6],ax                                 ; 161D
        mov [si+0x8],dx                                 ; 1620
        mov ax,[es:bx]                                  ; 1623
        mov [si+0xa],ax                                 ; 1626
        mov ax,[bp+0x4]                                 ; 1629
        mov dx,[bp+0x6]                                 ; 162C
        mov [si+0xc],ax                                 ; 162F
        mov [si+0xe],dx                                 ; 1632
        sub_ ax,ax                                      ; 1635
        mov [si+0x12],ax                                ; 1637
        mov [si+0x10],ax                                ; 163A
        mov [si+0x16],ax                                ; 163D
        mov [si+0x14],ax                                ; 1640
        mov_ ax,di                                      ; 1643
        mov dx,[bp-0xc]                                 ; 1645
        push ds                                         ; 1648
        lea di,[si+0x18]                                ; 1649
        mov_ si,ax                                      ; 164C
        push ds                                         ; 164E
        pop es                                          ; 164F
        mov ds,dx                                       ; 1650
        mov cx,0x8                                      ; 1652
        rep movsw                                       ; 1655
        pop ds                                          ; 1657
        mov bx,[bp-0x2]                                 ; 1658
        mov ax,[bp+0x10]                                ; 165B
        mov [bx],ax                                     ; 165E
        mov_ si,ax                                      ; 1660
        sub_ cx,cx                                      ; 1662
        mov [si+0x10d],cx                               ; 1664
        mov [si+0x10b],cx                               ; 1668
        mov [si+0xe1],cx                                ; 166C
        mov [si+0xae],cx                                ; 1670
        mov [si+0xac],cx                                ; 1674
        mov [si+0xbe],cx                                ; 1678
        mov [si+0xbc],cx                                ; 167C
        mov [si+0xb2],cx                                ; 1680
        mov [si+0xb0],cx                                ; 1684
        mov [si+0xb6],cx                                ; 1688
        mov [si+0xb4],cx                                ; 168C
        mov [si+0xba],cx                                ; 1690
        mov [si+0xb8],cx                                ; 1694
        mov [si+0xc2],cx                                ; 1698
        mov [si+0xc0],cx                                ; 169C
        mov [si+0x4b],cx                                ; 16A0
        mov [si+0x49],cx                                ; 16A3
        mov [si+0x4f],cx                                ; 16A6
        mov [si+0x4d],cx                                ; 16A9
        mov byte [si+0x48],0x1                          ; 16AC
        mov cx,[0x14]                                   ; 16B0
        mov [si+0x101],bx                               ; 16B4
        mov [si+0x103],cx                               ; 16B8
        inc word [0x14]                                 ; 16BC
        mov_ cx,bx                                      ; 16C0
        mov dx,[si+0x103]                               ; 16C2
        mov bx,[bp+0xe]                                 ; 16C6
        mov di,[bp+0xc]                                 ; 16C9
        mov es,bx                                       ; 16CC
        mov [es:di],cx                                  ; 16CE
        mov_ bx,cx                                      ; 16D1
        mov [es:di+0x2],dx                              ; 16D3
        mov cx,[bx+0x1c]                                ; 16D7
        mov [si+0x105],cx                               ; 16DA
        push ax                                         ; 16DE
        push cx                                         ; 16DF
        push word [bx+0x1a]                             ; 16E0
        xor_ al,al                                      ; 16E3
        push ax                                         ; 16E5
        push word [bp-0x8]                              ; 16E6
        push word [bp-0xa]                              ; 16E9
        callf L6_238A, R6_16EF, R6_1864                 ; 16EC far seg6
        cmp word [0x1a],byte +0x1                       ; 16F1
        jnz short L6_16FD                               ; 16F6
        push si                                         ; 16F8
        xor_ ax,ax                                      ; 16F9
        jmp short L6_1738                               ; 16FB

L6_16FD:
        mov dx,[0x1a]                                   ; 16FD
        cmp dx,byte +0x2                                ; 1701
        jnz short L6_170D                               ; 1704
        push si                                         ; 1706
        mov ax,0x1                                      ; 1707
        push ax                                         ; 170A
        jmp short L6_173C                               ; 170B

L6_170D:
        cmp dx,byte +0x3                                ; 170D
        jz short L6_1734                                ; 1710
        push si                                         ; 1712
        xor_ ax,ax                                      ; 1713
        push ax                                         ; 1715
        mov ax,0x1                                      ; 1716
        push ax                                         ; 1719
        push ax                                         ; 171A
        mov dx,0x10                                     ; 171B
        push dx                                         ; 171E
        callf L5_0538, R6_1722, R6_1732                 ; 171F far seg5
        push si                                         ; 1724
        mov ax,0x1                                      ; 1725
        push ax                                         ; 1728
        push ax                                         ; 1729
        push ax                                         ; 172A
        mov dx,0x10                                     ; 172B
        push dx                                         ; 172E
        callf L5_0538, R6_1732, R6_1745                 ; 172F far seg5

L6_1734:
        push si                                         ; 1734
        mov ax,0x2                                      ; 1735

L6_1738:
        push ax                                         ; 1738
        mov ax,0x1                                      ; 1739

L6_173C:
        push ax                                         ; 173C
        push ax                                         ; 173D
        mov ax,0x10                                     ; 173E
        push ax                                         ; 1741
        callf L5_0538, R6_1745, R6_17DD                 ; 1742 far seg5
        push word [bp-0x2]                              ; 1747
        mov ax,0x3bb                                    ; 174A
        push ax                                         ; 174D
        sub_ ax,ax                                      ; 174E
        push ax                                         ; 1750
        push ax                                         ; 1751
        callf L1_0010, R6_1755, R6_1790                 ; 1752 far seg1

L6_1757:
        xor_ ax,ax                                      ; 1757

L6_1759:
        xor_ dx,dx                                      ; 1759
        pop si                                          ; 175B
        pop di                                          ; 175C
        mov_ sp,bp                                      ; 175D
        pop bp                                          ; 175F
        ret 0xe                                         ; 1760
        db 0x90                                         ; 1763

; WODM_CLOSE
wod_close:
        push bp                                         ; 1764
        mov_ bp,sp                                      ; 1765
        sub sp,byte +0x2                                ; 1767
        push si                                         ; 176A
        mov si,[bp+0x4]                                 ; 176B
        mov ax,[si]                                     ; 176E
        mov_ bx,ax                                      ; 1770
        mov [bp-0x2],ax                                 ; 1772
        mov cx,[bx+0xae]                                ; 1775
        or cx,[bx+0xac]                                 ; 1779
        jnz short L6_1789                               ; 177D
        mov ax,[bx+0xbe]                                ; 177F
        or ax,[bx+0xbc]                                 ; 1783
        jz short L6_178C                                ; 1787

L6_1789:
        jmp near L6_183F                                ; 1789

L6_178C:
        push bx                                         ; 178C
        callf audio2_stop, R6_1790, R6_1816             ; 178D far seg1
        cmp word [0x1a],byte +0x1                       ; 1792
        jnz short L6_17A0                               ; 1797
        push word [bp-0x2]                              ; 1799
        xor_ ax,ax                                      ; 179C
        jmp short L6_17F9                               ; 179E

L6_17A0:
        mov dx,[0x1a]                                   ; 17A0
        cmp dx,byte +0x2                                ; 17A4
        jnz short L6_17B2                               ; 17A7
        push word [bp-0x2]                              ; 17A9
        mov ax,0x1                                      ; 17AC
        push ax                                         ; 17AF
        jmp short L6_17FD                               ; 17B0

L6_17B2:
        cmp dx,byte +0x3                                ; 17B2
        jnz short L6_17C9                               ; 17B5
        push word [bp-0x2]                              ; 17B7
        mov ax,0x2                                      ; 17BA
        push ax                                         ; 17BD
        mov ax,0x1                                      ; 17BE
        push ax                                         ; 17C1
        xor_ ax,ax                                      ; 17C2
        mov [0x1a],ax                                   ; 17C4
        jmp short L6_1800                               ; 17C7

L6_17C9:
        push word [bp-0x2]                              ; 17C9
        xor_ ax,ax                                      ; 17CC
        push ax                                         ; 17CE
        mov ax,0x1                                      ; 17CF
        push ax                                         ; 17D2
        xor_ cx,cx                                      ; 17D3
        push cx                                         ; 17D5
        mov dx,0x10                                     ; 17D6
        push dx                                         ; 17D9
        callf L5_0538, R6_17DD, R6_17F1                 ; 17DA far seg5
        push word [bp-0x2]                              ; 17DF
        mov ax,0x1                                      ; 17E2
        push ax                                         ; 17E5
        push ax                                         ; 17E6
        xor_ cx,cx                                      ; 17E7
        push cx                                         ; 17E9
        mov dx,0x10                                     ; 17EA
        push dx                                         ; 17ED
        callf L5_0538, R6_17F1, R6_1808                 ; 17EE far seg5
        push word [bp-0x2]                              ; 17F3
        mov ax,0x2                                      ; 17F6

L6_17F9:
        push ax                                         ; 17F9
        mov ax,0x1                                      ; 17FA

L6_17FD:
        push ax                                         ; 17FD
        xor_ ax,ax                                      ; 17FE

L6_1800:
        push ax                                         ; 1800
        mov ax,0x10                                     ; 1801
        push ax                                         ; 1804
        callf L5_0538, R6_1808, R6_0F03                 ; 1805 far seg5
        push si                                         ; 180A
        mov ax,0x3bc                                    ; 180B
        push ax                                         ; 180E
        sub_ ax,ax                                      ; 180F
        push ax                                         ; 1811
        push ax                                         ; 1812
        callf L1_0010, R6_1816, R6_1824                 ; 1813 far seg1
        push si                                         ; 1818
        callp R6_181A, R6_0FB6, 0x0000                  ; 1819 KERNEL.LocalFree
        push word [bp-0x2]                              ; 181E
        callf audio2_init, R6_1824, R6_1899             ; 1821 far seg1
        push word [bp-0x2]                              ; 1826
        callf wod_release, R6_182C, R6_186E             ; 1829 far seg4
        mov bx,[bp-0x2]                                 ; 182E
        cmp word [bx+0x76],byte +0x0                    ; 1831
        jz short L6_183B                                ; 1835
        push bx                                         ; 1837
        call L6_0566                                    ; 1838

L6_183B:
        xor_ ax,ax                                      ; 183B
        jmp short L6_1842                               ; 183D

L6_183F:
        mov ax,0x21                                     ; 183F

L6_1842:
        xor_ dx,dx                                      ; 1842
        pop si                                          ; 1844
        mov_ sp,bp                                      ; 1845
        pop bp                                          ; 1847
        ret 0x4                                         ; 1848
        db 0x90                                         ; 184B

; stop playback, mark suspended (+70h bit 2)
wod_suspend:
        push bp                                         ; 184C
        mov_ bp,sp                                      ; 184D
        push si                                         ; 184F
        mov si,[bp+0x6]                                 ; 1850
        test byte [si+0x70],0x1                         ; 1853
        jz short L6_1874                                ; 1857
        cmp word [si+0xaa],byte +0x0                    ; 1859
        jnz short L6_186A                               ; 185E
        push si                                         ; 1860
        callf L6_2EE4, R6_1864, R6_18C1                 ; 1861 far seg6
        or byte [si+0x70],0x8                           ; 1866

L6_186A:
        push si                                         ; 186A
        callf wod_release, R6_186E, R6_188F             ; 186B far seg4
        or byte [si+0x70],0x4                           ; 1870

L6_1874:
        mov ax,0x1                                      ; 1874
        pop si                                          ; 1877
        mov_ sp,bp                                      ; 1878
        pop bp                                          ; 187A
        retf 0x2                                        ; 187B

; audio2_init again, then restart
wod_resume:
        push bp                                         ; 187E
        mov_ bp,sp                                      ; 187F
        push si                                         ; 1881
        mov si,[bp+0x6]                                 ; 1882
        test byte [si+0x70],0x4                         ; 1885
        jz short L6_18D9                                ; 1889
        push si                                         ; 188B
        callf wod_acquire, R6_188F, R6_15B2             ; 188C far seg4
        or_ ax,ax                                       ; 1891
        jnz short L6_18D5                               ; 1893
        push si                                         ; 1895
        callf audio2_init, R6_1899, R6_1375             ; 1896 far seg1
        or_ ax,ax                                       ; 189B
        jz short L6_18CF                                ; 189D
        push si                                         ; 189F
        push word [si+0x105]                            ; 18A0
        mov al,[si+0x107]                               ; 18A4
        and ax,strict word 0x2                          ; 18A8
        cmp ax,strict word 0x1                          ; 18AB
        sbb_ ax,ax                                      ; 18AE
        inc ax                                          ; 18B0
        inc ax                                          ; 18B1
        push ax                                         ; 18B2
        xor_ al,al                                      ; 18B3
        push ax                                         ; 18B5
        push word [si+0x109]                            ; 18B6
        push word [si+0x107]                            ; 18BA
        callf L6_238A, R6_18C1, R6_18CD                 ; 18BE far seg6
        test byte [si+0x70],0x8                         ; 18C3
        jz short L6_18CF                                ; 18C7
        push si                                         ; 18C9
        callf L6_2BE8, R6_18CD, R6_0B8E                 ; 18CA far seg6

L6_18CF:
        and byte [si+0x70],0xf3                         ; 18CF
        jmp short L6_18D9                               ; 18D3

L6_18D5:
        xor_ ax,ax                                      ; 18D5
        jmp short L6_18DC                               ; 18D7

L6_18D9:
        mov ax,0x1                                      ; 18D9

L6_18DC:
        pop si                                          ; 18DC
        mov_ sp,bp                                      ; 18DD
        pop bp                                          ; 18DF
        retf 0x2                                        ; 18E0
        db 0x90                                         ; 18E3

L6_18E4:
        push bp                                         ; 18E4
        mov_ bp,sp                                      ; 18E5
        sub sp,byte +0x20                               ; 18E7
        push di                                         ; 18EA
        push si                                         ; 18EB
        push ds                                         ; 18EC
        movsel ax, R6_18EE, R6_0FFC                     ; 18ED seg7
        mov ds,ax                                       ; 18F0
        mov ax,[bp+0x12]                                ; 18F2
        cmp ax,strict word 0x67                         ; 18F5
        jnz short L6_18FD                               ; 18F8
        jmp near L6_1C5C                                ; 18FA

L6_18FD:
        ja short L6_1937                                ; 18FD
        sub al,0x3                                      ; 18FF
        jnz short L6_1906                               ; 1901
        jmp near L6_1AC5                                ; 1903

L6_1906:
        dec al                                          ; 1906
        jnz short L6_190D                               ; 1908
        jmp near L6_1AF5                                ; 190A

L6_190D:
        dec al                                          ; 190D
        jnz short L6_1914                               ; 190F
        jmp near L6_1B26                                ; 1911

L6_1914:
        sub al,0xb                                      ; 1914
        jnz short L6_191B                               ; 1916
        jmp near L6_1B72                                ; 1918

L6_191B:
        dec al                                          ; 191B
        jnz short L6_1922                               ; 191D
        jmp near L6_1BA5                                ; 191F

L6_1922:
        sub al,0x53                                     ; 1922
        jnz short L6_1929                               ; 1924
        jmp near L6_1C1B                                ; 1926

L6_1929:
        dec al                                          ; 1929
        jnz short L6_1930                               ; 192B
        jmp near L6_1C42                                ; 192D

L6_1930:
        dec al                                          ; 1930
        jnz short L6_1937                               ; 1932
        jmp near L6_1C4F                                ; 1934

L6_1937:
        cmp word [bp+0x14],byte +0x0                    ; 1937
        jz short L6_1943                                ; 193B

L6_193D:
        mov ax,0x2                                      ; 193D
        jmp near L6_1AEA                                ; 1940

L6_1943:
        mov di,[bp+0xe]                                 ; 1943
        mov si,[di]                                     ; 1946
        cmp word [si+0x1c],byte +0x0                    ; 1948
        jnz short L6_1951                               ; 194C
        jmp near L6_1AC0                                ; 194E

L6_1951:
        test byte [si+0x70],0x4                         ; 1951
        jz short L6_195A                                ; 1955
        jmp near L6_1AC0                                ; 1957

L6_195A:
        mov ax,[bp+0x12]                                ; 195A
        sub ax,strict word 0x6                          ; 195D
        cmp ax,strict word 0xe                          ; 1960
        ja short L6_198B                                ; 1963
        add_ ax,ax                                      ; 1965
        xchg ax,bx                                      ; 1967
        jmp [cs:bx+JT6_196D]                            ; 1968
JT6_196D:
        dw L6_1991                                      ; 196D
        dw L6_198B                                      ; 196F
        dw L6_198B                                      ; 1971
        dw L6_199B                                      ; 1973
        dw L6_19F7                                      ; 1975
        dw L6_1A13                                      ; 1977
        dw L6_1A32                                      ; 1979
        dw L6_1A7C                                      ; 197B
        dw L6_198B                                      ; 197D
        dw L6_198B                                      ; 197F
        dw L6_198B                                      ; 1981
        dw L6_198B                                      ; 1983
        dw L6_198B                                      ; 1985
        dw L6_198B                                      ; 1987
        dw L6_1A99                                      ; 1989

L6_198B:
        mov ax,0x8                                      ; 198B
        jmp near L6_1AEA                                ; 198E

L6_1991:
        push word [bp+0x10]                             ; 1991
        push di                                         ; 1994
        call wod_close                                  ; 1995
        jmp near L6_1C67                                ; 1998

L6_199B:
        mov ax,[si+0x101]                               ; 199B
        mov dx,[si+0x103]                               ; 199F
        cmp_ di,ax                                      ; 19A3
        jnz short L6_19AC                               ; 19A5
        cmp [bp+0x10],dx                                ; 19A7
        jz short L6_19B2                                ; 19AA

L6_19AC:
        mov ax,0xb                                      ; 19AC
        jmp near L6_1AEA                                ; 19AF

L6_19B2:
        mov cx,[bp+0xc]                                 ; 19B2
        mov bx,[bp+0xa]                                 ; 19B5
        mov es,cx                                       ; 19B8
        mov al,[es:bx+0x10]                             ; 19BA
        and ax,strict word 0x1f                         ; 19BE
        sub_ dx,dx                                      ; 19C1
        mov [es:bx+0x10],ax                             ; 19C3
        mov [es:bx+0x12],dx                             ; 19C7
        test al,0x2                                     ; 19CB
        jnz short L6_19D5                               ; 19CD
        mov ax,0x22                                     ; 19CF
        jmp near L6_1AEA                                ; 19D2

L6_19D5:
        mov [es:bx+0x1c],di                             ; 19D5
        mov [es:bx+0x1e],ds                             ; 19D9
        or byte [es:bx+0x10],0x10                       ; 19DD
        and byte [es:bx+0x10],0xfe                      ; 19E2
        push cx                                         ; 19E7
        push bx                                         ; 19E8
        push si                                         ; 19E9
        callf L6_2EFE, R6_19ED, R6_1A0F                 ; 19EA far seg6
        add sp,byte +0x6                                ; 19EF

L6_19F2:
        xor_ ax,ax                                      ; 19F2
        jmp near L6_1AEA                                ; 19F4

L6_19F7:
        mov ax,[si+0x101]                               ; 19F7
        mov dx,[si+0x103]                               ; 19FB
        cmp_ di,ax                                      ; 19FF
        jnz short L6_19AC                               ; 1A01
        cmp [bp+0x10],dx                                ; 1A03
        jz short L6_1A0B                                ; 1A06
        jmp near L6_1AAA                                ; 1A08

L6_1A0B:
        push si                                         ; 1A0B
        callf L6_2E3E, R6_1A0F, R6_1A2E                 ; 1A0C far seg6
        jmp short L6_19F2                               ; 1A11

L6_1A13:
        mov ax,[si+0x101]                               ; 1A13
        mov dx,[si+0x103]                               ; 1A17
        cmp_ di,ax                                      ; 1A1B
        jz short L6_1A22                                ; 1A1D
        jmp near L6_1AAA                                ; 1A1F

L6_1A22:
        cmp [bp+0x10],dx                                ; 1A22
        jz short L6_1A2A                                ; 1A25
        jmp near L6_1AAA                                ; 1A27

L6_1A2A:
        push si                                         ; 1A2A
        callf L6_2E18, R6_1A2E, R6_1A47                 ; 1A2B far seg6
        jmp short L6_19F2                               ; 1A30

L6_1A32:
        mov ax,[si+0x101]                               ; 1A32
        mov dx,[si+0x103]                               ; 1A36
        cmp_ di,ax                                      ; 1A3A
        jnz short L6_1AAA                               ; 1A3C
        cmp [bp+0x10],dx                                ; 1A3E
        jnz short L6_1AAA                               ; 1A41
        push si                                         ; 1A43
        callf L6_2EE4, R6_1A47, R6_16EF                 ; 1A44 far seg6
        push si                                         ; 1A49
        call L6_1366                                    ; 1A4A
        xor_ ax,ax                                      ; 1A4D
        mov [si+0xc6],ax                                ; 1A4F
        mov [si+0xaa],ax                                ; 1A53
        mov [si+0xc4],ax                                ; 1A57
        mov [si+0xbe],ax                                ; 1A5B
        mov [si+0xbc],ax                                ; 1A5F
        mov [si+0x10d],ax                               ; 1A63
        mov_ bx,di                                      ; 1A67
        mov [si+0x10b],ax                               ; 1A69
        mov [bx+0x12],ax                                ; 1A6D
        mov [bx+0x10],ax                                ; 1A70
        mov [bx+0x16],ax                                ; 1A73
        mov [bx+0x14],ax                                ; 1A76
        jmp near L6_19F2                                ; 1A79

L6_1A7C:
        mov ax,[bp+0x10]                                ; 1A7C
        or_ ax,di                                       ; 1A7F
        jz short L6_1AAA                                ; 1A81
        push word [bp+0x10]                             ; 1A83
        push di                                         ; 1A86
        push word [bp+0xc]                              ; 1A87
        push word [bp+0xa]                              ; 1A8A
        push word [bp+0x8]                              ; 1A8D
        push word [bp+0x6]                              ; 1A90
        call L6_039A                                    ; 1A93
        jmp near L6_1C67                                ; 1A96

L6_1A99:
        mov ax,[si+0x101]                               ; 1A99
        mov dx,[si+0x103]                               ; 1A9D
        cmp_ di,ax                                      ; 1AA1
        jnz short L6_1AAA                               ; 1AA3
        cmp [bp+0x10],dx                                ; 1AA5
        jz short L6_1AAD                                ; 1AA8

L6_1AAA:
        jmp near L6_19AC                                ; 1AAA

L6_1AAD:
        mov ax,[si+0xae]                                ; 1AAD
        or ax,[si+0xac]                                 ; 1AB1
        jz short L6_1AD6                                ; 1AB5
        mov word [si+0xc6],0x1                          ; 1AB7
        jmp near L6_19F2                                ; 1ABD

L6_1AC0:
        mov ax,0x3                                      ; 1AC0
        jmp short L6_1AEA                               ; 1AC3

L6_1AC5:
        push word [bp+0xc]                              ; 1AC5
        push word [bp+0xa]                              ; 1AC8
        callf L3_4EAE, R6_1ACE, R6_1B07                 ; 1ACB far seg3
        mov_ si,ax                                      ; 1AD0
        or_ si,ax                                       ; 1AD2
        jnz short L6_1AD9                               ; 1AD4

L6_1AD6:
        jmp near L6_19F2                                ; 1AD6

L6_1AD9:
        mov ax,[bp+0xc]                                 ; 1AD9
        or ax,[bp+0xa]                                  ; 1ADC
        jz short L6_1AEF                                ; 1ADF
        cmp word [si+0x1c],byte +0x0                    ; 1AE1
        jz short L6_1AEF                                ; 1AE5
        mov ax,0x1                                      ; 1AE7

L6_1AEA:
        xor_ dx,dx                                      ; 1AEA
        jmp near L6_1C67                                ; 1AEC

L6_1AEF:
        xor_ ax,ax                                      ; 1AEF
        cwd                                             ; 1AF1
        jmp near L6_1C67                                ; 1AF2

L6_1AF5:
        cmp word [bp+0x14],byte +0x0                    ; 1AF5
        jz short L6_1AFE                                ; 1AF9
        jmp near L6_193D                                ; 1AFB

L6_1AFE:
        push word [bp+0x8]                              ; 1AFE
        push word [bp+0x6]                              ; 1B01
        callf L3_4EAE, R6_1B07, R6_1B3F                 ; 1B04 far seg3
        mov_ si,ax                                      ; 1B09
        or_ si,ax                                       ; 1B0B
        jnz short L6_1B12                               ; 1B0D
        jmp near L6_193D                                ; 1B0F

L6_1B12:
        cmp word [si+0x1c],byte +0x0                    ; 1B12
        jz short L6_1AC0                                ; 1B16
        push si                                         ; 1B18
        push word [bp+0xc]                              ; 1B19
        push word [bp+0xa]                              ; 1B1C
        push cs                                         ; 1B1F
        call L6_13FE                                    ; 1B20
        jmp near L6_19F2                                ; 1B23

L6_1B26:
        cmp word [bp+0x14],byte +0x0                    ; 1B26
        jnz short L6_1B47                               ; 1B2A
        mov cx,[bp+0xc]                                 ; 1B2C
        mov bx,[bp+0xa]                                 ; 1B2F
        mov es,cx                                       ; 1B32
        push word [es:bx+0x12]                          ; 1B34
        push word [es:bx+0x10]                          ; 1B38
        callf L3_4EAE, R6_1B3F, R6_1B7B                 ; 1B3C far seg3
        mov_ si,ax                                      ; 1B41
        or_ si,ax                                       ; 1B43
        jnz short L6_1B4A                               ; 1B45

L6_1B47:
        jmp near L6_193D                                ; 1B47

L6_1B4A:
        cmp word [si+0x1c],byte +0x0                    ; 1B4A
        jz short L6_1B56                                ; 1B4E
        test byte [si+0x70],0x4                         ; 1B50
        jz short L6_1B59                                ; 1B54

L6_1B56:
        jmp near L6_1AC0                                ; 1B56

L6_1B59:
        push si                                         ; 1B59
        push word [bp+0x10]                             ; 1B5A
        push word [bp+0xe]                              ; 1B5D
        push word [bp+0xc]                              ; 1B60
        push word [bp+0xa]                              ; 1B63
        push word [bp+0x8]                              ; 1B66
        push word [bp+0x6]                              ; 1B69
        call wod_open                                   ; 1B6C
        jmp near L6_1C67                                ; 1B6F

L6_1B72:
        push word [bp+0x8]                              ; 1B72
        push word [bp+0x6]                              ; 1B75
        callf L3_4EAE, R6_1B7B, R6_1BAE                 ; 1B78 far seg3
        mov_ di,ax                                      ; 1B7D
        or_ di,ax                                       ; 1B7F
        jz short L6_1BB6                                ; 1B81
        les bx,[di+0x26]                                ; 1B83
        mov [bp-0x2],es                                 ; 1B86
        mov ax,[es:bx+0x2830]                           ; 1B89
        mov cx,[es:bx+0x282c]                           ; 1B8E
        mov dx,[bp+0xc]                                 ; 1B93
        mov bx,[bp+0xa]                                 ; 1B96
        mov es,dx                                       ; 1B99
        mov [es:bx],cx                                  ; 1B9B
        mov [es:bx+0x2],ax                              ; 1B9E
        jmp near L6_19F2                                ; 1BA2

L6_1BA5:
        push word [bp+0x8]                              ; 1BA5
        push word [bp+0x6]                              ; 1BA8
        callf L3_4EAE, R6_1BAE, R6_1C24                 ; 1BAB far seg3
        mov_ si,ax                                      ; 1BB0
        or_ si,ax                                       ; 1BB2
        jnz short L6_1BB9                               ; 1BB4

L6_1BB6:
        jmp near L6_193D                                ; 1BB6

L6_1BB9:
        mov word [bp-0x1c],0x4                          ; 1BB9
        mov word [bp-0x1a],0x0                          ; 1BBE
        mov word [bp-0x20],0x18                         ; 1BC3
        mov word [bp-0x1e],0x0                          ; 1BC8
        mov word [bp-0x18],0x2                          ; 1BCD
        mov word [bp-0x16],0x0                          ; 1BD2
        sub_ ax,ax                                      ; 1BD7
        mov [bp-0x12],ax                                ; 1BD9
        mov [bp-0x14],ax                                ; 1BDC
        mov word [bp-0x10],0x4                          ; 1BDF
        mov [bp-0xe],ax                                 ; 1BE4
        lea ax,[bp-0x8]                                 ; 1BE7
        mov [bp-0xc],ax                                 ; 1BEA
        mov [bp-0xa],ss                                 ; 1BED
        mov_ bx,ax                                      ; 1BF0
        mov cx,[bp+0xa]                                 ; 1BF2
        mov [ss:bx],cx                                  ; 1BF5
        mov word [ss:bx+0x2],0x0                        ; 1BF8
        mov ax,[bp+0xc]                                 ; 1BFE
        mov [bp-0x4],ax                                 ; 1C01
        mov word [bp-0x2],0x0                           ; 1C04
        push si                                         ; 1C09
        lea ax,[bp-0x20]                                ; 1C0A
        push ss                                         ; 1C0D
        push ax                                         ; 1C0E
        sub_ ax,ax                                      ; 1C0F
        push ax                                         ; 1C11
        push ax                                         ; 1C12
        callf mxd_set_control_details, R6_1C16, R6_1722 ; 1C13 far seg5
        jmp near L6_19F2                                ; 1C18

L6_1C1B:
        push word [bp+0x8]                              ; 1C1B
        push word [bp+0x6]                              ; 1C1E
        callf L3_43E2, R6_1C24, R6_1C30                 ; 1C21 far seg3
        cmp word [0xc8],byte +0x0                       ; 1C26
        jz short L6_1C35                                ; 1C2B
        callf vxd_check, R6_1C30, R6_1C3E               ; 1C2D far seg3
        jmp near L6_19F2                                ; 1C32

L6_1C35:
        push word [bp+0x8]                              ; 1C35
        push word [bp+0x6]                              ; 1C38
        callf L3_47C4, R6_1C3E, R6_1C4B                 ; 1C3B far seg3
        jmp short L6_1C67                               ; 1C40

L6_1C42:
        push word [bp+0x8]                              ; 1C42
        push word [bp+0x6]                              ; 1C45
        callf L3_4E68, R6_1C4B, R6_1C58                 ; 1C48 far seg3
        jmp short L6_1C67                               ; 1C4D

L6_1C4F:
        push word [bp+0x8]                              ; 1C4F
        push word [bp+0x6]                              ; 1C52
        callf L3_4D2C, R6_1C58, R6_1C65                 ; 1C55 far seg3
        jmp short L6_1C67                               ; 1C5A

L6_1C5C:
        push word [bp+0x8]                              ; 1C5C
        push word [bp+0x6]                              ; 1C5F
        callf L3_4940, R6_1C65, R6_12A6                 ; 1C62 far seg3

L6_1C67:
        pop ds                                          ; 1C67
        pop si                                          ; 1C68
        pop di                                          ; 1C69
        mov_ sp,bp                                      ; 1C6A
        pop bp                                          ; 1C6C
        retf 0x10                                       ; 1C6D
        db 0x90, 0x90, 0x90, 0x90                       ; 1C70

L6_1C74:
        push bp                                         ; 1C74
        mov_ bp,sp                                      ; 1C75
        sub sp,0xac                                     ; 1C77
        mov word [bp-0x2c],0x2e                         ; 1C7B
        mov word [bp-0x28],0x404                        ; 1C80
        mov word [bp-0x4],0x3                           ; 1C85
        mov word [bp-0x2],0x0                           ; 1C8A
        mov ax,[bp+0xa]                                 ; 1C8F
        or_ ax,ax                                       ; 1C92
        jz short L6_1CA9                                ; 1C94
        mov word [bp-0x2a],0x8                          ; 1C96
        mov word [bp-0x6],0x1                           ; 1C9B
        push word [0xbd0]                               ; 1CA0
        mov ax,0x16                                     ; 1CA4
        jmp short L6_1CBA                               ; 1CA7

L6_1CA9:
        mov word [bp-0x2a],0x3                          ; 1CA9
        mov word [bp-0x6],0x2                           ; 1CAE
        push word [0xbd0]                               ; 1CB3
        mov ax,0x15                                     ; 1CB7

L6_1CBA:
        push ax                                         ; 1CBA
        lea ax,[bp-0x6c]                                ; 1CBB
        push ss                                         ; 1CBE
        push ax                                         ; 1CBF
        mov ax,0x40                                     ; 1CC0
        push ax                                         ; 1CC3
        callp R6_1CC5, R6_1443, 0x0000                  ; 1CC4 USER.LoadString
        mov bx,[bp+0xc]                                 ; 1CC9
        push word [bx]                                  ; 1CCC
        lea ax,[bp-0x6c]                                ; 1CCE
        push ss                                         ; 1CD1
        push ax                                         ; 1CD2
        lea ax,[bp-0xac]                                ; 1CD3
        push ss                                         ; 1CD7
        push ax                                         ; 1CD8
        callp R6_1CDA, R6_1458, 0x0000                  ; 1CD9 USER.wsprintf
        add sp,byte +0xa                                ; 1CDE
        lea ax,[bp-0x26]                                ; 1CE1
        push ss                                         ; 1CE4
        push ax                                         ; 1CE5
        lea ax,[bp-0xac]                                ; 1CE6
        push ss                                         ; 1CEA
        push ax                                         ; 1CEB
        mov ax,0x1f                                     ; 1CEC
        push ax                                         ; 1CEF
        callp R6_1CF1, R6_146F, 0x0000                  ; 1CF0 KERNEL.lstrcpyn
        les bx,[bp+0x6]                                 ; 1CF5
        push word [es:bx+0x6]                           ; 1CF8
        push word [es:bx+0x4]                           ; 1CFC
        lea ax,[bp-0x2c]                                ; 1D00
        push ss                                         ; 1D03
        push ax                                         ; 1D04
        mov ax,[es:bx]                                  ; 1D05
        cmp ax,strict word 0x2c                         ; 1D08
        jna short L6_1D10                               ; 1D0B
        mov ax,0x2c                                     ; 1D0D

L6_1D10:
        push ax                                         ; 1D10
        callf L1_1AD3, R6_1D14, R6_1755                 ; 1D11 far seg1
        mov_ sp,bp                                      ; 1D16
        pop bp                                          ; 1D18
        retf 0x8                                        ; 1D19

L6_1D1C:
        push bp                                         ; 1D1C
        mov_ bp,sp                                      ; 1D1D
        sub sp,byte +0x20                               ; 1D1F
        push di                                         ; 1D22
        push si                                         ; 1D23
        push ds                                         ; 1D24
        movsel ax, R6_1D26, R6_18EE                     ; 1D25 seg7
        mov ds,ax                                       ; 1D28
        mov cx,[bp+0x12]                                ; 1D2A
        mov_ ax,cx                                      ; 1D2D
        cmp ax,strict word 0x67                         ; 1D2F
        jnz short L6_1D37                               ; 1D32
        jmp near L6_1F13                                ; 1D34

L6_1D37:
        ja short L6_1D5C                                ; 1D37
        sub al,0x3                                      ; 1D39
        jnz short L6_1D40                               ; 1D3B
        jmp near L6_1E73                                ; 1D3D

L6_1D40:
        dec al                                          ; 1D40
        jnz short L6_1D47                               ; 1D42
        jmp near L6_1E9F                                ; 1D44

L6_1D47:
        sub al,0x60                                     ; 1D47
        jnz short L6_1D4E                               ; 1D49
        jmp near L6_1ED2                                ; 1D4B

L6_1D4E:
        dec al                                          ; 1D4E
        jnz short L6_1D55                               ; 1D50
        jmp near L6_1EF9                                ; 1D52

L6_1D55:
        dec al                                          ; 1D55
        jnz short L6_1D5C                               ; 1D57
        jmp near L6_1F06                                ; 1D59

L6_1D5C:
        mov di,[bp+0x14]                                ; 1D5C
        cmp di,byte +0x1                                ; 1D5F
        jna short L6_1D67                               ; 1D62
        jmp near L6_1E92                                ; 1D64

L6_1D67:
        mov_ ax,cx                                      ; 1D67
        sub ax,strict word 0x5                          ; 1D69
        jz short L6_1D7F                                ; 1D6C
        dec ax                                          ; 1D6E
        jz short L6_1DD0                                ; 1D6F
        sub ax,0x4681                                   ; 1D71
        jnz short L6_1D79                               ; 1D74
        jmp near L6_1E4D                                ; 1D76

L6_1D79:
        mov ax,0x8                                      ; 1D79
        jmp near L6_1E95                                ; 1D7C

L6_1D7F:
        push word [bp+0x8]                              ; 1D7F
        push word [bp+0x6]                              ; 1D82
        callf L3_4EAE, R6_1D88, R6_1DD9                 ; 1D85 far seg3
        mov_ si,ax                                      ; 1D8A
        or_ si,ax                                       ; 1D8C
        jnz short L6_1D93                               ; 1D8E
        jmp near L6_1E92                                ; 1D90

L6_1D93:
        mov ax,[si+0x26]                                ; 1D93
        mov dx,[si+0x28]                                ; 1D96
        mov [bp-0x4],ax                                 ; 1D99
        mov [bp-0x2],dx                                 ; 1D9C
        mov cl,0x3                                      ; 1D9F
        cmp di,byte +0x1                                ; 1DA1
        sbb_ bx,bx                                      ; 1DA4
        and bl,0xfd                                     ; 1DA6
        add bx,byte +0x6                                ; 1DA9
        shl bx,cl                                       ; 1DAC
        add_ bx,ax                                      ; 1DAE
        mov es,dx                                       ; 1DB0
        mov ax,[es:bx+0x2810]                           ; 1DB2
        mov cx,[es:bx+0x280c]                           ; 1DB7
        mov dx,[bp+0xc]                                 ; 1DBC
        mov bx,[bp+0xa]                                 ; 1DBF
        mov es,dx                                       ; 1DC2
        mov [es:bx],cx                                  ; 1DC4
        mov [es:bx+0x2],ax                              ; 1DC7

L6_1DCB:
        xor_ ax,ax                                      ; 1DCB
        jmp near L6_1E95                                ; 1DCD

L6_1DD0:
        push word [bp+0x8]                              ; 1DD0
        push word [bp+0x6]                              ; 1DD3
        callf L3_4EAE, R6_1DD9, R6_1E7C                 ; 1DD6 far seg3
        mov_ si,ax                                      ; 1DDB
        or_ si,ax                                       ; 1DDD
        jnz short L6_1DE4                               ; 1DDF
        jmp near L6_1E92                                ; 1DE1

L6_1DE4:
        cmp di,byte +0x1                                ; 1DE4
        sbb_ ax,ax                                      ; 1DE7
        and al,0xfd                                     ; 1DE9
        add ax,strict word 0x6                          ; 1DEB
        cwd                                             ; 1DEE
        mov [bp-0x1c],ax                                ; 1DEF
        mov [bp-0x1a],dx                                ; 1DF2
        mov word [bp-0x20],0x18                         ; 1DF5
        mov word [bp-0x1e],0x0                          ; 1DFA
        mov word [bp-0x18],0x2                          ; 1DFF
        mov word [bp-0x16],0x0                          ; 1E04
        sub_ ax,ax                                      ; 1E09
        mov [bp-0x12],ax                                ; 1E0B
        mov [bp-0x14],ax                                ; 1E0E
        mov word [bp-0x10],0x4                          ; 1E11
        mov [bp-0xe],ax                                 ; 1E16
        lea ax,[bp-0x8]                                 ; 1E19
        mov [bp-0xc],ax                                 ; 1E1C
        mov [bp-0xa],ss                                 ; 1E1F
        mov_ bx,ax                                      ; 1E22
        mov cx,[bp+0xa]                                 ; 1E24
        mov [ss:bx],cx                                  ; 1E27
        mov word [ss:bx+0x2],0x0                        ; 1E2A
        mov ax,[bp+0xc]                                 ; 1E30
        mov [bp-0x4],ax                                 ; 1E33
        mov word [bp-0x2],0x0                           ; 1E36
        push si                                         ; 1E3B
        lea ax,[bp-0x20]                                ; 1E3C
        push ss                                         ; 1E3F
        push ax                                         ; 1E40
        sub_ ax,ax                                      ; 1E41
        push ax                                         ; 1E43
        push ax                                         ; 1E44
        callf mxd_set_control_details, R6_1E48, R6_1C16 ; 1E45 far seg5
        jmp near L6_1DCB                                ; 1E4A

L6_1E4D:
        push word [bp+0x8]                              ; 1E4D
        push word [bp+0x6]                              ; 1E50
        les bx,[bp+0xa]                                 ; 1E53
        push word [es:bx+0x2]                           ; 1E56
        push word [es:bx]                               ; 1E5A
        push word [es:bx+0x6]                           ; 1E5D
        push word [es:bx+0x4]                           ; 1E61
        callf vxd_0005, R6_1E68, R6_182C                ; 1E65 far seg4
        or_ ax,ax                                       ; 1E6A
        jz short L6_1E9A                                ; 1E6C
        mov ax,0x1                                      ; 1E6E
        jmp short L6_1E95                               ; 1E71

L6_1E73:
        push word [bp+0xc]                              ; 1E73
        push word [bp+0xa]                              ; 1E76
        callf L3_4EAE, R6_1E7C, R6_1EB0                 ; 1E79 far seg3
        mov_ si,ax                                      ; 1E7E
        or_ si,ax                                       ; 1E80
        jnz short L6_1E8C                               ; 1E82
        xor_ ax,ax                                      ; 1E84
        mov dx,0xb                                      ; 1E86
        jmp near L6_1F1E                                ; 1E89

L6_1E8C:
        cmp word [si+0x1c],byte +0x0                    ; 1E8C
        jz short L6_1E9A                                ; 1E90

L6_1E92:
        mov ax,0x2                                      ; 1E92

L6_1E95:
        xor_ dx,dx                                      ; 1E95
        jmp near L6_1F1E                                ; 1E97

L6_1E9A:
        xor_ ax,ax                                      ; 1E9A
        cwd                                             ; 1E9C
        jmp short L6_1F1E                               ; 1E9D

L6_1E9F:
        mov di,[bp+0x14]                                ; 1E9F
        cmp di,byte +0x1                                ; 1EA2
        ja short L6_1E92                                ; 1EA5
        push word [bp+0x8]                              ; 1EA7
        push word [bp+0x6]                              ; 1EAA
        callf L3_4EAE, R6_1EB0, R6_1EDB                 ; 1EAD far seg3
        mov_ si,ax                                      ; 1EB2
        or_ si,ax                                       ; 1EB4
        jz short L6_1E92                                ; 1EB6
        cmp word [si+0x1c],byte +0x0                    ; 1EB8
        jnz short L6_1EC3                               ; 1EBC
        mov ax,0x3                                      ; 1EBE
        jmp short L6_1E95                               ; 1EC1

L6_1EC3:
        push si                                         ; 1EC3
        push di                                         ; 1EC4
        push word [bp+0xc]                              ; 1EC5
        push word [bp+0xa]                              ; 1EC8
        push cs                                         ; 1ECB
        call L6_1C74                                    ; 1ECC
        jmp near L6_1DCB                                ; 1ECF

L6_1ED2:
        push word [bp+0x8]                              ; 1ED2
        push word [bp+0x6]                              ; 1ED5
        callf L3_43E2, R6_1EDB, R6_1EE7                 ; 1ED8 far seg3
        cmp word [0xc8],byte +0x0                       ; 1EDD
        jz short L6_1EEC                                ; 1EE2
        callf vxd_check, R6_1EE7, R6_1EF5               ; 1EE4 far seg3
        jmp near L6_1DCB                                ; 1EE9

L6_1EEC:
        push word [bp+0x8]                              ; 1EEC
        push word [bp+0x6]                              ; 1EEF
        callf L3_47C4, R6_1EF5, R6_1F02                 ; 1EF2 far seg3
        jmp short L6_1F1E                               ; 1EF7

L6_1EF9:
        push word [bp+0x8]                              ; 1EF9
        push word [bp+0x6]                              ; 1EFC
        callf L3_4E68, R6_1F02, R6_1F0F                 ; 1EFF far seg3
        jmp short L6_1F1E                               ; 1F04

L6_1F06:
        push word [bp+0x8]                              ; 1F06
        push word [bp+0x6]                              ; 1F09
        callf L3_4D2C, R6_1F0F, R6_1F1C                 ; 1F0C far seg3
        jmp short L6_1F1E                               ; 1F11

L6_1F13:
        push word [bp+0x8]                              ; 1F13
        push word [bp+0x6]                              ; 1F16
        callf L3_4940, R6_1F1C, R6_1ACE                 ; 1F19 far seg3

L6_1F1E:
        pop ds                                          ; 1F1E
        pop si                                          ; 1F1F
        pop di                                          ; 1F20
        mov_ sp,bp                                      ; 1F21
        pop bp                                          ; 1F23
        retf 0x10                                       ; 1F24
        db 0x90                                         ; 1F27

L6_1F28:
        push bp                                         ; 1F28
        mov_ bp,sp                                      ; 1F29
        push di                                         ; 1F2B
        mov ax,[0x5e]                                   ; 1F2C
        or ax,[0x5c]                                    ; 1F2F
        jz short L6_1F3E                                ; 1F33

L6_1F35:
        mov ax,[0x5c]                                   ; 1F35
        mov dx,[0x5e]                                   ; 1F38
        jmp short L6_1F5A                               ; 1F3C

L6_1F3E:
        push bx                                         ; 1F3E
        push es                                         ; 1F3F
        push di                                         ; 1F40
        xor_ di,di                                      ; 1F41
        mov ax,0x1684                                   ; 1F43
        mov bx,0x33                                     ; 1F46
        mov es,di                                       ; 1F49
        int 0x2f                                        ; 1F4B
        mov [0x5e],es                                   ; 1F4D
        mov [0x5c],di                                   ; 1F51
        pop di                                          ; 1F55
        pop es                                          ; 1F56
        pop bx                                          ; 1F57
        jmp short L6_1F35                               ; 1F58

L6_1F5A:
        pop di                                          ; 1F5A
        mov_ sp,bp                                      ; 1F5B
        pop bp                                          ; 1F5D
        retf                                            ; 1F5E
        db 0x90                                         ; 1F5F

L6_1F60:
        push bp                                         ; 1F60
        mov_ bp,sp                                      ; 1F61
        sub sp,byte +0x8                                ; 1F63
        mov word [bp-0x2],0x0                           ; 1F66
        mov word [bp-0x8],0x3e                          ; 1F6B
        push cs                                         ; 1F70
        call L6_1F28                                    ; 1F71
        mov [bp-0x6],ax                                 ; 1F74
        mov [bp-0x4],dx                                 ; 1F77
        or_ dx,ax                                       ; 1F7A
        jnz short L6_1F82                               ; 1F7C
        xor_ ax,ax                                      ; 1F7E
        jmp short L6_1F8E                               ; 1F80

L6_1F82:
        mov ax,[bp-0x8]                                 ; 1F82
        call far [bp-0x6]                               ; 1F85
        mov [bp-0x2],ax                                 ; 1F88
        mov ax,[bp-0x2]                                 ; 1F8B

L6_1F8E:
        mov_ sp,bp                                      ; 1F8E
        pop bp                                          ; 1F90
        ret                                             ; 1F91

; mixer register 00h (reset), then the driver's defaults (14h from Telegaming Vol)
mixer_reset_defaults:
        push bp                                         ; 1F92
        mov_ bp,sp                                      ; 1F93
        sub sp,byte +0x6                                ; 1F95
        push si                                         ; 1F98
        mov si,[bp+0x6]                                 ; 1F99
        push si                                         ; 1F9C
        xor_ al,al                                      ; 1F9D
        push ax                                         ; 1F9F
        push ax                                         ; 1FA0
        callf mixer_write, R6_1FA4, R6_1FEB             ; 1FA1 far seg1
        mov word [bp-0x6],0x1                           ; 1FA6
        mov word [bp-0x4],0x0                           ; 1FAB
        mov ax,0x1                                      ; 1FB0
        cwd                                             ; 1FB3
        push dx                                         ; 1FB4
        push ax                                         ; 1FB5
        lea ax,[bp-0x6]                                 ; 1FB6
        push ss                                         ; 1FB9
        push ax                                         ; 1FBA
        lea ax,[bp-0x1]                                 ; 1FBB
        push ss                                         ; 1FBE
        push ax                                         ; 1FBF
        mov ax,0x3                                      ; 1FC0
        cwd                                             ; 1FC3
        push dx                                         ; 1FC4
        push ax                                         ; 1FC5
        mov ax,0x90                                     ; 1FC6
        push ds                                         ; 1FC9
        push ax                                         ; 1FCA
        mov ax,0x9f                                     ; 1FCB
        push ds                                         ; 1FCE
        push ax                                         ; 1FCF
        push word [si+0x16]                             ; 1FD0
        push word [si+0x14]                             ; 1FD3
        call L6_1F60                                    ; 1FD6
        add sp,byte +0x1c                               ; 1FD9
        or_ ax,ax                                       ; 1FDC
        jnz short L6_1FED                               ; 1FDE
        push si                                         ; 1FE0
        mov al,0x14                                     ; 1FE1
        push ax                                         ; 1FE3
        mov al,[bp-0x1]                                 ; 1FE4
        push ax                                         ; 1FE7
        callf mixer_write, R6_1FEB, R6_20EB             ; 1FE8 far seg1

L6_1FED:
        pop si                                          ; 1FED
        mov_ sp,bp                                      ; 1FEE
        pop bp                                          ; 1FF0
        retf 0x2                                        ; 1FF1

L6_1FF4:
        push bp                                         ; 1FF4
        mov_ bp,sp                                      ; 1FF5
        sub sp,byte +0x12                               ; 1FF7
        push di                                         ; 1FFA
        push si                                         ; 1FFB
        mov si,[bp+0x10]                                ; 1FFC
        cmp word [si+0x1c],byte +0x0                    ; 1FFF
        jnz short L6_2008                               ; 2003
        jmp near L6_2381                                ; 2005

L6_2008:
        cmp byte [bp+0xa],0x1                           ; 2008
        jnz short L6_2055                               ; 200C
        mov byte [bp-0x1],0x1                           ; 200E
        mov word [si+0xa0],0x0                          ; 2012
        and byte [si+0x2a],0xfd                         ; 2018
        test byte [si+0x2a],0x1                         ; 201C
        jz short L6_202C                                ; 2020
        test byte [bp+0x6],0x3c                         ; 2022
        jnz short L6_202C                               ; 2026
        or byte [si+0x2a],0x2                           ; 2028

L6_202C:
        test byte [si+0x2a],0x4                         ; 202C
        jnz short L6_2036                               ; 2030
        mov byte [bp-0x1],0x0                           ; 2032

L6_2036:
        test byte [bp+0x6],0x1                          ; 2036
        jnz short L6_2042                               ; 203A
        test byte [si+0x2a],0x2                         ; 203C
        jnz short L6_2046                               ; 2040

L6_2042:
        mov byte [bp-0x1],0x0                           ; 2042

L6_2046:
        and byte [bp+0x7],0xbf                          ; 2046
        cmp byte [bp-0x1],0x0                           ; 204A
        jz short L6_2055                                ; 204E
        or word [bp+0x6],0x4001                         ; 2050

L6_2055:
        mov ax,[bp+0x6]                                 ; 2055
        mov dx,[bp+0x8]                                 ; 2058
        mov [si+0x6b],ax                                ; 205B
        mov [si+0x6d],dx                                ; 205E
        mov cx,[si+0x3b]                                ; 2061
        sub_ bx,bx                                      ; 2064
        add cx,byte +0x1                                ; 2066
        adc_ bx,bx                                      ; 2069
        mov [bp-0xa],cx                                 ; 206B
        mov [bp-0x8],bx                                 ; 206E
        shr bx,1                                        ; 2071
        rcr cx,1                                        ; 2073
        mov [si+0x3d],cx                                ; 2075
        and al,0x3c                                     ; 2078
        jnz short L6_207F                               ; 207A
        jmp near L6_2158                                ; 207C

L6_207F:
        mov al,[bp+0x6]                                 ; 207F
        and ax,strict word 0x3c                         ; 2082
        sub_ dx,dx                                      ; 2085
        cmp ax,strict word 0x20                         ; 2087
        jz short L6_20BE                                ; 208A
        ja short L6_20C6                                ; 208C
        sub al,0x4                                      ; 208E
        jz short L6_209C                                ; 2090
        sub al,0x4                                      ; 2092
        jz short L6_20AA                                ; 2094
        sub al,0x8                                      ; 2096
        jz short L6_20B4                                ; 2098
        jmp short L6_20C6                               ; 209A

L6_209C:
        mov byte [si+0x71],0x6f                         ; 209C
        mov byte [si+0x72],0x65                         ; 20A0
        mov byte [si+0x73],0xa                          ; 20A4
        jmp short L6_20C6                               ; 20A8

L6_20AA:
        mov byte [si+0x72],0x67                         ; 20AA
        mov byte [si+0x73],0x8                          ; 20AE
        jmp short L6_20C6                               ; 20B2

L6_20B4:
        mov byte [si+0x72],0x6d                         ; 20B4
        mov byte [si+0x73],0x3                          ; 20B8
        jmp short L6_20C6                               ; 20BC

L6_20BE:
        mov byte [si+0x71],0x7f                         ; 20BE
        mov byte [si+0x72],0x75                         ; 20C2

L6_20C6:
        test byte [bp+0x6],0x1c                         ; 20C6
        jz short L6_20FD                                ; 20CA
        mov ax,[bp+0xe]                                 ; 20CC
        mov cx,0x13                                     ; 20CF
        sub_ dx,dx                                      ; 20D2
        div cx                                          ; 20D4
        mov_ cx,ax                                      ; 20D6
        mov al,[si+0x73]                                ; 20D8
        sub_ ah,ah                                      ; 20DB
        mov [si+0x3f],ax                                ; 20DD
        mov_ bx,ax                                      ; 20E0
        mul cx                                          ; 20E2
        mov cl,0x4                                      ; 20E4
        mov_ di,bx                                      ; 20E6
        callf L1_24F8, R6_20EB, R6_211E                 ; 20E8 far seg1
        cmp ax,[si+0x3d]                                ; 20ED
        jna short L6_20F5                               ; 20F0
        mov ax,[si+0x3d]                                ; 20F2

L6_20F5:
        sub_ dx,dx                                      ; 20F5
        div di                                          ; 20F7
        mul di                                          ; 20F9
        jmp short L6_2103                               ; 20FB

L6_20FD:
        mov ax,0x100                                    ; 20FD
        mov [si+0x3f],ax                                ; 2100

L6_2103:
        mov [si+0x3d],ax                                ; 2103
        mov ax,[bp+0xe]                                 ; 2106
        sub_ dx,dx                                      ; 2109
        push dx                                         ; 210B
        push ax                                         ; 210C
        mov cx,0x4240                                   ; 210D
        mov bx,0xf                                      ; 2110
        push bx                                         ; 2113
        push cx                                         ; 2114
        mov [bp-0xe],ax                                 ; 2115
        mov [bp-0xc],dx                                 ; 2118
        callf L1_242E, R6_211E, R6_2133                 ; 211B far seg1
        mov_ di,ax                                      ; 2120
        push word [bp-0xc]                              ; 2122
        push word [bp-0xe]                              ; 2125
        mov ax,0x4240                                   ; 2128
        mov dx,0xf                                      ; 212B
        push dx                                         ; 212E
        push ax                                         ; 212F
        callf L1_248E, R6_2133, R6_214E                 ; 2130 far seg1
        mov cx,[bp+0xe]                                 ; 2135
        shr cx,1                                        ; 2138
        sub_ bx,bx                                      ; 213A
        cmp_ bx,dx                                      ; 213C
        ja short L6_2147                                ; 213E
        jc short L6_2146                                ; 2140
        cmp_ cx,ax                                      ; 2142
        jnc short L6_2147                               ; 2144

L6_2146:
        inc di                                          ; 2146

L6_2147:
        push si                                         ; 2147
        mov al,0x40                                     ; 2148
        push ax                                         ; 214A
        callf dsp_write, R6_214E, R6_217F               ; 214B far seg1
        push si                                         ; 2150
        mov_ ax,di                                      ; 2151
        neg al                                          ; 2153
        jmp near L6_2375                                ; 2155

L6_2158:
        mov di,[bp+0xe]                                 ; 2158
        mov al,[bp+0x6]                                 ; 215B
        and ax,strict word 0x1                          ; 215E
        cmp ax,strict word 0x1                          ; 2161
        sbb_ ax,ax                                      ; 2164
        inc ax                                          ; 2166
        inc ax                                          ; 2167
        mul word [bp+0xc]                               ; 2168
        mov [si+0x3f],ax                                ; 216B
        sub_ cx,cx                                      ; 216E
        mov [bp-0x12],di                                ; 2170
        mov [bp-0x10],cx                                ; 2173
        push cx                                         ; 2176
        push di                                         ; 2177
        sub_ dx,dx                                      ; 2178
        push dx                                         ; 217A
        push ax                                         ; 217B
        callf L1_23E4, R6_217F, R6_2186                 ; 217C far seg1
        mov cl,0x4                                      ; 2181
        callf L1_24F8, R6_2186, R6_21A7                 ; 2183 far seg1
        and al,0xfc                                     ; 2188
        cmp ax,[si+0x3d]                                ; 218A
        jna short L6_2192                               ; 218D
        mov ax,[si+0x3d]                                ; 218F

L6_2192:
        mov [si+0x3d],ax                                ; 2192
        cmp byte [si+0x6],0x3                           ; 2195
        jna short L6_21C7                               ; 2199
        mov word [bp-0x2],0x4                           ; 219B
        sub_ dx,dx                                      ; 21A0
        mov cl,0x2                                      ; 21A2
        callf L1_2416, R6_21A7, R6_21C0                 ; 21A4 far seg1
        cmp dx,[bp-0x8]                                 ; 21A9
        jc short L6_21C7                                ; 21AC
        ja short L6_21B5                                ; 21AE
        cmp ax,[bp-0xa]                                 ; 21B0
        jna short L6_21C7                               ; 21B3

L6_21B5:
        mov ax,[bp-0xa]                                 ; 21B5
        mov dx,[bp-0x8]                                 ; 21B8
        mov cl,0x2                                      ; 21BB
        callf L1_24F8, R6_21C0, R6_21D8                 ; 21BD far seg1
        and al,0xfc                                     ; 21C2
        mov [si+0x3d],ax                                ; 21C4

L6_21C7:
        push word [bp-0x10]                             ; 21C7
        push word [bp-0x12]                             ; 21CA
        mov ax,0x1198                                   ; 21CD
        mov dx,0x6                                      ; 21D0
        push dx                                         ; 21D3
        push ax                                         ; 21D4
        callf L1_242E, R6_21D8, R6_21EE                 ; 21D5 far seg1
        mov [bp-0x2],al                                 ; 21DA
        push word [bp-0x10]                             ; 21DD
        push word [bp-0x12]                             ; 21E0
        mov ax,0x1198                                   ; 21E3
        mov dx,0x6                                      ; 21E6
        push dx                                         ; 21E9
        push ax                                         ; 21EA
        callf L1_248E, R6_21EE, R6_224A                 ; 21EB far seg1
        mov_ cx,di                                      ; 21F0
        shr cx,1                                        ; 21F2
        sub_ bx,bx                                      ; 21F4
        cmp_ bx,dx                                      ; 21F6
        ja short L6_2203                                ; 21F8
        jc short L6_2200                                ; 21FA
        cmp_ cx,ax                                      ; 21FC
        jnc short L6_2203                               ; 21FE

L6_2200:
        inc byte [bp-0x2]                               ; 2200

L6_2203:
        mov al,0x80                                     ; 2203
        sub al,[bp-0x2]                                 ; 2205
        mov [bp-0x1],al                                 ; 2208
        cmp di,0x5622                                   ; 220B
        jna short L6_2217                               ; 220F
        shl byte [bp-0x2],1                             ; 2211
        shl byte [bp-0x1],1                             ; 2214

L6_2217:
        cmp di,0x5622                                   ; 2217
        ja short L6_2232                                ; 221B
        mov ax,0x80                                     ; 221D
        mov cl,[bp-0x1]                                 ; 2220
        sub_ ch,ch                                      ; 2223
        sub_ ax,cx                                      ; 2225
        cwd                                             ; 2227
        push dx                                         ; 2228
        push ax                                         ; 2229
        mov ax,0x1198                                   ; 222A
        mov dx,0x6                                      ; 222D
        jmp short L6_2245                               ; 2230

L6_2232:
        mov ax,0x100                                    ; 2232
        mov cl,[bp-0x1]                                 ; 2235
        sub_ ch,ch                                      ; 2238
        sub_ ax,cx                                      ; 223A
        cwd                                             ; 223C
        push dx                                         ; 223D
        push ax                                         ; 223E
        mov ax,0x236c                                   ; 223F
        mov dx,0xc                                      ; 2242

L6_2245:
        push dx                                         ; 2245
        push ax                                         ; 2246
        callf L1_242E, R6_224A, R6_229C                 ; 2247 far seg1
        mov [bp-0x4],ax                                 ; 224C
        cmp_ ax,di                                      ; 224F
        jnc short L6_225D                               ; 2251
        inc byte [bp-0x1]                               ; 2253
        mov_ ax,di                                      ; 2256
        sub ax,[bp-0x4]                                 ; 2258
        jmp short L6_2266                               ; 225B

L6_225D:
        cmp_ ax,di                                      ; 225D
        jna short L6_2269                               ; 225F
        dec byte [bp-0x1]                               ; 2261
        sub_ ax,di                                      ; 2264

L6_2266:
        mov [bp-0x6],ax                                 ; 2266

L6_2269:
        cmp di,0x5622                                   ; 2269
        ja short L6_2284                                ; 226D
        mov ax,0x80                                     ; 226F
        mov cl,[bp-0x1]                                 ; 2272
        sub_ ch,ch                                      ; 2275
        sub_ ax,cx                                      ; 2277
        cwd                                             ; 2279
        push dx                                         ; 227A
        push ax                                         ; 227B
        mov ax,0x1198                                   ; 227C
        mov dx,0x6                                      ; 227F
        jmp short L6_2297                               ; 2282

L6_2284:
        mov ax,0x100                                    ; 2284
        mov cl,[bp-0x1]                                 ; 2287
        sub_ ch,ch                                      ; 228A
        sub_ ax,cx                                      ; 228C
        cwd                                             ; 228E
        push dx                                         ; 228F
        push ax                                         ; 2290
        mov ax,0x236c                                   ; 2291
        mov dx,0xc                                      ; 2294

L6_2297:
        push dx                                         ; 2297
        push ax                                         ; 2298
        callf L1_242E, R6_229C, R6_22D6                 ; 2299 far seg1
        mov [bp-0x4],ax                                 ; 229E
        cmp_ ax,di                                      ; 22A1
        jnc short L6_22B4                               ; 22A3
        mov_ ax,di                                      ; 22A5
        sub ax,[bp-0x4]                                 ; 22A7
        cmp ax,[bp-0x6]                                 ; 22AA
        jna short L6_22C4                               ; 22AD
        inc byte [bp-0x1]                               ; 22AF
        jmp short L6_22C4                               ; 22B2

L6_22B4:
        mov_ cx,ax                                      ; 22B4
        cmp_ cx,di                                      ; 22B6
        jna short L6_22C4                               ; 22B8
        sub_ ax,di                                      ; 22BA
        cmp [bp-0x6],ax                                 ; 22BC
        jnc short L6_22C4                               ; 22BF
        dec byte [bp-0x1]                               ; 22C1

L6_22C4:
        cmp di,0xbb80                                   ; 22C4
        jnz short L6_22DC                               ; 22C8
        inc byte [bp-0x1]                               ; 22CA
        push si                                         ; 22CD
        mov al,0x71                                     ; 22CE
        push ax                                         ; 22D0
        push si                                         ; 22D1
        push ax                                         ; 22D2
        callf mixer_read, R6_22D6, R6_1D14              ; 22D3 far seg1
        or al,0x20                                      ; 22D8
        jmp short L6_22E9                               ; 22DA

L6_22DC:
        push si                                         ; 22DC
        mov al,0x71                                     ; 22DD
        push ax                                         ; 22DF
        push si                                         ; 22E0
        push ax                                         ; 22E1
        callf mixer_read, R6_22E5, R6_22ED              ; 22E2 far seg1
        and al,0xdf                                     ; 22E7

L6_22E9:
        push ax                                         ; 22E9
        callf mixer_write, R6_22ED, R6_22F6             ; 22EA far seg1
        push si                                         ; 22EF
        mov al,0xa1                                     ; 22F0
        push ax                                         ; 22F2
        callf dsp_write, R6_22F6, R6_2300               ; 22F3 far seg1
        push si                                         ; 22F8
        mov al,[bp-0x1]                                 ; 22F9
        push ax                                         ; 22FC
        callf dsp_write, R6_2300, R6_2348               ; 22FD far seg1
        cmp di,0x5622                                   ; 2302
        jna short L6_234C                               ; 2306
        cmp byte [bp-0x2],0x12                          ; 2308
        jc short L6_2337                                ; 230C
        cmp byte [bp-0x2],0x23                          ; 230E
        ja short L6_2337                                ; 2312
        cmp byte [bp+0xa],0x1                           ; 2314
        jnz short L6_2325                               ; 2318
        mov bl,[bp-0x2]                                 ; 231A
        sub_ bh,bh                                      ; 231D
        db 0x8A, 0x8F, 0x60, 0x00                       ; 231F mov cl,[bx+0x60]
        jmp short L6_232E                               ; 2323

L6_2325:
        mov bl,[bp-0x2]                                 ; 2325
        sub_ bh,bh                                      ; 2328
        db 0x8A, 0x8F, 0x4E, 0x00                       ; 232A mov cl,[bx+0x4e]

L6_232E:
        sub_ ch,ch                                      ; 232E
        neg cl                                          ; 2330
        mov [bp-0x1],cl                                 ; 2332
        jmp short L6_2368                               ; 2335

L6_2337:
        push word [bp-0x10]                             ; 2337
        push word [bp-0x12]                             ; 233A
        mov ax,0xb9e                                    ; 233D
        mov dx,0x3                                      ; 2340
        push dx                                         ; 2343
        push ax                                         ; 2344
        callf L1_242E, R6_2348, R6_236F                 ; 2345 far seg1
        jmp short L6_2363                               ; 234A

L6_234C:
        cmp byte [bp+0xa],0x1                           ; 234C
        jnz short L6_2357                               ; 2350
        mov ax,0x2                                      ; 2352
        jmp short L6_235A                               ; 2355

L6_2357:
        mov ax,0x1                                      ; 2357

L6_235A:
        mov cl,[bp-0x2]                                 ; 235A
        sub_ ch,ch                                      ; 235D
        add_ ax,cx                                      ; 235F
        sar ax,1                                        ; 2361

L6_2363:
        neg al                                          ; 2363
        mov [bp-0x1],al                                 ; 2365

L6_2368:
        push si                                         ; 2368
        mov al,0xa2                                     ; 2369
        push ax                                         ; 236B
        callf dsp_write, R6_236F, R6_2379               ; 236C far seg1
        push si                                         ; 2371
        mov al,[bp-0x1]                                 ; 2372

L6_2375:
        push ax                                         ; 2375
        callf dsp_write, R6_2379, R6_23EB               ; 2376 far seg1
        mov ax,[si+0x3d]                                ; 237B
        mov [0x8c],ax                                   ; 237E

L6_2381:
        pop si                                          ; 2381
        pop di                                          ; 2382
        mov_ sp,bp                                      ; 2383
        pop bp                                          ; 2385
        retf 0xc                                        ; 2386
        db 0x90                                         ; 2389

L6_238A:
        push bp                                         ; 238A
        mov_ bp,sp                                      ; 238B
        sub sp,byte +0x1e                               ; 238D
        push di                                         ; 2390
        push si                                         ; 2391
        mov di,[bp+0x10]                                ; 2392
        cmp word [di+0x1c],byte +0x0                    ; 2395
        jnz short L6_239E                               ; 2399
        jmp near L6_25DC                                ; 239B

L6_239E:
        mov si,[bp+0xe]                                 ; 239E
        mov ax,[bp+0x6]                                 ; 23A1
        mov dx,[bp+0x8]                                 ; 23A4
        mov [di+0x107],ax                               ; 23A7
        mov [di+0x109],dx                               ; 23AB
        mov cx,[di+0xef]                                ; 23AF
        sub_ bx,bx                                      ; 23B3
        add cx,byte +0x1                                ; 23B5
        adc_ bx,bx                                      ; 23B8
        mov [bp-0xc],cx                                 ; 23BA
        mov [bp-0xa],bx                                 ; 23BD
        shr bx,1                                        ; 23C0
        rcr cx,1                                        ; 23C2
        mov [di+0xf1],cx                                ; 23C4
        and ax,strict word 0x1                          ; 23C8
        cmp ax,strict word 0x1                          ; 23CB
        sbb_ ax,ax                                      ; 23CE
        inc ax                                          ; 23D0
        inc ax                                          ; 23D1
        mul word [bp+0xc]                               ; 23D2
        mov [di+0xf3],ax                                ; 23D5
        sub_ dx,dx                                      ; 23D9
        mov [bp-0x10],si                                ; 23DB
        mov [bp-0xe],dx                                 ; 23DE
        push dx                                         ; 23E1
        push si                                         ; 23E2
        push dx                                         ; 23E3
        push ax                                         ; 23E4
        mov [bp-0x12],cx                                ; 23E5
        callf L1_23E4, R6_23EB, R6_23F2                 ; 23E8 far seg1
        mov cl,0x4                                      ; 23ED
        callf L1_24F8, R6_23F2, R6_2415                 ; 23EF far seg1
        and al,0xfc                                     ; 23F4
        cmp ax,[bp-0x12]                                ; 23F6
        jna short L6_23FE                               ; 23F9
        mov ax,[bp-0x12]                                ; 23FB

L6_23FE:
        mov [di+0xf1],ax                                ; 23FE
        cmp byte [di+0xd4],0x3                          ; 2402
        jna short L6_2436                               ; 2407
        mov word [bp-0x2],0x4                           ; 2409
        sub_ dx,dx                                      ; 240E
        mov cl,0x2                                      ; 2410
        callf L1_2416, R6_2415, R6_242E                 ; 2412 far seg1
        cmp dx,[bp-0xa]                                 ; 2417
        jc short L6_2436                                ; 241A
        ja short L6_2423                                ; 241C
        cmp ax,[bp-0xc]                                 ; 241E
        jna short L6_2436                               ; 2421

L6_2423:
        mov ax,[bp-0xc]                                 ; 2423
        mov dx,[bp-0xa]                                 ; 2426
        mov cl,0x2                                      ; 2429
        callf L1_24F8, R6_242E, R6_2447                 ; 242B far seg1
        and al,0xfc                                     ; 2430
        mov [di+0xf1],ax                                ; 2432

L6_2436:
        push word [bp-0xe]                              ; 2436
        push word [bp-0x10]                             ; 2439
        mov ax,0xb800                                   ; 243C
        mov dx,0xb                                      ; 243F
        push dx                                         ; 2442
        push ax                                         ; 2443
        callf L1_242E, R6_2447, R6_245D                 ; 2444 far seg1
        mov [bp-0x5],al                                 ; 2449
        push word [bp-0xe]                              ; 244C
        push word [bp-0x10]                             ; 244F
        mov ax,0xb800                                   ; 2452
        mov dx,0xb                                      ; 2455
        push dx                                         ; 2458
        push ax                                         ; 2459
        callf L1_248E, R6_245D, R6_2492                 ; 245A far seg1
        mov_ cx,si                                      ; 245F
        shr cx,1                                        ; 2461
        sub_ bx,bx                                      ; 2463
        mov [bp-0x16],cx                                ; 2465
        mov [bp-0x14],bx                                ; 2468
        cmp_ bx,dx                                      ; 246B
        ja short L6_2478                                ; 246D
        jc short L6_2475                                ; 246F
        cmp_ cx,ax                                      ; 2471
        jnc short L6_2478                               ; 2473

L6_2475:
        inc byte [bp-0x5]                               ; 2475

L6_2478:
        mov al,[bp-0x5]                                 ; 2478
        sub_ ah,ah                                      ; 247B
        sub_ dx,dx                                      ; 247D
        push dx                                         ; 247F
        push ax                                         ; 2480
        mov cx,0xb800                                   ; 2481
        mov bx,0xb                                      ; 2484
        push bx                                         ; 2487
        push cx                                         ; 2488
        mov [bp-0x1a],ax                                ; 2489
        mov [bp-0x18],dx                                ; 248C
        callf L1_242E, R6_2492, R6_24A8                 ; 248F far seg1
        mov [bp-0x2],ax                                 ; 2494
        push word [bp-0x18]                             ; 2497
        push word [bp-0x1a]                             ; 249A
        mov ax,0xb800                                   ; 249D
        mov dx,0xb                                      ; 24A0
        push dx                                         ; 24A3
        push ax                                         ; 24A4
        callf L1_248E, R6_24A8, R6_24EF                 ; 24A5 far seg1
        mov cl,[bp-0x5]                                 ; 24AA
        shr cl,1                                        ; 24AD
        sub_ ch,ch                                      ; 24AF
        sub_ bx,bx                                      ; 24B1
        cmp_ bx,dx                                      ; 24B3
        ja short L6_24C0                                ; 24B5
        jc short L6_24BD                                ; 24B7
        cmp_ cx,ax                                      ; 24B9
        jnc short L6_24C0                               ; 24BB

L6_24BD:
        inc word [bp-0x2]                               ; 24BD

L6_24C0:
        cmp [bp-0x2],si                                 ; 24C0
        jna short L6_24CC                               ; 24C3
        mov ax,[bp-0x2]                                 ; 24C5
        sub_ ax,si                                      ; 24C8
        jmp short L6_24D1                               ; 24CA

L6_24CC:
        mov_ ax,si                                      ; 24CC
        sub ax,[bp-0x2]                                 ; 24CE

L6_24D1:
        mov [bp-0x8],ax                                 ; 24D1
        mov al,[bp-0x5]                                 ; 24D4
        neg al                                          ; 24D7
        or al,0x80                                      ; 24D9
        mov [bp-0x5],al                                 ; 24DB
        push word [bp-0xe]                              ; 24DE
        push word [bp-0x10]                             ; 24E1
        mov ax,0x1cc8                                   ; 24E4
        mov dx,0xc                                      ; 24E7
        push dx                                         ; 24EA
        push ax                                         ; 24EB
        callf L1_242E, R6_24EF, R6_2505                 ; 24EC far seg1
        mov [bp-0x1],al                                 ; 24F1
        push word [bp-0xe]                              ; 24F4
        push word [bp-0x10]                             ; 24F7
        mov ax,0x1cc8                                   ; 24FA
        mov dx,0xc                                      ; 24FD
        push dx                                         ; 2500
        push ax                                         ; 2501
        callf L1_248E, R6_2505, R6_2530                 ; 2502 far seg1
        cmp [bp-0x14],dx                                ; 2507
        ja short L6_2516                                ; 250A
        jc short L6_2513                                ; 250C
        cmp [bp-0x16],ax                                ; 250E
        jnc short L6_2516                               ; 2511

L6_2513:
        inc byte [bp-0x1]                               ; 2513

L6_2516:
        mov al,[bp-0x1]                                 ; 2516
        sub_ ah,ah                                      ; 2519
        sub_ dx,dx                                      ; 251B
        push dx                                         ; 251D
        push ax                                         ; 251E
        mov cx,0x1cc8                                   ; 251F
        mov bx,0xc                                      ; 2522
        push bx                                         ; 2525
        push cx                                         ; 2526
        mov [bp-0x1e],ax                                ; 2527
        mov [bp-0x1c],dx                                ; 252A
        callf L1_242E, R6_2530, R6_2546                 ; 252D far seg1
        mov [bp-0x4],ax                                 ; 2532
        push word [bp-0x1c]                             ; 2535
        push word [bp-0x1e]                             ; 2538
        mov ax,0x1cc8                                   ; 253B
        mov dx,0xc                                      ; 253E
        push dx                                         ; 2541
        push ax                                         ; 2542
        callf L1_248E, R6_2546, R6_259C                 ; 2543 far seg1
        mov cl,[bp-0x1]                                 ; 2548
        shr cl,1                                        ; 254B
        sub_ ch,ch                                      ; 254D
        sub_ bx,bx                                      ; 254F
        cmp_ bx,dx                                      ; 2551
        ja short L6_255E                                ; 2553
        jc short L6_255B                                ; 2555
        cmp_ cx,ax                                      ; 2557
        jnc short L6_255E                               ; 2559

L6_255B:
        inc word [bp-0x4]                               ; 255B

L6_255E:
        cmp [bp-0x4],si                                 ; 255E
        jna short L6_256A                               ; 2561
        mov cx,[bp-0x4]                                 ; 2563
        sub_ cx,si                                      ; 2566
        jmp short L6_256F                               ; 2568

L6_256A:
        mov_ cx,si                                      ; 256A
        sub cx,[bp-0x4]                                 ; 256C

L6_256F:
        mov al,[bp-0x1]                                 ; 256F
        neg al                                          ; 2572
        and al,0x7f                                     ; 2574
        mov [bp-0x1],al                                 ; 2576
        cmp [bp-0x8],cx                                 ; 2579
        jna short L6_2581                               ; 257C
        mov [bp-0x5],al                                 ; 257E

L6_2581:
        cmp si,0x16a8                                   ; 2581
        ja short L6_2591                                ; 2585
        cmp si,0xfa0                                    ; 2587
        jna short L6_2591                               ; 258B
        mov byte [bp-0x5],0x0                           ; 258D

L6_2591:
        push di                                         ; 2591
        mov al,0x70                                     ; 2592
        push ax                                         ; 2594
        mov al,[bp-0x5]                                 ; 2595
        push ax                                         ; 2598
        callf mixer_write, R6_259C, R6_25BB             ; 2599 far seg1
        cmp si,0xac44                                   ; 259E
        jz short L6_25C4                                ; 25A2
        cmp si,0xbb80                                   ; 25A4
        jz short L6_25C4                                ; 25A8
        push word [bp-0xe]                              ; 25AA
        push word [bp-0x10]                             ; 25AD
        mov ax,0xb9e                                    ; 25B0
        mov dx,0x3                                      ; 25B3
        push dx                                         ; 25B6
        push ax                                         ; 25B7
        callf L1_242E, R6_25BB, R6_25D3                 ; 25B8 far seg1
        neg al                                          ; 25BD
        mov [bp-0x1],al                                 ; 25BF
        jmp short L6_25C8                               ; 25C2

L6_25C4:
        mov byte [bp-0x1],0xfd                          ; 25C4

L6_25C8:
        push di                                         ; 25C8
        mov al,0x72                                     ; 25C9
        push ax                                         ; 25CB
        mov al,[bp-0x1]                                 ; 25CC
        push ax                                         ; 25CF
        callf mixer_write, R6_25D3, R6_1FA4             ; 25D0 far seg1
        mov ax,[di+0xf1]                                ; 25D5
        mov [0x8e],ax                                   ; 25D9

L6_25DC:
        pop si                                          ; 25DC
        pop di                                          ; 25DD
        mov_ sp,bp                                      ; 25DE
        pop bp                                          ; 25E0
        retf 0xc                                        ; 25E1

L6_25E4:
        push bp                                         ; 25E4
        mov_ bp,sp                                      ; 25E5
        sub sp,byte +0x6                                ; 25E7
        push di                                         ; 25EA
        push si                                         ; 25EB
        mov si,[bp+0xe]                                 ; 25EC
        mov di,[bp+0x6]                                 ; 25EF
        mov es,[bp+0x8]                                 ; 25F2
        sub_ ax,ax                                      ; 25F5
        mov [es:di+0x6],ax                              ; 25F7
        mov [es:di+0x4],ax                              ; 25FB
        mov [es:di+0x2],ax                              ; 25FF
        mov [es:di],ax                                  ; 2603
        cmp word [bp+0xa],byte +0x24                    ; 2606
        jnz short L6_2647                               ; 260A
        cmp [bp+0xc],ax                                 ; 260C
        jnz short L6_2647                               ; 260F
        mov al,[si+0x100]                               ; 2611
        and al,0x3                                      ; 2615
        jnz short L6_261C                               ; 2617
        jmp near L6_26DD                                ; 2619

L6_261C:
        cmp word [si+0xe1],byte +0x0                    ; 261C
        jz short L6_265F                                ; 2621
        mov al,[si+0x100]                               ; 2623
        and al,0x3                                      ; 2627
        dec al                                          ; 2629
        jnz short L6_265F                               ; 262B

L6_262D:
        mov ax,[si+0xcc]                                ; 262D
        mov dx,[si+0xce]                                ; 2631
        mov [es:di],ax                                  ; 2635
        mov [es:di+0x2],dx                              ; 2638
        mov ax,[si+0xd0]                                ; 263C
        mov dx,[si+0xd2]                                ; 2640
        jmp near L6_26D5                                ; 2644

L6_2647:
        mov al,[si+0x5d]                                ; 2647
        and al,0x3                                      ; 264A
        mov [bp-0x6],al                                 ; 264C
        or_ al,al                                       ; 264F
        jz short L6_265F                                ; 2651
        cmp word [si+0x24],byte +0x0                    ; 2653
        jz short L6_265F                                ; 2657
        test byte [si+0x6b],0x3c                        ; 2659
        jz short L6_2661                                ; 265D

L6_265F:
        jmp short L6_26DD                               ; 265F

L6_2661:
        mov ax,[bp+0xa]                                 ; 2661
        mov dx,[bp+0xc]                                 ; 2664
        or_ dx,dx                                       ; 2667
        jnz short L6_2676                               ; 2669
        sub ax,strict word 0x22                         ; 266B
        jz short L6_267A                                ; 266E
        dec ax                                          ; 2670
        jz short L6_2692                                ; 2671
        dec ax                                          ; 2673
        jz short L6_26AA                                ; 2674

L6_2676:
        xor_ ax,ax                                      ; 2676
        jmp short L6_26E0                               ; 2678

L6_267A:
        cmp byte [bp-0x6],0x2                           ; 267A
        jnz short L6_26DD                               ; 267E
        mov ax,[si+0x78]                                ; 2680
        mov dx,[si+0x7a]                                ; 2683
        cmp [si+0x65],ax                                ; 2686
        jnz short L6_26B0                               ; 2689
        cmp [si+0x67],dx                                ; 268B
        jnz short L6_26B0                               ; 268E
        jmp short L6_26DD                               ; 2690

L6_2692:
        cmp byte [bp-0x6],0x2                           ; 2692
        jnz short L6_26DD                               ; 2696
        mov ax,[si+0x78]                                ; 2698
        mov dx,[si+0x7a]                                ; 269B
        cmp [si+0x65],ax                                ; 269E
        jnz short L6_26DD                               ; 26A1
        cmp [si+0x67],dx                                ; 26A3
        jz short L6_26B0                                ; 26A6
        jmp short L6_26DD                               ; 26A8

L6_26AA:
        cmp byte [bp-0x6],0x1                           ; 26AA
        jnz short L6_26DD                               ; 26AE

L6_26B0:
        mov al,[bp-0x6]                                 ; 26B0
        sub_ ah,ah                                      ; 26B3
        dec ax                                          ; 26B5
        jnz short L6_26BB                               ; 26B6
        jmp near L6_262D                                ; 26B8

L6_26BB:
        dec ax                                          ; 26BB
        jnz short L6_26DD                               ; 26BC
        mov ax,[si+0x98]                                ; 26BE
        mov dx,[si+0x9a]                                ; 26C2
        mov [es:di],ax                                  ; 26C6
        mov [es:di+0x2],dx                              ; 26C9
        mov ax,[si+0x9c]                                ; 26CD
        mov dx,[si+0x9e]                                ; 26D1

L6_26D5:
        mov [es:di+0x4],ax                              ; 26D5
        mov [es:di+0x6],dx                              ; 26D9

L6_26DD:
        mov ax,0x1                                      ; 26DD

L6_26E0:
        pop si                                          ; 26E0
        pop di                                          ; 26E1
        mov_ sp,bp                                      ; 26E2
        pop bp                                          ; 26E4
        retf 0xa                                        ; 26E5
        db 0x90, 0x90                                   ; 26E8

L6_26EA:
        push bp                                         ; 26EA
        mov_ bp,sp                                      ; 26EB
        sub sp,byte +0x12                               ; 26ED
        push si                                         ; 26F0
        mov bx,[bp+0x6]                                 ; 26F1
        cmp word [bx+0x1c],byte +0x0                    ; 26F4
        jz short L6_2700                                ; 26F8
        cmp word [bx+0x74],byte +0x0                    ; 26FA
        jz short L6_2703                                ; 26FE

L6_2700:
        jmp near L6_2AAD                                ; 2700

L6_2703:
        mov ax,[0x8c]                                   ; 2703
        mov [bx+0x3d],ax                                ; 2706
        cmp word [0x86],byte +0x0                       ; 2709
        jnz short L6_2718                               ; 270E
        cmp word [0x84],0x200                           ; 2710
        jc short L6_2739                                ; 2716

L6_2718:
        sub_ dx,dx                                      ; 2718
        cmp dx,[0x86]                                   ; 271A
        jc short L6_2739                                ; 271E
        ja short L6_2728                                ; 2720
        cmp ax,[0x84]                                   ; 2722
        jna short L6_2739                               ; 2726

L6_2728:
        shr ax,1                                        ; 2728
        cmp ax,[0x84]                                   ; 272A
        jnz short L6_2739                               ; 272E
        cmp dx,[0x86]                                   ; 2730
        jnz short L6_2739                               ; 2734
        mov [bx+0x3d],ax                                ; 2736

L6_2739:
        mov bx,[bp+0x6]                                 ; 2739
        mov ax,0x1                                      ; 273C
        mov [bx+0x74],ax                                ; 273F
        mov [bx+0x24],ax                                ; 2742
        and byte [bx+0x2a],0xef                         ; 2745
        test byte [bx+0x2a],0x8                         ; 2749
        jnz short L6_275A                               ; 274D
        mov ax,[bx+0x2a]                                ; 274F
        mov dx,[bx+0x2c]                                ; 2752
        or al,0x10                                      ; 2755
        mov [bx+0x2a],ax                                ; 2757

L6_275A:
        mov word [bx+0x39],0x0                          ; 275A
        mov word [bx+0x22],0x0                          ; 275F
        mov ax,[0xa6]                                   ; 2764
        inc word [0xa6]                                 ; 2767
        or_ ax,ax                                       ; 276B
        jnz short L6_2770                               ; 276D
        cli                                             ; 276F

L6_2770:
        mov bx,[bp+0x6]                                 ; 2770
        test byte [bx+0x2b],0x8                         ; 2773
        jz short L6_2799                                ; 2777
        mov al,[bx+0x37]                                ; 2779
        and al,0xfc                                     ; 277C
        mov [bp-0x2],al                                 ; 277E
        mov cx,[bx+0x3d]                                ; 2781
        add_ cx,cx                                      ; 2784
        mov [bp-0x4],cx                                 ; 2786
        push word [bx+0x16]                             ; 2789
        push word [bx+0x14]                             ; 278C
        push cx                                         ; 278F
        push ax                                         ; 2790
        callf vxd_pio_buffer, R6_2794, R6_2886          ; 2791 far seg1
        jmp near L6_285E                                ; 2796

L6_2799:
        cmp byte [bx+0x6],0x3                           ; 2799
        ja short L6_27DF                                ; 279D
        mov al,[bx+0x36]                                ; 279F
        sub_ ah,ah                                      ; 27A2
        mov dl,[bx+0x31]                                ; 27A4
        sub_ dh,dh                                      ; 27A7
        out dx,al                                       ; 27A9
        xor_ ax,ax                                      ; 27AA
        mov dl,[bx+0x33]                                ; 27AC
        out dx,al                                       ; 27AF
        mov ch,[bx+0x2b]                                ; 27B0
        and cx,0x8000                                   ; 27B3
        cmp_ cx,ax                                      ; 27B7
        jz short L6_27C3                                ; 27B9
        mov al,[bx+0x37]                                ; 27BB
        and ax,strict word 0x3f                         ; 27BE
        jmp short L6_27C8                               ; 27C1

L6_27C3:
        sub_ ah,ah                                      ; 27C3
        mov al,[bx+0x37]                                ; 27C5

L6_27C8:
        mov dl,[bx+0x32]                                ; 27C8
        sub_ dh,dh                                      ; 27CB
        out dx,al                                       ; 27CD
        mov ax,[bx+0x45]                                ; 27CE
        mov dl,[bx+0x2f]                                ; 27D1
        out dx,al                                       ; 27D4
        mov_ al,ah                                      ; 27D5
        sub_ ah,ah                                      ; 27D7
        out dx,al                                       ; 27D9
        mov al,[bx+0x47]                                ; 27DA
        jmp short L6_283F                               ; 27DD

L6_27DF:
        sub_ ah,ah                                      ; 27DF
        mov al,[bx+0x36]                                ; 27E1
        mov dl,[bx+0x31]                                ; 27E4
        sub_ dh,dh                                      ; 27E7
        out dx,al                                       ; 27E9
        xor_ ax,ax                                      ; 27EA
        mov dl,[bx+0x33]                                ; 27EC
        out dx,al                                       ; 27EF
        mov ch,[bx+0x2b]                                ; 27F0
        and cx,0x8000                                   ; 27F3
        cmp_ cx,ax                                      ; 27F7
        jz short L6_2803                                ; 27F9
        mov al,[bx+0x37]                                ; 27FB
        and ax,strict word 0x3f                         ; 27FE
        jmp short L6_2808                               ; 2801

L6_2803:
        sub_ ah,ah                                      ; 2803
        mov al,[bx+0x37]                                ; 2805

L6_2808:
        mov dl,[bx+0x32]                                ; 2808
        sub_ dh,dh                                      ; 280B
        out dx,al                                       ; 280D
        mov ax,[bx+0x45]                                ; 280E
        mov_ cx,ax                                      ; 2811
        shr ax,1                                        ; 2813
        sub_ ah,ah                                      ; 2815
        mov dl,[bx+0x2f]                                ; 2817
        out dx,al                                       ; 281A
        mov al,0x9                                      ; 281B
        xchg ax,cx                                      ; 281D
        shr ax,cl                                       ; 281E
        mov [bp-0x6],al                                 ; 2820
        test byte [bx+0x47],0x1                         ; 2823
        jz short L6_282E                                ; 2827
        or al,0x80                                      ; 2829
        mov [bp-0x6],al                                 ; 282B

L6_282E:
        mov al,[bp-0x6]                                 ; 282E
        sub_ ah,ah                                      ; 2831
        mov dl,[bx+0x2f]                                ; 2833
        sub_ dh,dh                                      ; 2836
        out dx,al                                       ; 2838
        mov al,[bx+0x47]                                ; 2839
        and ax,0xfe                                     ; 283C

L6_283F:
        mov dl,[bx+0x34]                                ; 283F
        sub_ dh,dh                                      ; 2842
        out dx,al                                       ; 2844
        mov ax,[bx+0x3d]                                ; 2845
        add_ ax,ax                                      ; 2848
        dec ax                                          ; 284A
        mov [bp-0x4],ax                                 ; 284B
        mov dl,[bx+0x30]                                ; 284E
        out dx,al                                       ; 2851
        mov_ al,ah                                      ; 2852
        sub_ ah,ah                                      ; 2854
        out dx,al                                       ; 2856
        mov al,[bx+0x35]                                ; 2857
        mov dl,[bx+0x31]                                ; 285A
        out dx,al                                       ; 285D

L6_285E:
        dec word [0xa6]                                 ; 285E
        jnz short L6_2865                               ; 2862
        sti                                             ; 2864

L6_2865:
        push word [bp+0x6]                              ; 2865
        callf L5_35AC, R6_286B, R6_2873                 ; 2868 far seg5
        push word [bp+0x6]                              ; 286D
        callf L5_365B, R6_2873, R6_1E48                 ; 2870 far seg5
        mov bx,[bp+0x6]                                 ; 2875
        test byte [bx+0x6b],0x3c                        ; 2878
        jz short L6_28A4                                ; 287C
        push bx                                         ; 287E
        mov al,[bx+0x71]                                ; 287F
        push ax                                         ; 2882
        callf dsp_write, R6_2886, R6_2895               ; 2883 far seg1
        mov bx,[bp+0x6]                                 ; 2888
        push bx                                         ; 288B
        mov al,[bx+0x3d]                                ; 288C
        dec al                                          ; 288F
        push ax                                         ; 2891
        callf dsp_write, R6_2895, R6_28C2               ; 2892 far seg1
        mov bx,[bp+0x6]                                 ; 2897
        push bx                                         ; 289A
        mov ax,[bx+0x3d]                                ; 289B
        dec ax                                          ; 289E
        mov_ al,ah                                      ; 289F
        jmp near L6_2A08                                ; 28A1

L6_28A4:
        mov ax,0x2                                      ; 28A4
        mov dx,[bx]                                     ; 28A7
        add dx,byte +0x6                                ; 28A9
        out dx,al                                       ; 28AC
        xor_ ax,ax                                      ; 28AD
        out dx,al                                       ; 28AF
        mov ch,[bx+0x2b]                                ; 28B0
        and cx,0x8000                                   ; 28B3
        cmp_ cx,ax                                      ; 28B7
        jz short L6_28CF                                ; 28B9
        push bx                                         ; 28BB
        mov al,0xb9                                     ; 28BC
        push ax                                         ; 28BE
        callf dsp_write, R6_28C2, R6_28CD               ; 28BF far seg1
        push word [bp+0x6]                              ; 28C4
        mov al,0x2                                      ; 28C7
        push ax                                         ; 28C9
        callf dsp_write, R6_28CD, R6_28E1               ; 28CA far seg1

L6_28CF:
        mov bx,[bp+0x6]                                 ; 28CF
        mov ax,[bx+0x3d]                                ; 28D2
        neg ax                                          ; 28D5
        mov [bp-0x6],ax                                 ; 28D7
        push bx                                         ; 28DA
        mov al,0xa4                                     ; 28DB
        push ax                                         ; 28DD
        callf dsp_write, R6_28E1, R6_28ED               ; 28DE far seg1
        push word [bp+0x6]                              ; 28E3
        mov al,[bp-0x6]                                 ; 28E6
        push ax                                         ; 28E9
        callf dsp_write, R6_28ED, R6_28F8               ; 28EA far seg1
        push word [bp+0x6]                              ; 28EF
        mov al,0xa5                                     ; 28F2
        push ax                                         ; 28F4
        callf dsp_write, R6_28F8, R6_2904               ; 28F5 far seg1
        push word [bp+0x6]                              ; 28FA
        mov al,[bp-0x5]                                 ; 28FD
        push ax                                         ; 2900
        callf dsp_write, R6_2904, R6_2920               ; 2901 far seg1
        mov bx,[bp+0x6]                                 ; 2906
        test byte [bx+0x6b],0x2                         ; 2909
        jz short L6_2915                                ; 290D
        mov byte [bp-0x2],0xf5                          ; 290F
        jmp short L6_2919                               ; 2913

L6_2915:
        mov byte [bp-0x2],0xf6                          ; 2915

L6_2919:
        push bx                                         ; 2919
        mov al,0xa8                                     ; 291A
        push ax                                         ; 291C
        callf dsp_write, R6_2920, R6_292C               ; 291D far seg1
        push word [bp+0x6]                              ; 2922
        mov al,[bp-0x2]                                 ; 2925
        push ax                                         ; 2928
        callf dsp_write, R6_292C, R6_2937               ; 2929 far seg1
        push word [bp+0x6]                              ; 292E
        mov al,0xc0                                     ; 2931
        push ax                                         ; 2933
        callf dsp_write, R6_2937, R6_2942               ; 2934 far seg1
        push word [bp+0x6]                              ; 2939
        mov al,0xb1                                     ; 293C
        push ax                                         ; 293E
        callf dsp_write, R6_2942, R6_294A               ; 293F far seg1
        push word [bp+0x6]                              ; 2944
        callf dsp_read, R6_294A, R6_295A                ; 2947 far seg1
        or al,0x50                                      ; 294C
        mov [bp-0x2],al                                 ; 294E
        push word [bp+0x6]                              ; 2951
        mov al,0xb1                                     ; 2954
        push ax                                         ; 2956
        callf dsp_write, R6_295A, R6_2966               ; 2957 far seg1
        push word [bp+0x6]                              ; 295C
        mov al,[bp-0x2]                                 ; 295F
        push ax                                         ; 2962
        callf dsp_write, R6_2966, R6_2971               ; 2963 far seg1
        push word [bp+0x6]                              ; 2968
        mov al,0xc0                                     ; 296B
        push ax                                         ; 296D
        callf dsp_write, R6_2971, R6_297C               ; 296E far seg1
        push word [bp+0x6]                              ; 2973
        mov al,0xb2                                     ; 2976
        push ax                                         ; 2978
        callf dsp_write, R6_297C, R6_2984               ; 2979 far seg1
        push word [bp+0x6]                              ; 297E
        callf dsp_read, R6_2984, R6_2994                ; 2981 far seg1
        or al,0x50                                      ; 2986
        mov [bp-0x2],al                                 ; 2988
        push word [bp+0x6]                              ; 298B
        mov al,0xb2                                     ; 298E
        push ax                                         ; 2990
        callf dsp_write, R6_2994, R6_29A0               ; 2991 far seg1
        push word [bp+0x6]                              ; 2996
        mov al,[bp-0x2]                                 ; 2999
        push ax                                         ; 299C
        callf dsp_write, R6_29A0, R6_29C4               ; 299D far seg1
        mov byte [bp-0x2],0x90                          ; 29A2
        mov bx,[bp+0x6]                                 ; 29A6
        test byte [bx+0x6b],0x2                         ; 29A9
        jz short L6_29B3                                ; 29AD
        mov byte [bp-0x2],0x98                          ; 29AF

L6_29B3:
        test byte [bx+0x6b],0x1                         ; 29B3
        jz short L6_29BD                                ; 29B7
        or byte [bp-0x2],0x24                           ; 29B9

L6_29BD:
        push bx                                         ; 29BD
        mov al,0xb7                                     ; 29BE
        push ax                                         ; 29C0
        callf dsp_write, R6_29C4, R6_29D0               ; 29C1 far seg1
        push word [bp+0x6]                              ; 29C6
        mov al,[bp-0x2]                                 ; 29C9
        push ax                                         ; 29CC
        callf dsp_write, R6_29D0, R6_29DB               ; 29CD far seg1
        push word [bp+0x6]                              ; 29D2
        mov al,0xc0                                     ; 29D5
        push ax                                         ; 29D7
        callf dsp_write, R6_29DB, R6_29E6               ; 29D8 far seg1
        push word [bp+0x6]                              ; 29DD
        mov al,0xb8                                     ; 29E0
        push ax                                         ; 29E2
        callf dsp_write, R6_29E6, R6_29EE               ; 29E3 far seg1
        push word [bp+0x6]                              ; 29E8
        callf dsp_read, R6_29EE, R6_2A00                ; 29EB far seg1
        and al,0x30                                     ; 29F0
        or al,0xf                                       ; 29F2
        mov [bp-0x2],al                                 ; 29F4
        push word [bp+0x6]                              ; 29F7
        mov al,0xb8                                     ; 29FA
        push ax                                         ; 29FC
        callf dsp_write, R6_2A00, R6_2A0C               ; 29FD far seg1
        push word [bp+0x6]                              ; 2A02
        mov al,[bp-0x2]                                 ; 2A05

L6_2A08:
        push ax                                         ; 2A08
        callf dsp_write, R6_2A0C, R6_22E5               ; 2A09 far seg1
        callp R6_2A0F, R6_2A1A, 0x0000                  ; 2A0E MMSYSTEM.timeGetTime
        mov [bp-0xa],ax                                 ; 2A13
        mov [bp-0x8],dx                                 ; 2A16

L6_2A19:
        callp R6_2A1A, 0xFFFF, 0x0000                   ; 2A19 MMSYSTEM.timeGetTime
        sub ax,[bp-0xa]                                 ; 2A1E
        sbb dx,[bp-0x8]                                 ; 2A21
        or_ dx,dx                                       ; 2A24
        jnz short L6_2A2D                               ; 2A26
        cmp ax,strict word 0x64                         ; 2A28
        jc short L6_2A19                                ; 2A2B

L6_2A2D:
        mov bx,[bp+0x6]                                 ; 2A2D
        mov ax,[bx+0x26]                                ; 2A30
        mov dx,[bx+0x28]                                ; 2A33
        mov [bp-0x12],ax                                ; 2A36
        mov [bp-0x10],dx                                ; 2A39
        push bx                                         ; 2A3C
        mov al,0xc0                                     ; 2A3D
        push ax                                         ; 2A3F
        callf dsp_write, R6_2A43, R6_2A4E               ; 2A40 far seg1
        push word [bp+0x6]                              ; 2A45
        mov al,0xa8                                     ; 2A48
        push ax                                         ; 2A4A
        callf dsp_write, R6_2A4E, R6_2A56               ; 2A4B far seg1
        push word [bp+0x6]                              ; 2A50
        callf dsp_read, R6_2A56, R6_2A9F                ; 2A53 far seg1
        and al,0xf7                                     ; 2A58
        mov [bp-0x2],al                                 ; 2A5A
        mov bx,[bp+0x6]                                 ; 2A5D
        mov ax,[bx+0x78]                                ; 2A60
        mov dx,[bx+0x7a]                                ; 2A63
        cmp [bx+0x65],ax                                ; 2A66
        jnz short L6_2A70                               ; 2A69
        cmp [bx+0x67],dx                                ; 2A6B
        jz short L6_2A77                                ; 2A6E

L6_2A70:
        mov word [bp-0xe],0x25                          ; 2A70
        jmp short L6_2A7C                               ; 2A75

L6_2A77:
        mov word [bp-0xe],0x26                          ; 2A77

L6_2A7C:
        mov cl,0x3                                      ; 2A7C
        mov si,[bp-0xe]                                 ; 2A7E
        shl si,cl                                       ; 2A81
        les bx,[bp-0x12]                                ; 2A83
        mov ax,[es:bx+si+0x280e]                        ; 2A86
        or ax,[es:bx+si+0x280c]                         ; 2A8B
        jz short L6_2A96                                ; 2A90
        or byte [bp-0x2],0x8                            ; 2A92

L6_2A96:
        push word [bp+0x6]                              ; 2A96
        mov al,0xa8                                     ; 2A99
        push ax                                         ; 2A9B
        callf dsp_write, R6_2A9F, R6_2AAB               ; 2A9C far seg1
        push word [bp+0x6]                              ; 2AA1
        mov al,[bp-0x2]                                 ; 2AA4
        push ax                                         ; 2AA7
        callf dsp_write, R6_2AAB, R6_2ACD               ; 2AA8 far seg1

L6_2AAD:
        pop si                                          ; 2AAD
        mov_ sp,bp                                      ; 2AAE
        pop bp                                          ; 2AB0
        retf 0x2                                        ; 2AB1
        db 0x90, 0x90                                   ; 2AB4

L6_2AB6:
        push bp                                         ; 2AB6
        mov_ bp,sp                                      ; 2AB7
        push si                                         ; 2AB9
        mov si,[bp+0x6]                                 ; 2ABA
        cmp word [si+0x1c],byte +0x0                    ; 2ABD
        jz short L6_2ADA                                ; 2AC1
        cmp word [si+0x74],byte +0x0                    ; 2AC3
        jz short L6_2ADA                                ; 2AC7
        push si                                         ; 2AC9
        callf L1_1602, R6_2ACD, R6_2BB2                 ; 2ACA far seg1
        push si                                         ; 2ACF
        callf L6_005C, R6_2AD3, R6_19ED                 ; 2AD0 far seg6
        mov word [si+0x74],0x0                          ; 2AD5

L6_2ADA:
        pop si                                          ; 2ADA
        mov_ sp,bp                                      ; 2ADB
        pop bp                                          ; 2ADD
        retf 0x2                                        ; 2ADE
        db 0x90                                         ; 2AE1

L6_2AE2:
        push bp                                         ; 2AE2
        mov_ bp,sp                                      ; 2AE3
        sub sp,byte +0x6                                ; 2AE5
        mov bx,[bp+0xe]                                 ; 2AE8
        cmp word [bx+0x1c],byte +0x0                    ; 2AEB
        jnz short L6_2AFA                               ; 2AEF

L6_2AF1:
        mov ax,[bp+0xa]                                 ; 2AF1
        mov dx,[bp+0xc]                                 ; 2AF4
        jmp near L6_2B90                                ; 2AF7

L6_2AFA:
        les bx,[bp+0x6]                                 ; 2AFA
        sub_ ax,ax                                      ; 2AFD
        mov [es:bx+0x1a],ax                             ; 2AFF
        mov [es:bx+0x18],ax                             ; 2B03
        mov ax,[es:bx+0x4]                              ; 2B07
        mov dx,[es:bx+0x6]                              ; 2B0B
        mov bx,[es:bx+0x1c]                             ; 2B0F
        add [bx+0x10],ax                                ; 2B13
        adc [bx+0x12],dx                                ; 2B16
        mov ax,[0xa6]                                   ; 2B19
        inc word [0xa6]                                 ; 2B1C
        or_ ax,ax                                       ; 2B20
        jnz short L6_2B25                               ; 2B22
        cli                                             ; 2B24

L6_2B25:
        mov ax,[bp+0xc]                                 ; 2B25
        or ax,[bp+0xa]                                  ; 2B28
        jnz short L6_2B5D                               ; 2B2B
        les bx,[bp+0x6]                                 ; 2B2D
        mov [bp+0xa],bx                                 ; 2B30
        mov [bp+0xc],es                                 ; 2B33
        mov ax,[es:bx+0x4]                              ; 2B36
        mov dx,[es:bx+0x6]                              ; 2B3A
        and al,0xfe                                     ; 2B3E
        mov [0x84],ax                                   ; 2B40
        mov [0x86],dx                                   ; 2B43
        mov bx,[bp+0xe]                                 ; 2B47
        sub_ ax,ax                                      ; 2B4A
        mov [bx+0x92],ax                                ; 2B4C
        mov [bx+0x90],ax                                ; 2B50

L6_2B54:
        dec word [0xa6]                                 ; 2B54
        jnz short L6_2AF1                               ; 2B58
        sti                                             ; 2B5A
        jmp short L6_2AF1                               ; 2B5B

L6_2B5D:
        mov ax,[bp+0xa]                                 ; 2B5D
        mov dx,[bp+0xc]                                 ; 2B60
        jmp short L6_2B6D                               ; 2B63

L6_2B65:
        mov ax,[es:bx+0x18]                             ; 2B65
        mov dx,[es:bx+0x1a]                             ; 2B69

L6_2B6D:
        mov [bp-0x4],ax                                 ; 2B6D
        mov [bp-0x2],dx                                 ; 2B70
        les bx,[bp-0x4]                                 ; 2B73
        mov ax,[es:bx+0x1a]                             ; 2B76
        or ax,[es:bx+0x18]                              ; 2B7A
        jnz short L6_2B65                               ; 2B7E
        mov ax,[bp+0x6]                                 ; 2B80
        mov dx,[bp+0x8]                                 ; 2B83
        mov [es:bx+0x18],ax                             ; 2B86
        mov [es:bx+0x1a],dx                             ; 2B8A
        jmp short L6_2B54                               ; 2B8E

L6_2B90:
        mov_ sp,bp                                      ; 2B90
        pop bp                                          ; 2B92
        retf 0xa                                        ; 2B93

L6_2B96:
        push bp                                         ; 2B96
        mov_ bp,sp                                      ; 2B97
        push di                                         ; 2B99
        push si                                         ; 2B9A
        mov si,[bp+0x4]                                 ; 2B9B
        push si                                         ; 2B9E
        push word [si+0xf7]                             ; 2B9F
        push word [si+0xf5]                             ; 2BA3
        push word [si+0xf1]                             ; 2BA7
        mov ax,0x1                                      ; 2BAB
        push ax                                         ; 2BAE
        callf L1_07B2, R6_2BB2, R6_2BD0                 ; 2BAF far seg1
        mov_ di,ax                                      ; 2BB4
        push si                                         ; 2BB6
        mov ax,[si+0xf5]                                ; 2BB7
        mov dx,[si+0xf7]                                ; 2BBB
        add ax,[si+0xf1]                                ; 2BBF
        push dx                                         ; 2BC3
        push ax                                         ; 2BC4
        push word [si+0xf1]                             ; 2BC5
        mov ax,0x1                                      ; 2BC9
        push ax                                         ; 2BCC
        callf L1_07B2, R6_2BD0, R6_2D70                 ; 2BCD far seg1
        add_ di,ax                                      ; 2BD2
        cmp [si+0xf1],di                                ; 2BD4
        jnc short L6_2BE0                               ; 2BD8
        mov word [si+0xed],0x0                          ; 2BDA

L6_2BE0:
        mov_ ax,di                                      ; 2BE0
        pop si                                          ; 2BE2
        pop di                                          ; 2BE3
        mov_ sp,bp                                      ; 2BE4
        pop bp                                          ; 2BE6
        ret                                             ; 2BE7

L6_2BE8:
        push bp                                         ; 2BE8
        mov_ bp,sp                                      ; 2BE9
        sub sp,byte +0x6                                ; 2BEB
        mov bx,[bp+0x6]                                 ; 2BEE
        cmp word [bx+0x1c],byte +0x0                    ; 2BF1
        jnz short L6_2BFA                               ; 2BF5
        jmp near L6_2E11                                ; 2BF7

L6_2BFA:
        mov word [bx+0xed],0x1                          ; 2BFA
        mov word [bx+0xdf],0x0                          ; 2C00
        push bx                                         ; 2C06
        call L6_2B96                                    ; 2C07
        pop bx                                          ; 2C0A
        or_ ax,ax                                       ; 2C0B
        jnz short L6_2C1A                               ; 2C0D
        mov bx,[bp+0x6]                                 ; 2C0F
        cmp [bx+0x1e],ax                                ; 2C12
        jnz short L6_2C1A                               ; 2C15
        jmp near L6_2E11                                ; 2C17

L6_2C1A:
        mov bx,[bp+0x6]                                 ; 2C1A
        mov word [bx+0xe1],0x1                          ; 2C1D
        and byte [bx+0x2c],0xfe                         ; 2C23
        mov ax,[0xa6]                                   ; 2C27
        inc word [0xa6]                                 ; 2C2A
        or_ ax,ax                                       ; 2C2E
        jnz short L6_2C33                               ; 2C30
        cli                                             ; 2C32

L6_2C33:
        mov bx,[bp+0x6]                                 ; 2C33
        mov ax,[0x8e]                                   ; 2C36
        mov [bx+0xf1],ax                                ; 2C39
        cmp word [0x8a],byte +0x0                       ; 2C3D
        jnz short L6_2C4C                               ; 2C42
        cmp word [0x88],0x200                           ; 2C44
        jc short L6_2C6E                                ; 2C4A

L6_2C4C:
        sub_ dx,dx                                      ; 2C4C
        cmp dx,[0x8a]                                   ; 2C4E
        jc short L6_2C6E                                ; 2C52
        ja short L6_2C5C                                ; 2C54
        cmp ax,[0x88]                                   ; 2C56
        jna short L6_2C6E                               ; 2C5A

L6_2C5C:
        shr ax,1                                        ; 2C5C
        cmp ax,[0x88]                                   ; 2C5E
        jnz short L6_2C6E                               ; 2C62
        cmp dx,[0x8a]                                   ; 2C64
        jnz short L6_2C6E                               ; 2C68
        mov [bx+0xf1],ax                                ; 2C6A

L6_2C6E:
        mov bx,[bp+0x6]                                 ; 2C6E
        cmp byte [bx+0xd4],0x3                          ; 2C71
        ja short L6_2CC1                                ; 2C76
        mov al,[bx+0xea]                                ; 2C78
        sub_ ah,ah                                      ; 2C7C
        mov dl,[bx+0xe5]                                ; 2C7E
        sub_ dh,dh                                      ; 2C82
        out dx,al                                       ; 2C84
        xor_ ax,ax                                      ; 2C85
        mov dl,[bx+0xe7]                                ; 2C87
        out dx,al                                       ; 2C8B
        mov ch,[bx+0x2b]                                ; 2C8C
        and cx,0x8000                                   ; 2C8F
        cmp_ cx,ax                                      ; 2C93
        jz short L6_2CA0                                ; 2C95
        mov al,[bx+0xec]                                ; 2C97
        and ax,strict word 0x3f                         ; 2C9B
        jmp short L6_2CA6                               ; 2C9E

L6_2CA0:
        sub_ ah,ah                                      ; 2CA0
        mov al,[bx+0xec]                                ; 2CA2

L6_2CA6:
        mov dl,[bx+0xe6]                                ; 2CA6
        sub_ dh,dh                                      ; 2CAA
        out dx,al                                       ; 2CAC
        mov ax,[bx+0xf9]                                ; 2CAD
        mov dl,[bx+0xe3]                                ; 2CB1
        out dx,al                                       ; 2CB5
        mov_ al,ah                                      ; 2CB6
        sub_ ah,ah                                      ; 2CB8
        out dx,al                                       ; 2CBA
        mov al,[bx+0xfb]                                ; 2CBB
        jmp short L6_2D2C                               ; 2CBF

L6_2CC1:
        sub_ ah,ah                                      ; 2CC1
        mov al,[bx+0xea]                                ; 2CC3
        mov dl,[bx+0xe5]                                ; 2CC7
        sub_ dh,dh                                      ; 2CCB
        out dx,al                                       ; 2CCD
        xor_ ax,ax                                      ; 2CCE
        mov dl,[bx+0xe7]                                ; 2CD0
        out dx,al                                       ; 2CD4
        mov ch,[bx+0x2b]                                ; 2CD5
        and cx,0x8000                                   ; 2CD8
        cmp_ cx,ax                                      ; 2CDC
        jz short L6_2CE9                                ; 2CDE
        mov al,[bx+0xec]                                ; 2CE0
        and ax,strict word 0x3f                         ; 2CE4
        jmp short L6_2CEF                               ; 2CE7

L6_2CE9:
        sub_ ah,ah                                      ; 2CE9
        mov al,[bx+0xec]                                ; 2CEB

L6_2CEF:
        mov dl,[bx+0xe6]                                ; 2CEF
        sub_ dh,dh                                      ; 2CF3
        out dx,al                                       ; 2CF5
        mov ax,[bx+0xf9]                                ; 2CF6
        mov_ cx,ax                                      ; 2CFA
        shr ax,1                                        ; 2CFC
        sub_ ah,ah                                      ; 2CFE
        mov dl,[bx+0xe3]                                ; 2D00
        out dx,al                                       ; 2D04
        mov al,0x9                                      ; 2D05
        xchg ax,cx                                      ; 2D07
        shr ax,cl                                       ; 2D08
        mov [bp-0x6],al                                 ; 2D0A
        test byte [bx+0xfb],0x1                         ; 2D0D
        jz short L6_2D19                                ; 2D12
        or al,0x80                                      ; 2D14
        mov [bp-0x6],al                                 ; 2D16

L6_2D19:
        mov al,[bp-0x6]                                 ; 2D19
        sub_ ah,ah                                      ; 2D1C
        mov dl,[bx+0xe3]                                ; 2D1E
        sub_ dh,dh                                      ; 2D22
        out dx,al                                       ; 2D24
        mov al,[bx+0xfb]                                ; 2D25
        and ax,0xfe                                     ; 2D29

L6_2D2C:
        mov dl,[bx+0xe8]                                ; 2D2C
        sub_ dh,dh                                      ; 2D30
        out dx,al                                       ; 2D32
        mov ax,[bx+0xf1]                                ; 2D33
        add_ ax,ax                                      ; 2D37
        dec ax                                          ; 2D39
        mov [bp-0x4],ax                                 ; 2D3A
        mov dl,[bx+0xe4]                                ; 2D3D
        out dx,al                                       ; 2D41
        mov_ al,ah                                      ; 2D42
        sub_ ah,ah                                      ; 2D44
        out dx,al                                       ; 2D46
        mov al,[bx+0xe9]                                ; 2D47
        mov dl,[bx+0xe5]                                ; 2D4B
        out dx,al                                       ; 2D4F
        dec word [0xa6]                                 ; 2D50
        jnz short L6_2D57                               ; 2D54
        sti                                             ; 2D56

L6_2D57:
        push word [bp+0x6]                              ; 2D57
        callf sync_hw_volume, R6_2D5D, R6_286B          ; 2D5A far seg5
        push word [bp+0x6]                              ; 2D5F
        mov al,0x7c                                     ; 2D62
        push ax                                         ; 2D64
        mov bx,[bp+0x6]                                 ; 2D65
        mov al,[bx+0x124]                               ; 2D68
        push ax                                         ; 2D6C
        callf mixer_write, R6_2D70, R6_2D7B             ; 2D6D far seg1
        push word [bp+0x6]                              ; 2D72
        mov al,0xd3                                     ; 2D75
        push ax                                         ; 2D77
        callf dsp_write, R6_2D7B, R6_2D9B               ; 2D78 far seg1
        mov bx,[bp+0x6]                                 ; 2D7D
        cmp word [bx+0xaa],byte +0x0                    ; 2D80
        jz short L6_2D8A                                ; 2D85
        jmp near L6_2E11                                ; 2D87

L6_2D8A:
        push bx                                         ; 2D8A
        mov al,0x74                                     ; 2D8B
        push ax                                         ; 2D8D
        mov ax,[bx+0xf1]                                ; 2D8E
        neg ax                                          ; 2D92
        mov [bp-0x6],ax                                 ; 2D94
        push ax                                         ; 2D97
        callf mixer_write, R6_2D9B, R6_2DAA             ; 2D98 far seg1
        push word [bp+0x6]                              ; 2D9D
        mov al,0x76                                     ; 2DA0
        push ax                                         ; 2DA2
        mov al,[bp-0x5]                                 ; 2DA3
        push ax                                         ; 2DA6
        callf mixer_write, R6_2DAA, R6_2DDA             ; 2DA7 far seg1
        mov bx,[bp+0x6]                                 ; 2DAC
        test byte [bx+0x107],0x1                        ; 2DAF
        jz short L6_2DBC                                ; 2DB4
        mov byte [bp-0x2],0x5                           ; 2DB6
        jmp short L6_2DC0                               ; 2DBA

L6_2DBC:
        mov byte [bp-0x2],0x0                           ; 2DBC

L6_2DC0:
        test byte [bx+0x107],0x2                        ; 2DC0
        jz short L6_2DCB                                ; 2DC5
        or byte [bp-0x2],0x2                            ; 2DC7

L6_2DCB:
        push bx                                         ; 2DCB
        mov al,0x7a                                     ; 2DCC
        push ax                                         ; 2DCE
        or byte [bp-0x2],0x40                           ; 2DCF
        mov al,[bp-0x2]                                 ; 2DD3
        push ax                                         ; 2DD6
        callf mixer_write, R6_2DDA, R6_2DE9             ; 2DD7 far seg1
        push word [bp+0x6]                              ; 2DDC
        mov al,0x71                                     ; 2DDF
        push ax                                         ; 2DE1
        push word [bp+0x6]                              ; 2DE2
        push ax                                         ; 2DE5
        callf mixer_read, R6_2DE9, R6_2794              ; 2DE6 far seg1
%if ES1869_FIX
        and al,0xEF                     ; essreg: no 4x oversampling
%else
        or al,0x12                                      ; 2DEB
%endif
        push ax                                         ; 2DED
        callf mixer_write, R6_2DF1, R6_2E0F             ; 2DEE far seg1
        mov byte [bp-0x2],0x13                          ; 2DF3
        mov bx,[bp+0x6]                                 ; 2DF7
        test byte [bx+0x2b],0x80                        ; 2DFA
        jz short L6_2E04                                ; 2DFE
        mov byte [bp-0x2],0x93                          ; 2E00

L6_2E04:
        push bx                                         ; 2E04
        mov al,0x78                                     ; 2E05
        push ax                                         ; 2E07
        mov al,[bp-0x2]                                 ; 2E08
        push ax                                         ; 2E0B
        callf mixer_write, R6_2E0F, R6_2E6C             ; 2E0C far seg1

L6_2E11:
        mov_ sp,bp                                      ; 2E11
        pop bp                                          ; 2E13
        retf 0x2                                        ; 2E14
        db 0x90                                         ; 2E17

L6_2E18:
        push bp                                         ; 2E18
        mov_ bp,sp                                      ; 2E19
        push si                                         ; 2E1B
        mov si,[bp+0x6]                                 ; 2E1C
        cmp word [si+0x1c],byte +0x0                    ; 2E1F
        jz short L6_2E37                                ; 2E23
        cmp word [si+0xaa],byte +0x0                    ; 2E25
        jz short L6_2E37                                ; 2E2A
        mov word [si+0xaa],0x0                          ; 2E2C
        push si                                         ; 2E32
        push cs                                         ; 2E33
        call L6_2BE8                                    ; 2E34

L6_2E37:
        pop si                                          ; 2E37
        mov_ sp,bp                                      ; 2E38
        pop bp                                          ; 2E3A
        retf 0x2                                        ; 2E3B

L6_2E3E:
        push bp                                         ; 2E3E
        mov_ bp,sp                                      ; 2E3F
        push di                                         ; 2E41
        push si                                         ; 2E42
        mov si,[bp+0x6]                                 ; 2E43
        cmp word [si+0x1c],byte +0x0                    ; 2E46
        jz short L6_2E60                                ; 2E4A
        cmp word [si+0xaa],byte +0x0                    ; 2E4C
        jnz short L6_2E60                               ; 2E51
        mov word [si+0xaa],0x1                          ; 2E53
        cmp word [si+0xe1],byte +0x0                    ; 2E59
        jnz short L6_2E62                               ; 2E5E

L6_2E60:
        jmp short L6_2EDB                               ; 2E60

L6_2E62:
        push si                                         ; 2E62
        mov al,0x78                                     ; 2E63
        push ax                                         ; 2E65
        xor_ al,al                                      ; 2E66
        push ax                                         ; 2E68
        callf mixer_write, R6_2E6C, R6_2E82             ; 2E69 far seg1
        mov ax,[si+0x101]                               ; 2E6E
        mov_ bx,ax                                      ; 2E72
        mov_ di,ax                                      ; 2E74
        add word [bx+0x14],byte +0x1                    ; 2E76
        adc word [bx+0x16],byte +0x0                    ; 2E7A
        push si                                         ; 2E7E
        callf dma_pos_play, R6_2E82, R6_2EBB            ; 2E7F far seg1
        cmp ax,[si+0xf1]                                ; 2E84
        jc short L6_2E93                                ; 2E88
        test word [si+0x2c],0x1                         ; 2E8A
        jz short L6_2EA2                                ; 2E8F
        jmp short L6_2E9A                               ; 2E91

L6_2E93:
        test word [si+0x2c],0x1                         ; 2E93
        jnz short L6_2EA2                               ; 2E98

L6_2E9A:
        add word [di+0x14],byte +0x1                    ; 2E9A
        adc word [di+0x16],byte +0x0                    ; 2E9E

L6_2EA2:
        mov ax,[si+0xf1]                                ; 2EA2
        sub_ dx,dx                                      ; 2EA6
        push dx                                         ; 2EA8
        push ax                                         ; 2EA9
        mov ax,[di+0x14]                                ; 2EAA
        mov dx,[di+0x16]                                ; 2EAD
        add ax,strict word 0x1                          ; 2EB0
        adc dx,byte +0x0                                ; 2EB3
        push dx                                         ; 2EB6
        push ax                                         ; 2EB7
        callf L1_23E4, R6_2EBB, R6_2EF5                 ; 2EB8 far seg1
        cmp dx,[di+0x12]                                ; 2EBD
        ja short L6_2EDB                                ; 2EC0
        jc short L6_2EC9                                ; 2EC2
        cmp ax,[di+0x10]                                ; 2EC4
        ja short L6_2EDB                                ; 2EC7

L6_2EC9:
        mov ax,[di+0x14]                                ; 2EC9
        mov dx,[di+0x16]                                ; 2ECC
        add ax,strict word 0x1                          ; 2ECF
        adc dx,byte +0x0                                ; 2ED2
        mov [di+0x14],ax                                ; 2ED5
        mov [di+0x16],dx                                ; 2ED8

L6_2EDB:
        pop si                                          ; 2EDB
        pop di                                          ; 2EDC
        mov_ sp,bp                                      ; 2EDD
        pop bp                                          ; 2EDF
        retf 0x2                                        ; 2EE0
        db 0x90                                         ; 2EE3

L6_2EE4:
        push bp                                         ; 2EE4
        mov_ bp,sp                                      ; 2EE5
        push si                                         ; 2EE7
        mov si,[bp+0x6]                                 ; 2EE8
        cmp word [si+0x1c],byte +0x0                    ; 2EEB
        jz short L6_2EF7                                ; 2EEF
        push si                                         ; 2EF1
        callf audio2_stop, R6_2EF5, R6_2FFD             ; 2EF2 far seg1

L6_2EF7:
        pop si                                          ; 2EF7
        mov_ sp,bp                                      ; 2EF8
        pop bp                                          ; 2EFA
        retf 0x2                                        ; 2EFB

L6_2EFE:
        push bp                                         ; 2EFE
        mov_ bp,sp                                      ; 2EFF
        sub sp,byte +0xa                                ; 2F01
        push si                                         ; 2F04
        mov bx,[bp+0x6]                                 ; 2F05
        cmp word [bx+0x1c],byte +0x0                    ; 2F08
        jnz short L6_2F11                               ; 2F0C
        jmp near L6_3024                                ; 2F0E

L6_2F11:
        les bx,[bp+0x8]                                 ; 2F11
        sub_ ax,ax                                      ; 2F14
        mov [es:bx+0x1a],ax                             ; 2F16
        mov [es:bx+0x18],ax                             ; 2F1A
        mov ax,[0xa6]                                   ; 2F1E
        inc word [0xa6]                                 ; 2F21
        or_ ax,ax                                       ; 2F25
        jnz short L6_2F2A                               ; 2F27
        cli                                             ; 2F29

L6_2F2A:
        mov bx,[bp+0x6]                                 ; 2F2A
        mov ax,[bx+0xae]                                ; 2F2D
        or ax,[bx+0xac]                                 ; 2F31
        jnz short L6_2F84                               ; 2F35
        les si,[bp+0x8]                                 ; 2F37
        mov [bx+0xac],si                                ; 2F3A
        mov [bx+0xae],es                                ; 2F3E
        mov ax,[es:si+0x4]                              ; 2F42
        mov dx,[es:si+0x6]                              ; 2F46
        and al,0xfe                                     ; 2F4A
        mov [0x88],ax                                   ; 2F4C
        mov [0x8a],dx                                   ; 2F4F
        mov ax,[bx+0xbe]                                ; 2F53
        or ax,[bx+0xbc]                                 ; 2F57
        jz short L6_2FB7                                ; 2F5B
        mov ax,[bx+0xbc]                                ; 2F5D
        mov dx,[bx+0xbe]                                ; 2F61
        jmp short L6_2F6F                               ; 2F65

L6_2F67:
        mov ax,[es:bx+0x18]                             ; 2F67
        mov dx,[es:bx+0x1a]                             ; 2F6B

L6_2F6F:
        mov [bp-0x4],ax                                 ; 2F6F
        mov [bp-0x2],dx                                 ; 2F72
        les bx,[bp-0x4]                                 ; 2F75
        mov ax,[es:bx+0x1a]                             ; 2F78
        or ax,[es:bx+0x18]                              ; 2F7C
        jnz short L6_2F67                               ; 2F80
        jmp short L6_2FA9                               ; 2F82

L6_2F84:
        mov ax,[bx+0xac]                                ; 2F84
        mov dx,[bx+0xae]                                ; 2F88
        jmp short L6_2F96                               ; 2F8C

L6_2F8E:
        mov ax,[es:bx+0x18]                             ; 2F8E
        mov dx,[es:bx+0x1a]                             ; 2F92

L6_2F96:
        mov [bp-0x4],ax                                 ; 2F96
        mov [bp-0x2],dx                                 ; 2F99
        les bx,[bp-0x4]                                 ; 2F9C
        mov ax,[es:bx+0x1a]                             ; 2F9F
        or ax,[es:bx+0x18]                              ; 2FA3
        jnz short L6_2F8E                               ; 2FA7

L6_2FA9:
        mov ax,[bp+0x8]                                 ; 2FA9
        mov dx,[bp+0xa]                                 ; 2FAC
        mov [es:bx+0x18],ax                             ; 2FAF
        mov [es:bx+0x1a],dx                             ; 2FB3

L6_2FB7:
        mov bx,[bp+0x6]                                 ; 2FB7
        cmp word [bx+0xe1],byte +0x0                    ; 2FBA
        jz short L6_3011                                ; 2FBF
        mov ax,[bx+0xa6]                                ; 2FC1
        or ax,[bx+0xa4]                                 ; 2FC5
        jz short L6_301D                                ; 2FC9
        mov ax,[bx+0xa4]                                ; 2FCB
        mov dx,[bx+0xa6]                                ; 2FCF
        mov [bp-0xa],ax                                 ; 2FD3
        mov [bp-0x8],dx                                 ; 2FD6
        sub_ ax,ax                                      ; 2FD9
        mov [bx+0xa6],ax                                ; 2FDB
        mov [bx+0xa4],ax                                ; 2FDF
        mov ax,[bx+0xa8]                                ; 2FE3
        mov [bp-0x6],ax                                 ; 2FE7
        mov word [bx+0xa8],0x0                          ; 2FEA
        push bx                                         ; 2FF0
        push dx                                         ; 2FF1
        push word [bp-0xa]                              ; 2FF2
        push ax                                         ; 2FF5
        mov ax,0x1                                      ; 2FF6
        push ax                                         ; 2FF9
        callf L1_07B2, R6_2FFD, R6_2A43                 ; 2FFA far seg1
        mov bx,[bp+0x6]                                 ; 2FFF
        cmp word [bx+0xed],byte +0x0                    ; 3002
        jz short L6_301D                                ; 3007
        mov word [bx+0xed],0x0                          ; 3009
        jmp short L6_301D                               ; 300F

L6_3011:
        cmp word [bx+0xaa],byte +0x0                    ; 3011
        jnz short L6_301D                               ; 3016
        push bx                                         ; 3018
        push cs                                         ; 3019
        call L6_2BE8                                    ; 301A

L6_301D:
        dec word [0xa6]                                 ; 301D
        jnz short L6_3024                               ; 3021
        sti                                             ; 3023

L6_3024:
        pop si                                          ; 3024
        mov_ sp,bp                                      ; 3025
        pop bp                                          ; 3027
        retf                                            ; 3028
        db 0x90                                         ; 3029

seg6_data_end:

; relocation table
        dw (seg6_rel_end - seg6_rel_start) / 8
seg6_rel_start:
        reloc 2, 0, R6_2DF1, 0x0001, 0x0000             ; seg1
        reloc 2, 0, R6_1D88, 0x0003, 0x0000             ; seg3
        reloc 3, 1, R6_15E1, 0x0001, 0x0005             ; KERNEL.LocalAlloc
        reloc 3, 1, R6_181A, 0x0001, 0x0007             ; KERNEL.LocalFree
        reloc 2, 0, R6_1E68, 0x0004, 0x0000             ; seg4
        reloc 2, 0, R6_2D5D, 0x0005, 0x0000             ; seg5
        reloc 2, 0, R6_2AD3, 0x0006, 0x0000             ; seg6
        reloc 2, 0, R6_1D26, 0x0007, 0x0000             ; seg7
        reloc 3, 1, R6_1CDA, 0x0002, 0x01A4             ; USER.wsprintf
        reloc 3, 1, R6_1CC5, 0x0002, 0x00B0             ; USER.LoadString
        reloc 3, 1, R6_1CF1, 0x0001, 0x0161             ; KERNEL.lstrcpyn
        reloc 3, 1, R6_2A0F, 0x0003, 0x025F             ; MMSYSTEM.timeGetTime
seg6_rel_end:
seg6_end:
