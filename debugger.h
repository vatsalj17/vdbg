#ifndef DBG_H
#define DBG_H

#include <sys/types.h>

#define BREAKPOINTS_SIZE 1024

typedef struct DBG debugger;

debugger* dbg_init(char* pname, pid_t pid);
void dbg_run(debugger* dbg);
pid_t dbg_get_pid(debugger* dbg);
void dbg_free(debugger* dbg);

void set_breakpoint_at_addr(debugger* dbg, long addr);
long read_memory(debugger* dbg, long addr);
void write_memory(debugger* dbg, long addr, long value);
long get_pc(debugger* dbg);
void set_pc(debugger* dbg, long value);
void wait_for_signal(debugger* dbg);
void step_over_breakpoint(debugger* dbg);
void continue_execution(debugger* dbg);

#endif
