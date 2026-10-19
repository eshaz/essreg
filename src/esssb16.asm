; esssb16.com: a Sound Blaster 16 in front of the ES1869 for DOS games.
;
; Notes:
;
; The chip answers DSP commands as a Sound Blaster Pro (version 3.01) and
; plays 16-bit sound only through its own Extended mode, so a game written
; for the Sound Blaster 16 plays 8-bit sound on it at best. This TSR traps
; the game's accesses to seven of the Sound Blaster ports (the mixer, the
; reset, the read data, the command, the status and the 16-bit acknowledge
; ports), answers as a Sound Blaster 16 (version 4.05), and turns the
; game's commands into the chip's own: every transfer plays or records on
; Audio 1 in Extended mode, 16-bit ones two bytes a sample, on the card's
; 8-bit DMA channel, because the chip takes a byte per DMA cycle whatever
; the channel (DS p.14: the audio channels map to channels 0, 1 and 3). A
; game that programs its 16-bit channel (the H of BLASTER, mixer 81h) has
; that programming carried over to the 8-bit channel at each transfer
; command. A game that knows the chip (E7h) gets the chip as it is.
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
QUEUE_LEN       equ 48          ; bytes of the read queue (the copyright string)
DSP_POLLS       equ 0x400       ; polls for the chip's answer
MIX_LEN         equ 24          ; the Sound Blaster 16's mixer registers 30h-47h
STACK_LEN       equ 256         ; the front's stack: about 130 bytes at most
HOST_PARAS      equ 0x20        ; the DPMI host's data at most: HDPMI's
                                ; 512-byte real-mode stack

; the state flags (S16 in the VxD front)
F_ESS           equ 0x01        ; the program knows the chip (E7h): every
                                ; command is the chip's
F_EXT           equ 0x02        ; the chip is in Extended mode (C6h)
F_16            equ 0x04        ; the last transfer is 16-bit (mixer 82h bit 1)
F_8             equ 0x08        ; it is 8-bit (bit 0)
F_RUN           equ 0x10        ; a transfer of the front's runs on Audio 1
F_AUTO          equ 0x20        ; it auto-initializes
F_RESET         equ 0x80        ; the reset port is at 1

; the trap paths (how_io)
IO_DIRECT       equ 0           ; in and out: V86 (QPI) and ring 0 (EMM386)
IO_HDPMI        equ 1           ; through HDPMI32i's untrapped I/O (ring 3)

; installer results
E_USAGE         equ 1
E_NOCARD        equ 2
E_NOTRAP        equ 3
E_WINDOWS       equ 4
E_LOADED        equ 5

start:  jmp     near install

; --- resident data ---------------------------------------------------------

signature:      db "ESSSB16", 0 ; found by the installed check
flags:          db 0            ; F_*
cmd:            db 0            ; the command whose operands come
need:           db 0            ; operands still to come
got:            db 0            ; operands so far
ops:            db 0, 0, 0      ; the operands
pass:           db 0            ; 1 while the command's bytes go to the chip
index:          db 0            ; the mixer index the game wrote
tc:             db 0            ; the time constant of 40h, 0 if none
rate:           dw 0            ; the rate of 41h/42h (Hz), 0 if none
qcount:         db 0            ; bytes in the read queue
qhead:          db 0            ; where its next byte is
queue:          times QUEUE_LEN db 0
mix:            times MIX_LEN db 0      ; the game's view of 30h-47h
ctrl:           times 32 db 0   ; the chip's registers A0h-BFh as the front
                                ; last wrote or read them
known:          dd 0            ; bit n set while ctrl+n holds A0h+n
stale:          db 0            ; the view's levels a Sound Blaster Pro
                                ; write changed (bit 0 master, then aliases)
how_io:         db IO_DIRECT    ; IO_*
base:           dw 0x220        ; Audio_Base
cfg:            dw 0            ; Config_Base, 0 if none
irq:            db 5            ; the card's IRQ
dma8:           db 1            ; Audio 1's channel
dma16:          db 5            ; the Sound Blaster 16's 16-bit channel, 0
                                ; for none (16-bit on Audio 1 then)
single:         db 0            ; /S: single transfers
qpi:            dd 0            ; the QPI entry (seg:off), 0 if none
qpi_old:        dd 0            ; the I/O callback before ours, 0 if none
emm_table:      times 12 dw 0, 0        ; EMM386's dispatch table: the ports
                                        ; of Audio_Base+4h to +Fh, each with
                                        ; its handler, which EMM386 searches
                                        ; at every trapped access
vendor:         dd 0            ; HDPMI32i's vendor entry: offset
vendor_sel:     dw 0            ; and selector
pm_data:        dw 0            ; this segment's data selector, from the
                                ; DPMI switch
pm_result:      db 0            ; the byte read, for the protected-mode shim
save_ss:        dw 0            ; the caller's stack while a shim runs on
save_esp:       dd 0            ; the front's
stack:          times STACK_LEN db 0
stack_top:

; --- the trap shims ------------------------------------------------------

; QPI's I/O callback (QEMM, QPIEMU), in V86 mode: DX = port, CL = the
; direction (0 in, else out), AL = the byte written; AL = the byte read.
; The caller's stack may be small, so the front runs on its own. The
; previous callback gets the ports that aren't the front's
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
        cmp     dword [cs:qpi_old],0
        je      .io
        jmp     far [cs:qpi_old]
.io:    push    bx                      ; nobody's: QPI's untrapped in (1A00h)
        push    cx                      ; and out (1A01h), the byte in BL
        mov     bl,al
        mov     ax,0x1A01
        test    cl,cl
        jnz     .do
        mov     ax,0x1A00
.do:    call    far [cs:qpi]
        mov     al,bl
        pop     cx
        pop     bx
        clc
        retf

; EMM386's I/O handler, in protected mode at ring 0 on EMM386's stack, with
; interrupts off: DX = port, CL = the direction (0 in, 4 or 8 out), AL =
; the byte, CS and DS ring 0 selectors of this segment as long as the
; resident part. CF set tells EMM386 to do the access itself
emm_shim:
        call    front
        retf
emm_pass:                               ; a port in the range but not the front's
        stc
        retf

