; Segment 1 of ESFM.DRV: code, 7894 bytes, flags 0D40h.
; ESS's driver code, disassembled with tools/ne2asm.py.

        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ; 0000

; write an FM register: fm_write(dev, reg (9 bits), value)
; native mode: FM_Base+2 = register bits 7:0, +3 = bits 8:15, +1 = data,
; with fm_delay status reads after each write (not atomic)
fm_write:
        push bp                                         ; 0010
        mov_ bp,sp                                      ; 0011
        sub sp,byte +0x4                                ; 0013
        push si                                         ; 0016
        mov si,[fm_delay]                               ; 0017
        mov bx,[bp+0xa]                                 ; 001B
        mov al,[bp+0x8]                                 ; 001E
        sub_ ah,ah                                      ; 0021
        mov dx,[bx+0xa]                                 ; 0023
        mov [bp-0x4],dx                                 ; 0026
        inc dx                                          ; 0029
        inc dx                                          ; 002A
        out dx,al                                       ; 002B
        mov dx,[bp-0x4]                                 ; 002C
        in al,dx                                        ; 002F
        in al,dx                                        ; 0030
        mov al,[bp+0x9]                                 ; 0031
        add dx,byte +0x3                                ; 0034
        out dx,al                                       ; 0037
        or_ si,si                                       ; 0038
        jz short L1_0044                                ; 003A
        mov_ cx,si                                      ; 003C

L1_003E:
        mov dx,[bp-0x4]                                 ; 003E
        in al,dx                                        ; 0041
        loop L1_003E                                    ; 0042

L1_0044:
        mov al,[bp+0x6]                                 ; 0044
        sub_ ah,ah                                      ; 0047
        mov dx,[bp-0x4]                                 ; 0049
        inc dx                                          ; 004C
        out dx,al                                       ; 004D
        or_ si,si                                       ; 004E
        jz short L1_005B                                ; 0050
        mov_ bx,si                                      ; 0052

L1_0054:
        mov dx,[bp-0x4]                                 ; 0054
        in al,dx                                        ; 0057
        dec bx                                          ; 0058
        jnz short L1_0054                               ; 0059

L1_005B:
        pop si                                          ; 005B
        mov_ sp,bp                                      ; 005C
        pop bp                                          ; 005E
        retf 0x6                                        ; 005F

; attenuation for MIDI volume (CC 7) / 4
vol_curve:
        db 0x28, 0x24, 0x20, 0x1C, 0x17, 0x15, 0x13, 0x11, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08 ; 0062
        db 0x07, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00 ; 0072

; F-numbers of the 12 semitones
fnum_table:
        db 0x02, 0x02, 0x20, 0x02, 0x41, 0x02, 0x63, 0x02, 0x87, 0x02, 0xAE, 0x02, 0xD7, 0x02, 0x02, 0x03 ; 0082
        db 0x30, 0x03, 0x60, 0x03, 0x94, 0x03, 0xCA, 0x03 ; 0092

; reset the synthesizer (native mode, registers 000h-253h = 0) and the
; channel and voice state of dev
chip_reset:
        push bp                                         ; 009A
        mov_ bp,sp                                      ; 009B
        push di                                         ; 009D
        push si                                         ; 009E
%if ESFM_FIX
        call fix_enter
%endif
        mov di,[bp+0x6]                                 ; 009F
        xor_ ax,ax                                      ; 00A2
        mov dx,[di+0xa]                                 ; 00A4
        out dx,al                                       ; 00A7
        push di                                         ; 00A8
        mov cx,0x5                                      ; 00A9
        push cx                                         ; 00AC
        mov cl,0x80                                     ; 00AD
        push cx                                         ; 00AF
        push cs                                         ; 00B0
        call fm_write                                   ; 00B1
        xor_ si,si                                      ; 00B4

L1_00B6:
        push di                                         ; 00B6
        push si                                         ; 00B7
        xor_ al,al                                      ; 00B8
        push ax                                         ; 00BA
        push cs                                         ; 00BB
        call fm_write                                   ; 00BC
        inc si                                          ; 00BF
        cmp si,0x253                                    ; 00C0
        jna short L1_00B6                               ; 00C4
        xor_ si,si                                      ; 00C6
        lea cx,[di+0x50]                                ; 00C8

L1_00CB:
        mov_ bx,si                                      ; 00CB
        add_ bx,di                                      ; 00CD
        mov byte [bx+0x2d2],0x30                        ; 00CF
        mov byte [bx+0x2c2],0x4                         ; 00D4
        mov byte [bx+0x2e2],0x64                        ; 00D9
        mov byte [bx+0x2f2],0x7f                        ; 00DE
        mov byte [bx+0x40],0x0                          ; 00E3
        mov byte [bx+0x30],0x2                          ; 00E7
        mov_ bx,cx                                      ; 00EB
        inc cx                                          ; 00ED
        inc cx                                          ; 00EE
        mov word [bx],0x2000                            ; 00EF
        inc si                                          ; 00F3
        cmp si,byte +0x10                               ; 00F4
        jc short L1_00CB                                ; 00F7
%if ESFM_FIX
        call fix_gm_init                                ; esfmgm.asm
%endif
        lea si,[di+0x71]                                ; 00F9
        mov cx,0x12                                     ; 00FC

L1_00FF:
        sub_ ax,ax                                      ; 00FF
        mov [si+0x2],ax                                 ; 0101
        mov [si],ax                                     ; 0104
        mov [si-0x1],al                                 ; 0106
        add si,byte +0x21                               ; 0109
        loop L1_00FF                                    ; 010C
        mov [di+0x1e],ax                                ; 010E
        mov [di+0x1c],ax                                ; 0111
%if ESFM_FIX
        call fix_unlock
%endif
        pop si                                          ; 0114
        pop di                                          ; 0115
        mov_ sp,bp                                      ; 0116
        pop bp                                          ; 0118
        retf 0x2                                        ; 0119

; note_off for every voice's channel and note (close, suspend), but voices
; held by the sustain pedal stay keyed on
all_notes_off:
%if ESFM_FIX
        jmp near fix_all_off
%endif
        push bp                                         ; 011C
        mov_ bp,sp                                      ; 011D
        push di                                         ; 011F
        push si                                         ; 0120
        mov si,[bp+0x6]                                 ; 0121
        add si,byte +0x75                               ; 0124
        mov di,0x12                                     ; 0127

L1_012A:
        push word [bp+0x6]                              ; 012A
        mov al,[si]                                     ; 012D
        push ax                                         ; 012F
        mov al,[si+0x1]                                 ; 0130
        push ax                                         ; 0133
        push cs                                         ; 0134
        call note_off                                   ; 0135
        add si,byte +0x21                               ; 0138
        dec di                                          ; 013B
        jnz short L1_012A                               ; 013C
        pop si                                          ; 013E
        pop di                                          ; 013F
        mov_ sp,bp                                      ; 0140
        pop bp                                          ; 0142
        retf 0x2                                        ; 0143

; key off one voice: voice_off(dev, voice)
voice_off:
        push bp                                         ; 0146
        mov_ bp,sp                                      ; 0147
        push di                                         ; 0149
        push si                                         ; 014A
        mov di,[bp+0x6]                                 ; 014B
        cmp di,byte +0x10                               ; 014E
        jnl short L1_015D                               ; 0151
        mov si,[bp+0x8]                                 ; 0153
        push si                                         ; 0156
        lea ax,[di+0x240]                               ; 0157
        jmp short L1_018A                               ; 015B

L1_015D:
        cmp di,byte +0x10                               ; 015D
        jnz short L1_0177                               ; 0160
        mov si,[bp+0x8]                                 ; 0162
        push si                                         ; 0165
        mov ax,0x250                                    ; 0166
        push ax                                         ; 0169
        xor_ al,al                                      ; 016A
        push ax                                         ; 016C
        push cs                                         ; 016D
        call fm_write                                   ; 016E
        push si                                         ; 0171
        mov ax,0x251                                    ; 0172
        jmp short L1_018A                               ; 0175

L1_0177:
        mov si,[bp+0x8]                                 ; 0177
        push si                                         ; 017A
        mov ax,0x252                                    ; 017B
        push ax                                         ; 017E
        xor_ al,al                                      ; 017F
        push ax                                         ; 0181
        push cs                                         ; 0182
        call fm_write                                   ; 0183
        push si                                         ; 0186
        mov ax,0x253                                    ; 0187

L1_018A:
        push ax                                         ; 018A
        xor_ al,al                                      ; 018B
        push ax                                         ; 018D
        push cs                                         ; 018E
        call fm_write                                   ; 018F
        mov_ ax,di                                      ; 0192
        mov cx,0x21                                     ; 0194
        imul cx                                         ; 0197
        mov_ bx,ax                                      ; 0199
        add_ bx,si                                      ; 019B
        mov byte [bx+0x70],0x2                          ; 019D
        mov ax,[si+0x1c]                                ; 01A1
        mov dx,[si+0x1e]                                ; 01A4
        mov [bx+0x71],ax                                ; 01A7
        mov [bx+0x73],dx                                ; 01AA
        add word [si+0x1c],byte +0x1                    ; 01AD
        adc word [si+0x1e],byte +0x0                    ; 01B1
        pop si                                          ; 01B5
        pop di                                          ; 01B6
        mov_ sp,bp                                      ; 01B7
        pop bp                                          ; 01B9
        retf 0x4                                        ; 01BA
        db 0x90                                         ; 01BD

; MIDI note off: note_off(dev, channel, note)
; keys off every voice of that note, or marks it (flag 4) while the
; channel's sustain pedal is down
note_off:
        push bp                                         ; 01BE
        mov_ bp,sp                                      ; 01BF
        push di                                         ; 01C1
        push si                                         ; 01C2
        xor_ di,di                                      ; 01C3
        mov si,[bp+0xa]                                 ; 01C5
        add si,byte +0x70                               ; 01C8

L1_01CB:
        test byte [si],0x1                              ; 01CB
        jz short L1_01FB                                ; 01CE
        mov al,[si+0x5]                                 ; 01D0
        cmp [bp+0x8],al                                 ; 01D3
        jnz short L1_01FB                               ; 01D6
        mov al,[si+0x6]                                 ; 01D8
        cmp [bp+0x6],al                                 ; 01DB
        jnz short L1_01FB                               ; 01DE
        mov bl,[bp+0x8]                                 ; 01E0
        sub_ bh,bh                                      ; 01E3
        add bx,[bp+0xa]                                 ; 01E5
        test byte [bx+0x40],0x1                         ; 01E8
        jz short L1_01F3                                ; 01EC
        or byte [si],0x4                                ; 01EE
        jmp short L1_01FB                               ; 01F1

L1_01F3:
        push word [bp+0xa]                              ; 01F3
        push di                                         ; 01F6
        push cs                                         ; 01F7
        call voice_off                                  ; 01F8

L1_01FB:
        add si,byte +0x21                               ; 01FB
        inc di                                          ; 01FE
        cmp di,byte +0x12                               ; 01FF
        jl short L1_01CB                                ; 0202
        pop si                                          ; 0204
        pop di                                          ; 0205
        mov_ sp,bp                                      ; 0206
        pop bp                                          ; 0208
        retf 0x6                                        ; 0209

; pick the two oldest free voices for a note (dev+18h, dev+1Ah, FFh = none)
; and key off voices still playing the same note
find_voices:
        push bp                                         ; 020C
        mov_ bp,sp                                      ; 020D
        sub sp,byte +0xe                                ; 020F
        push di                                         ; 0212
        push si                                         ; 0213
        mov di,[bp+0xc]                                 ; 0214
        mov ax,0xff                                     ; 0217
        mov [di+0x18],ax                                ; 021A
        mov [di+0x1a],ax                                ; 021D
        sub_ ax,ax                                      ; 0220
        mov [bp-0x8],ax                                 ; 0222
        mov [bp-0xa],ax                                 ; 0225
        mov [bp-0xc],ax                                 ; 0228
        mov [bp-0xe],ax                                 ; 022B
        mov [bp-0x2],ax                                 ; 022E
        lea si,[di+0x70]                                ; 0231

L1_0234:
        test byte [si],0x1                              ; 0234
        jz short L1_0251                                ; 0237
        mov al,[si+0x5]                                 ; 0239
        cmp [bp+0x6],al                                 ; 023C
        jnz short L1_0251                               ; 023F
        mov al,[si+0x6]                                 ; 0241
        cmp [bp+0x4],al                                 ; 0244
        jnz short L1_0251                               ; 0247
        push di                                         ; 0249
        push word [bp-0x2]                              ; 024A
        push cs                                         ; 024D
        call voice_off                                  ; 024E

L1_0251:
        test byte [si],0x1                              ; 0251
        jnz short L1_02B2                               ; 0254
        mov ax,[di+0x1c]                                ; 0256
        mov dx,[di+0x1e]                                ; 0259
        sub ax,[si+0x1]                                 ; 025C
        sbb dx,[si+0x3]                                 ; 025F
        mov [bp-0x6],ax                                 ; 0262
        mov [bp-0x4],dx                                 ; 0265
        cmp dx,[bp-0x8]                                 ; 0268
        jc short L1_029A                                ; 026B
        ja short L1_0274                                ; 026D
        cmp ax,[bp-0xa]                                 ; 026F
        jc short L1_029A                                ; 0272

L1_0274:
        mov ax,[bp-0xa]                                 ; 0274
        mov dx,[bp-0x8]                                 ; 0277
        mov [bp-0xe],ax                                 ; 027A
        mov [bp-0xc],dx                                 ; 027D
        mov ax,[di+0x18]                                ; 0280
        mov [di+0x1a],ax                                ; 0283
        mov ax,[bp-0x6]                                 ; 0286
        mov dx,[bp-0x4]                                 ; 0289
        mov [bp-0xa],ax                                 ; 028C
        mov [bp-0x8],dx                                 ; 028F
        mov ax,[bp-0x2]                                 ; 0292
        mov [di+0x18],ax                                ; 0295
        jmp short L1_02B2                               ; 0298

L1_029A:
        cmp [bp-0xc],dx                                 ; 029A
        ja short L1_02B2                                ; 029D
        jc short L1_02A6                                ; 029F
        cmp [bp-0xe],ax                                 ; 02A1
        ja short L1_02B2                                ; 02A4

L1_02A6:
        mov [bp-0xe],ax                                 ; 02A6
        mov [bp-0xc],dx                                 ; 02A9
        mov ax,[bp-0x2]                                 ; 02AC
        mov [di+0x1a],ax                                ; 02AF

L1_02B2:
        add si,byte +0x21                               ; 02B2
        inc word [bp-0x2]                               ; 02B5
        cmp word [bp-0x2],byte +0x10                    ; 02B8
        jnl short L1_02C1                               ; 02BC
        jmp near L1_0234                                ; 02BE

L1_02C1:
        test byte [di+0x280],0x1                        ; 02C1
        jz short L1_02E3                                ; 02C6
        mov al,[di+0x285]                               ; 02C8
        cmp [bp+0x6],al                                 ; 02CC
        jnz short L1_02E3                               ; 02CF
        mov al,[di+0x286]                               ; 02D1
        cmp [bp+0x4],al                                 ; 02D5
        jnz short L1_02E3                               ; 02D8
        push di                                         ; 02DA
        mov ax,0x10                                     ; 02DB
        push ax                                         ; 02DE
        push cs                                         ; 02DF
        call voice_off                                  ; 02E0

L1_02E3:
        test byte [di+0x280],0x1                        ; 02E3
        jnz short L1_0352                               ; 02E8
        mov ax,[di+0x1c]                                ; 02EA
        mov dx,[di+0x1e]                                ; 02ED
        sub ax,[di+0x281]                               ; 02F0
        sbb dx,[di+0x283]                               ; 02F4
        mov [bp-0x6],ax                                 ; 02F8
        mov [bp-0x4],dx                                 ; 02FB
        cmp word [bp+0xa],byte +0x0                     ; 02FE
        jnz short L1_0335                               ; 0302
        cmp [bp-0x8],dx                                 ; 0304
        ja short L1_0335                                ; 0307
        jc short L1_0310                                ; 0309
        cmp [bp-0xa],ax                                 ; 030B
        ja short L1_0335                                ; 030E

L1_0310:
        mov ax,[bp-0xa]                                 ; 0310
        mov dx,[bp-0x8]                                 ; 0313
        mov [bp-0xe],ax                                 ; 0316
        mov [bp-0xc],dx                                 ; 0319
        mov ax,[di+0x18]                                ; 031C
        mov [di+0x1a],ax                                ; 031F
        mov ax,[bp-0x6]                                 ; 0322
        mov dx,[bp-0x4]                                 ; 0325
        mov [bp-0xa],ax                                 ; 0328
        mov [bp-0x8],dx                                 ; 032B
        mov word [di+0x18],0x10                         ; 032E
        jmp short L1_0352                               ; 0333

L1_0335:
        cmp word [bp+0x8],byte +0x0                     ; 0335
        jnz short L1_0352                               ; 0339
        cmp [bp-0xc],dx                                 ; 033B
        ja short L1_0352                                ; 033E
        jc short L1_0347                                ; 0340
        cmp [bp-0xe],ax                                 ; 0342
        ja short L1_0352                                ; 0345

L1_0347:
        mov [bp-0xe],ax                                 ; 0347
        mov [bp-0xc],dx                                 ; 034A
        mov word [di+0x1a],0x10                         ; 034D

L1_0352:
        test byte [di+0x2a1],0x1                        ; 0352
        jz short L1_0374                                ; 0357
        mov al,[di+0x2a6]                               ; 0359
        cmp [bp+0x6],al                                 ; 035D
        jnz short L1_0374                               ; 0360
        mov al,[di+0x2a7]                               ; 0362
        cmp [bp+0x4],al                                 ; 0366
        jnz short L1_0374                               ; 0369
        push di                                         ; 036B
        mov ax,0x11                                     ; 036C
        push ax                                         ; 036F
        push cs                                         ; 0370
        call voice_off                                  ; 0371

L1_0374:
        test byte [di+0x2a1],0x1                        ; 0374
        jnz short L1_03D1                               ; 0379
        mov ax,[di+0x1c]                                ; 037B
        mov dx,[di+0x1e]                                ; 037E
        sub ax,[di+0x2a2]                               ; 0381
        sbb dx,[di+0x2a4]                               ; 0385
        mov [bp-0x6],ax                                 ; 0389
        mov [bp-0x4],dx                                 ; 038C
        cmp word [bp+0xa],byte +0x0                     ; 038F
        jnz short L1_03BA                               ; 0393
        cmp [bp-0x8],dx                                 ; 0395
        ja short L1_03BA                                ; 0398
        jc short L1_03A1                                ; 039A
        cmp [bp-0xa],ax                                 ; 039C
        ja short L1_03BA                                ; 039F

L1_03A1:
        cmp word [di+0x18],byte +0x10                   ; 03A1
        jnz short L1_03AD                               ; 03A5
        cmp word [bp+0x8],byte +0x0                     ; 03A7
        jnz short L1_03B3                               ; 03AB

L1_03AD:
        mov ax,[di+0x18]                                ; 03AD
        mov [di+0x1a],ax                                ; 03B0

L1_03B3:
        mov word [di+0x18],0x11                         ; 03B3
        jmp short L1_03D1                               ; 03B8

L1_03BA:
        cmp word [bp+0x8],byte +0x0                     ; 03BA
        jnz short L1_03D1                               ; 03BE
        cmp [bp-0xc],dx                                 ; 03C0
        ja short L1_03D1                                ; 03C3
        jc short L1_03CC                                ; 03C5
        cmp [bp-0xe],ax                                 ; 03C7
        ja short L1_03D1                                ; 03CA

L1_03CC:
        mov word [di+0x1a],0x11                         ; 03CC

L1_03D1:
        pop si                                          ; 03D1
        pop di                                          ; 03D2
        mov_ sp,bp                                      ; 03D3
        pop bp                                          ; 03D5
        ret 0xa                                         ; 03D6
        db 0x90                                         ; 03D9

