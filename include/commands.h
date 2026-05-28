#ifndef COMMANDS_H
#define COMMANDS_H

#include "debugger.h"

typedef void (*cmd_handler)(debugger *dbg, char **args);

typedef struct {
	const char *name;
	cmd_handler handler;
	bool requires_running_pid;
	const char *help_text;
} command_entry;

extern const command_entry commands[];

void handle_command(debugger *dbg, char *input);
void cmd_run(debugger *dbg, char **args);
void cmd_restart(debugger *dbg, char **args);
void cmd_break(debugger *dbg, char **args);
void cmd_delete(debugger *dbg, char **args);
void cmd_enable(debugger *dbg, char **args);
void cmd_disable(debugger *dbg, char **args);
void cmd_clear(debugger *dbg, char **args);
void cmd_continue(debugger *dbg, char **args);
void cmd_arguments(debugger *dbg, char **args);
void cmd_reg(debugger *dbg, char **args);
void cmd_mem(debugger *dbg, char **args);
void cmd_stepi(debugger *dbg, char **args);
void cmd_exit(debugger *dbg, char **args);
void cmd_help(debugger *dbg, char **args);

#endif
