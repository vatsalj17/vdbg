#include "commands.h"

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
    {"continue", cmd_continue, true, false, "Resume execution"},
    {"delete", cmd_delete, false, false, "Delete specific breakpoint or all of them if not specified"},
    {"disable", cmd_disable, true, false, "Disable any breakpoint"},
    {"exit", cmd_exit, false, false, "Exit the debugger"},
    {"enable", cmd_enable, true, false, "Enable any breakpoint"},
    {"finish", cmd_finish, true, true, "Skip the current function"},
    {"help", cmd_help, false, false, "Show this menu"},
    {"header", cmd_header, false, false, "Print ELF header"},
    {"memory", cmd_mem, true, false, "Manipulate memory at address"},
    {"next", cmd_next, true, true, "Step over current instruction"},
    {"run", cmd_run, false, false, "Start tracee"},
    {"restart", cmd_restart, true, false, "Restart tracee"},
    {"register", cmd_reg, true, false, "Manage CPU registers"},
    {"step", cmd_step, true, true, "Single step throught source code"},
    {"stepi", cmd_stepi, true, false, "Single step through instructions"},
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
	print_source_at_current_pc(dbg_get_symbols(dbg), get_pc(dbg_get_pid(dbg)));
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

// TODO: proper argument validation (but after adding source level breakpoints)

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
	} else if ((lineno = strchr(arg, ':'))) {
        CRITICAL("UNIMPLEMENTED");
        int line = atoi(lineno + 1);
        lineno[0] = '\0';
        get_addr_from_lineno(dbg_get_symbols(dbg), arg, line);
    } else {
        set_breakpoint_at_func_symbol(dbg, arg);
    }
}

void cmd_delete(debugger_t *dbg, char **args) {
    if (!args[1]) {
        remove_all_breakpoints(dbg);
        return;
    }
	uintptr_t addr = strtoul(args[1], NULL, 16);
	unset_breakpoint_at_addr(dbg, addr);
}

void cmd_enable(debugger_t *dbg, char **args) {
	uintptr_t addr = strtoul(args[1], NULL, 16);
	addr += dbg_get_load_address(dbg);
	enable_breakpoint(dbg, addr);
}

void cmd_disable(debugger_t *dbg, char **args) {
	uintptr_t addr = strtoul(args[1], NULL, 16);
	addr += dbg_get_load_address(dbg);
	disable_breakpoint(dbg, addr);
}

void cmd_sections(debugger_t *dbg, char **args) {
	print_section_headers(dbg_get_symbols(dbg), args[1]);
}

void cmd_symbols(debugger_t *dbg, char **args) {
	print_symbols_table(dbg_get_symbols(dbg), args[1]);
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
