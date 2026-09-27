; Segment 3 of ESFM.DRV: code, 2546 bytes, flags 1D10h.
; ESS's driver code, disassembled with tools/ne2asm.py.

DriverProc:
        push bp                                         ; 0000
        mov_ bp,sp                                      ; 0001
        push ds                                         ; 0003
        movsel ax, R3_0005, 0xFFFF                      ; 0004 seg4
        mov ds,ax                                       ; 0007
        mov cx,[bp+0xe]                                 ; 0009
        mov_ ax,cx                                      ; 000C
        dec ax                                          ; 000E
        cmp ax,strict word 0xe                          ; 000F
        ja short L3_003A                                ; 0012
        add_ ax,ax                                      ; 0014
        xchg ax,bx                                      ; 0016
        jmp [cs:bx+jt_drvmsg]                           ; 0017

; DRV_LOAD .. DRV_POWER
jt_drvmsg:
        dw L3_0096                                      ; 001C
        dw L3_0057                                      ; 001E
        dw L3_0096                                      ; 0020
        dw L3_0096                                      ; 0022
        dw L3_0066                                      ; 0024
        dw L3_0096                                      ; 0026
        dw L3_006D                                      ; 0028
        dw L3_006D                                      ; 002A
        dw L3_0096                                      ; 002C
        dw L3_006D                                      ; 002E
        dw L3_003A                                      ; 0030
        dw L3_003A                                      ; 0032
        dw L3_003A                                      ; 0034
        dw L3_003A                                      ; 0036
        dw L3_0071                                      ; 0038

L3_003A:
        push word [bp+0x14]                             ; 003A
        push word [bp+0x12]                             ; 003D
        push word [bp+0x10]                             ; 0040
        push cx                                         ; 0043
        push word [bp+0xc]                              ; 0044
        push word [bp+0xa]                              ; 0047
        push word [bp+0x8]                              ; 004A
        push word [bp+0x6]                              ; 004D
        callp R3_0051, 0xFFFF, 0x0000                   ; 0050 USER.DefDriverProc
        jmp short L3_009B                               ; 0055

L3_0057:
%if ESFM_FIX
        ; DRV_ENABLE: the built-in bank, and the bank file is read again at
        ; the next MODM_OPEN (esfmfile.asm)
        callf fix_drv_enable, FIX_S3A, 0xFFFF           ; 0057 far seg1
%else
        callf bank_load, R3_005A, R3_0069               ; 0057 far seg3
%endif
        or_ dx,ax                                       ; 005C
        jnz short L3_006D                               ; 005E
        mov ax,0x1                                      ; 0060
        cwd                                             ; 0063
        jmp short L3_009B                               ; 0064

L3_0066:
        callf bank_free, R3_0069, R3_008D               ; 0066 far seg3
        jmp short L3_0096                               ; 006B

L3_006D:
        xor_ ax,ax                                      ; 006D
        jmp short L3_0099                               ; 006F

L3_0071:
        mov ax,[bp+0xa]                                 ; 0071
        mov dx,[bp+0xc]                                 ; 0074
        or_ dx,dx                                       ; 0077
        jnz short L3_0096                               ; 0079
        dec ax                                          ; 007B
        jz short L3_008A                                ; 007C
        sub ax,strict word 0x1                          ; 007E
        jc short L3_0096                                ; 0081
        sub ax,strict word 0x1                          ; 0083
        jna short L3_0091                               ; 0086
        jmp short L3_0096                               ; 0088

L3_008A:
        callf suspend_all, R3_008D, R3_0094             ; 008A far seg3
        jmp short L3_0096                               ; 008F

L3_0091:
        callf resume_all, R3_0094, 0xFFFF               ; 0091 far seg3

L3_0096:
        mov ax,0x1                                      ; 0096

L3_0099:
        xor_ dx,dx                                      ; 0099

L3_009B:
        pop ds                                          ; 009B
        mov_ sp,bp                                      ; 009C
        pop bp                                          ; 009E
        retf 0x10                                       ; 009F

mod_get_devcaps:
        push bp                                         ; 00A2
        mov_ bp,sp                                      ; 00A3
        sub sp,0xb2                                     ; 00A5
        push di                                         ; 00A9
        push si                                         ; 00AA
        mov word [bp-0x32],0x2e                         ; 00AB
        mov word [bp-0x2e],0x404                        ; 00B0
        mov ax,0x4                                      ; 00B5
        mov [bp-0x30],ax                                ; 00B8
        mov [bp-0xc],ax                                 ; 00BB
        mov ax,0x12                                     ; 00BE
        mov [bp-0xa],ax                                 ; 00C1
        mov [bp-0x8],ax                                 ; 00C4
        mov word [bp-0x6],0xffff                        ; 00C7
        mov word [bp-0x4],0x3                           ; 00CC
        mov word [bp-0x2],0x0                           ; 00D1
        push word [hinstance]                           ; 00D6
        mov ax,0x100                                    ; 00DA
        push ax                                         ; 00DD
        lea ax,[bp-0x72]                                ; 00DE
        push ss                                         ; 00E1
        push ax                                         ; 00E2
        mov ax,0x40                                     ; 00E3
        push ax                                         ; 00E6
        callp R3_00E8, 0xFFFF, 0x0000                   ; 00E7 USER.LoadString
        mov bx,[bp+0xa]                                 ; 00EC
        push word [bx+0xa]                              ; 00EF
        lea ax,[bp-0x72]                                ; 00F2
        push ss                                         ; 00F5
        push ax                                         ; 00F6
        lea ax,[bp-0xb2]                                ; 00F7
        push ss                                         ; 00FB
        push ax                                         ; 00FC
        callp R3_00FE, 0xFFFF, 0x0000                   ; 00FD USER.wsprintf
        add sp,byte +0xa                                ; 0102
        lea ax,[bp-0x2c]                                ; 0105
        push ss                                         ; 0108
        push ax                                         ; 0109
        lea ax,[bp-0xb2]                                ; 010A
        push ss                                         ; 010E
        push ax                                         ; 010F
        mov ax,0x1f                                     ; 0110
        push ax                                         ; 0113
        callp R3_0115, 0xFFFF, 0x0000                   ; 0114 KERNEL.lstrcpyn
        les bx,[bp+0x6]                                 ; 0119
        mov ax,[es:bx+0x4]                              ; 011C
        mov dx,[es:bx+0x6]                              ; 0120
        mov cx,[es:bx]                                  ; 0124
        cmp cx,byte +0x32                               ; 0127
        jna short L3_012F                               ; 012A
        mov cx,0x32                                     ; 012C

L3_012F:
        push ds                                         ; 012F
        mov_ di,ax                                      ; 0130
        lea si,[bp-0x32]                                ; 0132
        mov es,dx                                       ; 0135
        push ss                                         ; 0137
        pop ds                                          ; 0138
        shr cx,1                                        ; 0139
        rep movsw                                       ; 013B
        adc_ cx,cx                                      ; 013D
        rep movsb                                       ; 013F
        pop ds                                          ; 0141
        pop si                                          ; 0142
        pop di                                          ; 0143
        mov_ sp,bp                                      ; 0144
        pop bp                                          ; 0146
        retf 0x6                                        ; 0147

