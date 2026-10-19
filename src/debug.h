#ifndef DEBUG_H
#define DEBUG_H

#include "regs.h"

void print_reg(unsigned int reg_addr, unsigned char reg_value);
// 0, or 1 if the file couldn't be written
int dump_regs(const char *path);

#endif /* DEBUG_H */
