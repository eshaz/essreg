; Data of the changes to ES1869.DRV, assembled when ES1869_FIX=1 and
; appended to its data segment (DGROUP).
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

; essctl finds the settings by this (src/wavestat.c); a layout change
; bumps the version
es_status:      db "ESDRVFIX", 0, 0
                dw 1
es_opts:        dw OPT_DEFAULT          ; OPT_*: the changes that are on

; SYSTEM.INI keys (settings.asm): the key, its es_opts bit
es_settings:
        dw es_key_a1dev, OPT_A1_DEVICE
        dw es_key_shared, OPT_A1_SHARED
        dw es_key_a1filter, OPT_A1_FILTER
        dw es_key_dual, OPT_DUAL
        dw es_key_4x, OPT_A2_4X
        dw es_key_filter, OPT_A2_FILTER
        dw 0
es_section:     db "ES1869.DRV", 0
es_ini_file:    db "SYSTEM.INI", 0
es_key_a1dev:   db "Audio1Device", 0
es_key_shared:  db "SharedWaveOut", 0
es_key_a1filter: db "Audio1Filter", 0
es_key_dual:    db "DualPlayback", 0
es_key_4x:      db "Audio2Oversampling", 0
es_key_filter:  db "Audio2Filter", 0

; the Audio 1 device's name, Audio_Base in hex and ")" follow
a1_name:        db "ESS AudioDrive Audio 1 (", 0

; 4-channel frames on their way to the two rings (a1play.asm)
a1_bounce:      times 2 * A1_SPLIT db 0
