/*
 * ess3d's tray icon: an icon in the taskbar's notification area that shows

 * * whether 3-D is on, and a small panel with every 3-D setting when it's
 *
 * clicked (see ess3dtr.c).
 *
 * (c) 2026 Ethan Halsall
 * <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESS3DTR_H
#define ESS3DTR_H

#include <windows.h>

#include "ess3d.h"

#define TRAY_CLASS "Ess3dTray" // the tray's window, for FindWindow

// messages to the tray's window
#define TRAY_CHANGED (WM_USER + 21) // another program changed the setting
#define TRAY_PANEL (WM_USER + 22)   // show the panel

// the window of a running tray icon, 0 if there's none
HWND tray_window(void);

// tell a running tray icon that the setting changed
void tray_changed(void);

// the tray's hidden window, made right after tray_window() found none,
// before anything can yield, so two "ess3d tray" can't both start
// 0 if there's no memory for it
HWND tray_claim(HINSTANCE inst);

// show the tray icon until its panel's Close button or "ess3d exit"
// returns ess3d's exit code
int tray_run(const struct ess3d_cmd *c);

// append a line with the time to the log file name (ess3dw.c)
void ess3d_log(const char *name, const char *text);

#endif /* ESS3DTR_H */
