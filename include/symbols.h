#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <stddef.h>
#include <sys/types.h>
#include <elfutils/libdwfl.h>

typedef struct dbg_symbols dbg_symbols;

dbg_symbols *symbols_init(const char *pname);
void symbols_free(dbg_symbols *sym);
bool has_dwarf_symbols(dbg_symbols *sym);
Dwfl *get_dwarf_data(dbg_symbols *sym);
Elf *get_elf_data(dbg_symbols *sym);

void print_section_headers(dbg_symbols *sym, char *header_name);
void print_symbols_table(dbg_symbols *sym, char *sym_name);
void print_elf_header(dbg_symbols *sym);
void list_all_functions(dbg_symbols *syms, const char *symbol_name);

Elf64_Sym *get_valid_func_symbols(dbg_symbols *syms, const char *symbol_name, size_t *return_size,
                                  size_t *set_symtab_idx);

void setup_dwfl(dbg_symbols *sym, pid_t pid);
int get_line_from_pc(dbg_symbols *sym, Dwarf_Addr pc, const char **file, const char **comp_dir, bool *is_src_old);
uintptr_t get_addr_from_lineno(dbg_symbols *dbg, const char **file, int lineno);
Dwarf_Die *get_cudie_from_pc(dbg_symbols *syms, uintptr_t pc);
void get_func_die_from_pc(dbg_symbols *syms, uintptr_t pc, Dwarf_Die *func_die,
                          uintptr_t load_address);
void print_source_at_current_pc(dbg_symbols *syms, uintptr_t pc);
uintptr_t initialize_load_address(dbg_symbols *sym, pid_t pid);

#endif
