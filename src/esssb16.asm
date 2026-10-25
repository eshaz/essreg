; esssb16.com: a Sound Blaster 16 in front of the ES1869 for DOS games.
;
; Notes:
;
; The chip answers DSP commands as a Sound Blaster Pro (version 3.01) and
; plays 16-bit sound only through its own Extended mode, so a game written
; for the Sound Blaster 16 plays 8-bit sound on it at best. This TSR traps
; the game's accesses to seven of the Sound Blaster ports (the mixer, the
; reset, the read data, the command, the status and the 16-bit acknowledge
; ports) and answers as a Sound Blaster 16 (DSP 4.05 and the CT1745 mixer)
; does: every command takes the operands the DSP's firmware takes, an
; answer goes out a byte at a time through the read latch, the rate
; generator of 40h, 41h and 42h runs at v * 44100 / 248 Hz, and the 8-bit
; and 16-bit transfers have their own interrupts in mixer 82h and their own
; acknowledges (+Eh and +Fh).
;
; The 8-bit transfers, the Sound Blaster 16's Cxh and the older commands
; alike, and every recording play or record on Audio 1 in the chip's
; Extended mode; the 16-bit playback plays on Audio 2, where Audio 2 has
; an 8-bit DMA channel, so that both kinds play at once, each with its own
; rate, as on a Sound Blaster 16 they run together. Without one, the
; 16-bit playback plays on Audio 1 too, one transfer at a time. A DSP
; reset would take the chip out of Extended mode, so the front emulates
; the game's reset instead: the transfers stop, the interrupts are
; acknowledged, the FIFOs empty and AAh comes back, and the chip stays in
; Extended mode. The chip takes a byte per DMA cycle whatever the channel
; (DS p.14: the audio channels map to channels 0, 1 and 3; on a 16-bit
; channel it keeps a word's low byte), so the game's programming of its
; 16-bit channel (the H of BLASTER, mixer 81h) is carried over to the 8-bit
; channel that plays it, Audio 2's or Audio 1's. Where the TSR traps that
; channel's ports (QPI, HDPMI32i), it follows the game's programming as it
; comes, and the channel's address and count follow the card's channel, as
; the game reads them for its position; under EMM386, which has no traps
; for the DMA controllers, it reads the channel back at each 16-bit
; command. The firmware's commands that Extended mode doesn't have (the
; direct DAC and ADC, ADPCM, silence and MIDI) go to the chip in its
; Compatibility mode, after a reset of its own, which Audio 2 plays on
; through. Mixer 71h gets bit 5 cleared for each transfer, bit 1 for Audio
; 2's own rate, and the DACs' filters (/F) and Audio 2's 4x oversampling
; (/O) as the command line says.
;
; A program that knows the chip (C6h, then a register command) gets the
; chip as it is until its next reset.
;
; In DOS nothing sees a port access unless the processor traps it, so the
; TSR needs a V86 memory manager for real-mode games: QEMM (its QPI), JEMM
; with QPIEMU.DLL (the same QPI), or MS EMM386 4.46 or later (its own trap
; API, int 2Fh 4A15h). A protected-mode game (DOS/4GW) runs under a DPMI
; host and leaves the memory manager's traps behind: HDPMI32i, the HDPMI
; build that runs its clients with IOPL 0, traps for them, and the TSR
; registers with it too, entering protected mode once at installation and
; staying resident as its client.
;
; Build: nasm -f bin -o esssb16.com esssb16.asm (tools/build_esssb16.py).
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

        BITS    16
        CPU     386
        ORG     0x100

VERSION         equ 0x0405      ; the DSP version the front answers
DSP_POLLS       equ 0x400       ; polls for the chip
MIX_LEN         equ 24          ; the Sound Blaster 16's mixer registers 30h-47h
COPYRIGHT_LEN   equ 45          ; E3h's string with its 0
HOST_PARAS      equ 0x20        ; the DPMI host's data at most: HDPMI's
                                ; 512-byte real-mode stack
emm_table       equ 0x5C        ; in the PSP, unused once resident: EMM386's
stack           equ 0x8C        ; dispatch table (48 bytes: the ports of
stack_top       equ 0x100       ; Audio_Base+4h to +Fh, each with its
                                ; handler, which EMM386 searches at every
                                ; trapped access), and the front's stack
                                ; under QPI and HDPMI32i

; the state flags (S16 in the VxD front); F_AUTO and F_16 are the bits of
; xfer's BL. Audio 2's transfer has F_RUN, F_AUTO, F_LAST and F_PAUSE in
; flags2, and is always 16-bit
F_ESS           equ 0x01        ; the program knows the chip (C6h and a
                                ; register): every access is the chip's
F_EXT           equ 0x02        ; the chip is in Extended mode, its DAC on
F_AUTO          equ 0x04        ; the transfer auto-initializes
F_RUN           equ 0x08        ; a transfer of the front's is on Audio 1
F_16            equ 0x10        ; it is 16-bit (the 16-bit interrupt)
F_LAST          equ 0x20        ; a single-cycle block is queued: the
                                ; auto-initialize ends at the next acknowledge
F_PAUSE         equ 0x40        ; it is paused
F_RESET         equ 0x80        ; the reset port is at 1

; the DSP's state
S_FULL          equ 0x01        ; the read latch has a byte
S_HELD          equ 0x02        ; a written byte waits for the answer's end
S_UART          equ 0x04        ; MIDI UART: the DSP is the chip's until a reset
S_MIDI          equ 0x08        ; MIDI input: the next write ends it
S_COMP          equ 0x10        ; the chip is in Compatibility mode
S_WARM          equ 0x20        ; the next reset is a warm boot
S_SPKR          equ 0x40        ; D1h's flag, for D8h
S_T16           equ 0x80        ; the chip's processor interrupt is the
                                ; 16-bit one (F3h)

; the game's 16-bit channel
V_MASK          equ 0x01        ; masked
V_FF            equ 0x02        ; its controller's flip-flop
V_NEW           equ 0x04        ; programmed since it was carried over
V_LIVE          equ 0x08        ; Audio 1's channel carries its programming

; their bit numbers, for bt, bts and btr
F_EXT_BIT       equ 1
F_AUTO_BIT      equ 2
F_LAST_BIT      equ 5
F_PAUSE_BIT     equ 6
F_RESET_BIT     equ 7
S_FULL_BIT      equ 0
S_HELD_BIT      equ 1
S_COMP_BIT      equ 4

; the trap paths (how_io)
IO_DIRECT       equ 0           ; in and out: ring 0 (EMM386), the installer
IO_HDPMI        equ 1           ; HDPMI32i's untrapped I/O (ring 3)
IO_QPI          equ 2           ; QPI's untrapped I/O (V86)
IO_CHAIN        equ 3           ; the same, with a QPI callback before ours,
                                ; which gets the ports that aren't the front's

; installer results
E_USAGE         equ 1
E_NOCARD        equ 2
E_NOTRAP        equ 3
E_WINDOWS       equ 4
E_LOADED        equ 5

start:  jmp     near install

; --- resident data ---------------------------------------------------------
; The front reaches its data through DI = dbase ([V(x)], an 8-bit offset)

%define V(x) di+((x)-dbase)

signature:      db "ESSSB16", 0 ; found by the installed check
qpi:            dd 0            ; the QPI entry (seg:off), 0 if none
qpi_old:        dd 0            ; the I/O callback before ours, 0 if none
vendor:         dd 0            ; HDPMI32i's vendor entry: offset
vendor_sel:     dw 0            ; and selector
pm_data:        dw 0            ; this segment's data selector, from the
                                ; DPMI switch
save_ss:        dw 0            ; the caller's stack while a shim runs on
save_esp:       dd 0            ; the front's
ctrl:           times 25 db 0   ; the chip's registers A1h-B9h as the front
                                ; last wrote or read them
known:          dd 0            ; bit n set while ctrl+n holds A0h+n
flags:          db 0            ; F_*
flags2:         db 0            ; Audio 2's transfer: F_RUN, F_AUTO,
                                ; F_LAST, F_PAUSE
state:          db 0            ; S_*
cmd:            db 0            ; the last command
prev:           db 0            ; the one before it (FDh)
got:            db 0            ; operands so far
ops:            db 0, 0, 0      ; the operands
latch:          db 0            ; the byte the read port gives
aptr:           dw 0            ; the answer's bytes after the latch
need:           db 0            ; operands still to come
alen:           db 0            ; the answer's bytes after the latch, how many
abuf:           db 0            ; a one-byte answer
dbase:
held:           db 0            ; a byte written while an answer blocks
rate_v:         db 0x38         ; the rate generator's value (9958 Hz)
block8:         dw 0x07FF       ; the 8-bit block, less one (48h, Cxh)
test_reg:       db 0            ; E4h's, which a reset keeps
index:          db 0            ; the mixer index the game wrote
how_io:         db IO_DIRECT    ; IO_*
result:         db 0            ; the byte read, past the registers' restore
base:           dw 0x220        ; Audio_Base
cfg:            dw 0            ; Config_Base, 0 if none
mix80:          db 0xF2         ; mixer 80h, F0h and the card's IRQ's bit
mix81:          db 0x02         ; mixer 81h's bit of the 8-bit channel
dma8:           db 1            ; Audio 1's channel
dma16:          db 5            ; the Sound Blaster 16's 16-bit channel, 0
                                ; for none (16-bit on Audio 1's then)
dma2:           db 4            ; Audio 2's channel, 4 for none: the 16-bit
                                ; playback's when it has one
mchan:          db 1            ; the channel the game's 16-bit one is
                                ; carried over to, Audio 1's or Audio 2's
b9:             db 2            ; B9h: 2 demand transfers, 0 single (/S)
keep71:         db 0xDF         ; mixer 71h at a transfer: the bits kept
set71:          db 0            ; and the ones set (/F, /O)
vchan:          db 0xFF, 0xFF, 0xFF, 0xFF       ; the game's 16-bit channel
                                ; that each path (IO_*) traps, FFh for none
vbits:          db V_MASK       ; V_*
vport:          db 0xC4         ; that channel's address port (its count's
vpagep:         db 0x8B         ; is 2 on) and page port
vaddr:          dw 0            ; its word address, word count, page and
vcount:         dw 0            ; mode as the game wrote them
vpage:          db 0
vmode:          db 0
vhigh:          db 0            ; a read's high byte, from its low byte's
mpage:          db 0            ; the page Audio 1's channel got
xbits:          db 0            ; the transfer's bits (xfer's BL)
mix:            times MIX_LEN db 0      ; the game's view of 30h-47h

; --- the trap shims ------------------------------------------------------

; Each shim sets how_io for the routines it runs; the front keeps every
; register but AL

; QPI's I/O callback (QEMM, QPIEMU), in V86 mode: DX = port, CL = the
; direction (0 in, else out), AL = the byte written; AL = the byte read.
; The caller's stack may be small, so the front runs on its own, and it
; reaches the ports through QPI's untrapped I/O. A callback before ours
; (IO_CHAIN, which the installer puts in qpi_how) gets the ports that
; aren't the front's
qpi_shim:
        pushf
        cli
        push    ds
        push    bp
        mov     bp,cs
        mov     ds,bp
        mov     [save_ss],ss
        mov     [save_esp],esp
        mov     ss,bp
        mov     esp,stack_top
        mov     byte [how_io],IO_QPI
qpi_how         equ $ - 1
        call    front
        mov     ss,[save_ss]
        mov     esp,[save_esp]
        pop     bp
        pop     ds
        jc      .others
        popf
        clc
        retf
.others:
        popf
        jmp     far [cs:qpi_old]

; EMM386's I/O handler, in protected mode at ring 0 on EMM386's stack, with
; interrupts off: DX = port, CL = the direction (0 in, 4 or 8 out), AL =
; the byte, CS and DS ring 0 selectors of this segment as long as the
; resident part. CF set tells EMM386 to do the access itself
emm_shim:
        mov     byte [how_io],IO_DIRECT
        call    front
        retf
emm_pass:                               ; a port in the range but not the front's
        stc
        retf

; HDPMI32i's trap handler, in protected mode at ring 3 with interrupts
; off, called with a 32-bit far frame on the game's stack: EDX = port, ECX
; = the direction (0 in, else out), EAX = the byte, DS = this segment. The
; chip is reached through HDPMI's untrapped I/O, because the ports are
; trapped for this ring too. EBX, ESI and EBP are HDPMI's
hdpmi_shim:
        mov     [save_ss],ss
        mov     [save_esp],esp
        mov     ss,[pm_data]
        mov     esp,stack_top
        mov     byte [how_io],IO_HDPMI
        call    front
        mov     ss,[save_ss]
        mov     esp,[save_esp]
        o32 retf

; --- the chip's ports --------------------------------------------------------

; AL from port DX, and AL to port DX: directly at ring 0 and in the
; installer, else through the trap path's untrapped I/O, because a trapped
; port would come back to the front. AH is kept: callers carry a register
; number in it. CF clear
chip_in:
        cmp     byte [V(how_io)],IO_DIRECT
        je      .in
        push    cx
        mov     ch,0
        jmp     short trap_io
.in:    in      al,dx
        ret
chip_out:
        cmp     byte [V(how_io)],IO_DIRECT
        je      .out
        push    cx
        mov     ch,1
        jmp     short trap_io
.out:   out     dx,al
        ret

; the path's untrapped I/O of port DX, CH = 1 out, 0 in: QPI's 1A01h and
; 1A00h with the byte in BL, or HDPMI32i's function 8 with the byte out in
; CL and the byte in in AL
trap_io:
        push    bx
        push    ax
        mov     bl,al
        mov     cl,al
        cmp     byte [V(how_io)],IO_HDPMI
        je      .hdpmi
        mov     ax,0x1A00
        add     al,ch
        call    far [V(qpi)]
        jmp     short .done
.hdpmi: mov     ax,8
        o32 call far [V(vendor)]
        mov     bl,al
.done:  pop     ax
        test    ch,ch
        jnz     .keep
        mov     al,bl
.keep:  pop     bx
        pop     cx
        clc
        ret

; AL = mixer register AH (mix_read), and mixer register AH = AL (mix_write,
; AX kept); the chip's index goes back to the game's. The front runs with
; interrupts off, and nothing else reaches the mixer while the installer
; does
mix_read:
        push    dx
        call    mix_at
        call    chip_in
        jmp     short mix_back
