#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "esshw.h"
#include "piix4.h"
#include "regs.h"

static int failed; // the exit code: 1 when something didn't work

// the range of each setter's value, 1 (and why) if arg is out of it
static int bad_value(const char *opt, const char *arg) {
  static const struct {
    const char *name;
    int lo, hi, pct;
  } range[] = {{"3l", 0, 1, 0},       {"3", 0, 63, 1},   {"ol", -1024, 960, 0},
               {"or", -1024, 960, 0}, {"micp", 0, 1, 0}, {"ml", 0, 15, 1},
               {"m", 0, 1, 0},        {"pa", 0, 1, 0},   {"fmd", 0, 1, 0},
               {"fms", 0, 1, 0},      {"fmr", 0, 1, 0},  {"t", 0, 1, 0},
               {"tf", 0, 1, 0},       {"x", 0, 1, 0}};
  unsigned n = (unsigned)(arg - opt - 1), i;
  char *end;
  long v;

  for (i = 0; i < sizeof(range) / sizeof(range[0]); i++)
    if (strlen(range[i].name) == n && !strncmp(opt, range[i].name, n))
      break;
  if (i == sizeof(range) / sizeof(range[0]))
    return 0; // r=path
  v = strtol(arg, &end, 10);
  if (end != arg && range[i].pct && end[0] == '%' && !end[1] && v >= 0 &&
      v <= 100)
    return 0;
  if (end != arg && !*end && v >= range[i].lo && v <= range[i].hi)
    return 0;
  printf("%.*s=%s: use %d to %d%s\n", (int)n, opt, arg, range[i].lo,
         range[i].hi, range[i].pct ? ", or 0% to 100%" : "");
  return 1;
}

// 1 in a DOS box of Windows 95/98 (enhanced mode)
static int in_windows(void) {
  union REGS r;

  r.w.ax = 0x1600;
  int86(0x2F, &r, &r);
  return r.h.al != 0 && r.h.al != 0x80;
}

// tf: the Intel PIIX4's Type F DMA timing (piix4.h), read and explained;
// tf=0 or tf=1 turns it off or on for Audio 1's channel (PnP 74h) only, and
// only at the DOS prompt: in a DOS box the configuration ports aren't this
// program's alone
static void type_f(int want) {
  static char line[80];
  u8 r[2], dev = piix4_find(&piix4_dos);
  int ch = -1, i, got;

  if (dev == PIIX4_NONE) {
    printf("Type F: no Intel PIIX4 on PCI bus 0\n");
    failed = 1;
    return;
  }
  if (esshw_detect_config() == 0)
    ch = esshw_pnp_read(1, 0x74);
  piix4_mbdma(&piix4_dos, dev, r);
  if (want >= 0) {
    if (in_windows()) {
      printf("tf=%d: at the DOS prompt only, outside Windows\n", want);
      failed = 1;
      return;
    }
    if (ch < 0 || ch > 3) {
      printf("tf=%d: Audio 1's DMA channel (PnP 74h) is unknown\n", want);
      failed = 1;
      return;
    }
    got = piix4_typef(&piix4_dos, dev, (u8)ch, want, r);
    if (got < 0) {
      printf("tf=%d: no MBDMA register names DMA %d, nothing changed\n", want,
             ch);
      failed = 1;
    }
  }
  piix4_text(line, dev, r);
  printf("%s\n", line);
  if (ch >= 0 && ch <= 3) {
    i = piix4_reg_of(r, (u8)ch);
    printf("Audio 1's DMA %d: %s\n", ch,
           i < 0                  ? "no MBDMA register names it"
           : (r[i] & PIIX4_TYPEF) ? "Type F on"
                                  : "Type F off");
  }
}

// clang-format off
static void print_opts() {
  printf("ES1869 Register Utility (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>\n");
  printf("\n");
  printf("|Option--------------|Description----------------------------------------\n");
  printf("| a                  | Get all values\n");
  printf("| r=[path]           | Dump ES1869 Registers        default \"essreg.txt\"\n");
  printf("| c                  | Calibrate Op Amp\n");
  printf("| 3=[0,63; 0%%,100%%]  | 3D Amount                    Get / Set\n");
  printf("| 3l=[1,0]           | 3D Limit (undocumented)      Enable / Disable\n");
  printf("| ol=[-1024,960]     | ADC Offset Samples Left      Get / Set\n");
  printf("| or=[-1024,960]     | ADC Offset Samples Right     Get / Set\n");
  printf("| a1s                | Audio 1 Sample Rate          Get\n");
  printf("| a1f                | Audio 1 Filter Clock         Get\n");
  printf("| a2s                | Audio 2 Sample Rate          Get\n");
  printf("| a2f                | Audio 2 Filter Clock         Get\n");
  printf("| pa=[1,0]           | Analog Stays On              Enable / Disable\n");
  printf("| pd                 | Digital Power Down           Get\n");
  printf("| m=[1,0]            | Mono-In direct to output     Enable / Disable\n");
  printf("| ml=[0,15; 0%%,100%%] | Mono-In Mixer Volume         Get / Set \n");
  printf("| micp=[1,0]         | Mic Preamp                   Enable / Disable\n");
  printf("| fmd=[1,0]          | FM,IIS,ES689 digital record  Enable / Disable\n");
  printf("| fms=[1,0]          | FM,IIS,ES689 digital sync    Enable / Disable\n");
  printf("| fmr=[1,0]          | FM Reset                     Execute\n");
  printf("| t=[1,0]            | Telegaming Mode              Enable / Disable\n");
  printf("| tf=[1,0]           | PIIX4 Type F, Audio 1's DMA  Enable / Disable\n");
  printf("| x=[1,0]            | Safe DSP protocol (C6h)      Enable / Disable\n\n");
  printf("Example: `essreg r=before.txt 3=0 m=1 pa=1 t=0 r=after.txt`");
}
// clang-format on

int main(int argc, char *argv[]) {
  int i;
  char *arg_data;

  if (argc == 1)
    print_opts();

  // in a DOS box the chip only answers while no Windows program has the
  // sound device: without its ID every value would read FFh. tf alone
  // reads the PCI bridge, which answers anyway
  if (argc > 1 && in_windows() && !(argc == 2 && !strcmp(argv[1], "tf"))) {
    unsigned char id[4];
    esshw_mixer_id(id);
    if (id[0] != 0x18 || id[1] != 0x69) {
      printf("No ES1869 answers at %03Xh.  In a Windows DOS box it only does "
             "while no\nWindows program uses the sound device: close it, or "
             "use essctl in Windows.\n",
             esshw.audio_base);
      return 1;
    }
  }

  for (i = 1; i < argc; i++) {
    arg_data = strstr(argv[i], "=");

    // clang-format off
    if (arg_data == NULL) {
      // getter
      if (argv[i][0] == '3') {
        if (argv[i][1] == 'l') get_3d_limit();
        else {
          get_3d_mode();
          get_3d_level();
        }
      }
	  else if (argv[i][0] == 'o') {
	    if (argv[i][1] == 'l') get_adc_offset_left();
	    else if (argv[i][1] == 'r') get_adc_offset_right();
	  }
	  else if (argv[i][0] == 'm') {
        if (argv[i][1] == 0) get_mono_in();
        else if (argv[i][1] == 'i' && argv[i][2] == 'c' && argv[i][3] == 'p') get_mic_preamp();
        else if (argv[i][1] == 'l') get_mono_in_level();
      }
	  else if (argv[i][0] == 'p') {
	    if (argv[i][1] == 'a') get_analog_stays_on();
	    else if (argv[i][1] == 'd') get_digital_power_down();
	  }
	  else if (argv[i][0] == 'c')
        calibrate_op_amp();
      else if (argv[i][0] == 'r') {
        if (dump_regs("essreg.txt")) failed = 1;
      }
      else if (argv[i][0] == 'f' && argv[i][1] == 'm') {
	    if (argv[i][2] == 'd') get_digital_record();
	    else if (argv[i][2] == 's') get_fm_sync_audio_2();
	    else if (argv[i][2] == 'r') get_fm_reset();
	  }
      else if (argv[i][0] == 't' && argv[i][1] == 'f')
        type_f(-1);
      else if (argv[i][0] == 't')
        get_telegaming_mode();
      else if (argv[i][0] == 'a') {
	    if (argv[i][1] == '1') {
		  if (argv[i][2] == 's') get_audio_1_sample_rate();
		  else if (argv[i][2] == 'f') get_audio_1_filter_rate();
		}
	    else if (argv[i][1] == '2') {
		  if (argv[i][2] == 's') get_audio_2_sample_rate();
		  else if (argv[i][2] == 'f') get_audio_2_filter_rate();
		}
		else if (argv[i][1] == 0) {
          get_3d_mode();
          get_3d_level();
          get_3d_limit();
          get_mono_in();
          get_mono_in_level();
          get_adc_offset_left();
          get_adc_offset_right();
          get_audio_1_sample_rate();
          get_audio_1_filter_rate();
          get_audio_2_sample_rate();
          get_audio_2_filter_rate();
		  get_digital_power_down();
		  get_analog_stays_on();
          get_mic_preamp();
          get_digital_record();
          get_fm_sync_audio_2();
		  get_fm_reset();
          get_telegaming_mode();
        }
      }
    } else {
      // setter
      arg_data += 1;
      if (bad_value(argv[i], arg_data)) {
        failed = 1;
        continue;
      }

      if (argv[i][0] == '3') {
        if (argv[i][1] == 'l') set_3d_limit(atoi(arg_data));
        else if (strstr(arg_data, "%")) set_3d_level_pct(atoi(arg_data));
        else set_3d_level(atoi(arg_data));
      }
	  else if (argv[i][0] == 'o') {
	    if (argv[i][1] == 'l') set_adc_offset_left(atoi(arg_data));
	    else if (argv[i][1] == 'r') set_adc_offset_right(atoi(arg_data));
	  }
	  else if (argv[i][0] == 'm') {
	    if (argv[i][1] == '=') set_mono_in(atoi(arg_data));
        else if (argv[i][1] == 'i' && argv[i][2] == 'c' && argv[i][3] == 'p') set_mic_preamp(atoi(arg_data));
		else if (argv[i][1] == 'l') {
          if (strstr(arg_data, "%")) set_mono_in_level_pct(atoi(arg_data));
          else set_mono_in_level(atoi(arg_data));
        }
      }
	  else if (argv[i][0] == 'p') {
	    if (argv[i][1] == 'a') set_analog_stays_on(atoi(arg_data));
	  }
	  else if (argv[i][0] == 'r') {
        if (dump_regs(arg_data)) failed = 1;
      }
      else if (argv[i][0] == 'f' && argv[i][1] == 'm') {
	    if (argv[i][2] == 'd') set_digital_record(atoi(arg_data));
	    else if (argv[i][2] == 's') set_fm_sync_audio_2(atoi(arg_data));
	    else if (argv[i][2] == 'r') set_fm_reset(atoi(arg_data));
	  }
      else if (argv[i][0] == 't' && argv[i][1] == 'f') type_f(atoi(arg_data));
      else if (argv[i][0] == 't') set_telegaming_mode(atoi(arg_data));
      else if (argv[i][0] == 'x') set_safe_protocol(atoi(arg_data));
    }
    // clang-format on
  }

  return failed;
}
