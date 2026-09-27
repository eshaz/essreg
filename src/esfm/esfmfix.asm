; Stuck-note fixes for ESFM.DRV, assembled when ESFM_FIX=1.  The code is
; appended to segment 1 (fixed code, callable at interrupt time).
;
; Notes:
;
; 1. modMessage doesn't refuse MODM_DATA, MODM_LONGDATA or MODM_RESET
;    anymore.  When a message arrives while the ESS driver is still
;    handling another one (from an interrupt-time callback, or from the
;    client's MOM_DONE callback), it answers MIDIERR_NOTREADY and the
;    caller drops the message.  A lost note off leaves the note sounding.
;    Here the message is queued instead, and the call that holds the
;    driver handles the queue, in order, before it returns.
; 2. MODM_OPEN and MODM_CLOSE hold the driver too, so a message from an
;    interrupt can't write FM registers between the address and the data
;    write of the chip reset or of the silencing at close.  Queued
;    messages of a closed client are dropped.
; 3. all_notes_off (close, power suspend) keys off every voice and lifts
;    the sustain pedal of every channel.  The ESS code sent note offs, so
;    notes held by the pedal kept sounding after the close.
; 4. chip_reset holds the driver as well (it also runs for DRV_POWER and
;    DRVM_DISABLE).
; 5. MODM_OPEN loads the bank file first if it changed (esfmfile.asm).
; 6. The sustain pedal is let up by a program change (esfmped.asm) and by
;    a GM, GS or XG reset (esfmgm.asm).
; 7. What General MIDI asks for beyond ESS's code: modulation, channel
;    pressure, tuning, master volume, controller 121 as RP-015
;    (esfmgm.asm), and running status in long messages (seg1.asm).
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

MODM_OPEN       equ 3
MODM_CLOSE      equ 4
MODM_DATA       equ 7
MODM_LONGDATA   equ 8
MODM_RESET      equ 9
MIDIERR_UNPREPARED equ 0x40
MIDIERR_NOTREADY equ 0x43
MHDR_DONE       equ 0x01
MHDR_PREPARED   equ 0x02
MHDR_INQUEUE    equ 0x10
MIDIHDR_FLAGS   equ 0x10

; modMessage(uDeviceID, uMsg, dwUser, dwParam1, dwParam2), far pascal
%define ARG_ID          bp+0x14
%define ARG_MSG         bp+0x12
%define ARG_USER        bp+0x0E
%define ARG_DW1         bp+0x0A
%define ARG_DW2         bp+0x06

; MIDI output driver entry (exported)
modMessage:
        push    bp
        mov     bp,sp
        push    ds
        movsel  ax, FIX_DS1, 0xFFFF
        mov     ds,ax
        mov     ax,[ARG_MSG]
        cmp     ax,MODM_DATA
        je      .serial
        cmp     ax,MODM_LONGDATA
        je      .serial
        cmp     ax,MODM_RESET
        je      .serial
        cmp     ax,MODM_OPEN
        je      .open
        cmp     ax,MODM_CLOSE
        je      .hold
        call    call_orig               ; no FM register access
        jmp     .ret

.serial:
        pushf
        cli
        cmp     word [fix_lock],0
        jne     .queue
        inc     word [fix_lock]
        pop     dx
        call    restore_if
        call    call_orig
        jmp     .unlock
.queue:
        call    q_put                   ; AX = result
        pop     dx
        call    restore_if
        xor     dx,dx
        jmp     .ret

.open:
        ; load the bank file first if it changed (esfmfile.asm)
        call    fix_bank_check
.hold:
        ; OPEN and CLOSE come at task time, so if the driver is already
        ; held, the caller itself holds it (a callback): just count
        inc     word [fix_lock]
        call    call_orig
        cmp     word [ARG_MSG],MODM_CLOSE
        jne     .unlock
        or      ax,ax
        jnz     .unlock
        push    ax
        push    dx
        call    q_purge
        pop     dx
        pop     ax
.unlock:
        push    ax
        push    dx
        call    fix_unlock
        pop     dx
        pop     ax
.ret:
        lea     sp,[bp-2]
        pop     ds
        pop     bp
        retf    0x10

; turn IF back on if it's set in the flags in DX
restore_if:
        test    dh,0x02
        jz      .off
        sti
.off:
        ret

; call the ESS modMessage with this call's arguments
call_orig:
        push    word [ARG_ID]
        push    word [ARG_MSG]
        push    word [ARG_USER+2]
        push    word [ARG_USER]
        push    word [ARG_DW1+2]
        push    word [ARG_DW1]
        push    word [ARG_DW2+2]
        push    word [ARG_DW2]
        push    cs
        call    fix_process             ; esfmgm.asm
        ret

; queue this call's message (interrupts are off)
; AX = result for the caller
q_put:
        cmp     word [ARG_MSG],MODM_LONGDATA
        jne     .put
        les     bx,[ARG_DW1]
        test    byte [es:bx+MIDIHDR_FLAGS],MHDR_PREPARED
        jnz     .prepared
        mov     ax,MIDIERR_UNPREPARED
        ret
.prepared:
        and     byte [es:bx+MIDIHDR_FLAGS],~MHDR_DONE & 0xFF
        or      byte [es:bx+MIDIHDR_FLAGS],MHDR_INQUEUE
.put:
        mov     bx,[q_tail]
        mov     ax,bx
        inc     ax
        and     ax,QSIZE - 1
        cmp     ax,[q_head]
        je      .full
        mov     [q_tail],ax
        shl     bx,4
        add     bx,fix_queue
        mov     ax,[ARG_MSG]
        mov     [bx+Q_MSG],ax
        mov     ax,[ARG_USER]
        mov     [bx+Q_USER],ax
        mov     ax,[ARG_USER+2]
        mov     [bx+Q_USER+2],ax
        mov     ax,[ARG_DW1]
        mov     [bx+Q_DW1],ax
        mov     ax,[ARG_DW1+2]
        mov     [bx+Q_DW1+2],ax
        mov     ax,[ARG_DW2]
        mov     [bx+Q_DW2],ax
        mov     ax,[ARG_DW2+2]
        mov     [bx+Q_DW2+2],ax
        add     word [fix_queued],1
        adc     word [fix_queued+2],0
        mov     ax,[q_tail]
        sub     ax,[q_head]
        and     ax,QSIZE - 1
        cmp     ax,[fix_maxdepth]
        jbe     .done
        mov     [fix_maxdepth],ax
.done:
        xor     ax,ax
        ret
.full:
        cmp     word [ARG_MSG],MODM_LONGDATA
        jne     .refuse
        les     bx,[ARG_DW1]
        and     byte [es:bx+MIDIHDR_FLAGS],~MHDR_INQUEUE & 0xFF
.refuse:
        add     word [fix_overflow],1
        adc     word [fix_overflow+2],0
        mov     ax,MIDIERR_NOTREADY
        ret

; drop the queued messages of the client that was just closed (dwUser of
; this call) and mark its queued long messages done
q_purge:
        push    si
        push    di
        pushf
        cli
        pop     dx
        mov     bx,[q_head]
.next:
        cmp     bx,[q_tail]
        je      .done
        mov     si,bx
        shl     si,4
        add     si,fix_queue
        mov     ax,[si+Q_USER]
        cmp     ax,[ARG_USER]
        jne     .skip
        mov     ax,[si+Q_USER+2]
        cmp     ax,[ARG_USER+2]
        jne     .skip
        cmp     word [si+Q_MSG],MODM_LONGDATA
        jne     .drop
        les     di,[si+Q_DW1]
        and     byte [es:di+MIDIHDR_FLAGS],~MHDR_INQUEUE & 0xFF
        or      byte [es:di+MIDIHDR_FLAGS],MHDR_DONE
.drop:
        mov     word [si+Q_MSG],0
        inc     word [fix_purged]
.skip:
        inc     bx
        and     bx,QSIZE - 1
        jmp     .next
.done:
        call    restore_if
        pop     di
        pop     si
        ret

; hold the driver (counted, the outermost fix_unlock releases it)
fix_enter:
        inc     word [fix_lock]
        ret

; let go of the driver: the outermost holder handles every queued message
; first, then releases it with interrupts off so nothing can be queued
; after the last look at the queue
fix_unlock:
        push    si
        push    di
.again:
        pushf
        cli
        pop     dx
        cmp     word [fix_lock],1
        ja      .nested
        mov     si,[q_head]
        cmp     si,[q_tail]
        je      .release
        mov     ax,si
        inc     ax
        and     ax,QSIZE - 1
        mov     [q_head],ax
        shl     si,4
        add     si,fix_queue
        mov     ax,[si+Q_MSG]
        or      ax,ax
        jz      .dropped
        ; copy the entry to the stack before interrupts can reuse the slot
        push    word 0
        push    ax
        push    word [si+Q_USER+2]
        push    word [si+Q_USER]
        push    word [si+Q_DW1+2]
        push    word [si+Q_DW1]
        push    word [si+Q_DW2+2]
        push    word [si+Q_DW2]
        cmp     ax,MODM_LONGDATA
        jne     .call
        les     bx,[si+Q_DW1]
        and     byte [es:bx+MIDIHDR_FLAGS],~MHDR_INQUEUE & 0xFF
.call:
        call    restore_if
        push    cs
        call    fix_process             ; esfmgm.asm
        jmp     .again
.dropped:
        call    restore_if
        jmp     .again
.release:
        mov     word [fix_lock],0
        call    restore_if
        jmp     .out
.nested:
        dec     word [fix_lock]
        call    restore_if
.out:
        pop     di
        pop     si
        ret

; all_notes_off(dev), far: key off every voice and lift every sustain
; pedal (close, power suspend)
fix_all_off:
        push    bp
        mov     bp,sp
        push    di
        push    si
        call    fix_enter
        mov     si,[bp+6]
        xor     bx,bx
.pedal:
        and     byte [si+bx+DEV_CHAN_FLAGS],0xFE
        inc     bx
        cmp     bx,16
        jb      .pedal
        xor     di,di
.voice:
        push    si
        push    di
        push    cs
        call    voice_off
        inc     di
        cmp     di,NUM_VOICES
        jb      .voice
        call    fix_unlock
        pop     si
        pop     di
        mov     sp,bp
        pop     bp
        retf    2