mix_write:
        push    dx
        call    mix_at
        call    chip_out
mix_back:
        dec     dx
        push    ax
        mov     al,[V(index)]
        call    chip_out
        pop     ax
        pop     dx
        ret

; DX = the mixer's data port, its index at AH
mix_at:
        push    ax
        mov     dx,[V(base)]
        add     dx,4
        mov     al,ah
        call    chip_out
        inc     dx
        pop     ax
        ret

; mixer register AH with AL's bits set (mix_or) or kept (mix_and)
mix_or:
        push    cx
        mov     cl,al
        call    mix_read
        or      al,cl
        jmp     short mix_change
mix_and:
        push    cx
        mov     cl,al
        call    mix_read
        and     al,cl
mix_change:
        pop     cx
        jmp     mix_write

; DX = Audio_Base + AL
port_of:
        movzx   dx,al
        add     dx,[V(base)]
        ret

; AL to the DSP's command port once it takes a byte, or after DSP_POLLS
; polls anyway
dsp_put:
        push    cx
        push    dx
        push    ax
        mov     al,0x0C
        call    port_of
        mov     cx,DSP_POLLS
.wait:  call    chip_in
        test    al,0x80
        loopnz  .wait
        pop     ax
        call    chip_out
        pop     dx
        pop     cx
        ret

; the chip's register AH = AL (Extended mode)
dsp_reg:
        push    ax
        mov     al,ah
        call    dsp_put
        pop     ax
        jmp     dsp_put

; AL = the chip's next answer, polled at +Ch bit 6 so that no interrupt is
; acknowledged (a read of +Eh would); CF set and AL = 0 if none comes
chip_answer:
        push    cx
        push    dx
        mov     al,0x0C
        call    port_of
        mov     cx,DSP_POLLS
.wait:  call    chip_in
        test    al,0x40
        loopz   .wait
        stc
        mov     al,0
        jz      .out
        sub     dx,2
        call    chip_in
        clc
.out:   pop     dx
        pop     cx
        ret

