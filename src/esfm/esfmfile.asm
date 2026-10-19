; Bank file for ESFM.DRV, assembled when ESFM_FIX=1.  The code is appended
; to segment 1 (fixed code).
;
; Notes:
;
; SYSTEM.INI [ESFM.DRV] Bank= names a patch bank file, raw or RIFF "Ptch"
; with an "fm4 " chunk (the files esfmpat takes).  The driver plays that
; bank instead of the one built into ESFM.DRV:
; - the file is checked when a program opens the device (MODM_OPEN), and
;   read only if its name, date or time changed since it was last read
; - a file that is missing or isn't a patch bank keeps the bank that plays
; - without Bank= the driver goes back to its built-in bank
;
; A new bank goes into a new block, swapped in while the driver is held.
; The status is in DGROUP after the ESFMFIX counters (esfmfixd.asm), for
; essctl.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; module references, in the order of layout.json
KERNEL          equ 1

%define KERNEL_GlobalAlloc 15
%define KERNEL_GlobalFree 17
%define KERNEL_FindResource 60
%define KERNEL_LoadResource 61
%define KERNEL_LockResource 62
%define KERNEL_FreeResource 63
%define KERNEL_SizeofResource 65
%define KERNEL_DOS3Call 102
%define KERNEL_GlobalWire 111
%define KERNEL_GlobalUnWire 112
%define KERNEL_GetPrivateProfileInt 127
%define KERNEL_GetPrivateProfileString 128
%define KERNEL_GlobalPageLock 191
%define KERNEL_GlobalPageUnlock 192

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

%macro fix_file_relocs 0
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

; fix_bstate
BS_NONE         equ 0           ; no bank file plays
BS_LOADED       equ 1           ; the file's bank plays
BS_MISSING      equ 2           ; the file can't be opened or read
BS_BAD          equ 3           ; the file isn't a patch bank
BS_NOMEM        equ 4           ; no memory for it

; DRV_ENABLE (DriverProc calls this instead of bank_load): SYSTEM.INI's
; settings the first time (esfmini.asm), the built-in bank, and forget the
; file that was read, so the next MODM_OPEN reads it again
; DX:AX from bank_load
fix_drv_enable:
        call    fix_read_settings
        callf   bank_load, ..@FIX_S1A, 0xFFFF
        mov     word [fix_bsrc],0
        mov     word [fix_bstate],BS_NONE
        mov     byte [fix_bkey],0
        ; BetterSquareWave=0: ESS's bank (resource 1235) instead of the one
        ; bank_load put in (1234)
        test    word [fix_opts],OPT_SQUARE
        jnz     .done
        push    ax
        push    dx
        call    fix_builtin
        pop     dx
        pop     ax
.done:  retf

; MODM_OPEN, before ESS's code: load the bank file if it changed
; nothing is checked while a program has the device open, that open is
; refused and its bank stays
fix_bank_check:
        cmp     word [fix_polling],0    ; a file call of another check yielded
        jne     .busy
        call    fix_any_open
        jnz     .busy
        mov     word [fix_polling],1
        push    bp
        mov     bp,sp
        push    si
        push    di
        sub     sp,6                    ; [bp-6] file, [bp-8] date,
                                        ; [bp-10] time
        inc     word [fix_bchecks]
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
        mov     byte [fix_bkey],0
        cmp     word [fix_bsrc],0
        je      .done
        call    fix_builtin
        jc      .done
        mov     word [fix_bsrc],0
        mov     word [fix_blen],0
        jmp     .done
.file:
        call    fix_open
        jnc     .opened
        mov     word [fix_bstate],BS_MISSING
        jmp     .done
.opened:
        mov     [bp-6],ax
        mov     bx,ax
        mov     ax,0x5700               ; date and time of the last write
        kernel  DOS3Call                ; CX = time, DX = date
        jc      .ioerr
        mov     [bp-8],dx
        mov     [bp-10],cx
        ; the file read last time, not written since: nothing to do
        call    fix_same_file
        jne     .changed
        cmp     dx,[fix_bdate]
        jne     .changed
        cmp     cx,[fix_btime]
        jne     .changed
        mov     ax,[fix_bkstate]
        mov     [fix_bstate],ax
        jmp     .close
.changed:
        mov     bx,[bp-6]
        call    fix_read                ; SI = block, CX = bytes
        jnc     .read
        mov     [fix_bstate],ax
        jmp     .close
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
        push    cx
        mov     bx,si
        call    fix_install
        pop     cx
        jc      .nomem
        mov     word [fix_bsrc],1
        mov     [fix_blen],cx
        inc     word [fix_bloads]
        mov     ax,BS_LOADED
        call    .remember
        jmp     .close
