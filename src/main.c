#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "regs.h"

// clang-format off
static void print_opts() {
  printf("ES1869 Register Utility (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>\n");
  printf("\n");
  printf("|Option--------------|Description----------------------------------------\n");
  printf("| a                  | Get all values\n");
  printf("| r=[path]           | Dump ES1869 Registers        default \"essreg.txt\"\n");
  printf("| c                  | Calibrate Op Amp\n");
  printf("| 3=[0,63; 0%%,100%%]  | 3D Amount                    Get / Set\n");
  printf("| ol=[-1024,960]     | ADC Offset Samples Left      Get / Set\n");
  printf("| or=[-1024,960]     | ADC Offset Wamples Right     Get / Set\n");
  printf("| a1s                | Audio 1 Sample Rate          Get\n");
  printf("| a1f                | Audio 1 Filter Clock         Get\n");
  printf("| a2s                | Audio 2 Sample Rate          Get\n");
  printf("| a2f                | Audio 2 Filter Clock         Get\n");
  printf("| pa=[1,0]           | Analog Stays On              Enable / Disable\n");
  printf("| pd                 | Digital Power Down           Get\n");
  printf("| m=[1,0]            | Mono-In                      Enable / Disable\n");
  printf("| ml=[0,25; 0%%,100%%] | Mono-In Level                Get / Set \n");
  printf("| micp=[1,0]         | Mic Preamp                   Enable / Disable\n");
  printf("| fmd=[1,0]          | FM,IIS,ES689 digital record  Enable / Disable\n");
  printf("| fms=[1,0]          | FM,IIS,ES689 digital sync    Enable / Disable\n");
  printf("| fmr=[1,0]          | FM Reset                     Execute\n");
  printf("| t=[1,0]            | Telegaming Mode              Enable / Disable\n\n");
  printf("Example: `essreg r=before.txt 3=0 m=1 p=1 r=0 t=0 r=after.txt`");
}
// clang-format on

int main(int argc, char *argv[]) {
  int i;
  char *arg_data;

  if (argc == 1)
    print_opts();

  for (i = 1; i < argc; i++) {
    arg_data = strstr(argv[i], "=");

    // clang-format off
    if (arg_data == NULL) {
      // getter
      if (argv[i][0] == '3') {
        get_3d_mode();
        get_3d_level();
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
      else if (argv[i][0] == 'r')
        dump_regs("essreg.txt");
      else if (argv[i][0] == 'f' && argv[1][1] == 'm') {
	    if (argv[i][2] == 'd') get_digital_record();
	    else if (argv[i][2] == 's') get_fm_sync_audio_2();
	    else if (argv[i][2] == 'r') get_fm_reset();
	  }
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

      if (argv[i][0] == '3') {
        if (strstr(arg_data, "%")) set_3d_level_pct(atoi(arg_data));
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
	  else if (argv[i][0] == 'r')
        dump_regs(arg_data);
      else if (argv[i][0] == 'f' && argv[1][1] == 'm') {
	    if (argv[i][2] == 'd') set_digital_record(atoi(arg_data));
	    else if (argv[i][2] == 's') set_fm_sync_audio_2(atoi(arg_data));
	    else if (argv[i][2] == 'r') set_fm_reset(atoi(arg_data));
	  }
      else if (argv[i][0] == 't') set_telegaming_mode(atoi(arg_data));
    }
    // clang-format on
  }

  return 0;
}
