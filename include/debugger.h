#ifndef DBG_H
#define DBG_H

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#include "symbols.h"
#include "hashmap.h"

typedef struct Debugger debugger_t;

typedef enum Debugger_State { ACTIVE, NOT_ACTIVE } dbg_state;

debugger_t *dbg_init(const char *pname);
void dbg_start(debugger_t *dbg);
pid_t dbg_get_pid(debugger_t *dbg);
uintptr_t dbg_get_load_address(debugger_t *dbg);
map_t *dbg_get_breakpoints(debugger_t *dbg);
bool dbg_is_active(debugger_t *dbg);
dbg_symbols *dbg_get_symbols(debugger_t *dbg);
bool dbg_kill_tracee(debugger_t *dbg);
void dbg_free(debugger_t *dbg);

void set_breakpoint_at_addr(debugger_t *dbg, uintptr_t addr, bool quiet);
void unset_breakpoint_at_addr(debugger_t *dbg, uintptr_t addr);
void set_breakpoint_at_func_symbol(debugger_t *dbg, const char *symbol_name);
void set_breakpoint_at_lineno(debugger_t *dbg, const char *filename, int lineno);

void enable_breakpoint(debugger_t *dbg, uintptr_t addr);
void disable_breakpoint(debugger_t *dbg, uintptr_t addr);
void disable_all_breakpoints(debugger_t *dbg);
void remove_all_breakpoints(debugger_t *dbg);
bool set_temp_breakpoint(debugger_t *dbg, uintptr_t running_addr);
void unset_temp_breakpoint(debugger_t *dbg, uintptr_t running_addr);

void wait_for_signal(debugger_t *dbg);
void run(debugger_t *dbg);
void restart(debugger_t *dbg);
void add_arguments_for_tracee(debugger_t *dbg, char **args);
void continue_execution(debugger_t *dbg);

#endif
