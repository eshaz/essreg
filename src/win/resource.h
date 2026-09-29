/*
 * Resource IDs of essctl.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef RESOURCE_H
#define RESOURCE_H

/* main dialog */
#define IDC_PAGES 100    /* category list */
#define IDC_PAGEAREA 101 /* invisible frame that marks the page area */
#define IDC_PGSCROLL 105 /* vertical scroll bar of the page area */
#define IDC_HELPTEXT 102
#define IDC_STATUS 103
#define IDC_OWNERS 104

/* menu commands */
#define IDM_LOAD 200
#define IDM_SAVE 201
#define IDM_DUMP 202
#define IDM_EXIT 203
#define IDM_REFRESH 210
#define IDM_READDSP 211
#define IDM_AUTOREFRESH 212
#define IDM_EXPERT 220
#define IDM_FMDAC 221
#define IDM_ESFM_LOAD 230
#define IDM_ESFM_RESTORE 231
#define IDM_ABOUT 240

/* bit editor dialog */
#define IDC_BE_NAME 300
#define IDC_BE_HEX 301
#define IDC_BE_BIT0 310 /* bit n is IDC_BE_BIT0 + n */
#define IDC_BE_BIT1 311
#define IDC_BE_BIT2 312
#define IDC_BE_BIT3 313
#define IDC_BE_BIT4 314
#define IDC_BE_BIT5 315
#define IDC_BE_BIT6 316
#define IDC_BE_BIT7 317
#define IDC_BE_FIELDS 320
#define IDC_BE_SLIDER 321

/* controls created at run time in the page area */
#define IDC_ROW 1000 /* IDC_ROW + ROW_IDS * row + column */
#define ROW_IDS 5    /* label, control, value, tier, text field */
#define IDC_PG_EDIT 900
#define IDC_PG_BANK 901
#define IDC_PG_LIST 902
#define IDC_PG_READ 903
#define IDC_PG_EDITREG 904
#define IDC_PG_BTN1 905
#define IDC_PG_BTN2 906
#define IDC_PG_TEXT 907
#define IDC_PG_BTN3 908
#define IDC_PG_VOICES 909

#endif /* RESOURCE_H */