; AL = the chip's register AH (command C0h), after any byte nobody read
; (MIDI input's); CF set if none comes. The front runs with interrupts
; off, so nothing reads the answer first
dsp_read:
        push    cx
        push    dx
        mov     al,0x0C
        call    port_of
        mov     cx,8
.drain: call    chip_in
        test    al,0x40
        jz      .clean
        sub     dx,2
        call    chip_in
        add     dx,2
        loop    .drain
.clean: mov     al,ah
        mov     ah,0xC0
        call    dsp_reg
        mov     ah,al
        call    chip_answer
        pop     dx
        pop     cx
        ret

; The front keeps the chip's registers A1h-B9h that it wrote or read since
; the chip's last reset (ctrl, known), so that a transfer writes only what
; changes and reads each register once

; the chip's register AH = AL, unless the chip has it already; reg_force
; writes it in any case
reg_set:
        push    ebx
        movzx   ebx,ah
        sub     bx,0xA1
        bt      [V(known)],ebx
        jnc     reg_force.keep
        cmp     [bx+V(ctrl)],al
        jne     reg_force.keep
        pop     ebx
        ret
reg_force:
        push    ebx
        movzx   ebx,ah
        sub     bx,0xA1
.keep:  mov     [bx+V(ctrl)],al
        bts     [V(known)],ebx
        pop     ebx
        jmp     dsp_reg

; AL = the chip's register AH: as the front knows it, or read and then
; known; 0 if the chip doesn't answer
reg_get:
        push    ebx
        movzx   ebx,ah
        sub     bx,0xA1
        bt      [V(known)],ebx
        jnc     .read
        mov     al,[bx+V(ctrl)]
        pop     ebx
        ret
.read:  call    dsp_read
        jc      .none
        mov     [bx+V(ctrl)],al
        bts     [V(known)],ebx
.none:  pop     ebx
        ret

; the chip's register AH with AL's bits set (reg_or) or kept (reg_and)
reg_or:
        push    cx
        mov     cl,al
        call    reg_get
        or      al,cl
        jmp     short reg_change
reg_and:
        push    cx
        mov     cl,al
        call    reg_get
        and     al,cl
reg_change:
        pop     cx
        jmp     reg_set

; the chip's own reset (DS p.43): +6 = 3, a few microseconds, 0, then AAh;
; it is in Compatibility mode, its registers unknown. The reset clears
; Audio 2's format and interrupt (7Ah) and its volume (7Ch, DS p.51), which
; go back while Audio 2 plays
chip_reset:
        pusha
        test    byte [V(flags2)],F_RUN
        jz      .reset
        mov     ah,0x7C
        call    mix_read
        push    ax
        mov     ah,0x7A
        call    mix_read
        push    ax
.reset: mov     al,6
        call    port_of
        mov     al,3
        call    chip_out
        call    chip_in
        call    chip_in
        call    chip_in
        xor     al,al
        call    chip_out
        call    chip_answer             ; the AAh
        and     byte [V(flags)],~(F_EXT | F_RUN) & 0xFF
        and     dword [V(known)],0
        test    byte [V(flags2)],F_RUN
        jz      .out
        pop     ax                      ; 7Ah, then 7Ch
        call    mix_write
        pop     ax
        call    mix_write
.out:   popa
        ret

; the chip in Extended mode for a transfer, its DAC into the mixer; after
; Compatibility mode, a reset of its own first
ensure_ext:
        btr     word [V(state)],S_COMP_BIT
        jnc     .ext
        call    chip_reset
.ext:   bts     word [V(flags)],F_EXT_BIT
        jc      .ok
        mov     al,0xC6
        call    dsp_put
        mov     al,0xD1
        jmp     dsp_put
.ok:    ret

; the chip in Compatibility mode for a command of the firmware's: out of
; Extended mode by a reset of its own, its DAC into the mixer; compat_tc
; gives it the rate too, as the time constant 256 - 1 MHz / rate
compat_tc:
        call    compat
        xor     dx,dx
        mov     ax,5624                 ; 1 MHz * 248 / 44100
        movzx   cx,byte [V(rate_v)]
        div     cx
        cmp     ax,0xFF
        jbe     .tc
        mov     al,0xFF
.tc:    neg     al
        mov     ah,0x40
        jmp     dsp_reg
compat:
        bts     word [V(state)],S_COMP_BIT
        jc      .ok
        test    byte [V(flags)],F_EXT
        jz      .dac
        call    chip_reset
.dac:   mov     al,0xD1
        jmp     dsp_put
.ok:    ret

; --- the front ---------------------------------------------------------------

; a trapped access: DX = port, CL = the direction (0 in, else out), AL =
; the byte written; AL = the byte read. A port of the front's traps that
; it doesn't answer goes to the chip as it is; CF set for another
; program's (IO_CHAIN). DS = this segment; the direction flag is the
; game's. Every register but AL comes back
front:
        cld
        pushad
        mov     di,dbase
        mov     bx,dx
        sub     bx,[V(base)]
        test    cl,cl
        jnz     .out
        call    port_in
        mov     [V(result)],al
        popad
        mov     al,[result]
        ret
.out:   call    port_out
        popad
        ret

; AL = the game's read of port DX, BX = DX - Audio_Base
port_in:
        cmp     bx,0x0F
        jbe     s16_in
        call    dma_port
        jnc     dma_in
        cmp     byte [V(how_io)],IO_CHAIN
        cmc
        jnc     chip_in
        ret

; AL written to port DX, BX = DX - Audio_Base
port_out:
        cmp     bx,0x0F
        jbe     s16_out
        call    dma_port
        jnc     dma_out
        cmp     byte [V(how_io)],IO_CHAIN
        cmc
        jnc     chip_out
        ret

; AL = the game's read of Audio_Base+BX, DX = the port: the read latch and
; the status bits of the front's DSP; the chip's in MIDI modes and for a
; program that knows it
s16_in:
        cmp     bl,0x05
        je      mix_in
        mov     al,[V(index)]
        cmp     bl,0x04
        je      .ok
        cmp     bl,0x0A
        jb      .chip                   ; the reset port: as it is
        test    byte [V(flags)],F_ESS
        jnz     .chip
        test    byte [V(state)],S_UART | S_MIDI
        jnz     .chip
        cmp     bl,0x0C
        je      .busy
        ja      .acks
        mov     al,[V(latch)]           ; +Ah: the latch, and the answer's
        btr     word [V(state)],S_FULL_BIT      ; next byte into it
        jnc     .ok
        push    ax
        call    next_byte
        pop     ax
.ok:    clc
        ret
.chip:  call    chip_in
        jmp     short .ok
.busy:  mov     al,[V(state)]           ; +Ch: busy (bit 7) while a byte
        ror     al,2                    ; waits (S_HELD)
        or      al,0x7F
        jmp     short .ok
.acks:  lea     cx,[bx-0x0D]            ; +Eh 8-bit, +Fh 16-bit: CL = 1, 2
        call    ack
        mov     al,0xFF                 ; +Fh: FFh
        cmp     bl,0x0F
        je      .ok
        mov     al,[V(state)]           ; +Eh: bit 7 for a byte in the
        ror     al,1                    ; latch (S_FULL)
        or      al,0x7F
        jmp     short .ok

; the latch takes the answer's next byte; once the answer is out, a byte
; written meanwhile is the DSP's
next_byte:
        cmp     byte [V(alen)],0
        je      .end
        mov     si,[V(aptr)]
        lodsb
        mov     [V(aptr)],si
        mov     [V(latch)],al
        or      byte [V(state)],S_FULL
        dec     byte [V(alen)]
        jnz     .ret
.end:   btr     word [V(state)],S_HELD_BIT
        jnc     .ret
        mov     al,[V(held)]
        jmp     dsp_take
.ret:   ret

; the answer of CL bytes at SI: the first into the latch if it is free,
; the others after it. The DSP takes no byte until they are out
answer:
        bts     word [V(state)],S_FULL_BIT
        jc      .rest
        lodsb
        mov     [V(latch)],al
        dec     cl
.rest:  mov     [V(aptr)],si
        mov     [V(alen)],cl
        ret
put1:   mov     [V(abuf)],al            ; the answer AL
        mov     si,abuf
        mov     cl,1
        jmp     answer

; AL written to Audio_Base+BX, DX = the port; a write to the front's other
; ports (+Ah, +Eh, +Fh) does nothing, as on a Sound Blaster 16, but for a
; program that knows the chip (+Fh is the chip's FIFO)
s16_out:
        cmp     bl,0x0C
        je      s16_write
        cmp     bl,0x05
        je      mix_out
        cmp     bl,0x06
        je      reset_port
        cmp     bl,0x04
        je      .index
        test    byte [V(flags)],F_ESS
        jnz     chip_through
        ret
.index: mov     [V(index)],al           ; the chip gets the index too
chip_through:
        call    chip_out
        clc
        ret

; AL written to the reset port: the front's DSP halts at 1 and starts again
; at 0 with AAh, and the chip stays in Extended mode; the MIDI UART mode
; and Compatibility mode take a reset of the chip's own. A program that
; knows the chip resets it itself
reset_port:
        test    byte [V(flags)],F_ESS
        jz      .front
        call    chip_out
        test    al,1
        jnz     .done
        call    chip_answer             ; the chip's AAh: the front answers
        and     byte [V(flags)],~(F_ESS | F_EXT | F_RUN) & 0xFF
        and     dword [V(known)],0
        jmp     short boot
.front: test    al,1
        jz      .low
        bts     word [V(flags)],F_RESET_BIT
        jnc     halt
.done:  clc
        ret
.low:   btr     word [V(flags)],F_RESET_BIT
        jnc     .done
        test    byte [V(state)],S_UART | S_COMP
        jz      boot
        call    chip_reset

; the DSP's start after a reset: cold, or warm after the high-speed and
; MIDI UART commands, which keep the rate, the block and the speaker; AAh
; in the latch
boot:
        test    byte [V(state)],S_WARM
        jnz     .warm
        mov     byte [V(rate_v)],0x38
        mov     word [V(block8)],0x07FF
        and     byte [V(state)],~S_SPKR & 0xFF
.warm:  and     byte [V(state)],S_SPKR
        mov     byte [V(latch)],0xAA
        or      byte [V(state)],S_FULL
.clear: mov     word [V(need)],0     ; and alen
        clc
        ret

; the DSP held in reset: the front's transfers stop, the chip's interrupts
; are acknowledged (Audio 2's off until its next transfer), and nothing
; waits in the latches
halt:
        call    a1_stop
        call    a2_stop
        cmp     byte [V(dma2)],4
        jae     .idle
        mov     ax,0x7A00
        call    mix_write
.idle:  mov     al,0x0E
        call    port_of
        call    chip_in
        and     byte [V(state)],S_WARM | S_SPKR | S_UART | S_COMP
        jmp     short boot.clear

; Audio 1's transfer of the front's stops (B8h bit 0), at once, as a Sound
; Blaster 16's: its FIFO empties (a1_flush), so that nothing of it plays
; on while Audio 2 plays the next
a1_stop:
        test    byte [V(flags)],F_RUN
        jz      a2_stop.ret
        and     byte [V(flags)],~(F_RUN | F_AUTO | F_LAST | F_PAUSE) & 0xFF
        mov     ax,0xB8FE
        call    reg_and

; Audio 1's FIFO empties (+6 bit 1, DS p.48)
a1_flush:
        mov     al,6
        call    port_of
        mov     al,2
        call    chip_out
        mov     al,0
        jmp     chip_out

; Audio 2's transfer of the front's stops (78h bits 4 and 1), and what its
; FIFO holds plays out
a2_stop:
        test    byte [V(flags2)],F_RUN
        jz      .ret
        mov     byte [V(flags2)],0
        mov     ax,0x78ED
        jmp     mix_and
.ret:   ret

; AL written to the command port, DX = the port: the front's DSP takes
; it, or waits for its answer to be read; the chip takes it in MIDI modes
; and for a program that knows it
s16_write:
        test    byte [V(flags)],F_ESS
        jnz     chip_through
        test    byte [V(state)],S_UART | S_MIDI
        jz      .dsp
        and     byte [V(state)],~S_MIDI & 0xFF  ; a byte ends MIDI input
        jmp     chip_through
.dsp:   cmp     byte [V(alen)],0
        je      dsp_take
        mov     [V(held)],al            ; the answer blocks: the byte waits,
        or      byte [V(state)],S_HELD  ; over any before it
        clc
        ret

; AL, a byte the DSP takes: a command, or an operand of the last. C6h is
; also the chip's Extended mode: a first operand that is no mode (bits 4
; and 5 only) is the chip's register command, from a program that knows
; the chip
dsp_take:
        cmp     byte [V(need)],0
        jne     .operand
        mov     ah,al
        xchg    ah,[V(cmd)]
        mov     [V(prev)],ah
        mov     byte [V(got)],0
        movzx   bx,al                   ; its operands, as DSP 4.05's
        shr     bx,2                    ; firmware takes them: two bits a
        mov     cl,al                   ; command in optable
        and     cl,3
        shl     cl,1
        mov     al,[optable+bx]
        shr     al,cl
        and     al,3
        mov     [V(need)],al
        jz      command
        clc
        ret
.operand:
        movzx   bx,byte [V(got)]
        or      bx,bx
        jnz     .keep
        cmp     byte [V(cmd)],0xC6
        jne     .keep
        test    al,0xCF
        jz      .keep
        mov     ah,0xC6
        call    dsp_reg
        or      byte [V(flags)],F_ESS | F_EXT
        and     dword [V(known)],0
        mov     byte [V(need)],0
        clc
        ret
.keep:  mov     [ops+bx],al
        inc     byte [V(got)]
        dec     byte [V(need)]
        jz      command
        clc
        ret

optable:        db 0x10, 0x09, 0x40, 0x61       ; 0xh
                db 0x55, 0xAA, 0x00, 0x00       ; 1xh
                db 0x00, 0xAA, 0x00, 0x00       ; 2xh
                db 0x00, 0x00, 0x55, 0x55       ; 3xh
                db 0x29, 0x00, 0x02, 0xA6       ; 4xh
                db 0x00, 0x00, 0x00, 0x00       ; 5xh
                db 0x00, 0x00, 0x00, 0x00       ; 6xh
                db 0x00, 0xAA, 0x00, 0x00       ; 7xh
                db 0xAA, 0xAA, 0xAA, 0xAA       ; 8xh
                db 0x00, 0x00, 0x00, 0x00       ; 9xh
                db 0x00, 0x00, 0x00, 0x00       ; Axh
                db 0xFF, 0xFF, 0xFF, 0xFF       ; Bxh
                db 0xFF, 0xFF, 0xFF, 0xFF       ; Cxh
                db 0x00, 0x00, 0x00, 0x00       ; Dxh
                db 0x11, 0x01, 0x00, 0x00       ; Exh
                db 0x00, 0x00, 0x24, 0x00       ; Fxh

; the command in cmd with its operands: a constant answer from answers,
; else its group's handler, by the high nibble, with AL = the low one
command:
        mov     si,answers
.find:  lodsw                           ; AL = a command, AH = its answer's
        or      ah,ah                   ; length (0: the end), then where it
        jz      .group                  ; is from astrings
        cmp     al,[V(cmd)]
        lodsb
        jne     .find
        mov     cl,ah
        cbw
        add     ax,astrings
        xchg    ax,si
        call    answer
        clc
        ret
.group: movzx   bx,byte [V(cmd)]
        mov     al,bl
        and     al,0x0F
        shr     bx,3
        and     bl,0x1E
        call    [groups+bx]
        clc
        ret
groups:         dw g0, g1, g2, g3, g4, none, none, g7
                dw g8, g9, none, gx, gx, gd, ge, gf

; the commands that answer bytes that never change: the CSP's that this
; card doesn't have (03h 08h FFh, 09h its ID, 0Ah the lock and FFh), 4Dh
; and F9h, FBh, FCh the DSP's RAM (00h here), E1h the version, E3h the
; copyright, F4h the ROM's ID
answers:        db 0x03, 1, ff_byte - astrings
                db 0x08, 1, ff_byte - astrings
                db 0x09, 2, zeros - astrings
                db 0x0A, 2, zeros + 1 - astrings
                db 0x4D, 1, zeros - astrings
                db 0xF9, 1, zeros - astrings
                db 0xFB, 1, zeros - astrings
                db 0xFC, 1, zeros - astrings
                db 0xE1, 2, version - astrings
                db 0xE3, COPYRIGHT_LEN, copyright - astrings
                db 0xF4, 2, rom_id - astrings
                db 0, 0
astrings:
zeros:          db 0, 0
ff_byte:        db 0xFF
version:        db VERSION >> 8, VERSION & 0xFF
rom_id:         db 0x7D, 0x1B
copyright:      db "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.", 0

; 0Fh: an X-bus register of the CSP: FFh for 80h-83h, else 00h
g0:     cmp     al,0x0F
        jne     none
        mov     al,[V(ops)]
        and     al,0xFC
        cmp     al,0x80
        mov     al,0
        jne     put1
        dec     al
        jmp     put1
none:   ret

; 1xh: 10h-13h the direct DAC; 14h 15h 8-bit single-cycle DMA of the
; operands' length, 18h 19h 1Ch 1Dh auto-initialized of the 8-bit block's;
; with bit 1, 2-bit ADPCM (the chip's 7Ah and 7Bh)
g1:     cmp     al,4
        jb      direct_dac
        mov     bl,0
        test    al,0x02
        jz      old8
        mov     ah,al
        and     ah,1
        or      ah,0x7A
        cmp     al,8
        cmc                             ; CF: auto-initialized (lo >= 8)
        jmp     fw_xfer

; 2xh: 20h-23h the direct ADC; 24h-27h single-cycle 8-bit recording,
; 28h-2Fh auto-initialized
g2:     cmp     al,4
        jb      direct_adc
        mov     bl,0x08
old8:   cmp     al,8                    ; BL = 08h recording, else 00h
        mov     ax,[V(ops)]
        jb      pcm8
        or      bl,0x04
block:  mov     ax,[V(block8)]
pcm8:   xor     cl,cl                   ; unsigned mono
        jmp     xfer

; 9xh: the high-speed 8-bit commands, of the 8-bit block: auto-initialized
; unless bit 0, recording with bit 3, never queued; the next reset is warm
g9:     or      byte [V(state)],S_WARM
        mov     bl,al
        and     bl,0x08
        or      bl,0x05
        test    al,1
        jz      block
        and     bl,~0x04 & 0xFF
        jmp     short block

; 3xh: MIDI through the chip in Compatibility mode: 30h-33h input (30h, 31h
; with interrupts) until the next write, 34h-37h the UART mode until a
; reset, 38h-3Fh a byte out
g3:     push    ax
        call    compat
        pop     ax
        cmp     al,8
        jae     .out
        mov     ah,S_MIDI
        cmp     al,4
        jb      .mode
        mov     ah,S_UART | S_WARM
.mode:  or      [V(state)],ah
        and     al,0x05
        or      al,0x30
        jmp     dsp_put
.out:   mov     ah,0x38
        mov     al,[V(ops)]
        jmp     dsp_reg

; 4xh: 40h the time constant, 41h and 42h the rate (high byte first), 48h
; the 8-bit block; the others take their operands only
g4:     mov     cx,[V(ops)]
        cmp     al,8
        je      .block
        cmp     al,2
        ja      none
        xchg    ax,cx                   ; AX = the operands, CL = 0 for 40h
        test    cl,cl
        jz      .tc
        xchg    al,ah
        call    rate_to_v
        jmp     short .v
.tc:    call    tc_to_v
.v:     mov     [V(rate_v)],al
        ret
.block: mov     [V(block8)],cx
        ret

; 7xh: ADPCM through the chip: 74h-77h single-cycle (4-bit, then 2.6-bit,
; each with the reference byte at bit 0); the others auto-initialized on a
; Sound Blaster 16, which the chip doesn't have
g7:     mov     ah,al
        and     ah,3
        or      ah,0x74
        sub     al,4
        cmp     al,4                    ; CF clear for 74h-77h
        cmc
        jmp     short fw_xfer

; 8xh: silence, of the operands' length
g8:     mov     ah,0x80
        clc

; the firmware's command AH (ADPCM, silence) for the chip in Compatibility
; mode, at the rate's time constant, with the operands' length; CF set:
; auto-initialized, which the chip doesn't have: the 8-bit block, once.
; Its interrupt is the 8-bit one
fw_xfer:
        jnc     .ops
        mov     cx,[V(block8)]
        mov     [V(ops)],cx
.ops:   push    ax
        call    compat_tc
        pop     ax
        mov     al,ah
        call    dsp_put
        mov     ax,[V(ops)]
        call    dsp_put
        mov     al,ah
        call    dsp_put
        and     byte [V(state)],~S_T16 & 0xFF
        ret

; the direct DAC and ADC, through the chip in Compatibility mode
direct_dac:
        call    compat
        mov     ah,0x10
        mov     al,[V(ops)]
        jmp     dsp_reg
direct_adc:
        call    compat
        mov     al,0x20
        call    dsp_put
        call    chip_answer
        jmp     put1

; Bxh 16-bit and Cxh 8-bit: bit 3 records, bit 2 auto-initializes (bits 1
; and 0 don't count); the mode's bit 4 signed, bit 5 stereo; the length of
; a Cxh is the 8-bit block's too
gx:     mov     bl,[V(cmd)]
        and     bl,0x1C
        mov     cl,[V(ops)]
        mov     ax,[V(ops+1)]
        test    bl,0x10
        jnz     xfer
        mov     [V(block8)],ax
        jmp     xfer

; Dxh: D1h D3h the speaker flag, which D8h answers (D1h and D3h change no
; sound on DSP 4.xx); D0h D4h DAh pause, continue and end the 8-bit
; transfer's auto-initialize, D5h D6h D9h the 16-bit one's, when that kind
; runs, on Audio 1 or Audio 2 (dx_ops: the kind and the action of each)
gd:     cmp     al,8
        je      .status
        movzx   bx,al
        mov     ah,[dx_ops+bx]
        test    ah,ah
        jns     .speaker                ; bit 7 clear: the speaker, or nothing
        test    ah,F_16
        jz      .a1
        test    byte [V(flags2)],F_RUN
        jnz     .a2
.a1:    mov     dl,[V(flags)]
        xor     dl,ah
        test    dl,F_RUN | F_16         ; not the kind that runs: nothing
        jnz     none
        shl     ah,2                    ; CF: pause, SF: continue
        jc      .pause
        js      .cont
        btr     word [V(flags)],F_AUTO_BIT      ; the block that plays is
        jnc     none                            ; the last
        and     byte [V(flags)],~F_LAST & 0xFF
        mov     ax,0xB8FB
        jmp     reg_and
.pause: or      byte [V(flags)],F_PAUSE ; Audio 1's DMA requests off (B2h
        mov     ax,0xB2BF               ; bit 6)
        jmp     reg_and
.cont:  btr     word [V(flags)],F_PAUSE_BIT
        jnc     none
        call    a1_clocks               ; the rate as it is now
        mov     ax,0xB240
        jmp     reg_or
.a2:    shl     ah,2                    ; Audio 2: the same with 78h
        jc      .pause2
        js      .cont2
        btr     word [V(flags2)],F_AUTO_BIT
        jnc     none
        and     byte [V(flags2)],~F_LAST & 0xFF
        mov     ax,0x78EF               ; bit 4: auto-initialize
        jmp     mix_and
.pause2:
        or      byte [V(flags2)],F_PAUSE ; the FIFO off the DAC (bit 0): the
        mov     ax,0x78FE               ; DMA fills it and waits
        jmp     mix_and
.cont2: btr     word [V(flags2)],F_PAUSE_BIT
        jnc     none
        call    a2_rate
        mov     ax,0x7801
        jmp     mix_or
.speaker:
        dec     ah                      ; 1 on, 2 off, 0 nothing
        js      none
        jnz     .off
        or      byte [V(state)],S_SPKR
        ret
.off:   and     byte [V(state)],~S_SPKR & 0xFF
        ret
.status:
        mov     al,[V(state)]
        and     al,S_SPKR
        jz      put1
        mov     al,0xFF
        jmp     put1
; Dxh: 1 the speaker on, 2 off, 0 nothing; for the engines 80h, F_RUN and
; F_16 for the 16-bit one, and 40h pause, 20h continue, else the
; auto-initialize's end
dx_ops:         db 0xC0 | F_RUN, 1, 0, 2
                db 0xA0 | F_RUN, 0xC0 | F_RUN | F_16, 0xA0 | F_RUN | F_16, 0
                db 0, 0x80 | F_RUN | F_16, 0x80 | F_RUN, 0, 0, 0, 0, 0

; Exh: E0h the operand inverted, E4h and E8h the test register; E2h's byte
; would go to the 8-bit channel, which the front has no way to write
ge:     mov     ah,[V(ops)]
        cmp     al,0
        je      .inv
        cmp     al,4
        je      .test
        cmp     al,8
        jne     none
        mov     al,[V(test_reg)]
        jmp     put1
.inv:   mov     al,ah
        not     al
        jmp     put1
.test:  mov     [V(test_reg)],ah
        ret

; Fxh: F2h the 8-bit interrupt, F3h the 16-bit one (the chip's processor
; interrupt), F8h a 0 into the latch at once, FDh the command before
gf:     cmp     al,3
        ja      .others
        jb      .irq8
        or      byte [V(state)],S_T16
        jmp     short .f2
.irq8:  cmp     al,2
        jne     none
        and     byte [V(state)],~S_T16 & 0xFF
.f2:    mov     al,0xF2
        jmp     dsp_put
.others:
        cmp     al,0x0D
        jne     .f8
        mov     al,[V(prev)]
        jmp     put1
.f8:    cmp     al,8
        jne     none
        mov     byte [V(latch)],0
        or      byte [V(state)],S_FULL
        ret

; --- the rate ----------------------------------------------------------------

; AL = the rate generator's value for the time constant AL (40h): 248 MHz
; / 44.1 kHz over 256 - TC, with the ROM table's exceptions
tc_to_v:
        cmp     al,0xEA                 ; the ROM's limit, EBh, is FFh too
        jbe     .tc
        mov     al,0xEA
.tc:    cmp     al,0x68
        mov     ah,0x25
        je      .ah
        cmp     al,0x6C
        mov     ah,0x26
        je      .ah
        cmp     al,0xC8                 ; the table's own: 35.6 kHz
        je      .ret
        mov     ah,0
        mov     cx,256
        sub     cx,ax
        mov     ax,5623                 ; the same as 248 MHz over 44.1 kHz
        cwd                             ; for every divisor, at most FFh
        div     cx
.ret:   ret
.ah:    mov     al,ah
        ret

; AL = the value for the rate AH:AL in Hz (41h, 42h), as DSPs before 4.13
; compute it, losing a carry (23000 Hz plays at 20094 Hz)
rate_to_v:
        cmp     ah,0xB1
        jae     .max
        cmp     ah,0x13
        jb      .min
        mov     bl,al
        mov     al,23
        mul     ah                      ; AX = 23 times the high byte
        mov     cx,ax
        mov     al,23
        mul     bl                      ; AH = 23 times the low one, high byte
        add     cl,ah                   ; the carry lost
        mov     ax,cx
        shr     ax,3
        inc     ax
        shr     ax,1                    ; (x >> 4) + ((x >> 3) & 1)
        ret
.max:   mov     al,0xFF
        ret
.min:   mov     al,0x1C
        ret

; A1h and A2h for the rate v * 44100 / 248 Hz, as ES1869.DRV picks them
; for a rate (6:1FF4): the 795.5 kHz clock above 22050 Hz, the 397.7 kHz
; one up to it, and the filter clock from its tables between 22 and 44
; kHz, else 7.16 MHz over 35.9 times the rate; stereo as A8h has it.
; Clobbers AX, DX, SI
a1_clocks:
        push    bx
        push    cx
        movzx   bx,byte [V(rate_v)]
        mov     ax,4472                 ; 397.7 kHz: round(2236 / v)
        mov     cx,128
        cmp     bl,124
        jbe     .clock
        mov     ax,8947                 ; 795.5 kHz: round(4473.5 / v)
        mov     cx,256                  ; (the constants round as the
.clock: call    .div                    ; clocks do for every v)
        sub     cx,ax
        xchg    ax,cx                   ; CX = the divisor
        mov     ah,0xA1
        call    reg_set
        cmp     bl,124
        jbe     .general
        cmp     cx,18
        jb      .general
        cmp     cx,35
        ja      .general
        mov     si,filter_stereo - 18
        test    byte [V(ctrl)+0x07],0x01
        jnz     .table
        mov     si,filter_mono - 18
.table: add     si,cx
        mov     al,[si]
        jmp     short .a2
.general:
        mov     ax,2245                 ; 7.16 MHz / 35.9: round(1122.5 / v)
        call    .div
.a2:    neg     al
        mov     ah,0xA2
        call    reg_set
        pop     cx
        pop     bx
        ret
.div:   xor     dx,dx                   ; AX = round(AX / 2 / v)
        div     bx
        inc     ax
        shr     ax,1
        ret

; ES1869.DRV's filter dividers for the 795.5 kHz clock's divisors 18 to 35
filter_stereo:  db 3, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9
filter_mono:    db 3, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10

; --- the game's transfers: Audio 1 in Extended mode, and Audio 2 ----------

; a transfer: BL = bit 4 16-bit, bit 3 recording, bit 2 auto-initialize,
; bit 0 never queued; CL = the mode (bit 4 signed, bit 5 stereo); AX = the
; length less one, in samples. A single-cycle one while this kind
; auto-initializes is queued, as on the Sound Blaster 16: the counter
; gets its length for the next block, the last (its mode doesn't count).
; A 16-bit playback goes to Audio 2 when it has a channel and the game a
; 16-bit one (a2_xfer). Else the sequence of the data sheet (p.48) on Audio
; 1: B8h stopped with the direction and auto-initialize, B9h for demand
; transfers of 2 bytes, A8h for mono or stereo, A1h and A2h for the rate,
; A4h and A5h for minus the bytes, B6h and B7h for the format, B1h and B2h
; for the interrupt and the DMA request, mixer 71h, the DMA, then B8h bit
; 0. Only the registers that change are written. There is one transfer of
; each kind, so a 16-bit one ends the other engine's
xfer:
        mov     si,ax
        inc     si                      ; SI = the samples (0: 65536)
        test    bl,0x05
        jnz     .start
        test    bl,F_16
        jz      .q1
        mov     al,[V(flags2)]
        and     al,F_RUN | F_AUTO
        cmp     al,F_RUN | F_AUTO
        jne     .q1
        or      byte [V(flags2)],F_LAST
        jmp     a2_count
.q1:    mov     dl,bl
        and     dl,F_16
        or      dl,F_RUN | F_AUTO
        mov     al,[V(flags)]
        and     al,F_16 | F_RUN | F_AUTO
        cmp     al,dl
        jne     .start
        or      byte [V(flags)],F_LAST
        jmp     a1_count
.start: mov     [V(xbits)],bl
        test    bl,F_16
        jz      .a1
        test    bl,0x08                 ; 16-bit playback: Audio 2's, with
        jnz     .not2                   ; its channel and the game's 16-bit
        cmp     byte [V(dma16)],0       ; one
        je      .not2
        cmp     byte [V(dma2)],4
        jb      a2_xfer
.not2:  call    a2_stop
.a1:    test    byte [V(flags)],F_RUN   ; the first after a reset: the FIFO
        jnz     .ext                    ; empties, so that nothing of
        call    a1_flush                ; another stream plays in this
.ext:   call    ensure_ext              ; one's format
        mov     al,bl                   ; F_16 and F_AUTO from BL
        or      al,F_RUN
        and     al,F_16 | F_RUN | F_AUTO
        mov     ah,[V(flags)]
        and     ah,F_ESS | F_EXT | F_RESET
        or      al,ah
        mov     [V(flags)],al
        call    a1_count
        mov     al,bl                   ; B8h: stopped, auto-initialize (bit
        and     al,0x0C                 ; 2), the ADC (bit 3) and the DMA's
        test    al,0x08                 ; direction (bit 1); bits 7:4 0
        jz      .b8
        or      al,0x02
.b8:    mov     ah,0xB8
        call    reg_set
        mov     ah,0xB9                 ; demand transfers of 2 bytes, as
        mov     al,[V(b9)]              ; ESS's driver writes them, or
        call    reg_set                 ; single ones with /S
        mov     ah,0xA8                 ; mono or stereo: A8h bits 1:0
        call    reg_get
        and     al,0xFC
        or      al,0x02
        test    cl,0x20
        jz      .mono
        xor     al,0x03
.mono:  mov     ah,0xA8
        call    reg_set
        call    a1_clocks
        mov     dx,0xD080               ; the format: B7h in DH, B6h in DL;
        test    cl,0x10                 ; 8-bit mono unsigned D0h and 80h,
        jz      .size                   ; signed F0h and 00h
        xor     dx,0x2080
.size:  test    bl,0x10
        jz      .chans
        xor     dh,0x04                 ; 16-bit: D4h, F4h
.chans: test    cl,0x20
        jz      .fmt
        xor     dh,0x48                 ; stereo: 98h, B8h, 9Ch, BCh
.fmt:   call    a1_format
        mov     ax,0xB150               ; the interrupt and the DMA request
        call    reg_or                  ; on: bits 6 and 4, the others
        mov     ax,0xB250               ; kept, as ES1869.DRV keeps them
        call    reg_or
        call    dac_options
        mov     al,[V(dma8)]
        call    xfer_dma
        mov     ax,0xB801               ; go
        jmp     reg_or

; a 16-bit playback on Audio 2: SI = the samples, CL = the mode. As
; ES1869.DRV starts Audio 2 (6:2C1A, docs/DRIVER_CONFIG.md): stopped (78h),
; for a start anew, the rate and filter clock (70h, 72h), minus the bytes
; (74h, 76h), the volume of the view's voice (7Ch, which a reset of the
; chip clears), the format and the interrupt (7Ah), the DACs' mode (71h),
; the game's 16-bit channel carried over to Audio 2's, then 78h:
; auto-initialize as the command says, demand transfers of 4 bytes, or
; single ones with /S. Audio 1's 16-bit transfer, if one runs, ends first
a2_xfer:
        mov     al,[V(flags)]
        and     al,F_RUN | F_16
        cmp     al,F_RUN | F_16
        jne     .go
        call    a1_stop
.go:    mov     ax,0x7800
        call    mix_write
        mov     al,bl
        and     al,F_AUTO
        or      al,F_RUN
        mov     [V(flags2)],al
        call    a2_count
        call    a2_rate
        push    bx
        mov     al,5                    ; the voice, on Audio 2 (pairs)
        call    pair_out
        pop     bx
        mov     al,0x41                 ; 7Ah: the interrupt on, 16-bit,
        test    cl,0x10                 ; signed (bit 2) and stereo (bit 1)
        jz      .unsigned               ; as the mode says
        or      al,0x04
.unsigned:
        test    cl,0x20
        jz      .mono
        or      al,0x02
.mono:  mov     ah,0x7A
        call    mix_write
        call    dac_options
        mov     al,[V(dma2)]
        call    xfer_dma
        mov     al,0x03                 ; the DMA, and the FIFO to the DAC
        test    bl,F_AUTO
        jz      .single
        or      al,0x10
.single:
        cmp     byte [V(b9)],0
        je      .go78
        or      al,0x80
.go78:  mov     ah,0x78
        jmp     mix_write

; 74h and 76h, Audio 2's counter reload: minus the bytes of SI 16-bit
; samples
a2_count:
        shl     si,1
        neg     si
        mov     ax,si
        mov     ah,0x74
        call    mix_write
        mov     ax,si
        mov     al,ah
        mov     ah,0x76
        jmp     mix_write

; 70h and 72h for the rate v * 44100 / 248 Hz, as ES1869.DRV picks them
; (6:2436): the divisor of the 793.8 kHz clock, round(4464 / v), or of the
; 768 kHz one, round(4319 / v) with bit 7, whichever comes nearer, 128 at
; most (6 kHz); the filter clock FDh from 44.1 kHz on, else 7.16 MHz over
; 35.9 times the rate, as for A2h
a2_rate:
        pusha
        movzx   bx,byte [V(rate_v)]
        mov     ax,4464                 ; 793.8 kHz
        call    .div
        mov     cx,ax
        mov     bp,dx
        mov     ax,4319                 ; 768 kHz
        call    .div
        mov     ah,0x80
        cmp     dx,bp
        jb      .clock
        mov     ax,cx
.clock: cmp     al,128
        jbe     .d
        mov     al,128
.d:     neg     al                      ; 128 - the divisor, with the clock's
        and     al,0x7F                 ; bit
        or      al,ah
        mov     ah,0x70
        call    mix_write
        mov     al,0xFD
        cmp     bl,0xF8
        jae     .filter
        mov     ax,2245
        call    a1_clocks.div
        neg     al
.filter:
        mov     ah,0x72
        call    mix_write
        popa
        ret
; AX = round(AX / v), DX = how far that times v is from AX; BX = v
.div:   push    ax
        xor     dx,dx
        div     bx
        shl     dx,1
        cmp     dx,bx
        jb      .down
        inc     ax
.down:  pop     dx
        push    ax
        imul    ax,bx
        sub     dx,ax
        jns     .pos
        neg     dx
.pos:   pop     ax
        ret

; A4h and A5h, the counter's reload: minus the bytes of SI samples
a1_count:
        test    byte [V(flags)],F_16
        jz      .bytes
        shl     si,1
.bytes: neg     si
        mov     ax,si
        mov     ah,0xA4
        call    reg_set
        mov     ax,si
        mov     al,ah
        mov     ah,0xA5
        jmp     reg_set

; the format, B6h = DL and B7h = DH: B6h, then B7h twice (DS p.49, table
; 16), first 51h or 71h, unless the chip has the format
a1_format:
        cmp     dx,[V(ctrl)+0x15]
        jne     .write
        mov     al,[V(known)+2]         ; B6h and B7h known: bits 21, 22
        not     al
        test    al,0x60
        jz      .out
.write: mov     al,dl
        mov     ah,0xB6
        call    reg_force
        mov     ax,0xB771
        or      dl,dl
        jz      .first
        mov     al,0x51
.first: call    dsp_reg
        mov     al,dh
        mov     ah,0xB7
        jmp     reg_force
.out:   ret

; mixer 71h for a transfer: bit 5 off, so that A1h means what the front
; computes; the filters and 4x oversampling as /F and /O set them, else
; as they are
dac_options:
        mov     ah,0x71
        call    mix_read
        mov     dh,al
        and     al,[V(keep71)]
        or      al,[V(set71)]
        cmp     al,dh
        je      .same
        call    mix_write
.same:  ret

; --- the game's 16-bit channel ---------------------------------------------

; the DMA of a transfer, AL = the channel that plays it: an 8-bit one, or
; a 16-bit one without a 16-bit channel, runs on Audio 1's own channel as
; the game programmed it; a 16-bit one has the game's 16-bit channel
; carried over to AL's, Audio 1's or Audio 2's. Where the front traps that
; channel, it is carried over when the game programmed it anew or it goes
; to the other channel, and AL's waits while the game's is masked
xfer_dma:
        test    bl,0x10
        jz      .own
        cmp     byte [V(dma16)],0
        je      .own
        cmp     al,[V(mchan)]
        je      .same
        mov     [V(mchan)],al
        and     byte [V(vbits)],~V_LIVE & 0xFF
.same:  call    vtrapped
        jc      mirror
        test    byte [V(vbits)],V_MASK
        jnz     ch8_mask
        mov     al,[V(vbits)]
        and     al,V_NEW | V_LIVE
        cmp     al,V_LIVE
        jne     mirror
        ret
.own:   mov     al,[V(dma8)]            ; the game programs Audio 1's channel
        cmp     al,[V(mchan)]           ; itself: what it carried is gone,
        jne     .ret                    ; but on Audio 2's
        and     byte [V(vbits)],~V_LIVE & 0xFF
.ret:   ret

; CF clear if this trap path traps the game's 16-bit channel (the one the
; TSR was installed for, which mixer 81h may have changed)
vtrapped:
        push    bx
        movzx   bx,byte [V(how_io)]
        mov     bl,[bx+V(vchan)]
        cmp     bl,[V(dma16)]
        pop     bx
        je      .ret
        stc
.ret:   ret

; the game's 16-bit channel carried over to the 8-bit one that plays it
; (mchan): the word address and the page's bits 7:1 as a byte address and
; page, the word count as a byte count, demand transfers (single with /S),
; the direction of the transfer, auto-initialize as the game's channel has
; it. Without the traps, the channel's registers are read back (right
; after the game programmed them they hold what it wrote) and
; auto-initialize is the command's. The game's flip-flop is left cleared
mirror:
        call    vtrapped
        jnc     .have
        mov     dx,0xD8                 ; read back, the flip-flop cleared
        call    chip_out                ; before and after
        mov     dl,[V(vpagep)]
        call    chip_in
        mov     [V(vpage)],al
        mov     dl,[V(vport)]
        mov     si,vaddr
        call    word_in
        mov     si,vcount
        call    word_in
        mov     dl,0xD8
        call    chip_out
        mov     al,[V(xbits)]
        and     al,0x04
        shl     al,2
        mov     [V(vmode)],al
.have:  mov     ah,[V(vmode)]
        and     ah,0x10                 ; auto-initialize
        or      ah,0x08                 ; from memory
        test    byte [V(xbits)],0x08
        jz      .play
        xor     ah,0x0C                 ; into memory: a recording
.play:  cmp     byte [V(b9)],0
        jne     .mode
        or      ah,0x40                 ; single transfers
.mode:  movzx   si,byte [V(mchan)]
        mov     al,[V(mchan)]           ; AL = the channel
        or      ah,al                   ; AH = the mode
        or      al,0x04                 ; masked while it is set
        mov     dx,0x0A
        call    chip_out
        inc     dx
        mov     al,ah
        call    chip_out
        inc     dx                      ; the flip-flop
        call    chip_out
        mov     ax,[V(vaddr)]
        mov     cl,[V(vpage)]
        and     cl,0xFE
        shl     ax,1                    ; the byte address, its bit 16 into
        adc     cl,0                    ; the page
        mov     [V(mpage)],cl
        mov     dx,si
        shl     dx,1                    ; the address port
        call    word_out
        inc     dx                      ; the count port
        mov     ax,[V(vcount)]
        shl     ax,1
        or      al,1                    ; the byte count
        call    word_out
        mov     dl,[page8+si]
        mov     al,cl
        call    chip_out
        and     byte [V(vbits)],~V_NEW & 0xFF
        or      byte [V(vbits)],V_LIVE
        call    vtrapped
        jc      ch8_unmask
        test    byte [V(vbits)],V_MASK
        jz      ch8_unmask
ch8_mask:
        mov     al,0x04
        jmp     short ch8_set
ch8_unmask:
        mov     al,0
ch8_set:
        or      al,[V(mchan)]
        push    dx
        mov     dx,0x0A
        call    chip_out
        pop     dx
        ret

; AX to a DMA controller's register DX, low byte first; and a register's
; two bytes into [SI], DX moving on to the next
word_out:
        call    chip_out
        mov     al,ah
        jmp     chip_out
word_in:
        call    chip_in
        mov     [si],al
        call    chip_in
        mov     [si+1],al
        add     dl,2
        ret

; the page registers of the first controller's channels 0 to 3
page8:          db 0x87, 0x83, 0x81, 0x82

; CF clear if port DX is one of the game's 16-bit channel: its address,
; count and page ports, and its controller's mask, mode, flip-flop and
; clear registers (D4h-DEh)
dma_port:
        or      dh,dh
        jnz     .no
        cmp     dl,[V(vpagep)]
        je      .ret                    ; (CF clear)
        mov     ah,dl
        sub     ah,[V(vport)]
        jz      .ret
        cmp     ah,2
        je      .yes
        cmp     dl,0xD4
        jb      .no
        cmp     dl,0xDE
        ja      .no
        test    dl,1
        jz      .ret
.no:    stc
.ret:   ret
.yes:   clc
        ret

; AL = the game's read of port DX of its 16-bit channel: the controller's,
; except the channel's address and count while the card's channel carries
; its programming (mchan): they follow that channel, the high byte read
; with the low one. Only while this path traps the channel (vtrapped)
dma_in:
        call    chip_in                 ; the flip-flop moves as on the machine
        call    vtrapped
        jc      .ok
        mov     bl,dl
        sub     bl,[V(vport)]
        jz      .reg                    ; BL = 0 the address, 1 the count
        cmp     bl,2
        jne     .ok
        mov     bl,1
.reg:   xor     byte [V(vbits)],V_FF
        test    byte [V(vbits)],V_LIVE
        jz      .ret
        test    byte [V(vbits)],V_FF
        jz      .high
        push    dx                      ; the card's channel: the bytes it
        mov     dx,0x0C                 ; moved, halved
        call    chip_out
        movzx   dx,byte [V(mchan)]
        shl     dx,1
        add     dl,bl
        call    chip_in
        mov     ah,al
        call    chip_in
        xchg    al,ah
        pop     dx
        or      bl,bl
        jz      .addr
        cmp     ax,0xFFFF               ; at terminal count: FFFFh
        je      .both
        shr     ax,1
        jmp     short .both
.addr:  shr     ax,1
        test    byte [V(mpage)],1       ; the page's bit 0, the word's bit 15
        jz      .both
        or      ah,0x80
.both:  mov     [V(vhigh)],ah
.ok:    clc
        ret
.high:  mov     al,[V(vhigh)]
.ret:   ret

; AL written to port DX of the game's 16-bit channel: to the controller as
; it is, and the front follows the channel's programming while this path
; traps it
dma_out:
        call    chip_out
        call    vtrapped
        cmc
        jnc     .ret
        mov     si,vpage
        cmp     dl,[V(vpagep)]
        je      .byte
        mov     ah,dl
        sub     ah,[V(vport)]
        mov     si,vaddr
        jz      .word
        mov     si,vcount
        cmp     ah,2
        je      .word
        mov     ah,al                   ; D4h-DEh, AH = 0 for D4h's and
        xor     ah,[V(dma16)]           ; D6h's channel
        and     ah,3
        movzx   bx,dl
        jmp     [cs:bx+dma_regs-0xD4]
.single:                                ; D4h: one channel's mask bit
        jnz     .ret
        test    al,0x04
        jz      .unmask
        jmp     short .mask
.mode:  jnz     .ret                    ; D6h: its mode
        mov     si,vmode
        jmp     short .byte
.all:   mov     cl,[V(dma16)]           ; DEh: every mask bit
        and     cl,3
        shr     al,cl
        test    al,1
        jz      .unmask
        jmp     short .mask
.clear: and     byte [V(vbits)],~V_FF & 0xFF    ; DAh, the master clear:
.mask:  or      byte [V(vbits)],V_MASK          ; masked too
        jmp     short vmask_changed
.unmask:                                ; DCh: every channel unmasked
        and     byte [V(vbits)],~V_MASK & 0xFF
        jmp     short vmask_changed
.ff:    and     byte [V(vbits)],~V_FF & 0xFF    ; D8h: the flip-flop cleared
.ret:   ret
.byte:  mov     [si],al
        jmp     short .new
.word:  xor     byte [V(vbits)],V_FF
        test    byte [V(vbits)],V_FF
        jnz     .byte
        mov     [si+1],al
.new:   or      byte [V(vbits)],V_NEW
        and     byte [V(vbits)],~V_LIVE & 0xFF
        ret
dma_regs:       dw dma_out.single, dma_out.mode, dma_out.ff, dma_out.clear
                dw dma_out.unmask, dma_out.all

; the game masked or unmasked its 16-bit channel: while a 16-bit transfer
; of the front's runs, on Audio 1 or Audio 2, the card's channel follows,
; and an unmasking after new programming carries it over
vmask_changed:
        test    byte [V(flags2)],F_RUN
        jnz     .runs
        mov     al,[V(flags)]
        and     al,F_RUN | F_16
        xor     al,F_RUN | F_16
        jnz     .ret
.runs:  test    byte [V(vbits)],V_MASK
        jnz     ch8_mask
        mov     al,[V(vbits)]
        and     al,V_NEW | V_LIVE
        cmp     al,V_LIVE
        jne     mirror
        jmp     ch8_unmask
.ret:   ret

; --- the interrupts ----------------------------------------------------------

; AL = the kinds of Audio 1's interrupts that are pending, AH Audio 2's:
; bit 0 8-bit, bit 1 16-bit. Audio 1's counter (+Ch bit 0) is its
; transfer's kind, its processor's (bit 2: F2h, the firmware's transfers,
; MIDI input) the 8-bit one unless F3h asked for it; Audio 2's latch (7Ah
; bit 7) is the 16-bit one
irq_src:
        push    dx
        mov     al,0x0C
        call    port_of
        call    chip_in
        mov     ah,al
        mov     al,0
        test    ah,0x01
        jz      .proc
        inc     ax
        test    byte [V(flags)],F_16
        jz      .proc
        inc     ax
