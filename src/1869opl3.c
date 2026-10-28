/*
 * 1869opl3 is a program to allow OPL2 / OPL3 FM
 * playback with the ES1869 drivers in a Windows
 * dos box.
 *
 * Notes:
 *
 * The FM synthesizer volume may be mapped to the IIS
 * volume mixer in Windows. The mixer volume for IIS may
 * set to zero, but must not be muted.
 *
 * Audio_Base comes from the BLASTER variable (A220), 220h
 * without it. The exit code is the command's, or 1 if it
 * couldn't run.
 *
 * Usage:
 *   `1869opl3 "c:\path\to\game.exe /arg1 /arg2"`
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <conio.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>

// Audio_Base from BLASTER, like the A220 of "A220 I5 D1 T6"
static int blaster_base(void) {
  const char *b = getenv("BLASTER"), *p;
  unsigned long v;
  char *end;

  for (p = b; p && *p; p++)
    if ((*p == 'A' || *p == 'a') && (p == b || p[-1] == ' ')) {
      v = strtoul(p + 1, &end, 16);
      if (end > p + 1 && v >= 0x100 && v <= 0x3F0 && !(v & 0x0F))
        return (int)v;
    }
  return 0x220;
}

int main(int argc, char **argv) {
  int audio_base = blaster_base();
  char application_path[256];
  unsigned char prev_val;
  int rc;

  (void)argc;
  (void)argv;
  getcmd(application_path);
  if (!application_path[0]) {
    printf("Usage: 1869opl3 \"c:\\path\\to\\game.exe /arg1 /arg2\"\n");
    return 1;
  }

  // set mixer register to full FM volume
  outp(audio_base + 0x04, 0x36);
  prev_val = inp(audio_base + 0x05);
  outp(audio_base + 0x05, 0xff);

  // execute the command supplied from the args
  rc = system(application_path);

  // reset FM volume to previous value
  outp(audio_base + 0x04, 0x36);
  outp(audio_base + 0x05, prev_val);
  return rc == -1 ? 1 : rc;
}
