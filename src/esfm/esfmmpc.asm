; MPC songs for ESFM.DRV, assembled when ESFM_FIX=1.  The code is appended
; to segment 1 (fixed code).
;
; Notes:
;
; The MIDI files of Windows 3.0 and 3.1, and the classics of Windows 98,
; follow Microsoft's authoring guidelines for the Multimedia PC: each holds
; its song twice, for the two kinds of synthesizer that Windows 3.1's MIDI
; Mapper played on.  The extended arrangement is on channels 1 to 10, with
; its drums on 10, and the base-level one on channels 13 to 16, with its
; drums on 16; channels 11 and 12 stay unused.  The Mapper sent each
; synthesizer the arrangement it was set up for.  The Mapper of Windows 95
; and 98 sends all 16 channels to the device, so both arrangements play at
; once: every part twice, on FM in phase and so twice as loud, and the
; drums of channel 16 as the notes of a melodic instrument.
;
; With MpcSongs=1, the default, both devices play such a song's extended
; arrangement only.  A note on of channels 13 to 16 is left out while
; channels 1 to 10 have had a message and channels 11 and 12 none, since
; the device's open or its last reset (MODM_RESET, or a GM, GS or XG
; reset).  Note offs and the channel mode messages (controllers 120 to 127)
; don't count, since players and the resets send them to all 16 channels.
; A General MIDI song that uses channels 13 to 16 uses 11 or 12 as well,
; and sets them up at its start, so its notes play; should channel 11 or 12
; come later, the next notes of 13 to 16 play again.  Every other message
; of channels 13 to 16 still sets its channel up, so they play right then.
; With MpcSongs=0 both arrangements play, as on ESS's driver.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; a MIDI message for SI = dev: AL = its status, AH = data 1, CL = data 2.
; CF set: a note of a song's base-level copy, left out.  Keeps all
fix_mpc:
        test    word [fix_opts],OPT_MPC
        jz      .keep
        push    ax
        push    bx
        mov     bl,al
        and     bl,0x0F                 ; the channel
        mov     bh,al
        and     bh,0xF0
        cmp     bh,0xF0
        je      .pass                   ; a system message
        cmp     bh,0x80
        je      .note                   ; note offs don't count
        cmp     bh,0xB0
        jne     .count
        cmp     ah,120
        jae     .note                   ; nor channel mode messages
.count: mov     al,MPC_EXT
        cmp     bl,9
        jbe     .set
        cmp     bl,11
        ja      .note
        mov     al,MPC_MID
.set:   or      [si+DEV_MPC],al
.note:  cmp     bh,0x90
        jne     .pass
        or      cl,cl
        jz      .pass                   ; velocity 0: a note off
        cmp     bl,12
        jb      .pass
        mov     al,[si+DEV_MPC]
        and     al,MPC_EXT | MPC_MID
        cmp     al,MPC_EXT
        jne     .pass
        cmp     word [fix_mpc_drops],0xFFFF
        je      .drop
        inc     word [fix_mpc_drops]
.drop:  pop     bx
        pop     ax
        stc
        ret
.pass:  pop     bx
        pop     ax
.keep:  clc
        ret

; short_msg's start (seg1), with its frame: MpcSongs on device 0.  CF set:
; the message is left out.  Keeps all
fix_mpc_ess:
        push    ax
        push    cx
        push    si
        mov     si,[bp+0x8]             ; dev
        mov     ax,[bp+0x4]             ; the status, data 1
        and     ah,0x7F
        mov     cl,[bp+0x6]             ; data 2
        and     cl,0x7F
        call    fix_mpc
        pop     si
        pop     cx
        pop     ax
        ret