.proc:  test    ah,0x04
        jz      .a2
        mov     ah,1
        test    byte [V(state)],S_T16
        jz      .or
        inc     ah
.or:    or      al,ah
.a2:    mov     dl,al
        mov     dh,0
        cmp     byte [V(dma2)],4
        jae     .ret
        mov     ah,0x7A
        call    mix_read
        shl     al,1                    ; the latch, into CF
        jnc     .ret
        mov     dh,2
.ret:   mov     ax,dx
        pop     dx
        ret

; the acknowledge of the kind CL (1 8-bit, 2 16-bit): Audio 1's interrupt
; when one of that kind is pending (a read of +Eh acknowledges them all),
; and Audio 2's for the 16-bit kind (7Ah bit 7 cleared). A queued block's
; turn has come: the auto-initialize ends with it
ack:
        call    irq_src
        push    ax
        test    al,cl
        jz      .a2
        push    dx
        mov     al,0x0E
        call    port_of
        call    chip_in
        pop     dx
        btr     word [V(flags)],F_LAST_BIT
        jnc     .a2
        and     byte [V(flags)],~F_AUTO & 0xFF
        mov     ax,0xB8FB
        call    reg_and
.a2:    pop     ax
        test    ah,cl
        jz      .ret
        mov     ax,0x7A7F
        call    mix_and
        btr     word [V(flags2)],F_LAST_BIT
        jnc     .ret
        and     byte [V(flags2)],~F_AUTO & 0xFF
        mov     ax,0x78EF
        jmp     mix_and
