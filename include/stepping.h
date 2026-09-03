#ifndef STEPPING_H
#define STEPPING_H

#include "debugger.h"

void step_over_breakpoint(debugger_t *dbg);
void step_in(debugger_t *dbg);
void step_out(debugger_t *dbg);
void step_over(debugger_t *dbg);
void single_step_instruction_with_breakpoint_check(debugger_t *dbg);
void print_backtrace(debugger_t *dbg);

#endif