; HDPMI32i's trap handler, in protected mode at ring 3, called with a
; 32-bit far frame on the game's stack: EDX = port, ECX = the direction (0
; in, else out), EAX = the byte, DS = this segment. The chip is reached
; through HDPMI's untrapped I/O, because the ports are trapped for this
; ring too. EBX, ESI and EBP are HDPMI's
hdpmi_shim:
        pushad
        mov     [save_ss],ss
        mov     [save_esp],esp
        mov     ss,[pm_data]
        mov     esp,stack_top
        mov     byte [how_io],IO_HDPMI
        call    front
        jnc     .done
        test    cl,cl                   ; not the front's: the access as it is
        jnz     .out
        call    chip_in
        jmp     short .done
.out:   call    chip_out
.done:  mov     [pm_result],al
        mov     byte [how_io],IO_DIRECT
        mov     ss,[save_ss]
        mov     esp,[save_esp]
        popad
        test    cl,cl
        jnz     .ret
        mov     al,[pm_result]
.ret:   o32 retf

; --- the chip's ports --------------------------------------------------------

; AL from port DX, and AL to port DX: directly, or through HDPMI32i's
; function 8 (CH = 1 out, 0 in, CL = the byte out, DX = the port) while its
; handler runs
chip_in:
        cmp     byte [how_io],IO_DIRECT
        jne     .pm
        in      al,dx
        ret
.pm:    push    cx
        push    ax                      ; AH kept, as in keeps it: callers
        xor     ch,ch                   ; carry a register number in it
        mov     ax,8
        o32 call far [vendor]
        pop     cx
        mov     ah,ch
        pop     cx
        ret
chip_out:
        cmp     byte [how_io],IO_DIRECT
        jne     .pm
        out     dx,al
        ret
.pm:    push    cx
        push    ax
        mov     cl,al
        mov     ch,1
        mov     ax,8
        o32 call far [vendor]
        pop     ax
        pop     cx
        ret

; AL = mixer register AH; keeps the chip's index
mix_read:
        push    cx
        push    dx
        mov     dx,[base]
        add     dx,4
        pushf
        cli
        call    chip_in
        mov     cl,al                   ; the index
        mov     al,ah
        call    chip_out
        inc     dx
        call    chip_in
        dec     dx
        mov     ch,al
        mov     al,cl
        call    chip_out
        mov     al,ch
        popf
        pop     dx
        pop     cx
        ret

; mixer register AH = AL; keeps the chip's index and AX
mix_write:
        push    ax
        push    cx
        push    dx
        mov     dx,[base]
        add     dx,4
        pushf
        cli
        mov     cx,ax                   ; CL = the value, CH = the register
        call    chip_in
        xchg    al,ch                   ; CH = the index
        call    chip_out
        inc     dx
        mov     al,cl
        call    chip_out
        dec     dx
        mov     al,ch
        call    chip_out
        popf
        pop     dx
        pop     cx
        pop     ax
        ret

; AL to the DSP's command port once it takes a byte, or after DSP_POLLS
; polls anyway
dsp_put:
        push    ax
        push    cx
        push    dx
        mov     dx,[base]
        add     dx,0x0C
        mov     cx,DSP_POLLS
.wait:  call    chip_in
        test    al,0x80
        jz      .ready
        loop    .wait
.ready: pop     dx
        pop     cx
        pop     ax
        push    dx
        mov     dx,[base]
        add     dx,0x0C
        call    chip_out
        pop     dx
        ret

; the chip's register AH = AL (Extended mode)
dsp_reg:
        push    ax
        mov     al,ah
        call    dsp_put
        pop     ax
        jmp     dsp_put

; AL = the chip's register AH (command C0h), with interrupts off so that
; nothing reads the answer first; 0 and CF set if none comes
dsp_read:
        push    cx
        push    dx
        pushf
        cli
        mov     dx,[base]
        add     dx,0x0C
        mov     cx,8                    ; a byte nobody read
.drain: call    chip_in
        test    al,0x40
        jz      .clean
        sub     dx,2
        call    chip_in
        add     dx,2
        loop    .drain
.clean: mov     al,0xC0
        call    dsp_put
        mov     al,ah
        call    dsp_put
        mov     cx,DSP_POLLS
.wait:  call    chip_in
        test    al,0x40
        jnz     .got
        loop    .wait
        popf
        pop     dx
        pop     cx
        xor     al,al
        stc
        ret
.got:   sub     dx,2
        call    chip_in
        popf
        pop     dx
        pop     cx
        clc
        ret

; The front keeps the chip's registers A0h-BFh that it wrote or read since
; the chip's last reset (ctrl, known), so that a transfer writes only what
; changes and reads each register once: a read waits for the chip's answer
; with interrupts off. A reset and a command of the chip's that may change
; them (passed) make them unknown again

; the chip's register AH = AL, unless the chip has it already; reg_force
; writes it in any case
reg_set:
        push    ebx
        movzx   ebx,ah
        sub     bx,0xA0
        bt      dword [known],ebx
        jnc     reg_force.keep
        cmp     [ctrl+bx],al
        jne     reg_force.keep
        pop     ebx
        ret
reg_force:
        push    ebx
        movzx   ebx,ah
        sub     bx,0xA0
.keep:  mov     [ctrl+bx],al
        bts     dword [known],ebx
        pop     ebx
        jmp     dsp_reg

; AL = the chip's register AH: as the front knows it, or read and then
; known; 0 if the chip doesn't answer
reg_get:
        push    ebx
        movzx   ebx,ah
        sub     bx,0xA0
        bt      dword [known],ebx
        jnc     .read
        mov     al,[ctrl+bx]
        pop     ebx
        ret
.read:  call    dsp_read
        jc      .none
        mov     [ctrl+bx],al
        bts     dword [known],ebx
.none:  pop     ebx
        ret

; a command of the chip's in AL goes to it: the front no longer knows the
; chip's registers, unless the command leaves them alone: pause and
; continue, the speaker, the tests, the version and identification, and
; the interrupt request
passed:
        cmp     al,0xD0
        je      .keep
        cmp     al,0xD1
        je      .keep
        cmp     al,0xD3
        je      .keep
        cmp     al,0xD4
        je      .keep
        cmp     al,0xD8
        je      .keep
        cmp     al,0xF2
        je      .keep
        cmp     al,0xE0
        jb      .forget
        cmp     al,0xE8
        jbe     .keep
.forget:
        mov     dword [known],0
.keep:  ret

; --- the front ---------------------------------------------------------------

