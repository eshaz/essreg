; The Audio 1 player of ES1869.DRV: its interrupt service and the queue of
; wave headers, assembled when ES1869_FIX=1 and appended to segment 1
; (fixed code, run at interrupt time).
;
; Notes:
;
; ESS's driver plays on Audio 2 and records on Audio 1 (docs/AUDIO1.md).
; The player here plays a second wave-out stream through the Audio 1 DAC,
; for device 1 (Audio1Device) and for a device 0 open that finds Audio 2
; busy (SharedWaveOut).  a1wave.asm has the wave messages and programs the
; chip at task time; this file runs at interrupt time and only moves data:
; - the DMA plays a ring of two blocks in the Audio 1 DMA buffer, and the
;   chip interrupts once a block
; - each interrupt reads the DMA position, gives back the headers the DMA
;   played past (WOM_DONE) and copies what's queued into the free part of
;   the ring
; - without data it writes silence there and the DMA keeps running: a
;   stop takes DSP commands, which ESS's driver never sends at interrupt
;   time, and neither does this
; - when the DMA gets to where the data ends, the next data goes a guard
;   ahead of it, and the gap is left out of the position (A1_GAPEND)
;
; Positions are byte counts of the stream: PPOS what the DMA took, WPOS
; what was written, gaps too, DPOS the program's bytes.  A header's end,
; the low word of WPOS, goes in its reserved word, so the ring stays under
; 32 KB.  The first words of the player's block are laid out like ESS's
; wave instance, so ESS's callback (1:0010) and its WOM_DONE (1:0048)
; serve the player too.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; a1_isr(dev), far pascal: isr_srv_table[0], the Audio 1 interrupt while
; the player is its user (1); ESS's handler calls it with interrupts on and
; acknowledges the interrupt afterwards
a1_isr:
        push    bp
        mov     bp,sp
        push    si
        push    di
        mov     si,[bp+6]
        test    byte [si+A1_STATE],A1F_RUN
        jz      .done
        call    a1_advance
        call    a1_gap
        call    a1_retire
        call    a1_fill
.done:  pop     di
        pop     si
        pop     bp
        retf    2

; the DMA's offset in the ring, frame aligned, to AX; SI = dev
a1_offset:
        push    word [si+DEV_DEVNODE+2]
        push    word [si+DEV_DEVNODE]
        xor     ax,ax                   ; Audio 1
        push    ax
        push    cs
        call    vxd_dma_pos             ; the 8237's count: bytes left - 1
        inc     ax
        mov     dx,[si+A1_RING]
        sub     dx,ax
        jbe     .start                  ; a full pass left, or no answer
        cmp     dx,[si+A1_RING]
        jae     .start                  ; the pass just ended
        mov     ax,[si+A1_ALIGN]
        dec     ax
        not     ax
        and     ax,dx
        ret
.start: xor     ax,ax
        ret

; PPOS from the DMA; SI = dev
a1_advance:
        call    a1_offset
        mov     dx,ax
        sub     ax,[si+A1_PRING]
        jae     .fwd
        add     ax,[si+A1_RING]         ; it wrapped
.fwd:   mov     [si+A1_PRING],dx
        add     [si+A1_PPOS],ax
        adc     word [si+A1_PPOS+2],0
        ret

; the DMA got to where the data ends, or near it: the next data goes the
; guard ahead of it, over silence; SI = dev
a1_gap:
        mov     ax,[si+A1_PPOS]
        mov     dx,[si+A1_PPOS+2]
        add     ax,[si+A1_GUARD]
        adc     dx,0
        mov     cx,[si+A1_WPOS]
        mov     bx,[si+A1_WPOS+2]
        sub     cx,ax
        sbb     bx,dx
        jns     .ok                     ; WPOS leads by the guard
        mov     [si+A1_GAPEND],ax
        mov     [si+A1_GAPEND+2],dx
        ; silence from where the data ends, or from the DMA if it went
        ; past that, to the new WPOS: a1_fill wrote it there already,
        ; unless an interrupt came very late
        neg     cx                      ; the gap: at most the guard
        mov     bx,[si+A1_WRING]
        cmp     cx,[si+A1_GUARD]
        jbe     .from
        mov     cx,[si+A1_GUARD]
        mov     bx,[si+A1_PRING]
