/*
 * essctl.h -- shared declarations of the essctl modules.
 *
 * essctl is a 16-bit Windows program (Windows 3.1 API, marked for Windows
 * 4.0 so that Windows 95 draws it in 3-D).  The main window is a modeless
 * dialog with a category list on the left and a page area on the right
 * whose controls are created at run time from the register catalog.
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#ifndef ESSCTL_H
#define ESSCTL_H

#include <windows.h>

#include "esscat.h"
#include "esstypes.h"

/* missing from the Windows 3.1 headers */
#ifndef DS_3DLOOK
#define DS_3DLOOK 0x0004L
#endif
#ifndef WS_EX_CLIENTEDGE
#define WS_EX_CLIENTEDGE 0x00000200L
#endif
#ifndef COLOR_3DFACE
#define COLOR_3DFACE COLOR_BTNFACE
#endif

#define ESSCTL_VERSION "1.0"

/* page_refresh: why */
#define REFRESH_TIMER 0 /* periodic: registers without side effects only */
#define REFRESH_USER 1  /* page shown, File > Refresh */
#define REFRESH_DSP 2   /* also the controller registers (DSP channel) */

/* kinds of pages */
#define PK_INFO 0   /* device information */
#define PK_FIELDS 1 /* catalog page (arg = enum ess_page) */
#define PK_RAW 2    /* raw registers */
#define PK_ESFM 3   /* ESFM patch bank */

extern HINSTANCE g_inst;
extern HWND g_main;   /* the main dialog */
extern HFONT g_font;  /* dialog font, for controls created at run time */
extern int g_expert;  /* Expert mode: session only */

/* essctl.c */
void msg_error(HWND owner, const char *fmt, ...);
int confirm(HWND owner, const char *text);
void app_dir_file(const char *name, char *path, unsigned size);
int profile_save_file(const char *path, char *report, unsigned size);
int profile_load_file(const char *path, char *report, unsigned size);
int dump_file(const char *path);

/* dlgmain.c */
BOOL CALLBACK __export main_dlg_proc(HWND dlg, UINT msg, WPARAM wp,
                                     LPARAM lp);
void set_help(const char *text);
void set_status(const char *fmt, ...);
void update_owner_status(void);

/* page area (page.c): the controls of a page are children of the main
 * dialog, so that the dialog manager handles keyboard navigation; they are
 * placed in dialog units relative to the page area */
extern int area_w, area_h; /* size of the page area in dialog units */
void page_init(void);
void page_show(int kind, int arg);
void page_refresh(int how);
void page_command(int id, int code, HWND ctl);
void page_hscroll(int code, int pos, HWND ctl);
void page_vscroll(int code, int pos);
void page_click(int x, int y);
void page_focus(HWND ctl);
int page_kind(void);
HWND page_control(const char *cls, const char *text, DWORD style, int x,
                  int y, int w, int h, int id);
void page_move(HWND ctl, int x, int y, int w, int h, int show);
void page_scrollbar(int first, int count, int visible);
void dlu_to_px(int *x, int *y);

/* catalog pages (pagefld.c) */
void fields_create(int pg);
void fields_refresh(int how);
void fields_command(int id, int code, HWND ctl);
void fields_hscroll(int code, int pos, HWND ctl);
void fields_vscroll(int code, int pos);
void fields_click(int y);
void fields_focus(HWND ctl);

/* raw registers (pageraw.c) */
void raw_create(void);
void raw_refresh(int how);
void raw_command(int id, int code, HWND ctl);
int bit_editor(HWND owner, int reg, int value);

/* device information (pageinfo.c) */
void info_create(void);
void info_refresh(int how);
void info_text(char *buf, unsigned size);

/* ESFM bank (pageesfm.c) */
void esfm_create(void);
void esfm_refresh(int how);
void esfm_command(int id, int code, HWND ctl);
void esfm_menu_load(HWND owner);
void esfm_menu_restore(HWND owner);

#endif /* ESSCTL_H */
