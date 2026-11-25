; segment 4: code, 912 bytes, flags 1D50h


; VxD API 0002 (BX=1)
vxd_acquire:
        push bp                                         ; 0000
        mov_ bp,sp                                      ; 0001
        push si                                         ; 0003
        mov si,[bp+0xa]                                 ; 0004
        mov bx,[bp+0x6]                                 ; 0007
        mov ax,[bp+0x8]                                 ; 000A
        mov dx,0x2                                      ; 000D
        call far [0x10]                                 ; 0010
        jc short L4_001C                                ; 0014
        shr bx,1                                        ; 0016
        adc [si+0x2e],al                                ; 0018
        clc                                             ; 001B

L4_001C:
        pop si                                          ; 001C
        mov_ sp,bp                                      ; 001D
        pop bp                                          ; 001F
        retf 0x6                                        ; 0020

; VxD API 0003 (BX=1)
vxd_release:
        push bp                                         ; 0023
        mov_ bp,sp                                      ; 0024
        push si                                         ; 0026
        mov si,[bp+0xa]                                 ; 0027
        xor_ ax,ax                                      ; 002A
        mov bx,[bp+0x6]                                 ; 002C
        test bx,0x1                                     ; 002F
        jz short L4_0047                                ; 0033
        cmp byte [si+0x2e],0x1                          ; 0035
        jnz short L4_0047                               ; 0039
        mov ax,[bp+0x8]                                 ; 003B
        mov dx,0x3                                      ; 003E
        call far [0x10]                                 ; 0041
        jc short L4_004D                                ; 0045

L4_0047:
        shr bx,1                                        ; 0047
        sbb [si+0x2e],al                                ; 0049
        clc                                             ; 004C

L4_004D:
        pop si                                          ; 004D
        mov_ sp,bp                                      ; 004E
        pop bp                                          ; 0050
        retf 0x6                                        ; 0051

; wave-out: busy if +127h, an Audio 2 user (+100h) or +70h bit 0; else acquire the DSP and Audio 2 user = 1
wod_acquire:
        push bp                                         ; 0054
        mov_ bp,sp                                      ; 0055
        push si                                         ; 0057
        mov si,[bp+0x6]                                 ; 0058
        mov ax,0xffff                                   ; 005B
        test byte [si+0x127],0x1                        ; 005E
        jnz short L4_0098                               ; 0063
        test [si+0x100],al                              ; 0065
        jnz short L4_0098                               ; 0069
        and word [si+0x2a],byte -0x41                   ; 006B
        cmp byte [si+0xd4],0x3                          ; 006F
        jna short L4_007A                               ; 0074
        or word [si+0x2a],byte +0x40                    ; 0076

L4_007A:
        test byte [si+0x70],0x1                         ; 007A
        jnz short L4_0098                               ; 007E
        push si                                         ; 0080
        push word [si]                                  ; 0081
        push byte +0x1                                  ; 0083
        push cs                                         ; 0085
        call vxd_acquire                                ; 0086
        or_ ax,ax                                       ; 0089
        jnz short L4_0098                               ; 008B
        or byte [si+0x70],0x1                           ; 008D
        mov byte [si+0x100],0x1                         ; 0091
        xor_ ax,ax                                      ; 0096

L4_0098:
        pop si                                          ; 0098
        mov_ sp,bp                                      ; 0099
        pop bp                                          ; 009B
        retf 0x2                                        ; 009C

wod_release:
        push bp                                         ; 009F
        mov_ bp,sp                                      ; 00A0
        push si                                         ; 00A2
        mov si,[bp+0x6]                                 ; 00A3
        mov ax,0xffff                                   ; 00A6
        test byte [si+0x70],0x1                         ; 00A9
        jz short L4_00D9                                ; 00AD
        cmp byte [si+0x100],0x1                         ; 00AF
        jnz short L4_00D9                               ; 00B4
        mov byte [si+0x100],0x0                         ; 00B6
        and word [si+0x2a],byte -0x41                   ; 00BB
        cmp byte [si+0xd4],0x3                          ; 00BF
        jna short L4_00CA                               ; 00C4
        or word [si+0x2a],byte +0x40                    ; 00C6