; a trapped access: DX = port, CL = the direction (0 in, else out), AL =
; the byte written; AL = the byte read. CF set if the port isn't the
; front's. DS = this segment; the direction flag is the game's
front:
        cld
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        mov     bx,dx
        sub     bx,[base]
        cmp     bx,0x0F
        ja      .not
        test    cl,cl
        jnz     .out
        call    s16_in
        jmp     short .done
.out:   call    s16_out
.done:  pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
.not:   pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        stc
        ret

; AL written to Audio_Base+BX, DX = the port; CF set if not the front's
s16_out:
        cmp     bl,0x0C
        je      s16_write
        cmp     bl,0x05
        je      mix_out
        cmp     bl,0x04
        je      .index
        cmp     bl,0x06
        jne     .not
        test    al,1                    ; the reset's end: the chip is in
        jnz     .high                   ; Compatibility mode again, and
        test    byte [flags],F_RESET    ; the front starts over, for
        jz      .through                ; whatever program comes next
        mov     byte [flags],0
        mov     byte [need],0
        mov     byte [qcount],0
        mov     byte [pass],0
        mov     byte [tc],0
        mov     word [rate],0
        mov     dword [known],0
        jmp     short .through
.high:  or      byte [flags],F_RESET
.through:
        call    chip_out
        clc
        ret
.index: mov     [index],al              ; the chip gets the index too
        call    chip_out
        clc
        ret
.not:   stc
        ret

; AL = the game's read of Audio_Base+BX, DX = the port; CF set if not the
; front's
s16_in:
        cmp     bl,0x0A
        je      .data
        cmp     bl,0x0E
        je      .status
        cmp     bl,0x0F
        je      .ack16
        cmp     bl,0x0C
        je      .chip
        cmp     bl,0x05
        je      mix_in
        cmp     bl,0x04
        jne     .not
.chip:  call    chip_in                 ; the index, the write buffer's state
        clc
        ret
.data:  cmp     byte [qcount],0
        je      .chip
        push    bx                      ; the front's answer
        movzx   bx,byte [qhead]
        mov     al,[queue+bx]
        inc     bx
        cmp     bx,QUEUE_LEN
        jb      .head
        xor     bx,bx
.head:  mov     [qhead],bl
        dec     byte [qcount]
        pop     bx
        clc
        ret
.status:
        call    chip_in                 ; acknowledges Audio 1's interrupt,
        cmp     byte [qcount],0         ; as on the Sound Blaster 16
        je      .ok
        or      al,0x80                 ; a byte to read
.ok:    clc
        ret
.ack16: dec     dx                      ; the 16-bit acknowledge: Audio 1's
        call    chip_in                 ; interrupt, acknowledged at +Eh
        inc     dx
        clc
        ret
.not:   stc
        ret

; AL into the read queue; a full queue drops the byte
queue_put:
        push    bx
        movzx   bx,byte [qcount]
        cmp     bx,QUEUE_LEN
        jae     .full
        add     bl,[qhead]
        cmp     bx,QUEUE_LEN
        jb      .at
        sub     bx,QUEUE_LEN
.at:    mov     [queue+bx],al
        inc     byte [qcount]
.full:  pop     bx
        ret

; AL written to the command port: the Sound Blaster 16's commands are the
; front's; the chip's own go to it as they come, with their operands. C6h
; is both the Sound Blaster 16's 8-bit auto-initializing transfer and the
; chip's Extended mode: the byte after it tells, a transfer's mode (bits 4
; and 5 only) or a command
s16_write:
        cmp     byte [need],0
        jne     .operand
.cmd:   mov     [cmd],al
        mov     byte [got],0
        call    operands                ; CL = operands, CH = 1 for the chip's
        mov     [need],cl
        mov     [pass],ch
        or      ch,ch
        jz      .mine
        call    passed
        call    chip_out                ; as the game wrote it
.mine:  cmp     byte [need],0
        jne     .ok
        call    command
.ok:    clc
        ret
.operand:
        cmp     byte [cmd],0xC6
        jne     .keep
        cmp     byte [got],0
        jne     .keep
        cmp     byte [pass],0
        jne     .keep
        test    al,0xCF
        jz      .keep
        push    ax                      ; the game knows the chip: its
        mov     al,0xC6                 ; Extended mode on, and this byte a
        call    chip_out                ; command
        pop     ax
        or      byte [flags],F_EXT | F_ESS
        mov     byte [need],0
        jmp     short .cmd
.keep:  movzx   bx,byte [got]
        cmp     bx,3
        jae     .more
        mov     [ops+bx],al
.more:  inc     byte [got]
        cmp     byte [pass],0
        je      .kept
        call    chip_out
.kept:  dec     byte [need]
        jnz     .ok
        call    command
        clc
        ret

; CX for the command in AL: CL = its operands, CH = 1 when it is the
; chip's (its bytes go to the chip), 0 for the front's
operands:
        test    byte [flags],F_ESS
        jnz     .chip
        cmp     al,0xB0                 ; the Sound Blaster 16's own
        jb      .nox
        cmp     al,0xCF
        ja      .nox
        test    al,0x01                 ; an odd one is no transfer: C7h
        jnz     .chip                   ; and the chip's registers
        mov     cx,3                    ; a transfer: its mode and length
        ret
.nox:   xor     cx,cx
        cmp     al,0x41                 ; the rates
        je      .two
        cmp     al,0x42
        je      .two
        cmp     al,0xE1                 ; the version
        je      .out
        cmp     al,0xE3                 ; the copyright
        je      .out
        cmp     al,0xD5                 ; 16-bit pause, continue and stop
        je      .out
        cmp     al,0xD6
        je      .out
        cmp     al,0xD9
        je      .out
        cmp     al,0xF3                 ; the 16-bit interrupt
        je      .out
        cmp     al,0xDA                 ; the 8-bit stop, of a transfer of
        jne     .chip                   ; the front's
        test    byte [flags],F_RUN
        jnz     .out
.chip:  mov     ch,1
        mov     cl,2
        cmp     al,0x14
        je      .out
        cmp     al,0x15
        je      .out
        cmp     al,0x16
        je      .out
        cmp     al,0x17
        je      .out
        cmp     al,0x24
        je      .out
        cmp     al,0x25
        je      .out
        cmp     al,0x48
        je      .out
        cmp     al,0x80
        je      .out
        cmp     al,0x11
        je      .out
        cmp     al,0x64
        jb      .one
        cmp     al,0x6F
        jbe     .out                    ; ESPCM transfers
        cmp     al,0x74
        jb      .one
        cmp     al,0x77
        jbe     .out                    ; ADPCM transfers
