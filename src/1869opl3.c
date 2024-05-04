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
 * Usage:
 *	 `1869opl3 "c:\path\to\game.exe /arg1 /arg2"`
 * 
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * 
 * Licensed under GPL Version 3.0
 */

#include <conio.h>
#include <process.h>

int main(int argc, char **argv) {
	int audio_base = 0x220;
	char application_path[256];
	unsigned char prev_val;
	
	getcmd(application_path);
	
	// set mixer register to full FM volume
	outp(audio_base + 0x04, 0x36);
	prev_val = inp(audio_base + 0x05);
	outp(audio_base + 0x05, 0xff);
	
	// execute the command supplied from the args
	system(application_path);
	
	// reset FM volume to previous value
	outp(audio_base + 0x04, 0x36);
	outp(audio_base + 0x05, prev_val);
	return 0;
}