; GlobalWire and GlobalPageLock the bank on the first open
bank_lock:
        cmp word [bank_locks],byte +0x0                 ; 014A
        jnz short L3_0177                               ; 014F
        push word [0x14]                                ; 0151
        callp R3_0156, 0xFFFF, 0x0000                   ; 0155 KERNEL.GlobalWire
        push word [0x14]                                ; 015A
        callp R3_015F, 0xFFFF, 0x0000                   ; 015E KERNEL.GlobalPageLock
        or_ ax,ax                                       ; 0163
        jnz short L3_0173                               ; 0165
        push word [0x14]                                ; 0167
        callp R3_016C, R3_019D, 0x0000                  ; 016B KERNEL.GlobalUnWire
        xor_ ax,ax                                      ; 0170
        retf                                            ; 0172

L3_0173:
        inc word [bank_locks]                           ; 0173

L3_0177:
        mov ax,0x1                                      ; 0177
        retf                                            ; 017A
        db 0x90                                         ; 017B

bank_unlock:
        dec word [bank_locks]                           ; 017C
        jnz short L3_01A1                               ; 0180
        mov ax,[0x14]                                   ; 0182
        or ax,[bank_ptr]                                ; 0185
        jz short L3_01A1                                ; 0189
        push word [0x14]                                ; 018B
        callp R3_0190, 0xFFFF, 0x0000                   ; 018F KERNEL.GlobalPageUnlock
        or_ ax,ax                                       ; 0194
        jnz short L3_01A1                               ; 0196
        push word [0x14]                                ; 0198
        callp R3_019D, 0xFFFF, 0x0000                   ; 019C KERNEL.GlobalUnWire

L3_01A1:
        mov ax,0x1                                      ; 01A1
        retf                                            ; 01A4
        db 0x90                                         ; 01A5

; MODM_OPEN: acquire FM from ES1869.VXD, chip_reset, remember the client
; (one client at a time, a second open gets MMSYSERR_ALLOCATED)
mod_open:
        push bp                                         ; 01A6
        mov_ bp,sp                                      ; 01A7
        sub sp,byte +0x2                                ; 01A9
        push di                                         ; 01AC
        push si                                         ; 01AD
        mov si,[bp+0x12]                                ; 01AE
        cmp word [si+0x16],byte +0x0                    ; 01B1
        jnz short L3_01BF                               ; 01B5
        push si                                         ; 01B7
        call vxd_fm_acquire                             ; 01B8
        or_ ax,ax                                       ; 01BB
        jnz short L3_01C5                               ; 01BD

L3_01BF:
        mov ax,0x4                                      ; 01BF
        jmp near L3_0278                                ; 01C2

L3_01C5:
        mov ax,[0x14]                                   ; 01C5
        or ax,[bank_ptr]                                ; 01C8
        jz short L3_01ED                                ; 01CC
        push cs                                         ; 01CE
        call bank_lock                                  ; 01CF
        or_ ax,ax                                       ; 01D2
        jz short L3_01ED                                ; 01D4
        push si                                         ; 01D6
        callf chip_reset, R3_01DA, R3_023F              ; 01D7 far seg1
        mov ax,0x40                                     ; 01DC
        push ax                                         ; 01DF
        mov ax,0x10                                     ; 01E0
        push ax                                         ; 01E3
        callp R3_01E5, 0xFFFF, 0x0000                   ; 01E4 KERNEL.LocalAlloc
        or_ ax,ax                                       ; 01E9
        jnz short L3_01F0                               ; 01EB

L3_01ED:
        jmp near L3_0271                                ; 01ED

L3_01F0:
        les bx,[bp+0xa]                                 ; 01F0
        mov_ di,ax                                      ; 01F3
        mov ax,[es:bx+0x2]                              ; 01F5
        mov dx,[es:bx+0x4]                              ; 01F9
        mov [di],ax                                     ; 01FD
        mov [di+0x2],dx                                 ; 01FF
        mov ax,[es:bx+0x6]                              ; 0202
        mov dx,[es:bx+0x8]                              ; 0206
        mov [di+0x4],ax                                 ; 020A
        mov [di+0x6],dx                                 ; 020D
        mov ax,[es:bx]                                  ; 0210
        mov [di+0x8],ax                                 ; 0213
        mov ax,[bp+0x6]                                 ; 0216
        mov dx,[bp+0x8]                                 ; 0219
        mov [di+0xa],ax                                 ; 021C
        mov [di+0xc],dx                                 ; 021F
        mov [di+0xe],si                                 ; 0222
        les bx,[bp+0xe]                                 ; 0225
        mov [es:bx],di                                  ; 0228
        mov word [es:bx+0x2],0x0                        ; 022B
        push di                                         ; 0231
        mov ax,0x3c7                                    ; 0232
        push ax                                         ; 0235
        sub_ ax,ax                                      ; 0236
        push ax                                         ; 0238
        push ax                                         ; 0239
        push ax                                         ; 023A
        push ax                                         ; 023B
        callf driver_callback, R3_023F, R3_0291         ; 023C far seg1
        mov word [si+0x16],0x1                          ; 0241
        mov ax,[si+0x30c]                               ; 0246
        or ax,[si+0x30a]                                ; 024A
        jz short L3_026D                                ; 024E
        push word [si+0x308]                            ; 0250
        push word [si+0x306]                            ; 0254
        mov ax,0x1                                      ; 0258
        mov dx,0x3                                      ; 025B
        push dx                                         ; 025E
        push ax                                         ; 025F
        push word [si+0xe]                              ; 0260
        push word [si+0xc]                              ; 0263
        cwd                                             ; 0266
        push dx                                         ; 0267
        push ax                                         ; 0268
        call far [si+0x30a]                             ; 0269

L3_026D:
        xor_ ax,ax                                      ; 026D
        jmp short L3_0278                               ; 026F

L3_0271:
        push si                                         ; 0271
        call vxd_fm_release                             ; 0272
        mov ax,0x7                                      ; 0275

L3_0278:
        xor_ dx,dx                                      ; 0278
        pop si                                          ; 027A
        pop di                                          ; 027B
        mov_ sp,bp                                      ; 027C
        pop bp                                          ; 027E
        retf 0xe                                        ; 027F

