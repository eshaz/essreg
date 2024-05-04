#include "debug.h"
#include "regs.h"
#include <stdio.h>
#include <string.h>

static void print_opts() {
	printf("ES1869 Register Utility\n");
	printf("(c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>\n");
	printf("\n");
	printf("|Option------|Description----------------------------------------------\n");
	printf("| c					 | (Calibrate Op Amp)\n");
	printf("| r=[path]	 | (Dump ES1869 Registers)\n");
	printf("|						 |	 * `path` to save the registers; defaults to essreg.txt\n");
	printf("| g					 | (Get all values)\n");
	printf("| 3=[value]  | (Get / Set 3D Amount)\n");
	printf("|						 |	 * 0				 3D disable\n");
	printf("|						 |	 * 1	- 63	 3D amount (raw value)\n");
	printf("|						 |	 * 1%% - 100%% 3D amount (percent)\n");
	printf("| ml=[value] | (Get / Set Mono-In Level)\n");
	printf("|						 |	 * 0	- 15	 Mono-In Level (raw value)\n");
	printf("|						 |	 * 0%% - 100%% Mono-In Level (percent)\n");
	printf("| m=[1,0]		 | (Enable / Disable Mono-In)\n");
	printf("| p=[1,0]		 | (Enable / Disable Mic Preamp)\n");
	printf("| d=[1,0]		 | (Enable / Disable FM,IIS,ES689 digital record)\n");
	printf("| t=[1,0]		 | (Enable / Disable Telegaming Mode)\n\n");
	printf("`3`, `ml`, `m`, `p`, `d`, `t`\n");
	printf("	* Omitting the parameter will show the current value.\n\n");
	printf("Example: `essreg r=before.txt 3=0 m=1 p=1 r=0 t=0 r=after.txt`");
}

int main(int argc, char *argv[]) {
	int i;
	char* arg_data;
	
	if (argc == 1) print_opts();
	
	for (i = 1; i < argc; i++) {
		arg_data = strstr(argv[i], "=");
		if (arg_data == NULL) {
	    // getter
		  if (argv[i][0] == '3') {
				get_3d_mode();
				get_3d_level();
		  }
		  else if (argv[i][0] == 'm') {
				if (argv[i][1] == 'l') get_mono_in_level();
			  else if (argv[i][1] == 0) get_mono_in();
		  }
		  else if (argv[i][0] == 'c') calibrate_op_amp();
		  else if (argv[i][0] == 'r') dump_regs("essreg.txt");
		  else if (argv[i][0] == 'p') get_mic_preamp();
		  else if (argv[i][0] == 'd') get_digital_record();
		  else if (argv[i][0] == 't') get_telegaming_mode();
		  else if (argv[i][0] == 'g') {
			  get_3d_mode();
		    get_3d_level();
			  get_mono_in();
			  get_mono_in_level();
			  get_mic_preamp();
		    get_digital_record();
		    get_telegaming_mode();
		  }
	  } else {
		  // setter
		  arg_data += 1;

		  if (argv[i][0] == '3') {
			  if (strstr(arg_data, "%")) set_3d_level_pct(atoi(arg_data));
		    else set_3d_level(atoi(arg_data));
		  }
		  else if (argv[i][0] == 'm') {
			  if (argv[i][1] == 'l') {
			    if (strstr(arg_data, "%")) set_mono_in_level_pct(atoi(arg_data));
			    else set_mono_in_level(atoi(arg_data));
		    }
		    else if (argv[i][1] == '=') set_mono_in(atoi(arg_data));
		  }
		  else if (argv[i][0] == 'r') dump_regs(arg_data);
		  else if (argv[i][0] == 'p') set_mic_preamp(atoi(arg_data));
		  else if (argv[i][0] == 'd') set_digital_record(atoi(arg_data));
		  else if (argv[i][0] == 't') set_telegaming_mode(atoi(arg_data));
	  }
	}
	
	return 0;
}