L4_00CA:
        push si                                         ; 00CA
        push word [si]                                  ; 00CB
        push byte +0x1                                  ; 00CD
        push cs                                         ; 00CF
        call vxd_release                                ; 00D0
        and byte [si+0x70],0xfe                         ; 00D3
        xor_ ax,ax                                      ; 00D7

L4_00D9:
        pop si                                          ; 00D9
        mov_ sp,bp                                      ; 00DA
        pop bp                                          ; 00DC
        retf 0x2                                        ; 00DD

; wave-in: busy if +127h, an Audio 1 user (+5Dh) or +6Fh bit 0; else acquire the DSP and Audio 1 user = 2
wid_acquire:
        push bp                                         ; 00E0
        mov_ bp,sp                                      ; 00E1
        push si                                         ; 00E3
        mov si,[bp+0x6]                                 ; 00E4
        mov ax,0xffff                                   ; 00E7
        test byte [si+0x127],0x1                        ; 00EA
        jnz short L4_0113                               ; 00EF
        test [si+0x5d],al                               ; 00F1
        jnz short L4_0113                               ; 00F4
        test byte [si+0x6f],0x1                         ; 00F6
        jnz short L4_0113                               ; 00FA
        push si                                         ; 00FC
        push word [si]                                  ; 00FD
        push byte +0x1                                  ; 00FF
        push cs                                         ; 0101
        call vxd_acquire                                ; 0102
        or_ ax,ax                                       ; 0105
        jnz short L4_0113                               ; 0107
        or byte [si+0x6f],0x1                           ; 0109
        mov byte [si+0x5d],0x2                          ; 010D
        xor_ ax,ax                                      ; 0111

L4_0113:
        pop si                                          ; 0113
        mov_ sp,bp                                      ; 0114
        pop bp                                          ; 0116
        retf 0x2                                        ; 0117

wid_release:
        push bp                                         ; 011A
        mov_ bp,sp                                      ; 011B
        push si                                         ; 011D
        mov si,[bp+0x6]                                 ; 011E
        mov ax,0xffff                                   ; 0121
        test byte [si+0x6f],0x1                         ; 0124
        jz short L4_0143                                ; 0128
        cmp byte [si+0x5d],0x2                          ; 012A
        jnz short L4_0143                               ; 012E
        mov byte [si+0x5d],0x0                          ; 0130
        push si                                         ; 0134
        push word [si]                                  ; 0135
        push byte +0x1                                  ; 0137
        push cs                                         ; 0139
        call vxd_release                                ; 013A
        and byte [si+0x6f],0xfe                         ; 013D
        xor_ ax,ax                                      ; 0141

L4_0143:
        pop si                                          ; 0143
        mov_ sp,bp                                      ; 0144
        pop bp                                          ; 0146
        retf 0x2                                        ; 0147

vxd_0005:
        push bp                                         ; 014A
        mov_ bp,sp                                      ; 014B
        push ecx                                        ; 014D
        mov eax,[bp+0xa]                                ; 014F
        mov ebx,[bp+0x6]                                ; 0153
        mov ecx,[bp+0xe]                                ; 0157
        mov dx,0x5                                      ; 015B
        call far [0x10]                                 ; 015E
        pop ecx                                         ; 0162
        mov ax,0xffff                                   ; 0164
        jc short L4_0181                                ; 0167
        mov ax,[bp+0xa]                                 ; 0169
        or ax,[bp+0xc]                                  ; 016C
        jz short L4_0181                                ; 016F
        xor_ ax,ax                                      ; 0171
        push di                                         ; 0173
        push es                                         ; 0174
        les di,[bp+0x6]                                 ; 0175
        mov [es:di],bx                                  ; 0178
        mov [es:di+0x2],ax                              ; 017B
        pop es                                          ; 017F
        pop di                                          ; 0180