; no free voice: take one (second voices first, then the highest channel,
; then the oldest) and key it off
steal_voice:
        push bp                                         ; 03DA
        mov_ bp,sp                                      ; 03DB
        sub sp,byte +0x12                               ; 03DD
        push di                                         ; 03E0
        push si                                         ; 03E1
        cmp word [bp+0x4],byte +0x1                     ; 03E2
        sbb_ cx,cx                                      ; 03E6
        and cx,byte +0x2                                ; 03E8
        add cx,byte +0x10                               ; 03EB
        xor_ al,al                                      ; 03EE
        mov [bp-0x6],al                                 ; 03F0
        mov [bp-0x9],al                                 ; 03F3
        mov word [bp-0x8],0x0                           ; 03F6
        or_ cx,cx                                       ; 03FB
        jng short L1_042F                               ; 03FD
        mov [bp-0x10],cx                                ; 03FF
        mov si,[bp+0x6]                                 ; 0402
        add si,byte +0x75                               ; 0405
        mov cx,[bp-0x8]                                 ; 0408
        mov di,[bp-0x12]                                ; 040B

L1_040E:
        mov bx,[bp+0x6]                                 ; 040E
        mov ax,[bx+0x1c]                                ; 0411
        mov dx,[bx+0x1e]                                ; 0414
        sub ax,[si-0x4]                                 ; 0417
        sbb dx,[si-0x2]                                 ; 041A
        mov [bp-0x4],ax                                 ; 041D
        mov al,[si]                                     ; 0420
        mov [bp-0x5],al                                 ; 0422
        cmp al,0x9                                      ; 0425
        jnz short L1_0434                               ; 0427
        mov byte [bp-0x5],0x1                           ; 0429
        jmp short L1_0438                               ; 042D

L1_042F:
        mov di,[bp-0x12]                                ; 042F
        jmp short L1_0489                               ; 0432

L1_0434:
        add byte [bp-0x5],0x2                           ; 0434

L1_0438:
        mov al,[si-0x5]                                 ; 0438
        and al,0x8                                      ; 043B
        cmp al,[bp-0x9]                                 ; 043D
        jz short L1_044E                                ; 0440
        cmp byte [bp-0x9],0x0                           ; 0442
        jnz short L1_0480                               ; 0446
        mov byte [bp-0x9],0x8                           ; 0448
        jmp short L1_0456                               ; 044C

L1_044E:
        mov al,[bp-0x6]                                 ; 044E
        cmp [bp-0x5],al                                 ; 0451
        jna short L1_045E                               ; 0454

L1_0456:
        mov al,[bp-0x5]                                 ; 0456
        mov [bp-0x6],al                                 ; 0459
        jmp short L1_0475                               ; 045C

L1_045E:
        mov al,[bp-0x6]                                 ; 045E
        cmp [bp-0x5],al                                 ; 0461
        jnz short L1_0480                               ; 0464
        mov ax,[bp-0x4]                                 ; 0466
        cmp [bp-0xc],dx                                 ; 0469
        ja short L1_0480                                ; 046C
        jc short L1_0475                                ; 046E
        cmp [bp-0xe],ax                                 ; 0470
        jnc short L1_0480                               ; 0473

L1_0475:
        mov ax,[bp-0x4]                                 ; 0475
        mov [bp-0xe],ax                                 ; 0478
        mov [bp-0xc],dx                                 ; 047B
        mov_ di,cx                                      ; 047E

L1_0480:
        add si,byte +0x21                               ; 0480
        inc cx                                          ; 0483
        cmp [bp-0x10],cx                                ; 0484
        jg short L1_040E                                ; 0487

L1_0489:
        push word [bp+0x6]                              ; 0489
        push di                                         ; 048C
        push cs                                         ; 048D
        call voice_off                                  ; 048E
        mov_ ax,di                                      ; 0491
        pop si                                          ; 0493
        pop di                                          ; 0494
        mov_ sp,bp                                      ; 0495
        pop bp                                          ; 0497
        ret 0x4                                         ; 0498
        db 0x90                                         ; 049B

; key on one voice: voice_on(dev, voice)
voice_on:
        push bp                                         ; 049C
        mov_ bp,sp                                      ; 049D
        push si                                         ; 049F
        mov si,[bp+0x6]                                 ; 04A0
        cmp si,byte +0x10                               ; 04A3
        jnl short L1_04B1                               ; 04A6
        push word [bp+0x8]                              ; 04A8
        lea ax,[si+0x240]                               ; 04AB
        jmp short L1_04DE                               ; 04AF

L1_04B1:
        cmp si,byte +0x10                               ; 04B1
        jnz short L1_04CB                               ; 04B4
        mov si,[bp+0x8]                                 ; 04B6
        push si                                         ; 04B9
        mov ax,0x250                                    ; 04BA
        push ax                                         ; 04BD
        mov al,0x1                                      ; 04BE
        push ax                                         ; 04C0
        push cs                                         ; 04C1
        call fm_write                                   ; 04C2
        push si                                         ; 04C5
        mov ax,0x251                                    ; 04C6
        jmp short L1_04DE                               ; 04C9

L1_04CB:
        mov si,[bp+0x8]                                 ; 04CB
        push si                                         ; 04CE
        mov ax,0x252                                    ; 04CF
        push ax                                         ; 04D2
        mov al,0x1                                      ; 04D3
        push ax                                         ; 04D5
        push cs                                         ; 04D6
        call fm_write                                   ; 04D7
        push si                                         ; 04DA
        mov ax,0x253                                    ; 04DB

L1_04DE:
        push ax                                         ; 04DE
        mov al,0x1                                      ; 04DF
        push ax                                         ; 04E1
        push cs                                         ; 04E2
        call fm_write                                   ; 04E3
        pop si                                          ; 04E6
        mov_ sp,bp                                      ; 04E7
        pop bp                                          ; 04E9
        retf 0x4                                        ; 04EA
        db 0x90                                         ; 04ED

; F-number for a note, pitch bend and operator detune
calc_pitch:
        push bp                                         ; 04EE
        mov_ bp,sp                                      ; 04EF
        sub sp,0xe2                                     ; 04F1
        push di                                         ; 04F5
        push si                                         ; 04F6
        mov si,[bp+0x6]                                 ; 04F7
        mov word [bp-0x62],0x100                        ; 04FA
        mov word [bp-0x60],0x10f                        ; 04FF
        mov word [bp-0x5e],0x11f                        ; 0504
        mov word [bp-0x5c],0x130                        ; 0509
        mov word [bp-0x5a],0x143                        ; 050E
        mov word [bp-0x58],0x156                        ; 0513
        mov word [bp-0x56],0x16a                        ; 0518
        mov word [bp-0x54],0x180                        ; 051D
        mov word [bp-0x52],0x196                        ; 0522
        mov word [bp-0x50],0x1af                        ; 0527
        mov word [bp-0x4e],0x1c8                        ; 052C
        mov word [bp-0x4c],0x1e3                        ; 0531
        mov word [bp-0x4a],0x200                        ; 0536
        mov word [bp-0x48],0x21e                        ; 053B
        mov word [bp-0x46],0x23f                        ; 0540
        mov word [bp-0x44],0x261                        ; 0545
        mov word [bp-0x42],0x285                        ; 054A
        mov word [bp-0x40],0x2ab                        ; 054F
        mov word [bp-0x3e],0x2d4                        ; 0554
        mov word [bp-0x3c],0x2ff                        ; 0559
        mov word [bp-0x3a],0x32d                        ; 055E
        mov word [bp-0x38],0x35d                        ; 0563
        mov word [bp-0x36],0x390                        ; 0568
        mov word [bp-0x34],0x3c7                        ; 056D
        mov word [bp-0x30],0x43d                        ; 0572
        mov word [bp-0x2e],0x47d                        ; 0577
        mov word [bp-0x2c],0x4c2                        ; 057C
        mov word [bp-0x2a],0x50a                        ; 0581
        mov word [bp-0x28],0x557                        ; 0586
        mov word [bp-0x26],0x5a8                        ; 058B
        mov word [bp-0x24],0x5fe                        ; 0590
        mov word [bp-0x22],0x659                        ; 0595
        mov word [bp-0x20],0x6ba                        ; 059A
        mov word [bp-0x1e],0x721                        ; 059F
        mov word [bp-0x1c],0x78d                        ; 05A4
        mov word [bp-0x1a],0x800                        ; 05A9
        mov word [bp-0x18],0x87a                        ; 05AE
        mov word [bp-0x16],0x8fb                        ; 05B3
        mov word [bp-0x14],0x983                        ; 05B8
        mov word [bp-0x12],0xa14                        ; 05BD
        mov word [bp-0x10],0xaae                        ; 05C2
        mov word [bp-0xe],0xb50                         ; 05C7
        mov word [bp-0xc],0xbfd                         ; 05CC
        mov word [bp-0xa],0xcb3                         ; 05D1
        mov word [bp-0x8],0xd74                         ; 05D6
        mov word [bp-0x6],0xe41                         ; 05DB
        mov word [bp-0x4],0xf1a                         ; 05E0
        mov word [bp-0x2],0x1000                        ; 05E5
        mov ax,0x400                                    ; 05EA
        mov [bp-0x32],ax                                ; 05ED
        mov [bp-0xe2],ax                                ; 05F0
        mov word [bp-0xe0],0x401                        ; 05F4
        mov word [bp-0xde],0x402                        ; 05FA
        mov word [bp-0xdc],0x403                        ; 0600
        mov word [bp-0xda],0x404                        ; 0606
        mov word [bp-0xd8],0x405                        ; 060C
        mov ax,0x406                                    ; 0612
        mov [bp-0xd6],ax                                ; 0615
        mov [bp-0xd4],ax                                ; 0619
        mov word [bp-0xd2],0x407                        ; 061D
        mov word [bp-0xd0],0x408                        ; 0623
        mov word [bp-0xce],0x409                        ; 0629
        mov word [bp-0xcc],0x40a                        ; 062F
        mov word [bp-0xca],0x40b                        ; 0635
        mov word [bp-0xc8],0x40c                        ; 063B
        mov word [bp-0xc6],0x40d                        ; 0641
        mov word [bp-0xc4],0x40e                        ; 0647
        mov word [bp-0xc2],0x40f                        ; 064D
        mov word [bp-0xc0],0x410                        ; 0653
        mov word [bp-0xbe],0x411                        ; 0659
        mov word [bp-0xbc],0x412                        ; 065F
        mov word [bp-0xba],0x413                        ; 0665
        mov word [bp-0xb8],0x414                        ; 066B
        mov ax,0x415                                    ; 0671
        mov [bp-0xb6],ax                                ; 0674
        mov [bp-0xb4],ax                                ; 0678
        mov word [bp-0xb2],0x416                        ; 067C
        mov word [bp-0xb0],0x417                        ; 0682
        mov word [bp-0xae],0x418                        ; 0688
        mov word [bp-0xac],0x419                        ; 068E
        mov word [bp-0xaa],0x41a                        ; 0694
        mov word [bp-0xa8],0x41b                        ; 069A
        mov word [bp-0xa6],0x41c                        ; 06A0
        mov word [bp-0xa4],0x41d                        ; 06A6
        mov word [bp-0xa2],0x41e                        ; 06AC
        mov word [bp-0xa0],0x41f                        ; 06B2
        mov word [bp-0x9e],0x420                        ; 06B8
        mov word [bp-0x9c],0x421                        ; 06BE
        mov word [bp-0x9a],0x422                        ; 06C4
        mov word [bp-0x98],0x423                        ; 06CA
        mov word [bp-0x96],0x424                        ; 06D0
        mov word [bp-0x94],0x425                        ; 06D6
        mov word [bp-0x92],0x426                        ; 06DC
        mov word [bp-0x90],0x427                        ; 06E2
        mov word [bp-0x8e],0x428                        ; 06E8
        mov ax,0x429                                    ; 06EE
        mov [bp-0x8c],ax                                ; 06F1
        mov [bp-0x8a],ax                                ; 06F5
        mov word [bp-0x88],0x42a                        ; 06F9
        mov word [bp-0x86],0x42b                        ; 06FF
        mov word [bp-0x84],0x42c                        ; 0705
        mov word [bp-0x82],0x42d                        ; 070B
        mov word [bp-0x80],0x42e                        ; 0711
        mov word [bp-0x7e],0x42f                        ; 0716
        mov word [bp-0x7c],0x430                        ; 071B
        mov word [bp-0x7a],0x431                        ; 0720
        mov word [bp-0x78],0x432                        ; 0725
        mov word [bp-0x76],0x433                        ; 072A
        mov word [bp-0x74],0x434                        ; 072F
        mov word [bp-0x72],0x435                        ; 0734
        mov word [bp-0x70],0x436                        ; 0739
        mov word [bp-0x6e],0x437                        ; 073E
        mov word [bp-0x6c],0x438                        ; 0743
        mov word [bp-0x6a],0x439                        ; 0748
        mov word [bp-0x68],0x43a                        ; 074D
        mov word [bp-0x66],0x43b                        ; 0752
        mov word [bp-0x64],0x43c                        ; 0757
        cmp si,0x2000                                   ; 075C
        jnz short L1_0767                               ; 0760
        mov ax,[bp+0x8]                                 ; 0762
        jmp short L1_07BE                               ; 0765

L1_0767:
        cmp si,0x3f80                                   ; 0767
        jc short L1_0770                                ; 076B
        mov si,0x4000                                   ; 076D

L1_0770:
        lea ax,[si-0x2000]                              ; 0770
        cwd                                             ; 0774
        push dx                                         ; 0775
        push ax                                         ; 0776
        mov ax,[bp+0x4]                                 ; 0777
        sub_ dx,dx                                      ; 077A
        push dx                                         ; 077C
        push ax                                         ; 077D
        callf L1_1D94, R1_0781, R1_0788                 ; 077E far seg1
        mov cl,0x5                                      ; 0783
        callf L1_1DD2, R1_0788, R1_07AC                 ; 0785 far seg1
        mov_ si,ax                                      ; 078A
        add si,0x1800                                   ; 078C
        mov cl,0x8                                      ; 0790
        mov_ ax,si                                      ; 0792
        sar si,cl                                       ; 0794
        mov_ di,ax                                      ; 0796
        and di,0xfc                                     ; 0798
        sar di,1                                        ; 079C
        add_ si,si                                      ; 079E
        mov ax,[bp+si-0x62]                             ; 07A0
        imul word [bp+di-0xe2]                          ; 07A3
        mov cl,0xa                                      ; 07A7
        callf L1_1DD2, R1_07AC, R1_07BC                 ; 07A9 far seg1
        mul word [bp+0x8]                               ; 07AE
        add ah,0x2                                      ; 07B1
        adc dx,byte +0x0                                ; 07B4
        mov cl,0xa                                      ; 07B7
        callf L1_1DD2, R1_07BC, 0xFFFF                  ; 07B9 far seg1

L1_07BE:
        pop si                                          ; 07BE
        pop di                                          ; 07BF
        mov_ sp,bp                                      ; 07C0
        pop bp                                          ; 07C2
        ret 0x6                                         ; 07C3

; block and F-number bytes of an operator
calc_block_fnum:
        push bp                                         ; 07C6
        mov_ bp,sp                                      ; 07C7
        cmp word [bp+0x8],byte +0x0                     ; 07C9
        jnz short L1_07D6                               ; 07CD
        cmp word [bp+0x6],0x400                         ; 07CF
        jc short L1_07EC                                ; 07D4

L1_07D6:
        inc byte [bp+0x4]                               ; 07D6
        shr word [bp+0x8],1                             ; 07D9
        rcr word [bp+0x6],1                             ; 07DC
        cmp word [bp+0x8],byte +0x0                     ; 07DF
        jnz short L1_07D6                               ; 07E3
        cmp word [bp+0x6],0x400                         ; 07E5
        jnc short L1_07D6                               ; 07EA

L1_07EC:
        cmp byte [bp+0x4],0x7                           ; 07EC
        jna short L1_07F6                               ; 07F0
        mov byte [bp+0x4],0x7                           ; 07F2

L1_07F6:
        mov al,[bp+0x4]                                 ; 07F6
        sub_ ah,ah                                      ; 07F9
        mov cl,0xa                                      ; 07FB
        shl ax,cl                                       ; 07FD
        or ax,[bp+0x6]                                  ; 07FF
        mov_ sp,bp                                      ; 0802
        pop bp                                          ; 0804
        ret 0x6                                         ; 0805

; write the 8 registers of one operator (level from velocity and volume,
; pitch from the note)
program_operator:
        push bp                                         ; 0808
        mov_ bp,sp                                      ; 0809
        sub sp,byte +0x14                               ; 080B
        push di                                         ; 080E
        push si                                         ; 080F
%if ESFM_FIX
        call fix_coarse                                 ; esfmgm.asm
%endif
        mov bx,[bp+0x14]                                ; 0810
        add bx,[bp+0x6]                                 ; 0813
        sub_ ah,ah                                      ; 0816
        mov al,[bx+0x2d2]                               ; 0818
        mov [bp-0xc],ax                                 ; 081C
        push word [bp+0x6]                              ; 081F
        mov ax,[bp+0xe]                                 ; 0822
        add ax,strict word 0x7                          ; 0825
        push ax                                         ; 0828
        xor_ al,al                                      ; 0829
        push ax                                         ; 082B
        push cs                                         ; 082C
        call fm_write                                   ; 082D
        cmp word [bp+0x10],byte +0x0                    ; 0830
        jnz short L1_0862                               ; 0834
        mov bx,[bp+0x8]                                 ; 0836
        add bx,[bank_ptr]                               ; 0839
        mov es,[0x14]                                   ; 083D
        mov al,[es:bx+0x4]                              ; 0841
        and ax,strict word 0x3                          ; 0845
        mov dl,[es:bx+0x5]                              ; 0848
        sub_ dh,dh                                      ; 084C
        add_ dx,dx                                      ; 084E
        add_ dx,dx                                      ; 0850
        or_ al,dl                                       ; 0852
        and al,0x7f                                     ; 0854
        mov_ cx,ax                                      ; 0856
        test al,0x40                                    ; 0858
        jz short L1_085F                                ; 085A
        or cx,byte -0x80                                ; 085C

L1_085F:
        add [bp+0xa],cx                                 ; 085F

L1_0862:
        cmp word [bp+0xa],byte +0x13                    ; 0862
        jnl short L1_087A                               ; 0866
        mov ax,0x1e                                     ; 0868
        sub ax,[bp+0xa]                                 ; 086B
        mov cx,0xc                                      ; 086E
        sub_ dx,dx                                      ; 0871
        div cx                                          ; 0873
        mul cx                                          ; 0875
        add [bp+0xa],ax                                 ; 0877

L1_087A:
        cmp word [bp+0xa],byte +0x72                    ; 087A
        jng short L1_0892                               ; 087E
        mov ax,[bp+0xa]                                 ; 0880
        sub ax,strict word 0x67                         ; 0883
        mov cx,0xc                                      ; 0886
        sub_ dx,dx                                      ; 0889
        div cx                                          ; 088B
        mul cx                                          ; 088D
        sub [bp+0xa],ax                                 ; 088F

L1_0892:
        mov ax,[bp+0xa]                                 ; 0892
        sub ax,strict word 0x13                         ; 0895
        mov cx,0xc                                      ; 0898
        mov_ bx,ax                                      ; 089B
        cwd                                             ; 089D
        idiv cx                                         ; 089E
        mov [bp-0x8],dx                                 ; 08A0
        mov_ ax,bx                                      ; 08A3
        cwd                                             ; 08A5
        idiv cx                                         ; 08A6
        mov_ di,ax                                      ; 08A8
        push word [bp+0x6]                              ; 08AA
        push word [bp+0xe]                              ; 08AD
        mov bx,[bp+0x8]                                 ; 08B0
        les si,[bank_ptr]                               ; 08B3
        mov al,[es:bx+si]                               ; 08B7
%if ESFM_FIX
        call fix_op_reg0                                ; esfmgm.asm
