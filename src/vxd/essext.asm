; Adds the essreg register API to ES1869.VXD, as group 4 of the V86/PM API
; of the AUDDRV device (INT 2Fh AX=1684h BX=3B07h returns the entry point),
; and makes DOS programs first-class users of the chip. It's only assembled
; when ESSREG_EXT=1. The stock driver answers DH=4 with CF set, which is
; how programs detect it.
;
; Calling convention (same as the ESS functions):
;   DX  = function (DH = 4, DL = index)
;   ECX = devnode of the ES1869 (offset 55h of the structure returned by
;         function 0001)
;   AX, BX = inputs as listed, ES:DI = buffer for 040B
;   CF clear on success, CF set and AX = ESSREG_E_* on failure
;
;   0400  extension info       -> AX = version, BX = feature bits,
;                                 DX = number of functions
;   0401  read mixer register  BL = register              -> AL = value
;   0402  write mixer register BL = register, BH = value
;   0403  read controller reg  BL = A0h-BFh               -> AL = value
;   0404  write controller reg BL = A0h-BFh, BH = value
;   0405  read audio port      BL = offset 0-Fh           -> AL = value
;   0406  write audio port     BL = offset 0-Fh, BH = value
;   0407  read config port     BL = offset 0-7            -> AL = value
;   0408  write config port    BL = offset 0-7 (not 2-4), BH = value
;   0409  read PnP register    BL = LDN (FFh = card), BH = register -> AL
;   040A  write PnP register   BL = LDN (FFh = card), BH = register, AL = value
;   040B  read mixer 00h-7Fh   ES:DI -> 128 bytes (register 40h reads as 0)
;   040C  owner information    -> AL/AH = DSP/FM owner, BL = MPU owner
;                                 (0 none, 1 caller's VM, 2 another VM),
;                                 BH = Audio_Base+Ch status, DX = ADI flags
;   040D  take the DSP for a recording: a DOS program gives it up and gets a
;         virtual Sound Blaster (Windows only)
;
; Notes:
;
; Every port pair runs with interrupts disabled and puts back the mixer
; index (Audio_Base+4) or the PnP index and logical device number.  That
; way an access can't interleave with ES1869.DRV, a DOS program in another
; VM or the VxD's own interrupt-time code.
;
; Nothing here acquires the device, so the DSP is never reset and no
; "in use" message shows up.  What would disturb a VM that owns the DSP or
; FM is refused while another VM owns it: controller registers, the DSP
; data, reset and FIFO ports, and writes to the FM ports.  0408 never
; writes the EEPROM ports (Config_Base+2-4: erase all and write all).
;
; DOS programs (docs/VXD_INTERNALS.md, "DOS boxes"):
;   - FM detection always succeeds.  A VM that can't have the FM chip gets
;     a virtual one: the OPL3 registers of both banks, the timers and the
;     status port, ESFM native mode and its readback.  When the chip is
;     free, the VM's next FM access takes it, and its virtual registers go
;     to the chip first.
;   - Windows gets FM from a port access only until a DOS program wants it.
;     ESFM.DRV's MIDI (0102) still keeps it.
;   - a DOS VM taking back the FM chip it left finds its state still there
;   - a DOS FM owner has the music DAC and, if 36h was 00h, FM volume FFh
;   - Windows' mixer is saved when a DOS VM takes the audio device, and put
;     back when it lets go.  When Windows next plays a sound or changes the
;     mixer (0002, 0102, 0302), FM left by a DOS program is reset.
;   - a recording in Windows takes the DSP from a DOS program (040D), which
;     goes on with a silent virtual Sound Blaster, its FM still recorded
;   - a real-mode program's FM plays on a clock that follows its timer
;     ticks, a few milliseconds late but evenly, whatever bursts Windows
;     hands the ticks over in (DosFMDelay)
;
; DirectSound (docs/VXD_INTERNALS.md, "DirectSound"): with
; DirectSoundLock=1, off by default, on a 16-bit DMA channel the driver
; copies what DirectSound wrote into the channel's buffer as it is
; written, in Unlock, in place of ESS's time-out that copies 80 ms every
; 20 ms. With DrqLatch=1, it sets the chip's DRQ latch each time a VM
; takes the DSP (docs/AUDIO_PIPELINE.md).
;
; A Sound Blaster 16 for DOS programs (SoundBlaster16, ESSREG_S16_*): a
; DOS VM that takes the DSP finds DSP version 4.05, and its Sound Blaster
; 16 transfers, 16-bit stereo included, play through the chip's Extended
; mode, by DMA, with its 16-bit DMA channel's programming carried over to
; Audio 1's channel (docs/VXD_INTERNALS.md, "A Sound Blaster 16 for DOS
; programs").
;
; Settings (docs/DRIVER_CONFIG.md): each change can be turned off in
; SYSTEM.INI, [ES1869.VXD] and the Audio 2 mode in [ES1869.DRV], read once
; when the VxD starts (ESSREG_Read_Settings).  Off, the VxD does what ESS's
; does there.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

ESSREG_VERSION          equ 0x0118
ESSREG_FEATURES         equ 0xFFFF      ; mixer, controller, ports, config,
                                        ; PnP, mixer block, owner info,
                                        ; DOS FM, DOS mixer restore, settings,
                                        ; Audio 1 filter, recording takes DSP,
                                        ; DOS FM on its clock, DirectSound's
                                        ; Lock and Unlock, the DRQ latch, the
                                        ; Sound Blaster 16
FEATURE_DOS_FM          equ 0x0080      ; cleared while VirtualFM=0
FEATURE_DOS_MIXER       equ 0x0100      ; cleared while DosMixerRestore=0
FEATURE_DOS_A1          equ 0x0400      ; cleared while Audio1Filter=1
FEATURE_REC_DSP         equ 0x0800      ; cleared while RecordTakesDSP=0
FEATURE_FM_TIMED        equ 0x1000      ; cleared while DosFMDelay=0
FEATURE_DS_LOCK         equ 0x2000      ; cleared while DirectSoundLock=0
FEATURE_DRQ_LATCH       equ 0x4000      ; cleared while DrqLatch=0
FEATURE_SB16            equ 0x8000      ; cleared while SoundBlaster16=0

; settings (ESSREG_Opts), 1 = the change is on
OPT_API                 equ 0x0001      ; RegisterAPI: group 4
OPT_VFM                 equ 0x0002      ; VirtualFM: the virtual FM chip
OPT_TAKES_FM            equ 0x0004      ; DosTakesFM: Windows' soft FM gives way
OPT_KEEPS_FM            equ 0x0008      ; DosKeepsFM: no reset for a returning VM
OPT_FM_AUDIBLE          equ 0x0010      ; DosFMAudible: music DAC and FM volume
OPT_DOS_MIXER           equ 0x0020      ; DosMixerRestore: Windows' mixer back
OPT_RESET_FM            equ 0x0040      ; ResetDosFM: FM reset when Windows is back
OPT_REC_TAKES           equ 0x0080      ; RecordTakesDSP: a recording takes the DSP
OPT_A2_4X               equ 0x0100      ; Audio2Oversampling: ESS's 4x
OPT_A2_FILTER           equ 0x0200      ; Audio2Filter: the filter in use
OPT_A1_FILTER           equ 0x0400      ; Audio1Filter: the filter in use
OPT_FM_TIMED            equ 0x0800      ; DosFMDelay: DOS FM on its clock
OPT_DS_LOCK             equ 0x1000      ; DirectSoundLock: Lock and Unlock
OPT_DRQ_LATCH           equ 0x2000      ; DrqLatch: 25h bit 7 at each acquisition
OPT_SB16                equ 0x4000      ; SoundBlaster16: the front for DOS VMs
OPT_READ                equ 0x8000      ; SYSTEM.INI was read
OPT_DEFAULT             equ 0x48FF

S16_VERSION             equ 0x0405      ; the DSP version the front answers
S16_CHAN                equ 5           ; SoundBlaster16DMA's default
S16_POLL                equ 0x400       ; polls for a byte of the chip's
                                        ; answer, interrupts off
S16_PORTS               equ 0xD470      ; bit n = Audio_Base+n: the mixer,
                                        ; the reset, the read data, the
                                        ; command, the status and the 16-bit
                                        ; acknowledge ports stay trapped

FMQ_DELAY               equ 30          ; DosFMDelay's default (ms)
FMQ_DELAY_MAX           equ 250         ; and the most it takes
FMQ_WINDOW              equ 32          ; ticks the clock looks at at once
FMQ_TICK0               equ 54925       ; the timer's default tick (us)
FMQ_LATE                equ 4000        ; a data write this long (us) after a
                                        ; tick, and half a tick, is late
VMSTAT_PM_APP           equ 0x40        ; CB_VM_Status: a protected mode program

SYSSTATE_VXDINITCOMPLETED equ 0x40000000 ; VMM_GetSystemInitState: the profile
                                        ; services are gone from here on

ESSREG_E_NODEV          equ 1           ; ECX is not an ES1869 devnode
ESSREG_E_INUSE          equ 2           ; another VM owns the DSP or FM
ESSREG_E_PARAM          equ 3           ; register/offset out of range, bad buffer
ESSREG_E_BUSY           equ 4           ; DSP busy or powered down
ESSREG_E_TIMEOUT        equ 5           ; DSP returned no data
ESSREG_E_NOCFG          equ 6           ; configuration port not found

ESSREG_POLL_IDLE        equ 0x2000      ; polls for an idle DSP, interrupts on
ESSREG_POLL_BYTE        equ 0x200       ; polls for a command's first byte
ESSREG_POLL_REST        equ 0x2000      ; polls for the rest, once it started

; audio ports, bit n = Audio_Base+n
ESSREG_DSP_RD_PORTS     equ 0xC400      ; read data, IRQ acknowledge, FIFO
ESSREG_DSP_WR_PORTS     equ 0x9040      ; reset, command, FIFO
ESSREG_FM_PORTS         equ 0x030F      ; FM address and data

ESSREG_FM_VOLUME        equ 0xFF        ; 36h for a DOS FM owner, as 1869opl3

; Audio 2 mode (mixer 71h)
ESSREG_A2_4X            equ 0x10        ; 4x oversampling (and no filter)
ESSREG_A2_SCF_BYPASS    equ 0x08        ; the switched-capacitor filter bypassed
ESSREG_A1_SCF_BYPASS    equ 0x04        ; Audio 1's filter bypassed

; VDMAD_Get_Virt_State's mode (DL)
DMA_MASKED              equ 0x01        ; the channel is masked
DMA_16BIT               equ 0x40        ; VDMAD's extended mode: a word channel
DMA_TO_MEMORY           equ 0x04        ; a write transfer: device to memory
DMA_FROM_MEMORY         equ 0x08        ; a read transfer: memory to device

; virtual FM chip, one per VM and device, allocated zeroed on first use
VFM_Flags               equ 0x000       ; byte: VFM_*
VFM_Latch               equ 0x004       ; dword: address latch
VFM_T1Start             equ 0x008       ; dword: clock when timer 1 started
VFM_T2Start             equ 0x00C       ;        or last overflowed (us)
VFM_Status              equ 0x010       ; byte: status bits 7-5
VFM_Ctl                 equ 0x011       ; byte: 04h, timer starts and masks
VFM_T1                  equ 0x012       ; byte: 02h, timer 1 preset
VFM_T2                  equ 0x013       ; byte: 03h, timer 2 preset
VFM_Glob                equ 0x014       ; 4 bytes: native 408h, 4BDh, 501h, 504h
VFM_GlobMask            equ 0x018       ; byte: which of them were written
VFM_EmuMask             equ 0x020       ; 64 bytes: registers 000h-1FFh written
VFM_NatMask             equ 0x060       ; 76 bytes: native 000h-253h written
VFM_Emu                 equ 0x0B0       ; 512 bytes: registers 000h-1FFh
VFM_Nat                 equ 0x2B0       ; 596 bytes: native registers 000h-253h
VFM_SIZE                equ 0x504

VFM_NATIVE              equ 0x01        ; in ESFM native mode
VFM_USED                equ 0x02        ; written since it last went to the chip

VFM_NAT_KEYON           equ 0x240       ; native key-on registers 240h-253h
VFM_NAT_REGS            equ 0x254

; clock states
TSC_UNKNOWN             equ 0
TSC_NONE                equ 1           ; no time stamp counter
TSC_WAIT                equ 2           ; calibrating against the system time
TSC_READY               equ 3

global ESSREG_Rdtsc, ESSREG_Snap_Regs, ESSREG_Opts
global OPT_API, OPT_VFM, OPT_TAKES_FM, OPT_KEEPS_FM, OPT_FM_AUDIBLE
global OPT_DOS_MIXER, OPT_RESET_FM, OPT_A2_4X, OPT_A2_FILTER, OPT_A1_FILTER
global OPT_READ
global VFM_Flags, VFM_Latch, VFM_Status, VFM_Ctl, VFM_T1, VFM_T2
global VFM_EmuMask, VFM_NatMask, VFM_Emu, VFM_Nat, VFM_SIZE
global EX_Flags, EX_D1, EX_Snap, NODE_VFM

section LCOD

; --- helpers --------------------------------------------------------------

; EDI = ADI of the devnode in Client_ECX
; on failure: CF set, Client_AX = ESSREG_E_NODEV
ESSREG_Get_ADI:
        push    byte 0                  ; key type 0: devnode
        push    dword [ebp+Client_ECX]
        call    _AUDDRV_Get_pADI_From_XXX
        add     esp,byte 8
        or      edi,edi
        jz      .none
        clc
        ret
.none:  mov     ax,ESSREG_E_NODEV
        ; fall through

; return failure: Client_AX = AX, CF set
ESSREG_Fail:
        mov     [ebp+Client_EAX],ax
        stc
        ret

; CF set and Client_AX = ESSREG_E_INUSE when a VM other than the
; caller's (EBX) owns the DSP of the ADI in EDI
ESSREG_Check_DSP_Owner:
        mov     eax,[edi+ADI_DSPOwner]
        jmp     ESSREG_Check_Owner

; the same for FM
ESSREG_Check_FM_Owner:
        mov     eax,[edi+ADI_FMOwner]
ESSREG_Check_Owner:
        or      eax,eax
        jz      .ok
        cmp     eax,ebx
        jz      .ok
        mov     ax,ESSREG_E_INUSE
        jmp     ESSREG_Fail
.ok:    clc
        ret

; EDX = Audio_Base+Ch once the DSP is idle, waited for with interrupts on
; CF set and Client_AX = ESSREG_E_BUSY if it stays busy or ESS powered the
; chip down (it won't answer until ESS wakes it)
ESSREG_DSP_Idle:
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        test    word [edi+ADI_Flags],0x80
        jnz     .busy
        mov     ecx,ESSREG_POLL_IDLE
.wait:  in      al,dx
        test    al,0x80
        jz      .ready
        loop    .wait
.busy:  mov     ax,ESSREG_E_BUSY
        jmp     ESSREG_Fail
.ready: clc
        ret

; write AL to the DSP once it takes a byte, EDX = Audio_Base+Ch,
; ECX = poll limit; CF set if it stays busy, clobbers ECX
ESSREG_DSP_Put:
        push    eax
.wait:  in      al,dx
        test    al,0x80
        jz      .ready
        loop    .wait
        pop     eax
        stc
        ret
.ready: pop     eax
        out     dx,al
        clc
        ret

; wait for a DSP data byte and read it into AL, EDX = Audio_Base+Ch,
; ECX = poll limit
; reading Audio_Base+Eh clears the interrupt request, so poll bit 6 of
; Audio_Base+Ch instead (it mirrors the read-buffer flag)
ESSREG_DSP_Get:
.wait:  in      al,dx
        test    al,0x40
        jnz     .ready
        loop    .wait
        stc
        ret
.ready: sub     edx,byte 2              ; Audio_Base+Ah
        in      al,dx
        add     edx,byte 2
        clc
        ret

; drop up to 8 bytes nobody read (a late answer), EDX = Audio_Base+Ch
ESSREG_DSP_Drain:
        push    eax
        push    ecx
        mov     ecx,8
.next:  in      al,dx
        test    al,0x40
        jz      .done
        sub     edx,byte 2
        in      al,dx
        add     edx,byte 2
        loop    .next
.done:  pop     ecx
        pop     eax
        ret

; C6h stays on for the DSP's next owner: while nobody owns the DSP, turn
; it off again (C7h), so a DOS program finds it as after a reset
; EDX = Audio_Base+Ch, keeps EAX
ESSREG_DSP_Done:
        cmp     dword [edi+ADI_DSPOwner],byte 0
        jne     .done
        push    eax
        push    ecx
        mov     al,0xC7
        mov     ecx,ESSREG_POLL_BYTE
        call    ESSREG_DSP_Put
        pop     ecx
        pop     eax
.done:  ret

; AL = mixer register AH, EDX = Audio_Base; keeps the index and EAX's
; other bytes
ESSREG_Mix_Read:
        push    edx
        add     edx,byte 4
        pushfd
        cli
        push    eax
        in      al,dx
        xchg    al,[esp]                ; keep the index
        mov     al,ah
        out     dx,al
        inc     edx
        in      al,dx
        dec     edx
        xchg    al,[esp]                ; AL = index, [esp] = value
        out     dx,al
        pop     eax
        popfd
        pop     edx
        ret

; mixer register AH = AL, EDX = Audio_Base; keeps the index and EAX
ESSREG_Mix_Write:
        push    eax
        push    ecx
        push    edx
        add     edx,byte 4
        pushfd
        cli
        mov     ecx,eax                 ; CL = value, CH = register
        in      al,dx
        xchg    al,ch                   ; CH = index
        out     dx,al
        inc     edx
        mov     al,cl
        out     dx,al
        dec     edx
        mov     al,ch
        out     dx,al
        popfd
        pop     edx
        pop     ecx
        pop     eax
        ret

; EDX = configuration port of the ADI in EDI, looked up on every call and
; checked: in 100h-FF8h, a multiple of 8, and logical device 1 there has
; Audio_Base as its I/O base
; CF set and Client_AX = ESSREG_E_NOCFG if it can't be found
ESSREG_Get_Config_Port:
        push    eax
        push    ecx
        movzx   ecx,word [edi+ADI_AudioBase]
        pushfd
        cli                             ; ESS's search may read the mixer's
        lea     edx,[ecx+4]             ; identification sequence (40h):
        in      al,dx                   ; keep the index
        push    eax
        push    ecx
        call    Find_Config_Port        ; EDX = port or -1
        add     esp,byte 4
        pop     eax
        push    edx
        lea     edx,[ecx+4]
        out     dx,al
        pop     edx
        cmp     edx,0x100
        jb      .none
        cmp     edx,0xFF8
        ja      .none
        test    dl,7
        jnz     .none
        call    Read_LDN1_Base          ; AX = logical device 1's base
        cmp     ax,cx
        jne     .none
        popfd
        pop     ecx
        pop     eax
        clc
        ret
.none:  popfd
        pop     ecx
        pop     eax
        mov     ax,ESSREG_E_NOCFG
        jmp     ESSREG_Fail

; select a PnP register, call with interrupts disabled
; EDX = config port, SI = logical device (low byte, FFh = card level) and
; register (high byte)
; returns CL = previous index, CH = previous logical device
ESSREG_PnP_Select:
        in      al,dx
        mov     cl,al
        mov     eax,esi
        cmp     al,0xFF
        je      .card
        mov     al,0x07                 ; logical device number register
        out     dx,al
        inc     edx
        in      al,dx
        mov     ch,al
        mov     eax,esi
        out     dx,al
        dec     edx
.card:  mov     eax,esi
        mov     al,ah
        out     dx,al
        ret

; undo ESSREG_PnP_Select, clobbers EAX
ESSREG_PnP_Restore:
        mov     eax,esi
        cmp     al,0xFF
        je      .card
        mov     al,0x07
        out     dx,al
        inc     edx
        mov     al,ch
        out     dx,al
        dec     edx
.card:  mov     al,cl
        out     dx,al
        ret

; AL = 0 if EAX is 0, 1 if EAX is the caller's VM (EBX), 2 otherwise
ESSREG_Owner_Class:
        or      eax,eax
        jz      .done
        cmp     eax,ebx
        mov     al,1
        je      .done
        mov     al,2
.done:  ret

; CF set and Client_AX = ESSREG_E_INUSE when another VM owns what the port
; at Audio_Base+EAX belongs to: the DSP's data, reset and FIFO ports, and
; for a write (ECX = 1, 0 for a read) the FM ports
ESSREG_Port_Owner:
        push    eax
        push    edx
        mov     edx,ESSREG_DSP_RD_PORTS
        jecxz   .dsp
        mov     edx,ESSREG_DSP_WR_PORTS
.dsp:   bt      edx,eax
        jnc     .fm
        call    ESSREG_Check_DSP_Owner
        jc      .out
        mov     eax,[esp+4]
.fm:    jecxz   .ok
        mov     edx,ESSREG_FM_PORTS
        bt      edx,eax
        jnc     .ok
        call    ESSREG_Check_FM_Owner
        jc      .out
.ok:    clc
.out:   pop     edx
        pop     eax
        ret

; ESI = flat address of the client's ES:DI buffer of 128 bytes
; CF set and Client_AX = ESSREG_E_PARAM unless all of it is inside the
; segment: in V86 mode a 64K segment, in protected mode a present,
; writable, expand-up segment whose limit covers it
ESSREG_Map_Buffer:
        push    eax
        push    ecx
        push    edx
        mov     ax,(Client_ES << 8) | 0xFF      ; offset 0: the segment's base
        VxDCall Map_Flat
        cmp     eax,byte -1
        je      .bad
        mov     esi,eax
        mov     ax,(Client_ES << 8) | Client_EDI
        VxDCall Map_Flat
        cmp     eax,byte -1
        je      .bad
        sub     eax,esi                 ; the offset Map_Flat used
        mov     ecx,0xFFFF
        test    dword [ebp+Client_EFlags],0x20000       ; V86 mode
        jnz     .limit
        movzx   edx,word [ebp+Client_ES]
        test    edx,0xFFFC
        jz      .bad                    ; null selector
        verw    dx
        jnz     .bad                    ; not writable from ring 3
        lar     ecx,dx
        jnz     .bad
        test    ch,0x80
        jz      .bad                    ; not present
        test    ch,0x04
        jnz     .bad                    ; expand-down
        lsl     ecx,dx
        jnz     .bad
.limit: cmp     ecx,byte 0x7F
        jb      .bad
        sub     ecx,byte 0x7F
        cmp     eax,ecx
        ja      .bad
        add     esi,eax
        pop     edx
        pop     ecx
        pop     eax
        clc
        ret
.bad:   pop     edx
        pop     ecx
        pop     eax
        mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail

; --- 0400: extension information ------------------------------------------
ESSREG_API_Info:
        mov     word [ebp+Client_EAX],ESSREG_VERSION
        mov     ax,ESSREG_FEATURES
        test    byte [ESSREG_Opts],OPT_VFM
        jnz     .mixer
        and     ax,~FEATURE_DOS_FM & 0xFFFF
.mixer: test    byte [ESSREG_Opts],OPT_DOS_MIXER
        jnz     .a1
        and     ax,~FEATURE_DOS_MIXER & 0xFFFF
.a1:    test    byte [ESSREG_Opts+1],OPT_A1_FILTER >> 8
        jz      .rec
        and     ax,~FEATURE_DOS_A1 & 0xFFFF
.rec:   test    byte [ESSREG_Opts],OPT_REC_TAKES
        jnz     .timed
        and     ax,~FEATURE_REC_DSP & 0xFFFF
.timed: test    byte [ESSREG_Opts+1],OPT_FM_TIMED >> 8
        jnz     .lock
        and     ax,~FEATURE_FM_TIMED & 0xFFFF
.lock:  test    byte [ESSREG_Opts+1],OPT_DS_LOCK >> 8
        jnz     .latch
        and     ax,~FEATURE_DS_LOCK & 0xFFFF
.latch: test    byte [ESSREG_Opts+1],OPT_DRQ_LATCH >> 8
        jnz     .sb16
        and     ax,~FEATURE_DRQ_LATCH & 0xFFFF
.sb16:  test    byte [ESSREG_Opts+1],OPT_SB16 >> 8
        jnz     .out
        and     ax,~FEATURE_SB16 & 0xFFFF
.out:   mov     [ebp+Client_EBX],ax
        mov     ax,[ESSREG_Opts]
        mov     [ebp+Client_ECX],ax
        mov     word [ebp+Client_EDX],ESSREG_FUNC_COUNT
        clc
        ret

; --- 0401: read mixer register --------------------------------------------
ESSREG_API_MixerRead:
        call    ESSREG_Get_ADI
        jc      .done
        movzx   edx,word [edi+ADI_AudioBase]
        mov     ah,[ebp+Client_EBX]
        call    ESSREG_Mix_Read
        movzx   eax,al
        mov     [ebp+Client_EAX],ax
        clc
.done:  ret

; --- 0402: write mixer register -------------------------------------------
ESSREG_API_MixerWrite:
        call    ESSREG_Get_ADI
        jc      .done
        movzx   edx,word [edi+ADI_AudioBase]
        mov     ah,[ebp+Client_EBX]
        mov     al,[ebp+Client_EBX+1]
        call    ESSREG_Mix_Write
        call    ESSREG_Snap_Update
        clc
.done:  ret

; --- 0403: read controller register ---------------------------------------
ESSREG_API_CtrlRead:
        call    ESSREG_Get_ADI
        jc      .done
        call    ESSREG_Check_DSP_Owner
        jc      .done
        movzx   esi,byte [ebp+Client_EBX]
        mov     eax,esi
        cmp     al,0xA0
        jb      .param
        cmp     al,0xBF
        ja      .param
        call    ESSREG_DSP_Idle
        jc      .done
        pushfd
        cli
        call    ESSREG_DSP_Drain
        mov     al,0xC6                 ; enable Extended mode commands
        mov     ecx,ESSREG_POLL_BYTE
        call    ESSREG_DSP_Put
        jc      .busy
        mov     al,0xC0                 ; read controller register
        mov     ecx,ESSREG_POLL_REST
        call    ESSREG_DSP_Put
        jc      .busy
        mov     eax,esi
        mov     ecx,ESSREG_POLL_REST
        call    ESSREG_DSP_Put
        jc      .busy
        mov     ecx,ESSREG_POLL_REST
        call    ESSREG_DSP_Get
        jc      .timeout
        call    ESSREG_DSP_Done
        popfd
        movzx   eax,al
        mov     [ebp+Client_EAX],ax
        clc
.done:  ret
.param: mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail
.busy:  popfd
        mov     ax,ESSREG_E_BUSY
        jmp     ESSREG_Fail
.timeout:
        popfd                           ; a late answer goes at the next drain
        mov     ax,ESSREG_E_TIMEOUT
        jmp     ESSREG_Fail

; --- 0404: write controller register --------------------------------------
ESSREG_API_CtrlWrite:
        call    ESSREG_Get_ADI
        jc      .done
        call    ESSREG_Check_DSP_Owner
        jc      .done
        movzx   esi,word [ebp+Client_EBX]
        mov     eax,esi
        cmp     al,0xA0
        jb      .param
        cmp     al,0xBF
        ja      .param
        call    ESSREG_DSP_Idle
        jc      .done
        pushfd
        cli
        call    ESSREG_DSP_Drain
        mov     al,0xC6
        mov     ecx,ESSREG_POLL_BYTE
        call    ESSREG_DSP_Put
        jc      .busy
        mov     eax,esi                 ; register (command Axh/Bxh)
        mov     ecx,ESSREG_POLL_REST
        call    ESSREG_DSP_Put
        jc      .busy
        mov     eax,esi
        mov     al,ah                   ; value
        mov     ecx,ESSREG_POLL_REST
        call    ESSREG_DSP_Put
        jc      .busy
        call    ESSREG_DSP_Done
        popfd
        clc
.done:  ret
.param: mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail
.busy:  popfd
        mov     ax,ESSREG_E_BUSY
        jmp     ESSREG_Fail

; --- 0405/0406: audio device ports Audio_Base+0..F --------------------------
ESSREG_API_PortRead:
        call    ESSREG_Get_ADI
        jc      .done
        movzx   eax,byte [ebp+Client_EBX]
        cmp     al,0x0F
        ja      .param
        xor     ecx,ecx
        call    ESSREG_Port_Owner
        jc      .done
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,eax
        in      al,dx
        movzx   eax,al
        mov     [ebp+Client_EAX],ax
        clc
.done:  ret
.param: mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail

ESSREG_API_PortWrite:
        call    ESSREG_Get_ADI
        jc      .done
        movzx   eax,byte [ebp+Client_EBX]
        cmp     al,0x0F
        ja      .param
        xor     ecx,ecx
        inc     ecx
        call    ESSREG_Port_Owner
        jc      .done
        mov     ecx,ESSREG_FM_PORTS
        bt      ecx,eax
        jnc     .write
        ; the FM chip no longer holds what its last owner left
        mov     dword [edi+ADI_FMLastOwner],-1
.write: movzx   edx,word [edi+ADI_AudioBase]
        add     edx,eax
        mov     al,[ebp+Client_EBX+1]
        out     dx,al
        clc
.done:  ret
.param: mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail

; --- 0407/0408: configuration device ports Config_Base+0..7 -----------------
ESSREG_API_CfgRead:
        call    ESSREG_Get_ADI
        jc      .done
        movzx   eax,byte [ebp+Client_EBX]
        cmp     al,0x07
        ja      .param
        call    ESSREG_Get_Config_Port
        jc      .done
        movzx   eax,byte [ebp+Client_EBX]
        add     edx,eax
        in      al,dx
        movzx   eax,al
        mov     [ebp+Client_EAX],ax
        clc
.done:  ret
.param: mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail

ESSREG_API_CfgWrite:
        call    ESSREG_Get_ADI
        jc      .done
        movzx   eax,byte [ebp+Client_EBX]
        cmp     al,0x07
        ja      .param
        cmp     al,0x02                 ; the EEPROM data, command and
        jb      .port                   ; address ports: never
        cmp     al,0x04
        jbe     .param
.port:  call    ESSREG_Get_Config_Port
        jc      .done
        movzx   eax,byte [ebp+Client_EBX]
        add     edx,eax
        mov     al,[ebp+Client_EBX+1]
        out     dx,al
        clc
.done:  ret
.param: mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail

; --- 0409/040A: PnP registers --------------------------------------------------
ESSREG_API_PnPRead:
        call    ESSREG_Get_ADI
        jc      .done
        call    ESSREG_Get_Config_Port
        jc      .done
        movzx   esi,word [ebp+Client_EBX]
        pushfd
        cli
        call    ESSREG_PnP_Select
        inc     edx
        in      al,dx
        dec     edx
        movzx   eax,al
        push    eax
        call    ESSREG_PnP_Restore
        pop     eax
        popfd
        mov     [ebp+Client_EAX],ax
        clc
.done:  ret

ESSREG_API_PnPWrite:
        call    ESSREG_Get_ADI
        jc      .done
        mov     al,[ebp+Client_EBX+1]   ; BH: Config Control (02h), Activate
        cmp     al,0x02                 ; (30h) and I/O Range Check (31h)
        je      .param                  ; take the card from its drivers:
        cmp     al,0x30                 ; never
        je      .param
        cmp     al,0x31
        je      .param
        call    ESSREG_Get_Config_Port
        jc      .done
        movzx   esi,word [ebp+Client_EBX]
        pushfd
        cli
        call    ESSREG_PnP_Select
        inc     edx
        mov     al,[ebp+Client_EAX]
        out     dx,al
        dec     edx
        call    ESSREG_PnP_Restore
        popfd
        clc
.done:  ret
.param: mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail

; --- 040B: read mixer registers 00h-7Fh into ES:DI --------------------------
ESSREG_API_MixerBlock:
        call    ESSREG_Get_ADI
        jc      .done
        call    ESSREG_Map_Buffer
        jc      .done
        movzx   edx,word [edi+ADI_AudioBase]
        xor     ecx,ecx
.next:  xor     eax,eax
        cmp     cl,0x40                 ; identification sequence, not read
        je      .store
        mov     ah,cl
        call    ESSREG_Mix_Read
.store: mov     [esi+ecx],al
        inc     ecx
        cmp     ecx,0x80
        jb      .next
        clc
.done:  ret

; --- 040C: owner information ------------------------------------------------
ESSREG_API_Owners:
        call    ESSREG_Get_ADI
        jc      .done
        mov     eax,[edi+ADI_DSPOwner]
        call    ESSREG_Owner_Class
        mov     [ebp+Client_EAX],al
        mov     eax,[edi+ADI_FMOwner]
        call    ESSREG_Owner_Class
        mov     [ebp+Client_EAX+1],al
        mov     eax,[edi+ADI_MPUOwner]
        call    ESSREG_Owner_Class
        mov     [ebp+Client_EBX],al
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        in      al,dx
        mov     [ebp+Client_EBX+1],al
        mov     ax,[edi+ADI_Flags]
        mov     [ebp+Client_EDX],ax
        clc
.done:  ret

; --- the DACs ----------------------------------------------------------------

; AL = mixer 71h with the DACs' modes from SYSTEM.INI: by default both DACs
; play the samples as they are, Audio 2 not oversampled and both filters
; bypassed, as build/ES1869.DRV has them (docs/AUDIO_PIPELINE.md)
; Audio2Oversampling=1 sets bit 4 (4x), and with Audio2Filter=1 too, bit 3
; stays as it was: ESS's value. Audio1Filter=1 leaves bit 2 as it was
ESSREG_A2_Bits:
        test    byte [ESSREG_Opts+1],OPT_A1_FILTER >> 8
        jnz     .a2
        or      al,ESSREG_A1_SCF_BYPASS
.a2:    test    byte [ESSREG_Opts+1],OPT_A2_4X >> 8
        jnz     .over
        and     al,~ESSREG_A2_4X & 0xFF
        test    byte [ESSREG_Opts+1],OPT_A2_FILTER >> 8
        jnz     .filter
.bypass:
        or      al,ESSREG_A2_SCF_BYPASS
        ret
.filter:
        and     al,~ESSREG_A2_SCF_BYPASS & 0xFF
        ret
.over:  or      al,ESSREG_A2_4X
        test    byte [ESSREG_Opts+1],OPT_A2_FILTER >> 8
        jz      .bypass
        ret

; in place of ESS's write of mixer 71h at the VxD's own Audio 2 start
; (5:3603) and where a VM takes the DSP (5:198C), AL = its value (bits 4
; and 1: 4x oversampling, asynchronous), AH = 71h, EDX = Audio_Base: the
; DACs' modes from SYSTEM.INI, for Windows and DOS programs alike
ESSREG_A2_Mode:
        call    ESSREG_A2_Bits
        jmp     L1_09A8

; in place of VDMAD_Default_Handler as Audio 1's DMA handler (L1_0546),
; EAX = DMA handle, EBX = VM, EDI = the ADI or 0: when a DOS program
; starts a transfer on Audio 1's channel, in either direction, the CODEC's
; filter is bypassed again (71h bit 2), in case the program reset the
; mixer after it took the DSP. Then VDMAD's handler, as ESS's code calls it.
; A program with a virtual Sound Blaster doesn't get the channel
ESSREG_DMA1:
        or      edi,edi
        jz      .dos
        cmp     [edi+EX_VSB_VM],ebx
        jne     .dos
        ret                             ; a virtual Sound Blaster's: the
                                        ; channel stays the recording's
.dos:   test    byte [ESSREG_Opts+1],OPT_A1_FILTER >> 8
        jnz     .default
        pushad
        call    ESSREG_DMA_DOS
        jc      .done
        movzx   edx,word [edi+ADI_AudioBase]
        mov     ah,0x71
        call    ESSREG_Mix_Read
        test    al,ESSREG_A1_SCF_BYPASS
        jnz     .done
        or      al,ESSREG_A1_SCF_BYPASS
        call    ESSREG_Mix_Write
.done:  popad
.default:
        VxDJmp  VDMAD_Default_Handler

; in place of VDMAD_Default_Handler as Audio 2's DMA handler (L1_0870),
; EAX = DMA handle, EBX = VM, EDX = 0 when the VM programmed the channel,
; else what it did (ESSREG_S16_Notify): a DOS program's transfer on Audio
; 2's channel gets the DAC's mode from SYSTEM.INI in 71h again, in case the
; program reset the mixer after it took the DSP. ESS's setting leaves 71h.
; A program with a virtual Sound Blaster doesn't get the channel either,
; and a Sound Blaster 16 program's programming of it, as its 16-bit
; channel, goes to Audio 1's channel
ESSREG_DMA2:
        push    edi
        call    ESSREG_DMA2_ADI
        or      edi,edi
        jz      .read
        cmp     [edi+EX_VSB_VM],ebx
        je      .theirs
        cmp     [edi+EX_S16_VM],ebx
        jne     .read
        push    eax
        mov     al,[edi+ADI_DMA2Channel]
        cmp     [edi+EX_S16_Chan],al
        pop     eax
        jne     .read
        call    ESSREG_S16_Notify
.theirs:
        pop     edi
        ret                             ; the channel stays Windows'
.read:  cmp     edx,byte 3              ; a read of the channel's count, page
        ja      .mode                   ; or address (notified while the
        or      edx,edx                 ; front has the channel): not a
        jnz     .theirs                 ; change of its state
.mode:  pop     edi
        test    byte [ESSREG_Opts+1],OPT_A2_4X >> 8
        jz      .mine
        test    byte [ESSREG_Opts+1],OPT_A2_FILTER >> 8
        jnz     .default
.mine:  pushad
        call    ESSREG_DMA2_ADI
        call    ESSREG_DMA_DOS
        jc      .done
        movzx   edx,word [edi+ADI_AudioBase]
        mov     ah,0x71
        call    ESSREG_Mix_Read
        call    ESSREG_A2_Bits
        call    ESSREG_Mix_Write
.done:  popad
.default:
        VxDJmp  VDMAD_Default_Handler

; CF clear if the transfer of DMA handle EAX is a DOS program's: its VM
; (EBX) owns the DSP of the ADI in EDI (0: none) and the channel is
; unmasked; CL = its mode (VDMAD_Get_Virt_State). Keeps EAX, EBX and EDI
ESSREG_DMA_DOS:
        or      edi,edi
        jz      .no
        VxDCall Test_Sys_VM_Handle
        je      .no
        cmp     [edi+ADI_DSPOwner],ebx
        jne     .no
        push    edx
        push    esi
        VxDCall VDMAD_Get_Virt_State
        mov     cl,dl
        pop     esi
        pop     edx
        test    cl,DMA_MASKED
        jnz     .no
        clc
        ret
.no:    stc
        ret

; EDI = the ADI whose Audio 2 DMA handle is EAX, 0 if none
ESSREG_DMA2_ADI:
        push    eax
        push    ecx
        push    esi
        mov     ecx,eax
        xor     edi,edi
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        jz      .none
.next:  mov     edi,[eax]
        cmp     [edi+ADI_DMA2Handle],ecx
        je      .found
        VxDCall List_Get_Next
        jnz     .next
.none:  xor     edi,edi
.found: popfd
.done:  pop     esi
        pop     ecx
        pop     eax
        ret

; --- Windows' ESS functions, wrapped -------------------------------------------

; 0002 (acquire the DSP or FM), 0102 (acquire FM, ESFM.DRV) and 0302
; (acquire the MPU-401, ESSMPU.DRV) from Windows: Windows is using the
; card again, so first reset what DOS programs left (ESSREG_Reclaim)
ESSREG_API_0002:
        VxDCall Test_Sys_VM_Handle
        jne     .stock
        call    ESSREG_Reclaim
        call    API_0002_Acquire
        jc      .done
        test    byte [ebp+Client_EBX],2
        jz      .done
        call    ESSREG_FM_Hard
        clc
.done:  ret
.stock: jmp     API_0002_Acquire

ESSREG_API_0102:
        VxDCall Test_Sys_VM_Handle
        jne     .stock
        call    ESSREG_Reclaim
        call    API_0102_FM_Acquire
        jc      .done
        call    ESSREG_FM_Hard
        clc
.done:  ret
.stock: jmp     API_0102_FM_Acquire

ESSREG_API_0302:
        VxDCall Test_Sys_VM_Handle
        jne     .stock
        call    ESSREG_Reclaim
.stock: jmp     API_0302_MPU_Acquire

; 0103 from Windows: ESFM.DRV closes, and only then tells ES1869.DRV,
; which gives I2S the music DAC (7Fh bit 0) and IIS the FM volume (36h).
; A DOS program may take the FM chip in between.
ESSREG_API_0103:
        VxDCall Test_Sys_VM_Handle
        jne     .stock
        call    API_0103_FM_Release
        jc      .done
        mov     al,EXF_MIDI_CLOSED
        call    ESSREG_Set_Flag
        clc
.done:  ret
.stock: jmp     API_0103_FM_Release

; 0003 from Windows: ES1869.DRV releases the DSP after changing the mixer.
; Right after a MIDI close, that's the change above: a DOS FM owner gets
; the music DAC and an FM volume again. And a DOS program with a virtual
; Sound Blaster may get the real DSP back (ESSREG_VSB_Back).
ESSREG_API_0003:
        VxDCall Test_Sys_VM_Handle
        jne     .stock
        call    API_0003_Release
        pushfd
        call    ESSREG_After_Close
        call    ESSREG_VSB_Back_All
        popfd
        ret
.stock: jmp     API_0003_Release

; set flag AL on every device
ESSREG_Set_Flag:
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        mov     cl,[esp+0x1C]           ; AL from pushad
        or      [edi+EX_Flags],cl
        pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; the first DSP release by Windows after a MIDI close (EBX = system VM)
ESSREG_After_Close:
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        test    byte [edi+EX_Flags],EXF_MIDI_CLOSED
        jz      .skip
        and     byte [edi+EX_Flags],~EXF_MIDI_CLOSED
        mov     ecx,[edi+ADI_FMOwner]
        jecxz   .skip
        cmp     ecx,ebx
        je      .skip
        call    ESSREG_FM_Audible       ; a DOS VM has FM
.skip:  pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; Windows (EBX) acquired FM through the API: it keeps it, and the virtual
; chip it had is gone
ESSREG_FM_Hard:
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        cmp     [edi+ADI_FMOwner],ebx
        jne     .skip
        and     byte [edi+EX_Flags],~(EXF_FM_SOFT | EXF_MIDI_CLOSED)
        push    eax
        push    esi
        call    ESSREG_VFM_Find
        jz      .novfm
        mov     esi,eax
        call    ESSREG_VFM_Forget
.novfm: pop     esi
        pop     eax
.skip:  pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; Windows plays a sound or changes the mixer (EBX = system VM): for every
; device, Windows' saved mixer goes back if a DOS program's release didn't
; manage to, and the FM chip is reset if a DOS program had it (notes it
; left on stop, and the next DOS program starts from a clean chip), unless
; ResetDosFM=0
ESSREG_Reclaim:
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        test    byte [edi+EX_Flags],EXF_SNAP
        jz      .fm
        cmp     dword [edi+ADI_DSPOwner],byte 0
        jne     .fm
        call    ESSREG_Snap_Restore
.fm:    test    byte [ESSREG_Opts],OPT_RESET_FM
        jz      .skip
        test    byte [edi+EX_Flags],EXF_DOS_FM
        jz      .skip
        cmp     dword [edi+ADI_FMOwner],byte 0
        jne     .skip                   ; still in use
        and     byte [edi+EX_Flags],~EXF_DOS_FM
        cmp     word [edi+ADI_FMBase],byte -1
        je      .skip
        push    eax
        push    edx
        call    Wake_From_Local_Powerdown ; if ESS powered the chip down
        movzx   edx,word [edi+ADI_AudioBase] ; when the program ended
        call    L5_19C4                 ; ESS's FM reset pulse
        call    L5_0BD0                 ; and its init: OPL2 mode, silent
        mov     dword [edi+ADI_FMLastOwner],-1
        pop     edx
        pop     eax
.skip:  pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; --- Windows' mixer while a DOS VM has the audio device ------------------------

; in place of ESS's Save_DOS_Mixer when a VM (EBX) acquires the DSP: the
; DRQ latch first (DrqLatch=1), then a DOS VM taking the DSP from Windows
; saves Windows' mixer (unless it's already saved and not put back yet);
; DosMixerRestore=0: ESS's alone
ESSREG_DSP_Save:
        call    ESSREG_DRQ_Latch
        test    byte [ESSREG_Opts],OPT_DOS_MIXER
        jz      .stock
        VxDCall Test_Sys_VM_Handle
        je      .stock
        test    byte [edi+EX_Flags],EXF_SNAP
        jnz     .stock
        push    eax
        push    ecx
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        xor     ecx,ecx
.next:  mov     ah,[ESSREG_Snap_Regs+ecx]
        or      ah,ah
        jz      .end
        call    ESSREG_Mix_Read
        mov     [edi+EX_Snap+ecx],al
        inc     ecx
        jmp     .next
        ; what a DOS FM owner changed (ESSREG_FM_Audible) isn't Windows'
.end:   test    byte [edi+EX_D1],D1_VOL
        jz      .dac
        mov     byte [edi+EX_Snap+SNAP_36],0
.dac:   test    byte [edi+EX_D1],D1_DAC
        jz      .saved
        or      byte [edi+EX_Snap+SNAP_7F],1
.saved: or      byte [edi+EX_Flags],EXF_SNAP
        pop     edx
        pop     ecx
        pop     eax
.stock: jmp     Save_DOS_Mixer

; in place of ESS's Restore_DOS_Mixer when a VM (EBX) releases the DSP:
; after ESS's own restore, all of Windows' saved mixer goes back
ESSREG_DSP_Restore:
        call    ESSREG_S16_End          ; its Sound Blaster 16 front, if any
        test    byte [ESSREG_Opts],OPT_DOS_MIXER
        jz      near Restore_DOS_Mixer
        call    Restore_DOS_Mixer
        VxDCall Test_Sys_VM_Handle
        je      .done
        test    byte [edi+EX_Flags],EXF_SNAP
        jz      .done
        call    ESSREG_Snap_Restore
.done:  clc
        ret

; write Windows' saved mixer back, EDI = ADI
ESSREG_Snap_Restore:
        push    eax
        push    ecx
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        xor     ecx,ecx
.next:  mov     ah,[ESSREG_Snap_Regs+ecx]
        or      ah,ah
        jz      .end
        mov     al,[edi+EX_Snap+ecx]
        call    ESSREG_Mix_Write
        inc     ecx
        jmp     .next
.end:   and     byte [edi+EX_Flags],~EXF_SNAP
        pop     edx
        pop     ecx
        pop     eax
        ret

; a mixer write through 0402 by Windows (EBX) is a Windows setting: while
; a DOS VM has the audio device, it's also what Windows gets back
; EDI = ADI, AH = register, AL = value
ESSREG_Snap_Update:
        test    byte [edi+EX_Flags],EXF_SNAP
        jz      .done
        VxDCall Test_Sys_VM_Handle
        jne     .done
        push    ecx
        xor     ecx,ecx
.next:  cmp     byte [ESSREG_Snap_Regs+ecx],0
        je      .end
        cmp     [ESSREG_Snap_Regs+ecx],ah
        je      .found
        inc     ecx
        jmp     .next
.found: mov     [edi+EX_Snap+ecx],al
.end:   pop     ecx
.done:  ret

; the mixer registers Windows gets back after a DOS program: the playback
; and record levels and sources, the 3-D effect, the hardware volume and
; MPU-401 interrupt masks, Audio 2's mode and volume, MONO_IN/OUT and the
; music DAC; master volume last. Not the SB Pro views of these (04h-2Eh,
; 32h), nor Audio 2's transfer registers.
ESSREG_Snap_Regs:
        db 0x0E, 0x14, 0x1A, 0x1C, 0x36, 0x38, 0x3A, 0x3C, 0x3E
        db 0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x64
        db 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F
        db 0x71, 0x7C, 0x7D, 0x7F, 0x60, 0x62, 0
SNAP_36                 equ 4           ; the index of 36h in the list
SNAP_7F                 equ 27          ; and of 7Fh; 30 registers fit in the
                                        ; 32 bytes at EX_Snap

; --- DOS FM ------------------------------------------------------------------

; I/O trap of the FM ports (Audio_Base+0-3, +8, +9 and the FM alias), in
; place of ESS's FM_Port_Trap: EBX = VM, ESI = ADI, EDX = port, ECX = type,
; EAX = data written; returns EAX = data read
; the caller's own chip goes to the hardware, or to the queue that plays a
; DOS program's FM on its own clock; a free chip is taken first; otherwise
; the access goes to the VM's virtual chip
; VirtualFM=0: ESS's trap, FFh and a message while another VM has FM
ESSREG_FM_Trap:
        test    byte [ESSREG_Opts],OPT_VFM
        jz      near FM_Port_Trap
        cmp     word [esi+ADI_FMBase],byte -1
        je      near FM_Port_Trap       ; no FM port: ESS's answer
        cmp     ecx,byte 4              ; Byte_Input 0, Byte_Output 4
        jbe     .byte
        VxDJmp  Simulate_IO             ; words and strings come back as bytes
.byte:  push    edi
        push    esi
        push    ecx
        push    edx
        push    eax
        mov     edi,esi
        mov     eax,[edi+ADI_FMOwner]
        cmp     eax,ebx
        je      .real
        or      eax,eax
        jz      .take
        test    byte [edi+EX_Flags],EXF_FM_SOFT
        jz      .virtual                ; MIDI in Windows or another DOS VM
        ; Windows has it from a port access only: a DOS VM takes it
        push    ebx
        mov     ebx,eax
        mov     eax,2
        call    Release_Resources
        pop     ebx
        cmp     dword [edi+ADI_FMOwner],byte 0
        jne     .virtual
.take:  call    ESSREG_FM_Take
        jc      .virtual
        call    ESSREG_FMQ_Start
.real:  pop     eax
        pop     edx
        pop     ecx
        cmp     [edi+EX_FMQ_VM],ebx
        je      .queue
        jecxz   .in
        out     dx,al
        jmp     .ret
.in:    in      al,dx
.ret:   pop     esi
        pop     edi
        ret
.queue: call    ESSREG_FMQ_IO
        pop     esi
        pop     edi
        ret
.virtual:
        pop     eax
        pop     edx
        pop     ecx
        call    ESSREG_VFM_IO
        pop     esi
        pop     edi
        ret

; give the FM chip to the VM in EBX, EDI = ADI; CF set if it can't have it
; Windows gets it softly (a DOS VM may take it, unless DosTakesFM=0); a
; DOS VM gets the music DAC and an FM volume
ESSREG_FM_Take:
        push    eax
        mov     eax,2
        call    Acquire_Resources       ; resets the chip unless the VM had it
        jc      .out                    ; last (ESSREG_FM_Reset)
        test    byte [edi+EX_Flags],EXF_FM_RESET
        jz      .kept
        call    L5_0BD0                 ; ESS's init: OPL2 mode, silent
.kept:  call    ESSREG_VFM_Replay
        VxDCall Test_Sys_VM_Handle
        jne     .dos
        test    byte [ESSREG_Opts],OPT_TAKES_FM
        jz      .ok
        or      byte [edi+EX_Flags],EXF_FM_SOFT
        jmp     .ok
.dos:   or      byte [edi+EX_Flags],EXF_DOS_FM
        call    ESSREG_FM_Audible
.ok:    clc
.out:   pop     eax
        ret

; in place of the FM reset in ESS's Acquire_Resources, EBX = new owner,
; EDI = ADI, EDX = Audio_Base: a DOS VM taking back the chip it had last
; finds its state as it left it, since nobody used the chip in between
; (DosKeepsFM=0: always reset, as ESS's)
ESSREG_FM_Reset:
        and     byte [edi+EX_Flags],~EXF_FM_RESET
        test    byte [ESSREG_Opts],OPT_KEEPS_FM
        jz      .reset
        cmp     [edi+ADI_FMLastOwner],ebx
        jne     .reset
        VxDCall Test_Sys_VM_Handle
        jne     .keep
.reset: or      byte [edi+EX_Flags],EXF_FM_RESET
        jmp     L5_19C4
.keep:  clc
        ret

; a DOS FM owner needs the music DAC (7Fh bit 0 = 0) and an FM volume (36h)
; that ES1869.DRV only sets while Windows' MIDI has FM, EDI = ADI
; (not with DosFMAudible=0)
ESSREG_FM_Audible:
        test    byte [ESSREG_Opts],OPT_FM_AUDIBLE
        jnz     .on
        ret
.on:    push    eax
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        mov     ah,0x7F
        call    ESSREG_Mix_Read
        test    al,1
        jz      .vol
        and     al,0xFE
        call    ESSREG_Mix_Write
        or      byte [edi+EX_D1],D1_DAC
.vol:   mov     ah,0x36
        call    ESSREG_Mix_Read
        or      al,al
        jnz     .done
        mov     al,ESSREG_FM_VOLUME
        call    ESSREG_Mix_Write
        or      byte [edi+EX_D1],D1_VOL
.done:  pop     edx
        pop     eax
        ret

; in place of FM_Enable_Local_Trapping in ESS's Release_Resources, EBX =
; the owner, EDI = ADI: what its queue holds goes to the chip, then what
; ESSREG_FM_Audible changed goes back, unless something else changed it
; since, or the FM is being recorded (7Fh bit 4)
ESSREG_FM_Released:
        cmp     [edi+EX_FMQ_VM],ebx
        jne     .soft
        push    eax
        mov     al,1
        call    ESSREG_FMQ_End
        pop     eax
.soft:  and     byte [edi+EX_Flags],~EXF_FM_SOFT
        test    byte [edi+EX_D1],D1_DAC | D1_VOL
        jz      .done
        push    eax
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        test    byte [edi+EX_D1],D1_VOL
        jz      .dac
        mov     ah,0x36
        call    ESSREG_Mix_Read
        cmp     al,ESSREG_FM_VOLUME
        jne     .dac
        xor     al,al
        call    ESSREG_Mix_Write
.dac:   test    byte [edi+EX_D1],D1_DAC
        jz      .d1
        mov     ah,0x7F
        call    ESSREG_Mix_Read
        test    al,0x11
        jnz     .d1
        or      al,1
        call    ESSREG_Mix_Write
.d1:    mov     byte [edi+EX_D1],0
        pop     edx
        pop     eax
.done:  jmp     FM_Enable_Local_Trapping

; --- the virtual FM chip -------------------------------------------------------

; ESI = the node of VM EBX for the ADI in EDI, ZF clear with EAX = its
; virtual chip; ZF set if it has none
ESSREG_VFM_Find:
        call    L1_0479
        jc      .none
        mov     eax,[esi+NODE_VFM]
        or      eax,eax
        ret
.none:  xor     eax,eax
        ret

; ESI = the virtual chip of VM EBX for the ADI in EDI, allocated zeroed on
; first use; 0 without memory
ESSREG_VFM_Get:
        push    eax
        push    ecx
        push    edx
        call    ESSREG_VFM_Find
        jnz     .have
        or      esi,esi
        jz      .out                    ; no node (not expected)
        push    esi
        push    byte 1                  ; HEAPZEROINIT
        push    dword VFM_SIZE
        VxDCall _HeapAllocate
        add     esp,byte 8
        pop     esi
        mov     [esi+NODE_VFM],eax
.have:  mov     esi,eax
.out:   pop     edx
        pop     ecx
        pop     eax
        ret

; a trapped byte access to the virtual chip: EBX = VM, EDI = ADI,
; EDX = port, ECX = type, EAX = data written; returns AL = data read
ESSREG_VFM_IO:
        push    esi
        push    ecx
        push    edx
        inc     dword [ESSREG_FM_Accesses]
        call    ESSREG_VFM_Get
        or      esi,esi
        jz      .nomem
        and     edx,byte 3              ; +8/+9 are +0/+1
        jecxz   .read
        call    ESSREG_VFM_Write
        jmp     .done
.read:  or      edx,edx
        jz      .status
        cmp     edx,byte 1
        jne     .ff                     ; +2, +3: FFh as on an OPL3
        test    byte [esi+VFM_Flags],VFM_NATIVE
        jz      .zero                   ; +1 reads 0 in emulation mode
        call    ESSREG_VFM_Readback
        jmp     .done
.status:
        call    ESSREG_VFM_Status
        jmp     .done
.zero:  xor     eax,eax
        jmp     .done
.nomem: jecxz   .ff                     ; no memory: ESS's answer
        jmp     .done
.ff:    mov     eax,0xFF
.done:  pop     edx
        pop     ecx
        pop     esi
        ret

; a write to port offset EDX of the virtual chip ESI, AL = value
ESSREG_VFM_Write:
        movzx   eax,al
        test    byte [esi+VFM_Flags],VFM_NATIVE
        jnz     .native
        cmp     edx,byte 2
        je      .high
        jb      .odd
        ; +3: data, as +1
.data:  mov     ecx,[esi+VFM_Latch]
        jmp     ESSREG_VFM_Emu
.odd:   or      edx,edx
        jnz     .data
        mov     [esi+VFM_Latch],eax     ; +0: low bank address
        ret
.high:  or      ah,1                    ; +2: high bank address
        mov     [esi+VFM_Latch],eax
        ret
.native:
        or      edx,edx
        jz      .leave
        cmp     edx,byte 2
        je      .nlow
        ja      .nhigh
        mov     ecx,[esi+VFM_Latch]     ; +1: data
        jmp     ESSREG_VFM_Nat
.nlow:  mov     [esi+VFM_Latch],al
        ret
.nhigh: mov     [esi+VFM_Latch+1],al
        ret
.leave: ; a write to +0 leaves native mode (DS p.41)
        and     byte [esi+VFM_Flags],~VFM_NATIVE
        or      byte [esi+VFM_Flags],VFM_USED
        mov     [esi+VFM_Latch],eax
        ret

; emulation mode register ECX = AL, ESI = virtual chip
ESSREG_VFM_Emu:
        and     ecx,0x1FF
        or      byte [esi+VFM_Flags],VFM_USED
        cmp     ecx,0x105
        je      .new
        mov     ah,cl
        cmp     ah,2                    ; 02h, 03h: timer presets, both banks
        je      .t1
        cmp     ah,3
        je      .t2
        cmp     ecx,4                   ; 004h: timer control
        je      ESSREG_VFM_Ctl
.store: mov     [esi+VFM_Emu+ecx],al
        bts     dword [esi+VFM_EmuMask],ecx
        ret
.new:   test    al,0x80                 ; 105h bit 7: native mode
        jz      .store
        or      byte [esi+VFM_Flags],VFM_NATIVE
        and     al,0x7F
        jmp     .store
.t1:    mov     [esi+VFM_T1],al
        test    byte [esi+VFM_Ctl],1
        jz      .ret
        call    ESSREG_Clock
        mov     [esi+VFM_T1Start],eax
.ret:   ret
.t2:    mov     [esi+VFM_T2],al
        test    byte [esi+VFM_Ctl],2
        jz      .ret
        call    ESSREG_Clock
        mov     [esi+VFM_T2Start],eax
        ret

; timer control (04h, native 404h) = AL, ESI = virtual chip
; bit 7 resets the flags; else bits 0/1 run timers 1/2 and 6/5 mask them
ESSREG_VFM_Ctl:
        test    al,0x80
        jz      .set
        mov     byte [esi+VFM_Status],0
        ret
.set:   push    ecx
        and     al,0x63
        mov     cl,[esi+VFM_Ctl]
        mov     [esi+VFM_Ctl],al
        not     cl
        and     cl,al                   ; timers starting now
        test    cl,3
        jz      .out
        call    ESSREG_Clock
        test    cl,1
        jz      .t2
        mov     [esi+VFM_T1Start],eax
.t2:    test    cl,2
        jz      .out
        mov     [esi+VFM_T2Start],eax
.out:   pop     ecx
        ret

; native register ECX = AL, ESI = virtual chip
ESSREG_VFM_Nat:
        and     ecx,0x7FF
        or      byte [esi+VFM_Flags],VFM_USED
        cmp     ecx,VFM_NAT_REGS
        jae     .glob
        mov     [esi+VFM_Nat+ecx],al
        bts     dword [esi+VFM_NatMask],ecx
        ret
.glob:  and     ecx,0x5FF
        cmp     ecx,0x402
        je      .t1
        cmp     ecx,0x403
        je      .t2
        cmp     ecx,0x404
        je      ESSREG_VFM_Ctl
        push    edx
        xor     edx,edx
        cmp     ecx,0x408
        je      .g
        inc     edx
        cmp     ecx,0x4BD
        je      .g
        inc     edx
        cmp     ecx,0x501
        je      .g
        inc     edx
        cmp     ecx,0x504
        jne     .none
.g:     mov     [esi+VFM_Glob+edx],al
        bts     dword [esi+VFM_GlobMask],edx
.none:  pop     edx
        ret
.t1:    mov     ecx,2
        jmp     ESSREG_VFM_Emu
.t2:    mov     ecx,3
        jmp     ESSREG_VFM_Emu

; AL = the native register at the latch of virtual chip ESI, as the
; ES1869 reads it back (ESFMu's model)
ESSREG_VFM_Readback:
        mov     ecx,[esi+VFM_Latch]
        and     ecx,0x7FF
        cmp     ecx,VFM_NAT_KEYON
        jb      .slot
        cmp     ecx,VFM_NAT_REGS
        jb      .keyon
        and     ecx,0x5FF
        xor     eax,eax
        cmp     ecx,0x402
        je      .t1
        cmp     ecx,0x403
        je      .t2
        cmp     ecx,0x404
        je      .ctl
        cmp     ecx,0x408
        je      .g408
        cmp     ecx,0x4BD
        je      .g4bd
        cmp     ecx,0x501
        je      .g501
        cmp     ecx,0x504
        je      .g504
        cmp     ecx,0x505
        jne     .ret
        mov     al,[esi+VFM_Emu+0x105]  ; NEW, and native mode
        and     al,1
        or      al,0x80
.ret:   ret
.t1:    mov     al,[esi+VFM_T1]
        ret
.t2:    mov     al,[esi+VFM_T2]
        ret
.ctl:   mov     al,[esi+VFM_Ctl]
        ret
.g408:  mov     al,[esi+VFM_Glob]
        and     al,0x40
        ret
.g4bd:  mov     al,[esi+VFM_Glob+1]
        ret
.g501:  mov     al,[esi+VFM_Glob+2]
        ret
.g504:  mov     al,[esi+VFM_Glob+3]
        and     al,0x3F
        ret
.slot:  movzx   eax,byte [esi+VFM_Nat+ecx]
        test    cl,7
        jnz     .ret
        and     al,0xEF                 ; register 0: bit 4 reads bit 6
        test    al,0x40
        jz      .ret
        or      al,0x10
        ret
.keyon: movzx   eax,byte [esi+VFM_Nat+ecx]
        and     al,3
        ret

; AL = the status port of virtual chip ESI: IRQ, FT1, FT2 from its timers
; (80 and 320 us per count, overflowing at 256 and restarting from the
; preset); a masked timer sets no flag
ESSREG_VFM_Status:
        push    ecx
        push    edx
        call    ESSREG_Clock
        mov     edx,eax
        test    byte [esi+VFM_Ctl],1
        jz      .t2
        movzx   ecx,byte [esi+VFM_T1]
        neg     ecx
        add     ecx,256
        imul    ecx,ecx,80
        lea     eax,[esi+VFM_T1Start]
        call    ESSREG_VFM_Timer
        jc      .t2
        test    byte [esi+VFM_Ctl],0x40
        jnz     .t2
        or      byte [esi+VFM_Status],0xC0
.t2:    test    byte [esi+VFM_Ctl],2
        jz      .done
        movzx   ecx,byte [esi+VFM_T2]
        neg     ecx
        add     ecx,256
        imul    ecx,ecx,320
        lea     eax,[esi+VFM_T2Start]
        call    ESSREG_VFM_Timer
        jc      .done
        test    byte [esi+VFM_Ctl],0x20
        jnz     .done
        or      byte [esi+VFM_Status],0xA0
.done:  movzx   eax,byte [esi+VFM_Status]
        pop     edx
        pop     ecx
        ret

; did the timer whose start time is at [EAX] overflow by EDX (now)?
; ECX = period (us); CF clear if it did, and the start moves on to its
; last overflow
ESSREG_VFM_Timer:
        push    edx
        push    ebx
        mov     ebx,eax
        mov     eax,edx
        sub     eax,[ebx]               ; elapsed
        cmp     eax,ecx
        jb      .no
        push    edx
        xor     edx,edx
        div     ecx                     ; EDX = time since the last overflow
        mov     eax,edx
        pop     edx
        sub     edx,eax
        mov     [ebx],edx
        clc
        jmp     .out
.no:    stc
.out:   pop     ebx
        pop     edx
        ret

; the virtual chip of VM EBX goes to the real chip, which it now owns and
; which ESS just reset (or which still holds the VM's state), EDI = ADI:
; OPL3 mode, 4-op, the registers, rhythm, then key-on; the timers; native
; registers if it used native mode; then its address latch
ESSREG_VFM_Replay:
        pushad
        call    ESSREG_VFM_Find
        jz      .done
        mov     esi,eax
        test    byte [esi+VFM_Flags],VFM_USED
        jz      .done
        movzx   edx,word [edi+ADI_FMBase]
        mov     ebx,0x105
        call    ESSREG_VFM_Put
        mov     ebx,0x104
        call    ESSREG_VFM_Put
        xor     ebx,ebx
.regs:  mov     eax,ebx
        and     al,0xF0
        cmp     al,0xB0                 ; B0h-B8h key-on and BDh: later
        je      .skip
        cmp     ebx,0x104
        jae     .hi
        call    ESSREG_VFM_Put
        jmp     .skip
.hi:    cmp     ebx,0x106
        jb      .skip
        call    ESSREG_VFM_Put
.skip:  inc     ebx
        cmp     ebx,0x200
        jb      .regs
        mov     ebx,0xBD
        call    ESSREG_VFM_Put
        mov     ebx,0xB0
.keys:  call    ESSREG_VFM_Put
        inc     bh
        call    ESSREG_VFM_Put
        dec     bh
        inc     ebx
        cmp     ebx,0xB9
        jb      .keys
        ; timers, as the program left them running
        mov     ebx,2
        mov     al,[esi+VFM_T1]
        call    L1_0A78
        inc     ebx
        mov     al,[esi+VFM_T2]
        call    L1_0A78
        inc     ebx
        mov     al,[esi+VFM_Ctl]
        call    L1_0A78
        call    ESSREG_VFM_Native
        ; the address latch, for the access being trapped
        mov     eax,[esi+VFM_Latch]
        test    byte [esi+VFM_Flags],VFM_NATIVE
        jnz     .nlatch
        test    ah,1
        jz      .low
        add     edx,byte 2
.low:   out     dx,al
        jmp     .forget
.nlatch:
        add     edx,byte 2
        out     dx,al
        inc     edx
        mov     al,ah
        out     dx,al
.forget:
        call    ESSREG_VFM_Forget
.done:  popad
        ret

; write emulation register EBX (BH = bank) of virtual chip ESI to the chip
; at EDX, if the program wrote it
ESSREG_VFM_Put:
        movzx   ecx,bx
        bt      dword [esi+VFM_EmuMask],ecx
        jnc     .out
        mov     al,[esi+VFM_Emu+ecx]
        call    L1_0A78
.out:   ret

; the native registers of virtual chip ESI to the chip at EDX: enter
; native mode, slots, globals, key-on; leave it again unless the program
; stayed in it
ESSREG_VFM_Native:
        test    byte [esi+VFM_Flags],VFM_NATIVE
        jnz     .go
        xor     ecx,ecx
.any:   cmp     dword [esi+VFM_NatMask+ecx*4],byte 0
        jne     .go
        inc     ecx
        cmp     ecx,VFM_NAT_REGS / 32 + 1
        jb      .any
        ret
.go:    mov     ebx,0x105
        mov     al,[esi+VFM_Emu+0x105]
        and     al,1
        or      al,0x80
        call    L1_0A78
        xor     ebx,ebx
.slot:  cmp     ebx,VFM_NAT_KEYON
        je      .glob
        call    ESSREG_VFM_NPut
        inc     ebx
        jmp     .slot
.glob:  push    ebx
        xor     ecx,ecx
.g:     bt      dword [esi+VFM_GlobMask],ecx
        jnc     .gn
        mov     bx,[ESSREG_Glob_Regs+ecx*2]
        mov     al,[esi+VFM_Glob+ecx]
        call    ESSREG_NOut
.gn:    inc     ecx
        cmp     ecx,4
        jb      .g
        pop     ebx
.key:   call    ESSREG_VFM_NPut
        inc     ebx
        cmp     ebx,VFM_NAT_REGS
        jb      .key
        test    byte [esi+VFM_Flags],VFM_NATIVE
        jnz     .out
        mov     al,[esi+VFM_Latch]      ; +0 leaves native mode
        out     dx,al
.out:   ret

ESSREG_Glob_Regs:
        dw 0x408, 0x4BD, 0x501, 0x504

; native register EBX of virtual chip ESI to the chip at EDX, if written
ESSREG_VFM_NPut:
        bt      dword [esi+VFM_NatMask],ebx
        jnc     .out
        mov     al,[esi+VFM_Nat+ebx]
        jmp     ESSREG_NOut
.out:   ret

; native register BX = AL on the chip at EDX (in native mode): address low
; and high byte, then the data
ESSREG_NOut:
        push    eax
        add     edx,byte 2
        mov     al,bl
        out     dx,al
        inc     edx
        mov     al,bh
        out     dx,al
        sub     edx,byte 2
        pop     eax
        out     dx,al
        dec     edx
        in      al,dx                   ; a little time between writes
        ret

; virtual chip ESI went to the chip or isn't needed: its registers are
; forgotten, its mode and latch kept
ESSREG_VFM_Forget:
        push    eax
        push    ecx
        push    edi
        and     byte [esi+VFM_Flags],~VFM_USED
        lea     edi,[esi+VFM_EmuMask]
        mov     ecx,(VFM_Emu - VFM_EmuMask) / 4
        xor     eax,eax
        cld
        rep     stosd
        mov     byte [esi+VFM_GlobMask],0
        pop     edi
        pop     ecx
        pop     eax
        ret

; a program ended in VM EBX (DOSMGR_End_V86_App): its virtual Sound
; Blaster and its FM queue end, and its virtual chips let go of the notes
; and timers it left, so they don't sound when the chip comes to the VM. In
; place of the jump to the next hook at the end of ESS's hook (L1_04B8), so
; ESS's code keeps its size: every register and flag goes on as it came
ESSREG_App_End:
        pushfd
        pushad
        call    ESSREG_VSB_End_VM
        call    ESSREG_S16_End_VM
        mov     al,1
        call    ESSREG_FMQ_End_VM
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        push    eax
        push    esi
        call    ESSREG_VFM_Find
        jz      .none
        mov     ecx,0xB0
.keys:  and     byte [eax+VFM_Emu+ecx],~0x20
        and     byte [eax+VFM_Emu+0x100+ecx],~0x20
        inc     ecx
        cmp     ecx,0xB9
        jb      .keys
        and     byte [eax+VFM_Emu+0xBD],~0x1F
        mov     ecx,VFM_NAT_KEYON
.nkeys: and     byte [eax+VFM_Nat+ecx],~1
        inc     ecx
        cmp     ecx,VFM_NAT_REGS
        jb      .nkeys
        and     byte [eax+VFM_Ctl],~3
        mov     byte [eax+VFM_Status],0
.none:  pop     esi
        pop     eax
        pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        popfd
        jmp     [D1_02B6]               ; the next hook

; free the virtual chip of VM EBX for the ADI in EDI
ESSREG_VFM_Drop:
        pushad
        call    ESSREG_VFM_Find
        jz      .none
        mov     dword [esi+NODE_VFM],0
        push    byte 0
        push    eax
        VxDCall _HeapFree
        add     esp,byte 8
.none:  popad
        ret

; free the virtual chips of VM EBX
ESSREG_VFM_Free:
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        call    ESSREG_VFM_Drop
        pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; in place of ESS's removal of VM EBX's node when the device in EDI goes:
; the device's virtual Sound Blaster and FM queue, with their time-outs, and
; the event for a program's channel go first, then the VM's virtual chip
ESSREG_Node_Remove:
        call    ESSREG_VSB_End
        call    ESSREG_VSB_Unschedule
        push    eax
        mov     al,0
        call    ESSREG_FMQ_End
        pop     eax
        mov     dword [edi+EX_FMQ_Direct],0
        call    ESSREG_VFM_Drop
        jmp     L4_00CB

; in place of ESS's VM_Not_Executeable handler: the dying VM's virtual
; Sound Blaster, FM queue and virtual chips go first
ESSREG_VM_Not_Executeable:
        call    ESSREG_VSB_End_VM
        call    ESSREG_S16_End_VM
        push    eax
        mov     al,1
        call    ESSREG_FMQ_End_VM
        pop     eax
        call    ESSREG_VFM_Free
        jmp     AUDDRV_VM_Not_Executeable

; in place of ESS's Sys_Dynamic_Device_Exit: every virtual Sound Blaster,
; FM queue and VM's virtual chips go first, and the hook of interrupt 8
; and the Sound Blaster 16's DMA channel
ESSREG_Dynamic_Exit:
        pushad
        xor     ebx,ebx                 ; no time-out may outlive the VxD
        call    ESSREG_VSB_End_VM
        mov     al,0
        call    ESSREG_FMQ_End_VM
        call    ESSREG_FMQ_Unhook
        call    ESSREG_S16_Unhook
        VxDCall Get_Sys_VM_Handle
        mov     edx,ebx
.vm:    call    ESSREG_VFM_Free
        VxDCall Get_Next_VM_Handle
        cmp     ebx,edx
        jne     .vm
        popad
        jmp     AUDDRV_Dynamic_Exit

; --- a recording takes the DSP from a DOS program ------------------------------

; 040D from Windows before an FM recording opens ES1869.DRV's wave input
; (esfmrec, and ES1869.DRV's FM recording device): a DOS program that has
; the DSP, which it takes with its first Sound Blaster access, gives it up
; for the recording. It gets a virtual Sound Blaster in its place, which
; answers as the chip does and times its transfers and interrupts without
; sound, until it resets the DSP once nobody has it, or ends. Its FM goes on
; to the chip. CF set with INUSE while RecordTakesDSP=0, or for a DOS VM's
; call
ESSREG_API_TakeDSP:
        call    ESSREG_Get_ADI
        jc      .done
        VxDCall Test_Sys_VM_Handle
        jne     .inuse
        mov     eax,[edi+ADI_DSPOwner]
        or      eax,eax
        jz      .ok
        cmp     eax,ebx
        je      .ok
        test    byte [ESSREG_Opts],OPT_REC_TAKES
        jz      .inuse
        call    ESSREG_VSB_Start
.ok:    clc
.done:  ret
.inuse: mov     ax,ESSREG_E_INUSE
        jmp     ESSREG_Fail

; the DOS VM in EAX gives up the DSP, EDI = ADI: an older virtual Sound
; Blaster of the device ends, then what the VM's channel does and its mixer
; as the chip has it, ESS's release (a DSP reset, its ports trapped again,
; Windows' mixer back, and maybe I2S the music DAC, which its FM gets back),
; the chip's answers to E7h and E1h, and the virtual Sound Blaster
ESSREG_VSB_Start:
        pushad
        mov     ebx,eax
        call    ESSREG_VSB_End
        call    ESSREG_VSB_Unschedule
        push    edi
        lea     edi,[edi+EX_VSB_VM]
        mov     ecx,(EX_VSB_Event - EX_VSB_VM) / 4
        xor     eax,eax
        cld
        rep     stosd
        pop     edi
        call    ESSREG_VSB_Mix_Save
        xor     ecx,ecx
        mov     dl,DMA_MASKED           ; no channel yet (the no-DMA
        mov     eax,[edi+ADI_DMA1Handle] ; variant): as masked
        or      eax,eax
        jz      .state
        push    edi
        VxDCall VDMAD_Get_Virt_State    ; ECX = count, DL = mode
        pop     edi
.state: push    ecx
        push    edx
        mov     eax,1
        call    Release_Resources       ; EBX = the VM, EDI = ADI
        cmp     [edi+ADI_FMOwner],ebx
        jne     .vsb
        call    ESSREG_FM_Audible
.vsb:   mov     [edi+EX_VSB_VM],ebx
        mov     byte [edi+EX_VSB_TC],0xD2       ; 22 kHz, as after a reset
        mov     word [edi+EX_VSB_Size],0x07FF
        mov     dword [edi+EX_VSB_Id],0x01038968 ; 68h 89h, 03h 01h
        mov     eax,1                   ; its blocks end on time: VTD's
        VxDCall VTD_Begin_Min_Int_Period ; 1 ms interrupts, not the timer's
        jc      .ask                    ; 55 ms ticks
        or      byte [edi+EX_VSB_Flags2],VSB2_MININT
.ask:   test    byte [edi+EX_S16_Flags],S16_ESS ; the program knows the
        jz      .ask2                   ; chip: its Bxh and Cxh are ESS's
        or      byte [edi+EX_VSB_Flags2],VSB2_ESS
.ask2:  call    ESSREG_VSB_Ask
        pop     edx
        pop     ecx
        ; a transfer that runs goes on as blocks of half its buffer, at the
        ; time constant's 21.7 kHz: what the program set is gone with the
        ; reset, until it starts its next transfer
        test    dl,DMA_MASKED
        jnz     .done
        or      byte [edi+EX_VSB_Flags],VSB_SPEAKER
        inc     ecx
        shr     ecx,1
        jnz     .half
        mov     ecx,0x800
.half:  mov     ax,(VSB_AUTO << 8) | 1
        call    ESSREG_VSB_Go
.done:  popad
        ret

; the chip's answers to E7h (its identification) and E1h (its version)
; into EX_VSB_Id and EX_VSB_Ver, EDI = ADI, while nobody has the DSP: not
; while ESS has the chip powered down (the release may have), when the
; defaults stand. The DSP is waited for with interrupts on, then each
; command's bytes go with them off
ESSREG_VSB_Ask:
        pushad
        test    word [edi+ADI_Flags],0x80
        jnz     .done
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        mov     ecx,ESSREG_POLL_IDLE
.idle:  in      al,dx
        test    al,0x80
        jz      .ready
        loop    .idle
        jmp     .done
.ready: lea     esi,[edi+EX_VSB_Id]
        mov     bl,0xE7
.cmd:   pushfd
        cli
        call    ESSREG_DSP_Drain
        mov     al,bl
        mov     ecx,ESSREG_POLL_BYTE
        call    ESSREG_DSP_Put
        jc      .out
        mov     ecx,ESSREG_POLL_REST
        call    ESSREG_DSP_Get
        jc      .out
        mov     ah,al
        mov     ecx,ESSREG_POLL_REST
        call    ESSREG_DSP_Get
        jc      .out
        popfd
        xchg    al,ah
        mov     [esi],ax
        add     esi,byte 2
        cmp     bl,0xE1
        je      .done
        mov     bl,0xE1
        jmp     .cmd
.out:   popfd
.done:  popad
        ret

; I/O trap of Audio_Base+4h-Fh, in place of ESS's DSP_Port_Trap: the DOS VM
; that gave up the DSP for a recording gets the virtual Sound Blaster
; ESI = ADI, EBX = VM, EDX = port, ECX = type, EAX = data written
ESSREG_DSP_Trap:
        cmp     [esi+EX_S16_VM],ebx
        je      ESSREG_S16_Trap         ; the DSP owner's Sound Blaster 16
        cmp     [esi+EX_VSB_VM],ebx
        je      .vsb
        cmp     dword [esi+ADI_DSPOwner],byte 0
        jne     DSP_Port_Trap
        ; nobody's: the VM takes it with this access, as in ESS's trap
        ; (o5:0C84), and the access goes through its front, if it got one
        push    eax
        push    ebx
        push    ecx
        push    edx
        mov     edi,esi
        mov     eax,1
        call    Acquire_Resources
        pop     edx
        pop     ecx
        pop     ebx
        pop     eax
        jc      .refused
        cmp     [esi+EX_S16_VM],ebx
        je      ESSREG_S16_Trap
        jmp     DSP_Port_Trap           ; the owner's access
.refused:
        mov     eax,1                   ; ESS's "in use" message, and FFh
        mov     edi,esi
        call    Show_Contention_Message
        xor     eax,eax
        dec     eax
        ret
.vsb:   cmp     ecx,byte 4              ; Byte_Input 0, Byte_Output 4
        jbe     .byte
        VxDJmp  Simulate_IO             ; words and strings come back as bytes
.byte:  push    edi
        push    ecx
        push    edx
        mov     edi,esi
        push    ecx
        movzx   ecx,word [edi+ADI_AudioBase]
        sub     edx,ecx                 ; the port's offset
        pop     ecx
        jecxz   .in
        call    ESSREG_VSB_Out
        jmp     short .ret
.in:    call    ESSREG_VSB_In
        movzx   eax,al
.ret:   pop     edx
        pop     ecx
        pop     edi
        ret

; AL written to Audio_Base+EDX of the virtual Sound Blaster, EDI = ADI
ESSREG_VSB_Out:
        cmp     dl,0x0C
        je      ESSREG_VSB_Write
        cmp     dl,0x06
        je      ESSREG_VSB_Reset
        cmp     dl,0x04
        je      .index
        cmp     dl,0x05
        jne     .done
        movzx   edx,byte [edi+EX_VSB_Index]
        or      edx,edx
        jz      .clear                  ; a write to 00h resets the mixer
        cmp     dl,0x40
        jae     .done
        mov     [edi+EX_VSB_Mix+edx],al
        ret
.clear: push    edi
        push    ecx
        lea     edi,[edi+EX_VSB_Mix]
        mov     ecx,64 / 4
        xor     eax,eax
        cld
        rep     stosd
        pop     ecx
        pop     edi
        ret
.index: mov     [edi+EX_VSB_Index],al
.done:  ret

; AL read from Audio_Base+EDX of the virtual Sound Blaster, EDI = ADI
ESSREG_VSB_In:
        cmp     dl,0x0A
        je      .data
        cmp     dl,0x0E
        je      .status
        cmp     dl,0x0F                 ; the Sound Blaster 16's acknowledge
        je      .status                 ; of a 16-bit transfer's interrupt
        cmp     dl,0x0C
        je      .ready
        cmp     dl,0x04
        je      .index
        cmp     dl,0x05
        je      .mixer
        mov     al,0xFF
        ret
.ready: xor     al,al                   ; it takes a command at once
        ret
.index: mov     al,[edi+EX_VSB_Index]
        ret
.mixer: movzx   edx,byte [edi+EX_VSB_Index]
        cmp     dl,0x80                 ; the Sound Blaster 16's IRQ, its
        je      ESSREG_S16_IRQ_Bits     ; channels and the interrupt that
        cmp     dl,0x81                 ; waits
        je      ESSREG_S16_DMA_Bits
        cmp     dl,0x82
        je      .waits
        xor     al,al
        cmp     dl,0x40
        jae     .ret
        mov     al,[edi+EX_VSB_Mix+edx]
.ret:   ret
.waits: xor     al,al
        test    byte [edi+EX_VSB_Flags],VSB_IRQ
        jz      .ret
        mov     al,0x01
        test    byte [edi+EX_VSB_Flags2],VSB2_16
        jz      .ret
        mov     al,0x02
        ret
        ; reading the status acknowledges the interrupt, as on the chip
.status:
        call    ESSREG_VSB_Ack
        mov     al,0x7F
        cmp     byte [edi+EX_VSB_Count],0
        je      .ret
        mov     al,0xFF                 ; a byte to read
        ret
.data:  mov     al,[edi+EX_VSB_Last]
        cmp     byte [edi+EX_VSB_Count],0
        je      .ret
        mov     al,[edi+EX_VSB_Queue]
        mov     [edi+EX_VSB_Last],al
        push    eax
        mov     eax,[edi+EX_VSB_Queue]
        shr     eax,8
        mov     [edi+EX_VSB_Queue],eax
        pop     eax
        dec     byte [edi+EX_VSB_Count]
        ret

; AL into the read queue, EDI = ADI; a full queue loses its oldest byte
ESSREG_VSB_Put:
        push    ecx
        movzx   ecx,byte [edi+EX_VSB_Count]
        cmp     ecx,4
        jb      .room
        push    eax
        mov     eax,[edi+EX_VSB_Queue]
        shr     eax,8
        mov     [edi+EX_VSB_Queue],eax
        pop     eax
        dec     ecx
        dec     byte [edi+EX_VSB_Count]
.room:  mov     [edi+EX_VSB_Queue+ecx],al
        inc     byte [edi+EX_VSB_Count]
        pop     ecx
        ret

; AL written to the reset port, EDI = ADI: 1 then 0 resets the virtual
; Sound Blaster, or, once nobody has the DSP, ends it, and the program gets
; the real DSP back with the same reset, its stereo bit and its channel
ESSREG_VSB_Reset:
        test    al,1
        jz      .low
        or      byte [edi+EX_VSB_Flags],VSB_RESET
        ret
.low:   test    byte [edi+EX_VSB_Flags],VSB_RESET
        jz      .done
        call    ESSREG_S16_Cold         ; its Sound Blaster 16 front, when
                                        ; it's back, as after a reset
        cmp     dword [edi+ADI_DSPOwner],byte 0
        je      .real
        call    ESSREG_VSB_Stop
        call    ESSREG_VSB_Ack
        mov     byte [edi+EX_VSB_Flags],0
        mov     byte [edi+EX_VSB_Need],0
        mov     byte [edi+EX_VSB_Count],0
        mov     word [edi+EX_VSB_Rate],0
        mov     al,0xAA
        jmp     ESSREG_VSB_Put
.real:  pushad
        mov     ebx,[edi+EX_VSB_VM]
        call    ESSREG_VSB_End
        push    ebx
        mov     eax,1
        call    Acquire_Resources       ; its ports untrapped, the DSP reset
        pop     ebx
        jc      .out
        call    ESSREG_VSB_Mix_Put
        call    ESSREG_VSB_Channel
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 6
        mov     al,1
        out     dx,al
        in      al,dx                   ; 3 us high
        in      al,dx
        in      al,dx
        in      al,dx
        xor     al,al
        out     dx,al
.out:   popad
.done:  ret

; the virtual Sound Blaster gone, EDI = ADI: its transfer stopped and its
; interrupt request taken back
ESSREG_VSB_End:
        cmp     dword [edi+EX_VSB_VM],byte 0
        je      .done
        call    ESSREG_VSB_Stop
        call    ESSREG_VSB_Ack
        test    byte [edi+EX_VSB_Flags2],VSB2_MININT
        jz      .vm
        and     byte [edi+EX_VSB_Flags2],~VSB2_MININT & 0xFF
        push    eax
        mov     eax,1
        VxDCall VTD_End_Min_Int_Period
        pop     eax
.vm:    mov     dword [edi+EX_VSB_VM],0
.done:  ret

; the real DSP back to the program once nobody has it and its virtual Sound
; Blaster is idle in Sound Blaster mode, EDI = ADI: after ESS's acquire,
; its stereo bit and its channel, and the speaker, the time constant and
; the block size it set
ESSREG_VSB_Back:
        cmp     dword [edi+EX_VSB_VM],byte 0
        je      .done
        cmp     dword [edi+ADI_DSPOwner],byte 0
        jne     .done
        test    byte [edi+EX_VSB_Flags],VSB_RUN | VSB_PAUSE | VSB_EXT | VSB_IRQ
        jnz     .done
        cmp     byte [edi+EX_VSB_Need],0
        jne     .done                   ; in the middle of a command
        pushad
        mov     ebx,[edi+EX_VSB_VM]
        mov     cl,[edi+EX_VSB_Flags]
        mov     ch,[edi+EX_VSB_TC]
        movzx   esi,word [edi+EX_VSB_Size]
        push    esi
        push    ecx
        push    ebx
        call    ESSREG_VSB_End
        mov     eax,1
        call    Acquire_Resources       ; its ports untrapped, the DSP reset
        pop     ebx
        pop     ecx
        pop     esi
        jc      .out
        call    ESSREG_VSB_Mix_Put
        call    ESSREG_VSB_Channel
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        pushfd
        cli
        test    cl,VSB_SPEAKER
        jz      .tc
        mov     al,0xD1
        call    ESSREG_VSB_Send
        jc      .sent
.tc:    mov     al,0x40
        call    ESSREG_VSB_Send
        jc      .sent
        mov     al,ch
        call    ESSREG_VSB_Send
        jc      .sent
        mov     al,0x48
        call    ESSREG_VSB_Send
        jc      .sent
        mov     eax,esi
        call    ESSREG_VSB_Send
        jc      .sent
        mov     eax,esi
        mov     al,ah
        call    ESSREG_VSB_Send
.sent:  popfd
.out:   popad
.done:  ret

; AL to the DSP, EDX = Audio_Base+Ch; CF set if it stays busy
ESSREG_VSB_Send:
        push    ecx
        mov     ecx,ESSREG_POLL_REST
        call    ESSREG_DSP_Put
        pop     ecx
        ret

; ESSREG_VSB_Back for every device, after Windows let go of the DSP
ESSREG_VSB_Back_All:
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        call    ESSREG_VSB_Back
        pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; the virtual Sound Blaster of VM EBX, and an event for its channel, end on
; every device (its program ended, or the VM did); EBX = 0 ends them all
ESSREG_VSB_End_VM:
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        or      ebx,ebx
        jz      .end
        cmp     [edi+EX_VSB_Event_VM],ebx
        jne     .vsb
        call    ESSREG_VSB_Unschedule
.vsb:   cmp     [edi+EX_VSB_VM],ebx
        jne     .skip
        call    ESSREG_VSB_End
        jmp     short .skip
.end:   call    ESSREG_VSB_End
        call    ESSREG_VSB_Unschedule
.skip:  pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; the program's mixer registers 00h-3Fh as the chip has them, into the
; virtual Sound Blaster's copy, EDI = ADI
ESSREG_VSB_Mix_Save:
        pushad
        movzx   edx,word [edi+ADI_AudioBase]
        xor     ecx,ecx
.reg:   mov     ah,cl
        call    ESSREG_Mix_Read
        mov     [edi+EX_VSB_Mix+ecx],al
        inc     ecx
        cmp     cl,0x40
        jb      .reg
        popad
        ret

; the program's stereo bit and output filter (0Eh) from the virtual Sound
; Blaster's copy back to the chip, EDI = ADI. The levels stay Windows', as
; after any DOS program
ESSREG_VSB_Mix_Put:
        push    eax
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        mov     ah,0x0E
        mov     al,[edi+EX_VSB_Mix+0x0E]
        call    ESSREG_Mix_Write
        pop     edx
        pop     eax
        ret

; once the program has the real DSP back, EBX = its VM, EDI = ADI: an event
; in its VM, which runs before the program does, gives Audio 1's channel
; the state the program set meanwhile, which ESSREG_DMA1 kept off the chip
ESSREG_VSB_Channel:
        pushad
        call    ESSREG_VSB_Unschedule
        mov     edx,edi                 ; its reference data: the ADI
        mov     esi,ESSREG_VSB_Event
        VxDCall Schedule_VM_Event       ; ESI = its handle
        mov     [edi+EX_VSB_Event_VM],ebx
        mov     [edi+EX_VSB_Event],esi
        popad
        ret

; the event of ESSREG_VSB_Channel, EBX = the program's VM, EDX = ADI: ESS's
; DMA handler, as VDMAD calls it when a VM programs the channel, while the
; VM still has the DSP
ESSREG_VSB_Event:
        pushad
        mov     edi,edx
        mov     dword [edi+EX_VSB_Event],0
        cmp     [edi+ADI_DSPOwner],ebx
        jne     .done
        mov     eax,[edi+ADI_DMA1Handle]
        or      eax,eax
        jz      .done
        call    L1_0518
.done:  popad
        ret

; the event of ESSREG_VSB_Channel cancelled, if it hasn't run, EDI = ADI
ESSREG_VSB_Unschedule:
        pushad
        xor     esi,esi
        xchg    esi,[edi+EX_VSB_Event]
        or      esi,esi
        jz      .done
        mov     ebx,[edi+EX_VSB_Event_VM]
        VxDCall Cancel_VM_Event
.done:  popad
        ret

; a byte AL to the virtual Sound Blaster's command port, EDI = ADI: a
; command, or the operand it waits for
ESSREG_VSB_Write:
        cmp     byte [edi+EX_VSB_Need],0
        jne     .operand
        mov     [edi+EX_VSB_Cmd],al
        mov     byte [edi+EX_VSB_Got],0
        mov     word [edi+EX_VSB_Ops],0
        call    ESSREG_VSB_Operands
        mov     [edi+EX_VSB_Need],cl
        or      cl,cl
        jz      ESSREG_VSB_Command
        ret
.operand:
        movzx   ecx,byte [edi+EX_VSB_Got]
        cmp     byte [edi+EX_VSB_Cmd],0xC6      ; C6h, then not a Sound
        jne     .nth                    ; Blaster 16 transfer's mode (bits
        or      ecx,ecx                 ; 4 and 5 only): the chip's Extended
        jnz     .nth                    ; mode on, and this byte a command
        test    al,0xCF
        jz      .nth
        or      byte [edi+EX_VSB_Flags],VSB_EXT
        mov     byte [edi+EX_VSB_Need],0
        jmp     ESSREG_VSB_Write
.nth:   cmp     ecx,byte 2
        jb      .two
        mov     [edi+EX_VSB_Ops3],al    ; the third, of a Sound Blaster 16
        jmp     short .got              ; transfer
.two:   mov     [edi+EX_VSB_Ops+ecx],al
.got:   inc     byte [edi+EX_VSB_Got]
        dec     byte [edi+EX_VSB_Need]
        jz      ESSREG_VSB_Command
        ret

; CL = the operands of command AL, EDI = ADI: the Sound Blaster's commands,
; the Sound Blaster 16's transfers (Bxh and Cxh, for a program that doesn't
; know the chip, with SoundBlaster16=1), and in Extended mode the
; controller registers (A0h-BFh) and C0h
ESSREG_VSB_Operands:
        call    ESSREG_VSB_Is16
        jnz     .sb
        test    al,0x01                 ; an odd one is no transfer (C7h)
        jnz     .sb
        mov     cl,3                    ; a transfer: its mode and length
        cmp     al,0xB0
        jb      .sb
        cmp     al,0xCF
        jbe     .done
.sb:    mov     cl,2
        cmp     al,0x14                 ; 8-bit, ADPCM and silence transfers
        je      .done
        cmp     al,0x15
        je      .done
        cmp     al,0x16
        je      .done
        cmp     al,0x17
        je      .done
        cmp     al,0x24
        je      .done
        cmp     al,0x25
        je      .done
        cmp     al,0x41                 ; rate
        je      .done
        cmp     al,0x42
        je      .done
        cmp     al,0x48                 ; block size
        je      .done
        cmp     al,0x80
        je      .done
        cmp     al,0x74
        jb      .one
        cmp     al,0x77
        jbe     .done
.one:   mov     cl,1
        cmp     al,0x10                 ; direct DAC
        je      .done
        cmp     al,0x38                 ; MIDI out
        je      .done
        cmp     al,0x40                 ; time constant
        je      .done
        cmp     al,0xE0
        je      .done
        cmp     al,0xE4
        je      .done
        test    byte [edi+EX_VSB_Flags],VSB_EXT
        jz      .none
        cmp     al,0xC0                 ; Extended mode: read a register
        je      .done
        cmp     al,0xA0                 ; and write one
        jb      .none
        cmp     al,0xBF
        jbe     .done
.none:  mov     cl,0
.done:  ret

; ZF set when the virtual Sound Blaster is a Sound Blaster 16, EDI = ADI:
; SoundBlaster16=1, the program doesn't know the chip (S16_ESS), and it
; isn't in Extended mode, where Bxh and C0h are the chip's
ESSREG_VSB_Is16:
        test    byte [ESSREG_Opts+1],OPT_SB16 >> 8
        jz      .no
        test    byte [edi+EX_VSB_Flags2],VSB2_ESS
        jnz     .no
        test    byte [edi+EX_VSB_Flags],VSB_EXT
        ret
.no:    or      esp,esp                 ; ZF clear
        ret

; the command in EX_VSB_Cmd with its operands, EDI = ADI
ESSREG_VSB_Command:
        movzx   eax,byte [edi+EX_VSB_Cmd]
        movzx   edx,word [edi+EX_VSB_Ops]
        cmp     al,0xB0                 ; the Sound Blaster 16's transfers
        jb      .notx
        cmp     al,0xCF
        ja      .notx
        test    al,0x01
        jnz     .notx
        call    ESSREG_VSB_Is16
        je      .x16
.notx:  cmp     al,0xD5                 ; its pause, continue and stop of a
        je      ESSREG_VSB_Pause        ; 16-bit transfer, and its 16-bit
        cmp     al,0xD6                 ; interrupt
        je      ESSREG_VSB_Continue
        cmp     al,0xF3
        je      .irq16
        cmp     al,0x40
        je      .tc
        cmp     al,0x41
        je      .rate
        cmp     al,0x42
        je      .rate
        cmp     al,0x48
        je      .size
        cmp     al,0x14
        je      .pcm
        cmp     al,0x24
        je      .pcm
        cmp     al,0x80
        je      .pcm
        cmp     al,0x16
        je      .adpcm2
        cmp     al,0x17
        je      .adpcm2
        cmp     al,0x74
        je      .adpcm4
        cmp     al,0x75
        je      .adpcm4
        cmp     al,0x76
        je      .adpcm3
        cmp     al,0x77
        je      .adpcm3
        cmp     al,0x1C
        je      .auto
        cmp     al,0x2C
        je      .auto
        cmp     al,0x1F
        je      .auto2
        cmp     al,0x7D
        je      .auto4
        cmp     al,0x7F
        je      .auto3
        cmp     al,0x90
        je      .auto
        cmp     al,0x98
        je      .auto
        cmp     al,0x91
        je      .block
        cmp     al,0x99
        je      .block
        cmp     al,0xD0
        je      ESSREG_VSB_Pause
        cmp     al,0xD4
        je      ESSREG_VSB_Continue
        cmp     al,0xDA
        je      .noauto
        cmp     al,0xD9
        je      .noauto
        cmp     al,0xD1
        je      .spkon
        cmp     al,0xD3
        je      .spkoff
        cmp     al,0xD8
        je      .spk
        cmp     al,0x20
        je      .adc
        cmp     al,0xE0
        je      .ident
        cmp     al,0xE1
        je      .version
        cmp     al,0xE4
        je      .test
        cmp     al,0xE8
        je      .testrd
        cmp     al,0xE7
        je      .ess
        cmp     al,0xF2
        je      ESSREG_VSB_IRQ
        cmp     al,0xC6
        je      .exton
        cmp     al,0xC7
        je      .extoff
        test    byte [edi+EX_VSB_Flags],VSB_EXT
        jz      .done
        cmp     al,0xC0
        je      .regrd
        cmp     al,0xA0
        jb      .done
        cmp     al,0xBF
        ja      .done
        mov     [edi+EX_VSB_Ess+eax-0xA0],dl
        cmp     al,0xB8
        je      ESSREG_VSB_Ext
.done:  ret
.tc:    mov     [edi+EX_VSB_TC],dl
        mov     word [edi+EX_VSB_Rate],0
        ret
.rate:  xchg    dl,dh                   ; high byte first
        mov     [edi+EX_VSB_Rate],dx
        ret
.size:  mov     [edi+EX_VSB_Size],dx
        ret
.pcm:   mov     ax,1
        jmp     short .single
.adpcm2:
        mov     ax,4
        jmp     short .single
.adpcm4:
        mov     ax,2
        jmp     short .single
.adpcm3:
        mov     ax,3
.single:
        lea     ecx,[edx+1]
        jmp     short .old
.auto:  mov     ax,(VSB_AUTO << 8) | 1
        jmp     short .autogo
.auto2: mov     ax,(VSB_AUTO << 8) | 4
        jmp     short .autogo
.auto4: mov     ax,(VSB_AUTO << 8) | 2
        jmp     short .autogo
.auto3: mov     ax,(VSB_AUTO << 8) | 3
.autogo:
        movzx   ecx,word [edi+EX_VSB_Size]
        inc     ecx
        jmp     short .old
.block: movzx   ecx,word [edi+EX_VSB_Size]
        inc     ecx
        mov     ax,1
.old:   and     byte [edi+EX_VSB_Flags2],~(VSB2_16 | VSB2_STEREO) & 0xFF
        jmp     ESSREG_VSB_Go
        ; a Sound Blaster 16 transfer: 16-bit (Bxh) or 8-bit (Cxh), with
        ; its mode (bit 5 stereo) and its length in samples less one, in
        ; the rate of 41h/42h or of the time constant
.x16:   movzx   ecx,byte [edi+EX_VSB_Ops3]
        shl     ecx,8
        mov     cl,[edi+EX_VSB_Ops+1]
        inc     ecx
        mov     ah,[edi+EX_VSB_Flags2]
        and     ah,~(VSB2_16 | VSB2_STEREO) & 0xFF
        test    byte [edi+EX_VSB_Ops],0x20
        jz      .mono16
        or      ah,VSB2_STEREO
.mono16:
        test    al,0x10
        jz      .bytes16                ; Cxh: 8-bit
        shl     ecx,1                   ; two bytes a sample
        or      ah,VSB2_16
.bytes16:
        mov     [edi+EX_VSB_Flags2],ah
        xor     ah,ah
        test    al,0x04
        jz      .once16
        mov     ah,VSB_AUTO
.once16:
        mov     al,1
        jmp     ESSREG_VSB_Go
.irq16: or      byte [edi+EX_VSB_Flags2],VSB2_16
        jmp     ESSREG_VSB_IRQ
.noauto:
        and     byte [edi+EX_VSB_Flags],~VSB_AUTO & 0xFF
        ret
.spkon: or      byte [edi+EX_VSB_Flags],VSB_SPEAKER
        ret
.spkoff:
        and     byte [edi+EX_VSB_Flags],~VSB_SPEAKER & 0xFF
        ret
.spk:   mov     al,0
        test    byte [edi+EX_VSB_Flags],VSB_SPEAKER
        jz      .put
        mov     al,0xFF
.put:   jmp     ESSREG_VSB_Put
.adc:   mov     al,0x80                 ; silence
        jmp     .put
.ident: mov     al,dl
        not     al
        jmp     .put
.version:
        call    ESSREG_VSB_Is16         ; a Sound Blaster 16's version, as
        jne     .chipver               ; the front answers
        mov     al,S16_VERSION >> 8
        call    ESSREG_VSB_Put
        mov     al,S16_VERSION & 0xFF
        jmp     .put
.chipver:
        mov     al,[edi+EX_VSB_Ver]
        call    ESSREG_VSB_Put
        mov     al,[edi+EX_VSB_Ver+1]
        jmp     .put
.ess:   or      byte [edi+EX_VSB_Flags2],VSB2_ESS ; it knows the chip: its
        mov     al,[edi+EX_VSB_Id]      ; Bxh and Cxh are the chip's
        call    ESSREG_VSB_Put
        mov     al,[edi+EX_VSB_Id+1]
        jmp     .put
.test:  mov     [edi+EX_VSB_Test],dl
        ret
.testrd:
        mov     al,[edi+EX_VSB_Test]
        jmp     .put
.exton: or      byte [edi+EX_VSB_Flags],VSB_EXT
        ret
.extoff:
        and     byte [edi+EX_VSB_Flags],~VSB_EXT & 0xFF
        ret
.regrd: mov     al,0
        movzx   edx,dl
        cmp     dl,0xA0
        jb      .put
        cmp     dl,0xBF
        ja      .put
        mov     al,[edi+EX_VSB_Ess+edx-0xA0]
        jmp     .put

; B8h written in Extended mode, EDI = ADI: bit 0 starts or stops the
; transfer of the count in A4h/A5h (two's complement), auto-initializing
; with bit 2, and interrupting at its end with B1h bit 6
ESSREG_VSB_Ext:
        test    byte [edi+EX_VSB_Ess+0x18],1
        jz      ESSREG_VSB_Stop
        movzx   ecx,word [edi+EX_VSB_Ess+4]     ; A4h, A5h
        neg     ecx
        and     ecx,0xFFFF
        jnz     .count
        mov     ecx,0x10000
.count: xor     ah,ah
        test    byte [edi+EX_VSB_Ess+0x18],4
        jz      .once
        mov     ah,VSB_AUTO
.once:  test    byte [edi+EX_VSB_Ess+0x11],0x40 ; B1h bit 6
        jnz     .irq
        or      ah,VSB_NOIRQ
.irq:   mov     al,1
        jmp     ESSREG_VSB_Go

; a transfer of ECX bytes starts, AL samples a byte (ADPCM 2-4, else 1),
; AH = VSB_AUTO and VSB_NOIRQ, EDI = ADI: its block lasts as long as the
; chip would take
ESSREG_VSB_Go:
        pushad
        call    ESSREG_VSB_Stop
        and     byte [edi+EX_VSB_Flags],~VSB_NOIRQ & 0xFF
        movzx   ebx,al
        imul    ecx,ebx                 ; samples (bytes, for PCM)
        mov     bl,ah
        call    ESSREG_VSB_Rate         ; EAX = bytes a second
        xchg    eax,ecx
        mov     edx,1024000             ; 1/1024 ms in a second
        mul     edx
        cmp     edx,ecx
        jae     .long
        div     ecx
        jmp     short .len
.long:  mov     eax,0x7FFFFFFF
.len:   cmp     eax,1024
        jae     .ok
        mov     eax,1024                ; 1 ms at least
.ok:    mov     [edi+EX_VSB_Block],eax
        or      bl,VSB_RUN
        or      [edi+EX_VSB_Flags],bl
        VxDCall Get_System_Time
        shl     eax,10
        add     eax,[edi+EX_VSB_Block]
        mov     [edi+EX_VSB_Due],eax
        call    ESSREG_VSB_Arm
        popad
        ret

; EAX = the bytes a second of the transfer, EDI = ADI: Extended mode's
; rate (A1h, as the chip clocks it), channels (A8h) and sample size (B7h
; bit 2), else the rate of 41h, else the time constant, twice for a 16-bit
; Sound Blaster 16 transfer and twice for a stereo one
ESSREG_VSB_Rate:
        push    ecx
        push    edx
        test    byte [edi+EX_VSB_Flags],VSB_EXT
        jz      .sb
        movzx   ecx,byte [edi+EX_VSB_Ess+1]     ; A1h
        mov     eax,397700
        test    cl,0x80
        jz      .low
        mov     eax,795500
        neg     ecx
        add     ecx,256
        jmp     short .div
.low:   neg     ecx
        add     ecx,128
.div:   xor     edx,edx
        div     ecx                     ; samples a second
        test    byte [edi+EX_VSB_Ess+8],1       ; A8h: 01 stereo
        jz      .mono
        shl     eax,1
.mono:  test    byte [edi+EX_VSB_Ess+0x17],4    ; B7h bit 2: 16-bit
        jz      .done
        shl     eax,1
        jmp     short .done
.sb:    movzx   eax,word [edi+EX_VSB_Rate]
        or      eax,eax
        jnz     .frame
        movzx   ecx,byte [edi+EX_VSB_TC]
        neg     ecx
        add     ecx,256
        mov     eax,1000000
        xor     edx,edx
        div     ecx
.frame: test    byte [edi+EX_VSB_Flags2],VSB2_16
        jz      .chans
        shl     eax,1
.chans: test    byte [edi+EX_VSB_Flags2],VSB2_STEREO
        jz      .done
        shl     eax,1
.done:  or      eax,eax
        jnz     .out
        mov     eax,22050
.out:   pop     edx
        pop     ecx
        ret

; the time-out at EX_VSB_Due, EDI = ADI
ESSREG_VSB_Arm:
        pushad
        VxDCall Get_System_Time
        shl     eax,10
        mov     ecx,[edi+EX_VSB_Due]
        sub     ecx,eax                 ; 1/1024 ms from now
        jns     .later
        xor     ecx,ecx
.later: add     ecx,1023
        shr     ecx,10                  ; ms, rounded up
        jnz     .ms
        inc     ecx
.ms:    mov     eax,ecx
        mov     edx,edi                 ; its reference data: the ADI
        mov     esi,ESSREG_VSB_Tick
        VxDCall Set_Global_Time_Out     ; ESI = its handle
        mov     [edi+EX_VSB_Timer],esi
        popad
        ret

; the transfer stopped and its time-out cancelled, EDI = ADI
ESSREG_VSB_Stop:
        push    esi
        xor     esi,esi
        xchg    esi,[edi+EX_VSB_Timer]
        or      esi,esi
        jz      .none
        VxDCall Cancel_Time_Out
.none:  and     byte [edi+EX_VSB_Flags],~(VSB_RUN | VSB_AUTO | VSB_PAUSE) & 0xFF
        pop     esi
        ret

; D0h, EDI = ADI: the transfer waits with what's left of its block
ESSREG_VSB_Pause:
        test    byte [edi+EX_VSB_Flags],VSB_RUN
        jz      .done
        test    byte [edi+EX_VSB_Flags],VSB_PAUSE
        jnz     .done
        push    eax
        push    esi
        xor     esi,esi
        xchg    esi,[edi+EX_VSB_Timer]
        or      esi,esi
        jz      .left
        VxDCall Cancel_Time_Out
.left:  VxDCall Get_System_Time
        shl     eax,10
        neg     eax
        add     eax,[edi+EX_VSB_Due]
        jns     .keep
        xor     eax,eax
.keep:  mov     [edi+EX_VSB_Due],eax
        or      byte [edi+EX_VSB_Flags],VSB_PAUSE
        pop     esi
        pop     eax
.done:  ret

; D4h, EDI = ADI: the transfer goes on
ESSREG_VSB_Continue:
        test    byte [edi+EX_VSB_Flags],VSB_PAUSE
        jz      .done
        and     byte [edi+EX_VSB_Flags],~VSB_PAUSE & 0xFF
        push    eax
        VxDCall Get_System_Time
        shl     eax,10
        add     [edi+EX_VSB_Due],eax
        pop     eax
        call    ESSREG_VSB_Arm
.done:  ret

; the end of a block (Set_Global_Time_Out), EDX = ADI: the program's
; interrupt, then the next block, or once idle maybe the real DSP back
ESSREG_VSB_Tick:
        pushad
        mov     edi,edx
        mov     dword [edi+EX_VSB_Timer],0
        cmp     dword [edi+EX_VSB_VM],byte 0
        je      .done
        test    byte [edi+EX_VSB_Flags],VSB_RUN
        jz      .done
        test    byte [edi+EX_VSB_Flags],VSB_NOIRQ
        jnz     .next
        call    ESSREG_VSB_IRQ
.next:  test    byte [edi+EX_VSB_Flags],VSB_AUTO
        jz      .end
        mov     eax,[edi+EX_VSB_Block]
        add     [edi+EX_VSB_Due],eax
        call    ESSREG_VSB_Arm
        jmp     short .done
.end:   and     byte [edi+EX_VSB_Flags],~VSB_RUN & 0xFF
.done:  popad
        ret

; the program's interrupt request, EDI = ADI
ESSREG_VSB_IRQ:
        pushad
        or      byte [edi+EX_VSB_Flags],VSB_IRQ
        mov     eax,[edi+ADI_IRQHandle]
        mov     ebx,[edi+EX_VSB_VM]
        VxDCall VPICD_Set_Int_Request
        popad
        ret

; the request taken back, EDI = ADI: the program read the status, or its
; virtual Sound Blaster ends
ESSREG_VSB_Ack:
        test    byte [edi+EX_VSB_Flags],VSB_IRQ
        jz      .done
        and     byte [edi+EX_VSB_Flags],~VSB_IRQ & 0xFF
        pushad
        mov     eax,[edi+ADI_IRQHandle]
        mov     ebx,[edi+EX_VSB_VM]
        VxDCall VPICD_Clear_Int_Request
        popad
.done:  ret

; in place of ESS's VID_EOI_Proc (L1_0734), EAX = IRQ handle, EBX = VM: an
; interrupt of the virtual Sound Blaster has no physical one to end
ESSREG_EOI:
        push    edi
        push    eax
        push    ecx
        push    edx
        push    byte 1                  ; key type 1: the IRQ handle
        push    eax
        call    _AUDDRV_Get_pADI_From_XXX
        add     esp,byte 8
        pop     edx
        pop     ecx
        pop     eax
        or      edi,edi
        jz      .ess
        cmp     [edi+EX_S16_VM],ebx
        jne     .vsb
        call    ESSREG_S16_Position     ; its 16-bit channel's count, at the
.vsb:   cmp     [edi+EX_VSB_VM],ebx     ; end of a block
        jne     .ess
        test    word [edi+4],0x1000     ; a physical MPU-401 interrupt of
        jnz     .ess                    ; its own on the shared IRQ: ESS's
        pop     edi                     ; EOI ends it
        VxDCall VPICD_Clear_Int_Request
        clc
        ret
.ess:   pop     edi
        jmp     L1_0734

; --- a Sound Blaster 16 for DOS programs -----------------------------------------

; The chip answers DSP commands as a Sound Blaster Pro (version 3.01) and
; plays 16-bit sound only through its own Extended mode, so a DOS program
; written for the Sound Blaster 16 plays 8-bit sound on it at best. With
; SoundBlaster16=1, a DOS VM that takes the DSP finds a Sound Blaster 16 in
; front of the chip, the same as esssb16.com's: the VxD keeps the mixer,
; reset, read data, command, status and 16-bit acknowledge ports trapped
; for the VM (ESSREG_S16_Start, from Acquire_Resources) and answers as DSP
; 4.05 and the CT1745 mixer do. Every command takes the operands the DSP's
; firmware takes, an answer goes out a byte at a time through the read
; latch, the rate generator of 40h, 41h and 42h runs at v * 44100 / 248
; Hz, and the 8-bit and 16-bit transfers have their own interrupts in mixer
; 82h and their own acknowledges (+Eh and +Fh).
;
; Every PCM transfer plays or records in the chip's Extended mode
; (ESSREG_S16_Xfer): the 8-bit ones and the recordings on Audio 1, and a
; 16-bit playback on Audio 2 (ESSREG_S16_A2_Xfer), so that an 8-bit and a
; 16-bit transfer play at once, as on the Sound Blaster 16. That takes
; Audio 2 on an 8-bit channel other than Audio 1's, and
; SoundBlaster16Audio2=1; else the 16-bit transfer plays on Audio 1 too,
; one transfer at a time. A DSP reset would take the chip out of Extended
; mode, so the front emulates the program's reset. The DMA runs as for any
; DOS program, on the DACs' 8-bit channels, where a 16-bit sample is two
; bytes. A program that uses a 16-bit channel (SoundBlaster16DMA, 5)
; programs it as ever: the VxD owns that channel, or ESS's code does when
; it is Audio 2's, and carries the programming over to the channel of the
; DAC that plays it, in bytes (ESSREG_S16_DMA_State), and VDMAD tells it
; when the program reads the channel's count, which it takes from that
; channel (ESSREG_S16_Position). The firmware's commands that Extended mode
; doesn't have (the direct DAC and ADC, ADPCM, silence and MIDI) go to the
; chip in its Compatibility mode, after a reset of its own. The Sound
; Blaster 16's mixer registers go to the chip's own volumes
; (ESSREG_S16_Mix_*), and 80h-82h answer with the card's IRQ, its channels
; and the interrupts that are pending. A program that knows the chip (C6h,
; then a register command) gets the chip as it is until its next reset.

; the VM in EBX took the DSP (Acquire_Resources), EDI = ADI, after ESS's
; code untrapped the audio ports for it: a DOS VM gets the front, and the
; front's ports trapped again. A new program finds the DSP as after a cold
; boot and the chip's levels in the view; the program that gave the DSP up
; for a recording finds its settings again
ESSREG_DSP_Untrap:
        call    Audio_Disable_Local_Trapping
ESSREG_S16_Start:
        test    byte [ESSREG_Opts+1],OPT_SB16 >> 8
        jz      .out
        VxDCall Test_Sys_VM_Handle
        je      .out
        test    word [edi+ADI_Flags],0x20       ; the no-DMA emulation: ESS's
        jnz     .out
        pushad
        mov     [edi+EX_S16_VM],ebx
        mov     dword [edi+EX_S16_Known],0      ; the chip was reset: its
        and     byte [edi+EX_S16_Flags],S16_ESS ; registers unknown, and
        mov     byte [edi+EX_S16_Flags2],0      ; ESS's code stopped Audio 2
        mov     byte [edi+EX_S16_To],0
        call    ESSREG_S16_A2_Chan
        cmp     [edi+EX_S16_Last],ebx   ; the same program back (after a
        je      .same                   ; recording): its settings stay,
        mov     [edi+EX_S16_Last],ebx   ; its levels go back to the chip
        mov     byte [edi+EX_S16_Flags],0
        mov     byte [edi+EX_S16_State],0
        mov     byte [edi+EX_S16_Test],0
        call    ESSREG_S16_Cold
        mov     eax,[ESSREG_S16_Chan]
        mov     [edi+EX_S16_Sel],al
        call    ESSREG_S16_Mix_Chip     ; a new one sees the chip's levels
        jmp     short .ports
.same:  test    byte [edi+EX_S16_Flags],S16_ESS
        jnz     .ports
        call    ESSREG_S16_Mix_All
.ports: and     byte [edi+EX_S16_State],S16S_WARM | S16S_SPKR
        mov     byte [edi+EX_S16_Need],0
        mov     byte [edi+EX_S16_Alen],0
        mov     byte [edi+EX_S16_DMA],0
        mov     byte [edi+EX_S16_Index],0xFF    ; the chip's until it writes one
        call    ESSREG_S16_Channel
        movzx   edx,word [edi+ADI_AudioBase]
        mov     esi,S16_PORTS
.port:  shr     esi,1
        jnc     .next
        VxDCall Enable_Local_Trapping
.next:  inc     edx
        or      esi,esi
        jnz     .port
        popad
.out:   ret

; Audio 2's channel for the front's 16-bit playback, EDI = ADI: an 8-bit
; one other than Audio 1's, ESS's from VDMAD, with SoundBlaster16Audio2=1;
; 4 for none, and the 16-bit playback on Audio 1 then
ESSREG_S16_A2_Chan:
        mov     al,4
        cmp     dword [ESSREG_S16_Audio2],0
        je      .set
        cmp     dword [edi+ADI_DMA2Handle],0
        je      .set
        mov     ah,[edi+ADI_DMA2Channel]
        cmp     ah,3
        ja      .set
        cmp     ah,2
        je      .set
        cmp     ah,[edi+ADI_DMA1Channel]
        je      .set
        mov     al,ah
.set:   mov     [edi+EX_S16_A2],al
        ret

; the 16-bit channel for the VM's Sound Blaster 16, EDI = ADI: Audio 2's
; when it is that channel (ESSREG_DMA2 gets the VM's programming), else
; the VxD takes it from VDMAD once (ESSREG_S16_DMA gets it); either way
; with the VM's reads of its count notified while the front has it. The
; front carries it over while the program's mixer 81h picks it
ESSREG_S16_Channel:
        mov     byte [edi+EX_S16_Chan],0
        mov     eax,[ESSREG_S16_Chan]
        or      eax,eax
        jz      .out
        cmp     [edi+ADI_DMA2Channel],al
        jne     .own
        mov     eax,[edi+ADI_DMA2Handle]
        jmp     short .notify
.own:   cmp     dword [ESSREG_S16_Virt],0
        jne     .mine
        mov     esi,ESSREG_S16_DMA
        VxDCall VDMAD_Virtualize_Channel
        jc      .out
        mov     [ESSREG_S16_Virt],eax
.notify:
        or      eax,eax
        jz      .mine
        push    ebx
        mov     ebx,0xFFFF              ; its callbacks kept, and the reads
        mov     ecx,1                   ; of the count notified
        VxDCall VDMAD_Set_Channel_Callbacks
        pop     ebx
.mine:  mov     eax,[ESSREG_S16_Chan]
        cmp     [edi+EX_S16_Sel],al
        jne     .out
        mov     [edi+EX_S16_Chan],al
.out:   ret

; the VxD's 16-bit channel goes back to VDMAD (Sys_Dynamic_Device_Exit)
ESSREG_S16_Unhook:
        push    eax
        xor     eax,eax
        xchg    eax,[ESSREG_S16_Virt]
        or      eax,eax
        jz      .out
        VxDCall VDMAD_Unvirtualize_Channel
.out:   pop     eax
        ret

; the VM in EBX lets go of the DSP (Release_Resources), EDI = ADI: its
; front's transfer stops, and its front ends, its settings kept for the
; program's return
ESSREG_S16_End:
        cmp     [edi+EX_S16_VM],ebx
        jne     .out
        call    ESSREG_S16_Mix_Fresh    ; its levels, for its return
        pushad
        call    ESSREG_S16_A1_Stop
        call    ESSREG_S16_A2_Stop      ; and Audio 2's, which a reset leaves
        call    ESSREG_S16_A2_Off
        cmp     byte [edi+EX_S16_DMA],0
        je      .reads
        mov     byte [edi+EX_S16_DMA],0
        call    ESSREG_S16_To_Handle
        or      eax,eax
        jz      .reads
        VxDCall VDMAD_Mask_Channel
.reads: call    ESSREG_S16_Handle       ; its count's reads no longer
        or      eax,eax                 ; notified
        jz      .pop
        mov     ebx,0xFFFE
        xor     ecx,ecx
        VxDCall VDMAD_Set_Channel_Callbacks
.pop:   popad
        mov     dword [edi+EX_S16_VM],0
.out:   ret

; the VM in EBX ends (VM_Not_Executeable), or its program did
; (DOSMGR_End_V86_App): its front on every device, and the settings kept
; for it, which the next program starts without
ESSREG_S16_End_VM:
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        call    ESSREG_S16_End
        cmp     [edi+EX_S16_Last],ebx
        jne     .list
        mov     dword [edi+EX_S16_Last],0
.list:  pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; EDI = the ADI whose front serves the VM in EBX, 0 if none
ESSREG_S16_ADI:
        push    eax
        push    esi
        xor     edi,edi
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        jz      .none
.next:  mov     edi,[eax]
        cmp     [edi+EX_S16_VM],ebx
        je      .found
        VxDCall List_Get_Next
        jnz     .next
.none:  xor     edi,edi
.found: popfd
.done:  pop     esi
        pop     eax
        ret

; I/O trap of the front's ports for its VM: ESI = ADI, EBX = VM, EDX =
; port, ECX = type, EAX = data written; returns EAX = data read. The front
; runs with EDI = ADI, ESI = the port, EDX = its offset from Audio_Base,
; EBX = VM; every register but EAX comes back
ESSREG_S16_Trap:
        cmp     ecx,byte 4              ; Byte_Input 0, Byte_Output 4
        jbe     .byte
        VxDJmp  Simulate_IO             ; words and strings come back as bytes
.byte:  pushad
        mov     edi,esi
        mov     esi,edx                 ; ESI = the port
        movzx   ecx,word [edi+ADI_AudioBase]
        sub     edx,ecx                 ; EDX = its offset
        cmp     dword [esp+24],byte 0   ; the type (the saved ECX)
        jne     .out
        call    ESSREG_S16_In
        movzx   eax,al
        mov     [esp+28],eax            ; the saved EAX
        popad
        ret
.out:   call    ESSREG_S16_Out
        popad
        ret

; AL written to Audio_Base+EDX by the VM, ESI = the port, EDI = ADI; a
; write to the front's other ports (+Ah, +Eh, +Fh) does nothing, as on a
; Sound Blaster 16, but for a program that knows the chip (+Fh is the
; chip's FIFO)
ESSREG_S16_Out:
        cmp     dl,0x0C
        je      ESSREG_S16_Write
        cmp     dl,0x05
        je      ESSREG_S16_Mix_Write
        cmp     dl,0x06
        je      ESSREG_S16_Reset
        cmp     dl,0x04
        je      .index
        test    byte [edi+EX_S16_Flags],S16_ESS
        jnz     ESSREG_S16_Chip_Out
        ret
.index: mov     [edi+EX_S16_Index],al   ; the chip gets the index too
        jmp     ESSREG_S16_Chip_Out

; AL to the chip's port ESI, and AL = the chip's port ESI
ESSREG_S16_Chip_Out:
        push    edx
        mov     edx,esi
        out     dx,al
        pop     edx
        ret
ESSREG_S16_Chip_In:
        push    edx
        mov     edx,esi
        in      al,dx
        pop     edx
        ret

; AL = the VM's read of Audio_Base+EDX, ESI = the port, EDI = ADI: the
; read latch and the status bits of the front's DSP; the chip's in the
; MIDI modes and for a program that knows it
ESSREG_S16_In:
        cmp     dl,0x05
        je      ESSREG_S16_Mix_Read
        cmp     dl,0x04
        je      .index
        cmp     dl,0x0A
        jb      ESSREG_S16_Chip_In      ; the reset port: as it is
        test    byte [edi+EX_S16_Flags],S16_ESS
        jnz     ESSREG_S16_Chip_In
        test    byte [edi+EX_S16_State],S16S_UART | S16S_MIDI
        jnz     ESSREG_S16_Chip_In
        cmp     dl,0x0C
        je      .busy
        ja      .acks
        mov     al,[edi+EX_S16_Latch]   ; +Ah: the latch, and the answer's
        test    byte [edi+EX_S16_State],S16S_FULL       ; next byte into it
        jz      .ret
        and     byte [edi+EX_S16_State],~S16S_FULL & 0xFF
        push    eax
        call    ESSREG_S16_Next
        pop     eax
.ret:   ret
.index: mov     al,[edi+EX_S16_Index]
        cmp     al,0xFF
        jne     .ret
        jmp     ESSREG_S16_Chip_In
.busy:  mov     al,[edi+EX_S16_State]   ; +Ch: busy (bit 7) while a byte
        ror     al,2                    ; waits (S16S_HELD)
        or      al,0x7F
        ret
.acks:  lea     ecx,[edx-0x0D]          ; +Eh 8-bit, +Fh 16-bit: CL = 1, 2
        call    ESSREG_S16_Ack
        mov     al,0xFF                 ; +Fh: FFh
        cmp     dl,0x0F
        je      .ret
        mov     al,[edi+EX_S16_State]   ; +Eh: bit 7 for a byte in the
        ror     al,1                    ; latch (S16S_FULL)
        or      al,0x7F
        ret

; the latch takes the answer's next byte, EDI = ADI; once the answer is
; out, a byte written meanwhile is the DSP's
ESSREG_S16_Next:
        cmp     byte [edi+EX_S16_Alen],0
        je      .end
        mov     ecx,[edi+EX_S16_Aptr]
        mov     al,[ecx]
        inc     ecx
        mov     [edi+EX_S16_Aptr],ecx
        mov     [edi+EX_S16_Latch],al
        or      byte [edi+EX_S16_State],S16S_FULL
        dec     byte [edi+EX_S16_Alen]
        jnz     .ret
.end:   test    byte [edi+EX_S16_State],S16S_HELD
        jz      .ret
        and     byte [edi+EX_S16_State],~S16S_HELD & 0xFF
        mov     al,[edi+EX_S16_Held]
        jmp     ESSREG_S16_Take
.ret:   ret

; the answer of CL bytes at ESI, EDI = ADI: the first into the latch if it
; is free, the others after it. The DSP takes no byte until they are out
ESSREG_S16_Answer:
        test    byte [edi+EX_S16_State],S16S_FULL
        jnz     .rest
        or      byte [edi+EX_S16_State],S16S_FULL
        mov     al,[esi]
        inc     esi
        mov     [edi+EX_S16_Latch],al
        dec     cl
.rest:  mov     [edi+EX_S16_Aptr],esi
        mov     [edi+EX_S16_Alen],cl
        ret
; the answer AL
ESSREG_S16_Put1:
        mov     [edi+EX_S16_Abuf],al
        lea     esi,[edi+EX_S16_Abuf]
        mov     cl,1
        jmp     ESSREG_S16_Answer

; AL written to the reset port, ESI = the port, EDI = ADI: the front's DSP
; halts at 1 and starts again at 0 with AAh, and the chip stays in
; Extended mode; the MIDI UART mode and Compatibility mode take a reset of
; the chip's own. A program that knows the chip resets it itself
ESSREG_S16_Reset:
        test    byte [edi+EX_S16_Flags],S16_ESS
        jz      .front
        call    ESSREG_S16_Chip_Out
        test    al,1
        jnz     .done
        and     byte [edi+EX_S16_Flags],~(S16_ESS | S16_EXT | S16_RUN) & 0xFF
        mov     dword [edi+EX_S16_Known],0
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        mov     ecx,S16_POLL
        call    ESSREG_DSP_Get          ; the chip's AAh, for nobody
        jmp     ESSREG_S16_Boot
.front: test    al,1
        jz      .low
        test    byte [edi+EX_S16_Flags],S16_RESET
        jnz     .done
        or      byte [edi+EX_S16_Flags],S16_RESET
        jmp     ESSREG_S16_Halt
.low:   test    byte [edi+EX_S16_Flags],S16_RESET
        jz      .done
        and     byte [edi+EX_S16_Flags],~S16_RESET & 0xFF
        test    byte [edi+EX_S16_State],S16S_UART | S16S_COMP
        jz      ESSREG_S16_Boot
        call    ESSREG_S16_Chip_Reset
        jmp     ESSREG_S16_Boot
.done:  ret

; the DSP's start after a reset, EDI = ADI: cold, or warm after the
; high-speed and MIDI UART commands, which keep the rate, the block and
; the speaker; AAh in the latch
ESSREG_S16_Boot:
        test    byte [edi+EX_S16_State],S16S_WARM
        jnz     .warm
        call    ESSREG_S16_Cold
.warm:  and     byte [edi+EX_S16_State],S16S_SPKR
        mov     byte [edi+EX_S16_Latch],0xAA
        or      byte [edi+EX_S16_State],S16S_FULL
.clear: mov     byte [edi+EX_S16_Need],0
        mov     byte [edi+EX_S16_Alen],0
        ret

; the rate (9958 Hz), the 8-bit block and the speaker as after a cold
; boot, EDI = ADI
ESSREG_S16_Cold:
        mov     byte [edi+EX_S16_RateV],0x38
        mov     word [edi+EX_S16_Block8],0x07FF
        and     byte [edi+EX_S16_State],~S16S_SPKR & 0xFF
        ret

; the DSP held in reset, EDI = ADI: the front's transfers stop, the chip's
; interrupts are acknowledged (Audio 2's interrupt off and its latch clear
; until its next transfer), and nothing waits in the latches
ESSREG_S16_Halt:
        call    ESSREG_S16_A1_Stop
        call    ESSREG_S16_A2_Stop
        call    ESSREG_S16_A2_Off
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0E
        in      al,dx
        and     byte [edi+EX_S16_State],S16S_WARM | S16S_SPKR | S16S_UART | S16S_COMP
        jmp     ESSREG_S16_Boot.clear

; Audio 1's transfer of the front's stops (B8h bit 0), at once, as a Sound
; Blaster 16's, and its FIFO empties (ESSREG_S16_Flush), so that nothing of
; it plays on while Audio 2 plays the next, or in the next one's format;
; EDI = ADI
ESSREG_S16_A1_Stop:
        test    byte [edi+EX_S16_Flags],S16_RUN
        jz      ESSREG_S16_Flush
        and     byte [edi+EX_S16_Flags],~(S16_RUN | S16_AUTO | S16_LAST | S16_PAUSE) & 0xFF
        mov     ax,0xB8FE
        call    ESSREG_S16_And

; Audio 1's FIFO empties (+6 bit 1, DS p.48), EDI = ADI
ESSREG_S16_Flush:
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 6
        mov     al,2
        out     dx,al
        xor     al,al
        out     dx,al
        pop     edx
.out:   ret

; Audio 2's transfer of the front's stops (78h bits 4 and 1), and what its
; FIFO holds plays out; EDI = ADI
ESSREG_S16_A2_Stop:
        test    byte [edi+EX_S16_Flags2],S16_RUN
        jz      .out
        mov     byte [edi+EX_S16_Flags2],0
        push    eax
        mov     ax,0x78ED
        call    ESSREG_S16_Mix_And
        pop     eax
.out:   ret

; Audio 2's interrupt off and its latch cleared (7Ah), while the front uses
; Audio 2; its format stays for what its FIFO still plays; EDI = ADI
ESSREG_S16_A2_Off:
        cmp     byte [edi+EX_S16_A2],4
        jae     .out
        push    eax
        mov     ax,0x7A07
        call    ESSREG_S16_Mix_And
        pop     eax
.out:   ret

; AL written to the command port, ESI = the port, EDI = ADI: the front's
; DSP takes it, or waits for its answer to be read; the chip takes it in
; the MIDI modes and for a program that knows it
ESSREG_S16_Write:
        test    byte [edi+EX_S16_Flags],S16_ESS
        jnz     ESSREG_S16_Chip_Out
        test    byte [edi+EX_S16_State],S16S_UART | S16S_MIDI
        jz      .dsp
        and     byte [edi+EX_S16_State],~S16S_MIDI & 0xFF ; a byte ends MIDI
        jmp     ESSREG_S16_Chip_Out                     ; input
.dsp:   cmp     byte [edi+EX_S16_Alen],0
        je      ESSREG_S16_Take
        mov     [edi+EX_S16_Held],al    ; the answer blocks: the byte waits,
        or      byte [edi+EX_S16_State],S16S_HELD ; over any before it
        ret

; AL, a byte the DSP takes, EDI = ADI: a command, or an operand of the
; last. C6h is also the chip's Extended mode: a first operand that is no
; mode (bits 4 and 5 only) is the chip's register command, from a program
; that knows the chip
ESSREG_S16_Take:
        cmp     byte [edi+EX_S16_Need],0
        jne     .operand
        mov     ah,al
        xchg    ah,[edi+EX_S16_Cmd]
        mov     [edi+EX_S16_Prev],ah
        mov     byte [edi+EX_S16_Got],0
        movzx   ebx,al                  ; its operands, as DSP 4.05's
        shr     ebx,2                   ; firmware takes them: two bits a
        mov     cl,al                   ; command in ESSREG_S16_Operands
        and     cl,3
        shl     cl,1
        mov     al,[ESSREG_S16_Operands+ebx]
        shr     al,cl
        and     al,3
        mov     [edi+EX_S16_Need],al
        jz      ESSREG_S16_Command
        ret
.operand:
        movzx   ebx,byte [edi+EX_S16_Got]
        or      ebx,ebx
        jnz     .keep
        cmp     byte [edi+EX_S16_Cmd],0xC6
        jne     .keep
        test    al,0xCF
        jz      .keep
        mov     ah,0xC6                 ; the chip's Extended mode on, and
        call    ESSREG_S16_Reg          ; this byte its command
        or      byte [edi+EX_S16_Flags],S16_ESS | S16_EXT
        mov     dword [edi+EX_S16_Known],0
        mov     byte [edi+EX_S16_Need],0
        ret
.keep:  mov     [edi+EX_S16_Ops+ebx],al
        inc     byte [edi+EX_S16_Got]
        dec     byte [edi+EX_S16_Need]
        jz      ESSREG_S16_Command
        ret

ESSREG_S16_Operands:
        db 0x10, 0x09, 0x40, 0x61       ; 0xh
        db 0x55, 0xAA, 0x00, 0x00       ; 1xh
        db 0x00, 0xAA, 0x00, 0x00       ; 2xh
        db 0x00, 0x00, 0x55, 0x55       ; 3xh
        db 0x29, 0x00, 0x02, 0xA6       ; 4xh
        db 0x00, 0x00, 0x00, 0x00       ; 5xh
        db 0x00, 0x00, 0x00, 0x00       ; 6xh
        db 0x00, 0xAA, 0x00, 0x00       ; 7xh
        db 0xAA, 0xAA, 0xAA, 0xAA       ; 8xh
        db 0x00, 0x00, 0x00, 0x00       ; 9xh
        db 0x00, 0x00, 0x00, 0x00       ; Axh
        db 0xFF, 0xFF, 0xFF, 0xFF       ; Bxh
        db 0xFF, 0xFF, 0xFF, 0xFF       ; Cxh
        db 0x00, 0x00, 0x00, 0x00       ; Dxh
        db 0x11, 0x01, 0x00, 0x00       ; Exh
        db 0x00, 0x00, 0x24, 0x00       ; Fxh

; the command in EX_S16_Cmd with its operands, EDI = ADI: a constant answer
; from ESSREG_S16_Answers, else its group's handler, by the high nibble,
; with AL = the low one
ESSREG_S16_Command:
        mov     esi,ESSREG_S16_Answers
        mov     al,[edi+EX_S16_Cmd]
.find:  cmp     byte [esi+1],0
        je      .group
        cmp     al,[esi]
        je      .found
        add     esi,byte 3
        jmp     .find
.found: mov     cl,[esi+1]
        movzx   esi,byte [esi+2]
        add     esi,ESSREG_S16_Strings
        jmp     ESSREG_S16_Answer
.group: movzx   ebx,al
        and     al,0x0F
        shr     ebx,4
        jmp     [ESSREG_S16_Groups+ebx*4]
ESSREG_S16_Groups:
        dd ESSREG_S16_G0, ESSREG_S16_G1, ESSREG_S16_G2, ESSREG_S16_G3
        dd ESSREG_S16_G4, ESSREG_S16_None, ESSREG_S16_None, ESSREG_S16_G7
        dd ESSREG_S16_G8, ESSREG_S16_G9, ESSREG_S16_None, ESSREG_S16_GX
        dd ESSREG_S16_GX, ESSREG_S16_GD, ESSREG_S16_GE, ESSREG_S16_GF

; the commands that answer bytes that never change: the CSP's that this
; card doesn't have (03h 08h FFh, 09h its ID, 0Ah the lock and FFh), 4Dh
; and F9h, FBh, FCh the DSP's RAM (00h here), E1h the version, E3h the
; copyright, F4h the ROM's ID; the command, how many bytes, and where
; from ESSREG_S16_Strings
ESSREG_S16_Answers:
        db 0x03, 1, S16_FF
        db 0x08, 1, S16_FF
        db 0x09, 2, S16_ZEROS
        db 0x0A, 2, S16_ZEROS + 1
        db 0x4D, 1, S16_ZEROS
        db 0xF9, 1, S16_ZEROS
        db 0xFB, 1, S16_ZEROS
        db 0xFC, 1, S16_ZEROS
        db 0xE1, 2, S16_VER
        db 0xE3, S16_COPYRIGHT_LEN, S16_COPYRIGHT
        db 0xF4, 2, S16_ROM
        db 0, 0
ESSREG_S16_Strings:
        db 0, 0                         ; S16_ZEROS
        db 0xFF                         ; S16_FF
        db S16_VERSION >> 8, S16_VERSION & 0xFF ; S16_VER
        db 0x7D, 0x1B                   ; S16_ROM
        db "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.", 0
S16_ZEROS               equ 0           ; where each is in ESSREG_S16_Strings
S16_FF                  equ 2
S16_VER                 equ 3
S16_ROM                 equ 5
S16_COPYRIGHT           equ 7
S16_COPYRIGHT_LEN       equ $ - ESSREG_S16_Strings - S16_COPYRIGHT

; 0Fh: an X-bus register of the CSP: FFh for 80h-83h, else 00h
ESSREG_S16_G0:
        cmp     al,0x0F
        jne     ESSREG_S16_None
        mov     al,[edi+EX_S16_Ops]
        and     al,0xFC
        cmp     al,0x80
        mov     al,0
        jne     ESSREG_S16_Put1
        dec     al
        jmp     ESSREG_S16_Put1
ESSREG_S16_None:
        ret

; 1xh: 10h-13h the direct DAC; 14h 15h 8-bit single-cycle DMA of the
; operands' length, 18h 19h 1Ch 1Dh auto-initialized of the 8-bit
; block's; with bit 1, 2-bit ADPCM (the chip's 7Ah and 7Bh)
ESSREG_S16_G1:
        cmp     al,4
        jb      .dac
        mov     bl,0
        test    al,0x02
        jz      ESSREG_S16_Old8
        mov     ah,al
        and     ah,1
        or      ah,0x7A
        cmp     al,8
        cmc                             ; CF: auto-initialized (lo >= 8)
        jmp     ESSREG_S16_Fw
.dac:   call    ESSREG_S16_Compat       ; the direct DAC, through the chip in
        mov     ah,0x10                 ; Compatibility mode
        mov     al,[edi+EX_S16_Ops]
        jmp     ESSREG_S16_Reg

; 2xh: 20h-23h the direct ADC; 24h-27h single-cycle 8-bit recording,
; 28h-2Fh auto-initialized
ESSREG_S16_G2:
        cmp     al,4
        jb      .adc
        mov     bl,0x08
        jmp     short ESSREG_S16_Old8
.adc:   call    ESSREG_S16_Compat       ; the direct ADC, through the chip
        mov     al,0x20                 ; in Compatibility mode
        call    ESSREG_S16_DSP
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        mov     ecx,S16_POLL
        call    ESSREG_DSP_Get
        jnc     ESSREG_S16_Put1
        mov     al,0
        jmp     ESSREG_S16_Put1
ESSREG_S16_Old8:
        cmp     al,8                    ; BL = 08h recording, else 00h
        mov     ax,[edi+EX_S16_Ops]
        jb      ESSREG_S16_PCM8
        or      bl,0x04
ESSREG_S16_Block:
        mov     ax,[edi+EX_S16_Block8]
ESSREG_S16_PCM8:
        xor     cl,cl                   ; unsigned mono
        jmp     ESSREG_S16_Xfer

; 9xh: the high-speed 8-bit commands, of the 8-bit block: auto-initialized
; unless bit 0, recording with bit 3, never queued; the next reset is warm
ESSREG_S16_G9:
        or      byte [edi+EX_S16_State],S16S_WARM
        mov     bl,al
        and     bl,0x08
        or      bl,0x05
        test    al,1
        jz      ESSREG_S16_Block
        and     bl,~0x04 & 0xFF
        jmp     ESSREG_S16_Block

; 3xh: MIDI through the chip in Compatibility mode: 30h-33h input (30h,
; 31h with interrupts) until the next write, 34h-37h the UART mode until a
; reset, 38h-3Fh a byte out
ESSREG_S16_G3:
        push    eax
        call    ESSREG_S16_Compat
        pop     eax
        cmp     al,8
        jae     .out
        mov     ah,S16S_MIDI
        cmp     al,4
        jb      .mode
        mov     ah,S16S_UART | S16S_WARM
.mode:  or      [edi+EX_S16_State],ah
        and     al,0x05
        or      al,0x30
        jmp     ESSREG_S16_DSP
.out:   mov     ah,0x38
        mov     al,[edi+EX_S16_Ops]
        jmp     ESSREG_S16_Reg

; 4xh: 40h the time constant, 41h and 42h the rate (high byte first), 48h
; the 8-bit block; the others take their operands only
ESSREG_S16_G4:
        mov     cx,[edi+EX_S16_Ops]
        cmp     al,8
        je      .block
        cmp     al,2
        ja      ESSREG_S16_None
        xchg    ax,cx                   ; AX = the operands, CL = 0 for 40h
        test    cl,cl
        jz      .tc
        xchg    al,ah
        call    ESSREG_S16_Rate_V
        jmp     short .v
.tc:    call    ESSREG_S16_TC_V
.v:     mov     [edi+EX_S16_RateV],al
        ret
.block: mov     [edi+EX_S16_Block8],cx
        ret

; 7xh: ADPCM through the chip: 74h-77h single-cycle (4-bit, then 2.6-bit,
; each with the reference byte at bit 0); the others auto-initialized on a
; Sound Blaster 16, which the chip doesn't have
ESSREG_S16_G7:
        mov     ah,al
        and     ah,3
        or      ah,0x74
        sub     al,4
        cmp     al,4                    ; CF clear for 74h-77h
        cmc
        jmp     short ESSREG_S16_Fw

; 8xh: silence, of the operands' length
ESSREG_S16_G8:
        mov     ah,0x80
        clc

; the firmware's command AH (ADPCM, silence) for the chip in Compatibility
; mode, at the rate's time constant, with the operands' length, EDI =
; ADI; CF set: auto-initialized, which the chip doesn't have: the 8-bit
; block, once. Its interrupt is the 8-bit one
ESSREG_S16_Fw:
        jnc     .ops
        mov     cx,[edi+EX_S16_Block8]
        mov     [edi+EX_S16_Ops],cx
.ops:   push    eax
        call    ESSREG_S16_Compat_TC
        pop     eax
        mov     al,ah
        call    ESSREG_S16_DSP
        mov     ax,[edi+EX_S16_Ops]
        call    ESSREG_S16_DSP
        mov     al,ah
        call    ESSREG_S16_DSP
        and     byte [edi+EX_S16_State],~S16S_T16 & 0xFF
        ret

; Bxh 16-bit and Cxh 8-bit: bit 3 records, bit 2 auto-initializes (bits 1
; and 0 don't count); the mode's bit 4 signed, bit 5 stereo; the length of
; a Cxh is the 8-bit block's too
ESSREG_S16_GX:
        mov     bl,[edi+EX_S16_Cmd]
        and     bl,0x1C
        mov     cl,[edi+EX_S16_Ops]
        mov     ax,[edi+EX_S16_Ops+1]
        test    bl,0x10
        jnz     ESSREG_S16_Xfer
        mov     [edi+EX_S16_Block8],ax
        jmp     ESSREG_S16_Xfer

; Dxh: D1h D3h the speaker flag, which D8h answers (D1h and D3h change no
; sound on DSP 4.xx); D0h D4h DAh pause, continue and end the 8-bit
; transfer's auto-initialize, D5h D6h D9h the 16-bit one's, when that kind
; runs, on Audio 1 or Audio 2 (ESSREG_S16_Dx: the kind and the action of
; each)
ESSREG_S16_GD:
        cmp     al,8
        je      .status
        movzx   ebx,al
        mov     ah,[ESSREG_S16_Dx+ebx]
        test    ah,ah
        jns     .speaker                ; bit 7 clear: the speaker, or nothing
        test    ah,S16_16
        jz      .a1
        test    byte [edi+EX_S16_Flags2],S16_RUN
        jnz     .a2
.a1:    mov     dl,[edi+EX_S16_Flags]
        xor     dl,ah
        test    dl,S16_RUN | S16_16     ; not the kind that runs: nothing
        jnz     ESSREG_S16_None
        shl     ah,2                    ; CF: pause, SF: continue
        jc      .pause
        js      .cont
        test    byte [edi+EX_S16_Flags],S16_AUTO        ; the block that
        jz      ESSREG_S16_None                         ; plays is the last
        and     byte [edi+EX_S16_Flags],~(S16_AUTO | S16_LAST) & 0xFF
        mov     ax,0xB8FB
        jmp     ESSREG_S16_And
.pause: or      byte [edi+EX_S16_Flags],S16_PAUSE       ; Audio 1's DMA
        mov     ax,0xB2BF                               ; requests off (B2h
        jmp     ESSREG_S16_And                          ; bit 6)
.cont:  test    byte [edi+EX_S16_Flags],S16_PAUSE
        jz      ESSREG_S16_None
        and     byte [edi+EX_S16_Flags],~S16_PAUSE & 0xFF
        mov     cl,[edi+EX_S16_Ctrl+0x08]       ; the rate as it is now
        call    ESSREG_S16_Clocks
        mov     ax,0xB240
        jmp     ESSREG_S16_Or
.a2:    shl     ah,2                    ; Audio 2: the same with 78h
        jc      .pause2
        js      .cont2
        test    byte [edi+EX_S16_Flags2],S16_AUTO       ; bit 4: the block
        jz      ESSREG_S16_None                         ; that plays is the
        and     byte [edi+EX_S16_Flags2],~(S16_AUTO | S16_LAST) & 0xFF ; last
        mov     ax,0x78EF
        jmp     ESSREG_S16_Mix_And
.pause2:
        or      byte [edi+EX_S16_Flags2],S16_PAUSE      ; the FIFO off the
        mov     ax,0x78FE                               ; DAC (bit 0): the
        jmp     ESSREG_S16_Mix_And                      ; DMA fills it, waits
.cont2: test    byte [edi+EX_S16_Flags2],S16_PAUSE
        jz      ESSREG_S16_None
        and     byte [edi+EX_S16_Flags2],~S16_PAUSE & 0xFF
        call    ESSREG_S16_A2_Rate
        mov     ax,0x7801
        jmp     ESSREG_S16_Mix_Or
.speaker:
        dec     ah                      ; 1 on, 2 off, 0 nothing
        js      ESSREG_S16_None
        jnz     .off
        or      byte [edi+EX_S16_State],S16S_SPKR
        ret
.off:   and     byte [edi+EX_S16_State],~S16S_SPKR & 0xFF
        ret
.status:
        mov     al,[edi+EX_S16_State]
        and     al,S16S_SPKR
        jz      ESSREG_S16_Put1
        mov     al,0xFF
        jmp     ESSREG_S16_Put1
; Dxh: 1 the speaker on, 2 off, 0 nothing; for the engines 80h, S16_RUN
; and S16_16 for the 16-bit one, and 40h pause, 20h continue, else the
; auto-initialize's end
ESSREG_S16_Dx:
        db 0xC0 | S16_RUN, 1, 0, 2
        db 0xA0 | S16_RUN, 0xC0 | S16_RUN | S16_16, 0xA0 | S16_RUN | S16_16, 0
        db 0, 0x80 | S16_RUN | S16_16, 0x80 | S16_RUN, 0, 0, 0, 0, 0

; Exh: E0h the operand inverted, E4h and E8h the test register; E2h's byte
; would go to the 8-bit channel, which the front has no way to write
ESSREG_S16_GE:
        mov     ah,[edi+EX_S16_Ops]
        cmp     al,0
        je      .inv
        cmp     al,4
        je      .test
        cmp     al,8
        jne     ESSREG_S16_None
        mov     al,[edi+EX_S16_Test]
        jmp     ESSREG_S16_Put1
.inv:   mov     al,ah
        not     al
        jmp     ESSREG_S16_Put1
.test:  mov     [edi+EX_S16_Test],ah
        ret

; Fxh: F2h the 8-bit interrupt, F3h the 16-bit one (the chip's processor
; interrupt), F8h a 0 into the latch at once, FDh the command before
ESSREG_S16_GF:
        cmp     al,3
        ja      .others
        jb      .irq8
        or      byte [edi+EX_S16_State],S16S_T16
        jmp     short .f2
.irq8:  cmp     al,2
        jne     ESSREG_S16_None
        and     byte [edi+EX_S16_State],~S16S_T16 & 0xFF
.f2:    mov     al,0xF2
        jmp     ESSREG_S16_DSP
.others:
        cmp     al,0x0D
        jne     .f8
        mov     al,[edi+EX_S16_Prev]
        jmp     ESSREG_S16_Put1
.f8:    cmp     al,8
        jne     ESSREG_S16_None
        mov     byte [edi+EX_S16_Latch],0
        or      byte [edi+EX_S16_State],S16S_FULL
        ret

; --- the chip ----------------------------------------------------------------

; AL to the chip's command port, EDI = ADI, once it takes a byte, or after
; a short wait anyway
ESSREG_S16_DSP:
        push    eax
        push    ecx
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        mov     ecx,S16_POLL
.wait:  in      al,dx
        test    al,0x80
        jz      .ready
        loop    .wait
.ready: mov     eax,[esp+8]
        out     dx,al
        pop     edx
        pop     ecx
        pop     eax
        ret

; the chip's register AH = AL (Extended mode), EDI = ADI
ESSREG_S16_Reg:
        push    eax
        mov     al,ah
        call    ESSREG_S16_DSP
        pop     eax
        jmp     ESSREG_S16_DSP

; The front keeps the chip's registers A0h-BFh that it wrote or read since
; the chip's last reset (EX_S16_Ctrl, EX_S16_Known), so that a transfer
; writes only what changes and reads each register once

; the chip's register AH = AL, EDI = ADI, unless the chip has it already;
; ESSREG_S16_Force writes it in any case
ESSREG_S16_Set:
        push    ecx
        movzx   ecx,ah
        sub     ecx,0xA0
        bt      dword [edi+EX_S16_Known],ecx
        jnc     ESSREG_S16_Force.keep
        cmp     [edi+EX_S16_Ctrl+ecx],al
        jne     ESSREG_S16_Force.keep
        pop     ecx
        ret
ESSREG_S16_Force:
        push    ecx
        movzx   ecx,ah
        sub     ecx,0xA0
.keep:  mov     [edi+EX_S16_Ctrl+ecx],al
        bts     dword [edi+EX_S16_Known],ecx
        pop     ecx
        jmp     ESSREG_S16_Reg

; AL = the chip's register AH, EDI = ADI: as the front knows it, or read
; and then known; 0 if the chip doesn't answer
ESSREG_S16_Get:
        push    ecx
        movzx   ecx,ah
        sub     ecx,0xA0
        bt      dword [edi+EX_S16_Known],ecx
        jnc     .read
        mov     al,[edi+EX_S16_Ctrl+ecx]
        pop     ecx
        ret
.read:  call    ESSREG_S16_Read
        jc      .none
        mov     [edi+EX_S16_Ctrl+ecx],al
        bts     dword [edi+EX_S16_Known],ecx
.none:  pop     ecx
        ret

; the chip's register AH with AL's bits set (ESSREG_S16_Or) or kept
; (ESSREG_S16_And), EDI = ADI
ESSREG_S16_Or:
        push    ecx
        mov     cl,al
        call    ESSREG_S16_Get
        or      al,cl
        pop     ecx
        jmp     ESSREG_S16_Set
ESSREG_S16_And:
        push    ecx
        mov     cl,al
        call    ESSREG_S16_Get
        and     al,cl
        pop     ecx
        jmp     ESSREG_S16_Set

; AL = the chip's register AH (command C0h), EDI = ADI, with interrupts off
; so that nothing reads the answer first, after any byte nobody read; 0
; and CF set if none comes
ESSREG_S16_Read:
        push    ecx
        push    edx
        pushfd
        cli
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        call    ESSREG_DSP_Drain        ; a byte nobody read
        mov     al,0xC0
        call    ESSREG_S16_DSP
        mov     al,ah
        call    ESSREG_S16_DSP
        mov     ecx,S16_POLL
        call    ESSREG_DSP_Get
        jnc     .got
        popfd
        pop     edx
        pop     ecx
        xor     al,al
        stc
        ret
.got:   popfd
        pop     edx
        pop     ecx
        clc
        ret

; the chip's own reset (DS p.43), EDI = ADI: +6 = 3, a few microseconds,
; 0, then its AAh; it is in Compatibility mode, its registers unknown,
; and Audio 1's transfer is over. The reset clears Audio 2's format and
; interrupt (7Ah) and its volume (7Ch, DS p.51), which go back while
; Audio 2 plays
ESSREG_S16_Chip_Reset:
        pushad
        movzx   edx,word [edi+ADI_AudioBase]
        test    byte [edi+EX_S16_Flags2],S16_RUN
        jz      .reset
        mov     ah,0x7C
        call    ESSREG_Mix_Read
        push    eax
        mov     ah,0x7A
        call    ESSREG_Mix_Read
        push    eax
.reset: add     edx,byte 6
        mov     al,3
        out     dx,al
        in      al,dx
        in      al,dx
        in      al,dx
        xor     al,al
        out     dx,al
        add     edx,byte 6              ; +Ch
        mov     ecx,S16_POLL
        call    ESSREG_DSP_Get          ; the AAh
        and     byte [edi+EX_S16_Flags],S16_ESS | S16_RESET
        mov     dword [edi+EX_S16_Known],0
        test    byte [edi+EX_S16_Flags2],S16_RUN
        jz      .out
        movzx   edx,word [edi+ADI_AudioBase]
        pop     eax                     ; 7Ah, then 7Ch
        call    ESSREG_Mix_Write
        pop     eax
        call    ESSREG_Mix_Write
.out:   popad
        ret

; the chip in Extended mode for a transfer, its DAC into the mixer, EDI =
; ADI; after Compatibility mode, a reset of its own first
ESSREG_S16_Ext:
        test    byte [edi+EX_S16_State],S16S_COMP
        jz      .ext
        and     byte [edi+EX_S16_State],~S16S_COMP & 0xFF
        call    ESSREG_S16_Chip_Reset
.ext:   test    byte [edi+EX_S16_Flags],S16_EXT
        jnz     .ok
        or      byte [edi+EX_S16_Flags],S16_EXT
        mov     al,0xC6
        call    ESSREG_S16_DSP
        mov     al,0xD1
        jmp     ESSREG_S16_DSP
.ok:    ret

; the chip in Compatibility mode for a command of the firmware's, EDI =
; ADI: out of Extended mode by a reset of its own, its DAC into the mixer;
; ESSREG_S16_Compat_TC gives it the rate too, as the time constant 256 - 1
; MHz / rate
ESSREG_S16_Compat_TC:
        call    ESSREG_S16_Compat
        push    ecx
        push    edx
        xor     edx,edx
        mov     eax,5624                ; 1 MHz * 248 / 44100
        movzx   ecx,byte [edi+EX_S16_RateV]
        div     ecx
        pop     edx
        pop     ecx
        cmp     eax,0xFF
        jbe     .tc
        mov     al,0xFF
.tc:    neg     al
        mov     ah,0x40
        jmp     ESSREG_S16_Reg
ESSREG_S16_Compat:
        test    byte [edi+EX_S16_State],S16S_COMP
        jnz     .ok
        or      byte [edi+EX_S16_State],S16S_COMP
        test    byte [edi+EX_S16_Flags],S16_EXT
        jz      .dac
        call    ESSREG_S16_Chip_Reset
.dac:   mov     al,0xD1
        jmp     ESSREG_S16_DSP
.ok:    ret

; --- the rate ----------------------------------------------------------------

; AL = the rate generator's value for the time constant AL (40h): 248 MHz
; / 44.1 kHz over 256 - TC, with the ROM table's exceptions
ESSREG_S16_TC_V:
        cmp     al,0xEA                 ; the ROM's limit, EBh, is FFh too
        jbe     .tc
        mov     al,0xEA
.tc:    cmp     al,0x68
        mov     ah,0x25
        je      .ah
        cmp     al,0x6C
        mov     ah,0x26
        je      .ah
        cmp     al,0xC8                 ; the table's own: 35.6 kHz
        je      .ret
        push    ecx
        push    edx
        movzx   ecx,al
        neg     ecx
        add     ecx,256
        mov     eax,5623                ; the same as 248 MHz over 44.1 kHz
        xor     edx,edx                 ; for every divisor, at most FFh
        div     ecx
        pop     edx
        pop     ecx
.ret:   ret
.ah:    mov     al,ah
        ret

; AL = the value for the rate AH:AL in Hz (41h, 42h), as DSPs before 4.13
; compute it, losing a carry (23000 Hz plays at 20094 Hz)
ESSREG_S16_Rate_V:
        cmp     ah,0xB1
        jae     .max
        cmp     ah,0x13
        jb      .min
        push    ebx
        push    ecx
        mov     bl,al
        mov     al,23
        mul     ah                      ; AX = 23 times the high byte
        mov     cx,ax
        mov     al,23
        mul     bl                      ; AH = 23 times the low one, high byte
        add     cl,ah                   ; the carry lost
        movzx   eax,cx
        shr     eax,3
        inc     eax
        shr     eax,1                   ; (x >> 4) + ((x >> 3) & 1)
        pop     ecx
        pop     ebx
        ret
.max:   mov     al,0xFF
        ret
.min:   mov     al,0x1C
        ret

; A1h and A2h for the rate v * 44100 / 248 Hz, as ES1869.DRV picks them
; for a rate (6:1FF4): the 795.5 kHz clock above 22050 Hz, the 397.7 kHz
; one up to it, and the filter clock from its tables between 22 and 44
; kHz, else 7.16 MHz over 35.9 times the rate; CL = A8h (bit 0 stereo),
; EDI = ADI
ESSREG_S16_Clocks:
        pushad
        movzx   ebx,byte [edi+EX_S16_RateV]
        mov     eax,4472                ; 397.7 kHz: round(2236 / v)
        mov     esi,128
        cmp     bl,124
        jbe     .clock
        mov     eax,8947                ; 795.5 kHz: round(4473.5 / v)
        mov     esi,256                 ; (the constants round as the
.clock: call    .div                    ; clocks do for every v)
        mov     ebp,eax                 ; EBP = the divisor
        sub     esi,eax
        mov     eax,esi
        mov     ah,0xA1
        call    ESSREG_S16_Set
        cmp     bl,124
        jbe     .general
        cmp     ebp,byte 18
        jb      .general
        cmp     ebp,byte 35
        ja      .general
        mov     esi,ESSREG_S16_Filter_Stereo
        test    cl,0x01
        jnz     .table
        mov     esi,ESSREG_S16_Filter_Mono
.table: movzx   eax,byte [esi+ebp-18]
        jmp     short .a2
.general:
        mov     eax,2245                ; 7.16 MHz / 35.9: round(1122.5 / v)
        call    .div
.a2:    neg     eax
        mov     ah,0xA2
        call    ESSREG_S16_Set
        popad
        ret
.div:   xor     edx,edx                 ; EAX = round(EAX / 2 / v)
        div     ebx
        inc     eax
        shr     eax,1
        ret

; ES1869.DRV's filter dividers (DGROUP 4Eh and 60h) for the 795.5 kHz
; clock's divisors 18 to 35 (44.2 to 22.7 kHz)
ESSREG_S16_Filter_Stereo:
        db 3, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9
ESSREG_S16_Filter_Mono:
        db 3, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10

; --- the program's transfers: Audio 1 in Extended mode, and Audio 2 ----------

; a transfer, EDI = ADI: BL = bit 4 16-bit, bit 3 recording, bit 2
; auto-initialize, bit 0 never queued; CL = the mode (bit 4 signed, bit 5
; stereo); AX = the length less one, in samples. A single-cycle one while
; this kind auto-initializes is queued, as on the Sound Blaster 16: the
; counter gets its length for the next block, the last (its mode doesn't
; count). A 16-bit playback goes to Audio 2 when the front has it and the
; program a 16-bit channel (ESSREG_S16_A2_Xfer). Else the sequence of the
; data sheet (p.48) on Audio 1: B8h stopped with the direction and
; auto-initialize, B9h for demand transfers of 2 bytes, A8h for mono or
; stereo, A1h and A2h for the rate, A4h and A5h for minus the bytes, B6h
; and B7h for the format, B1h and B2h for the interrupt and the DMA
; request, mixer 71h bit 5 off, then B8h bit 0. Only the registers that
; change are written. The DMA is the program's, through VDMAD: Audio 1's
; channel as the program programs it, or its 16-bit channel carried over
; (ESSREG_S16_Carry). There is one transfer of each kind, so a 16-bit one
; ends the other engine's
ESSREG_S16_Xfer:
        movzx   esi,ax
        inc     esi                     ; ESI = the samples
        test    bl,0x05
        jnz     .start
        test    bl,S16_16
        jz      .q1
        mov     al,[edi+EX_S16_Flags2]
        and     al,S16_RUN | S16_AUTO
        cmp     al,S16_RUN | S16_AUTO
        jne     .q1
        or      byte [edi+EX_S16_Flags2],S16_LAST
        jmp     ESSREG_S16_A2_Count
.q1:    mov     dl,bl
        and     dl,S16_16
        or      dl,S16_RUN | S16_AUTO
        mov     al,[edi+EX_S16_Flags]
        and     al,S16_16 | S16_RUN | S16_AUTO
        cmp     al,dl
        jne     .start
        or      byte [edi+EX_S16_Flags],S16_LAST
        jmp     ESSREG_S16_Count
.start: mov     [edi+EX_S16_Xbits],bl
        test    bl,S16_16
        jz      .eight
        test    bl,0x08                 ; 16-bit playback: Audio 2's, with
        jnz     .not2                   ; its channel and the program's
        cmp     byte [edi+EX_S16_Chan],0        ; 16-bit one
        je      .not2
        cmp     byte [edi+EX_S16_A2],4
        jb      ESSREG_S16_A2_Xfer
.not2:  call    ESSREG_S16_A2_Stop
        mov     al,0                    ; the 16-bit channel to Audio 1's
        call    ESSREG_S16_To
        jmp     short .a1
.eight: cmp     byte [edi+EX_S16_To],0  ; the program programs Audio 1's
        jne     .a1                     ; channel itself: what the front
        mov     byte [edi+EX_S16_DMA],0 ; carried over there is gone
.a1:    mov     al,[edi+EX_S16_Flags]   ; anew, Audio 1 stops and its FIFO
        and     al,S16_RUN | S16_AUTO   ; empties (ESSREG_S16_A1_Stop), so
        cmp     al,S16_RUN              ; that nothing of another stream
        je      .ext                    ; plays in this one's format; a
        call    ESSREG_S16_A1_Stop      ; single-cycle block goes on from
.ext:   call    ESSREG_S16_Ext          ; the last (ESSREG_S16_Format)
        mov     al,bl                   ; S16_16 and S16_AUTO from BL
        or      al,S16_RUN
        and     al,S16_16 | S16_RUN | S16_AUTO
        mov     ah,[edi+EX_S16_Flags]
        and     ah,S16_ESS | S16_EXT | S16_RESET
        or      al,ah
        mov     [edi+EX_S16_Flags],al
        call    ESSREG_S16_Count
        mov     al,bl                   ; B8h: stopped, auto-initialize (bit
        and     al,0x0C                 ; 2), the ADC (bit 3) and the DMA's
        test    al,0x08                 ; direction (bit 1); bits 7:4 0
        jz      .b8
        or      al,0x02
.b8:    mov     ah,0xB8
        call    ESSREG_S16_Set
        mov     ax,0xB902               ; demand transfers of 2 bytes, as
        test    byte [edi+ADI_CfgFlags+2],0x01  ; ESS's recording writes them,
        jnz     .b9                     ; or single ones with ESS's "Single
        xor     al,al                   ; Mode DMA"
.b9:    call    ESSREG_S16_Set
        mov     ah,0xA8                 ; mono or stereo: A8h bits 1:0
        call    ESSREG_S16_Get
        and     al,0xFC
        or      al,0x02
        test    cl,0x20
        jz      .mono
        xor     al,0x03
.mono:  mov     ah,0xA8
        call    ESSREG_S16_Set
        push    ecx
        mov     cl,al
        call    ESSREG_S16_Clocks
        pop     ecx
        mov     dx,0xD080               ; the format: B7h in DH, B6h in DL;
        test    cl,0x10                 ; 8-bit mono unsigned D0h and 80h,
        jz      .size                   ; signed F0h and 00h
        xor     dx,0x2080
.size:  test    bl,0x10
        jz      .chans
        xor     dh,0x04                 ; 16-bit: D4h, F4h
.chans: test    cl,0x20
        jz      .fmt
        xor     dh,0x48                 ; stereo: 98h, B8h, 9Ch, BCh
.fmt:   call    ESSREG_S16_Format
        mov     ax,0xB150               ; the interrupt and the DMA request
        call    ESSREG_S16_Or           ; on: bits 6 and 4, the others
        mov     ax,0xB250               ; kept, as ES1869.DRV keeps them
        call    ESSREG_S16_Or
        call    ESSREG_S16_DAC
        test    bl,0x10                 ; a 16-bit transfer on the 16-bit
        jz      .go                     ; channel: Audio 1's takes its
        call    ESSREG_S16_Carry        ; programming
.go:    mov     ax,0xB801
        jmp     ESSREG_S16_Or

; a 16-bit transfer on the program's 16-bit channel, EDI = ADI: the channel
; it goes to (EX_S16_To) takes its programming as VDMAD has it, or waits
; masked for it; EBX = the VM. Clobbers EAX
ESSREG_S16_Carry:
        cmp     byte [edi+EX_S16_DMA],0
        jne     .out
        call    ESSREG_S16_Handle
        or      eax,eax
        jz      .out
        mov     ebx,[edi+EX_S16_VM]
        call    ESSREG_S16_DMA_State
        cmp     byte [edi+EX_S16_DMA],0
        jne     .out
        call    ESSREG_S16_To_Handle
        or      eax,eax
        jz      .out
        VxDCall VDMAD_Mask_Channel
.out:   ret

; the 16-bit channel goes to Audio 1's channel (AL = 0) or Audio 2's (1),
; EDI = ADI: carried over anew when that changes
ESSREG_S16_To:
        cmp     [edi+EX_S16_To],al
        je      .out
        mov     [edi+EX_S16_To],al
        mov     byte [edi+EX_S16_DMA],0
.out:   ret

; EAX = the VDMAD handle of the channel the 16-bit one goes to, EDI = ADI
ESSREG_S16_To_Handle:
        mov     eax,[edi+ADI_DMA1Handle]
        cmp     byte [edi+EX_S16_To],0
        je      .out
        mov     eax,[edi+ADI_DMA2Handle]
.out:   ret

; a 16-bit playback on Audio 2, EDI = ADI: ESI = the samples, CL = the
; mode, BL = the transfer's bits. As ES1869.DRV starts Audio 2 (6:2C1A,
; docs/DRIVER_CONFIG.md): stopped (78h), for a start anew, the rate and
; filter clock (70h, 72h), minus the bytes (74h, 76h), the volume of the
; view's voice (7Ch, which a reset of the chip clears), the format and the
; interrupt (7Ah), its own rate (71h bit 1), the program's 16-bit channel
; carried over to Audio 2's, then 78h: auto-initialize as the command
; says, demand transfers of 4 bytes, or single ones with ESS's "Single Mode
; DMA". Audio 1's 16-bit transfer, if one runs, ends first
ESSREG_S16_A2_Xfer:
        mov     al,[edi+EX_S16_Flags]
        and     al,S16_RUN | S16_16
        cmp     al,S16_RUN | S16_16
        jne     .go
        call    ESSREG_S16_A1_Stop
.go:    mov     ax,0x7800
        call    ESSREG_S16_Mix_Set
        mov     al,bl
        and     al,S16_AUTO
        or      al,S16_RUN
        mov     [edi+EX_S16_Flags2],al
        call    ESSREG_S16_A2_Count
        call    ESSREG_S16_A2_Rate
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        mov     al,[edi+EX_S16_Mix+2]   ; the voice's volume
        mov     ah,[edi+EX_S16_Mix+3]
        call    ESSREG_S16_Nibbles
        mov     ah,0x7C
        call    ESSREG_Mix_Write
        mov     al,0x41                 ; 7Ah: the interrupt on, 16-bit,
        test    cl,0x10                 ; signed (bit 2) and stereo (bit 1)
        jz      .unsigned               ; as the mode says
        or      al,0x04
.unsigned:
        test    cl,0x20
        jz      .mono
        or      al,0x02
.mono:  mov     ah,0x7A
        call    ESSREG_Mix_Write
        mov     ah,0x71                 ; its own rate; and A1h as the
        call    ESSREG_Mix_Read         ; front computes it
        and     al,~0x20 & 0xFF
        or      al,0x02
        call    ESSREG_Mix_Write
        pop     edx
        mov     al,0x03                 ; 78h: the DMA, and the FIFO to the
        test    bl,S16_AUTO             ; DAC
        jz      .single
        or      al,0x10
.single:
        test    byte [edi+ADI_CfgFlags+2],0x01
        jz      .carry
        or      al,0x80
.carry: push    eax
        mov     al,1
        call    ESSREG_S16_To
        call    ESSREG_S16_Carry
        pop     eax
        mov     ah,0x78
        jmp     ESSREG_S16_Mix_Set

; 74h and 76h, Audio 2's counter reload, EDI = ADI: minus the bytes of ESI
; 16-bit samples
ESSREG_S16_A2_Count:
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        mov     eax,esi
        shl     eax,1
        neg     eax
        push    eax
        mov     ah,0x74
        call    ESSREG_Mix_Write
        pop     eax
        mov     al,ah
        mov     ah,0x76
        call    ESSREG_Mix_Write
        pop     edx
        ret

; 70h and 72h for the rate v * 44100 / 248 Hz, as ES1869.DRV picks them
; (6:2436): the divisor of the 793.8 kHz clock, round(4464 / v), or of the
; 768 kHz one, round(4319 / v) with bit 7, whichever comes nearer, 128 at
; most (6 kHz); the filter clock FDh from 44.1 kHz on, else 7.16 MHz over
; 35.9 times the rate, as for A2h; EDI = ADI
ESSREG_S16_A2_Rate:
        pushad
        movzx   ebx,byte [edi+EX_S16_RateV]
        mov     eax,4464                ; 793.8 kHz
        call    .div
        mov     ecx,eax
        mov     ebp,edx
        mov     eax,4319                ; 768 kHz
        call    .div
        mov     ah,0x80
        cmp     edx,ebp
        jb      .clock
        mov     eax,ecx
.clock: cmp     al,128
        jbe     .d
        mov     al,128
.d:     neg     al                      ; 128 - the divisor, with the clock's
        and     al,0x7F                 ; bit
        or      al,ah
        mov     ah,0x70
        call    ESSREG_S16_Mix_Set
        mov     al,0xFD
        cmp     bl,0xF8
        jae     .filter
        mov     eax,2245
        call    ESSREG_S16_Clocks.div
        neg     al
.filter:
        mov     ah,0x72
        call    ESSREG_S16_Mix_Set
        popad
        ret
; EAX = round(EAX / v), EDX = how far that times v is from EAX; EBX = v
.div:   push    eax
        xor     edx,edx
        div     ebx
        shl     edx,1
        cmp     edx,ebx
        jb      .down
        inc     eax
.down:  pop     edx
        push    eax
        imul    eax,ebx
        sub     edx,eax
        jns     .pos
        neg     edx
.pos:   pop     eax
        ret

; mixer register AH = AL (ESSREG_S16_Mix_Set), with AL's bits set
; (ESSREG_S16_Mix_Or) or kept (ESSREG_S16_Mix_And), EDI = ADI; keeps EDX
ESSREG_S16_Mix_Set:
        push    ecx
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        jmp     short ESSREG_S16_Mix_Chg
ESSREG_S16_Mix_Or:
        push    ecx
        push    edx
        mov     cl,al
        movzx   edx,word [edi+ADI_AudioBase]
        call    ESSREG_Mix_Read
        or      al,cl
        jmp     short ESSREG_S16_Mix_Chg
ESSREG_S16_Mix_And:
        push    ecx
        push    edx
        mov     cl,al
        movzx   edx,word [edi+ADI_AudioBase]
        call    ESSREG_Mix_Read
        and     al,cl
ESSREG_S16_Mix_Chg:
        call    ESSREG_Mix_Write
        pop     edx
        pop     ecx
        ret

; A4h and A5h, the counter's reload, EDI = ADI: minus the bytes of ESI
; samples
ESSREG_S16_Count:
        mov     eax,esi
        test    byte [edi+EX_S16_Flags],S16_16
        jz      .bytes
        shl     eax,1
.bytes: neg     eax
        push    eax
        mov     ah,0xA4
        call    ESSREG_S16_Set
        pop     eax
        mov     al,ah
        mov     ah,0xA5
        jmp     ESSREG_S16_Set

; the format, B6h = DL and B7h = DH, EDI = ADI: B6h, then B7h twice (DS
; p.49, table 16), first 51h or 71h, unless the chip has the format.
; Another format starts on an empty FIFO, the DMA stopped (B8h), so that
; the bytes of the last block don't play in it
ESSREG_S16_Format:
        bt      dword [edi+EX_S16_Known],0xB6 - 0xA0
        jnc     .write
        bt      dword [edi+EX_S16_Known],0xB7 - 0xA0
        jnc     .write
        cmp     dx,[edi+EX_S16_Ctrl+0xB6-0xA0]
        je      .out
.write: call    ESSREG_S16_Flush
        mov     al,dl
        mov     ah,0xB6
        call    ESSREG_S16_Force
        mov     ax,0xB771
        or      dl,dl
        jz      .first
        mov     al,0x51
.first: call    ESSREG_S16_Reg
        mov     al,dh
        mov     ah,0xB7
        jmp     ESSREG_S16_Force
.out:   ret

; mixer 71h bit 5 off for a transfer, so that A1h means what the front
; computes, EDI = ADI; the DACs' filters and 4x oversampling are
; SYSTEM.INI's (ESSREG_DMA1, at the DMA's start)
ESSREG_S16_DAC:
        push    eax
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        mov     ah,0x71
        call    ESSREG_Mix_Read
        test    al,0x20
        jz      .out
        and     al,~0x20 & 0xFF
        call    ESSREG_Mix_Write
.out:   pop     edx
        pop     eax
        ret

; --- the interrupts ----------------------------------------------------------

; AL = the kinds of Audio 1's interrupts that are pending, AH Audio 2's,
; EDI = ADI: bit 0 8-bit, bit 1 16-bit. Audio 1's counter (+Ch bit 0) is
; its transfer's kind, its processor's (bit 2: F2h, the firmware's
; transfers, MIDI input) the 8-bit one unless F3h asked for it; Audio 2's
; latch (7Ah bit 7) is the 16-bit one, read from 7Ah because ESS's
; interrupt procedure masks it (bit 6) where the MPU-401 shares the IRQ
ESSREG_S16_Pending:
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        in      al,dx
        mov     ah,al
        xor     al,al
        test    ah,0x01
        jz      .proc
        inc     al
        test    byte [edi+EX_S16_Flags],S16_16
        jz      .proc
        inc     al
.proc:  test    ah,0x04
        jz      .a2
        mov     ah,1
        test    byte [edi+EX_S16_State],S16S_T16
        jz      .or
        inc     ah
.or:    or      al,ah
.a2:    mov     ah,0
        cmp     byte [edi+EX_S16_A2],4
        jae     .ret
        push    eax
        sub     edx,byte 0x0C
        mov     ah,0x7A
        call    ESSREG_Mix_Read
        test    al,0x80
        pop     eax
        jz      .ret
        mov     ah,2
.ret:   pop     edx
        ret

; the acknowledge of the kind CL (1 8-bit, 2 16-bit), EDI = ADI: Audio 1's
; interrupt when one of that kind is pending (a read of +Eh acknowledges
; them all), and Audio 2's for the 16-bit kind (7Ah bit 7 cleared). A
; queued block's turn has come: the auto-initialize ends with it
ESSREG_S16_Ack:
        call    ESSREG_S16_Pending
        push    eax
        test    al,cl
        jz      .a2
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0E
        in      al,dx
        pop     edx
        test    byte [edi+EX_S16_Flags],S16_LAST
        jz      .a2
        and     byte [edi+EX_S16_Flags],~(S16_LAST | S16_AUTO) & 0xFF
        mov     ax,0xB8FB
        call    ESSREG_S16_And
.a2:    pop     eax
        test    ah,cl
        jz      .ret
        mov     ax,0x7A7F
        call    ESSREG_S16_Mix_And
        test    byte [edi+EX_S16_Flags2],S16_LAST
        jz      .ret
        and     byte [edi+EX_S16_Flags2],~(S16_LAST | S16_AUTO) & 0xFF
        mov     ax,0x78EF
        jmp     ESSREG_S16_Mix_And
.ret:   ret

; --- the mixer ---------------------------------------------------------------

; the Sound Blaster 16's mixer registers 30h-47h: the program's view in
; EX_S16_Mix, with the bits the CT1745 keeps, and the chip's volumes they
; stand for; the Sound Blaster Pro's registers are views of them. AL
; written to the mixer register the VM selected, ESI = the data port, EDI =
; ADI; the chip's own registers get it as it comes, at the index the VM
; gave the chip, and the view follows the levels they change. A reset is
; the Sound Blaster 16's: its levels go to the chip, and the chip's other
; registers stay as Windows and the VxD have them (the 3-D effect, the
; DACs' mode, the music DAC's source, Audio 2), which the chip's own reset
; would put back to the power-on values
ESSREG_S16_Mix_Write:
        call    ESSREG_S16_Index        ; CH = the register
        movzx   edx,word [edi+ADI_AudioBase]
        test    byte [edi+EX_S16_Flags],S16_ESS ; the chip's own mixer for
        jnz     .chip                   ; a program that knows the chip
        or      ch,ch
        jz      .reset
        cmp     ch,0x30
        jb      .pro
        cmp     ch,0x47
        ja      .high
        call    ESSREG_S16_Mix_Fresh
        movzx   ebx,ch
        and     al,[ESSREG_S16_Mix_Mask+ebx-0x30]
        mov     [edi+EX_S16_Mix+ebx-0x30],al
        jmp     ESSREG_S16_Mix_Apply
.pro:   call    ESSREG_S16_Pro
        jc      .chip
        jnz     .done                   ; 0Ch and 0Eh: nothing
        call    ESSREG_S16_Mix_Fresh
        cmp     ch,0x0A
        je      .mic
        mov     ah,al                   ; a pair's nibbles, as the CT1745
        and     al,0xF0                 ; keeps them
        or      al,0x08
        shl     ah,4
        or      ah,0x08
        mov     [edi+EX_S16_Mix+ebx],ax
        mov     ch,bl
        add     ch,0x31
        call    ESSREG_S16_Mix_Apply
        dec     ch
        jmp     ESSREG_S16_Mix_Apply
.mic:   and     al,7                    ; 0Ah's 3 bits
        shl     al,5
        or      al,0x18
        mov     [edi+EX_S16_Mix+10],al
        mov     ch,0x3A
        jmp     ESSREG_S16_Mix_Apply
.chip:  call    ESSREG_S16_Chip_Out     ; the chip's own registers, and the
        jmp     ESSREG_S16_Mix_Stale    ; view follows the levels they change
.high:  cmp     ch,0x80
        jb      .chip
        cmp     ch,0x81                 ; the 16-bit channel the program
        je      .select                 ; picks
        cmp     ch,0x82                 ; the card's IRQ and status: kept
        ja      .chip
.done:  ret
.select:
        and     eax,0xE0                ; bit 5 channel 5, 6 and 7 theirs, the
        bsf     eax,eax                 ; first that is set, as the DSP picks
        jnz     .ch                     ; it; none 0
        xor     eax,eax
.ch:    mov     [edi+EX_S16_Sel],al
        mov     byte [edi+EX_S16_Chan],0
        cmp     eax,[ESSREG_S16_Chan]   ; the one the front carries over, if
        jne     .done                   ; it has it
        call    ESSREG_S16_Handle.any
        or      eax,eax
        jz      .done
        mov     al,[edi+EX_S16_Sel]
        mov     [edi+EX_S16_Chan],al
        ret
.reset: call    ESSREG_S16_Mix_Reset
        call    ESSREG_S16_Mix_All
        mov     ah,0x0E                 ; and the Sound Blaster Pro's stereo
        call    ESSREG_Mix_Read         ; off, as after the chip's reset
        and     al,~0x02 & 0xFF
        jmp     ESSREG_Mix_Write

; the bits of 30h-47h that the CT1745 keeps
ESSREG_S16_Mix_Mask:
        times 11 db 0xF8                ; 30h-3Ah
        db 0xC0, 0x1F, 0x7F, 0x7F       ; 3Bh-3Eh
        db 0xC0, 0xC0, 0xC0, 0xC0, 0x01 ; 3Fh-43h
        db 0xF0, 0xF0, 0xF0, 0xF0       ; 44h-47h

; the Sound Blaster Pro's register CH (below 30h), EDI = ADI: EBX = the
; view's offset of its left level, ZF set; ZF clear for 0Ch and 0Eh, which
; the CT1745 doesn't have; CF set for the chip's own. Keeps EAX and ECX
ESSREG_S16_Pro:
        xor     ebx,ebx
.next:  cmp     [ESSREG_S16_Pro_Regs+ebx],ch
        je      .found
        inc     ebx
        cmp     bl,6
        jb      .next
        cmp     ch,0x0C
        je      .nothing
        cmp     ch,0x0E
        je      .nothing
        stc
        ret
.nothing:
        or      ch,ch                   ; ZF and CF clear
        ret
.found: shl     ebx,1
        cmp     al,al                   ; ZF set, CF clear
        ret
; master, voice, MIDI, CD, line and the mic, each at twice its place
ESSREG_S16_Pro_Regs:
        db 0x22, 0x04, 0x26, 0x28, 0x2E, 0x0A

; a write of the chip's mixer register CH, EDI = ADI: the view's levels it
; changes are stale until the program uses the Sound Blaster 16's
; registers (ESSREG_S16_Mix_Fresh), so that a program's accesses reach the
; chip as they come and the view follows what another program set. The
; chip's own level registers are 60h and 62h (the master), the extended
; ones of the Sound Blaster Pro's (ESSREG_S16_Aliases), 3Ch and 1Ch; its
; reset (00h, for a program that knows the chip) changes them all
ESSREG_S16_Mix_Stale:
        push    eax
        push    ebx
        mov     al,0xFF
        or      ch,ch
        jz      .set
        mov     al,0x01                 ; the master
        cmp     ch,0x60
        je      .set
        cmp     ch,0x62
        je      .set
        mov     ebx,ESSREG_S16_Aliases
.find:  shl     al,1
        cmp     byte [ebx],0
        je      .other
        cmp     [ebx+1],ch
        je      .set
        add     ebx,byte 3
        jmp     .find
.other: cmp     ch,0x3C                 ; AL = 40h: the PC speaker
        je      .set
        shl     al,1                    ; 80h: the record source
        cmp     ch,0x1C
        jne     .out
.set:   or      [edi+EX_S16_Stale],al
.out:   pop     ebx
        pop     eax
        ret

; the view's stale levels from the chip, EDI = ADI: before the program
; reads or writes the Sound Blaster 16's registers, and when its front
; ends; the output switches gate them again at the next write. The record
; source becomes the input switches that pick it: the mic, CD or line
; alone, the record mixer as the reset's several, else none, so that
; writing them back keeps it
ESSREG_S16_Mix_Fresh:
        cmp     byte [edi+EX_S16_Stale],0
        jne     .fresh
        ret
.fresh: pushad
        movzx   edx,word [edi+ADI_AudioBase]
        mov     bl,[edi+EX_S16_Stale]
        mov     byte [edi+EX_S16_Stale],0
        shr     bl,1
        jnc     .pairs
        call    ESSREG_S16_Mix_Master
.pairs: mov     esi,ESSREG_S16_Aliases
.next:  cmp     byte [esi],0
        je      .speaker
        shr     bl,1
        jnc     .skip
        mov     ah,[esi+1]
        movzx   ecx,byte [esi+2]
        call    ESSREG_S16_Mix_Pair
.skip:  add     esi,byte 3
        jmp     .next
.speaker:
        shr     bl,1
        jnc     .source
        mov     ah,0x3C                 ; the PC speaker, the 2 bits of 3Bh
        call    ESSREG_Mix_Read
        and     al,0x06
        shl     al,5
        mov     [edi+EX_S16_Mix+0x0B],al
.source:
        shr     bl,1
        jnc     .done
        mov     ah,0x1C
        call    ESSREG_Mix_Read
        and     al,0x07
        mov     cx,0x0101               ; CH left, CL right: the mic
        or      al,al
        jz      .src
        mov     cx,0x0402               ; CD
        cmp     al,2
        je      .src
        mov     cx,0x1008               ; line
        cmp     al,6
        je      .src
        mov     cx,0x150B               ; the record mixer
        cmp     al,5
        je      .src
        xor     ecx,ecx
.src:   mov     [edi+EX_S16_Mix+0x0D],ch
        mov     [edi+EX_S16_Mix+0x0E],cl
.done:  popad
        ret

; the Sound Blaster Pro's register, the chip's it goes to, the view's
; offset of its left level
ESSREG_S16_Aliases:
        db 0x04, 0x14, 2, 0x26, 0x36, 4, 0x28, 0x38, 6, 0x2E, 0x3E, 8
        db 0x0A, 0x1A, 10, 0

; the view's L and R at EX_S16_Mix+ECX from the nibbles of the chip's
; register AH (EX_S16_Mix+10, the mono mic, from 1Ah's left only), EDX =
; Audio_Base, EDI = ADI; clobbers EAX
ESSREG_S16_Mix_Pair:
        call    ESSREG_Mix_Read
        mov     ah,al
        and     al,0xF0
        mov     [edi+EX_S16_Mix+ecx],al
        cmp     ecx,byte 10
        je      .out
        shl     ah,4
        mov     [edi+EX_S16_Mix+ecx+1],ah
.out:   ret

; the view's master from the chip's 60h and 62h, their 6 bits to the 5 of
; 30h and 31h, 0 when muted; EDX = Audio_Base, EDI = ADI; clobbers EAX
ESSREG_S16_Mix_Master:
        mov     ah,0x60
        call    .one
        mov     [edi+EX_S16_Mix+0],al
        mov     ah,0x62
        call    .one
        mov     [edi+EX_S16_Mix+1],al
        ret
.one:   call    ESSREG_Mix_Read
        test    al,0x40
        jz      .on
        xor     al,al
.on:    and     al,0x3E
        shl     al,2
        ret

; the view as a Sound Blaster 16 shows the levels it has, for a program
; that starts: master, voice, MIDI, CD, line, mic and the PC speaker from
; the chip, the input switches from its record source, the rest as after
; a reset; EDI = ADI
ESSREG_S16_Mix_Chip:
        call    ESSREG_S16_Mix_Reset
        mov     byte [edi+EX_S16_Stale],0xFF
        jmp     ESSREG_S16_Mix_Fresh

; the view's levels and record source to the chip, EDI = ADI
ESSREG_S16_Mix_All:
        push    ecx
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
        mov     ch,0x30                 ; master
        call    ESSREG_S16_Mix_Apply
        mov     ch,0x31
        call    ESSREG_S16_Mix_Apply
        mov     ch,0x32                 ; voice
        call    ESSREG_S16_Mix_Apply
        mov     ch,0x34                 ; MIDI
        call    ESSREG_S16_Mix_Apply
        mov     ch,0x3B                 ; the PC speaker
        call    ESSREG_S16_Mix_Apply
        mov     ch,0x3C                 ; CD, line and the mic, switched
        call    ESSREG_S16_Mix_Apply
        mov     ch,0x3D                 ; the record source
        call    ESSREG_S16_Mix_Apply
        pop     edx
        pop     ecx
        ret

; CH = the mixer register the VM selected, EDI = ADI, ESI = the data port:
; the index it wrote through the front, or the chip's when it wrote it
; before the front started (EX_S16_Index FFh)
ESSREG_S16_Index:
        mov     ch,[edi+EX_S16_Index]
        cmp     ch,0xFF
        jne     .out
        push    eax
        push    edx
        lea     edx,[esi-1]
        in      al,dx
        mov     [edi+EX_S16_Index],al
        mov     ch,al
        pop     edx
        pop     eax
.out:   ret

; the Sound Blaster 16's reset values into the view, EDI = ADI
ESSREG_S16_Mix_Reset:
        mov     byte [edi+EX_S16_Stale],0
        push    ecx
        push    esi
        push    edi
        lea     edi,[edi+EX_S16_Mix]
        mov     esi,ESSREG_S16_Mix_Default
        mov     ecx,24 / 4
        cld
        rep     movsd
        pop     edi
        pop     esi
        pop     ecx
        ret
ESSREG_S16_Mix_Default:
        db 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0   ; master, voice, MIDI
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   ; CD, line, mic, PC speaker
        db 0x1F, 0x15, 0x0B, 0x00, 0x00, 0x00   ; the switches, the gains
        db 0x00, 0x00, 0x80, 0x80, 0x80, 0x80   ; AGC, treble, bass

; AL = the VM's read of the mixer register it selected, ESI = the data
; port, EDI = ADI, EBX = VM
ESSREG_S16_Mix_Read:
        call    ESSREG_S16_Index
        test    byte [edi+EX_S16_Flags],S16_ESS
        jnz     ESSREG_S16_Chip_In
        cmp     ch,0x30
        jb      .pro
        cmp     ch,0x47
        ja      .high
        call    ESSREG_S16_Mix_Fresh
        movzx   ebx,ch
        mov     al,[edi+EX_S16_Mix+ebx-0x30]
        ret
.pro:   call    ESSREG_S16_Pro
        jc      ESSREG_S16_Chip_In
        mov     al,0
        jnz     .ret                    ; 0Ch and 0Eh: 00h
        call    ESSREG_S16_Mix_Fresh
        mov     ax,[edi+EX_S16_Mix+ebx]
        cmp     ch,0x0A
        je      .mic
        and     al,0xF0
        shr     ah,4
        or      al,ah
        ret
.mic:   shr     al,5
.ret:   ret
.high:  cmp     ch,0x80
        je      ESSREG_S16_IRQ_Bits
        cmp     ch,0x81
        je      ESSREG_S16_DMA_Bits
        cmp     ch,0x82
        jne     ESSREG_S16_Chip_In
        call    ESSREG_S16_Pending      ; 82h: 20h, the interrupts that are
        or      al,ah                   ; pending, and the MPU-401's
        or      al,0x20
        jmp     ESSREG_S16_MPU_Bit

; AL = mixer 80h: F0h and bit 0 IRQ 2, 1 IRQ 5, 2 IRQ 7, 3 IRQ 10; EDI =
; ADI
ESSREG_S16_IRQ_Bits:
        mov     al,[edi+ADI_IRQ]
        cmp     al,2
        je      .b0
        cmp     al,9                    ; 9 is 2's
        je      .b0
        cmp     al,5
        je      .b1
        cmp     al,7
        je      .b2
        cmp     al,10
        je      .b3
        mov     al,0xF0
        ret
.b0:    mov     al,0xF1
        ret
.b1:    mov     al,0xF2
        ret
.b2:    mov     al,0xF4
        ret
.b3:    mov     al,0xF8
        ret

; AL = mixer 81h: bits 0, 1 and 3 for the 8-bit channel, 5 to 7 for the
; 16-bit one the program picked; EDI = ADI
ESSREG_S16_DMA_Bits:
        push    ecx
        mov     cl,[edi+ADI_DMA1Channel]
        mov     al,1
        shl     al,cl
        mov     cl,[edi+EX_S16_Sel]
        or      cl,cl
        jz      .out
        mov     ah,1
        shl     ah,cl
        or      al,ah
.out:   pop     ecx
        ret

; AL |= 4 when a physical MPU-401 interrupt of the VM in EBX waits (ESS's
; handler sets ADI flag 1000h for the MPU-401's owner), EDI = ADI
ESSREG_S16_MPU_Bit:
        test    word [edi+ADI_Flags],0x1000
        jz      .out
        cmp     [edi+ADI_MPUOwner],ebx
        jne     .out
        or      al,0x04
.out:   ret

; the chip's volume for the Sound Blaster 16's register CH, from the
; program's view, EDI = ADI, EDX = Audio_Base; keeps ECX. Each register
; goes to its own: master left and right to 60h and 62h, voice to 14h,
; MIDI to 36h, CD to 38h, line to 3Eh, the mic to 1Ah, the PC speaker to
; 3Ch; the output switches gate CD, line and the mic, and the input
; switches pick the record source of 1Ch
ESSREG_S16_Mix_Apply:
        push    eax
        push    ecx
        cmp     ch,0x30
        je      .left
        cmp     ch,0x31
        je      .right
        cmp     ch,0x33
        jbe     .voice
        cmp     ch,0x35
        jbe     .fm
        cmp     ch,0x37
        jbe     .cd
        cmp     ch,0x39
        jbe     .line
        cmp     ch,0x3A
        je      .mic
        cmp     ch,0x3B
        je      .speaker
        cmp     ch,0x3C
        je      .switches
        cmp     ch,0x3E
        jbe     .input
        jmp     .out                    ; the gains, AGC and tone: kept only
.left:  mov     al,[edi+EX_S16_Mix+0]   ; 8 bits to the 6 of 60h and 62h
        mov     ah,0x60
        jmp     short .master
.right: mov     al,[edi+EX_S16_Mix+1]
        mov     ah,0x62
.master:
        shr     al,2
        call    ESSREG_Mix_Write
        jmp     .out
.voice: mov     al,[edi+EX_S16_Mix+2]   ; the Audio 1 (wave) volume 14h
        mov     ah,[edi+EX_S16_Mix+3]
        call    ESSREG_S16_Nibbles
        mov     ah,0x14
        call    ESSREG_Mix_Write
        cmp     byte [edi+EX_S16_A2],4  ; and Audio 2's, 7Ch, when it plays
        jae     .out                    ; the program's sound
        mov     ah,0x7C
        call    ESSREG_Mix_Write
        jmp     .out
.fm:    mov     al,[edi+EX_S16_Mix+4]   ; the music DAC (FM) volume 36h
        mov     ah,[edi+EX_S16_Mix+5]
        call    ESSREG_S16_Nibbles
        mov     ah,0x36
        call    ESSREG_Mix_Write
        jmp     .out
.switches:
        call    .cd_
        call    .line_
        call    .mic_
        jmp     .out
.cd:    call    .cd_
        jmp     .out
.line:  call    .line_
        jmp     .out
.mic:   call    .mic_
        jmp     .out
.speaker:
        mov     al,[edi+EX_S16_Mix+0x0B] ; the PC speaker 3Ch, 3 bits
        shr     al,5
        mov     ah,0x3C
        call    ESSREG_Mix_Write
        jmp     .out
.input: mov     ah,[edi+EX_S16_Mix+0x0D] ; the record source 1Ch, bits 2:0,
        or      ah,[edi+EX_S16_Mix+0x0E] ; from the input switches of both
        xor     ecx,ecx                 ; channels: CL = a source alone, CH
        test    ah,0x01                 ; = how many; the mic
        jz      .nomic
        inc     ch
.nomic: test    ah,0x06                 ; AuxA (CD)
        jz      .nocd
        mov     cl,2
        inc     ch
.nocd:  test    ah,0x18                 ; line
        jz      .noline
        mov     cl,6
        inc     ch
.noline:
        test    ah,0x60                 ; the music: the record mixer
        jz      .count
        mov     cl,5
        inc     ch
.count: cmp     ch,1
        jb      .out                    ; nothing: as it is
        je      .src
        mov     cl,5                    ; several: the record mixer
.src:   mov     ah,0x1C
        call    ESSREG_Mix_Read
        and     al,0xF8
        or      al,cl
        mov     ah,0x1C
        call    ESSREG_Mix_Write
.out:   pop     ecx
        pop     eax
        ret
.cd_:   mov     al,[edi+EX_S16_Mix+6]   ; AuxA (CD) 38h, through the
        mov     ah,[edi+EX_S16_Mix+7]   ; switches' bits 2 and 1
        test    byte [edi+EX_S16_Mix+0x0C],0x04
        jnz     .cdl
        xor     al,al
.cdl:   test    byte [edi+EX_S16_Mix+0x0C],0x02
        jnz     .cdr
        xor     ah,ah
.cdr:   call    ESSREG_S16_Nibbles
        mov     ah,0x38
        jmp     ESSREG_Mix_Write
.line_: mov     al,[edi+EX_S16_Mix+8]   ; line 3Eh, through bits 4 and 3
        mov     ah,[edi+EX_S16_Mix+9]
        test    byte [edi+EX_S16_Mix+0x0C],0x10
        jnz     .linel
        xor     al,al
.linel: test    byte [edi+EX_S16_Mix+0x0C],0x08
        jnz     .liner
        xor     ah,ah
.liner: call    ESSREG_S16_Nibbles
        mov     ah,0x3E
        jmp     ESSREG_Mix_Write
.mic_:  mov     al,[edi+EX_S16_Mix+0x0A] ; the mono mic 1Ah, through bit 0
        test    byte [edi+EX_S16_Mix+0x0C],0x01
        jnz     .micon
        xor     al,al
.micon: mov     ah,al
        call    ESSREG_S16_Nibbles
        mov     ah,0x1A
        jmp     ESSREG_Mix_Write

; AL = the chip's two 4-bit volumes for the 8-bit left AL and right AH
ESSREG_S16_Nibbles:
        and     al,0xF0
        shr     ah,4
        or      al,ah
        ret

; --- the program's 16-bit channel ------------------------------------------------

; VDMAD's callback for the VxD's 16-bit channel: EAX = its handle, EBX =
; the VM, EDX = what the VM did. The device whose front serves the VM and
; carries the channel over acts on it
ESSREG_S16_DMA:
        pushad
        call    ESSREG_S16_ADI
        or      edi,edi
        jz      .out
        cmp     byte [edi+EX_S16_Chan],0
        je      .out
        call    ESSREG_S16_Notify
.out:   popad
        ret

; what the VM did with its 16-bit channel, as VDMAD tells its owner: EAX =
; the channel's VDMAD handle, EBX = the VM, EDX = 0 when it programmed the
; channel (while unmasked), the port's address with bit 0 when it masked or
; requested it, 1 when it read its count, 2 or 3 when it read its page or
; address; EDI = ADI
ESSREG_S16_Notify:
        or      edx,edx
        jz      ESSREG_S16_DMA_State
        cmp     edx,byte 3
        ja      ESSREG_S16_DMA_State
        cmp     edx,byte 1
        je      ESSREG_S16_Position
        ret

; the VM's programming of its 16-bit channel, EAX = the channel's VDMAD
; handle, EBX = the VM, EDI = ADI: unmasked, the channel it goes to
; (EX_S16_To, Audio 1's or Audio 2's) gets the same region, bytes and mode,
; in demand transfers unless ESS's "Single Mode DMA" (ADI flag 10000h
; clear) keeps the VM's, and ESS's handler for that channel and VDMAD's
; program the chip's channel from them, as if the VM had; masked, that
; channel is masked too
ESSREG_S16_DMA_State:
        pushad
        push    eax
        call    ESSREG_S16_To_Handle
        mov     ebp,eax                 ; EBP = the channel it goes to
        pop     eax
        or      ebp,ebp
        jz      .out
        VxDCall VDMAD_Get_Virt_State    ; ESI, ECX, DL and DH
        test    dl,DMA_MASKED
        jz      .go
        cmp     byte [edi+EX_S16_DMA],0
        je      .out
        mov     byte [edi+EX_S16_DMA],0
        mov     eax,ebp
        VxDCall VDMAD_Mask_Channel
        jmp     .out
.go:    and     dh,~DMA_16BIT & 0xFF    ; a byte channel's count
        test    byte [edi+ADI_CfgFlags+2],0x01
        jz      .mode
        and     dl,0x3F                 ; demand mode
.mode:  mov     eax,ebp
        VxDCall VDMAD_Set_Virt_State
        mov     byte [edi+EX_S16_DMA],1
        cmp     byte [edi+EX_S16_To],0
        jne     .a2
        call    L1_0518                 ; ESS's handler of Audio 1's channel
        jmp     short .out
.a2:    xor     edx,edx                 ; of Audio 2's: the VM programmed it
        call    L1_0870
.out:   popad
        ret

; the VM's view of its 16-bit channel's count, from the channel it goes
; to, EDI = ADI, EBX = the VM; AL kept
ESSREG_S16_Position:
        cmp     byte [edi+EX_S16_DMA],0
        je      .out
        pushad
        call    ESSREG_S16_To_Handle
        VxDCall VDMAD_Get_Phys_Count    ; ECX = bytes left - 1
        inc     ecx
        call    ESSREG_S16_Handle
        or      eax,eax
        jz      .done
        VxDCall VDMAD_Set_Virt_Count    ; in bytes: words - 1 to the VM
.done:  popad
.out:   ret

; EAX = the VDMAD handle of the front's 16-bit channel, EDI = ADI, 0 if
; none; .any: of the channel SoundBlaster16DMA names, whether the program
; picked it or not
ESSREG_S16_Handle:
        mov     al,[edi+EX_S16_Chan]
        or      al,al
        jz      .none
.any:   mov     eax,[ESSREG_S16_Chan]
        or      eax,eax
        jz      .out
        cmp     [edi+ADI_DMA2Channel],al
        mov     eax,[edi+ADI_DMA2Handle]
        je      .out
        mov     eax,[ESSREG_S16_Virt]
        ret
.none:  xor     eax,eax
.out:   ret

; --- a DOS program's FM on its own clock ---------------------------------------

; Windows hands a DOS box the timer ticks it missed in a burst once it runs
; again, and the FM music that a program plays from its timer interrupt
; comes in the same bursts, so its tempo wavers. With DosFMDelay=n, the FM
; writes of a real-mode program that owns the chip go to its virtual chip,
; which answers its reads, and to a queue, each with the time of the
; program's last tick on a clock that follows its ticks but runs evenly
; (ESSREG_FMQ_Tick). A time-out plays each write to the chip n ms after that
; time, in the program's order. The ticks come from interrupt 8 as VMM
; reflects it into the VM (Hook_V86_Int_Chain), so a program in protected
; mode, whose ticks don't pass there, and one whose writes don't follow its
; ticks, play directly, as with ESS's driver

; the VM in EBX took the FM chip, EDI = ADI: a real-mode DOS program's FM
; plays from a queue from now on, unless its writes were found off its
; ticks before. Its FM ports trap again, and VTD gives the time-outs 1 ms
; timer interrupts
ESSREG_FMQ_Start:
        test    byte [ESSREG_Opts+1],OPT_FM_TIMED >> 8
        jz      .out
        cmp     byte [ESSREG_FMQ_Hooked],0
        je      .out
        VxDCall Test_Sys_VM_Handle
        je      .out
        test    byte [ebx],VMSTAT_PM_APP        ; CB_VM_Status
        jnz     .out
        cmp     [edi+EX_FMQ_Direct],ebx
        je      .out
        cmp     dword [edi+EX_FMQ_VM],byte 0
        jne     .out
        pushad
        call    ESSREG_VFM_Get                  ; for its reads
        or      esi,esi
        jz      .done
        push    byte 1                          ; HEAPZEROINIT
        push    dword FMQ_SIZE
        VxDCall _HeapAllocate
        add     esp,byte 8
        or      eax,eax
        jz      .done
        mov     esi,eax
        mov     [edi+EX_FMQ_Blk],esi
        mov     [edi+EX_FMQ_VM],ebx
        inc     dword [ESSREG_FMQ_Streams]
        call    ESSREG_Clock
        mov     [esi+FMQ_V],eax                 ; the real time, until a tick
        mov     [esi+FMQ_Last],eax
        mov     eax,[ESSREG_FMQ_Delay]
        mov     [esi+FMQ_Min],eax
        call    FM_Enable_Local_Trapping
        mov     eax,1
        VxDCall VTD_Begin_Min_Int_Period
        jc      .done
        or      byte [esi+FMQ_Flags],FMQF_MININT
.done:  popad
.out:   ret

; a trapped access by the VM whose FM plays from the queue, EBX = VM,
; EDI = ADI, EDX = port, ECX = type, EAX = data written; returns AL = data
; read: its virtual chip answers the reads and keeps the registers, and the
; writes go to the queue as well
ESSREG_FMQ_IO:
        or      ecx,ecx
        jz      ESSREG_VFM_IO
        push    eax
        push    edx
        call    ESSREG_VFM_IO
        pop     edx
        pop     eax
        jmp     ESSREG_FMQ_Put

; the write of AL to FM port EDX, for the program's last tick, EDI = ADI:
; due the delay after the tick's time on the clock. A data write long after
; the last tick counts against the program: with a quarter of 64 late, it
; plays directly from then on
ESSREG_FMQ_Put:
        pushad
        mov     esi,[edi+EX_FMQ_Blk]
        or      esi,esi
        jz      .done
        and     edx,byte 3
        mov     dh,al                           ; DL = the port's offset,
        call    ESSREG_Clock                    ; DH = the value
        mov     ecx,eax                         ; ECX = now
        xor     ebp,ebp                         ; EBP = 1: off its ticks
        test    dl,1
        jz      .queue                          ; an address
        mov     eax,FMQ_TICK0 / 2               ; no tick yet: the start was
        test    byte [esi+FMQ_Flags],FMQF_LAST  ; one, of the timer's default
        jz      .half                           ; length
        mov     eax,[esi+FMQ_T]
        shr     eax,9                           ; half a tick (us)
.half:  cmp     eax,FMQ_LATE
        jae     .limit
        mov     eax,FMQ_LATE
.limit: mov     ebx,ecx
        sub     ebx,[esi+FMQ_Last]
        cmp     ebx,eax
        jbe     .count
.late:  inc     byte [esi+FMQ_Late]
.count: inc     byte [esi+FMQ_Writes]
        cmp     byte [esi+FMQ_Writes],64
        jb      .queue
        mov     al,[esi+FMQ_Late]
        mov     byte [esi+FMQ_Writes],0
        mov     byte [esi+FMQ_Late],0
        cmp     al,16
        jb      .queue
        inc     ebp
.queue: pushfd
        cli
        cmp     dword [esi+FMQ_Count],FMQ_LEN
        jb      .room
        call    ESSREG_FMQ_Out                  ; the oldest now, for room
.room:  mov     eax,[esi+FMQ_Head]
        add     eax,[esi+FMQ_Count]
        and     eax,FMQ_LEN - 1
        lea     eax,[esi+FMQ_Queue+eax*8]
        mov     ebx,[esi+FMQ_V]
        add     ebx,[ESSREG_FMQ_Delay]
        mov     [eax],ebx
        mov     [eax+4],dx
        inc     dword [esi+FMQ_Count]
        popfd
        or      ebp,ebp
        jnz     .direct
        cmp     dword [esi+FMQ_Timer],byte 0
        jne     .done
        call    ESSREG_FMQ_Drain                ; what's due, and the time-out
.done:  popad
        ret
.direct:
        mov     ebx,[edi+EX_FMQ_VM]
        call    ESSREG_FMQ_Direct
        popad
        ret

; the oldest entry to the chip, ESI = block, EDI = ADI, interrupts off:
; its port write, then ESS's 6 status reads (L1_0A68)
ESSREG_FMQ_Out:
        push    eax
        push    ecx
        push    edx
        mov     eax,[esi+FMQ_Head]
        lea     ecx,[esi+FMQ_Queue+eax*8]
        inc     eax
        and     eax,FMQ_LEN - 1
        mov     [esi+FMQ_Head],eax
        dec     dword [esi+FMQ_Count]
        movzx   edx,word [edi+ADI_FMBase]
        movzx   eax,byte [ecx+4]
        add     edx,eax
        mov     al,[ecx+5]
        out     dx,al
        movzx   edx,word [edi+ADI_FMBase]
        call    L1_0A68
        pop     edx
        pop     ecx
        pop     eax
        ret

; what's due to the chip, in order, ESI = block, EDI = ADI; then the
; time-out of the next entry
ESSREG_FMQ_Drain:
        pushad
        call    ESSREG_Clock
        mov     ecx,eax                         ; ECX = now
.next:  pushfd
        cli
        cmp     dword [esi+FMQ_Count],byte 0
        je      .empty
        mov     eax,[esi+FMQ_Head]
        mov     eax,[esi+FMQ_Queue+eax*8]
        sub     eax,ecx                         ; us until it's due
        jg      .later
        call    ESSREG_FMQ_Out
        popfd
        jmp     .next
.later: popfd
        add     eax,999
        mov     ebx,1000
        xor     edx,edx
        div     ebx                             ; ms, rounded up
        mov     edx,edi                         ; its reference data: the ADI
        push    esi
        mov     esi,ESSREG_FMQ_Due
        VxDCall Set_Global_Time_Out             ; ESI = its handle
        mov     eax,esi
        pop     esi
        mov     [esi+FMQ_Timer],eax
        jmp     .done
.empty: popfd
.done:  popad
        ret

; the time-out of the next entry (Set_Global_Time_Out), EDX = ADI
ESSREG_FMQ_Due:
        pushad
        mov     edi,edx
        mov     esi,[edi+EX_FMQ_Blk]
        or      esi,esi
        jz      .done
        mov     dword [esi+FMQ_Timer],0
        call    ESSREG_FMQ_Drain
.done:  popad
        ret

; the program's FM stops playing from the queue, EDI = ADI: what's queued
; goes to the chip now, in its order (AL = 1), or is dropped (AL = 0: the
; device or the VxD goes away). Its virtual chip, which the chip now
; matches, has nothing to give back at the next take, as after direct play.
; The block goes, and VTD's 1 ms interrupts
ESSREG_FMQ_End:
        pushad
        mov     esi,[edi+EX_FMQ_Blk]
        or      esi,esi
        jz      .done
        push    eax
        push    esi
        mov     ebx,[edi+EX_FMQ_VM]
        call    ESSREG_VFM_Find
        jz      .vfm
        mov     esi,eax
        call    ESSREG_VFM_Forget
.vfm:   pop     esi
        pop     eax
        mov     bl,al
        push    esi
        xor     eax,eax
        xchg    eax,[esi+FMQ_Timer]
        or      eax,eax
        jz      .flush
        mov     esi,eax
        VxDCall Cancel_Time_Out
.flush: pop     esi
        or      bl,bl
        jz      .free
.out:   pushfd                                  ; interrupts in between the
        cli                                     ; entries: up to 1024
        cmp     dword [esi+FMQ_Count],byte 0
        je      .sent
        call    ESSREG_FMQ_Out
        popfd
        jmp     .out
.sent:  popfd
.free:  test    byte [esi+FMQ_Flags],FMQF_MININT
        jz      .heap
        mov     eax,1
        VxDCall VTD_End_Min_Int_Period
.heap:  xor     eax,eax
        mov     [edi+EX_FMQ_Blk],eax
        mov     [edi+EX_FMQ_VM],eax
        dec     dword [ESSREG_FMQ_Streams]
        push    eax                             ; flags
        push    esi
        VxDCall _HeapFree
        add     esp,byte 8
.done:  popad
        ret

; the program's writes don't follow its ticks, EBX = VM, EDI = ADI: what's
; queued goes to the chip, and the program plays directly until it ends
ESSREG_FMQ_Direct:
        push    eax
        mov     al,1
        call    ESSREG_FMQ_End
        mov     [edi+EX_FMQ_Direct],ebx
        call    FM_Disable_Local_Trapping
        pop     eax
        ret

; the queue of VM EBX's program ends on every device, and so does its
; direct play (its program or its VM ended); EBX = 0: every queue. AL = 1
; sends what's queued to the chip, 0 drops it
ESSREG_FMQ_End_VM:
        pushad
        mov     dl,al
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        or      ebx,ebx
        jz      .end
        cmp     [edi+EX_FMQ_Direct],ebx
        jne     .queue
        mov     dword [edi+EX_FMQ_Direct],0
.queue: cmp     [edi+EX_FMQ_VM],ebx
        jne     .skip
.end:   push    eax
        mov     al,dl
        call    ESSREG_FMQ_End
        pop     eax
        or      ebx,ebx
        jnz     .skip
        mov     dword [edi+EX_FMQ_Direct],0
.skip:  pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
        ret

; Hook_V86_Int_Chain for interrupt 8, EBX = VM: a tick of a program whose
; FM plays from a queue. CF set passes the interrupt on
ESSREG_FMQ_Int8:
        cmp     dword [ESSREG_FMQ_Streams],byte 0
        je      .out
        pushad
        mov     esi,[ADI_List]
        or      esi,esi
        jz      .done
        pushfd
        cli
        VxDCall List_Get_First
        popfd
        or      eax,eax
        jz      .done
.next:  mov     edi,[eax]
        cmp     [edi+EX_FMQ_VM],ebx
        jne     .skip
        call    ESSREG_FMQ_Tick
.skip:  pushfd
        cli
        VxDCall List_Get_Next
        popfd
        or      eax,eax
        jnz     .next
.done:  popad
.out:   stc
        ret

; a tick of the program, EDI = ADI. Until the clock locks, it collects 16
; intervals; then each tick moves the clock on by a tick, kept between the
; real time less the delay and the real time. After each window of 32
; ticks, the least lag of the window, that of a tick on time, pulls the
; clock to the ticks over the next window, and the baseline since the lock
; gives the tick. A window held back or ahead most of the time means that
; the program changed its rate, and the clock locks again
ESSREG_FMQ_Tick:
        pushad
        mov     esi,[edi+EX_FMQ_Blk]
        call    ESSREG_Clock
        mov     ecx,eax                         ; ECX = now
        mov     edx,eax
        xchg    edx,[esi+FMQ_Last]
        neg     edx
        add     edx,ecx                         ; EDX = the interval
        test    byte [esi+FMQ_Flags],FMQF_LAST
        jnz     .ival
        or      byte [esi+FMQ_Flags],FMQF_LAST
        jmp     .pass
.ival:  movzx   eax,byte [esi+FMQ_IPos]
        mov     [esi+FMQ_Ivals+eax*4],edx
        inc     eax
        and     al,15
        mov     [esi+FMQ_IPos],al
        test    byte [esi+FMQ_Flags],FMQF_LOCKED
        jnz     .locked
        inc     byte [esi+FMQ_NIval]
        cmp     byte [esi+FMQ_NIval],16
        jb      .pass
        call    ESSREG_FMQ_Lock
        jmp     .done
.pass:  mov     [esi+FMQ_V],ecx                 ; the real time, until it locks
        jmp     .done
.locked:
        inc     dword [esi+FMQ_Ticks]
        movzx   eax,byte [esi+FMQ_Frac]
        add     eax,[esi+FMQ_T]
        mov     [esi+FMQ_Frac],al
        shr     eax,8
        add     eax,[esi+FMQ_Slew]
        add     eax,[esi+FMQ_V]                 ; EAX = the clock's next tick
        mov     edx,ecx
        sub     edx,eax                         ; EDX = how far it lags
        jns     .behind
        mov     eax,ecx                         ; not ahead of the real time
        xor     edx,edx
        inc     byte [esi+FMQ_High]
        jmp     .set
.behind:
        cmp     edx,[ESSREG_FMQ_Delay]
        jbe     .set
        mov     edx,[ESSREG_FMQ_Delay]          ; nor behind it by the delay
        mov     eax,ecx
        sub     eax,edx
        inc     byte [esi+FMQ_Low]
.set:   mov     [esi+FMQ_V],eax
        cmp     edx,[esi+FMQ_Min]
        jae     .win
        mov     [esi+FMQ_Min],edx
.win:   inc     byte [esi+FMQ_Win]
        cmp     byte [esi+FMQ_Win],FMQ_WINDOW
        jb      .done
        cmp     byte [esi+FMQ_Low],FMQ_WINDOW / 2
        jae     .again
        cmp     byte [esi+FMQ_High],FMQ_WINDOW * 7 / 8
        jae     .again
        mov     eax,[esi+FMQ_Min]
        shr     eax,5                           ; over the next window
        mov     [esi+FMQ_Slew],eax
        mov     ebx,[esi+FMQ_Ticks]
        sub     ebx,[esi+FMQ_BaseK]
        cmp     ebx,FMQ_WINDOW * 2
        jb      .next
        mov     eax,[esi+FMQ_V]
        add     eax,[esi+FMQ_Min]
        sub     eax,[esi+FMQ_BaseV]             ; us over EBX ticks
        mov     edx,256
        mul     edx
        cmp     edx,ebx
        jae     .base                           ; it wouldn't divide
        div     ebx
        mov     [esi+FMQ_T],eax
        cmp     ebx,0x10000
        jb      .next
.base:  mov     eax,[esi+FMQ_V]                 ; a new baseline from here
        mov     [esi+FMQ_BaseV],eax
        mov     eax,[esi+FMQ_Ticks]
        mov     [esi+FMQ_BaseK],eax
.next:  xor     eax,eax
        mov     [esi+FMQ_Win],al
        mov     [esi+FMQ_Low],al
        mov     [esi+FMQ_High],al
        mov     eax,[ESSREG_FMQ_Delay]
        mov     [esi+FMQ_Min],eax
        jmp     .done
.again: and     byte [esi+FMQ_Flags],~FMQF_LOCKED
        mov     byte [esi+FMQ_NIval],0
        mov     [esi+FMQ_V],ecx
.done:  popad
        ret

; the clock locks, ESI = block, ECX = now: a tick is the 12th shortest of
; the last 16 intervals, the usual one between ticks on time, whatever
; bursts came among them
ESSREG_FMQ_Lock:
        pushad
        sub     esp,16 * 4
        xor     ebx,ebx
.sort:  mov     eax,[esi+FMQ_Ivals+ebx*4]
        mov     edx,ebx
.shift: or      edx,edx
        jz      .put
        cmp     [esp+edx*4-4],eax
        jbe     .put
        mov     edi,[esp+edx*4-4]
        mov     [esp+edx*4],edi
        dec     edx
        jmp     .shift
.put:   mov     [esp+edx*4],eax
        inc     ebx
        cmp     ebx,16
        jb      .sort
        mov     eax,[esp+11 * 4]
        add     esp,16 * 4
        shl     eax,8
        mov     [esi+FMQ_T],eax
        mov     [esi+FMQ_V],ecx
        mov     [esi+FMQ_BaseV],ecx
        xor     eax,eax
        mov     [esi+FMQ_Ticks],eax
        mov     [esi+FMQ_BaseK],eax
        mov     [esi+FMQ_Slew],eax
        mov     [esi+FMQ_Frac],al
        mov     [esi+FMQ_Win],al
        mov     [esi+FMQ_Low],al
        mov     [esi+FMQ_High],al
        mov     eax,[ESSREG_FMQ_Delay]
        mov     [esi+FMQ_Min],eax
        or      byte [esi+FMQ_Flags],FMQF_LOCKED
        popad
        ret

; interrupt 8's V86 hook, while DosFMDelay isn't 0
ESSREG_FMQ_Hook:
        test    byte [ESSREG_Opts+1],OPT_FM_TIMED >> 8
        jz      .out
        pushad
        mov     eax,8
        mov     esi,ESSREG_FMQ_Int8
        VxDCall Hook_V86_Int_Chain
        jc      .done
        mov     byte [ESSREG_FMQ_Hooked],1
.done:  popad
.out:   ret

; and out again, before the VxD goes
ESSREG_FMQ_Unhook:
        cmp     byte [ESSREG_FMQ_Hooked],0
        je      .out
        pushad
        mov     eax,8
        mov     esi,ESSREG_FMQ_Int8
        VxDCall Unhook_V86_Int_Chain
        mov     byte [ESSREG_FMQ_Hooked],0
        popad
.out:   ret

; --- settings ------------------------------------------------------------------

; in place of ESS's Sys_Dynamic_Device_Init: SYSTEM.INI first, and the
; hook of interrupt 8, out again if ESS's init fails and the VxD goes
ESSREG_Dynamic_Init:
        call    ESSREG_Read_Settings
        call    ESSREG_FMQ_Hook
        call    AUDDRV_Dynamic_Init
        jnc     .ok
        call    ESSREG_FMQ_Unhook
        stc
.ok:    ret

; ESSREG_Opts from SYSTEM.INI (ESSREG_Settings), once when the VxD starts.
; VMM reads SYSTEM.INI only while Windows starts: a VxD loaded later (a
; card found while Windows runs) keeps the defaults, and OPT_READ stays
; clear for essctl to show. A key is read as GetPrivateProfileInt reads
; it in the 16-bit drivers: its leading digits, 0 if none
ESSREG_Read_Settings:
        pushad
        VxDCall VMM_GetSystemInitState
        cmp     eax,SYSSTATE_VXDINITCOMPLETED
        jae     .api
        mov     ebx,ESSREG_Settings
.next:  mov     edi,[ebx]               ; the key
        or      edi,edi
        jz      .read
        mov     esi,[ebx+4]             ; its section
        xor     edx,edx
        VxDCall Get_Profile_String
        jc      .skip                   ; not there: the default
        call    ESSREG_Profile_Int
        cmp     dword [ebx],ESSREG_Key_Delay
        jne     .chan
        mov     ecx,eax                 ; DosFMDelay is the delay in ms too
        cmp     ecx,FMQ_DELAY_MAX
        jbe     .ms
        mov     ecx,FMQ_DELAY_MAX
.ms:    imul    ecx,ecx,1000
        mov     [ESSREG_FMQ_Delay],ecx
.chan:  cmp     dword [ebx],ESSREG_Key_S16DMA
        jne     .a2
        mov     ecx,eax                 ; SoundBlaster16DMA: 5, 6 or 7, or
        cmp     ecx,byte 5              ; none
        jb      .none
        cmp     ecx,byte 7
        jbe     .set
.none:  xor     ecx,ecx
.set:   mov     [ESSREG_S16_Chan],ecx
        jmp     .skip
.a2:    cmp     dword [ebx],ESSREG_Key_S16A2
        jne     .bit
        mov     [ESSREG_S16_Audio2],eax ; SoundBlaster16Audio2: 0 or not
        jmp     .skip
.bit:   mov     ecx,[ebx+8]             ; its bit
        or      eax,eax
        jz      .off
        or      [ESSREG_Opts],cx
        jmp     .skip
.off:   not     ecx
        and     [ESSREG_Opts],cx
.skip:  add     ebx,byte 12
        jmp     .next
.read:  or      word [ESSREG_Opts],OPT_READ
.api:   test    byte [ESSREG_Opts],OPT_API
        jnz     .done
        mov     dword [ESSREG_Group4_Count],0   ; CF set, as ESS's driver
.done:  popad
        ret

; EAX = the number at the start of the string at EDX, after blanks
ESSREG_Profile_Int:
        push    ecx
        push    edx
        xor     eax,eax
.blank: cmp     byte [edx],' '
        je      .skip
        cmp     byte [edx],9
        jne     .digit
.skip:  inc     edx
        jmp     .blank
.digit: movzx   ecx,byte [edx]
        sub     ecx,byte '0'
        cmp     ecx,byte 9
        ja      .done
        imul    eax,eax,byte 10
        add     eax,ecx
        inc     edx
        jmp     .digit
.done:  pop     edx
        pop     ecx
        ret

; --- DirectSound's primary buffer on a 16-bit DMA channel -------------------

; ESS's DirectSound driver (pcod.asm L5_2A30-L5_3220: the IDsDriver and
; IDsDriverBuffer of DirectX's dsdriver.h) gives DirectSound a buffer of
; system memory and plays it by DMA. On an 8-bit channel the DMA plays
; that buffer itself. On a 16-bit channel the chip takes the low byte of
; each word only, so the DMA plays a second buffer of twice the size, each
; byte of DirectSound's in the low byte of a word (L5_3309), which ESS's
; driver fills from a time-out: every 20 ms, 80 ms from where the DMA is
; (L5_3040). A late time-out then plays what the buffer held a lap ago.
; With DirectSoundLock=1 the driver asks DirectSound to bracket its writes
; with Lock and Unlock (DSDDESC_DONTNEEDPRIMARYLOCK clear), copies in
; Unlock what was written, once and at once, and starts without the
; time-out. It is off by default, until it has been checked on the card:
; a report of 8-bit playback came with it on. The vtables in pdat.asm
; point here.
DSDDESC_DONTNEEDPRIMARYLOCK equ 0x08
DSERR_GENERIC           equ 0x80004005
DSERR_INVALIDPARAM      equ 0x80070057
; ESS's buffer object
DSB_Playing             equ 0x08        ; dword: the DMA runs
DSB_Buffer              equ 0x10        ; dword: DirectSound's buffer
DSB_Size                equ 0x1C        ; dword: its size in bytes
DSB_Base                equ 0x2E        ; word: Audio_Base
DSB_Channel             equ 0x30        ; byte: the DMA channel, 5-7 16-bit
DSB_Words               equ 0x35        ; dword: the 16-bit channel's buffer

; IDsDriver::GetDriverDesc (this, description): ESS's, then
; DSDDESC_DONTNEEDPRIMARYLOCK cleared, for Lock and Unlock
ESSREG_DS_GetDriverDesc:
        push    dword [esp+8]
        push    dword [esp+8]
        call    L5_2AA0                 ; stdcall: takes its arguments
        test    byte [ESSREG_Opts+1],OPT_DS_LOCK >> 8
        jz      .done
        push    edx
        mov     edx,[esp+12]
        and     dword [edx],~DSDDESC_DONTNEEDPRIMARYLOCK
        pop     edx
.done:  ret     8

; IDsDriverBuffer::Lock (this, piece 1 and its length, piece 2 and its
; length, position, length, flags): the part of DirectSound's buffer from
; position, in two pieces where it wraps
ESSREG_DS_Lock:
        test    byte [ESSREG_Opts+1],OPT_DS_LOCK >> 8
        jz      L5_2EF0                 ; ESS's: not implemented
        push    ebx
        push    esi
        push    edi
        mov     edi,[esp+16]            ; this
        mov     esi,[edi+DSB_Size]
        mov     eax,[esp+36]            ; the position
        mov     ecx,[esp+40]            ; and the length
        test    byte [esp+44],0x02      ; DSBLOCK_ENTIREBUFFER
        jz      .given
        xor     eax,eax
        mov     ecx,esi
.given: cmp     eax,esi
        jb      .inside
        push    edx                     ; a position past the end wraps
        xor     edx,edx
        div     esi
        mov     eax,edx
        pop     edx
.inside:
        cmp     ecx,esi
        jbe     .fits
        mov     ecx,esi
.fits:  mov     ebx,esi
        sub     ebx,eax                 ; to the end of the buffer
        add     eax,[edi+DSB_Buffer]
        mov     edx,[esp+20]
        mov     [edx],eax               ; piece 1
        xor     eax,eax
        cmp     ecx,ebx
        jbe     .one
        sub     ecx,ebx
        xchg    ecx,ebx                 ; ECX = piece 1's length, EBX = 2's
        mov     eax,[edi+DSB_Buffer]    ; piece 2 starts the buffer
        jmp     .two
.one:   xor     ebx,ebx                 ; no piece 2
.two:   mov     edx,[esp+24]
        mov     [edx],ecx
        mov     edx,[esp+28]            ; which may not be asked for
        or      edx,edx
        jz      .len2
        mov     [edx],eax
.len2:  mov     edx,[esp+32]
        or      edx,edx
        jz      .ok
        mov     [edx],ebx
.ok:    xor     eax,eax
        pop     edi
        pop     esi
        pop     ebx
        ret     32

; IDsDriverBuffer::Unlock (this, piece 1 and its length, piece 2 and its
; length): what DirectSound wrote, into the 16-bit channel's buffer; an
; 8-bit channel plays DirectSound's buffer itself
ESSREG_DS_Unlock:
        test    byte [ESSREG_Opts+1],OPT_DS_LOCK >> 8
        jz      L5_2F00                 ; ESS's: not implemented
        push    edi
        mov     edi,[esp+8]             ; this
        cmp     byte [edi+DSB_Channel],3
        jbe     .ok
        push    dword [esp+16]          ; piece 1's length
        push    dword [esp+16]          ; and piece 1
        call    ESSREG_DS_Words
        jc      .param
        push    dword [esp+24]          ; piece 2's length
        push    dword [esp+24]          ; and piece 2
        call    ESSREG_DS_Words
        jc      .param
.ok:    xor     eax,eax
.out:   pop     edi
        ret     20
.param: mov     eax,DSERR_INVALIDPARAM
        jmp     .out

; a piece of DirectSound's buffer (address, length) into the 16-bit
; channel's buffer, each byte in the low byte of a word (L5_3309) at twice
; its offset; EDI = the buffer object; CF set for a piece that isn't in the
; buffer; keeps the registers
ESSREG_DS_Words:
        push    eax
        push    ecx
        push    edx
        mov     ecx,[esp+20]            ; the length
        jecxz   .ok
        mov     eax,[esp+16]            ; the address
        sub     eax,[edi+DSB_Buffer]    ; its offset
        jc      .bad
        mov     edx,eax
        add     edx,ecx
        jc      .bad
        cmp     edx,[edi+DSB_Size]
        ja      .bad
        add     eax,eax
        add     eax,[edi+DSB_Words]
        push    ecx
        push    eax
        push    dword [esp+24]          ; the address
        call    L5_3309                 ; (from, to, bytes)
        add     esp,byte 12
.ok:    clc
.out:   pop     edx
        pop     ecx
        pop     eax
        ret     8
.bad:   stc
        jmp     .out

; IDsDriverBuffer::Play (this, reserved, reserved, flags): on a 16-bit
; channel, the whole of DirectSound's buffer into the channel's first,
; which Unlock keeps up to date from here on, and none of ESS's time-out;
; then ESS's start (L5_3178), which has EDI = the buffer and SI = Audio_Base
ESSREG_DS_Play:
        test    byte [ESSREG_Opts+1],OPT_DS_LOCK >> 8
        jz      L5_30D0                 ; ESS's
        push    esi
        push    edi
        mov     edi,[esp+12]            ; this
        mov     eax,DSERR_GENERIC
        cmp     dword [edi+DSB_Playing],0
        jnz     .out                    ; playing already
        cmp     byte [edi+DSB_Channel],3
        jbe     .start
        push    dword [edi+DSB_Size]
        push    dword [edi+DSB_Words]
        push    dword [edi+DSB_Buffer]
        call    L5_3309
        add     esp,byte 12
.start: xor     esi,esi
        mov     si,[edi+DSB_Base]
        jmp     L5_3178
.out:   pop     edi
        pop     esi
        ret     16

; --- the DRQ latch -------------------------------------------------------------

; each time a VM takes the DSP (DrqLatch=1): card-level PnP register 25h
; bit 7, the DRQ latch, which holds each DMA request until the controller
; answers it (docs/AUDIO_PIPELINE.md). The chip loads 25h from its PnP
; data at a reset, and the data sheet has it read-only, so the write may
; not take; essctl's Plug and Play page shows whether it did. EDI = the
; ADI; keeps the registers
ESSREG_DRQ_Latch:
        test    byte [ESSREG_Opts+1],OPT_DRQ_LATCH >> 8
        jz      .done
        pushad
        pushfd
        cli
        movzx   ecx,word [edi+ADI_AudioBase]
        lea     edx,[ecx+4]
        in      al,dx                   ; the mixer index: ESS's search
        mov     bl,al                   ; may read the identification
        push    ecx                     ; sequence (40h)
        call    Find_Config_Port        ; EDX = the port or -1
        pop     ecx
        push    edx
        lea     edx,[ecx+4]
        mov     al,bl
        out     dx,al                   ; the index back
        pop     edx
        cmp     edx,0x100
        jb      .skip
        cmp     edx,0xFF8
        ja      .skip
        test    dl,7
        jnz     .skip
        call    Read_LDN1_Base          ; AX = logical device 1's base
        cmp     ax,cx
        jne     .skip                   ; not this card's port
        mov     esi,0x25FF              ; card level, register 25h
        call    ESSREG_PnP_Select
        inc     edx
        in      al,dx
        or      al,0x80
        out     dx,al
        dec     edx
        call    ESSREG_PnP_Restore
.skip:  popfd
        popad
.done:  ret

; --- a microsecond clock for the virtual timers --------------------------------

; EAX = microseconds (wraps after 71 minutes)
; the time stamp counter, once it's calibrated against the system time
; about a second after the first call; until then, and without a TSC, the
; system time plus 2 us per virtual FM access (programs wait for a timer
; by reading the status port)
ESSREG_Clock:
        push    ecx
        push    edx
        cmp     byte [ESSREG_TSC_State],TSC_READY
        je      .tsc
        call    ESSREG_Calibrate
        cmp     byte [ESSREG_TSC_State],TSC_READY
        je      .tsc
        VxDCall Get_System_Time
        mov     ecx,1000
        mul     ecx
        mov     ecx,[ESSREG_FM_Accesses]
        lea     eax,[eax+ecx*2]
        jmp     .out
.tsc:   call    ESSREG_TSC
        sub     eax,[ESSREG_TSC_Base]
        sbb     edx,[ESSREG_TSC_Base+4]
        cmp     edx,[ESSREG_TSC_MHz]
        jae     .rebase                 ; an hour or more since the base
        div     dword [ESSREG_TSC_MHz]
        cmp     eax,0x40000000
        jb      .add
        ; move the base on, so the division can't overflow
        push    eax
        mul     dword [ESSREG_TSC_MHz]
        add     [ESSREG_TSC_Base],eax
        adc     [ESSREG_TSC_Base+4],edx
        pop     eax
        add     [ESSREG_TSC_BaseUs],eax
        xor     eax,eax
.add:   add     eax,[ESSREG_TSC_BaseUs]
        mov     [ESSREG_TSC_Last],eax
        jmp     .out
.rebase:
        call    ESSREG_TSC
        mov     [ESSREG_TSC_Base],eax
        mov     [ESSREG_TSC_Base+4],edx
        mov     eax,[ESSREG_TSC_Last]
        add     eax,0x40000000
        mov     [ESSREG_TSC_BaseUs],eax
        mov     [ESSREG_TSC_Last],eax
.out:   pop     edx
        pop     ecx
        ret

; EDX:EAX = time stamp counter
ESSREG_TSC:
ESSREG_Rdtsc:
        rdtsc
        ret

; look for a time stamp counter, then calibrate it: its rate over at least
; a second of system time, in counts per microsecond
ESSREG_Calibrate:
        pushad
        mov     al,[ESSREG_TSC_State]
        cmp     al,TSC_UNKNOWN
        jne     .wait
        mov     byte [ESSREG_TSC_State],TSC_NONE
        pushfd                          ; CPUID if bit 21 of EFLAGS toggles
        pop     eax
        mov     ecx,eax
        xor     eax,0x200000
        push    eax
        popfd
        pushfd
        pop     eax
        push    ecx
        popfd
        xor     eax,ecx
        test    eax,0x200000
        jz      .done
        mov     eax,1
        cpuid
        test    edx,0x10                ; TSC
        jz      .done
        mov     byte [ESSREG_TSC_State],TSC_WAIT
.start: call    ESSREG_TSC
        mov     [ESSREG_TSC_Base],eax
        mov     [ESSREG_TSC_Base+4],edx
        VxDCall Get_System_Time
        mov     [ESSREG_TSC_BaseMs],eax
        jmp     .done
.wait:  cmp     al,TSC_WAIT
        jne     .done
        VxDCall Get_System_Time
        mov     ecx,eax
        sub     ecx,[ESSREG_TSC_BaseMs]
        cmp     ecx,1000
        jb      .done
        cmp     ecx,20000
        ja      .start                  ; too long ago: start again
        mov     ebx,eax                 ; now (ms)
        imul    ecx,ecx,1000            ; elapsed (us)
        call    ESSREG_TSC
        sub     eax,[ESSREG_TSC_Base]
        sbb     edx,[ESSREG_TSC_Base+4]
        cmp     edx,ecx
        jae     .start                  ; faster than 4 GHz: not believed
        div     ecx
        or      eax,eax
        jnz     .rate
        inc     eax
.rate:  mov     [ESSREG_TSC_MHz],eax
        call    ESSREG_TSC
        mov     [ESSREG_TSC_Base],eax
        mov     [ESSREG_TSC_Base+4],edx
        mov     eax,1000
        mul     ebx
        mov     ecx,[ESSREG_FM_Accesses]
        lea     eax,[eax+ecx*2]
        mov     [ESSREG_TSC_BaseUs],eax
        mov     byte [ESSREG_TSC_State],TSC_READY
.done:  popad
        ret

ESSREG_FM_Accesses:     dd 0            ; virtual FM accesses so far
ESSREG_TSC_Base:        dd 0, 0         ; TSC at ESSREG_TSC_BaseUs
ESSREG_TSC_BaseUs:      dd 0
ESSREG_TSC_BaseMs:      dd 0            ; system time when calibration began
ESSREG_TSC_MHz:         dd 0            ; TSC counts per us
ESSREG_TSC_Last:        dd 0            ; the clock's last value
ESSREG_TSC_State:       db TSC_UNKNOWN
ESSREG_Opts:            dw OPT_DEFAULT  ; OPT_*: the changes that are on
ESSREG_FMQ_Delay:       dd FMQ_DELAY * 1000     ; DosFMDelay (us)
ESSREG_FMQ_Streams:     dd 0            ; FM queues that play
ESSREG_FMQ_Hooked:      db 0            ; interrupt 8's V86 hook is in
ESSREG_S16_Chan:        dd S16_CHAN     ; SoundBlaster16DMA: the Sound Blaster
                                        ; 16's 16-bit channel, 0 for none
ESSREG_S16_Virt:        dd 0            ; its VDMAD handle, once taken (not
                                        ; Audio 2's channel)
ESSREG_S16_Audio2:      dd 1            ; SoundBlaster16Audio2: the 16-bit
                                        ; playback on Audio 2, 0 for Audio 1

section PDAT

ESSREG_Group4_Funcs:
        dd ESSREG_API_Info              ; 0400
        dd ESSREG_API_MixerRead         ; 0401
        dd ESSREG_API_MixerWrite        ; 0402
        dd ESSREG_API_CtrlRead          ; 0403
        dd ESSREG_API_CtrlWrite         ; 0404
        dd ESSREG_API_PortRead          ; 0405
        dd ESSREG_API_PortWrite         ; 0406
        dd ESSREG_API_CfgRead           ; 0407
        dd ESSREG_API_CfgWrite          ; 0408
        dd ESSREG_API_PnPRead           ; 0409
        dd ESSREG_API_PnPWrite          ; 040A
        dd ESSREG_API_MixerBlock        ; 040B
        dd ESSREG_API_Owners            ; 040C
        dd ESSREG_API_TakeDSP           ; 040D
ESSREG_FUNC_COUNT equ ($ - ESSREG_Group4_Funcs) / 4

; ESS's groups 0, 1 and 3 with the acquire functions wrapped
ESSREG_Group0_Funcs:
        dd API_x000_GetVersion          ; 0000
        dd API_0001_GetInfo             ; 0001
        dd ESSREG_API_0002              ; 0002
        dd ESSREG_API_0003              ; 0003
        dd API_0004_GetDMACount         ; 0004
        dd API_0005_GPO                 ; 0005
        dd API_0006_SetHwVolCallback    ; 0006
        dd API_0007_SetupPIOBuffer      ; 0007
        dd API_0008_GetConfigPort       ; 0008
        dd API_0009_SetCallback2        ; 0009
        dd API_000A_GetGlobalFlag       ; 000A
        dd API_000B_SetCallback3        ; 000B
ESSREG_Group1_Funcs:
        dd API_x000_GetVersion          ; 0100
        dd API_0101_FM_GetInfo          ; 0101
        dd ESSREG_API_0102              ; 0102
        dd ESSREG_API_0103              ; 0103
ESSREG_Group3_Funcs:
        dd API_x000_GetVersion          ; 0300
        dd API_0301_MPU_GetInfo         ; 0301
        dd ESSREG_API_0302              ; 0302
        dd API_0303_MPU_Release         ; 0303

; replaces API_Group_Table (see AUDDRV_API_Proc in pcod.asm)
ESSREG_Group_Table:
        dd 12, ESSREG_Group0_Funcs
        dd 4, ESSREG_Group1_Funcs
        dd 2, API_Group2_Funcs
        dd 4, ESSREG_Group3_Funcs
ESSREG_Group4_Count:                    ; 0 with RegisterAPI=0
        dd ESSREG_FUNC_COUNT, ESSREG_Group4_Funcs
ESSREG_API_GROUPS equ ($ - ESSREG_Group_Table) / 8

; SYSTEM.INI keys: the key, its section, its ESSREG_Opts bit
ESSREG_Settings:
        dd ESSREG_Key_API, ESSREG_Sec_VxD, OPT_API
        dd ESSREG_Key_VFM, ESSREG_Sec_VxD, OPT_VFM
        dd ESSREG_Key_Takes, ESSREG_Sec_VxD, OPT_TAKES_FM
        dd ESSREG_Key_Keeps, ESSREG_Sec_VxD, OPT_KEEPS_FM
        dd ESSREG_Key_Audible, ESSREG_Sec_VxD, OPT_FM_AUDIBLE
        dd ESSREG_Key_Mixer, ESSREG_Sec_VxD, OPT_DOS_MIXER
        dd ESSREG_Key_Reset, ESSREG_Sec_VxD, OPT_RESET_FM
        dd ESSREG_Key_Record, ESSREG_Sec_VxD, OPT_REC_TAKES
        dd ESSREG_Key_Delay, ESSREG_Sec_VxD, OPT_FM_TIMED
        ; the Audio 2 mode, the same for DirectSound as for ES1869.DRV
        dd ESSREG_Key_4X, ESSREG_Sec_Drv, OPT_A2_4X
        dd ESSREG_Key_Filter, ESSREG_Sec_Drv, OPT_A2_FILTER
        ; and the Audio 1 DAC's filter, for DOS programs here
        dd ESSREG_Key_A1Filter, ESSREG_Sec_Drv, OPT_A1_FILTER
        dd ESSREG_Key_DSLock, ESSREG_Sec_VxD, OPT_DS_LOCK
        dd ESSREG_Key_Latch, ESSREG_Sec_VxD, OPT_DRQ_LATCH
        dd ESSREG_Key_SB16, ESSREG_Sec_VxD, OPT_SB16
        dd ESSREG_Key_S16DMA, ESSREG_Sec_VxD, 0 ; the channel, no bit
        dd ESSREG_Key_S16A2, ESSREG_Sec_VxD, 0  ; no bit either
        dd 0
ESSREG_Sec_VxD:         db "ES1869.VXD", 0
ESSREG_Sec_Drv:         db "ES1869.DRV", 0
ESSREG_Key_API:         db "RegisterAPI", 0
ESSREG_Key_VFM:         db "VirtualFM", 0
ESSREG_Key_Takes:       db "DosTakesFM", 0
ESSREG_Key_Keeps:       db "DosKeepsFM", 0
ESSREG_Key_Audible:     db "DosFMAudible", 0
ESSREG_Key_Mixer:       db "DosMixerRestore", 0
ESSREG_Key_Reset:       db "ResetDosFM", 0
ESSREG_Key_Record:      db "RecordTakesDSP", 0
ESSREG_Key_Delay:       db "DosFMDelay", 0
ESSREG_Key_4X:          db "Audio2Oversampling", 0
ESSREG_Key_Filter:      db "Audio2Filter", 0
ESSREG_Key_A1Filter:    db "Audio1Filter", 0
ESSREG_Key_DSLock:      db "DirectSoundLock", 0
ESSREG_Key_Latch:       db "DrqLatch", 0
ESSREG_Key_SB16:        db "SoundBlaster16", 0
ESSREG_Key_S16DMA:      db "SoundBlaster16DMA", 0
ESSREG_Key_S16A2:       db "SoundBlaster16Audio2", 0