.one:   mov     cl,1
        cmp     al,0x10
        je      .out
        cmp     al,0x38
        je      .out
        cmp     al,0x40
        je      .out
        cmp     al,0xE0
        je      .out
        cmp     al,0xE2
        je      .out
        cmp     al,0xE4
        je      .out
        cmp     al,0xDD
        je      .out
        test    byte [flags],F_ESS
        jz      .ext
        cmp     al,0x41                 ; the chip's own time constants
        je      .out
        cmp     al,0x42
        je      .out
        cmp     al,0xCF
        je      .out
.ext:   test    byte [flags],F_EXT
        jz      .none
        cmp     al,0xC0                 ; a register read, and the writes
        je      .out
        cmp     al,0xA0
        jb      .none
        cmp     al,0xBF
        jbe     .out
.none:  mov     cl,0
.out:   ret
.two:   mov     cl,2
        ret

; the command in cmd with its operands: the chip has the chip's own
; already, the front acts on its own and on what the chip's mean to it
command:
        push    ax
        push    cx
        movzx   ax,byte [cmd]
        cmp     al,0xC7                 ; the chip's Extended mode off: a
        je      .c7                     ; program that knows the chip
        test    byte [flags],F_ESS
        jnz     .chipcmd
        cmp     al,0xB0
        jb      .notx
        cmp     al,0xCF
        ja      .notx
        test    al,0x01
        jnz     .out
        call    transfer
        jmp     .out
.notx:  cmp     al,0x41
        je      .rate
        cmp     al,0x42
        je      .rate
        cmp     al,0x40
        je      .tc
        cmp     al,0xE1
        je      .version
        cmp     al,0xE3
        je      .copyright
        cmp     al,0xD5
        je      .pause
        cmp     al,0xD6
        je      .continue
        cmp     al,0xD9
        je      .stop
        cmp     al,0xDA
        je      .stop
        cmp     al,0xF3
        je      .irq16
        cmp     al,0xF2
        je      .irq8
        cmp     al,0xE7
        je      .ident
        cmp     al,0x10                 ; the chip's 8-bit transfers, for
        jb      .out                    ; mixer 82h: 1xh, 2xh, 6xh, 7xh, 9xh
        cmp     al,0x2F
        jbe     .sb8
        cmp     al,0x64
        jb      .out
        cmp     al,0x7F
        jbe     .sb8
        cmp     al,0x90
        jb      .out
        cmp     al,0x9F
        ja      .out
.sb8:   and     byte [flags],~(F_16 | F_RUN | F_AUTO) & 0xFF
        or      byte [flags],F_8
        jmp     .out
.chipcmd:
        cmp     al,0xC6                 ; the chip's Extended mode on
        jne     .out
        or      byte [flags],F_EXT
        jmp     .out
.c7:    and     byte [flags],~F_EXT & 0xFF
        or      byte [flags],F_ESS
        jmp     .out
.rate:  mov     ah,[ops]                ; high byte first
        mov     al,[ops+1]
        mov     [rate],ax
        jmp     .out
.tc:    mov     al,[ops]
        mov     [tc],al
        mov     word [rate],0
        jmp     .out
.version:
        mov     al,VERSION >> 8
        call    queue_put
        mov     al,VERSION & 0xFF
        call    queue_put
        jmp     .out
.copyright:
        push    si
        mov     si,copyright
.str:   lodsb
        call    queue_put
        or      al,al
        jnz     .str
        pop     si
        jmp     .out
.pause: mov     al,0xD0                 ; Audio 1 halts, as for 8-bit
        call    dsp_put
        jmp     .out
.continue:
        mov     al,0xD4
        call    dsp_put
        jmp     .out
.stop:  test    byte [flags],F_RUN      ; the block that plays is the last:
        jz      .out                    ; auto-initialize off
        and     byte [flags],~F_AUTO & 0xFF
        mov     ah,0xB8
        call    reg_get
        and     al,~0x04 & 0xFF
        mov     ah,0xB8
        call    reg_force
        jmp     .out
.irq16: mov     al,0xF2                 ; the 16-bit interrupt: the chip's,
        call    dsp_put                 ; Audio 1's
        and     byte [flags],~F_8 & 0xFF
        or      byte [flags],F_16
        jmp     .out
.irq8:  and     byte [flags],~F_16 & 0xFF
        or      byte [flags],F_8
        jmp     .out
.ident: or      byte [flags],F_ESS
.out:   pop     cx
        pop     ax
        ret
copyright:
        db "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.", 0

; a transfer command of the Sound Blaster 16 (Bxh 16-bit, Cxh 8-bit; bit 3
; records, bit 2 auto-initializes), its mode (bit 4 signed, bit 5 stereo)
; and its length in samples less one: Audio 1 in Extended mode, two bytes
; a sample for 16-bit; a 16-bit transfer on the game's 16-bit channel has
; that channel's programming carried over first
transfer:
        pushad
        movzx   bx,byte [cmd]           ; BL = the command
        movzx   cx,byte [ops]           ; CL = the mode
        movzx   esi,word [ops+1]        ; ESI = the samples
        inc     esi
        test    bl,0x10
        jz      .a1
        cmp     byte [dma16],0
        je      .a1
        call    dma_mirror
.a1:    call    a1_transfer
        popad
        ret

; BX = the rate of the transfer (Hz): the rate of 41h/42h, the time
; constant of 40h (1 MHz / (256 - TC), the rate of each channel), or
; 22050 Hz after a reset; clamped to 4000-48000
rate_hz:
        push    eax
        push    edx
        movzx   ebx,word [rate]
        or      ebx,ebx
        jnz     .clamp
        mov     ebx,22050
        movzx   eax,byte [tc]
        or      eax,eax
        jz      .clamp
        neg     eax
        add     eax,256
        mov     ebx,eax
        mov     eax,1000000
        xor     edx,edx
        div     ebx
        mov     ebx,eax
.clamp: cmp     ebx,4000
        jae     .min
        mov     ebx,4000
.min:   cmp     ebx,48000
        jbe     .max
        mov     ebx,48000
