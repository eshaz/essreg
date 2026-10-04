; Fields of the AUDDRV device instance (ADI) structure.
;
; Notes:
;
; ES1869.VXD allocates one ADI per ES1869 devnode (ADI_Create, o4:01FC)
; and keeps it in the list ADI_List.  API function 0001 copies it to the
; caller.  Only the fields below are known for sure (see
; docs/VXD_INTERNALS.md).
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

%ifndef ADI_INC
%define ADI_INC

ADI_Flags               equ 0x04    ; word: 0020h no-DMA (PIO) emulation, 2000h MPU IRQ shared
ADI_AudioBase           equ 0x06    ; word: Audio_Base (e.g. 220h)
ADI_FMBase              equ 0x08    ; word: the FM port ESS uses (Audio_Base), FFFFh if none
ADI_FMAlias             equ 0x0A    ; word: FM alias base (e.g. 388h), negative if none
ADI_MPUBase             equ 0x0C    ; word: MPU-401 port (e.g. 330h), FFFFh if none
ADI_MPUIRQ              equ 0x0E    ; byte
ADI_DSPOwner            equ 0x35    ; dword: VM handle that owns the DSP, 0 = none
ADI_DSPLastOwner        equ 0x39    ; dword: previous DSP owner
ADI_FMOwner             equ 0x3D    ; dword: VM handle that owns the FM synthesizer
ADI_FMLastOwner         equ 0x41
ADI_MPUOwner            equ 0x45    ; dword: VM handle that owns the MPU-401
ADI_MPULastOwner        equ 0x49
ADI_DevNode             equ 0x55    ; dword: CONFIGMG devnode
ADI_DMA2Handle          equ 0x7A    ; dword: VDMAD handle of Audio 2's channel

%endif