; MODM_CLOSE: all_notes_off, release FM, MOM_CLOSE
mod_close:
        push bp                                         ; 0282
        mov_ bp,sp                                      ; 0283
        push di                                         ; 0285
        push si                                         ; 0286
        mov di,[bp+0x6]                                 ; 0287
        mov si,[di+0xe]                                 ; 028A
        push si                                         ; 028D
        callf all_notes_off, R3_0291, R3_02A9           ; 028E far seg1
        push si                                         ; 0293
        call vxd_fm_release                             ; 0294
        push cs                                         ; 0297
        call bank_unlock                                ; 0298
        push di                                         ; 029B
        mov ax,0x3c8                                    ; 029C
        push ax                                         ; 029F
        sub_ ax,ax                                      ; 02A0
        push ax                                         ; 02A2
        push ax                                         ; 02A3
        push ax                                         ; 02A4
        push ax                                         ; 02A5
        callf driver_callback, R3_02A9, R3_03B0         ; 02A6 far seg1
        push di                                         ; 02AB
        callp R3_02AD, 0xFFFF, 0x0000                   ; 02AC KERNEL.LocalFree
        mov word [si+0x16],0x0                          ; 02B1
        mov ax,[si+0x30c]                               ; 02B6
        or ax,[si+0x30a]                                ; 02BA
        jz short L3_02DE                                ; 02BE
        push word [si+0x308]                            ; 02C0
        push word [si+0x306]                            ; 02C4
        mov ax,0x1                                      ; 02C8
        mov dx,0x3                                      ; 02CB
        push dx                                         ; 02CE
        push ax                                         ; 02CF
        push word [si+0xe]                              ; 02D0
        push word [si+0xc]                              ; 02D3
        sub_ ax,ax                                      ; 02D6
        push ax                                         ; 02D8
        push ax                                         ; 02D9
        call far [si+0x30a]                             ; 02DA

L3_02DE:
        pop si                                          ; 02DE
        pop di                                          ; 02DF
        mov_ sp,bp                                      ; 02E0
        pop bp                                          ; 02E2
        retf 0x2                                        ; 02E3

; notification callback called by ES1869.VXD (registered with 0200)
vxd_notify:
        push bp                                         ; 02E6
        mov_ bp,sp                                      ; 02E7
        push si                                         ; 02E9
        push ds                                         ; 02EA
        movsel ax, R3_02EC, R3_0005                     ; 02EB seg4
        mov ds,ax                                       ; 02EE
        push word [bp+0xc]                              ; 02F0
        push word [bp+0xa]                              ; 02F3
%if ESFM_FIX
        ; the seg3 selector chain skips the DRV_ENABLE call, now to seg1
        callf find_device, R3_02F9, R3_0069             ; 02F6 far seg3
%else
        callf find_device, R3_02F9, R3_005A             ; 02F6 far seg3
%endif
        mov_ si,ax                                      ; 02FB
        or_ si,ax                                       ; 02FD
        jnz short L3_0307                               ; 02FF
        mov ax,0xfffe                                   ; 0301
        jmp near L3_0390                                ; 0304

L3_0307:
        mov ax,[bp+0xe]                                 ; 0307
        mov dx,[bp+0x10]                                ; 030A
        or_ dx,dx                                       ; 030D
        jz short L3_038E                                ; 030F
        dec dx                                          ; 0311
        jnz short L3_031A                               ; 0312
        or_ ax,ax                                       ; 0314
        jz short L3_0336                                ; 0316
        jmp short L3_038E                               ; 0318

L3_031A:
        dec dx                                          ; 031A
        jnz short L3_032A                               ; 031B
        or_ ax,ax                                       ; 031D
        jz short L3_0346                                ; 031F
        dec ax                                          ; 0321
        dec ax                                          ; 0322
        jz short L3_0353                                ; 0323
        dec ax                                          ; 0325
        jz short L3_0363                                ; 0326
        jmp short L3_038E                               ; 0328

L3_032A:
        sub ax,strict word 0x0                          ; 032A
        sbb dx,byte +0x2                                ; 032D
        or_ ax,dx                                       ; 0330
        jz short L3_037C                                ; 0332
        jmp short L3_038E                               ; 0334

L3_0336:
        mov ax,[bp+0x6]                                 ; 0336
        mov dx,[bp+0x8]                                 ; 0339
        mov [si+0x30a],ax                               ; 033C
        mov [si+0x30c],dx                               ; 0340
        jmp short L3_038E                               ; 0344

L3_0346:
        push si                                         ; 0346
        push word [bp+0x8]                              ; 0347
        push word [bp+0x6]                              ; 034A
        push cs                                         ; 034D
        call mod_get_devcaps                            ; 034E
        jmp short L3_038E                               ; 0351

L3_0353:
        mov ax,[bp+0x6]                                 ; 0353
        mov dx,[bp+0x8]                                 ; 0356
        mov [si+0x302],ax                               ; 0359
        mov [si+0x304],dx                               ; 035D
        jmp short L3_038E                               ; 0361

L3_0363:
        mov ax,[si+0x302]                               ; 0363
        mov dx,[si+0x304]                               ; 0367
        mov cx,[bp+0x8]                                 ; 036B
        mov bx,[bp+0x6]                                 ; 036E
        mov es,cx                                       ; 0371
        mov [es:bx],ax                                  ; 0373
        mov [es:bx+0x2],dx                              ; 0376
        jmp short L3_038E                               ; 037A

L3_037C:
        sub_ ax,ax                                      ; 037C
        mov [si+0x308],ax                               ; 037E
        mov [si+0x306],ax                               ; 0382
        mov [si+0x30c],ax                               ; 0386
        mov [si+0x30a],ax                               ; 038A

L3_038E:
        xor_ ax,ax                                      ; 038E

L3_0390:
        cwd                                             ; 0390
        pop ds                                          ; 0391
        pop si                                          ; 0392
        mov_ sp,bp                                      ; 0393
        pop bp                                          ; 0395
        retf 0x10                                       ; 0396
        db 0x90                                         ; 0399

; power suspend: all_notes_off, release FM
dev_suspend:
        push bp                                         ; 039A
        mov_ bp,sp                                      ; 039B
        push si                                         ; 039D
        mov si,[bp+0x6]                                 ; 039E
        cmp word [si+0x16],byte +0x0                    ; 03A1
        jz short L3_03BA                                ; 03A5
        or byte [si+0x30e],0x4                          ; 03A7
        push si                                         ; 03AC
        callf all_notes_off, R3_03B0, R3_03EF           ; 03AD far seg1
        push si                                         ; 03B2
        call vxd_fm_release                             ; 03B3
        push cs                                         ; 03B6
        call bank_unlock                                ; 03B7

L3_03BA:
        mov ax,0x1                                      ; 03BA
        pop si                                          ; 03BD
        mov_ sp,bp                                      ; 03BE
        pop bp                                          ; 03C0
        retf 0x2                                        ; 03C1

; power resume: acquire FM, chip_reset
dev_resume:
        push bp                                         ; 03C4
        mov_ bp,sp                                      ; 03C5
        push si                                         ; 03C7
        mov si,[bp+0x6]                                 ; 03C8
        test byte [si+0x30e],0x4                        ; 03CB
        jz short L3_03FC                                ; 03D0
        push si                                         ; 03D2
        call vxd_fm_acquire                             ; 03D3
        or_ ax,ax                                       ; 03D6
        jz short L3_03F8                                ; 03D8
        mov ax,[0x14]                                   ; 03DA
        or ax,[bank_ptr]                                ; 03DD
        jz short L3_03F8                                ; 03E1
        push cs                                         ; 03E3
        call bank_lock                                  ; 03E4
        or_ ax,ax                                       ; 03E7
        jz short L3_03F8                                ; 03E9
        push si                                         ; 03EB
        callf chip_reset, R3_03EF, 0xFFFF               ; 03EC far seg1
        and byte [si+0x30e],0xfb                        ; 03F1
        jmp short L3_03FC                               ; 03F6

L3_03F8:
        xor_ ax,ax                                      ; 03F8
        jmp short L3_03FF                               ; 03FA

L3_03FC:
        mov ax,0x1                                      ; 03FC

L3_03FF:
        pop si                                          ; 03FF
        mov_ sp,bp                                      ; 0400
        pop bp                                          ; 0402
        retf 0x2                                        ; 0403

; add dev to device_list
dev_link:
        push bp                                         ; 0406
        mov_ bp,sp                                      ; 0407
        mov bx,[bp+0x6]                                 ; 0409
        mov ax,[device_list]                            ; 040C
        mov [bx+0x30f],ax                               ; 040F
        mov [device_list],bx                            ; 0413
        pop bp                                          ; 0417
        retf                                            ; 0418
        db 0x90                                         ; 0419

dev_unlink:
        push bp                                         ; 041A
        mov_ bp,sp                                      ; 041B
        push si                                         ; 041D
        mov bx,0x3c                                     ; 041E
        cmp word [bx],byte +0x0                         ; 0421
        jz short L3_0443                                ; 0424
        mov cx,[bp+0x6]                                 ; 0426

L3_0429:
        cmp [bx],cx                                     ; 0429
        jz short L3_043B                                ; 042B
        mov ax,[bx]                                     ; 042D
        add ax,0x30f                                    ; 042F
        mov_ bx,ax                                      ; 0432
        cmp word [bx],byte +0x0                         ; 0434
        jnz short L3_0429                               ; 0437
        jmp short L3_0443                               ; 0439

L3_043B:
        mov si,[bx]                                     ; 043B
        mov ax,[si+0x30f]                               ; 043D
        mov [bx],ax                                     ; 0441

L3_0443:
        pop si                                          ; 0443
        mov_ sp,bp                                      ; 0444
        pop bp                                          ; 0446
        retf                                            ; 0447

; DRVM_INIT (64h): a devnode of the ES1869 appeared (allocate the device structure)
dev_add:
        push bp                                         ; 0448
        mov_ bp,sp                                      ; 0449
        push si                                         ; 044B
        push word [bp+0x8]                              ; 044C
        push word [bp+0x6]                              ; 044F
        push cs                                         ; 0452
        call find_device                                ; 0453
        mov_ si,ax                                      ; 0456
        or_ si,ax                                       ; 0458
        jz short L3_0464                                ; 045A
        inc word [si+0x10]                              ; 045C
        inc word [si+0x12]                              ; 045F
        jmp short L3_0493                               ; 0462

L3_0464:
        mov ax,0x40                                     ; 0464
        push ax                                         ; 0467
        mov ax,0x311                                    ; 0468
        push ax                                         ; 046B
        callp R3_046D, R3_01E5, 0x0000                  ; 046C KERNEL.LocalAlloc
        mov_ si,ax                                      ; 0471
        or_ si,ax                                       ; 0473
        jnz short L3_047C                               ; 0475
        mov ax,0x7                                      ; 0477
        jmp short L3_0495                               ; 047A

L3_047C:
        mov ax,[bp+0x6]                                 ; 047C
        mov dx,[bp+0x8]                                 ; 047F
        mov [si+0xc],ax                                 ; 0482
        mov [si+0xe],dx                                 ; 0485
        push si                                         ; 0488
        push cs                                         ; 0489
        call dev_link                                   ; 048A
        pop bx                                          ; 048D
        mov word [si+0x10],0x1                          ; 048E

L3_0493:
        xor_ ax,ax                                      ; 0493

L3_0495:
        xor_ dx,dx                                      ; 0495
        pop si                                          ; 0497
        mov_ sp,bp                                      ; 0498
        pop bp                                          ; 049A
        retf 0x4                                        ; 049B

; DRVM_ENABLE (67h): find the VxD, read the FM port (0101), register the
; notification client (0200)
dev_enable:
        push bp                                         ; 049E
        mov_ bp,sp                                      ; 049F
        sub sp,byte +0x8                                ; 04A1
        push si                                         ; 04A4
        push word [bp+0xa]                              ; 04A5
        push word [bp+0x8]                              ; 04A8
        push cs                                         ; 04AB
        call find_device                                ; 04AC
        mov_ si,ax                                      ; 04AF
        or_ si,ax                                       ; 04B1
        jnz short L3_04BA                               ; 04B3
        mov ax,0x5                                      ; 04B5
        jmp short L3_0536                               ; 04B8

L3_04BA:
        cmp word [si+0x12],byte +0x0                    ; 04BA
        jz short L3_04C5                                ; 04BE
        inc word [si+0x12]                              ; 04C0
        jmp short L3_052F                               ; 04C3

L3_04C5:
        push word [bp+0x6]                              ; 04C5
        call vxd_get_entry                              ; 04C8
        mov [si],ax                                     ; 04CB
        mov [si+0x2],dx                                 ; 04CD
        mov_ ax,dx                                      ; 04D0
        or ax,[si]                                      ; 04D2
        jz short L3_0533                                ; 04D4
        push si                                         ; 04D6
        push word [bp+0xa]                              ; 04D7
        push word [bp+0x8]                              ; 04DA
        call vxd_fm_info                                ; 04DD
        or_ ax,ax                                       ; 04E0
        jz short L3_0533                                ; 04E2
        mov word [si+0x302],0xffff                      ; 04E4
        mov word [si+0x304],0xffff                      ; 04EA
        mov word [bp-0x8],0x8                           ; 04F0
        mov word [bp-0x6],0x0                           ; 04F5
        mov word [bp-0x4],vxd_notify                    ; 04FA
        db 0xC7, 0x46, 0xFE                             ; 04FF mov word [bp-0x2],0x512
R3_0502: dw R3_0512                                     ; seg3
        lea ax,[bp-0x8]                                 ; 0504
        push ss                                         ; 0507
        push ax                                         ; 0508
        mov ax,0x3e                                     ; 0509
        push ds                                         ; 050C
        push ax                                         ; 050D
        push si                                         ; 050E
        callf vxd_register, R3_0512, R3_0553            ; 050F far seg3
        add sp,byte +0xa                                ; 0514
        mov [si+0x306],ax                               ; 0517
        mov [si+0x308],dx                               ; 051B
        mov word [si+0x12],0x1                          ; 051F
        cmp word [si+0xa],byte +0x0                     ; 0524
        jz short L3_052F                                ; 0528
        mov word [si+0x14],0x1                          ; 052A

L3_052F:
        xor_ ax,ax                                      ; 052F
        jmp short L3_0536                               ; 0531

L3_0533:
        mov ax,0x2                                      ; 0533

L3_0536:
        xor_ dx,dx                                      ; 0536
        pop si                                          ; 0538
        mov_ sp,bp                                      ; 0539
        pop bp                                          ; 053B
        retf 0x6                                        ; 053C
        db 0x90                                         ; 053F

