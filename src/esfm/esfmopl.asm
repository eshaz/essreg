; The OPL3 device of ESFM.DRV, assembled when ESFM_FIX=1.  The code is
; appended to segment 1 (fixed code, callable at interrupt time).
;
; Notes:
;
; 1. With Opl3Device=1, the default, the driver has a second MIDI output
;    device for each ES1869, device 1, "OPL3 FM Synthesis".  Device 0 is
;    ESS's synthesizer in the chip's ESFM native mode; device 1 plays
;    General MIDI on the chip's OPL3 compatibility mode and writes FM_Base+0
;    to +3 as a driver writes an OPL3: 18 voices of two operators with
;    Freedoom's GENMIDI instruments (third_party/genmidi), the bank of
;    Doom's DMX sound library.
; 2. The two devices share the chip, so one of them is open at a time: the
;    other's open gets MMSYSERR_ALLOCATED from ESS's code, as a second
;    client of device 0 does.  ESS's code opens and closes device 1 as it
;    does device 0, so FM comes from ES1869.VXD and the mixer hears of MIDI
;    the same way.  After ESS's open the chip leaves native mode and is as
;    an OPL3 after a reset (opl_claim), and before ESS's close every note is
;    cut and chip_reset brings native mode back.
; 3. The program that opened device 1 has the chip as a real OPL3: Windows
;    owns FM while the device is open, so ES1869.VXD traps none of the FM
;    ports for it (Audio_Base+0h-3h, 8h, 9h and 388h-38Bh), and its own
;    reads and writes reach the chip.  The device writes nothing until a
;    MIDI message comes; the first one sets the OPL3 up for General MIDI
;    (opl_ensure, OPL_ENGINE).  While device 1 has the chip (OPL_HAS),
;    fm_write drops ESS's native register writes for that ES1869
;    (fix_fm_guard), so nothing of ESS's lands in the OPL3's registers.
;    chip_reset clears DEV_OPL_READY first (a power resume, the close), and
;    device 1's next message sets the OPL3 up again.
; 4. With Opl3Device=2, which essinst writes when it makes ESFMOPL3.DRV
;    one of the card's MIDI drivers, the count is ESS's alone: device 1 is
;    ESFMOPL3.DRV's, which passes every message of its one device on to
;    this modMessage as device 1's (src/esfm/esfmopl3.asm), so that
;    Windows lists the device in the Multimedia control panel as a driver
;    of the card.  Device 1 answers here as it does with Opl3Device=1.
;    That holds if ESFMOPL3.DRV is loaded at the card's first count, or if
;    ES1869.VXD told Windows at the card's start that it is there, so that
;    Windows loads it after this driver (function 040E); otherwise this
;    driver counts device 1 itself for the session (opl_drv_latch), and
;    ESFMOPL3.DRV, which asks this count, has none.
; 5. The voices play the bank as DMX does in Chocolate Doom, for which it
;    was made: a second voice with the instrument's fine tuning, the base
;    note offsets an octave below DMX's table, the fixed notes of the drums,
;    the carrier's level from the volume and the modulator's from the
;    instrument (and at least the carrier's in additive mode).  The levels
;    are General MIDI's 40 log10 curve in the chip's 0.75 dB steps for the
;    velocity and controllers 7 and 11, the pitch is tuned to the chip's
;    49,612.5 Hz, and TrueSides puts left on the left output, as for device
;    0.  Controllers 1, 6/38 with RPN 0, 7, 10, 11, 64, 120, 121 and 123 to
;    127, programs, pitch bend and running status play; a GM, GS or XG reset
;    in a long message sets every channel back.  A program change lets the
;    channel's sustain pedal up, as device 0's does (PedalRelease), since a
;    pedal still down at a new instrument is left over from the part
;    before, and FM's sustained notes don't fade.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

MODM_GETNUMDEVS equ 1
MODM_GETDEVCAPS equ 2
MMSYSERR_NOTENABLED equ 3

; a GENMIDI instrument (third_party/genmidi/README.md)
GI_FLAGS        equ 0           ; word: GIF_*
GI_FINE         equ 2           ; the second voice's tuning, 128 = none
GI_FIXED        equ 3           ; the note of a fixed-pitch instrument
GI_VOICE        equ 4           ; two voices of GV_SIZE bytes
GI_SIZE         equ 36
GIF_FIXED       equ 1
GIF_DOUBLE      equ 4
GV_MOD          equ 0           ; the modulator: GO_*
GV_FB           equ 6           ; register C0h bits 3:0
GV_CAR          equ 7           ; the carrier: GO_*
GV_OFFSET       equ 14          ; word: the base note offset
GV_SIZE         equ 16
GO_20           equ 0           ; registers 20h, 60h, 80h and E0h
GO_60           equ 1
GO_80           equ 2
GO_E0           equ 3
GO_KSL          equ 4           ; register 40h bits 7:6
GO_TL           equ 5           ; and bits 5:0
GENMIDI_N       equ 175
OPL_DRUM_FIRST  equ 35          ; the drums of channel 10, instruments
OPL_DRUM_LAST   equ 81          ; 128 and up

; fix_process's look at device 1 (esfmgm.asm), with BP = its frame, the
; arguments of modMessage, and DS = DGROUP.  CF set: the call was handled
; here and DX:AX is its result; CF clear: ESS's code answers it.  Keeps SI
; and DI
opl_route:
        push    si
        push    di
        mov     ax,[bp+0x12]            ; uMsg
        cmp     ax,MODM_GETNUMDEVS
        je      .numdevs
        cmp     word [bp+0x14],1
        je      .dev1
        cmp     word [bp+0x14],0
        jne     .ess
        call    opl_mine                ; a queued message of device 1's
        jc      .client                 ; client comes as device 0's
.ess:   pop     di
        pop     si
        clc
        ret
.numdevs:
        call    opl_ess                 ; 1 with an ES1869 makes 2, but
        cmp     ax,1                    ; ESFMOPL3.DRV may count device 1
        jne     .done
        or      dx,dx
        jnz     .done
        call    opl_drv_latch
        mov     ax,1
        xor     dx,dx
        test    word [fix_opts],OPT_OPL3_DRV
        jnz     .done
        inc     ax
        jmp     .done
.dev1:  cmp     ax,MODM_OPEN
        je      .open
        cmp     ax,MODM_GETDEVCAPS
        je      .caps
        call    opl_mine
        jc      .client
        call    opl_ess                 ; volume, prepare: as device 0's
        jmp     .done
.open:  call    opl_open
        jmp     .done
.caps:  call    opl_caps
        jmp     .done
.client:
        mov     ax,[bp+0x12]            ; SI = dev, DI = the client
        cmp     ax,MODM_DATA
        je      .data
        cmp     ax,MODM_LONGDATA
        je      .long
        cmp     ax,MODM_RESET
        je      .reset
        call    opl_close
        jmp     .done
.data:  call    opl_data
        jmp     .done
.long:  call    opl_long
        jmp     .done
.reset: call    opl_reset_msg
.done:  pop     di
        pop     si
        stc
        ret

; Opl3Device=2, at the first count of an ES1869: device 1 is
; ESFMOPL3.DRV's if that driver is loaded, or if ES1869.VXD told Windows at
; the card's start that it is there (opl_vxd_said), so that Windows loads
; it after this driver, and this driver's for the rest of the session
; otherwise, so that the device is neither missing nor counted twice
; whatever order Windows loads the two drivers in.  The count comes at
; task time.  BP = modMessage's frame, DS = DGROUP, keeps SI and DI
opl_drv_latch:
        cmp     byte [fix_opl3_latched],0
        jne     .out
        mov     byte [fix_opl3_latched],1
        test    word [fix_opts],OPT_OPL3_DRV
        jz      .out
        push    ds
        push    word fix_opl3_mod
        kernel  GetModuleHandle
        or      ax,ax
        jnz     .out
        call    opl_vxd_said
        jnc     .out
        and     word [fix_opts],~OPT_OPL3_DRV
.out:   ret

; CF clear if ES1869.VXD told Windows at the start of the ES1869 whose
; devnode is GETNUMDEVS's dwParam1 that its ESFMOPL3.DRV is there (the
; extension's function 040E): ESS's VxD and an older extension answer
; with CF set.  BP = modMessage's frame, DS = DGROUP, keeps SI and DI
opl_vxd_said:
        push    si
        push    ecx
        mov     ax,[bp+0x0A]            ; dwParam1: the devnode
        mov     dx,[bp+0x0C]
        mov     si,[device_list]
.find:  or      si,si
        jz      .no
        cmp     [si+DEV_DEVNODE],ax
        jne     .next
        cmp     [si+DEV_DEVNODE+2],dx
        je      .found
.next:  mov     si,[si+DEV_NEXT]
        jmp     .find
.found: mov     ax,[si+DEV_VXD_ENTRY]
        or      ax,[si+DEV_VXD_ENTRY+2]
        jz      .no
        mov     ecx,[si+DEV_DEVNODE]
        xor     ax,ax
        mov     dx,0x040E
        call    far [si+DEV_VXD_ENTRY]
        jc      .no
        cmp     ax,1
        jne     .no
        clc
        jmp     .out
.no:    stc
.out:   pop     ecx
        pop     si
        ret

; CF set if this call is a message of device 1's client (close, data, long
; data, reset): SI = dev, DI = the client
opl_mine:
        mov     ax,[bp+0x12]
        cmp     ax,MODM_CLOSE
        je      .msg
        cmp     ax,MODM_DATA
        jb      .no
        cmp     ax,MODM_RESET
        ja      .no
.msg:   cmp     word [bp+0x10],0        ; dwUser: the client, in DGROUP
        jne     .no
        mov     di,[bp+0x0E]
        or      di,di
        jz      .no
        mov     si,[di+CLIENT_DEV]
        or      si,si
        jz      .no
        cmp     [si+DEV_OPL_CLIENT],di
        jne     .no
        stc
        ret
.no:    clc
        ret

; ESS's modMessage with this call's arguments as device 0's: DX:AX is its
; result
opl_ess:
        push    word 0
        push    word [bp+0x12]
        push    word [bp+0x10]
        push    word [bp+0x0E]
        push    word [bp+0x0C]
        push    word [bp+0x0A]
        push    word [bp+0x08]
        push    word [bp+0x06]
        push    cs
        call    modMessage_orig
        ret

; MODM_OPEN of device 1: ESS's open as device 0's (one client at a time,
; FM from ES1869.VXD, MOM_OPEN), then the chip as an OPL3 after a reset
opl_open:
        call    opl_ess
        or      ax,ax
        jnz     .out
        push    ax
        push    dx
        les     bx,[bp+0x0E]            ; where ESS put the client
        mov     di,[es:bx]
        mov     si,[di+CLIENT_DEV]
        mov     [si+DEV_OPL_CLIENT],di
        call    opl_claim
        pop     dx
        pop     ax
.out:   ret

; MODM_GETDEVCAPS of device 1: ESS's caps of the ES1869 (device 0's), with
; device 1's name, "OPL3 FM Synthesis (220)" for FM_Base 220h
opl_caps:
        call    opl_ess
        or      ax,ax
        jnz     .out
        push    ax
        push    dx
        mov     ax,[bp+0x06]            ; dwParam2: the devnode
        mov     dx,[bp+0x08]
        mov     si,[device_list]
.find:  or      si,si
        jz      .done
        cmp     [si+DEV_DEVNODE],ax
        jne     .next
        cmp     [si+DEV_DEVNODE+2],dx
        je      .found
.next:  mov     si,[si+DEV_NEXT]
        jmp     .find
.found: sub     sp,32                   ; the name, on the stack
        mov     di,sp
        push    di
        push    ss
        pop     es
        mov     bx,opl_name
.str:   mov     al,[cs:bx]
        inc     bx
        or      al,al
        jz      .hex
        stosb
        jmp     .str
.hex:   mov     dx,[si+DEV_FM_PORT]     ; in hex, without leading zeros
        mov     cx,4
        xor     ah,ah                   ; digits written
.digit: rol     dx,4
        mov     al,dl
        and     al,0x0F
        jnz     .put
        or      ah,ah
        jnz     .put
        cmp     cx,1
        jne     .skip
.put:   inc     ah
        add     al,'0'
        cmp     al,'9'
        jbe     .store
        add     al,'A' - '9' - 1
.store: stosb
.skip:  loop    .digit
        mov     ax,')'                  ; and the NUL
        stosw
        pop     si                      ; the name
        les     bx,[bp+0x0A]            ; MDEVICECAPSEX: cbSize, pCaps
        mov     cx,[es:bx]
        cmp     word [es:bx+2],0
        jne     .max
        cmp     cx,6 + 32
        jbe     .room
.max:   mov     cx,6 + 32
.room:  sub     cx,6                    ; szPname, at 6, as far as it fits
        jbe     .free
        les     di,[es:bx+4]
        add     di,6
        push    ds
        push    ss
        pop     ds
        rep     movsb
        pop     ds
.free:  add     sp,32
.done:  pop     dx
        pop     ax
.out:   ret

; MODM_CLOSE of device 1: its notes cut, the chip back in native mode,
; then ESS's close (FM back to ES1869.VXD, MOM_CLOSE).  SI = dev
opl_close:
        call    opl_stop
        mov     word [si+DEV_OPL_CLIENT],0
        call    opl_ess
        ret

; MODM_DATA of device 1: a short message, with running status.  SI = dev
opl_data:
        test    byte [si+DEV_FLAGS],4   ; suspended, as ESS's code answers
        jnz     .off
        call    opl_ensure
        mov     ax,[bp+0x0A]            ; the status, data 1
        mov     cl,[bp+0x0C]            ; data 2
        test    al,0x80
        jnz     .status
        mov     cl,ah                   ; running status: two data bytes
        mov     ah,al
        mov     al,[si+DEV_OPL_STATUS]
        or      al,al
        jz      .done
        jmp     .play
.status:
        cmp     al,0xF8
        jae     .done                   ; real-time: running status stays
        cmp     al,0xF0
        jb      .chan
        mov     byte [si+DEV_OPL_STATUS],0      ; system common ends it
        jmp     .done
.chan:  mov     [si+DEV_OPL_STATUS],al
.play:  mov     bl,al
        mov     bh,ah
        call    opl_message
.done:  xor     ax,ax
        xor     dx,dx
        ret
.off:   mov     ax,MMSYSERR_NOTENABLED
        xor     dx,dx
        ret

; MODM_LONGDATA of device 1: the buffer as a MIDI stream, with running
; status across buffers, and a GM, GS or XG reset; the buffer goes back
; with MOM_DONE.  SI = dev, DI = the client
opl_long:
        les     bx,[bp+0x0A]            ; MIDIHDR
        test    byte [es:bx+MIDIHDR_FLAGS],MHDR_PREPARED
        jz      .unprepared
        test    byte [si+DEV_FLAGS],4
        jnz     .off
        call    opl_ensure
        push    di
        mov     cx,[es:bx+MIDIHDR_LENGTH]
        cmp     word [es:bx+MIDIHDR_LENGTH+2],0
        je      .len
        mov     cx,0xFFFF
.len:   push    es
        push    bx
        les     di,[es:bx+MIDIHDR_DATA]
        jcxz    .done
.byte:  mov     al,[es:di]
        inc     di
        push    cx
        push    es
        push    di
        call    opl_byte
        pop     di
        pop     es
        pop     cx
        loop    .byte
.done:  pop     di                      ; the header
        pop     es
        call    fix_long_done           ; (the client) MHDR_DONE, MOM_DONE
        xor     ax,ax
        xor     dx,dx
        ret
.unprepared:
        mov     ax,MIDIERR_UNPREPARED
        xor     dx,dx
        ret
.off:   mov     ax,MMSYSERR_NOTENABLED
        xor     dx,dx
        ret

; MODM_RESET of device 1: every note off, every channel as at the start.
; SI = dev
opl_reset_msg:
        test    byte [si+DEV_FLAGS],4
        jnz     .off
        call    opl_ensure
        call    opl_reset_all
        xor     ax,ax
        xor     dx,dx
        ret
.off:   mov     ax,MMSYSERR_NOTENABLED
        xor     dx,dx
        ret

; one byte of a long message for device 1, AL.  SI = dev
opl_byte:
        cmp     al,0xF8
        jae     .out                    ; real-time
        cmp     byte [si+DEV_OPL_LSYX],0
        je      .stream
        cmp     al,0xF7                 ; in a SysEx
        je      .end
        test    al,0x80
        jnz     .abort                  ; another status ends it
        mov     bl,[si+DEV_OPL_LSYX]
        cmp     bl,OPL_SYX
        jae     .over
        xor     bh,bh
        mov     [si+bx+DEV_OPL_SYX],al
        inc     byte [si+DEV_OPL_LSYX]
        jmp     .out
.over:  mov     byte [si+DEV_OPL_LSYX],OPL_SYX + 1      ; no reset
        jmp     .out
.end:   call    opl_sysex
        mov     byte [si+DEV_OPL_LSYX],0
        jmp     .out
.abort: mov     byte [si+DEV_OPL_LSYX],0
.stream:
        test    al,0x80
        jz      .data
        cmp     al,0xF0
        jb      .chan
        jne     .common
        mov     [si+DEV_OPL_SYX],al     ; F0: a SysEx starts
        mov     byte [si+DEV_OPL_LSYX],1
.common:
        mov     byte [si+DEV_OPL_LSTAT],0       ; no running status after it
        jmp     .out
.chan:  mov     [si+DEV_OPL_LSTAT],al
        mov     byte [si+DEV_OPL_LGOT],0
        mov     byte [si+DEV_OPL_LNEED],2
        and     al,0xE0
        cmp     al,0xC0                 ; C0h and D0h have one data byte
        jne     .out
        mov     byte [si+DEV_OPL_LNEED],1
        jmp     .out
.data:  mov     bl,[si+DEV_OPL_LSTAT]
        or      bl,bl
        jz      .out
        cmp     byte [si+DEV_OPL_LNEED],1
        je      .one
        cmp     byte [si+DEV_OPL_LGOT],0
        jne     .two
        mov     [si+DEV_OPL_LD1],al
        mov     byte [si+DEV_OPL_LGOT],1
        jmp     .out
.two:   mov     byte [si+DEV_OPL_LGOT],0
        mov     cl,al
        mov     bh,[si+DEV_OPL_LD1]
        call    opl_message
        jmp     .out
.one:   mov     bh,al
        xor     cl,cl
        call    opl_message
.out:   ret

; the SysEx in DEV_OPL_SYX ended: a GM (or GM2), GS or XG reset sets every
; channel back, and the master volume turns every note down as on device 0
; (fix_master_curve).  SI = dev
;   F0 7E dd 09 01 F7                   GM System On (09 03: GM2)
;   F0 41 dd 42 12 40 00 7F 00 41 F7    GS reset
;   F0 43 1n 4C 00 00 7E 00 F7          XG System On
;   F0 7F dd 04 01 ll mm F7             master volume
opl_sysex:
        push    cx
        push    dx
        push    di
        mov     al,[si+DEV_OPL_LSYX]
        mov     ah,[si+DEV_OPL_SYX+1]
        cmp     ah,0x7F
        je      .master
        cmp     ah,0x7E
        je      .gm
        cmp     ah,0x41
        je      .gs
        cmp     ah,0x43
        je      .xg
        jmp     .out
.gm:    cmp     al,5
        jne     .out
        cmp     byte [si+DEV_OPL_SYX+3],0x09
        jne     .out
        mov     ah,[si+DEV_OPL_SYX+4]
        cmp     ah,0x01
        je      .reset
        cmp     ah,0x03
        je      .reset
        jmp     .out
.gs:    cmp     al,10
        jne     .out
        mov     di,fix_gs_tail          ; 42 12 40 00 7F 00 41
        mov     cx,7
        jmp     .tail
.xg:    cmp     al,8
        jne     .out
        mov     ah,[si+DEV_OPL_SYX+2]
        and     ah,0xF0
        cmp     ah,0x10
        jne     .out
        mov     di,fix_xg_tail          ; 4C 00 00 7E 00
        mov     cx,5
.tail:  lea     bx,[si+DEV_OPL_SYX+3]
.cmp:   mov     al,[bx]
        cmp     al,[di]
        jne     .out
        inc     bx
        inc     di
        loop    .cmp
.reset: call    opl_reset_all
        jmp     .out
.master:
        cmp     al,7
        jne     .out
        cmp     word [si+DEV_OPL_SYX+3],0x0104
        jne     .out
        mov     bl,[si+DEV_OPL_SYX+6]   ; mm: the 14 bits / 512
        and     bl,0x7F
        shr     bl,2
        xor     bh,bh
        mov     al,[cs:bx+fix_master_curve]
        mov     [si+DEV_GM_MASTER],al
        xor     dl,dl
