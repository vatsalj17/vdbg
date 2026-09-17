#include "commands.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "registers.h"
#include "util.h"
#include "macro.h"
#include "symbols.h"
#include "stepping.h"

const command_entry commands[] = {
    {"arguments", cmd_arguments, false, false, "Pass arguments to the tracee"},
    {"break", cmd_break, false, false, "Set breakpoint"},
    {"backtrace", cmd_backtrace, true, false, "Pring backtrace"},
    {"continue", cmd_continue, true, false, "Resume execution"},
    {"delete", cmd_delete, false, false, "Delete specific breakpoint or all if not specified"},
    {"disable", cmd_disable, false, false, "Disable any breakpoint"},
    {"exit", cmd_exit, false, false, "Exit the debugger"},
    {"enable", cmd_enable, false, false, "Enable any breakpoint"},
    {"finish", cmd_finish, true, true, "Skip the current function"},
    {"functions", cmd_functions, false, false, "List all the functions"},
    {"help", cmd_help, false, false, "Show this menu"},
    {"header", cmd_header, false, false, "Print ELF header"},
    {"info", cmd_info, false, false, "List all the breakpoints"},
    {"ls", cmd_ls, false, false, "List current directory"},
    {"memory", cmd_mem, true, false, "Manipulate memory at address"},
    {"next", cmd_next, true, true, "Step over current instruction"},
    {"run", cmd_run, false, false, "Start tracee"},
    {"restart", cmd_restart, true, false, "Restart tracee"},
    {"register", cmd_reg, true, false, "Manage CPU registers"},
    {"step", cmd_step, true, true, "Single step throught source code"},
    {"stepi", cmd_stepi, true, false, "Single step through instructions"},
    {"source", cmd_source, true, true, "Print the source code at the current instruction"},
    {"sections", cmd_sections, false, false, "List all the matching section headers"},
    {"symbols", cmd_symbols, false, false, "List all the matching symbols"},
    {NULL, NULL, false, false, NULL},
};

void cmd_help(UNUSED debugger_t *dbg, UNUSED char **args) {
	printf("\n Commands:\n");
	for (int i = 0; commands[i].name != NULL; i++) {
		printf("    %-10s ->  %s\n", commands[i].name, commands[i].help_text);
	}
	printf("\n");
}

void cmd_ls(UNUSED debugger_t *dbg, UNUSED char **args) {
    list_current_dir();
}

void cmd_run(debugger_t *dbg, UNUSED char **args) {
	run(dbg);
}

void cmd_restart(debugger_t *dbg, UNUSED char **args) {
	restart(dbg);
}

void cmd_continue(debugger_t *dbg, UNUSED char **args) {
	continue_execution(dbg);
}

void cmd_stepi(debugger_t *dbg, UNUSED char **args) {
	single_step_instruction_with_breakpoint_check(dbg);
	print_source_at_current_pc(
	    dbg_get_symbols(dbg), get_pc(dbg_get_pid(dbg)), DEFAULT_LINE_CONTEXT);
}

void cmd_step(debugger_t *dbg, UNUSED char **args) {
	step_in(dbg);
}

void cmd_finish(debugger_t *dbg, UNUSED char **args) {
	step_out(dbg);
}

void cmd_next(debugger_t *dbg, UNUSED char **args) {
	step_over(dbg);
}

void cmd_header(debugger_t *dbg, UNUSED char **args) {
	print_elf_header(dbg_get_symbols(dbg));
}

void cmd_backtrace(debugger_t *dbg, UNUSED char **args) {
	print_backtrace(dbg);
}

void cmd_info(debugger_t *dbg, UNUSED char **args) {
    print_breakpoint_list(dbg_get_pending_list(dbg));
}

void cmd_exit(debugger_t *dbg, UNUSED char **args) {
	if (dbg_kill_tracee(dbg)) {
		printf("Exiting....\n");
		dbg_free(dbg);
		exit(0);
	} else {
		printf("You told not to exit, so let's continue..\n");
	}
}

void cmd_arguments(debugger_t *dbg, char **args) {
	// ignoring the command and passing the arguments
	add_arguments_for_tracee(dbg, args + 1);
}

