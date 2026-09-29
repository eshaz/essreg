; ESFM_FIX data, appended to DGROUP.  essctl finds it by the signature.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

fix_sig:        db 'ESFMFIX', 0
fix_version:    dw 4            ; 2: the bank file, 4: fix_opts.  3 was
                                ; an earlier build's pedal times
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
fix_bchecks:    dw 0            ; times the file was checked (MODM_OPEN)
fix_bdate:      dw 0            ; DOS date and time of the file read last
fix_btime:      dw 0
fix_bpath:      times FIX_PATH db 0     ; Bank= from SYSTEM.INI

fix_bkey:       times FIX_PATH db 0     ; the file read last, "" for none
fix_bkstate:    dw 0            ; and what it was, BS_LOADED or BS_BAD
fix_polling:    dw 0            ; a check is running
fix_opts:       dw OPT_DEFAULT  ; OPT_*: the changes that are on

; SYSTEM.INI keys (esfmini.asm): the key, its fix_opts bit
fix_settings:
        dw fix_key_queue, OPT_QUEUE
        dw fix_key_silence, OPT_SILENCE
        dw fix_key_pedal, OPT_PEDAL
        dw fix_key_vibrato, OPT_VIBRATO
        dw fix_key_tuning, OPT_TUNING
        dw fix_key_cc121, OPT_CC121
        dw fix_key_pan, OPT_LIVE_PAN
        dw fix_key_sysex, OPT_SYSEX
        dw fix_key_running, OPT_RUNNING
        dw fix_key_square, OPT_SQUARE
        dw 0
fix_key_queue:   db 'QueueWhileBusy', 0
fix_key_silence: db 'SilenceOnClose', 0
fix_key_pedal:   db 'PedalRelease', 0
fix_key_vibrato: db 'Vibrato', 0
fix_key_tuning:  db 'Tuning', 0
fix_key_cc121:   db 'ResetControllers', 0
fix_key_pan:     db 'LivePan', 0
fix_key_sysex:   db 'SysEx', 0
fix_key_running: db 'RunningStatus', 0
fix_key_square:  db 'BetterSquareWave', 0
fix_ini_sect:   db 'ESFM.DRV', 0
fix_ini_key:    db 'Bank', 0
fix_ini_file:   db 'SYSTEM.INI', 0
fix_nul:        db 0
fix_gs_tail:    db 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7
fix_xg_tail:    db 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7

fix_queue:      times QSIZE * QENTRY db 0
