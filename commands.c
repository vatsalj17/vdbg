#include "commands.h"

#include <stdio.h>
#include <stdlib.h>

#include "registers.h"
#include "util.h"

void handle_command(debugger *dbg, char *input) {
	char **args = split(input, ' ');
	char *command = args[0];
	if (command == NULL) {
		free(args);
		return;
	}

	if (is_prefix(command, "continue")) {
		continue_execution(dbg);

	} else if (is_prefix(command, "break")) {
		long addr = strtol(args[1], NULL, 16);
		set_breakpoint_at_addr(dbg, addr);

	} else if (is_prefix(command, "register")) {

		if (is_prefix(args[1], "dump")) {
			dump_registers(dbg_get_pid(dbg));
		} else if (is_prefix(args[1], "read")) {
			printf("0x%016lx\n",
			       get_register_value(get_register_from_name(args[2]), dbg_get_pid(dbg)));
		} else if (is_prefix(args[1], "write")) {
			long value = strtol(args[3], NULL, 16);
			set_register_value(get_register_from_name(args[2]), dbg_get_pid(dbg), value);
		} else {
			fprintf(stderr, "register %s: invalid command\n", args[1]);
		}

	} else if (is_prefix(command, "memory")) {

		long address = strtol(args[2], NULL, 16);
		if (is_prefix(args[1], "read")) {
			printf("0x%016lx\n", read_memory(dbg, address));
		} else if (is_prefix(args[1], "write")) {
			long value = strtol(args[3], NULL, 16);
			write_memory(dbg, address, value);
		} else {
			fprintf(stderr, "memory %s: invalid command\n", args[1]);
		}

	} else {
		fprintf(stderr, "%s: invalid command\n", command);
	}
	free(args);
}
