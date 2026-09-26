#ifndef DEBUG_H
#define DEBUG_H

#include "regs.h"

void print_reg(unsigned int reg_addr, unsigned char reg_value);
void dump_regs(const char *path);

#endif /* DEBUG_H */
