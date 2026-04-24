#ifndef DBG_H
#define DBG_H

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct DBG debugger;

typedef enum Debugger_State {
    ACTIVE,
    NOT_ACTIVE
} state;

debugger* dbg_init(const char* pname);
void dbg_start(debugger* dbg);
pid_t dbg_get_pid(debugger* dbg);
bool dbg_is_active(debugger *dbg);
bool dbg_kill_tracee(debugger *dbg);
void dbg_free(debugger* dbg);

void run(debugger *dbg);
void restart(debugger *dbg);
void add_arguments_for_tracee(debugger *dbg, char **args);
void set_breakpoint_at_addr(debugger* dbg, uintptr_t addr);
void unset_breakpoint_at_addr(debugger *dbg, uintptr_t addr);
void enable_breakpoint(debugger *dbg, uintptr_t addr);
void disable_breakpoint(debugger *dbg, uintptr_t addr);
void resolve_pending_breakpoints(debugger *dbg) ;
void wait_for_signal(debugger* dbg);
void step_over_breakpoint(debugger* dbg);
void continue_execution(debugger* dbg);
void disable_all_breakpoints(debugger *dbg);
void cleanup_at_tracee_death(debugger *dbg);
void remove_all_breakpoints(debugger *dbg);

#endif