L4_0181:
        mov_ sp,bp                                      ; 0181
        pop bp                                          ; 0183
        retf 0xc                                        ; 0184

; VxD API 0006: hardware volume callback
vxd_hwvol_callback:
        push bp                                         ; 0187
        mov_ bp,sp                                      ; 0188
        push eax                                        ; 018A
        push ecx                                        ; 018C
        mov eax,[bp+0x6]                                ; 018E
        mov ecx,[bp+0xa]                                ; 0192
        mov dx,0x6                                      ; 0196
        call far [0x10]                                 ; 0199
        pop ecx                                         ; 019D
        pop eax                                         ; 019F
        mov_ sp,bp                                      ; 01A1
        pop bp                                          ; 01A3
        retf 0x8                                        ; 01A4

; VxD API 000A: register dsp_busy_callback
vxd_dsp_callback:
        push bp                                         ; 01A7
        mov_ bp,sp                                      ; 01A8
        push eax                                        ; 01AA
        push ecx                                        ; 01AC
        mov eax,[bp+0x6]                                ; 01AE
        mov ecx,[bp+0xa]                                ; 01B2
        mov dx,0xa                                      ; 01B6
        call far [0x10]                                 ; 01B9
        pop ecx                                         ; 01BD
        pop eax                                         ; 01BF
        mov_ sp,bp                                      ; 01C1
        pop bp                                          ; 01C3
        retf 0x8                                        ; 01C4
        db 0x00                                         ; 01C7
        push bp                                         ; 01C8
        mov_ bp,sp                                      ; 01C9
        sub sp,byte +0x26                               ; 01CB
        push di                                         ; 01CE
        push si                                         ; 01CF
        mov bx,[bp+0x6]                                 ; 01D0
        xor_ ax,ax                                      ; 01D3
        mov [bp-0x26],ax                                ; 01D5
        mov [bp-0x24],ax                                ; 01D8
        mov [bp-0x22],ax                                ; 01DB
        mov ax,0x1                                      ; 01DE
        mov [bp-0x20],ax                                ; 01E1
        mov [bp-0x1e],ax                                ; 01E4
        mov [bp-0x1c],ax                                ; 01E7
        mov ax,0x2                                      ; 01EA
        mov [bp-0x1a],ax                                ; 01ED
        mov [bp-0x18],ax                                ; 01F0
        mov [bp-0x16],ax                                ; 01F3
        mov [bp-0x14],ax                                ; 01F6
        mov ax,0x3                                      ; 01F9
        mov [bp-0x12],ax                                ; 01FC
        mov [bp-0x10],ax                                ; 01FF
        mov [bp-0xe],ax                                 ; 0202
        mov [bp-0xc],ax                                 ; 0205
        mov [bp-0xa],ax                                 ; 0208
        mov [bp-0x8],ax                                 ; 020B
        cmp bx,byte -0x1                                ; 020E
        jnz short L4_0217                               ; 0211
        xor_ al,al                                      ; 0213
        jmp short L4_028C                               ; 0215

L4_0217:
        or_ bx,bx                                       ; 0217
        jnz short L4_021F                               ; 0219
        mov al,0xc0                                     ; 021B
        jmp short L4_028C                               ; 021D

L4_021F:
        mov_ cx,bx                                      ; 021F
        xor_ di,di                                      ; 0221
        mov [bp-0x2],di                                 ; 0223
        or_ bx,bx                                       ; 0226
        jz short L4_0252                                ; 0228
        mov [bp-0x6],bx                                 ; 022A
        mov_ si,bx                                      ; 022D

