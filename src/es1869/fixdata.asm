; Data of the changes to ES1869.DRV, assembled when ES1869_FIX=1 and
; appended to its data segment (DGROUP).
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

es_opts:        dw OPT_DEFAULT          ; OPT_*: the changes that are on

; SYSTEM.INI keys (settings.asm): the key, its es_opts bit
es_settings:
        dw es_key_4x, OPT_A2_4X
        dw es_key_filter, OPT_A2_FILTER
        dw 0
es_section:     db "ES1869.DRV", 0
es_ini_file:    db "SYSTEM.INI", 0
es_key_4x:      db "Audio2Oversampling", 0
es_key_filter:  db "Audio2Filter", 0