.max:   pop     edx
        pop     eax
        ret

; EAX = round(EAX / EBX)
div_round:
        push    edx
        xor     edx,edx
        div     ebx
        shl     edx,1
        cmp     edx,ebx
        jb      .done
        inc     eax
.done:  pop     edx
        ret

; --- the game's 16-bit channel ---------------------------------------------

; the page registers of the first controller's channels 0 to 3, and of the
; second's channels 5 to 7
page8:          db 0x87, 0x83, 0x81, 0x82
page16:         db 0x8B, 0x89, 0x8A

; the game's programming of its 16-bit channel, in the second controller's
; registers, carried over to Audio 1's 8-bit channel: the word address and
; the page's bits 7:1 as a byte address and page, the word count as a byte
; count, demand transfers (single with /S), auto-initialize and the
; direction from the command in BL. The chip takes a byte per DMA cycle
; whatever the channel, so the game's channel can't carry its words. The
; game's current registers are read right after it programmed them, so
; they hold what it wrote, and its flip-flop is left cleared
dma_mirror:
        pushad
        mov     dx,0xD8                 ; the second controller's flip-flop
        call    chip_out
        movzx   si,byte [dma16]
        mov     dl,[page16+si-5]
        xor     dh,dh
        call    chip_in
        and     al,0xFE
        mov     bh,al                   ; BH = the page, bits 7:1
        and     si,3
        shl     si,2
        lea     dx,[si+0xC0]            ; the channel's address, low then high
        call    chip_in
        mov     cl,al
        call    chip_in
        mov     ch,al                   ; CX = the word address
        add     dx,2                    ; its count
        call    chip_in
        movzx   bp,al
        call    chip_in
        mov     ah,al
        xor     al,al
        or      bp,ax                   ; BP = the word count
        mov     dx,0xD8
        call    chip_out
        mov     ax,cx
        shl     ax,1                    ; AX = the byte address, its bit 16
        adc     bh,0                    ; into the page
        shl     bp,1
        inc     bp                      ; BP = the byte count
        movzx   si,byte [dma8]
        push    ax
        mov     ax,si
        or      al,0x04                 ; the channel masked while it is set
        mov     dx,0x0A
        call    chip_out
        mov     al,0x08                 ; demand transfers from memory
        test    bl,0x08
        jz      .play
        mov     al,0x04                 ; into memory: a recording
.play:  test    bl,0x04
        jz      .once
        or      al,0x10                 ; auto-initialize
.once:  cmp     byte [single],0
        je      .mode
        or      al,0x40                 ; single transfers
.mode:  or      al,[dma8]
        mov     dx,0x0B
        call    chip_out
        mov     dx,0x0C                 ; the first controller's flip-flop
        call    chip_out
        pop     ax
        mov     dx,si
        shl     dx,1                    ; the channel's address port
        call    chip_out
        mov     al,ah
        call    chip_out
        inc     dx                      ; its count port
        mov     ax,bp
        call    chip_out
        mov     al,ah
        call    chip_out
        mov     dl,[page8+si]
        xor     dh,dh
        mov     al,bh
        call    chip_out
        mov     ax,si                   ; unmasked
        mov     dx,0x0A
        call    chip_out
        popad
        ret

; --- Audio 1: the game's transfers, in Extended mode ---------------------

; a transfer on Audio 1: BL = the command, CL = the mode, ESI = the
; samples. The sequence of the data sheet (p.48), as the VxD front sends
; it: C6h once after a reset, B8h for the direction and auto-initialize,
; B9h for demand transfers of 2 bytes, A8h for mono or stereo, A1h and A2h
; for the rate, A4h and A5h for minus the bytes, B6h and B7h for the
; format, B1h and B2h for the interrupt and the DMA request, B8h bit 0 and
; D1h. Only the registers that change are written, and the ones whose
; other bits stay are read once after a reset, so a transfer like the
; last costs B8h twice and D1h
a1_transfer:
        mov     al,[flags]
        and     al,F_ESS | F_EXT | F_RESET
        or      al,F_RUN | F_8
        test    bl,0x10
        jz      .bits                   ; Cxh: 8-bit
        shl     esi,1                   ; two bytes a sample
        xor     al,F_8 | F_16
.bits:  test    bl,0x04
        jz      .flags
        or      al,F_AUTO
.flags: mov     [flags],al
        test    al,F_EXT
        jnz     .ext
        mov     al,0xC6
        call    dsp_put
        or      byte [flags],F_EXT
.ext:   mov     ah,0xB8
        call    reg_get
        and     al,0x30                 ; bits 5:4 kept, as ES1869.DRV keeps them
        test    bl,0x04
        jz      .once
        or      al,0x04
.once:  test    bl,0x08
        jz      .dac
        or      al,0x0A                 ; the ADC
.dac:   mov     ah,0xB8
        call    reg_force
        mov     ax,0xB902               ; demand transfers of 2 bytes, as
        cmp     byte [single],0         ; ESS's driver writes them, or
        je      .b9                     ; single ones (01h) with /S, ESS's
        mov     al,0x01                 ; "Single Mode DMA"
.b9:    call    reg_set
        mov     ah,0xA8                 ; mono or stereo: A8h bits 1:0
        call    reg_get
        and     al,0xFC
        or      al,0x02
        test    cl,0x20
        jz      .mono
        xor     al,0x03
.mono:  mov     ah,0xA8
        call    reg_set
        call    a1_clocks               ; A1h and A2h for the rate
        mov     eax,esi                 ; an interrupt every ESI bytes: A4h
        neg     eax                     ; and A5h, two's complement
        mov     ah,0xA4
        call    reg_set
        mov     eax,esi
        neg     eax
        shr     eax,8
        mov     ah,0xA5
        call    reg_set
        mov     dl,0xD0                 ; the format in DL: 8-bit mono
        test    cl,0x10                 ; unsigned
        jz      .size
        xor     dl,0x20                 ; signed: F0h
.size:  test    bl,0x10
        jz      .chans
        xor     dl,0x04                 ; 16-bit: D4h, F4h
.chans: test    cl,0x20
        jz      .fmt
        xor     dl,0x48                 ; stereo: 98h, B8h, 9Ch, BCh
.fmt:   mov     dh,0x80                 ; and B6h in DH: 80h unsigned, 00h
        test    cl,0x10                 ; signed
        jz      .b6
        mov     dh,0x00