; DRV_POWER suspend
suspend_all:
        push si                                         ; 0540
        mov si,[device_list]                            ; 0541
        or_ si,si                                       ; 0545
        jz short L3_055D                                ; 0547

L3_0549:
        cmp word [si+0x14],byte +0x0                    ; 0549
        jz short L3_0555                                ; 054D
        push si                                         ; 054F
        callf dev_suspend, R3_0553, R3_0573             ; 0550 far seg3

L3_0555:
        mov si,[si+0x30f]                               ; 0555
        or_ si,si                                       ; 0559
        jnz short L3_0549                               ; 055B

L3_055D:
        pop si                                          ; 055D
        retf                                            ; 055E
        db 0x90                                         ; 055F

; DRV_POWER resume
resume_all:
        push si                                         ; 0560
        mov si,[device_list]                            ; 0561
        or_ si,si                                       ; 0565
        jz short L3_057D                                ; 0567

L3_0569:
        cmp word [si+0x14],byte +0x0                    ; 0569
        jz short L3_0575                                ; 056D
        push si                                         ; 056F
        callf dev_resume, R3_0573, R3_05CC              ; 0570 far seg3

L3_0575:
        mov si,[si+0x30f]                               ; 0575
        or_ si,si                                       ; 0579
        jnz short L3_0569                               ; 057B

L3_057D:
        pop si                                          ; 057D
        retf                                            ; 057E
        db 0x90                                         ; 057F

; DRVM_DISABLE (66h): unregister from the VxD
dev_disable:
        push bp                                         ; 0580
        mov_ bp,sp                                      ; 0581
        push si                                         ; 0583
        push word [bp+0x8]                              ; 0584
        push word [bp+0x6]                              ; 0587
        push cs                                         ; 058A
        call find_device                                ; 058B
        mov_ si,ax                                      ; 058E
        or_ si,ax                                       ; 0590
        jnz short L3_0599                               ; 0592
        mov ax,0x5                                      ; 0594
        jmp short L3_05E0                               ; 0597

L3_0599:
        dec word [si+0x12]                              ; 0599
        jz short L3_05A2                                ; 059C
        xor_ ax,ax                                      ; 059E
        jmp short L3_05E0                               ; 05A0

L3_05A2:
        cmp word [si+0x14],byte +0x0                    ; 05A2
        jz short L3_05B6                                ; 05A6
        push si                                         ; 05A8
        call vxd_fm_acquire                             ; 05A9
        push si                                         ; 05AC
        callf chip_reset, R3_05B0, R3_01DA              ; 05AD far seg1
        push si                                         ; 05B2
        call vxd_fm_release                             ; 05B3

L3_05B6:
        mov ax,[si+0x308]                               ; 05B6
        or ax,[si+0x306]                                ; 05BA
        jz short L3_05DB                                ; 05BE
        push word [si+0x308]                            ; 05C0
        push word [si+0x306]                            ; 05C4
        push si                                         ; 05C8
        callf vxd_unregister, R3_05CC, R3_02F9          ; 05C9 far seg3
        add sp,byte +0x6                                ; 05CE
        sub_ ax,ax                                      ; 05D1
        mov [si+0x308],ax                               ; 05D3
        mov [si+0x306],ax                               ; 05D7

L3_05DB:
        xor_ ax,ax                                      ; 05DB
        mov [si+0x14],ax                                ; 05DD

L3_05E0:
        xor_ dx,dx                                      ; 05E0
        pop si                                          ; 05E2
        mov_ sp,bp                                      ; 05E3
        pop bp                                          ; 05E5
        retf 0x4                                        ; 05E6
        db 0x90                                         ; 05E9

; DRVM_EXIT (65h): free the device structure
dev_remove:
        push bp                                         ; 05EA
        mov_ bp,sp                                      ; 05EB
        push si                                         ; 05ED
        push word [bp+0x8]                              ; 05EE
        push word [bp+0x6]                              ; 05F1
        push cs                                         ; 05F4
        call find_device                                ; 05F5
        mov_ si,ax                                      ; 05F8
        or_ si,ax                                       ; 05FA
        jnz short L3_0603                               ; 05FC
        mov ax,0x5                                      ; 05FE
        jmp short L3_0621                               ; 0601

L3_0603:
        dec word [si+0x10]                              ; 0603
        jnz short L3_061F                               ; 0606
        cmp word [si+0x12],byte +0x0                    ; 0608
        jz short L3_0613                                ; 060C
        mov ax,0x4                                      ; 060E
        jmp short L3_0621                               ; 0611

L3_0613:
        push si                                         ; 0613
        push cs                                         ; 0614
        call dev_unlink                                 ; 0615
        pop bx                                          ; 0618
        push si                                         ; 0619
        callp R3_061B, R3_02AD, 0x0000                  ; 061A KERNEL.LocalFree

L3_061F:
        xor_ ax,ax                                      ; 061F

L3_0621:
        xor_ dx,dx                                      ; 0621
        pop si                                          ; 0623
        mov_ sp,bp                                      ; 0624
        pop bp                                          ; 0626
        retf 0x4                                        ; 0627

; device structure of a devnode
find_device:
        push bp                                         ; 062A
        mov_ bp,sp                                      ; 062B
        cmp word [device_list],byte +0x0                ; 062D
        jz short L3_065A                                ; 0632
        mov bx,[device_list]                            ; 0634
        or_ bx,bx                                       ; 0638
        jz short L3_065A                                ; 063A

L3_063C:
        mov ax,[bx+0xc]                                 ; 063C
        mov dx,[bx+0xe]                                 ; 063F
        cmp [bp+0x6],ax                                 ; 0642
        jnz short L3_064C                               ; 0645
        cmp [bp+0x8],dx                                 ; 0647
        jz short L3_0656                                ; 064A

L3_064C:
        mov bx,[bx+0x30f]                               ; 064C
        or_ bx,bx                                       ; 0650
        jnz short L3_063C                               ; 0652
        jmp short L3_065A                               ; 0654

L3_0656:
        mov_ ax,bx                                      ; 0656
        jmp short L3_065C                               ; 0658

L3_065A:
        xor_ ax,ax                                      ; 065A

L3_065C:
        mov_ sp,bp                                      ; 065C
        pop bp                                          ; 065E
        retf 0x4                                        ; 065F

; DRV_ENABLE: allocate the bank and copy resource 1234 into it
bank_load:
        push bp                                         ; 0662
        mov_ bp,sp                                      ; 0663
        sub sp,0x134                                    ; 0665
        push di                                         ; 0669
        push si                                         ; 066A
        mov ax,0x2042                                   ; 066B
        push ax                                         ; 066E
        mov ax,0x2060                                   ; 066F
        cwd                                             ; 0672
        push dx                                         ; 0673
        push ax                                         ; 0674
        callp R3_0676, 0xFFFF, 0x0000                   ; 0675 KERNEL.GlobalAlloc
        mov word [bank_ptr],0x0                         ; 067A
        mov [0x14],ax                                   ; 0680
        or ax,[bank_ptr]                                ; 0683
        jnz short L3_068C                               ; 0687
        jmp near L3_07BA                                ; 0689