void cmd_break(debugger_t *dbg, char **args) {
	if (!args[1]) {
		fprintf(stderr, BHRED "✗ " RESET "usage: break <address>\n");
		return;
	}
	char *arg = args[1];
	char *lineno;
	if (arg && arg[0] == '0' && arg[1] == 'x') {
		uintptr_t addr = strtoul(arg, NULL, 16);
		set_breakpoint_at_addr(dbg, addr, false);
	} else if (is_number(arg)) {
		if (!has_dwarf_symbols(dbg_get_symbols(dbg))) {
			printf("can't set breakpoint at line number\n");
			printf("the executable doesn't contain dwarf symbols");
			return;
		}
		set_breakpoint_at_lineno(dbg, NULL, atoi(arg));
	} else if ((lineno = strchr(arg, ':'))) {
		if (!has_dwarf_symbols(dbg_get_symbols(dbg))) {
			printf("can't set breakpoint at line number\n");
			printf("the executable doesn't contain dwarf symbols");
			return;
		}
		int line = atoi(lineno + 1);
		lineno[0] = '\0';
		set_breakpoint_at_lineno(dbg, arg, line);
	} else {
		set_breakpoint_at_func_symbol(dbg, arg);
	}
}

static breakpoint_t *parse_arg_bp(debugger_t *dbg, char *arg) {
	if (!arg) return NULL;
	char *lineno;
	if (is_number(arg)) {
		// at id
		uint64_t id = strtoul(arg, NULL, 10);
		breakpoint_t *bp = get_breakpoint_by_id(dbg, id);
		if (!bp) printf("no such bp found with id %lu\n", id);
		return bp;
	} else if (arg && arg[0] == '0' && arg[1] == 'x') {
		// at addr
		uintptr_t addr = strtoul(arg, NULL, 16);
		if (addr == ULONG_MAX) DBG_ERR("Invalid argument. give proper 0x<addr>");
		breakpoint_t *bp =
		    find_breakpoint_from_pending(dbg_get_pending_list(dbg), BP_ADDR, (void *)addr);
		if (!bp) printf("no such bp found with addr %#lx\n", addr);
		return bp;
	} else if ((lineno = strchr(arg, ':'))) {
		if (!has_dwarf_symbols(dbg_get_symbols(dbg))) {
			printf("what are you doing with line numbers when the executable doesn't even contain "
			       "dwarf symbols\n");
			return NULL;
		}
		// at file:line
		int line = atoi(lineno + 1);
		lineno[0] = '\0';
		char *file = arg;
		if (line == 0 || !file) DBG_ERR("Invalid argument. give proper file:lineno");
		file_line_pair pair = {
		    .file = file,
		    .lineno = line,
		};
		breakpoint_t *bp =
		    find_breakpoint_from_pending(dbg_get_pending_list(dbg), BP_LINENO, (void *)&pair);
		if (!bp) printf("no such bp found with file %s and line %d\n", file, line);
		return bp;
	} else {
		// at func_sym
		breakpoint_t *bp =
		    find_breakpoint_from_pending(dbg_get_pending_list(dbg), BP_SYMBOL, (void *)arg);
		if (!bp) printf("no such bp found with symbol %s\n", arg);
		return bp;
	}
}

void cmd_delete(debugger_t *dbg, char **args) {
	// find the breakpoint and then disable it's location
	// then delete it from the map and list
	if (!args[1]) {
		remove_all_breakpoints(dbg);
		return;
	}
	breakpoint_t *bp = parse_arg_bp(dbg, args[1]);
	if (!bp) return;
	remove_breakpoint(dbg, bp);
}

void cmd_enable(debugger_t *dbg, char **args) {
	if (!args[1]) {
		// enable all bp
		enable_all_breakpoints(dbg);
		return;
	}
	breakpoint_t *bp = parse_arg_bp(dbg, args[1]);
	if (!bp) return;
	bp_enable(bp);
}

void cmd_disable(debugger_t *dbg, char **args) {
	if (!args[1]) {
		// disable all bp
		disable_all_breakpoints(dbg);
		return;
	}
	breakpoint_t *bp = parse_arg_bp(dbg, args[1]);
	if (!bp) return;
	bp_disable(bp);
}

