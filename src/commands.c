#include "commands.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "registers.h"
#include "util.h"
#include "debugger.h"
#include "colors.h"

typedef void (*cmd_handler)(debugger *dbg, char **args);

typedef struct {
	const char *name;
	cmd_handler handler;
	bool requires_running_pid;
	const char *help_text;
} command_entry;

static const command_entry commands[] = {
    {"run", cmd_run, false, "Start tracee"},
    {"break", cmd_break, false, "Set breakpoint"},
    {"continue", cmd_continue, true, "Resume execution"},
    {"register", cmd_reg, true, "Manage CPU registers"},
    {"memory", cmd_mem, true, "Manipulate memory at address"},
    {"exit", cmd_exit, false, "Exit the debugger"},
    {"help", cmd_help, false, "Show this menu"},
    {"delete", cmd_delete, false, "Delete a specific breakpoint"},
    {"enable", cmd_enable, true, "Enable any breakpoint"},
    {"disable", cmd_disable, true, "Disable any breakpoint"},
    {"clear", cmd_clear, false, "Clear all breakpoints"},
    {"restart", cmd_restart, true, "Restart tracee"},
    {"arguments", cmd_arguments, false, "Pass arguments to the tracee"},
    {NULL, NULL, false, NULL},
};

void cmd_help(debugger *dbg __attribute__((unused)), char **args __attribute__((unused))) {
	printf("\n Commands:\n");
	for (int i = 0; commands[i].name != NULL; i++) {
		printf("    %-10s ->  %s\n", commands[i].name, commands[i].help_text);
	}
	printf("\n");
}

void cmd_clear(debugger *dbg, char **args __attribute__((unused))) {
	remove_all_breakpoints(dbg);
}

// TODO: add argument support

void cmd_run(debugger *dbg, char **args __attribute__((unused))) {
	run(dbg);
}

void cmd_restart(debugger *dbg, char **args __attribute__((unused))) {
	restart(dbg);
}

void cmd_continue(debugger *dbg, char **args __attribute__((unused))) {
	continue_execution(dbg);
}

void cmd_exit(debugger *dbg, char **args __attribute__((unused))) {
	if (dbg_kill_tracee(dbg)) {
		printf("Exiting....\n");
		dbg_free(dbg);
		exit(0);
	} else {
		printf("You told not to exit, so let's continue..\n");
	}
}

void cmd_arguments(debugger *dbg, char **args) {
    // ignoring the command and passing the arguments
    add_arguments_for_tracee(dbg, args + 1);
}

// TODO: proper argument validation (but after adding dwarf support)

void cmd_break(debugger *dbg, char **args) {
	if (!args[1]) {
		fprintf(stderr, BHRED "✗ " reset "usage: break <address>\n");
		return;
	}
	uintptr_t addr = strtoul(args[1], NULL, 16);
	set_breakpoint_at_addr(dbg, addr);
}

void cmd_delete(debugger *dbg, char **args) {
	uintptr_t addr = strtoul(args[1], NULL, 16);
	unset_breakpoint_at_addr(dbg, addr);
}

void cmd_enable(debugger *dbg, char **args) {
	uintptr_t addr = strtoul(args[1], NULL, 16);
	enable_breakpoint(dbg, addr);
}

void cmd_disable(debugger *dbg, char **args) {
	uintptr_t addr = strtoul(args[1], NULL, 16);
	disable_breakpoint(dbg, addr);
}

void cmd_reg(debugger *dbg, char **args) {
	if (is_prefix(args[1], "dump")) {
		dump_registers(dbg_get_pid(dbg));
	} else if (is_prefix(args[1], "read")) {
		printf("0x%016lx\n", get_register_value(get_register_from_name(args[2]), dbg_get_pid(dbg)));
	} else if (is_prefix(args[1], "write")) {
		uintptr_t value = strtoul(args[3], NULL, 16);
		set_register_value(get_register_from_name(args[2]), dbg_get_pid(dbg), value);
	} else {
		fprintf(stderr, BHRED "✗ " reset "register %s: invalid command\n", args[1]);
	}
}

void cmd_mem(debugger *dbg, char **args) {
	uintptr_t address = strtoul(args[2], NULL, 16);
	if (is_prefix(args[1], "read")) {
		printf("0x%016lx\n", read_memory(dbg_get_pid(dbg), address));
	} else if (is_prefix(args[1], "write")) {
		uintptr_t value = strtoul(args[3], NULL, 16);
		write_memory(dbg_get_pid(dbg), address, value);
	} else {
		fprintf(stderr, BHRED "✗ " reset "memory %s: invalid command\n", args[1]);
	}
}

void handle_command(debugger *dbg, char *input) {
	char **args = split(input, ' ');
	char *command = args[0];
	if (command == NULL) {
		free(args);
		return;
	}

	for (int i = 0; commands[i].name != NULL; i++) {
		if (is_prefix(command, commands[i].name)) {
			if (!dbg_is_active(dbg) && commands[i].requires_running_pid) {
				fprintf(stderr,
				        BHRED "✗ " BCYN "%s:" reset " this command requires running tracee\n",
				        commands[i].name);
				goto cleanup;
			}
			commands[i].handler(dbg, args);
			goto cleanup;
		}
	}
	fprintf(stderr, BHRED "✗ " BCYN "%s:" reset " invalid command. Use help.\n", command);

cleanup:
	free(args);
}