L3_068C:
        lea ax,[bp-0x134]                               ; 068C
        push ss                                         ; 0690
        push ax                                         ; 0691
        mov ax,0x47                                     ; 0692
        push ds                                         ; 0695
        push ax                                         ; 0696
        callp R3_0698, 0xFFFF, 0x0000                   ; 0697 KERNEL.lstrcpy
        lea ax,[bp-0x134]                               ; 069C
        push ss                                         ; 06A0
        push ax                                         ; 06A1
        mov ax,0x51                                     ; 06A2
        push ds                                         ; 06A5
        push ax                                         ; 06A6
        callp R3_06A8, 0xFFFF, 0x0000                   ; 06A7 USER.lstrcmpi
        or_ ax,ax                                       ; 06AC
        jnz short L3_070C                               ; 06AE
        push word [hinstance]                           ; 06B0
        mov ax,0x4d2                                    ; 06B4
        cwd                                             ; 06B7
        push dx                                         ; 06B8
        push ax                                         ; 06B9
        mov ax,0x100                                    ; 06BA
        cwd                                             ; 06BD
        push dx                                         ; 06BE
        push ax                                         ; 06BF
        callp R3_06C1, 0xFFFF, 0x0000                   ; 06C0 KERNEL.FindResource
        mov [bp-0x2],ax                                 ; 06C5
        or_ ax,ax                                       ; 06C8
        jz short L3_0723                                ; 06CA
        push word [hinstance]                           ; 06CC
        push ax                                         ; 06D0
        callp R3_06D2, 0xFFFF, 0x0000                   ; 06D1 KERNEL.LoadResource
        mov [bp-0x8],ax                                 ; 06D6
        push ax                                         ; 06D9
        callp R3_06DB, 0xFFFF, 0x0000                   ; 06DA KERNEL.LockResource
        mov [bp-0x6],ax                                 ; 06DF
        mov [bp-0x4],dx                                 ; 06E2
        mov ax,[bank_ptr]                               ; 06E5
        mov dx,[0x14]                                   ; 06E8
        mov bx,[bp-0x6]                                 ; 06EC
        mov si,[bp-0x4]                                 ; 06EF
        push ds                                         ; 06F2
        push si                                         ; 06F3
        mov_ di,ax                                      ; 06F4
        mov_ si,bx                                      ; 06F6
        mov es,dx                                       ; 06F8
        pop ds                                          ; 06FA
        mov cx,0x1030                                   ; 06FB
        rep movsw                                       ; 06FE
        pop ds                                          ; 0700
        push word [bp-0x8]                              ; 0701
        callp R3_0705, 0xFFFF, 0x0000                   ; 0704 KERNEL.FreeResource
        jmp near L3_07A5                                ; 0709

L3_070C:
        lea ax,[bp-0x134]                               ; 070C
        push ss                                         ; 0710
        push ax                                         ; 0711
        sub_ ax,ax                                      ; 0712
        push ax                                         ; 0714
        push ax                                         ; 0715
        push ax                                         ; 0716
        push ax                                         ; 0717
        callp R3_0719, 0xFFFF, 0x0000                   ; 0718 MMSYSTEM.mmioOpen
        mov_ si,ax                                      ; 071D
        or_ si,ax                                       ; 071F
        jnz short L3_0726                               ; 0721

L3_0723:
        jmp near L3_07A9                                ; 0723

L3_0726:
        push si                                         ; 0726
        lea ax,[bp-0x1c]                                ; 0727
        push ss                                         ; 072A
        push ax                                         ; 072B
        sub_ ax,ax                                      ; 072C
        push ax                                         ; 072E
        push ax                                         ; 072F
        push ax                                         ; 0730
        callp R3_0732, R3_076C, 0x0000                  ; 0731 MMSYSTEM.mmioDescend
        cmp word [bp-0x1c],0x4952                       ; 0736
        jnz short L3_079C                               ; 073B
        cmp word [bp-0x1a],0x4646                       ; 073D
        jnz short L3_079C                               ; 0742
        cmp word [bp-0x14],0x7450                       ; 0744
        jnz short L3_079C                               ; 0749
        cmp word [bp-0x12],0x6863                       ; 074B
        jnz short L3_079C                               ; 0750
        mov word [bp-0x30],0x6d66                       ; 0752
        mov word [bp-0x2e],0x2034                       ; 0757
        push si                                         ; 075C
        lea ax,[bp-0x30]                                ; 075D
        push ss                                         ; 0760
        push ax                                         ; 0761
        lea ax,[bp-0x1c]                                ; 0762
        push ss                                         ; 0765
        push ax                                         ; 0766
        mov ax,0x10                                     ; 0767
        push ax                                         ; 076A
        callp R3_076C, 0xFFFF, 0x0000                   ; 076B MMSYSTEM.mmioDescend
        or_ ax,ax                                       ; 0770
        jnz short L3_079C                               ; 0772
        cmp [bp-0x2a],ax                                ; 0774
        jnz short L3_0780                               ; 0777
        cmp word [bp-0x2c],0x2060                       ; 0779
        jna short L3_0788                               ; 077E

L3_0780:
        mov word [bp-0x2c],0x2060                       ; 0780
        mov [bp-0x2a],ax                                ; 0785

L3_0788:
        push si                                         ; 0788
        push word [0x14]                                ; 0789
        push word [bank_ptr]                            ; 078D
        push word [bp-0x2a]                             ; 0791
        push word [bp-0x2c]                             ; 0794
        callp R3_0798, 0xFFFF, 0x0000                   ; 0797 MMSYSTEM.mmioRead

L3_079C:
        push si                                         ; 079C
        xor_ ax,ax                                      ; 079D
        push ax                                         ; 079F
        callp R3_07A1, 0xFFFF, 0x0000                   ; 07A0 MMSYSTEM.mmioClose

L3_07A5:
        xor_ ax,ax                                      ; 07A5
        jmp short L3_07BD                               ; 07A7

L3_07A9:
        push word [0x14]                                ; 07A9
        callp R3_07AE, 0xFFFF, 0x0000                   ; 07AD KERNEL.GlobalFree
        sub_ ax,ax                                      ; 07B2
        mov [0x14],ax                                   ; 07B4
        mov [bank_ptr],ax                               ; 07B7

L3_07BA:
        mov ax,0x7                                      ; 07BA

L3_07BD:
        xor_ dx,dx                                      ; 07BD
        pop si                                          ; 07BF
        pop di                                          ; 07C0
        mov_ sp,bp                                      ; 07C1
        pop bp                                          ; 07C3
        retf                                            ; 07C4
        db 0x90                                         ; 07C5

; DRV_DISABLE
bank_free:
        mov ax,[0x14]                                   ; 07C6
        or ax,[bank_ptr]                                ; 07C9
        jz short L3_07E0                                ; 07CD
        push word [0x14]                                ; 07CF
        callp R3_07D4, R3_07AE, 0x0000                  ; 07D3 KERNEL.GlobalFree
        sub_ ax,ax                                      ; 07D8
        mov [0x14],ax                                   ; 07DA
        mov [bank_ptr],ax                               ; 07DD