.b6:    call    a1_format
        mov     ah,0xB1                 ; its interrupt and DMA request on:
        call    reg_get                 ; bits 6 and 4, 7 and 5 clear
        and     al,0x0F
        or      al,0x50
        mov     ah,0xB1
        call    reg_set
        mov     ah,0xB2
        call    reg_get
        and     al,0x0F
        or      al,0x50
        mov     ah,0xB2
        call    reg_set
        mov     ah,0xB8                 ; go, and the DAC into the mixer
        call    reg_get
        or      al,0x01
        mov     ah,0xB8
        call    reg_force
        test    bl,0x08
        jnz     .done
        mov     al,0xD1
        call    dsp_put
.done:  ret

; the format, B6h = DH and B7h = DL: B6h, then B7h twice (DS p.49, table
; 16), first 51h or 71h, unless the chip has the format
a1_format:
        bt      dword [known],0xB6 - 0xA0
        jnc     .write
        bt      dword [known],0xB7 - 0xA0
        jnc     .write
        cmp     [ctrl+0xB6-0xA0],dh
        jne     .write
        cmp     [ctrl+0xB7-0xA0],dl
        je      .out
.write: mov     al,dh
        mov     ah,0xB6
        call    reg_force
        mov     ax,0xB771
        or      dh,dh
        jz      .first
        mov     al,0x51
.first: call    dsp_reg
        mov     al,dl
        mov     ah,0xB7
        call    reg_force
.out:   ret

; A1h and A2h for the rate, as ES1869.DRV computes them (6:1FF4): the
; 795.5 kHz clock above 22050 Hz, the 397.7 kHz one up to it, and the
; filter clock from its tables between 22 and 44 kHz, else 7.16 MHz over
; 35.9 times the rate; CL bit 5 = stereo
a1_clocks:
        pushad
        call    rate_hz                 ; EBX = the rate
        mov     eax,397700
        mov     esi,128
        cmp     ebx,22050
        jbe     .clock
        mov     eax,795500
        mov     esi,256
.clock: call    div_round               ; EAX = the divisor
        mov     ebp,eax
        sub     esi,eax
        mov     eax,esi
        mov     ah,0xA1
        call    reg_set
        cmp     ebx,22050
        jbe     .general
        cmp     ebp,18
        jb      .general
        cmp     ebp,35
        ja      .general
        mov     si,filter_stereo
        test    cl,0x20
        jnz     .table
        mov     si,filter_mono
.table: mov     eax,ebp
        sub     eax,18
        add     si,ax
        mov     al,[si]
        jmp     short .a2
.general:
        mov     eax,199582
        call    div_round
.a2:    neg     al
        mov     ah,0xA2
        call    reg_set
        popad
        ret

; ES1869.DRV's filter dividers for the 795.5 kHz clock's divisors 18 to 35
filter_stereo:  db 3, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9
filter_mono:    db 3, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10

; --- the mixer -------------------------------------------------------------

; AL written to mixer register index, DX = the data port: the Sound
; Blaster 16's registers 30h-47h keep the game's view and give the chip
; the volumes they stand for; the chip's own get it as it comes. A reset is
; the Sound Blaster 16's: its levels go to the chip, and the chip's other
; registers stay (the 3-D effect, the DACs' mode, the music DAC's source,
; Audio 2), which the chip's own reset would put back to their power-on
; values
mix_out:
        push    ax
        push    cx
        mov     cl,[index]
        test    byte [flags],F_ESS      ; the chip's own mixer for a game
        jnz     .chip                   ; that knows the chip
        or      cl,cl
        jz      .reset
        cmp     cl,0x30
        jb      .chip
        cmp     cl,0x47
        ja      .high
        call    mix_fresh
        push    bx
        movzx   bx,cl
        mov     [mix+bx-0x30],al
        pop     bx
        call    mix_apply
        jmp     .out
.high:  cmp     cl,0x80
        jb      .chip
        cmp     cl,0x81                 ; the 16-bit channel the game picks
        je      .select
        cmp     cl,0x82                 ; the card's IRQ and status: kept
        jbe     .out
.chip:  call    chip_out                ; the chip's own registers, and the
        call    mix_stale               ; view follows the levels they change
        jmp     .out
.select:
        call    dma_select
        jmp     .out
.reset: call    mix_defaults
        call    mix_all
        mov     ah,0x0E                 ; and the Sound Blaster Pro's stereo
        call    mix_read                ; off, as after the chip's reset
        and     al,~0x02 & 0xFF
        call    mix_write
.out:   pop     cx
        pop     ax
        clc
        ret

; AL = the game's read of mixer register index, DX = the data port
mix_in:
        push    bx
        push    cx
        test    byte [flags],F_ESS
        jnz     .chip
        mov     cl,[index]
        cmp     cl,0x30
        jb      .chip
        cmp     cl,0x47
        ja      .high
        call    mix_fresh
        movzx   bx,cl
        mov     al,[mix+bx-0x30]
        jmp     short .out
.high:  cmp     cl,0x80
        je      .irq
        cmp     cl,0x81
        je      .dma
        cmp     cl,0x82
        je      .status
.chip:  call    chip_in
        jmp     short .out
.irq:   call    irq_bits
        jmp     short .out
.dma:   call    dma_bits
        jmp     short .out
.status:                                ; bit 0 an 8-bit transfer's
        xor     al,al                   ; interrupt, bit 1 a 16-bit one:
        test    byte [flags],F_8        ; the running transfer's kind, as
        jz      .st16                   ; the VxD's front answers
        or      al,0x01
.st16:  test    byte [flags],F_16
        jz      .out
        or      al,0x02
.out:   pop     cx
        pop     bx
        clc
        ret

; a write of the chip's mixer register CL: the view's levels it changes are
; stale until the game uses the Sound Blaster 16's registers (mix_fresh),
; so that a game's accesses reach the chip as they come and the view
; follows what another program set. The Sound Blaster Pro's registers are
; translated by the chip into its own (04h voice, 22h master, 26h MIDI, 28h
; CD, 2Eh line, 0Ah mic, 0Ch the record source); the chip's own are 32h
; (the master, DS p.54), 60h and 62h, the extended ones of the aliases, 3Ch
; and 1Ch, and its reset (00h, for a game that knows the chip) changes them
; all
mix_stale:
        push    ax
        push    si
        mov     al,0xFF
        or      cl,cl
        jz      .set
        mov     al,0x01                 ; the master
        cmp     cl,0x22
        je      .set
        cmp     cl,0x32
        je      .set
        cmp     cl,0x60
        je      .set
        cmp     cl,0x62
        je      .set
        mov     si,mix_aliases