L4_022F:
        add di,byte +0x4                                ; 022F
        inc word [bp-0x2]                               ; 0232
        shr si,1                                        ; 0235
        or_ si,si                                       ; 0237
        jnz short L4_022F                               ; 0239
        mov [bp-0x4],di                                 ; 023B
        mov bx,[bp+0x6]                                 ; 023E

L4_0241:
        mov di,[bp-0x2]                                 ; 0241
        cmp di,byte +0x5                                ; 0244
        jna short L4_0257                               ; 0247
        lea cx,[di-0x5]                                 ; 0249
        mov_ ax,bx                                      ; 024C
        shr ax,cl                                       ; 024E
        jmp short L4_0266                               ; 0250

L4_0252:
        mov [bp-0x4],di                                 ; 0252
        jmp short L4_0241                               ; 0255

L4_0257:
        cmp di,byte +0x5                                ; 0257
        jnc short L4_026B                               ; 025A
        mov cl,0x5                                      ; 025C
        mov_ ax,di                                      ; 025E
        sub_ cl,al                                      ; 0260
        mov_ ax,bx                                      ; 0262
        shl ax,cl                                       ; 0264

L4_0266:
        mov [bp-0x6],ax                                 ; 0266
        jmp short L4_026E                               ; 0269

L4_026B:
        mov [bp-0x6],bx                                 ; 026B

L4_026E:
        mov al,0x43                                     ; 026E
        mov si,[bp-0x6]                                 ; 0270
        and si,byte +0xf                                ; 0273
        add_ si,si                                      ; 0276
        sub al,[bp+si-0x26]                             ; 0278
        sub al,[bp-0x4]                                 ; 027B
        mov [bp-0x1],al                                 ; 027E
        cmp al,0x80                                     ; 0281
        jc short L4_0289                                ; 0283
        mov al,0x7f                                     ; 0285
        jmp short L4_028C                               ; 0287

L4_0289:
        mov al,[bp-0x1]                                 ; 0289

L4_028C:
        pop si                                          ; 028C
        pop di                                          ; 028D
        mov_ sp,bp                                      ; 028E
        pop bp                                          ; 0290
        retf 0x2                                        ; 0291
        push bp                                         ; 0294
        mov_ bp,sp                                      ; 0295
        sub sp,byte +0x8                                ; 0297
        push di                                         ; 029A
        push si                                         ; 029B
        mov cx,[bp+0x6]                                 ; 029C
        mov word [bp-0x8],0xffff                        ; 029F
        mov word [bp-0x6],0xe000                        ; 02A4
        mov word [bp-0x4],0xc000                        ; 02A9
        mov word [bp-0x2],0xa000                        ; 02AE
        mov_ di,cx                                      ; 02B3
        shr di,1                                        ; 02B5
        shr di,1                                        ; 02B7
        mov_ bx,cx                                      ; 02B9
        and bx,byte +0x3                                ; 02BB
        cmp di,byte +0x10                               ; 02BE
        jc short L4_02C7                                ; 02C1
        xor_ ax,ax                                      ; 02C3
        jmp short L4_02D2                               ; 02C5

L4_02C7:
        mov_ si,bx                                      ; 02C7
        add_ si,bx                                      ; 02C9
        mov ax,[bp+si-0x8]                              ; 02CB
        mov_ cx,di                                      ; 02CE
        shr ax,cl                                       ; 02D0

L4_02D2:
        pop si                                          ; 02D2
        pop di                                          ; 02D3
        mov_ sp,bp                                      ; 02D4
        pop bp                                          ; 02D6
        retf 0x2                                        ; 02D7

L4_02DA:
        push bp                                         ; 02DA
        mov_ bp,sp                                      ; 02DB
        push ds                                         ; 02DD
        movsel ax, R4_02DF, 0xFFFF                      ; 02DE seg7
        mov ds,ax                                       ; 02E1
        mov cx,[bp+0xe]                                 ; 02E3
        mov_ ax,cx                                      ; 02E6
        dec ax                                          ; 02E8
        cmp ax,strict word 0xe                          ; 02E9
        ja short L4_0314                                ; 02EC
        add_ ax,ax                                      ; 02EE
        xchg ax,bx                                      ; 02F0
        jmp [cs:bx+JT4_02F6]                            ; 02F1
