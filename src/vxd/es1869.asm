; Reassemblable source of ES1869.VXD, the Windows 95 driver of the ES1869.
; ESSREG_EXT=0 reproduces the original ESS driver byte for byte,
; ESSREG_EXT=1 adds the essreg register API (src/vxd/essext.asm).
;
; Usage:
;   `python3 tools/build_vxd.py`           (extended driver)
;   `python3 tools/build_vxd.py --stock`   (identical to the original)
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

%ifndef ESSREG_EXT
%define ESSREG_EXT 1
%endif

%include "vxd.inc"
%if ESSREG_EXT
%include "essext.inc"
%endif

%include "lcod.asm"
%include "mcod.asm"
%include "rare.asm"
%include "pnp.asm"
%include "pcod.asm"
%include "pdat.asm"
%include "icod.asm"

%if ESSREG_EXT
%include "essext.asm"
%endif