.find:  shl     al,1
        cmp     byte [si],0
        je      .other
        cmp     [si],cl
        je      .set
        cmp     [si+1],cl
        je      .set
        add     si,3
        jmp     short .find
.other: cmp     cl,0x3C                 ; AL = 40h: the PC speaker
        je      .set
        shl     al,1                    ; 80h: the record source
        cmp     cl,0x0C
        je      .set
        cmp     cl,0x1C
        jne     .out
.set:   or      [stale],al
.out:   pop     si
        pop     ax
        ret

; the view's stale levels from the chip, before the game reads or writes
; the Sound Blaster 16's registers; the output switches gate them again at
; the next write. The record source becomes the input switches that pick
; it: the mic, CD or line alone, the record mixer as the reset's several,
; else none, so that writing them back keeps it
mix_fresh:
        cmp     byte [stale],0
        jne     .fresh
        ret
.fresh: pusha
        mov     bl,[stale]
        mov     byte [stale],0
        shr     bl,1
        jnc     .pairs
        call    mix_master
.pairs: mov     si,mix_aliases
.next:  cmp     byte [si],0
        je      .speaker
        shr     bl,1
        jnc     .skip
        mov     ah,[si+1]
        movzx   cx,byte [si+2]
        call    mix_pair
.skip:  add     si,3
        jmp     short .next
.speaker:
        shr     bl,1
        jnc     .source
        mov     ah,0x3C                 ; the PC speaker, 3 bits
        call    mix_read
        and     al,0x07
        shl     al,5
        mov     [mix+0x0B],al
.source:
        shr     bl,1
        jnc     .done
        mov     ah,0x1C
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
.src:   mov     [mix+0x0D],ch
        mov     [mix+0x0E],cl
.done:  popa
        ret

; the Sound Blaster Pro's registers, the chip's they go to, the view's
; offset of the left level
mix_aliases:    db 0x04, 0x14, 2, 0x26, 0x36, 4, 0x28, 0x38, 6, 0x2E, 0x3E, 8
                db 0x0A, 0x1A, 10, 0

