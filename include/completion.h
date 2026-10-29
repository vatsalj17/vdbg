#ifndef COMPLETION_H
#define COMPLETION_H

#include "debugger.h"

// stores dbg and registers rl_attempted_completion_function
void completion_init(debugger_t *dbg);

// argument completers for commands
char **complete_break(const char *text, int word_index);
char **complete_delete(const char *text, int word_index);
char **complete_disable(const char *text, int word_index);
char **complete_disassemble(const char *text, int word_index);
char **complete_enable(const char *text, int word_index);
char **complete_memory(const char *text, int word_index);
char **complete_register(const char *text, int word_index);
char **complete_source(const char *text, int word_index);

#endif
