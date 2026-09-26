/*
 * pageesfm.c -- the ESFM patch bank page and menu commands.
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "essctl.h"

#include <commdlg.h>
#include "esfmlive.h"
#include "resource.h"

static HWND text;

static void show_status(void) {
  static char buf[600];
  struct esfm_status st;

  esfm_get_status(&st);
  if (!st.loaded) {
    strcpy(buf, "ESFM.DRV is not loaded.  It is the MIDI driver of the "
                "ES1869's FM synthesizer; it loads when a program opens "
                "the ESS FM MIDI device.");
  } else {
    sprintf(buf, "Driver:\t%.100s\r\nBank in memory:\t%lu bytes\r\n",
            st.path, (unsigned long)st.bank_size);
    if (esfm_last_bank[0])
      sprintf(buf + strlen(buf), "Loaded bank:\t%.100s\r\n", esfm_last_bank);
    else
      strcat(buf, "Loaded bank:\tthe driver's own\r\n");
    if (!st.known)
      sprintf(buf + strlen(buf), "\r\nCannot load banks: %s\r\n", st.why);
  }
  strcat(buf, "\r\n\r\nA bank is a file of 256 patch offsets followed by the "
              "patches, as in esfm_patch_banks\\*.bin, or a RIFF \"Ptch\" "
              "file.  It replaces the sounds of the running driver until "
              "Windows restarts; save a profile to load it again with "
              "essctl /load.");
  SetWindowText(text, buf);
}

void esfm_create(void) {
  int tabs = 64;

  page_control("BUTTON", "&Load bank...",
               WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 70, 14,
               IDC_PG_BTN1);
  page_control("BUTTON", "&Restore original",
               WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 76, 0, 70, 14,
               IDC_PG_BTN2);
  text = page_control("EDIT", "",
                      WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY |
                          WS_VSCROLL,
                      0, 20, area_w, area_h - 20, IDC_PG_EDIT);
  SendMessage(text, EM_SETTABSTOPS, 1, (LPARAM)(int FAR *)&tabs);
}

void esfm_refresh(int how) {
  if (how != REFRESH_TIMER)
    show_status();
}

void esfm_command(int id, int code, HWND ctl) {
  (void)ctl;
  if (code != BN_CLICKED)
    return;
  if (id == IDC_PG_BTN1)
    esfm_menu_load(g_main);
  else if (id == IDC_PG_BTN2)
    esfm_menu_restore(g_main);
}

void esfm_menu_load(HWND owner) {
  static const char filter[] = "Patch banks (*.bin;*.fm4)\0*.bin;*.fm4\0"
                               "All files (*.*)\0*.*\0";
  OPENFILENAME ofn;
  char path[144], msg[160];

  path[0] = 0;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = path;
  ofn.nMaxFile = sizeof(path);
  ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;
  if (!GetOpenFileName(&ofn))
    return;
  if (esfm_live_load(path, msg, sizeof(msg)))
    msg_error(owner, "%s", msg);
  else
    set_status("%s", msg);
  if (page_kind() == PK_ESFM)
    show_status();
}

void esfm_menu_restore(HWND owner) {
  char msg[160];

  if (esfm_live_restore(msg, sizeof(msg)))
    msg_error(owner, "%s", msg);
  else
    set_status("%s", msg);
  if (page_kind() == PK_ESFM)
    show_status();
}
