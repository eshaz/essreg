; ESFM_FIX data, appended to DGROUP.  essctl finds it by the signature.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

fix_sig:        db 'ESFMFIX', 0
fix_version:    dw 1
fix_lock:       dw 0            ; nonzero while a call holds the driver
q_head:         dw 0            ; next entry to handle
q_tail:         dw 0            ; next free entry
fix_queued:     dd 0            ; messages that arrived while the driver was held
fix_overflow:   dd 0            ; messages refused: queue full
fix_maxdepth:   dw 0            ; most entries waiting at once
fix_purged:     dw 0            ; dropped because their client closed
fix_qsize:      dw QSIZE
fix_queue:      times QSIZE * QENTRY db 0
