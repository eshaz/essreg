; lengths and exported offsets for tools/nelink.py

        db 'NELINK', 1, 0
        dw seg1_data_end, seg1_end
        dw seg2_data_end, seg2_end
        dw seg3_data_end, seg3_end
        dw seg4_data_end, seg4_end
        dw seg5_data_end, seg5_end
        dw seg6_data_end, seg6_end
        dw seg7_data_end, seg7_end
        dw L2_0000
        dw L4_02DA
%if ES1869_FIX
        dw es_wod_message
%else
        dw L6_18E4
%endif
%if ES1869_FIX
        dw es_wid_message
%else
        dw L6_0FF2
%endif
        dw L6_1D1C
        dw L5_1BA4
        dw L1_22F4
        dw L1_1ED4
