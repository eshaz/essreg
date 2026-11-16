/*
 * The mouse wheel on the sliders of essctl and ess3d. A notch moves the
 * slider under the mouse pointer one step, as a click on its arrow does:
 * a turn away from you raises the value, a turn toward you lowers it.
 *
 * Notes:
 *
 * Windows 98 posts WM_MOUSEWHEEL to the window with the focus, with the
 * turn in the high word of its 32-bit wParam, which a 16-bit program
 * doesn't get: its wParam is the low word, the keys. So wheel_get takes
 * the message through USER32's PeekMessageA (the generic thunks) before
 * GetMessage would, and the turn comes with it. Without the thunks
 * (Windows 3.1), it reads the 16-bit message, and the MSWHEEL_ROLLMSG of
 * IntelliPoint on Windows 95, whose wParam is the turn.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef WHEEL_H
#define WHEEL_H

#include <windows.h>

// the wheel elsewhere than on a slider, at pt on the screen, steps notches
// away from you (toward you below 0): nonzero if the program used it,
// else the message goes to its window; 0 for none
extern int (*wheel_other)(POINT pt, int steps);

// find USER32's PeekMessageA and DispatchMessageA; without them, the
// wheel works where the 16-bit messages have the turn
void wheel_init(void);
void wheel_free(void);

// GetMessage, which uses the wheel's messages on the way
BOOL wheel_get(MSG FAR *msg);

// DialogBox with wheel_get as its loop, which wheel_end ends: the result
// that it gets, or -1
int wheel_dialog(HINSTANCE inst, LPCSTR name, HWND owner, DLGPROC proc);
void wheel_end(int result);

#endif /* WHEEL_H */
