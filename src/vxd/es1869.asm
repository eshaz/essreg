; ES1869.VXD -- reassemblable source
;
; Build:  python3 tools/build_vxd.py            (extended driver)
;         python3 tools/build_vxd.py --stock    (identical to the original)
;
; ESSREG_EXT=0 reproduces the original ESS driver byte for byte;
; ESSREG_EXT=1 adds the essreg register API (src/vxd/essext.asm).

%ifndef ESSREG_EXT
%define ESSREG_EXT 1
%endif

%include "vxd.inc"

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