L3_07E0:
        retf                                            ; 07E0
        db 0x90                                         ; 07E1

; remember hInstance
LibMain:
        push bp                                         ; 07E2
        mov_ bp,sp                                      ; 07E3
        mov ax,[bp+0xe]                                 ; 07E5
        mov [hinstance],ax                              ; 07E8
        mov ax,0x1                                      ; 07EB
        pop bp                                          ; 07EE
        retf 0xa                                        ; 07EF

; INT 2Fh AX=1684h
vxd_get_entry:
        push bp                                         ; 07F2
        mov_ bp,sp                                      ; 07F3
        sub sp,byte +0x4                                ; 07F5
        push di                                         ; 07F8
        push es                                         ; 07F9
        push di                                         ; 07FA
        push bx                                         ; 07FB
        xor_ di,di                                      ; 07FC
        mov es,di                                       ; 07FE
        mov ax,0x1684                                   ; 0800
        mov bx,[bp+0x4]                                 ; 0803
        int 0x2f                                        ; 0806
        mov [bp-0x4],di                                 ; 0808
        mov [bp-0x2],es                                 ; 080B
        pop bx                                          ; 080E
        pop di                                          ; 080F
        pop es                                          ; 0810
        mov ax,[bp-0x4]                                 ; 0811
        mov dx,[bp-0x2]                                 ; 0814
        pop di                                          ; 0817
        mov_ sp,bp                                      ; 0818
        pop bp                                          ; 081A
        ret 0x2                                         ; 081B

; ES1869.VXD 0000
vxd_version:
        push bp                                         ; 081E
        mov_ bp,sp                                      ; 081F
        sub sp,byte +0x2                                ; 0821
        mov word [bp-0x2],0x0                           ; 0824
        mov ax,[bp+0x6]                                 ; 0829
        or ax,[bp+0x4]                                  ; 082C
        jnz short L3_0835                               ; 082F
        xor_ ax,ax                                      ; 0831
        jmp short L3_0840                               ; 0833

L3_0835:
        xor_ dx,dx                                      ; 0835
        call far [bp+0x4]                               ; 0837
        mov [bp-0x2],ax                                 ; 083A
        mov ax,[bp-0x2]                                 ; 083D

L3_0840:
        mov_ sp,bp                                      ; 0840
        pop bp                                          ; 0842
        ret 0x4                                         ; 0843

; ES1869.VXD 0101
vxd_fm_info:
        push bp                                         ; 0846
        mov_ bp,sp                                      ; 0847
        sub sp,byte +0x22                               ; 0849
        push di                                         ; 084C
        mov word [bp-0x2],0x0                           ; 084D
        mov bx,[bp+0x8]                                 ; 0852
        mov ax,[bx]                                     ; 0855
        mov dx,[bx+0x2]                                 ; 0857
        mov [bp-0x22],ax                                ; 085A
        mov [bp-0x20],dx                                ; 085D
        or_ dx,ax                                       ; 0860
        jnz short L3_0868                               ; 0862
        xor_ ax,ax                                      ; 0864
        jmp short L3_08BD                               ; 0866

L3_0868:
        mov word [bp-0x1e],0x1c                         ; 0868
        mov word [bp-0x1c],0x0                          ; 086D
        push es                                         ; 0872
        push di                                         ; 0873
        push ss                                         ; 0874
        pop es                                          ; 0875
        push ecx                                        ; 0876
        lea bx,[bp-0x1e]                                ; 0878
        mov ax,[bp+0x6]                                 ; 087B
        push ax                                         ; 087E
        mov ax,[bp+0x4]                                 ; 087F
        push ax                                         ; 0882
        pop ecx                                         ; 0883
        mov ax,0x1                                      ; 0885
        mov dx,0x101                                    ; 0888
        call far [bp-0x22]                              ; 088B
        pop ecx                                         ; 088E
        jc short L3_0897                                ; 0890
        mov word [bp-0x2],0x1                           ; 0892

L3_0897:
        pop di                                          ; 0897
        pop es                                          ; 0898
        cmp word [bp-0x2],byte +0x0                     ; 0899
        jz short L3_08BA                                ; 089D
        mov bx,[bp+0x8]                                 ; 089F
        mov ax,[bp-0x1a]                                ; 08A2
        mov [bx+0x8],ax                                 ; 08A5
        mov ax,[bp-0x18]                                ; 08A8
        mov [bx+0xa],ax                                 ; 08AB
        mov ax,[bp-0x12]                                ; 08AE
        mov dx,[bp-0x10]                                ; 08B1
        mov [bx+0xc],ax                                 ; 08B4
        mov [bx+0xe],dx                                 ; 08B7

L3_08BA:
        mov ax,[bp-0x2]                                 ; 08BA

L3_08BD:
        pop di                                          ; 08BD
        mov_ sp,bp                                      ; 08BE
        pop bp                                          ; 08C0
        ret 0x6                                         ; 08C1

; ES1869.VXD 0102 (counted)
vxd_fm_acquire:
        push bp                                         ; 08C4
        mov_ bp,sp                                      ; 08C5
        sub sp,byte +0x8                                ; 08C7
        mov word [bp-0x2],0x0                           ; 08CA
        mov bx,[bp+0x4]                                 ; 08CF
        mov ax,[bx+0xa]                                 ; 08D2
        mov [bp-0x4],ax                                 ; 08D5
        mov ax,[bx]                                     ; 08D8
        mov dx,[bx+0x2]                                 ; 08DA
        mov [bp-0x8],ax                                 ; 08DD
        mov [bp-0x6],dx                                 ; 08E0
        or_ dx,ax                                       ; 08E3
        jnz short L3_08EB                               ; 08E5
        xor_ ax,ax                                      ; 08E7
        jmp short L3_0926                               ; 08E9

L3_08EB:
        mov ax,[bx+0x6]                                 ; 08EB
        or ax,[bx+0x4]                                  ; 08EE
        jz short L3_0900                                ; 08F1
        add word [bx+0x4],byte +0x1                     ; 08F3
        adc word [bx+0x6],byte +0x0                     ; 08F7
        mov ax,0x1                                      ; 08FB
        jmp short L3_0926                               ; 08FE

L3_0900:
        mov ax,[bp-0x4]                                 ; 0900
        mov dx,0x102                                    ; 0903
        call far [bp-0x8]                               ; 0906
        jc short L3_0910                                ; 0909
        mov word [bp-0x2],0x1                           ; 090B

L3_0910:
        cmp word [bp-0x2],byte +0x0                     ; 0910
        jz short L3_0923                                ; 0914
        mov bx,[bp+0x4]                                 ; 0916
        mov word [bx+0x4],0x1                           ; 0919
        mov word [bx+0x6],0x0                           ; 091E

L3_0923:
        mov ax,[bp-0x2]                                 ; 0923

L3_0926:
        mov_ sp,bp                                      ; 0926
        pop bp                                          ; 0928
        ret 0x2                                         ; 0929

; ES1869.VXD 0103 (counted)
vxd_fm_release:
        push bp                                         ; 092C
        mov_ bp,sp                                      ; 092D
        sub sp,byte +0x8                                ; 092F
        mov word [bp-0x2],0x0                           ; 0932
        mov bx,[bp+0x4]                                 ; 0937
        mov ax,[bx+0xa]                                 ; 093A
        mov [bp-0x4],ax                                 ; 093D
        mov ax,[bx]                                     ; 0940
        mov dx,[bx+0x2]                                 ; 0942
        mov [bp-0x8],ax                                 ; 0945
        mov [bp-0x6],dx                                 ; 0948
        or_ dx,ax                                       ; 094B
        jnz short L3_0953                               ; 094D
        xor_ ax,ax                                      ; 094F
        jmp short L3_097E                               ; 0951

L3_0953:
        mov ax,[bx+0x6]                                 ; 0953
        or ax,[bx+0x4]                                  ; 0956
        jz short L3_097B                                ; 0959
        sub word [bx+0x4],byte +0x1                     ; 095B
        sbb word [bx+0x6],byte +0x0                     ; 095F
        mov ax,[bx+0x6]                                 ; 0963
        or ax,[bx+0x4]                                  ; 0966
        jnz short L3_097B                               ; 0969
        mov ax,[bp-0x4]                                 ; 096B
        mov dx,0x103                                    ; 096E
        call far [bp-0x8]                               ; 0971
        jc short L3_097B                                ; 0974
        mov word [bp-0x2],0x1                           ; 0976

L3_097B:
        mov ax,[bp-0x2]                                 ; 097B

L3_097E:
        mov_ sp,bp                                      ; 097E
        pop bp                                          ; 0980
        ret 0x2                                         ; 0981

; ES1869.VXD 0200: notification client
vxd_register:
        push bp                                         ; 0984
        mov_ bp,sp                                      ; 0985
        mov bx,[bp+0x6]                                 ; 0987
        sub sp,byte +0x4                                ; 098A
        mov ax,[bx]                                     ; 098D
        mov dx,[bx+0x2]                                 ; 098F
        mov [bp-0x4],ax                                 ; 0992
        mov [bp-0x2],dx                                 ; 0995
        or_ dx,ax                                       ; 0998
        jnz short L3_09A1                               ; 099A
        xor_ ax,ax                                      ; 099C
        cwd                                             ; 099E
        jmp short L3_09BC                               ; 099F

L3_09A1:
        mov dx,0x200                                    ; 09A1
        push word [bp+0xe]                              ; 09A4
        push word [bp+0xc]                              ; 09A7
        push word [bp+0xa]                              ; 09AA
        push word [bp+0x8]                              ; 09AD
        mov bx,[bp+0x6]                                 ; 09B0
        push word [bx+0xe]                              ; 09B3
        push word [bx+0xc]                              ; 09B6
        call far [bp-0x4]                               ; 09B9

L3_09BC:
        mov_ sp,bp                                      ; 09BC
        pop bp                                          ; 09BE
        retf                                            ; 09BF

; ES1869.VXD 0201
vxd_unregister:
        push bp                                         ; 09C0
        mov_ bp,sp                                      ; 09C1
        mov bx,[bp+0x6]                                 ; 09C3
        sub sp,byte +0x4                                ; 09C6
        mov ax,[bx]                                     ; 09C9
        mov dx,[bx+0x2]                                 ; 09CB
        mov [bp-0x4],ax                                 ; 09CE
        mov [bp-0x2],dx                                 ; 09D1
        or_ dx,ax                                       ; 09D4
        jz short L3_09ED                                ; 09D6
        mov dx,0x201                                    ; 09D8
        push word [bp+0xa]                              ; 09DB
        push word [bp+0x8]                              ; 09DE
        mov bx,[bp+0x6]                                 ; 09E1
        push word [bx+0xe]                              ; 09E4
        push word [bx+0xc]                              ; 09E7
        call far [bp-0x4]                               ; 09EA

L3_09ED:
        mov_ sp,bp                                      ; 09ED
        pop bp                                          ; 09EF
        retf                                            ; 09F0
        db 0x90                                         ; 09F1

seg3_data_end:

; relocation table
        dw (seg3_rel_end - seg3_rel_start) / 8
seg3_rel_start:
        reloc 2, 0, R3_05B0, 0x0001, 0x0000             ; seg1
        reloc 2, 0, R3_0502, 0x0003, 0x0000             ; seg3
        reloc 3, 1, R3_046D, 0x0001, 0x0005             ; KERNEL.LocalAlloc
        reloc 3, 1, R3_0051, 0x0002, 0x00FF             ; USER.DefDriverProc
        reloc 3, 1, R3_061B, 0x0001, 0x0007             ; KERNEL.LocalFree
        reloc 2, 0, R3_02EC, 0x0004, 0x0000             ; seg4
        reloc 3, 1, R3_0676, 0x0001, 0x000F             ; KERNEL.GlobalAlloc
        reloc 3, 1, R3_07D4, 0x0001, 0x0011             ; KERNEL.GlobalFree
        reloc 3, 1, R3_00FE, 0x0002, 0x01A4             ; USER.wsprintf
        reloc 3, 1, R3_00E8, 0x0002, 0x00B0             ; USER.LoadString
        reloc 3, 1, R3_06C1, 0x0001, 0x003C             ; KERNEL.FindResource
        reloc 3, 1, R3_06D2, 0x0001, 0x003D             ; KERNEL.LoadResource
        reloc 3, 1, R3_06DB, 0x0001, 0x003E             ; KERNEL.LockResource
        reloc 3, 1, R3_015F, 0x0001, 0x00BF             ; KERNEL.GlobalPageLock
        reloc 3, 1, R3_0705, 0x0001, 0x003F             ; KERNEL.FreeResource
        reloc 3, 1, R3_0190, 0x0001, 0x00C0             ; KERNEL.GlobalPageUnlock
        reloc 3, 1, R3_0719, 0x0003, 0x04BA             ; MMSYSTEM.mmioOpen
        reloc 3, 1, R3_07A1, 0x0003, 0x04BB             ; MMSYSTEM.mmioClose
        reloc 3, 1, R3_0798, 0x0003, 0x04BC             ; MMSYSTEM.mmioRead
        reloc 3, 1, R3_0732, 0x0003, 0x04C7             ; MMSYSTEM.mmioDescend
        reloc 3, 1, R3_0698, 0x0001, 0x0058             ; KERNEL.lstrcpy
        reloc 3, 1, R3_06A8, 0x0002, 0x01D7             ; USER.lstrcmpi
        reloc 3, 1, R3_0115, 0x0001, 0x0161             ; KERNEL.lstrcpyn
        reloc 3, 1, R3_0156, 0x0001, 0x006F             ; KERNEL.GlobalWire
        reloc 3, 1, R3_016C, 0x0001, 0x0070             ; KERNEL.GlobalUnWire
%if ESFM_FIX
        reloc 2, 0, FIX_S3A, 0x0001, 0x0000             ; seg1
%endif
seg3_rel_end:
seg3_end:
