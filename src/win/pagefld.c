/*
 * Pages built from the register catalog. Each field gets a
 * row with its label, a control, the decoded value and its
 * tier. The control depends on the kind of field:
 *
 *   one bit             check box
 *   level, signed, raw  text field and a slider on its right, in steps
 *                       of one over the field's range
 *   named values        drop-down list
 *   action / pulse      push button
 *   status              value only
 *
 * Notes:
 *
 * Every change is a read-modify-write of the register and then
 * a read back, so the row shows what the chip returned.
 * The text field takes decimal for levels and signed values
 * and hex for raw values. It's written on Enter or when it
 * loses the focus.
 * Controller registers go through the DSP command channel and
 * are only read on request.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "essctl.h"
#include "esshw.h"
#include "essio.h"
#include "resource.h"
#include "winio.h"

#define MAX_ROWS 96
#define RH 13      // row pitch in dialog units
#define X_LABEL 0
#define W_LABEL 120
#define X_CTL 124
#define W_CTL 80
#define X_VAL 208
#define W_VAL 76
#define W_EDIT 24 // a slider's text field, the slider on its right
#define X_TAG 286
#define W_TAG 28

#define NOT_READ (-100) // register not read (yet)

struct row {
  int field;
  HWND lab, ctl, val, tag;
  HWND edit;  // the slider's text field
  int raw;    // register value, NOT_READ or a negative error
  int lo, hi; // scroll bar range
};

static struct row rows[MAX_ROWS];
static int nrows, top, nvis, y0;
static int cur_pg;
static int drag_row = -1;
static HWND read_btn;

static int row_of(HWND ctl) {
  int id = GetDlgCtrlID(ctl);
  int r = (id - IDC_ROW) / ROW_IDS;
  return id >= IDC_ROW && r < nrows ? r : -1;
}

// a slider and a text field
static int is_ranged(const struct ess_field *f) {
  return f->kind == K_UINT || f->kind == K_SMAG || f->kind == K_HEX;
}

static int can_write(const struct ess_field *f) {
  return cat_writable(f, g_expert);
}

static const char *tag_text(const struct ess_field *f) {
  switch (f->tier) {
  case T_CAUTION:
    return "caution";
  case T_EXPERT:
    return "expert";
  }
  return "";
}

static void show_row(int i);

static void layout(void) {
  int i;

  for (i = 0; i < nrows; i++) {
    struct row *r = &rows[i];
    const struct ess_field *f = &ess_fields[r->field];
    int show = i >= top && i < top + nvis;
    int y = y0 + (i - top) * RH;

    page_move(r->lab, X_LABEL, y + 2, W_LABEL, 9, show);
    switch (f->kind) {
    case K_ENUM:
      page_move(r->ctl, X_CTL, y, W_CTL, 100, show);
      break;
    case K_ACTION:
    case K_PULSE:
      page_move(r->ctl, X_CTL, y, 44, 12, show);
      break;
    case K_BOOL:
      page_move(r->ctl, X_CTL, y + 1, 12, 10, show);
      break;
    default:
      if (r->edit) {
        // the text field, then the slider on its right
        page_move(r->edit, X_CTL, y, W_EDIT, 12, show);
        page_move(r->ctl, X_CTL + W_EDIT + 3, y + 1, W_CTL, 10, show);
      } else if (r->ctl) {
        page_move(r->ctl, X_CTL, y + 2, W_CTL, 9, show);
      }
    }
    if (r->edit)
      page_move(r->val, X_CTL + W_EDIT + W_CTL + 6, y + 2,
                X_TAG - (X_CTL + W_EDIT + W_CTL + 6), 9, show);
    else
      page_move(r->val, X_VAL, y + 2, W_VAL, 9, show);
    page_move(r->tag, X_TAG, y + 2, W_TAG, 9, show);
  }
  page_scrollbar(top, nrows, nvis);
}

static void fill_enum(HWND cb, u8 enum_id) {
  unsigned i;

  for (i = 0; i < ess_enumv_count; i++)
    if (ess_enumvs[i].enum_id == enum_id) {
      int idx = (int)SendMessage(cb, CB_ADDSTRING, 0,
                                 (LPARAM)(LPSTR)ess_enumvs[i].text);
      SendMessage(cb, CB_SETITEMDATA, idx, ess_enumvs[i].value);
    }
}

static int is_bool(const struct ess_field *f) {
  return f->kind == K_BOOL || (f->kind == K_UINT && f->width == 1);
}

// the slider position of a field value
static int slider_pos(const struct ess_field *f, u8 v) {
  return f->kind == K_SMAG ? cat_smag(f, v) : v;
}

// the field value of a slider position
static u8 slider_value(const struct ess_field *f, int pos) {
  return f->kind == K_SMAG ? cat_smag_code(f, pos) : (u8)pos;
}

// the text field shows the slider position, hex for raw values
static void show_edit(struct row *r, int pos, int force) {
  char text[8];

  if (!r->edit || (!force && GetFocus() == r->edit))
    return; // don't change what's being typed
  sprintf(text, ess_fields[r->field].kind == K_HEX ? "%02X" : "%d", pos);
  SetWindowText(r->edit, text);
}

// next to the text field: the decoded value, or the range
static void range_text(const struct row *r, u8 raw, char *text, unsigned size) {
  const struct ess_field *f = &ess_fields[r->field];

  if (f->fmt != FMT_NONE)
    cat_format(f, raw, text, size);
  else if (f->kind == K_HEX)
    sprintf(text, "%02Xh-%02Xh", r->lo, r->hi);
  else if (f->kind == K_SMAG)
    sprintf(text, "%d to +%d", r->lo, r->hi);
  else
    sprintf(text, "of %d", r->hi);
}

static void create_row(int i, int field) {
  const struct ess_field *f = &ess_fields[field];
  struct row *r = &rows[i];
  int id = IDC_ROW + ROW_IDS * i;
  DWORD tab = WS_TABSTOP;

  memset(r, 0, sizeof(*r));
  r->field = field;
  r->raw = NOT_READ;
  r->lab = page_control("STATIC", f->label, SS_LEFTNOWORDWRAP | SS_NOPREFIX,
                        0, 0, W_LABEL, 9, id);
  if (f->tier == T_RO || f->kind == K_RO) {
    // status, only the value column
  } else if (is_bool(f)) {
    r->ctl = page_control("BUTTON", "", tab | BS_AUTOCHECKBOX, 0, 0, 12, 10,
                          id + 1);
  } else {
    switch (f->kind) {
    case K_UINT:
    case K_SMAG:
    case K_HEX:
      // the text field first, so Tab goes from it to its slider
      r->edit = page_control("EDIT", "",
                             tab | WS_BORDER | ES_AUTOHSCROLL |
                                 (f->kind == K_HEX ? ES_UPPERCASE : 0),
                             0, 0, W_EDIT, 12, id + 4);
      SendMessage(r->edit, EM_LIMITTEXT, 5, 0);
      r->ctl = page_control("SCROLLBAR", "", tab | SBS_HORZ, 0, 0, W_CTL, 10,
                            id + 1);
      if (f->kind == K_SMAG) {
        r->lo = -(1 << (f->width - 1));
        r->hi = (1 << (f->width - 1)) - 1;
      } else {
        r->lo = 0;
        r->hi = cat_max(f);
      }
      SetScrollRange(r->ctl, SB_CTL, r->lo, r->hi, FALSE);
      break;
    case K_ENUM:
      r->ctl = page_control("COMBOBOX", "",
                            tab | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, W_CTL,
                            100, id + 1);
      fill_enum(r->ctl, f->enum_id);
      break;
    case K_ACTION:
    case K_PULSE:
      r->ctl = page_control("BUTTON", f->kind == K_PULSE ? "Pulse" : "Do it",
                            tab | BS_PUSHBUTTON, 0, 0, 44, 12, id + 1);
      break;
    }
  }
  if (r->ctl && !can_write(f))
    EnableWindow(r->ctl, FALSE);
  if (r->edit && !can_write(f))
    EnableWindow(r->edit, FALSE);
  r->val = page_control("STATIC", "", SS_LEFTNOWORDWRAP | SS_NOPREFIX, 0, 0,
                        W_VAL, 9, id + 2);
  r->tag = page_control("STATIC", tag_text(f), SS_LEFTNOWORDWRAP, 0, 0,
                        W_TAG, 9, id + 3);
}

void fields_create(int pg) {
  int i, dsp = 0;

  cur_pg = pg;
  nrows = top = 0;
  drag_row = -1;
  read_btn = 0;
  for (i = 0; i < F_COUNT; i++)
    if (ess_fields[i].page == pg &&
        (ess_regs[ess_fields[i].reg].flags & RF_NEEDS_IDLE))
      dsp = 1;
  y0 = 0;
  if (dsp) {
    read_btn = page_control("BUTTON", "Read controller registers",
                            WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0,
                            110, 13, IDC_PG_READ);
    page_control("STATIC",
                 "These go through the DSP command channel and are read "
                 "only on request.",
                 WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 114, 0, area_w - 124,
                 16, IDC_PG_TEXT);
    y0 = 18;
  }
  for (i = 0; i < F_COUNT && nrows < MAX_ROWS; i++)
    if (ess_fields[i].page == pg)
      create_row(nrows++, i);
  nvis = (area_h - y0) / RH;
  layout();
  for (i = 0; i < nrows; i++)
    show_row(i);
}

static const char *short_error(int err) {
  switch (-err) {
  case ESSHW_EINUSE:
    return "in use by DOS";
  case ESSHW_EBUSY:
    return "DSP busy";
  case ESSHW_ETIMEOUT:
    return "no answer";
  case ESSHW_ENOCFG:
    return "no config port";
  case ESSHW_ENODEV:
    return "no device";
  case ESSIO_EABSENT:
    return "not present";
  }
  return "error";
}

static void show_row(int i) {
  struct row *r = &rows[i];
  const struct ess_field *f = &ess_fields[r->field];
  const struct ess_reg *reg = &ess_regs[f->reg];
  char text[48];
  u8 v;
  int j, n;

  if (r->raw == NOT_READ) {
    if (reg->flags & RF_WRITEONLY)
      strcpy(text, "(write only)");
    else if (reg->flags & RF_READ_SIDEFX)
      strcpy(text, "(not read)");
    else
      strcpy(text, "-");
    SetWindowText(r->val, text);
    return;
  }
  if (r->raw < 0) {
    SetWindowText(r->val, short_error(r->raw));
    // nothing to set on a device the card doesn't have
    if (r->raw == -ESSIO_EABSENT && r->ctl)
      EnableWindow(r->ctl, FALSE);
    if (r->raw == -ESSIO_EABSENT && r->edit)
      EnableWindow(r->edit, FALSE);
    return;
  }
  v = cat_get(f, (u8)r->raw);
  if (r->edit)
    range_text(r, (u8)r->raw, text, sizeof(text));
  else
    cat_format(f, (u8)r->raw, text, sizeof(text));
  SetWindowText(r->val, text);
  if (!r->ctl || i == drag_row)
    return;
  if (is_bool(f)) {
    SendMessage(r->ctl, BM_SETCHECK, v ? 1 : 0, 0);
  } else if (is_ranged(f)) {
    SetScrollPos(r->ctl, SB_CTL, slider_pos(f, v), TRUE);
    show_edit(r, slider_pos(f, v), 0);
  } else if (f->kind == K_ENUM) {
    n = (int)SendMessage(r->ctl, CB_GETCOUNT, 0, 0);
    for (j = 0; j < n; j++)
      if ((u8)SendMessage(r->ctl, CB_GETITEMDATA, j, 0) == v)
        break;
    SendMessage(r->ctl, CB_SETCURSEL, j < n ? j : -1, 0);
  }
}

// show raw in every row of register reg
static void set_reg(int reg, int raw) {
  int i;

  for (i = 0; i < nrows; i++)
    if (ess_fields[rows[i].field].reg == reg) {
      rows[i].raw = raw;
      show_row(i);
    }
}

void fields_refresh(int how) {
  static u8 done[R_COUNT];
  int i, err, reg;
  int dsp = how >= 2;
  u8 flags;

  if (!nrows)
    return;
  memset(done, 0, sizeof(done));
  err = winio_begin();
  // how to decode the Audio 1 rate: mixer 71h bit 5
  if (err == 0 && dsp && (reg = ess_read(R_MX71)) >= 0)
    cat_a1_like_70 = (reg >> 5) & 1;
  for (i = 0; i < nrows; i++) {
    reg = ess_fields[rows[i].field].reg;
    if (done[reg])
      continue;
    done[reg] = 1;
    flags = ess_regs[reg].flags;
    if (flags & (RF_READ_SIDEFX | RF_WRITEONLY))
      continue;
    if ((flags & RF_NEEDS_IDLE) && !dsp)
      continue;
    set_reg(reg, err < 0 ? err : ess_read(reg));
  }
  winio_end();
  // with no device the rows say so, otherwise the status line says why
  if (err < 0 && err != -ESSHW_ENODEV)
    set_status("%s", esshw_strerror(err));
}

static void help_for(int i) {
  const struct ess_field *f = &ess_fields[rows[i].field];
  const struct ess_reg *r = &ess_regs[f->reg];
  char text[400];

  sprintf(text, "%s (%s %02Xh", f->label, ess_bank_names[r->bank], r->addr);
  if (f->width == 1)
    sprintf(text + strlen(text), " bit %u", f->shift);
  else
    sprintf(text + strlen(text), " bits %u-%u", f->shift + f->width - 1,
            f->shift);
  sprintf(text + strlen(text), "): %s", f->help);
  if (f->flags & FF_DRVOWNED)
    strcat(text, " Windows' driver sets this itself.");
  if (f->flags & FF_VOLATILE)
    strcat(text, " A DSP reset sets it back.");
  if (f->tier == T_EXPERT && !g_expert)
    strcat(text, " Needs Expert mode.");
  set_help(text);
}

// write the field of row i and read the register back
static void write_row(int i, u8 value) {
  struct row *r = &rows[i];
  const struct ess_field *f = &ess_fields[r->field];
  const struct ess_reg *reg = &ess_regs[f->reg];
  char text[48];
  int err, raw;

  help_for(i);
  if (f->tier == T_EXPERT && (f->kind == K_ACTION || f->kind == K_PULSE)) {
    sprintf(text, "%.30s?", f->label);
    if (!confirm(g_main, text)) {
      show_row(i);
      return;
    }
  }
  err = winio_begin();
  if (err == 0)
    err = ess_field_write(r->field, value, g_expert);
  raw = NOT_READ;
  if (err == 0 && !(reg->flags & (RF_WRITEONLY | RF_READ_SIDEFX)))
    raw = ess_read(f->reg);
  winio_end();

  if (err < 0) {
    set_status("%s: %s", f->label, ess_strerror(err));
    show_row(i); // back to what the chip had
    return;
  }
  set_reg(f->reg, raw);
  tray_notify(f->reg);
  if (raw >= 0 && f->kind != K_ACTION && f->kind != K_PULSE &&
      cat_get(f, (u8)raw) != value)
    set_status("%s: wrote %u, the chip returns %u", f->label, value,
               cat_get(f, (u8)raw));
  else {
    cat_format(f, (u8)(raw >= 0 ? raw : cat_set(f, 0, value)), text,
               sizeof(text));
    set_status("%s: %s", f->label, text);
  }
}

// write what was typed in row i's text field, if it's a new value
static void apply_edit(int i) {
  struct row *r = &rows[i];
  const struct ess_field *f = &ess_fields[r->field];
  char text[16], *end;
  long v;

  GetWindowText(r->edit, text, sizeof(text));
  v = strtol(text, &end, f->kind == K_HEX ? 16 : 10);
  if (f->kind == K_HEX && (*end == 'h' || *end == 'H'))
    end++;
  if (!text[0] || *end || v < r->lo || v > r->hi) {
    MessageBeep(0);
    if (f->kind == K_HEX)
      set_status("%s: %s is not 00h to %02Xh", f->label, text, r->hi);
    else
      set_status("%s: %s is not %d to %d", f->label, text, r->lo, r->hi);
    if (r->raw >= 0)
      show_edit(r, slider_pos(f, cat_get(f, (u8)r->raw)), 1);
    return;
  }
  if (r->raw >= 0 && v == slider_pos(f, cat_get(f, (u8)r->raw)))
    return;
  SetScrollPos(r->ctl, SB_CTL, (int)v, TRUE);
  write_row(i, slider_value(f, (int)v));
  if (r->raw >= 0)
    show_edit(r, slider_pos(f, cat_get(f, (u8)r->raw)), 1);
}

void fields_command(int id, int code, HWND ctl) {
  int i, sel;
  const struct ess_field *f;
  HWND focus;

  if (id == IDC_PG_READ) {
    fields_refresh(2);
    return;
  }
  if (id == IDOK) {
    // Enter in a text field
    focus = GetFocus();
    i = row_of(focus);
    if (i >= 0 && rows[i].edit == focus)
      apply_edit(i);
    return;
  }
  i = row_of(ctl);
  if (i >= 0 && (id - IDC_ROW) % ROW_IDS == 4) {
    if (code == EN_KILLFOCUS && can_write(&ess_fields[rows[i].field]))
      apply_edit(i);
    else if (code == EN_SETFOCUS)
      help_for(i);
    return;
  }
  if (i < 0 || (id - IDC_ROW) % ROW_IDS != 1)
    return;
  f = &ess_fields[rows[i].field];
  if (is_bool(f) && code == BN_CLICKED) {
    write_row(i, (u8)(SendMessage(ctl, BM_GETCHECK, 0, 0) ? 1 : 0));
  } else if (f->kind == K_ENUM && code == CBN_SELCHANGE) {
    sel = (int)SendMessage(ctl, CB_GETCURSEL, 0, 0);
    if (sel >= 0)
      write_row(i, (u8)SendMessage(ctl, CB_GETITEMDATA, sel, 0));
  } else if ((f->kind == K_ACTION || f->kind == K_PULSE) &&
             code == BN_CLICKED) {
    write_row(i, cat_max(f));
  } else if (code == CBN_SETFOCUS) {
    help_for(i);
  }
}

void fields_hscroll(int code, int pos, HWND ctl) {
  int i = row_of(ctl);
  struct row *r;
  const struct ess_field *f;
  int cur;
  char text[48];

  if (i < 0)
    return;
  r = &rows[i];
  f = &ess_fields[r->field];
  cur = GetScrollPos(ctl, SB_CTL);
  // steps of one: arrows, keys and clicks beside the thumb
  switch (code) {
  case SB_LINEUP:
  case SB_PAGEUP:
    cur--;
    break;
  case SB_LINEDOWN:
  case SB_PAGEDOWN:
    cur++;
    break;
  case SB_TOP:
    cur = r->lo;
    break;
  case SB_BOTTOM:
    cur = r->hi;
    break;
  case SB_THUMBTRACK:
  case SB_THUMBPOSITION:
    cur = (short)pos;
    break;
  case SB_ENDSCROLL:
    if (drag_row == i) {
      drag_row = -1;
      write_row(i, slider_value(f, cur));
    }
    return;
  default:
    return;
  }
  if (cur < r->lo)
    cur = r->lo;
  if (cur > r->hi)
    cur = r->hi;
  SetScrollPos(ctl, SB_CTL, cur, TRUE);
  show_edit(r, cur, 1);
  if (!can_write(f))
    return;
  if (code == SB_THUMBTRACK && !winio_can_poll()) {
    // direct I/O with ES1869.VXD, only write when the thumb is released
    drag_row = i;
    range_text(r, cat_set(f, 0, slider_value(f, cur)), text, sizeof(text));
    SetWindowText(r->val, text);
    return;
  }
  drag_row = -1;
  write_row(i, slider_value(f, cur));
}

void fields_vscroll(int code, int pos) {
  int old = top;

  switch (code) {
  case SB_LINEUP:
    top--;
    break;
  case SB_LINEDOWN:
    top++;
    break;
  case SB_PAGEUP:
    top -= nvis;
    break;
  case SB_PAGEDOWN:
    top += nvis;
    break;
  case SB_THUMBTRACK:
  case SB_THUMBPOSITION:
    top = pos;
    break;
  case SB_TOP:
    top = 0;
    break;
  case SB_BOTTOM:
    top = nrows;
    break;
  }
  if (top > nrows - nvis)
    top = nrows - nvis;
  if (top < 0)
    top = 0;
  if (top != old)
    layout();
}

void fields_click(int y) {
  int i = (y - y0) / RH + top;

  if (y >= y0 && i < nrows)
    help_for(i);
}

void fields_focus(HWND ctl) {
  int i = row_of(ctl);

  if (i < 0)
    return;
  // keep the focused row visible
  if (i < top || i >= top + nvis) {
    top = i < top ? i : i - nvis + 1;
    layout();
  }
  help_for(i);
}