.level: mov     al,2                    ; every channel's notes
        call    opl_update
        inc     dl
        cmp     dl,16
        jb      .level
.out:   pop     di
        pop     dx
        pop     cx
        ret

; the chip in OPL3 mode for device 1 if a chip_reset took it back (a power
; resume).  SI = dev
opl_ensure:
        test    byte [si+DEV_OPL_READY],OPL_ENGINE
        jnz     .out
        call    opl_start
.out:   ret

; device 1 opened: the chip out of native mode and as an OPL3 after a
; reset, as ES1869.VXD leaves it for a program that takes FM with a port
; access (o5:0BD0): OPL2 mode, both timers masked, every key off and every
; operator silent.  From then on the chip is the program's, and the device
; writes nothing to it until a MIDI message comes.  SI = dev
opl_claim:
        push    ax
        push    bx
        push    dx
        mov     dx,[si+DEV_FM_PORT]
        xor     al,al
        out     dx,al                   ; native mode left
        call    opl_wait
        mov     bx,0x105
        mov     al,1                    ; the second bank, for these writes
        call    opl_out
        mov     bx,0x004
        mov     al,0x60                 ; both timers masked
        call    opl_out
        xor     al,al
        mov     bx,0x104                ; two operators on every channel
        call    opl_out
        mov     bx,0x001
        call    opl_out
        mov     bx,0x008
        call    opl_out
        mov     bx,0x0BD                ; no rhythm, its keys off
        call    opl_out
        call    opl_voices_off
        mov     bx,0x105                ; OPL2 mode
        call    opl_out
        mov     byte [si+DEV_OPL_READY],OPL_HAS
        pop     dx
        pop     bx
        pop     ax
        ret

; every voice keyed off and silent at once, and the device's voices as at
; the start.  SI = dev
opl_voices_off:
        push    cx
        push    di
        mov     cx,OPL_VOICES
        lea     di,[si+DEV_OPL_VOICE]
.voice: mov     byte [di+OV_FLAGS],0
        mov     word [di+OV_AGE],0
        mov     byte [di+OV_B0],0
        call    opl_mute                ; keyed off and silent
        add     di,OV_SIZE
        loop    .voice
        pop     di
        pop     cx
        ret

; the chip in OPL3 mode for device 1's General MIDI, and the device's
; voices and channels as at the start.  SI = dev
opl_start:
        push    ax
        push    bx
        push    cx
        push    dx
        push    di
        mov     dx,[si+DEV_FM_PORT]
        xor     al,al
        out     dx,al                   ; native mode left
        call    opl_wait
        mov     bx,0x105
        mov     al,1                    ; OPL3 mode
        call    opl_out
        mov     bx,0x104
        xor     al,al                   ; two operators on every channel
        call    opl_out
        mov     bx,0x001
        mov     al,0x20                 ; the waveforms on, as DMX
        call    opl_out
        mov     bx,0x008
        mov     al,0x40                 ; NTS, as DMX
        call    opl_out
        mov     bx,0x0BD
        xor     al,al                   ; no rhythm, shallow vibrato
        call    opl_out
        mov     bx,0x004
        mov     al,0x60                 ; both timers masked
        call    opl_out
        call    opl_voices_off
        mov     cx,16
        lea     di,[si+DEV_OPL_CHAN]
.chan:  call    opl_chan_init
        add     di,OCH_SIZE
        loop    .chan
        xor     ax,ax
        mov     [si+DEV_OPL_STATUS],al
        mov     [si+DEV_OPL_CLOCK],ax
        mov     [si+DEV_OPL_LSTAT],al
        mov     [si+DEV_OPL_LGOT],al
        mov     [si+DEV_OPL_LSYX],al
        mov     [si+DEV_MPC],al
        mov     [si+DEV_GM_MASTER],al
        mov     byte [si+DEV_OPL_READY],OPL_HAS | OPL_ENGINE
        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; device 1's close: every note cut, also those its program played itself,
; then the chip back in native mode (chip_reset).  SI = dev
opl_stop:
        test    byte [si+DEV_OPL_READY],OPL_HAS
        jz      .out
        test    byte [si+DEV_FLAGS],4   ; suspended: FM isn't the driver's
        jnz     .clear
        push    ax
        push    bx
        mov     bx,0x105
        mov     al,1                    ; the second bank reachable
        call    opl_out
        xor     al,al
        mov     bx,0x104                ; a key for each channel
        call    opl_out
        mov     bx,0x0BD                ; no rhythm, its keys off
        call    opl_out
        call    opl_voices_off
        pop     bx
        pop     ax
        push    si
        push    cs
        call    chip_reset              ; clears DEV_OPL_READY first
        ret
.clear: mov     byte [si+DEV_OPL_READY],0
.out:   ret

; all_notes_off of an ES1869 whose OPL3 device has the chip (a power
; suspend, esfmfix.asm), and the resets: every voice keyed off, also those
; its program keyed on itself, rhythm off, and every pedal up.  SI = dev
opl_all_off:
        push    ax
        push    bx
        push    cx
        push    di
        mov     cx,16
        lea     di,[si+DEV_OPL_CHAN]
.chan:  mov     byte [di+OCH_PEDAL],0
        add     di,OCH_SIZE
        loop    .chan
        mov     cx,OPL_VOICES
        lea     di,[si+DEV_OPL_VOICE]
.voice: test    byte [di+OV_FLAGS],OVF_ON
        jz      .quiet
        call    opl_key_off
        jmp     .next
.quiet: call    opl_key_reg             ; the program's note, if any
.next:  add     di,OV_SIZE
        loop    .voice
        mov     bx,0x0BD
        xor     al,al                   ; no rhythm, its keys off
        call    opl_out
        pop     di
        pop     cx
        pop     bx
        pop     ax
        ret

; every voice off and every channel as at the start (MODM_RESET, the
; resets).  SI = dev
opl_reset_all:
        push    cx
        push    di
        call    opl_all_off
        mov     byte [si+DEV_MPC],0     ; a new song (esfmmpc.asm)
        mov     byte [si+DEV_GM_MASTER],0
        mov     cx,16
        lea     di,[si+DEV_OPL_CHAN]
.chan:  call    opl_chan_init
        add     di,OCH_SIZE
        loop    .chan
        pop     di
        pop     cx
        ret

; channel DI as at the start
opl_chan_init:
        mov     byte [di+OCH_PROG],0
        mov     byte [di+OCH_VOL],100
        mov     byte [di+OCH_PAN],0x30
        mov     byte [di+OCH_SEMI],2
        mov     byte [di+OCH_CENT],0
        ; and what controller 121 resets (RP-015)
opl_ctl_reset:
        mov     byte [di+OCH_EXPR],127
        mov     byte [di+OCH_MOD],0
        mov     byte [di+OCH_PEDAL],0
        mov     word [di+OCH_BEND],0x2000
        mov     word [di+OCH_RPN],0x7F7F
        ; the bend range in 1/256 semitones from RPN 0's semitones and cents
opl_range:
        push    ax
        push    bx
        push    dx
        mov     al,[di+OCH_CENT]
        xor     ah,ah
        mov     bx,256
        mul     bx
        mov     bx,100
        div     bx
        mov     bl,[di+OCH_SEMI]
        cmp     bl,24
        jbe     .semi
        mov     bl,24
.semi:  add     ah,bl
        mov     [di+OCH_RANGE],ax
        pop     dx
        pop     bx
        pop     ax
        ret

; a channel message for device 1: BL = status, BH and CL its data bytes.
; SI = dev
opl_message:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     dl,bl
        and     dl,0x0F                 ; the channel
        mov     dh,bh
        and     dh,0x7F
        and     cl,0x7F
        mov     al,bl                   ; MpcSongs (esfmmpc.asm)
        mov     ah,dh
        call    fix_mpc
        jc      .out
        mov     al,bl
        and     al,0xF0
        cmp     al,0x80
        je      .off
        cmp     al,0x90
        je      .on
        cmp     al,0xB0
        je      .ctl
        cmp     al,0xC0
        je      .prog
        cmp     al,0xE0
        je      .bend
        jmp     .out                    ; pressure isn't played
.off:   call    opl_note_off
        jmp     .out
.on:    call    opl_note_on
        jmp     .out
.ctl:   call    opl_control
        jmp     .out
.prog:  mov     al,dl
        call    opl_chrec
        mov     [bx+OCH_PROG],dh
        test    word [fix_opts],OPT_PEDAL       ; PedalRelease: the pedal
        jz      .out                    ; goes up, as device 0's does
        cmp     byte [bx+OCH_PEDAL],0   ; (esfmped.asm)
        je      .out
        mov     byte [bx+OCH_PEDAL],0
        mov     al,1                    ; the held notes end
        call    opl_chan_off
        jmp     .out
.bend:  mov     al,cl                   ; 14 bits, data 1 the low 7
        xor     ah,ah
        shl     ax,7
        or      al,dh
        mov     cx,ax
        mov     al,dl
        call    opl_chrec
        mov     [bx+OCH_BEND],cx
        mov     al,8
        call    opl_update              ; the channel's notes bend
.out:   pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; note on: DL = the channel, DH = the key, CL = the velocity.  SI = dev
opl_note_on:
        or      cl,cl
        jz      opl_note_off
        push    ax
        push    bx
        call    opl_same_off            ; the key sounding already ends
        cmp     dl,9                    ; channel 10: the drums
        jne     .melodic
        cmp     dh,OPL_DRUM_FIRST
        jb      .out
        cmp     dh,OPL_DRUM_LAST
        ja      .out
        mov     al,dh
        add     al,128 - OPL_DRUM_FIRST
        jmp     .instr
.melodic:
        mov     al,dl
        call    opl_chrec
        mov     al,[bx+OCH_PROG]
.instr: xor     ah,ah
        push    ax
        shl     ax,1
        call    opl_voice_on            ; its first voice
        pop     ax
        call    opl_gi_ax
        test    byte [gs:bx+GI_FLAGS],GIF_DOUBLE
        jz      .out
        shl     ax,1
        inc     ax
        call    opl_voice_on            ; and its second
.out:   pop     bx
        pop     ax
        ret

; note off: DL = the channel, DH = the key; with the pedal down the note
; goes on until the pedal lifts.  SI = dev
opl_note_off:
        push    ax
        push    bx
        push    cx
        push    di
        mov     al,dl
        call    opl_chrec
        mov     cx,OPL_VOICES
        lea     di,[si+DEV_OPL_VOICE]
.voice: test    byte [di+OV_FLAGS],OVF_ON
        jz      .next
        cmp     [di+OV_CHAN],dl
        jne     .next
        cmp     [di+OV_KEY],dh
        jne     .next
        cmp     byte [bx+OCH_PEDAL],0
        je      .off
        or      byte [di+OV_FLAGS],OVF_HELD
        jmp     .next
.off:   call    opl_key_off
.next:  add     di,OV_SIZE
        loop    .voice
        pop     di
        pop     cx
        pop     bx
        pop     ax
        ret

; the voices of channel DL whose key is DH keyed off, the pedal or not
opl_same_off:
        push    cx
        push    di
        mov     cx,OPL_VOICES
        lea     di,[si+DEV_OPL_VOICE]
.voice: test    byte [di+OV_FLAGS],OVF_ON
        jz      .next
        cmp     [di+OV_CHAN],dl
        jne     .next
        cmp     [di+OV_KEY],dh
        jne     .next
        call    opl_key_off
.next:  add     di,OV_SIZE
        loop    .voice
        pop     di
        pop     cx
        ret

; controller DH = CL on channel DL.  SI = dev
opl_control:
        push    ax
        push    bx
        push    di
        mov     al,dl
        call    opl_chrec
        mov     di,bx                   ; DI = the channel
        cmp     dh,1
        je      .mod
        cmp     dh,6
        je      .data_msb
        cmp     dh,7
        je      .volume
        cmp     dh,10
        je      .pan
        cmp     dh,11
        je      .expr
        cmp     dh,38
        je      .data_lsb
        cmp     dh,64
        je      .pedal
        cmp     dh,98
        je      .nrpn
        cmp     dh,99
        je      .nrpn
        cmp     dh,100
        je      .rpn_lsb
        cmp     dh,101
        je      .rpn_msb
        cmp     dh,120
        je      .sound_off
        cmp     dh,121
        je      .reset
        cmp     dh,123
        jb      .out
        mov     al,0                    ; 123-127: all notes off
        call    opl_chan_off
        jmp     .out
.mod:   mov     [di+OCH_MOD],cl
        mov     al,1
        jmp     .update
.volume:
        mov     [di+OCH_VOL],cl
        mov     al,2
        jmp     .update
.expr:  mov     [di+OCH_EXPR],cl
        mov     al,2
        jmp     .update
.pan:   mov     al,0x30                 ; 0-47 left, 48-80 both, 81-127 right
        cmp     cl,48
        jb      .left
        cmp     cl,80
        jbe     .setpan
        mov     al,0x20                 ; an OPL3's right
        jmp     .sides
.left:  mov     al,0x10                 ; and its left
.sides: test    word [fix_opts],OPT_SIDES
        jz      .setpan
        xor     al,0x30                 ; the chip plays bit 4 on the right
.setpan:
        mov     [di+OCH_PAN],al
        mov     al,4
        jmp     .update
.pedal: cmp     cl,64
        jb      .up
        mov     byte [di+OCH_PEDAL],1
        jmp     .out
.up:    mov     byte [di+OCH_PEDAL],0
        mov     al,1                    ; the held notes end
        call    opl_chan_off
        jmp     .out
.nrpn:  mov     word [di+OCH_RPN],0x7F7F
        jmp     .out
.rpn_lsb:
        mov     [di+OCH_RPN],cl
        jmp     .out
.rpn_msb:
        mov     [di+OCH_RPN+1],cl
        jmp     .out
.data_msb:
        cmp     word [di+OCH_RPN],0     ; RPN 0, the bend range
        jne     .out
        mov     [di+OCH_SEMI],cl
        mov     byte [di+OCH_CENT],0
        jmp     .range
.data_lsb:
        cmp     word [di+OCH_RPN],0
        jne     .out
        mov     [di+OCH_CENT],cl
.range: call    opl_range
        mov     al,8
        jmp     .update
.sound_off:
        mov     al,2                    ; cut at once
        call    opl_chan_off
        jmp     .out
.reset: mov     al,1                    ; the held notes end
        call    opl_chan_off
        call    opl_ctl_reset
        mov     al,1 | 2 | 8
.update:
        call    opl_update
.out:   pop     di
        pop     bx
        pop     ax
        ret

; the notes of channel DL: AL = 0 off as note offs (the pedal holds them),
; 1 the ones the pedal held off, 2 every one cut at once.  SI = dev;
; keeps all
opl_chan_off:
        push    ax
        push    bx
        push    cx
        push    di
        push    ax
        mov     al,dl
        call    opl_chrec
        mov     ah,[bx+OCH_PEDAL]
        pop     bx
        mov     al,bl                   ; AL = the kind, AH = the pedal
        mov     cx,OPL_VOICES
        lea     di,[si+DEV_OPL_VOICE]
.voice: test    byte [di+OV_FLAGS],OVF_ON
        jz      .next
        cmp     [di+OV_CHAN],dl
        jne     .next
        cmp     al,1
        jb      .notes
        ja      .cut
        test    byte [di+OV_FLAGS],OVF_HELD
        jz      .next
        call    opl_key_off
        jmp     .next
.notes: or      ah,ah
        jz      .off
        or      byte [di+OV_FLAGS],OVF_HELD
        jmp     .next
.off:   call    opl_key_off
        jmp     .next
.cut:   call    opl_key_off
        call    opl_mute
.next:  add     di,OV_SIZE
        loop    .voice
        pop     di
        pop     cx
        pop     bx
        pop     ax
        ret

; what changed on channel DL goes to its notes: AL bit 0 the vibrato, bit
; 1 the level, bit 2 the pan, bit 3 the pitch.  SI = dev
opl_update:
        push    cx
        push    di
        mov     cx,OPL_VOICES
        lea     di,[si+DEV_OPL_VOICE]
.voice: test    byte [di+OV_FLAGS],OVF_ON
        jz      .next
        cmp     [di+OV_CHAN],dl
        jne     .next
        test    al,1
        jz      .level
        call    opl_vib
.level: test    al,2
        jz      .pan
        call    opl_level
.pan:   test    al,4
        jz      .pitch
        call    opl_pan
.pitch: test    al,8
        jz      .next
        call    opl_freq
.next:  add     di,OV_SIZE
        loop    .voice
        pop     di
        pop     cx
        ret

; a voice of instrument voice AX (instrument * 2 + voice) for the note DH
; of channel DL at velocity CL, keyed on.  SI = dev
opl_voice_on:
        push    di
        call    opl_alloc
        cmp     [di+OV_INSTR],ax
        je      .loaded
        call    opl_load
.loaded:
        mov     byte [di+OV_FLAGS],OVF_ON
        test    al,1
        jz      .first
        mov     byte [di+OV_FLAGS],OVF_ON | OVF_SECOND
.first: mov     [di+OV_CHAN],dl
        mov     [di+OV_KEY],dh
        mov     [di+OV_VEL],cl
        call    opl_pitch
        call    opl_level
        call    opl_pan
        call    opl_vib
        call    opl_freq
        call    opl_age
        pop     di
        ret

; DI = a voice for a note: one that is off and has the instrument voice
; AX's registers, else the one off the longest, else the second voice on
; the longest, else the voice on the longest, keyed off first.  Keeps all
; but DI
opl_alloc:
        push    bp
        mov     bp,sp
        push    ax                      ; [bp-2]: the instrument voice
        push    bx
        push    cx
        push    dx
        xor     cl,cl                   ; the rank looked for, 0 to 3
.rank:  xor     di,di
        xor     dx,dx                   ; how long ago the best was keyed
        lea     bx,[si+DEV_OPL_VOICE]
        mov     ch,OPL_VOICES
.voice: mov     al,[bx+OV_FLAGS]
        test    al,OVF_ON
        jnz     .on
        cmp     cl,1                    ; off
        je      .cand
        or      cl,cl
        jnz     .next
        mov     ax,[bx+OV_INSTR]
        cmp     ax,[bp-2]
        jne     .next
        jmp     .cand
.on:    cmp     cl,3
        je      .cand
        cmp     cl,2
        jne     .next
        test    al,OVF_SECOND
        jz      .next
.cand:  mov     ax,[si+DEV_OPL_CLOCK]
        sub     ax,[bx+OV_AGE]
        or      di,di
        jz      .take
        cmp     ax,dx
        jbe     .next
.take:  mov     di,bx
        mov     dx,ax
.next:  add     bx,OV_SIZE
        dec     ch
        jnz     .voice
        or      di,di
        jnz     .found
        inc     cl
        jmp     .rank
.found: test    byte [di+OV_FLAGS],OVF_ON
        jz      .out
        call    opl_key_off
.out:   pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret

; the registers of instrument voice AX into voice DI: both operators' 20h,
; 60h, 80h and E0h and the modulator's 40h; the carrier's level, C0h and
; the vibrato follow, forced.  SI = dev; keeps all
opl_load:
        push    ax
        push    bx
        push    dx
        push    bp
        mov     [di+OV_INSTR],ax
        call    opl_gv                  ; BP = the GENMIDI voice
        call    opl_vregs               ; DX = the first operator's registers
        mov     bx,dx
        add     bx,0x20
        mov     al,[gs:bp+GV_MOD+GO_20]
        call    opl_out
        add     bx,0x40                 ; 60h
        mov     al,[gs:bp+GV_MOD+GO_60]
        call    opl_out
        add     bx,0x20                 ; 80h
        mov     al,[gs:bp+GV_MOD+GO_80]
        call    opl_out
        add     bx,0x60                 ; E0h
        mov     al,[gs:bp+GV_MOD+GO_E0]
        call    opl_out
        sub     bx,0xA0                 ; 40h
        mov     al,[gs:bp+GV_MOD+GO_KSL]
        and     al,0xC0
        or      al,[gs:bp+GV_MOD+GO_TL]
        call    opl_out
        add     bx,3 - 0x40 + 0x20      ; the carrier's 20h
        mov     al,[gs:bp+GV_CAR+GO_20]
        call    opl_out
        add     bx,0x40                 ; 60h
        mov     al,[gs:bp+GV_CAR+GO_60]
        call    opl_out
        add     bx,0x20                 ; 80h
        mov     al,[gs:bp+GV_CAR+GO_80]
        call    opl_out
        add     bx,0x60                 ; E0h
        mov     al,[gs:bp+GV_CAR+GO_E0]
        call    opl_out
        mov     byte [di+OV_TL],0xFF
        mov     byte [di+OV_C0],0xFF
        mov     byte [di+OV_VIB],0      ; 20h as the instrument has it
        pop     bp
        pop     dx
        pop     bx
        pop     ax
        ret

