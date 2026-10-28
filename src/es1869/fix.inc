; Constants of the changes to ES1869.DRV, assembled when ES1869_FIX=1.
;
; (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
;
; Licensed under GPL Version 3.0

%ifndef FIX_INC
%define FIX_INC

; es_opts: the changes that are on, from SYSTEM.INI [ES1869.DRV]
; (settings.asm); the Audio 2 bits are those of ES1869.VXD's ESSREG_Opts
OPT_A1_DEVICE   equ 0x0001      ; Audio1Device: a second wave-out device
OPT_A1_SHARED   equ 0x0002      ; SharedWaveOut: device 0 falls back to it
OPT_A1_FILTER   equ 0x0004      ; Audio1Filter: its filter in use
OPT_DUAL        equ 0x0008      ; DualPlayback: 4 channels on both DACs
OPT_FM_RECORD   equ 0x0010      ; FMRecordDevice: the FM recorded digitally
OPT_A2_4X       equ 0x0100      ; Audio2Oversampling: ESS's 4x
OPT_A2_FILTER   equ 0x0200      ; Audio2Filter: the filter in use
OPT_READ        equ 0x8000      ; SYSTEM.INI was read
OPT_DEFAULT     equ OPT_A1_DEVICE | OPT_A1_SHARED | OPT_DUAL | OPT_FM_RECORD

; mixer 71h, the Audio 2 mode
A2_4X           equ 0x10        ; 4x oversampling (the filter bypassed too)
A2_BYPASS       equ 0x08        ; the switched-capacitor filter bypassed
A1_BYPASS       equ 0x04        ; the Audio 1 CODEC's filter bypassed
A2_ASYNC        equ 0x02        ; Audio 2 at its own rate

; ESS's device structure (LocalAlloc'd at 3:47E1, 132h bytes)
DEV_BASE        equ 0x00        ; Audio_Base
DEV_DMA1        equ 0x06        ; the Audio 1 DMA channel
DEV_BUFSIZE     equ 0x0E        ; the Audio 1 DMA buffer's size
DEV_DEVNODE     equ 0x14        ; dword
DEV_ENABLED     equ 0x1C
DEV_DEAD        equ 0x20        ; the hardware stopped answering
DEV_FLAGS       equ 0x2A        ; word; +2Bh bit 7 demand transfers, bit 3 no DMA
DEV_DSPCOUNT    equ 0x2E        ; holders of the DSP (vxd_acquire)
DEV_DMA_ADDR    equ 0x2F        ; the 8237's ports and values for Audio 1
DEV_DMA_COUNT   equ 0x30
DEV_DMA_MASK    equ 0x31
DEV_DMA_MODE    equ 0x32
DEV_DMA_FF      equ 0x33
DEV_DMA_PAGE    equ 0x34
DEV_DMA_ON      equ 0x35        ; the channel's bits: unmask
DEV_DMA_OFF     equ 0x36        ; and mask
DEV_DMA_PLAY    equ 0x38        ; 58h + channel: single, auto-initialize, read
DEV_BLOCK       equ 0x3D        ; bytes per interrupt, from rate_setup
DEV_BUF_OFF     equ 0x41        ; the Audio 1 DMA buffer: offset,
DEV_BUF_SEL     equ 0x43        ; selector,
DEV_BUF_PHYS    equ 0x45        ; physical address, low word
DEV_BUF_PAGE    equ 0x47        ; and page
DEV_A1_USER     equ 0x5D        ; the Audio 1 user: 2 wave-in, 1 the player here
DEV_WID_FLAGS   equ 0x6F        ; wave-in: bit 0 open
DEV_WOD_FLAGS   equ 0x70        ; wave-out: bit 0 open, bit 2 suspended
DEV_VOICE_SUSP  equ 0x76        ; a voice recording held for a playback
DEV_VOICE       equ 0x78        ; dword: its instance
DEV_A2_DMA      equ 0xD4        ; the Audio 2 DMA channel
DEV_A2_BUFSIZE  equ 0xD9        ; its buffer's size
DEV_A2_ADDR     equ 0xE3        ; its 8237 ports and values, as Audio 1's
DEV_A2_COUNT    equ 0xE4
DEV_A2_MASK     equ 0xE5
DEV_A2_MODE     equ 0xE6
DEV_A2_FF       equ 0xE7
DEV_A2_PAGE     equ 0xE8
DEV_A2_ON       equ 0xE9
DEV_A2_OFF      equ 0xEA
DEV_A2_PLAY     equ 0xEC
DEV_A2_BUF_OFF  equ 0xF5        ; the Audio 2 DMA buffer: offset,
DEV_A2_BUF_SEL  equ 0xF7        ; selector,
DEV_A2_PHYS     equ 0xF9        ; physical address, low word
DEV_A2_PHYS_PG  equ 0xFB        ; and page
DEV_A2_USER     equ 0x100       ; the Audio 2 user: 1 wave-out, 2 the player
DEV_FM_DAC      equ 0x117       ; 1 while FM or an ES689 has the music DAC
DEV_WAVE_VOL    equ 0x124       ; the wave volume, 7Ch
DEV_BUSY        equ 0x127       ; bit 0: the VxD gave the chip away
DEV_SIZE        equ 0x132

DEMAND_HI       equ 0x80        ; +2Bh: demand transfers
NODMA_HI        equ 0x08        ; +2Bh: no-DMA emulation

; the Audio 1 player (a1play.asm, a1wave.asm), after ESS's fields; the
; first fields are those of ESS's wave instance that callback reads
A1_BASE         equ DEV_SIZE
A1_SELF         equ A1_BASE + 0x00      ; this block's address while open
A1_CB           equ A1_BASE + 0x02      ; dword: dwCallback
A1_CBINST       equ A1_BASE + 0x06      ; dword: dwInstance
A1_HWAVE        equ A1_BASE + 0x0A      ; the handle
A1_OFLAGS       equ A1_BASE + 0x0C      ; dword: the open's flags; the
A1_CBTYPE       equ A1_BASE + 0x0E      ;   high word is the callback type
A1_ID           equ A1_BASE + 0x10      ; A1_MAGIC while open
A1_STATE        equ A1_BASE + 0x12      ; A1F_*
A1_FMT          equ A1_BASE + 0x13      ; FMT_16BIT, FMT_STEREO
A1_ALIGN        equ A1_BASE + 0x14      ; bytes per sample frame
A1_SILENCE      equ A1_BASE + 0x16      ; the byte of silence
A1_RATE         equ A1_BASE + 0x18      ; samples per second
A1_BLOCK        equ A1_BASE + 0x1A      ; bytes per interrupt
A1_RING         equ A1_BASE + 0x1C      ; bytes of the DMA ring, 2 blocks
A1_GUARD        equ A1_BASE + 0x1E      ; the least lead on the DMA
A1_HEAD         equ A1_BASE + 0x20      ; far: the oldest header queued
A1_TAIL         equ A1_BASE + 0x24      ; far: the newest
A1_CUR          equ A1_BASE + 0x28      ; far: the one being copied, or 0
A1_CURPTR       equ A1_BASE + 0x2C      ; huge: where in its data
A1_CURLEFT      equ A1_BASE + 0x30      ; dword: bytes of it left to copy
A1_LOOPHDR      equ A1_BASE + 0x34      ; far: the loop's first header, or 0
A1_LOOPS        equ A1_BASE + 0x38      ; dword: passes left, this one too
A1_PPOS         equ A1_BASE + 0x3C      ; dword: bytes the DMA took
A1_WPOS         equ A1_BASE + 0x40      ; dword: bytes written, gaps too
A1_DPOS         equ A1_BASE + 0x44      ; dword: bytes of the program's data
A1_GAPEND       equ A1_BASE + 0x48      ; dword: WPOS after the last gap
A1_PRING        equ A1_BASE + 0x4C      ; PPOS in the ring
A1_WRING        equ A1_BASE + 0x4E      ; WPOS in the ring
A1_LOOPW        equ A1_BASE + 0x50      ; WPOS (low word) at the pass's start
A1_SPLITN       equ A1_BASE + 0x52      ; bytes a1_split puts in each ring
A1_SIZEOF       equ 0x54

; the FM recording device (fmwave.asm, fmdac.asm), after the player's
; block
FM_BASE         equ A1_BASE + A1_SIZEOF
FM_STATE        equ FM_BASE + 0x00      ; FMF_*
FM_SAVED        equ FM_BASE + 0x01      ; FMS_*: what it changed, as it was
FM_FORMAT       equ FM_BASE + 0x02      ; the format ESS's open gets
FM_DESC         equ FM_BASE + 0x12      ; the WAVEOPENDESC ESS's open gets
FM_SIZEOF       equ 0x26
DEV_FIX_SIZE    equ DEV_SIZE + A1_SIZEOF + FM_SIZEOF

; FM_STATE
FMF_OPEN        equ 0x01        ; device 1 has ESS's wave-in
FMF_ROUTED      equ 0x02        ; 7Fh sends the FM to Audio 1's DMA

; FM_SAVED
FMS_DCDRIFT     equ 0x01        ; DEV_FLAGS bit 0, ESS's DC drift removal
FMS_I2S         equ 0x02        ; 7Fh bit 0, as ESS's code last left it
FMS_DREC        equ 0x04        ; 7Fh bit 4 was set

FM_RATE         equ 49716       ; the music DAC's rate, the recording's
FM_OPEN_RATE    equ 48000       ; what ESS's open is given for it

A1_MAGIC        equ 0xA1A1      ; the high word of the instance (dwUser)

; A1_STATE
A1F_OPEN        equ 0x01
A1F_RUN         equ 0x02        ; the DMA runs
A1F_PAUSED      equ 0x04
A1F_SHARED      equ 0x08        ; opened as device 0 (SharedWaveOut)

; A1_FMT: the format word of rate_setup (6:1FF4), for each DAC
FMT_16BIT       equ 0x01
FMT_STEREO      equ 0x02
FMT_DUAL        equ 0x80        ; 4 channels: 1-2 Audio 1, 3-4 Audio 2
A1_SPLIT        equ 512         ; ring bytes split at a time (a1_bounce)

; the Windows wave API (MMSYSTEM.H, MMDDK.H)
WODM_GETNUMDEVS equ 3
WODM_GETDEVCAPS equ 4
WODM_OPEN       equ 5
WODM_CLOSE      equ 6
WODM_WRITE      equ 9
WODM_PAUSE      equ 10
WODM_RESTART    equ 11
WODM_RESET      equ 12
WODM_GETPOS     equ 13
WODM_GETVOLUME  equ 16
WODM_SETVOLUME  equ 17
WODM_BREAKLOOP  equ 20
WIDM_GETNUMDEVS equ 50
WIDM_GETDEVCAPS equ 51
WIDM_OPEN       equ 52
WIDM_CLOSE      equ 53
WIDM_START      equ 57
WIDM_STOP       equ 58
WIDM_RESET      equ 59
WOM_OPEN        equ 0x3BB
WOM_CLOSE       equ 0x3BC
MMSYSERR_BADDEVICEID    equ 2
MMSYSERR_NOTENABLED     equ 3
MMSYSERR_ALLOCATED      equ 4
MMSYSERR_NOTSUPPORTED   equ 8
MMSYSERR_INVALPARAM     equ 11
WAVERR_BADFORMAT        equ 32
WAVERR_STILLPLAYING     equ 33
WAVERR_UNPREPARED       equ 34
WAVE_FORMAT_QUERY       equ 0x0001
WAVE_FORMAT_PCM         equ 1
TIME_MS                 equ 0x0001
TIME_SAMPLES            equ 0x0002
TIME_BYTES              equ 0x0004

; WAVEHDR
WH_DATA         equ 0x00        ; far
WH_LENGTH       equ 0x04        ; dword
WH_FLAGS        equ 0x10        ; dword
WH_LOOPS        equ 0x14        ; dword
WH_NEXT         equ 0x18        ; far
WH_OWNER        equ 0x1C        ; reserved: ESS's instance, here the block
WH_END          equ 0x1E        ; and here WPOS (low word) after its data
WHDR_DONE       equ 0x01
WHDR_PREPARED   equ 0x02
WHDR_BEGINLOOP  equ 0x04
WHDR_ENDLOOP    equ 0x08
WHDR_INQUEUE    equ 0x10

; WAVEOPENDESC
WOD_HWAVE       equ 0x00
WOD_FORMAT      equ 0x02        ; far: the PCMWAVEFORMAT
WOD_CALLBACK    equ 0x06
WOD_INSTANCE    equ 0x0A
WOD_DEVNODE     equ 0x10
WOD_SIZE        equ 0x14

; PCMWAVEFORMAT
WF_TAG          equ 0x00
WF_CHANNELS     equ 0x02
WF_RATE         equ 0x04        ; dword
WF_AVG          equ 0x08        ; dword: bytes per second
WF_ALIGN        equ 0x0C
WF_BITS         equ 0x0E
WF_SIZE         equ 0x10

; WAVEINCAPS and WAVEOUTCAPS, in the MDEVICECAPSEX's pCaps
CAPS_NAME       equ 0x06        ; szPname, 32 bytes
CAPS_FORMATS    equ 0x26        ; dword
CAPS_CHANNELS   equ 0x2A

; controller registers (DSP commands A0h-BFh write them, C0h reads)
CR_RATE         equ 0xA1
CR_COUNT_LO     equ 0xA4
CR_COUNT_HI     equ 0xA5
CR_ANALOG       equ 0xA8        ; bits 1:0: 10 mono, 01 stereo
CR_IRQ          equ 0xB1
CR_DRQ          equ 0xB2
CR_DAC_INIT     equ 0xB6
CR_FIFO         equ 0xB7
CR_CONTROL      equ 0xB8        ; bit 3 ADC, bit 2 auto-initialize, bit 0 go
CR_XFER         equ 0xB9
DSP_READ_REG    equ 0xC0
DSP_EXTENDED    equ 0xC6
DSP_A1_ON       equ 0xD1        ; the Audio 1 DAC into the mixer
DSP_A1_OFF      equ 0xD3
MX_A1_VOLUME    equ 0x14
MX_A2_MODE      equ 0x71
MX_A2_COUNT_LO  equ 0x74
MX_A2_COUNT_HI  equ 0x76
MX_A2_CONTROL1  equ 0x78        ; bit 0 go, bit 1 DMA, bit 4 auto, 7:6 demand
MX_A2_CONTROL2  equ 0x7A        ; bit 0 16-bit, 1 stereo, 2 signed, 6 IRQ
MX_A2_VOLUME    equ 0x7C
MX_DREC         equ 0x7F        ; bit 4 the music DAC's samples to Audio
                                ; 1's DMA, bit 0 the music DAC to I2S

%endif
