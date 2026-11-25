; ESFM_FIX data, appended to DGROUP.  essctl finds it by the signature.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

fix_sig:        db 'ESFMFIX', 0
fix_version:    dw 7            ; 2: the bank file, 4: fix_opts, 5: the
                                ; OPL3 device's DEV_OPL_READY, 6: its bank
                                ; file, 7: its headroom and MpcSongs.  3
                                ; was an earlier build's pedal times
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

; the OPL3 device's bank file, Opl3Bank= (esfmfile.asm), from version 6
fix_ostate:     dw 0            ; BS_*, as fix_bstate
fix_osrc:       dw 0            ; 1: the bank that plays came from the file
fix_oloads:     dw 0            ; times the file was loaded
fix_ochecks:    dw 0            ; times the file was checked (MODM_OPEN)
fix_odate:      dw 0            ; DOS date and time of the file read last
fix_otime:      dw 0
fix_opath:      times FIX_PATH db 0     ; Opl3Bank= from SYSTEM.INI
fix_okey:       times FIX_PATH db 0     ; the file read last, "" for none
fix_okstate:    dw 0            ; and what it was, BS_LOADED or BS_BAD
fix_opl3_sel:   dw 0            ; the OPL3 bank that plays: its selector,
fix_opl3_off:   dw 0            ; CS for the built-in one, and offset
fix_opl3_blk:   dw 0            ; the file's block, 0 for the built-in bank

; from version 7: Opl3Headroom= in the chip's 0.75 dB steps (esfmini.asm),
; and the notes of base-level copies that MpcSongs left out (esfmmpc.asm)
fix_opl3_att:   dw OPL3_ATT_DEFAULT
fix_mpc_drops:  dw 0

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
        dw fix_key_sides, OPT_SIDES
        dw fix_key_opl3, OPT_OPL3
        dw fix_key_settings, OPT_SETTINGS
        dw fix_key_mpc, OPT_MPC
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
fix_key_sides:   db 'TrueSides', 0
fix_key_opl3:    db 'Opl3Device', 0
fix_key_settings: db 'BankSettings', 0
fix_key_mpc:     db 'MpcSongs', 0
fix_key_headroom: db 'Opl3Headroom', 0
fix_ini_sect:   db 'ESFM.DRV', 0
fix_ini_key:    db 'Bank', 0
fix_ini_okey:   db 'Opl3Bank', 0
fix_ini_file:   db 'SYSTEM.INI', 0
fix_nul:        db 0
fix_gs_tail:    db 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7
fix_xg_tail:    db 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7

fix_queue:      times QSIZE * QENTRY db 0

; Opl3Device=2 (esfmopl.asm): whether the first count decided who serves
; device 1, and the module that may
fix_opl3_latched: db 0
fix_opl3_mod:   db 'ESFMOPL3', 0

; the Settings box of the patch banks (esfmcfg.asm)
fix_cfg_dll:    db 'ESFMCFG.DLL', 0
fix_cfg_proc:   db 'BANKDIALOG', 0
fix_cfg_ptr:    dd 0