.ret:   ret

; --- the mixer -------------------------------------------------------------

; AL written to mixer register index, DX = the data port: the Sound
; Blaster 16's registers 30h-47h keep the game's view, with the bits the
; CT1745 keeps, and give the chip the levels they stand for; the Sound
; Blaster Pro's are views of them; the chip's own registers get the byte
; as it comes. A reset is the
; Sound Blaster 16's: its levels go to the chip, and the chip's other
; registers stay (the 3-D effect, the DACs' mode, the music DAC's source,
; Audio 2), which the chip's own reset would put back to their power-on
; values
mix_out:
        mov     cl,[V(index)]
        test    byte [V(flags)],F_ESS   ; the chip's own mixer for a game
        jnz     .chip                   ; that knows the chip
        or      cl,cl
        jz      .reset
        cmp     cl,0x30
        jb      .pro
        cmp     cl,0x47
        ja      .high
        movzx   bx,cl
        and     al,[mix_mask+bx-0x30]
        mov     [bx+V(mix)-0x30],al
.apply: call    mix_apply
.done:  clc
        ret
.pro:   call    pro_find
        jc      .chip
        jnz     .done                   ; 0Ch and 0Eh: nothing
        cmp     cl,0x0A
        je      .mic
        mov     ah,al                   ; a pair's nibbles, as the CT1745
        and     al,0xF0                 ; keeps them
        or      al,0x08
        shl     ah,4
        or      ah,0x08
        mov     [bx+V(mix)],ax
        lea     cx,[bx+0x31]
        call    mix_apply
        dec     cx
        jmp     short .apply
.mic:   and     al,7                    ; 0Ah's 3 bits
        shl     al,5
        or      al,0x18
        mov     [bx+V(mix)],al
        mov     cl,0x3A
        jmp     short .apply
.chip:  call    chip_out                ; the chip's own registers
        jmp     short .done
.high:  cmp     cl,0x80
        jb      .chip
        cmp     cl,0x81                 ; the 16-bit channel the game picks
        je      .select
        cmp     cl,0x82                 ; the card's IRQ and status: kept
        ja      .chip
        jmp     short .done
.select:
        and     al,0xE0                 ; bit 5 channel 5, 6 and 7 theirs,
        bsf     ax,ax                   ; the first that is set, as the
        jnz     .ch                     ; DSP picks it; none 0
        xor     ax,ax
.ch:    mov     [V(dma16)],al
        call    v_ports
        jmp     short .done
.reset: call    mix_defaults
        mov     cl,0x30
.all:   call    mix_apply
        inc     cx
        cmp     cl,0x3F
        jb      .all
        jmp     short .done

; AL = the game's read of mixer register index, DX = the data port
mix_in:
        test    byte [V(flags)],F_ESS
        jnz     .chip
        mov     cl,[V(index)]
        cmp     cl,0x30
        jb      .pro
        cmp     cl,0x47
        ja      .high
        movzx   bx,cl
        mov     al,[bx+V(mix)-0x30]
        jmp     short .out
.pro:   call    pro_find
        jc      .chip
        mov     al,0
        jnz     .out                    ; 0Ch and 0Eh: 00h
        mov     ax,[bx+V(mix)]
        cmp     cl,0x0A
        je      .mic
        and     al,0xF0
        shr     ah,4
        or      al,ah
        jmp     short .out
.mic:   shr     al,5
        jmp     short .out
.chip:  call    chip_in
        jmp     short .out
.high:  cmp     cl,0x80
        je      .irq
        cmp     cl,0x81
        je      .dma
        cmp     cl,0x82
        jne     .chip
        call    irq_src                 ; 82h: 20h, the interrupts that are
        or      al,ah                   ; pending, and the MPU-401's
        or      al,0x20
        mov     dx,[V(cfg)]             ; (Config_Base+6 bit 3)
        or      dx,dx
        jz      .out
        add     dl,6
        mov     ah,al
        call    chip_in
        and     al,0x08
        shr     al,1
        or      al,ah
        jmp     short .out
.irq:   mov     al,[V(mix80)]           ; 80h: F0h and the IRQ's bit
        jmp     short .out
.dma:   mov     al,[V(mix81)]           ; 81h: the 8-bit channel's bit, the
        movzx   cx,byte [V(dma16)]      ; 16-bit one's
        jcxz    .out
        bts     ax,cx
