; Adds the essreg register API to ES1869.VXD, as group 4 of the V86/PM API
; of the AUDDRV device (INT 2Fh AX=1684h BX=3B07h returns the entry point).
; It's only assembled when ESSREG_EXT=1.  The stock driver answers DH=4
; with CF set, which is how programs detect it.
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
;   0408  write config port    BL = offset 0-7, BH = value
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
; "in use" message shows up.  Controller registers are refused while
; another VM owns the DSP.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

ESSREG_VERSION          equ 0x0100
ESSREG_FEATURES         equ 0x007F      ; mixer, controller, ports, config,
                                        ; PnP, mixer block, owner info

ESSREG_E_NODEV          equ 1           ; ECX is not an ES1869 devnode
ESSREG_E_INUSE          equ 2           ; another VM owns the DSP
ESSREG_E_PARAM          equ 3           ; register/offset out of range, bad buffer
ESSREG_E_BUSY           equ 4           ; DSP write buffer stays busy
ESSREG_E_TIMEOUT        equ 5           ; DSP returned no data
ESSREG_E_NOCFG          equ 6           ; configuration port not found

ESSREG_POLL             equ 0x2000      ; DSP handshake poll limit

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
        or      eax,eax
        jz      .ok
        cmp     eax,ebx
        jz      .ok
        mov     ax,ESSREG_E_INUSE
        jmp     ESSREG_Fail
.ok:    clc
        ret

; write AL to the DSP, EDX = Audio_Base+Ch
; CF set if the write buffer stays busy, clobbers EAX and ECX
ESSREG_DSP_Write:
        mov     ah,al
        mov     ecx,ESSREG_POLL
.wait:  in      al,dx
        test    al,0x80
        jz      .ready
        loop    .wait
        stc
        ret
.ready: mov     al,ah
        out     dx,al
        clc
        ret

; wait for a DSP data byte and read it into AL, EDX = Audio_Base+Ch
; reading Audio_Base+Eh clears the interrupt request, so poll bit 6 of
; Audio_Base+Ch instead (it mirrors the read-buffer flag)
ESSREG_DSP_Read:
        mov     ecx,ESSREG_POLL
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

; EDX = configuration port of the ADI in EDI (cached per ADI)
; CF set and Client_AX = ESSREG_E_NOCFG if it can't be found
ESSREG_Get_Config_Port:
        cmp     edi,[ESSREG_Cfg_ADI]
        jne     .find
        mov     edx,[ESSREG_Cfg_Port]
        clc
        ret
.find:  movzx   edx,word [edi+ADI_AudioBase]
        push    edx
        call    Find_Config_Port
        add     esp,byte 4
        cmp     edx,byte -1
        je      .none
        mov     [ESSREG_Cfg_Port],edx
        mov     [ESSREG_Cfg_ADI],edi
        clc
        ret
.none:  mov     ax,ESSREG_E_NOCFG
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
        add     edx,byte 4              ; mixer index port
        mov     ah,[ebp+Client_EBX]
        pushfd
        cli
        in      al,dx
        mov     cl,al                   ; previous index
        mov     al,ah
        out     dx,al
        inc     edx
        in      al,dx
        mov     ch,al
        dec     edx
        mov     al,cl
        out     dx,al
        popfd
        movzx   eax,ch
        mov     [ebp+Client_EAX],ax
        clc
.done:  ret

; --- 0402: write mixer register -------------------------------------------
ESSREG_API_MixerWrite:
        call    ESSREG_Get_ADI
        jc      .done
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 4
        movzx   esi,word [ebp+Client_EBX]
        pushfd
        cli
        in      al,dx
        mov     cl,al
        mov     eax,esi
        out     dx,al                   ; register
        inc     edx
        mov     al,ah
        out     dx,al                   ; value
        dec     edx
        mov     al,cl
        out     dx,al
        popfd
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
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        pushfd
        cli
        mov     al,0xC6                 ; enable Extended mode commands
        call    ESSREG_DSP_Write
        jc      .busy
        mov     al,0xC0                 ; read controller register
        call    ESSREG_DSP_Write
        jc      .busy
        mov     eax,esi
        call    ESSREG_DSP_Write
        jc      .busy
        call    ESSREG_DSP_Read
        jc      .timeout
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
        popfd
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
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 0x0C
        pushfd
        cli
        mov     al,0xC6
        call    ESSREG_DSP_Write
        jc      .busy
        mov     eax,esi                 ; register (command Axh/Bxh)
        call    ESSREG_DSP_Write
        jc      .busy
        mov     eax,esi
        mov     al,ah                   ; value
        call    ESSREG_DSP_Write
        jc      .busy
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
        movzx   edx,word [edi+ADI_AudioBase]
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
        call    ESSREG_Get_Config_Port
        jc      .done
        movzx   eax,byte [ebp+Client_EBX]
        cmp     al,0x07
        ja      .param
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
        call    ESSREG_Get_Config_Port
        jc      .done
        movzx   eax,byte [ebp+Client_EBX]
        cmp     al,0x07
        ja      .param
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
        movzx   edx,word [edi+ADI_AudioBase]
        add     edx,byte 4
        push    edx
        mov     ax,(Client_ES << 8) | Client_EDI
        VxDCall Map_Flat
        pop     edx
        cmp     eax,byte -1
        je      .param
        mov     esi,eax
        xor     ecx,ecx
.next:  xor     eax,eax
        cmp     cl,0x40                 ; identification sequence, not read
        je      .store
        pushfd
        cli
        in      al,dx
        mov     ah,al                   ; previous index
        mov     al,cl
        out     dx,al
        inc     edx
        in      al,dx
        dec     edx
        xchg    al,ah
        out     dx,al
        popfd
        mov     al,ah
.store: mov     [esi+ecx],al
        inc     ecx
        cmp     ecx,0x80
        jb      .next
        clc
.done:  ret
.param: mov     ax,ESSREG_E_PARAM
        jmp     ESSREG_Fail

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

; configuration port cache
ESSREG_Cfg_ADI:         dd 0
ESSREG_Cfg_Port:        dd 0

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

; replaces API_Group_Table (see AUDDRV_API_Proc in pcod.asm)
ESSREG_Group_Table:
        dd 12, API_Group0_Funcs
        dd 4, API_Group1_Funcs
        dd 2, API_Group2_Funcs
        dd 4, API_Group3_Funcs
        dd ESSREG_FUNC_COUNT, ESSREG_Group4_Funcs
ESSREG_API_GROUPS equ ($ - ESSREG_Group_Table) / 8