JT4_02F6:
        dw L4_0384                                      ; 02F6
        dw L4_0331                                      ; 02F8
        dw L4_0384                                      ; 02FA
        dw L4_0384                                      ; 02FC
        dw L4_0344                                      ; 02FE
        dw L4_0384                                      ; 0300
        dw L4_034B                                      ; 0302
        dw L4_0384                                      ; 0304
        dw L4_0384                                      ; 0306
        dw L4_033A                                      ; 0308
        dw L4_0314                                      ; 030A
        dw L4_0314                                      ; 030C
        dw L4_0314                                      ; 030E
        dw L4_0314                                      ; 0310
        dw L4_035F                                      ; 0312

L4_0314:
        push word [bp+0x14]                             ; 0314
        push word [bp+0x12]                             ; 0317
        push word [bp+0x10]                             ; 031A
        push cx                                         ; 031D
        push word [bp+0xc]                              ; 031E
        push word [bp+0xa]                              ; 0321
        push word [bp+0x8]                              ; 0324
        push word [bp+0x6]                              ; 0327
        callp R4_032B, 0xFFFF, 0x0000                   ; 032A USER.DefDriverProc
        jmp short L4_0389                               ; 032F

L4_0331:
        callf L3_5012, R4_0334, R4_0347                 ; 0331 far seg3
        or_ dx,ax                                       ; 0336
        jz short L4_033E                                ; 0338

L4_033A:
        xor_ ax,ax                                      ; 033A
        jmp short L4_0387                               ; 033C

L4_033E:
        mov ax,0x1                                      ; 033E

L4_0341:
        cwd                                             ; 0341
        jmp short L4_0389                               ; 0342

L4_0344:
        callf L3_503C, R4_0347, R4_035B                 ; 0344 far seg3
        jmp short L4_0384                               ; 0349

L4_034B:
        push word [bp+0xa]                              ; 034B
        push word [0xbd0]                               ; 034E
        push word [bp+0x8]                              ; 0352
        push word [bp+0x6]                              ; 0355
        callf L3_4550, R4_035B, R4_037B                 ; 0358 far seg3
        jmp short L4_0341                               ; 035D

L4_035F:
        mov ax,[bp+0xa]                                 ; 035F
        mov dx,[bp+0xc]                                 ; 0362
        or_ dx,dx                                       ; 0365
        jnz short L4_0384                               ; 0367
        dec ax                                          ; 0369
        jz short L4_0378                                ; 036A
        sub ax,strict word 0x1                          ; 036C
        jc short L4_0384                                ; 036F
        sub ax,strict word 0x1                          ; 0371
        jna short L4_037F                               ; 0374
        jmp short L4_0384                               ; 0376

L4_0378:
        callf power_suspend, R4_037B, R4_0382           ; 0378 far seg3
        jmp short L4_0384                               ; 037D

L4_037F:
        callf power_resume, R4_0382, 0xFFFF             ; 037F far seg3

L4_0384:
        mov ax,0x1                                      ; 0384

L4_0387:
        xor_ dx,dx                                      ; 0387

L4_0389:
        pop ds                                          ; 0389
        mov_ sp,bp                                      ; 038A
        pop bp                                          ; 038C
        retf 0x10                                       ; 038D

seg4_data_end:

; relocation table
        dw (seg4_rel_end - seg4_rel_start) / 8
seg4_rel_start:
        reloc 2, 0, R4_0334, 0x0003, 0x0000             ; seg3
        reloc 3, 1, R4_032B, 0x0002, 0x00FF             ; USER.DefDriverProc
        reloc 2, 0, R4_02DF, 0x0007, 0x0000             ; seg7
seg4_rel_end:
seg4_end:
