/*
 * The raw registers page and the bit editor. The page lists
 * the catalog registers of one bank in hex and binary.
 *
 * Notes:
 *
 * Writing a whole register bypasses the per-field tiers, so
 * the bit editor only writes in Expert mode. Its slider, text
 * field and bits all show the same value, in steps of one.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "essctl.h"
#include "esshw.h"
#include "essio.h"
#include "resource.h"
#include "winio.h"

#define MAX_VIEWS 12
#define MAX_LINES 128

struct view {
  u8 bank, ldn;
};

static struct view views[MAX_VIEWS];
static int nviews, cur_view;
static int line_reg[MAX_LINES]; // catalog register of each list line
static int line_raw[MAX_LINES];
static int nlines;
static HWND list, bank_cb;

static void view_name(const struct view *v, char *buf) {
  switch (v->bank) {
  case BK_MIXER:
    strcpy(buf, "Mixer registers");
    break;
  case BK_CTRL:
    strcpy(buf, "Controller registers (DSP)");
    break;
  case BK_APORT:
    strcpy(buf, "Audio_Base ports");
    break;
  case BK_CPORT:
    strcpy(buf, "Configuration ports");
    break;
  case BK_PNPCARD:
    strcpy(buf, "PnP card registers");
    break;
  default:
    sprintf(buf, "PnP logical device %u", v->ldn);
  }
}

static void binary(u8 v, char *out) {
  int i;
  for (i = 7; i >= 0; i--) {
    *out++ = (char)((v >> i) & 1 ? '1' : '0');
    if (i == 4)
      *out++ = ' ';
  }
  *out = 0;
}

static void format_line(int i, char *text) {
  const struct ess_reg *r = &ess_regs[line_reg[i]];
  char bits[12];

  if (line_raw[i] >= 0) {
    binary((u8)line_raw[i], bits);
    sprintf(text, "%02Xh  %02Xh  %s  %.60s", r->addr, line_raw[i], bits,
            r->name);
  } else {
    sprintf(text, "%02Xh  --   ---- ----  %.60s", r->addr, r->name);
  }
}

static void fill_list(void) {
  const struct view *v = &views[cur_view];
  char text[96];
  int i, sel, topi;

  sel = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
  topi = (int)SendMessage(list, LB_GETTOPINDEX, 0, 0);
  SendMessage(list, WM_SETREDRAW, FALSE, 0);
  SendMessage(list, LB_RESETCONTENT, 0, 0);
  for (i = 0; i < nlines; i++) {
    format_line(i, text);
    SendMessage(list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)text);
  }
  if (sel >= 0 && sel < nlines)
    SendMessage(list, LB_SETCURSEL, sel, 0);
  SendMessage(list, LB_SETTOPINDEX, topi, 0);
  SendMessage(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, 0, TRUE);
  (void)v;
}

static void select_view(int n) {
  int i;

  cur_view = n;
  nlines = 0;
  for (i = 0; i < R_COUNT && nlines < MAX_LINES; i++)
    if (ess_regs[i].bank == views[n].bank &&
        (ess_regs[i].bank != BK_PNPLDN || ess_regs[i].ldn == views[n].ldn)) {
      line_reg[nlines] = i;
      line_raw[nlines++] = -1;
    }
  SendMessage(list, LB_RESETCONTENT, 0, 0);
}

void raw_create(void) {
  char name[48];
  int i, j;

  nviews = 0;
  for (i = 0; i < R_COUNT; i++) {
    for (j = 0; j < nviews; j++)
      if (views[j].bank == ess_regs[i].bank &&
          (ess_regs[i].bank != BK_PNPLDN || views[j].ldn == ess_regs[i].ldn))
        break;
    if (j == nviews && nviews < MAX_VIEWS) {
      views[nviews].bank = ess_regs[i].bank;
      views[nviews++].ldn = ess_regs[i].ldn;
    }
  }
  bank_cb = page_control("COMBOBOX", "",
                         WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                             CBS_DROPDOWNLIST,
                         0, 0, 150, 120, IDC_PG_BANK);
  for (i = 0; i < nviews; i++) {
    view_name(&views[i], name);
    SendMessage(bank_cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)name);
  }
  page_control("BUTTON", "&Read", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
               156, 0, 50, 13, IDC_PG_READ);
  page_control("BUTTON", "&Edit...", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
               210, 0, 50, 13, IDC_PG_EDITREG);
  list = page_control("LISTBOX", "",
                      WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL |
                          LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                      0, 17, area_w, area_h - 38, IDC_PG_LIST);
  SendMessage(list, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), 0);
  page_control("STATIC",
               "Registers that change state when read, and write-only ones, "
               "are not read.  Double-click a register to edit its bits.",
               WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, area_h - 18, area_w, 18,
               IDC_PG_TEXT);
  SendMessage(bank_cb, CB_SETCURSEL, cur_view < nviews ? cur_view : 0, 0);
  select_view(cur_view < nviews ? cur_view : 0);
}

void raw_refresh(int how) {
  int i, err;
  u8 flags;

  err = winio_begin();
  for (i = 0; i < nlines; i++) {
    flags = ess_regs[line_reg[i]].flags;
    if (flags & (RF_READ_SIDEFX | RF_WRITEONLY))
      line_raw[i] = -1;
    else if ((flags & RF_NEEDS_IDLE) && how < 2)
      continue; // DSP channel only on the Read button
    else
      line_raw[i] = err < 0 ? err : ess_read(line_reg[i]);
  }
  winio_end();
  fill_list();
  // with no device the rows say so, otherwise the status line says why
  if (err < 0 && err != -ESSHW_ENODEV)
    set_status("%s", esshw_strerror(err));
}

static void edit_selected(void) {
  int sel = (int)SendMessage(list, LB_GETCURSEL, 0, 0);

  if (sel < 0 || sel >= nlines)
    return;
  bit_editor(g_main, line_reg[sel], line_raw[sel] >= 0 ? line_raw[sel] : 0);
  raw_refresh(1);
}

void raw_command(int id, int code, HWND ctl) {
  (void)ctl;
  if (id == IDC_PG_BANK && code == CBN_SELCHANGE) {
    select_view((int)SendMessage(bank_cb, CB_GETCURSEL, 0, 0));
    raw_refresh(1);
  } else if (id == IDC_PG_READ && code == BN_CLICKED) {
    raw_refresh(2);
  } else if (id == IDC_PG_EDITREG && code == BN_CLICKED) {
    edit_selected();
  } else if (id == IDC_PG_LIST && code == LBN_DBLCLK) {
    edit_selected();
  }
}

// --- bit editor -------------------------------------------------------------

static struct {
  int reg;
  u8 value;
} be;

static void be_show(HWND dlg, int from_edit) {
  char text[96];
  int i;

  if (!from_edit) {
    sprintf(text, "%02X", be.value);
    SetDlgItemText(dlg, IDC_BE_HEX, text);
  }
  for (i = 0; i < 8; i++)
    CheckDlgButton(dlg, IDC_BE_BIT0 + i, (be.value >> i) & 1);
  SetScrollPos(GetDlgItem(dlg, IDC_BE_SLIDER), SB_CTL, be.value, TRUE);
  SendDlgItemMessage(dlg, IDC_BE_FIELDS, LB_RESETCONTENT, 0, 0);
  for (i = 0; i < F_COUNT; i++) {
    const struct ess_field *f = &ess_fields[i];
    char value[48];
    if (f->reg != be.reg)
      continue;
    cat_format(f, be.value, value, sizeof(value));
    if (f->width == 1)
      sprintf(text, "%u\t%s\t%.40s", f->shift, value, f->label);
    else
      sprintf(text, "%u-%u\t%s\t%.40s", f->shift + f->width - 1, f->shift,
              value, f->label);
    SendDlgItemMessage(dlg, IDC_BE_FIELDS, LB_ADDSTRING, 0,
                       (LPARAM)(LPSTR)text);
  }
}

static void be_write(HWND dlg) {
  const struct ess_reg *r = &ess_regs[be.reg];
  char text[128];
  int err, raw = -1;

  sprintf(text, "Write %02Xh to %s %02Xh (%.60s)?", be.value,
          ess_bank_names[r->bank], r->addr, r->name);
  if (!confirm(dlg, text))
    return;
  err = winio_begin();
  if (err == 0)
    err = ess_write(be.reg, be.value);
  if (err == 0 && !(r->flags & (RF_WRITEONLY | RF_READ_SIDEFX)))
    raw = ess_read(be.reg);
  winio_end();
  if (err < 0) {
    msg_error(dlg, "Could not write %s: %s", r->name, esshw_strerror(err));
    return;
  }
  if (raw >= 0 && raw != be.value)
    set_status("%s: wrote %02Xh, the chip returns %02Xh", r->name, be.value,
               raw);
  else
    set_status("%s: wrote %02Xh", r->name, be.value);
  if (raw >= 0) {
    be.value = (u8)raw;
    be_show(dlg, 0);
  }
}

BOOL CALLBACK __export bit_dlg_proc(HWND dlg, UINT msg, WPARAM wp,
                                    LPARAM lp) {
  const struct ess_reg *r = &ess_regs[be.reg];
  char text[128];
  int tabs[2];
  int i;

  (void)lp;
  switch (msg) {
  case WM_INITDIALOG:
    sprintf(text, "%s %02Xh: %.80s%s", ess_bank_names[r->bank], r->addr,
            r->name, g_expert ? "" : "\n(read only: writing needs Expert mode)");
    SetDlgItemText(dlg, IDC_BE_NAME, text);
    tabs[0] = 20;
    tabs[1] = 64;
    SendDlgItemMessage(dlg, IDC_BE_FIELDS, LB_SETTABSTOPS, 2,
                       (LPARAM)(int FAR *)tabs);
    SendDlgItemMessage(dlg, IDC_BE_HEX, EM_LIMITTEXT, 2, 0);
    SetScrollRange(GetDlgItem(dlg, IDC_BE_SLIDER), SB_CTL, 0, 255, FALSE);
    EnableWindow(GetDlgItem(dlg, IDOK), g_expert);
    be_show(dlg, 0);
    return TRUE;
  case WM_HSCROLL:
    // the slider, in steps of one
    i = be.value;
    switch (wp) {
    case SB_LINEUP:
    case SB_PAGEUP:
      i--;
      break;
    case SB_LINEDOWN:
    case SB_PAGEDOWN:
      i++;
      break;
    case SB_TOP:
      i = 0;
      break;
    case SB_BOTTOM:
      i = 255;
      break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
      i = LOWORD(lp);
      break;
    default:
      return TRUE;
    }
    if (i >= 0 && i <= 255 && i != be.value) {
      be.value = (u8)i;
      be_show(dlg, 0);
    }
    return TRUE;
  case WM_COMMAND:
    if (wp >= IDC_BE_BIT0 && wp < IDC_BE_BIT0 + 8 &&
        HIWORD(lp) == BN_CLICKED) {
      be.value = 0;
      for (i = 0; i < 8; i++)
        if (IsDlgButtonChecked(dlg, IDC_BE_BIT0 + i))
          be.value |= (u8)(1 << i);
      be_show(dlg, 0);
      return TRUE;
    }
    if (wp == IDC_BE_HEX && HIWORD(lp) == EN_CHANGE) {
      unsigned v;
      GetDlgItemText(dlg, IDC_BE_HEX, text, sizeof(text));
      if (sscanf(text, "%x", &v) == 1 && v < 256) {
        be.value = (u8)v;
        be_show(dlg, 1);
      }
      return TRUE;
    }
    if (wp == IDOK) {
      if (g_expert)
        be_write(dlg);
      return TRUE;
    }
    if (wp == IDCANCEL) {
      EndDialog(dlg, 0);
      return TRUE;
    }
    break;
  }
  return FALSE;
}

int bit_editor(HWND owner, int reg, int value) {
  DLGPROC proc = (DLGPROC)MakeProcInstance((FARPROC)bit_dlg_proc, g_inst);
  int rc;

  be.reg = reg;
  be.value = (u8)value;
  rc = DialogBox(g_inst, "BITEDIT", owner, proc);
  FreeProcInstance((FARPROC)proc);
  return rc;
}
