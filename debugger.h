#ifndef DBG_H
#define DBG_H

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#define MAP_SIZE 1024

typedef struct DBG debugger;

debugger* dbg_init(const char* pname, pid_t pid);
void dbg_start(debugger* dbg);
pid_t dbg_get_pid(debugger* dbg);
bool dbg_kill_tracee(debugger *dbg);
void dbg_free(debugger* dbg);

void set_breakpoint_at_addr(debugger* dbg, uintptr_t addr);
long read_memory(debugger* dbg, uintptr_t addr);
void write_memory(debugger* dbg, uintptr_t addr, uintptr_t value);
void wait_for_signal(debugger* dbg);
void step_over_breakpoint(debugger* dbg);
void continue_execution(debugger* dbg);

#endif