void cmd_source(debugger_t *dbg, char **args) {
	char *context = args[1];
	int lines_context = (context) ? atoi(context) : DEFAULT_LINE_CONTEXT;
	print_source_at_current_pc(
	    dbg_get_symbols(dbg), get_pc(dbg_get_pid(dbg)), (unsigned int)lines_context);
}

void cmd_sections(debugger_t *dbg, char **args) {
	print_section_headers(dbg_get_symbols(dbg), args[1]);
}

void cmd_symbols(debugger_t *dbg, char **args) {
	print_symbols_table(dbg_get_symbols(dbg), args[1]);
}

void cmd_functions(debugger_t *dbg, char **args) {
	list_all_functions(dbg_get_symbols(dbg), args[1]);
}

void cmd_reg(debugger_t *dbg, char **args) {
	if (is_prefix(args[1], "dump")) {
		dump_registers(dbg_get_pid(dbg));
	} else if (is_prefix(args[1], "read")) {
		printf("%#016lx\n", get_register_value(get_register_from_name(args[2]), dbg_get_pid(dbg)));
	} else if (is_prefix(args[1], "write")) {
		uintptr_t value = strtoul(args[3], NULL, 16);
		set_register_value(get_register_from_name(args[2]), dbg_get_pid(dbg), value);
	} else {
		fprintf(stderr, BHRED "✗ " RESET "register %s: invalid command\n", args[1]);
	}
}

void cmd_mem(debugger_t *dbg, char **args) {
	// TODO: add support for custom length memory read and write
	// and print the memory dump in hexdump style

	uintptr_t address = strtoul(args[2], NULL, 16);
	if (is_prefix(args[1], "read")) {
		printf("%#016lx\n", read_memory(dbg_get_pid(dbg), address));
	} else if (is_prefix(args[1], "write")) {
		uintptr_t value = strtoul(args[3], NULL, 16);
		write_memory(dbg_get_pid(dbg), address, value);
	} else {
		fprintf(stderr, BHRED "✗ " RESET "memory %s: invalid command\n", args[1]);
	}
}

void handle_command(debugger_t *dbg, char *input) {
	char **args = split(input, ' ');
	char *command = args[0];
	if (command == NULL) {
		free(args);
		return;
	}

	// check for exact matches of entered command
	for (int i = 0; commands[i].name != NULL; i++) {
		if (strcmp(command, commands[i].name) == 0) {
			if (!dbg_is_active(dbg) && commands[i].requires_running_pid) {
				fprintf(stderr,
				        BHRED "✗ " BCYN "%s:" RESET " this command requires running tracee\n",
				        commands[i].name);
				goto cleanup;
			}
			if (commands[i].requires_dwarf_symbols && !has_dwarf_symbols(dbg_get_symbols(dbg))) {
				fprintf(stderr,
				        BHRED "✗ " BCYN "%s:" RESET " this command requires dwarf symbols\n",
				        commands[i].name);
				goto cleanup;
			}
			commands[i].handler(dbg, args);
			goto cleanup;
		}
	}

	// if previous fails then check for prefix matches
	for (int i = 0; commands[i].name != NULL; i++) {
		if (is_prefix(command, commands[i].name)) {
			if (!dbg_is_active(dbg) && commands[i].requires_running_pid) {
				fprintf(stderr,
				        BHRED "✗ " BCYN "%s:" RESET " this command requires running tracee\n",
				        commands[i].name);
				goto cleanup;
			}
			if (commands[i].requires_dwarf_symbols && !has_dwarf_symbols(dbg_get_symbols(dbg))) {
				fprintf(stderr,
				        BHRED "✗ " BCYN "%s:" RESET " this command requires dwarf symbols\n",
				        commands[i].name);
				goto cleanup;
			}
			commands[i].handler(dbg, args);
			goto cleanup;
		}
	}
	fprintf(stderr, BHRED "✗ " BCYN "%s:" RESET " invalid command. Use help.\n", command);

cleanup:
	free(args);
}
