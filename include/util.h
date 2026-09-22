#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <elf.h>
#include <sys/types.h>

void print_src_file_outdated_warning(void);
char *command_generator(const char *text, int state);
char **split(char *str, char delim, char **free_it);
bool is_prefix(const char *input, const char *command);
bool is_number(const char *str);
void print_memory_map(pid_t pid);
void print_source(const char *file_name, unsigned line, unsigned lines_context);
void list_current_dir(void);
char *str_sigsegv_code(int si_code);
void str_section_header_flag(Elf64_Xword flag, char flagbuf[20]);
const char *str_section_header_type(Elf64_Word type);
const char *str_symbol_type(unsigned char type);
const char *str_symbol_bind(unsigned char bind);
const char *str_symbol_visibility(unsigned char vis);
const char *str_osabi_name(unsigned char osabi);
const char *str_elf_filetype(Elf64_Half type);

#endif
