#ifndef DBG_H
#define DBG_H

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct DBG debugger;

typedef enum Debugger_State {
    ACTIVE,
    NOT_ACTIVE
} dbg_state;

debugger* dbg_init(const char* pname);
void dbg_start(debugger* dbg);
pid_t dbg_get_pid(debugger* dbg);
uintptr_t dbg_get_load_address(debugger *dbg);
bool dbg_is_active(debugger *dbg);
bool dbg_kill_tracee(debugger *dbg);
void dbg_free(debugger* dbg);

uintptr_t offset_load_address(debugger *dbg, uintptr_t addr);

void set_breakpoint_at_addr(debugger* dbg, uintptr_t addr);
void unset_breakpoint_at_addr(debugger *dbg, uintptr_t addr);
void enable_breakpoint(debugger *dbg, uintptr_t addr);
void disable_breakpoint(debugger *dbg, uintptr_t addr);
void disable_all_breakpoints(debugger *dbg);
void single_step_instruction_with_breakpoint_check(debugger *dbg);
void remove_all_breakpoints(debugger *dbg);

void run(debugger *dbg);
void restart(debugger *dbg);
void print_source_at_pc(debugger *dbg);
void add_arguments_for_tracee(debugger *dbg, char **args);
void step_in(debugger *dbg);
void step_out(debugger *dbg);
void continue_execution(debugger* dbg);

#endif
