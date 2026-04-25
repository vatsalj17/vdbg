#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>

char *command_generator(const char *text, int state);
char **split(char *str, char delim);
bool is_prefix(const char *input, const char *command);
void print_source(const char *file_name, unsigned line, unsigned lines_context);

#endif