; the pitch of voice DI's note without the bend, OV_PITCH, as DMX plays
; its instrument: the fixed note, or the key plus the voice's base note
; offset, an octave up (DMX's table starts an octave above MIDI's), folded
; into notes 12 to 107, and the second voice's fine tuning, (fine / 2 -
; 64) / 32 semitone.  SI = dev; keeps all
opl_pitch:
        push    ax
        push    bx
        push    cx
        push    bp
        call    opl_gi                  ; BX = the instrument
        call    opl_gv                  ; BP = its voice
        test    byte [gs:bx+GI_FLAGS],GIF_FIXED
        jz      .key
        mov     al,[gs:bx+GI_FIXED]
        xor     ah,ah
        jmp     .note
.key:   mov     al,[di+OV_KEY]
        xor     ah,ah
        add     ax,[gs:bp+GV_OFFSET]
.note:  add     ax,12
.low:   cmp     ax,12
        jge     .high
        add     ax,12
        jmp     .low
.high:  cmp     ax,107
        jle     .fold
        sub     ax,12
        jmp     .high
.fold:  mov     ah,al                   ; 1/256 semitones
        xor     al,al
        test    byte [di+OV_INSTR],1
        jz      .set
        mov     cl,[gs:bx+GI_FINE]
        shr     cl,1
        sub     cl,64
        mov     ch,cl
        sar     ch,7                    ; sign
        shl     cx,3                    ; 1/32 semitone in 1/256 semitones
        add     ax,cx
.set:   mov     [di+OV_PITCH],ax
        pop     bp
        pop     cx
        pop     bx
        pop     ax
        ret

; voice DI's carrier level: General MIDI's 40 log10 curve, 0.75 dB a step,
; for its velocity and its channel's controllers 7 and 11.  In additive
; mode the modulator sounds too, at least as low as the carrier.  SI = dev;
; keeps all
opl_level:
        push    ax
        push    bx
        push    dx
        push    bp
        mov     al,[di+OV_CHAN]
        call    opl_chrec
        mov     dl,[bx+OCH_VOL]
        mov     dh,[bx+OCH_EXPR]
        mov     bl,[di+OV_VEL]
        xor     bh,bh
        mov     al,[cs:bx+opl_att]
        mov     bl,dl
        add     al,[cs:bx+opl_att]
        mov     bl,dh
        add     al,[cs:bx+opl_att]      ; at most 189
        add     al,[fix_opl3_att]       ; Opl3Headroom (esfmini.asm), 252
        add     al,[si+DEV_GM_MASTER]   ; the master volume
        jc      .max
        cmp     al,0x3F
        jbe     .att
.max:   mov     al,0x3F
.att:   cmp     al,[di+OV_TL]
        je      .out
        mov     [di+OV_TL],al
        mov     ah,al
        call    opl_gv
        call    opl_vregs               ; DX = the first operator's registers
        mov     bx,dx
        add     bx,3 + 0x40             ; the carrier's 40h
        mov     al,[gs:bp+GV_CAR+GO_KSL]
        and     al,0xC0
        or      al,ah
        call    opl_out
        test    byte [gs:bp+GV_FB],1    ; additive
        jz      .out
        mov     al,[gs:bp+GV_MOD+GO_TL]
        cmp     al,0x3F
        je      .out                    ; a silent modulator stays silent
        cmp     al,ah
        jae     .mod
        mov     al,ah
.mod:   mov     ah,[gs:bp+GV_MOD+GO_KSL]
        and     ah,0xC0
        or      al,ah
        sub     bx,3
        call    opl_out
.out:   pop     bp
        pop     dx
        pop     bx
        pop     ax
        ret

; voice DI's register C0h: the instrument's feedback and connection, and
; its channel's pan.  SI = dev; keeps all
opl_pan:
        push    ax
        push    bx
        push    dx
        push    bp
        mov     al,[di+OV_CHAN]
        call    opl_chrec
        mov     ah,[bx+OCH_PAN]
        call    opl_gv
        mov     al,[gs:bp+GV_FB]
        and     al,0x0F
        or      al,ah
        cmp     al,[di+OV_C0]
        je      .out
        mov     [di+OV_C0],al
        call    opl_vregs               ; BX = the channel's registers
        add     bx,0xC0
        call    opl_out
.out:   pop     bp
        pop     dx
        pop     bx
        pop     ax
        ret

; voice DI's vibrato: both operators' 20h with bit 6 set while its
; channel's controller 1 is up.  SI = dev; keeps all
opl_vib:
        push    ax
        push    bx
        push    dx
        push    bp
        mov     al,[di+OV_CHAN]
        call    opl_chrec
        xor     ah,ah
        cmp     byte [bx+OCH_MOD],0
        je      .want
        mov     ah,0x40
.want:  cmp     ah,[di+OV_VIB]
        je      .out
        mov     [di+OV_VIB],ah
        call    opl_gv
        call    opl_vregs
        mov     bx,dx
        add     bx,0x20
        mov     al,[gs:bp+GV_MOD+GO_20]
        or      al,ah
        call    opl_out
        add     bx,3
        mov     al,[gs:bp+GV_CAR+GO_20]
        or      al,ah
        call    opl_out
.out:   pop     bp
        pop     dx
        pop     bx
        pop     ax
        ret

; voice DI keyed on at its pitch with its channel's bend: registers A0h
; and B0h.  The F-number comes from the table of notes 19 to 31 at block
; 0 for the chip's 49,612.5 Hz, interpolated between the semitones.  SI =
; dev; keeps all
opl_freq:
        push    ax
        push    bx
        push    cx
        push    dx
        push    bp
        mov     al,[di+OV_CHAN]
        call    opl_chrec
        mov     ax,[bx+OCH_BEND]
        sub     ax,0x2000
        imul    word [bx+OCH_RANGE]     ; DX:AX = bend * range
        mov     cx,13
.div:   sar     dx,1
        rcr     ax,1
        loop    .div                    ; / 2000h
        add     ax,[di+OV_PITCH]
        jo      .max                    ; past 32767
        jns     .pos
        xor     ax,ax
        jmp     .top
.pos:   cmp     ax,127 * 256
        jbe     .top
.max:   mov     ax,127 * 256
.top:   xor     bp,bp                   ; octaves below note 19
        sub     ax,19 * 256
.below: jns     .block
        add     ax,12 * 256
        inc     bp
        jmp     .below
.block: xor     dx,dx
        mov     cx,12 * 256
        div     cx                      ; AX = the block, DH the semitone,
        mov     cx,ax                   ; DL the fraction
        mov     bl,dh
        xor     bh,bh
        shl     bx,1
        mov     ax,[cs:bx+opl_fnum+2]
        sub     ax,[cs:bx+opl_fnum]
        mul     dl                      ; AX = the step * the fraction
        mov     al,ah
        xor     ah,ah
        add     ax,[cs:bx+opl_fnum]     ; the F-number at block CL
        or      bp,bp
        jz      .fit
        push    cx
        mov     cx,bp
        shr     ax,cl                   ; below the table: an octave down
        pop     cx