%endif
        push ax                                         ; 08BA
        push cs                                         ; 08BB
        call fm_write                                   ; 08BC
        mov ax,[bp+0x12]                                ; 08BF
        or_ ax,ax                                       ; 08C2
        jz short L1_08D4                                ; 08C4
        dec ax                                          ; 08C6
        jz short L1_08D8                                ; 08C7
        dec ax                                          ; 08C9
        jz short L1_08E4                                ; 08CA
        dec ax                                          ; 08CC
        jz short L1_08E8                                ; 08CD
        mov si,[bp-0x2]                                 ; 08CF
        jmp short L1_0905                               ; 08D2

L1_08D4:
        xor_ si,si                                      ; 08D4
        jmp short L1_0905                               ; 08D6

L1_08D8:
        mov cl,0x4                                      ; 08D8

L1_08DA:
        mov si,0x7f                                     ; 08DA
        sub si,[bp+0xc]                                 ; 08DD
        sar si,cl                                       ; 08E0
        jmp short L1_0905                               ; 08E2

L1_08E4:
        mov cl,0x3                                      ; 08E4
        jmp short L1_08DA                               ; 08E6

L1_08E8:
        mov cx,[bp+0xc]                                 ; 08E8
        cmp cx,byte +0x40                               ; 08EB
        jl short L1_08FB                                ; 08EE
        mov si,0x7f                                     ; 08F0
        sub_ si,cx                                      ; 08F3
        sar si,1                                        ; 08F5
        sar si,1                                        ; 08F7
        jmp short L1_0905                               ; 08F9

L1_08FB:
        mov si,0x3f                                     ; 08FB
        sub_ si,cx                                      ; 08FE
        sar si,1                                        ; 0900
        add si,byte +0x10                               ; 0902

L1_0905:
        mov bx,[bp+0x8]                                 ; 0905
        mov es,[0x14]                                   ; 0908
        add bx,[bank_ptr]                               ; 090C
        mov [bp-0x10],bx                                ; 0910
        mov [bp-0xe],es                                 ; 0913
        mov al,[es:bx+0x1]                              ; 0916
        and ax,strict word 0x3f                         ; 091A
        add_ ax,si                                      ; 091D
        mov [bp-0x6],ax                                 ; 091F
        cmp ax,strict word 0x3f                         ; 0922
        jng short L1_092C                               ; 0925
        mov word [bp-0x6],0x3f                          ; 0927

L1_092C:
        push word [bp+0x6]                              ; 092C
        mov es,[bp-0xe]                                 ; 092F
        mov al,[es:bx+0x1]                              ; 0932
        and ax,0xc0                                     ; 0936
        mov_ si,ax                                      ; 0939
        add si,[bp-0x6]                                 ; 093B
        mov ax,0x21                                     ; 093E
        imul word [bp+0x18]                             ; 0941
        mov_ bx,ax                                      ; 0944
        mov [bp-0x14],ax                                ; 0946
        add bx,[bp+0x16]                                ; 0949
        add bx,[bp+0x6]                                 ; 094C
        mov [bp-0x12],bx                                ; 094F
        mov_ ax,si                                      ; 0952
        mov [bx+0x8d],al                                ; 0954
        push si                                         ; 0958
        push word [bp+0x12]                             ; 0959
        mov al,[bp+0x14]                                ; 095C
        push ax                                         ; 095F
        call calc_level                                 ; 0960
        sub_ ah,ah                                      ; 0963
        mov_ si,ax                                      ; 0965
        push word [bp+0x6]                              ; 0967
        mov ax,[bp+0xe]                                 ; 096A
        inc ax                                          ; 096D
        push ax                                         ; 096E
        push si                                         ; 096F
        push cs                                         ; 0970
        call fm_write                                   ; 0971
        push word [bp+0x6]                              ; 0974
        mov ax,[bp+0xe]                                 ; 0977
        inc ax                                          ; 097A
        inc ax                                          ; 097B
        push ax                                         ; 097C
        les bx,[bank_ptr]                               ; 097D
        mov si,[bp+0x8]                                 ; 0981
        mov al,[es:bx+si+0x2]                           ; 0984
        push ax                                         ; 0988
        push cs                                         ; 0989
        call fm_write                                   ; 098A
        push word [bp+0x6]                              ; 098D
        mov ax,[bp+0xe]                                 ; 0990
        add ax,strict word 0x3                          ; 0993
        push ax                                         ; 0996
        les bx,[bank_ptr]                               ; 0997
        mov al,[es:bx+si+0x3]                           ; 099B
        push ax                                         ; 099F
        push cs                                         ; 09A0
        call fm_write                                   ; 09A1
        cmp word [bp+0x10],byte +0x0                    ; 09A4
        jz short L1_09AD                                ; 09A8
        jmp near L1_0A72                                ; 09AA

L1_09AD:
        mov_ bx,si                                      ; 09AD
        mov es,[0x14]                                   ; 09AF
        add bx,[bank_ptr]                               ; 09B3
        mov [bp-0x10],bx                                ; 09B7
        mov [bp-0xe],es                                 ; 09BA
        mov al,[es:bx+0x4]                              ; 09BD
        and al,0xfc                                     ; 09C1
        cbw                                             ; 09C3
        mov_ si,ax                                      ; 09C4
        or_ si,ax                                       ; 09C6
        jz short L1_09E8                                ; 09C8
        mov bx,[bp-0x8]                                 ; 09CA
        add_ bx,bx                                      ; 09CD
        db 0x8B, 0x87, 0x24, 0x00                       ; 09CF mov ax,[bx+0x24]
        sar si,1                                        ; 09D3
        sar si,1                                        ; 09D5
        imul si                                         ; 09D7
        mov_ al,ah                                      ; 09D9
        cbw                                             ; 09DB
        mov_ si,ax                                      ; 09DC
        cmp di,byte +0x1                                ; 09DE
        jng short L1_09E8                               ; 09E1
        lea cx,[di-0x1]                                 ; 09E3
        sar si,cl                                       ; 09E6

L1_09E8:
        mov_ ax,si                                      ; 09E8
        mov bx,[bp-0x8]                                 ; 09EA
        add_ bx,bx                                      ; 09ED
        add ax,[cs:bx+0x82]                             ; 09EF
        mov_ cx,ax                                      ; 09F4
        mov_ al,ah                                      ; 09F6
        and ax,strict word 0x3                          ; 09F8
        les bx,[bp-0x10]                                ; 09FB
        mov dl,[es:bx+0x5]                              ; 09FE
        and dx,0xe0                                     ; 0A02
        add_ ax,dx                                      ; 0A06
        mov_ dx,di                                      ; 0A08
        add_ di,di                                      ; 0A0A
        add_ di,di                                      ; 0A0C
        add_ ax,di                                      ; 0A0E
        mov [bp-0x2],ax                                 ; 0A10
        mov bx,[bp-0x12]                                ; 0A13
        mov [bp-0x6],cx                                 ; 0A16
        mov [bx+0x89],al                                ; 0A19
        push cx                                         ; 0A1D
        mov si,[bp+0x14]                                ; 0A1E
        add_ si,si                                      ; 0A21
        mov bx,[bp+0x6]                                 ; 0A23
        push word [bx+si+0x50]                          ; 0A26
        add bx,[bp+0x14]                                ; 0A29
        sub_ ah,ah                                      ; 0A2C
        mov al,[bx+0x30]                                ; 0A2E
        push ax                                         ; 0A31
        mov_ si,dx                                      ; 0A32
%if ESFM_FIX
        push word [bp+0x14]
        push word [bp+0x6]
        call fix_calc_pitch                             ; esfmgm.asm
%else
        call calc_pitch                                 ; 0A34
%endif
        sub_ dx,dx                                      ; 0A37
        push dx                                         ; 0A39
        push ax                                         ; 0A3A
        push si                                         ; 0A3B
        call calc_block_fnum                            ; 0A3C
        mov [bp-0xa],ax                                 ; 0A3F
        sub_ ah,ah                                      ; 0A42
        mov [bp-0x4],ax                                 ; 0A44
        mov al,[bp-0x9]                                 ; 0A47
        mov cl,[bp-0x2]                                 ; 0A4A
        and cx,0xe0                                     ; 0A4D
        add_ ax,cx                                      ; 0A51
        mov [bp-0x2],ax                                 ; 0A53
        mov ax,[bp-0x6]                                 ; 0A56
        cwd                                             ; 0A59
        mov si,[bp+0x16]                                ; 0A5A
        add_ si,si                                      ; 0A5D
        add_ si,si                                      ; 0A5F
        mov bx,[bp+0x6]                                 ; 0A61
        add si,[bp-0x14]                                ; 0A64
        mov [bx+si+0x78],ax                             ; 0A67
        mov [bx+si+0x7a],dx                             ; 0A6A
        mov si,[bp-0x2]                                 ; 0A6D
        jmp short L1_0A89                               ; 0A70

L1_0A72:
        les di,[bank_ptr]                               ; 0A72
        mov_ bx,si                                      ; 0A76
        add_ bx,di                                      ; 0A78
        sub_ ah,ah                                      ; 0A7A
        mov al,[es:bx+0x4]                              ; 0A7C
        mov [bp-0x4],ax                                 ; 0A80
        mov al,[es:bx+0x5]                              ; 0A83
        mov_ si,ax                                      ; 0A87

L1_0A89:
        push word [bp+0x6]                              ; 0A89
        mov ax,[bp+0xe]                                 ; 0A8C
        add ax,strict word 0x4                          ; 0A8F
        push ax                                         ; 0A92
        mov al,[bp-0x4]                                 ; 0A93
        push ax                                         ; 0A96
        push cs                                         ; 0A97
        call fm_write                                   ; 0A98
        push word [bp+0x6]                              ; 0A9B
        mov ax,[bp+0xe]                                 ; 0A9E
        add ax,strict word 0x5                          ; 0AA1
        push ax                                         ; 0AA4
        push si                                         ; 0AA5
        push cs                                         ; 0AA6
        call fm_write                                   ; 0AA7
        mov bx,[bp+0x8]                                 ; 0AAA
        mov es,[0x14]                                   ; 0AAD
        add bx,[bank_ptr]                               ; 0AB1
        mov [bp-0xe],es                                 ; 0AB5
        test byte [es:bx+0x6],0x30                      ; 0AB8
        jz short L1_0ADA                                ; 0ABD
        cmp word [bp-0xc],byte +0x30                    ; 0ABF
        jz short L1_0ADA                                ; 0AC3
        push word [bp+0x6]                              ; 0AC5
        mov ax,[bp+0xe]                                 ; 0AC8
        add ax,strict word 0x6                          ; 0ACB
        push ax                                         ; 0ACE
        mov al,[es:bx+0x6]                              ; 0ACF
        and al,0xcf                                     ; 0AD3
        or al,[bp-0xc]                                  ; 0AD5
        jmp short L1_0AEB                               ; 0AD8

L1_0ADA:
        push word [bp+0x6]                              ; 0ADA
        mov ax,[bp+0xe]                                 ; 0ADD
        add ax,strict word 0x6                          ; 0AE0
        push ax                                         ; 0AE3
        mov es,[bp-0xe]                                 ; 0AE4
        mov al,[es:bx+0x6]                              ; 0AE7

L1_0AEB:
%if ESFM_FIX
        call fix_op_reg6                                ; esfmgm.asm
%endif
        push ax                                         ; 0AEB
        push cs                                         ; 0AEC
        call fm_write                                   ; 0AED
        push word [bp+0x6]                              ; 0AF0
        mov ax,[bp+0xe]                                 ; 0AF3
        add ax,strict word 0x7                          ; 0AF6
        push ax                                         ; 0AF9
        les bx,[bank_ptr]                               ; 0AFA
        mov si,[bp+0x8]                                 ; 0AFE
        mov al,[es:bx+si+0x7]                           ; 0B01
        push ax                                         ; 0B05
        push cs                                         ; 0B06
        call fm_write                                   ; 0B07
        pop si                                          ; 0B0A
        pop di                                          ; 0B0B
        mov_ sp,bp                                      ; 0B0C
        pop bp                                          ; 0B0E
        retf                                            ; 0B0F

; write a 36-byte patch voice to a hardware voice and mark it active
; (flag 1) with a new timestamp
program_voice:
        push bp                                         ; 0B10
        mov_ bp,sp                                      ; 0B11
        sub sp,byte +0x6                                ; 0B13
        push di                                         ; 0B16
        push si                                         ; 0B17
        mov di,[bp+0x6]                                 ; 0B18
        mov si,[bp+0x8]                                 ; 0B1B
        mov bx,[bp+0xa]                                 ; 0B1E
        add bx,[bank_ptr]                               ; 0B21
        mov es,[0x14]                                   ; 0B25
        mov al,[es:bx]                                  ; 0B29
        mov [bp-0x1],al                                 ; 0B2C
        mov al,[es:bx+0x3]                              ; 0B2F
        mov [bp-0x2],al                                 ; 0B33
        push si                                         ; 0B36
        xor_ ax,ax                                      ; 0B37
        push ax                                         ; 0B39
        push word [bp+0xc]                              ; 0B3A
        mov al,[bp-0x2]                                 ; 0B3D
        and ax,strict word 0x3                          ; 0B40
        push ax                                         ; 0B43
        mov al,[bp-0x1]                                 ; 0B44
        and ax,strict word 0x10                         ; 0B47
        push ax                                         ; 0B4A
        mov cl,0x5                                      ; 0B4B
        mov_ ax,si                                      ; 0B4D
        shl si,cl                                       ; 0B4F
        push si                                         ; 0B51
        push word [bp+0x10]                             ; 0B52
        push word [bp+0xe]                              ; 0B55
        mov cx,[bp+0xa]                                 ; 0B58
        add cx,byte +0x4                                ; 0B5B
        push cx                                         ; 0B5E
        push di                                         ; 0B5F
        mov [bp-0x4],ax                                 ; 0B60
        mov [bp-0x6],cx                                 ; 0B63
        push cs                                         ; 0B66
        call program_operator                           ; 0B67
        add sp,byte +0x14                               ; 0B6A
        push word [bp-0x4]                              ; 0B6D
        mov ax,0x1                                      ; 0B70
        push ax                                         ; 0B73
        push word [bp+0xc]                              ; 0B74
        mov al,[bp-0x2]                                 ; 0B77
        and al,0xc                                      ; 0B7A
        shr al,1                                        ; 0B7C
        shr al,1                                        ; 0B7E
        push ax                                         ; 0B80
        mov al,[bp-0x1]                                 ; 0B81
        and ax,strict word 0x20                         ; 0B84
        push ax                                         ; 0B87
        lea ax,[si+0x8]                                 ; 0B88
        push ax                                         ; 0B8B
        push word [bp+0x10]                             ; 0B8C
        push word [bp+0xe]                              ; 0B8F
        mov ax,[bp-0x6]                                 ; 0B92
        add ax,strict word 0x8                          ; 0B95
        push ax                                         ; 0B98
        push di                                         ; 0B99
        push cs                                         ; 0B9A
        call program_operator                           ; 0B9B
        add sp,byte +0x14                               ; 0B9E
        push word [bp-0x4]                              ; 0BA1
        mov ax,0x2                                      ; 0BA4
        push ax                                         ; 0BA7
        push word [bp+0xc]                              ; 0BA8
        mov cl,0x4                                      ; 0BAB
        mov al,[bp-0x2]                                 ; 0BAD
        and al,0x30                                     ; 0BB0
        shr al,cl                                       ; 0BB2
        push ax                                         ; 0BB4
        mov al,[bp-0x1]                                 ; 0BB5
        and ax,strict word 0x40                         ; 0BB8
        push ax                                         ; 0BBB
        lea ax,[si+0x10]                                ; 0BBC
        push ax                                         ; 0BBF
        push word [bp+0x10]                             ; 0BC0
        push word [bp+0xe]                              ; 0BC3
        mov ax,[bp-0x6]                                 ; 0BC6
        add ax,strict word 0x10                         ; 0BC9
        push ax                                         ; 0BCC
        push di                                         ; 0BCD
        push cs                                         ; 0BCE
        call program_operator                           ; 0BCF
        add sp,byte +0x14                               ; 0BD2
        push word [bp-0x4]                              ; 0BD5
        mov ax,0x3                                      ; 0BD8
        push ax                                         ; 0BDB
        push word [bp+0xc]                              ; 0BDC
        mov cl,0x6                                      ; 0BDF
        mov al,[bp-0x2]                                 ; 0BE1
        shr al,cl                                       ; 0BE4
        push ax                                         ; 0BE6
        mov al,[bp-0x1]                                 ; 0BE7
        and ax,0x80                                     ; 0BEA
        push ax                                         ; 0BED
        lea ax,[si+0x18]                                ; 0BEE
        push ax                                         ; 0BF1
        push word [bp+0x10]                             ; 0BF2
        push word [bp+0xe]                              ; 0BF5
        mov ax,[bp-0x6]                                 ; 0BF8
        add ax,strict word 0x18                         ; 0BFB
        push ax                                         ; 0BFE
        push di                                         ; 0BFF
        push cs                                         ; 0C00
        call program_operator                           ; 0C01
        add sp,byte +0x14                               ; 0C04
        mov ax,0x21                                     ; 0C07
        imul word [bp-0x4]                              ; 0C0A
        mov_ bx,di                                      ; 0C0D
        add_ di,ax                                      ; 0C0F
        mov al,[bp-0x1]                                 ; 0C11
        mov [di+0x88],al                                ; 0C14
        mov al,[bp-0x2]                                 ; 0C18
        mov [di+0x77],al                                ; 0C1B
        mov byte [di+0x70],0x1                          ; 0C1E
        mov ax,[bx+0x1c]                                ; 0C22
        mov dx,[bx+0x1e]                                ; 0C25
        mov [di+0x71],ax                                ; 0C28
        mov [di+0x73],dx                                ; 0C2B
        add word [bx+0x1c],byte +0x1                    ; 0C2E
        adc word [bx+0x1e],byte +0x0                    ; 0C32
        mov al,[bp+0xc]                                 ; 0C36
        mov [di+0x75],al                                ; 0C39
        mov al,[bp+0xe]                                 ; 0C3C
        mov [di+0x76],al                                ; 0C3F
        pop si                                          ; 0C42
        pop di                                          ; 0C43
        mov_ sp,bp                                      ; 0C44
        pop bp                                          ; 0C46
        retf                                            ; 0C47

; MIDI note on: note_on(velocity, note, channel, dev)
; channel 9 uses drum patches 128-255
note_on:
        push bp                                         ; 0C48
        mov_ bp,sp                                      ; 0C49
        sub sp,byte +0x8                                ; 0C4B
        push di                                         ; 0C4E
        push si                                         ; 0C4F
        cmp byte [bp+0x8],0x9                           ; 0C50
        jnz short L1_0C66                               ; 0C54
        mov di,[bp+0x6]                                 ; 0C56
        and di,0xff                                     ; 0C59
        add di,0x80                                     ; 0C5D
        mov si,[bp+0xa]                                 ; 0C61
        jmp short L1_0C75                               ; 0C64

L1_0C66:
        mov si,[bp+0xa]                                 ; 0C66
        mov bl,[bp+0x8]                                 ; 0C69
        sub_ bh,bh                                      ; 0C6C
        mov al,[bx+si+0x20]                             ; 0C6E
        sub_ ah,ah                                      ; 0C71
        mov_ di,ax                                      ; 0C73

L1_0C75:
        mov es,[0x14]                                   ; 0C75
        mov bx,[bank_ptr]                               ; 0C79
        add_ di,di                                      ; 0C7D
        mov al,[es:bx+di]                               ; 0C7F
        add_ bx,di                                      ; 0C82
        mov ah,[es:bx+0x1]                              ; 0C84
        mov [bp-0x2],ax                                 ; 0C88
        or_ ax,ax                                       ; 0C8B
        jnz short L1_0C92                               ; 0C8D
        jmp near L1_0E3E                                ; 0C8F

L1_0C92:
        mov_ bx,ax                                      ; 0C92
        mov di,[bank_ptr]                               ; 0C94
        mov al,[es:bx+di]                               ; 0C98
        and al,0x6                                      ; 0C9B
        shr al,1                                        ; 0C9D
        sub_ ah,ah                                      ; 0C9F
        or_ ax,ax                                       ; 0CA1
        jz short L1_0CB1                                ; 0CA3
        dec ax                                          ; 0CA5
        jz short L1_0D02                                ; 0CA6
        dec ax                                          ; 0CA8
        jnz short L1_0CAE                               ; 0CA9
        jmp near L1_0D99                                ; 0CAB

L1_0CAE:
        jmp near L1_0E3E                                ; 0CAE

L1_0CB1:
        push si                                         ; 0CB1
        mov al,[es:bx+di]                               ; 0CB2
        and ax,strict word 0x1                          ; 0CB5
        push ax                                         ; 0CB8
        xor_ ax,ax                                      ; 0CB9
        push ax                                         ; 0CBB
        mov al,[bp+0x8]                                 ; 0CBC
        push ax                                         ; 0CBF
        mov al,[bp+0x6]                                 ; 0CC0
        push ax                                         ; 0CC3
        call find_voices                                ; 0CC4
        cmp word [si+0x18],0xff                         ; 0CC7
        jnz short L1_0CE3                               ; 0CCC
        push si                                         ; 0CCE
        mov bx,[bp-0x2]                                 ; 0CCF
        les di,[bank_ptr]                               ; 0CD2
        mov al,[es:bx+di]                               ; 0CD6
        and ax,strict word 0x1                          ; 0CD9
        push ax                                         ; 0CDC
        call steal_voice                                ; 0CDD
        mov [si+0x18],ax                                ; 0CE0

L1_0CE3:
        mov al,[bp+0x4]                                 ; 0CE3
        sub_ ah,ah                                      ; 0CE6
        push ax                                         ; 0CE8
        mov al,[bp+0x6]                                 ; 0CE9
        push ax                                         ; 0CEC
        mov al,[bp+0x8]                                 ; 0CED
        push ax                                         ; 0CF0
        push word [bp-0x2]                              ; 0CF1
        push word [si+0x18]                             ; 0CF4
        push si                                         ; 0CF7
        push cs                                         ; 0CF8
        call program_voice                              ; 0CF9
        add sp,byte +0xc                                ; 0CFC
        jmp near L1_0D92                                ; 0CFF

L1_0D02:
        push si                                         ; 0D02
        mov al,[es:bx+di]                               ; 0D03
        and ax,strict word 0x1                          ; 0D06
        push ax                                         ; 0D09
        add_ bx,di                                      ; 0D0A
        mov al,[es:bx+0x24]                             ; 0D0C
        and ax,strict word 0x1                          ; 0D10
        push ax                                         ; 0D13
        mov al,[bp+0x8]                                 ; 0D14
        push ax                                         ; 0D17
        mov al,[bp+0x6]                                 ; 0D18
        push ax                                         ; 0D1B
        call find_voices                                ; 0D1C
        cmp word [si+0x18],0xff                         ; 0D1F
        jnz short L1_0D3B                               ; 0D24
        push si                                         ; 0D26
        mov bx,[bp-0x2]                                 ; 0D27
        les di,[bank_ptr]                               ; 0D2A
        mov al,[es:bx+di]                               ; 0D2E
        and ax,strict word 0x1                          ; 0D31
        push ax                                         ; 0D34
        call steal_voice                                ; 0D35
        mov [si+0x18],ax                                ; 0D38

L1_0D3B:
        mov al,[bp+0x4]                                 ; 0D3B
        sub_ ah,ah                                      ; 0D3E
        push ax                                         ; 0D40
        mov al,[bp+0x6]                                 ; 0D41
        push ax                                         ; 0D44
        mov al,[bp+0x8]                                 ; 0D45
        push ax                                         ; 0D48
        push word [bp-0x2]                              ; 0D49
        push word [si+0x18]                             ; 0D4C
        push si                                         ; 0D4F
        push cs                                         ; 0D50
        call program_voice                              ; 0D51
        add sp,byte +0xc                                ; 0D54
        cmp word [si+0x1a],0xff                         ; 0D57
        jz short L1_0D92                                ; 0D5C
        mov al,[bp+0x4]                                 ; 0D5E
        sub_ ah,ah                                      ; 0D61
        push ax                                         ; 0D63
        mov al,[bp+0x6]                                 ; 0D64
        push ax                                         ; 0D67
        mov al,[bp+0x8]                                 ; 0D68
        push ax                                         ; 0D6B
        mov ax,[bp-0x2]                                 ; 0D6C
        add ax,strict word 0x24                         ; 0D6F
        push ax                                         ; 0D72
        push word [si+0x1a]                             ; 0D73
        push si                                         ; 0D76
        push cs                                         ; 0D77
        call program_voice                              ; 0D78
        add sp,byte +0xc                                ; 0D7B
        mov ax,0x21                                     ; 0D7E
        imul word [si+0x1a]                             ; 0D81
        mov_ bx,ax                                      ; 0D84
        or byte [bx+si+0x70],0x8                        ; 0D86
        push si                                         ; 0D8A
        push word [si+0x1a]                             ; 0D8B
        push cs                                         ; 0D8E
        call voice_on                                   ; 0D8F

L1_0D92:
        push si                                         ; 0D92
        push word [si+0x18]                             ; 0D93
        jmp near L1_0E3A                                ; 0D96

L1_0D99:
        mov_ di,bx                                      ; 0D99
        push si                                         ; 0D9B
        mov bx,[bank_ptr]                               ; 0D9C
        mov al,[es:bx+di]                               ; 0DA0
        and ax,strict word 0x1                          ; 0DA3
        push ax                                         ; 0DA6
        add_ bx,di                                      ; 0DA7
        mov al,[es:bx+0x24]                             ; 0DA9
        and ax,strict word 0x1                          ; 0DAD
        push ax                                         ; 0DB0
        mov al,[bp+0x8]                                 ; 0DB1
        push ax                                         ; 0DB4
        mov al,[bp+0x6]                                 ; 0DB5
        push ax                                         ; 0DB8
        call find_voices                                ; 0DB9
        cmp word [si+0x18],0xff                         ; 0DBC
        jnz short L1_0DD5                               ; 0DC1
        push si                                         ; 0DC3
        les bx,[bank_ptr]                               ; 0DC4
        mov al,[es:bx+di]                               ; 0DC8
        and ax,strict word 0x1                          ; 0DCB
        push ax                                         ; 0DCE
        call steal_voice                                ; 0DCF
        mov [si+0x18],ax                                ; 0DD2

L1_0DD5:
        cmp word [si+0x1a],0xff                         ; 0DD5
        jnz short L1_0DEF                               ; 0DDA
        push si                                         ; 0DDC
        les bx,[bank_ptr]                               ; 0DDD
        mov al,[es:bx+di+0x24]                          ; 0DE1
        and ax,strict word 0x1                          ; 0DE5
        push ax                                         ; 0DE8
        call steal_voice                                ; 0DE9
        mov [si+0x1a],ax                                ; 0DEC

L1_0DEF:
        mov al,[bp+0x4]                                 ; 0DEF
        sub_ ah,ah                                      ; 0DF2
        push ax                                         ; 0DF4
        mov cl,[bp+0x6]                                 ; 0DF5
        sub_ ch,ch                                      ; 0DF8
        push cx                                         ; 0DFA
        mov dl,[bp+0x8]                                 ; 0DFB
        sub_ dh,dh                                      ; 0DFE
        push dx                                         ; 0E00
        push di                                         ; 0E01
        push word [si+0x18]                             ; 0E02
        push si                                         ; 0E05
        mov [bp-0x4],ax                                 ; 0E06
        mov [bp-0x6],cx                                 ; 0E09
        mov [bp-0x8],dx                                 ; 0E0C
        push cs                                         ; 0E0F
        call program_voice                              ; 0E10
        add sp,byte +0xc                                ; 0E13
        push word [bp-0x4]                              ; 0E16
        push word [bp-0x6]                              ; 0E19
        push word [bp-0x8]                              ; 0E1C
        lea ax,[di+0x24]                                ; 0E1F
        push ax                                         ; 0E22
        push word [si+0x1a]                             ; 0E23
        push si                                         ; 0E26
        push cs                                         ; 0E27
        call program_voice                              ; 0E28
        add sp,byte +0xc                                ; 0E2B
        push si                                         ; 0E2E
        push word [si+0x18]                             ; 0E2F
        push cs                                         ; 0E32
        call voice_on                                   ; 0E33
        push si                                         ; 0E36
        push word [si+0x1a]                             ; 0E37

L1_0E3A:
        push cs                                         ; 0E3A
        call voice_on                                   ; 0E3B

L1_0E3E:
        pop si                                          ; 0E3E
        pop di                                          ; 0E3F
        mov_ sp,bp                                      ; 0E40
        pop bp                                          ; 0E42
        ret 0x8                                         ; 0E43

; MIDI controller 64: pedal up keys off the voices marked by note_off
sustain:
        push bp                                         ; 0E46
        mov_ bp,sp                                      ; 0E47
        push di                                         ; 0E49
        push si                                         ; 0E4A
        cmp word [bp+0xa],byte +0x40                    ; 0E4B
        jl short L1_0E5D                                ; 0E4F
        mov bx,[bp+0x6]                                 ; 0E51
        mov si,[bp+0x8]                                 ; 0E54
        or byte [bx+si+0x40],0x1                        ; 0E57
        jmp short L1_0E8E                               ; 0E5B

L1_0E5D:
        mov cx,[bp+0x6]                                 ; 0E5D
        mov_ bx,cx                                      ; 0E60
        mov si,[bp+0x8]                                 ; 0E62
        and byte [bx+si+0x40],0xfe                      ; 0E65
        xor_ si,si                                      ; 0E69
        lea di,[bx+0x70]                                ; 0E6B

L1_0E6E:
        test byte [di],0x4                              ; 0E6E
        jz short L1_0E85                                ; 0E71
        mov al,[di+0x5]                                 ; 0E73
        sub_ ah,ah                                      ; 0E76
        cmp ax,[bp+0x8]                                 ; 0E78
        jnz short L1_0E85                               ; 0E7B
        push word [bp+0x6]                              ; 0E7D
        push si                                         ; 0E80
        push cs                                         ; 0E81
        call voice_off                                  ; 0E82

L1_0E85:
        add di,byte +0x21                               ; 0E85
        inc si                                          ; 0E88
        cmp si,byte +0x12                               ; 0E89
        jl short L1_0E6E                                ; 0E8C

L1_0E8E:
        pop si                                          ; 0E8E
        pop di                                          ; 0E8F
        mov_ sp,bp                                      ; 0E90
        pop bp                                          ; 0E92
        retf                                            ; 0E93

; MIDI pitch bend: rewrites the pitch of every active voice of the channel
pitch_bend:
        push bp                                         ; 0E94
        mov_ bp,sp                                      ; 0E95
        sub sp,byte +0x1c                               ; 0E97
        push di                                         ; 0E9A
        push si                                         ; 0E9B
        mov di,[bp+0x8]                                 ; 0E9C
        mov byte [bp-0x18],0x10                         ; 0E9F
        mov byte [bp-0x17],0x20                         ; 0EA3
        mov byte [bp-0x16],0x40                         ; 0EA7
        mov byte [bp-0x15],0x80                         ; 0EAB
        mov ax,[bp+0x4]                                 ; 0EAF
        mov bl,[bp+0x6]                                 ; 0EB2
        sub_ bh,bh                                      ; 0EB5
        add_ bx,bx                                      ; 0EB7
        mov [bx+di+0x50],ax                             ; 0EB9
        xor_ ax,ax                                      ; 0EBC
        mov [bp-0x10],ax                                ; 0EBE
        mov [bp-0xe],ax                                 ; 0EC1
        lea cx,[di+0x75]                                ; 0EC4
        mov word [bp-0x12],0x12                         ; 0EC7

L1_0ECC:
        mov_ bx,cx                                      ; 0ECC
        mov al,[bx]                                     ; 0ECE
        cmp [bp+0x6],al                                 ; 0ED0
        jnz short L1_0EDB                               ; 0ED3
        test byte [bx-0x5],0x1                          ; 0ED5
        jnz short L1_0EDE                               ; 0ED9

L1_0EDB:
        jmp near L1_0F7C                                ; 0EDB

L1_0EDE:
        xor_ si,si                                      ; 0EDE
        mov [bp-0x8],si                                 ; 0EE0
        mov_ ax,cx                                      ; 0EE3
        add ax,strict word 0x3                          ; 0EE5
        mov [bp-0xa],ax                                 ; 0EE8
        mov [bp-0xc],cx                                 ; 0EEB
        mov di,[bp+0x8]                                 ; 0EEE

L1_0EF1:
        mov bx,[bp-0xc]                                 ; 0EF1
        mov al,[bx+0x13]                                ; 0EF4
        test [bp+si-0x18],al                            ; 0EF7
        jnz short L1_0F68                               ; 0EFA
        mov bx,[bp-0xa]                                 ; 0EFC
        push word [bx]                                  ; 0EFF
        push word [bp+0x4]                              ; 0F01
        mov bl,[bp+0x6]                                 ; 0F04
        sub_ bh,bh                                      ; 0F07
        mov al,[bx+di+0x30]                             ; 0F09
        sub_ ah,ah                                      ; 0F0C
        push ax                                         ; 0F0E
%if ESFM_FIX
        push word [bp+0x6]
        push di
        call fix_calc_pitch                             ; esfmgm.asm
%else
        call calc_pitch                                 ; 0F0F
%endif
        mov [bp-0x6],ax                                 ; 0F12
        mov word [bp-0x4],0x0                           ; 0F15
        push word [bp-0x4]                              ; 0F1A
        push ax                                         ; 0F1D
        mov bx,[bp-0xe]                                 ; 0F1E
        add_ bx,si                                      ; 0F21
        mov al,[bx+di+0x89]                             ; 0F23
        and al,0x1c                                     ; 0F27
        shr al,1                                        ; 0F29
        shr al,1                                        ; 0F2B
        push ax                                         ; 0F2D
        mov [bp-0x1a],bx                                ; 0F2E
        call calc_block_fnum                            ; 0F31
        mov [bp-0x2],ax                                 ; 0F34
        push di                                         ; 0F37
        mov ax,[bp-0x8]                                 ; 0F38
        add ax,[bp-0x10]                                ; 0F3B
        mov_ cx,ax                                      ; 0F3E
        add ax,strict word 0x5                          ; 0F40
        push ax                                         ; 0F43
        mov bx,[bp-0x1a]                                ; 0F44
        mov al,[bx+di+0x89]                             ; 0F47
        and al,0xe0                                     ; 0F4B
        or al,[bp-0x1]                                  ; 0F4D
        push ax                                         ; 0F50
        mov [bp-0x1c],cx                                ; 0F51
        push cs                                         ; 0F54
        call fm_write                                   ; 0F55
        push di                                         ; 0F58
        mov ax,[bp-0x1c]                                ; 0F59
        add ax,strict word 0x4                          ; 0F5C
        push ax                                         ; 0F5F
        mov al,[bp-0x2]                                 ; 0F60
        push ax                                         ; 0F63
        push cs                                         ; 0F64
        call fm_write                                   ; 0F65

L1_0F68:
        add word [bp-0xa],byte +0x4                     ; 0F68
        add word [bp-0x8],byte +0x8                     ; 0F6C
        inc si                                          ; 0F70
        cmp si,byte +0x4                                ; 0F71
        jnc short L1_0F79                               ; 0F74
        jmp near L1_0EF1                                ; 0F76

L1_0F79:
        mov cx,[bp-0xc]                                 ; 0F79

L1_0F7C:
        add word [bp-0x10],byte +0x20                   ; 0F7C
        add word [bp-0xe],byte +0x21                    ; 0F80
        add cx,byte +0x21                               ; 0F84
        dec word [bp-0x12]                              ; 0F87
        jz short L1_0F8F                                ; 0F8A
        jmp near L1_0ECC                                ; 0F8C

L1_0F8F:
        pop si                                          ; 0F8F
        pop di                                          ; 0F90
        mov_ sp,bp                                      ; 0F91
        pop bp                                          ; 0F93
        ret 0x6                                         ; 0F94
        db 0x90                                         ; 0F97

; operator attenuation from velocity, volume, expression and pan
calc_level:
        push bp                                         ; 0F98
        mov_ bp,sp                                      ; 0F99
        sub sp,byte +0x6                                ; 0F9B
        push di                                         ; 0F9E
        push si                                         ; 0F9F
        mov di,[bp+0xa]                                 ; 0FA0
        mov bl,[bp+0x4]                                 ; 0FA3
        sub_ bh,bh                                      ; 0FA6
        add_ bx,di                                      ; 0FA8
        cmp byte [bx+0x2e2],0x0                         ; 0FAA
        jnz short L1_0FB6                               ; 0FAF
        mov al,0x3f                                     ; 0FB1
        jmp near L1_105F                                ; 0FB3

L1_0FB6:
        mov ax,[bp+0x6]                                 ; 0FB6
        or_ ax,ax                                       ; 0FB9
        jz short L1_0FCB                                ; 0FBB
        dec ax                                          ; 0FBD
        jz short L1_0FCF                                ; 0FBE
        dec ax                                          ; 0FC0
        jz short L1_0FD3                                ; 0FC1
        dec ax                                          ; 0FC3
        jz short L1_0FF1                                ; 0FC4
        mov si,[bp-0x2]                                 ; 0FC6
        jmp short L1_1043                               ; 0FC9

L1_0FCB:
        xor_ si,si                                      ; 0FCB
        jmp short L1_1043                               ; 0FCD

L1_0FCF:
        mov cl,0x4                                      ; 0FCF
        jmp short L1_0FD5                               ; 0FD1

L1_0FD3:
        mov cl,0x3                                      ; 0FD3

L1_0FD5:
        mov si,0x7f                                     ; 0FD5
        mov al,[bx+0x2f2]                               ; 0FD8
        mov dl,[bx+0x2e2]                               ; 0FDC
        sub_ dh,dh                                      ; 0FE0
        sub_ si,dx                                      ; 0FE2
        sub_ ah,ah                                      ; 0FE4
        sub ax,strict word 0x7f                         ; 0FE6
        neg ax                                          ; 0FE9
        sar ax,cl                                       ; 0FEB
        sar si,cl                                       ; 0FED
        jmp short L1_1041                               ; 0FEF

L1_0FF1:
        cmp byte [bx+0x2f2],0x40                        ; 0FF1
        jc short L1_1009                                ; 0FF6
        sub_ ah,ah                                      ; 0FF8
        mov al,[bx+0x2f2]                               ; 0FFA
        mov si,0x7f                                     ; 0FFE
        sub_ si,ax                                      ; 1001
        sar si,1                                        ; 1003
        sar si,1                                        ; 1005
        jmp short L1_1019                               ; 1007

L1_1009:
        sub_ ah,ah                                      ; 1009
        mov al,[bx+0x2f2]                               ; 100B
        mov si,0x3f                                     ; 100F
        sub_ si,ax                                      ; 1012
        sar si,1                                        ; 1014
        add si,byte +0x10                               ; 1016

L1_1019:
        cmp byte [bx+0x2e2],0x40                        ; 1019
        jc short L1_1031                                ; 101E
        sub_ ah,ah                                      ; 1020
        mov al,[bx+0x2e2]                               ; 1022
        sub ax,strict word 0x7f                         ; 1026
        neg ax                                          ; 1029
        sar ax,1                                        ; 102B
        sar ax,1                                        ; 102D
        jmp short L1_1041                               ; 102F

L1_1031:
        sub_ ah,ah                                      ; 1031
        mov al,[bx+0x2e2]                               ; 1033
        sub ax,strict word 0x3f                         ; 1037
        neg ax                                          ; 103A
        sar ax,1                                        ; 103C
        add ax,strict word 0x10                         ; 103E

L1_1041:
        add_ si,ax                                      ; 1041

L1_1043:
%if ESFM_FIX
        call fix_master                                 ; esfmgm.asm
%endif
        mov al,[bp+0x8]                                 ; 1043
        and al,0x3f                                     ; 1046
        mov_ cx,si                                      ; 1048
        add_ al,cl                                      ; 104A
        mov [bp-0x3],al                                 ; 104C
        cmp al,0x3f                                     ; 104F
        jna short L1_1057                               ; 1051
        mov byte [bp-0x3],0x3f                          ; 1053