; the view's L and R at mix+CX from the nibbles of the chip's register AH
; (mix+10, the mono mic, from 1Ah's left only); clobbers AX
mix_pair:
        push    bx
        mov     bx,cx
        call    mix_read
        mov     ah,al
        and     al,0xF0
        mov     [mix+bx],al
        cmp     bx,10
        je      .out
        shl     ah,4
        mov     [mix+bx+1],ah
.out:   pop     bx
        ret

; the view's master from the chip's 60h and 62h, 6 bits to the 8 of 30h
; and 31h, 0 when muted; clobbers AX
mix_master:
        mov     ah,0x60
        call    .one
        mov     [mix+0],al
        mov     ah,0x62
        call    .one
        mov     [mix+1],al
        ret
.one:   call    mix_read
        test    al,0x40
        jz      .on
        xor     al,al
.on:    and     al,0x3F
        shl     al,2
        ret

; the view's levels and record source to the chip
mix_all:
        push    cx
        push    si
        mov     si,mix_all_regs
.next:  mov     cl,[si]
        or      cl,cl
        jz      .done
        call    mix_apply
        inc     si
        jmp     short .next
.done:  pop     si
        pop     cx
        ret
mix_all_regs:   db 0x30, 0x31, 0x32, 0x34, 0x3B, 0x3C, 0x3D, 0

; AL = mixer 80h: bit 0 IRQ 2, 1 IRQ 5, 2 IRQ 7, 3 IRQ 10
irq_bits:
        mov     al,[irq]
        cmp     al,2
        je      .b0
        cmp     al,9                    ; 9 is 2's
        je      .b0
        cmp     al,5
        je      .b1
        cmp     al,7
        je      .b2
        cmp     al,10
        je      .b3
        xor     al,al
        ret
.b0:    mov     al,1
        ret
.b1:    mov     al,2
        ret
.b2:    mov     al,4
        ret
.b3:    mov     al,8
        ret

; AL = mixer 81h: bits 0, 1 and 3 for the 8-bit channel, 5 to 7 for the
; 16-bit one
dma_bits:
        push    cx
        mov     cl,[dma8]
        mov     al,1
        shl     al,cl
        mov     cl,[dma16]
        or      cl,cl
        jz      .out
        mov     ah,1
        shl     ah,cl
        or      al,ah
.out:   pop     cx
        ret

; the 16-bit channel from a write to mixer 81h: bit 5 channel 5, 6 and 7
; theirs, none 0, as the Sound Blaster 16's DSP picks the channel of a
; 16-bit transfer; the 8-bit channel's bits can't change the card's
dma_select:
        push    ax
        xor     ah,ah
        test    al,0x20
        jz      .b6
        mov     ah,5
.b6:    test    al,0x40
        jz      .b7
        mov     ah,6
.b7:    test    al,0x80
        jz      .set
        mov     ah,7
.set:   mov     [dma16],ah
        pop     ax
        ret

; the Sound Blaster 16's reset values into the view
mix_defaults:
        mov     byte [stale],0
        push    cx
        push    si
        push    di
        push    es
        push    ds
        pop     es
        mov     si,mix_default
        mov     di,mix
        mov     cx,MIX_LEN
        cld
        rep     movsb
        pop     es
        pop     di
        pop     si
        pop     cx
        ret
mix_default:
        db 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0   ; master, voice, MIDI
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   ; CD, line, mic, PC speaker
        db 0x1F, 0x15, 0x0B, 0x00, 0x00, 0x00   ; the switches, the gains
        db 0x00, 0x00, 0x80, 0x80, 0x80, 0x80   ; AGC, treble, bass

; the chip's volume for the Sound Blaster 16's register CL, from the
; game's view, the VxD front's mapping: master left and right to 60h and
; 62h, voice to 14h, MIDI to 36h, CD to 38h, line to 3Eh, the mic to 1Ah,
; the PC speaker to 3Ch; the output switches gate CD, line and the mic, and
; the input switches pick the record source of 1Ch
mix_apply:
        push    ax
        push    cx
        cmp     cl,0x30
        je      .left
        cmp     cl,0x31
        je      .right
        cmp     cl,0x33
        jbe     .voice
        cmp     cl,0x35
        jbe     .fm
        cmp     cl,0x37
        jbe     .cd
        cmp     cl,0x39
        jbe     .line
        cmp     cl,0x3A
        je      .mic
        cmp     cl,0x3B
        je      .speaker
        cmp     cl,0x3C
        je      .switches
        cmp     cl,0x3E
        jbe     .input
        jmp     .out                    ; the gains, AGC and tone: kept only
.left:  mov     al,[mix+0]              ; 8 bits to the 6 of 60h and 62h
        mov     ah,0x60
        jmp     short .master
.right: mov     al,[mix+1]
        mov     ah,0x62
.master:
        shr     al,2
        call    mix_write
        jmp     .out
.voice: mov     al,[mix+2]              ; the Audio 1 volume 14h
        mov     ah,[mix+3]
        call    nibbles
        mov     ah,0x14
        call    mix_write
        jmp     .out
.fm:    mov     al,[mix+4]              ; the music DAC (FM) volume 36h
        mov     ah,[mix+5]
        call    nibbles
        mov     ah,0x36
        call    mix_write
        jmp     .out
.switches:
        call    .cd_
        call    .line_
        call    .mic_
        jmp     .out
.cd:    call    .cd_
        jmp     .out
.line:  call    .line_
        jmp     .out
.mic:   call    .mic_
        jmp     .out
.speaker:
        mov     al,[mix+0x0B]           ; the PC speaker 3Ch, 3 bits
        shr     al,5
        mov     ah,0x3C
        call    mix_write
        jmp     .out
.input: mov     ah,[mix+0x0D]           ; the record source 1Ch, bits 2:0,
        or      ah,[mix+0x0E]           ; from the input switches of both
        xor     cx,cx                   ; channels: CL = a source alone, CH
        test    ah,0x01                 ; = how many; the mic
        jz      .nomic
        inc     ch
.nomic: test    ah,0x06                 ; AuxA (CD)
        jz      .nocd
        mov     cl,2
        inc     ch
.nocd:  test    ah,0x18                 ; line
        jz      .noline
        mov     cl,6
        inc     ch
.noline:
        test    ah,0x60                 ; the music: the record mixer
        jz      .count
        mov     cl,5
        inc     ch
.count: cmp     ch,1
        jb      .out                    ; nothing: as it is
        je      .src
        mov     cl,5                    ; several: the record mixer
.src:   mov     ah,0x1C
        call    mix_read
        and     al,0xF8
        or      al,cl
        mov     ah,0x1C
        call    mix_write
.out:   pop     cx
        pop     ax
        ret
.cd_:   mov     al,[mix+6]              ; AuxA (CD) 38h, through the
        mov     ah,[mix+7]              ; switches' bits 2 and 1
        test    byte [mix+0x0C],0x04
        jnz     .cdl
        xor     al,al
.cdl:   test    byte [mix+0x0C],0x02
        jnz     .cdr
        xor     ah,ah
.cdr:   call    nibbles
        mov     ah,0x38
        jmp     mix_write
.line_: mov     al,[mix+8]              ; line 3Eh, through bits 4 and 3
        mov     ah,[mix+9]
        test    byte [mix+0x0C],0x10
        jnz     .linel
        xor     al,al
.linel: test    byte [mix+0x0C],0x08
        jnz     .liner
        xor     ah,ah
.liner: call    nibbles
        mov     ah,0x3E
        jmp     mix_write
.mic_:  mov     al,[mix+0x0A]           ; the mono mic 1Ah, through bit 0
        test    byte [mix+0x0C],0x01
        jnz     .micon
        xor     al,al
.micon: mov     ah,al
        call    nibbles
        mov     ah,0x1A
        jmp     mix_write

; AL = the chip's two 4-bit volumes for the 8-bit left AL and right AH
nibbles:
        and     al,0xF0
        shr     ah,4
        or      al,ah
        ret

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
; 16-bit channel, /A220 the base) and BLASTER's A, I, D and H; CF set
; for /? or an unknown option
parse_options:
        call    blaster
        mov     si,0x81
        movzx   cx,byte [0x80]
.next:  jcxz    .done
        lodsb
        dec     cx
        cmp     al,'/'
        je      .option
        cmp     al,'-'
        jne     .next
.option:
        jcxz    .bad
        lodsb
        dec     cx
        and     al,~0x20                ; upper case
        cmp     al,'S'
        je      .single
        cmp     al,'H'
        je      .h
        cmp     al,'A'
        je      .a
        jmp     short .bad
.single:
        mov     byte [single],1
        jmp     short .next
.h:     jcxz    .bad
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
        jmp     short .next
.a:     call    hex_word                ; the base, 3 hex digits
        jc      .bad
        mov     [base],ax
        jmp     short .next
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
.h:     call    env_dec
        mov     [dma16],al
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
        call    dsp_byte
        jc      .no
        cmp     al,0x68
        jne     .no
        call    dsp_byte
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
dsp_byte:
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

; AL = PnP register BL of logical device 1, through the config port
pnp_read:
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
        in      al,dx
        ret

; the card's IRQ and 8-bit channel from its PnP registers, where the
; config port was found, else BLASTER's; then the mixer's view
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
        jae     .view
        mov     [dma8],al
.view:  call    mix_defaults            ; the view as a Sound Blaster 16 shows
        mov     byte [stale],0xFF       ; the levels it has: the chip's, the
        jmp     mix_fresh               ; rest as after a reset

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
        cmp     byte [dma16],0
        je      .t
        mov     dx,msg_h
        call    print
        movzx   ax,byte [dma16]
        call    print_dec
.t:     mov     dx,msg_t
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
        push    cs
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
        pop     es
        clc
        ret
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
        mov     edx,msg_hdpmi
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
usage:          db "usage: esssb16 [/A220] [/H5|/H6|/H7|/H0] [/S]", 13, 10
                db "  /A  the card's base, else BLASTER's A (220)", 13, 10
                db "  /H  the game's 16-bit DMA channel, else BLASTER's H (5); 0: none", 13, 10
                db "  /S  single DMA transfers (ESS's Single Mode DMA)", 13, 10
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