.fit:   cmp     ax,0x3FF
        jbe     .clamp
        shr     ax,1                    ; the top of a block: the next one
        inc     cl
.clamp: cmp     cl,7
        jbe     .write
        mov     cl,7
        mov     ax,0x3FF
.write: push    ax
        call    opl_vregs               ; BX = the channel's registers
        pop     ax
        add     bx,0xA0
        call    opl_out                 ; F-number bits 7:0
        shl     cl,2
        or      cl,ah
        mov     [di+OV_B0],cl
        mov     al,cl
        or      al,0x20                 ; key on
        add     bx,0x10
        call    opl_out
        pop     bp
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; voice DI keyed off: its release begins.  SI = dev; keeps all
opl_key_off:
        call    opl_key_reg
        mov     byte [di+OV_FLAGS],0
        call    opl_age
        ret

; voice DI's channel keyed off in the chip, with the device's table as it
; is.  SI = dev; keeps all
opl_key_reg:
        push    ax
        push    bx
        push    dx
        call    opl_vregs
        add     bx,0xB0
        mov     al,[di+OV_B0]
        call    opl_out
        pop     dx
        pop     bx
        pop     ax
        ret

; voice DI keyed off and both operators silent at once; its registers no
; longer the instrument's.  SI = dev; keeps all
opl_mute:
        push    ax
        push    bx
        push    dx
        call    opl_vregs
        add     bx,0xB0
        mov     al,[di+OV_B0]
        call    opl_out
        mov     bx,dx
        add     bx,0x40
        mov     al,0x3F
        call    opl_out
        add     bx,3
        call    opl_out
        mov     byte [di+OV_FLAGS],0
        mov     byte [di+OV_TL],0x3F
        mov     word [di+OV_INSTR],0xFFFF
        pop     dx
        pop     bx
        pop     ax
        ret

; voice DI gets the next age.  SI = dev
opl_age:
        push    ax
        mov     ax,[si+DEV_OPL_CLOCK]
        inc     ax
        mov     [si+DEV_OPL_CLOCK],ax
        mov     [di+OV_AGE],ax
        pop     ax
        ret

; BX = channel AL's record.  SI = dev
opl_chrec:
        push    ax
        and     ax,0x0F
        shl     ax,4                    ; OCH_SIZE
        lea     bx,[si+DEV_OPL_CHAN]
        add     bx,ax
        pop     ax
        ret

; BX = instrument AX's GENMIDI record, an offset in GS
opl_gi_ax:
        push    ax
        push    dx
        mov     dx,GI_SIZE
        mul     dx
        add     ax,[fix_opl3_off]       ; in GS (esfmfile.asm)
        mov     bx,ax
        pop     dx
        pop     ax
        ret

; BX = the GENMIDI instrument of voice DI, an offset in GS
opl_gi:
        push    ax
        mov     ax,[di+OV_INSTR]
        shr     ax,1
        call    opl_gi_ax
        pop     ax
        ret

; BP = the GENMIDI voice of voice DI, an offset in GS
opl_gv:
        push    ax
        push    bx
        call    opl_gi
        mov     bp,bx
        add     bp,GI_VOICE
        test    byte [di+OV_INSTR],1
        jz      .out
        add     bp,GV_SIZE
.out:   pop     bx
        pop     ax
        ret

; register numbers of voice DI: BX = its channel's, DX = its first
; operator's (the second's is DX + 3), with the bank in bit 8.  SI = dev
opl_vregs:
        mov     bx,di
        sub     bx,si
        sub     bx,DEV_OPL_VOICE
        shr     bx,4                    ; OV_SIZE: the voice, 0 to 17
        xor     dx,dx
        cmp     bx,9
        jb      .bank0
        sub     bx,9
        inc     dh
.bank0: mov     dl,[cs:bx+opl_optab]
        mov     bh,dh
        ret

; OPL3 register BX (bit 8: the second bank) = AL, as an OPL3 driver writes
; it: the register at FM_Base+0 or +2, the value at +1 or +3, each with
; ESS's status reads after it.  SI = dev; keeps all
opl_out:
        push    ax
        push    dx
        mov     dx,[si+DEV_FM_PORT]
        test    bh,1
        jz      .bank0
        add     dx,2
.bank0: push    ax
        mov     al,bl
        out     dx,al
        call    opl_wait
        pop     ax
        inc     dx
        out     dx,al
        call    opl_wait
        pop     dx
        pop     ax
        ret

; ESS's fm_delay status reads at FM_Base.  SI = dev; keeps all
opl_wait:
        push    ax
        push    cx
        push    dx
        mov     dx,[si+DEV_FM_PORT]
        mov     cx,[fm_delay]
        jcxz    .out
.read:  in      al,dx
        loop    .read
.out:   pop     dx
        pop     cx
        pop     ax
        ret

; fm_write's start (seg1:0013), in place of its sub sp,4: ESS's write of a
; native register goes on, unless device 1 has that ES1869's chip; then
; fm_write returns at once.  BP = fm_write's frame, DS = DGROUP; changes BX
; and CX
fix_fm_guard:
        pop     cx
        sub     sp,4
        push    cx
        mov     bx,[bp+0xA]             ; dev
        test    byte [bx+DEV_OPL_READY],OPL_HAS
        jnz     .drop
        ret
.drop:  mov     sp,bp
        pop     bp
        retf    6

; chip_reset's start: hold the driver, and the chip is ESS's again, so
; fm_write takes its writes.  BP = chip_reset's frame; changes BX
fix_reset_enter:
        mov     bx,[bp+6]
        mov     byte [bx+DEV_OPL_READY],0
        jmp     fix_enter

; the first operator of the OPL3's channels 0 to 8
opl_optab:      db 0, 1, 2, 8, 9, 10, 16, 17, 18

; F-numbers of notes 19 (G0) to 31 at block 0 for 49,612.5 Hz, the chip's
; 23.814 MHz master over 480 as measured on the laptop:
; 440 * 2^((n - 69) / 12) * 2^20 / 49612.5
opl_fnum:       dw 518, 549, 581, 616, 652, 691, 732, 776, 822, 871, 923, 977, 1036

; attenuation for a velocity or a controller x: 40 log10(127 / x) dB in
; 0.75 dB steps, 63 at most
opl_att:        db 63, 63, 63, 63, 63, 63, 63, 63, 63, 61, 59, 57, 55, 53, 51, 49
                db 48, 47, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 33
                db 32, 31, 31, 30, 29, 29, 28, 27, 27, 26, 26, 25, 25, 24, 24, 23
                db 23, 22, 22, 21, 21, 20, 20, 19, 19, 19, 18, 18, 17, 17, 17, 16
                db 16, 16, 15, 15, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 11, 11
                db 11, 10, 10, 10, 10, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7
                db 6, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3
                db 3, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0

opl_name:       db 'OPL3 FM Synthesis (', 0

; Freedoom's GENMIDI instruments: the lump without its header and names
opl_bank:       incbin "../../third_party/genmidi/GENMIDI.lmp", 8, GENMIDI_N * GI_SIZE