L1_1057:
        mov al,[bp+0x8]                                 ; 1057
        and al,0xc0                                     ; 105A
        or al,[bp-0x3]                                  ; 105C

L1_105F:
        pop si                                          ; 105F
        pop di                                          ; 1060
        mov_ sp,bp                                      ; 1061
        pop bp                                          ; 1063
        ret 0x8                                         ; 1064
        db 0x90                                         ; 1067

; volume or expression changed: rewrite the levels of the channel's active voices
update_volume:
        push bp                                         ; 1068
        mov_ bp,sp                                      ; 1069
        sub sp,byte +0xa                                ; 106B
        push di                                         ; 106E
        push si                                         ; 106F
        xor_ di,di                                      ; 1070
        mov [bp-0xa],di                                 ; 1072
        mov cx,[bp+0x6]                                 ; 1075
        add cx,byte +0x75                               ; 1078

L1_107B:
        mov_ bx,cx                                      ; 107B
        test byte [bx-0x5],0x1                          ; 107D
        jz short L1_10F3                                ; 1081
        mov al,[bx]                                     ; 1083
        cmp [bp+0x4],al                                 ; 1085
        jz short L1_1090                                ; 1088
        cmp byte [bp+0x4],0xff                          ; 108A
        jnz short L1_10F3                               ; 108E

L1_1090:
        mov [bp-0x8],di                                 ; 1090
        mov al,[bx]                                     ; 1093
        mov [bp-0x2],al                                 ; 1095
        xor_ si,si                                      ; 1098
        mov byte [bp-0x1],0x0                           ; 109A
        mov [bp-0x6],cx                                 ; 109E
        mov di,[bp+0x6]                                 ; 10A1

L1_10A4:
        mov bx,[bp-0x6]                                 ; 10A4
        mov cl,[bp-0x1]                                 ; 10A7
        mov al,[bx+0x2]                                 ; 10AA
        shr al,cl                                       ; 10AD
        and ax,strict word 0x3                          ; 10AF
        mov [bp-0x4],ax                                 ; 10B2
        push di                                         ; 10B5
        mov cl,0x3                                      ; 10B6
        mov ax,[bp-0x8]                                 ; 10B8
        add_ ax,ax                                      ; 10BB
        add_ ax,ax                                      ; 10BD
        add_ ax,si                                      ; 10BF
        shl ax,cl                                       ; 10C1
        inc ax                                          ; 10C3
        push ax                                         ; 10C4
        push di                                         ; 10C5
        mov bx,[bp-0xa]                                 ; 10C6
        add_ bx,si                                      ; 10C9
        mov al,[bx+di+0x8d]                             ; 10CB
        push ax                                         ; 10CF
        push word [bp-0x4]                              ; 10D0
        mov al,[bp-0x2]                                 ; 10D3
        push ax                                         ; 10D6
        call calc_level                                 ; 10D7
        push ax                                         ; 10DA
        push cs                                         ; 10DB
        call fm_write                                   ; 10DC
        mov al,[bp-0x1]                                 ; 10DF
        add al,0x2                                      ; 10E2
        mov [bp-0x1],al                                 ; 10E4
        inc si                                          ; 10E7
        cmp si,byte +0x4                                ; 10E8
        jl short L1_10A4                                ; 10EB
        mov cx,[bp-0x6]                                 ; 10ED
        mov di,[bp-0x8]                                 ; 10F0

L1_10F3:
        add word [bp-0xa],byte +0x21                    ; 10F3
        add cx,byte +0x21                               ; 10F7
        inc di                                          ; 10FA
        cmp di,byte +0x12                               ; 10FB
        jnl short L1_1103                               ; 10FE
        jmp near L1_107B                                ; 1100

L1_1103:
        pop si                                          ; 1103
        pop di                                          ; 1104
        mov_ sp,bp                                      ; 1105
        pop bp                                          ; 1107
        ret 0x4                                         ; 1108
        db 0x90                                         ; 110B

; one complete MIDI short message (status, data 1, data 2) for dev
short_msg:
        push bp                                         ; 110C
        mov_ bp,sp                                      ; 110D
        sub sp,byte +0x6                                ; 110F
        push di                                         ; 1112
        push si                                         ; 1113
        mov ax,[bp+0x4]                                 ; 1114
        mov_ cx,ax                                      ; 1117
        mov_ dx,ax                                      ; 1119
        and al,0xf                                      ; 111B
        mov [bp-0x3],al                                 ; 111D
        mov al,[bp+0x6]                                 ; 1120
        and al,0x7f                                     ; 1123
        mov [bp-0x1],al                                 ; 1125
        mov_ cl,ch                                      ; 1128
        and cl,0x7f                                     ; 112A
        mov [bp-0x2],cl                                 ; 112D
        mov_ al,dl                                      ; 1130
        and ax,0xf0                                     ; 1132
        sub ax,0x80                                     ; 1135
        jnz short L1_113D                               ; 1138
        jmp near L1_13EF                                ; 113A

L1_113D:
        sub ax,strict word 0x10                         ; 113D
        jz short L1_115A                                ; 1140
        sub ax,strict word 0x20                         ; 1142
        jz short L1_1178                                ; 1145
        sub ax,strict word 0x10                         ; 1147
        jnz short L1_114F                               ; 114A
        jmp near L1_13C0                                ; 114C

L1_114F:
        sub ax,strict word 0x20                         ; 114F
        jnz short L1_1157                               ; 1152
        jmp near L1_13D2                                ; 1154

L1_1157:
%if ESFM_FIX
        cmp ax,byte -0x10                               ; Dn: channel pressure
        jne ..@fix_no_pressure
        mov al,[bp-0x2]
        push ax
        mov al,[bp-0x3]
        push ax
        push word [bp+0x8]
        call fix_pressure                               ; esfmgm.asm
..@fix_no_pressure:
%endif
        jmp near L1_13FE                                ; 1157

L1_115A:
        cmp byte [bp-0x1],0x0                           ; 115A
        jnz short L1_1163                               ; 115E
        jmp near L1_13EF                                ; 1160

L1_1163:
        push word [bp+0x8]                              ; 1163
        mov al,[bp-0x3]                                 ; 1166
        push ax                                         ; 1169
        mov al,[bp-0x2]                                 ; 116A
        push ax                                         ; 116D
        mov al,[bp-0x1]                                 ; 116E
        push ax                                         ; 1171
        call note_on                                    ; 1172
        jmp near L1_13FE                                ; 1175

L1_1178:
%if ESFM_FIX
        call fix_control                                ; esfmgm.asm
        jnc ..@fix_ess_control
        jmp near L1_13FE
..@fix_ess_control:
%endif
        mov al,[bp-0x2]                                 ; 1178
        sub_ ah,ah                                      ; 117B
        sub ax,strict word 0x6                          ; 117D
        jz short L1_11E7                                ; 1180
        dec ax                                          ; 1182
        jnz short L1_1188                               ; 1183
        jmp near L1_120E                                ; 1185

L1_1188:
        dec ax                                          ; 1188
        jz short L1_118F                                ; 1189
        dec ax                                          ; 118B
        dec ax                                          ; 118C
        jnz short L1_1192                               ; 118D

L1_118F:
        jmp near L1_123F                                ; 118F

L1_1192:
        dec ax                                          ; 1192
        jnz short L1_1198                               ; 1193
        jmp near L1_1281                                ; 1195

L1_1198:
        sub ax,strict word 0x35                         ; 1198
        jnz short L1_11A0                               ; 119B
        jmp near L1_1294                                ; 119D

L1_11A0:
        sub ax,strict word 0x22                         ; 11A0
        jnz short L1_11A8                               ; 11A3
        jmp near L1_12AB                                ; 11A5

L1_11A8:
        dec ax                                          ; 11A8
        jnz short L1_11AE                               ; 11A9
        jmp near L1_12BC                                ; 11AB

L1_11AE:
        dec ax                                          ; 11AE
        jnz short L1_11B4                               ; 11AF
        jmp near L1_12CD                                ; 11B1

L1_11B4:
        dec ax                                          ; 11B4
        jnz short L1_11BA                               ; 11B5
        jmp near L1_12E4                                ; 11B7

L1_11BA:
        sub ax,strict word 0x13                         ; 11BA
        jnz short L1_11C2                               ; 11BD
        jmp near L1_12FB                                ; 11BF

L1_11C2:
        dec ax                                          ; 11C2
        jnz short L1_11C8                               ; 11C3
        jmp near L1_1324                                ; 11C5

L1_11C8:
        dec ax                                          ; 11C8
        dec ax                                          ; 11C9
        jl short L1_11CE                                ; 11CA
        jno short L1_11D1                               ; 11CC

L1_11CE:
        jmp near L1_13FE                                ; 11CE

L1_11D1:
        dec ax                                          ; 11D1
        dec ax                                          ; 11D2
        jg short L1_11D8                                ; 11D3
        jmp near L1_1393                                ; 11D5

L1_11D8:
        dec ax                                          ; 11D8
        jnl short L1_11DE                               ; 11D9
        jmp near L1_13FE                                ; 11DB

L1_11DE:
        dec ax                                          ; 11DE
        jg short L1_11E4                                ; 11DF
        jmp near L1_12FB                                ; 11E1

L1_11E4:
        jmp near L1_13FE                                ; 11E4

L1_11E7:
        mov si,[bp+0x8]                                 ; 11E7
        mov bl,[bp-0x3]                                 ; 11EA
        sub_ bh,bh                                      ; 11ED
        mov [bp-0x6],bx                                 ; 11EF
        add_ bx,si                                      ; 11F2
        mov al,[bx+0x40]                                ; 11F4
        and al,0x6                                      ; 11F7
        cmp al,0x6                                      ; 11F9
        jz short L1_1200                                ; 11FB
        jmp near L1_13FE                                ; 11FD

L1_1200:
        mov al,[bp-0x1]                                 ; 1200
        mov bx,[bp-0x6]                                 ; 1203
        add_ bx,si                                      ; 1206
        mov [bx+0x30],al                                ; 1208
        jmp near L1_13FE                                ; 120B

L1_120E:
        mov si,[bp+0x8]                                 ; 120E
        mov bl,[bp-0x1]                                 ; 1211
        and bl,0x7c                                     ; 1214
        shr bl,1                                        ; 1217
        shr bl,1                                        ; 1219
        sub_ bh,bh                                      ; 121B
        db 0x2E, 0x8A, 0x87, 0x62, 0x00                 ; 121D mov al,[cs:bx+0x62]
        mov bl,[bp-0x3]                                 ; 1222
        add_ bx,si                                      ; 1225
        mov [bx+0x2c2],al                               ; 1227
        mov al,[bp-0x1]                                 ; 122B
        and al,0x7f                                     ; 122E
        mov [bx+0x2e2],al                               ; 1230

L1_1234:
        push si                                         ; 1234
        mov al,[bp-0x3]                                 ; 1235
        push ax                                         ; 1238
        call update_volume                              ; 1239
        jmp near L1_13FE                                ; 123C

L1_123F:
        cmp byte [bp-0x1],0x50                          ; 123F
        jna short L1_1257                               ; 1243
        mov si,[bp-0x3]                                 ; 1245
        mov bx,[bp+0x8]                                 ; 1248
        and si,0xff                                     ; 124B
        mov byte [bx+si+0x2d2],0x20                     ; 124F
        jmp near L1_13FE                                ; 1254

L1_1257:
        cmp byte [bp-0x1],0x30                          ; 1257
        jnc short L1_126F                               ; 125B
        mov si,[bp-0x3]                                 ; 125D
        mov bx,[bp+0x8]                                 ; 1260
        and si,0xff                                     ; 1263
        mov byte [bx+si+0x2d2],0x10                     ; 1267
        jmp near L1_13FE                                ; 126C

L1_126F:
        mov si,[bp-0x3]                                 ; 126F
        mov bx,[bp+0x8]                                 ; 1272
        and si,0xff                                     ; 1275
        mov byte [bx+si+0x2d2],0x30                     ; 1279
        jmp near L1_13FE                                ; 127E

L1_1281:
        mov si,[bp+0x8]                                 ; 1281
        mov al,[bp-0x1]                                 ; 1284
        and al,0x7f                                     ; 1287
        mov bl,[bp-0x3]                                 ; 1289
        sub_ bh,bh                                      ; 128C
        mov [bx+si+0x2f2],al                            ; 128E
        jmp short L1_1234                               ; 1292

L1_1294:
        mov al,[bp-0x1]                                 ; 1294
        sub_ ah,ah                                      ; 1297
        push ax                                         ; 1299
        mov al,[bp-0x3]                                 ; 129A
        push ax                                         ; 129D
        push word [bp+0x8]                              ; 129E
        push cs                                         ; 12A1
        call sustain                                    ; 12A2
        add sp,byte +0x6                                ; 12A5
        jmp near L1_13FE                                ; 12A8

L1_12AB:
        mov si,[bp-0x3]                                 ; 12AB
        mov bx,[bp+0x8]                                 ; 12AE
        and si,0xff                                     ; 12B1
        and byte [bx+si+0x40],0xfd                      ; 12B5
        jmp near L1_13FE                                ; 12B9

L1_12BC:
        mov si,[bp-0x3]                                 ; 12BC
        mov bx,[bp+0x8]                                 ; 12BF
        and si,0xff                                     ; 12C2
        and byte [bx+si+0x40],0xfb                      ; 12C6
        jmp near L1_13FE                                ; 12CA

L1_12CD:
        cmp byte [bp-0x1],0x0                           ; 12CD
        jnz short L1_12AB                               ; 12D1
        mov si,[bp-0x3]                                 ; 12D3
        mov bx,[bp+0x8]                                 ; 12D6
        and si,0xff                                     ; 12D9
        or byte [bx+si+0x40],0x2                        ; 12DD
        jmp near L1_13FE                                ; 12E1

L1_12E4:
        cmp byte [bp-0x1],0x0                           ; 12E4
        jnz short L1_12BC                               ; 12E8
        mov si,[bp-0x3]                                 ; 12EA
        mov bx,[bp+0x8]                                 ; 12ED
        and si,0xff                                     ; 12F0
        or byte [bx+si+0x40],0x4                        ; 12F4
        jmp near L1_13FE                                ; 12F8

L1_12FB:
        xor_ si,si                                      ; 12FB
        mov di,[bp+0x8]                                 ; 12FD
        add di,byte +0x70                               ; 1300

L1_1303:
        test byte [di],0x1                              ; 1303
        jz short L1_1318                                ; 1306
        mov al,[di+0x5]                                 ; 1308
        cmp [bp-0x3],al                                 ; 130B
        jnz short L1_1318                               ; 130E
        push word [bp+0x8]                              ; 1310
        push si                                         ; 1313
        push cs                                         ; 1314
        call voice_off                                  ; 1315

L1_1318:
        add di,byte +0x21                               ; 1318
        inc si                                          ; 131B
        cmp si,byte +0x12                               ; 131C
        jl short L1_1303                                ; 131F
        jmp near L1_13FE                                ; 1321

L1_1324:
        mov si,[bp+0x8]                                 ; 1324
        mov bl,[bp-0x3]                                 ; 1327
        sub_ bh,bh                                      ; 132A
        mov [bp-0x6],bx                                 ; 132C
        add_ bx,si                                      ; 132F
        test byte [bx+0x40],0x1                         ; 1331
        jz short L1_1367                                ; 1335
        xor_ di,di                                      ; 1337
        lea cx,[si+0x70]                                ; 1339
        mov [bp-0x2],cx                                 ; 133C
        mov_ si,cx                                      ; 133F

L1_1341:
        test byte [si],0x1                              ; 1341
        jz short L1_135B                                ; 1344
        mov al,[si+0x5]                                 ; 1346
        cmp [bp-0x3],al                                 ; 1349
        jnz short L1_135B                               ; 134C
        test byte [si],0x4                              ; 134E
        jz short L1_135B                                ; 1351
        push word [bp+0x8]                              ; 1353
        push di                                         ; 1356
        push cs                                         ; 1357
        call voice_off                                  ; 1358

L1_135B:
        add si,byte +0x21                               ; 135B
        inc di                                          ; 135E
        cmp di,byte +0x12                               ; 135F
        jl short L1_1341                                ; 1362
        mov si,[bp+0x8]                                 ; 1364

L1_1367:
        mov bx,[bp-0x6]                                 ; 1367
        add_ bx,si                                      ; 136A
        mov byte [bx+0x2e2],0x64                        ; 136C
        mov byte [bx+0x2f2],0x7f                        ; 1371
        and byte [bx+0x40],0xfe                         ; 1376
        mov_ ax,bx                                      ; 137A
        mov bx,[bp-0x6]                                 ; 137C
        add_ bx,bx                                      ; 137F
        mov word [bx+si+0x50],0x2000                    ; 1381
        mov_ bx,ax                                      ; 1386
        mov byte [bx+0x2d2],0x30                        ; 1388
        mov byte [bx+0x30],0x2                          ; 138D
        jmp short L1_13FE                               ; 1391

L1_1393:
        xor_ di,di                                      ; 1393
        mov si,[bp+0x8]                                 ; 1395
        add si,byte +0x70                               ; 1398

L1_139B:
        test byte [si],0x1                              ; 139B
        jz short L1_13B5                                ; 139E
        mov al,[si+0x5]                                 ; 13A0
        cmp [bp-0x3],al                                 ; 13A3
        jnz short L1_13B5                               ; 13A6
        test byte [si],0x4                              ; 13A8
        jnz short L1_13B5                               ; 13AB
        push word [bp+0x8]                              ; 13AD
        push di                                         ; 13B0
        push cs                                         ; 13B1
        call voice_off                                  ; 13B2

L1_13B5:
        add si,byte +0x21                               ; 13B5
        inc di                                          ; 13B8
        cmp di,byte +0x12                               ; 13B9
        jl short L1_139B                                ; 13BC
        jmp short L1_13FE                               ; 13BE

L1_13C0:
        mov al,[bp-0x2]                                 ; 13C0
        mov si,[bp-0x3]                                 ; 13C3
        mov bx,[bp+0x8]                                 ; 13C6
        and si,0xff                                     ; 13C9
        mov [bx+si+0x20],al                             ; 13CD
%if ESFM_FIX
        ; a new program lets go of the channel's sustain pedal
        push si
        push bx
        call fix_program
%endif
        jmp short L1_13FE                               ; 13D0

L1_13D2:
        push word [bp+0x8]                              ; 13D2
        mov al,[bp-0x3]                                 ; 13D5
        push ax                                         ; 13D8
        mov cl,0x7                                      ; 13D9
        mov al,[bp-0x1]                                 ; 13DB
        sub_ ah,ah                                      ; 13DE
        shl ax,cl                                       ; 13E0
        mov cl,[bp-0x2]                                 ; 13E2
        sub_ ch,ch                                      ; 13E5
        or_ ax,cx                                       ; 13E7
        push ax                                         ; 13E9
        call pitch_bend                                 ; 13EA
        jmp short L1_13FE                               ; 13ED

L1_13EF:
        push word [bp+0x8]                              ; 13EF
        mov al,[bp-0x3]                                 ; 13F2
        push ax                                         ; 13F5
        mov al,[bp-0x2]                                 ; 13F6
        push ax                                         ; 13F9
        push cs                                         ; 13FA
        call note_off                                   ; 13FB

L1_13FE:
        pop si                                          ; 13FE
        pop di                                          ; 13FF
        mov_ sp,bp                                      ; 1400
        pop bp                                          ; 1402
        ret 0x6                                         ; 1403
        db 0x90, 0x90                                   ; 1406

; DriverCallback(dwCallback, flags, hmidi, msg, dwInstance, dw1, dw2) for the client of dev
driver_callback:
        push bp                                         ; 1408
        mov_ bp,sp                                      ; 1409
        push si                                         ; 140B
        mov si,[bp+0x10]                                ; 140C
        cmp word [si+0xc],byte +0x0                     ; 140F
        jz short L1_143A                                ; 1413
        push word [si+0x2]                              ; 1415
        push word [si]                                  ; 1418
        push word [si+0xc]                              ; 141A
        push word [si+0x8]                              ; 141D
        push word [bp+0xe]                              ; 1420
        push word [si+0x6]                              ; 1423
        push word [si+0x4]                              ; 1426
        push word [bp+0xc]                              ; 1429
        push word [bp+0xa]                              ; 142C
        push word [bp+0x8]                              ; 142F
        push word [bp+0x6]                              ; 1432
        callp R1_1436, 0xFFFF, 0x0000                   ; 1435 MMSYSTEM.DriverCallback

