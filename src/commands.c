#include "commands.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "completion.h"
#include "registers.h"
#include "util.h"
#include "macro.h"
#include "symbols.h"
#include "stepping.h"
#include "disassemble.h"

const command_entry commands[] = {
    {"arguments", cmd_arguments, false, false, "Pass arguments to the tracee", NULL},
    {"break", cmd_break, false, false, "Set breakpoint", complete_break},
    {"backtrace", cmd_backtrace, true, false, "Pring backtrace", NULL},
    {"continue", cmd_continue, true, false, "Resume execution", NULL},
    {"clear", cmd_clear, false, false, "Clears the screen", NULL},
    {"context", cmd_context, true, true, "Print the source code at the current instruction", NULL},
    {"delete", cmd_delete, false, false, "Delete a bp or all if not specified", complete_delete},
    {"disable", cmd_disable, false, false, "Disable any breakpoint", complete_disable},
    {"disassemble", cmd_disassemble, false, false, "Print the disassembly", complete_disassemble},
    {"exit", cmd_exit, false, false, "Exit the debugger", NULL},
    {"enable", cmd_enable, false, false, "Enable any breakpoint", complete_enable},
    {"finish", cmd_finish, true, true, "Skip the current function", NULL},
    {"files", cmd_files, false, false, "List all the source files", NULL},
    {"functions", cmd_functions, false, false, "List all the functions", NULL},
    {"help", cmd_help, false, false, "Show this menu", NULL},
    {"header", cmd_header, false, false, "Print ELF header", NULL},
    {"list", cmd_list, false, false, "List all the breakpoints", NULL},
    {"ls", cmd_ls, false, false, "List current directory", NULL},
    {"memory", cmd_mem, true, false, "Manipulate memory at address", complete_memory},
    {"next", cmd_next, true, true, "Step over current instruction", NULL},
    {"plt", cmd_plt, false, false, "Print the procedure linkage table", NULL},
    {"run", cmd_run, false, false, "Start tracee", NULL},
    {"restart", cmd_restart, true, false, "Restart tracee", NULL},
    {"register", cmd_reg, true, false, "Manage CPU registers", complete_register},
    {"step", cmd_step, true, true, "Single step throught source code", NULL},
    {"stepi", cmd_stepi, true, false, "Single step through instructions", NULL},
    {"source", cmd_source, false, false, "Source your script", complete_source},
    {"sections", cmd_sections, false, false, "List all the matching section headers", NULL},
    {"symbols", cmd_symbols, false, false, "List all the matching symbols", NULL},
    {"vmmap", cmd_vmmap, true, false, "Print the virtual memory layout of program", NULL},
    {NULL, NULL, NULL, NULL, NULL, NULL},
};

void cmd_help(UNUSED debugger_t *dbg, UNUSED char **args) {
	printf("\n Commands:\n");
	for (int i = 0; commands[i].name != NULL; i++) {
		printf("    %-10s ->  %s\n", commands[i].name, commands[i].help_text);
	}
	printf("\n");
}

// adding ls and clear because they are in muscle memory now and
// i don't want to see error if type it by mistake
void cmd_ls(UNUSED debugger_t *dbg, UNUSED char **args) {
	list_current_dir();
}

void cmd_clear(UNUSED debugger_t *dbg, UNUSED char **args) {
	printf("\e[2J\e[H");
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

void cmd_list(debugger_t *dbg, UNUSED char **args) {
	print_breakpoint_list(dbg_get_pending_list(dbg));
}

void cmd_files(debugger_t *dbg, UNUSED char **args) {
	size_t size = 0;
	const char **files = get_source_files(dbg_get_symbols(dbg), &size);
	for (size_t i = 0; i < size; i++)
		printf("%s\n", files[i]);
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

void cmd_vmmap(debugger_t *dbg, UNUSED char **args) {
	print_memory_map(dbg_get_pid(dbg));
}

void cmd_plt(debugger_t *dbg, char **args) {
	char *func_name = args[1];
	bool print_all = func_name == NULL;
	size_t size;
	struct func_entry *list = get_plt_entries(dbg_get_symbols(dbg), &size);

	if (!list) {
		printf("can't print plt\n");
		return;
	}
	if (size == 0) {
		printf("no plt entry found\n");
	}

	for (size_t i = 0; i < size; i++) {
		if (print_all || strstr(list[i].func_name, func_name)) {
			printf("%#lx: %s\n", list[i].addr, list[i].func_name);
		}
	}
	free(list);
}

void cmd_arguments(debugger_t *dbg, char **args) {
	add_arguments_for_tracee(dbg, args);
}

void cmd_break(debugger_t *dbg, char **args) {
	if (!args[1]) {
		fprintf(stderr, BHRED "✗ " RESET "usage: break <address/symbol/file:lineno>\n");
		return;
	}

	for (char **arg = args + 1; *arg; arg++) {
		char *lineno;
		if (*arg && (*arg)[0] == '0' && (*arg)[1] == 'x') {
			uintptr_t addr = strtoul(*arg, NULL, 16);
			set_breakpoint_at_addr(dbg, addr, false);
		} else if (is_number(*arg)) {
			if (!has_dwarf_symbols(dbg_get_symbols(dbg))) {
				printf("can't set breakpoint at line number\n");
				printf("the executable doesn't contain dwarf symbols\n");
				return;
			}
			set_breakpoint_at_lineno(dbg, NULL, atoi(*arg));
		} else if ((lineno = strchr(*arg, ':'))) {
			if (!has_dwarf_symbols(dbg_get_symbols(dbg))) {
				printf("can't set breakpoint at line number\n");
				printf("the executable doesn't contain dwarf symbols\n");
				return;
			}
			int line = atoi(lineno + 1);
			lineno[0] = '\0';
			set_breakpoint_at_lineno(dbg, *arg, line);
		} else {
			set_breakpoint_at_func_symbol(dbg, *arg);
		}
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
		if (addr == ULONG_MAX || addr == 0) {
			printf("Invalid argument \"%s\". give proper 0x<addr>\n", arg);
			return NULL;
		}
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
		if (line == 0 || !file) {
			printf("Invalid argument \"%s\". give proper file:lineno\n", arg);
			return NULL;
		}
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
	for (char **arg = args + 1; *arg; arg++) {
		breakpoint_t *bp = parse_arg_bp(dbg, *arg);
		if (!bp) continue;
		remove_breakpoint(dbg, bp);
		printf("removed bp: %s\n", *arg);
	}
}

void cmd_enable(debugger_t *dbg, char **args) {
	if (!args[1]) {
		// enable all bp
		enable_all_breakpoints(dbg);
		return;
	}
	for (char **arg = args + 1; *arg; arg++) {
		breakpoint_t *bp = parse_arg_bp(dbg, *arg);
		if (!bp) continue;
		bp_enable(bp);
		printf("enabled bp: %s\n", *arg);
	}
}

void cmd_disable(debugger_t *dbg, char **args) {
	if (!args[1]) {
		// disable all bp
		disable_all_breakpoints(dbg);
		return;
	}
	for (char **arg = args + 1; *arg; arg++) {
		breakpoint_t *bp = parse_arg_bp(dbg, *arg);
		if (!bp) continue;
		bp_disable(bp);
		printf("disabled bp: %s\n", *arg);
	}
}

void cmd_context(debugger_t *dbg, char **args) {
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
	char *func_name = args[1];

	size_t funcs_list_size = 0;
	bool print_all = (func_name == NULL);
	struct func_entry *funcs_list = get_valid_func_symbols(dbg_get_symbols(dbg), &funcs_list_size);
	if (funcs_list_size == 0) {
		printf("no functions found! what's wrong with your executable\n");
		return;
	}

	uintptr_t load_address = dbg_get_load_address(dbg);
	for (size_t i = 0; i < funcs_list_size; i++) {
		if (print_all || strstr(funcs_list[i].func_name, func_name)) {
			// Elf64_Addr addr = list[i].addr;
			// if (addr) addr += load_address;
			printf("0x%08lx %s\n", funcs_list[i].addr + load_address, funcs_list[i].func_name);
		}
	}
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

void cmd_disassemble(debugger_t *dbg, char **args) {
	print_disassembly_of_function(dbg, args[1]);
}

void cmd_source(debugger_t *dbg, char **args) {
	char *script_path = args[1];
	if (!script_path) {
		printf("nothing to source\n");
	}

	FILE *fp = fopen(script_path, "r");
	if (!fp) {
		fprintf(stderr, "Invalid script path: %s\n", script_path);
		perror("fopen");
		return;
	}

	char *line = NULL;
	size_t len = 0;
	while (getline(&line, &len, fp) != EOF) {
		if (line[0] == '#') continue; // using # as comment
		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '\0') continue; // skip empty lines
		handle_command(dbg, line);
	}

	free(line);
	fclose(fp);
}

void handle_command(debugger_t *dbg, char *input) {
	char *temp = NULL;
	char **args = split(input, ' ', &temp);
	if (args == NULL) return;

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
	free(temp); // freeing the string that we duped in split()
	free(args);
}
