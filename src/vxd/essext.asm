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
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

ESSREG_VERSION          equ 0x0110
ESSREG_FEATURES         equ 0x01FF      ; mixer, controller, ports, config,
                                        ; PnP, mixer block, owner info,
                                        ; DOS FM, DOS mixer restore

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

global ESSREG_Rdtsc, ESSREG_Snap_Regs
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
        mov     word [ebp+Client_EBX],ESSREG_FEATURES
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

; --- the Audio 2 DAC ---------------------------------------------------------

; in place of ESS's write of mixer 71h, AL = its value (bits 4 and 1: 4x
; oversampling, asynchronous), AH = 71h, EDX = Audio_Base: the DAC plays
; the samples as they are, not oversampled and the filter bypassed, as
; build/ES1869.DRV has it (docs/AUDIO_PIPELINE.md)
ESSREG_A2_Mode:
        and     al,~ESSREG_A2_4X & 0xFF
        or      al,ESSREG_A2_SCF_BYPASS
        jmp     L1_09A8

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
; the music DAC and an FM volume again.
ESSREG_API_0003:
        VxDCall Test_Sys_VM_Handle
        jne     .stock
        call    API_0003_Release
        pushfd
        call    ESSREG_After_Close
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
; left on stop, and the next DOS program starts from a clean chip)
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
.fm:    test    byte [edi+EX_Flags],EXF_DOS_FM
        jz      .skip
        cmp     dword [edi+ADI_FMOwner],byte 0
        jne     .skip                   ; still in use
        and     byte [edi+EX_Flags],~EXF_DOS_FM
        cmp     word [edi+ADI_FMBase],byte -1
        je      .skip
        push    eax
        push    edx
        movzx   edx,word [edi+ADI_AudioBase]
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

; in place of ESS's Save_DOS_Mixer when a VM (EBX) acquires the DSP:
; a DOS VM taking it from Windows first saves Windows' mixer (unless it's
; already saved and not put back yet)
ESSREG_DSP_Save:
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
; the caller's own chip goes to the hardware; a free chip is taken first;
; otherwise the access goes to the VM's virtual chip
ESSREG_FM_Trap:
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
.real:  pop     eax
        pop     edx
        pop     ecx
        jecxz   .in
        out     dx,al
        jmp     .ret
.in:    in      al,dx
.ret:   pop     esi
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
; Windows gets it softly (a DOS VM may take it); a DOS VM gets the music
; DAC and an FM volume
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
ESSREG_FM_Reset:
        and     byte [edi+EX_Flags],~EXF_FM_RESET
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
ESSREG_FM_Audible:
        push    eax
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
; the owner, EDI = ADI: puts back what ESSREG_FM_Audible changed, unless
; something else changed it since
ESSREG_FM_Released:
        and     byte [edi+EX_Flags],~EXF_FM_SOFT
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
        test    al,1
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

; a program ended in VM EBX (DOSMGR_End_V86_App): its virtual chips let
; go of the notes and timers it left, so they don't sound when the chip
; comes to the VM. In place of the jump to the next hook at the end of
; ESS's hook (L1_04B8), so ESS's code keeps its size: every register and
; flag goes on as it came
ESSREG_App_End:
        pushfd
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
; its virtual chip goes first
ESSREG_Node_Remove:
        call    ESSREG_VFM_Drop
        jmp     L4_00CB

; in place of ESS's VM_Not_Executeable handler: the dying VM's virtual
; chips go first
ESSREG_VM_Not_Executeable:
        call    ESSREG_VFM_Free
        jmp     AUDDRV_VM_Not_Executeable

; in place of ESS's Sys_Dynamic_Device_Exit: every VM's virtual chips go
; first
ESSREG_Dynamic_Exit:
        pushad
        VxDCall Get_Sys_VM_Handle
        mov     edx,ebx
.vm:    call    ESSREG_VFM_Free
        VxDCall Get_Next_VM_Handle
        cmp     ebx,edx
        jne     .vm
        popad
        jmp     AUDDRV_Dynamic_Exit

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
        dd ESSREG_FUNC_COUNT, ESSREG_Group4_Funcs
ESSREG_API_GROUPS equ ($ - ESSREG_Group_Table) / 8
