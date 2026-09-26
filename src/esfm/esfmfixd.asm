; ESFM_FIX data, appended to DGROUP.  essctl finds it by the signature.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

fix_sig:        db 'ESFMFIX', 0
fix_version:    dw 2            ; 2: with the bank file fields
fix_lock:       dw 0            ; nonzero while a call holds the driver
q_head:         dw 0            ; next entry to handle
q_tail:         dw 0            ; next free entry
fix_queued:     dd 0            ; messages that arrived while the driver was held
fix_overflow:   dd 0            ; messages refused: queue full
fix_maxdepth:   dw 0            ; most entries waiting at once
fix_purged:     dw 0            ; dropped because their client closed
fix_qsize:      dw QSIZE

; the bank file (esfmfile.asm)
fix_bstate:     dw 0            ; BS_* in esfmfile.asm
fix_bsrc:       dw 0            ; 1: the bank that plays came from the file
fix_blen:       dw 0            ; bytes of the file's bank that plays
fix_bloads:     dw 0            ; times the file was loaded
fix_bpolls:     dw 0            ; times the file was checked
fix_bwatch:     dw 0            ; WATCH_* in esfmfile.asm
fix_bwerr:      dw 0            ; mmTaskCreate error, FFFFh: no timer
fix_bpath:      times FIX_PATH db 0     ; Bank= from SYSTEM.INI
fix_task:       dw 0            ; the task that watches the file
fix_timer:      dw 0            ; its timer
fix_tasknew:    dw 0            ; handle from mmTaskCreate
fix_quit:       dw 0            ; tells the task to end
fix_polling:    dw 0            ; a check is running
fix_bpsum:      dd 0            ; a change seen by one check: checksum
fix_bpsize:     dw 0            ; and bytes, 0 if none
fix_ini_sect:   db 'ESFM.DRV', 0
fix_ini_key:    db 'Bank', 0
fix_ini_file:   db 'SYSTEM.INI', 0
fix_nul:        db 0

fix_queue:      times QSIZE * QENTRY db 0
