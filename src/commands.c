#include "commands.h"

#include <stdio.h>
#include <stdlib.h>

#include "registers.h"
#include "util.h"
#include "debugger.h"

typedef void (*cmd_handler)(debugger *dbg, char **args);

typedef struct {
	const char *name;
	cmd_handler handler;
	bool requires_running_pid;
	const char *help_text;
} command_entry;

// TODO: add help command

// static const command_entry commands[] = {
//     {"run",       cmd_run,       false, "Start tracee."},
//     {"break",     cmd_break,     false, "Set breakpoint."},
//     {"continue",  cmd_continue,  true,  "Resume execution."},
//     {"register",  cmd_reg,       true,  "Manage CPU registers."},
//     {"memory",    cmd_mem,       true,  "Manipulate memory at address."},
//     {"exit",      cmd_exit,      false, "Exit the debugger."},
//     {NULL,        NULL,          false, NULL}
// };

void handle_command(debugger *dbg, char *input) {
	char **args = split(input, ' ');
	char *command = args[0];
	if (command == NULL) {
		free(args);
		return;
	}

	// TODO: state handling in commands
	if (is_prefix(command, "continue")) {
		continue_execution(dbg);

	} else if (is_prefix(command, "run")) {
		run(dbg);

	} else if (is_prefix(command, "break")) {
		// TODO: add feature to delete breakpoint
		uintptr_t addr = strtoul(args[1], NULL, 16);
		set_breakpoint_at_addr(dbg, addr);

	} else if (is_prefix(command, "register")) {

		if (is_prefix(args[1], "dump")) {
			dump_registers(dbg_get_pid(dbg));
		} else if (is_prefix(args[1], "read")) {
			printf("0x%016lx\n",
			       get_register_value(get_register_from_name(args[2]), dbg_get_pid(dbg)));
		} else if (is_prefix(args[1], "write")) {
			uintptr_t value = strtoul(args[3], NULL, 16);
			set_register_value(get_register_from_name(args[2]), dbg_get_pid(dbg), value);
		} else {
			fprintf(stderr, "register %s: invalid command\n", args[1]);
		}

	} else if (is_prefix(command, "memory")) {

		uintptr_t address = strtoul(args[2], NULL, 16);
		if (is_prefix(args[1], "read")) {
			printf("0x%016lx\n", read_memory(dbg_get_pid(dbg), address));
		} else if (is_prefix(args[1], "write")) {
			uintptr_t value = strtoul(args[3], NULL, 16);
			write_memory(dbg_get_pid(dbg), address, value);
		} else {
			fprintf(stderr, "memory %s: invalid command\n", args[1]);
		}

	} else if (is_prefix(command, "exit")) {
		if (dbg_kill_tracee(dbg)) {
			printf("Exiting....\n");
			dbg_free(dbg);
			exit(0);
		} else {
			printf("You told not to exit, so let's continue..\n");
		}
	} else {
		fprintf(stderr, "%s: invalid command\n", command);
	}
	free(args);
}