.nomem:
        mov     word [fix_bstate],BS_NOMEM
        jmp     .free
.bad:
        ; remembered too, so it isn't read again until it changes
        mov     ax,BS_BAD
        call    .remember
.free:
        ; no segment register may hold a selector that is freed: an
        ; interrupt handler that pops it would fault
        push    ds
        pop     es
        push    si
        kernel  GlobalFree
        jmp     .close
.ioerr:
        mov     word [fix_bstate],BS_MISSING
.close:
        mov     ah,0x3E
        mov     bx,[bp-6]
        kernel  DOS3Call
.done:
        lea     sp,[bp-4]
        pop     di
        pop     si
        pop     bp
        mov     word [fix_polling],0
        ; and the caller gets no freed selector in ES either
        push    ds
        pop     es
.busy:
        ret

; AX = what the file turned out to be, kept with its name, date and time
.remember:
        mov     [fix_bstate],ax
        mov     [fix_bkstate],ax
        mov     ax,[bp-8]
        mov     [fix_bdate],ax
        mov     ax,[bp-10]
        mov     [fix_btime],ax
        push    si
        push    di
        push    es
        push    ds
        pop     es
        mov     si,fix_bpath
        mov     di,fix_bkey
        mov     cx,FIX_PATH
        cld
        rep     movsb
        pop     es
        pop     di
        pop     si
        ret

; ZF set if Bank= (fix_bpath) names the file read last time (fix_bkey)
fix_same_file:
        push    si
        push    di
        push    es
        push    ds
        pop     es
        mov     si,fix_bpath
        mov     di,fix_bkey
        cmp     byte [di],0
        je      .differ
.next:
        lodsb
        scasb
        jne     .out
        or      al,al
        jnz     .next
        jmp     .out
.differ:
        or      sp,sp                   ; ZF clear
.out:
        pop     es
        pop     di
        pop     si
        ret

; open the file named in fix_bpath to read it: AX = handle, or CF
fix_open:
        push    si
        push    di
        ; the long file name call first (Windows 95)
        mov     ax,0x716C
        mov     bx,0x2040               ; read only, deny none, errors
                                        ; returned: no "not ready" box
        xor     cx,cx
        mov     dx,0x0001               ; open, fail if missing
        mov     si,fix_bpath
        xor     di,di
        stc
        kernel  DOS3Call
        jc      .nolfn
        cmp     ax,0x7100               ; unknown call, and the carry clear
        je      .dos
        clc
        jmp     .out
.nolfn:
        cmp     ax,0x7100               ; no long file names
        stc
        jne     .out
.dos:
        mov     ax,0x6C00               ; the same without long names
        mov     bx,0x2040
        xor     cx,cx
        mov     dx,0x0001
        mov     si,fix_bpath
        kernel  DOS3Call
.out:
        pop     di
        pop     si
        ret

; read the open file BX into a new block
; SI = block, CX = bytes, or CF and AX = BS_MISSING, BS_BAD or BS_NOMEM
fix_read:
        push    di
        push    bx
        mov     ax,0x4202               ; the size
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
        pop     bx
        push    bx
        mov     ax,0x4200
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
        mov     si,ax
        pop     bx
        push    bx
        push    ds
        mov     ds,si
        mov     ah,0x3F
        mov     cx,di
        xor     dx,dx
        kernel  DOS3Call
        pop     ds
        jc      .rderr
        cmp     ax,di
        jne     .rderr                  ; it got shorter
        mov     cx,di
        clc
        jmp     .out
.rderr:
        push    si
        kernel  GlobalFree
.ioerr:
        mov     ax,BS_MISSING
        jmp     .fail
.toobig:
        mov     ax,BS_BAD
        jmp     .fail
.nomem:
        mov     ax,BS_NOMEM
.fail:
        stc
.out:
        pop     bx
        pop     di
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

; make the bank in block BX the one that plays and free the old block
; CF if it can't be page-locked
fix_install:
        push    si
        push    di
        mov     si,bx                   ; SI = new block
        mov     di,[bank_locks]
        or      di,di
        jz      .swap
        ; the bank that plays is page-locked for interrupt time (bank_lock):
        ; lock this one the same way
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
        push    ds                      ; ES may hold the old block
        pop     es
        push    si
        kernel  GlobalFree
.done:
        clc
.out:
        pop     di
        pop     si
        ret

; put the bank built into the driver back: resource 256, 1234, or ESS's
; bank, 1235, with BetterSquareWave=0
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
        mov     ax,BANK_ID
        test    word [fix_opts],OPT_SQUARE
        jnz     .id
        mov     ax,BANK_ESS_ID
.id:    push    ax
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
        mov     bx,[bp-8]
        call    fix_install
        jc      .freeblk
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