L1_143A:
        pop si                                          ; 143A
        mov_ sp,bp                                      ; 143B
        pop bp                                          ; 143D
        retf 0xc                                        ; 143E
        db 0x90                                         ; 1441

; MIDI output driver entry (exported): modMessage(id, msg, dwUser, dw1, dw2)
%if ESFM_FIX
modMessage_orig:
%else
modMessage:
%endif
        push bp                                         ; 1442
        mov_ bp,sp                                      ; 1443
        sub sp,byte +0x1a                               ; 1445
        push di                                         ; 1448
        push si                                         ; 1449
        push ds                                         ; 144A
        movsel ax, R1_144C, 0xFFFF                      ; 144B seg4
        mov ds,ax                                       ; 144E
        sub_ ax,ax                                      ; 1450
        mov [bp-0x12],ax                                ; 1452
        mov [bp-0x14],ax                                ; 1455
        mov [bp-0x2],al                                 ; 1458
        mov [bp-0x15],al                                ; 145B
        mov ax,[bp+0x12]                                ; 145E
        cmp ax,strict word 0x67                         ; 1461
        jnz short L1_1469                               ; 1464
        jmp near L1_18AA                                ; 1466

L1_1469:
        ja short L1_1495                                ; 1469
        dec al                                          ; 146B
        jnz short L1_1472                               ; 146D
        jmp near modm_getnumdevs                        ; 146F

L1_1472:
        dec al                                          ; 1472
        jnz short L1_1479                               ; 1474
        jmp near modm_getdevcaps                        ; 1476

L1_1479:
        dec al                                          ; 1479
        jnz short L1_1480                               ; 147B
        jmp near modm_open                              ; 147D

L1_1480:
        sub al,0x61                                     ; 1480
        jnz short L1_1487                               ; 1482
        jmp near L1_1883                                ; 1484

L1_1487:
        dec al                                          ; 1487
        jnz short L1_148E                               ; 1489
        jmp near L1_1890                                ; 148B

L1_148E:
        dec al                                          ; 148E
        jnz short L1_1495                               ; 1490
        jmp near L1_189D                                ; 1492

L1_1495:
        cmp word [bp+0x14],byte +0x0                    ; 1495
        jz short L1_14A1                                ; 1499

L1_149B:
        mov ax,0x2                                      ; 149B
        jmp near modmsg_ret_ax                          ; 149E

L1_14A1:
        mov di,[bp+0xe]                                 ; 14A1
        mov si,[di+0xe]                                 ; 14A4
        test byte [si+0x30e],0x4                        ; 14A7
        jz short L1_14C0                                ; 14AC
        cmp word [bp+0x12],byte +0x4                    ; 14AE
        jnz short L1_14C0                               ; 14B2
        cmp word [si+0x14],byte +0x0                    ; 14B4
        jz short L1_14C0                                ; 14B8
        push si                                         ; 14BA
        callf dev_resume, R1_14BE, R1_14FA              ; 14BB far seg3

L1_14C0:
        test byte [si+0x30e],0x4                        ; 14C0
        jz short L1_14CD                                ; 14C5

L1_14C7:
        mov ax,0x3                                      ; 14C7
        jmp near modmsg_ret_ax                          ; 14CA

L1_14CD:
        mov ax,[bp+0x12]                                ; 14CD
        sub ax,strict word 0x4                          ; 14D0
        cmp ax,strict word 0x7                          ; 14D3
        ja short L1_14F0                                ; 14D6
        add_ ax,ax                                      ; 14D8
        xchg ax,bx                                      ; 14DA
        jmp [cs:bx+jt_modmsg]                           ; 14DB

; MODM_CLOSE .. MODM_SETVOLUME
jt_modmsg:
        dw modm_close                                   ; 14E0
        dw L1_14F0                                      ; 14E2
        dw L1_14F0                                      ; 14E4
        dw modm_data                                    ; 14E6
        dw modm_longdata                                ; 14E8
        dw modm_reset                                   ; 14EA
        dw modm_getvolume                               ; 14EC
        dw modm_setvolume                               ; 14EE

L1_14F0:
        mov ax,0x8                                      ; 14F0
        jmp near modmsg_ret_ax                          ; 14F3

modm_close:
        push di                                         ; 14F6
        callf mod_close, R1_14FA, 0xFFFF                ; 14F7 far seg3
        mov word [si+0x16],0x0                          ; 14FC

L1_1501:
        xor_ ax,ax                                      ; 1501
        jmp near modmsg_ret_ax                          ; 1503

; MODM_DATA: refused with MIDIERR_NOTREADY (43h) while busy
modm_data:
        inc word [busy]                                 ; 1506
        cmp word [busy],byte +0x1                       ; 150A
        jna short L1_151B                               ; 150F

L1_1511:
        dec word [busy]                                 ; 1511
        mov ax,0x43                                     ; 1515
        jmp near modmsg_ret_ax                          ; 1518

L1_151B:
        test byte [bp+0xa],0x80                         ; 151B
        jz short L1_1529                                ; 151F
        mov al,[bp+0xa]                                 ; 1521
        mov [running_status],al                         ; 1524
        jmp short L1_1544                               ; 1527

L1_1529:
        mov ax,[bp+0xa]                                 ; 1529
        mov dx,[bp+0xc]                                 ; 152C
        mov cl,0x8                                      ; 152F
        callf L1_1DC6, R1_1534, R1_169F                 ; 1531 far seg1
        mov cl,[running_status]                         ; 1536
        sub_ ch,ch                                      ; 153A
        or_ ax,cx                                       ; 153C
        mov [bp+0xa],ax                                 ; 153E
        mov [bp+0xc],dx                                 ; 1541

L1_1544:
        push si                                         ; 1544
        push word [bp+0xc]                              ; 1545
        push word [bp+0xa]                              ; 1548
        call short_msg                                  ; 154B

L1_154E:
        dec word [busy]                                 ; 154E
        jmp short L1_1501                               ; 1552

; MODM_LONGDATA: parse the buffer, refused while busy
modm_longdata:
        mov [bp-0x18],di                                ; 1554
        inc word [busy]                                 ; 1557
        cmp word [busy],byte +0x1                       ; 155B
        ja short L1_1511                                ; 1560
        mov cx,[bp+0xc]                                 ; 1562
        mov bx,[bp+0xa]                                 ; 1565
        mov es,cx                                       ; 1568
        mov_ di,bx                                      ; 156A
        mov [bp-0xc],es                                 ; 156C
        test byte [es:bx+0x10],0x2                      ; 156F
        jnz short L1_1580                               ; 1574
        dec word [busy]                                 ; 1576
        mov ax,0x40                                     ; 157A
        jmp near modmsg_ret_ax                          ; 157D

L1_1580:
        mov ax,[es:di]                                  ; 1580
        mov dx,[es:di+0x2]                              ; 1583
        mov_ cx,ax                                      ; 1587
        mov [bp-0x8],dx                                 ; 1589
        sub_ bx,bx                                      ; 158C
        mov [bp-0x4],bx                                 ; 158E
        mov [bp-0x6],bx                                 ; 1591
        mov_ bx,ax                                      ; 1594
        mov es,dx                                       ; 1596
        mov al,[es:bx]                                  ; 1598
        mov [bp-0x1],al                                 ; 159B
        mov [bp-0xe],di                                 ; 159E
        mov [bp-0x10],si                                ; 15A1
        mov [bp-0xa],cx                                 ; 15A4
        mov_ di,cx                                      ; 15A7

L1_15A9:
        cmp byte [bp-0x1],0xf8                          ; 15A9
        jc short L1_15BF                                ; 15AD
        push si                                         ; 15AF
        mov al,[bp-0x1]                                 ; 15B0
        sub_ ah,ah                                      ; 15B3
        sub_ dx,dx                                      ; 15B5

L1_15B7:
        push dx                                         ; 15B7
        push ax                                         ; 15B8
        call short_msg                                  ; 15B9
        jmp near L1_16E0                                ; 15BC

L1_15BF:
        cmp byte [bp-0x1],0xf0                          ; 15BF
        jc short L1_1626                                ; 15C3
        sub_ ax,ax                                      ; 15C5
        mov [bp-0x12],ax                                ; 15C7
        mov [bp-0x14],ax                                ; 15CA
        mov [running_status],al                         ; 15CD
        mov [bp-0x2],al                                 ; 15D0
        mov al,[bp-0x1]                                 ; 15D3
        mov [bp-0x1a],ax                                ; 15D6
        sub ax,0xf1                                     ; 15D9
        cmp ax,strict word 0x5                          ; 15DC
        jna short L1_15E4                               ; 15DF
        jmp near L1_16E0                                ; 15E1

L1_15E4:
        add_ ax,ax                                      ; 15E4
        xchg ax,bx                                      ; 15E6
        jmp [cs:bx+jt_syscommon]                        ; 15E7

; F1h .. F6h in long messages
jt_syscommon:
        dw L1_15F8                                      ; 15EC
        dw L1_160A                                      ; 15EE
        dw L1_15F8                                      ; 15F0
        dw L1_161F                                      ; 15F2
        dw L1_161F                                      ; 15F4
        dw L1_161F                                      ; 15F6

L1_15F8:
        mov ax,[bp-0x1a]                                ; 15F8
        cwd                                             ; 15FB
        mov [bp-0x14],ax                                ; 15FC
        mov [bp-0x12],dx                                ; 15FF

L1_1602:
        mov al,0x1                                      ; 1602
        mov [bp-0x15],al                                ; 1604
        jmp near L1_16DD                                ; 1607

L1_160A:
        mov ax,[bp-0x1a]                                ; 160A
        cwd                                             ; 160D
        mov [bp-0x14],ax                                ; 160E
        mov [bp-0x12],dx                                ; 1611

L1_1614:
        mov byte [bp-0x15],0x2                          ; 1614

L1_1618:
        mov byte [bp-0x2],0x1                           ; 1618
        jmp near L1_16E0                                ; 161C

L1_161F:
        push si                                         ; 161F
        mov ax,[bp-0x1a]                                ; 1620
        cwd                                             ; 1623
        jmp short L1_15B7                               ; 1624

L1_1626:
        cmp byte [bp-0x1],0x80                          ; 1626
        jc short L1_1686                                ; 162A
        mov al,[bp-0x1]                                 ; 162C
        mov [running_status],al                         ; 162F
        sub_ cx,cx                                      ; 1632
        mov [bp-0x12],cx                                ; 1634
        mov [bp-0x14],cx                                ; 1637
        and ax,0xf0                                     ; 163A
        sub ax,0x80                                     ; 163D
        test al,0xf                                     ; 1640
        jz short L1_1647                                ; 1642
        jmp near L1_16E0                                ; 1644

L1_1647:
        mov cl,0x3                                      ; 1647
        shr ax,cl                                       ; 1649
        cmp ax,strict word 0xc                          ; 164B
        jna short L1_1653                               ; 164E
        jmp near L1_16E0                                ; 1650

L1_1653:
        xchg ax,bx                                      ; 1653
        jmp [cs:bx+jt_status]                           ; 1654

; 80h .. E0h in long messages
jt_status:
        dw L1_1667                                      ; 1659
        dw L1_1667                                      ; 165B
        dw L1_1667                                      ; 165D
        dw L1_1667                                      ; 165F
        dw L1_1676                                      ; 1661
        dw L1_1676                                      ; 1663
        dw L1_1667                                      ; 1665

L1_1667:
        mov al,[bp-0x1]                                 ; 1667
        sub_ ah,ah                                      ; 166A
        mov [bp-0x14],ax                                ; 166C
        mov word [bp-0x12],0x0                          ; 166F
        jmp short L1_1614                               ; 1674

L1_1676:
        mov al,[bp-0x1]                                 ; 1676
        sub_ ah,ah                                      ; 1679
        mov [bp-0x14],ax                                ; 167B
        mov word [bp-0x12],0x0                          ; 167E
        jmp near L1_1602                                ; 1683

L1_1686:
        cmp byte [bp-0x2],0x0                           ; 1686
        jz short L1_16E0                                ; 168A
        mov al,[bp-0x1]                                 ; 168C
        sub_ ah,ah                                      ; 168F
        sub_ dx,dx                                      ; 1691
        mov cl,0x3                                      ; 1693
        mov bl,[bp-0x2]                                 ; 1695
        shl bl,cl                                       ; 1698
        mov_ cl,bl                                      ; 169A
        callf L1_1DC6, R1_169F, R1_0781                 ; 169C far seg1
        or [bp-0x14],ax                                 ; 16A1
        or [bp-0x12],dx                                 ; 16A4
        inc byte [bp-0x2]                               ; 16A7
        dec byte [bp-0x15]                              ; 16AA
        jnz short L1_16E0                               ; 16AD
        push si                                         ; 16AF
        push word [bp-0x12]                             ; 16B0
        push word [bp-0x14]                             ; 16B3
        call short_msg                                  ; 16B6
        cmp byte [running_status],0x0                   ; 16B9
        jz short L1_16D5                                ; 16BE
        mov al,[running_status]                         ; 16C0
        sub_ ah,ah                                      ; 16C3
        sub_ dx,dx                                      ; 16C5
%if ESFM_FIX
        ; running status: a new message with the status alone.  ESS's
        ; code kept the data bytes of the last one and ORed the new ones in
        mov [bp-0x14],ax
        mov [bp-0x12],dx
%else
        or [bp-0x14],ax                                 ; 16C7
%endif
        mov al,[bp-0x2]                                 ; 16CA
        dec al                                          ; 16CD
        mov [bp-0x15],al                                ; 16CF
        jmp near L1_1618                                ; 16D2

L1_16D5:
        sub_ ax,ax                                      ; 16D5
        mov [bp-0x12],ax                                ; 16D7
        mov [bp-0x14],ax                                ; 16DA

L1_16DD:
        mov [bp-0x2],al                                 ; 16DD

L1_16E0:
        add word [bp-0x6],byte +0x1                     ; 16E0
        adc word [bp-0x4],byte +0x0                     ; 16E4
        mov ax,[bp-0x6]                                 ; 16E8
        mov dx,[bp-0x4]                                 ; 16EB
        les bx,[bp-0xe]                                 ; 16EE
        cmp [es:bx+0x6],dx                              ; 16F1
        jc short L1_170C                                ; 16F5
        ja short L1_16FF                                ; 16F7
        cmp [es:bx+0x4],ax                              ; 16F9
        jna short L1_170C                               ; 16FD

L1_16FF:
        inc di                                          ; 16FF
        mov es,[bp-0x8]                                 ; 1700
        mov al,[es:di]                                  ; 1703
        mov [bp-0x1],al                                 ; 1706
        jmp near L1_15A9                                ; 1709

L1_170C:
        or byte [es:bx+0x10],0x1                        ; 170C
        push word [bp-0x18]                             ; 1711
        mov ax,0x3c9                                    ; 1714
        push ax                                         ; 1717
        push word [bp+0xc]                              ; 1718
        push word [bp+0xa]                              ; 171B
        sub_ ax,ax                                      ; 171E
        push ax                                         ; 1720
        push ax                                         ; 1721
        push cs                                         ; 1722
        call driver_callback                            ; 1723
        jmp near L1_154E                                ; 1726

; MODM_RESET: chip_reset, refused while busy
modm_reset:
        inc word [busy]                                 ; 1729
        cmp word [busy],byte +0x1                       ; 172D
        jnz short L1_1743                               ; 1732
        push si                                         ; 1734
        push cs                                         ; 1735
        call chip_reset                                 ; 1736
        sub_ ax,ax                                      ; 1739
        mov [bp+0xc],ax                                 ; 173B
        mov [bp+0xa],ax                                 ; 173E
        jmp short L1_174D                               ; 1741

L1_1743:
        mov word [bp+0xa],0x43                          ; 1743
        mov word [bp+0xc],0x0                           ; 1748

L1_174D:
        dec word [busy]                                 ; 174D
        mov ax,[bp+0xa]                                 ; 1751
        mov dx,[bp+0xc]                                 ; 1754
        jmp near modmsg_ret                             ; 1757

modm_getvolume:
        push word [bp+0x8]                              ; 175A
        push word [bp+0x6]                              ; 175D
        callf find_device, R1_1763, R1_178E             ; 1760 far seg3
        mov_ si,ax                                      ; 1765
        or_ si,ax                                       ; 1767
        jz short L1_1796                                ; 1769
        mov ax,[si+0x302]                               ; 176B
        mov dx,[si+0x304]                               ; 176F
        mov cx,[bp+0xc]                                 ; 1773
        mov bx,[bp+0xa]                                 ; 1776
        mov es,cx                                       ; 1779
        mov [es:bx],ax                                  ; 177B
        mov [es:bx+0x2],dx                              ; 177E
        jmp near L1_1501                                ; 1782

modm_setvolume:
        push word [bp+0x8]                              ; 1785
        push word [bp+0x6]                              ; 1788
        callf find_device, R1_178E, R1_17DF             ; 178B far seg3
        mov_ si,ax                                      ; 1790
        or_ si,ax                                       ; 1792
        jnz short L1_1799                               ; 1794

L1_1796:
        jmp near L1_149B                                ; 1796

L1_1799:
        mov ax,[bp+0xa]                                 ; 1799
        mov dx,[bp+0xc]                                 ; 179C
        mov [si+0x302],ax                               ; 179F
        mov [si+0x304],dx                               ; 17A3
        mov ax,[si+0x30c]                               ; 17A7
        or ax,[si+0x30a]                                ; 17AB
        jnz short L1_17B4                               ; 17AF
        jmp near L1_14F0                                ; 17B1

L1_17B4:
        push word [si+0x308]                            ; 17B4
        push word [si+0x306]                            ; 17B8
        mov dx,0x2                                      ; 17BC
        push dx                                         ; 17BF
        push dx                                         ; 17C0
        push word [si+0xe]                              ; 17C1
        push word [si+0xc]                              ; 17C4
        push word [si+0x304]                            ; 17C7
        push word [si+0x302]                            ; 17CB
        call far [si+0x30a]                             ; 17CF
        jmp near L1_1501                                ; 17D3

modm_getnumdevs:
        push word [bp+0xc]                              ; 17D6
        push word [bp+0xa]                              ; 17D9
        callf find_device, R1_17DF, R1_1812             ; 17DC far seg3
        mov_ si,ax                                      ; 17E1
        or_ si,ax                                       ; 17E3
        jnz short L1_17EF                               ; 17E5
        xor_ ax,ax                                      ; 17E7
        mov dx,0xb                                      ; 17E9
        jmp near modmsg_ret                             ; 17EC

L1_17EF:
        cmp word [si+0x14],byte +0x0                    ; 17EF
        jz short L1_17FD                                ; 17F3
        mov ax,0x1                                      ; 17F5

; return AX (DX = 0)
modmsg_ret_ax:
        xor_ dx,dx                                      ; 17F8
        jmp near modmsg_ret                             ; 17FA

L1_17FD:
        xor_ ax,ax                                      ; 17FD
        cwd                                             ; 17FF
        jmp near modmsg_ret                             ; 1800

modm_getdevcaps:
        cmp word [bp+0x14],byte +0x0                    ; 1803
        jnz short L1_181A                               ; 1807
        push word [bp+0x8]                              ; 1809
        push word [bp+0x6]                              ; 180C
        callf find_device, R1_1812, R1_1830             ; 180F far seg3
        mov_ si,ax                                      ; 1814
        or_ si,ax                                       ; 1816
        jnz short L1_181D                               ; 1818

L1_181A:
        jmp near L1_149B                                ; 181A

L1_181D:
        cmp word [si+0x14],byte +0x0                    ; 181D
        jnz short L1_1826                               ; 1821
        jmp near L1_14C7                                ; 1823

L1_1826:
        push si                                         ; 1826
        push word [bp+0xc]                              ; 1827
        push word [bp+0xa]                              ; 182A
        callf mod_get_devcaps, R1_1830, R1_184E         ; 182D far seg3
        jmp near L1_1501                                ; 1832

modm_open:
        cmp word [bp+0x14],byte +0x0                    ; 1835
        jnz short L1_1856                               ; 1839
        mov cx,[bp+0xc]                                 ; 183B
        mov bx,[bp+0xa]                                 ; 183E
        mov es,cx                                       ; 1841
        push word [es:bx+0xe]                           ; 1843
        push word [es:bx+0xc]                           ; 1847
        callf find_device, R1_184E, R1_187F             ; 184B far seg3
        mov_ si,ax                                      ; 1850
        or_ si,ax                                       ; 1852
        jnz short L1_1859                               ; 1854

L1_1856:
        jmp near L1_149B                                ; 1856

L1_1859:
        cmp word [si+0x14],byte +0x0                    ; 1859
        jz short L1_1866                                ; 185D
        test byte [si+0x30e],0x4                        ; 185F
        jz short L1_1869                                ; 1864

L1_1866:
        jmp near L1_14C7                                ; 1866

L1_1869:
        push si                                         ; 1869
        push word [bp+0x10]                             ; 186A
        push word [bp+0xe]                              ; 186D
        push word [bp+0xc]                              ; 1870
        push word [bp+0xa]                              ; 1873
        push word [bp+0x8]                              ; 1876
        push word [bp+0x6]                              ; 1879
        callf mod_open, R1_187F, R1_188C                ; 187C far seg3
        jmp short modmsg_ret                            ; 1881

L1_1883:
        push word [bp+0x8]                              ; 1883
        push word [bp+0x6]                              ; 1886
        callf dev_add, R1_188C, R1_1899                 ; 1889 far seg3
        jmp short modmsg_ret                            ; 188E

L1_1890:
        push word [bp+0x8]                              ; 1890
        push word [bp+0x6]                              ; 1893
        callf dev_remove, R1_1899, R1_18A6              ; 1896 far seg3
        jmp short modmsg_ret                            ; 189B

L1_189D:
        push word [bp+0x8]                              ; 189D
        push word [bp+0x6]                              ; 18A0
        callf dev_disable, R1_18A6, R1_18E0             ; 18A3 far seg3
        jmp short modmsg_ret                            ; 18A8

L1_18AA:
        mov word [bp-0x6],0x1                           ; 18AA
        mov word [bp-0x4],0x0                           ; 18AF
        push word [bp+0xa]                              ; 18B4
        mov ax,0x804                                    ; 18B7
        push ax                                         ; 18BA
        lea ax,[bp-0x2]                                 ; 18BB
        push ss                                         ; 18BE
        push ax                                         ; 18BF
        lea ax,[bp-0x6]                                 ; 18C0
        push ss                                         ; 18C3
        push ax                                         ; 18C4
        callp R1_18C6, 0xFFFF, 0x0000                   ; 18C5 MMSYSTEM.midiOutMessage
        or_ dx,ax                                       ; 18CA
        jz short L1_18D4                                ; 18CC
        mov ax,0xb                                      ; 18CE
        jmp near modmsg_ret_ax                          ; 18D1

L1_18D4:
        push word [bp+0x8]                              ; 18D4
        push word [bp+0x6]                              ; 18D7
        push word [bp-0x2]                              ; 18DA
        callf dev_enable, R1_18E0, R1_14BE              ; 18DD far seg3

modmsg_ret:
        pop ds                                          ; 18E2
        pop si                                          ; 18E3
        pop di                                          ; 18E4
        mov_ sp,bp                                      ; 18E5
        pop bp                                          ; 18E7
        retf 0x10                                       ; 18E8
        db 0x90, 0x90, 0x90                             ; 18EB

L1_18EE:
        mov ax,ds                                       ; 18EE
        nop                                             ; 18F0
        inc bp                                          ; 18F1
        push bp                                         ; 18F2
        mov_ bp,sp                                      ; 18F3
        push ds                                         ; 18F5
        mov ds,ax                                       ; 18F6
        push word [0x5e]                                ; 18F8
        push word [0x60]                                ; 18FC
        push word [0x62]                                ; 1900
        push word [0x66]                                ; 1904
        push word [0x64]                                ; 1908
        callf LibMain, R1_190F, R1_1763                 ; 190C far seg3
        sub bp,byte +0x2                                ; 1911
        mov_ sp,bp                                      ; 1914
        pop ds                                          ; 1916
        pop bp                                          ; 1917
        dec bp                                          ; 1918
        retf                                            ; 1919
R1_191A: dw 0xFFFF                                      ; 191A KERNEL.__WINFLAGS
        db 'PSQR'                                       ; 191C
        db 0x06, 0xB8                                   ; 1920
R1_1922: dw R1_191A                                     ; 1922 KERNEL.__WINFLAGS
        db 0x0B, 0xC0, 0x79, 0x12, 0x07                 ; 1924
        db 'ZY[X'                                       ; 1929
        db 0x9A                                         ; 192D
R1_192E: dw 0xFFFB, 0x0000                              ; 192E KERNEL.InitTask
        db 0xEB, 0x04, 0x90, 0x33, 0xC0, 0xCB, 0xEB, 0x24, 0x90, 0x90, 0xEB, 0x0E, 0x57, 0x9A ; 1932
R1_1940: dw 0xFFFF, 0x0000                              ; 1940 KERNEL.GetModuleUsage
        db 0x48, 0x74, 0x05, 0x40, 0x83, 0xC4, 0x0A, 0xCB, 0x07 ; 1944
        db 'ZY[X'                                       ; 194D
        db 0xEB, 0x0B, 0x43, 0x44, 0x44, 0x01, 0x00, 0x16, 0x00, 0x1E, 0x00, 0x42, 0x00 ; 1951

; start address: C runtime initialization of the DLL
LibEntry:
        mov ax,ds                                       ; 195E
        nop                                             ; 1960
        inc bp                                          ; 1961
        push bp                                         ; 1962
        mov_ bp,sp                                      ; 1963
        push ds                                         ; 1965
        mov ds,ax                                       ; 1966
        push di                                         ; 1968
        push si                                         ; 1969
        mov [0x5e],di                                   ; 196A
        mov [0x60],ds                                   ; 196E
        mov [0x62],cx                                   ; 1972
        mov [0x64],bx                                   ; 1976
        mov [0x66],si                                   ; 197A
        jcxz L1_198E                                    ; 197E
        push ds                                         ; 1980
        xor_ ax,ax                                      ; 1981
        push ax                                         ; 1983
        push cx                                         ; 1984
        callp R1_1986, 0xFFFF, 0x0000                   ; 1985 KERNEL.LocalInit
        or_ ax,ax                                       ; 198A
        jz short L1_19E6                                ; 198C

L1_198E:
        callp R1_198F, 0xFFFF, 0x0000                   ; 198E KERNEL.GetVersion
        xchg al,ah                                      ; 1993
        mov [0x84],ax                                   ; 1995
        mov ah,0x30                                     ; 1998
        test word [cs:0x191a],0x1                       ; 199A
        jz short L1_19AA                                ; 19A1
        callp R1_19A4, 0xFFFF, 0x0000                   ; 19A3 KERNEL.DOS3Call
        jmp short L1_19AC                               ; 19A8

L1_19AA:
        int 0x21                                        ; 19AA

L1_19AC:
        mov [0x88],ax                                   ; 19AC
        xchg al,ah                                      ; 19AF
        mov [0x86],ax                                   ; 19B1
        test word [cs:0x191a],0x1                       ; 19B4
        jnz short L1_19C2                               ; 19BB
        mov al,0x0                                      ; 19BD
        mov [0x8b],al                                   ; 19BF

L1_19C2:
        callf L1_1A08, R1_19C5, R1_1534                 ; 19C2 far seg1
        callf L1_1B5E, R1_19CA, R1_19C5                 ; 19C7 far seg1
        inc byte [0x68]                                 ; 19CC
        push word [0xa8]                                ; 19D0
        push word [0xa6]                                ; 19D4
        push word [0xa4]                                ; 19D8
        callf L1_18EE, R1_19DF, R1_19CA                 ; 19DC far seg1
        add sp,byte +0x6                                ; 19E1
        pop si                                          ; 19E4
        pop di                                          ; 19E5

L1_19E6:
        sub bp,byte +0x2                                ; 19E6
        mov_ sp,bp                                      ; 19E9
        pop ds                                          ; 19EB
        pop bp                                          ; 19EC
        dec bp                                          ; 19ED
        retf                                            ; 19EE
        db 0x8C, 0xD8, 0x90, 0x45, 0x55, 0x8B, 0xEC, 0x1E, 0x8E, 0xD8, 0xB8, 0x01, 0x00, 0x83, 0xED, 0x02 ; 19EF
        db 0x8B, 0xE5, 0x1F, 0x5D, 0x4D, 0xCA, 0x0A, 0x00, 0x00 ; 19FF

L1_1A08:
        mov ax,ds                                       ; 1A08
        nop                                             ; 1A0A
        inc bp                                          ; 1A0B
        push bp                                         ; 1A0C
        mov_ bp,sp                                      ; 1A0D
        push ds                                         ; 1A0F
        mov ds,ax                                       ; 1A10
        mov cx,[0xc0]                                   ; 1A12
        jcxz L1_1A2C                                    ; 1A16
        xor_ si,si                                      ; 1A18
        mov ax,[0xc2]                                   ; 1A1A
        mov dx,[0xc4]                                   ; 1A1D
        xor_ bx,bx                                      ; 1A21
        call far [0xbe]                                 ; 1A23
        jnc short L1_1A2C                               ; 1A27
        jmp near L1_1D3A                                ; 1A29

L1_1A2C:
        mov si,0xca                                     ; 1A2C
        mov di,0xca                                     ; 1A2F
        call L1_1AD8                                    ; 1A32
        mov si,0xca                                     ; 1A35
        mov di,0xca                                     ; 1A38
        call L1_1AD8                                    ; 1A3B
        mov si,0xca                                     ; 1A3E
        mov di,0xca                                     ; 1A41
        call L1_1AD8                                    ; 1A44
        sub bp,byte +0x2                                ; 1A47
        mov_ sp,bp                                      ; 1A4A
        pop ds                                          ; 1A4C
        pop bp                                          ; 1A4D
        dec bp                                          ; 1A4E
        retf                                            ; 1A4F

L1_1A50:
        mov ax,ds                                       ; 1A50
        nop                                             ; 1A52
        inc bp                                          ; 1A53
        push bp                                         ; 1A54
        mov_ bp,sp                                      ; 1A55
        push ds                                         ; 1A57
        mov ds,ax                                       ; 1A58
        push si                                         ; 1A5A
        push di                                         ; 1A5B
        mov cx,0x100                                    ; 1A5C
        jmp short L1_1A70                               ; 1A5F
        db 0x8C, 0xD8, 0x90, 0x45, 0x55, 0x8B, 0xEC, 0x1E, 0x8E, 0xD8, 0x56, 0x57, 0xB9, 0x01, 0x01 ; 1A61

L1_1A70:
        mov [0xb1],ch                                   ; 1A70
        push cx                                         ; 1A74
        or_ cl,cl                                       ; 1A75
        jnz short L1_1A8B                               ; 1A77
        mov si,0x1aa                                    ; 1A79
        mov di,0x1aa                                    ; 1A7C
        call L1_1AD8                                    ; 1A7F
        mov si,0xca                                     ; 1A82
        mov di,0xca                                     ; 1A85
        call L1_1AD8                                    ; 1A88

L1_1A8B:
        mov si,0xca                                     ; 1A8B
        mov di,0xca                                     ; 1A8E
        call L1_1AD8                                    ; 1A91
        mov si,0xca                                     ; 1A94
        mov di,0xca                                     ; 1A97
        call L1_1AD8                                    ; 1A9A
        call L1_1D92                                    ; 1A9D
        call L1_1D92                                    ; 1AA0
        pop ax                                          ; 1AA3
        pop di                                          ; 1AA4
        pop si                                          ; 1AA5
        sub bp,byte +0x2                                ; 1AA6
        mov_ sp,bp                                      ; 1AA9
        pop ds                                          ; 1AAB
        pop bp                                          ; 1AAC
        dec bp                                          ; 1AAD
        retf                                            ; 1AAE
        db 0x8B, 0x0E, 0xC0, 0x00, 0xE3, 0x07, 0xBB, 0x02, 0x00, 0xFF, 0x1E, 0xBE, 0x00, 0x1E, 0xC5, 0x16 ; 1AAF
        db 0x72, 0x00, 0xB8, 0x00, 0x25, 0x2E, 0xF7, 0x06, 0x1A, 0x19, 0x01, 0x00, 0x74, 0x07, 0x9A ; 1ABF
R1_1ACE: dw R1_19A4, 0x0000                             ; 1ACE KERNEL.DOS3Call
        db 0xEB, 0x02, 0xCD, 0x21, 0x1F, 0xC3           ; 1AD2

L1_1AD8:
        cmp_ si,di                                      ; 1AD8
        jnc short L1_1AEA                               ; 1ADA
        sub di,byte +0x4                                ; 1ADC
        mov ax,[di]                                     ; 1ADF
        or ax,[di+0x2]                                  ; 1AE1
        jz short L1_1AD8                                ; 1AE4
        call far [di]                                   ; 1AE6
        jmp short L1_1AD8                               ; 1AE8

L1_1AEA:
        ret                                             ; 1AEA
        db 0x00                                         ; 1AEB

L1_1AEC:
        mov ax,ds                                       ; 1AEC
        nop                                             ; 1AEE
        inc bp                                          ; 1AEF
        push bp                                         ; 1AF0
        mov_ bp,sp                                      ; 1AF1
        push ds                                         ; 1AF3
        mov ds,ax                                       ; 1AF4
        mov ax,0xfc                                     ; 1AF6
        push ax                                         ; 1AF9
        push cs                                         ; 1AFA
        call L1_1B47                                    ; 1AFB
        mov ax,0xff                                     ; 1AFE
        push ax                                         ; 1B01
        push cs                                         ; 1B02
        call L1_1B47                                    ; 1B03
        sub bp,byte +0x2                                ; 1B06
        mov_ sp,bp                                      ; 1B09
        pop ds                                          ; 1B0B
        pop bp                                          ; 1B0C
        dec bp                                          ; 1B0D
        retf                                            ; 1B0E
        db 0x00                                         ; 1B0F

L1_1B10:
        mov ax,ds                                       ; 1B10
        nop                                             ; 1B12
        inc bp                                          ; 1B13
        push bp                                         ; 1B14
        mov_ bp,sp                                      ; 1B15
        push ds                                         ; 1B17
        mov ds,ax                                       ; 1B18
        push si                                         ; 1B1A
        push di                                         ; 1B1B
        push ds                                         ; 1B1C
        pop es                                          ; 1B1D
        mov dx,[bp+0x6]                                 ; 1B1E
        mov si,0xd2                                     ; 1B21

L1_1B24:
        lodsw                                           ; 1B24
        cmp_ ax,dx                                      ; 1B25
        jz short L1_1B39                                ; 1B27
        inc ax                                          ; 1B29
        xchg ax,si                                      ; 1B2A
        jz short L1_1B39                                ; 1B2B
        xchg ax,di                                      ; 1B2D
        xor_ ax,ax                                      ; 1B2E
        mov cx,0xffff                                   ; 1B30
        repne scasb                                     ; 1B33
        mov_ si,di                                      ; 1B35
        jmp short L1_1B24                               ; 1B37

L1_1B39:
        xchg ax,si                                      ; 1B39
        pop di                                          ; 1B3A
        pop si                                          ; 1B3B
        sub bp,byte +0x2                                ; 1B3C
        mov_ sp,bp                                      ; 1B3F
        pop ds                                          ; 1B41
        pop bp                                          ; 1B42
        dec bp                                          ; 1B43
        retf 0x2                                        ; 1B44

L1_1B47:
        mov ax,ds                                       ; 1B47
        nop                                             ; 1B49
        inc bp                                          ; 1B4A
        push bp                                         ; 1B4B
        mov_ bp,sp                                      ; 1B4C
        push ds                                         ; 1B4E
        mov ds,ax                                       ; 1B4F
        push di                                         ; 1B51
        pop di                                          ; 1B52
        sub bp,byte +0x2                                ; 1B53
        mov_ sp,bp                                      ; 1B56
        pop ds                                          ; 1B58
        pop bp                                          ; 1B59
        dec bp                                          ; 1B5A
        retf 0x2                                        ; 1B5B

L1_1B5E:
        mov ax,ds                                       ; 1B5E
        nop                                             ; 1B60
        inc bp                                          ; 1B61
        push bp                                         ; 1B62
        mov_ bp,sp                                      ; 1B63
        push ds                                         ; 1B65
        mov ds,ax                                       ; 1B66
        push ds                                         ; 1B68
        callp R1_1B6A, 0xFFFF, 0x0000                   ; 1B69 KERNEL.GetDOSEnvironment
        or_ ax,ax                                       ; 1B6E
        jz short L1_1B75                                ; 1B70
        mov dx,0x0                                      ; 1B72

L1_1B75:
        mov_ bx,dx                                      ; 1B75
        mov es,dx                                       ; 1B77
        xor_ ax,ax                                      ; 1B79
        xor_ si,si                                      ; 1B7B
        xor_ di,di                                      ; 1B7D
        mov cx,0xffff                                   ; 1B7F
        or_ bx,bx                                       ; 1B82
        jz short L1_1B94                                ; 1B84
        cmp byte [es:0x0],0x0                           ; 1B86
        jz short L1_1B94                                ; 1B8C

L1_1B8E:
        repne scasb                                     ; 1B8E
        inc si                                          ; 1B90
        scasb                                           ; 1B91
        jnz short L1_1B8E                               ; 1B92

L1_1B94:
        mov_ ax,di                                      ; 1B94
        inc ax                                          ; 1B96
        and al,0xfe                                     ; 1B97
        inc si                                          ; 1B99
        mov_ di,si                                      ; 1B9A
        shl si,1                                        ; 1B9C
        mov cx,0x9                                      ; 1B9E
        call L1_1D40                                    ; 1BA1
        push ax                                         ; 1BA4
        mov_ ax,si                                      ; 1BA5
        call L1_1D40                                    ; 1BA7
        mov [0xa8],ax                                   ; 1BAA
        push es                                         ; 1BAD
        push ds                                         ; 1BAE
        pop es                                          ; 1BAF
        pop ds                                          ; 1BB0
        mov_ cx,di                                      ; 1BB1
        mov_ bx,ax                                      ; 1BB3
        xor_ si,si                                      ; 1BB5
        pop di                                          ; 1BB7
        dec cx                                          ; 1BB8
        jcxz L1_1BC8                                    ; 1BB9

L1_1BBB:
        mov [es:bx],di                                  ; 1BBB
        inc bx                                          ; 1BBE
        inc bx                                          ; 1BBF

L1_1BC0:
        lodsb                                           ; 1BC0
        stosb                                           ; 1BC1
        or_ al,al                                       ; 1BC2
        jnz short L1_1BC0                               ; 1BC4
        loop L1_1BBB                                    ; 1BC6

L1_1BC8:
        mov [es:bx],cx                                  ; 1BC8
        pop ds                                          ; 1BCB
        sub bp,byte +0x2                                ; 1BCC
        mov_ sp,bp                                      ; 1BCF
        pop ds                                          ; 1BD1
        pop bp                                          ; 1BD2
        dec bp                                          ; 1BD3
        retf                                            ; 1BD4
        db 0x00, 0x9A, 0x6E, 0x1D                       ; 1BD5
R1_1BD9: dw R1_19DF                                     ; 1BD9 seg1
        db 0x8E, 0xD8, 0xB8, 0x03, 0x00                 ; 1BDB

L1_1BE0:
        push ax                                         ; 1BE0
        push ax                                         ; 1BE1
        push cs                                         ; 1BE2
        call L1_1AEC                                    ; 1BE3
        push cs                                         ; 1BE6
        call L1_1B47                                    ; 1BE7
        push cs                                         ; 1BEA
        call L1_1B10                                    ; 1BEB
        xor_ bx,bx                                      ; 1BEE
        or_ ax,ax                                       ; 1BF0
        jz short L1_1C11                                ; 1BF2
        mov_ di,ax                                      ; 1BF4
        mov ax,0x9                                      ; 1BF6
        cmp byte [di],0x4d                              ; 1BF9
        jnz short L1_1C01                               ; 1BFC
        mov ax,0xf                                      ; 1BFE

L1_1C01:
        add_ di,ax                                      ; 1C01
        push di                                         ; 1C03
        push ds                                         ; 1C04
        pop es                                          ; 1C05
        mov al,0xd                                      ; 1C06
        mov cx,0x22                                     ; 1C08
        repne scasb                                     ; 1C0B
        mov [di-0x1],bl                                 ; 1C0D
        pop ax                                          ; 1C10

L1_1C11:
        push bx                                         ; 1C11
        push ds                                         ; 1C12
        push ax                                         ; 1C13
        callp R1_1C15, 0xFFFF, 0x0000                   ; 1C14 KERNEL.FatalAppExit
        mov ax,0xff                                     ; 1C19
        push ax                                         ; 1C1C
        callp R1_1C1E, 0xFFFF, 0x0000                   ; 1C1D KERNEL.FatalExit
        push cx                                         ; 1C22
        push di                                         ; 1C23
        test byte [bx+0x2],0x1                          ; 1C24
        jz short L1_1C92                                ; 1C28
        call L1_1D19                                    ; 1C2A
        mov_ di,si                                      ; 1C2D
        mov ax,[si]                                     ; 1C2F
        test al,0x1                                     ; 1C31
        jz short L1_1C38                                ; 1C33
        sub_ cx,ax                                      ; 1C35
        dec cx                                          ; 1C37

L1_1C38:
        inc cx                                          ; 1C38
        inc cx                                          ; 1C39
        mov si,[bx+0x4]                                 ; 1C3A
        or_ si,si                                       ; 1C3D
        jz short L1_1C92                                ; 1C3F
        add_ cx,si                                      ; 1C41
        jnc short L1_1C4E                               ; 1C43
        xor_ ax,ax                                      ; 1C45
        mov dx,0xfff0                                   ; 1C47
        jcxz L1_1C81                                    ; 1C4A
        jmp short L1_1C92                               ; 1C4C

L1_1C4E:
        callf L1_1D6E, R1_1C51, R1_1BD9                 ; 1C4E far seg1
        mov es,ax                                       ; 1C53
        mov ax,[es:0xb6]                                ; 1C55
        cmp ax,0x1000                                   ; 1C59
        jz short L1_1C74                                ; 1C5C
        mov dx,0x8000                                   ; 1C5E

L1_1C61:
        cmp_ dx,ax                                      ; 1C61
        jc short L1_1C6B                                ; 1C63
        shr dx,1                                        ; 1C65
        jnz short L1_1C61                               ; 1C67
        jmp short L1_1C8D                               ; 1C69

L1_1C6B:
        cmp dx,byte +0x8                                ; 1C6B
        jc short L1_1C8D                                ; 1C6E
        shl dx,1                                        ; 1C70
        mov_ ax,dx                                      ; 1C72

L1_1C74:
        dec ax                                          ; 1C74
        mov_ dx,ax                                      ; 1C75
        add_ ax,cx                                      ; 1C77
        jnc short L1_1C7D                               ; 1C79
        xor_ ax,ax                                      ; 1C7B

L1_1C7D:
        not dx                                          ; 1C7D
        and_ ax,dx                                      ; 1C7F

L1_1C81:
        push dx                                         ; 1C81
        call L1_1CB3                                    ; 1C82
        pop dx                                          ; 1C85
        jnc short L1_1C95                               ; 1C86
        cmp dx,byte -0x10                               ; 1C88
        jz short L1_1C92                                ; 1C8B

L1_1C8D:
        mov ax,0x10                                     ; 1C8D
        jmp short L1_1C74                               ; 1C90

L1_1C92:
        stc                                             ; 1C92
        jmp short L1_1CB0                               ; 1C93

L1_1C95:
        mov_ dx,ax                                      ; 1C95
        sub dx,[bx+0x4]                                 ; 1C97
        mov [bx+0x4],ax                                 ; 1C9A
        mov [bx+0xa],di                                 ; 1C9D
        mov si,[bx+0xc]                                 ; 1CA0
        dec dx                                          ; 1CA3
        mov [si],dx                                     ; 1CA4
        inc dx                                          ; 1CA6
        add_ si,dx                                      ; 1CA7
        mov word [si],0xfffe                            ; 1CA9
        mov [bx+0xc],si                                 ; 1CAD

L1_1CB0:
        pop di                                          ; 1CB0
        pop cx                                          ; 1CB1
        ret                                             ; 1CB2

L1_1CB3:
        mov_ dx,ax                                      ; 1CB3
        test byte [bx+0x2],0x4                          ; 1CB5
        jz short L1_1CBD                                ; 1CB9
        jmp short L1_1D0E                               ; 1CBB

L1_1CBD:
        push dx                                         ; 1CBD
        push cx                                         ; 1CBE
        push bx                                         ; 1CBF
        mov si,[bx+0x6]                                 ; 1CC0
        mov bx,[cs:0x191a]                              ; 1CC3
        xor_ cx,cx                                      ; 1CC8
        or_ dx,dx                                       ; 1CCA
        jnz short L1_1CD5                               ; 1CCC
        test bx,0x10                                    ; 1CCE
        jnz short L1_1D14                               ; 1CD2
        inc cx                                          ; 1CD4

L1_1CD5:
        mov ax,0x2002                                   ; 1CD5
        test bx,0x1                                     ; 1CD8
        jnz short L1_1CE1                               ; 1CDC
        mov ax,0x2020                                   ; 1CDE

L1_1CE1:
        push si                                         ; 1CE1
        push cx                                         ; 1CE2
        push dx                                         ; 1CE3
        push ax                                         ; 1CE4
        callp R1_1CE6, 0xFFFF, 0x0000                   ; 1CE5 KERNEL.GlobalReAlloc
        or_ ax,ax                                       ; 1CEA
        jz short L1_1D14                                ; 1CEC
        cmp_ ax,si                                      ; 1CEE
        jnz short L1_1D0E                               ; 1CF0
        push si                                         ; 1CF2
        callp R1_1CF4, 0xFFFF, 0x0000                   ; 1CF3 KERNEL.GlobalSize
        or_ dx,ax                                       ; 1CF8
        jz short L1_1D0E                                ; 1CFA
        pop bx                                          ; 1CFC
        pop cx                                          ; 1CFD
        pop dx                                          ; 1CFE
        mov_ ax,dx                                      ; 1CFF
        test byte [bx+0x2],0x4                          ; 1D01
        jz short L1_1D0B                                ; 1D05
        dec dx                                          ; 1D07
        mov [bx-0x2],dx                                 ; 1D08

L1_1D0B:
        clc                                             ; 1D0B
        jmp short L1_1D18                               ; 1D0C

L1_1D0E:
        mov ax,0x12                                     ; 1D0E
        jmp near L1_1BE0                                ; 1D11

L1_1D14:
        pop bx                                          ; 1D14
        pop cx                                          ; 1D15
        pop dx                                          ; 1D16
        stc                                             ; 1D17

L1_1D18:
        ret                                             ; 1D18

L1_1D19:
        push di                                         ; 1D19
        mov si,[bx+0xa]                                 ; 1D1A
        cmp si,[bx+0xc]                                 ; 1D1D
        jnz short L1_1D25                               ; 1D20
        mov si,[bx+0x8]                                 ; 1D22

L1_1D25:
        lodsw                                           ; 1D25
        cmp ax,byte -0x2                                ; 1D26
        jz short L1_1D33                                ; 1D29
        mov_ di,si                                      ; 1D2B
        and al,0xfe                                     ; 1D2D
        add_ si,ax                                      ; 1D2F
        jmp short L1_1D25                               ; 1D31

L1_1D33:
        dec di                                          ; 1D33
        dec di                                          ; 1D34
        mov_ si,di                                      ; 1D35
        pop di                                          ; 1D37
        ret                                             ; 1D38
        db 0x00                                         ; 1D39

L1_1D3A:
        mov ax,0x2                                      ; 1D3A
        jmp near L1_1BE0                                ; 1D3D

L1_1D40:
        push bp                                         ; 1D40
        mov_ bp,sp                                      ; 1D41
        push bx                                         ; 1D43
        push es                                         ; 1D44
        push cx                                         ; 1D45
        mov cx,0x1000                                   ; 1D46
        xchg cx,[0xb6]                                  ; 1D49
        push cx                                         ; 1D4D
        push ax                                         ; 1D4E
        callf L1_1DDE, R1_1D52, R1_1C51                 ; 1D4F far seg1
        pop bx                                          ; 1D54
        pop word [0xb6]                                 ; 1D55
        pop cx                                          ; 1D59
        mov dx,ds                                       ; 1D5A
        or_ ax,ax                                       ; 1D5C
        jz short L1_1D64                                ; 1D5E
        pop es                                          ; 1D60
        pop bx                                          ; 1D61
        jmp short L1_1D69                               ; 1D62

L1_1D64:
        mov_ ax,cx                                      ; 1D64
        jmp near L1_1BE0                                ; 1D66

L1_1D69:
        mov_ sp,bp                                      ; 1D69
        pop bp                                          ; 1D6B
        ret                                             ; 1D6C
        db 0x00                                         ; 1D6D

L1_1D6E:
        cmp byte [cs:0x1d7e],0xb8                       ; 1D6E
        jz short L1_1D79                                ; 1D74
        mov ax,ss                                       ; 1D76
        retf                                            ; 1D78

L1_1D79:
        mov ax,[cs:0x1d7f]                              ; 1D79
        retf                                            ; 1D7D

___EXPORTEDSTUB:
        mov ax,ds                                       ; 1D7E
        nop                                             ; 1D80
        inc bp                                          ; 1D81
        push bp                                         ; 1D82
        mov_ bp,sp                                      ; 1D83
        push ds                                         ; 1D85
        mov ds,ax                                       ; 1D86
        xor_ ax,ax                                      ; 1D88
        lea sp,[bp-0x2]                                 ; 1D8A
        pop ds                                          ; 1D8D
        pop bp                                          ; 1D8E
        dec bp                                          ; 1D8F
        retf                                            ; 1D90
        db 0x90                                         ; 1D91

L1_1D92:
        ret                                             ; 1D92
        db 0x00                                         ; 1D93

L1_1D94:
        push bp                                         ; 1D94
        mov_ bp,sp                                      ; 1D95
        mov ax,[bp+0x8]                                 ; 1D97
        mov cx,[bp+0xc]                                 ; 1D9A
        or_ cx,ax                                       ; 1D9D
        mov cx,[bp+0xa]                                 ; 1D9F
        jnz short L1_1DAD                               ; 1DA2
        mov ax,[bp+0x6]                                 ; 1DA4
        mul cx                                          ; 1DA7
        pop bp                                          ; 1DA9
        retf 0x8                                        ; 1DAA

L1_1DAD:
        push bx                                         ; 1DAD
        mul cx                                          ; 1DAE
        mov_ bx,ax                                      ; 1DB0
        mov ax,[bp+0x6]                                 ; 1DB2
        mul word [bp+0xc]                               ; 1DB5
        add_ bx,ax                                      ; 1DB8
        mov ax,[bp+0x6]                                 ; 1DBA
        mul cx                                          ; 1DBD
        add_ dx,bx                                      ; 1DBF
        pop bx                                          ; 1DC1
        pop bp                                          ; 1DC2
        retf 0x8                                        ; 1DC3

L1_1DC6:
        xor_ ch,ch                                      ; 1DC6
        jcxz L1_1DD0                                    ; 1DC8

L1_1DCA:
        shl ax,1                                        ; 1DCA
        rcl dx,1                                        ; 1DCC
        loop L1_1DCA                                    ; 1DCE

L1_1DD0:
        retf                                            ; 1DD0
        db 0x00                                         ; 1DD1

L1_1DD2:
        xor_ ch,ch                                      ; 1DD2
        jcxz L1_1DDC                                    ; 1DD4

L1_1DD6:
        sar dx,1                                        ; 1DD6
        rcr ax,1                                        ; 1DD8
        loop L1_1DD6                                    ; 1DDA

L1_1DDC:
        retf                                            ; 1DDC
        db 0x00                                         ; 1DDD

L1_1DDE:
        inc bp                                          ; 1DDE
        push bp                                         ; 1DDF
        mov_ bp,sp                                      ; 1DE0
        push ds                                         ; 1DE2
        sub sp,byte +0x2                                ; 1DE3
        cmp word [bp+0x6],byte +0x0                     ; 1DE6
        jnz short L1_1DF1                               ; 1DEA
        mov word [bp+0x6],0x1                           ; 1DEC

L1_1DF1:
        mov ax,0xffff                                   ; 1DF1
        push ax                                         ; 1DF4
        callp R1_1DF6, R1_1E8B, 0x0000                  ; 1DF5 KERNEL.LockSegment
        mov ax,0x20                                     ; 1DFA
        push ax                                         ; 1DFD
        push word [bp+0x6]                              ; 1DFE
        callp R1_1E02, 0xFFFF, 0x0000                   ; 1E01 KERNEL.LocalAlloc
        mov [bp-0x4],ax                                 ; 1E06
        mov ax,0xffff                                   ; 1E09
        push ax                                         ; 1E0C
        callp R1_1E0E, R1_1EB3, 0x0000                  ; 1E0D KERNEL.UnlockSegment
        cmp word [bp-0x4],byte +0x0                     ; 1E12
        jnz short L1_1E2F                               ; 1E16
        mov ax,[0xba]                                   ; 1E18
        or ax,[0xb8]                                    ; 1E1B
        jz short L1_1E2F                                ; 1E1F
        push word [bp+0x6]                              ; 1E21
        call far [0xb8]                                 ; 1E24
        add sp,byte +0x2                                ; 1E28
        or_ ax,ax                                       ; 1E2B
        jnz short L1_1DF1                               ; 1E2D

L1_1E2F:
        mov ax,[bp-0x4]                                 ; 1E2F
        lea sp,[bp-0x2]                                 ; 1E32
        pop ds                                          ; 1E35
        pop bp                                          ; 1E36
        dec bp                                          ; 1E37
        retf                                            ; 1E38
        db 0x90, 0x45, 0x55, 0x8B, 0xEC, 0x1E, 0x83, 0x7E, 0x06, 0x00, 0x74, 0x08, 0xFF, 0x76, 0x06, 0x9A ; 1E39
R1_1E49: dw 0xFFFF, 0x0000                              ; 1E49 KERNEL.LocalFree
        db 0x8D, 0x66, 0xFE, 0x1F, 0x5D, 0x4D, 0xCB, 0x45, 0x55, 0x8B, 0xEC, 0x1E, 0x83, 0xEC, 0x04, 0x83 ; 1E4D
        db 0x7E, 0x06, 0x00, 0x75, 0x0E, 0xFF, 0x76, 0x08, 0x9A, 0xDE, 0x1D ; 1E5D
R1_1E68: dw R1_1E7C                                     ; 1E68 seg1
        db 0x83, 0xC4, 0x02, 0xEB, 0x4B, 0x90, 0x83, 0x7E, 0x08, 0x00, 0x75, 0x10, 0xFF, 0x76, 0x06, 0x9A ; 1E6A
        db 0x3A, 0x1E                                   ; 1E7A
R1_1E7C: dw R1_1D52                                     ; 1E7C seg1
        db 0x83, 0xC4, 0x02, 0x33, 0xC0, 0xEB, 0x35, 0x90, 0xB8, 0xFF, 0xFF, 0x50, 0x9A ; 1E7E
R1_1E8B: dw 0xFFFF, 0x0000                              ; 1E8B KERNEL.LockSegment
        db 0xFF, 0x76, 0x06, 0x83, 0x7E, 0x08, 0x00, 0x74, 0x06, 0x8B, 0x46, 0x08, 0xEB, 0x04, 0x90, 0xB8 ; 1E8F
        db 0x01, 0x00, 0x50, 0xB8, 0x62, 0x00, 0x50, 0x9A ; 1E9F
R1_1EA7: dw 0xFFFF, 0x0000                              ; 1EA7 KERNEL.LocalReAlloc
        db 0x89, 0x46, 0xFC, 0xB8, 0xFF, 0xFF, 0x50, 0x9A ; 1EAB
R1_1EB3: dw 0xFFFF, 0x0000                              ; 1EB3 KERNEL.UnlockSegment
        db 0x8B, 0x46, 0xFC, 0x8D, 0x66, 0xFE, 0x1F, 0x5D, 0x4D, 0xCB, 0x90, 0x45, 0x55, 0x8B, 0xEC, 0x1E ; 1EB7
        db 0xFF, 0x76, 0x06, 0x9A                       ; 1EC7
R1_1ECB: dw 0xFFFF, 0x0000                              ; 1ECB KERNEL.LocalSize
        db 0x8D, 0x66, 0xFE, 0x1F, 0x5D, 0x4D, 0xCB     ; 1ECF

%if ESFM_FIX
%include "esfmfix.asm"
%include "esfmfile.asm"
%include "esfmped.asm"
%include "esfmgm.asm"
%endif

seg1_data_end:

; relocation table
        dw (seg1_rel_end - seg1_rel_start) / 8
seg1_rel_start:
        reloc 2, 0, R1_1E68, 0x0001, 0x0000             ; seg1
        reloc 3, 1, R1_1C1E, 0x0001, 0x0001             ; KERNEL.FatalExit
        reloc 3, 1, R1_198F, 0x0001, 0x0003             ; KERNEL.GetVersion
        reloc 3, 1, R1_1B6A, 0x0001, 0x0083             ; KERNEL.GetDOSEnvironment
        reloc 2, 0, R1_190F, 0x0003, 0x0000             ; seg3
        reloc 3, 1, R1_1986, 0x0001, 0x0004             ; KERNEL.LocalInit
        reloc 3, 1, R1_1E02, 0x0001, 0x0005             ; KERNEL.LocalAlloc
        reloc 3, 1, R1_1EA7, 0x0001, 0x0006             ; KERNEL.LocalReAlloc
        reloc 3, 1, R1_1E49, 0x0001, 0x0007             ; KERNEL.LocalFree
        reloc 2, 0, R1_144C, 0x0004, 0x0000             ; seg4
        reloc 3, 1, R1_1C15, 0x0001, 0x0089             ; KERNEL.FatalAppExit
        reloc 3, 1, R1_1ECB, 0x0001, 0x000A             ; KERNEL.LocalSize
        reloc 3, 1, R1_1CE6, 0x0001, 0x0010             ; KERNEL.GlobalReAlloc
        reloc 3, 1, R1_1CF4, 0x0001, 0x0014             ; KERNEL.GlobalSize
        reloc 3, 1, R1_1DF6, 0x0001, 0x0017             ; KERNEL.LockSegment
        reloc 3, 1, R1_1E0E, 0x0001, 0x0018             ; KERNEL.UnlockSegment
        reloc 3, 1, R1_1436, 0x0003, 0x001F             ; MMSYSTEM.DriverCallback
        reloc 3, 1, R1_1940, 0x0001, 0x0030             ; KERNEL.GetModuleUsage
        reloc 5, 1, R1_1922, 0x0001, 0x00B2             ; KERNEL.__WINFLAGS
        reloc 3, 5, R1_192E, 0x0001, 0x005B             ; KERNEL.InitTask
        reloc 3, 1, R1_1ACE, 0x0001, 0x0066             ; KERNEL.DOS3Call
        reloc 3, 1, R1_18C6, 0x0003, 0x00D8             ; MMSYSTEM.midiOutMessage
%if ESFM_FIX
        reloc 2, 0, FIX_DS1, 0x0004, 0x0000             ; seg4
        fix_file_relocs
%endif
seg1_rel_end:
seg1_end:
