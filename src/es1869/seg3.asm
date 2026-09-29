; segment 3: code, 20580 bytes, flags 1D50h

L3_0000:
        push bp                                         ; 0000
        mov_ bp,sp                                      ; 0001
        sub sp,byte +0x2                                ; 0003
        pusha                                           ; 0006
        mov ax,0x8102                                   ; 0007
        xor_ dx,dx                                      ; 000A
        int 0x4b                                        ; 000C
        jnc short L3_0012                               ; 000E
        xor_ cx,cx                                      ; 0010

L3_0012:
        mov [bp-0x2],cx                                 ; 0012
        popa                                            ; 0015
        mov ax,[bp-0x2]                                 ; 0016
        mov_ sp,bp                                      ; 0019
        pop bp                                          ; 001B
        ret                                             ; 001C

L3_001D:
        push bp                                         ; 001D
        mov_ bp,sp                                      ; 001E
        mov cl,[bp+0x6]                                 ; 0020
        mov dx,0x21                                     ; 0023
        cmp cl,0x8                                      ; 0026
        jc short L3_0031                                ; 0029
        and cl,0x7                                      ; 002B
        mov dx,0xa1                                     ; 002E

L3_0031:
        mov ch,0x1                                      ; 0031
        shl ch,cl                                       ; 0033
        mov cl,[bp+0x4]                                 ; 0035
        or_ cl,cl                                       ; 0038
        jz short L3_003E                                ; 003A
        mov_ cl,ch                                      ; 003C

L3_003E:
        not ch                                          ; 003E
        pushf                                           ; 0040
        pushf                                           ; 0041
        pop bx                                          ; 0042
        test bh,0x2                                     ; 0043
        jz short L3_0049                                ; 0046
        cli                                             ; 0048

L3_0049:
        in al,dx                                        ; 0049
        mov_ ah,al                                      ; 004A
        and_ al,ch                                      ; 004C
        or_ al,cl                                       ; 004E
        cmp al,ah                                       ; 0050
        jz short L3_0055                                ; 0052
        out dx,al                                       ; 0054

L3_0055:
        pop bx                                          ; 0055
        test bh,0x2                                     ; 0056
        jz short L3_005C                                ; 0059
        sti                                             ; 005B

L3_005C:
        not ch                                          ; 005C
        mov_ al,ah                                      ; 005E
        and_ al,ch                                      ; 0060
        xor_ ah,ah                                      ; 0062
        mov_ sp,bp                                      ; 0064
        pop bp                                          ; 0066
        ret 0x4                                         ; 0067

L3_006A:
        push bp                                         ; 006A
        mov_ bp,sp                                      ; 006B
        mov al,[bp+0x8]                                 ; 006D
        mov ah,0x8                                      ; 0070
        cmp al,ah                                       ; 0072
        jl short L3_0078                                ; 0074
        mov ah,0x68                                     ; 0076

L3_0078:
        add_ al,ah                                      ; 0078
        mov ah,0x35                                     ; 007A
        int 0x21                                        ; 007C
        push es                                         ; 007E
        push bx                                         ; 007F
        mov ah,0x25                                     ; 0080
        push ds                                         ; 0082
        lds dx,[bp+0x4]                                 ; 0083
        int 0x21                                        ; 0086
        pop ds                                          ; 0088
        pop ax                                          ; 0089
        pop dx                                          ; 008A
        mov_ sp,bp                                      ; 008B
        pop bp                                          ; 008D
        ret 0x6                                         ; 008E

; the VxD's entry point (INT 2Fh AX=1684h BX=3B07h) to DS:0010
get_vxd_entry:
        push di                                         ; 0091
        push es                                         ; 0092
        mov ax,[0x10]                                   ; 0093
        or ax,[0x12]                                    ; 0096
        jnz short L3_00B8                               ; 009A
        xor_ ax,ax                                      ; 009C
        xor_ di,di                                      ; 009E
        mov es,di                                       ; 00A0
        mov ax,0x1684                                   ; 00A2
        mov bx,0x3b07                                   ; 00A5
        int 0x2f                                        ; 00A8
        mov ax,es                                       ; 00AA
        or_ ax,di                                       ; 00AC
        jz short L3_00BE                                ; 00AE
        mov [0x10],di                                   ; 00B0
        mov [0x12],es                                   ; 00B4

L3_00B8:
        mov ax,0x1                                      ; 00B8
        clc                                             ; 00BB
        jmp short L3_00C1                               ; 00BC

L3_00BE:
        xor_ ax,ax                                      ; 00BE
        stc                                             ; 00C0

L3_00C1:
        pop es                                          ; 00C1
        pop di                                          ; 00C2
        ret                                             ; 00C3

; VxD API 0000
vxd_version:
        call get_vxd_entry                              ; 00C4
        jc short L3_00D0                                ; 00C7
        mov dx,0x0                                      ; 00C9
        call far [0x10]                                 ; 00CC

L3_00D0:
        ret                                             ; 00D0
        mov dx,0x9                                      ; 00D1
        call far [0x10]                                 ; 00D4
        retf                                            ; 00D8

; VxD API 0001: copy the ADI (C9h bytes): Audio_Base, MPU-401, IRQs, DMA channels
vxd_get_adi:
        push bp                                         ; 00D9
        mov_ bp,sp                                      ; 00DA
        sub sp,0xca                                     ; 00DC
        push si                                         ; 00E0
        push di                                         ; 00E1
        push es                                         ; 00E2
        push ds                                         ; 00E3
        call get_vxd_entry                              ; 00E4
        jnc short L3_00EC                               ; 00E7
        jmp near L3_01AF                                ; 00E9

L3_00EC:
        push ss                                         ; 00EC
        pop es                                          ; 00ED
        lea bx,[bp-0xca]                                ; 00EE
        mov word [es:bx],0xc9                           ; 00F2
        mov word [es:bx+0x2],0x0                        ; 00F7
        mov ax,0x1                                      ; 00FD
        push ecx                                        ; 0100
        mov ecx,[bp+0x4]                                ; 0102
        mov dx,0x1                                      ; 0106
        call far [0x10]                                 ; 0109
        pop ecx                                         ; 010D
        jnc short L3_0114                               ; 010F
        jmp near L3_01AF                                ; 0111

L3_0114:
        mov si,[bp+0x8]                                 ; 0114
        mov ax,[es:bx+0x6]                              ; 0117
        mov [si],ax                                     ; 011B
        mov ax,[es:bx+0xc]                              ; 011D
        mov [si+0x2],ax                                 ; 0121
        mov al,[es:bx+0xe]                              ; 0124
        mov [si+0x4],al                                 ; 0128
        mov al,[es:bx+0xf]                              ; 012B
        mov [si+0x5],al                                 ; 012F
        mov al,[es:bx+0x10]                             ; 0132
        mov [si+0x6],al                                 ; 0136
        mov al,[es:bx+0x12]                             ; 0139
        mov [si+0x7],al                                 ; 013D
        mov ax,[es:bx+0x15]                             ; 0140
        mov [si+0x8],ax                                 ; 0144
        mov ax,[es:bx+0x29]                             ; 0147
        mov [si+0x12],ax                                ; 014B
        mov ax,[es:bx+0x1d]                             ; 014E
        mov [si+0xa],ax                                 ; 0152
        mov ax,[es:bx+0x1f]                             ; 0155
        mov [si+0xc],ax                                 ; 0159
        mov ax,[es:bx+0x25]                             ; 015C
        mov [si+0xe],ax                                 ; 0160
        mov ax,[es:bx+0x27]                             ; 0163
        mov [si+0x10],ax                                ; 0167
        test word [es:bx+0x4],0x20                      ; 016A
        jz short L3_0177                                ; 0170
        or word [si+0x2a],0x800                         ; 0172

L3_0177:
        mov al,[es:bx+0x79]                             ; 0177
        mov [si+0xd4],al                                ; 017B
        mov ax,[es:bx+0x8e]                             ; 017F
        mov [si+0xdd],ax                                ; 0184
        mov ax,[es:bx+0x82]                             ; 0188
        mov [si+0xd5],ax                                ; 018D
        mov ax,[es:bx+0x84]                             ; 0191
        mov [si+0xd7],ax                                ; 0196
        mov ax,[es:bx+0x8a]                             ; 019A
        mov [si+0xd9],ax                                ; 019F
        mov ax,[es:bx+0x8c]                             ; 01A3
        mov [si+0xdb],ax                                ; 01A8
        mov ax,0x1                                      ; 01AC

L3_01AF:
        pop ds                                          ; 01AF
        pop es                                          ; 01B0
        pop di                                          ; 01B1
        pop si                                          ; 01B2
        mov_ sp,bp                                      ; 01B3
        pop bp                                          ; 01B5
        ret 0x6                                         ; 01B6

; the interrupt handler, the DDK sample's ISR_Stub: copied to a fixed block by isr_install, the device pointer patched at +0Ah (mov si,1234h). An Audio 2 interrupt goes to the second stub (+FEh:0000), an Audio 1 one through isr_srv_table by the Audio 1 user (+5Dh), then the MPU-401 and the EOI (+5Bh)
isr_template:
        cld                                             ; 01B9
        push ds                                         ; 01BA
        push ax                                         ; 01BB
        movsel ax, R3_01BD, 0xFFFF                      ; 01BC seg7
        mov ds,ax                                       ; 01BF
        push si                                         ; 01C1
        mov si,0x1234                                   ; 01C2
        push es                                         ; 01C5
        pushad                                          ; 01C6
        test word [si+0x2a],0x2000                      ; 01C8
        jz short L3_01EA                                ; 01CD
        mov byte [si+0x64],0x2                          ; 01CF

L3_01D3:
        test word [si+0x2a],0x1000                      ; 01D3
        jz short L3_01EA                                ; 01D8
        mov dx,[si+0x2]                                 ; 01DA
        inc dx                                          ; 01DD
        in al,dx                                        ; 01DE
        test al,0x80                                    ; 01DF
        jnz short L3_01EA                               ; 01E1
        mov ax,[si+0x62]                                ; 01E3
        push ax                                         ; 01E6
        call far [si+0x5e]                              ; 01E7

L3_01EA:
        cmp byte [si+0x100],0x0                         ; 01EA
        jz short L3_0214                                ; 01EF
        mov dx,[si]                                     ; 01F1
        add dx,byte +0x4                                ; 01F3
        in al,dx                                        ; 01F6
        push ax                                         ; 01F7
        push dx                                         ; 01F8
        push si                                         ; 01F9
        push byte +0x7a                                 ; 01FA
        callf mixer_read, R3_01FF, 0xFFFF               ; 01FC far seg1
        test al,0x80                                    ; 0201
        pop dx                                          ; 0203
        pop ax                                          ; 0204
        out dx,al                                       ; 0205
        jz short L3_0214                                ; 0206
        pushf                                           ; 0208
        push cs                                         ; 0209
        push word strict word 0x5b                      ; 020A
        push word [si+0xfe]                             ; 020D
        push byte +0x0                                  ; 0211
        retf                                            ; 0213

L3_0214:
        cmp byte [si+0x5d],0x0                          ; 0214
        jz short L3_0241                                ; 0218
        mov dx,[si]                                     ; 021A
        add dx,byte +0xc                                ; 021C
        in al,dx                                        ; 021F
        test al,0x7                                     ; 0220
        jz short L3_0241                                ; 0222
        mov al,[si+0x5d]                                ; 0224
        or_ al,al                                       ; 0227
        jz short L3_023B                                ; 0229
        dec ax                                          ; 022B
        mov_ di,ax                                      ; 022C
        and di,byte +0x3                                ; 022E
        shl di,byte 0x2                                 ; 0231
        sti                                             ; 0234
        push si                                         ; 0235
        call far [di+0xae]                              ; 0236
        cli                                             ; 023A

L3_023B:
        mov dx,[si]                                     ; 023B
        add dx,byte +0xe                                ; 023D
        in al,dx                                        ; 0240

L3_0241:
        test word [si+0x2a],0x2000                      ; 0241
        jz short L3_024D                                ; 0246
        dec byte [si+0x64]                              ; 0248
        jnz short L3_01D3                               ; 024B

L3_024D:
        mov ax,[si+0x5b]                                ; 024D
        or_ al,al                                       ; 0250
        jz short L3_0256                                ; 0252
        out 0xa0,al                                     ; 0254

L3_0256:
        mov_ al,ah                                      ; 0256
        out 0x20,al                                     ; 0258
        popad                                           ; 025A
        pop es                                          ; 025C
        pop si                                          ; 025D
        pop ax                                          ; 025E
        pop ds                                          ; 025F
        iret                                            ; 0260

; the sample's Create_ISR: 0A8h fixed bytes (+51h), a code alias (+53h), isr_template copied and +0Ah patched
isr_install:
        push bp                                         ; 0261
        mov_ bp,sp                                      ; 0262
        push si                                         ; 0264
        push di                                         ; 0265
        push es                                         ; 0266
        mov si,[bp+0x4]                                 ; 0267
        mov ax,[si+0x53]                                ; 026A
        or_ ax,ax                                       ; 026D
        jnz short L3_02B8                               ; 026F
        push word 0x2040                                ; 0271
        push byte +0x0                                  ; 0274
        push word 0xa8                                  ; 0276
        callp R3_027A, 0xFFFF, 0x0000                   ; 0279 KERNEL.GlobalAlloc
        or_ ax,ax                                       ; 027E
        jz short L3_02B8                                ; 0280
        mov_ dx,ax                                      ; 0282
        xor_ ax,ax                                      ; 0284
        mov [si+0x51],dx                                ; 0286
        xor_ ax,ax                                      ; 0289
        push ax                                         ; 028B
        callp R3_028D, 0xFFFF, 0x0000                   ; 028C KERNEL.AllocSelector
        push dx                                         ; 0291
        push ax                                         ; 0292
        callp R3_0294, 0xFFFF, 0x0000                   ; 0293 KERNEL.PrestoChangoSelector
        mov [si+0x53],ax                                ; 0298
        push ds                                         ; 029B
        push si                                         ; 029C
        push cs                                         ; 029D
        pop ds                                          ; 029E
        mov si,0x1b9                                    ; 029F
        mov es,dx                                       ; 02A2
        xor_ di,di                                      ; 02A4
        mov cx,0xa8                                     ; 02A6
        rep movsb                                       ; 02A9
        pop si                                          ; 02AB
        pop ds                                          ; 02AC
        mov di,0xa                                      ; 02AD
        mov [es:di],si                                  ; 02B0
        mov ax,0x1                                      ; 02B3
        jmp short L3_02BA                               ; 02B6

L3_02B8:
        xor_ ax,ax                                      ; 02B8

L3_02BA:
        pop es                                          ; 02BA
        pop di                                          ; 02BB
        pop si                                          ; 02BC
        mov_ sp,bp                                      ; 02BD
        pop bp                                          ; 02BF
        ret 0x2                                         ; 02C0

; the Audio 2 stub: dispatch by the Audio 2 user (+100h) through isr_srv_table+8, then 7Ah bit 7 cleared, the mixer index kept
isr_template_b:
        cld                                             ; 02C3
        push ds                                         ; 02C4
        push ax                                         ; 02C5
        movsel ax, R3_02C7, R3_01BD                     ; 02C6 seg7
        mov ds,ax                                       ; 02C9
        push si                                         ; 02CB
        mov si,0x1234                                   ; 02CC
        push es                                         ; 02CF
        pushad                                          ; 02D0
        mov al,[si+0x100]                               ; 02D2
        or_ al,al                                       ; 02D6
        jz short L3_02EA                                ; 02D8
        dec ax                                          ; 02DA
        mov_ di,ax                                      ; 02DB
        and di,byte +0x3                                ; 02DD
        shl di,byte 0x2                                 ; 02E0
        sti                                             ; 02E3
        push si                                         ; 02E4
        call far [di+0xb6]                              ; 02E5
        cli                                             ; 02E9

L3_02EA:
        mov dx,[si]                                     ; 02EA
        add dx,byte +0x4                                ; 02EC
        in al,dx                                        ; 02EF
        push ax                                         ; 02F0
        push dx                                         ; 02F1
        push si                                         ; 02F2
        push byte +0x7a                                 ; 02F3
        callf mixer_read, R3_02F8, R3_01FF              ; 02F5 far seg1
        and al,0x7f                                     ; 02FA
        push si                                         ; 02FC
        push byte +0x7a                                 ; 02FD
        push ax                                         ; 02FF
        callf mixer_write, R3_0303, R3_02F8             ; 0300 far seg1
        pop dx                                          ; 0305
        pop ax                                          ; 0306
        out dx,al                                       ; 0307
        popad                                           ; 0308
        pop es                                          ; 030A
        pop si                                          ; 030B
        pop ax                                          ; 030C
        pop ds                                          ; 030D
        iret                                            ; 030E

L3_030F:
        push bp                                         ; 030F
        mov_ bp,sp                                      ; 0310
        push si                                         ; 0312
        push di                                         ; 0313
        push es                                         ; 0314
        mov si,[bp+0x4]                                 ; 0315
        mov ax,[si+0xfe]                                ; 0318
        or_ ax,ax                                       ; 031C
        jnz short L3_0369                               ; 031E
        push word 0x2040                                ; 0320
        push byte +0x0                                  ; 0323
        push word strict word 0x4c                      ; 0325
        callp R3_0329, R3_027A, 0x0000                  ; 0328 KERNEL.GlobalAlloc
        or_ ax,ax                                       ; 032D
        jz short L3_0369                                ; 032F
        mov_ dx,ax                                      ; 0331
        xor_ ax,ax                                      ; 0333
        mov [si+0xfc],dx                                ; 0335
        xor_ ax,ax                                      ; 0339
        push ax                                         ; 033B
        callp R3_033D, R3_028D, 0x0000                  ; 033C KERNEL.AllocSelector
        push dx                                         ; 0341
        push ax                                         ; 0342
        callp R3_0344, R3_0294, 0x0000                  ; 0343 KERNEL.PrestoChangoSelector
        mov [si+0xfe],ax                                ; 0348
        push ds                                         ; 034C
        push si                                         ; 034D
        push cs                                         ; 034E
        pop ds                                          ; 034F
        mov si,0x2c3                                    ; 0350
        mov es,dx                                       ; 0353
        xor_ di,di                                      ; 0355
        mov cx,0x4c                                     ; 0357
        rep movsb                                       ; 035A
        pop si                                          ; 035C
        pop ds                                          ; 035D
        mov di,0xa                                      ; 035E
        mov [es:di],si                                  ; 0361
        mov ax,0x1                                      ; 0364
        jmp short L3_036B                               ; 0367

L3_0369:
        xor_ ax,ax                                      ; 0369

L3_036B:
        pop es                                          ; 036B
        pop di                                          ; 036C
        pop si                                          ; 036D
        mov_ sp,bp                                      ; 036E
        pop bp                                          ; 0370
        ret 0x2                                         ; 0371

L3_0374:
        push bp                                         ; 0374
        mov_ bp,sp                                      ; 0375
        push di                                         ; 0377
        mov ax,[0xc0]                                   ; 0378
        or ax,[0xbe]                                    ; 037B
        jz short L3_038A                                ; 037F

L3_0381:
        mov ax,[0xbe]                                   ; 0381
        mov dx,[0xc0]                                   ; 0384
        jmp short L3_03A6                               ; 0388

L3_038A:
        push bx                                         ; 038A
        push es                                         ; 038B
        push di                                         ; 038C
        xor_ di,di                                      ; 038D
        mov ax,0x1684                                   ; 038F
        mov bx,0x33                                     ; 0392
        mov es,di                                       ; 0395
        int 0x2f                                        ; 0397
        mov [0xc0],es                                   ; 0399
        mov [0xbe],di                                   ; 039D
        pop di                                          ; 03A1
        pop es                                          ; 03A2
        pop bx                                          ; 03A3
        jmp short L3_0381                               ; 03A4

L3_03A6:
        pop di                                          ; 03A6
        mov_ sp,bp                                      ; 03A7
        pop bp                                          ; 03A9
        retf                                            ; 03AA
        db 'LeftVoiceDACVol'                            ; 03AB
        db 0x00                                         ; 03BA
        db 'GPO0 Show'                                  ; 03BB
        db 0x00                                         ; 03C4
        db 'AuxBVolumeOutMap'                           ; 03C5
        db 0x00                                         ; 03D5
        db 'GPO0 Default'                               ; 03D6
        db 0x00                                         ; 03E2
        db 'LeftVoiceAuxBVol'                           ; 03E3
        db 0x00                                         ; 03F3
        db 'SynthVolumeOutMap'                          ; 03F4
        db 0x00                                         ; 0405
        db 'SpatializerEnable'                          ; 0406
        db 0x00                                         ; 0417
        db 'LeftVoiceCDAudioVol'                        ; 0418
        db 0x00                                         ; 042B
        db 'CDAudioVolumeOutMap'                        ; 042C
        db 0x00                                         ; 043F
        db 'LeftVoiceMicVol'                            ; 0440
        db 0x00                                         ; 044F
        db 'SpatializerEffect'                          ; 0450
        db 0x00                                         ; 0461
        db 'MicVolumeOutMap'                            ; 0462
        db 0x00                                         ; 0471
        db 'LeftVoiceLineVol'                           ; 0472
        db 0x00                                         ; 0482
        db '3D Effect Enable'                           ; 0483
        db 0x00                                         ; 0493
        db '3D Effect'                                  ; 0494
        db 0x00                                         ; 049D
        db 'WaveVolumeOutMap'                           ; 049E
        db 0x00                                         ; 04AE
        db 'LeftVoiceMasterVol'                         ; 04AF
        db 0x00                                         ; 04C1
        db 'Bass'                                       ; 04C2
        db 0x00                                         ; 04C6
        db 'Mixer:Voice'                                ; 04C7
        db 0x00                                         ; 04D2
        db 'LineInVolumeOutMap'                         ; 04D3
        db 0x00                                         ; 04E5
        db 'Treble'                                     ; 04E6
        db 0x00                                         ; 04EC
        db 'MonitorVoice'                               ; 04ED
        db 0x00                                         ; 04F9
        db 'MutesOut'                                   ; 04FA
        db 0x00                                         ; 0502
        db 'IISVolumeInMap'                             ; 0503
        db 0x00                                         ; 0511
        db 'LeftWaveDACVol'                             ; 0512
        db 0x00                                         ; 0520
        db 'Mute'                                       ; 0521
        db 0x00                                         ; 0525
        db 'AuxBVolumeInMap'                            ; 0526
        db 0x00                                         ; 0535
        db 'StartupMuteMsg'                             ; 0536
        db 0x00                                         ; 0544
        db 'LeftWaveAuxBVol'                            ; 0545
        db 0x00                                         ; 0554
        db 'SynthVolumeInMap'                           ; 0555
        db 0x00                                         ; 0565
        db 'LeftWaveSynthVol'                           ; 0566
        db 0x00                                         ; 0576
        db 'CDAudioVolumeInMap'                         ; 0577
        db 0x00                                         ; 0589
        db 'LeftWaveCDAudioVol'                         ; 058A
        db 0x00                                         ; 059C
        db 'LeftWaveMicVol'                             ; 059D
        db 0x00                                         ; 05AB
        db 'MicVolumeInMap'                             ; 05AC
        db 0x00                                         ; 05BA
        db 'LeftWaveLineVol'                            ; 05BB
        db 0x00                                         ; 05CA
        db 'WaveVolumeInMap'                            ; 05CB
        db 0x00                                         ; 05DA
        db 'LeftWaveMasterVol'                          ; 05DB
        db 0x00                                         ; 05EC
        db 'LineInVolumeInMap'                          ; 05ED
        db 0x00                                         ; 05FE
        db 'Mixer:Wave'                                 ; 05FF
        db 0x00                                         ; 0609
        db 'ESSWaveTableChip'                           ; 060A
        db 0x00                                         ; 061A
        db 'MonitorWave'                                ; 061B
        db 0x00                                         ; 0626
        db 'RightVoiceDACVol'                           ; 0627
        db 0x00                                         ; 0637
        db 'MonoInPhoneMute'                            ; 0638
        db 0x00                                         ; 0647
        db 'RightVoiceAuxBVol'                          ; 0648
        db 0x00                                         ; 0659
        db 'MonoInMicMute'                              ; 065A
        db 0x00                                         ; 0667
        db 'RightVoiceCDAudioVol'                       ; 0668
        db 0x00                                         ; 067C
        db '\GPO Selections'                            ; 067D
        db 0x00                                         ; 068C
        db 'RightMonoInPhone'                           ; 068D
        db 0x00                                         ; 069D
        db '\Config'                                    ; 069E
        db 0x00                                         ; 06A5
        db 'RightVoiceMicVol'                           ; 06A6
        db 0x00                                         ; 06B6
        db 'LeftMonoInPhone'                            ; 06B7
        db 0x00                                         ; 06C6
        db 'MIDIInPersistence'                          ; 06C7
        db 0x00                                         ; 06D8
        db 'RightVoiceLineVol'                          ; 06D9
        db 0x00                                         ; 06EA
        db 'RightMonoInMic'                             ; 06EB
        db 0x00                                         ; 06F9
        db 'Enable Software 3D Effect'                  ; 06FA
        db 0x00                                         ; 0713
        db 'RightVoiceMasterVol'                        ; 0714
        db 0x00                                         ; 0727
        db 'LeftMonoInMic'                              ; 0728
        db 0x00                                         ; 0735
        db 'Do Not Want ES938'                          ; 0736
        db 0x00                                         ; 0747
        db 'RightWaveDACVol'                            ; 0748
        db 0x00                                         ; 0757
        db 'PCspeakerVol'                               ; 0758
        db 0x00                                         ; 0764
        db '3D Limit'                                   ; 0765
        db 0x00                                         ; 076D
        db 'RightWaveAuxBVol'                           ; 076E
        db 0x00                                         ; 077E
        db 'LeftIISVol'                                 ; 077F
        db 0x00                                         ; 0789
        db 'Enable ES938'                               ; 078A
        db 0x00                                         ; 0796
        db 'LeftAuxBVol'                                ; 0797
        db 0x00                                         ; 07A2
        db 'RightWaveSynthVol'                          ; 07A3
        db 0x00                                         ; 07B4
        db 'Enable IIS'                                 ; 07B5
        db 0x00                                         ; 07BF
        db 'LeftSynthVol'                               ; 07C0
        db 0x00                                         ; 07CC
        db 'Enable AUXB'                                ; 07CD
        db 0x00                                         ; 07D8
        db 'RightWaveCDAudioVol'                        ; 07D9
        db 0x00                                         ; 07EC
        db 'LeftCDAudioVol'                             ; 07ED
        db 0x00                                         ; 07FB
        db 'RightWaveMicVol'                            ; 07FC
        db 0x00                                         ; 080B
        db 'Disable Mic Preamp'                         ; 080C
        db 0x00                                         ; 081E
        db 'LeftMicVol'                                 ; 081F
        db 0x00                                         ; 0829
        db 'RightWaveLineVol'                           ; 082A
        db 0x00                                         ; 083A
        db 'HwVolume Count By 3'                        ; 083B
        db 0x00                                         ; 084E
        db 'LeftDACVol'                                 ; 084F
        db 0x00                                         ; 0859
        db 'RightWaveMasterVol'                         ; 085A
        db 0x00                                         ; 086C
        db 'LeftLineInVol'                              ; 086D
        db 0x00                                         ; 087A
        db 'Single Mode DMA'                            ; 087B
        db 0x00                                         ; 088A
        db 'RightIISVol'                                ; 088B
        db 0x00                                         ; 0896
        db 'Config'                                     ; 0897
        db 0x00                                         ; 089D
        db 'LeftMasterVol'                              ; 089E
        db 0x00                                         ; 08AB
        db 'RightAuxBVol'                               ; 08AC
        db 0x00                                         ; 08B8
        db 'HwVolumeMap'                                ; 08B9
        db 0x00                                         ; 08C4
        db 'Mixer:Output'                               ; 08C5
        db 0x00                                         ; 08D1
        db 'RightSynthVol'                              ; 08D2
        db 0x00                                         ; 08DF
        db 'HwVolumeStep'                               ; 08E0
        db 0x00                                         ; 08EC
        db 'GPO1 Label'                                 ; 08ED
        db 0x00                                         ; 08F7
        db 'RightCDAudioVol'                            ; 08F8
        db 0x00                                         ; 0907
        db 'DiscardBlock'                               ; 0908
        db 0x00                                         ; 0914
        db 'GPO1 LongLabel'                             ; 0915
        db 0x00                                         ; 0923
        db 'RightMicVol'                                ; 0924
        db 0x00, 0x41, 0x47, 0x43, 0x00                 ; 092F
        db 'GPO1 Show'                                  ; 0934
        db 0x00                                         ; 093D
        db 'DCdrift'                                    ; 093E
        db 0x00                                         ; 0945
        db 'RightDACVol'                                ; 0946
        db 0x00                                         ; 0951
        db 'GPO1 Default'                               ; 0952
        db 0x00                                         ; 095E
        db 'RightLineInVol'                             ; 095F
        db 0x00                                         ; 096D
        db 'Disable Warning'                            ; 096E
        db 0x00                                         ; 097D
        db 'GPO0 Label'                                 ; 097E
        db 0x00                                         ; 0988
        db 'RightMasterVol'                             ; 0989
        db 0x00                                         ; 0997
        db 'GPO0 LongLabel'                             ; 0998
        db 0x00                                         ; 09A6
        db 'IISVolumeOutMap'                            ; 09A7
        db 0x00, 0x90                                   ; 09B6

L3_09B8:
        push bp                                         ; 09B8
        mov_ bp,sp                                      ; 09B9
        sub sp,byte +0x8                                ; 09BB
        mov word [bp-0x2],0x0                           ; 09BE
        mov word [bp-0x8],0x7                           ; 09C3
        push cs                                         ; 09C8
        call L3_0374                                    ; 09C9
        mov [bp-0x6],ax                                 ; 09CC
        mov [bp-0x4],dx                                 ; 09CF
        or_ dx,ax                                       ; 09D2
        jnz short L3_09DA                               ; 09D4
        xor_ ax,ax                                      ; 09D6
        jmp short L3_09E6                               ; 09D8

L3_09DA:
        mov ax,[bp-0x8]                                 ; 09DA
        call far [bp-0x6]                               ; 09DD
        mov [bp-0x2],ax                                 ; 09E0
        mov ax,[bp-0x2]                                 ; 09E3

L3_09E6:
        mov_ sp,bp                                      ; 09E6
        pop bp                                          ; 09E8
        ret                                             ; 09E9

; CONFIGMG CM_Get_DevNode_Key through the VxD
cm_get_devnode_key:
        push bp                                         ; 09EA
        mov_ bp,sp                                      ; 09EB
        sub sp,byte +0x8                                ; 09ED
        mov word [bp-0x2],0x0                           ; 09F0
        mov word [bp-0x8],0x3d                          ; 09F5
        push cs                                         ; 09FA
        call L3_0374                                    ; 09FB
        mov [bp-0x6],ax                                 ; 09FE
        mov [bp-0x4],dx                                 ; 0A01
        or_ dx,ax                                       ; 0A04
        jnz short L3_0A0C                               ; 0A06
        xor_ ax,ax                                      ; 0A08
        jmp short L3_0A18                               ; 0A0A

L3_0A0C:
        mov ax,[bp-0x8]                                 ; 0A0C
        call far [bp-0x6]                               ; 0A0F
        mov [bp-0x2],ax                                 ; 0A12
        mov ax,[bp-0x2]                                 ; 0A15

L3_0A18:
        mov_ sp,bp                                      ; 0A18
        pop bp                                          ; 0A1A
        ret                                             ; 0A1B

L3_0A1C:
        push bp                                         ; 0A1C
        mov_ bp,sp                                      ; 0A1D
        sub sp,byte +0x8                                ; 0A1F
        mov word [bp-0x2],0x0                           ; 0A22
        mov word [bp-0x8],0x3e                          ; 0A27
        push cs                                         ; 0A2C
        call L3_0374                                    ; 0A2D
        mov [bp-0x6],ax                                 ; 0A30
        mov [bp-0x4],dx                                 ; 0A33
        or_ dx,ax                                       ; 0A36
        jnz short L3_0A3E                               ; 0A38
        xor_ ax,ax                                      ; 0A3A
        jmp short L3_0A4A                               ; 0A3C

L3_0A3E:
        mov ax,[bp-0x8]                                 ; 0A3E
        call far [bp-0x6]                               ; 0A41
        mov [bp-0x2],ax                                 ; 0A44
        mov ax,[bp-0x2]                                 ; 0A47

L3_0A4A:
        mov_ sp,bp                                      ; 0A4A
        pop bp                                          ; 0A4C
        ret                                             ; 0A4D

L3_0A4E:
        push bp                                         ; 0A4E
        mov_ bp,sp                                      ; 0A4F
        sub sp,byte +0x8                                ; 0A51
        mov word [bp-0x2],0x0                           ; 0A54
        mov word [bp-0x8],0x3f                          ; 0A59
        push cs                                         ; 0A5E
        call L3_0374                                    ; 0A5F
        mov [bp-0x6],ax                                 ; 0A62
        mov [bp-0x4],dx                                 ; 0A65
        or_ dx,ax                                       ; 0A68
        jnz short L3_0A70                               ; 0A6A
        xor_ ax,ax                                      ; 0A6C
        jmp short L3_0A7C                               ; 0A6E

L3_0A70:
        mov ax,[bp-0x8]                                 ; 0A70
        call far [bp-0x6]                               ; 0A73
        mov [bp-0x2],ax                                 ; 0A76
        mov ax,[bp-0x2]                                 ; 0A79

L3_0A7C:
        mov_ sp,bp                                      ; 0A7C
        pop bp                                          ; 0A7E
        ret                                             ; 0A7F

L3_0A80:
        callp R3_0A81, 0xFFFF, 0x0000                   ; 0A80 KERNEL.GetVersion
        mov_ cx,ax                                      ; 0A85
        mov_ ah,al                                      ; 0A87
        mov_ al,ch                                      ; 0A89
        retf                                            ; 0A8B

L3_0A8C:
        push bp                                         ; 0A8C
        mov_ bp,sp                                      ; 0A8D
        sub sp,0x232                                    ; 0A8F
        test byte [0xc4],0x1                            ; 0A93
        jnz short L3_0AE6                               ; 0A98
        push cs                                         ; 0A9A
        call L3_0A80                                    ; 0A9B
        cmp ax,0x30a                                    ; 0A9E
        jc short L3_0AE6                                ; 0AA1
        push word [0xbd0]                               ; 0AA3
        mov ax,0x1                                      ; 0AA7
        push ax                                         ; 0AAA
        lea ax,[bp-0x32]                                ; 0AAB
        push ss                                         ; 0AAE
        push ax                                         ; 0AAF
        mov ax,0x32                                     ; 0AB0
        push ax                                         ; 0AB3
        callp R3_0AB5, R3_0ACB, 0x0000                  ; 0AB4 USER.LoadString
        push word [0xbd0]                               ; 0AB9
        push word [bp+0x4]                              ; 0ABD
        lea ax,[bp-0x232]                               ; 0AC0
        push ss                                         ; 0AC4
        push ax                                         ; 0AC5
        mov ax,0x200                                    ; 0AC6
        push ax                                         ; 0AC9
        callp R3_0ACB, R3_0B0C, 0x0000                  ; 0ACA USER.LoadString
        xor_ ax,ax                                      ; 0ACF
        push ax                                         ; 0AD1
        lea ax,[bp-0x232]                               ; 0AD2
        push ss                                         ; 0AD6
        push ax                                         ; 0AD7
        lea ax,[bp-0x32]                                ; 0AD8
        push ss                                         ; 0ADB
        push ax                                         ; 0ADC
        mov ax,0x1010                                   ; 0ADD
        push ax                                         ; 0AE0
        callp R3_0AE2, R3_0B4F, 0x0000                  ; 0AE1 USER.MessageBox

L3_0AE6:
        mov_ sp,bp                                      ; 0AE6
        pop bp                                          ; 0AE8
        ret 0x2                                         ; 0AE9

L3_0AEC:
        push bp                                         ; 0AEC
        mov_ bp,sp                                      ; 0AED
        sub sp,0x332                                    ; 0AEF
        test byte [0xc4],0x1                            ; 0AF3
        jnz short L3_0B53                               ; 0AF8
        push word [0xbd0]                               ; 0AFA
        mov ax,0x1                                      ; 0AFE
        push ax                                         ; 0B01
        lea ax,[bp-0x32]                                ; 0B02
        push ss                                         ; 0B05
        push ax                                         ; 0B06
        mov ax,0x32                                     ; 0B07
        push ax                                         ; 0B0A
        callp R3_0B0C, R3_0B22, 0x0000                  ; 0B0B USER.LoadString
        push word [0xbd0]                               ; 0B10
        push word [bp+0x8]                              ; 0B14
        lea ax,[bp-0x132]                               ; 0B17
        push ss                                         ; 0B1B
        push ax                                         ; 0B1C
        mov ax,0x100                                    ; 0B1D
        push ax                                         ; 0B20
        callp R3_0B22, 0xFFFF, 0x0000                   ; 0B21 USER.LoadString
        lea ax,[bp-0x332]                               ; 0B26
        push ss                                         ; 0B2A
        push ax                                         ; 0B2B
        lea ax,[bp-0x132]                               ; 0B2C
        push ss                                         ; 0B30
        push ax                                         ; 0B31
        lea ax,[bp+0xa]                                 ; 0B32
        push ss                                         ; 0B35
        push ax                                         ; 0B36
        callp R3_0B38, 0xFFFF, 0x0000                   ; 0B37 USER.wvsprintf
        push word [bp+0x6]                              ; 0B3C
        lea ax,[bp-0x332]                               ; 0B3F
        push ss                                         ; 0B43
        push ax                                         ; 0B44
        lea ax,[bp-0x32]                                ; 0B45
        push ss                                         ; 0B48
        push ax                                         ; 0B49
        mov ax,0x40                                     ; 0B4A
        push ax                                         ; 0B4D
        callp R3_0B4F, 0xFFFF, 0x0000                   ; 0B4E USER.MessageBox

L3_0B53:
        mov_ sp,bp                                      ; 0B53
        pop bp                                          ; 0B55
        retf                                            ; 0B56
        db 0x90                                         ; 0B57

; "The ES1869.VXD driver is not present..." / "...out of date" boxes
vxd_check:
        mov dx,[0xc8]                                   ; 0B58
        or_ dx,dx                                       ; 0B5C
        jz short L3_0B91                                ; 0B5E
        mov_ ax,dx                                      ; 0B60
        dec ax                                          ; 0B62
        jz short L3_0B6F                                ; 0B63
        dec ax                                          ; 0B65
        jz short L3_0B74                                ; 0B66
        dec ax                                          ; 0B68
        jz short L3_0B79                                ; 0B69
        xor_ cx,cx                                      ; 0B6B
        jmp short L3_0B7C                               ; 0B6D

L3_0B6F:
        mov cx,0x1b                                     ; 0B6F
        jmp short L3_0B7C                               ; 0B72

L3_0B74:
        mov cx,0x1c                                     ; 0B74
        jmp short L3_0B7C                               ; 0B77

L3_0B79:
        mov cx,0x1d                                     ; 0B79

L3_0B7C:
        or_ cx,cx                                       ; 0B7C
        jz short L3_0B8B                                ; 0B7E
        push cx                                         ; 0B80
        xor_ ax,ax                                      ; 0B81
        push ax                                         ; 0B83
        push cs                                         ; 0B84
        call L3_0AEC                                    ; 0B85
        add sp,byte +0x4                                ; 0B88

L3_0B8B:
        mov word [0xc8],0x0                             ; 0B8B

L3_0B91:
        retf                                            ; 0B91

L3_0B92:
        push bp                                         ; 0B92
        mov_ bp,sp                                      ; 0B93
        sub sp,0xca                                     ; 0B95
        mov byte [bp-0x2],0x0                           ; 0B99
        sub_ ax,ax                                      ; 0B9D
        push ax                                         ; 0B9F
        push ax                                         ; 0BA0
        mov ax,0xc9                                     ; 0BA1
        cwd                                             ; 0BA4
        push dx                                         ; 0BA5
        push ax                                         ; 0BA6
        lea ax,[bp-0xca]                                ; 0BA7
        push ss                                         ; 0BAB
        push ax                                         ; 0BAC
        mov bx,[bp+0x6]                                 ; 0BAD
        push word [bx+0x16]                             ; 0BB0
        push word [bx+0x14]                             ; 0BB3
        call L3_09B8                                    ; 0BB6
        add sp,byte +0x10                               ; 0BB9
        or_ ax,ax                                       ; 0BBC
        jz short L3_0BC4                                ; 0BBE

L3_0BC0:
        xor_ ax,ax                                      ; 0BC0
        jmp short L3_0BE5                               ; 0BC2

L3_0BC4:
        mov ax,0xd2                                     ; 0BC4
        push ds                                         ; 0BC7
        push ax                                         ; 0BC8
        lea ax,[bp-0xca]                                ; 0BC9
        push ss                                         ; 0BCD
        push ax                                         ; 0BCE
        callp R3_0BD0, R3_0C2A, 0x0000                  ; 0BCF USER.AnsiUpper
        push dx                                         ; 0BD4
        push ax                                         ; 0BD5
        callf L1_2504, R3_0BD9, R3_0C33                 ; 0BD6 far seg1
        add sp,byte +0x8                                ; 0BDB
        or_ dx,ax                                       ; 0BDE
        jz short L3_0BC0                                ; 0BE0
        mov ax,0x1                                      ; 0BE2

L3_0BE5:
        mov_ sp,bp                                      ; 0BE5
        pop bp                                          ; 0BE7
        retf 0x2                                        ; 0BE8
        db 0x90                                         ; 0BEB

L3_0BEC:
        push bp                                         ; 0BEC
        mov_ bp,sp                                      ; 0BED
        sub sp,0xca                                     ; 0BEF
        mov byte [bp-0x2],0x0                           ; 0BF3
        sub_ ax,ax                                      ; 0BF7
        push ax                                         ; 0BF9
        push ax                                         ; 0BFA
        mov ax,0xc9                                     ; 0BFB
        cwd                                             ; 0BFE
        push dx                                         ; 0BFF
        push ax                                         ; 0C00
        lea ax,[bp-0xca]                                ; 0C01
        push ss                                         ; 0C05
        push ax                                         ; 0C06
        mov bx,[bp+0x6]                                 ; 0C07
        push word [bx+0x16]                             ; 0C0A
        push word [bx+0x14]                             ; 0C0D
        call L3_09B8                                    ; 0C10
        add sp,byte +0x10                               ; 0C13
        or_ ax,ax                                       ; 0C16
        jz short L3_0C1E                                ; 0C18

L3_0C1A:
        xor_ ax,ax                                      ; 0C1A
        jmp short L3_0C3F                               ; 0C1C

L3_0C1E:
        mov ax,0xda                                     ; 0C1E
        push ds                                         ; 0C21
        push ax                                         ; 0C22
        lea ax,[bp-0xca]                                ; 0C23
        push ss                                         ; 0C27
        push ax                                         ; 0C28
        callp R3_0C2A, R3_0C84, 0x0000                  ; 0C29 USER.AnsiUpper
        push dx                                         ; 0C2E
        push ax                                         ; 0C2F
        callf L1_2504, R3_0C33, R3_0C8D                 ; 0C30 far seg1
        add sp,byte +0x8                                ; 0C35
        or_ dx,ax                                       ; 0C38
        jz short L3_0C1A                                ; 0C3A
        mov ax,0x1                                      ; 0C3C

L3_0C3F:
        mov_ sp,bp                                      ; 0C3F
        pop bp                                          ; 0C41
        retf 0x2                                        ; 0C42
        db 0x90                                         ; 0C45

L3_0C46:
        push bp                                         ; 0C46
        mov_ bp,sp                                      ; 0C47
        sub sp,0xca                                     ; 0C49
        mov byte [bp-0x2],0x0                           ; 0C4D
        sub_ ax,ax                                      ; 0C51
        push ax                                         ; 0C53
        push ax                                         ; 0C54
        mov ax,0xc9                                     ; 0C55
        cwd                                             ; 0C58
        push dx                                         ; 0C59
        push ax                                         ; 0C5A
        lea ax,[bp-0xca]                                ; 0C5B
        push ss                                         ; 0C5F
        push ax                                         ; 0C60
        mov bx,[bp+0x6]                                 ; 0C61
        push word [bx+0x16]                             ; 0C64
        push word [bx+0x14]                             ; 0C67
        call L3_09B8                                    ; 0C6A
        add sp,byte +0x10                               ; 0C6D
        or_ ax,ax                                       ; 0C70
        jz short L3_0C78                                ; 0C72

L3_0C74:
        xor_ ax,ax                                      ; 0C74
        jmp short L3_0C99                               ; 0C76

L3_0C78:
        mov ax,0xe2                                     ; 0C78
        push ds                                         ; 0C7B
        push ax                                         ; 0C7C
        lea ax,[bp-0xca]                                ; 0C7D
        push ss                                         ; 0C81
        push ax                                         ; 0C82
        callp R3_0C84, R3_0CDE, 0x0000                  ; 0C83 USER.AnsiUpper
        push dx                                         ; 0C88
        push ax                                         ; 0C89
        callf L1_2504, R3_0C8D, R3_0CE7                 ; 0C8A far seg1
        add sp,byte +0x8                                ; 0C8F
        or_ dx,ax                                       ; 0C92
        jz short L3_0C74                                ; 0C94
        mov ax,0x1                                      ; 0C96

L3_0C99:
        mov_ sp,bp                                      ; 0C99
        pop bp                                          ; 0C9B
        retf 0x2                                        ; 0C9C
        db 0x90                                         ; 0C9F

L3_0CA0:
        push bp                                         ; 0CA0
        mov_ bp,sp                                      ; 0CA1
        sub sp,0xca                                     ; 0CA3
        mov byte [bp-0x2],0x0                           ; 0CA7
        sub_ ax,ax                                      ; 0CAB
        push ax                                         ; 0CAD
        push ax                                         ; 0CAE
        mov ax,0xc9                                     ; 0CAF
        cwd                                             ; 0CB2
        push dx                                         ; 0CB3
        push ax                                         ; 0CB4
        lea ax,[bp-0xca]                                ; 0CB5
        push ss                                         ; 0CB9
        push ax                                         ; 0CBA
        mov bx,[bp+0x6]                                 ; 0CBB
        push word [bx+0x16]                             ; 0CBE
        push word [bx+0x14]                             ; 0CC1
        call L3_09B8                                    ; 0CC4
        add sp,byte +0x10                               ; 0CC7
        or_ ax,ax                                       ; 0CCA
        jz short L3_0CD2                                ; 0CCC

L3_0CCE:
        xor_ ax,ax                                      ; 0CCE
        jmp short L3_0CF3                               ; 0CD0

L3_0CD2:
        mov ax,0xea                                     ; 0CD2
        push ds                                         ; 0CD5
        push ax                                         ; 0CD6
        lea ax,[bp-0xca]                                ; 0CD7
        push ss                                         ; 0CDB
        push ax                                         ; 0CDC
        callp R3_0CDE, R3_0D38, 0x0000                  ; 0CDD USER.AnsiUpper
        push dx                                         ; 0CE2
        push ax                                         ; 0CE3
        callf L1_2504, R3_0CE7, R3_0D41                 ; 0CE4 far seg1
        add sp,byte +0x8                                ; 0CE9
        or_ dx,ax                                       ; 0CEC
        jz short L3_0CCE                                ; 0CEE
        mov ax,0x1                                      ; 0CF0

L3_0CF3:
        mov_ sp,bp                                      ; 0CF3
        pop bp                                          ; 0CF5
        retf 0x2                                        ; 0CF6
        db 0x90                                         ; 0CF9
        push bp                                         ; 0CFA
        mov_ bp,sp                                      ; 0CFB
        sub sp,0xca                                     ; 0CFD
        mov byte [bp-0x2],0x0                           ; 0D01
        sub_ ax,ax                                      ; 0D05
        push ax                                         ; 0D07
        push ax                                         ; 0D08
        mov ax,0xc9                                     ; 0D09
        cwd                                             ; 0D0C
        push dx                                         ; 0D0D
        push ax                                         ; 0D0E
        lea ax,[bp-0xca]                                ; 0D0F
        push ss                                         ; 0D13
        push ax                                         ; 0D14
        mov bx,[bp+0x6]                                 ; 0D15
        push word [bx+0x16]                             ; 0D18
        push word [bx+0x14]                             ; 0D1B
        call L3_09B8                                    ; 0D1E
        add sp,byte +0x10                               ; 0D21
        or_ ax,ax                                       ; 0D24
        jz short L3_0D2C                                ; 0D26

L3_0D28:
        xor_ ax,ax                                      ; 0D28
        jmp short L3_0D4D                               ; 0D2A

L3_0D2C:
        mov ax,0xf2                                     ; 0D2C
        push ds                                         ; 0D2F
        push ax                                         ; 0D30
        lea ax,[bp-0xca]                                ; 0D31
        push ss                                         ; 0D35
        push ax                                         ; 0D36
        callp R3_0D38, 0xFFFF, 0x0000                   ; 0D37 USER.AnsiUpper
        push dx                                         ; 0D3C
        push ax                                         ; 0D3D
        callf L1_2504, R3_0D41, R3_0303                 ; 0D3E far seg1
        add sp,byte +0x8                                ; 0D43
        or_ dx,ax                                       ; 0D46
        jz short L3_0D28                                ; 0D48
        mov ax,0x1                                      ; 0D4A

L3_0D4D:
        mov_ sp,bp                                      ; 0D4D
        pop bp                                          ; 0D4F
        retf 0x2                                        ; 0D50
        db 0x90                                         ; 0D53

; the mixer state to the registry as REG_BINARY, at the last disable and at suspend
save_mixer_state:
        push bp                                         ; 0D54
        mov_ bp,sp                                      ; 0D55
        sub sp,0x112                                    ; 0D57
        push di                                         ; 0D5B
        push si                                         ; 0D5C
        mov di,[bp+0x6]                                 ; 0D5D
        mov ax,0x1                                      ; 0D60
        cwd                                             ; 0D63
        push dx                                         ; 0D64
        push ax                                         ; 0D65
        mov ax,0x100                                    ; 0D66
        cwd                                             ; 0D69
        push dx                                         ; 0D6A
        push ax                                         ; 0D6B
        lea ax,[bp-0x112]                               ; 0D6C
        push ss                                         ; 0D70
        push ax                                         ; 0D71
        sub_ ax,ax                                      ; 0D72
        push ax                                         ; 0D74
        push ax                                         ; 0D75
        push word [di+0x16]                             ; 0D76
        push word [di+0x14]                             ; 0D79
        call cm_get_devnode_key                         ; 0D7C
        add sp,byte +0x14                               ; 0D7F
        or_ ax,ax                                       ; 0D82
        jz short L3_0D8C                                ; 0D84
        mov ax,0xf                                      ; 0D86
        jmp near L3_18E1                                ; 0D89

L3_0D8C:
        lea ax,[bp-0x112]                               ; 0D8C
        push ss                                         ; 0D90
        push ax                                         ; 0D91
        mov ax,0x69e                                    ; 0D92
        push cs                                         ; 0D95
        push ax                                         ; 0D96
        callp R3_0D98, 0xFFFF, 0x0000                   ; 0D97 KERNEL.lstrcat
        mov ax,0x2                                      ; 0D9C
        mov dx,0x8000                                   ; 0D9F
        push dx                                         ; 0DA2
        push ax                                         ; 0DA3
        lea ax,[bp-0x112]                               ; 0DA4
        push ss                                         ; 0DA8
        push ax                                         ; 0DA9
        lea ax,[bp-0xe]                                 ; 0DAA
        push ss                                         ; 0DAD
        push ax                                         ; 0DAE
        callp R3_0DB0, 0xFFFF, 0x0000                   ; 0DAF KERNEL.RegCreateKey
        or_ dx,ax                                       ; 0DB4
        jz short L3_0DBB                                ; 0DB6
        jmp near L3_186F                                ; 0DB8

L3_0DBB:
        mov ax,[di+0x26]                                ; 0DBB
        mov dx,[di+0x28]                                ; 0DBE
        mov_ si,ax                                      ; 0DC1
        mov [bp-0x2],dx                                 ; 0DC3
        push word [bp-0xc]                              ; 0DC6
        push word [bp-0xe]                              ; 0DC9
        mov cx,0x521                                    ; 0DCC
        push cs                                         ; 0DCF
        push cx                                         ; 0DD0
        sub_ cx,cx                                      ; 0DD1
        push cx                                         ; 0DD3
        push cx                                         ; 0DD4
        mov cx,0x3                                      ; 0DD5
        xor_ bx,bx                                      ; 0DD8
        push bx                                         ; 0DDA
        push cx                                         ; 0DDB
        add ax,0x2914                                   ; 0DDC
        push dx                                         ; 0DDF
        push ax                                         ; 0DE0
        mov ax,0x4                                      ; 0DE1
        cwd                                             ; 0DE4
        push dx                                         ; 0DE5
        push ax                                         ; 0DE6
        callp R3_0DE8, R3_0E64, 0x0000                  ; 0DE7 KERNEL.RegSetValueEx
        mov word [bp-0x6],0x0                           ; 0DEC
        sub_ ax,ax                                      ; 0DF1
        mov [bp-0x10],ax                                ; 0DF3
        mov [bp-0x12],ax                                ; 0DF6
        mov es,[bp-0x2]                                 ; 0DF9
        cmp [es:si+0x69e],ax                            ; 0DFC
        jz short L3_0E42                                ; 0E01
        lea ax,[si+0x28d4]                              ; 0E03
        mov [bp-0x8],es                                 ; 0E07
        mov bx,[bp-0x6]                                 ; 0E0A
        mov [bp-0xa],ax                                 ; 0E0D
        mov [bp-0x4],si                                 ; 0E10
        mov_ si,ax                                      ; 0E13

L3_0E15:
        mov es,[bp-0x8]                                 ; 0E15
        mov ax,[es:si+0x2]                              ; 0E18
        or ax,[es:si]                                   ; 0E1C
        jz short L3_0E2F                                ; 0E1F
        mov ax,0x1                                      ; 0E21
        mov_ cx,bx                                      ; 0E24
        shl ax,cl                                       ; 0E26
        cwd                                             ; 0E28
        or [bp-0x12],ax                                 ; 0E29
        or [bp-0x10],dx                                 ; 0E2C

L3_0E2F:
        add si,byte +0x8                                ; 0E2F
        inc bx                                          ; 0E32
        les di,[bp-0x4]                                 ; 0E33
        cmp [es:di+0x69e],bx                            ; 0E36
        ja short L3_0E15                                ; 0E3B
        mov_ si,di                                      ; 0E3D
        mov di,[bp+0x6]                                 ; 0E3F

L3_0E42:
        push word [bp-0xc]                              ; 0E42
        push word [bp-0xe]                              ; 0E45
        mov ax,0x4fa                                    ; 0E48
        push cs                                         ; 0E4B
        push ax                                         ; 0E4C
        sub_ ax,ax                                      ; 0E4D
        push ax                                         ; 0E4F
        push ax                                         ; 0E50
        mov ax,0x3                                      ; 0E51
        cwd                                             ; 0E54
        push dx                                         ; 0E55
        push ax                                         ; 0E56
        lea cx,[bp-0x12]                                ; 0E57
        push ss                                         ; 0E5A
        push cx                                         ; 0E5B
        mov cx,0x4                                      ; 0E5C
        xor_ bx,bx                                      ; 0E5F
        push bx                                         ; 0E61
        push cx                                         ; 0E62
        callp R3_0E64, R3_0E90, 0x0000                  ; 0E63 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0E68
        push word [bp-0xe]                              ; 0E6B
        mov ax,0x8c5                                    ; 0E6E
        push cs                                         ; 0E71
        push ax                                         ; 0E72
        sub_ ax,ax                                      ; 0E73
        push ax                                         ; 0E75
        push ax                                         ; 0E76
        mov ax,0x3                                      ; 0E77
        cwd                                             ; 0E7A
        push dx                                         ; 0E7B
        push ax                                         ; 0E7C
        mov_ cx,si                                      ; 0E7D
        mov bx,[bp-0x2]                                 ; 0E7F
        add cx,0x280c                                   ; 0E82
        push bx                                         ; 0E86
        push cx                                         ; 0E87
        mov cx,0x4                                      ; 0E88
        xor_ bx,bx                                      ; 0E8B
        push bx                                         ; 0E8D
        push cx                                         ; 0E8E
        callp R3_0E90, R3_0EBC, 0x0000                  ; 0E8F KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0E94
        push word [bp-0xe]                              ; 0E97
        mov ax,0x89e                                    ; 0E9A
        push cs                                         ; 0E9D
        push ax                                         ; 0E9E
        sub_ ax,ax                                      ; 0E9F
        push ax                                         ; 0EA1
        push ax                                         ; 0EA2
        mov ax,0x3                                      ; 0EA3
        cwd                                             ; 0EA6
        push dx                                         ; 0EA7
        push ax                                         ; 0EA8
        mov_ cx,si                                      ; 0EA9
        mov bx,[bp-0x2]                                 ; 0EAB
        add cx,0x2864                                   ; 0EAE
        push bx                                         ; 0EB2
        push cx                                         ; 0EB3
        mov cx,0x4                                      ; 0EB4
        xor_ bx,bx                                      ; 0EB7
        push bx                                         ; 0EB9
        push cx                                         ; 0EBA
        callp R3_0EBC, R3_0EE8, 0x0000                  ; 0EBB KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0EC0
        push word [bp-0xe]                              ; 0EC3
        mov ax,0x989                                    ; 0EC6
        push cs                                         ; 0EC9
        push ax                                         ; 0ECA
        sub_ ax,ax                                      ; 0ECB
        push ax                                         ; 0ECD
        push ax                                         ; 0ECE
        mov ax,0x3                                      ; 0ECF
        cwd                                             ; 0ED2
        push dx                                         ; 0ED3
        push ax                                         ; 0ED4
        mov_ cx,si                                      ; 0ED5
        mov bx,[bp-0x2]                                 ; 0ED7
        add cx,0x2868                                   ; 0EDA
        push bx                                         ; 0EDE
        push cx                                         ; 0EDF
        mov cx,0x4                                      ; 0EE0
        xor_ bx,bx                                      ; 0EE3
        push bx                                         ; 0EE5
        push cx                                         ; 0EE6
        callp R3_0EE8, R3_0F14, 0x0000                  ; 0EE7 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0EEC
        push word [bp-0xe]                              ; 0EEF
        mov ax,0x86d                                    ; 0EF2
        push cs                                         ; 0EF5
        push ax                                         ; 0EF6
        sub_ ax,ax                                      ; 0EF7
        push ax                                         ; 0EF9
        push ax                                         ; 0EFA
        mov ax,0x3                                      ; 0EFB
        cwd                                             ; 0EFE
        push dx                                         ; 0EFF
        push ax                                         ; 0F00
        mov_ cx,si                                      ; 0F01
        mov bx,[bp-0x2]                                 ; 0F03
        add cx,0x2824                                   ; 0F06
        push bx                                         ; 0F0A
        push cx                                         ; 0F0B
        mov cx,0x4                                      ; 0F0C
        xor_ bx,bx                                      ; 0F0F
        push bx                                         ; 0F11
        push cx                                         ; 0F12
        callp R3_0F14, R3_0F40, 0x0000                  ; 0F13 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0F18
        push word [bp-0xe]                              ; 0F1B
        mov ax,0x95f                                    ; 0F1E
        push cs                                         ; 0F21
        push ax                                         ; 0F22
        sub_ ax,ax                                      ; 0F23
        push ax                                         ; 0F25
        push ax                                         ; 0F26
        mov ax,0x3                                      ; 0F27
        cwd                                             ; 0F2A
        push dx                                         ; 0F2B
        push ax                                         ; 0F2C
        mov_ cx,si                                      ; 0F2D
        mov bx,[bp-0x2]                                 ; 0F2F
        add cx,0x2828                                   ; 0F32
        push bx                                         ; 0F36
        push cx                                         ; 0F37
        mov cx,0x4                                      ; 0F38
        xor_ bx,bx                                      ; 0F3B
        push bx                                         ; 0F3D
        push cx                                         ; 0F3E
        callp R3_0F40, R3_0F6C, 0x0000                  ; 0F3F KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0F44
        push word [bp-0xe]                              ; 0F47
        mov ax,0x84f                                    ; 0F4A
        push cs                                         ; 0F4D
        push ax                                         ; 0F4E
        sub_ ax,ax                                      ; 0F4F
        push ax                                         ; 0F51
        push ax                                         ; 0F52
        mov ax,0x3                                      ; 0F53
        cwd                                             ; 0F56
        push dx                                         ; 0F57
        push ax                                         ; 0F58
        mov_ cx,si                                      ; 0F59
        mov bx,[bp-0x2]                                 ; 0F5B
        add cx,0x282c                                   ; 0F5E
        push bx                                         ; 0F62
        push cx                                         ; 0F63
        mov cx,0x4                                      ; 0F64
        xor_ bx,bx                                      ; 0F67
        push bx                                         ; 0F69
        push cx                                         ; 0F6A
        callp R3_0F6C, R3_0F98, 0x0000                  ; 0F6B KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0F70
        push word [bp-0xe]                              ; 0F73
        mov ax,0x946                                    ; 0F76
        push cs                                         ; 0F79
        push ax                                         ; 0F7A
        sub_ ax,ax                                      ; 0F7B
        push ax                                         ; 0F7D
        push ax                                         ; 0F7E
        mov ax,0x3                                      ; 0F7F
        cwd                                             ; 0F82
        push dx                                         ; 0F83
        push ax                                         ; 0F84
        mov_ cx,si                                      ; 0F85
        mov bx,[bp-0x2]                                 ; 0F87
        add cx,0x2830                                   ; 0F8A
        push bx                                         ; 0F8E
        push cx                                         ; 0F8F
        mov cx,0x4                                      ; 0F90
        xor_ bx,bx                                      ; 0F93
        push bx                                         ; 0F95
        push cx                                         ; 0F96
        callp R3_0F98, R3_0FC4, 0x0000                  ; 0F97 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0F9C
        push word [bp-0xe]                              ; 0F9F
        mov ax,0x81f                                    ; 0FA2
        push cs                                         ; 0FA5
        push ax                                         ; 0FA6
        sub_ ax,ax                                      ; 0FA7
        push ax                                         ; 0FA9
        push ax                                         ; 0FAA
        mov ax,0x3                                      ; 0FAB
        cwd                                             ; 0FAE
        push dx                                         ; 0FAF
        push ax                                         ; 0FB0
        mov_ cx,si                                      ; 0FB1
        mov bx,[bp-0x2]                                 ; 0FB3
        add cx,0x2834                                   ; 0FB6
        push bx                                         ; 0FBA
        push cx                                         ; 0FBB
        mov cx,0x4                                      ; 0FBC
        xor_ bx,bx                                      ; 0FBF
        push bx                                         ; 0FC1
        push cx                                         ; 0FC2
        callp R3_0FC4, R3_0FF0, 0x0000                  ; 0FC3 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0FC8
        push word [bp-0xe]                              ; 0FCB
        mov ax,0x924                                    ; 0FCE
        push cs                                         ; 0FD1
        push ax                                         ; 0FD2
        sub_ ax,ax                                      ; 0FD3
        push ax                                         ; 0FD5
        push ax                                         ; 0FD6
        mov ax,0x3                                      ; 0FD7
        cwd                                             ; 0FDA
        push dx                                         ; 0FDB
        push ax                                         ; 0FDC
        mov_ cx,si                                      ; 0FDD
        mov bx,[bp-0x2]                                 ; 0FDF
        add cx,0x2838                                   ; 0FE2
        push bx                                         ; 0FE6
        push cx                                         ; 0FE7
        mov cx,0x4                                      ; 0FE8
        xor_ bx,bx                                      ; 0FEB
        push bx                                         ; 0FED
        push cx                                         ; 0FEE
        callp R3_0FF0, R3_101C, 0x0000                  ; 0FEF KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 0FF4
        push word [bp-0xe]                              ; 0FF7
        mov ax,0x7ed                                    ; 0FFA
        push cs                                         ; 0FFD
        push ax                                         ; 0FFE
        sub_ ax,ax                                      ; 0FFF
        push ax                                         ; 1001
        push ax                                         ; 1002
        mov ax,0x3                                      ; 1003
        cwd                                             ; 1006
        push dx                                         ; 1007
        push ax                                         ; 1008
        mov_ cx,si                                      ; 1009
        mov bx,[bp-0x2]                                 ; 100B
        add cx,0x283c                                   ; 100E
        push bx                                         ; 1012
        push cx                                         ; 1013
        mov cx,0x4                                      ; 1014
        xor_ bx,bx                                      ; 1017
        push bx                                         ; 1019
        push cx                                         ; 101A
        callp R3_101C, R3_1048, 0x0000                  ; 101B KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1020
        push word [bp-0xe]                              ; 1023
        mov ax,0x8f8                                    ; 1026
        push cs                                         ; 1029
        push ax                                         ; 102A
        sub_ ax,ax                                      ; 102B
        push ax                                         ; 102D
        push ax                                         ; 102E
        mov ax,0x3                                      ; 102F
        cwd                                             ; 1032
        push dx                                         ; 1033
        push ax                                         ; 1034
        mov_ cx,si                                      ; 1035
        mov bx,[bp-0x2]                                 ; 1037
        add cx,0x2840                                   ; 103A
        push bx                                         ; 103E
        push cx                                         ; 103F
        mov cx,0x4                                      ; 1040
        xor_ bx,bx                                      ; 1043
        push bx                                         ; 1045
        push cx                                         ; 1046
        callp R3_1048, R3_1074, 0x0000                  ; 1047 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 104C
        push word [bp-0xe]                              ; 104F
        mov ax,0x7c0                                    ; 1052
        push cs                                         ; 1055
        push ax                                         ; 1056
        sub_ ax,ax                                      ; 1057
        push ax                                         ; 1059
        push ax                                         ; 105A
        mov ax,0x3                                      ; 105B
        cwd                                             ; 105E
        push dx                                         ; 105F
        push ax                                         ; 1060
        mov_ cx,si                                      ; 1061
        mov bx,[bp-0x2]                                 ; 1063
        add cx,0x2844                                   ; 1066
        push bx                                         ; 106A
        push cx                                         ; 106B
        mov cx,0x4                                      ; 106C
        xor_ bx,bx                                      ; 106F
        push bx                                         ; 1071
        push cx                                         ; 1072
        callp R3_1074, R3_10A0, 0x0000                  ; 1073 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1078
        push word [bp-0xe]                              ; 107B
        mov ax,0x8d2                                    ; 107E
        push cs                                         ; 1081
        push ax                                         ; 1082
        sub_ ax,ax                                      ; 1083
        push ax                                         ; 1085
        push ax                                         ; 1086
        mov ax,0x3                                      ; 1087
        cwd                                             ; 108A
        push dx                                         ; 108B
        push ax                                         ; 108C
        mov_ cx,si                                      ; 108D
        mov bx,[bp-0x2]                                 ; 108F
        add cx,0x2848                                   ; 1092
        push bx                                         ; 1096
        push cx                                         ; 1097
        mov cx,0x4                                      ; 1098
        xor_ bx,bx                                      ; 109B
        push bx                                         ; 109D
        push cx                                         ; 109E
        callp R3_10A0, R3_10CC, 0x0000                  ; 109F KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 10A4
        push word [bp-0xe]                              ; 10A7
        mov ax,0x797                                    ; 10AA
        push cs                                         ; 10AD
        push ax                                         ; 10AE
        sub_ ax,ax                                      ; 10AF
        push ax                                         ; 10B1
        push ax                                         ; 10B2
        mov ax,0x3                                      ; 10B3
        cwd                                             ; 10B6
        push dx                                         ; 10B7
        push ax                                         ; 10B8
        mov_ cx,si                                      ; 10B9
        mov bx,[bp-0x2]                                 ; 10BB
        add cx,0x284c                                   ; 10BE
        push bx                                         ; 10C2
        push cx                                         ; 10C3
        mov cx,0x4                                      ; 10C4
        xor_ bx,bx                                      ; 10C7
        push bx                                         ; 10C9
        push cx                                         ; 10CA
        callp R3_10CC, R3_10F6, 0x0000                  ; 10CB KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 10D0
        push word [bp-0xe]                              ; 10D3
        mov ax,0x8ac                                    ; 10D6
        push cs                                         ; 10D9
        push ax                                         ; 10DA
        sub_ ax,ax                                      ; 10DB
        push ax                                         ; 10DD
        push ax                                         ; 10DE
        mov ax,0x3                                      ; 10DF
        cwd                                             ; 10E2
        push dx                                         ; 10E3
        push ax                                         ; 10E4
        mov_ ax,si                                      ; 10E5
        mov dx,[bp-0x2]                                 ; 10E7
        add ax,0x2850                                   ; 10EA
        push dx                                         ; 10ED
        push ax                                         ; 10EE
        mov ax,0x4                                      ; 10EF
        cwd                                             ; 10F2
        push dx                                         ; 10F3
        push ax                                         ; 10F4
        callp R3_10F6, 0xFFFF, 0x0000                   ; 10F5 KERNEL.RegSetValueEx
        test byte [di+0x2b],0x40                        ; 10FA
        jnz short L3_1156                               ; 10FE
        push word [bp-0xc]                              ; 1100
        push word [bp-0xe]                              ; 1103
        mov ax,0x77f                                    ; 1106
        push cs                                         ; 1109
        push ax                                         ; 110A
        sub_ ax,ax                                      ; 110B
        push ax                                         ; 110D
        push ax                                         ; 110E
        mov ax,0x3                                      ; 110F
        cwd                                             ; 1112
        push dx                                         ; 1113
        push ax                                         ; 1114
        mov_ cx,si                                      ; 1115
        mov bx,[bp-0x2]                                 ; 1117
        add cx,0x2854                                   ; 111A
        push bx                                         ; 111E
        push cx                                         ; 111F
        mov cx,0x4                                      ; 1120
        xor_ bx,bx                                      ; 1123
        push bx                                         ; 1125
        push cx                                         ; 1126
        callp R3_1128, R3_1152, 0x0000                  ; 1127 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 112C
        push word [bp-0xe]                              ; 112F
        mov ax,0x88b                                    ; 1132
        push cs                                         ; 1135
        push ax                                         ; 1136
        sub_ ax,ax                                      ; 1137
        push ax                                         ; 1139
        push ax                                         ; 113A
        mov ax,0x3                                      ; 113B
        cwd                                             ; 113E
        push dx                                         ; 113F
        push ax                                         ; 1140
        mov_ ax,si                                      ; 1141
        mov dx,[bp-0x2]                                 ; 1143
        add ax,0x2858                                   ; 1146
        push dx                                         ; 1149
        push ax                                         ; 114A
        mov ax,0x4                                      ; 114B
        cwd                                             ; 114E
        push dx                                         ; 114F
        push ax                                         ; 1150
        callp R3_1152, R3_117E, 0x0000                  ; 1151 KERNEL.RegSetValueEx

L3_1156:
        push word [bp-0xc]                              ; 1156
        push word [bp-0xe]                              ; 1159
        mov ax,0x758                                    ; 115C
        push cs                                         ; 115F
        push ax                                         ; 1160
        sub_ ax,ax                                      ; 1161
        push ax                                         ; 1163
        push ax                                         ; 1164
        mov ax,0x3                                      ; 1165
        cwd                                             ; 1168
        push dx                                         ; 1169
        push ax                                         ; 116A
        mov_ cx,si                                      ; 116B
        mov bx,[bp-0x2]                                 ; 116D
        add cx,0x285c                                   ; 1170
        push bx                                         ; 1174
        push cx                                         ; 1175
        mov cx,0x4                                      ; 1176
        xor_ bx,bx                                      ; 1179
        push bx                                         ; 117B
        push cx                                         ; 117C
        callp R3_117E, R3_11AA, 0x0000                  ; 117D KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1182
        push word [bp-0xe]                              ; 1185
        mov ax,0x61b                                    ; 1188
        push cs                                         ; 118B
        push ax                                         ; 118C
        sub_ ax,ax                                      ; 118D
        push ax                                         ; 118F
        push ax                                         ; 1190
        mov ax,0x3                                      ; 1191
        cwd                                             ; 1194
        push dx                                         ; 1195
        push ax                                         ; 1196
        mov_ cx,si                                      ; 1197
        mov bx,[bp-0x2]                                 ; 1199
        add cx,0x2934                                   ; 119C
        push bx                                         ; 11A0
        push cx                                         ; 11A1
        mov cx,0x4                                      ; 11A2
        xor_ bx,bx                                      ; 11A5
        push bx                                         ; 11A7
        push cx                                         ; 11A8
        callp R3_11AA, R3_11D6, 0x0000                  ; 11A9 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 11AE
        push word [bp-0xe]                              ; 11B1
        mov ax,0x5ff                                    ; 11B4
        push cs                                         ; 11B7
        push ax                                         ; 11B8
        sub_ ax,ax                                      ; 11B9
        push ax                                         ; 11BB
        push ax                                         ; 11BC
        mov ax,0x3                                      ; 11BD
        cwd                                             ; 11C0
        push dx                                         ; 11C1
        push ax                                         ; 11C2
        mov_ cx,si                                      ; 11C3
        mov bx,[bp-0x2]                                 ; 11C5
        add cx,0x2814                                   ; 11C8
        push bx                                         ; 11CC
        push cx                                         ; 11CD
        mov cx,0x4                                      ; 11CE
        xor_ bx,bx                                      ; 11D1
        push bx                                         ; 11D3
        push cx                                         ; 11D4
        callp R3_11D6, R3_1202, 0x0000                  ; 11D5 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 11DA
        push word [bp-0xe]                              ; 11DD
        mov ax,0x5db                                    ; 11E0
        push cs                                         ; 11E3
        push ax                                         ; 11E4
        sub_ ax,ax                                      ; 11E5
        push ax                                         ; 11E7
        push ax                                         ; 11E8
        mov ax,0x3                                      ; 11E9
        cwd                                             ; 11EC
        push dx                                         ; 11ED
        push ax                                         ; 11EE
        mov_ cx,si                                      ; 11EF
        mov bx,[bp-0x2]                                 ; 11F1
        add cx,0x289c                                   ; 11F4
        push bx                                         ; 11F8
        push cx                                         ; 11F9
        mov cx,0x4                                      ; 11FA
        xor_ bx,bx                                      ; 11FD
        push bx                                         ; 11FF
        push cx                                         ; 1200
        callp R3_1202, R3_122E, 0x0000                  ; 1201 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1206
        push word [bp-0xe]                              ; 1209
        mov ax,0x85a                                    ; 120C
        push cs                                         ; 120F
        push ax                                         ; 1210
        sub_ ax,ax                                      ; 1211
        push ax                                         ; 1213
        push ax                                         ; 1214
        mov ax,0x3                                      ; 1215
        cwd                                             ; 1218
        push dx                                         ; 1219
        push ax                                         ; 121A
        mov_ cx,si                                      ; 121B
        mov bx,[bp-0x2]                                 ; 121D
        add cx,0x28a0                                   ; 1220
        push bx                                         ; 1224
        push cx                                         ; 1225
        mov cx,0x4                                      ; 1226
        xor_ bx,bx                                      ; 1229
        push bx                                         ; 122B
        push cx                                         ; 122C
        callp R3_122E, R3_125A, 0x0000                  ; 122D KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1232
        push word [bp-0xe]                              ; 1235
        mov ax,0x5bb                                    ; 1238
        push cs                                         ; 123B
        push ax                                         ; 123C
        sub_ ax,ax                                      ; 123D
        push ax                                         ; 123F
        push ax                                         ; 1240
        mov ax,0x3                                      ; 1241
        cwd                                             ; 1244
        push dx                                         ; 1245
        push ax                                         ; 1246
        mov_ cx,si                                      ; 1247
        mov bx,[bp-0x2]                                 ; 1249
        add cx,0x286c                                   ; 124C
        push bx                                         ; 1250
        push cx                                         ; 1251
        mov cx,0x4                                      ; 1252
        xor_ bx,bx                                      ; 1255
        push bx                                         ; 1257
        push cx                                         ; 1258
        callp R3_125A, R3_1286, 0x0000                  ; 1259 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 125E
        push word [bp-0xe]                              ; 1261
        mov ax,0x82a                                    ; 1264
        push cs                                         ; 1267
        push ax                                         ; 1268
        sub_ ax,ax                                      ; 1269
        push ax                                         ; 126B
        push ax                                         ; 126C
        mov ax,0x3                                      ; 126D
        cwd                                             ; 1270
        push dx                                         ; 1271
        push ax                                         ; 1272
        mov_ cx,si                                      ; 1273
        mov bx,[bp-0x2]                                 ; 1275
        add cx,0x2870                                   ; 1278
        push bx                                         ; 127C
        push cx                                         ; 127D
        mov cx,0x4                                      ; 127E
        xor_ bx,bx                                      ; 1281
        push bx                                         ; 1283
        push cx                                         ; 1284
        callp R3_1286, R3_12B2, 0x0000                  ; 1285 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 128A
        push word [bp-0xe]                              ; 128D
        mov ax,0x59d                                    ; 1290
        push cs                                         ; 1293
        push ax                                         ; 1294
        sub_ ax,ax                                      ; 1295
        push ax                                         ; 1297
        push ax                                         ; 1298
        mov ax,0x3                                      ; 1299
        cwd                                             ; 129C
        push dx                                         ; 129D
        push ax                                         ; 129E
        mov_ cx,si                                      ; 129F
        mov bx,[bp-0x2]                                 ; 12A1
        add cx,0x2874                                   ; 12A4
        push bx                                         ; 12A8
        push cx                                         ; 12A9
        mov cx,0x4                                      ; 12AA
        xor_ bx,bx                                      ; 12AD
        push bx                                         ; 12AF
        push cx                                         ; 12B0
        callp R3_12B2, R3_12DE, 0x0000                  ; 12B1 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 12B6
        push word [bp-0xe]                              ; 12B9
        mov ax,0x7fc                                    ; 12BC
        push cs                                         ; 12BF
        push ax                                         ; 12C0
        sub_ ax,ax                                      ; 12C1
        push ax                                         ; 12C3
        push ax                                         ; 12C4
        mov ax,0x3                                      ; 12C5
        cwd                                             ; 12C8
        push dx                                         ; 12C9
        push ax                                         ; 12CA
        mov_ cx,si                                      ; 12CB
        mov bx,[bp-0x2]                                 ; 12CD
        add cx,0x2878                                   ; 12D0
        push bx                                         ; 12D4
        push cx                                         ; 12D5
        mov cx,0x4                                      ; 12D6
        xor_ bx,bx                                      ; 12D9
        push bx                                         ; 12DB
        push cx                                         ; 12DC
        callp R3_12DE, R3_130A, 0x0000                  ; 12DD KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 12E2
        push word [bp-0xe]                              ; 12E5
        mov ax,0x58a                                    ; 12E8
        push cs                                         ; 12EB
        push ax                                         ; 12EC
        sub_ ax,ax                                      ; 12ED
        push ax                                         ; 12EF
        push ax                                         ; 12F0
        mov ax,0x3                                      ; 12F1
        cwd                                             ; 12F4
        push dx                                         ; 12F5
        push ax                                         ; 12F6
        mov_ cx,si                                      ; 12F7
        mov bx,[bp-0x2]                                 ; 12F9
        add cx,0x287c                                   ; 12FC
        push bx                                         ; 1300
        push cx                                         ; 1301
        mov cx,0x4                                      ; 1302
        xor_ bx,bx                                      ; 1305
        push bx                                         ; 1307
        push cx                                         ; 1308
        callp R3_130A, R3_1336, 0x0000                  ; 1309 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 130E
        push word [bp-0xe]                              ; 1311
        mov ax,0x7d9                                    ; 1314
        push cs                                         ; 1317
        push ax                                         ; 1318
        sub_ ax,ax                                      ; 1319
        push ax                                         ; 131B
        push ax                                         ; 131C
        mov ax,0x3                                      ; 131D
        cwd                                             ; 1320
        push dx                                         ; 1321
        push ax                                         ; 1322
        mov_ cx,si                                      ; 1323
        mov bx,[bp-0x2]                                 ; 1325
        add cx,0x2880                                   ; 1328
        push bx                                         ; 132C
        push cx                                         ; 132D
        mov cx,0x4                                      ; 132E
        xor_ bx,bx                                      ; 1331
        push bx                                         ; 1333
        push cx                                         ; 1334
        callp R3_1336, R3_1362, 0x0000                  ; 1335 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 133A
        push word [bp-0xe]                              ; 133D
        mov ax,0x545                                    ; 1340
        push cs                                         ; 1343
        push ax                                         ; 1344
        sub_ ax,ax                                      ; 1345
        push ax                                         ; 1347
        push ax                                         ; 1348
        mov ax,0x3                                      ; 1349
        cwd                                             ; 134C
        push dx                                         ; 134D
        push ax                                         ; 134E
        mov_ cx,si                                      ; 134F
        mov bx,[bp-0x2]                                 ; 1351
        add cx,0x2884                                   ; 1354
        push bx                                         ; 1358
        push cx                                         ; 1359
        mov cx,0x4                                      ; 135A
        xor_ bx,bx                                      ; 135D
        push bx                                         ; 135F
        push cx                                         ; 1360
        callp R3_1362, R3_138E, 0x0000                  ; 1361 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1366
        push word [bp-0xe]                              ; 1369
        mov ax,0x76e                                    ; 136C
        push cs                                         ; 136F
        push ax                                         ; 1370
        sub_ ax,ax                                      ; 1371
        push ax                                         ; 1373
        push ax                                         ; 1374
        mov ax,0x3                                      ; 1375
        cwd                                             ; 1378
        push dx                                         ; 1379
        push ax                                         ; 137A
        mov_ cx,si                                      ; 137B
        mov bx,[bp-0x2]                                 ; 137D
        add cx,0x2888                                   ; 1380
        push bx                                         ; 1384
        push cx                                         ; 1385
        mov cx,0x4                                      ; 1386
        xor_ bx,bx                                      ; 1389
        push bx                                         ; 138B
        push cx                                         ; 138C
        callp R3_138E, R3_13BA, 0x0000                  ; 138D KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1392
        push word [bp-0xe]                              ; 1395
        mov ax,0x566                                    ; 1398
        push cs                                         ; 139B
        push ax                                         ; 139C
        sub_ ax,ax                                      ; 139D
        push ax                                         ; 139F
        push ax                                         ; 13A0
        mov ax,0x3                                      ; 13A1
        cwd                                             ; 13A4
        push dx                                         ; 13A5
        push ax                                         ; 13A6
        mov_ cx,si                                      ; 13A7
        mov bx,[bp-0x2]                                 ; 13A9
        add cx,0x288c                                   ; 13AC
        push bx                                         ; 13B0
        push cx                                         ; 13B1
        mov cx,0x4                                      ; 13B2
        xor_ bx,bx                                      ; 13B5
        push bx                                         ; 13B7
        push cx                                         ; 13B8
        callp R3_13BA, R3_13E6, 0x0000                  ; 13B9 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 13BE
        push word [bp-0xe]                              ; 13C1
        mov ax,0x7a3                                    ; 13C4
        push cs                                         ; 13C7
        push ax                                         ; 13C8
        sub_ ax,ax                                      ; 13C9
        push ax                                         ; 13CB
        push ax                                         ; 13CC
        mov ax,0x3                                      ; 13CD
        cwd                                             ; 13D0
        push dx                                         ; 13D1
        push ax                                         ; 13D2
        mov_ cx,si                                      ; 13D3
        mov bx,[bp-0x2]                                 ; 13D5
        add cx,0x2890                                   ; 13D8
        push bx                                         ; 13DC
        push cx                                         ; 13DD
        mov cx,0x4                                      ; 13DE
        xor_ bx,bx                                      ; 13E1
        push bx                                         ; 13E3
        push cx                                         ; 13E4
        callp R3_13E6, R3_1412, 0x0000                  ; 13E5 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 13EA
        push word [bp-0xe]                              ; 13ED
        mov ax,0x512                                    ; 13F0
        push cs                                         ; 13F3
        push ax                                         ; 13F4
        sub_ ax,ax                                      ; 13F5
        push ax                                         ; 13F7
        push ax                                         ; 13F8
        mov ax,0x3                                      ; 13F9
        cwd                                             ; 13FC
        push dx                                         ; 13FD
        push ax                                         ; 13FE
        mov_ cx,si                                      ; 13FF
        mov bx,[bp-0x2]                                 ; 1401
        add cx,0x2894                                   ; 1404
        push bx                                         ; 1408
        push cx                                         ; 1409
        mov cx,0x4                                      ; 140A
        xor_ bx,bx                                      ; 140D
        push bx                                         ; 140F
        push cx                                         ; 1410
        callp R3_1412, R3_143E, 0x0000                  ; 1411 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1416
        push word [bp-0xe]                              ; 1419
        mov ax,0x748                                    ; 141C
        push cs                                         ; 141F
        push ax                                         ; 1420
        sub_ ax,ax                                      ; 1421
        push ax                                         ; 1423
        push ax                                         ; 1424
        mov ax,0x3                                      ; 1425
        cwd                                             ; 1428
        push dx                                         ; 1429
        push ax                                         ; 142A
        mov_ cx,si                                      ; 142B
        mov bx,[bp-0x2]                                 ; 142D
        add cx,0x2898                                   ; 1430
        push bx                                         ; 1434
        push cx                                         ; 1435
        mov cx,0x4                                      ; 1436
        xor_ bx,bx                                      ; 1439
        push bx                                         ; 143B
        push cx                                         ; 143C
        callp R3_143E, R3_146A, 0x0000                  ; 143D KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1442
        push word [bp-0xe]                              ; 1445
        mov ax,0x4ed                                    ; 1448
        push cs                                         ; 144B
        push ax                                         ; 144C
        sub_ ax,ax                                      ; 144D
        push ax                                         ; 144F
        push ax                                         ; 1450
        mov ax,0x3                                      ; 1451
        cwd                                             ; 1454
        push dx                                         ; 1455
        push ax                                         ; 1456
        mov_ cx,si                                      ; 1457
        mov bx,[bp-0x2]                                 ; 1459
        add cx,0x293c                                   ; 145C
        push bx                                         ; 1460
        push cx                                         ; 1461
        mov cx,0x4                                      ; 1462
        xor_ bx,bx                                      ; 1465
        push bx                                         ; 1467
        push cx                                         ; 1468
        callp R3_146A, R3_1496, 0x0000                  ; 1469 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 146E
        push word [bp-0xe]                              ; 1471
        mov ax,0x4c7                                    ; 1474
        push cs                                         ; 1477
        push ax                                         ; 1478
        sub_ ax,ax                                      ; 1479
        push ax                                         ; 147B
        push ax                                         ; 147C
        mov ax,0x3                                      ; 147D
        cwd                                             ; 1480
        push dx                                         ; 1481
        push ax                                         ; 1482
        mov_ cx,si                                      ; 1483
        mov bx,[bp-0x2]                                 ; 1485
        add cx,0x281c                                   ; 1488
        push bx                                         ; 148C
        push cx                                         ; 148D
        mov cx,0x4                                      ; 148E
        xor_ bx,bx                                      ; 1491
        push bx                                         ; 1493
        push cx                                         ; 1494
        callp R3_1496, R3_14C2, 0x0000                  ; 1495 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 149A
        push word [bp-0xe]                              ; 149D
        mov ax,0x4af                                    ; 14A0
        push cs                                         ; 14A3
        push ax                                         ; 14A4
        sub_ ax,ax                                      ; 14A5
        push ax                                         ; 14A7
        push ax                                         ; 14A8
        mov ax,0x3                                      ; 14A9
        cwd                                             ; 14AC
        push dx                                         ; 14AD
        push ax                                         ; 14AE
        mov_ cx,si                                      ; 14AF
        mov bx,[bp-0x2]                                 ; 14B1
        add cx,0x28cc                                   ; 14B4
        push bx                                         ; 14B8
        push cx                                         ; 14B9
        mov cx,0x4                                      ; 14BA
        xor_ bx,bx                                      ; 14BD
        push bx                                         ; 14BF
        push cx                                         ; 14C0
        callp R3_14C2, R3_0DE8, 0x0000                  ; 14C1 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 14C6
        push word [bp-0xe]                              ; 14C9
        mov ax,0x714                                    ; 14CC
        push cs                                         ; 14CF
        push ax                                         ; 14D0
        sub_ ax,ax                                      ; 14D1
        push ax                                         ; 14D3
        push ax                                         ; 14D4
        mov ax,0x3                                      ; 14D5
        cwd                                             ; 14D8
        push dx                                         ; 14D9
        push ax                                         ; 14DA
        mov_ cx,si                                      ; 14DB
        mov bx,[bp-0x2]                                 ; 14DD
        add cx,0x28d0                                   ; 14E0
        push bx                                         ; 14E4
        push cx                                         ; 14E5
        mov cx,0x4                                      ; 14E6
        xor_ bx,bx                                      ; 14E9
        push bx                                         ; 14EB
        push cx                                         ; 14EC
        callp R3_14EE, R3_151A, 0x0000                  ; 14ED KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 14F2
        push word [bp-0xe]                              ; 14F5
        mov ax,0x472                                    ; 14F8
        push cs                                         ; 14FB
        push ax                                         ; 14FC
        sub_ ax,ax                                      ; 14FD
        push ax                                         ; 14FF
        push ax                                         ; 1500
        mov ax,0x3                                      ; 1501
        cwd                                             ; 1504
        push dx                                         ; 1505
        push ax                                         ; 1506
        mov_ cx,si                                      ; 1507
        mov bx,[bp-0x2]                                 ; 1509
        add cx,0x28a4                                   ; 150C
        push bx                                         ; 1510
        push cx                                         ; 1511
        mov cx,0x4                                      ; 1512
        xor_ bx,bx                                      ; 1515
        push bx                                         ; 1517
        push cx                                         ; 1518
        callp R3_151A, R3_1546, 0x0000                  ; 1519 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 151E
        push word [bp-0xe]                              ; 1521
        mov ax,0x6d9                                    ; 1524
        push cs                                         ; 1527
        push ax                                         ; 1528
        sub_ ax,ax                                      ; 1529
        push ax                                         ; 152B
        push ax                                         ; 152C
        mov ax,0x3                                      ; 152D
        cwd                                             ; 1530
        push dx                                         ; 1531
        push ax                                         ; 1532
        mov_ cx,si                                      ; 1533
        mov bx,[bp-0x2]                                 ; 1535
        add cx,0x28a8                                   ; 1538
        push bx                                         ; 153C
        push cx                                         ; 153D
        mov cx,0x4                                      ; 153E
        xor_ bx,bx                                      ; 1541
        push bx                                         ; 1543
        push cx                                         ; 1544
        callp R3_1546, R3_1572, 0x0000                  ; 1545 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 154A
        push word [bp-0xe]                              ; 154D
        mov ax,0x440                                    ; 1550
        push cs                                         ; 1553
        push ax                                         ; 1554
        sub_ ax,ax                                      ; 1555
        push ax                                         ; 1557
        push ax                                         ; 1558
        mov ax,0x3                                      ; 1559
        cwd                                             ; 155C
        push dx                                         ; 155D
        push ax                                         ; 155E
        mov_ cx,si                                      ; 155F
        mov bx,[bp-0x2]                                 ; 1561
        add cx,0x28ac                                   ; 1564
        push bx                                         ; 1568
        push cx                                         ; 1569
        mov cx,0x4                                      ; 156A
        xor_ bx,bx                                      ; 156D
        push bx                                         ; 156F
        push cx                                         ; 1570
        callp R3_1572, R3_159E, 0x0000                  ; 1571 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1576
        push word [bp-0xe]                              ; 1579
        mov ax,0x6a6                                    ; 157C
        push cs                                         ; 157F
        push ax                                         ; 1580
        sub_ ax,ax                                      ; 1581
        push ax                                         ; 1583
        push ax                                         ; 1584
        mov ax,0x3                                      ; 1585
        cwd                                             ; 1588
        push dx                                         ; 1589
        push ax                                         ; 158A
        mov_ cx,si                                      ; 158B
        mov bx,[bp-0x2]                                 ; 158D
        add cx,0x28b0                                   ; 1590
        push bx                                         ; 1594
        push cx                                         ; 1595
        mov cx,0x4                                      ; 1596
        xor_ bx,bx                                      ; 1599
        push bx                                         ; 159B
        push cx                                         ; 159C
        callp R3_159E, R3_15CA, 0x0000                  ; 159D KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 15A2
        push word [bp-0xe]                              ; 15A5
        mov ax,0x418                                    ; 15A8
        push cs                                         ; 15AB
        push ax                                         ; 15AC
        sub_ ax,ax                                      ; 15AD
        push ax                                         ; 15AF
        push ax                                         ; 15B0
        mov ax,0x3                                      ; 15B1
        cwd                                             ; 15B4
        push dx                                         ; 15B5
        push ax                                         ; 15B6
        mov_ cx,si                                      ; 15B7
        mov bx,[bp-0x2]                                 ; 15B9
        add cx,0x28b4                                   ; 15BC
        push bx                                         ; 15C0
        push cx                                         ; 15C1
        mov cx,0x4                                      ; 15C2
        xor_ bx,bx                                      ; 15C5
        push bx                                         ; 15C7
        push cx                                         ; 15C8
        callp R3_15CA, R3_15F6, 0x0000                  ; 15C9 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 15CE
        push word [bp-0xe]                              ; 15D1
        mov ax,0x668                                    ; 15D4
        push cs                                         ; 15D7
        push ax                                         ; 15D8
        sub_ ax,ax                                      ; 15D9
        push ax                                         ; 15DB
        push ax                                         ; 15DC
        mov ax,0x3                                      ; 15DD
        cwd                                             ; 15E0
        push dx                                         ; 15E1
        push ax                                         ; 15E2
        mov_ cx,si                                      ; 15E3
        mov bx,[bp-0x2]                                 ; 15E5
        add cx,0x28b8                                   ; 15E8
        push bx                                         ; 15EC
        push cx                                         ; 15ED
        mov cx,0x4                                      ; 15EE
        xor_ bx,bx                                      ; 15F1
        push bx                                         ; 15F3
        push cx                                         ; 15F4
        callp R3_15F6, R3_1622, 0x0000                  ; 15F5 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 15FA
        push word [bp-0xe]                              ; 15FD
        mov ax,0x3e3                                    ; 1600
        push cs                                         ; 1603
        push ax                                         ; 1604
        sub_ ax,ax                                      ; 1605
        push ax                                         ; 1607
        push ax                                         ; 1608
        mov ax,0x3                                      ; 1609
        cwd                                             ; 160C
        push dx                                         ; 160D
        push ax                                         ; 160E
        mov_ cx,si                                      ; 160F
        mov bx,[bp-0x2]                                 ; 1611
        add cx,0x28bc                                   ; 1614
        push bx                                         ; 1618
        push cx                                         ; 1619
        mov cx,0x4                                      ; 161A
        xor_ bx,bx                                      ; 161D
        push bx                                         ; 161F
        push cx                                         ; 1620
        callp R3_1622, R3_164E, 0x0000                  ; 1621 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1626
        push word [bp-0xe]                              ; 1629
        mov ax,0x648                                    ; 162C
        push cs                                         ; 162F
        push ax                                         ; 1630
        sub_ ax,ax                                      ; 1631
        push ax                                         ; 1633
        push ax                                         ; 1634
        mov ax,0x3                                      ; 1635
        cwd                                             ; 1638
        push dx                                         ; 1639
        push ax                                         ; 163A
        mov_ cx,si                                      ; 163B
        mov bx,[bp-0x2]                                 ; 163D
        add cx,0x28c0                                   ; 1640
        push bx                                         ; 1644
        push cx                                         ; 1645
        mov cx,0x4                                      ; 1646
        xor_ bx,bx                                      ; 1649
        push bx                                         ; 164B
        push cx                                         ; 164C
        callp R3_164E, R3_167A, 0x0000                  ; 164D KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1652
        push word [bp-0xe]                              ; 1655
        mov ax,0x3ab                                    ; 1658
        push cs                                         ; 165B
        push ax                                         ; 165C
        sub_ ax,ax                                      ; 165D
        push ax                                         ; 165F
        push ax                                         ; 1660
        mov ax,0x3                                      ; 1661
        cwd                                             ; 1664
        push dx                                         ; 1665
        push ax                                         ; 1666
        mov_ cx,si                                      ; 1667
        mov bx,[bp-0x2]                                 ; 1669
        add cx,0x28c4                                   ; 166C
        push bx                                         ; 1670
        push cx                                         ; 1671
        mov cx,0x4                                      ; 1672
        xor_ bx,bx                                      ; 1675
        push bx                                         ; 1677
        push cx                                         ; 1678
        callp R3_167A, R3_16A6, 0x0000                  ; 1679 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 167E
        push word [bp-0xe]                              ; 1681
        mov ax,0x627                                    ; 1684
        push cs                                         ; 1687
        push ax                                         ; 1688
        sub_ ax,ax                                      ; 1689
        push ax                                         ; 168B
        push ax                                         ; 168C
        mov ax,0x3                                      ; 168D
        cwd                                             ; 1690
        push dx                                         ; 1691
        push ax                                         ; 1692
        mov_ cx,si                                      ; 1693
        mov bx,[bp-0x2]                                 ; 1695
        add cx,0x28c8                                   ; 1698
        push bx                                         ; 169C
        push cx                                         ; 169D
        mov cx,0x4                                      ; 169E
        xor_ bx,bx                                      ; 16A1
        push bx                                         ; 16A3
        push cx                                         ; 16A4
        callp R3_16A6, R3_16D2, 0x0000                  ; 16A5 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 16AA
        push word [bp-0xe]                              ; 16AD
        mov ax,0x6b7                                    ; 16B0
        push cs                                         ; 16B3
        push ax                                         ; 16B4
        sub_ ax,ax                                      ; 16B5
        push ax                                         ; 16B7
        push ax                                         ; 16B8
        mov ax,0x3                                      ; 16B9
        cwd                                             ; 16BC
        push dx                                         ; 16BD
        push ax                                         ; 16BE
        mov_ cx,si                                      ; 16BF
        mov bx,[bp-0x2]                                 ; 16C1
        add cx,0x298c                                   ; 16C4
        push bx                                         ; 16C8
        push cx                                         ; 16C9
        mov cx,0x4                                      ; 16CA
        xor_ bx,bx                                      ; 16CD
        push bx                                         ; 16CF
        push cx                                         ; 16D0
        callp R3_16D2, R3_16FE, 0x0000                  ; 16D1 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 16D6
        push word [bp-0xe]                              ; 16D9
        mov ax,0x68d                                    ; 16DC
        push cs                                         ; 16DF
        push ax                                         ; 16E0
        sub_ ax,ax                                      ; 16E1
        push ax                                         ; 16E3
        push ax                                         ; 16E4
        mov ax,0x3                                      ; 16E5
        cwd                                             ; 16E8
        push dx                                         ; 16E9
        push ax                                         ; 16EA
        mov_ cx,si                                      ; 16EB
        mov bx,[bp-0x2]                                 ; 16ED
        add cx,0x2990                                   ; 16F0
        push bx                                         ; 16F4
        push cx                                         ; 16F5
        mov cx,0x4                                      ; 16F6
        xor_ bx,bx                                      ; 16F9
        push bx                                         ; 16FB
        push cx                                         ; 16FC
        callp R3_16FE, R3_1728, 0x0000                  ; 16FD KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 1702
        push word [bp-0xe]                              ; 1705
        mov ax,0x638                                    ; 1708
        push cs                                         ; 170B
        push ax                                         ; 170C
        sub_ ax,ax                                      ; 170D
        push ax                                         ; 170F
        push ax                                         ; 1710
        mov ax,0x3                                      ; 1711
        cwd                                             ; 1714
        push dx                                         ; 1715
        push ax                                         ; 1716
        mov_ ax,si                                      ; 1717
        mov dx,[bp-0x2]                                 ; 1719
        add ax,0x297c                                   ; 171C
        push dx                                         ; 171F
        push ax                                         ; 1720
        mov ax,0x4                                      ; 1721
        cwd                                             ; 1724
        push dx                                         ; 1725
        push ax                                         ; 1726
        callp R3_1728, R3_176B, 0x0000                  ; 1727 KERNEL.RegSetValueEx
        test word [di+0x2c],0x4                         ; 172C
        jnz short L3_1736                               ; 1731
        jmp near L3_17C8                                ; 1733

L3_1736:
        mov es,[bp-0x2]                                 ; 1736
        mov ax,[es:si+0x2964]                           ; 1739
        mov cx,[es:si+0x2968]                           ; 173E
        mov [bp-0x8],ax                                 ; 1743
        mov [bp-0x6],cx                                 ; 1746
        push word [bp-0xc]                              ; 1749
        push word [bp-0xe]                              ; 174C
        mov ax,0x4e6                                    ; 174F
        push cs                                         ; 1752
        push ax                                         ; 1753
        sub_ ax,ax                                      ; 1754
        push ax                                         ; 1756
        push ax                                         ; 1757
        mov ax,0x3                                      ; 1758
        cwd                                             ; 175B
        push dx                                         ; 175C
        push ax                                         ; 175D
        lea cx,[bp-0x8]                                 ; 175E
        push ss                                         ; 1761
        push cx                                         ; 1762
        mov cx,0x4                                      ; 1763
        xor_ bx,bx                                      ; 1766
        push bx                                         ; 1768
        push cx                                         ; 1769
        callp R3_176B, R3_17A4, 0x0000                  ; 176A KERNEL.RegSetValueEx
        mov es,[bp-0x2]                                 ; 176F
        mov ax,[es:si+0x2970]                           ; 1772
        mov cx,[es:si+0x296c]                           ; 1777
        mov [bp-0x8],cx                                 ; 177C
        mov [bp-0x6],ax                                 ; 177F
        push word [bp-0xc]                              ; 1782
        push word [bp-0xe]                              ; 1785
        mov ax,0x4c2                                    ; 1788
        push cs                                         ; 178B
        push ax                                         ; 178C
        sub_ ax,ax                                      ; 178D
        push ax                                         ; 178F
        push ax                                         ; 1790
        mov ax,0x3                                      ; 1791
        cwd                                             ; 1794
        push dx                                         ; 1795
        push ax                                         ; 1796
        lea cx,[bp-0x8]                                 ; 1797
        push ss                                         ; 179A
        push cx                                         ; 179B
        mov cx,0x4                                      ; 179C
        xor_ bx,bx                                      ; 179F
        push bx                                         ; 17A1
        push cx                                         ; 17A2
        callp R3_17A4, R3_17FC, 0x0000                  ; 17A3 KERNEL.RegSetValueEx
        push word [bp-0xc]                              ; 17A8
        push word [bp-0xe]                              ; 17AB
        mov ax,0x483                                    ; 17AE
        push cs                                         ; 17B1
        push ax                                         ; 17B2
        sub_ ax,ax                                      ; 17B3
        push ax                                         ; 17B5
        push ax                                         ; 17B6
        mov ax,0x3                                      ; 17B7
        cwd                                             ; 17BA
        push dx                                         ; 17BB
        push ax                                         ; 17BC
        mov_ ax,si                                      ; 17BD
        mov dx,[bp-0x2]                                 ; 17BF
        add ax,0x2954                                   ; 17C2
        push dx                                         ; 17C5
        jmp short L3_182C                               ; 17C6

L3_17C8:
        test byte [di+0x2a],0x80                        ; 17C8
        jnz short L3_17D4                               ; 17CC
        test byte [di+0x2a],0x20                        ; 17CE
        jz short L3_1838                                ; 17D2

L3_17D4:
        push word [bp-0xc]                              ; 17D4
        push word [bp-0xe]                              ; 17D7
        mov ax,0x483                                    ; 17DA
        push cs                                         ; 17DD
        push ax                                         ; 17DE
        sub_ ax,ax                                      ; 17DF
        push ax                                         ; 17E1
        push ax                                         ; 17E2
        mov ax,0x3                                      ; 17E3
        cwd                                             ; 17E6
        push dx                                         ; 17E7
        push ax                                         ; 17E8
        mov_ cx,si                                      ; 17E9
        mov bx,[bp-0x2]                                 ; 17EB
        add cx,0x2954                                   ; 17EE
        push bx                                         ; 17F2
        push cx                                         ; 17F3
        mov cx,0x4                                      ; 17F4
        xor_ bx,bx                                      ; 17F7
        push bx                                         ; 17F9
        push cx                                         ; 17FA
        callp R3_17FC, R3_1834, 0x0000                  ; 17FB KERNEL.RegSetValueEx
        mov es,[bp-0x2]                                 ; 1800
        mov ax,[es:si+0x2960]                           ; 1803
        mov cx,[es:si+0x295c]                           ; 1808
        mov [bp-0x8],cx                                 ; 180D
        mov [bp-0x6],ax                                 ; 1810
        push word [bp-0xc]                              ; 1813
        push word [bp-0xe]                              ; 1816
        mov ax,0x494                                    ; 1819
        push cs                                         ; 181C
        push ax                                         ; 181D
        sub_ ax,ax                                      ; 181E
        push ax                                         ; 1820
        push ax                                         ; 1821
        mov ax,0x3                                      ; 1822
        cwd                                             ; 1825
        push dx                                         ; 1826
        push ax                                         ; 1827
        lea ax,[bp-0x8]                                 ; 1828
        push ss                                         ; 182B

L3_182C:
        push ax                                         ; 182C
        mov ax,0x4                                      ; 182D
        cwd                                             ; 1830
        push dx                                         ; 1831
        push ax                                         ; 1832
        callp R3_1834, R3_1128, 0x0000                  ; 1833 KERNEL.RegSetValueEx

L3_1838:
        push word [bp-0xc]                              ; 1838
        push word [bp-0xe]                              ; 183B
        callp R3_183F, 0xFFFF, 0x0000                   ; 183E KERNEL.RegCloseKey
        lea ax,[bp-0x112]                               ; 1843
        push ss                                         ; 1847
        push ax                                         ; 1848
        mov ax,0x67d                                    ; 1849
        push cs                                         ; 184C
        push ax                                         ; 184D
        callp R3_184F, R3_0D98, 0x0000                  ; 184E KERNEL.lstrcat
        mov ax,0x2                                      ; 1853
        mov dx,0x8000                                   ; 1856
        push dx                                         ; 1859
        push ax                                         ; 185A
        lea ax,[bp-0x112]                               ; 185B
        push ss                                         ; 185F
        push ax                                         ; 1860
        lea ax,[bp-0xe]                                 ; 1861
        push ss                                         ; 1864
        push ax                                         ; 1865
        callp R3_1867, R3_0DB0, 0x0000                  ; 1866 KERNEL.RegCreateKey
        or_ dx,ax                                       ; 186B
        jz short L3_1874                                ; 186D

L3_186F:
        mov ax,0xe                                      ; 186F
        jmp short L3_18E1                               ; 1872

L3_1874:
        test byte [di+0x2b],0x1                         ; 1874
        jz short L3_18A4                                ; 1878
        push word [bp-0xc]                              ; 187A
        push word [bp-0xe]                              ; 187D
        mov ax,0x3d6                                    ; 1880
        push cs                                         ; 1883
        push ax                                         ; 1884
        sub_ ax,ax                                      ; 1885
        push ax                                         ; 1887
        push ax                                         ; 1888
        mov ax,0x3                                      ; 1889
        cwd                                             ; 188C
        push dx                                         ; 188D
        push ax                                         ; 188E
        mov_ ax,si                                      ; 188F
        mov dx,[bp-0x2]                                 ; 1891
        add ax,0x2944                                   ; 1894
        push dx                                         ; 1897
        push ax                                         ; 1898
        mov ax,0x4                                      ; 1899
        cwd                                             ; 189C
        push dx                                         ; 189D
        push ax                                         ; 189E
        callp R3_18A0, R3_18D0, 0x0000                  ; 189F KERNEL.RegSetValueEx

L3_18A4:
        test byte [di+0x2b],0x2                         ; 18A4
        jz short L3_18D4                                ; 18A8
        push word [bp-0xc]                              ; 18AA
        push word [bp-0xe]                              ; 18AD
        mov ax,0x952                                    ; 18B0
        push cs                                         ; 18B3
        push ax                                         ; 18B4
        sub_ ax,ax                                      ; 18B5
        push ax                                         ; 18B7
        push ax                                         ; 18B8
        mov ax,0x3                                      ; 18B9
        cwd                                             ; 18BC
        push dx                                         ; 18BD
        push ax                                         ; 18BE
        mov_ ax,si                                      ; 18BF
        mov dx,[bp-0x2]                                 ; 18C1
        add ax,0x294c                                   ; 18C4
        push dx                                         ; 18C7
        push ax                                         ; 18C8
        mov ax,0x4                                      ; 18C9
        cwd                                             ; 18CC
        push dx                                         ; 18CD
        push ax                                         ; 18CE
        callp R3_18D0, R3_14EE, 0x0000                  ; 18CF KERNEL.RegSetValueEx

L3_18D4:
        push word [bp-0xc]                              ; 18D4
        push word [bp-0xe]                              ; 18D7
        callp R3_18DB, R3_183F, 0x0000                  ; 18DA KERNEL.RegCloseKey
        xor_ ax,ax                                      ; 18DF

L3_18E1:
        xor_ dx,dx                                      ; 18E1
        pop si                                          ; 18E3
        pop di                                          ; 18E4
        mov_ sp,bp                                      ; 18E5
        pop bp                                          ; 18E7
        retf 0x2                                        ; 18E8
        db 0x90                                         ; 18EB

; at every enable and resume, through set-control-details (5:17CA)
restore_mixer_state:
        push bp                                         ; 18EC
        mov_ bp,sp                                      ; 18ED
        sub sp,0x178                                    ; 18EF
        push di                                         ; 18F3
        push si                                         ; 18F4
        mov si,[bp+0x6]                                 ; 18F5
        mov ax,0x1                                      ; 18F8
        cwd                                             ; 18FB
        push dx                                         ; 18FC
        push ax                                         ; 18FD
        mov ax,0x100                                    ; 18FE
        cwd                                             ; 1901
        push dx                                         ; 1902
        push ax                                         ; 1903
        lea ax,[bp-0x178]                               ; 1904
        push ss                                         ; 1908
        push ax                                         ; 1909
        sub_ ax,ax                                      ; 190A
        push ax                                         ; 190C
        push ax                                         ; 190D
        push word [si+0x16]                             ; 190E
        push word [si+0x14]                             ; 1911
        call cm_get_devnode_key                         ; 1914
        add sp,byte +0x14                               ; 1917
        or_ ax,ax                                       ; 191A
        jz short L3_1924                                ; 191C
        mov ax,0xf                                      ; 191E
        jmp near L3_3DE1                                ; 1921

L3_1924:
        lea ax,[bp-0x178]                               ; 1924
        push ss                                         ; 1928
        push ax                                         ; 1929
        mov ax,0x69e                                    ; 192A
        push cs                                         ; 192D
        push ax                                         ; 192E
        callp R3_1930, R3_184F, 0x0000                  ; 192F KERNEL.lstrcat
        mov ax,0x2                                      ; 1934
        mov dx,0x8000                                   ; 1937
        push dx                                         ; 193A
        push ax                                         ; 193B
        lea ax,[bp-0x178]                               ; 193C
        push ss                                         ; 1940
        push ax                                         ; 1941
        lea ax,[bp-0x10]                                ; 1942
        push ss                                         ; 1945
        push ax                                         ; 1946
        callp R3_1948, 0xFFFF, 0x0000                   ; 1947 KERNEL.RegOpenKey
        or_ dx,ax                                       ; 194C
        jz short L3_1958                                ; 194E
        sub_ ax,ax                                      ; 1950
        mov [bp-0xe],ax                                 ; 1952
        mov [bp-0x10],ax                                ; 1955

L3_1958:
        mov ax,[si+0x26]                                ; 1958
        mov dx,[si+0x28]                                ; 195B
        mov_ di,ax                                      ; 195E
        mov [bp-0x6],dx                                 ; 1960
        sub_ ax,ax                                      ; 1963
        mov [bp-0x76],ax                                ; 1965
        mov [bp-0x78],ax                                ; 1968
        mov ax,[bp-0xe]                                 ; 196B
        or ax,[bp-0x10]                                 ; 196E
        jz short L3_199D                                ; 1971
        mov word [bp-0xc],0x4                           ; 1973
        mov word [bp-0xa],0x0                           ; 1978
        push word [bp-0xe]                              ; 197D
        push word [bp-0x10]                             ; 1980
        mov ax,0x521                                    ; 1983
        push cs                                         ; 1986
        push ax                                         ; 1987
        sub_ ax,ax                                      ; 1988
        push ax                                         ; 198A
        push ax                                         ; 198B
        push ax                                         ; 198C
        push ax                                         ; 198D
        lea ax,[bp-0x78]                                ; 198E
        push ss                                         ; 1991
        push ax                                         ; 1992
        lea ax,[bp-0xc]                                 ; 1993
        push ss                                         ; 1996
        push ax                                         ; 1997
        callp R3_1999, R3_1A21, 0x0000                  ; 1998 KERNEL.RegQueryValueEx

L3_199D:
        mov word [bp-0x4c],0x21                         ; 199D
        mov word [bp-0x4a],0x0                          ; 19A2
        mov word [bp-0x50],0x18                         ; 19A7
        mov word [bp-0x4e],0x0                          ; 19AC
        mov es,[bp-0x6]                                 ; 19B1
        mov ax,[es:di+0x2634]                           ; 19B4
        mov [bp-0x48],ax                                ; 19B9
        mov word [bp-0x46],0x0                          ; 19BC
        sub_ ax,ax                                      ; 19C1
        mov [bp-0x42],ax                                ; 19C3
        mov [bp-0x44],ax                                ; 19C6
        mov word [bp-0x40],0x4                          ; 19C9
        mov [bp-0x3e],ax                                ; 19CE
        lea ax,[bp-0x78]                                ; 19D1
        mov [bp-0x3c],ax                                ; 19D4
        mov [bp-0x3a],ss                                ; 19D7
        push si                                         ; 19DA
        lea ax,[bp-0x50]                                ; 19DB
        push ss                                         ; 19DE
        push ax                                         ; 19DF
        sub_ ax,ax                                      ; 19E0
        push ax                                         ; 19E2
        push ax                                         ; 19E3
        callf mxd_set_control_details, R3_19E7, R3_1AB0 ; 19E4 far seg5
        mov word [bp-0x1c],0x4                          ; 19E9
        mov word [bp-0x1a],0x0                          ; 19EE
        mov ax,[bp-0xe]                                 ; 19F3
        or ax,[bp-0x10]                                 ; 19F6
        jz short L3_1A25                                ; 19F9
        mov word [bp-0xc],0x4                           ; 19FB
        mov word [bp-0xa],0x0                           ; 1A00
        push word [bp-0xe]                              ; 1A05
        push word [bp-0x10]                             ; 1A08
        mov ax,0x4fa                                    ; 1A0B
        push cs                                         ; 1A0E
        push ax                                         ; 1A0F
        sub_ ax,ax                                      ; 1A10
        push ax                                         ; 1A12
        push ax                                         ; 1A13
        push ax                                         ; 1A14
        push ax                                         ; 1A15
        lea ax,[bp-0x1c]                                ; 1A16
        push ss                                         ; 1A19
        push ax                                         ; 1A1A
        lea ax,[bp-0xc]                                 ; 1A1B
        push ss                                         ; 1A1E
        push ax                                         ; 1A1F
        callp R3_1A21, R3_1AF8, 0x0000                  ; 1A20 KERNEL.RegQueryValueEx

L3_1A25:
        xor_ cx,cx                                      ; 1A25
        mov es,[bp-0x6]                                 ; 1A27
        cmp [es:di+0x69e],cx                            ; 1A2A
        jnz short L3_1A34                               ; 1A2F
        jmp near L3_1AC0                                ; 1A31

L3_1A34:
        mov [bp-0x2],cx                                 ; 1A34
        mov [bp-0x8],di                                 ; 1A37
        mov_ si,cx                                      ; 1A3A

L3_1A3C:
        mov ax,0x1                                      ; 1A3C
        mov_ cx,si                                      ; 1A3F
        shl ax,cl                                       ; 1A41
        cwd                                             ; 1A43
        test [bp-0x1a],dx                               ; 1A44
        ja short L3_1A4E                                ; 1A47
        test [bp-0x1c],ax                               ; 1A49
        jna short L3_1A53                               ; 1A4C

L3_1A4E:
        mov ax,0x1                                      ; 1A4E
        jmp short L3_1A55                               ; 1A51

L3_1A53:
        xor_ ax,ax                                      ; 1A53

L3_1A55:
        cwd                                             ; 1A55
        mov [bp-0x78],ax                                ; 1A56
        mov [bp-0x76],dx                                ; 1A59
        lea ax,[si+0x19]                                ; 1A5C
        mov [bp-0x4c],ax                                ; 1A5F
        mov word [bp-0x4a],0x0                          ; 1A62
        mov word [bp-0x50],0x18                         ; 1A67
        mov word [bp-0x4e],0x0                          ; 1A6C
        mov ax,0x6                                      ; 1A71
        mul word [bp-0x4c]                              ; 1A74
        mov_ bx,ax                                      ; 1A77
        add_ bx,di                                      ; 1A79
        mov cx,[es:bx+0x256e]                           ; 1A7B
        mov [bp-0x48],cx                                ; 1A80
        mov word [bp-0x46],0x0                          ; 1A83
        sub_ cx,cx                                      ; 1A88
        mov [bp-0x42],cx                                ; 1A8A
        mov [bp-0x44],cx                                ; 1A8D
        mov word [bp-0x40],0x4                          ; 1A90
        mov [bp-0x3e],cx                                ; 1A95
        lea cx,[bp-0x78]                                ; 1A98
        mov [bp-0x3c],cx                                ; 1A9B
        mov [bp-0x3a],ss                                ; 1A9E
        push word [bp+0x6]                              ; 1AA1
        lea cx,[bp-0x50]                                ; 1AA4
        push ss                                         ; 1AA7
        push cx                                         ; 1AA8
        sub_ cx,cx                                      ; 1AA9
        push cx                                         ; 1AAB
        push cx                                         ; 1AAC
        callf mxd_set_control_details, R3_1AB0, R3_1B95 ; 1AAD far seg5
        inc si                                          ; 1AB2
        mov es,[bp-0x6]                                 ; 1AB3
        cmp [es:di+0x69e],si                            ; 1AB6
        jna short L3_1AC0                               ; 1ABB
        jmp near L3_1A3C                                ; 1ABD

L3_1AC0:
        mov word [bp-0x18],0x7b                         ; 1AC0
        mov word [bp-0x16],0x0                          ; 1AC5
        mov ax,[bp-0xe]                                 ; 1ACA
        or ax,[bp-0x10]                                 ; 1ACD
        jz short L3_1AFC                                ; 1AD0
        mov word [bp-0xc],0x4                           ; 1AD2
        mov word [bp-0xa],0x0                           ; 1AD7
        push word [bp-0xe]                              ; 1ADC
        push word [bp-0x10]                             ; 1ADF
        mov ax,0x8c5                                    ; 1AE2
        push cs                                         ; 1AE5
        push ax                                         ; 1AE6
        sub_ ax,ax                                      ; 1AE7
        push ax                                         ; 1AE9
        push ax                                         ; 1AEA
        push ax                                         ; 1AEB
        push ax                                         ; 1AEC
        lea ax,[bp-0x18]                                ; 1AED
        push ss                                         ; 1AF0
        push ax                                         ; 1AF1
        lea ax,[bp-0xc]                                 ; 1AF2
        push ss                                         ; 1AF5
        push ax                                         ; 1AF6
        callp R3_1AF8, R3_1BD4, 0x0000                  ; 1AF7 KERNEL.RegQueryValueEx

L3_1AFC:
        sub_ ax,ax                                      ; 1AFC
        mov [bp-0x4a],ax                                ; 1AFE
        mov [bp-0x4c],ax                                ; 1B01
        mov word [bp-0x50],0x18                         ; 1B04
        mov [bp-0x4e],ax                                ; 1B09
        mov word [bp-0x48],0x1                          ; 1B0C
        mov [bp-0x46],ax                                ; 1B11
        mov ax,0x94                                     ; 1B14
        mul word [bp-0x4c]                              ; 1B17
        mov_ si,ax                                      ; 1B1A
        mov_ bx,di                                      ; 1B1C
        mov es,[bp-0x6]                                 ; 1B1E
        mov ax,[es:bx+si+0x926]                         ; 1B21
        mov dx,[es:bx+si+0x928]                         ; 1B26
        mov [bp-0x44],ax                                ; 1B2B
        mov [bp-0x42],dx                                ; 1B2E
        mov word [bp-0x40],0x4                          ; 1B31
        mov word [bp-0x3e],0x0                          ; 1B36
        lea ax,[bp-0x78]                                ; 1B3B
        mov [bp-0x3c],ax                                ; 1B3E
        mov [bp-0x3a],ss                                ; 1B41
        mov word [bp-0x2],0x0                           ; 1B44
        cmp word [bp-0x44],byte +0x0                    ; 1B49
        jz short L3_1B85                                ; 1B4D
        mov [bp-0x8],di                                 ; 1B4F
        lea bx,[bp-0x78]                                ; 1B52
        mov di,[bp-0x2]                                 ; 1B55

L3_1B58:
        mov ax,0x1                                      ; 1B58
        mov_ cx,di                                      ; 1B5B
        shl ax,cl                                       ; 1B5D
        cwd                                             ; 1B5F
        test [bp-0x16],dx                               ; 1B60
        ja short L3_1B6A                                ; 1B63
        test [bp-0x18],ax                               ; 1B65
        jna short L3_1B6F                               ; 1B68

L3_1B6A:
        mov ax,0x1                                      ; 1B6A
        jmp short L3_1B71                               ; 1B6D

L3_1B6F:
        xor_ ax,ax                                      ; 1B6F

L3_1B71:
        cwd                                             ; 1B71
        mov [ss:bx],ax                                  ; 1B72
        mov [ss:bx+0x2],dx                              ; 1B75
        add bx,byte +0x4                                ; 1B79
        inc di                                          ; 1B7C
        cmp [bp-0x44],di                                ; 1B7D
        ja short L3_1B58                                ; 1B80
        mov di,[bp-0x8]                                 ; 1B82

L3_1B85:
        mov si,[bp+0x6]                                 ; 1B85
        push si                                         ; 1B88
        lea ax,[bp-0x50]                                ; 1B89
        push ss                                         ; 1B8C
        push ax                                         ; 1B8D
        sub_ ax,ax                                      ; 1B8E
        push ax                                         ; 1B90
        push ax                                         ; 1B91
        callf mxd_set_control_details, R3_1B95, 0xFFFF  ; 1B92 far seg5
        mov ax,0x8000                                   ; 1B97
        xor_ dx,dx                                      ; 1B9A
        mov [bp-0x24],ax                                ; 1B9C
        mov [bp-0x22],dx                                ; 1B9F
        mov [bp-0x20],ax                                ; 1BA2
        mov [bp-0x1e],dx                                ; 1BA5
        mov ax,[bp-0xe]                                 ; 1BA8
        or ax,[bp-0x10]                                 ; 1BAB
        jz short L3_1C02                                ; 1BAE
        mov word [bp-0xc],0x4                           ; 1BB0
        mov [bp-0xa],dx                                 ; 1BB5
        push word [bp-0xe]                              ; 1BB8
        push word [bp-0x10]                             ; 1BBB
        mov ax,0x89e                                    ; 1BBE
        push cs                                         ; 1BC1
        push ax                                         ; 1BC2
        sub_ ax,ax                                      ; 1BC3
        push ax                                         ; 1BC5
        push ax                                         ; 1BC6
        push ax                                         ; 1BC7
        push ax                                         ; 1BC8
        lea ax,[bp-0x24]                                ; 1BC9
        push ss                                         ; 1BCC
        push ax                                         ; 1BCD
        lea ax,[bp-0xc]                                 ; 1BCE
        push ss                                         ; 1BD1
        push ax                                         ; 1BD2
        callp R3_1BD4, R3_1BFE, 0x0000                  ; 1BD3 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 1BD8
        mov word [bp-0xa],0x0                           ; 1BDD
        push word [bp-0xe]                              ; 1BE2
        push word [bp-0x10]                             ; 1BE5
        mov ax,0x989                                    ; 1BE8
        push cs                                         ; 1BEB
        push ax                                         ; 1BEC
        sub_ ax,ax                                      ; 1BED
        push ax                                         ; 1BEF
        push ax                                         ; 1BF0
        push ax                                         ; 1BF1
        push ax                                         ; 1BF2
        lea ax,[bp-0x20]                                ; 1BF3
        push ss                                         ; 1BF6
        push ax                                         ; 1BF7
        lea ax,[bp-0xc]                                 ; 1BF8
        push ss                                         ; 1BFB
        push ax                                         ; 1BFC
        callp R3_1BFE, 0xFFFF, 0x0000                   ; 1BFD KERNEL.RegQueryValueEx

L3_1C02:
        mov word [bp-0x4c],0xb                          ; 1C02
        mov word [bp-0x4a],0x0                          ; 1C07
        mov word [bp-0x50],0x18                         ; 1C0C
        mov word [bp-0x4e],0x0                          ; 1C11
        mov es,[bp-0x6]                                 ; 1C16
        mov ax,[es:di+0x25b0]                           ; 1C19
        mov [bp-0x48],ax                                ; 1C1E
        mov word [bp-0x46],0x0                          ; 1C21
        sub_ ax,ax                                      ; 1C26
        mov [bp-0x42],ax                                ; 1C28
        mov [bp-0x44],ax                                ; 1C2B
        mov word [bp-0x40],0x4                          ; 1C2E
        mov [bp-0x3e],ax                                ; 1C33
        lea ax,[bp-0x24]                                ; 1C36
        mov [bp-0x3c],ax                                ; 1C39
        mov [bp-0x3a],ss                                ; 1C3C
        push si                                         ; 1C3F
        lea cx,[bp-0x50]                                ; 1C40
        push ss                                         ; 1C43
        push cx                                         ; 1C44
        sub_ cx,cx                                      ; 1C45
        push cx                                         ; 1C47
        push cx                                         ; 1C48
        callf mxd_set_control_details, R3_1C4C, R3_1D03 ; 1C49 far seg5
        mov ax,0x8000                                   ; 1C4E
        xor_ dx,dx                                      ; 1C51
        mov [bp-0x24],ax                                ; 1C53
        mov [bp-0x22],dx                                ; 1C56
        mov [bp-0x20],ax                                ; 1C59
        mov [bp-0x1e],dx                                ; 1C5C
        mov ax,[bp-0xe]                                 ; 1C5F
        or ax,[bp-0x10]                                 ; 1C62
        jz short L3_1CB9                                ; 1C65
        mov word [bp-0xc],0x4                           ; 1C67
        mov [bp-0xa],dx                                 ; 1C6C
        push word [bp-0xe]                              ; 1C6F
        push word [bp-0x10]                             ; 1C72
        mov ax,0x86d                                    ; 1C75
        push cs                                         ; 1C78
        push ax                                         ; 1C79
        sub_ ax,ax                                      ; 1C7A
        push ax                                         ; 1C7C
        push ax                                         ; 1C7D
        push ax                                         ; 1C7E
        push ax                                         ; 1C7F
        lea ax,[bp-0x24]                                ; 1C80
        push ss                                         ; 1C83
        push ax                                         ; 1C84
        lea ax,[bp-0xc]                                 ; 1C85
        push ss                                         ; 1C88
        push ax                                         ; 1C89
        callp R3_1C8B, R3_1CB5, 0x0000                  ; 1C8A KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 1C8F
        mov word [bp-0xa],0x0                           ; 1C94
        push word [bp-0xe]                              ; 1C99
        push word [bp-0x10]                             ; 1C9C
        mov ax,0x95f                                    ; 1C9F
        push cs                                         ; 1CA2
        push ax                                         ; 1CA3
        sub_ ax,ax                                      ; 1CA4
        push ax                                         ; 1CA6
        push ax                                         ; 1CA7
        push ax                                         ; 1CA8
        push ax                                         ; 1CA9
        lea ax,[bp-0x20]                                ; 1CAA
        push ss                                         ; 1CAD
        push ax                                         ; 1CAE
        lea ax,[bp-0xc]                                 ; 1CAF
        push ss                                         ; 1CB2
        push ax                                         ; 1CB3
        callp R3_1CB5, R3_1D42, 0x0000                  ; 1CB4 KERNEL.RegQueryValueEx

L3_1CB9:
        mov word [bp-0x4c],0x3                          ; 1CB9
        mov word [bp-0x4a],0x0                          ; 1CBE
        mov word [bp-0x50],0x18                         ; 1CC3
        mov word [bp-0x4e],0x0                          ; 1CC8
        mov es,[bp-0x6]                                 ; 1CCD
        mov ax,[es:di+0x2580]                           ; 1CD0
        mov [bp-0x48],ax                                ; 1CD5
        mov word [bp-0x46],0x0                          ; 1CD8
        sub_ ax,ax                                      ; 1CDD
        mov [bp-0x42],ax                                ; 1CDF
        mov [bp-0x44],ax                                ; 1CE2
        mov word [bp-0x40],0x4                          ; 1CE5
        mov [bp-0x3e],ax                                ; 1CEA
        lea ax,[bp-0x24]                                ; 1CED
        mov [bp-0x3c],ax                                ; 1CF0
        mov [bp-0x3a],ss                                ; 1CF3
        push si                                         ; 1CF6
        lea cx,[bp-0x50]                                ; 1CF7
        push ss                                         ; 1CFA
        push cx                                         ; 1CFB
        sub_ cx,cx                                      ; 1CFC
        push cx                                         ; 1CFE
        push cx                                         ; 1CFF
        callf mxd_set_control_details, R3_1D03, R3_1DBA ; 1D00 far seg5
        mov ax,0x8000                                   ; 1D05
        xor_ dx,dx                                      ; 1D08
        mov [bp-0x24],ax                                ; 1D0A
        mov [bp-0x22],dx                                ; 1D0D
        mov [bp-0x20],ax                                ; 1D10
        mov [bp-0x1e],dx                                ; 1D13
        mov ax,[bp-0xe]                                 ; 1D16
        or ax,[bp-0x10]                                 ; 1D19
        jz short L3_1D70                                ; 1D1C
        mov word [bp-0xc],0x4                           ; 1D1E
        mov [bp-0xa],dx                                 ; 1D23
        push word [bp-0xe]                              ; 1D26
        push word [bp-0x10]                             ; 1D29
        mov ax,0x84f                                    ; 1D2C
        push cs                                         ; 1D2F
        push ax                                         ; 1D30
        sub_ ax,ax                                      ; 1D31
        push ax                                         ; 1D33
        push ax                                         ; 1D34
        push ax                                         ; 1D35
        push ax                                         ; 1D36
        lea ax,[bp-0x24]                                ; 1D37
        push ss                                         ; 1D3A
        push ax                                         ; 1D3B
        lea ax,[bp-0xc]                                 ; 1D3C
        push ss                                         ; 1D3F
        push ax                                         ; 1D40
        callp R3_1D42, R3_1D6C, 0x0000                  ; 1D41 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 1D46
        mov word [bp-0xa],0x0                           ; 1D4B
        push word [bp-0xe]                              ; 1D50
        push word [bp-0x10]                             ; 1D53
        mov ax,0x946                                    ; 1D56
        push cs                                         ; 1D59
        push ax                                         ; 1D5A
        sub_ ax,ax                                      ; 1D5B
        push ax                                         ; 1D5D
        push ax                                         ; 1D5E
        push ax                                         ; 1D5F
        push ax                                         ; 1D60
        lea ax,[bp-0x20]                                ; 1D61
        push ss                                         ; 1D64
        push ax                                         ; 1D65
        lea ax,[bp-0xc]                                 ; 1D66
        push ss                                         ; 1D69
        push ax                                         ; 1D6A
        callp R3_1D6C, R3_1DF9, 0x0000                  ; 1D6B KERNEL.RegQueryValueEx

L3_1D70:
        mov word [bp-0x4c],0x4                          ; 1D70
        mov word [bp-0x4a],0x0                          ; 1D75
        mov word [bp-0x50],0x18                         ; 1D7A
        mov word [bp-0x4e],0x0                          ; 1D7F
        mov es,[bp-0x6]                                 ; 1D84
        mov ax,[es:di+0x2586]                           ; 1D87
        mov [bp-0x48],ax                                ; 1D8C
        mov word [bp-0x46],0x0                          ; 1D8F
        sub_ ax,ax                                      ; 1D94
        mov [bp-0x42],ax                                ; 1D96
        mov [bp-0x44],ax                                ; 1D99
        mov word [bp-0x40],0x4                          ; 1D9C
        mov [bp-0x3e],ax                                ; 1DA1
        lea ax,[bp-0x24]                                ; 1DA4
        mov [bp-0x3c],ax                                ; 1DA7
        mov [bp-0x3a],ss                                ; 1DAA
        push si                                         ; 1DAD
        lea cx,[bp-0x50]                                ; 1DAE
        push ss                                         ; 1DB1
        push cx                                         ; 1DB2
        sub_ cx,cx                                      ; 1DB3
        push cx                                         ; 1DB5
        push cx                                         ; 1DB6
        callf mxd_set_control_details, R3_1DBA, R3_1E71 ; 1DB7 far seg5
        mov ax,0x8000                                   ; 1DBC
        xor_ dx,dx                                      ; 1DBF
        mov [bp-0x24],ax                                ; 1DC1
        mov [bp-0x22],dx                                ; 1DC4
        mov [bp-0x20],ax                                ; 1DC7
        mov [bp-0x1e],dx                                ; 1DCA
        mov ax,[bp-0xe]                                 ; 1DCD
        or ax,[bp-0x10]                                 ; 1DD0
        jz short L3_1E27                                ; 1DD3
        mov word [bp-0xc],0x4                           ; 1DD5
        mov [bp-0xa],dx                                 ; 1DDA
        push word [bp-0xe]                              ; 1DDD
        push word [bp-0x10]                             ; 1DE0
        mov ax,0x81f                                    ; 1DE3
        push cs                                         ; 1DE6
        push ax                                         ; 1DE7
        sub_ ax,ax                                      ; 1DE8
        push ax                                         ; 1DEA
        push ax                                         ; 1DEB
        push ax                                         ; 1DEC
        push ax                                         ; 1DED
        lea ax,[bp-0x24]                                ; 1DEE
        push ss                                         ; 1DF1
        push ax                                         ; 1DF2
        lea ax,[bp-0xc]                                 ; 1DF3
        push ss                                         ; 1DF6
        push ax                                         ; 1DF7
        callp R3_1DF9, R3_1E23, 0x0000                  ; 1DF8 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 1DFD
        mov word [bp-0xa],0x0                           ; 1E02
        push word [bp-0xe]                              ; 1E07
        push word [bp-0x10]                             ; 1E0A
        mov ax,0x924                                    ; 1E0D
        push cs                                         ; 1E10
        push ax                                         ; 1E11
        sub_ ax,ax                                      ; 1E12
        push ax                                         ; 1E14
        push ax                                         ; 1E15
        push ax                                         ; 1E16
        push ax                                         ; 1E17
        lea ax,[bp-0x20]                                ; 1E18
        push ss                                         ; 1E1B
        push ax                                         ; 1E1C
        lea ax,[bp-0xc]                                 ; 1E1D
        push ss                                         ; 1E20
        push ax                                         ; 1E21
        callp R3_1E23, R3_1EB0, 0x0000                  ; 1E22 KERNEL.RegQueryValueEx

L3_1E27:
        mov word [bp-0x4c],0x5                          ; 1E27
        mov word [bp-0x4a],0x0                          ; 1E2C
        mov word [bp-0x50],0x18                         ; 1E31
        mov word [bp-0x4e],0x0                          ; 1E36
        mov es,[bp-0x6]                                 ; 1E3B
        mov ax,[es:di+0x258c]                           ; 1E3E
        mov [bp-0x48],ax                                ; 1E43
        mov word [bp-0x46],0x0                          ; 1E46
        sub_ ax,ax                                      ; 1E4B
        mov [bp-0x42],ax                                ; 1E4D
        mov [bp-0x44],ax                                ; 1E50
        mov word [bp-0x40],0x4                          ; 1E53
        mov [bp-0x3e],ax                                ; 1E58
        lea ax,[bp-0x24]                                ; 1E5B
        mov [bp-0x3c],ax                                ; 1E5E
        mov [bp-0x3a],ss                                ; 1E61
        push si                                         ; 1E64
        lea cx,[bp-0x50]                                ; 1E65
        push ss                                         ; 1E68
        push cx                                         ; 1E69
        sub_ cx,cx                                      ; 1E6A
        push cx                                         ; 1E6C
        push cx                                         ; 1E6D
        callf mxd_set_control_details, R3_1E71, R3_1F28 ; 1E6E far seg5
        mov ax,0x8000                                   ; 1E73
        xor_ dx,dx                                      ; 1E76
        mov [bp-0x24],ax                                ; 1E78
        mov [bp-0x22],dx                                ; 1E7B
        mov [bp-0x20],ax                                ; 1E7E
        mov [bp-0x1e],dx                                ; 1E81
        mov ax,[bp-0xe]                                 ; 1E84
        or ax,[bp-0x10]                                 ; 1E87
        jz short L3_1EDE                                ; 1E8A
        mov word [bp-0xc],0x4                           ; 1E8C
        mov [bp-0xa],dx                                 ; 1E91
        push word [bp-0xe]                              ; 1E94
        push word [bp-0x10]                             ; 1E97
        mov ax,0x7ed                                    ; 1E9A
        push cs                                         ; 1E9D
        push ax                                         ; 1E9E
        sub_ ax,ax                                      ; 1E9F
        push ax                                         ; 1EA1
        push ax                                         ; 1EA2
        push ax                                         ; 1EA3
        push ax                                         ; 1EA4
        lea ax,[bp-0x24]                                ; 1EA5
        push ss                                         ; 1EA8
        push ax                                         ; 1EA9
        lea ax,[bp-0xc]                                 ; 1EAA
        push ss                                         ; 1EAD
        push ax                                         ; 1EAE
        callp R3_1EB0, R3_1EDA, 0x0000                  ; 1EAF KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 1EB4
        mov word [bp-0xa],0x0                           ; 1EB9
        push word [bp-0xe]                              ; 1EBE
        push word [bp-0x10]                             ; 1EC1
        mov ax,0x8f8                                    ; 1EC4
        push cs                                         ; 1EC7
        push ax                                         ; 1EC8
        sub_ ax,ax                                      ; 1EC9
        push ax                                         ; 1ECB
        push ax                                         ; 1ECC
        push ax                                         ; 1ECD
        push ax                                         ; 1ECE
        lea ax,[bp-0x20]                                ; 1ECF
        push ss                                         ; 1ED2
        push ax                                         ; 1ED3
        lea ax,[bp-0xc]                                 ; 1ED4
        push ss                                         ; 1ED7
        push ax                                         ; 1ED8
        callp R3_1EDA, R3_1F67, 0x0000                  ; 1ED9 KERNEL.RegQueryValueEx

L3_1EDE:
        mov word [bp-0x4c],0x6                          ; 1EDE
        mov word [bp-0x4a],0x0                          ; 1EE3
        mov word [bp-0x50],0x18                         ; 1EE8
        mov word [bp-0x4e],0x0                          ; 1EED
        mov es,[bp-0x6]                                 ; 1EF2
        mov ax,[es:di+0x2592]                           ; 1EF5
        mov [bp-0x48],ax                                ; 1EFA
        mov word [bp-0x46],0x0                          ; 1EFD
        sub_ ax,ax                                      ; 1F02
        mov [bp-0x42],ax                                ; 1F04
        mov [bp-0x44],ax                                ; 1F07
        mov word [bp-0x40],0x4                          ; 1F0A
        mov [bp-0x3e],ax                                ; 1F0F
        lea ax,[bp-0x24]                                ; 1F12
        mov [bp-0x3c],ax                                ; 1F15
        mov [bp-0x3a],ss                                ; 1F18
        push si                                         ; 1F1B
        lea cx,[bp-0x50]                                ; 1F1C
        push ss                                         ; 1F1F
        push cx                                         ; 1F20
        sub_ cx,cx                                      ; 1F21
        push cx                                         ; 1F23
        push cx                                         ; 1F24
        callf mxd_set_control_details, R3_1F28, R3_1FDF ; 1F25 far seg5
        mov ax,0x8000                                   ; 1F2A
        xor_ dx,dx                                      ; 1F2D
        mov [bp-0x24],ax                                ; 1F2F
        mov [bp-0x22],dx                                ; 1F32
        mov [bp-0x20],ax                                ; 1F35
        mov [bp-0x1e],dx                                ; 1F38
        mov ax,[bp-0xe]                                 ; 1F3B
        or ax,[bp-0x10]                                 ; 1F3E
        jz short L3_1F95                                ; 1F41
        mov word [bp-0xc],0x4                           ; 1F43
        mov [bp-0xa],dx                                 ; 1F48
        push word [bp-0xe]                              ; 1F4B
        push word [bp-0x10]                             ; 1F4E
        mov ax,0x7c0                                    ; 1F51
        push cs                                         ; 1F54
        push ax                                         ; 1F55
        sub_ ax,ax                                      ; 1F56
        push ax                                         ; 1F58
        push ax                                         ; 1F59
        push ax                                         ; 1F5A
        push ax                                         ; 1F5B
        lea ax,[bp-0x24]                                ; 1F5C
        push ss                                         ; 1F5F
        push ax                                         ; 1F60
        lea ax,[bp-0xc]                                 ; 1F61
        push ss                                         ; 1F64
        push ax                                         ; 1F65
        callp R3_1F67, R3_1F91, 0x0000                  ; 1F66 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 1F6B
        mov word [bp-0xa],0x0                           ; 1F70
        push word [bp-0xe]                              ; 1F75
        push word [bp-0x10]                             ; 1F78
        mov ax,0x8d2                                    ; 1F7B
        push cs                                         ; 1F7E
        push ax                                         ; 1F7F
        sub_ ax,ax                                      ; 1F80
        push ax                                         ; 1F82
        push ax                                         ; 1F83
        push ax                                         ; 1F84
        push ax                                         ; 1F85
        lea ax,[bp-0x20]                                ; 1F86
        push ss                                         ; 1F89
        push ax                                         ; 1F8A
        lea ax,[bp-0xc]                                 ; 1F8B
        push ss                                         ; 1F8E
        push ax                                         ; 1F8F
        callp R3_1F91, R3_1999, 0x0000                  ; 1F90 KERNEL.RegQueryValueEx

L3_1F95:
        mov word [bp-0x4c],0x7                          ; 1F95
        mov word [bp-0x4a],0x0                          ; 1F9A
        mov word [bp-0x50],0x18                         ; 1F9F
        mov word [bp-0x4e],0x0                          ; 1FA4
        mov es,[bp-0x6]                                 ; 1FA9
        mov ax,[es:di+0x2598]                           ; 1FAC
        mov [bp-0x48],ax                                ; 1FB1
        mov word [bp-0x46],0x0                          ; 1FB4
        sub_ ax,ax                                      ; 1FB9
        mov [bp-0x42],ax                                ; 1FBB
        mov [bp-0x44],ax                                ; 1FBE
        mov word [bp-0x40],0x4                          ; 1FC1
        mov [bp-0x3e],ax                                ; 1FC6
        lea ax,[bp-0x24]                                ; 1FC9
        mov [bp-0x3c],ax                                ; 1FCC
        mov [bp-0x3a],ss                                ; 1FCF
        push si                                         ; 1FD2
        lea cx,[bp-0x50]                                ; 1FD3
        push ss                                         ; 1FD6
        push cx                                         ; 1FD7
        sub_ cx,cx                                      ; 1FD8
        push cx                                         ; 1FDA
        push cx                                         ; 1FDB
        callf mxd_set_control_details, R3_1FDF, R3_19E7 ; 1FDC far seg5
        mov ax,0x8000                                   ; 1FE1
        xor_ dx,dx                                      ; 1FE4
        mov [bp-0x24],ax                                ; 1FE6
        mov [bp-0x22],dx                                ; 1FE9
        mov [bp-0x20],ax                                ; 1FEC
        mov [bp-0x1e],dx                                ; 1FEF
        mov ax,[bp-0xe]                                 ; 1FF2
        or ax,[bp-0x10]                                 ; 1FF5
        jz short L3_204C                                ; 1FF8
        mov word [bp-0xc],0x4                           ; 1FFA
        mov [bp-0xa],dx                                 ; 1FFF
        push word [bp-0xe]                              ; 2002
        push word [bp-0x10]                             ; 2005
        mov ax,0x797                                    ; 2008
        push cs                                         ; 200B
        push ax                                         ; 200C
        sub_ ax,ax                                      ; 200D
        push ax                                         ; 200F
        push ax                                         ; 2010
        push ax                                         ; 2011
        push ax                                         ; 2012
        lea ax,[bp-0x24]                                ; 2013
        push ss                                         ; 2016
        push ax                                         ; 2017
        lea ax,[bp-0xc]                                 ; 2018
        push ss                                         ; 201B
        push ax                                         ; 201C
        callp R3_201E, R3_2048, 0x0000                  ; 201D KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2022
        mov word [bp-0xa],0x0                           ; 2027
        push word [bp-0xe]                              ; 202C
        push word [bp-0x10]                             ; 202F
        mov ax,0x8ac                                    ; 2032
        push cs                                         ; 2035
        push ax                                         ; 2036
        sub_ ax,ax                                      ; 2037
        push ax                                         ; 2039
        push ax                                         ; 203A
        push ax                                         ; 203B
        push ax                                         ; 203C
        lea ax,[bp-0x20]                                ; 203D
        push ss                                         ; 2040
        push ax                                         ; 2041
        lea ax,[bp-0xc]                                 ; 2042
        push ss                                         ; 2045
        push ax                                         ; 2046
        callp R3_2048, R3_20DE, 0x0000                  ; 2047 KERNEL.RegQueryValueEx

L3_204C:
        mov word [bp-0x4c],0x8                          ; 204C
        mov word [bp-0x4a],0x0                          ; 2051
        mov word [bp-0x50],0x18                         ; 2056
        mov word [bp-0x4e],0x0                          ; 205B
        mov es,[bp-0x6]                                 ; 2060
        mov ax,[es:di+0x259e]                           ; 2063
        mov [bp-0x48],ax                                ; 2068
        mov word [bp-0x46],0x0                          ; 206B
        sub_ ax,ax                                      ; 2070
        mov [bp-0x42],ax                                ; 2072
        mov [bp-0x44],ax                                ; 2075
        mov word [bp-0x40],0x4                          ; 2078
        mov [bp-0x3e],ax                                ; 207D
        lea ax,[bp-0x24]                                ; 2080
        mov [bp-0x3c],ax                                ; 2083
        mov [bp-0x3a],ss                                ; 2086
        push si                                         ; 2089
        lea ax,[bp-0x50]                                ; 208A
        push ss                                         ; 208D
        push ax                                         ; 208E
        sub_ ax,ax                                      ; 208F
        push ax                                         ; 2091
        push ax                                         ; 2092
        callf mxd_set_control_details, R3_2096, R3_2156 ; 2093 far seg5
        test byte [si+0x2b],0x40                        ; 2098
        jz short L3_20A1                                ; 209C
        jmp near L3_2158                                ; 209E

L3_20A1:
        mov ax,0x8000                                   ; 20A1
        xor_ dx,dx                                      ; 20A4
        mov [bp-0x24],ax                                ; 20A6
        mov [bp-0x22],dx                                ; 20A9
        mov [bp-0x20],ax                                ; 20AC
        mov [bp-0x1e],dx                                ; 20AF
        mov ax,[bp-0xe]                                 ; 20B2
        or ax,[bp-0x10]                                 ; 20B5
        jz short L3_210C                                ; 20B8
        mov word [bp-0xc],0x4                           ; 20BA
        mov [bp-0xa],dx                                 ; 20BF
        push word [bp-0xe]                              ; 20C2
        push word [bp-0x10]                             ; 20C5
        mov ax,0x77f                                    ; 20C8
        push cs                                         ; 20CB
        push ax                                         ; 20CC
        sub_ ax,ax                                      ; 20CD
        push ax                                         ; 20CF
        push ax                                         ; 20D0
        push ax                                         ; 20D1
        push ax                                         ; 20D2
        lea ax,[bp-0x24]                                ; 20D3
        push ss                                         ; 20D6
        push ax                                         ; 20D7
        lea ax,[bp-0xc]                                 ; 20D8
        push ss                                         ; 20DB
        push ax                                         ; 20DC
        callp R3_20DE, R3_2108, 0x0000                  ; 20DD KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 20E2
        mov word [bp-0xa],0x0                           ; 20E7
        push word [bp-0xe]                              ; 20EC
        push word [bp-0x10]                             ; 20EF
        mov ax,0x88b                                    ; 20F2
        push cs                                         ; 20F5
        push ax                                         ; 20F6
        sub_ ax,ax                                      ; 20F7
        push ax                                         ; 20F9
        push ax                                         ; 20FA
        push ax                                         ; 20FB
        push ax                                         ; 20FC
        lea ax,[bp-0x20]                                ; 20FD
        push ss                                         ; 2100
        push ax                                         ; 2101
        lea ax,[bp-0xc]                                 ; 2102
        push ss                                         ; 2105
        push ax                                         ; 2106
        callp R3_2108, R3_2190, 0x0000                  ; 2107 KERNEL.RegQueryValueEx

L3_210C:
        mov word [bp-0x4c],0x9                          ; 210C
        mov word [bp-0x4a],0x0                          ; 2111
        mov word [bp-0x50],0x18                         ; 2116
        mov word [bp-0x4e],0x0                          ; 211B
        mov es,[bp-0x6]                                 ; 2120
        mov ax,[es:di+0x25a4]                           ; 2123
        mov [bp-0x48],ax                                ; 2128
        mov word [bp-0x46],0x0                          ; 212B
        sub_ ax,ax                                      ; 2130
        mov [bp-0x42],ax                                ; 2132
        mov [bp-0x44],ax                                ; 2135
        mov word [bp-0x40],0x4                          ; 2138
        mov [bp-0x3e],ax                                ; 213D
        lea ax,[bp-0x24]                                ; 2140
        mov [bp-0x3c],ax                                ; 2143
        mov [bp-0x3a],ss                                ; 2146
        push si                                         ; 2149
        lea ax,[bp-0x50]                                ; 214A
        push ss                                         ; 214D
        push ax                                         ; 214E
        sub_ ax,ax                                      ; 214F
        push ax                                         ; 2151
        push ax                                         ; 2152
        callf mxd_set_control_details, R3_2156, R3_21DE ; 2153 far seg5

L3_2158:
        mov word [bp-0x24],0x8000                       ; 2158
        mov word [bp-0x22],0x0                          ; 215D
        mov ax,[bp-0xe]                                 ; 2162
        or ax,[bp-0x10]                                 ; 2165
        jz short L3_2194                                ; 2168
        mov word [bp-0xc],0x4                           ; 216A
        mov word [bp-0xa],0x0                           ; 216F
        push word [bp-0xe]                              ; 2174
        push word [bp-0x10]                             ; 2177
        mov ax,0x758                                    ; 217A
        push cs                                         ; 217D
        push ax                                         ; 217E
        sub_ ax,ax                                      ; 217F
        push ax                                         ; 2181
        push ax                                         ; 2182
        push ax                                         ; 2183
        push ax                                         ; 2184
        lea ax,[bp-0x24]                                ; 2185
        push ss                                         ; 2188
        push ax                                         ; 2189
        lea ax,[bp-0xc]                                 ; 218A
        push ss                                         ; 218D
        push ax                                         ; 218E
        callp R3_2190, R3_2216, 0x0000                  ; 218F KERNEL.RegQueryValueEx

L3_2194:
        mov word [bp-0x4c],0xa                          ; 2194
        mov word [bp-0x4a],0x0                          ; 2199
        mov word [bp-0x50],0x18                         ; 219E
        mov word [bp-0x4e],0x0                          ; 21A3
        mov es,[bp-0x6]                                 ; 21A8
        mov ax,[es:di+0x25aa]                           ; 21AB
        mov [bp-0x48],ax                                ; 21B0
        mov word [bp-0x46],0x0                          ; 21B3
        sub_ ax,ax                                      ; 21B8
        mov [bp-0x42],ax                                ; 21BA
        mov [bp-0x44],ax                                ; 21BD
        mov word [bp-0x40],0x4                          ; 21C0
        mov [bp-0x3e],ax                                ; 21C5
        lea ax,[bp-0x24]                                ; 21C8
        mov [bp-0x3c],ax                                ; 21CB
        mov [bp-0x3a],ss                                ; 21CE
        push si                                         ; 21D1
        lea ax,[bp-0x50]                                ; 21D2
        push ss                                         ; 21D5
        push ax                                         ; 21D6
        sub_ ax,ax                                      ; 21D7
        push ax                                         ; 21D9
        push ax                                         ; 21DA
        callf mxd_set_control_details, R3_21DE, R3_2264 ; 21DB far seg5
        sub_ ax,ax                                      ; 21E0
        mov [bp-0x76],ax                                ; 21E2
        mov [bp-0x78],ax                                ; 21E5
        mov ax,[bp-0xe]                                 ; 21E8
        or ax,[bp-0x10]                                 ; 21EB
        jz short L3_221A                                ; 21EE
        mov word [bp-0xc],0x4                           ; 21F0
        mov word [bp-0xa],0x0                           ; 21F5
        push word [bp-0xe]                              ; 21FA
        push word [bp-0x10]                             ; 21FD
        mov ax,0x61b                                    ; 2200
        push cs                                         ; 2203
        push ax                                         ; 2204
        sub_ ax,ax                                      ; 2205
        push ax                                         ; 2207
        push ax                                         ; 2208
        push ax                                         ; 2209
        push ax                                         ; 220A
        lea ax,[bp-0x78]                                ; 220B
        push ss                                         ; 220E
        push ax                                         ; 220F
        lea ax,[bp-0xc]                                 ; 2210
        push ss                                         ; 2213
        push ax                                         ; 2214
        callp R3_2216, R3_229E, 0x0000                  ; 2215 KERNEL.RegQueryValueEx

L3_221A:
        mov word [bp-0x4c],0x25                         ; 221A
        mov word [bp-0x4a],0x0                          ; 221F
        mov word [bp-0x50],0x18                         ; 2224
        mov word [bp-0x4e],0x0                          ; 2229
        mov es,[bp-0x6]                                 ; 222E
        mov ax,[es:di+0x264c]                           ; 2231
        mov [bp-0x48],ax                                ; 2236
        mov word [bp-0x46],0x0                          ; 2239
        sub_ ax,ax                                      ; 223E
        mov [bp-0x42],ax                                ; 2240
        mov [bp-0x44],ax                                ; 2243
        mov word [bp-0x40],0x4                          ; 2246
        mov [bp-0x3e],ax                                ; 224B
        lea ax,[bp-0x78]                                ; 224E
        mov [bp-0x3c],ax                                ; 2251
        mov [bp-0x3a],ss                                ; 2254
        push si                                         ; 2257
        lea ax,[bp-0x50]                                ; 2258
        push ss                                         ; 225B
        push ax                                         ; 225C
        sub_ ax,ax                                      ; 225D
        push ax                                         ; 225F
        push ax                                         ; 2260
        callf mxd_set_control_details, R3_2264, R3_2358 ; 2261 far seg5
        mov word [bp-0x18],0x2                          ; 2266
        mov word [bp-0x16],0x0                          ; 226B
        mov ax,[bp-0xe]                                 ; 2270
        or ax,[bp-0x10]                                 ; 2273
        jz short L3_22A2                                ; 2276
        mov word [bp-0xc],0x4                           ; 2278
        mov word [bp-0xa],0x0                           ; 227D
        push word [bp-0xe]                              ; 2282
        push word [bp-0x10]                             ; 2285
        mov ax,0x5ff                                    ; 2288
        push cs                                         ; 228B
        push ax                                         ; 228C
        sub_ ax,ax                                      ; 228D
        push ax                                         ; 228F
        push ax                                         ; 2290
        push ax                                         ; 2291
        push ax                                         ; 2292
        lea ax,[bp-0x18]                                ; 2293
        push ss                                         ; 2296
        push ax                                         ; 2297
        lea ax,[bp-0xc]                                 ; 2298
        push ss                                         ; 229B
        push ax                                         ; 229C
        callp R3_229E, R3_1C8B, 0x0000                  ; 229D KERNEL.RegQueryValueEx

L3_22A2:
        mov word [bp-0x4c],0x1                          ; 22A2
        mov word [bp-0x4a],0x0                          ; 22A7
        mov word [bp-0x50],0x18                         ; 22AC
        mov word [bp-0x4e],0x0                          ; 22B1
        mov word [bp-0x48],0x1                          ; 22B6
        mov word [bp-0x46],0x0                          ; 22BB
        mov ax,0x94                                     ; 22C0
        mul word [bp-0x4c]                              ; 22C3
        mov_ bx,ax                                      ; 22C6
        mov ax,[bp-0x6]                                 ; 22C8
        add_ bx,di                                      ; 22CB
        mov es,ax                                       ; 22CD
        mov ax,[es:bx+0x926]                            ; 22CF
        mov dx,[es:bx+0x928]                            ; 22D4
        mov [bp-0x44],ax                                ; 22D9
        mov [bp-0x42],dx                                ; 22DC
        mov word [bp-0x40],0x4                          ; 22DF
        mov word [bp-0x3e],0x0                          ; 22E4
        lea ax,[bp-0x78]                                ; 22E9
        mov [bp-0x3c],ax                                ; 22EC
        mov [bp-0x3a],ss                                ; 22EF
        mov al,[bp-0x18]                                ; 22F2
        and ax,strict word 0x2                          ; 22F5
        cmp ax,strict word 0x1                          ; 22F8
        sbb_ al,al                                      ; 22FB
        inc al                                          ; 22FD
        mov [0xc6],al                                   ; 22FF
        mov word [bp-0x2],0x0                           ; 2302
        cmp word [bp-0x44],byte +0x0                    ; 2307
        jz short L3_234B                                ; 230B
        mov [bp-0x8],di                                 ; 230D
        lea dx,[bp-0x78]                                ; 2310
        mov [bp-0x4],dx                                 ; 2313
        mov_ si,dx                                      ; 2316
        mov bx,[bp-0x2]                                 ; 2318

L3_231B:
        mov ax,0x1                                      ; 231B
        mov_ cx,bx                                      ; 231E
        shl ax,cl                                       ; 2320
        cwd                                             ; 2322
        test [bp-0x16],dx                               ; 2323
        ja short L3_232D                                ; 2326
        test [bp-0x18],ax                               ; 2328
        jna short L3_2332                               ; 232B

L3_232D:
        mov ax,0x1                                      ; 232D
        jmp short L3_2334                               ; 2330

L3_2332:
        xor_ ax,ax                                      ; 2332

L3_2334:
        cwd                                             ; 2334
        mov [ss:si],ax                                  ; 2335
        mov [ss:si+0x2],dx                              ; 2338
        add si,byte +0x4                                ; 233C
        inc bx                                          ; 233F
        cmp [bp-0x44],bx                                ; 2340
        ja short L3_231B                                ; 2343
        mov si,[bp+0x6]                                 ; 2345
        mov di,[bp-0x8]                                 ; 2348

L3_234B:
        push si                                         ; 234B
        lea ax,[bp-0x50]                                ; 234C
        push ss                                         ; 234F
        push ax                                         ; 2350
        sub_ ax,ax                                      ; 2351
        push ax                                         ; 2353
        push ax                                         ; 2354
        callf mxd_set_control_details, R3_2358, R3_1C4C ; 2355 far seg5
        cmp byte [0xc6],0x0                             ; 235A
        jz short L3_236B                                ; 235F
        sub_ ax,ax                                      ; 2361
        mov [bp-0x76],ax                                ; 2363
        mov [bp-0x78],ax                                ; 2366
        jmp short L3_2375                               ; 2369

L3_236B:
        mov word [bp-0x78],0x1                          ; 236B
        mov word [bp-0x76],0x0                          ; 2370

L3_2375:
        mov word [bp-0x4c],0x2d                         ; 2375
        mov word [bp-0x4a],0x0                          ; 237A
        mov word [bp-0x50],0x18                         ; 237F
        mov word [bp-0x4e],0x0                          ; 2384
        mov es,[bp-0x6]                                 ; 2389
        mov ax,[es:di+0x267c]                           ; 238C
        mov [bp-0x48],ax                                ; 2391
        mov word [bp-0x46],0x0                          ; 2394
        sub_ ax,ax                                      ; 2399
        mov [bp-0x42],ax                                ; 239B
        mov [bp-0x44],ax                                ; 239E
        mov word [bp-0x40],0x4                          ; 23A1
        mov [bp-0x3e],ax                                ; 23A6
        lea ax,[bp-0x78]                                ; 23A9
        mov [bp-0x3c],ax                                ; 23AC
        mov [bp-0x3a],ss                                ; 23AF
        push si                                         ; 23B2
        lea ax,[bp-0x50]                                ; 23B3
        push ss                                         ; 23B6
        push ax                                         ; 23B7
        sub_ ax,ax                                      ; 23B8
        push ax                                         ; 23BA
        push ax                                         ; 23BB
        callf mxd_set_control_details, R3_23BF, R3_2476 ; 23BC far seg5
        mov ax,0x8000                                   ; 23C1
        xor_ dx,dx                                      ; 23C4
        mov [bp-0x24],ax                                ; 23C6
        mov [bp-0x22],dx                                ; 23C9
        mov [bp-0x20],ax                                ; 23CC
        mov [bp-0x1e],dx                                ; 23CF
        mov ax,[bp-0xe]                                 ; 23D2
        or ax,[bp-0x10]                                 ; 23D5
        jz short L3_242C                                ; 23D8
        mov word [bp-0xc],0x4                           ; 23DA
        mov [bp-0xa],dx                                 ; 23DF
        push word [bp-0xe]                              ; 23E2
        push word [bp-0x10]                             ; 23E5
        mov ax,0x5db                                    ; 23E8
        push cs                                         ; 23EB
        push ax                                         ; 23EC
        sub_ ax,ax                                      ; 23ED
        push ax                                         ; 23EF
        push ax                                         ; 23F0
        push ax                                         ; 23F1
        push ax                                         ; 23F2
        lea ax,[bp-0x24]                                ; 23F3
        push ss                                         ; 23F6
        push ax                                         ; 23F7
        lea ax,[bp-0xc]                                 ; 23F8
        push ss                                         ; 23FB
        push ax                                         ; 23FC
        callp R3_23FE, R3_2428, 0x0000                  ; 23FD KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2402
        mov word [bp-0xa],0x0                           ; 2407
        push word [bp-0xe]                              ; 240C
        push word [bp-0x10]                             ; 240F
        mov ax,0x85a                                    ; 2412
        push cs                                         ; 2415
        push ax                                         ; 2416
        sub_ ax,ax                                      ; 2417
        push ax                                         ; 2419
        push ax                                         ; 241A
        push ax                                         ; 241B
        push ax                                         ; 241C
        lea ax,[bp-0x20]                                ; 241D
        push ss                                         ; 2420
        push ax                                         ; 2421
        lea ax,[bp-0xc]                                 ; 2422
        push ss                                         ; 2425
        push ax                                         ; 2426
        callp R3_2428, R3_24B5, 0x0000                  ; 2427 KERNEL.RegQueryValueEx

L3_242C:
        mov word [bp-0x4c],0x12                         ; 242C
        mov word [bp-0x4a],0x0                          ; 2431
        mov word [bp-0x50],0x18                         ; 2436
        mov word [bp-0x4e],0x0                          ; 243B
        mov es,[bp-0x6]                                 ; 2440
        mov ax,[es:di+0x25da]                           ; 2443
        mov [bp-0x48],ax                                ; 2448
        mov word [bp-0x46],0x0                          ; 244B
        sub_ ax,ax                                      ; 2450
        mov [bp-0x42],ax                                ; 2452
        mov [bp-0x44],ax                                ; 2455
        mov word [bp-0x40],0x4                          ; 2458
        mov [bp-0x3e],ax                                ; 245D
        lea ax,[bp-0x24]                                ; 2460
        mov [bp-0x3c],ax                                ; 2463
        mov [bp-0x3a],ss                                ; 2466
        push si                                         ; 2469
        lea cx,[bp-0x50]                                ; 246A
        push ss                                         ; 246D
        push cx                                         ; 246E
        sub_ cx,cx                                      ; 246F
        push cx                                         ; 2471
        push cx                                         ; 2472
        callf mxd_set_control_details, R3_2476, R3_252D ; 2473 far seg5
        mov ax,0x8000                                   ; 2478
        xor_ dx,dx                                      ; 247B
        mov [bp-0x24],ax                                ; 247D
        mov [bp-0x22],dx                                ; 2480
        mov [bp-0x20],ax                                ; 2483
        mov [bp-0x1e],dx                                ; 2486
        mov ax,[bp-0xe]                                 ; 2489
        or ax,[bp-0x10]                                 ; 248C
        jz short L3_24E3                                ; 248F
        mov word [bp-0xc],0x4                           ; 2491
        mov [bp-0xa],dx                                 ; 2496
        push word [bp-0xe]                              ; 2499
        push word [bp-0x10]                             ; 249C
        mov ax,0x5bb                                    ; 249F
        push cs                                         ; 24A2
        push ax                                         ; 24A3
        sub_ ax,ax                                      ; 24A4
        push ax                                         ; 24A6
        push ax                                         ; 24A7
        push ax                                         ; 24A8
        push ax                                         ; 24A9
        lea ax,[bp-0x24]                                ; 24AA
        push ss                                         ; 24AD
        push ax                                         ; 24AE
        lea ax,[bp-0xc]                                 ; 24AF
        push ss                                         ; 24B2
        push ax                                         ; 24B3
        callp R3_24B5, R3_24DF, 0x0000                  ; 24B4 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 24B9
        mov word [bp-0xa],0x0                           ; 24BE
        push word [bp-0xe]                              ; 24C3
        push word [bp-0x10]                             ; 24C6
        mov ax,0x82a                                    ; 24C9
        push cs                                         ; 24CC
        push ax                                         ; 24CD
        sub_ ax,ax                                      ; 24CE
        push ax                                         ; 24D0
        push ax                                         ; 24D1
        push ax                                         ; 24D2
        push ax                                         ; 24D3
        lea ax,[bp-0x20]                                ; 24D4
        push ss                                         ; 24D7
        push ax                                         ; 24D8
        lea ax,[bp-0xc]                                 ; 24D9
        push ss                                         ; 24DC
        push ax                                         ; 24DD
        callp R3_24DF, R3_256C, 0x0000                  ; 24DE KERNEL.RegQueryValueEx

L3_24E3:
        mov word [bp-0x4c],0xc                          ; 24E3
        mov word [bp-0x4a],0x0                          ; 24E8
        mov word [bp-0x50],0x18                         ; 24ED
        mov word [bp-0x4e],0x0                          ; 24F2
        mov es,[bp-0x6]                                 ; 24F7
        mov ax,[es:di+0x25b6]                           ; 24FA
        mov [bp-0x48],ax                                ; 24FF
        mov word [bp-0x46],0x0                          ; 2502
        sub_ ax,ax                                      ; 2507
        mov [bp-0x42],ax                                ; 2509
        mov [bp-0x44],ax                                ; 250C
        mov word [bp-0x40],0x4                          ; 250F
        mov [bp-0x3e],ax                                ; 2514
        lea ax,[bp-0x24]                                ; 2517
        mov [bp-0x3c],ax                                ; 251A
        mov [bp-0x3a],ss                                ; 251D
        push si                                         ; 2520
        lea cx,[bp-0x50]                                ; 2521
        push ss                                         ; 2524
        push cx                                         ; 2525
        sub_ cx,cx                                      ; 2526
        push cx                                         ; 2528
        push cx                                         ; 2529
        callf mxd_set_control_details, R3_252D, R3_25E4 ; 252A far seg5
        mov ax,0x8000                                   ; 252F
        xor_ dx,dx                                      ; 2532
        mov [bp-0x24],ax                                ; 2534
        mov [bp-0x22],dx                                ; 2537
        mov [bp-0x20],ax                                ; 253A
        mov [bp-0x1e],dx                                ; 253D
        mov ax,[bp-0xe]                                 ; 2540
        or ax,[bp-0x10]                                 ; 2543
        jz short L3_259A                                ; 2546
        mov word [bp-0xc],0x4                           ; 2548
        mov [bp-0xa],dx                                 ; 254D
        push word [bp-0xe]                              ; 2550
        push word [bp-0x10]                             ; 2553
        mov ax,0x59d                                    ; 2556
        push cs                                         ; 2559
        push ax                                         ; 255A
        sub_ ax,ax                                      ; 255B
        push ax                                         ; 255D
        push ax                                         ; 255E
        push ax                                         ; 255F
        push ax                                         ; 2560
        lea ax,[bp-0x24]                                ; 2561
        push ss                                         ; 2564
        push ax                                         ; 2565
        lea ax,[bp-0xc]                                 ; 2566
        push ss                                         ; 2569
        push ax                                         ; 256A
        callp R3_256C, R3_2596, 0x0000                  ; 256B KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2570
        mov word [bp-0xa],0x0                           ; 2575
        push word [bp-0xe]                              ; 257A
        push word [bp-0x10]                             ; 257D
        mov ax,0x7fc                                    ; 2580
        push cs                                         ; 2583
        push ax                                         ; 2584
        sub_ ax,ax                                      ; 2585
        push ax                                         ; 2587
        push ax                                         ; 2588
        push ax                                         ; 2589
        push ax                                         ; 258A
        lea ax,[bp-0x20]                                ; 258B
        push ss                                         ; 258E
        push ax                                         ; 258F
        lea ax,[bp-0xc]                                 ; 2590
        push ss                                         ; 2593
        push ax                                         ; 2594
        callp R3_2596, R3_266F, 0x0000                  ; 2595 KERNEL.RegQueryValueEx

L3_259A:
        mov word [bp-0x4c],0xd                          ; 259A
        mov word [bp-0x4a],0x0                          ; 259F
        mov word [bp-0x50],0x18                         ; 25A4
        mov word [bp-0x4e],0x0                          ; 25A9
        mov es,[bp-0x6]                                 ; 25AE
        mov ax,[es:di+0x25bc]                           ; 25B1
        mov [bp-0x48],ax                                ; 25B6
        mov word [bp-0x46],0x0                          ; 25B9
        sub_ ax,ax                                      ; 25BE
        mov [bp-0x42],ax                                ; 25C0
        mov [bp-0x44],ax                                ; 25C3
        mov word [bp-0x40],0x4                          ; 25C6
        mov [bp-0x3e],ax                                ; 25CB
        lea ax,[bp-0x24]                                ; 25CE
        mov [bp-0x3c],ax                                ; 25D1
        mov [bp-0x3a],ss                                ; 25D4
        push si                                         ; 25D7
        lea cx,[bp-0x50]                                ; 25D8
        push ss                                         ; 25DB
        push cx                                         ; 25DC
        sub_ cx,cx                                      ; 25DD
        push cx                                         ; 25DF
        push cx                                         ; 25E0
        callf mxd_set_control_details, R3_25E4, R3_2630 ; 25E1 far seg5
        mov word [bp-0x4c],0x2f                         ; 25E6
        mov word [bp-0x4a],0x0                          ; 25EB
        mov word [bp-0x50],0x18                         ; 25F0
        mov word [bp-0x4e],0x0                          ; 25F5
        mov es,[bp-0x6]                                 ; 25FA
        mov ax,[es:di+0x2688]                           ; 25FD
        mov [bp-0x48],ax                                ; 2602
        mov word [bp-0x46],0x0                          ; 2605
        sub_ ax,ax                                      ; 260A
        mov [bp-0x42],ax                                ; 260C
        mov [bp-0x44],ax                                ; 260F
        mov word [bp-0x40],0x4                          ; 2612
        mov [bp-0x3e],ax                                ; 2617
        lea ax,[bp-0x24]                                ; 261A
        mov [bp-0x3c],ax                                ; 261D
        mov [bp-0x3a],ss                                ; 2620
        push si                                         ; 2623
        lea cx,[bp-0x50]                                ; 2624
        push ss                                         ; 2627
        push cx                                         ; 2628
        sub_ cx,cx                                      ; 2629
        push cx                                         ; 262B
        push cx                                         ; 262C
        callf mxd_set_control_details, R3_2630, R3_26E7 ; 262D far seg5
        mov ax,0x8000                                   ; 2632
        xor_ dx,dx                                      ; 2635
        mov [bp-0x24],ax                                ; 2637
        mov [bp-0x22],dx                                ; 263A
        mov [bp-0x20],ax                                ; 263D
        mov [bp-0x1e],dx                                ; 2640
        mov ax,[bp-0xe]                                 ; 2643
        or ax,[bp-0x10]                                 ; 2646
        jz short L3_269D                                ; 2649
        mov word [bp-0xc],0x4                           ; 264B
        mov [bp-0xa],dx                                 ; 2650
        push word [bp-0xe]                              ; 2653
        push word [bp-0x10]                             ; 2656
        mov ax,0x58a                                    ; 2659
        push cs                                         ; 265C
        push ax                                         ; 265D
        sub_ ax,ax                                      ; 265E
        push ax                                         ; 2660
        push ax                                         ; 2661
        push ax                                         ; 2662
        push ax                                         ; 2663
        lea ax,[bp-0x24]                                ; 2664
        push ss                                         ; 2667
        push ax                                         ; 2668
        lea ax,[bp-0xc]                                 ; 2669
        push ss                                         ; 266C
        push ax                                         ; 266D
        callp R3_266F, R3_2699, 0x0000                  ; 266E KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2673
        mov word [bp-0xa],0x0                           ; 2678
        push word [bp-0xe]                              ; 267D
        push word [bp-0x10]                             ; 2680
        mov ax,0x7d9                                    ; 2683
        push cs                                         ; 2686
        push ax                                         ; 2687
        sub_ ax,ax                                      ; 2688
        push ax                                         ; 268A
        push ax                                         ; 268B
        push ax                                         ; 268C
        push ax                                         ; 268D
        lea ax,[bp-0x20]                                ; 268E
        push ss                                         ; 2691
        push ax                                         ; 2692
        lea ax,[bp-0xc]                                 ; 2693
        push ss                                         ; 2696
        push ax                                         ; 2697
        callp R3_2699, R3_2726, 0x0000                  ; 2698 KERNEL.RegQueryValueEx

L3_269D:
        mov word [bp-0x4c],0xe                          ; 269D
        mov word [bp-0x4a],0x0                          ; 26A2
        mov word [bp-0x50],0x18                         ; 26A7
        mov word [bp-0x4e],0x0                          ; 26AC
        mov es,[bp-0x6]                                 ; 26B1
        mov ax,[es:di+0x25c2]                           ; 26B4
        mov [bp-0x48],ax                                ; 26B9
        mov word [bp-0x46],0x0                          ; 26BC
        sub_ ax,ax                                      ; 26C1
        mov [bp-0x42],ax                                ; 26C3
        mov [bp-0x44],ax                                ; 26C6
        mov word [bp-0x40],0x4                          ; 26C9
        mov [bp-0x3e],ax                                ; 26CE
        lea ax,[bp-0x24]                                ; 26D1
        mov [bp-0x3c],ax                                ; 26D4
        mov [bp-0x3a],ss                                ; 26D7
        push si                                         ; 26DA
        lea cx,[bp-0x50]                                ; 26DB
        push ss                                         ; 26DE
        push cx                                         ; 26DF
        sub_ cx,cx                                      ; 26E0
        push cx                                         ; 26E2
        push cx                                         ; 26E3
        callf mxd_set_control_details, R3_26E7, R3_2096 ; 26E4 far seg5
        mov ax,0x8000                                   ; 26E9
        xor_ dx,dx                                      ; 26EC
        mov [bp-0x24],ax                                ; 26EE
        mov [bp-0x22],dx                                ; 26F1
        mov [bp-0x20],ax                                ; 26F4
        mov [bp-0x1e],dx                                ; 26F7
        mov ax,[bp-0xe]                                 ; 26FA
        or ax,[bp-0x10]                                 ; 26FD
        jz short L3_2754                                ; 2700
        mov word [bp-0xc],0x4                           ; 2702
        mov [bp-0xa],dx                                 ; 2707
        push word [bp-0xe]                              ; 270A
        push word [bp-0x10]                             ; 270D
        mov ax,0x545                                    ; 2710
        push cs                                         ; 2713
        push ax                                         ; 2714
        sub_ ax,ax                                      ; 2715
        push ax                                         ; 2717
        push ax                                         ; 2718
        push ax                                         ; 2719
        push ax                                         ; 271A
        lea ax,[bp-0x24]                                ; 271B
        push ss                                         ; 271E
        push ax                                         ; 271F
        lea ax,[bp-0xc]                                 ; 2720
        push ss                                         ; 2723
        push ax                                         ; 2724
        callp R3_2726, R3_201E, 0x0000                  ; 2725 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 272A
        mov word [bp-0xa],0x0                           ; 272F
        push word [bp-0xe]                              ; 2734
        push word [bp-0x10]                             ; 2737
        mov ax,0x76e                                    ; 273A
        push cs                                         ; 273D
        push ax                                         ; 273E
        sub_ ax,ax                                      ; 273F
        push ax                                         ; 2741
        push ax                                         ; 2742
        push ax                                         ; 2743
        push ax                                         ; 2744
        lea ax,[bp-0x20]                                ; 2745
        push ss                                         ; 2748
        push ax                                         ; 2749
        lea ax,[bp-0xc]                                 ; 274A
        push ss                                         ; 274D
        push ax                                         ; 274E
        callp R3_2750, R3_27DD, 0x0000                  ; 274F KERNEL.RegQueryValueEx

L3_2754:
        mov word [bp-0x4c],0xf                          ; 2754
        mov word [bp-0x4a],0x0                          ; 2759
        mov word [bp-0x50],0x18                         ; 275E
        mov word [bp-0x4e],0x0                          ; 2763
        mov es,[bp-0x6]                                 ; 2768
        mov ax,[es:di+0x25c8]                           ; 276B
        mov [bp-0x48],ax                                ; 2770
        mov word [bp-0x46],0x0                          ; 2773
        sub_ ax,ax                                      ; 2778
        mov [bp-0x42],ax                                ; 277A
        mov [bp-0x44],ax                                ; 277D
        mov word [bp-0x40],0x4                          ; 2780
        mov [bp-0x3e],ax                                ; 2785
        lea ax,[bp-0x24]                                ; 2788
        mov [bp-0x3c],ax                                ; 278B
        mov [bp-0x3a],ss                                ; 278E
        push si                                         ; 2791
        lea cx,[bp-0x50]                                ; 2792
        push ss                                         ; 2795
        push cx                                         ; 2796
        sub_ cx,cx                                      ; 2797
        push cx                                         ; 2799
        push cx                                         ; 279A
        callf mxd_set_control_details, R3_279E, R3_2855 ; 279B far seg5
        mov ax,0x8000                                   ; 27A0
        xor_ dx,dx                                      ; 27A3
        mov [bp-0x24],ax                                ; 27A5
        mov [bp-0x22],dx                                ; 27A8
        mov [bp-0x20],ax                                ; 27AB
        mov [bp-0x1e],dx                                ; 27AE
        mov ax,[bp-0xe]                                 ; 27B1
        or ax,[bp-0x10]                                 ; 27B4
        jz short L3_280B                                ; 27B7
        mov word [bp-0xc],0x4                           ; 27B9
        mov [bp-0xa],dx                                 ; 27BE
        push word [bp-0xe]                              ; 27C1
        push word [bp-0x10]                             ; 27C4
        mov ax,0x566                                    ; 27C7
        push cs                                         ; 27CA
        push ax                                         ; 27CB
        sub_ ax,ax                                      ; 27CC
        push ax                                         ; 27CE
        push ax                                         ; 27CF
        push ax                                         ; 27D0
        push ax                                         ; 27D1
        lea ax,[bp-0x24]                                ; 27D2
        push ss                                         ; 27D5
        push ax                                         ; 27D6
        lea ax,[bp-0xc]                                 ; 27D7
        push ss                                         ; 27DA
        push ax                                         ; 27DB
        callp R3_27DD, R3_2807, 0x0000                  ; 27DC KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 27E1
        mov word [bp-0xa],0x0                           ; 27E6
        push word [bp-0xe]                              ; 27EB
        push word [bp-0x10]                             ; 27EE
        mov ax,0x7a3                                    ; 27F1
        push cs                                         ; 27F4
        push ax                                         ; 27F5
        sub_ ax,ax                                      ; 27F6
        push ax                                         ; 27F8
        push ax                                         ; 27F9
        push ax                                         ; 27FA
        push ax                                         ; 27FB
        lea ax,[bp-0x20]                                ; 27FC
        push ss                                         ; 27FF
        push ax                                         ; 2800
        lea ax,[bp-0xc]                                 ; 2801
        push ss                                         ; 2804
        push ax                                         ; 2805
        callp R3_2807, R3_2894, 0x0000                  ; 2806 KERNEL.RegQueryValueEx

L3_280B:
        mov word [bp-0x4c],0x10                         ; 280B
        mov word [bp-0x4a],0x0                          ; 2810
        mov word [bp-0x50],0x18                         ; 2815
        mov word [bp-0x4e],0x0                          ; 281A
        mov es,[bp-0x6]                                 ; 281F
        mov ax,[es:di+0x25ce]                           ; 2822
        mov [bp-0x48],ax                                ; 2827
        mov word [bp-0x46],0x0                          ; 282A
        sub_ ax,ax                                      ; 282F
        mov [bp-0x42],ax                                ; 2831
        mov [bp-0x44],ax                                ; 2834
        mov word [bp-0x40],0x4                          ; 2837
        mov [bp-0x3e],ax                                ; 283C
        lea ax,[bp-0x24]                                ; 283F
        mov [bp-0x3c],ax                                ; 2842
        mov [bp-0x3a],ss                                ; 2845
        push si                                         ; 2848
        lea cx,[bp-0x50]                                ; 2849
        push ss                                         ; 284C
        push cx                                         ; 284D
        sub_ cx,cx                                      ; 284E
        push cx                                         ; 2850
        push cx                                         ; 2851
        callf mxd_set_control_details, R3_2855, R3_290C ; 2852 far seg5
        mov ax,0x8000                                   ; 2857
        xor_ dx,dx                                      ; 285A
        mov [bp-0x24],ax                                ; 285C
        mov [bp-0x22],dx                                ; 285F
        mov [bp-0x20],ax                                ; 2862
        mov [bp-0x1e],dx                                ; 2865
        mov ax,[bp-0xe]                                 ; 2868
        or ax,[bp-0x10]                                 ; 286B
        jz short L3_28C2                                ; 286E
        mov word [bp-0xc],0x4                           ; 2870
        mov [bp-0xa],dx                                 ; 2875
        push word [bp-0xe]                              ; 2878
        push word [bp-0x10]                             ; 287B
        mov ax,0x512                                    ; 287E
        push cs                                         ; 2881
        push ax                                         ; 2882
        sub_ ax,ax                                      ; 2883
        push ax                                         ; 2885
        push ax                                         ; 2886
        push ax                                         ; 2887
        push ax                                         ; 2888
        lea ax,[bp-0x24]                                ; 2889
        push ss                                         ; 288C
        push ax                                         ; 288D
        lea ax,[bp-0xc]                                 ; 288E
        push ss                                         ; 2891
        push ax                                         ; 2892
        callp R3_2894, R3_28BE, 0x0000                  ; 2893 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2898
        mov word [bp-0xa],0x0                           ; 289D
        push word [bp-0xe]                              ; 28A2
        push word [bp-0x10]                             ; 28A5
        mov ax,0x748                                    ; 28A8
        push cs                                         ; 28AB
        push ax                                         ; 28AC
        sub_ ax,ax                                      ; 28AD
        push ax                                         ; 28AF
        push ax                                         ; 28B0
        push ax                                         ; 28B1
        push ax                                         ; 28B2
        lea ax,[bp-0x20]                                ; 28B3
        push ss                                         ; 28B6
        push ax                                         ; 28B7
        lea ax,[bp-0xc]                                 ; 28B8
        push ss                                         ; 28BB
        push ax                                         ; 28BC
        callp R3_28BE, R3_2944, 0x0000                  ; 28BD KERNEL.RegQueryValueEx

L3_28C2:
        mov word [bp-0x4c],0x11                         ; 28C2
        mov word [bp-0x4a],0x0                          ; 28C7
        mov word [bp-0x50],0x18                         ; 28CC
        mov word [bp-0x4e],0x0                          ; 28D1
        mov es,[bp-0x6]                                 ; 28D6
        mov ax,[es:di+0x25d4]                           ; 28D9
        mov [bp-0x48],ax                                ; 28DE
        mov word [bp-0x46],0x0                          ; 28E1
        sub_ ax,ax                                      ; 28E6
        mov [bp-0x42],ax                                ; 28E8
        mov [bp-0x44],ax                                ; 28EB
        mov word [bp-0x40],0x4                          ; 28EE
        mov [bp-0x3e],ax                                ; 28F3
        lea ax,[bp-0x24]                                ; 28F6
        mov [bp-0x3c],ax                                ; 28F9
        mov [bp-0x3a],ss                                ; 28FC
        push si                                         ; 28FF
        lea ax,[bp-0x50]                                ; 2900
        push ss                                         ; 2903
        push ax                                         ; 2904
        sub_ ax,ax                                      ; 2905
        push ax                                         ; 2907
        push ax                                         ; 2908
        callf mxd_set_control_details, R3_290C, R3_2992 ; 2909 far seg5
        sub_ ax,ax                                      ; 290E
        mov [bp-0x76],ax                                ; 2910
        mov [bp-0x78],ax                                ; 2913
        mov ax,[bp-0xe]                                 ; 2916
        or ax,[bp-0x10]                                 ; 2919
        jz short L3_2948                                ; 291C
        mov word [bp-0xc],0x4                           ; 291E
        mov word [bp-0xa],0x0                           ; 2923
        push word [bp-0xe]                              ; 2928
        push word [bp-0x10]                             ; 292B
        mov ax,0x4ed                                    ; 292E
        push cs                                         ; 2931
        push ax                                         ; 2932
        sub_ ax,ax                                      ; 2933
        push ax                                         ; 2935
        push ax                                         ; 2936
        push ax                                         ; 2937
        push ax                                         ; 2938
        lea ax,[bp-0x78]                                ; 2939
        push ss                                         ; 293C
        push ax                                         ; 293D
        lea ax,[bp-0xc]                                 ; 293E
        push ss                                         ; 2941
        push ax                                         ; 2942
        callp R3_2944, R3_29CC, 0x0000                  ; 2943 KERNEL.RegQueryValueEx

L3_2948:
        mov word [bp-0x4c],0x26                         ; 2948
        mov word [bp-0x4a],0x0                          ; 294D
        mov word [bp-0x50],0x18                         ; 2952
        mov word [bp-0x4e],0x0                          ; 2957
        mov es,[bp-0x6]                                 ; 295C
        mov ax,[es:di+0x2652]                           ; 295F
        mov [bp-0x48],ax                                ; 2964
        mov word [bp-0x46],0x0                          ; 2967
        sub_ ax,ax                                      ; 296C
        mov [bp-0x42],ax                                ; 296E
        mov [bp-0x44],ax                                ; 2971
        mov word [bp-0x40],0x4                          ; 2974
        mov [bp-0x3e],ax                                ; 2979
        lea ax,[bp-0x78]                                ; 297C
        mov [bp-0x3c],ax                                ; 297F
        mov [bp-0x3a],ss                                ; 2982
        push si                                         ; 2985
        lea ax,[bp-0x50]                                ; 2986
        push ss                                         ; 2989
        push ax                                         ; 298A
        sub_ ax,ax                                      ; 298B
        push ax                                         ; 298D
        push ax                                         ; 298E
        callf mxd_set_control_details, R3_2992, R3_2A70 ; 298F far seg5
        mov word [bp-0x18],0x2                          ; 2994
        mov word [bp-0x16],0x0                          ; 2999
        mov ax,[bp-0xe]                                 ; 299E
        or ax,[bp-0x10]                                 ; 29A1
        jz short L3_29D0                                ; 29A4
        mov word [bp-0xc],0x4                           ; 29A6
        mov word [bp-0xa],0x0                           ; 29AB
        push word [bp-0xe]                              ; 29B0
        push word [bp-0x10]                             ; 29B3
        mov ax,0x4c7                                    ; 29B6
        push cs                                         ; 29B9
        push ax                                         ; 29BA
        sub_ ax,ax                                      ; 29BB
        push ax                                         ; 29BD
        push ax                                         ; 29BE
        push ax                                         ; 29BF
        push ax                                         ; 29C0
        lea ax,[bp-0x18]                                ; 29C1
        push ss                                         ; 29C4
        push ax                                         ; 29C5
        lea ax,[bp-0xc]                                 ; 29C6
        push ss                                         ; 29C9
        push ax                                         ; 29CA
        callp R3_29CC, R3_2AAF, 0x0000                  ; 29CB KERNEL.RegQueryValueEx

L3_29D0:
        mov word [bp-0x4c],0x2                          ; 29D0
        mov word [bp-0x4a],0x0                          ; 29D5
        mov word [bp-0x50],0x18                         ; 29DA
        mov word [bp-0x4e],0x0                          ; 29DF
        mov word [bp-0x48],0x1                          ; 29E4
        mov word [bp-0x46],0x0                          ; 29E9
        mov ax,0x94                                     ; 29EE
        mul word [bp-0x4c]                              ; 29F1
        mov_ si,ax                                      ; 29F4
        mov_ bx,di                                      ; 29F6
        mov es,[bp-0x6]                                 ; 29F8
        mov ax,[es:bx+si+0x926]                         ; 29FB
        mov dx,[es:bx+si+0x928]                         ; 2A00
        mov [bp-0x44],ax                                ; 2A05
        mov [bp-0x42],dx                                ; 2A08
        mov word [bp-0x40],0x4                          ; 2A0B
        mov word [bp-0x3e],0x0                          ; 2A10
        lea ax,[bp-0x78]                                ; 2A15
        mov [bp-0x3c],ax                                ; 2A18
        mov [bp-0x3a],ss                                ; 2A1B
        mov word [bp-0x2],0x0                           ; 2A1E
        mov [bp-0x8],di                                 ; 2A23
        cmp word [bp-0x44],byte +0x0                    ; 2A26
        jz short L3_2A61                                ; 2A2A
        lea dx,[bp-0x78]                                ; 2A2C
        mov [bp-0x4],dx                                 ; 2A2F
        mov_ si,dx                                      ; 2A32
        mov bx,[bp-0x2]                                 ; 2A34

L3_2A37:
        mov ax,0x1                                      ; 2A37
        mov_ cx,bx                                      ; 2A3A
        shl ax,cl                                       ; 2A3C
        cwd                                             ; 2A3E
        test [bp-0x16],dx                               ; 2A3F
        ja short L3_2A49                                ; 2A42
        test [bp-0x18],ax                               ; 2A44
        jna short L3_2A4E                               ; 2A47

L3_2A49:
        mov ax,0x1                                      ; 2A49
        jmp short L3_2A50                               ; 2A4C

L3_2A4E:
        xor_ ax,ax                                      ; 2A4E

L3_2A50:
        cwd                                             ; 2A50
        mov [ss:si],ax                                  ; 2A51
        mov [ss:si+0x2],dx                              ; 2A54
        add si,byte +0x4                                ; 2A58
        inc bx                                          ; 2A5B
        cmp [bp-0x44],bx                                ; 2A5C
        ja short L3_2A37                                ; 2A5F

L3_2A61:
        push word [bp+0x6]                              ; 2A61
        lea ax,[bp-0x50]                                ; 2A64
        push ss                                         ; 2A67
        push ax                                         ; 2A68
        sub_ ax,ax                                      ; 2A69
        push ax                                         ; 2A6B
        push ax                                         ; 2A6C
        callf mxd_set_control_details, R3_2A70, R3_23BF ; 2A6D far seg5
        mov ax,0x8000                                   ; 2A72
        xor_ dx,dx                                      ; 2A75
        mov [bp-0x24],ax                                ; 2A77
        mov [bp-0x22],dx                                ; 2A7A
        mov [bp-0x20],ax                                ; 2A7D
        mov [bp-0x1e],dx                                ; 2A80
        mov ax,[bp-0xe]                                 ; 2A83
        or ax,[bp-0x10]                                 ; 2A86
        jz short L3_2ADD                                ; 2A89
        mov word [bp-0xc],0x4                           ; 2A8B
        mov [bp-0xa],dx                                 ; 2A90
        push word [bp-0xe]                              ; 2A93
        push word [bp-0x10]                             ; 2A96
        mov ax,0x4af                                    ; 2A99
        push cs                                         ; 2A9C
        push ax                                         ; 2A9D
        sub_ ax,ax                                      ; 2A9E
        push ax                                         ; 2AA0
        push ax                                         ; 2AA1
        push ax                                         ; 2AA2
        push ax                                         ; 2AA3
        lea ax,[bp-0x24]                                ; 2AA4
        push ss                                         ; 2AA7
        push ax                                         ; 2AA8
        lea ax,[bp-0xc]                                 ; 2AA9
        push ss                                         ; 2AAC
        push ax                                         ; 2AAD
        callp R3_2AAF, R3_2AD9, 0x0000                  ; 2AAE KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2AB3
        mov word [bp-0xa],0x0                           ; 2AB8
        push word [bp-0xe]                              ; 2ABD
        push word [bp-0x10]                             ; 2AC0
        mov ax,0x714                                    ; 2AC3
        push cs                                         ; 2AC6
        push ax                                         ; 2AC7
        sub_ ax,ax                                      ; 2AC8
        push ax                                         ; 2ACA
        push ax                                         ; 2ACB
        push ax                                         ; 2ACC
        push ax                                         ; 2ACD
        lea ax,[bp-0x20]                                ; 2ACE
        push ss                                         ; 2AD1
        push ax                                         ; 2AD2
        lea ax,[bp-0xc]                                 ; 2AD3
        push ss                                         ; 2AD6
        push ax                                         ; 2AD7
        callp R3_2AD9, R3_23FE, 0x0000                  ; 2AD8 KERNEL.RegQueryValueEx

L3_2ADD:
        mov ax,0x18                                     ; 2ADD
        cwd                                             ; 2AE0
        mov [bp-0x4c],ax                                ; 2AE1
        mov [bp-0x4a],dx                                ; 2AE4
        mov [bp-0x50],ax                                ; 2AE7
        mov [bp-0x4e],dx                                ; 2AEA
        les bx,[bp-0x8]                                 ; 2AED
        mov ax,[es:bx+0x25fe]                           ; 2AF0
        mov [bp-0x48],ax                                ; 2AF5
        mov word [bp-0x46],0x0                          ; 2AF8
        sub_ ax,ax                                      ; 2AFD
        mov [bp-0x42],ax                                ; 2AFF
        mov [bp-0x44],ax                                ; 2B02
        mov word [bp-0x40],0x4                          ; 2B05
        mov [bp-0x3e],ax                                ; 2B0A
        lea ax,[bp-0x24]                                ; 2B0D
        mov [bp-0x3c],ax                                ; 2B10
        mov [bp-0x3a],ss                                ; 2B13
        push word [bp+0x6]                              ; 2B16
        lea cx,[bp-0x50]                                ; 2B19
        push ss                                         ; 2B1C
        push cx                                         ; 2B1D
        sub_ cx,cx                                      ; 2B1E
        push cx                                         ; 2B20
        push cx                                         ; 2B21
        callf mxd_set_control_details, R3_2B25, R3_2BDE ; 2B22 far seg5
        mov ax,0x8000                                   ; 2B27
        xor_ dx,dx                                      ; 2B2A
        mov [bp-0x24],ax                                ; 2B2C
        mov [bp-0x22],dx                                ; 2B2F
        mov [bp-0x20],ax                                ; 2B32
        mov [bp-0x1e],dx                                ; 2B35
        mov ax,[bp-0xe]                                 ; 2B38
        or ax,[bp-0x10]                                 ; 2B3B
        jz short L3_2B92                                ; 2B3E
        mov word [bp-0xc],0x4                           ; 2B40
        mov [bp-0xa],dx                                 ; 2B45
        push word [bp-0xe]                              ; 2B48
        push word [bp-0x10]                             ; 2B4B
        mov ax,0x472                                    ; 2B4E
        push cs                                         ; 2B51
        push ax                                         ; 2B52
        sub_ ax,ax                                      ; 2B53
        push ax                                         ; 2B55
        push ax                                         ; 2B56
        push ax                                         ; 2B57
        push ax                                         ; 2B58
        lea ax,[bp-0x24]                                ; 2B59
        push ss                                         ; 2B5C
        push ax                                         ; 2B5D
        lea ax,[bp-0xc]                                 ; 2B5E
        push ss                                         ; 2B61
        push ax                                         ; 2B62
        callp R3_2B64, R3_2B8E, 0x0000                  ; 2B63 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2B68
        mov word [bp-0xa],0x0                           ; 2B6D
        push word [bp-0xe]                              ; 2B72
        push word [bp-0x10]                             ; 2B75
        mov ax,0x6d9                                    ; 2B78
        push cs                                         ; 2B7B
        push ax                                         ; 2B7C
        sub_ ax,ax                                      ; 2B7D
        push ax                                         ; 2B7F
        push ax                                         ; 2B80
        push ax                                         ; 2B81
        push ax                                         ; 2B82
        lea ax,[bp-0x20]                                ; 2B83
        push ss                                         ; 2B86
        push ax                                         ; 2B87
        lea ax,[bp-0xc]                                 ; 2B88
        push ss                                         ; 2B8B
        push ax                                         ; 2B8C
        callp R3_2B8E, R3_2C1D, 0x0000                  ; 2B8D KERNEL.RegQueryValueEx

L3_2B92:
        mov word [bp-0x4c],0x13                         ; 2B92
        mov word [bp-0x4a],0x0                          ; 2B97
        mov word [bp-0x50],0x18                         ; 2B9C
        mov word [bp-0x4e],0x0                          ; 2BA1
        les bx,[bp-0x8]                                 ; 2BA6
        mov ax,[es:bx+0x25e0]                           ; 2BA9
        mov [bp-0x48],ax                                ; 2BAE
        mov word [bp-0x46],0x0                          ; 2BB1
        sub_ ax,ax                                      ; 2BB6
        mov [bp-0x42],ax                                ; 2BB8
        mov [bp-0x44],ax                                ; 2BBB
        mov word [bp-0x40],0x4                          ; 2BBE
        mov [bp-0x3e],ax                                ; 2BC3
        lea ax,[bp-0x24]                                ; 2BC6
        mov [bp-0x3c],ax                                ; 2BC9
        mov [bp-0x3a],ss                                ; 2BCC
        push word [bp+0x6]                              ; 2BCF
        lea cx,[bp-0x50]                                ; 2BD2
        push ss                                         ; 2BD5
        push cx                                         ; 2BD6
        sub_ cx,cx                                      ; 2BD7
        push cx                                         ; 2BD9
        push cx                                         ; 2BDA
        callf mxd_set_control_details, R3_2BDE, R3_2C97 ; 2BDB far seg5
        mov ax,0x8000                                   ; 2BE0
        xor_ dx,dx                                      ; 2BE3
        mov [bp-0x24],ax                                ; 2BE5
        mov [bp-0x22],dx                                ; 2BE8
        mov [bp-0x20],ax                                ; 2BEB
        mov [bp-0x1e],dx                                ; 2BEE
        mov ax,[bp-0xe]                                 ; 2BF1
        or ax,[bp-0x10]                                 ; 2BF4
        jz short L3_2C4B                                ; 2BF7
        mov word [bp-0xc],0x4                           ; 2BF9
        mov [bp-0xa],dx                                 ; 2BFE
        push word [bp-0xe]                              ; 2C01
        push word [bp-0x10]                             ; 2C04
        mov ax,0x440                                    ; 2C07
        push cs                                         ; 2C0A
        push ax                                         ; 2C0B
        sub_ ax,ax                                      ; 2C0C
        push ax                                         ; 2C0E
        push ax                                         ; 2C0F
        push ax                                         ; 2C10
        push ax                                         ; 2C11
        lea ax,[bp-0x24]                                ; 2C12
        push ss                                         ; 2C15
        push ax                                         ; 2C16
        lea ax,[bp-0xc]                                 ; 2C17
        push ss                                         ; 2C1A
        push ax                                         ; 2C1B
        callp R3_2C1D, R3_2C47, 0x0000                  ; 2C1C KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2C21
        mov word [bp-0xa],0x0                           ; 2C26
        push word [bp-0xe]                              ; 2C2B
        push word [bp-0x10]                             ; 2C2E
        mov ax,0x6a6                                    ; 2C31
        push cs                                         ; 2C34
        push ax                                         ; 2C35
        sub_ ax,ax                                      ; 2C36
        push ax                                         ; 2C38
        push ax                                         ; 2C39
        push ax                                         ; 2C3A
        push ax                                         ; 2C3B
        lea ax,[bp-0x20]                                ; 2C3C
        push ss                                         ; 2C3F
        push ax                                         ; 2C40
        lea ax,[bp-0xc]                                 ; 2C41
        push ss                                         ; 2C44
        push ax                                         ; 2C45
        callp R3_2C47, R3_2CD6, 0x0000                  ; 2C46 KERNEL.RegQueryValueEx

L3_2C4B:
        mov word [bp-0x4c],0x14                         ; 2C4B
        mov word [bp-0x4a],0x0                          ; 2C50
        mov word [bp-0x50],0x18                         ; 2C55
        mov word [bp-0x4e],0x0                          ; 2C5A
        les bx,[bp-0x8]                                 ; 2C5F
        mov ax,[es:bx+0x25e6]                           ; 2C62
        mov [bp-0x48],ax                                ; 2C67
        mov word [bp-0x46],0x0                          ; 2C6A
        sub_ ax,ax                                      ; 2C6F
        mov [bp-0x42],ax                                ; 2C71
        mov [bp-0x44],ax                                ; 2C74
        mov word [bp-0x40],0x4                          ; 2C77
        mov [bp-0x3e],ax                                ; 2C7C
        lea ax,[bp-0x24]                                ; 2C7F
        mov [bp-0x3c],ax                                ; 2C82
        mov [bp-0x3a],ss                                ; 2C85
        push word [bp+0x6]                              ; 2C88
        lea cx,[bp-0x50]                                ; 2C8B
        push ss                                         ; 2C8E
        push cx                                         ; 2C8F
        sub_ cx,cx                                      ; 2C90
        push cx                                         ; 2C92
        push cx                                         ; 2C93
        callf mxd_set_control_details, R3_2C97, R3_2D50 ; 2C94 far seg5
        mov ax,0x8000                                   ; 2C99
        xor_ dx,dx                                      ; 2C9C
        mov [bp-0x24],ax                                ; 2C9E
        mov [bp-0x22],dx                                ; 2CA1
        mov [bp-0x20],ax                                ; 2CA4
        mov [bp-0x1e],dx                                ; 2CA7
        mov ax,[bp-0xe]                                 ; 2CAA
        or ax,[bp-0x10]                                 ; 2CAD
        jz short L3_2D04                                ; 2CB0
        mov word [bp-0xc],0x4                           ; 2CB2
        mov [bp-0xa],dx                                 ; 2CB7
        push word [bp-0xe]                              ; 2CBA
        push word [bp-0x10]                             ; 2CBD
        mov ax,0x418                                    ; 2CC0
        push cs                                         ; 2CC3
        push ax                                         ; 2CC4
        sub_ ax,ax                                      ; 2CC5
        push ax                                         ; 2CC7
        push ax                                         ; 2CC8
        push ax                                         ; 2CC9
        push ax                                         ; 2CCA
        lea ax,[bp-0x24]                                ; 2CCB
        push ss                                         ; 2CCE
        push ax                                         ; 2CCF
        lea ax,[bp-0xc]                                 ; 2CD0
        push ss                                         ; 2CD3
        push ax                                         ; 2CD4
        callp R3_2CD6, R3_2D00, 0x0000                  ; 2CD5 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2CDA
        mov word [bp-0xa],0x0                           ; 2CDF
        push word [bp-0xe]                              ; 2CE4
        push word [bp-0x10]                             ; 2CE7
        mov ax,0x668                                    ; 2CEA
        push cs                                         ; 2CED
        push ax                                         ; 2CEE
        sub_ ax,ax                                      ; 2CEF
        push ax                                         ; 2CF1
        push ax                                         ; 2CF2
        push ax                                         ; 2CF3
        push ax                                         ; 2CF4
        lea ax,[bp-0x20]                                ; 2CF5
        push ss                                         ; 2CF8
        push ax                                         ; 2CF9
        lea ax,[bp-0xc]                                 ; 2CFA
        push ss                                         ; 2CFD
        push ax                                         ; 2CFE
        callp R3_2D00, R3_2D8F, 0x0000                  ; 2CFF KERNEL.RegQueryValueEx

L3_2D04:
        mov word [bp-0x4c],0x15                         ; 2D04
        mov word [bp-0x4a],0x0                          ; 2D09
        mov word [bp-0x50],0x18                         ; 2D0E
        mov word [bp-0x4e],0x0                          ; 2D13
        les bx,[bp-0x8]                                 ; 2D18
        mov ax,[es:bx+0x25ec]                           ; 2D1B
        mov [bp-0x48],ax                                ; 2D20
        mov word [bp-0x46],0x0                          ; 2D23
        sub_ ax,ax                                      ; 2D28
        mov [bp-0x42],ax                                ; 2D2A
        mov [bp-0x44],ax                                ; 2D2D
        mov word [bp-0x40],0x4                          ; 2D30
        mov [bp-0x3e],ax                                ; 2D35
        lea ax,[bp-0x24]                                ; 2D38
        mov [bp-0x3c],ax                                ; 2D3B
        mov [bp-0x3a],ss                                ; 2D3E
        push word [bp+0x6]                              ; 2D41
        lea cx,[bp-0x50]                                ; 2D44
        push ss                                         ; 2D47
        push cx                                         ; 2D48
        sub_ cx,cx                                      ; 2D49
        push cx                                         ; 2D4B
        push cx                                         ; 2D4C
        callf mxd_set_control_details, R3_2D50, R3_2E09 ; 2D4D far seg5
        mov ax,0x8000                                   ; 2D52
        xor_ dx,dx                                      ; 2D55
        mov [bp-0x24],ax                                ; 2D57
        mov [bp-0x22],dx                                ; 2D5A
        mov [bp-0x20],ax                                ; 2D5D
        mov [bp-0x1e],dx                                ; 2D60
        mov ax,[bp-0xe]                                 ; 2D63
        or ax,[bp-0x10]                                 ; 2D66
        jz short L3_2DBD                                ; 2D69
        mov word [bp-0xc],0x4                           ; 2D6B
        mov [bp-0xa],dx                                 ; 2D70
        push word [bp-0xe]                              ; 2D73
        push word [bp-0x10]                             ; 2D76
        mov ax,0x3e3                                    ; 2D79
        push cs                                         ; 2D7C
        push ax                                         ; 2D7D
        sub_ ax,ax                                      ; 2D7E
        push ax                                         ; 2D80
        push ax                                         ; 2D81
        push ax                                         ; 2D82
        push ax                                         ; 2D83
        lea ax,[bp-0x24]                                ; 2D84
        push ss                                         ; 2D87
        push ax                                         ; 2D88
        lea ax,[bp-0xc]                                 ; 2D89
        push ss                                         ; 2D8C
        push ax                                         ; 2D8D
        callp R3_2D8F, R3_2DB9, 0x0000                  ; 2D8E KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2D93
        mov word [bp-0xa],0x0                           ; 2D98
        push word [bp-0xe]                              ; 2D9D
        push word [bp-0x10]                             ; 2DA0
        mov ax,0x648                                    ; 2DA3
        push cs                                         ; 2DA6
        push ax                                         ; 2DA7
        sub_ ax,ax                                      ; 2DA8
        push ax                                         ; 2DAA
        push ax                                         ; 2DAB
        push ax                                         ; 2DAC
        push ax                                         ; 2DAD
        lea ax,[bp-0x20]                                ; 2DAE
        push ss                                         ; 2DB1
        push ax                                         ; 2DB2
        lea ax,[bp-0xc]                                 ; 2DB3
        push ss                                         ; 2DB6
        push ax                                         ; 2DB7
        callp R3_2DB9, R3_2E48, 0x0000                  ; 2DB8 KERNEL.RegQueryValueEx

L3_2DBD:
        mov word [bp-0x4c],0x16                         ; 2DBD
        mov word [bp-0x4a],0x0                          ; 2DC2
        mov word [bp-0x50],0x18                         ; 2DC7
        mov word [bp-0x4e],0x0                          ; 2DCC
        les bx,[bp-0x8]                                 ; 2DD1
        mov ax,[es:bx+0x25f2]                           ; 2DD4
        mov [bp-0x48],ax                                ; 2DD9
        mov word [bp-0x46],0x0                          ; 2DDC
        sub_ ax,ax                                      ; 2DE1
        mov [bp-0x42],ax                                ; 2DE3
        mov [bp-0x44],ax                                ; 2DE6
        mov word [bp-0x40],0x4                          ; 2DE9
        mov [bp-0x3e],ax                                ; 2DEE
        lea ax,[bp-0x24]                                ; 2DF1
        mov [bp-0x3c],ax                                ; 2DF4
        mov [bp-0x3a],ss                                ; 2DF7
        push word [bp+0x6]                              ; 2DFA
        lea cx,[bp-0x50]                                ; 2DFD
        push ss                                         ; 2E00
        push cx                                         ; 2E01
        sub_ cx,cx                                      ; 2E02
        push cx                                         ; 2E04
        push cx                                         ; 2E05
        callf mxd_set_control_details, R3_2E09, R3_279E ; 2E06 far seg5
        mov ax,0x8000                                   ; 2E0B
        xor_ dx,dx                                      ; 2E0E
        mov [bp-0x24],ax                                ; 2E10
        mov [bp-0x22],dx                                ; 2E13
        mov [bp-0x20],ax                                ; 2E16
        mov [bp-0x1e],dx                                ; 2E19
        mov ax,[bp-0xe]                                 ; 2E1C
        or ax,[bp-0x10]                                 ; 2E1F
        jz short L3_2E76                                ; 2E22
        mov word [bp-0xc],0x4                           ; 2E24
        mov [bp-0xa],dx                                 ; 2E29
        push word [bp-0xe]                              ; 2E2C
        push word [bp-0x10]                             ; 2E2F
        mov ax,0x3ab                                    ; 2E32
        push cs                                         ; 2E35
        push ax                                         ; 2E36
        sub_ ax,ax                                      ; 2E37
        push ax                                         ; 2E39
        push ax                                         ; 2E3A
        push ax                                         ; 2E3B
        push ax                                         ; 2E3C
        lea ax,[bp-0x24]                                ; 2E3D
        push ss                                         ; 2E40
        push ax                                         ; 2E41
        lea ax,[bp-0xc]                                 ; 2E42
        push ss                                         ; 2E45
        push ax                                         ; 2E46
        callp R3_2E48, R3_2E72, 0x0000                  ; 2E47 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2E4C
        mov word [bp-0xa],0x0                           ; 2E51
        push word [bp-0xe]                              ; 2E56
        push word [bp-0x10]                             ; 2E59
        mov ax,0x627                                    ; 2E5C
        push cs                                         ; 2E5F
        push ax                                         ; 2E60
        sub_ ax,ax                                      ; 2E61
        push ax                                         ; 2E63
        push ax                                         ; 2E64
        push ax                                         ; 2E65
        push ax                                         ; 2E66
        lea ax,[bp-0x20]                                ; 2E67
        push ss                                         ; 2E6A
        push ax                                         ; 2E6B
        lea ax,[bp-0xc]                                 ; 2E6C
        push ss                                         ; 2E6F
        push ax                                         ; 2E70
        callp R3_2E72, R3_2750, 0x0000                  ; 2E71 KERNEL.RegQueryValueEx

L3_2E76:
        mov word [bp-0x4c],0x17                         ; 2E76
        mov word [bp-0x4a],0x0                          ; 2E7B
        mov word [bp-0x50],0x18                         ; 2E80
        mov word [bp-0x4e],0x0                          ; 2E85
        les bx,[bp-0x8]                                 ; 2E8A
        mov ax,[es:bx+0x25f8]                           ; 2E8D
        mov [bp-0x48],ax                                ; 2E92
        mov word [bp-0x46],0x0                          ; 2E95
        sub_ ax,ax                                      ; 2E9A
        mov [bp-0x42],ax                                ; 2E9C
        mov [bp-0x44],ax                                ; 2E9F
        mov word [bp-0x40],0x4                          ; 2EA2
        mov [bp-0x3e],ax                                ; 2EA7
        lea ax,[bp-0x24]                                ; 2EAA
        mov [bp-0x3c],ax                                ; 2EAD
        mov [bp-0x3a],ss                                ; 2EB0
        push word [bp+0x6]                              ; 2EB3
        lea cx,[bp-0x50]                                ; 2EB6
        push ss                                         ; 2EB9
        push cx                                         ; 2EBA
        sub_ cx,cx                                      ; 2EBB
        push cx                                         ; 2EBD
        push cx                                         ; 2EBE
        callf mxd_set_control_details, R3_2EC2, R3_2F7B ; 2EBF far seg5
        mov ax,0x8000                                   ; 2EC4
        xor_ dx,dx                                      ; 2EC7
        mov [bp-0x24],ax                                ; 2EC9
        mov [bp-0x22],dx                                ; 2ECC
        mov [bp-0x20],ax                                ; 2ECF
        mov [bp-0x1e],dx                                ; 2ED2
        mov ax,[bp-0xe]                                 ; 2ED5
        or ax,[bp-0x10]                                 ; 2ED8
        jz short L3_2F2F                                ; 2EDB
        mov word [bp-0xc],0x4                           ; 2EDD
        mov [bp-0xa],dx                                 ; 2EE2
        push word [bp-0xe]                              ; 2EE5
        push word [bp-0x10]                             ; 2EE8
        mov ax,0x6b7                                    ; 2EEB
        push cs                                         ; 2EEE
        push ax                                         ; 2EEF
        sub_ ax,ax                                      ; 2EF0
        push ax                                         ; 2EF2
        push ax                                         ; 2EF3
        push ax                                         ; 2EF4
        push ax                                         ; 2EF5
        lea ax,[bp-0x24]                                ; 2EF6
        push ss                                         ; 2EF9
        push ax                                         ; 2EFA
        lea ax,[bp-0xc]                                 ; 2EFB
        push ss                                         ; 2EFE
        push ax                                         ; 2EFF
        callp R3_2F01, R3_2F2B, 0x0000                  ; 2F00 KERNEL.RegQueryValueEx
        mov word [bp-0xc],0x4                           ; 2F05
        mov word [bp-0xa],0x0                           ; 2F0A
        push word [bp-0xe]                              ; 2F0F
        push word [bp-0x10]                             ; 2F12
        mov ax,0x68d                                    ; 2F15
        push cs                                         ; 2F18
        push ax                                         ; 2F19
        sub_ ax,ax                                      ; 2F1A
        push ax                                         ; 2F1C
        push ax                                         ; 2F1D
        push ax                                         ; 2F1E
        push ax                                         ; 2F1F
        lea ax,[bp-0x20]                                ; 2F20
        push ss                                         ; 2F23
        push ax                                         ; 2F24
        lea ax,[bp-0xc]                                 ; 2F25
        push ss                                         ; 2F28
        push ax                                         ; 2F29
        callp R3_2F2B, R3_2FBF, 0x0000                  ; 2F2A KERNEL.RegQueryValueEx

L3_2F2F:
        mov word [bp-0x4c],0x30                         ; 2F2F
        mov word [bp-0x4a],0x0                          ; 2F34
        mov word [bp-0x50],0x18                         ; 2F39
        mov word [bp-0x4e],0x0                          ; 2F3E
        les bx,[bp-0x8]                                 ; 2F43
        mov ax,[es:bx+0x268e]                           ; 2F46
        mov [bp-0x48],ax                                ; 2F4B
        mov word [bp-0x46],0x0                          ; 2F4E
        sub_ ax,ax                                      ; 2F53
        mov [bp-0x42],ax                                ; 2F55
        mov [bp-0x44],ax                                ; 2F58
        mov word [bp-0x40],0x4                          ; 2F5B
        mov [bp-0x3e],ax                                ; 2F60
        lea ax,[bp-0x24]                                ; 2F63
        mov [bp-0x3c],ax                                ; 2F66
        mov [bp-0x3a],ss                                ; 2F69
        push word [bp+0x6]                              ; 2F6C
        lea cx,[bp-0x50]                                ; 2F6F
        push ss                                         ; 2F72
        push cx                                         ; 2F73
        sub_ cx,cx                                      ; 2F74
        push cx                                         ; 2F76
        push cx                                         ; 2F77
        callf mxd_set_control_details, R3_2F7B, R3_301D ; 2F78 far seg5
        mov word [bp-0x78],0x1                          ; 2F7D
        mov word [bp-0x76],0x0                          ; 2F82
        mov word [bp-0x24],0x8000                       ; 2F87
        mov word [bp-0x22],0x0                          ; 2F8C
        mov ax,[bp-0xe]                                 ; 2F91
        or ax,[bp-0x10]                                 ; 2F94
        jz short L3_2FD1                                ; 2F97
        mov word [bp-0xc],0x4                           ; 2F99
        mov word [bp-0xa],0x0                           ; 2F9E
        push word [bp-0xe]                              ; 2FA3
        push word [bp-0x10]                             ; 2FA6
        mov ax,0x638                                    ; 2FA9
        push cs                                         ; 2FAC
        push ax                                         ; 2FAD
        sub_ ax,ax                                      ; 2FAE
        push ax                                         ; 2FB0
        push ax                                         ; 2FB1
        push ax                                         ; 2FB2
        push ax                                         ; 2FB3
        lea cx,[bp-0x24]                                ; 2FB4
        push ss                                         ; 2FB7
        push cx                                         ; 2FB8
        lea dx,[bp-0xc]                                 ; 2FB9
        push ss                                         ; 2FBC
        push dx                                         ; 2FBD
        callp R3_2FBF, R3_3064, 0x0000                  ; 2FBE KERNEL.RegQueryValueEx
        test byte [bp-0x24],0x1                         ; 2FC3
        jnz short L3_2FD1                               ; 2FC7
        sub_ ax,ax                                      ; 2FC9
        mov [bp-0x76],ax                                ; 2FCB
        mov [bp-0x78],ax                                ; 2FCE

L3_2FD1:
        mov word [bp-0x4c],0x2e                         ; 2FD1
        mov word [bp-0x4a],0x0                          ; 2FD6
        mov word [bp-0x50],0x18                         ; 2FDB
        mov word [bp-0x4e],0x0                          ; 2FE0
        les bx,[bp-0x8]                                 ; 2FE5
        mov ax,[es:bx+0x2682]                           ; 2FE8
        mov [bp-0x48],ax                                ; 2FED
        mov word [bp-0x46],0x0                          ; 2FF0
        sub_ ax,ax                                      ; 2FF5
        mov [bp-0x42],ax                                ; 2FF7
        mov [bp-0x44],ax                                ; 2FFA
        mov word [bp-0x40],0x4                          ; 2FFD
        mov [bp-0x3e],ax                                ; 3002
        lea ax,[bp-0x78]                                ; 3005
        mov [bp-0x3c],ax                                ; 3008
        mov [bp-0x3a],ss                                ; 300B
        push word [bp+0x6]                              ; 300E
        lea ax,[bp-0x50]                                ; 3011
        push ss                                         ; 3014
        push ax                                         ; 3015
        sub_ ax,ax                                      ; 3016
        push ax                                         ; 3018
        push ax                                         ; 3019
        callf mxd_set_control_details, R3_301D, R3_30BA ; 301A far seg5
        mov bx,[bp+0x6]                                 ; 301F
        test word [bx+0x2c],0x4                         ; 3022
        jnz short L3_302C                               ; 3027
        jmp near L3_3286                                ; 3029

L3_302C:
        mov word [bp-0x78],0x1                          ; 302C
        mov word [bp-0x76],0x0                          ; 3031
        mov ax,[bp-0xe]                                 ; 3036
        or ax,[bp-0x10]                                 ; 3039
        jz short L3_306E                                ; 303C
        mov word [bp-0xc],0x4                           ; 303E
        mov word [bp-0xa],0x0                           ; 3043
        push word [bp-0xe]                              ; 3048
        push word [bp-0x10]                             ; 304B
        mov ax,0x483                                    ; 304E
        push cs                                         ; 3051
        push ax                                         ; 3052
        sub_ ax,ax                                      ; 3053
        push ax                                         ; 3055
        push ax                                         ; 3056
        push ax                                         ; 3057
        push ax                                         ; 3058
        lea ax,[bp-0x78]                                ; 3059
        push ss                                         ; 305C
        push ax                                         ; 305D
        lea ax,[bp-0xc]                                 ; 305E
        push ss                                         ; 3061
        push ax                                         ; 3062
        callp R3_3064, R3_30FE, 0x0000                  ; 3063 KERNEL.RegQueryValueEx
        mov [bp-0x14],ax                                ; 3068
        mov [bp-0x12],dx                                ; 306B

L3_306E:
        mov word [bp-0x4c],0x29                         ; 306E
        mov word [bp-0x4a],0x0                          ; 3073
        mov word [bp-0x50],0x18                         ; 3078
        mov word [bp-0x4e],0x0                          ; 307D
        les bx,[bp-0x8]                                 ; 3082
        mov ax,[es:bx+0x2664]                           ; 3085
        mov [bp-0x48],ax                                ; 308A
        mov word [bp-0x46],0x0                          ; 308D
        sub_ ax,ax                                      ; 3092
        mov [bp-0x42],ax                                ; 3094
        mov [bp-0x44],ax                                ; 3097
        mov word [bp-0x40],0x4                          ; 309A
        mov [bp-0x3e],ax                                ; 309F
        lea ax,[bp-0x78]                                ; 30A2
        mov [bp-0x3c],ax                                ; 30A5
        mov [bp-0x3a],ss                                ; 30A8
        push word [bp+0x6]                              ; 30AB
        lea ax,[bp-0x50]                                ; 30AE
        push ss                                         ; 30B1
        push ax                                         ; 30B2
        sub_ ax,ax                                      ; 30B3
        push ax                                         ; 30B5
        push ax                                         ; 30B6
        callf mxd_set_control_details, R3_30BA, R3_3164 ; 30B7 far seg5
        mov word [bp-0x24],0xffff                       ; 30BC
        mov word [bp-0x22],0xffff                       ; 30C1
        mov word [bp-0x14],0x2                          ; 30C6
        mov word [bp-0x12],0x0                          ; 30CB
        mov ax,[bp-0xe]                                 ; 30D0
        or ax,[bp-0x10]                                 ; 30D3
        jz short L3_3108                                ; 30D6
        mov word [bp-0xc],0x4                           ; 30D8
        mov word [bp-0xa],0x0                           ; 30DD
        push word [bp-0xe]                              ; 30E2
        push word [bp-0x10]                             ; 30E5
        mov ax,0x494                                    ; 30E8
        push cs                                         ; 30EB
        push ax                                         ; 30EC
        sub_ ax,ax                                      ; 30ED
        push ax                                         ; 30EF
        push ax                                         ; 30F0
        push ax                                         ; 30F1
        push ax                                         ; 30F2
        lea ax,[bp-0x24]                                ; 30F3
        push ss                                         ; 30F6
        push ax                                         ; 30F7
        lea ax,[bp-0xc]                                 ; 30F8
        push ss                                         ; 30FB
        push ax                                         ; 30FC
        callp R3_30FE, R3_319E, 0x0000                  ; 30FD KERNEL.RegQueryValueEx
        mov [bp-0x14],ax                                ; 3102
        mov [bp-0x12],dx                                ; 3105

L3_3108:
        mov ax,[bp-0x22]                                ; 3108
        mov [bp-0x20],ax                                ; 310B
        mov word [bp-0x1e],0x0                          ; 310E
        mov word [bp-0x22],0x0                          ; 3113
        mov word [bp-0x4c],0x2a                         ; 3118
        mov word [bp-0x4a],0x0                          ; 311D
        mov word [bp-0x50],0x18                         ; 3122
        mov word [bp-0x4e],0x0                          ; 3127
        les bx,[bp-0x8]                                 ; 312C
        mov ax,[es:bx+0x266a]                           ; 312F
        mov [bp-0x48],ax                                ; 3134
        mov word [bp-0x46],0x0                          ; 3137
        sub_ ax,ax                                      ; 313C
        mov [bp-0x42],ax                                ; 313E
        mov [bp-0x44],ax                                ; 3141
        mov word [bp-0x40],0x4                          ; 3144
        mov [bp-0x3e],ax                                ; 3149
        lea ax,[bp-0x24]                                ; 314C
        mov [bp-0x3c],ax                                ; 314F
        mov [bp-0x3a],ss                                ; 3152
        push word [bp+0x6]                              ; 3155
        lea cx,[bp-0x50]                                ; 3158
        push ss                                         ; 315B
        push cx                                         ; 315C
        sub_ cx,cx                                      ; 315D
        push cx                                         ; 315F
        push cx                                         ; 3160
        callf mxd_set_control_details, R3_3164, R3_31FE ; 3161 far seg5
        mov word [bp-0x24],0x7fff                       ; 3166
        mov word [bp-0x22],0x7fff                       ; 316B
        mov ax,[bp-0xe]                                 ; 3170
        or ax,[bp-0x10]                                 ; 3173
        jz short L3_31A2                                ; 3176
        mov word [bp-0xc],0x4                           ; 3178
        mov word [bp-0xa],0x0                           ; 317D
        push word [bp-0xe]                              ; 3182
        push word [bp-0x10]                             ; 3185
        mov ax,0x4e6                                    ; 3188
        push cs                                         ; 318B
        push ax                                         ; 318C
        sub_ ax,ax                                      ; 318D
        push ax                                         ; 318F
        push ax                                         ; 3190
        push ax                                         ; 3191
        push ax                                         ; 3192
        lea ax,[bp-0x24]                                ; 3193
        push ss                                         ; 3196
        push ax                                         ; 3197
        lea ax,[bp-0xc]                                 ; 3198
        push ss                                         ; 319B
        push ax                                         ; 319C
        callp R3_319E, R3_3238, 0x0000                  ; 319D KERNEL.RegQueryValueEx

L3_31A2:
        mov ax,[bp-0x22]                                ; 31A2
        mov [bp-0x20],ax                                ; 31A5
        mov word [bp-0x1e],0x0                          ; 31A8
        mov word [bp-0x22],0x0                          ; 31AD
        mov word [bp-0x4c],0x2b                         ; 31B2
        mov word [bp-0x4a],0x0                          ; 31B7
        mov word [bp-0x50],0x18                         ; 31BC
        mov word [bp-0x4e],0x0                          ; 31C1
        les bx,[bp-0x8]                                 ; 31C6
        mov ax,[es:bx+0x2670]                           ; 31C9
        mov [bp-0x48],ax                                ; 31CE
        mov word [bp-0x46],0x0                          ; 31D1
        sub_ ax,ax                                      ; 31D6
        mov [bp-0x42],ax                                ; 31D8
        mov [bp-0x44],ax                                ; 31DB
        mov word [bp-0x40],0x4                          ; 31DE
        mov [bp-0x3e],ax                                ; 31E3
        lea ax,[bp-0x24]                                ; 31E6
        mov [bp-0x3c],ax                                ; 31E9
        mov [bp-0x3a],ss                                ; 31EC
        push word [bp+0x6]                              ; 31EF
        lea cx,[bp-0x50]                                ; 31F2
        push ss                                         ; 31F5
        push cx                                         ; 31F6
        sub_ cx,cx                                      ; 31F7
        push cx                                         ; 31F9
        push cx                                         ; 31FA
        callf mxd_set_control_details, R3_31FE, R3_2B25 ; 31FB far seg5
        mov word [bp-0x24],0x7fff                       ; 3200
        mov word [bp-0x22],0x7fff                       ; 3205
        mov ax,[bp-0xe]                                 ; 320A
        or ax,[bp-0x10]                                 ; 320D
        jz short L3_323C                                ; 3210
        mov word [bp-0xc],0x4                           ; 3212
        mov word [bp-0xa],0x0                           ; 3217
        push word [bp-0xe]                              ; 321C
        push word [bp-0x10]                             ; 321F
        mov ax,0x4c2                                    ; 3222
        push cs                                         ; 3225
        push ax                                         ; 3226
        sub_ ax,ax                                      ; 3227
        push ax                                         ; 3229
        push ax                                         ; 322A
        push ax                                         ; 322B
        push ax                                         ; 322C
        lea ax,[bp-0x24]                                ; 322D
        push ss                                         ; 3230
        push ax                                         ; 3231
        lea ax,[bp-0xc]                                 ; 3232
        push ss                                         ; 3235
        push ax                                         ; 3236
        callp R3_3238, R3_2B64, 0x0000                  ; 3237 KERNEL.RegQueryValueEx

L3_323C:
        mov ax,[bp-0x22]                                ; 323C
        mov [bp-0x20],ax                                ; 323F
        mov word [bp-0x1e],0x0                          ; 3242
        mov word [bp-0x22],0x0                          ; 3247
        mov word [bp-0x4c],0x2c                         ; 324C
        mov word [bp-0x4a],0x0                          ; 3251
        mov word [bp-0x50],0x18                         ; 3256
        mov word [bp-0x4e],0x0                          ; 325B
        les bx,[bp-0x8]                                 ; 3260
        mov ax,[es:bx+0x2676]                           ; 3263
        mov [bp-0x48],ax                                ; 3268
        mov word [bp-0x46],0x0                          ; 326B
        sub_ ax,ax                                      ; 3270
        mov [bp-0x42],ax                                ; 3272
        mov [bp-0x44],ax                                ; 3275
        mov word [bp-0x40],0x4                          ; 3278
        mov [bp-0x3e],ax                                ; 327D
        lea ax,[bp-0x24]                                ; 3280
        jmp near L3_3510                                ; 3283

L3_3286:
        test byte [bx+0x2a],0x80                        ; 3286
        jnz short L3_328F                               ; 328A
        jmp near L3_33FC                                ; 328C

L3_328F:
        mov word [bp-0x24],0xffff                       ; 328F
        mov word [bp-0x22],0xffff                       ; 3294
        mov word [bp-0x14],0x2                          ; 3299
        mov word [bp-0x12],0x0                          ; 329E
        mov ax,[bp-0xe]                                 ; 32A3
        or ax,[bp-0x10]                                 ; 32A6
        jz short L3_32DB                                ; 32A9
        mov word [bp-0xc],0x4                           ; 32AB
        mov word [bp-0xa],0x0                           ; 32B0
        push word [bp-0xe]                              ; 32B5
        push word [bp-0x10]                             ; 32B8
        mov ax,0x494                                    ; 32BB
        push cs                                         ; 32BE
        push ax                                         ; 32BF
        sub_ ax,ax                                      ; 32C0
        push ax                                         ; 32C2
        push ax                                         ; 32C3
        push ax                                         ; 32C4
        push ax                                         ; 32C5
        lea ax,[bp-0x24]                                ; 32C6
        push ss                                         ; 32C9
        push ax                                         ; 32CA
        lea ax,[bp-0xc]                                 ; 32CB
        push ss                                         ; 32CE
        push ax                                         ; 32CF
        callp R3_32D1, R3_331B, 0x0000                  ; 32D0 KERNEL.RegQueryValueEx
        mov [bp-0x14],ax                                ; 32D5
        mov [bp-0x12],dx                                ; 32D8

L3_32DB:
        mov ax,[bp-0x12]                                ; 32DB
        or ax,[bp-0x14]                                 ; 32DE
        jz short L3_331F                                ; 32E1
        mov word [bp-0x24],0xffff                       ; 32E3
        mov word [bp-0x22],0xffff                       ; 32E8
        mov ax,[bp-0xe]                                 ; 32ED
        or ax,[bp-0x10]                                 ; 32F0
        jz short L3_331F                                ; 32F3
        mov word [bp-0xc],0x4                           ; 32F5
        mov word [bp-0xa],0x0                           ; 32FA
        push word [bp-0xe]                              ; 32FF
        push word [bp-0x10]                             ; 3302
        mov ax,0x450                                    ; 3305
        push cs                                         ; 3308
        push ax                                         ; 3309
        sub_ ax,ax                                      ; 330A
        push ax                                         ; 330C
        push ax                                         ; 330D
        push ax                                         ; 330E
        push ax                                         ; 330F
        lea ax,[bp-0x24]                                ; 3310
        push ss                                         ; 3313
        push ax                                         ; 3314
        lea ax,[bp-0xc]                                 ; 3315
        push ss                                         ; 3318
        push ax                                         ; 3319
        callp R3_331B, R3_33BF, 0x0000                  ; 331A KERNEL.RegQueryValueEx

L3_331F:
        mov ax,[bp-0x22]                                ; 331F
        mov [bp-0x20],ax                                ; 3322
        mov word [bp-0x1e],0x0                          ; 3325
        mov word [bp-0x22],0x0                          ; 332A
        mov word [bp-0x4c],0x2a                         ; 332F
        mov word [bp-0x4a],0x0                          ; 3334
        mov word [bp-0x50],0x18                         ; 3339
        mov word [bp-0x4e],0x0                          ; 333E
        les bx,[bp-0x8]                                 ; 3343
        mov ax,[es:bx+0x266a]                           ; 3346
        mov [bp-0x48],ax                                ; 334B
        mov word [bp-0x46],0x0                          ; 334E
        sub_ ax,ax                                      ; 3353
        mov [bp-0x42],ax                                ; 3355
        mov [bp-0x44],ax                                ; 3358
        mov word [bp-0x40],0x4                          ; 335B
        mov [bp-0x3e],ax                                ; 3360
        lea ax,[bp-0x24]                                ; 3363
        mov [bp-0x3c],ax                                ; 3366
        mov [bp-0x3a],ss                                ; 3369
        push word [bp+0x6]                              ; 336C
        lea ax,[bp-0x50]                                ; 336F
        push ss                                         ; 3372
        push ax                                         ; 3373
        sub_ ax,ax                                      ; 3374
        push ax                                         ; 3376
        push ax                                         ; 3377
        callf mxd_set_control_details, R3_337B, R3_349D ; 3378 far seg5
        mov word [bp-0x78],0x1                          ; 337D
        mov word [bp-0x76],0x0                          ; 3382
        mov word [bp-0x14],0x2                          ; 3387
        mov word [bp-0x12],0x0                          ; 338C
        mov ax,[bp-0xe]                                 ; 3391
        or ax,[bp-0x10]                                 ; 3394
        jz short L3_33C9                                ; 3397
        mov word [bp-0xc],0x4                           ; 3399
        mov word [bp-0xa],0x0                           ; 339E
        push word [bp-0xe]                              ; 33A3
        push word [bp-0x10]                             ; 33A6
        mov ax,0x483                                    ; 33A9
        push cs                                         ; 33AC
        push ax                                         ; 33AD
        sub_ ax,ax                                      ; 33AE
        push ax                                         ; 33B0
        push ax                                         ; 33B1
        push ax                                         ; 33B2
        push ax                                         ; 33B3
        lea ax,[bp-0x78]                                ; 33B4
        push ss                                         ; 33B7
        push ax                                         ; 33B8
        lea ax,[bp-0xc]                                 ; 33B9
        push ss                                         ; 33BC
        push ax                                         ; 33BD
        callp R3_33BF, R3_343D, 0x0000                  ; 33BE KERNEL.RegQueryValueEx
        mov [bp-0x14],ax                                ; 33C3
        mov [bp-0x12],dx                                ; 33C6

L3_33C9:
        mov ax,[bp-0x12]                                ; 33C9
        or ax,[bp-0x14]                                 ; 33CC
        jz short L3_33E3                                ; 33CF
        mov word [bp-0x78],0x1                          ; 33D1
        mov word [bp-0x76],0x0                          ; 33D6
        mov ax,[bp-0xe]                                 ; 33DB
        or ax,[bp-0x10]                                 ; 33DE
        jnz short L3_33E6                               ; 33E1

L3_33E3:
        jmp near L3_34D9                                ; 33E3

L3_33E6:
        mov word [bp-0xc],0x4                           ; 33E6
        mov word [bp-0xa],0x0                           ; 33EB
        push word [bp-0xe]                              ; 33F0
        push word [bp-0x10]                             ; 33F3
        mov ax,0x406                                    ; 33F6
        jmp near L3_34C2                                ; 33F9

L3_33FC:
        test byte [bx+0x2a],0x20                        ; 33FC
        jnz short L3_3405                               ; 3400
        jmp near L3_3527                                ; 3402

L3_3405:
        mov word [bp-0x24],0xbfff                       ; 3405
        mov word [bp-0x22],0xbfff                       ; 340A
        mov ax,[bp-0xe]                                 ; 340F
        or ax,[bp-0x10]                                 ; 3412
        jz short L3_3441                                ; 3415
        mov word [bp-0xc],0x4                           ; 3417
        mov word [bp-0xa],0x0                           ; 341C
        push word [bp-0xe]                              ; 3421
        push word [bp-0x10]                             ; 3424
        mov ax,0x494                                    ; 3427
        push cs                                         ; 342A
        push ax                                         ; 342B
        sub_ ax,ax                                      ; 342C
        push ax                                         ; 342E
        push ax                                         ; 342F
        push ax                                         ; 3430
        push ax                                         ; 3431
        lea ax,[bp-0x24]                                ; 3432
        push ss                                         ; 3435
        push ax                                         ; 3436
        lea ax,[bp-0xc]                                 ; 3437
        push ss                                         ; 343A
        push ax                                         ; 343B
        callp R3_343D, R3_34D5, 0x0000                  ; 343C KERNEL.RegQueryValueEx

L3_3441:
        mov ax,[bp-0x22]                                ; 3441
        mov [bp-0x20],ax                                ; 3444
        mov word [bp-0x1e],0x0                          ; 3447
        mov word [bp-0x22],0x0                          ; 344C
        mov word [bp-0x4c],0x2a                         ; 3451
        mov word [bp-0x4a],0x0                          ; 3456
        mov word [bp-0x50],0x18                         ; 345B
        mov word [bp-0x4e],0x0                          ; 3460
        les bx,[bp-0x8]                                 ; 3465
        mov ax,[es:bx+0x266a]                           ; 3468
        mov [bp-0x48],ax                                ; 346D
        mov word [bp-0x46],0x0                          ; 3470
        sub_ ax,ax                                      ; 3475
        mov [bp-0x42],ax                                ; 3477
        mov [bp-0x44],ax                                ; 347A
        mov word [bp-0x40],0x4                          ; 347D
        mov [bp-0x3e],ax                                ; 3482
        lea ax,[bp-0x24]                                ; 3485
        mov [bp-0x3c],ax                                ; 3488
        mov [bp-0x3a],ss                                ; 348B
        push word [bp+0x6]                              ; 348E
        lea ax,[bp-0x50]                                ; 3491
        push ss                                         ; 3494
        push ax                                         ; 3495
        sub_ ax,ax                                      ; 3496
        push ax                                         ; 3498
        push ax                                         ; 3499
        callf mxd_set_control_details, R3_349D, R3_3525 ; 349A far seg5
        sub_ ax,ax                                      ; 349F
        mov [bp-0x76],ax                                ; 34A1
        mov [bp-0x78],ax                                ; 34A4
        mov ax,[bp-0xe]                                 ; 34A7
        or ax,[bp-0x10]                                 ; 34AA
        jz short L3_34D9                                ; 34AD
        mov word [bp-0xc],0x4                           ; 34AF
        mov word [bp-0xa],0x0                           ; 34B4
        push word [bp-0xe]                              ; 34B9
        push word [bp-0x10]                             ; 34BC
        mov ax,0x483                                    ; 34BF

L3_34C2:
        push cs                                         ; 34C2
        push ax                                         ; 34C3
        sub_ ax,ax                                      ; 34C4
        push ax                                         ; 34C6
        push ax                                         ; 34C7
        push ax                                         ; 34C8
        push ax                                         ; 34C9
        lea ax,[bp-0x78]                                ; 34CA
        push ss                                         ; 34CD
        push ax                                         ; 34CE
        lea ax,[bp-0xc]                                 ; 34CF
        push ss                                         ; 34D2
        push ax                                         ; 34D3
        callp R3_34D5, R3_355D, 0x0000                  ; 34D4 KERNEL.RegQueryValueEx

L3_34D9:
        mov word [bp-0x4c],0x29                         ; 34D9
        mov word [bp-0x4a],0x0                          ; 34DE
        mov word [bp-0x50],0x18                         ; 34E3
        mov word [bp-0x4e],0x0                          ; 34E8
        les bx,[bp-0x8]                                 ; 34ED
        mov ax,[es:bx+0x2664]                           ; 34F0
        mov [bp-0x48],ax                                ; 34F5
        mov word [bp-0x46],0x0                          ; 34F8
        sub_ ax,ax                                      ; 34FD
        mov [bp-0x42],ax                                ; 34FF
        mov [bp-0x44],ax                                ; 3502
        mov word [bp-0x40],0x4                          ; 3505
        mov [bp-0x3e],ax                                ; 350A
        lea ax,[bp-0x78]                                ; 350D

L3_3510:
        mov [bp-0x3c],ax                                ; 3510
        mov [bp-0x3a],ss                                ; 3513
        push word [bp+0x6]                              ; 3516
        lea ax,[bp-0x50]                                ; 3519
        push ss                                         ; 351C
        push ax                                         ; 351D
        sub_ ax,ax                                      ; 351E
        push ax                                         ; 3520
        push ax                                         ; 3521
        callf mxd_set_control_details, R3_3525, R3_2EC2 ; 3522 far seg5

L3_3527:
        sub_ ax,ax                                      ; 3527
        mov [bp-0xa],ax                                 ; 3529
        mov [bp-0xc],ax                                 ; 352C
        mov ax,[bp-0xe]                                 ; 352F
        or ax,[bp-0x10]                                 ; 3532
        jz short L3_356D                                ; 3535
        mov word [bp-0xc],0x10                          ; 3537
        mov word [bp-0xa],0x0                           ; 353C
        push word [bp-0xe]                              ; 3541
        push word [bp-0x10]                             ; 3544
        mov ax,0x5ed                                    ; 3547
        push cs                                         ; 354A
        push ax                                         ; 354B
        sub_ ax,ax                                      ; 354C
        push ax                                         ; 354E
        push ax                                         ; 354F
        push ax                                         ; 3550
        push ax                                         ; 3551
        lea ax,[bp-0x38]                                ; 3552
        push ss                                         ; 3555
        push ax                                         ; 3556
        lea ax,[bp-0xc]                                 ; 3557
        push ss                                         ; 355A
        push ax                                         ; 355B
        callp R3_355D, R3_35BA, 0x0000                  ; 355C KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 3561
        jz short L3_356D                                ; 3563
        sub_ ax,ax                                      ; 3565
        mov [bp-0xa],ax                                 ; 3567
        mov [bp-0xc],ax                                 ; 356A

L3_356D:
        cmp word [bp-0xc],byte +0x10                    ; 356D
        jnz short L3_358C                               ; 3571
        cmp word [bp-0xa],byte +0x0                     ; 3573
        jnz short L3_358C                               ; 3577
        mov ax,0x990                                    ; 3579
        mov cx,0x8                                      ; 357C
        push ds                                         ; 357F
        mov_ di,ax                                      ; 3580
        lea si,[bp-0x38]                                ; 3582
        push ds                                         ; 3585
        pop es                                          ; 3586
        push ss                                         ; 3587
        pop ds                                          ; 3588
        rep movsw                                       ; 3589
        pop ds                                          ; 358B

L3_358C:
        mov ax,[bp-0xe]                                 ; 358C
        or ax,[bp-0x10]                                 ; 358F
        jz short L3_35CA                                ; 3592
        mov word [bp-0xc],0x10                          ; 3594
        mov word [bp-0xa],0x0                           ; 3599
        push word [bp-0xe]                              ; 359E
        push word [bp-0x10]                             ; 35A1
        mov ax,0x5cb                                    ; 35A4
        push cs                                         ; 35A7
        push ax                                         ; 35A8
        sub_ ax,ax                                      ; 35A9
        push ax                                         ; 35AB
        push ax                                         ; 35AC
        push ax                                         ; 35AD
        push ax                                         ; 35AE
        lea ax,[bp-0x38]                                ; 35AF
        push ss                                         ; 35B2
        push ax                                         ; 35B3
        lea ax,[bp-0xc]                                 ; 35B4
        push ss                                         ; 35B7
        push ax                                         ; 35B8
        callp R3_35BA, R3_2F01, 0x0000                  ; 35B9 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 35BE
        jz short L3_35CA                                ; 35C0
        sub_ ax,ax                                      ; 35C2
        mov [bp-0xa],ax                                 ; 35C4
        mov [bp-0xc],ax                                 ; 35C7

L3_35CA:
        cmp word [bp-0xc],byte +0x10                    ; 35CA
        jnz short L3_35E9                               ; 35CE
        cmp word [bp-0xa],byte +0x0                     ; 35D0
        jnz short L3_35E9                               ; 35D4
        mov ax,0x9a0                                    ; 35D6
        mov cx,0x8                                      ; 35D9
        push ds                                         ; 35DC
        mov_ di,ax                                      ; 35DD
        lea si,[bp-0x38]                                ; 35DF
        push ds                                         ; 35E2
        pop es                                          ; 35E3
        push ss                                         ; 35E4
        pop ds                                          ; 35E5
        rep movsw                                       ; 35E6
        pop ds                                          ; 35E8

L3_35E9:
        mov ax,[bp-0xe]                                 ; 35E9
        or ax,[bp-0x10]                                 ; 35EC
        jz short L3_3627                                ; 35EF
        mov word [bp-0xc],0x10                          ; 35F1
        mov word [bp-0xa],0x0                           ; 35F6
        push word [bp-0xe]                              ; 35FB
        push word [bp-0x10]                             ; 35FE
        mov ax,0x5ac                                    ; 3601
        push cs                                         ; 3604
        push ax                                         ; 3605
        sub_ ax,ax                                      ; 3606
        push ax                                         ; 3608
        push ax                                         ; 3609
        push ax                                         ; 360A
        push ax                                         ; 360B
        lea ax,[bp-0x38]                                ; 360C
        push ss                                         ; 360F
        push ax                                         ; 3610
        lea ax,[bp-0xc]                                 ; 3611
        push ss                                         ; 3614
        push ax                                         ; 3615
        callp R3_3617, R3_3674, 0x0000                  ; 3616 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 361B
        jz short L3_3627                                ; 361D
        sub_ ax,ax                                      ; 361F
        mov [bp-0xa],ax                                 ; 3621
        mov [bp-0xc],ax                                 ; 3624

L3_3627:
        cmp word [bp-0xc],byte +0x10                    ; 3627
        jnz short L3_3646                               ; 362B
        cmp word [bp-0xa],byte +0x0                     ; 362D
        jnz short L3_3646                               ; 3631
        mov ax,0x9b0                                    ; 3633
        mov cx,0x8                                      ; 3636
        push ds                                         ; 3639
        mov_ di,ax                                      ; 363A
        lea si,[bp-0x38]                                ; 363C
        push ds                                         ; 363F
        pop es                                          ; 3640
        push ss                                         ; 3641
        pop ds                                          ; 3642
        rep movsw                                       ; 3643
        pop ds                                          ; 3645

L3_3646:
        mov ax,[bp-0xe]                                 ; 3646
        or ax,[bp-0x10]                                 ; 3649
        jz short L3_3684                                ; 364C
        mov word [bp-0xc],0x10                          ; 364E
        mov word [bp-0xa],0x0                           ; 3653
        push word [bp-0xe]                              ; 3658
        push word [bp-0x10]                             ; 365B
        mov ax,0x577                                    ; 365E
        push cs                                         ; 3661
        push ax                                         ; 3662
        sub_ ax,ax                                      ; 3663
        push ax                                         ; 3665
        push ax                                         ; 3666
        push ax                                         ; 3667
        push ax                                         ; 3668
        lea ax,[bp-0x38]                                ; 3669
        push ss                                         ; 366C
        push ax                                         ; 366D
        lea ax,[bp-0xc]                                 ; 366E
        push ss                                         ; 3671
        push ax                                         ; 3672
        callp R3_3674, R3_36D1, 0x0000                  ; 3673 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 3678
        jz short L3_3684                                ; 367A
        sub_ ax,ax                                      ; 367C
        mov [bp-0xa],ax                                 ; 367E
        mov [bp-0xc],ax                                 ; 3681

L3_3684:
        cmp word [bp-0xc],byte +0x10                    ; 3684
        jnz short L3_36A3                               ; 3688
        cmp word [bp-0xa],byte +0x0                     ; 368A
        jnz short L3_36A3                               ; 368E
        mov ax,0x9c0                                    ; 3690
        mov cx,0x8                                      ; 3693
        push ds                                         ; 3696
        mov_ di,ax                                      ; 3697
        lea si,[bp-0x38]                                ; 3699
        push ds                                         ; 369C
        pop es                                          ; 369D
        push ss                                         ; 369E
        pop ds                                          ; 369F
        rep movsw                                       ; 36A0
        pop ds                                          ; 36A2

L3_36A3:
        mov ax,[bp-0xe]                                 ; 36A3
        or ax,[bp-0x10]                                 ; 36A6
        jz short L3_36E1                                ; 36A9
        mov word [bp-0xc],0x10                          ; 36AB
        mov word [bp-0xa],0x0                           ; 36B0
        push word [bp-0xe]                              ; 36B5
        push word [bp-0x10]                             ; 36B8
        mov ax,0x555                                    ; 36BB
        push cs                                         ; 36BE
        push ax                                         ; 36BF
        sub_ ax,ax                                      ; 36C0
        push ax                                         ; 36C2
        push ax                                         ; 36C3
        push ax                                         ; 36C4
        push ax                                         ; 36C5
        lea ax,[bp-0x38]                                ; 36C6
        push ss                                         ; 36C9
        push ax                                         ; 36CA
        lea ax,[bp-0xc]                                 ; 36CB
        push ss                                         ; 36CE
        push ax                                         ; 36CF
        callp R3_36D1, R3_372E, 0x0000                  ; 36D0 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 36D5
        jz short L3_36E1                                ; 36D7
        sub_ ax,ax                                      ; 36D9
        mov [bp-0xa],ax                                 ; 36DB
        mov [bp-0xc],ax                                 ; 36DE

L3_36E1:
        cmp word [bp-0xc],byte +0x10                    ; 36E1
        jnz short L3_3700                               ; 36E5
        cmp word [bp-0xa],byte +0x0                     ; 36E7
        jnz short L3_3700                               ; 36EB
        mov ax,0x9d0                                    ; 36ED
        mov cx,0x8                                      ; 36F0
        push ds                                         ; 36F3
        mov_ di,ax                                      ; 36F4
        lea si,[bp-0x38]                                ; 36F6
        push ds                                         ; 36F9
        pop es                                          ; 36FA
        push ss                                         ; 36FB
        pop ds                                          ; 36FC
        rep movsw                                       ; 36FD
        pop ds                                          ; 36FF

L3_3700:
        mov ax,[bp-0xe]                                 ; 3700
        or ax,[bp-0x10]                                 ; 3703
        jz short L3_373E                                ; 3706
        mov word [bp-0xc],0x10                          ; 3708
        mov word [bp-0xa],0x0                           ; 370D
        push word [bp-0xe]                              ; 3712
        push word [bp-0x10]                             ; 3715
        mov ax,0x526                                    ; 3718
        push cs                                         ; 371B
        push ax                                         ; 371C
        sub_ ax,ax                                      ; 371D
        push ax                                         ; 371F
        push ax                                         ; 3720
        push ax                                         ; 3721
        push ax                                         ; 3722
        lea ax,[bp-0x38]                                ; 3723
        push ss                                         ; 3726
        push ax                                         ; 3727
        lea ax,[bp-0xc]                                 ; 3728
        push ss                                         ; 372B
        push ax                                         ; 372C
        callp R3_372E, R3_378B, 0x0000                  ; 372D KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 3732
        jz short L3_373E                                ; 3734
        sub_ ax,ax                                      ; 3736
        mov [bp-0xa],ax                                 ; 3738
        mov [bp-0xc],ax                                 ; 373B

L3_373E:
        cmp word [bp-0xc],byte +0x10                    ; 373E
        jnz short L3_375D                               ; 3742
        cmp word [bp-0xa],byte +0x0                     ; 3744
        jnz short L3_375D                               ; 3748
        mov ax,0x9e0                                    ; 374A
        mov cx,0x8                                      ; 374D
        push ds                                         ; 3750
        mov_ di,ax                                      ; 3751
        lea si,[bp-0x38]                                ; 3753
        push ds                                         ; 3756
        pop es                                          ; 3757
        push ss                                         ; 3758
        pop ds                                          ; 3759
        rep movsw                                       ; 375A
        pop ds                                          ; 375C

L3_375D:
        mov ax,[bp-0xe]                                 ; 375D
        or ax,[bp-0x10]                                 ; 3760
        jz short L3_379B                                ; 3763
        mov word [bp-0xc],0x10                          ; 3765
        mov word [bp-0xa],0x0                           ; 376A
        push word [bp-0xe]                              ; 376F
        push word [bp-0x10]                             ; 3772
        mov ax,0x503                                    ; 3775
        push cs                                         ; 3778
        push ax                                         ; 3779
        sub_ ax,ax                                      ; 377A
        push ax                                         ; 377C
        push ax                                         ; 377D
        push ax                                         ; 377E
        push ax                                         ; 377F
        lea ax,[bp-0x38]                                ; 3780
        push ss                                         ; 3783
        push ax                                         ; 3784
        lea ax,[bp-0xc]                                 ; 3785
        push ss                                         ; 3788
        push ax                                         ; 3789
        callp R3_378B, R3_37E8, 0x0000                  ; 378A KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 378F
        jz short L3_379B                                ; 3791
        sub_ ax,ax                                      ; 3793
        mov [bp-0xa],ax                                 ; 3795
        mov [bp-0xc],ax                                 ; 3798

L3_379B:
        cmp word [bp-0xc],byte +0x10                    ; 379B
        jnz short L3_37BA                               ; 379F
        cmp word [bp-0xa],byte +0x0                     ; 37A1
        jnz short L3_37BA                               ; 37A5
        mov ax,0x9f0                                    ; 37A7
        mov cx,0x8                                      ; 37AA
        push ds                                         ; 37AD
        mov_ di,ax                                      ; 37AE
        lea si,[bp-0x38]                                ; 37B0
        push ds                                         ; 37B3
        pop es                                          ; 37B4
        push ss                                         ; 37B5
        pop ds                                          ; 37B6
        rep movsw                                       ; 37B7
        pop ds                                          ; 37B9

L3_37BA:
        mov ax,[bp-0xe]                                 ; 37BA
        or ax,[bp-0x10]                                 ; 37BD
        jz short L3_37F8                                ; 37C0
        mov word [bp-0xc],0x10                          ; 37C2
        mov word [bp-0xa],0x0                           ; 37C7
        push word [bp-0xe]                              ; 37CC
        push word [bp-0x10]                             ; 37CF
        mov ax,0x4d3                                    ; 37D2
        push cs                                         ; 37D5
        push ax                                         ; 37D6
        sub_ ax,ax                                      ; 37D7
        push ax                                         ; 37D9
        push ax                                         ; 37DA
        push ax                                         ; 37DB
        push ax                                         ; 37DC
        lea ax,[bp-0x38]                                ; 37DD
        push ss                                         ; 37E0
        push ax                                         ; 37E1
        lea ax,[bp-0xc]                                 ; 37E2
        push ss                                         ; 37E5
        push ax                                         ; 37E6
        callp R3_37E8, R3_3845, 0x0000                  ; 37E7 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 37EC
        jz short L3_37F8                                ; 37EE
        sub_ ax,ax                                      ; 37F0
        mov [bp-0xa],ax                                 ; 37F2
        mov [bp-0xc],ax                                 ; 37F5

L3_37F8:
        cmp word [bp-0xc],byte +0x10                    ; 37F8
        jnz short L3_3817                               ; 37FC
        cmp word [bp-0xa],byte +0x0                     ; 37FE
        jnz short L3_3817                               ; 3802
        mov ax,0xa00                                    ; 3804
        mov cx,0x8                                      ; 3807
        push ds                                         ; 380A
        mov_ di,ax                                      ; 380B
        lea si,[bp-0x38]                                ; 380D
        push ds                                         ; 3810
        pop es                                          ; 3811
        push ss                                         ; 3812
        pop ds                                          ; 3813
        rep movsw                                       ; 3814
        pop ds                                          ; 3816

L3_3817:
        mov ax,[bp-0xe]                                 ; 3817
        or ax,[bp-0x10]                                 ; 381A
        jz short L3_3855                                ; 381D
        mov word [bp-0xc],0x10                          ; 381F
        mov word [bp-0xa],0x0                           ; 3824
        push word [bp-0xe]                              ; 3829
        push word [bp-0x10]                             ; 382C
        mov ax,0x49e                                    ; 382F
        push cs                                         ; 3832
        push ax                                         ; 3833
        sub_ ax,ax                                      ; 3834
        push ax                                         ; 3836
        push ax                                         ; 3837
        push ax                                         ; 3838
        push ax                                         ; 3839
        lea ax,[bp-0x38]                                ; 383A
        push ss                                         ; 383D
        push ax                                         ; 383E
        lea ax,[bp-0xc]                                 ; 383F
        push ss                                         ; 3842
        push ax                                         ; 3843
        callp R3_3845, R3_38A2, 0x0000                  ; 3844 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 3849
        jz short L3_3855                                ; 384B
        sub_ ax,ax                                      ; 384D
        mov [bp-0xa],ax                                 ; 384F
        mov [bp-0xc],ax                                 ; 3852

L3_3855:
        cmp word [bp-0xc],byte +0x10                    ; 3855
        jnz short L3_3874                               ; 3859
        cmp word [bp-0xa],byte +0x0                     ; 385B
        jnz short L3_3874                               ; 385F
        mov ax,0xa10                                    ; 3861
        mov cx,0x8                                      ; 3864
        push ds                                         ; 3867
        mov_ di,ax                                      ; 3868
        lea si,[bp-0x38]                                ; 386A
        push ds                                         ; 386D
        pop es                                          ; 386E
        push ss                                         ; 386F
        pop ds                                          ; 3870
        rep movsw                                       ; 3871
        pop ds                                          ; 3873

L3_3874:
        mov ax,[bp-0xe]                                 ; 3874
        or ax,[bp-0x10]                                 ; 3877
        jz short L3_38B2                                ; 387A
        mov word [bp-0xc],0x10                          ; 387C
        mov word [bp-0xa],0x0                           ; 3881
        push word [bp-0xe]                              ; 3886
        push word [bp-0x10]                             ; 3889
        mov ax,0x462                                    ; 388C
        push cs                                         ; 388F
        push ax                                         ; 3890
        sub_ ax,ax                                      ; 3891
        push ax                                         ; 3893
        push ax                                         ; 3894
        push ax                                         ; 3895
        push ax                                         ; 3896
        lea ax,[bp-0x38]                                ; 3897
        push ss                                         ; 389A
        push ax                                         ; 389B
        lea ax,[bp-0xc]                                 ; 389C
        push ss                                         ; 389F
        push ax                                         ; 38A0
        callp R3_38A2, R3_38FF, 0x0000                  ; 38A1 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 38A6
        jz short L3_38B2                                ; 38A8
        sub_ ax,ax                                      ; 38AA
        mov [bp-0xa],ax                                 ; 38AC
        mov [bp-0xc],ax                                 ; 38AF

L3_38B2:
        cmp word [bp-0xc],byte +0x10                    ; 38B2
        jnz short L3_38D1                               ; 38B6
        cmp word [bp-0xa],byte +0x0                     ; 38B8
        jnz short L3_38D1                               ; 38BC
        mov ax,0xa20                                    ; 38BE
        mov cx,0x8                                      ; 38C1
        push ds                                         ; 38C4
        mov_ di,ax                                      ; 38C5
        lea si,[bp-0x38]                                ; 38C7
        push ds                                         ; 38CA
        pop es                                          ; 38CB
        push ss                                         ; 38CC
        pop ds                                          ; 38CD
        rep movsw                                       ; 38CE
        pop ds                                          ; 38D0

L3_38D1:
        mov ax,[bp-0xe]                                 ; 38D1
        or ax,[bp-0x10]                                 ; 38D4
        jz short L3_390F                                ; 38D7
        mov word [bp-0xc],0x10                          ; 38D9
        mov word [bp-0xa],0x0                           ; 38DE
        push word [bp-0xe]                              ; 38E3
        push word [bp-0x10]                             ; 38E6
        mov ax,0x42c                                    ; 38E9
        push cs                                         ; 38EC
        push ax                                         ; 38ED
        sub_ ax,ax                                      ; 38EE
        push ax                                         ; 38F0
        push ax                                         ; 38F1
        push ax                                         ; 38F2
        push ax                                         ; 38F3
        lea ax,[bp-0x38]                                ; 38F4
        push ss                                         ; 38F7
        push ax                                         ; 38F8
        lea ax,[bp-0xc]                                 ; 38F9
        push ss                                         ; 38FC
        push ax                                         ; 38FD
        callp R3_38FF, R3_395C, 0x0000                  ; 38FE KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 3903
        jz short L3_390F                                ; 3905
        sub_ ax,ax                                      ; 3907
        mov [bp-0xa],ax                                 ; 3909
        mov [bp-0xc],ax                                 ; 390C

L3_390F:
        cmp word [bp-0xc],byte +0x10                    ; 390F
        jnz short L3_392E                               ; 3913
        cmp word [bp-0xa],byte +0x0                     ; 3915
        jnz short L3_392E                               ; 3919
        mov ax,0xa30                                    ; 391B
        mov cx,0x8                                      ; 391E
        push ds                                         ; 3921
        mov_ di,ax                                      ; 3922
        lea si,[bp-0x38]                                ; 3924
        push ds                                         ; 3927
        pop es                                          ; 3928
        push ss                                         ; 3929
        pop ds                                          ; 392A
        rep movsw                                       ; 392B
        pop ds                                          ; 392D

L3_392E:
        mov ax,[bp-0xe]                                 ; 392E
        or ax,[bp-0x10]                                 ; 3931
        jz short L3_396C                                ; 3934
        mov word [bp-0xc],0x10                          ; 3936
        mov word [bp-0xa],0x0                           ; 393B
        push word [bp-0xe]                              ; 3940
        push word [bp-0x10]                             ; 3943
        mov ax,0x3f4                                    ; 3946
        push cs                                         ; 3949
        push ax                                         ; 394A
        sub_ ax,ax                                      ; 394B
        push ax                                         ; 394D
        push ax                                         ; 394E
        push ax                                         ; 394F
        push ax                                         ; 3950
        lea ax,[bp-0x38]                                ; 3951
        push ss                                         ; 3954
        push ax                                         ; 3955
        lea ax,[bp-0xc]                                 ; 3956
        push ss                                         ; 3959
        push ax                                         ; 395A
        callp R3_395C, R3_39B9, 0x0000                  ; 395B KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 3960
        jz short L3_396C                                ; 3962
        sub_ ax,ax                                      ; 3964
        mov [bp-0xa],ax                                 ; 3966
        mov [bp-0xc],ax                                 ; 3969

L3_396C:
        cmp word [bp-0xc],byte +0x10                    ; 396C
        jnz short L3_398B                               ; 3970
        cmp word [bp-0xa],byte +0x0                     ; 3972
        jnz short L3_398B                               ; 3976
        mov ax,0xa40                                    ; 3978
        mov cx,0x8                                      ; 397B
        push ds                                         ; 397E
        mov_ di,ax                                      ; 397F
        lea si,[bp-0x38]                                ; 3981
        push ds                                         ; 3984
        pop es                                          ; 3985
        push ss                                         ; 3986
        pop ds                                          ; 3987
        rep movsw                                       ; 3988
        pop ds                                          ; 398A

L3_398B:
        mov ax,[bp-0xe]                                 ; 398B
        or ax,[bp-0x10]                                 ; 398E
        jz short L3_39C9                                ; 3991
        mov word [bp-0xc],0x10                          ; 3993
        mov word [bp-0xa],0x0                           ; 3998
        push word [bp-0xe]                              ; 399D
        push word [bp-0x10]                             ; 39A0
        mov ax,0x3c5                                    ; 39A3
        push cs                                         ; 39A6
        push ax                                         ; 39A7
        sub_ ax,ax                                      ; 39A8
        push ax                                         ; 39AA
        push ax                                         ; 39AB
        push ax                                         ; 39AC
        push ax                                         ; 39AD
        lea ax,[bp-0x38]                                ; 39AE
        push ss                                         ; 39B1
        push ax                                         ; 39B2
        lea ax,[bp-0xc]                                 ; 39B3
        push ss                                         ; 39B6
        push ax                                         ; 39B7
        callp R3_39B9, R3_32D1, 0x0000                  ; 39B8 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 39BD
        jz short L3_39C9                                ; 39BF
        sub_ ax,ax                                      ; 39C1
        mov [bp-0xa],ax                                 ; 39C3
        mov [bp-0xc],ax                                 ; 39C6

L3_39C9:
        cmp word [bp-0xc],byte +0x10                    ; 39C9
        jnz short L3_39E8                               ; 39CD
        cmp word [bp-0xa],byte +0x0                     ; 39CF
        jnz short L3_39E8                               ; 39D3
        mov ax,0xa50                                    ; 39D5
        mov cx,0x8                                      ; 39D8
        push ds                                         ; 39DB
        mov_ di,ax                                      ; 39DC
        lea si,[bp-0x38]                                ; 39DE
        push ds                                         ; 39E1
        pop es                                          ; 39E2
        push ss                                         ; 39E3
        pop ds                                          ; 39E4
        rep movsw                                       ; 39E5
        pop ds                                          ; 39E7

L3_39E8:
        mov ax,[bp-0xe]                                 ; 39E8
        or ax,[bp-0x10]                                 ; 39EB
        jz short L3_3A26                                ; 39EE
        mov word [bp-0xc],0x10                          ; 39F0
        mov word [bp-0xa],0x0                           ; 39F5
        push word [bp-0xe]                              ; 39FA
        push word [bp-0x10]                             ; 39FD
        mov ax,0x9a7                                    ; 3A00
        push cs                                         ; 3A03
        push ax                                         ; 3A04
        sub_ ax,ax                                      ; 3A05
        push ax                                         ; 3A07
        push ax                                         ; 3A08
        push ax                                         ; 3A09
        push ax                                         ; 3A0A
        lea ax,[bp-0x38]                                ; 3A0B
        push ss                                         ; 3A0E
        push ax                                         ; 3A0F
        lea ax,[bp-0xc]                                 ; 3A10
        push ss                                         ; 3A13
        push ax                                         ; 3A14
        callp R3_3A16, R3_3AFE, 0x0000                  ; 3A15 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 3A1A
        jz short L3_3A26                                ; 3A1C
        sub_ ax,ax                                      ; 3A1E
        mov [bp-0xa],ax                                 ; 3A20
        mov [bp-0xc],ax                                 ; 3A23

L3_3A26:
        cmp word [bp-0xc],byte +0x10                    ; 3A26
        jnz short L3_3A45                               ; 3A2A
        cmp word [bp-0xa],byte +0x0                     ; 3A2C
        jnz short L3_3A45                               ; 3A30
        mov ax,0xa60                                    ; 3A32
        mov cx,0x8                                      ; 3A35
        push ds                                         ; 3A38
        mov_ di,ax                                      ; 3A39
        lea si,[bp-0x38]                                ; 3A3B
        push ds                                         ; 3A3E
        pop es                                          ; 3A3F
        push ss                                         ; 3A40
        pop ds                                          ; 3A41
        rep movsw                                       ; 3A42
        pop ds                                          ; 3A44

L3_3A45:
        mov ax,[bp-0xe]                                 ; 3A45
        or ax,[bp-0x10]                                 ; 3A48
        jz short L3_3A58                                ; 3A4B
        push word [bp-0xe]                              ; 3A4D
        push word [bp-0x10]                             ; 3A50
        callp R3_3A54, R3_18DB, 0x0000                  ; 3A53 KERNEL.RegCloseKey

L3_3A58:
        lea ax,[bp-0x178]                               ; 3A58
        push ss                                         ; 3A5C
        push ax                                         ; 3A5D
        mov ax,0x67d                                    ; 3A5E
        push cs                                         ; 3A61
        push ax                                         ; 3A62
        callp R3_3A64, R3_1930, 0x0000                  ; 3A63 KERNEL.lstrcat
        mov ax,0x2                                      ; 3A68
        mov dx,0x8000                                   ; 3A6B
        push dx                                         ; 3A6E
        push ax                                         ; 3A6F
        lea ax,[bp-0x178]                               ; 3A70
        push ss                                         ; 3A74
        push ax                                         ; 3A75
        lea ax,[bp-0x10]                                ; 3A76
        push ss                                         ; 3A79
        push ax                                         ; 3A7A
        callp R3_3A7C, R3_1948, 0x0000                  ; 3A7B KERNEL.RegOpenKey
        or_ dx,ax                                       ; 3A80
        jz short L3_3A8C                                ; 3A82
        sub_ ax,ax                                      ; 3A84
        mov [bp-0xe],ax                                 ; 3A86
        mov [bp-0x10],ax                                ; 3A89

L3_3A8C:
        mov bx,[bp+0x6]                                 ; 3A8C
        push word [bx+0x16]                             ; 3A8F
        push word [bx+0x14]                             ; 3A92
        mov ax,0x1                                      ; 3A95
        cwd                                             ; 3A98
        push dx                                         ; 3A99
        push ax                                         ; 3A9A
        lea ax,[bp-0x14]                                ; 3A9B
        push ss                                         ; 3A9E
        push ax                                         ; 3A9F
        callf vxd_0005, R3_3AA3, R3_3C43                ; 3AA0 far seg4
        or_ ax,ax                                       ; 3AA5
        jz short L3_3AB3                                ; 3AA7
        mov word [bp-0x14],0xfffe                       ; 3AA9
        mov word [bp-0x12],0xffff                       ; 3AAE

L3_3AB3:
        mov al,[bp-0x14]                                ; 3AB3
        and ax,strict word 0x1                          ; 3AB6
        cmp ax,strict word 0x1                          ; 3AB9
        sbb_ ax,ax                                      ; 3ABC
        inc ax                                          ; 3ABE
        cwd                                             ; 3ABF
        mov [bp-0x78],ax                                ; 3AC0
        mov [bp-0x76],dx                                ; 3AC3
        mov word [bp-0x14],0x2                          ; 3AC6
        mov word [bp-0x12],0x0                          ; 3ACB
        mov ax,[bp-0xe]                                 ; 3AD0
        or ax,[bp-0x10]                                 ; 3AD3
        jz short L3_3B08                                ; 3AD6
        mov word [bp-0xc],0x4                           ; 3AD8
        mov word [bp-0xa],0x0                           ; 3ADD
        push word [bp-0xe]                              ; 3AE2
        push word [bp-0x10]                             ; 3AE5
        mov ax,0x3d6                                    ; 3AE8
        push cs                                         ; 3AEB
        push ax                                         ; 3AEC
        sub_ ax,ax                                      ; 3AED
        push ax                                         ; 3AEF
        push ax                                         ; 3AF0
        push ax                                         ; 3AF1
        push ax                                         ; 3AF2
        lea ax,[bp-0x78]                                ; 3AF3
        push ss                                         ; 3AF6
        push ax                                         ; 3AF7
        lea ax,[bp-0xc]                                 ; 3AF8
        push ss                                         ; 3AFB
        push ax                                         ; 3AFC
        callp R3_3AFE, R3_3BBB, 0x0000                  ; 3AFD KERNEL.RegQueryValueEx
        mov [bp-0x14],ax                                ; 3B02
        mov [bp-0x12],dx                                ; 3B05

L3_3B08:
        mov ax,[bp-0x12]                                ; 3B08
        or ax,[bp-0x14]                                 ; 3B0B
        jz short L3_3B19                                ; 3B0E
        mov bx,[bp+0x6]                                 ; 3B10
        test byte [bx+0x2b],0x1                         ; 3B13
        jz short L3_3B7B                                ; 3B17

L3_3B19:
        mov word [bp-0x4c],0x27                         ; 3B19
        mov word [bp-0x4a],0x0                          ; 3B1E
        mov word [bp-0x50],0x18                         ; 3B23
        mov word [bp-0x4e],0x0                          ; 3B28
        les bx,[bp-0x8]                                 ; 3B2D
        mov ax,[es:bx+0x2658]                           ; 3B30
        mov [bp-0x48],ax                                ; 3B35
        mov word [bp-0x46],0x0                          ; 3B38
        sub_ ax,ax                                      ; 3B3D
        mov [bp-0x42],ax                                ; 3B3F
        mov [bp-0x44],ax                                ; 3B42
        mov word [bp-0x40],0x4                          ; 3B45
        mov [bp-0x3e],ax                                ; 3B4A
        lea ax,[bp-0x78]                                ; 3B4D
        mov [bp-0x3c],ax                                ; 3B50
        mov [bp-0x3a],ss                                ; 3B53
        mov bx,[bp+0x6]                                 ; 3B56
        test byte [bx+0x2b],0x1                         ; 3B59
        jz short L3_3B70                                ; 3B5D
        push bx                                         ; 3B5F
        lea ax,[bp-0x50]                                ; 3B60
        push ss                                         ; 3B63
        push ax                                         ; 3B64
        sub_ ax,ax                                      ; 3B65
        push ax                                         ; 3B67
        push ax                                         ; 3B68
        callf mxd_set_control_details, R3_3B6C, R3_3B79 ; 3B69 far seg5
        jmp short L3_3B7B                               ; 3B6E

L3_3B70:
        push bx                                         ; 3B70
        lea ax,[bp-0x50]                                ; 3B71
        push ss                                         ; 3B74
        push ax                                         ; 3B75
        callf L5_39C0, R3_3B79, R3_3D0C                 ; 3B76 far seg5

L3_3B7B:
        mov bx,[bp+0x6]                                 ; 3B7B
        test byte [bx+0x2b],0x1                         ; 3B7E
        jz short L3_3B8C                                ; 3B82
        mov ax,[bp-0xe]                                 ; 3B84
        or ax,[bp-0x10]                                 ; 3B87
        jnz short L3_3B8F                               ; 3B8A

L3_3B8C:
        jmp near L3_3C2C                                ; 3B8C

L3_3B8F:
        mov word [bp-0xc],0x40                          ; 3B8F
        mov word [bp-0xa],0x0                           ; 3B94
        push word [bp-0xe]                              ; 3B99
        push word [bp-0x10]                             ; 3B9C
        mov ax,0x998                                    ; 3B9F
        push cs                                         ; 3BA2
        push ax                                         ; 3BA3
        sub_ ax,ax                                      ; 3BA4
        push ax                                         ; 3BA6
        push ax                                         ; 3BA7
        push ax                                         ; 3BA8
        push ax                                         ; 3BA9
        mov ax,[bp-0x8]                                 ; 3BAA
        mov dx,[bp-0x6]                                 ; 3BAD
        add ax,0x1fc6                                   ; 3BB0
        push dx                                         ; 3BB3
        push ax                                         ; 3BB4
        lea ax,[bp-0xc]                                 ; 3BB5
        push ss                                         ; 3BB8
        push ax                                         ; 3BB9
        callp R3_3BBB, R3_3BF8, 0x0000                  ; 3BBA KERNEL.RegQueryValueEx
        mov [bp-0x14],ax                                ; 3BBF
        mov [bp-0x12],dx                                ; 3BC2
        mov_ ax,dx                                      ; 3BC5
        or ax,[bp-0x14]                                 ; 3BC7
        jz short L3_3BFC                                ; 3BCA
        mov word [bp-0xc],0x40                          ; 3BCC
        mov word [bp-0xa],0x0                           ; 3BD1
        push word [bp-0xe]                              ; 3BD6
        push word [bp-0x10]                             ; 3BD9
        mov ax,0x97e                                    ; 3BDC
        push cs                                         ; 3BDF
        push ax                                         ; 3BE0
        sub_ ax,ax                                      ; 3BE1
        push ax                                         ; 3BE3
        push ax                                         ; 3BE4
        push ax                                         ; 3BE5
        push ax                                         ; 3BE6
        mov ax,[bp-0x8]                                 ; 3BE7
        mov dx,[bp-0x6]                                 ; 3BEA
        add ax,0x1fc6                                   ; 3BED
        push dx                                         ; 3BF0
        push ax                                         ; 3BF1
        lea ax,[bp-0xc]                                 ; 3BF2
        push ss                                         ; 3BF5
        push ax                                         ; 3BF6
        callp R3_3BF8, R3_3C28, 0x0000                  ; 3BF7 KERNEL.RegQueryValueEx

L3_3BFC:
        mov word [bp-0xc],0x10                          ; 3BFC
        mov word [bp-0xa],0x0                           ; 3C01
        push word [bp-0xe]                              ; 3C06
        push word [bp-0x10]                             ; 3C09
        mov ax,0x97e                                    ; 3C0C
        push cs                                         ; 3C0F
        push ax                                         ; 3C10
        sub_ ax,ax                                      ; 3C11
        push ax                                         ; 3C13
        push ax                                         ; 3C14
        push ax                                         ; 3C15
        push ax                                         ; 3C16
        mov ax,[bp-0x8]                                 ; 3C17
        mov dx,[bp-0x6]                                 ; 3C1A
        add ax,0x1fb6                                   ; 3C1D
        push dx                                         ; 3C20
        push ax                                         ; 3C21
        lea ax,[bp-0xc]                                 ; 3C22
        push ss                                         ; 3C25
        push ax                                         ; 3C26
        callp R3_3C28, R3_3C9E, 0x0000                  ; 3C27 KERNEL.RegQueryValueEx

L3_3C2C:
        mov bx,[bp+0x6]                                 ; 3C2C
        push word [bx+0x16]                             ; 3C2F
        push word [bx+0x14]                             ; 3C32
        mov ax,0x1                                      ; 3C35
        cwd                                             ; 3C38
        push dx                                         ; 3C39
        push ax                                         ; 3C3A
        lea ax,[bp-0x14]                                ; 3C3B
        push ss                                         ; 3C3E
        push ax                                         ; 3C3F
        callf vxd_0005, R3_3C43, 0xFFFF                 ; 3C40 far seg4
        or_ ax,ax                                       ; 3C45
        jz short L3_3C53                                ; 3C47
        mov word [bp-0x14],0x2                          ; 3C49
        mov word [bp-0x12],0x0                          ; 3C4E

L3_3C53:
        mov al,[bp-0x14]                                ; 3C53
        and ax,strict word 0x2                          ; 3C56
        cmp ax,strict word 0x1                          ; 3C59
        sbb_ ax,ax                                      ; 3C5C
        inc ax                                          ; 3C5E
        cwd                                             ; 3C5F
        mov [bp-0x78],ax                                ; 3C60
        mov [bp-0x76],dx                                ; 3C63
        mov word [bp-0x14],0x2                          ; 3C66
        mov word [bp-0x12],0x0                          ; 3C6B
        mov ax,[bp-0xe]                                 ; 3C70
        or ax,[bp-0x10]                                 ; 3C73
        jz short L3_3CA8                                ; 3C76
        mov word [bp-0xc],0x4                           ; 3C78
        mov word [bp-0xa],0x0                           ; 3C7D
        push word [bp-0xe]                              ; 3C82
        push word [bp-0x10]                             ; 3C85
        mov ax,0x952                                    ; 3C88
        push cs                                         ; 3C8B
        push ax                                         ; 3C8C
        sub_ ax,ax                                      ; 3C8D
        push ax                                         ; 3C8F
        push ax                                         ; 3C90
        push ax                                         ; 3C91
        push ax                                         ; 3C92
        lea ax,[bp-0x78]                                ; 3C93
        push ss                                         ; 3C96
        push ax                                         ; 3C97
        lea ax,[bp-0xc]                                 ; 3C98
        push ss                                         ; 3C9B
        push ax                                         ; 3C9C
        callp R3_3C9E, R3_3D5B, 0x0000                  ; 3C9D KERNEL.RegQueryValueEx
        mov [bp-0x14],ax                                ; 3CA2
        mov [bp-0x12],dx                                ; 3CA5

L3_3CA8:
        mov ax,[bp-0x12]                                ; 3CA8
        or ax,[bp-0x14]                                 ; 3CAB
        jz short L3_3CB9                                ; 3CAE
        mov bx,[bp+0x6]                                 ; 3CB0
        test byte [bx+0x2b],0x2                         ; 3CB3
        jz short L3_3D1B                                ; 3CB7

L3_3CB9:
        mov word [bp-0x4c],0x28                         ; 3CB9
        mov word [bp-0x4a],0x0                          ; 3CBE
        mov word [bp-0x50],0x18                         ; 3CC3
        mov word [bp-0x4e],0x0                          ; 3CC8
        les bx,[bp-0x8]                                 ; 3CCD
        mov ax,[es:bx+0x265e]                           ; 3CD0
        mov [bp-0x48],ax                                ; 3CD5
        mov word [bp-0x46],0x0                          ; 3CD8
        sub_ ax,ax                                      ; 3CDD
        mov [bp-0x42],ax                                ; 3CDF
        mov [bp-0x44],ax                                ; 3CE2
        mov word [bp-0x40],0x4                          ; 3CE5
        mov [bp-0x3e],ax                                ; 3CEA
        lea ax,[bp-0x78]                                ; 3CED
        mov [bp-0x3c],ax                                ; 3CF0
        mov [bp-0x3a],ss                                ; 3CF3
        mov bx,[bp+0x6]                                 ; 3CF6
        test byte [bx+0x2b],0x2                         ; 3CF9
        jz short L3_3D10                                ; 3CFD
        push bx                                         ; 3CFF
        lea ax,[bp-0x50]                                ; 3D00
        push ss                                         ; 3D03
        push ax                                         ; 3D04
        sub_ ax,ax                                      ; 3D05
        push ax                                         ; 3D07
        push ax                                         ; 3D08
        callf mxd_set_control_details, R3_3D0C, R3_3D19 ; 3D09 far seg5
        jmp short L3_3D1B                               ; 3D0E

L3_3D10:
        push bx                                         ; 3D10
        lea ax,[bp-0x50]                                ; 3D11
        push ss                                         ; 3D14
        push ax                                         ; 3D15
        callf L5_39C0, R3_3D19, R3_337B                 ; 3D16 far seg5

L3_3D1B:
        mov bx,[bp+0x6]                                 ; 3D1B
        test byte [bx+0x2b],0x2                         ; 3D1E
        jz short L3_3D2C                                ; 3D22
        mov ax,[bp-0xe]                                 ; 3D24
        or ax,[bp-0x10]                                 ; 3D27
        jnz short L3_3D2F                               ; 3D2A

L3_3D2C:
        jmp near L3_3DCC                                ; 3D2C

L3_3D2F:
        mov word [bp-0xc],0x40                          ; 3D2F
        mov word [bp-0xa],0x0                           ; 3D34
        push word [bp-0xe]                              ; 3D39
        push word [bp-0x10]                             ; 3D3C
        mov ax,0x915                                    ; 3D3F
        push cs                                         ; 3D42
        push ax                                         ; 3D43
        sub_ ax,ax                                      ; 3D44
        push ax                                         ; 3D46
        push ax                                         ; 3D47
        push ax                                         ; 3D48
        push ax                                         ; 3D49
        mov ax,[bp-0x8]                                 ; 3D4A
        mov dx,[bp-0x6]                                 ; 3D4D
        add ax,0x205a                                   ; 3D50
        push dx                                         ; 3D53
        push ax                                         ; 3D54
        lea ax,[bp-0xc]                                 ; 3D55
        push ss                                         ; 3D58
        push ax                                         ; 3D59
        callp R3_3D5B, R3_3617, 0x0000                  ; 3D5A KERNEL.RegQueryValueEx
        mov [bp-0x14],ax                                ; 3D5F
        mov [bp-0x12],dx                                ; 3D62
        mov_ ax,dx                                      ; 3D65
        or ax,[bp-0x14]                                 ; 3D67
        jz short L3_3D9C                                ; 3D6A
        mov word [bp-0xc],0x40                          ; 3D6C
        mov word [bp-0xa],0x0                           ; 3D71
        push word [bp-0xe]                              ; 3D76
        push word [bp-0x10]                             ; 3D79
        mov ax,0x8ed                                    ; 3D7C
        push cs                                         ; 3D7F
        push ax                                         ; 3D80
        sub_ ax,ax                                      ; 3D81
        push ax                                         ; 3D83
        push ax                                         ; 3D84
        push ax                                         ; 3D85
        push ax                                         ; 3D86
        mov ax,[bp-0x8]                                 ; 3D87
        mov dx,[bp-0x6]                                 ; 3D8A
        add ax,0x205a                                   ; 3D8D
        push dx                                         ; 3D90
        push ax                                         ; 3D91
        lea ax,[bp-0xc]                                 ; 3D92
        push ss                                         ; 3D95
        push ax                                         ; 3D96
        callp R3_3D98, R3_3DC8, 0x0000                  ; 3D97 KERNEL.RegQueryValueEx

L3_3D9C:
        mov word [bp-0xc],0x10                          ; 3D9C
        mov word [bp-0xa],0x0                           ; 3DA1
        push word [bp-0xe]                              ; 3DA6
        push word [bp-0x10]                             ; 3DA9
        mov ax,0x8ed                                    ; 3DAC
        push cs                                         ; 3DAF
        push ax                                         ; 3DB0
        sub_ ax,ax                                      ; 3DB1
        push ax                                         ; 3DB3
        push ax                                         ; 3DB4
        push ax                                         ; 3DB5
        push ax                                         ; 3DB6
        mov ax,[bp-0x8]                                 ; 3DB7
        mov dx,[bp-0x6]                                 ; 3DBA
        add ax,0x204a                                   ; 3DBD
        push dx                                         ; 3DC0
        push ax                                         ; 3DC1
        lea ax,[bp-0xc]                                 ; 3DC2
        push ss                                         ; 3DC5
        push ax                                         ; 3DC6
        callp R3_3DC8, R3_3E90, 0x0000                  ; 3DC7 KERNEL.RegQueryValueEx

L3_3DCC:
        mov ax,[bp-0xe]                                 ; 3DCC
        or ax,[bp-0x10]                                 ; 3DCF
        jz short L3_3DDF                                ; 3DD2
        push word [bp-0xe]                              ; 3DD4
        push word [bp-0x10]                             ; 3DD7
        callp R3_3DDB, R3_3A54, 0x0000                  ; 3DDA KERNEL.RegCloseKey

L3_3DDF:
        xor_ ax,ax                                      ; 3DDF

L3_3DE1:
        xor_ dx,dx                                      ; 3DE1
        pop si                                          ; 3DE3
        pop di                                          ; 3DE4
        mov_ sp,bp                                      ; 3DE5
        pop bp                                          ; 3DE7
        retf 0x2                                        ; 3DE8
        db 0x90                                         ; 3DEB

; the configuration values and GPO0/GPO1 Show, at the first DRVM_ENABLE
read_config:
        push bp                                         ; 3DEC
        mov_ bp,sp                                      ; 3DED
        sub sp,0x150                                    ; 3DEF
        push di                                         ; 3DF3
        push si                                         ; 3DF4
        mov si,[bp+0x6]                                 ; 3DF5
        mov ax,0x1                                      ; 3DF8
        cwd                                             ; 3DFB
        push dx                                         ; 3DFC
        push ax                                         ; 3DFD
        mov ax,0x100                                    ; 3DFE
        cwd                                             ; 3E01
        push dx                                         ; 3E02
        push ax                                         ; 3E03
        lea ax,[bp-0x150]                               ; 3E04
        push ss                                         ; 3E08
        push ax                                         ; 3E09
        sub_ ax,ax                                      ; 3E0A
        push ax                                         ; 3E0C
        push ax                                         ; 3E0D
        push word [si+0x16]                             ; 3E0E
        push word [si+0x14]                             ; 3E11
        call cm_get_devnode_key                         ; 3E14
        add sp,byte +0x14                               ; 3E17
        or_ ax,ax                                       ; 3E1A
        jz short L3_3E24                                ; 3E1C
        mov ax,0xf                                      ; 3E1E
        jmp near L3_43D8                                ; 3E21

L3_3E24:
        lea ax,[bp-0x150]                               ; 3E24
        push ss                                         ; 3E28
        push ax                                         ; 3E29
        mov ax,0x69e                                    ; 3E2A
        push cs                                         ; 3E2D
        push ax                                         ; 3E2E
        callp R3_3E30, R3_3A64, 0x0000                  ; 3E2F KERNEL.lstrcat
        mov ax,0x2                                      ; 3E34
        mov dx,0x8000                                   ; 3E37
        push dx                                         ; 3E3A
        push ax                                         ; 3E3B
        lea ax,[bp-0x150]                               ; 3E3C
        push ss                                         ; 3E40
        push ax                                         ; 3E41
        lea ax,[bp-0xc]                                 ; 3E42
        push ss                                         ; 3E45
        push ax                                         ; 3E46
        callp R3_3E48, R3_3A7C, 0x0000                  ; 3E47 KERNEL.RegOpenKey
        or_ dx,ax                                       ; 3E4C
        jz short L3_3E58                                ; 3E4E
        sub_ ax,ax                                      ; 3E50
        mov [bp-0xa],ax                                 ; 3E52
        mov [bp-0xc],ax                                 ; 3E55

L3_3E58:
        mov word [bp-0x8],0x1                           ; 3E58
        mov word [bp-0x6],0x0                           ; 3E5D
        mov ax,[bp-0xa]                                 ; 3E62
        or ax,[bp-0xc]                                  ; 3E65
        jz short L3_3E94                                ; 3E68
        mov word [bp-0x10],0x4                          ; 3E6A
        mov word [bp-0xe],0x0                           ; 3E6F
        push word [bp-0xa]                              ; 3E74
        push word [bp-0xc]                              ; 3E77
        mov ax,0x93e                                    ; 3E7A
        push cs                                         ; 3E7D
        push ax                                         ; 3E7E
        sub_ ax,ax                                      ; 3E7F
        push ax                                         ; 3E81
        push ax                                         ; 3E82
        push ax                                         ; 3E83
        push ax                                         ; 3E84
        lea ax,[bp-0x8]                                 ; 3E85
        push ss                                         ; 3E88
        push ax                                         ; 3E89
        lea ax,[bp-0x10]                                ; 3E8A
        push ss                                         ; 3E8D
        push ax                                         ; 3E8E
        callp R3_3E90, R3_3EE2, 0x0000                  ; 3E8F KERNEL.RegQueryValueEx

L3_3E94:
        mov ax,[bp-0x6]                                 ; 3E94
        or ax,[bp-0x8]                                  ; 3E97
        jz short L3_3EA0                                ; 3E9A
        or byte [si+0x2a],0x1                           ; 3E9C

L3_3EA0:
        cmp byte [si+0x6],0x3                           ; 3EA0
        jna short L3_3EAA                               ; 3EA4
        and byte [si+0x2a],0xfe                         ; 3EA6

L3_3EAA:
        mov word [bp-0x8],0x1                           ; 3EAA
        mov word [bp-0x6],0x0                           ; 3EAF
        mov ax,[bp-0xa]                                 ; 3EB4
        or ax,[bp-0xc]                                  ; 3EB7
        jz short L3_3EE6                                ; 3EBA
        mov word [bp-0x10],0x4                          ; 3EBC
        mov word [bp-0xe],0x0                           ; 3EC1
        push word [bp-0xa]                              ; 3EC6
        push word [bp-0xc]                              ; 3EC9
        mov ax,0x930                                    ; 3ECC
        push cs                                         ; 3ECF
        push ax                                         ; 3ED0
        sub_ ax,ax                                      ; 3ED1
        push ax                                         ; 3ED3
        push ax                                         ; 3ED4
        push ax                                         ; 3ED5
        push ax                                         ; 3ED6
        lea ax,[bp-0x8]                                 ; 3ED7
        push ss                                         ; 3EDA
        push ax                                         ; 3EDB
        lea ax,[bp-0x10]                                ; 3EDC
        push ss                                         ; 3EDF
        push ax                                         ; 3EE0
        callp R3_3EE2, R3_3F34, 0x0000                  ; 3EE1 KERNEL.RegQueryValueEx

L3_3EE6:
        mov ax,[bp-0x6]                                 ; 3EE6
        or ax,[bp-0x8]                                  ; 3EE9
        jz short L3_3EF2                                ; 3EEC
        or byte [si+0x2a],0x4                           ; 3EEE

L3_3EF2:
        cmp byte [si+0x6],0x3                           ; 3EF2
        jna short L3_3EFC                               ; 3EF6
        and byte [si+0x2a],0xfb                         ; 3EF8

L3_3EFC:
        mov word [bp-0x8],0x1                           ; 3EFC
        mov word [bp-0x6],0x0                           ; 3F01
        mov ax,[bp-0xa]                                 ; 3F06
        or ax,[bp-0xc]                                  ; 3F09
        jz short L3_3F38                                ; 3F0C
        mov word [bp-0x10],0x4                          ; 3F0E
        mov word [bp-0xe],0x0                           ; 3F13
        push word [bp-0xa]                              ; 3F18
        push word [bp-0xc]                              ; 3F1B
        mov ax,0x80c                                    ; 3F1E
        push cs                                         ; 3F21
        push ax                                         ; 3F22
        sub_ ax,ax                                      ; 3F23
        push ax                                         ; 3F25
        push ax                                         ; 3F26
        push ax                                         ; 3F27
        push ax                                         ; 3F28
        lea ax,[bp-0x8]                                 ; 3F29
        push ss                                         ; 3F2C
        push ax                                         ; 3F2D
        lea ax,[bp-0x10]                                ; 3F2E
        push ss                                         ; 3F31
        push ax                                         ; 3F32
        callp R3_3F34, R3_3F81, 0x0000                  ; 3F33 KERNEL.RegQueryValueEx

L3_3F38:
        mov ax,[bp-0x6]                                 ; 3F38
        or ax,[bp-0x8]                                  ; 3F3B
        jz short L3_3F44                                ; 3F3E
        mov al,0x1                                      ; 3F40
        jmp short L3_3F46                               ; 3F42

L3_3F44:
        xor_ al,al                                      ; 3F44

L3_3F46:
        mov [0xc7],al                                   ; 3F46
        mov word [bp-0x8],0x1                           ; 3F49
        mov word [bp-0x6],0x0                           ; 3F4E
        mov ax,[bp-0xa]                                 ; 3F53
        or ax,[bp-0xc]                                  ; 3F56
        jz short L3_3F85                                ; 3F59
        mov word [bp-0x10],0x4                          ; 3F5B
        mov word [bp-0xe],0x0                           ; 3F60
        push word [bp-0xa]                              ; 3F65
        push word [bp-0xc]                              ; 3F68
        mov ax,0x908                                    ; 3F6B
        push cs                                         ; 3F6E
        push ax                                         ; 3F6F
        sub_ ax,ax                                      ; 3F70
        push ax                                         ; 3F72
        push ax                                         ; 3F73
        push ax                                         ; 3F74
        push ax                                         ; 3F75
        lea ax,[bp-0x8]                                 ; 3F76
        push ss                                         ; 3F79
        push ax                                         ; 3F7A
        lea ax,[bp-0x10]                                ; 3F7B
        push ss                                         ; 3F7E
        push ax                                         ; 3F7F
        callp R3_3F81, R3_3FC9, 0x0000                  ; 3F80 KERNEL.RegQueryValueEx

L3_3F85:
        mov ax,[bp-0x6]                                 ; 3F85
        or ax,[bp-0x8]                                  ; 3F88
        jz short L3_3F91                                ; 3F8B
        or byte [si+0x2a],0x8                           ; 3F8D

L3_3F91:
        mov word [bp-0x8],0x1                           ; 3F91
        mov word [bp-0x6],0x0                           ; 3F96
        mov ax,[bp-0xa]                                 ; 3F9B
        or ax,[bp-0xc]                                  ; 3F9E
        jz short L3_3FCD                                ; 3FA1
        mov word [bp-0x10],0x4                          ; 3FA3
        mov word [bp-0xe],0x0                           ; 3FA8
        push word [bp-0xa]                              ; 3FAD
        push word [bp-0xc]                              ; 3FB0
        mov ax,0x8e0                                    ; 3FB3
        push cs                                         ; 3FB6
        push ax                                         ; 3FB7
        sub_ ax,ax                                      ; 3FB8
        push ax                                         ; 3FBA
        push ax                                         ; 3FBB
        push ax                                         ; 3FBC
        push ax                                         ; 3FBD
        lea ax,[bp-0x8]                                 ; 3FBE
        push ss                                         ; 3FC1
        push ax                                         ; 3FC2
        lea ax,[bp-0x10]                                ; 3FC3
        push ss                                         ; 3FC6
        push ax                                         ; 3FC7
        callp R3_3FC9, R3_402C, 0x0000                  ; 3FC8 KERNEL.RegQueryValueEx

L3_3FCD:
        cmp word [bp-0x6],byte +0x0                     ; 3FCD
        jnz short L3_3FD9                               ; 3FD1
        cmp word [bp-0x8],byte +0x1                     ; 3FD3
        jc short L3_3FE5                                ; 3FD7

L3_3FD9:
        cmp word [bp-0x6],byte +0x0                     ; 3FD9
        jnz short L3_3FE5                               ; 3FDD
        cmp word [bp-0x8],byte +0x4                     ; 3FDF
        jna short L3_3FEF                               ; 3FE3

L3_3FE5:
        mov word [bp-0x8],0x1                           ; 3FE5
        mov word [bp-0x6],0x0                           ; 3FEA

L3_3FEF:
        mov al,[bp-0x8]                                 ; 3FEF
        mov [si+0x123],al                               ; 3FF2
        sub_ ax,ax                                      ; 3FF6
        mov [bp-0x6],ax                                 ; 3FF8
        mov [bp-0x8],ax                                 ; 3FFB
        mov ax,[bp-0xa]                                 ; 3FFE
        or ax,[bp-0xc]                                  ; 4001
        jz short L3_4030                                ; 4004
        mov word [bp-0x10],0x4                          ; 4006
        mov word [bp-0xe],0x0                           ; 400B
        push word [bp-0xa]                              ; 4010
        push word [bp-0xc]                              ; 4013
        mov ax,0x83b                                    ; 4016
        push cs                                         ; 4019
        push ax                                         ; 401A
        sub_ ax,ax                                      ; 401B
        push ax                                         ; 401D
        push ax                                         ; 401E
        push ax                                         ; 401F
        push ax                                         ; 4020
        lea ax,[bp-0x8]                                 ; 4021
        push ss                                         ; 4024
        push ax                                         ; 4025
        lea ax,[bp-0x10]                                ; 4026
        push ss                                         ; 4029
        push ax                                         ; 402A
        callp R3_402C, R3_4077, 0x0000                  ; 402B KERNEL.RegQueryValueEx

L3_4030:
        cmp word [bp-0x8],byte +0x1                     ; 4030
        jnz short L3_4041                               ; 4034
        cmp word [bp-0x6],byte +0x0                     ; 4036
        jnz short L3_4041                               ; 403A
        mov byte [si+0x123],0x3                         ; 403C

L3_4041:
        sub_ ax,ax                                      ; 4041
        mov [bp-0xe],ax                                 ; 4043
        mov [bp-0x10],ax                                ; 4046
        mov ax,[bp-0xa]                                 ; 4049
        or ax,[bp-0xc]                                  ; 404C
        jz short L3_4087                                ; 404F
        mov word [bp-0x10],0x40                         ; 4051
        mov word [bp-0xe],0x0                           ; 4056
        push word [bp-0xa]                              ; 405B
        push word [bp-0xc]                              ; 405E
        mov ax,0x8b9                                    ; 4061
        push cs                                         ; 4064
        push ax                                         ; 4065
        sub_ ax,ax                                      ; 4066
        push ax                                         ; 4068
        push ax                                         ; 4069
        push ax                                         ; 406A
        push ax                                         ; 406B
        lea ax,[bp-0x50]                                ; 406C
        push ss                                         ; 406F
        push ax                                         ; 4070
        lea ax,[bp-0x10]                                ; 4071
        push ss                                         ; 4074
        push ax                                         ; 4075
        callp R3_4077, R3_40D8, 0x0000                  ; 4076 KERNEL.RegQueryValueEx
        or_ dx,ax                                       ; 407B
        jz short L3_4087                                ; 407D
        sub_ ax,ax                                      ; 407F
        mov [bp-0xe],ax                                 ; 4081
        mov [bp-0x10],ax                                ; 4084

L3_4087:
        cmp word [bp-0x10],byte +0x40                   ; 4087
        jnz short L3_40A2                               ; 408B
        cmp word [bp-0xe],byte +0x0                     ; 408D
        jnz short L3_40A2                               ; 4091
        xor_ di,di                                      ; 4093

L3_4095:
        mov al,[bp+di-0x50]                             ; 4095
        mov [di+0x950],al                               ; 4098
        inc di                                          ; 409C
        cmp di,byte +0x40                               ; 409D
        jc short L3_4095                                ; 40A0

L3_40A2:
        sub_ ax,ax                                      ; 40A2
        mov [bp-0x6],ax                                 ; 40A4
        mov [bp-0x8],ax                                 ; 40A7
        mov ax,[bp-0xa]                                 ; 40AA
        or ax,[bp-0xc]                                  ; 40AD
        jz short L3_40DC                                ; 40B0
        mov word [bp-0x10],0x4                          ; 40B2
        mov word [bp-0xe],0x0                           ; 40B7
        push word [bp-0xa]                              ; 40BC
        push word [bp-0xc]                              ; 40BF
        mov ax,0x87b                                    ; 40C2
        push cs                                         ; 40C5
        push ax                                         ; 40C6
        sub_ ax,ax                                      ; 40C7
        push ax                                         ; 40C9
        push ax                                         ; 40CA
        push ax                                         ; 40CB
        push ax                                         ; 40CC
        lea ax,[bp-0x8]                                 ; 40CD
        push ss                                         ; 40D0
        push ax                                         ; 40D1
        lea ax,[bp-0x10]                                ; 40D2
        push ss                                         ; 40D5
        push ax                                         ; 40D6
        callp R3_40D8, R3_4120, 0x0000                  ; 40D7 KERNEL.RegQueryValueEx

L3_40DC:
        mov ax,[bp-0x6]                                 ; 40DC
        or ax,[bp-0x8]                                  ; 40DF
        jnz short L3_40E8                               ; 40E2
        or byte [si+0x2b],0x80                          ; 40E4

L3_40E8:
        mov word [bp-0x8],0x1                           ; 40E8
        mov word [bp-0x6],0x0                           ; 40ED
        mov ax,[bp-0xa]                                 ; 40F2
        or ax,[bp-0xc]                                  ; 40F5
        jz short L3_4124                                ; 40F8
        mov word [bp-0x10],0x4                          ; 40FA
        mov word [bp-0xe],0x0                           ; 40FF
        push word [bp-0xa]                              ; 4104
        push word [bp-0xc]                              ; 4107
        mov ax,0x7cd                                    ; 410A
        push cs                                         ; 410D
        push ax                                         ; 410E
        sub_ ax,ax                                      ; 410F
        push ax                                         ; 4111
        push ax                                         ; 4112
        push ax                                         ; 4113
        push ax                                         ; 4114
        lea ax,[bp-0x8]                                 ; 4115
        push ss                                         ; 4118
        push ax                                         ; 4119
        lea ax,[bp-0x10]                                ; 411A
        push ss                                         ; 411D
        push ax                                         ; 411E
        callp R3_4120, R3_3A16, 0x0000                  ; 411F KERNEL.RegQueryValueEx

L3_4124:
        mov ax,[bp-0x6]                                 ; 4124
        or ax,[bp-0x8]                                  ; 4127
        jz short L3_4130                                ; 412A
        or byte [si+0x2c],0x8                           ; 412C

L3_4130:
        push si                                         ; 4130
        push cs                                         ; 4131
        call L3_0B92                                    ; 4132
        or_ ax,ax                                       ; 4135
        jz short L3_413D                                ; 4137
        or byte [si+0x2c],0x4                           ; 4139

L3_413D:
        push si                                         ; 413D
        push cs                                         ; 413E
        call L3_0BEC                                    ; 413F
        or_ ax,ax                                       ; 4142
        jz short L3_414A                                ; 4144
        or byte [si+0x2c],0x4                           ; 4146

L3_414A:
        push si                                         ; 414A
        push cs                                         ; 414B
        call L3_0C46                                    ; 414C
        or_ ax,ax                                       ; 414F
        jz short L3_4157                                ; 4151
        or byte [si+0x2c],0x4                           ; 4153

L3_4157:
        push si                                         ; 4157
        push cs                                         ; 4158
        call L3_0CA0                                    ; 4159
        or_ ax,ax                                       ; 415C
        jz short L3_4164                                ; 415E
        or byte [si+0x2c],0x4                           ; 4160

L3_4164:
        sub_ ax,ax                                      ; 4164
        mov [bp-0x6],ax                                 ; 4166
        mov [bp-0x8],ax                                 ; 4169
        mov word [bp-0x4],0x2                           ; 416C
        mov [bp-0x2],ax                                 ; 4171
        mov ax,[bp-0xa]                                 ; 4174
        or ax,[bp-0xc]                                  ; 4177
        jz short L3_41AC                                ; 417A
        mov word [bp-0x10],0x4                          ; 417C
        mov word [bp-0xe],0x0                           ; 4181
        push word [bp-0xa]                              ; 4186
        push word [bp-0xc]                              ; 4189
        mov ax,0x78a                                    ; 418C
        push cs                                         ; 418F
        push ax                                         ; 4190
        sub_ ax,ax                                      ; 4191
        push ax                                         ; 4193
        push ax                                         ; 4194
        push ax                                         ; 4195
        push ax                                         ; 4196
        lea ax,[bp-0x8]                                 ; 4197
        push ss                                         ; 419A
        push ax                                         ; 419B
        lea ax,[bp-0x10]                                ; 419C
        push ss                                         ; 419F
        push ax                                         ; 41A0
        callp R3_41A2, R3_41FA, 0x0000                  ; 41A1 KERNEL.RegQueryValueEx
        mov [bp-0x4],ax                                 ; 41A6
        mov [bp-0x2],dx                                 ; 41A9

L3_41AC:
        mov ax,[bp-0x6]                                 ; 41AC
        or ax,[bp-0x8]                                  ; 41AF
        jz short L3_41B8                                ; 41B2
        or byte [si+0x2a],0x80                          ; 41B4

L3_41B8:
        mov ax,[bp-0x2]                                 ; 41B8
        or ax,[bp-0x4]                                  ; 41BB
        jz short L3_41C4                                ; 41BE
        or byte [si+0x2a],0x80                          ; 41C0

L3_41C4:
        sub_ ax,ax                                      ; 41C4
        mov [bp-0x6],ax                                 ; 41C6
        mov [bp-0x8],ax                                 ; 41C9
        mov ax,[bp-0xa]                                 ; 41CC
        or ax,[bp-0xc]                                  ; 41CF
        jz short L3_41FE                                ; 41D2
        mov word [bp-0x10],0x4                          ; 41D4
        mov word [bp-0xe],0x0                           ; 41D9
        push word [bp-0xa]                              ; 41DE
        push word [bp-0xc]                              ; 41E1
        mov ax,0x765                                    ; 41E4
        push cs                                         ; 41E7
        push ax                                         ; 41E8
        sub_ ax,ax                                      ; 41E9
        push ax                                         ; 41EB
        push ax                                         ; 41EC
        push ax                                         ; 41ED
        push ax                                         ; 41EE
        lea ax,[bp-0x8]                                 ; 41EF
        push ss                                         ; 41F2
        push ax                                         ; 41F3
        lea ax,[bp-0x10]                                ; 41F4
        push ss                                         ; 41F7
        push ax                                         ; 41F8
        callp R3_41FA, R3_4248, 0x0000                  ; 41F9 KERNEL.RegQueryValueEx

L3_41FE:
        mov ax,[bp-0x6]                                 ; 41FE
        or ax,[bp-0x8]                                  ; 4201
        jz short L3_420C                                ; 4204
        or byte [si+0x2b],0x4                           ; 4206
        jmp short L3_4210                               ; 420A

L3_420C:
        and byte [si+0x2b],0xfb                         ; 420C

L3_4210:
        mov word [bp-0x8],0x1                           ; 4210
        mov word [bp-0x6],0x0                           ; 4215
        mov ax,[bp-0xa]                                 ; 421A
        or ax,[bp-0xc]                                  ; 421D
        jz short L3_424C                                ; 4220
        mov word [bp-0x10],0x4                          ; 4222
        mov word [bp-0xe],0x0                           ; 4227
        push word [bp-0xa]                              ; 422C
        push word [bp-0xc]                              ; 422F
        mov ax,0x7b5                                    ; 4232
        push cs                                         ; 4235
        push ax                                         ; 4236
        sub_ ax,ax                                      ; 4237
        push ax                                         ; 4239
        push ax                                         ; 423A
        push ax                                         ; 423B
        push ax                                         ; 423C
        lea ax,[bp-0x8]                                 ; 423D
        push ss                                         ; 4240
        push ax                                         ; 4241
        lea ax,[bp-0x10]                                ; 4242
        push ss                                         ; 4245
        push ax                                         ; 4246
        callp R3_4248, R3_4294, 0x0000                  ; 4247 KERNEL.RegQueryValueEx

L3_424C:
        mov ax,[bp-0x6]                                 ; 424C
        or ax,[bp-0x8]                                  ; 424F
        jz short L3_425A                                ; 4252
        or byte [si+0x2c],0x10                          ; 4254
        jmp short L3_425E                               ; 4258

L3_425A:
        and byte [si+0x2c],0xef                         ; 425A

L3_425E:
        sub_ ax,ax                                      ; 425E
        mov [bp-0x6],ax                                 ; 4260
        mov [bp-0x8],ax                                 ; 4263
        mov ax,[bp-0xa]                                 ; 4266
        or ax,[bp-0xc]                                  ; 4269
        jz short L3_4298                                ; 426C
        mov word [bp-0x10],0x4                          ; 426E
        mov word [bp-0xe],0x0                           ; 4273
        push word [bp-0xa]                              ; 4278
        push word [bp-0xc]                              ; 427B
        mov ax,0x6fa                                    ; 427E
        push cs                                         ; 4281
        push ax                                         ; 4282
        sub_ ax,ax                                      ; 4283
        push ax                                         ; 4285
        push ax                                         ; 4286
        push ax                                         ; 4287
        push ax                                         ; 4288
        lea ax,[bp-0x8]                                 ; 4289
        push ss                                         ; 428C
        push ax                                         ; 428D
        lea ax,[bp-0x10]                                ; 428E
        push ss                                         ; 4291
        push ax                                         ; 4292
        callp R3_4294, R3_42E0, 0x0000                  ; 4293 KERNEL.RegQueryValueEx

L3_4298:
        mov ax,[bp-0x6]                                 ; 4298
        or ax,[bp-0x8]                                  ; 429B
        jz short L3_42AA                                ; 429E
        test byte [si+0x2a],0x80                        ; 42A0
        jnz short L3_42AA                               ; 42A4
        or byte [si+0x2a],0x20                          ; 42A6

L3_42AA:
        sub_ ax,ax                                      ; 42AA
        mov [bp-0x6],ax                                 ; 42AC
        mov [bp-0x8],ax                                 ; 42AF
        mov ax,[bp-0xa]                                 ; 42B2
        or ax,[bp-0xc]                                  ; 42B5
        jz short L3_42E4                                ; 42B8
        mov word [bp-0x10],0x4                          ; 42BA
        mov word [bp-0xe],0x0                           ; 42BF
        push word [bp-0xa]                              ; 42C4
        push word [bp-0xc]                              ; 42C7
        mov ax,0x60a                                    ; 42CA
        push cs                                         ; 42CD
        push ax                                         ; 42CE
        sub_ ax,ax                                      ; 42CF
        push ax                                         ; 42D1
        push ax                                         ; 42D2
        push ax                                         ; 42D3
        push ax                                         ; 42D4
        lea ax,[bp-0x8]                                 ; 42D5
        push ss                                         ; 42D8
        push ax                                         ; 42D9
        lea ax,[bp-0x10]                                ; 42DA
        push ss                                         ; 42DD
        push ax                                         ; 42DE
        callp R3_42E0, R3_436D, 0x0000                  ; 42DF KERNEL.RegQueryValueEx

L3_42E4:
        mov ax,[bp-0x6]                                 ; 42E4
        or ax,[bp-0x8]                                  ; 42E7
        jz short L3_42F0                                ; 42EA
        or byte [si+0x2b],0x40                          ; 42EC

L3_42F0:
        mov ax,[bp-0xa]                                 ; 42F0
        or ax,[bp-0xc]                                  ; 42F3
        jz short L3_4303                                ; 42F6
        push word [bp-0xa]                              ; 42F8
        push word [bp-0xc]                              ; 42FB
        callp R3_42FF, R3_43D2, 0x0000                  ; 42FE KERNEL.RegCloseKey

L3_4303:
        lea ax,[bp-0x150]                               ; 4303
        push ss                                         ; 4307
        push ax                                         ; 4308
        mov ax,0x67d                                    ; 4309
        push cs                                         ; 430C
        push ax                                         ; 430D
        callp R3_430F, R3_4421, 0x0000                  ; 430E KERNEL.lstrcat
        mov ax,0x2                                      ; 4313
        mov dx,0x8000                                   ; 4316
        push dx                                         ; 4319
        push ax                                         ; 431A
        lea ax,[bp-0x150]                               ; 431B
        push ss                                         ; 431F
        push ax                                         ; 4320
        lea ax,[bp-0xc]                                 ; 4321
        push ss                                         ; 4324
        push ax                                         ; 4325
        callp R3_4327, R3_4439, 0x0000                  ; 4326 KERNEL.RegOpenKey
        or_ dx,ax                                       ; 432B
        jz short L3_4337                                ; 432D
        sub_ ax,ax                                      ; 432F
        mov [bp-0xa],ax                                 ; 4331
        mov [bp-0xc],ax                                 ; 4334

L3_4337:
        sub_ ax,ax                                      ; 4337
        mov [bp-0x6],ax                                 ; 4339
        mov [bp-0x8],ax                                 ; 433C
        mov ax,[bp-0xa]                                 ; 433F
        or ax,[bp-0xc]                                  ; 4342
        jz short L3_4371                                ; 4345
        mov word [bp-0x10],0x4                          ; 4347
        mov word [bp-0xe],0x0                           ; 434C
        push word [bp-0xa]                              ; 4351
        push word [bp-0xc]                              ; 4354
        mov ax,0x3bb                                    ; 4357
        push cs                                         ; 435A
        push ax                                         ; 435B
        sub_ ax,ax                                      ; 435C
        push ax                                         ; 435E
        push ax                                         ; 435F
        push ax                                         ; 4360
        push ax                                         ; 4361
        lea ax,[bp-0x8]                                 ; 4362
        push ss                                         ; 4365
        push ax                                         ; 4366
        lea ax,[bp-0x10]                                ; 4367
        push ss                                         ; 436A
        push ax                                         ; 436B
        callp R3_436D, R3_43B3, 0x0000                  ; 436C KERNEL.RegQueryValueEx

L3_4371:
        mov ax,[bp-0x6]                                 ; 4371
        or ax,[bp-0x8]                                  ; 4374
        jz short L3_437D                                ; 4377
        or byte [si+0x2b],0x1                           ; 4379

L3_437D:
        sub_ ax,ax                                      ; 437D
        mov [bp-0x6],ax                                 ; 437F
        mov [bp-0x8],ax                                 ; 4382
        mov ax,[bp-0xa]                                 ; 4385
        or ax,[bp-0xc]                                  ; 4388
        jz short L3_43B7                                ; 438B
        mov word [bp-0x10],0x4                          ; 438D
        mov word [bp-0xe],0x0                           ; 4392
        push word [bp-0xa]                              ; 4397
        push word [bp-0xc]                              ; 439A
        mov ax,0x934                                    ; 439D
        push cs                                         ; 43A0
        push ax                                         ; 43A1
        sub_ ax,ax                                      ; 43A2
        push ax                                         ; 43A4
        push ax                                         ; 43A5
        push ax                                         ; 43A6
        push ax                                         ; 43A7
        lea ax,[bp-0x8]                                 ; 43A8
        push ss                                         ; 43AB
        push ax                                         ; 43AC
        lea ax,[bp-0x10]                                ; 43AD
        push ss                                         ; 43B0
        push ax                                         ; 43B1
        callp R3_43B3, R3_447B, 0x0000                  ; 43B2 KERNEL.RegQueryValueEx

L3_43B7:
        mov ax,[bp-0x6]                                 ; 43B7
        or ax,[bp-0x8]                                  ; 43BA
        jz short L3_43C3                                ; 43BD
        or byte [si+0x2b],0x2                           ; 43BF

L3_43C3:
        mov ax,[bp-0xa]                                 ; 43C3
        or ax,[bp-0xc]                                  ; 43C6
        jz short L3_43D6                                ; 43C9
        push word [bp-0xa]                              ; 43CB
        push word [bp-0xc]                              ; 43CE
        callp R3_43D2, R3_4499, 0x0000                  ; 43D1 KERNEL.RegCloseKey

L3_43D6:
        xor_ ax,ax                                      ; 43D6

L3_43D8:
        xor_ dx,dx                                      ; 43D8
        pop si                                          ; 43DA
        pop di                                          ; 43DB
        mov_ sp,bp                                      ; 43DC
        pop bp                                          ; 43DE
        retf 0x2                                        ; 43DF

L3_43E2:
        push bp                                         ; 43E2
        mov_ bp,sp                                      ; 43E3
        sub sp,0x10a                                    ; 43E5
        mov ax,0x1                                      ; 43E9
        cwd                                             ; 43EC
        push dx                                         ; 43ED
        push ax                                         ; 43EE
        mov ax,0x100                                    ; 43EF
        cwd                                             ; 43F2
        push dx                                         ; 43F3
        push ax                                         ; 43F4
        lea ax,[bp-0x10a]                               ; 43F5
        push ss                                         ; 43F9
        push ax                                         ; 43FA
        sub_ ax,ax                                      ; 43FB
        push ax                                         ; 43FD
        push ax                                         ; 43FE
        push word [bp+0x8]                              ; 43FF
        push word [bp+0x6]                              ; 4402
        call cm_get_devnode_key                         ; 4405
        add sp,byte +0x14                               ; 4408
        or_ ax,ax                                       ; 440B
        jz short L3_4415                                ; 440D
        mov ax,0xf                                      ; 440F
        jmp near L3_449F                                ; 4412

L3_4415:
        lea ax,[bp-0x10a]                               ; 4415
        push ss                                         ; 4419
        push ax                                         ; 441A
        mov ax,0x69e                                    ; 441B
        push cs                                         ; 441E
        push ax                                         ; 441F
        callp R3_4421, R3_3E30, 0x0000                  ; 4420 KERNEL.lstrcat
        mov ax,0x2                                      ; 4425
        mov dx,0x8000                                   ; 4428
        push dx                                         ; 442B
        push ax                                         ; 442C
        lea ax,[bp-0x10a]                               ; 442D
        push ss                                         ; 4431
        push ax                                         ; 4432
        lea ax,[bp-0x6]                                 ; 4433
        push ss                                         ; 4436
        push ax                                         ; 4437
        callp R3_4439, R3_3E48, 0x0000                  ; 4438 KERNEL.RegOpenKey
        or_ dx,ax                                       ; 443D
        jz short L3_4449                                ; 443F
        sub_ ax,ax                                      ; 4441
        mov [bp-0x4],ax                                 ; 4443
        mov [bp-0x6],ax                                 ; 4446

L3_4449:
        mov byte [bp-0x1],0x0                           ; 4449
        mov ax,[bp-0x4]                                 ; 444D
        or ax,[bp-0x6]                                  ; 4450
        jz short L3_447F                                ; 4453
        mov word [bp-0xa],0x1                           ; 4455
        mov word [bp-0x8],0x0                           ; 445A
        push word [bp-0x4]                              ; 445F
        push word [bp-0x6]                              ; 4462
        mov ax,0x96e                                    ; 4465
        push cs                                         ; 4468
        push ax                                         ; 4469
        sub_ ax,ax                                      ; 446A
        push ax                                         ; 446C
        push ax                                         ; 446D
        push ax                                         ; 446E
        push ax                                         ; 446F
        lea ax,[bp-0x1]                                 ; 4470
        push ss                                         ; 4473
        push ax                                         ; 4474
        lea ax,[bp-0xa]                                 ; 4475
        push ss                                         ; 4478
        push ax                                         ; 4479
        callp R3_447B, R3_3D98, 0x0000                  ; 447A KERNEL.RegQueryValueEx

L3_447F:
        cmp byte [bp-0x1],0x0                           ; 447F
        jz short L3_448A                                ; 4483
        or byte [0xc4],0x1                              ; 4485

L3_448A:
        mov ax,[bp-0x4]                                 ; 448A
        or ax,[bp-0x6]                                  ; 448D
        jz short L3_449D                                ; 4490
        push word [bp-0x4]                              ; 4492
        push word [bp-0x6]                              ; 4495
        callp R3_4499, R3_3DDB, 0x0000                  ; 4498 KERNEL.RegCloseKey

L3_449D:
        xor_ ax,ax                                      ; 449D

L3_449F:
        xor_ dx,dx                                      ; 449F
        mov_ sp,bp                                      ; 44A1
        pop bp                                          ; 44A3
        retf 0x4                                        ; 44A4
        db 0x90                                         ; 44A7

L3_44A8:
        push bp                                         ; 44A8
        mov_ bp,sp                                      ; 44A9
        sub sp,byte +0x8                                ; 44AB
        push si                                         ; 44AE
        mov si,[bp+0xa]                                 ; 44AF
        or_ si,si                                       ; 44B2
        jz short L3_4509                                ; 44B4
        mov word [bp-0x8],0x4                           ; 44B6
        mov word [bp-0x6],0x0                           ; 44BB
        mov ax,0x1                                      ; 44C0
        cwd                                             ; 44C3
        push dx                                         ; 44C4
        push ax                                         ; 44C5
        lea ax,[bp-0x8]                                 ; 44C6
        push ss                                         ; 44C9
        push ax                                         ; 44CA
        lea ax,[bp-0x4]                                 ; 44CB
        push ss                                         ; 44CE
        push ax                                         ; 44CF
        mov ax,0x3                                      ; 44D0
        cwd                                             ; 44D3
        push dx                                         ; 44D4
        push ax                                         ; 44D5
        mov ax,0x87b                                    ; 44D6
        push cs                                         ; 44D9
        push ax                                         ; 44DA
        mov ax,0x897                                    ; 44DB
        push cs                                         ; 44DE
        push ax                                         ; 44DF
        push word [bp+0x8]                              ; 44E0
        push word [bp+0x6]                              ; 44E3
        call L3_0A1C                                    ; 44E6
        add sp,byte +0x1c                               ; 44E9
        or_ ax,ax                                       ; 44EC
        jz short L3_44F8                                ; 44EE
        sub_ ax,ax                                      ; 44F0
        mov [bp-0x2],ax                                 ; 44F2
        mov [bp-0x4],ax                                 ; 44F5

L3_44F8:
        mov ax,[bp-0x2]                                 ; 44F8
        or ax,[bp-0x4]                                  ; 44FB
        jz short L3_4505                                ; 44FE
        mov ax,0x1                                      ; 4500
        jmp short L3_4507                               ; 4503

L3_4505:
        xor_ ax,ax                                      ; 4505

L3_4507:
        mov [si],ax                                     ; 4507

L3_4509:
        pop si                                          ; 4509
        mov_ sp,bp                                      ; 450A
        pop bp                                          ; 450C
        retf                                            ; 450D

L3_450E:
        push bp                                         ; 450E
        mov_ bp,sp                                      ; 450F
        sub sp,byte +0x4                                ; 4511
        cmp word [bp+0xa],byte +0x1                     ; 4514
        sbb_ ax,ax                                      ; 4518
        inc ax                                          ; 451A
        cwd                                             ; 451B
        mov [bp-0x4],ax                                 ; 451C
        mov [bp-0x2],dx                                 ; 451F
        mov ax,0x1                                      ; 4522
        cwd                                             ; 4525
        push dx                                         ; 4526
        push ax                                         ; 4527
        mov ax,0x4                                      ; 4528
        cwd                                             ; 452B
        push dx                                         ; 452C
        push ax                                         ; 452D
        lea ax,[bp-0x4]                                 ; 452E
        push ss                                         ; 4531
        push ax                                         ; 4532
        mov ax,0x3                                      ; 4533
        cwd                                             ; 4536
        push dx                                         ; 4537
        push ax                                         ; 4538
        mov ax,0x87b                                    ; 4539
        push cs                                         ; 453C
        push ax                                         ; 453D
        mov ax,0x897                                    ; 453E
        push cs                                         ; 4541
        push ax                                         ; 4542
        push word [bp+0x8]                              ; 4543
        push word [bp+0x6]                              ; 4546
        call L3_0A4E                                    ; 4549
        mov_ sp,bp                                      ; 454C
        pop bp                                          ; 454E
        retf                                            ; 454F

L3_4550:
        push bp                                         ; 4550
        mov_ bp,sp                                      ; 4551
        push word [bp+0xa]                              ; 4553
        mov ax,0xa                                      ; 4556
        cwd                                             ; 4559
        push dx                                         ; 455A
        push ax                                         ; 455B
        push word [bp+0xc]                              ; 455C
        mov ax,0x4578                                   ; 455F
        movsel dx, R3_4563, 0xFFFF                      ; 4562 seg3
        push dx                                         ; 4565
        push ax                                         ; 4566
        push word [bp+0x8]                              ; 4567
        push word [bp+0x6]                              ; 456A
        callp R3_456E, 0xFFFF, 0x0000                   ; 456D USER.DialogBoxParam
        mov_ sp,bp                                      ; 4572
        pop bp                                          ; 4574
        retf 0x8                                        ; 4575
        push bp                                         ; 4578
        mov_ bp,sp                                      ; 4579
        sub sp,byte +0xa                                ; 457B
        push di                                         ; 457E
        push si                                         ; 457F
        push ds                                         ; 4580
        movsel ax, R3_4582, R3_02C7                     ; 4581 seg7
        mov ds,ax                                       ; 4584
        mov ax,[bp+0xc]                                 ; 4586
        sub ax,strict word 0x10                         ; 4589
        jz short L3_459C                                ; 458C
        sub ax,0x100                                    ; 458E
        jz short L3_45AD                                ; 4591
        dec ax                                          ; 4593
        jnz short L3_4599                               ; 4594
        jmp near L3_4646                                ; 4596

L3_4599:
        jmp near L3_468F                                ; 4599

L3_459C:
        push word [bp+0xe]                              ; 459C

L3_459F:
        xor_ ax,ax                                      ; 459F

L3_45A1:
        push ax                                         ; 45A1
        callp R3_45A3, 0xFFFF, 0x0000                   ; 45A2 USER.EndDialog

L3_45A7:
        mov ax,0x1                                      ; 45A7
        jmp near L3_4691                                ; 45AA

L3_45AD:
        mov si,[bp+0xe]                                 ; 45AD
        push si                                         ; 45B0
        lea ax,[bp-0xa]                                 ; 45B1
        push ss                                         ; 45B4
        push ax                                         ; 45B5
        callp R3_45B7, 0xFFFF, 0x0000                   ; 45B6 USER.GetWindowRect
        xor_ ax,ax                                      ; 45BB
        push ax                                         ; 45BD
        callp R3_45BF, R3_45DA, 0x0000                  ; 45BE USER.GetSystemMetrics
        sub ax,[bp-0x6]                                 ; 45C3
        neg ax                                          ; 45C6
        sub ax,[bp-0xa]                                 ; 45C8
        cwd                                             ; 45CB
        sub_ ax,dx                                      ; 45CC
        sar ax,1                                        ; 45CE
        mov_ di,ax                                      ; 45D0
        add di,[bp-0xa]                                 ; 45D2
        mov ax,0x1                                      ; 45D5
        push ax                                         ; 45D8
        callp R3_45DA, 0xFFFF, 0x0000                   ; 45D9 USER.GetSystemMetrics
        sub ax,[bp-0x4]                                 ; 45DE
        neg ax                                          ; 45E1
        sub ax,[bp-0x8]                                 ; 45E3
        cwd                                             ; 45E6
        sub_ ax,dx                                      ; 45E7
        sar ax,1                                        ; 45E9
        add ax,[bp-0x8]                                 ; 45EB
        mov [bp-0x2],ax                                 ; 45EE
        push si                                         ; 45F1
        xor_ ax,ax                                      ; 45F2
        push ax                                         ; 45F4
        sub di,[bp-0xa]                                 ; 45F5
        neg di                                          ; 45F8
        push di                                         ; 45FA
        mov ax,[bp-0x8]                                 ; 45FB
        sub ax,[bp-0x2]                                 ; 45FE
        push ax                                         ; 4601
        xor_ ax,ax                                      ; 4602
        push ax                                         ; 4604
        push ax                                         ; 4605
        mov ax,0x5                                      ; 4606
        push ax                                         ; 4609
        callp R3_460B, 0xFFFF, 0x0000                   ; 460A USER.SetWindowPos
        mov ax,0xbc6                                    ; 460F
        push ax                                         ; 4612
        les bx,[bp+0x6]                                 ; 4613
        mov [0xbc2],bx                                  ; 4616
        mov [0xbc4],es                                  ; 461A
        push word [es:bx+0xe]                           ; 461E
        push word [es:bx+0xc]                           ; 4622
        push cs                                         ; 4626
        call L3_44A8                                    ; 4627
        add sp,byte +0x6                                ; 462A
        push si                                         ; 462D
        mov ax,0x200                                    ; 462E
        push ax                                         ; 4631
        mov ax,0x401                                    ; 4632
        push ax                                         ; 4635
        push word [0xbc6]                               ; 4636
        sub_ ax,ax                                      ; 463A
        push ax                                         ; 463C
        push ax                                         ; 463D
        callp R3_463F, R3_4666, 0x0000                  ; 463E USER.SendDlgItemMessage
        jmp near L3_45A7                                ; 4643

L3_4646:
        mov ax,[bp+0xa]                                 ; 4646
        dec ax                                          ; 4649
        jz short L3_4654                                ; 464A
        dec ax                                          ; 464C
        jnz short L3_4652                               ; 464D
        jmp near L3_459C                                ; 464F

L3_4652:
        jmp short L3_468F                               ; 4652

L3_4654:
        mov di,[bp+0xe]                                 ; 4654
        push di                                         ; 4657
        mov ax,0x200                                    ; 4658
        push ax                                         ; 465B
        mov ax,0x400                                    ; 465C
        push ax                                         ; 465F
        xor_ ax,ax                                      ; 4660
        push ax                                         ; 4662
        push ax                                         ; 4663
        push ax                                         ; 4664
        callp R3_4666, 0xFFFF, 0x0000                   ; 4665 USER.SendDlgItemMessage
        cmp [0xbc6],ax                                  ; 466A
        jnz short L3_4674                               ; 466E
        push di                                         ; 4670
        jmp near L3_459F                                ; 4671

L3_4674:
        push ax                                         ; 4674
        les bx,[0xbc2]                                  ; 4675
        push word [es:bx+0xe]                           ; 4679
        push word [es:bx+0xc]                           ; 467D
        push cs                                         ; 4681
        call L3_450E                                    ; 4682
        add sp,byte +0x6                                ; 4685
        push di                                         ; 4688
        mov ax,0x2                                      ; 4689
        jmp near L3_45A1                                ; 468C

L3_468F:
        xor_ ax,ax                                      ; 468F

L3_4691:
        pop ds                                          ; 4691
        pop si                                          ; 4692
        pop di                                          ; 4693
        mov_ sp,bp                                      ; 4694
        pop bp                                          ; 4696
        retf 0xa                                        ; 4697
        push bp                                         ; 469A
        mov_ bp,sp                                      ; 469B
        mov ax,[bp+0x4]                                 ; 469D
        jmp short L3_46CD                               ; 46A0

L3_46A2:
        cmp word [0xfa],byte +0x0                       ; 46A2
        jnz short L3_46AF                               ; 46A7
        pushf                                           ; 46A9
        pop ax                                          ; 46AA
        mov [0xfc],ax                                   ; 46AB
        cli                                             ; 46AE

L3_46AF:
        inc word [0xfa]                                 ; 46AF
        jmp short L3_46D3                               ; 46B3

L3_46B5:
        cmp word [0xfa],byte +0x0                       ; 46B5
        jz short L3_46D3                                ; 46BA
        dec word [0xfa]                                 ; 46BC
        jnz short L3_46D3                               ; 46C0
        mov ax,[0xfc]                                   ; 46C2
        test ah,0x2                                     ; 46C5
        jz short L3_46CB                                ; 46C8
        sti                                             ; 46CA

L3_46CB:
        jmp short L3_46D3                               ; 46CB

L3_46CD:
        dec ax                                          ; 46CD
        jz short L3_46A2                                ; 46CE
        dec ax                                          ; 46D0
        jz short L3_46B5                                ; 46D1

L3_46D3:
        mov_ sp,bp                                      ; 46D3
        pop bp                                          ; 46D5
        ret 0x2                                         ; 46D6
        db 0x90                                         ; 46D9

L3_46DA:
        push bp                                         ; 46DA
        mov_ bp,sp                                      ; 46DB
        sub sp,byte +0x4                                ; 46DD
        cmp word [bp+0x4],byte +0x7                     ; 46E0
        jnz short L3_470F                               ; 46E4
        mov ax,[0xa6]                                   ; 46E6
        inc word [0xa6]                                 ; 46E9
        or_ ax,ax                                       ; 46ED
        jnz short L3_46F2                               ; 46EF
        cli                                             ; 46F1

L3_46F2:
        mov ax,0xb                                      ; 46F2
        out 0x20,al                                     ; 46F5
        in al,0x20                                      ; 46F7
        in al,0x20                                      ; 46F9
        mov [bp-0x2],al                                 ; 46FB
        or_ al,al                                       ; 46FE
        jz short L3_4708                                ; 4700
        mov dx,0x20                                     ; 4702
        mov_ ax,dx                                      ; 4705
        out dx,al                                       ; 4707

L3_4708:
        dec word [0xa6]                                 ; 4708
        jnz short L3_470F                               ; 470C
        sti                                             ; 470E

L3_470F:
        mov_ sp,bp                                      ; 470F
        pop bp                                          ; 4711
        ret 0x2                                         ; 4712
        db 0x90                                         ; 4715

L3_4716:
        push bp                                         ; 4716
        mov_ bp,sp                                      ; 4717
        mov bx,[bp+0x6]                                 ; 4719
        mov ax,[0xbd2]                                  ; 471C
        mov [bx+0x125],ax                               ; 471F
        mov [0xbd2],bx                                  ; 4723
        pop bp                                          ; 4727
        retf                                            ; 4728
        db 0x90                                         ; 4729

L3_472A:
        push bp                                         ; 472A
        mov_ bp,sp                                      ; 472B
        push si                                         ; 472D
        mov bx,0xbd2                                    ; 472E
        cmp word [bx],byte +0x0                         ; 4731
        jz short L3_4753                                ; 4734
        mov cx,[bp+0x6]                                 ; 4736

L3_4739:
        cmp [bx],cx                                     ; 4739
        jz short L3_474B                                ; 473B
        mov ax,[bx]                                     ; 473D
        add ax,0x125                                    ; 473F
        mov_ bx,ax                                      ; 4742
        cmp word [bx],byte +0x0                         ; 4744
        jnz short L3_4739                               ; 4747
        jmp short L3_4753                               ; 4749

L3_474B:
        mov si,[bx]                                     ; 474B
        mov ax,[si+0x125]                               ; 474D
        mov [bx],ax                                     ; 4751

L3_4753:
        pop si                                          ; 4753
        mov_ sp,bp                                      ; 4754
        pop bp                                          ; 4756
        retf                                            ; 4757

; VxD API 0200 ("ESSFMMXD", "ESMPUISR")
vxd_register_pipe:
        push bp                                         ; 4758
        mov_ bp,sp                                      ; 4759
        sub sp,byte +0x4                                ; 475B
        mov ax,[0x10]                                   ; 475E
        mov dx,[0x12]                                   ; 4761
        mov [bp-0x4],ax                                 ; 4765
        mov [bp-0x2],dx                                 ; 4768
        or_ dx,ax                                       ; 476B
        jnz short L3_4774                               ; 476D
        xor_ ax,ax                                      ; 476F
        cwd                                             ; 4771
        jmp short L3_478F                               ; 4772

L3_4774:
        mov dx,0x200                                    ; 4774
        push word [bp+0xe]                              ; 4777
        push word [bp+0xc]                              ; 477A
        push word [bp+0xa]                              ; 477D
        push word [bp+0x8]                              ; 4780
        mov bx,[bp+0x6]                                 ; 4783
        push word [bx+0x16]                             ; 4786
        push word [bx+0x14]                             ; 4789
        call far [bp-0x4]                               ; 478C

L3_478F:
        mov_ sp,bp                                      ; 478F
        pop bp                                          ; 4791
        retf                                            ; 4792
        db 0x90                                         ; 4793

; VxD API 0201
vxd_unregister_pipe:
        push bp                                         ; 4794
        mov_ bp,sp                                      ; 4795
        sub sp,byte +0x4                                ; 4797
        mov ax,[0x10]                                   ; 479A
        mov dx,[0x12]                                   ; 479D
        mov [bp-0x4],ax                                 ; 47A1
        mov [bp-0x2],dx                                 ; 47A4
        or_ dx,ax                                       ; 47A7
        jz short L3_47C0                                ; 47A9
        mov dx,0x201                                    ; 47AB
        push word [bp+0xa]                              ; 47AE
        push word [bp+0x8]                              ; 47B1
        mov bx,[bp+0x6]                                 ; 47B4
        push word [bx+0x16]                             ; 47B7
        push word [bx+0x14]                             ; 47BA
        call far [bp-0x4]                               ; 47BD

L3_47C0:
        mov_ sp,bp                                      ; 47C0
        pop bp                                          ; 47C2
        retf                                            ; 47C3

L3_47C4:
        push bp                                         ; 47C4
        mov_ bp,sp                                      ; 47C5
        push si                                         ; 47C7
        push word [bp+0x8]                              ; 47C8
        push word [bp+0x6]                              ; 47CB
        push cs                                         ; 47CE
        call L3_4EAE                                    ; 47CF
        mov_ si,ax                                      ; 47D2
        or_ si,ax                                       ; 47D4
        jz short L3_47DD                                ; 47D6
        inc word [si+0x18]                              ; 47D8
        jmp short L3_480C                               ; 47DB

L3_47DD:
        mov ax,0x40                                     ; 47DD
        push ax                                         ; 47E0
%if ES1869_FIX
        mov ax,DEV_FIX_SIZE             ; essreg: the Audio 1 player after it
%else
        mov ax,0x132                                    ; 47E1
%endif
        push ax                                         ; 47E4
        callp R3_47E6, 0xFFFF, 0x0000                   ; 47E5 KERNEL.LocalAlloc
        mov_ si,ax                                      ; 47EA
        or_ si,ax                                       ; 47EC
        jnz short L3_47F5                               ; 47EE
        mov ax,0x7                                      ; 47F0
        jmp short L3_480E                               ; 47F3

L3_47F5:
        mov ax,[bp+0x6]                                 ; 47F5
        mov dx,[bp+0x8]                                 ; 47F8
        mov [si+0x14],ax                                ; 47FB
        mov [si+0x16],dx                                ; 47FE
        mov word [si+0x18],0x1                          ; 4801
        push si                                         ; 4806
        push cs                                         ; 4807
        call L3_4716                                    ; 4808
        pop bx                                          ; 480B

L3_480C:
        xor_ ax,ax                                      ; 480C

L3_480E:
        xor_ dx,dx                                      ; 480E
        pop si                                          ; 4810
        mov_ sp,bp                                      ; 4811
        pop bp                                          ; 4813
        retf 0x4                                        ; 4814
        db 0x90                                         ; 4817

; mixer reset and defaults (6:1F92), Telegaming Vol to 14h, then restore_mixer_state; at enable and resume
hw_init:
        push bp                                         ; 4818
        mov_ bp,sp                                      ; 4819
        push si                                         ; 481B
        mov si,[bp+0x6]                                 ; 481C
        mov word [si+0x20],0x0                          ; 481F
        push si                                         ; 4824
        push word [si]                                  ; 4825
        mov ax,0x1                                      ; 4827
        push ax                                         ; 482A
        callf vxd_acquire, R3_482E, R3_3AA3             ; 482B far seg4
        or_ ax,ax                                       ; 4830
        jnz short L3_48A6                               ; 4832
        push si                                         ; 4834
        callf mixer_reset_defaults, R3_4838, 0xFFFF     ; 4835 far seg6
        test byte [si+0x2a],0x80                        ; 483A
        jz short L3_4846                                ; 483E
        push si                                         ; 4840
        push cs                                         ; 4841
        call L3_48AC                                    ; 4842
        pop bx                                          ; 4845

L3_4846:
        test word [si+0x2c],0x4                         ; 4846
        jz short L3_4854                                ; 484B
        push si                                         ; 484D
        callf L5_0000, R3_4851, R3_3B6C                 ; 484E far seg5
        pop bx                                          ; 4853

L3_4854:
        push si                                         ; 4854
        mov al,0x7d                                     ; 4855
        push ax                                         ; 4857
        mov al,0x6                                      ; 4858
        push ax                                         ; 485A
        callf mixer_write, R3_485E, R3_4870             ; 485B far seg1
        cmp byte [0xc7],0x1                             ; 4860
        jnz short L3_4876                               ; 4865
        push si                                         ; 4867
        mov al,0x7d                                     ; 4868
        push ax                                         ; 486A
        push si                                         ; 486B
        push ax                                         ; 486C
        callf mixer_read, R3_4870, R3_487F              ; 486D far seg1
        and al,0xf7                                     ; 4872
        jmp short L3_4883                               ; 4874

L3_4876:
        push si                                         ; 4876
        mov al,0x7d                                     ; 4877
        push ax                                         ; 4879
        push si                                         ; 487A
        push ax                                         ; 487B
        callf mixer_read, R3_487F, R3_0BD9              ; 487C far seg1
        or al,0x8                                       ; 4881

L3_4883:
        push ax                                         ; 4883
        callf mixer_write, R3_4887, R3_4893             ; 4884 far seg1
        push si                                         ; 4889
        mov al,0x1c                                     ; 488A
        push ax                                         ; 488C
        mov al,0x5                                      ; 488D
        push ax                                         ; 488F
        callf mixer_write, R3_4893, R3_48C3             ; 4890 far seg1
        push si                                         ; 4895
        push cs                                         ; 4896
        call restore_mixer_state                        ; 4897
        push si                                         ; 489A
        push word [si]                                  ; 489B
        mov ax,0x1                                      ; 489D
        push ax                                         ; 48A0
        callf vxd_release, R3_48A4, R3_4B17             ; 48A1 far seg4

L3_48A6:
        pop si                                          ; 48A6
        mov_ sp,bp                                      ; 48A7
        pop bp                                          ; 48A9
        retf                                            ; 48AA
        db 0x90                                         ; 48AB

L3_48AC:
        push bp                                         ; 48AC
        mov_ bp,sp                                      ; 48AD
        sub sp,byte +0x4                                ; 48AF
        push si                                         ; 48B2
        mov si,[bp+0x6]                                 ; 48B3
        mov ax,[si+0x26]                                ; 48B6
        push si                                         ; 48B9
        mov al,0x50                                     ; 48BA
        push ax                                         ; 48BC
        xor_ al,al                                      ; 48BD
        push ax                                         ; 48BF
        callf mixer_write, R3_48C3, R3_48D5             ; 48C0 far seg1
        test byte [si+0x2a],0x80                        ; 48C5
        jz short L3_493B                                ; 48C9
        push si                                         ; 48CB
        mov al,0x50                                     ; 48CC
        push ax                                         ; 48CE
        mov al,0xc                                      ; 48CF
        push ax                                         ; 48D1
        callf mixer_write, R3_48D5, R3_48E6             ; 48D2 far seg1
        test byte [si+0x2b],0x4                         ; 48D7
        jz short L3_48EC                                ; 48DB
        push si                                         ; 48DD
        mov al,0x50                                     ; 48DE
        push ax                                         ; 48E0
        push si                                         ; 48E1
        push ax                                         ; 48E2
        callf mixer_read, R3_48E6, R3_48F5              ; 48E3 far seg1
        or al,0x1                                       ; 48E8
        jmp short L3_48F9                               ; 48EA

L3_48EC:
        push si                                         ; 48EC
        mov al,0x50                                     ; 48ED
        push ax                                         ; 48EF
        push si                                         ; 48F0
        push ax                                         ; 48F1
        callf mixer_read, R3_48F5, R3_48FD              ; 48F2 far seg1
        and al,0xfe                                     ; 48F7

L3_48F9:
        push ax                                         ; 48F9
        callf mixer_write, R3_48FD, R3_4909             ; 48FA far seg1
        push si                                         ; 48FF
        mov al,0x52                                     ; 4900
        push ax                                         ; 4902
        mov al,0x3f                                     ; 4903
        push ax                                         ; 4905
        callf mixer_write, R3_4909, R3_4915             ; 4906 far seg1
        push si                                         ; 490B
        mov al,0x54                                     ; 490C
        push ax                                         ; 490E
        mov al,0x8f                                     ; 490F
        push ax                                         ; 4911
        callf mixer_write, R3_4915, R3_4921             ; 4912 far seg1
        push si                                         ; 4917
        mov al,0x56                                     ; 4918
        push ax                                         ; 491A
        mov al,0x95                                     ; 491B
        push ax                                         ; 491D
        callf mixer_write, R3_4921, R3_492D             ; 491E far seg1
        push si                                         ; 4923
        mov al,0x58                                     ; 4924
        push ax                                         ; 4926
        mov al,0x94                                     ; 4927
        push ax                                         ; 4929
        callf mixer_write, R3_492D, R3_4939             ; 492A far seg1
        push si                                         ; 492F
        mov al,0x5a                                     ; 4930
        push ax                                         ; 4932
        mov al,0x80                                     ; 4933
        push ax                                         ; 4935
        callf mixer_write, R3_4939, R3_4AEF             ; 4936 far seg1

L3_493B:
        pop si                                          ; 493B
        mov_ sp,bp                                      ; 493C
        pop bp                                          ; 493E
        retf                                            ; 493F

L3_4940:
        push bp                                         ; 4940
        mov_ bp,sp                                      ; 4941
        sub sp,byte +0x8                                ; 4943
        push si                                         ; 4946
        push word [bp+0x8]                              ; 4947
        push word [bp+0x6]                              ; 494A
        push cs                                         ; 494D
        call L3_4EAE                                    ; 494E
        mov_ si,ax                                      ; 4951
        or_ si,ax                                       ; 4953
        jnz short L3_495D                               ; 4955
        mov ax,0x5                                      ; 4957
        jmp near L3_4C33                                ; 495A

L3_495D:
        mov ax,[si+0x1a]                                ; 495D
        inc word [si+0x1a]                              ; 4960
        or_ ax,ax                                       ; 4963
        jz short L3_496A                                ; 4965
        jmp near L3_4C31                                ; 4967

L3_496A:
        push si                                         ; 496A
        push word [bp+0x8]                              ; 496B
        push word [bp+0x6]                              ; 496E
        call vxd_get_adi                                ; 4971
        or_ ax,ax                                       ; 4974
        jnz short L3_4981                               ; 4976
        dec word [si+0x1a]                              ; 4978
        mov ax,0x2                                      ; 497B
        jmp near L3_4C33                                ; 497E

L3_4981:
        mov ax,[si+0x12]                                ; 4981
        mov word [si+0x41],0x0                          ; 4984
        mov [si+0x43],ax                                ; 4989
        mov ax,[si+0xa]                                 ; 498C
        mov [si+0x45],ax                                ; 498F
        mov al,[si+0xc]                                 ; 4992
        mov [si+0x47],al                                ; 4995
        mov ax,[si+0xe]                                 ; 4998
        dec ax                                          ; 499B
        mov [si+0x3b],ax                                ; 499C
        cmp byte [si+0x5],0x7                           ; 499F
        jna short L3_49B0                               ; 49A3
        mov al,[si+0x5]                                 ; 49A5
        and ax,strict word 0x7                          ; 49A8
        or ax,0x6260                                    ; 49AB
        jmp short L3_49B9                               ; 49AE

L3_49B0:
        mov al,[si+0x5]                                 ; 49B0
        or al,0x60                                      ; 49B3
        mov_ ah,al                                      ; 49B5
        sub_ al,al                                      ; 49B7

L3_49B9:
        mov [si+0x5b],ax                                ; 49B9
        mov bl,[si+0x6]                                 ; 49BC
        and bx,byte +0x7                                ; 49BF
        mov al,[bx+0xca]                                ; 49C2
        mov [si+0x34],al                                ; 49C6
        mov al,[si+0x6]                                 ; 49C9
        and al,0x3                                      ; 49CC
        mov [si+0x35],al                                ; 49CE
        add al,0x4                                      ; 49D1
        mov [si+0x36],al                                ; 49D3
        mov al,[si+0x35]                                ; 49D6
        add al,0x58                                     ; 49D9
        mov [si+0x38],al                                ; 49DB
        mov al,[si+0x35]                                ; 49DE
        add al,0x54                                     ; 49E1
        mov [si+0x37],al                                ; 49E3
        cmp byte [si+0x6],0x3                           ; 49E6
        ja short L3_4A0D                                ; 49EA
        mov al,[si+0x6]                                 ; 49EC
        and al,0x3                                      ; 49EF
        add_ al,al                                      ; 49F1
        mov [si+0x2f],al                                ; 49F3
        inc al                                          ; 49F6
        mov [si+0x30],al                                ; 49F8
        mov byte [si+0x31],0xa                          ; 49FB
        mov byte [si+0x32],0xb                          ; 49FF
        mov byte [si+0x33],0xc                          ; 4A03
        and byte [si+0x2a],0xbf                         ; 4A07
        jmp short L3_4A33                               ; 4A0B

L3_4A0D:
        mov al,[si+0x6]                                 ; 4A0D
        and al,0x3                                      ; 4A10
        add_ al,al                                      ; 4A12
        add_ al,al                                      ; 4A14
        mov_ cx,ax                                      ; 4A16
        sub al,0x40                                     ; 4A18
        mov [si+0x2f],al                                ; 4A1A
        sub cl,0x3e                                     ; 4A1D
        mov [si+0x30],cl                                ; 4A20
        mov byte [si+0x31],0xd4                         ; 4A23
        mov byte [si+0x32],0xd6                         ; 4A27
        mov byte [si+0x33],0xd8                         ; 4A2B
        or byte [si+0x2a],0x40                          ; 4A2F

L3_4A33:
        mov ax,[si+0xdd]                                ; 4A33
        mov word [si+0xf5],0x0                          ; 4A37
        mov [si+0xf7],ax                                ; 4A3D
        mov ax,[si+0xd5]                                ; 4A41
        mov [si+0xf9],ax                                ; 4A45
        mov al,[si+0xd7]                                ; 4A49
        mov [si+0xfb],al                                ; 4A4D
        mov ax,[si+0xd9]                                ; 4A51
        dec ax                                          ; 4A55
        mov [si+0xef],ax                                ; 4A56
        mov bl,[si+0xd4]                                ; 4A5A
        and bx,byte +0x7                                ; 4A5E
        mov al,[bx+0xca]                                ; 4A61
        mov [si+0xe8],al                                ; 4A65
        mov al,[si+0xd4]                                ; 4A69
        and al,0x3                                      ; 4A6D
        mov [si+0xe9],al                                ; 4A6F
        add al,0x4                                      ; 4A73
        mov [si+0xea],al                                ; 4A75
        mov al,[si+0xe9]                                ; 4A79
        add al,0x58                                     ; 4A7D
        mov [si+0xec],al                                ; 4A7F
        mov al,[si+0xe9]                                ; 4A83
        add al,0x54                                     ; 4A87
        mov [si+0xeb],al                                ; 4A89
        cmp byte [si+0xd4],0x3                          ; 4A8D
        ja short L3_4ABB                                ; 4A92
        mov al,[si+0xd4]                                ; 4A94
        and al,0x3                                      ; 4A98
        add_ al,al                                      ; 4A9A
        mov [si+0xe3],al                                ; 4A9C
        inc al                                          ; 4AA0
        mov [si+0xe4],al                                ; 4AA2
        mov byte [si+0xe5],0xa                          ; 4AA6
        mov byte [si+0xe6],0xb                          ; 4AAB
        mov byte [si+0xe7],0xc                          ; 4AB0
        and byte [si+0x2a],0xbf                         ; 4AB5
        jmp short L3_4AE7                               ; 4AB9

L3_4ABB:
        mov al,[si+0xd4]                                ; 4ABB
        and al,0x3                                      ; 4ABF
        add_ al,al                                      ; 4AC1
        add_ al,al                                      ; 4AC3
        mov_ cx,ax                                      ; 4AC5
        sub al,0x40                                     ; 4AC7
        mov [si+0xe3],al                                ; 4AC9
        sub cl,0x3e                                     ; 4ACD
        mov [si+0xe4],cl                                ; 4AD0
        mov byte [si+0xe5],0xd4                         ; 4AD4
        mov byte [si+0xe6],0xd6                         ; 4AD9
        mov byte [si+0xe7],0xd8                         ; 4ADE
        or byte [si+0x2a],0x40                          ; 4AE3

L3_4AE7:
        mov word [si+0x5e],0x1a8e                       ; 4AE7
        db 0xC7, 0x44, 0x60                             ; 4AEC mov word [si+0x60],0x4bd8
R3_4AEF: dw R3_4BD8                                     ; seg1
        mov [si+0x62],si                                ; 4AF1
        push si                                         ; 4AF4
        call isr_install                                ; 4AF5
        or_ ax,ax                                       ; 4AF8
        jz short L3_4B04                                ; 4AFA
        push si                                         ; 4AFC
        call L3_030F                                    ; 4AFD
        or_ ax,ax                                       ; 4B00
        jnz short L3_4B0D                               ; 4B02

L3_4B04:
        dec word [si+0x1a]                              ; 4B04
        mov ax,0x3                                      ; 4B07
        jmp near L3_4C33                                ; 4B0A

L3_4B0D:
        push si                                         ; 4B0D
        push word [si]                                  ; 4B0E
        mov ax,0x1                                      ; 4B10
        push ax                                         ; 4B13
        callf vxd_acquire, R3_4B17, R3_4B4D             ; 4B14 far seg4
        mov al,[si+0x5]                                 ; 4B19
        push ax                                         ; 4B1C
        mov dx,[si+0x53]                                ; 4B1D
        sub_ cx,cx                                      ; 4B20
        push dx                                         ; 4B22
        push cx                                         ; 4B23
        call L3_006A                                    ; 4B24
        mov [si+0x57],ax                                ; 4B27
        mov [si+0x59],dx                                ; 4B2A
        mov al,[si+0x5]                                 ; 4B2D
        push ax                                         ; 4B30
        xor_ al,al                                      ; 4B31
        push ax                                         ; 4B33
        call L3_001D                                    ; 4B34
        mov [si+0x56],al                                ; 4B37
        mov al,[si+0x5]                                 ; 4B3A
        sub_ ah,ah                                      ; 4B3D
        push ax                                         ; 4B3F
        call L3_46DA                                    ; 4B40
        push si                                         ; 4B43
        push word [si]                                  ; 4B44
        mov ax,0x1                                      ; 4B46
        push ax                                         ; 4B49
        callf vxd_release, R3_4B4D, R3_4B9E             ; 4B4A far seg4
        mov word [si+0x1c],0x1                          ; 4B4F
        push si                                         ; 4B54
        push cs                                         ; 4B55
%if ES1869_FIX
        call es_read_config             ; essreg: then SYSTEM.INI
%else
        call read_config                                ; 4B56
%endif
        push si                                         ; 4B59
        callf L5_00D6, R3_4B5D, R3_4B71                 ; 4B5A far seg5
        mov word [bp-0x8],0x8                           ; 4B5F
        mov word [bp-0x6],0x0                           ; 4B64
        mov word [bp-0x4],0x1d6e                        ; 4B69
        db 0xC7, 0x46, 0xFE                             ; 4B6E mov word [bp-0x2],0x4b97
R3_4B71: dw R3_4B97                                     ; seg5
        lea ax,[bp-0x8]                                 ; 4B73
        push ss                                         ; 4B76
        push ax                                         ; 4B77
        mov ax,0xfe                                     ; 4B78
        push ds                                         ; 4B7B
        push ax                                         ; 4B7C
        push si                                         ; 4B7D
        push cs                                         ; 4B7E
        call vxd_register_pipe                          ; 4B7F
        add sp,byte +0xa                                ; 4B82
        mov [si+0x10f],ax                               ; 4B85
        mov [si+0x111],dx                               ; 4B89
        push word [bp+0x8]                              ; 4B8D
        push word [bp+0x6]                              ; 4B90
        mov ax,0x20de                                   ; 4B93
        movsel dx, R3_4B97, R3_4851                     ; 4B96 seg5
        push dx                                         ; 4B99
        push ax                                         ; 4B9A
        callf vxd_hwvol_callback, R3_4B9E, R3_4BB1      ; 4B9B far seg4
        push word [bp+0x8]                              ; 4BA0
        push word [bp+0x6]                              ; 4BA3
        mov ax,0x4c3c                                   ; 4BA6
        movsel dx, R3_4BAA, R3_4C15                     ; 4BA9 seg3
        push dx                                         ; 4BAC
        push ax                                         ; 4BAD
        callf vxd_dsp_callback, R3_4BB1, R3_4BC9        ; 4BAE far seg4
        push si                                         ; 4BB3
        push cs                                         ; 4BB4
        call hw_init                                    ; 4BB5
        pop bx                                          ; 4BB8
        test byte [si+0x2b],0x40                        ; 4BB9
        jnz short L3_4BF3                               ; 4BBD
        push si                                         ; 4BBF
        push word [si]                                  ; 4BC0
        mov ax,0x1                                      ; 4BC2
        push ax                                         ; 4BC5
        callf vxd_acquire, R3_4BC9, R3_4BF1             ; 4BC6 far seg4
        or_ ax,ax                                       ; 4BCB
        jnz short L3_4BF3                               ; 4BCD
        push si                                         ; 4BCF
        mov al,0x7f                                     ; 4BD0
        push ax                                         ; 4BD2
        push si                                         ; 4BD3
        push ax                                         ; 4BD4
        callf mixer_read, R3_4BD8, R3_4BE0              ; 4BD5 far seg1
        or al,0x1                                       ; 4BDA
        push ax                                         ; 4BDC
        callf mixer_write, R3_4BE0, R3_485E             ; 4BDD far seg1
        mov byte [si+0x117],0x0                         ; 4BE2
        push si                                         ; 4BE7
        push word [si]                                  ; 4BE8
        mov ax,0x1                                      ; 4BEA
        push ax                                         ; 4BED
        callf vxd_release, R3_4BF1, R3_482E             ; 4BEE far seg4

L3_4BF3:
        cmp word [si+0x2],byte -0x1                     ; 4BF3
        jz short L3_4C31                                ; 4BF7
        cmp byte [si+0x4],0xff                          ; 4BF9
        jnz short L3_4C31                               ; 4BFD
        or byte [si+0x2b],0x20                          ; 4BFF
        mov word [bp-0x8],0x8                           ; 4C03
        mov word [bp-0x6],0x0                           ; 4C08
        mov word [bp-0x4],0x4ee6                        ; 4C0D
        db 0xC7, 0x46, 0xFE                             ; 4C12 mov word [bp-0x2],0x4563
R3_4C15: dw R3_4563                                     ; seg3
        lea ax,[bp-0x8]                                 ; 4C17
        push ss                                         ; 4C1A
        push ax                                         ; 4C1B
        mov ax,0x107                                    ; 4C1C
        push ds                                         ; 4C1F
        push ax                                         ; 4C20
        push si                                         ; 4C21
        push cs                                         ; 4C22
        call vxd_register_pipe                          ; 4C23
        add sp,byte +0xa                                ; 4C26
        mov [si+0x118],ax                               ; 4C29
        mov [si+0x11a],dx                               ; 4C2D

L3_4C31:
        xor_ ax,ax                                      ; 4C31

L3_4C33:
        xor_ dx,dx                                      ; 4C33
        pop si                                          ; 4C35
        mov_ sp,bp                                      ; 4C36
        pop bp                                          ; 4C38
        retf 0x4                                        ; 4C39

; the VxD's 000A callback: with a pointer, both channels in use (Audio 1 user 2, Audio 2 user 1, +127h); without, both free
dsp_busy_callback:
        push bp                                         ; 4C3C
        mov_ bp,sp                                      ; 4C3D
        push si                                         ; 4C3F
        push ds                                         ; 4C40
        movsel ax, R3_4C42, R3_4EEC                     ; 4C41 seg7
        mov ds,ax                                       ; 4C44
        push word [bp+0xc]                              ; 4C46
        push word [bp+0xa]                              ; 4C49
        push cs                                         ; 4C4C
        call L3_4EAE                                    ; 4C4D
        mov_ si,ax                                      ; 4C50
        or_ si,ax                                       ; 4C52
        jz short L3_4C81                                ; 4C54
        mov ax,[bp+0x14]                                ; 4C56
        or ax,[bp+0x12]                                 ; 4C59
        jz short L3_4C71                                ; 4C5C
        mov byte [si+0x5d],0x2                          ; 4C5E
        mov al,0x1                                      ; 4C62
        mov [si+0x100],al                               ; 4C64
        mov [si+0x127],al                               ; 4C68
        add [si+0x2e],al                                ; 4C6C
        jmp short L3_4C81                               ; 4C6F

L3_4C71:
        xor_ al,al                                      ; 4C71
        mov [si+0x5d],al                                ; 4C73
        mov [si+0x100],al                               ; 4C76
        mov [si+0x127],al                               ; 4C7A
        dec byte [si+0x2e]                              ; 4C7E

L3_4C81:
        pop ds                                          ; 4C81
        pop si                                          ; 4C82
        mov_ sp,bp                                      ; 4C83
        pop bp                                          ; 4C85
        retf 0x10                                       ; 4C86
        db 0x90                                         ; 4C89

; DRV_POWER, suspend: every enabled device
power_suspend:
        push si                                         ; 4C8A
        mov si,[0xbd2]                                  ; 4C8B
        or_ si,si                                       ; 4C8F
        jz short L3_4CA6                                ; 4C91

L3_4C93:
        cmp word [si+0x1c],byte +0x0                    ; 4C93
        jz short L3_4C9E                                ; 4C97
        push si                                         ; 4C99
        push cs                                         ; 4C9A
        call save_mixer_state                           ; 4C9B

L3_4C9E:
        mov si,[si+0x125]                               ; 4C9E
        or_ si,si                                       ; 4CA2
        jnz short L3_4C93                               ; 4CA4

L3_4CA6:
        pop si                                          ; 4CA6
        retf                                            ; 4CA7

; DRV_POWER, resume: hw_init, then the wave devices' suspend and resume
power_resume:
        push si                                         ; 4CA8
        mov si,[0xbd2]                                  ; 4CA9
        or_ si,si                                       ; 4CAD
        jz short L3_4D28                                ; 4CAF

L3_4CB1:
        cmp word [si+0x1c],byte +0x0                    ; 4CB1
        jz short L3_4D20                                ; 4CB5
        push si                                         ; 4CB7
        push cs                                         ; 4CB8
        call hw_init                                    ; 4CB9
        pop bx                                          ; 4CBC
        push si                                         ; 4CBD
        push word [si]                                  ; 4CBE
        mov ax,0x1                                      ; 4CC0
        push ax                                         ; 4CC3
        callf vxd_acquire, R3_4CC7, R3_4D06             ; 4CC4 far seg4
        or_ ax,ax                                       ; 4CC9
        jnz short L3_4D08                               ; 4CCB
        cmp byte [si+0x117],0x0                         ; 4CCD
        jz short L3_4CE3                                ; 4CD2
        push si                                         ; 4CD4
        mov al,0x7f                                     ; 4CD5
        push ax                                         ; 4CD7
        push si                                         ; 4CD8
        push ax                                         ; 4CD9
        callf mixer_read, R3_4CDD, R3_4CF2              ; 4CDA far seg1
        and al,0xfe                                     ; 4CDF
        jmp short L3_4CF6                               ; 4CE1

L3_4CE3:
        test byte [si+0x2b],0x40                        ; 4CE3
        jnz short L3_4CFC                               ; 4CE7
        push si                                         ; 4CE9
        mov al,0x7f                                     ; 4CEA
        push ax                                         ; 4CEC
        push si                                         ; 4CED
        push ax                                         ; 4CEE
        callf mixer_read, R3_4CF2, R3_4CFA              ; 4CEF far seg1
        or al,0x1                                       ; 4CF4

L3_4CF6:
        push ax                                         ; 4CF6
        callf mixer_write, R3_4CFA, R3_4E23             ; 4CF7 far seg1

L3_4CFC:
        push si                                         ; 4CFC
        push word [si]                                  ; 4CFD
        mov ax,0x1                                      ; 4CFF
        push ax                                         ; 4D02
        callf vxd_release, R3_4D06, R3_4DD5             ; 4D03 far seg4

L3_4D08:
        push si                                         ; 4D08
        callf wod_suspend, R3_4D0C, R3_4D12             ; 4D09 far seg6
        push si                                         ; 4D0E
        callf L6_0D0E, R3_4D12, R3_4D18                 ; 4D0F far seg6
        push si                                         ; 4D14
        callf wod_resume, R3_4D18, R3_4D1E              ; 4D15 far seg6
        push si                                         ; 4D1A
%if ES1869_FIX
        callf es_wid_resume, R3_4D1E, R3_4838   ; essreg: and the Audio 1 player
%else
        callf L6_0D40, R3_4D1E, R3_4838                 ; 4D1B far seg6
%endif

L3_4D20:
        mov si,[si+0x125]                               ; 4D20
        or_ si,si                                       ; 4D24
        jnz short L3_4CB1                               ; 4D26

L3_4D28:
        pop si                                          ; 4D28
        retf                                            ; 4D29
        db 0x90, 0x90                                   ; 4D2A

L3_4D2C:
        push bp                                         ; 4D2C
        mov_ bp,sp                                      ; 4D2D
        push si                                         ; 4D2F
        push word [bp+0x8]                              ; 4D30
        push word [bp+0x6]                              ; 4D33
        push cs                                         ; 4D36
        call L3_4EAE                                    ; 4D37
        mov_ si,ax                                      ; 4D3A
        or_ si,ax                                       ; 4D3C
        jnz short L3_4D46                               ; 4D3E
        mov ax,0x5                                      ; 4D40
        jmp near L3_4E5F                                ; 4D43

L3_4D46:
        dec word [si+0x1a]                              ; 4D46
        jz short L3_4D4E                                ; 4D49
        jmp near L3_4E5D                                ; 4D4B

L3_4D4E:
        mov al,[si+0x5]                                 ; 4D4E
        push ax                                         ; 4D51
        mov al,0x1                                      ; 4D52
        push ax                                         ; 4D54
        call L3_001D                                    ; 4D55
        mov al,[si+0x5]                                 ; 4D58
        push ax                                         ; 4D5B
        push word [si+0x59]                             ; 4D5C
        push word [si+0x57]                             ; 4D5F
        call L3_006A                                    ; 4D62
        mov al,[si+0x5]                                 ; 4D65
        push ax                                         ; 4D68
        mov al,[si+0x56]                                ; 4D69
        push ax                                         ; 4D6C
        call L3_001D                                    ; 4D6D
        push word [si+0x51]                             ; 4D70
        callp R3_4D74, R3_4D8D, 0x0000                  ; 4D73 KERNEL.GlobalFree
        push word [si+0x53]                             ; 4D78
        callp R3_4D7C, R3_4D96, 0x0000                  ; 4D7B KERNEL.FreeSelector
        xor_ ax,ax                                      ; 4D80
        mov [si+0x51],ax                                ; 4D82
        mov [si+0x53],ax                                ; 4D85
        push word [si+0xfc]                             ; 4D88
        callp R3_4D8D, 0xFFFF, 0x0000                   ; 4D8C KERNEL.GlobalFree
        push word [si+0xfe]                             ; 4D91
        callp R3_4D96, 0xFFFF, 0x0000                   ; 4D95 KERNEL.FreeSelector
        xor_ ax,ax                                      ; 4D9A
        mov [si+0xfc],ax                                ; 4D9C
        mov [si+0xfe],ax                                ; 4DA0
        mov ax,[si+0x11a]                               ; 4DA4
        or ax,[si+0x118]                                ; 4DA8
        jz short L3_4DC8                                ; 4DAC
        push word [si+0x11a]                            ; 4DAE
        push word [si+0x118]                            ; 4DB2
        push si                                         ; 4DB6
        push cs                                         ; 4DB7
        call vxd_unregister_pipe                        ; 4DB8
        add sp,byte +0x6                                ; 4DBB
        sub_ ax,ax                                      ; 4DBE
        mov [si+0x11a],ax                               ; 4DC0
        mov [si+0x118],ax                               ; 4DC4

L3_4DC8:
        push word [bp+0x8]                              ; 4DC8
        push word [bp+0x6]                              ; 4DCB
        sub_ ax,ax                                      ; 4DCE
        push ax                                         ; 4DD0
        push ax                                         ; 4DD1
        callf vxd_hwvol_callback, R3_4DD5, R3_4DE4      ; 4DD2 far seg4
        push word [bp+0x8]                              ; 4DD7
        push word [bp+0x6]                              ; 4DDA
        sub_ ax,ax                                      ; 4DDD
        push ax                                         ; 4DDF
        push ax                                         ; 4DE0
        callf vxd_dsp_callback, R3_4DE4, R3_4E19        ; 4DE1 far seg4
        push si                                         ; 4DE6
        push cs                                         ; 4DE7
        call save_mixer_state                           ; 4DE8
        mov ax,[si+0x111]                               ; 4DEB
        or ax,[si+0x10f]                                ; 4DEF
        jz short L3_4E0F                                ; 4DF3
        push word [si+0x111]                            ; 4DF5
        push word [si+0x10f]                            ; 4DF9
        push si                                         ; 4DFD
        push cs                                         ; 4DFE
        call vxd_unregister_pipe                        ; 4DFF
        add sp,byte +0x6                                ; 4E02
        sub_ ax,ax                                      ; 4E05
        mov [si+0x111],ax                               ; 4E07
        mov [si+0x10f],ax                               ; 4E0B

L3_4E0F:
        push si                                         ; 4E0F
        push word [si]                                  ; 4E10
        mov ax,0x1                                      ; 4E12
        push ax                                         ; 4E15
        callf vxd_acquire, R3_4E19, R3_4E48             ; 4E16 far seg4
        or_ ax,ax                                       ; 4E1B
        jnz short L3_4E4A                               ; 4E1D
        push si                                         ; 4E1F
%if ES1869_FIX
        callf a1_disable, R3_4E23, R3_4E29      ; essreg: and the Audio 1 player
%else
        callf L1_1602, R3_4E23, R3_4E29                 ; 4E20 far seg1
%endif
        push si                                         ; 4E25
        callf audio2_stop, R3_4E29, R3_4E34             ; 4E26 far seg1
        push si                                         ; 4E2B
        mov al,0x7f                                     ; 4E2C
        push ax                                         ; 4E2E
        push si                                         ; 4E2F
        push ax                                         ; 4E30
        callf mixer_read, R3_4E34, R3_4E3C              ; 4E31 far seg1
        and al,0xfe                                     ; 4E36
        push ax                                         ; 4E38
        callf mixer_write, R3_4E3C, R3_4F7A             ; 4E39 far seg1
        push si                                         ; 4E3E
        push word [si]                                  ; 4E3F
        mov ax,0x1                                      ; 4E41
        push ax                                         ; 4E44
        callf vxd_release, R3_4E48, R3_4F5C             ; 4E45 far seg4

L3_4E4A:
        mov word [si+0x1c],0x0                          ; 4E4A
        test word [si+0x2c],0x4                         ; 4E4F
        jz short L3_4E5D                                ; 4E54
        push si                                         ; 4E56
        callf L5_00B4, R3_4E5A, R3_4E95                 ; 4E57 far seg5
        pop bx                                          ; 4E5C

L3_4E5D:
        xor_ ax,ax                                      ; 4E5D

L3_4E5F:
        xor_ dx,dx                                      ; 4E5F
        pop si                                          ; 4E61
        mov_ sp,bp                                      ; 4E62
        pop bp                                          ; 4E64
        retf 0x4                                        ; 4E65

L3_4E68:
        push bp                                         ; 4E68
        mov_ bp,sp                                      ; 4E69
        push si                                         ; 4E6B
        push word [bp+0x8]                              ; 4E6C
        push word [bp+0x6]                              ; 4E6F
        push cs                                         ; 4E72
        call L3_4EAE                                    ; 4E73
        mov_ si,ax                                      ; 4E76
        or_ si,ax                                       ; 4E78
        jnz short L3_4E81                               ; 4E7A
        mov ax,0x5                                      ; 4E7C
        jmp short L3_4EA5                               ; 4E7F

L3_4E81:
        dec word [si+0x18]                              ; 4E81
        jnz short L3_4EA3                               ; 4E84
        cmp word [si+0x1a],byte +0x0                    ; 4E86
        jz short L3_4E91                                ; 4E8A
        mov ax,0x4                                      ; 4E8C
        jmp short L3_4EA5                               ; 4E8F

L3_4E91:
        push si                                         ; 4E91
        callf L5_04D8, R3_4E95, R3_4F6F                 ; 4E92 far seg5
        push si                                         ; 4E97
        push cs                                         ; 4E98
        call L3_472A                                    ; 4E99
        pop bx                                          ; 4E9C
        push si                                         ; 4E9D
        callp R3_4E9F, 0xFFFF, 0x0000                   ; 4E9E KERNEL.LocalFree

L3_4EA3:
        xor_ ax,ax                                      ; 4EA3

L3_4EA5:
        xor_ dx,dx                                      ; 4EA5
        pop si                                          ; 4EA7
        mov_ sp,bp                                      ; 4EA8
        pop bp                                          ; 4EAA
        retf 0x4                                        ; 4EAB

L3_4EAE:
        push bp                                         ; 4EAE
        mov_ bp,sp                                      ; 4EAF
        cmp word [0xbd2],byte +0x0                      ; 4EB1
        jz short L3_4EDE                                ; 4EB6
        mov bx,[0xbd2]                                  ; 4EB8
        or_ bx,bx                                       ; 4EBC
        jz short L3_4EDE                                ; 4EBE

L3_4EC0:
        mov ax,[bx+0x14]                                ; 4EC0
        mov dx,[bx+0x16]                                ; 4EC3
        cmp [bp+0x6],ax                                 ; 4EC6
        jnz short L3_4ED0                               ; 4EC9
        cmp [bp+0x8],dx                                 ; 4ECB
        jz short L3_4EDA                                ; 4ECE

L3_4ED0:
        mov bx,[bx+0x125]                               ; 4ED0
        or_ bx,bx                                       ; 4ED4
        jnz short L3_4EC0                               ; 4ED6
        jmp short L3_4EDE                               ; 4ED8

L3_4EDA:
        mov_ ax,bx                                      ; 4EDA
        jmp short L3_4EE0                               ; 4EDC

L3_4EDE:
        xor_ ax,ax                                      ; 4EDE

L3_4EE0:
        mov_ sp,bp                                      ; 4EE0
        pop bp                                          ; 4EE2
        retf 0x4                                        ; 4EE3

; "ESMPUISR" pipe callback
mpu_isr_callback:
        push bp                                         ; 4EE6
        mov_ bp,sp                                      ; 4EE7
        push si                                         ; 4EE9
        push ds                                         ; 4EEA
        movsel ax, R3_4EEC, R3_4582                     ; 4EEB seg7
        mov ds,ax                                       ; 4EEE
        push word [bp+0xc]                              ; 4EF0
        push word [bp+0xa]                              ; 4EF3
        push cs                                         ; 4EF6
        call L3_4EAE                                    ; 4EF7
        mov_ si,ax                                      ; 4EFA
        or_ si,ax                                       ; 4EFC
        jnz short L3_4F06                               ; 4EFE
        mov ax,0xfffe                                   ; 4F00
        jmp near L3_5008                                ; 4F03

L3_4F06:
        mov ax,[bp+0xe]                                 ; 4F06
        mov dx,[bp+0x10]                                ; 4F09
        dec dx                                          ; 4F0C
        jnz short L3_4F13                               ; 4F0D
        or_ ax,ax                                       ; 4F0F
        jmp short L3_4F22                               ; 4F11

L3_4F13:
        dec dx                                          ; 4F13
        jnz short L3_4F2A                               ; 4F14
        or_ ax,ax                                       ; 4F16
        jz short L3_4F3A                                ; 4F18
        dec ax                                          ; 4F1A
        jnz short L3_4F20                               ; 4F1B
        jmp near L3_4FE4                                ; 4F1D

L3_4F20:
        dec ax                                          ; 4F20
        dec ax                                          ; 4F21

L3_4F22:
        jnz short L3_4F27                               ; 4F22
        jmp near L3_4FEC                                ; 4F24

L3_4F27:
        jmp near L3_5006                                ; 4F27

L3_4F2A:
        sub ax,strict word 0x0                          ; 4F2A
        sbb dx,byte +0x2                                ; 4F2D
        or_ ax,dx                                       ; 4F30
        jnz short L3_4F37                               ; 4F32
        jmp near L3_4FFC                                ; 4F34

L3_4F37:
        jmp near L3_5006                                ; 4F37

L3_4F3A:
        mov ax,[bp+0x8]                                 ; 4F3A
        or ax,[bp+0x6]                                  ; 4F3D
        jz short L3_4F97                                ; 4F40
        mov ax,[bp+0x6]                                 ; 4F42
        mov dx,[bp+0x8]                                 ; 4F45
        mov [si+0x5e],ax                                ; 4F48
        mov [si+0x60],dx                                ; 4F4B
        or byte [si+0x2b],0x10                          ; 4F4E
        push si                                         ; 4F52
        push word [si]                                  ; 4F53
        mov ax,0x1                                      ; 4F55
        push ax                                         ; 4F58
        callf vxd_acquire, R3_4F5C, R3_4F93             ; 4F59 far seg4
        or_ ax,ax                                       ; 4F5E
        jz short L3_4F65                                ; 4F60
        jmp near L3_5006                                ; 4F62

L3_4F65:
        push si                                         ; 4F65
        mov ax,0x7                                      ; 4F66
        cwd                                             ; 4F69
        push dx                                         ; 4F6A
        push ax                                         ; 4F6B
        callf L5_3D72, R3_4F6F, R3_4FC8                 ; 4F6C far seg5
        push si                                         ; 4F71
        mov al,0x7f                                     ; 4F72
        push ax                                         ; 4F74
        push si                                         ; 4F75
        push ax                                         ; 4F76
        callf mixer_read, R3_4F7A, R3_4F82              ; 4F77 far seg1
        and al,0xfe                                     ; 4F7C
        push ax                                         ; 4F7E
        callf mixer_write, R3_4F82, R3_4F9F             ; 4F7F far seg1
        mov byte [si+0x117],0x1                         ; 4F84

L3_4F89:
        push si                                         ; 4F89
        push word [si]                                  ; 4F8A
        mov ax,0x1                                      ; 4F8C
        push ax                                         ; 4F8F
        callf vxd_release, R3_4F93, R3_4FB8             ; 4F90 far seg4
        jmp short L3_5006                               ; 4F95

L3_4F97:
        mov word [si+0x5e],0x1a8e                       ; 4F97
        db 0xC7, 0x44, 0x60                             ; 4F9C mov word [si+0x60],0x4fd3
R3_4F9F: dw R3_4FD3                                     ; seg1
        mov [si+0x62],si                                ; 4FA1
        and byte [si+0x2b],0xef                         ; 4FA4
        test byte [si+0x2b],0x40                        ; 4FA8
        jnz short L3_5006                               ; 4FAC
        push si                                         ; 4FAE
        push word [si]                                  ; 4FAF
        mov ax,0x1                                      ; 4FB1
        push ax                                         ; 4FB4
        callf vxd_acquire, R3_4FB8, R3_48A4             ; 4FB5 far seg4
        or_ ax,ax                                       ; 4FBA
        jnz short L3_5006                               ; 4FBC
        push si                                         ; 4FBE
        mov ax,0x9                                      ; 4FBF
        cwd                                             ; 4FC2
        push dx                                         ; 4FC3
        push ax                                         ; 4FC4
        callf L5_3D72, R3_4FC8, R3_4B5D                 ; 4FC5 far seg5
        push si                                         ; 4FCA
        mov al,0x7f                                     ; 4FCB
        push ax                                         ; 4FCD
        push si                                         ; 4FCE
        push ax                                         ; 4FCF
        callf mixer_read, R3_4FD3, R3_4FDB              ; 4FD0 far seg1
        or al,0x1                                       ; 4FD5
        push ax                                         ; 4FD7
        callf mixer_write, R3_4FDB, R3_4887             ; 4FD8 far seg1
        mov byte [si+0x117],0x0                         ; 4FDD
        jmp short L3_4F89                               ; 4FE2

L3_4FE4:
        mov ax,[bp+0x6]                                 ; 4FE4
        mov [si+0x62],ax                                ; 4FE7
        jmp short L3_5006                               ; 4FEA

L3_4FEC:
        mov ax,[bp+0x6]                                 ; 4FEC
        mov dx,[bp+0x8]                                 ; 4FEF
        mov [si+0x11c],ax                               ; 4FF2
        mov [si+0x11e],dx                               ; 4FF6
        jmp short L3_5006                               ; 4FFA

L3_4FFC:
        sub_ ax,ax                                      ; 4FFC
        mov [si+0x11e],ax                               ; 4FFE
        mov [si+0x11c],ax                               ; 5002

L3_5006:
        xor_ ax,ax                                      ; 5006

L3_5008:
        cwd                                             ; 5008
        pop ds                                          ; 5009
        pop si                                          ; 500A
        mov_ sp,bp                                      ; 500B
        pop bp                                          ; 500D
        retf 0x10                                       ; 500E
        db 0x90                                         ; 5011

L3_5012:
        push si                                         ; 5012
        call vxd_version                                ; 5013
        mov_ si,ax                                      ; 5016
        or_ si,ax                                       ; 5018
        jz short L3_502E                                ; 501A
        cmp si,0x404                                    ; 501C
        jnc short L3_502A                               ; 5020
        mov word [0xc8],0x3                             ; 5022
        jmp short L3_5034                               ; 5028

L3_502A:
        xor_ ax,ax                                      ; 502A
        jmp short L3_5037                               ; 502C

L3_502E:
        mov word [0xc8],0x2                             ; 502E

L3_5034:
        mov ax,0x1                                      ; 5034

L3_5037:
        xor_ dx,dx                                      ; 5037
        pop si                                          ; 5039
        retf                                            ; 503A
        db 0x90                                         ; 503B

L3_503C:
        retf                                            ; 503C
        db 0x90                                         ; 503D

L3_503E:
        push bp                                         ; 503E
        mov_ bp,sp                                      ; 503F
        mov ax,[bp+0xe]                                 ; 5041
        mov [0xbd0],ax                                  ; 5044
        call L3_0000                                    ; 5047
        cmp ax,0x200                                    ; 504A
        jnc short L3_505A                               ; 504D
        mov ax,0x3                                      ; 504F
        push ax                                         ; 5052
        call L3_0A8C                                    ; 5053
        xor_ ax,ax                                      ; 5056
        jmp short L3_505D                               ; 5058

L3_505A:
        mov ax,0x1                                      ; 505A

L3_505D:
        mov_ sp,bp                                      ; 505D
        pop bp                                          ; 505F
        retf 0xa                                        ; 5060
        db 0x90                                         ; 5063

%if ES1869_FIX
%include "settings.asm"
%endif

seg3_data_end:

; relocation table
        dw (seg3_rel_end - seg3_rel_start) / 8
seg3_rel_start:
        reloc 2, 0, R3_4CDD, 0x0001, 0x0000             ; seg1
        reloc 3, 1, R3_0A81, 0x0001, 0x0003             ; KERNEL.GetVersion
        reloc 2, 0, R3_4BAA, 0x0003, 0x0000             ; seg3
        reloc 3, 1, R3_47E6, 0x0001, 0x0005             ; KERNEL.LocalAlloc
        reloc 3, 1, R3_4E9F, 0x0001, 0x0007             ; KERNEL.LocalFree
        reloc 2, 0, R3_4CC7, 0x0004, 0x0000             ; seg4
        reloc 3, 1, R3_0AE2, 0x0002, 0x0001             ; USER.MessageBox
        reloc 2, 0, R3_4E5A, 0x0005, 0x0000             ; seg5
        reloc 3, 1, R3_0329, 0x0001, 0x000F             ; KERNEL.GlobalAlloc
        reloc 2, 0, R3_4D0C, 0x0006, 0x0000             ; seg6
        reloc 3, 1, R3_4D74, 0x0001, 0x0011             ; KERNEL.GlobalFree
        reloc 2, 0, R3_4C42, 0x0007, 0x0000             ; seg7
        reloc 3, 1, R3_45B7, 0x0002, 0x0020             ; USER.GetWindowRect
        reloc 3, 1, R3_0B38, 0x0002, 0x01A5             ; USER.wvsprintf
        reloc 3, 1, R3_033D, 0x0001, 0x00AF             ; KERNEL.AllocSelector
        reloc 3, 1, R3_4D7C, 0x0001, 0x00B0             ; KERNEL.FreeSelector
        reloc 3, 1, R3_0344, 0x0001, 0x00B1             ; KERNEL.PrestoChangoSelector
        reloc 3, 1, R3_0BD0, 0x0002, 0x01AF             ; USER.AnsiUpper
        reloc 3, 1, R3_0AB5, 0x0002, 0x00B0             ; USER.LoadString
        reloc 3, 1, R3_45BF, 0x0002, 0x00B3             ; USER.GetSystemMetrics
        reloc 3, 1, R3_430F, 0x0001, 0x0059             ; KERNEL.lstrcat
        reloc 3, 1, R3_4327, 0x0001, 0x00D9             ; KERNEL.RegOpenKey
        reloc 3, 1, R3_1867, 0x0001, 0x00DA             ; KERNEL.RegCreateKey
        reloc 3, 1, R3_42FF, 0x0001, 0x00DC             ; KERNEL.RegCloseKey
        reloc 3, 1, R3_45A3, 0x0002, 0x0058             ; USER.EndDialog
        reloc 3, 1, R3_41A2, 0x0001, 0x00E1             ; KERNEL.RegQueryValueEx
        reloc 3, 1, R3_18A0, 0x0001, 0x00E2             ; KERNEL.RegSetValueEx
        reloc 3, 1, R3_463F, 0x0002, 0x0065             ; USER.SendDlgItemMessage
        reloc 3, 1, R3_460B, 0x0002, 0x00E8             ; USER.SetWindowPos
        reloc 3, 1, R3_456E, 0x0002, 0x00EF             ; USER.DialogBoxParam
%if ES1869_FIX
        settings_relocs
%endif
seg3_rel_end:
seg3_end:
