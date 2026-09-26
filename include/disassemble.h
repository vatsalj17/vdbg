#ifndef DISAS_H
#define DISAS_H

#include <stddef.h>
#include <stdint.h>

#include "debugger.h"

void print_disassembly_of_function(debugger_t *dbg, const char *function_name);

#endif