.from:  mov     [si+A1_WPOS],ax
        mov     [si+A1_WPOS+2],dx
        mov     ax,bx
        call    a1_quiet
        mov     ax,[si+A1_PRING]
        add     ax,[si+A1_GUARD]
        cmp     ax,[si+A1_RING]
        jb      .in
        sub     ax,[si+A1_RING]
.in:    mov     [si+A1_WRING],ax
.ok:    ret

; give back, oldest first, the headers the DMA played past; SI = dev
a1_retire:
.next:  pushf
        cli
        mov     bx,[si+A1_HEAD]
        mov     dx,[si+A1_HEAD+2]
        mov     ax,bx
        or      ax,dx
        jz      .none
        cmp     bx,[si+A1_CUR]
        jne     .copied
        cmp     dx,[si+A1_CUR+2]
        je      .none                   ; still being copied
.copied:
        mov     es,dx
        mov     ax,[si+A1_PPOS]
        sub     ax,[es:bx+WH_END]
        js      .none                   ; not played yet
        mov     ax,[es:bx+WH_NEXT]
        mov     cx,[es:bx+WH_NEXT+2]
        mov     [si+A1_HEAD],ax
        mov     [si+A1_HEAD+2],cx
        or      ax,cx
        jnz     .out
        mov     [si+A1_TAIL],ax
        mov     [si+A1_TAIL+2],ax
.out:   popf
        push    es
        push    bx
        push    cs
        call    L1_0048                 ; WHDR_DONE, and WOM_DONE
        jmp     .next
.none:  popf
        ret

; copy what's queued into the ring, up to the DMA, and silence where
; nothing is; SI = dev
a1_fill:
        mov     di,[si+A1_PPOS]
        add     di,[si+A1_RING]
        sub     di,[si+A1_WPOS]         ; free bytes, 0 to the ring's size
        call    a1_copy
        mov     cx,di                   ; the rest silent, WPOS kept
        mov     ax,[si+A1_WRING]
        jmp     a1_quiet

; copy up to DI bytes of what's queued into the ring at WPOS: DI = what's
; left of it; SI = dev
a1_copy:
        or      di,di
        jz      .full
.hdr:   mov     ax,[si+A1_CUR]
        or      ax,[si+A1_CUR+2]
        jz      .full
        mov     ax,[si+A1_CURLEFT]
        mov     dx,[si+A1_CURLEFT+2]
        test    byte [si+A1_FMT],FMT_DUAL
        jz      .left1
        shr     dx,1                    ; each ring gets half, in whole
        rcr     ax,1                    ; frames
        mov     cx,[si+A1_ALIGN]
        dec     cx
        not     cx
        and     ax,cx
.left1: or      dx,dx
        jnz     .big
        or      ax,ax
        jnz     .some
        mov     [si+A1_CURLEFT],ax      ; copied, but for part of a frame
        mov     [si+A1_CURLEFT+2],ax
        call    a1_next
        jmp     .hdr
.big:   mov     ax,di
.some:  cmp     ax,di
        jbe     .end
        mov     ax,di
.end:   mov     cx,[si+A1_RING]
        sub     cx,[si+A1_WRING]        ; not past the ring's end
        cmp     ax,cx
        jbe     .n
        mov     ax,cx
.n:     test    byte [si+A1_FMT],FMT_DUAL
        jz      .copy
        cmp     ax,A1_SPLIT
        jbe     .split
        mov     ax,A1_SPLIT
.split: call    a1_split
        jmp     .copied
.copy:  push    ax
        push    word [si+DEV_BUF_SEL]
        mov     cx,[si+DEV_BUF_OFF]
        add     cx,[si+A1_WRING]
        push    cx
        push    word [si+A1_CURPTR+2]
        push    word [si+A1_CURPTR]
        push    ax
        call    L1_1AF4                 ; huge source: DX:AX after it
        mov     [si+A1_CURPTR],ax
        mov     [si+A1_CURPTR+2],dx
        pop     ax
        sub     [si+A1_CURLEFT],ax
        sbb     word [si+A1_CURLEFT+2],0
.copied:
        add     [si+A1_DPOS],ax
        adc     word [si+A1_DPOS+2],0
        call    a1_wrote
        sub     di,ax
        mov     ax,[si+A1_CURLEFT]      ; all of it: its end is set now
        or      ax,[si+A1_CURLEFT+2]
        jnz     .left
        call    a1_next
.left:  or      di,di
        jnz     .hdr
.full:  ret

; AX bytes (whole frames, at most A1_SPLIT) of each ring from 4-channel
; frames, at WRING: channels 1-2 to Audio 1's ring, 3-4 to Audio 2's; AX
; kept; SI = dev
a1_split:
        push    ax
        push    di
        mov     [si+A1_SPLITN],ax
        pushf
        cli                             ; a1_bounce is shared
        push    ds                      ; the frames to a1_bounce: the
        mov     cx,a1_bounce            ; source is huge
        push    cx
        push    word [si+A1_CURPTR+2]
        push    word [si+A1_CURPTR]
        shl     ax,1
        push    ax
        call    L1_1AF4
        mov     [si+A1_CURPTR],ax
        mov     [si+A1_CURPTR+2],dx
        mov     bx,a1_bounce
        mov     es,[si+DEV_BUF_SEL]
        mov     di,[si+DEV_BUF_OFF]
        call    .half
        mov     bx,a1_bounce
        add     bx,[si+A1_ALIGN]
        mov     es,[si+DEV_A2_BUF_SEL]
        mov     di,[si+DEV_A2_BUF_OFF]
        call    .half
        popf
        pop     di
        pop     ax
        mov     cx,ax
        shl     cx,1
        sub     [si+A1_CURLEFT],cx
        sbb     word [si+A1_CURLEFT+2],0
        ret
; every other ALIGN bytes from DS:BX to the ring at ES:DI, from WRING
.half:  add     di,[si+A1_WRING]
        mov     dx,[si+A1_SPLITN]
        mov     ax,[si+A1_ALIGN]
        push    si
        mov     si,bx
        cld
.frame: mov     cx,ax
        rep     movsb
        add     si,ax                   ; past the other DAC's half
        sub     dx,ax
        jnz     .frame
        pop     si
        ret

; AX more bytes written, AX kept; SI = dev
a1_wrote:
        add     [si+A1_WPOS],ax
        adc     word [si+A1_WPOS+2],0
        push    ax
        add     ax,[si+A1_WRING]
        cmp     ax,[si+A1_RING]
        jb      .in
        sub     ax,[si+A1_RING]
.in:    mov     [si+A1_WRING],ax
        pop     ax
        ret

; CX bytes of silence into the ring from offset AX, wrapping at its end,
; and into Audio 2's in dual playback; SI = dev
a1_quiet:
        jcxz    .done
        push    di
        push    cx
        push    ax
        mov     es,[si+DEV_BUF_SEL]
        mov     di,[si+DEV_BUF_OFF]
        call    .ring
        pop     ax
        pop     cx
        test    byte [si+A1_FMT],FMT_DUAL
        jz      .one
        mov     es,[si+DEV_A2_BUF_SEL]
        mov     di,[si+DEV_A2_BUF_OFF]
        call    .ring
.one:   pop     di
.done:  ret
; CX bytes from offset AX of the ring at ES:DI
.ring:  mov     dx,[si+A1_RING]
        sub     dx,ax                   ; bytes to the end
        push    di
        add     di,ax
        mov     al,[si+A1_SILENCE]
        cld
        cmp     cx,dx
        jbe     .last
        sub     cx,dx
        xchg    cx,dx
        rep     stosb
        pop     di
        push    di
        mov     cx,dx
.last:  rep     stosb
        pop     di
        ret

; the header being copied is all in the ring: it's played once PPOS gets
; to WPOS; then the loop's next pass, or the next header; SI = dev
a1_next:
        les     bx,[si+A1_CUR]
        mov     ax,[si+A1_WPOS]
        mov     [es:bx+WH_END],ax
        test    byte [es:bx+WH_FLAGS],WHDR_ENDLOOP
        jz      .on
        mov     ax,[si+A1_LOOPHDR]
        or      ax,[si+A1_LOOPHDR+2]
        jz      .on                     ; no loop began
        cmp     word [si+A1_LOOPS+2],0
        jne     .again
        cmp     word [si+A1_LOOPS],1
        jbe     .last                   ; its last pass
.again: mov     ax,[si+A1_WPOS]
        cmp     ax,[si+A1_LOOPW]
        je      .last                   ; a pass without data
        mov     [si+A1_LOOPW],ax
        sub     word [si+A1_LOOPS],1
        sbb     word [si+A1_LOOPS+2],0
        les     bx,[si+A1_LOOPHDR]
        jmp     a1_again
.last:  xor     ax,ax
        mov     [si+A1_LOOPHDR],ax
        mov     [si+A1_LOOPHDR+2],ax
.on:    les     bx,[es:bx+WH_NEXT]
        mov     ax,es
        or      ax,bx
        jnz     a1_enter
        mov     [si+A1_CUR],ax
        mov     [si+A1_CUR+2],ax
        ret

; ES:BX is the next header to copy: a WHDR_BEGINLOOP one starts a loop,
; unless one plays (loops don't nest); SI = dev
a1_enter:
        test    byte [es:bx+WH_FLAGS],WHDR_BEGINLOOP
        jz      a1_again
        mov     ax,[si+A1_LOOPHDR]
        or      ax,[si+A1_LOOPHDR+2]
        jnz     a1_again
        mov     [si+A1_LOOPHDR],bx
        mov     [si+A1_LOOPHDR+2],es
        mov     ax,[es:bx+WH_LOOPS]
        mov     [si+A1_LOOPS],ax
        mov     ax,[es:bx+WH_LOOPS+2]
        mov     [si+A1_LOOPS+2],ax
        mov     ax,[si+A1_WPOS]
        mov     [si+A1_LOOPW],ax
; ES:BX is the header to copy, from its start
a1_again:
        mov     [si+A1_CUR],bx
        mov     [si+A1_CUR+2],es
        mov     ax,[es:bx+WH_DATA]
        mov     [si+A1_CURPTR],ax
        mov     ax,[es:bx+WH_DATA+2]
        mov     [si+A1_CURPTR+2],ax
        mov     ax,[es:bx+WH_LENGTH]
        mov     [si+A1_CURLEFT],ax
        mov     ax,[es:bx+WH_LENGTH+2]
        mov     [si+A1_CURLEFT+2],ax
        ret

; --- far routines for a1wave.asm, at task time -------------------------

; a1_ready(dev), far pascal: the ring for a DMA that starts at its
; beginning, the DMA stopped: turned so what it didn't play comes first,
; then filled
a1_ready:
        push    bp
        mov     bp,sp
        push    si
        push    di
        mov     si,[bp+6]
        mov     ax,[si+A1_WPOS]         ; the DMA went past the data: none
        mov     dx,[si+A1_WPOS+2]       ; to keep
        sub     ax,[si+A1_PPOS]
        sbb     dx,[si+A1_PPOS+2]
        jns     .turn
        mov     ax,[si+A1_PPOS]
        mov     dx,[si+A1_PPOS+2]
        mov     [si+A1_WPOS],ax
        mov     [si+A1_WPOS+2],dx
        mov     [si+A1_GAPEND],ax
        mov     [si+A1_GAPEND+2],dx
        mov     ax,[si+A1_PRING]
        mov     [si+A1_WRING],ax
.turn:  cmp     word [si+A1_PRING],0
        je      .fill
        mov     es,[si+DEV_BUF_SEL]
        mov     di,[si+DEV_BUF_OFF]
        call    .ring
        test    byte [si+A1_FMT],FMT_DUAL
        jz      .pos
        mov     es,[si+DEV_A2_BUF_SEL]
        mov     di,[si+DEV_A2_BUF_OFF]
        call    .ring
.pos:   mov     ax,[si+A1_WRING]
        sub     ax,[si+A1_PRING]
        jae     .w
        add     ax,[si+A1_RING]
.w:     mov     [si+A1_WRING],ax
        mov     word [si+A1_PRING],0
.fill:  call    a1_fill
        pop     di
        pop     si
        pop     bp
        retf    2
; the ring at ES:DI turned left by PRING: three reversals
.ring:  mov     ax,di
        mov     dx,[si+A1_PRING]
        call    .rev
        mov     ax,di
        add     ax,[si+A1_PRING]
        mov     dx,[si+A1_RING]
        sub     dx,[si+A1_PRING]
        call    .rev
        mov     ax,di
        mov     dx,[si+A1_RING]
; DX bytes at ES:AX reversed
.rev:   or      dx,dx
        jz      .rdone
        push    si
        push    di
        mov     si,ax
        mov     di,ax
        add     di,dx
        dec     di
.swap:  cmp     si,di
        jae     .rend
        mov     al,[es:si]
        xchg    al,[es:di]
        mov     [es:si],al
        inc     si
        dec     di
        jmp     .swap
.rend:  pop     di
        pop     si
.rdone: ret

; a1_freeze(dev), far pascal: PPOS where the stopped DMA is
a1_freeze:
        push    bp
        mov     bp,sp
        push    si
        mov     si,[bp+6]
        call    a1_advance
        pop     si
        pop     bp
        retf    2

; a1_topup(dev), far pascal: what a program writes while the DMA runs
; goes into the ring now, as ESS's write does for Audio 2 (6:2FC1), not at
; the next interrupt: after a short first header the DMA would play a
; gap.  A1_TOPUP bytes at a time with interrupts off, so they can run in
; between; silence is there already past WPOS
A1_TOPUP        equ 2048
a1_topup:
        push    bp
        mov     bp,sp
        push    si
        push    di
        mov     si,[bp+6]
.more:  pushf
        cli
        test    byte [si+A1_STATE],A1F_RUN
        jz      .done
        call    a1_advance
        call    a1_gap
        mov     di,[si+A1_PPOS]
        add     di,[si+A1_RING]
        sub     di,[si+A1_WPOS]
        cmp     di,A1_TOPUP
        jbe     .n
        mov     di,A1_TOPUP
.n:     mov     ax,di
        push    ax
        call    a1_copy
        pop     ax
        cmp     di,ax
        je      .done                   ; nothing copied
        or      di,di
        jnz     .done                   ; all there was
        popf
        jmp     .more
.done:  popf
        pop     di
        pop     si
        pop     bp
        retf    2

; a1_queue(dev, lpHdr), far pascal: a header at the end of the queue
a1_queue:
        push    bp
        mov     bp,sp
        push    si
        mov     si,[bp+10]
        les     bx,[bp+6]
        pushf
        cli
        mov     ax,[si+A1_TAIL]
        or      ax,[si+A1_TAIL+2]
        jz      .first
        push    es
        push    bx
        les     bx,[si+A1_TAIL]
        pop     word [es:bx+WH_NEXT]
        pop     word [es:bx+WH_NEXT+2]
        les     bx,[bp+6]
        jmp     .tail
.first: mov     [si+A1_HEAD],bx
        mov     [si+A1_HEAD+2],es
.tail:  mov     [si+A1_TAIL],bx
        mov     [si+A1_TAIL+2],es
        mov     ax,[si+A1_CUR]
        or      ax,[si+A1_CUR+2]
        jnz     .done
        call    a1_enter                ; the copy was waiting for data
.done:  popf
        pop     si
        pop     bp
        retf    6

; a1_flush(dev), far pascal: every header back, oldest first, and the
; queue and positions from zero; the DMA is stopped
a1_flush:
        push    bp
        mov     bp,sp
        push    si
        mov     si,[bp+6]
        xor     ax,ax
        mov     [si+A1_CUR],ax
        mov     [si+A1_CUR+2],ax
.next:  mov     bx,[si+A1_HEAD]
        mov     dx,[si+A1_HEAD+2]
        mov     ax,bx
        or      ax,dx
        jz      .empty
        mov     es,dx
        mov     ax,[es:bx+WH_NEXT]
        mov     [si+A1_HEAD],ax
        mov     ax,[es:bx+WH_NEXT+2]
        mov     [si+A1_HEAD+2],ax
        push    es
        push    bx
        push    cs
        call    L1_0048
        jmp     .next
.empty: push    di
        push    ds
        pop     es
        lea     di,[si+A1_TAIL]
        mov     cx,A1_SIZEOF - (A1_TAIL - A1_BASE)
        xor     al,al
        cld
        rep     stosb
        pop     di
        pop     si
        pop     bp
        retf    2

; a1_played(dev), far pascal: DX:AX = the program's bytes played, the gaps
; left out
a1_played:
        push    bp
        mov     bp,sp
        push    si
        push    di
        mov     si,[bp+6]
        pushf
        cli
        mov     ax,[si+A1_PPOS]
        mov     dx,[si+A1_PPOS+2]
        test    byte [si+A1_STATE],A1F_RUN
        jz      .now
        push    dx
        push    ax
        call    a1_offset               ; where the DMA is since the last
        sub     ax,[si+A1_PRING]        ; interrupt
        jae     .fwd
        add     ax,[si+A1_RING]
.fwd:   pop     cx
        pop     dx
        add     ax,cx
        adc     dx,0
.now:   mov     cx,[si+A1_GAPEND]       ; not before the last gap's end
        mov     bx,[si+A1_GAPEND+2]
        sub     cx,ax
        sbb     bx,dx
        js      .skew
        mov     ax,[si+A1_GAPEND]
        mov     dx,[si+A1_GAPEND+2]
.skew:  sub     ax,[si+A1_WPOS]         ; less the gaps: WPOS - DPOS
        sbb     dx,[si+A1_WPOS+2]
        add     ax,[si+A1_DPOS]
        adc     dx,[si+A1_DPOS+2]
        mov     cx,[si+A1_DPOS]         ; and not past the data
        mov     bx,[si+A1_DPOS+2]
        sub     cx,ax
        sbb     bx,dx
        jns     .done
        mov     ax,[si+A1_DPOS]
        mov     dx,[si+A1_DPOS+2]
.done:  popf
        pop     di
        pop     si
        pop     bp
        retf    2

; a1_breakloop(dev), far pascal: the loop playing ends after this pass
a1_breakloop:
        push    bp
        mov     bp,sp
        mov     bx,[bp+6]
        pushf
        cli
        mov     ax,[bx+A1_LOOPHDR]
        or      ax,[bx+A1_LOOPHDR+2]
        jz      .none
        mov     word [bx+A1_LOOPS],1
        mov     word [bx+A1_LOOPS+2],0
.none:  popf
        pop     bp
        retf    2

; a1_hwstop(dev), far pascal: the DMA stopped, if it runs
a1_hwstop:
        push    bp
        mov     bp,sp
        push    si
        mov     si,[bp+6]
        call    a1_halt
        pop     si
        pop     bp
        retf    2

; a1_disable(dev), far pascal, in place of ESS's record stop at the last
; DRVM_DISABLE (3:4E20), which frees the interrupt handler after: that,
; and the player's DMA stopped too
a1_disable:
        push    bp
        mov     bp,sp
        push    si
        mov     si,[bp+6]
        push    si
        push    cs
        call    L1_1602
        call    a1_halt
        pop     si
        pop     bp
        retf    2

; the DMA stopped where it is (B8h bit 0 and the rest but bits 5:4 clear),
; the channel masked, the FIFO empty and an interrupt left over cleared;
; task time; SI = dev
a1_halt:
        test    byte [si+A1_STATE],A1F_RUN
        jz      .done
        pushf
        cli
        and     byte [si+A1_STATE],~A1F_RUN & 0FFh
        popf
        mov     al,DSP_READ_REG
        call    .dsp
        mov     al,CR_CONTROL
        call    .dsp
        push    si
        push    cs
        call    dsp_read
        and     al,30h
        push    ax
        mov     al,CR_CONTROL
        call    .dsp
        pop     ax
        call    .dsp
        xor     dh,dh
        mov     dl,[si+DEV_DMA_MASK]
        mov     al,[si+DEV_DMA_OFF]
        out     dx,al
        mov     dx,[si+DEV_BASE]
        add     dx,6
        mov     al,2
        out     dx,al
        xor     al,al
        out     dx,al
        add     dx,8
        in      al,dx                   ; Audio_Base+Eh
        test    byte [si+A1_FMT],FMT_DUAL
        jz      .done
        ; Audio 2 as well: stopped, its channel masked, its latch clear
        mov     ax,MX_A2_CONTROL1
        call    .mixer
        xor     dh,dh
        mov     dl,[si+DEV_A2_MASK]
        mov     al,[si+DEV_A2_OFF]
        out     dx,al
        push    si
        mov     al,MX_A2_CONTROL2
        push    ax
        push    cs
        call    mixer_read
        and     al,3Fh
        mov     ah,al
        mov     al,MX_A2_CONTROL2
        call    .mixer
.done:  ret
.dsp:   push    si
        push    ax
        push    cs
        call    dsp_write
        ret
; mixer register AL = AH
.mixer: push    si
        push    ax
        mov     al,ah
        push    ax
        push    cs
        call    mixer_write
        ret

; a1_d3_gate(dev, command), far pascal, in place of dsp_write where
; ESS's playback start sends D3h (6:2D75): the Audio 1 DAC stays in the
; mixer while the player has the device open
a1_d3_gate:
        push    bp
        mov     bp,sp
        mov     bx,[bp+8]
        test    byte [bx+A1_STATE],A1F_OPEN
        pop     bp
        jz      .ess
        retf    4
.ess:   jmp     dsp_write
