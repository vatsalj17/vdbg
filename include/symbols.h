#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <stddef.h>
#include <sys/types.h>
#include <elfutils/libdwfl.h>

typedef struct dbg_symbols dbg_symbols;

struct elf_section_header {
    char *name;
    Elf64_Shdr *shdr;
};

dbg_symbols *symbols_init(char *pname);
void symbols_free(dbg_symbols *sym);
struct elf_section_header *get_section_headers(dbg_symbols *sym, char *header_name, size_t *return_size);
bool has_dwarf_symbols(dbg_symbols *sym);
Dwfl *get_dwarf_data(dbg_symbols *sym);

void setup_dwfl(dbg_symbols *sym, pid_t pid);
int get_line_from_pc(dbg_symbols *sym, Dwarf_Addr pc, const char **file);
void get_func_die_from_pc(dbg_symbols *syms, uintptr_t pc, Dwarf_Die *func_die, uintptr_t load_address);
void print_source_at_current_pc(dbg_symbols *syms, uintptr_t pc);
uintptr_t initialize_load_address(dbg_symbols *sym, pid_t pid);

#endif
