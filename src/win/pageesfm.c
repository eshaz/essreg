/*
 * The ESFM patch bank page and the ESFM menu commands.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "essctl.h"

#include <commdlg.h>
#include "esfmlive.h"
#include "esfmtest.h"
#include "resource.h"

static HWND text, voices;
static char voice_text[2048];

// show the driver's voices and what the chip says (see esfm_diag_text)
static void show_voices(void) {
  static char buf[2048];
  struct esfm_diag d;
  int rc = esfm_diag_read(&d, 1);

  esfm_diag_text(&d, rc, "\r\n", buf, sizeof(buf));
  if (strcmp(buf, voice_text)) {
    strcpy(voice_text, buf);
    SetWindowText(voices, buf);
  }
}

static void show_status(void) {
  static char buf[900];
  struct esfm_status st;
  struct esfm_diag d;
  int file;

  esfm_get_status(&st);
  // the fixed ESFM.DRV plays the bank file named in SYSTEM.INI
  file = esfm_diag_read(&d, 0) == 0 && d.version >= 2;
  if (!st.loaded) {
    strcpy(buf, "ESFM.DRV is not loaded.  It is the MIDI driver of the "
                "ES1869's FM synthesizer; it loads when a program opens "
                "the ESS FM MIDI device.");
  } else {
    sprintf(buf, "Driver:\t%.100s\r\nBank in memory:\t%lu bytes\r\n",
            st.path, (unsigned long)st.bank_size);
    if (file && d.file_used)
      sprintf(buf + strlen(buf), "Bank file:\t%.100s\r\n", d.file);
    else if (esfm_last_bank[0])
      sprintf(buf + strlen(buf), "Loaded bank:\t%.100s\r\n", esfm_last_bank);
    else
      strcat(buf, "Loaded bank:\tthe driver's own\r\n");
    if (!st.known)
      sprintf(buf + strlen(buf), "\r\nCannot load banks: %s\r\n", st.why);
  }
  if (file)
    strcat(buf, "\r\nA bank is a file of 256 patch offsets followed by the "
                "patches, as in esfm_patch_banks\\*.bin, or a RIFF \"Ptch\" "
                "file.  This ESFM.DRV plays the bank file named in "
                "SYSTEM.INI [" ESFM_INI_SECTION "] " ESFM_INI_KEY "=, and "
                "loads it again when the file changes.  Load bank sets it, "
                "Restore original removes it.");
  else
    strcat(buf, "\r\nA bank is a file of 256 patch offsets followed by the "
                "patches, as in esfm_patch_banks\\*.bin, or a RIFF \"Ptch\" "
                "file.  It replaces the sounds of the running driver until "
                "Windows restarts; save a profile to load it again with "
                "essctl /load.");
  SetWindowText(text, buf);
}

void esfm_create(void) {
  int tabs = 64, split = area_h * 2 / 5;

  page_control("BUTTON", "&Load bank...",
               WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 70, 14,
               IDC_PG_BTN1);
  page_control("BUTTON", "&Restore original",
               WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 76, 0, 70, 14,
               IDC_PG_BTN2);
  page_control("BUTTON", "&Stress test",
               WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 152, 0, 70, 14,
               IDC_PG_BTN3);
  text = page_control("EDIT", "",
                      WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY |
                          WS_VSCROLL,
                      0, 20, area_w, split - 24, IDC_PG_EDIT);
  SendMessage(text, EM_SETTABSTOPS, 1, (LPARAM)(int FAR *)&tabs);
  voices = page_control("EDIT", "",
                        WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY |
                            WS_VSCROLL,
                        0, split, area_w, area_h - split, IDC_PG_VOICES);
  SendMessage(voices, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), 0);
  voice_text[0] = 0;
}

void esfm_refresh(int how) {
  if (how != REFRESH_TIMER)
    show_status();
  show_voices();
}

static void stress_test(HWND owner) {
  static char report[900];
  int rc;

  if (!confirm(owner, "The stress test plays about 7 seconds of dense music "
                      "on the ESFM synthesizer and then looks for notes "
                      "left sounding.  Stop other MIDI playback first.\n\n"
                      "Run it now?"))
    return;
  rc = esfm_stress_test(report, sizeof(report));
  MessageBox(owner, report, "ESFM stress test",
             MB_OK | (rc < 0 ? MB_ICONEXCLAMATION
                             : rc ? MB_ICONSTOP : MB_ICONINFORMATION));
  show_voices();
}

void esfm_command(int id, int code, HWND ctl) {
  (void)ctl;
  if (code != BN_CLICKED)
    return;
  if (id == IDC_PG_BTN1)
    esfm_menu_load(g_main);
  else if (id == IDC_PG_BTN2)
    esfm_menu_restore(g_main);
  else if (id == IDC_PG_BTN3)
    stress_test(g_main);
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