.out:   clc
        ret

; the Sound Blaster Pro's register CL (below 30h): BX = the view's offset
; of its left level, ZF set; ZF clear for 0Ch and 0Eh, which the CT1745
; doesn't have; CF set for the chip's own
pro_find:
        xor     bx,bx
.next:  cmp     [bx+pro_regs],cl
        je      .found
        inc     bx
        cmp     bl,6
        jb      .next
        cmp     cl,0x0C
        je      .nothing
        cmp     cl,0x0E
        je      .nothing
        stc
        ret
.nothing:
        or      cl,cl                   ; ZF and CF clear
        ret
.found: shl     bx,1
        cmp     al,al                   ; ZF set, CF clear
        ret
; master, voice, MIDI, CD, line and the mic, each at twice its place
pro_regs:       db 0x22, 0x04, 0x26, 0x28, 0x2E, 0x0A

; the bits of 30h-47h that the CT1745 keeps
mix_mask:       times 11 db 0xF8        ; 30h-3Ah
                db 0xC0, 0x1F, 0x7F, 0x7F       ; 3Bh-3Eh
                db 0xC0, 0xC0, 0xC0, 0xC0, 0x01 ; 3Fh-43h
                db 0xF0, 0xF0, 0xF0, 0xF0       ; 44h-47h

; the chip's level registers for the view's pairs: the register, the
; view's offset of the left level, the output switch bits that gate the
; left and the right (none for voice and MIDI); the mic's is mono. The
; voice is both DACs': Audio 2's comes last, apart from the levels the
; view takes from the chip (mix_fresh)
pairs:          db 0x14, 2, 0, 0        ; voice (Audio 1)
                db 0x36, 4, 0, 0        ; MIDI (FM)
                db 0x38, 6, 0x04, 0x02  ; CD (AuxA)
                db 0x3E, 8, 0x10, 0x08  ; line
                db 0x1A, 10, 0x01, 0x01 ; mic
                db 0x7C, 2, 0, 0        ; voice (Audio 2)

; the chip's level for the Sound Blaster 16's register CL, from the view:
; master left and right to 60h and 62h (6 bits), the pairs, the voice to
; both DACs' (14h, and 7Ch when Audio 2 plays the game's sound), the PC
; speaker to 3Ch (3 bits); the output switches gate CD, line and the mic,
; the input switches pick the record source of 1Ch
mix_apply:
        pusha
        mov     al,cl
        sub     al,0x30
        cmp     al,1
        jbe     .master
        cmp     al,0x0B
        jb      .pair
        je      .speaker
        cmp     al,0x0C
        je      .switches
        cmp     al,0x0E
        jbe     .source
.out:   popa
        ret
.master:
        movzx   bx,al                   ; 8 bits to the 6 of 60h and 62h
        mov     ah,0x60
        add     ah,bl
        add     ah,bl
        mov     al,[bx+V(mix)]
        shr     al,2
        call    mix_write
        jmp     short .out
.pair:  sub     al,2                    ; 32h-3Ah: the pair
        shr     al,1
        call    pair_out
        or      al,al
        jnz     .out
        cmp     byte [V(dma2)],4
        jae     .out
        mov     al,5                    ; the voice: Audio 2's too, when it
        call    pair_out                ; plays the game's sound
        jmp     short .out
.switches:                              ; CD, line and the mic again
        mov     al,2
.each:  call    pair_out
        inc     ax
        cmp     al,4
        jbe     .each
        jmp     short .out
.speaker:
        mov     al,[V(mix)+0x0B]
        shr     al,5
        mov     ah,0x3C
        call    mix_write
        jmp     short .out
.source:
        mov     dl,[V(mix)+0x0D]           ; the input switches of both channels:
        or      dl,[V(mix)+0x0E]           ; CL = a source alone, CH = how many
        xor     cx,cx
        mov     si,sources
.next:  lodsw
        test    dl,al
        jz      .skip
        mov     cl,ah
        inc     ch
.skip:  cmp     si,sources_end
        jb      .next
        cmp     ch,1
        jb      .out                    ; nothing: as it is
        je      .src
        mov     cl,5                    ; several: the record mixer
.src:   mov     ah,0x1C
        call    mix_read
        and     al,0xF8
        or      al,cl
        call    mix_write
        jmp     short .out
; the input switches' bits and the record source of 1Ch they pick: the
; mic, CD (AuxA), line, the music (the record mixer)
sources:        db 0x01, 0, 0x06, 2, 0x18, 6, 0x60, 5
sources_end:

; the chip's register of pair AL (pairs): the view's left and right
; levels' high nibbles (the mono mic's its own twice), each 0 while its
; output switch is off; clobbers AX, BX, DX, SI
pair_out:
        push    ax
        movzx   si,al
        shl     si,2
        add     si,pairs
        movzx   bx,byte [si+1]
        mov     ax,[bx+V(mix)]             ; AL left, AH right
        cmp     bl,10
        jne     .gate
        mov     ah,al
.gate:  mov     dx,[si+2]               ; DL, DH: the switch bits
        or      dl,dl
        jz      .nib
        test    [V(mix)+0x0C],dl
        jnz     .right
        mov     al,0
.right: test    [V(mix)+0x0C],dh
        jnz     .nib
        mov     ah,0
.nib:   and     al,0xF0
        shr     ah,4
        or      al,ah
        mov     ah,[si]
        call    mix_write
        pop     ax
        ret

; the Sound Blaster 16's reset values into the view
mix_defaults:
        xor     bx,bx
.copy:  mov     al,[bx+mix_default]
        mov     [bx+V(mix)],al
        inc     bx
        cmp     bl,MIX_LEN
        jb      .copy
        ret
mix_default:
        db 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0   ; master, voice, MIDI
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   ; CD, line, mic, PC speaker
        db 0x1F, 0x15, 0x0B, 0x00, 0x00, 0x00   ; the switches, the gains
        db 0x00, 0x00, 0x80, 0x80, 0x80, 0x80   ; AGC, treble, bass

; vport and vpagep for the 16-bit channel dma16
v_ports:
        movzx   bx,byte [V(dma16)]
        cmp     bl,5
        jb      .ret
        lea     ax,[bx-4]
        shl     al,2
        add     al,0xC0
        mov     [V(vport)],al
        mov     al,[page16+bx-5]
        mov     [V(vpagep)],al
.ret:   ret
page16:         db 0x8B, 0x89, 0x8A     ; the page registers of channels 5-7

resident_end:
RESIDENT_PARAS  equ (resident_end - $$ + 0x100 + 15) >> 4   ; with the PSP

; --- the installer ---------------------------------------------------------
; discarded once the TSR is in: everything from here on. With HDPMI, its
; first paragraphs stay as the host's data, the client's real-mode stack
; (HOST_PARAS at most), which the host takes at the switch: they hold the
; routines that run before it, up to host_code_end, never after
; (build_esssb16.py checks that they fill HOST_PARAS)

        align   16
host_area:

; the options on the command line (/S single transfers, /H5 /H6 /H7 /H0 the
; 16-bit channel, /20 /21 /23 Audio 2's channel, /2- no Audio 2, /A220 the
; base, /F0 /F1 the DACs' filters off or on, /O0 /O1 Audio 2's 4x
; oversampling off or on) and BLASTER's A, I, D and H; CF set for /? or an
; unknown option
parse_options:
        call    blaster
        mov     si,0x81
        movzx   cx,byte [0x80]
.next:  test    cx,cx
        jz      .done
        lodsb
        dec     cx
        cmp     al,'/'
        je      .option
        cmp     al,'-'
        jne     .next
.option:
        test    cx,cx
        jz      .bad
        lodsb
        dec     cx
        and     al,~0x20                ; upper case
        cmp     al,'S'
        je      .single
        cmp     al,'H'
        je      .h
        cmp     al,'A'
        je      .a
        cmp     al,'F'
        je      .f
        cmp     al,'O'
        je      .o
        cmp     al,'2' & ~0x20
        je      .two
        jmp     .bad
.two:   test    cx,cx                   ; /2n: Audio 2's channel, 0, 1 or 3,
        jz      .bad                    ; and /2- none
        lodsb
        dec     cx
        mov     ah,4
        cmp     al,'-'
        je      .set2
        sub     al,'0'
        mov     ah,al
        cmp     al,3
        ja      .bad
        cmp     al,2
        je      .bad
.set2:  mov     [want2],ah
        jmp     .next
.f:     mov     ah,0x0C                 ; mixer 71h bits 3 and 2: the filters
        mov     bl,'0'                  ; bypassed, set for /F0
        jmp     short .stage
.o:     mov     ah,0x10                 ; bit 4: Audio 2's 4x oversampling,
        mov     bl,'1'                  ; set for /O1
.stage: test    cx,cx
        jz      .bad
        lodsb
        dec     cx
        cmp     al,'0'
        je      .bits
        cmp     al,'1'
        jne     .bad
.bits:  not     ah
        and     [keep71],ah
        and     [set71],ah
        not     ah
        cmp     al,bl
        jne     .next
        or      [set71],ah
        jmp     .next
.single:
        mov     byte [b9],0
        jmp     .next
.h:     test    cx,cx
        jz      .bad
        lodsb
        dec     cx
        sub     al,'0'
        cmp     al,0
        je      .seth
        cmp     al,5
        jb      .bad
        cmp     al,7
        ja      .bad
.seth:  mov     [dma16],al
        jmp     .next
.a:     call    hex_word                ; the base, 3 hex digits
        jc      .bad
        mov     [base],ax
        jmp     .next
.done:  clc
        ret
.bad:   stc
        ret

; AX = the hex number at DS:SI (CX bytes left), CF set if none
hex_word:
        xor     ax,ax
        xor     dx,dx                   ; digits so far
.digit: jcxz    .end
        mov     bl,[si]
        sub     bl,'0'
        jc      .end
        cmp     bl,9
        jbe     .ok
        and     bl,~0x20
        sub     bl,'A' - '0'
        jc      .end
        cmp     bl,5
        ja      .end
        add     bl,10
.ok:    shl     ax,4
        or      al,bl
        inc     si
        dec     cx
        inc     dx
        jmp     short .digit
.end:   or      dx,dx
        jz      .none
        clc
        ret
.none:  stc
        ret

; BLASTER from the environment: A, I, D and H as the game will use them
blaster:
        push    es
        mov     es,[0x2C]               ; the environment segment
        xor     di,di
.var:   cmp     byte [es:di],0
        je      .done
        mov     si,blaster_name
        mov     bx,di
.cmp:   lodsb
        or      al,al
        jz      .found
        cmp     al,[es:bx]
        jne     .skip
        inc     bx
        jmp     short .cmp
.skip:  mov     cx,0x1000               ; past this variable
        xor     al,al
        repne   scasb
        jmp     short .var
.found: mov     si,bx                   ; its value
.field: mov     al,[es:si]
        inc     si
        or      al,al
        jz      .done
        and     al,~0x20
        cmp     al,'A'
        je      .a
        cmp     al,'I'
        je      .i
        cmp     al,'D'
        je      .d
        cmp     al,'H'
        je      .h
        jmp     short .field
.a:     call    env_hex
        mov     [base],ax
        jmp     short .field
.i:     call    env_dec
        mov     [irq],al
        jmp     short .field
.d:     call    env_dec
        mov     [dma8],al
        jmp     short .field
.h:     call    env_dec                 ; 5 to 7; else (H equal to D, as an
        cmp     al,5                    ; SB16 allows) 16-bit transfers on
        jb      .noh                    ; the 8-bit channel
        cmp     al,7
        jbe     .seth
.noh:   xor     al,al
.seth:  mov     [dma16],al
        jmp     short .field
.done:  pop     es
        ret

; AX = the hex number at ES:SI, which moves past it
env_hex:
        xor     ax,ax
.digit: mov     bl,[es:si]
        sub     bl,'0'
        jc      .end
        cmp     bl,9
        jbe     .ok
        and     bl,~0x20
        sub     bl,'A' - '0'
        jc      .end
        cmp     bl,5
        ja      .end
        add     bl,10
.ok:    shl     ax,4
        or      al,bl
        inc     si
        jmp     short .digit
.end:   ret

; AX = the decimal number at ES:SI, which moves past it
env_dec:
        xor     ax,ax
.digit: mov     bl,[es:si]
        sub     bl,'0'
        jc      .end
        cmp     bl,9
        ja      .end
        imul    ax,ax,10
        add     al,bl
        adc     ah,0
        inc     si
        jmp     short .digit
.end:   ret

; CF set under Windows (int 2Fh 1600h), where ES1869.VXD does this
windows_check:
        mov     ax,0x1600
        int     0x2F
        cmp     al,0x00
        je      .no
        cmp     al,0x80
        je      .no
        stc
        ret
.no:    clc
        ret

; CF set if a copy is resident already: every DOS memory block that a
; program owns, UMBs included, is looked at for the signature
installed_check:
        push    es
        mov     ax,0x5802               ; the UMB link, kept
        int     0x21
        push    ax
        mov     ax,0x5803
        mov     bx,1
        int     0x21
        mov     ah,0x52                 ; the first memory block
        int     0x21
        mov     ax,[es:bx-2]
        mov     bx,cs
.block: mov     es,ax
        cmp     byte [es:0],'M'
        je      .one
        cmp     byte [es:0],'Z'
        jne     .end
.one:   mov     dx,[es:1]               ; its owner
        mov     cx,ax
        inc     cx
        cmp     dx,cx                   ; a program's own block, not ours
        jne     .skip
        cmp     cx,bx
        je      .skip
        cmp     word [es:3],RESIDENT_PARAS
        jb      .skip
        mov     es,cx
        mov     di,signature
        mov     si,signature
        mov     cx,8
        repe    cmpsb
        je      .found
.skip:  mov     es,ax
        cmp     byte [es:0],'Z'
        je      .end
        add     ax,[es:3]
        inc     ax
        jmp     short .block
.found: pop     bx                      ; the link back
        xor     bh,bh
        mov     ax,0x5803
        int     0x21
        pop     es
        stc
        ret
.end:   pop     bx
        xor     bh,bh
        mov     ax,0x5803
        int     0x21
        pop     es
        clc
        ret

; CF set unless an ES1869 answers at the base: a DSP reset, then E7h
; (68h 8xh), then the identification sequence of mixer 40h (18h 69h and
; Config_Base)
find_card:
        mov     dx,[base]
        add     dx,6
        mov     al,1
        out     dx,al
        in      al,dx                   ; 3 us
        in      al,dx
        in      al,dx
        xor     al,al
        out     dx,al
        add     dx,8                    ; Audio_Base+Eh
        mov     cx,2000
.wait:  in      al,dx
        test    al,0x80
        jnz     .ready
        loop    .wait
        jmp     .no
.ready: sub     dx,4                    ; Audio_Base+Ah
        in      al,dx
        cmp     al,0xAA
        jne     .no
        mov     al,0xE7
        call    dsp_put
        call    find_byte
        jc      .no
        cmp     al,0x68
        jne     .no
        call    find_byte
        jc      .no
        and     al,0xF0
        cmp     al,0x80
        jne     .no
        mov     dx,[base]
        add     dx,4
        mov     al,0x40
        out     dx,al
        inc     dx
        in      al,dx
        cmp     al,0x18
        jne     .no
        in      al,dx
        cmp     al,0x69
        jne     .no
        in      al,dx
        mov     ah,al
        in      al,dx
        cmp     ax,0x100
        jb      .nocfg
        cmp     ax,0xFF8
        ja      .nocfg
        test    al,7
        jnz     .nocfg
        mov     [cfg],ax
.nocfg: clc
        ret
.no:    stc
        ret

; AL = the DSP's next answer byte, CF set if none comes
find_byte:
        mov     dx,[base]
        add     dx,0x0C
        mov     cx,DSP_POLLS
.wait:  in      al,dx
        test    al,0x40
        jnz     .got
        loop    .wait
        stc
        ret
.got:   sub     dx,2
        in      al,dx
        clc
        ret

; the view's levels from the chip, at the start: the master from 60h and
; 62h (0 when muted), the pairs' nibbles where their switches are on, the
; PC speaker, and the record source as the input switches that pick it:
; the mic, CD or line alone, the record mixer as the reset's several, else
; none, so that writing them back keeps it. Once the TSR is in, the view
; is the game's: the chip's own registers that a program writes don't
; change it
mix_fresh:
        pusha
        mov     ax,0x6000               ; AH = 60h, then 62h
        mov     bx,mix
.master:
        call    mix_read
        test    al,0x40
        jz      .on
        mov     al,0
.on:    and     al,0x3E                 ; 6 bits to the 5 of 30h and 31h
        shl     al,2
        mov     [bx],al
        inc     bx
        add     ah,2
        cmp     ah,0x62
        jbe     .master
        mov     si,pairs
.pair:  lodsw                           ; AL = the register, AH = the offset
        movzx   bx,ah
        mov     ah,al
        call    mix_read
        mov     dl,al                   ; the chip's nibbles
        lodsw                           ; AL, AH: the switch bits
        mov     cl,[V(mix)+0x0C]
        or      al,al
        jz      .left
        test    cl,al
        jz      .right                  ; switched off: the view's level kept
.left:  mov     dh,dl
        and     dh,0xF0
        mov     [bx+V(mix)],dh
.right: cmp     bl,10
        je      .next                   ; the mono mic: the left only
        or      ah,ah
        jz      .r
        test    cl,ah
        jz      .next
.r:     shl     dl,4
        mov     [bx+V(mix)+1],dl
.next:  cmp     si,pairs + 20
        jb      .pair
        mov     ah,0x3C                 ; the PC speaker, the 2 bits of
        call    mix_read                ; 3Bh
        and     al,0x06
        shl     al,5
        mov     [V(mix)+0x0B],al
        mov     ah,0x1C                 ; the record source
        call    mix_read
        and     al,0x07
        mov     cx,0x0101               ; CH left, CL right: the mic
        or      al,al
        jz      .src
        mov     cx,0x0402               ; CD
        cmp     al,2
        je      .src
        mov     cx,0x1008               ; line
        cmp     al,6
        je      .src
        mov     cx,0x150B               ; the record mixer
        cmp     al,5
        je      .src
        xor     cx,cx
.src:   mov     [V(mix)+0x0D],ch
        mov     [V(mix)+0x0E],cl
        popa
        ret

; AL = PnP register BL of logical device 1 (pnp_read), and the register =
; AL (pnp_write), through the config port; DX = its data port
pnp_read:
        call    pnp_at
        in      al,dx
        ret
pnp_write:
        call    pnp_at
        out     dx,al
        ret
pnp_at:
        push    ax
        mov     dx,[cfg]
        mov     al,0x07
        out     dx,al
        inc     dx
        mov     al,1
        out     dx,al
        dec     dx
        mov     al,bl
        out     dx,al
        inc     dx
        pop     ax
        ret

; the card's IRQ and 8-bit channel from its PnP registers, where the
; config port was found, else BLASTER's, and Audio 2; then the mixer's view
setup_card:
        cmp     word [cfg],0
        je      .view
        mov     bl,0x70
        call    pnp_read
        and     al,0x0F
        mov     [irq],al
        mov     bl,0x74
        call    pnp_read
        and     al,0x07
        cmp     al,4                    ; 4 = none
        jae     .a2
        mov     [dma8],al
.a2:    call    audio2_setup
.view:  mov     al,[dma8]               ; the 16-bit channel goes to Audio
        mov     [mchan],al              ; 1's until Audio 2 plays it
        movzx   bx,byte [irq]           ; mixer 80h and 81h's 8-bit channel
        and     bl,0x0F
        mov     al,[irq_bit+bx]
        mov     [mix80],al
        mov     cl,[dma8]
        mov     al,1
        shl     al,cl
        mov     [mix81],al
        call    v_ports                 ; the game's 16-bit channel's ports
        call    dac_options             ; mixer 71h as /F and /O say
        mov     ah,0x0E                 ; the Sound Blaster Pro's stereo off:
        call    mix_read                ; the front never passes 0Eh on
        and     al,~0x02 & 0xFF
        call    mix_write
        call    mix_defaults            ; the view as a Sound Blaster 16 shows
        call    mix_fresh               ; the levels it has: the chip's, the
        cmp     byte [dma2],4           ; rest as after a reset; Audio 2's
        jae     .out                    ; volume the voice's
        mov     al,5
        jmp     pair_out
.out:   ret

; Audio 2 for the 16-bit playback (dma2): on the channel /2 gives it, when
; one of the card's four DMA pin pairs is wired to that channel (card
; registers 23h and 24h, a channel in each nibble), else on the card's
; (PnP 75h), if it is an 8-bit channel other than Audio 1's; its interrupt
; on the card's (PnP 72h = 70h) and unmasked (Config_Base+7 bit 1, which a
; DOS program may have cleared, DS p.18), and its own rate (71h bit 1).
; /2- leaves Audio 2 alone
audio2_setup:
        mov     al,[want2]
        cmp     al,4
        je      .out
        ja      .card
        mov     bl,0x23
        call    .wired
        je      .move
        mov     bl,0x24
        call    .wired
        je      .move
        mov     dx,msg_unwired
        call    print
        jmp     short .card
.move:  mov     bl,0x75
        call    pnp_write
.card:  mov     bl,0x75
        call    pnp_read
        and     al,0x07
        cmp     al,3
        ja      .out                    ; none (4), or a 16-bit channel
        cmp     al,2
        je      .out
        cmp     al,[dma8]
        je      .out
        mov     [dma2],al
        mov     bl,0x72
        call    pnp_read
        and     al,0x0F
        cmp     al,[irq]
        je      .irq
        mov     al,[irq]
        call    pnp_write
.irq:   mov     dx,[cfg]
        add     dx,byte 7
        in      al,dx
        or      al,0x02
        out     dx,al
        or      byte [set71],0x02
        mov     ax,0x7800               ; stopped, its interrupt off
        call    mix_write
        mov     ax,0x7A00
        jmp     mix_write
.out:   ret
; ZF set if a nibble of card register BL is channel AL, AL kept
.wired: mov     ah,al
        call    pnp_read
        mov     bh,al
        and     al,0x0F
        cmp     al,ah
        je      .w
        shr     bh,4
        cmp     bh,ah
.w:     mov     al,ah
        ret

; what was found, and the BLASTER to set
report:
        mov     dx,msg_card
        call    print
        mov     ax,[base]
        call    print_hex3
        mov     dx,msg_irq
        call    print
        movzx   ax,byte [irq]
        call    print_dec
        mov     dx,msg_dma
        call    print
        movzx   ax,byte [dma8]
        call    print_dec
        cmp     byte [dma16],0
        je      .blaster
        mov     dx,msg_and
        call    print
        movzx   ax,byte [dma16]
        call    print_dec
        mov     dx,msg_through
        movzx   ax,byte [dma2]
        cmp     al,4
        jae     .one
        mov     dx,msg_audio2           ; on Audio 2's channel
        call    print
        call    print_dec
        jmp     short .blaster
.one:   call    print                   ; on Audio 1's, and why
        cmp     byte [want2],4
        je      .blaster
        mov     dx,msg_no_audio2
        call    print
.blaster:
        mov     dx,msg_blaster
        call    print
        mov     ax,[base]
        call    print_hex3
        mov     dx,msg_i
        call    print
        movzx   ax,byte [irq]
        call    print_dec
        mov     dx,msg_d
        call    print
        movzx   ax,byte [dma8]
        call    print_dec
        mov     dx,msg_h
        call    print
        movzx   ax,byte [dma16]
        or      al,al
        jnz     .h
        mov     al,[dma8]               ; H = D: 16-bit on the 8-bit channel
.h:     call    print_dec
        mov     dx,msg_t
        call    print
        ret
host_code_end:

install:
        mov     ax,cs
        mov     ds,ax
        mov     es,ax
        cld
        mov     dx,banner
        call    print
        call    parse_options
        jc      .usage
        call    windows_check
        jc      .windows
        call    installed_check
        jc      .loaded
        mov     di,dbase                ; the resident routines' data
        call    find_card
        jc      .nocard
        call    setup_card
        call    report
        call    trap_v86                ; QPI, else EMM386
        jc      .notrap
        call    free_environment
        call    trap_dpmi               ; HDPMI32i, and the TSR from there
        mov     dx,RESIDENT_PARAS
        mov     ax,0x3100
        int     0x21
.usage: mov     dx,usage
        call    print
        mov     ax,0x4C00 | E_USAGE
        int     0x21
.windows:
        mov     dx,msg_windows
        mov     al,E_WINDOWS
        jmp     short .fail
.loaded:
        mov     dx,msg_loaded
        mov     al,E_LOADED
        jmp     short .fail
.nocard:
        mov     dx,msg_nocard
        mov     al,E_NOCARD
        jmp     short .fail
.notrap:
        mov     dx,msg_notrap
        mov     al,E_NOTRAP
.fail:  push    ax
        call    print
        pop     ax
        mov     ah,0x4C
        int     0x21

; the string at DX, $-terminated
print:  push    ax
        mov     ah,0x09
        int     0x21
        pop     ax
        ret

; the number in AX in decimal
print_dec:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     bx,10
        xor     cx,cx
.digit: xor     dx,dx
        div     bx
        push    dx
        inc     cx
        or      ax,ax
        jnz     .digit
.put:   pop     dx
        add     dl,'0'
        mov     ah,0x02
        int     0x21
        loop    .put
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; the number in AX in hex, 3 digits
print_hex3:
        push    ax
        push    cx
        push    dx
        mov     cx,3
        rol     ax,4                    ; past the top digit, a 0
.digit: rol     ax,4
        push    ax
        and     al,0x0F
        add     al,'0'
        cmp     al,'9'
        jbe     .put
        add     al,'A' - '0' - 10
.put:   mov     dl,al
        mov     ah,0x02
        int     0x21
        pop     ax
        loop    .digit
        pop     dx
        pop     cx
        pop     ax
        ret

; the traps for real-mode games: QPI (QEMM, or JEMM with QPIEMU), else
; EMM386's API. CF set if neither is there, each having said why
trap_v86:
        call    qpi_find
        jc      .emm
        call    qpi_install
        jc      .emm
        mov     dx,msg_qpi
        call    print
        clc
        ret
.emm:   call    emm_install
        jc      .none
        mov     dx,msg_emm
        call    print
        mov     ax,[emm_version]
        xchg    al,ah
        call    print_version
        mov     dx,msg_emm2
        call    print
        clc
        ret
.none:  stc
        ret

; AX = a handle on the device named at DX, CF set if there is none
dev_open:
        mov     ax,0x3D00
        int     0x21
        ret

; "AH.AL", the minor version with two digits
print_version:
        push    ax
        push    dx
        push    ax
        movzx   ax,ah
        call    print_dec
        mov     dl,'.'
        mov     ah,0x02
        int     0x21
        pop     ax
        cmp     al,10
        jae     .minor
        push    ax
        mov     dl,'0'
        mov     ah,0x02
        int     0x21
        pop     ax
.minor: movzx   ax,al
        call    print_dec
        pop     dx
        pop     ax
        ret

; the QPI entry into qpi: QEMM's, through its device, or QPIEMU's (int 2Fh
; 1684h, device 4354h); CF set if neither answers
qpi_find:
        mov     dx,qemm_device
        call    dev_open
        jc      .emu
        mov     bx,ax
        mov     dx,qpi
        mov     cx,4
        mov     ax,0x4402
        int     0x21
        pushf
        mov     ah,0x3E
        int     0x21
        popf
        jc      .emu
        cmp     dword [qpi],0
        je      .emu
        mov     ah,0x03                 ; its version, 7.03 or later
        call    far [qpi]
        cmp     bx,0x0703
        jae     .found
        mov     dx,msg_qemm
        call    print
        mov     ax,bx
        call    print_version
        mov     dx,msg_qpi_old
        call    print
        jmp     short .emu
.found: clc
        ret
.emu:   mov     dword [qpi],0
        push    es
        mov     ax,0x1684
        mov     bx,0x4354
        xor     di,di
        mov     es,di
        int     0x2F
        mov     cx,es
        pop     es
        or      al,al
        jnz     .none
        mov     [qpi],di
        mov     [qpi+2],cx
        or      cx,di
        jz      .none
        clc
        ret
.none:  stc
        ret

; the front's ports trapped through QPI: the callback before ours kept
; (1A06h), ours set (1A07h), each port trapped (1A09h)
qpi_install:
        push    es
        mov     ax,0x1A06
        call    far [qpi]
        jc      .fail
        mov     [qpi_old],di
        mov     [qpi_old+2],es
        mov     ax,es
        or      ax,di
        jz      .first
        mov     byte [qpi_how],IO_CHAIN ; the ports not the front's go on
