; Segment lengths and exported offsets for tools/nelink.py.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

        db 'NELINK', 1, 0
        dw seg1_data_end, seg1_end
        dw seg2_data_end, seg2_end
        dw seg3_data_end, seg3_end
        dw seg4_data_end, seg4_end
        dw WEP
        dw DriverProc
        dw modMessage
        dw ___EXPORTEDSTUB
        dw LibEntry
