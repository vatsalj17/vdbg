#ifndef COMMANDS_H
#define COMMANDS_H

#include "debugger.h"

typedef void (*cmd_handler)(debugger_t *dbg, char **args);

// argument completer for a command
// @param text: the partial word being completed (as given by readline)
// @param word_index: 0-based index of the word in the command line (0 = command name,
//               1 = first arg, etc.)
// @return a null-terminated heap-allocated array of match strings,
// or null if there are no completions. each string in the array must be
// individually heap-allocated (readline frees them)
typedef char **(*arg_completer_fn)(const char *text, int word_index);

typedef struct {
	const char *name;
	cmd_handler handler;
	bool requires_running_pid;
	bool requires_dwarf_symbols;
	const char *help_text;
	arg_completer_fn completer;
} command_entry;

extern const command_entry commands[];

void handle_command(debugger_t *dbg, char *input);

void cmd_run(debugger_t *dbg, char **args);
void cmd_restart(debugger_t *dbg, char **args);
void cmd_break(debugger_t *dbg, char **args);
void cmd_delete(debugger_t *dbg, char **args);
void cmd_enable(debugger_t *dbg, char **args);
void cmd_disable(debugger_t *dbg, char **args);
void cmd_continue(debugger_t *dbg, char **args);
void cmd_arguments(debugger_t *dbg, char **args);
void cmd_reg(debugger_t *dbg, char **args);
void cmd_mem(debugger_t *dbg, char **args);
void cmd_stepi(debugger_t *dbg, char **args);
void cmd_step(debugger_t *dbg, char **args);
void cmd_finish(debugger_t *dbg, char **args);
void cmd_next(debugger_t *dbg, char **args);
void cmd_exit(debugger_t *dbg, char **args);
void cmd_help(debugger_t *dbg, char **args);
void cmd_sections(debugger_t *dbg, char **args);
void cmd_symbols(debugger_t *dbg, char **args);
void cmd_header(debugger_t *dbg, char **args);
void cmd_functions(debugger_t *dbg, char **args);
void cmd_backtrace(debugger_t *dbg, char **args);
void cmd_list(debugger_t *dbg, char **args);
void cmd_context(debugger_t *dbg, char **args);
void cmd_ls(debugger_t *dbg, char **args);
void cmd_clear(debugger_t *dbg, char **args);
void cmd_vmmap(debugger_t *dbg, char **args);
void cmd_disassemble(debugger_t *dbg, char **args);
void cmd_plt(debugger_t *dbg, char **args);
void cmd_source(debugger_t *dbg, char **args);
void cmd_files(debugger_t *dbg, char **args);

#endif
