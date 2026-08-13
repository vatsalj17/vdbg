#include "stepping.h"

#include <sys/ptrace.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "debugger.h"
#include "macro.h"
#include "breakpoint.h"
#include "hashmap.h"
#include "registers.h"
#include "util.h"

static void single_step_instruction(debugger_t *dbg) {
	DBG_LOG("single stepping instruction");
	// DBG_LOG("pc before single stepping: 0x%lx", get_pc(dbg_get_pid(dbg)));
	ptrace(PTRACE_SINGLESTEP, dbg_get_pid(dbg), NULL, NULL);
	// DBG_LOG("pc after single stepping: 0x%lx", get_pc(dbg_get_pid(dbg)));
	wait_for_signal(dbg);
}

static void execute_step_over_bp(debugger_t *dbg, breakpoint_t *bp) {
	// if not null means we have currently hitted the breakpoint_t
	if (bp == NULL) return;
	// else
	// 	printf("found bp\n");

	if (bp_is_enabled(bp)) {
		bp_disable(bp);
		// single step forward to jump through the breakpoint_t instruction
		single_step_instruction(dbg);
		bp_enable(bp);
	}
}

void step_over_breakpoint(debugger_t *dbg) {
	breakpoint_t *bp = map_lookup(dbg_get_breakpoints(dbg), get_pc(dbg_get_pid(dbg)));
	execute_step_over_bp(dbg, bp);
}

void single_step_instruction_with_breakpoint_check(debugger_t *dbg) {
	breakpoint_t *bp = map_lookup(dbg_get_breakpoints(dbg), get_pc(dbg_get_pid(dbg)));
	if (bp != NULL) {
		DBG_LOG("bp found here now stepping over it");
		execute_step_over_bp(dbg, bp);
	} else {
		single_step_instruction(dbg);
	}
}

// the finish instruction
void step_out(debugger_t *dbg) {
	// getting the base pointer of the current stackframe from the data stored in rbp
	uint64_t frame_pointer = get_register_value(rbp, dbg_get_pid(dbg));

	// fetching return address from the value just above it
	uint64_t return_address = read_memory(dbg_get_pid(dbg), frame_pointer + 8);
	DBG_LOG("got return_address: 0x%lx", return_address);

	bool should_remove_breakpoint = false;
	// adding a temp breakpoint_t
	if (set_temp_breakpoint(dbg, return_address)) {
		should_remove_breakpoint = true;
	}

	continue_execution(dbg);

	// if added that temp bp then remove it
	if (should_remove_breakpoint) {
		unset_temp_breakpoint(dbg, return_address);
	}
}

void step_in(debugger_t *dbg) {
	const char *file;
	int next_line;
	int line = get_line_from_pc(dbg_get_symbols(dbg), get_pc(dbg_get_pid(dbg)), &file);
	if (line == 0) {
		CRITICAL("Something is wrong");
	}

	// loop until we are on the same line
	while ((next_line = get_line_from_pc(dbg_get_symbols(dbg), get_pc(dbg_get_pid(dbg)), &file)) == line) {
		single_step_instruction_with_breakpoint_check(dbg);
	}

	if (has_dwarf_symbols(dbg_get_symbols(dbg))) print_source(file, (unsigned)next_line, 3);
}

// the next instruction
void step_over(debugger_t *dbg) {
    uintptr_t load_address = dbg_get_load_address(dbg);
	uintptr_t pc = get_pc(dbg_get_pid(dbg));
	Dwarf_Die func;
	get_func_die_from_pc(dbg_get_symbols(dbg), pc, &func, load_address);

	Dwarf_Addr func_entry, func_end;
	const char *func_name = dwarf_diename(&func);
	DBG_LOG("diename: %s", func_name);
	dwarf_lowpc(&func, &func_entry);
	dwarf_highpc(&func, &func_end);
	DBG_LOG("func_entry: %lx, func_end: %lx", func_entry, func_end);
	func_entry += load_address;
	func_end += load_address;

	size_t to_delete_cap = 100;
	uintptr_t *to_delete = malloc(to_delete_cap * sizeof(uintptr_t));
	size_t to_delete_size = 0;

	Dwfl_Module *mod = dwfl_addrmodule(get_dwarf_data(dbg_get_symbols(dbg)), pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwfl_Line *src = dwfl_module_getsrc(mod, pc);
	DWFL_SANITY_CHECK(src, "dwfl_module_getsrc");

	// setting breakpoints on all the lines from current to the end of the function
	int current_line = 0, line = 0;
	dwfl_lineinfo(src, NULL, &current_line, NULL, NULL, NULL);
	assert(current_line != 0);

	while (pc < func_end) {
		pc++; // TODO: now this is a problem (checking every byte is just so bad)
		      //       i have to figure out the correct way
		src = dwfl_module_getsrc(mod, pc);
		dwfl_lineinfo(src, NULL, &line, NULL, NULL, NULL);
		// printf("line %d, current_line %d, pc: %lx\n", line, current_line, pc);
		if (line != current_line) {
			if (set_temp_breakpoint(dbg, pc)) {
				to_delete[to_delete_size++] = pc;
				if (to_delete_size >= to_delete_cap - 1) {
					to_delete_cap *= 2;
					to_delete = realloc(to_delete, to_delete_cap * sizeof(uintptr_t));
				}
			}
			// print_source(file, (uint32_t)current_line, 1);
			current_line = line;
		}
	}

	// ignoring to set breakpoint_t on return address of main so that it doesn't
	// set bp on libc, and try to print the source
	if (strcmp(func_name, "main") != 0) {
		// setting breakpoint_t at return address
		uint64_t frame_pointer = get_register_value(rbp, dbg_get_pid(dbg));
		uint64_t return_address = read_memory(dbg_get_pid(dbg), frame_pointer + 8);
		if (set_temp_breakpoint(dbg, return_address)) {
			to_delete[to_delete_size++] = return_address;
		}
	}
	// DBG_LOG("to_delete_size: %zu", to_delete_size);

	continue_execution(dbg);

	DBG_LOG("cleaning up all the temp breakpoints");
	for (size_t i = 0; i < to_delete_size; i++) {
		unset_temp_breakpoint(dbg, to_delete[i]);
	}
	free(to_delete);
}
