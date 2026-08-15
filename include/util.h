#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <elf.h>

char *command_generator(const char *text, int state);
char **split(char *str, char delim);
bool is_prefix(const char *input, const char *command);
void print_source(const char *file_name, unsigned line, unsigned lines_context);
char *str_sigsegv_code(int si_code);
void str_section_header_flag(Elf64_Xword flag, char flagbuf[20]);
const char *str_section_header_type(Elf64_Word type);

#endif