.first: push    cs
        pop     es
        mov     di,qpi_shim
        mov     ax,0x1A07
        call    far [qpi]
        jc      .fail
        mov     si,trap_ports
        mov     cx,7
.port:  mov     dx,[base]
        add     dl,[si]
        inc     si
        mov     ax,0x1A09
        call    far [qpi]
        jc      .fail
        loop    .port
        call    dma_list_fill           ; the game's 16-bit channel's ports:
        jc      .done                   ; all of them or none
        mov     si,dma_list
        mov     cx,DMA_PORTS
.dport: movzx   dx,byte [si]
        inc     si
        mov     ax,0x1A09
        call    far [qpi]
        jc      .undo
        loop    .dport
        mov     bx,IO_QPI
        call    dma_trapped
        mov     bl,IO_CHAIN
        call    dma_trapped
.done:  pop     es
        clc
        ret
.undo:  mov     si,dma_list
        mov     cx,DMA_PORTS
.free:  movzx   dx,byte [si]
        inc     si
        mov     ax,0x1A0A
        call    far [qpi]
        loop    .free
        jmp     short .done
.fail:  pop     es
        mov     dx,msg_qpi_refused
        call    print
        stc
        ret

; the front's ports trapped through EMM386 (int 2Fh 4A15h, 4.46 or later):
; one dispatch table of Audio_Base+4h to +Fh, where +7h to +9h, +Bh and
; +Dh pass. EMM386 keeps the table where it is and searches it at every
; trapped access, through a ring 0 selector of this segment as long as DI
; says, so the table and its handlers are resident. CF set, having said
; why, without EMM386's device (EMMXXXX0, or EMMQXXX0 with NOEMS), with an
; older one, or when it refuses
emm_install:
        mov     dx,emm_device
        call    dev_open
        jnc     .open
        mov     dx,emm_device2
        call    dev_open
        jc      .nodev
.open:  mov     bx,ax                   ; the version, read through the
        mov     dx,emm_version          ; device's IOCTL: the buffer's first
        mov     cx,2                    ; byte asks for subfunction 2, and
        mov     ax,0x4402               ; comes back as the major version,
        int     0x21                    ; the second as the minor
        pushf
        mov     ah,0x3E
        int     0x21
        popf
        jc      .nodev
        cmp     byte [emm_version],4
        jb      .old
        ja      .ok
        cmp     byte [emm_version+1],46
        jb      .old
.ok:    mov     di,emm_table            ; each port with its handler
        mov     si,trap_handlers
        mov     cx,12
        mov     ax,[base]
        add     ax,4
.fill:  stosw
        movsw
        inc     ax
        loop    .fill
        mov     si,emm_table
        mov     cx,12
        mov     dx,[base]
        add     dx,4
        call    emm_call
        jc      .refused
        clc
        ret
.nodev: mov     dx,msg_emm_nodev
        jmp     short .fail
.old:   mov     dx,msg_emm_old
        jmp     short .version
.refused:
        mov     dx,msg_emm_refused
.version:
        push    dx
        mov     dx,msg_emm386
        call    print
        mov     ax,[emm_version]
        xchg    al,ah
        call    print_version
        pop     dx
.fail:  call    print
        stc
        ret

; int 2Fh 4A15h for the CX ports from DX with the table at DS:SI
emm_call:
        mov     ax,dx
        add     ax,cx
        dec     ax
        shl     eax,16
        mov     ax,dx                   ; EDX = the last port : the first
        mov     edx,eax
        mov     di,RESIDENT_PARAS * 16  ; the resident block, for the selectors
        mov     ax,0x4A15
        xor     bx,bx
        stc
        int     0x2F
        ret

; the PSP's environment freed
free_environment:
        push    es
        mov     es,[0x2C]
        mov     ah,0x49
        int     0x21
        pop     es
        mov     word [0x2C],0
        ret

; the traps for protected-mode games, through HDPMI32i, when the DPMI host
; is HDPMI (its logo starts the entry's segment, as HDPMI itself checks):
; the TSR enters protected mode as a 32-bit client and stays resident from
; there, as SBEMU does, so that its selectors live on, with the host's data
; in the paragraphs after the resident part. The host's first CS is a
; 16-bit selector of this segment whatever the client (DPMI 0.9), so the
; code after the switch is 16-bit, with the 32-bit registers a 32-bit
; client passes, and that CS and DS serve the handler. Returns in real
; mode only without the switch
trap_dpmi:
        mov     ax,0x1687
        int     0x2F
        or      ax,ax
        jnz     .none
        test    bl,1                    ; 32-bit clients
        jz      .none
        cmp     dword [es:0],'HDPM'     ; HDPMI's logo
        jne     .none
        cmp     byte [es:4],'I'
        jne     .none
        cmp     si,HOST_PARAS
        ja      .none
        mov     [dpmi_entry],di
        mov     [dpmi_entry+2],es
        mov     [host_paras],si
        mov     ax,cs                   ; the host's data
        add     ax,RESIDENT_PARAS
        mov     es,ax
        mov     ax,1                    ; a 32-bit client
        call    far [dpmi_entry]
        jc      .none                   ; still in real mode
        mov     [pm_data],ds            ; protected mode: DS is this
        mov     ax,0x168A               ; segment's data selector, CS its
        mov     esi,hdpmi_name          ; code one; HDPMI's vendor API
        int     0x2F
        or      al,al
        jnz     .nohdpmi
        mov     [vendor],edi
        mov     [vendor_sel],es
        mov     bx,hdpmi_ranges         ; the front's ports, trapped for it
.trap:  push    bx
        movzx   esi,byte [bx]           ; ESI = the first port, EDI = how
        add     si,[base]               ; many, CX:EDX = the handler, BX
        movzx   edi,byte [bx+1]         ; its data
        mov     cx,cs
        mov     edx,hdpmi_shim
        mov     bx,[pm_data]
        mov     ax,6
        o32 call far [vendor]
        pop     bx
        jc      .release
        mov     [bx+2],eax              ; the handle
        add     bx,6
        cmp     bx,hdpmi_ranges_end
        jb      .trap
        call    dma_list_fill           ; the game's 16-bit channel's ports:
        jc      .said                   ; all of them or none
        mov     bx,dma_ranges
.dtrap: push    bx
        movzx   esi,byte [bx]
        movzx   edi,byte [bx+1]
        mov     cx,cs
        mov     edx,hdpmi_shim
        mov     bx,[pm_data]
        mov     ax,6
        o32 call far [vendor]
        pop     bx
        jc      .drelease
        mov     [bx+2],eax
        add     bx,6
        cmp     bx,dma_ranges_end
        jb      .dtrap
        mov     bx,IO_HDPMI
        call    dma_trapped
        jmp     short .said
.drelease:
        sub     bx,6
        cmp     bx,dma_ranges
        jb      .said
        mov     edx,[bx+2]
        mov     ax,7
        o32 call far [vendor]
        jmp     short .drelease
.said:  mov     edx,msg_hdpmi
        jmp     short .tsr
.release:                               ; all or none: HDPMI32, without the
        sub     bx,6                    ; i, refuses the first
        cmp     bx,hdpmi_ranges
        jb      .nohdpmi
        mov     edx,[bx+2]
        mov     ax,7
        o32 call far [vendor]
        jmp     short .release
.nohdpmi:
        mov     edx,msg_nohdpmi
.tsr:   mov     ah,0x09
        int     0x21
        mov     dword [rmcs_eax],0x3100 ; the TSR, as a real-mode call from
        movzx   eax,word [host_paras]   ; which DOS doesn't return, with the
        add     ax,RESIDENT_PARAS       ; host's data
        mov     [rmcs_edx],eax
        push    ds
        pop     es
        mov     edi,rmcs
        mov     ax,0x0300
        mov     bx,0x0021
        xor     ecx,ecx
        int     0x31
        mov     dx,[rmcs_edx]           ; not reached: the same through the
        mov     ax,0x3100               ; host
        int     0x21
.none:  push    cs                      ; real mode
        pop     ds
        mov     dx,msg_nohdpmi
        call    print
        ret

; the port offsets the front traps, and EMM386's handler for each of
; Audio_Base+4h to +Fh
trap_ports:     db 0x04, 0x05, 0x06, 0x0A, 0x0C, 0x0E, 0x0F
trap_handlers:  dw emm_shim, emm_shim, emm_shim, emm_pass    ; +4h to +7h
                dw emm_pass, emm_pass, emm_shim, emm_pass    ; +8h to +Bh
                dw emm_shim, emm_pass, emm_shim, emm_shim    ; +Ch to +Fh
; HDPMI32i's ranges of them: the first port, how many, the trap's handle
hdpmi_ranges:   db 0x04, 3
                dd 0
                db 0x0A, 1
                dd 0
                db 0x0C, 1
                dd 0
                db 0x0E, 2
                dd 0
hdpmi_ranges_end:
; and of the game's 16-bit channel's ports: its address and count, its page,
; its controller's D4h-DEh (filled in by dma_list_fill)
dma_ranges:     db 0, 3
                dd 0
                db 0, 1
                dd 0
                db 0xD4, 11
                dd 0
dma_ranges_end:
; the same for QPI, port by port
DMA_PORTS       equ 9
dma_list:       db 0, 0, 0, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC, 0xDE

; the lists of the game's 16-bit channel's ports; CF set without one
dma_list_fill:
        cmp     byte [dma16],5
        jb      .none
        mov     al,[vport]
        mov     [dma_list],al
        mov     [dma_ranges],al
        add     al,2
        mov     [dma_list+1],al
        mov     al,[vpagep]
        mov     [dma_list+2],al
        mov     [dma_ranges+6],al
        clc
        ret
.none:  stc
        ret

; the path BX (IO_*) traps the game's 16-bit channel, which is dma16
dma_trapped:
        mov     al,[dma16]
        mov     [vchan+bx],al
        ret
want2:          db 0xFF         ; /2's channel for Audio 2, 4 for /2-,
                                ; FFh for the card's
irq:            db 5            ; the card's IRQ, and its bit in mixer 80h
irq_bit:        db 0xF0, 0xF0, 0xF1, 0xF0, 0xF0, 0xF2, 0xF0, 0xF4
                db 0xF0, 0xF1, 0xF8, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0
emm_version:    db 2, 0         ; the IOCTL's subfunction 2, the version
dpmi_entry:     dd 0
host_paras:     dw 0            ; the DPMI host's data, in paragraphs
hdpmi_name:     db "HDPMI", 0
rmcs:           times 7 dd 0    ; the real-mode call structure: EDI, ESI,
rmcs_eax:       dd 0            ; EBP, reserved, EBX, EDX, ECX; EAX
                dw 0, 0, 0, 0, 0, 0, 0, 0, 0    ; flags, ES, DS, FS, GS, IP,
                                                ; CS; SP, SS = 0: the host's
rmcs_edx        equ rmcs + 5 * 4

qemm_device:    db "QEMM386$", 0
emm_device:     db "EMMXXXX0", 0
emm_device2:    db "EMMQXXX0", 0
blaster_name:   db "BLASTER=", 0

banner:         db "esssb16 1.0: a Sound Blaster 16 for DOS games on the ES1869", 13, 10, "$"
usage:          db "usage: esssb16 [/A220] [/H5|/H6|/H7|/H0] [/20|/21|/23|/2-] [/S] [/F0|/F1]", 13, 10
                db "       [/O0|/O1]", 13, 10
                db "  /A  the card's base, else BLASTER's A (220)", 13, 10
                db "  /H  the game's 16-bit DMA channel, else BLASTER's H (5); 0: none", 13, 10
                db "  /2  Audio 2's DMA channel, for 16-bit sound, else the card's; -: none", 13, 10
                db "  /S  single DMA transfers (ESS's Single Mode DMA)", 13, 10
                db "  /F  the DACs' filters off (0) or on (1), else as they are", 13, 10
                db "  /O  Audio 2's 4x oversampling off (0) or on (1), else as it is", 13, 10
                db "needs EMM386 4.46+, QEMM or JEMM with QPIEMU, and for", 13, 10
                db "protected-mode games HDPMI32i, loaded resident first", 13, 10, "$"
msg_windows:    db "not for a Windows DOS box: ES1869.VXD does this there", 13, 10, "$"
msg_loaded:     db "already loaded", 13, 10, "$"
msg_nocard:     db "no ES1869 at the base: set BLASTER or /A", 13, 10, "$"
msg_notrap:     db "no port traps: load EMM386 4.46+, QEMM, or JEMM with QPIEMU first", 13, 10, "$"
msg_card:       db "ES1869 at $"
msg_irq:        db "h, IRQ $"
msg_dma:        db ", DMA $"
msg_and:        db ", the game's DMA $"
msg_through:    db " through it$"
msg_audio2:     db " on Audio 2's DMA $"
msg_no_audio2:  db 13, 10, "16-bit sound plays on Audio 1: Audio 2 has no 8-bit DMA channel of its own (/2)$"
msg_unwired:    db "Audio 2 can't have that DMA channel: no DMA pin of the card is wired to it", 13, 10, "$"
msg_blaster:    db 13, 10, "set BLASTER=A$"
msg_i:          db " I$"
msg_d:          db " D$"
msg_h:          db " H$"
msg_t:          db " P330 T6", 13, 10, "$"
msg_qpi:        db "traps: QPI for real-mode games$"
msg_emm:        db "traps: EMM386 $"
msg_emm2:       db " for real-mode games$"
msg_qemm:       db "QEMM's QPI $"
msg_qpi_old:    db " is older than 7.03", 13, 10, "$"
msg_qpi_refused: db "QPI refused the port traps", 13, 10, "$"
msg_emm_nodev:  db "no EMM386: no EMMXXXX0 or EMMQXXX0 device", 13, 10, "$"
msg_emm386:     db "EMM386 $"
msg_emm_old:    db " is older than 4.46", 13, 10, "$"
msg_emm_refused: db " refused the port traps", 13, 10, "$"
msg_hdpmi:      db ", HDPMI32i for protected-mode games", 13, 10, "$"
msg_nohdpmi:    db "; protected-mode games play 8-bit without HDPMI32i", 13, 10, "$"

install_end:
