; ESFM.DRV -- reassemblable source (tools/ne2asm.py)
;
; Build: python3 tools/build_esfm.py [--stock]

%ifndef ESFM_FIX
%define ESFM_FIX 1
%endif

        bits 16
%include "ne16.inc"

        section seg1 progbits start=0 vstart=0 align=1
%include "seg1.asm"

        section seg2 progbits follows=seg1 vstart=0 align=1
%include "seg2.asm"

        section seg3 progbits follows=seg2 vstart=0 align=1
%include "seg3.asm"

        section seg4 progbits follows=seg3 vstart=0 align=1
%include "seg4.asm"

        section link progbits follows=seg4 vstart=0 align=1
%include "link.inc"
