/*
 * vxdapi.h -- calls into the V86/PM API of ES1869.VXD (device AUDDRV,
 * ID 3B07h) from 16-bit Windows.  docs/VXD_API.md describes every function.
 *
 * Only functions without lasting side effects are wrapped, plus the DSP
 * acquire/release pair that brackets direct port access on the stock
 * driver (see vxd_dsp_begin).  The callback, notification and PIO buffer
 * functions (0006, 0007, 0009, 000B, 0200, 0201) are deliberately absent:
 * registering a callback from an application would leave the driver
 * calling into freed code after the application exits.
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#ifndef VXDAPI_H
#define VXDAPI_H

#include "esstypes.h"

#define VXD_AUDDRV_ID 0x3B07

#ifdef ESS_HOST
#define VXD_CALL
#else
#define VXD_CALL __far __cdecl
#endif

/* register image passed to the thunk; layout fixed by src/win/vxdcall.asm */
typedef struct {
  u32 eax, ebx, ecx, edx, esi, edi; /* offsets 0, 4, 8, 12, 16, 20 */
  u16 es;                           /* 24 */
  u16 flags;                        /* 26: FLAGS after the call */
#ifdef ESS_HOST
  void *host_buf; /* ES:BX / ES:DI for the test double */
#endif
} vxd_regs;

/* src/win/vxdcall.asm (a test double on the host) */
void ESS_FAR *VXD_CALL vxd_get_entry(u16 device_id);
int VXD_CALL vxd_raw_call(void ESS_FAR *entry, vxd_regs ESS_FAR *r);

/* offsets in the device instance (ADI) copied by function 0001 */
#define ADI_COPY_SIZE 0xE9 /* the most 0001 copies */
#define ADI_FLAGS 0x04
#define ADI_AUDIO_BASE 0x06
#define ADI_FM_BASE 0x08
#define ADI_FM_ALIAS 0x0A
#define ADI_MPU_BASE 0x0C
#define ADI_MPU_IRQ 0x0E
#define ADI_IRQ 0x0F
#define ADI_DMA1 0x10
#define ADI_FLAGS_HI 0x13
#define ADI_VERSION 0x17
#define ADI_DSP_OWNER 0x35
#define ADI_DSP_LAST 0x39
#define ADI_FM_OWNER 0x3D
#define ADI_FM_LAST 0x41
#define ADI_MPU_OWNER 0x45
#define ADI_MPU_LAST 0x49
#define ADI_DEVNODE 0x55
#define ADI_DMA2 0x79

#define ADI_F_NODMA 0x0020     /* no-DMA (PIO) emulation */
#define ADI_F_MPU_SHARED 0x2000 /* MPU-401 shares the audio IRQ */

/* owner classes reported by function 040C */
#define VXD_OWNER_NONE 0
#define VXD_OWNER_SELF 1  /* the caller's VM: Windows itself */
#define VXD_OWNER_OTHER 2 /* another VM: a DOS box */
#define VXD_OWNER_UNKNOWN 3 /* stock driver, Windows' VM handle not known yet */

struct vxd_owners {
  u8 dsp, fm, mpu; /* VXD_OWNER_* */
  u8 status;       /* Audio_Base+Ch */
  u16 adi_flags;
};

struct vxd_state {
  void ESS_FAR *entry; /* 0 when AUDDRV is not loaded */
  u32 devnode;
  u16 version;      /* function 0000 */
  u16 ext_version;  /* function 0400; 0 on the stock driver */
  u16 ext_features;
  u16 ext_count;    /* number of group 4 functions */
  u8 adi[ADI_COPY_SIZE];
  u8 adi_valid;
  u8 dsp_taken; /* vxd_dsp_begin acquired the DSP and must release it */
  u32 sys_vm;   /* Windows' VM handle, learnt from the first acquire */
};

extern struct vxd_state vxd;

/* find AUDDRV and read version, device info and extension info:
 * 0 on success, or -ESSHW_ENODEV */
int vxd_open(void);

/* raw call: DX = fn, ECX = devnode unless set by the caller (flag);
 * returns 0, or AX (at least 1) when the carry flag was set */
int vxd_call(u16 fn, vxd_regs *r);

/* 0001: copy the ADI into vxd.adi */
int vxd_read_adi(void);
u16 vxd_adi_word(unsigned offset);
u32 vxd_adi_dword(unsigned offset);

/* VXD_OWNER_* of an owner field of the ADI (stock driver) */
int vxd_owner_class(u32 owner);

int vxd_dma_count(int channel, u16 *count); /* 0004 (0 = audio 1) */
int vxd_gpo_read(u8 *bits);                /* 0005 with EAX != 0 */
int vxd_config_port(u16 *port);            /* 0008 */
int vxd_global_flag(u8 *flag);             /* 000A */
int vxd_fm_info(u16 *port);                /* 0101 */
int vxd_mpu_info(u16 *port, u8 *irq);      /* 0301 */

/* Stock driver: every port access from Windows makes the Windows VM own
 * the DSP (the driver's I/O trap acquires it), and a port access while a
 * DOS box owns it pops up "device in use" and reads FFh.  vxd_dsp_begin
 * acquires the DSP explicitly (0002) before direct port access and fails
 * with ESSHW_EINUSE instead; vxd_dsp_end releases it (0003) only if it was
 * free before, which resets the DSP exactly as closing a wave device does.
 * Neither may be interleaved with a yield to other programs. */
int vxd_dsp_begin(void);
void vxd_dsp_end(void);

/* esshw hook for the group 4 functions */
int vxd_ext_call(u16 fn, u8 bl, u8 bh, u8 al, u8 *result);
int vxd_ext_mixer_block(u8 ESS_FAR *buf); /* 040B: 128 bytes */
int vxd_ext_owners(struct vxd_owners *o); /* 040C */

#endif /* VXDAPI_H */
