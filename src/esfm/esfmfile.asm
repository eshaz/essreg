; Bank file for ESFM.DRV, assembled when ESFM_FIX=1.  The code is appended
; to segment 1 (fixed code).
;
; Notes:
;
; SYSTEM.INI [ESFM.DRV] Bank= names a patch bank file, raw or RIFF "Ptch"
; with an "fm4 " chunk (the files esfmpat takes).  The driver plays that
; bank instead of the one built into ESFM.DRV:
; - DRV_ENABLE and every MODM_OPEN load the file if it changed
; - while a program has the device open, a task checks the file every
;   second and loads it when it changes, once two checks in a row read the
;   same bytes (a file that is still being written isn't loaded)
; - a file that is missing or isn't a patch bank keeps the bank that plays
; - without Bank= the driver goes back to its built-in bank
;
; A new bank goes into a new block, swapped in while the driver is held,
; so a note on never sees half a bank.  The status is in DGROUP after the
; ESFMFIX counters (esfmfixd.asm), for essctl.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; module references, in the order of layout.json
KERNEL          equ 1
MMSYSTEM        equ 3

%define KERNEL_GlobalAlloc 15
%define KERNEL_GlobalFree 17
%define KERNEL_GlobalSize 20
%define KERNEL_Yield 29
%define KERNEL_GetCurrentTask 36
%define KERNEL_FindResource 60
%define KERNEL_LoadResource 61
%define KERNEL_LockResource 62
%define KERNEL_FreeResource 63
%define KERNEL_SizeofResource 65
%define KERNEL_DOS3Call 102
%define KERNEL_GlobalWire 111
%define KERNEL_GlobalUnWire 112
%define KERNEL_GetPrivateProfileString 128
%define KERNEL_GlobalPageLock 191
%define KERNEL_GlobalPageUnlock 192
%define MMSYSTEM_timeSetEvent 602
%define MMSYSTEM_timeKillEvent 603
%define MMSYSTEM_mmTaskCreate 900
%define MMSYSTEM_mmTaskBlock 902
%define MMSYSTEM_mmTaskSignal 903

; far call to an import: each call site gets a relocation record, written
; by fix_file_relocs into the relocation table of segment 1
%assign FIX_NIMP 0
%macro import 2
%assign FIX_NIMP FIX_NIMP + 1
        callp   ..@FIX_I%[FIX_NIMP], 0xFFFF, 0x0000
%xdefine FIX_IMOD%[FIX_NIMP] %1
%xdefine FIX_IORD%[FIX_NIMP] %2
%endmacro
%macro kernel 1
        import  KERNEL, KERNEL_%1
%endmacro
%macro mmsystem 1
        import  MMSYSTEM, MMSYSTEM_%1
%endmacro

%macro fix_file_relocs 0
        reloc 2, 0, ..@FIX_DS2, 0x0004, 0x0000          ; seg4
        reloc 2, 0, ..@FIX_DS3, 0x0004, 0x0000          ; seg4
        reloc 2, 0, ..@FIX_S1A, 0x0003, 0x0000          ; seg3
%assign i 1
%rep FIX_NIMP
        reloc 3, 1, ..@FIX_I%[i], FIX_IMOD%[i], FIX_IORD%[i]
%assign i i + 1
%endrep
%endmacro

; GMEM_SHARE | GMEM_ZEROINIT | GMEM_MOVEABLE, as bank_load allocates
GMEM_BANK       equ 0x2042
BANK_TABLE      equ 512         ; 256 offsets
BANK_VOICE      equ 36
BANK_MAX        equ 0x7FF0      ; the most esfmpat and essctl take
FILE_MAX        equ 0xFFF0
TIME_PERIODIC   equ 1
WATCH_MS        equ 1000

; fix_bstate
BS_NONE         equ 0           ; no Bank=, the built-in bank plays
BS_LOADED       equ 1           ; the file's bank plays
BS_MISSING      equ 2           ; the file can't be opened or read
BS_BAD          equ 3           ; the file isn't a patch bank
BS_NOMEM        equ 4           ; no memory for it
BS_CHANGING     equ 5           ; changed, waiting for the next check

; fix_bwatch
WATCH_OFF       equ 0
WATCH_STARTING  equ 1           ; task created, not running yet
WATCH_RUNNING   equ 2
WATCH_FAILED    equ 3           ; fix_bwerr says why

; DRV_ENABLE (DriverProc calls this instead of bank_load): the built-in
; bank, then the bank file if there is one
; DX:AX = 0 on success, like bank_load
fix_drv_enable:
        callf   bank_load, ..@FIX_S1A, ..@FIX_S1B
        mov     cx,ax
        or      cx,dx
        jnz     .out
        mov     word [fix_bsrc],0
        xor     ax,ax
        call    fix_bank_poll
        xor     ax,ax
        cwd
.out:
        retf

; DRV_DISABLE (instead of bank_free): stop the task before the driver can
; be unloaded, then free the bank
fix_drv_disable:
        call    fix_watch_stop
        callf   bank_free, ..@FIX_S1B, 0xFFFF
        retf

; check the bank file and load it if it's new or changed (task time)
; AX = 1: load a changed file only once two checks read the same bytes
fix_bank_poll:
        cmp     word [fix_polling],0    ; a file call of another check yielded
        jne     .busy
        mov     word [fix_polling],1
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    ax                      ; [bp-6] wait for two checks
        inc     word [fix_bpolls]
        push    ds
        push    word fix_ini_sect
        push    ds
        push    word fix_ini_key
        push    ds
        push    word fix_nul
        push    ds
        push    word fix_bpath
        push    word FIX_PATH
        push    ds
        push    word fix_ini_file
        kernel  GetPrivateProfileString
        or      ax,ax
        jnz     .file
        ; no Bank=: back to the built-in bank
        mov     word [fix_bstate],BS_NONE
        mov     word [fix_bpsize],0
        cmp     word [fix_bsrc],0
        je      .done
        call    fix_builtin
        jc      .done
        mov     word [fix_bsrc],0
        mov     word [fix_blen],0
        jmp     .done
.file:
        call    fix_read                ; SI = block, CX = bytes
        jnc     .read
        mov     [fix_bstate],ax
        mov     word [fix_bpsize],0
        jmp     .done
.read:
        mov     es,si
        call    fix_unwrap              ; DI = where the bank starts, CX = bytes
        jc      .bad
        or      di,di
        jz      .check
        ; move the bank to the start of the block
        push    ds
        push    si
        push    cx
        mov     ds,si
        mov     si,di
        xor     di,di
        cld
        rep     movsb
        pop     cx
        pop     si
        pop     ds
.check:
        call    fix_valid
        jc      .bad
        call    fix_same
        jne     .changed
        ; already playing it
        mov     word [fix_bstate],BS_LOADED
        mov     word [fix_bsrc],1
        mov     [fix_blen],cx
        mov     word [fix_bpsize],0
        jmp     .free
.changed:
        call    fix_sum                 ; DX:AX
        cmp     word [bp-6],0
        je      .load
        cmp     cx,[fix_bpsize]
        jne     .pending
        cmp     ax,[fix_bpsum]
        jne     .pending
        cmp     dx,[fix_bpsum+2]
        je      .load
.pending:
        ; the file may still be being written, look again next time
        mov     [fix_bpsize],cx
        mov     [fix_bpsum],ax
        mov     [fix_bpsum+2],dx
        mov     word [fix_bstate],BS_CHANGING
        jmp     .free
.load:
        cmp     word [fix_quit],0       ; the driver is being disabled
        jne     .free
        push    cx
        mov     bx,si
        call    fix_install
        pop     cx
        jc      .nomem
        mov     word [fix_bstate],BS_LOADED
        mov     word [fix_bsrc],1
        mov     [fix_blen],cx
        inc     word [fix_bloads]
        mov     word [fix_bpsize],0
        jmp     .done
.nomem:
        mov     word [fix_bstate],BS_NOMEM
        jmp     .free
.bad:
        mov     word [fix_bstate],BS_BAD
        mov     word [fix_bpsize],0
.free:
        ; no segment register may hold a selector that is freed: an
        ; interrupt handler that pops it would fault
        push    ds
        pop     es
        push    si
        kernel  GlobalFree
.done:
        pop     ax
        pop     di
        pop     si
        pop     bp
        mov     word [fix_polling],0
        ; and the caller gets no freed selector in ES either (the old
        ; bank's, from bank_load)
        push    ds
        pop     es
.busy:
        ret

; read the file named in fix_bpath into a new block
; SI = block, CX = bytes, or CF and AX = BS_MISSING, BS_BAD or BS_NOMEM
fix_read:
        push    bp
        mov     bp,sp
        push    di
        push    word 0                  ; [bp-4] block
        ; open it, with the long file name call first (Windows 95)
        mov     ax,0x716C
        mov     bx,0x0040               ; read only, deny none
        xor     cx,cx
        mov     dx,0x0001               ; open, fail if missing
        mov     si,fix_bpath
        xor     di,di
        stc
        kernel  DOS3Call
        jc      .nolfn
        cmp     ax,0x7100               ; unknown call, and the carry clear
        jne     .open
.nolfn:
        cmp     ax,0x7100               ; no long file names
        jne     .missing
        mov     ax,0x3D40               ; open, read only, deny none
        mov     dx,fix_bpath
        kernel  DOS3Call
        jc      .missing
.open:
        mov     si,ax                   ; SI = file
        mov     ax,0x4202               ; the size
        mov     bx,si
        xor     cx,cx
        xor     dx,dx
        kernel  DOS3Call
        jc      .ioerr
        or      dx,dx
        jnz     .toobig
        cmp     ax,FILE_MAX
        ja      .toobig
        cmp     ax,BANK_TABLE
        jb      .toobig
        mov     di,ax                   ; DI = bytes
        mov     ax,0x4200
        mov     bx,si
        xor     cx,cx
        xor     dx,dx
        kernel  DOS3Call
        jc      .ioerr
        push    word GMEM_BANK
        push    word 0
        push    di
        kernel  GlobalAlloc
        or      ax,ax
        jz      .nomem
        mov     [bp-4],ax
        push    ds
        mov     ds,ax
        mov     ah,0x3F
        mov     bx,si
        mov     cx,di
        xor     dx,dx
        kernel  DOS3Call
        pop     ds
        jc      .ioerr
        cmp     ax,di
        jne     .ioerr                  ; it got shorter, try again next time
        call    .close
        mov     si,[bp-4]
        mov     cx,di
        clc
        jmp     .out
.toobig:
        mov     di,BS_BAD
        jmp     .fail
.nomem:
        mov     di,BS_NOMEM
        jmp     .fail
.ioerr:
        mov     di,BS_MISSING
.fail:
        call    .close
        mov     ax,[bp-4]
        or      ax,ax
        jz      .failed
        push    ax
        kernel  GlobalFree
.failed:
        mov     ax,di
        stc
        jmp     .out
.missing:
        mov     ax,BS_MISSING
        stc
.out:
        mov     di,[bp-2]
        mov     sp,bp
        pop     bp
        ret
.close:
        mov     ah,0x3E
        mov     bx,si
        kernel  DOS3Call
        ret

; find the bank in a bank file: the whole file, or the "fm4 " chunk of a
; RIFF "Ptch" file (bank_unwrap in src/esfmbank.c)
; ES:0 = file, CX = bytes; DI = where the bank starts, CX = its bytes, or CF
fix_unwrap:
        xor     di,di
        cmp     cx,12
        jb      .ok
        cmp     word [es:0],'RI'
        jne     .ok
        cmp     word [es:2],'FF'
        jne     .ok
        cmp     word [es:8],'Pt'
        jne     .ok
        cmp     word [es:10],'ch'
        jne     .ok
        mov     di,12
.chunk:
        mov     ax,cx
        sub     ax,di
        jb      .bad
        sub     ax,8                    ; AX = bytes after the chunk header
        jb      .bad
        cmp     word [es:di+6],0
        jne     .bad
        mov     dx,[es:di+4]            ; chunk length
        cmp     dx,ax
        ja      .bad
        cmp     word [es:di],'fm'
        jne     .skip
        cmp     word [es:di+2],'4 '
        jne     .skip
        add     di,8
        mov     cx,dx
.ok:
        clc
        ret
.skip:
        add     di,8
        add     di,dx
        test    dl,1
        jz      .chunk
        inc     di
        jmp     .chunk
.bad:
        stc
        ret

; check a bank the way bank_check (src/esfmbank.c) does: the table, every
; patch inside the bank, at least one patch
; ES:0 = bank, CX = bytes; CF if it isn't a bank
fix_valid:
        push    si
        push    di
        cmp     cx,BANK_TABLE
        jb      .bad
        cmp     cx,BANK_MAX
        ja      .bad
        xor     bx,bx
        xor     di,di                   ; DI = patches
.entry:
        mov     si,[es:bx]
        or      si,si
        jz      .next
        cmp     si,BANK_TABLE
        jb      .bad
        cmp     si,cx
        jae     .bad
        ; mode 0: one voice, 3: ignored by the driver, else two voices
        mov     al,[es:si]
        shr     al,1
        and     al,3
        mov     dx,BANK_VOICE
        jz      .fits
        xor     dx,dx
        cmp     al,3
        je      .fits
        mov     dx,2 * BANK_VOICE
.fits:
        add     dx,si
        jc      .bad
        cmp     dx,cx
        ja      .bad
        inc     di
.next:
        add     bx,2
        cmp     bx,BANK_TABLE
        jb      .entry
        or      di,di
        jz      .bad
        clc
        jmp     .out
.bad:
        stc
.out:
        pop     di
        pop     si
        ret

; ZF set if the bank that plays starts with the same bytes
; ES:0 = bank, CX = bytes
fix_same:
        push    si
        push    di
        push    cx
        push    es
        mov     ax,[bank_ptr+2]
        or      ax,ax
        jz      .differ
        push    ax
        kernel  GlobalSize
        pop     es
        pop     cx
        push    cx
        push    es
        or      dx,dx
        jnz     .cmp
        cmp     ax,cx
        jb      .differ
.cmp:
        push    ds
        mov     ax,[bank_ptr+2]
        mov     ds,ax
        xor     si,si
        xor     di,di
        cld
        repe    cmpsb
        pop     ds
        jmp     .out
.differ:
        or      sp,sp                   ; ZF clear
.out:
        pop     es
        pop     cx
        pop     di
        pop     si
        ret

; checksum of a bank, to tell whether the file changed between two checks
; ES:0 = bank, CX = bytes; DX:AX = sum
fix_sum:
        push    si
        push    cx
        xor     ax,ax
        xor     dx,dx
        xor     bx,bx
        xor     si,si
.next:
        mov     bl,[es:si]
        inc     si
        add     ax,bx
        add     dx,ax
        loop    .next
        pop     cx
        pop     si
        ret

; make the bank in block BX the one that plays and free the old block
; CF if it can't be page-locked
fix_install:
        push    si
        push    di
        mov     si,bx                   ; SI = new block
        mov     di,[bank_locks]
        or      di,di
        jz      .swap
        ; a program has the device open, so the bank that plays is
        ; page-locked for interrupt time: lock this one the same way
        ; (bank_lock)
        push    si
        kernel  GlobalWire
        push    si
        kernel  GlobalPageLock
        or      ax,ax
        jnz     .swap
        push    si
        kernel  GlobalUnWire
        stc
        jmp     .out
.swap:
        ; messages that come meanwhile wait in the queue
        call    fix_enter
        pushf
        cli
        pop     dx
        mov     ax,[bank_ptr+2]
        mov     [bank_ptr+2],si
        mov     word [bank_ptr],0
        call    restore_if
        mov     si,ax                   ; SI = old block
        call    fix_unlock
        or      si,si
        jz      .done
        or      di,di
        jz      .free
        push    si
        kernel  GlobalPageUnlock
        or      ax,ax
        jnz     .free
        push    si
        kernel  GlobalUnWire
.free:
        push    si
        kernel  GlobalFree
.done:
        clc
.out:
        pop     di
        pop     si
        ret

; put the bank built into the driver back (resource 256, 1234)
; CF if it can't
fix_builtin:
        push    bp
        mov     bp,sp
        push    si
        push    di
        sub     sp,6                    ; [bp-6] bytes, [bp-8] block,
                                        ; [bp-10] resource data
        push    word [hinstance]
        push    word 0
        push    word 1234
        push    word 0
        push    word 256
        kernel  FindResource
        or      ax,ax
        jz      .fail
        mov     si,ax                   ; SI = resource
        push    word [hinstance]
        push    si
        kernel  SizeofResource
        or      dx,dx
        jnz     .fail
        cmp     ax,BANK_TABLE
        jb      .fail
        cmp     ax,FILE_MAX
        ja      .fail
        mov     [bp-6],ax
        push    word GMEM_BANK
        push    word 0
        push    ax
        kernel  GlobalAlloc
        or      ax,ax
        jz      .fail
        mov     [bp-8],ax
        push    word [hinstance]
        push    si
        kernel  LoadResource
        or      ax,ax
        jz      .freeblk
        mov     [bp-10],ax
        push    ax
        kernel  LockResource
        or      dx,dx
        jz      .freeres
        push    ds
        mov     es,[bp-8]
        mov     si,ax
        mov     ds,dx
        xor     di,di
        mov     cx,[bp-6]
        cld
        rep     movsb
        pop     ds
        push    word [bp-10]
        kernel  FreeResource
        mov     es,[bp-8]
        mov     cx,[bp-6]
        call    fix_same
        je      .same
        mov     bx,[bp-8]
        call    fix_install
        jc      .freeblk
        jmp     .ok
.same:
        push    ds
        pop     es
        push    word [bp-8]
        kernel  GlobalFree
.ok:
        clc
        jmp     .out
.freeres:
        push    word [bp-10]
        kernel  FreeResource
.freeblk:
        push    ds
        pop     es
        push    word [bp-8]
        kernel  GlobalFree
.fail:
        stc
.out:
        lea     sp,[bp-4]
        pop     di
        pop     si
        pop     bp
        ret

; ZF clear if a program has the device open (any device of device_list)
fix_any_open:
        mov     bx,[device_list]
.next:
        or      bx,bx
        jz      .out
        cmp     word [bx+DEV_OPEN],0
        jne     .out
        mov     bx,[bx+DEV_NEXT]
        jmp     .next
.out:
        ret

; start the task that checks the bank file, if it isn't running (after a
; MODM_OPEN)
fix_watch_start:
        mov     ax,[fix_bwatch]
        cmp     ax,WATCH_STARTING
        je      .out
        cmp     ax,WATCH_RUNNING
        je      .out
        mov     word [fix_quit],0
        mov     word [fix_bwatch],WATCH_STARTING
        push    cs
        push    word fix_task_proc
        push    ds
        push    word fix_tasknew
        push    word 0
        push    word 0
        mmsystem mmTaskCreate
        or      ax,ax
        jz      .out
        mov     [fix_bwerr],ax
        mov     word [fix_bwatch],WATCH_FAILED
.out:
        ret

; end the task and wait for it, so it can't run code of a driver that is
; being unloaded (DRV_DISABLE)
fix_watch_stop:
        push    si
        mov     ax,[fix_bwatch]
        cmp     ax,WATCH_STARTING
        je      .stop
        cmp     ax,WATCH_RUNNING
        jne     .out
.stop:
        mov     word [fix_quit],1
        mov     si,200
.wait:
        mov     ax,[fix_task]
        or      ax,ax
        jz      .yield
        push    ax
        mmsystem mmTaskSignal
.yield:
        kernel  Yield
        mov     ax,[fix_bwatch]
        cmp     ax,WATCH_STARTING
        je      .again
        cmp     ax,WATCH_RUNNING
        jne     .out
.again:
        dec     si
        jnz     .wait
        ; it didn't end: at least stop its timer
        mov     ax,[fix_timer]
        or      ax,ax
        jz      .out
        push    ax
        mmsystem timeKillEvent
        mov     word [fix_timer],0
.out:
        pop     si
        ret

; the task (mmTaskCreate): wakes up every second from fix_tick, and checks
; the bank file while a program has the device open
; void FAR PASCAL fix_task_proc(DWORD dwInst)
fix_task_proc:
        push    bp
        mov     bp,sp
        push    ds
        push    si
        push    di
        movsel  ax, ..@FIX_DS2, 0xFFFF
        mov     ds,ax
        kernel  GetCurrentTask
        mov     [fix_task],ax
        cmp     word [fix_quit],0
        jne     .quit
        push    word WATCH_MS
        push    word 100
        push    cs
        push    word fix_tick
        push    word 0
        push    word 0
        push    word TIME_PERIODIC
        mmsystem timeSetEvent
        mov     [fix_timer],ax
        or      ax,ax
        jnz     .run
        mov     word [fix_bwerr],0xFFFF
        mov     word [fix_bwatch],WATCH_FAILED
        jmp     .gone
.run:
        mov     word [fix_bwatch],WATCH_RUNNING
.wait:
        cmp     word [fix_quit],0
        jne     .quit
        push    word [fix_task]
        mmsystem mmTaskBlock
        cmp     word [fix_quit],0
        jne     .quit
        call    fix_any_open
        jz      .wait
        mov     ax,1
        call    fix_bank_poll
        jmp     .wait
.quit:
        mov     ax,[fix_timer]
        or      ax,ax
        jz      .stopped
        push    ax
        mmsystem timeKillEvent
        mov     word [fix_timer],0
.stopped:
        mov     word [fix_bwatch],WATCH_OFF
.gone:
        mov     word [fix_task],0
        pop     di
        pop     si
        pop     ds
        pop     bp
        retf    4

; timer callback (interrupt time): wake the task up
; void FAR PASCAL fix_tick(UINT id, UINT msg, DWORD user, DWORD dw1, DWORD dw2)
fix_tick:
        push    bp
        mov     bp,sp
        push    ds
        movsel  ax, ..@FIX_DS3, 0xFFFF
        mov     ds,ax
        mov     ax,[fix_task]
        or      ax,ax
        jz      .out
        push    ax
        mmsystem mmTaskSignal
.out:
        pop     ds
        pop     bp
        retf    16
