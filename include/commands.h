#ifndef COMMANDS_H
#define COMMANDS_H

#include "debugger.h"

void handle_command(debugger *dbg, char *input);
void cmd_run(debugger *dbg, char **args);
void cmd_restart(debugger *dbg, char **args);
void cmd_break(debugger *dbg, char **args);
void cmd_delete(debugger *dbg, char **args);
void cmd_enable(debugger *dbg, char **args);
void cmd_disable(debugger *dbg, char **args);
void cmd_clear(debugger *dbg, char **args);
void cmd_continue(debugger *dbg, char **args);
void cmd_reg(debugger *dbg, char **args);
void cmd_mem(debugger *dbg, char **args);
void cmd_exit(debugger *dbg, char **args);
void cmd_help(debugger *dbg, char **args);

#endif
