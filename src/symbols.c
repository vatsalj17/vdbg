#include "symbols.h"

#include <dwarf.h>
#include <libelf.h>
#include <elfutils/libdwfl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ptrace.h>

#include "debugger.h"
#include "macro.h"
#include "util.h"

// for initializing dwfl
static const Dwfl_Callbacks callbacks = {
    .find_elf = dwfl_linux_proc_find_elf,
    .find_debuginfo = dwfl_standard_find_debuginfo,
    .debuginfo_path = NULL,
};

struct dbg_symbols {
	Elf *elf_data;          // elf stuffs
	Dwfl *dwarf_data;       // dwarf parsing
	bool has_dwarf_symbols; // check for printing the src code
	time_t mtime;           // last modified to time of the executable
};

// returns true if debug symbols are present in the code
static bool check_for_debug_symbols(dbg_symbols *sym) {
	Elf64_Ehdr *ehdr = elf64_getehdr(sym->elf_data);
	for (size_t i = 1;; i++) {
		Elf_Scn *scnhdr = elf_getscn(sym->elf_data, i);
		if (scnhdr == NULL) break;
		Elf64_Shdr *shdr = elf64_getshdr(scnhdr);
		char *name = elf_strptr(sym->elf_data, ehdr->e_shstrndx, shdr->sh_name);
		if (strncmp(name, ".debug_info", 11) == 0) {
			return true;
		}
	}
	return false;
}

dbg_symbols *symbols_init(char *pname) {
	dbg_symbols *new = malloc(sizeof(dbg_symbols));
	struct stat file_stats;
	stat(pname, &file_stats);
	new->mtime = file_stats.st_mtim.tv_sec;

	int fd = open(pname, O_RDONLY);
	if (fd == -1) {
		CRITICAL_PERROR("open");
	}

	elf_version(EV_CURRENT); // necessary because libelf just won't work
	new->elf_data = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (new->elf_data == NULL) {
		CRITICAL("elf_begin: %s", elf_errmsg(elf_errno()));
	}
	close(fd);

	new->dwarf_data = dwfl_begin(&callbacks);
	if (new->dwarf_data == NULL) {
		CRITICAL("dwfl_begin: %s", dwfl_errmsg(dwfl_errno()));
	}

	new->has_dwarf_symbols = check_for_debug_symbols(new);

	return new;
}

bool has_dwarf_symbols(dbg_symbols *sym) {
	return sym->has_dwarf_symbols;
}

Dwfl *get_dwarf_data(dbg_symbols *sym) {
	return sym->dwarf_data;
}

struct elf_section_header *get_section_headers(dbg_symbols *sym, char *header_name,
                                               size_t *return_size) {
	bool get_all = false;
	if (!header_name) get_all = true;
	Elf64_Ehdr *ehdr = elf64_getehdr(sym->elf_data);
	Elf_Scn *scn = NULL;
	size_t initialcap = 100;
	struct elf_section_header *header_list = malloc(initialcap * sizeof(struct elf_section_header));
	size_t list_index = 0;
	while ((scn = elf_nextscn(sym->elf_data, scn)) != NULL) {
		Elf64_Shdr *shdr = elf64_getshdr(scn);
		char *name = elf_strptr(sym->elf_data, ehdr->e_shstrndx, shdr->sh_name);
		if (get_all || strstr(name, header_name) != NULL) {
			header_list[list_index].name = name;
			header_list[list_index].shdr = shdr;
			list_index++;
			// DBG_LOG("shdr type: %u\n", shdr->sh_type);
		}
	}
	*return_size = list_index;
	return header_list;
}

char **get_symbols(UNUSED debugger_t *sym, UNUSED char *sym_name) {
	// TODO: incomplete
	return NULL;
}

// setting up dwfl to read modules later on in the code
void setup_dwfl(dbg_symbols *sym, pid_t pid) {
	// actually idk what the heck it is doing
	// adding this lead to finally being able to
	// get Dwfl_Module not null
	// hope libdwfl had documentation
	// figuring this out took hours
	if (dwfl_linux_proc_report(sym->dwarf_data, pid) != 0) {
		fprintf(stderr, "dwfl_linux_proc_report: %s\n", dwfl_errmsg(-1));
		dwfl_end(sym->dwarf_data);
		ptrace(PTRACE_CONT, pid, NULL, NULL);
	}
	dwfl_report_end(sym->dwarf_data, NULL, NULL);
}

// returns the line number and the file
int get_line_from_pc(dbg_symbols *sym, Dwarf_Addr pc, const char **file) {
	Dwfl_Module *mod = dwfl_addrmodule(sym->dwarf_data, pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwfl_Line *src = dwfl_module_getsrc(mod, pc);
	DWFL_SANITY_CHECK(src, "dwfl_module_getsrc");

	int line_number = 0, column = 0;
	*file = dwfl_lineinfo(src, &pc, &line_number, &column, NULL, NULL);
	DWFL_SANITY_CHECK(*file, "dwfl_lineinfo");

	DBG_LOG("file: %s", *file);
	DBG_LOG("pc: %lx, line no.: %d, column: %d", pc, line_number, column);

	struct stat statbuf;
	stat(*file, &statbuf);
	DBG_LOG("times: %lu, %lu ", sym->mtime, statbuf.st_mtim.tv_sec);
	if (sym->mtime < statbuf.st_mtim.tv_sec) {
		printf(RED "\n[" BYEL " Warning: " RESET "source code is modified" RED " \t]\n");
		printf("[" RESET " pls recompile the code and then debug " RED "]\n" RESET);
	}
	return line_number;
}

void get_func_die_from_pc(dbg_symbols *syms, uintptr_t pc, Dwarf_Die *func_die,
                          uintptr_t load_address) {
	Dwfl_Module *mod = dwfl_addrmodule(syms->dwarf_data, pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwarf_Addr bias;
	Dwarf_Die *die = dwfl_module_addrdie(mod, pc, &bias);
	DWFL_SANITY_CHECK(die, "dwfl_module_addrdie");

	int tag = dwarf_tag(die);

	// finally figured out how to get the function die
	Dwarf_Die *scopes;
	int count = dwarf_getscopes(die, load_address, &scopes);
	for (int i = 0; i < count; i++) {
		tag = dwarf_tag(&scopes[i]);
		if (tag == DW_TAG_subprogram) {
			memcpy(func_die, &scopes[i], sizeof(Dwarf_Die));
		}
	}

	free(scopes);
}

void print_source_at_current_pc(dbg_symbols *syms, uintptr_t pc) {
	// it it does not have dwarf symbols than just return
	// don't try to print the source code
	if (!syms->has_dwarf_symbols) return;

	const char *file;
	int line_no = get_line_from_pc(syms, pc, &file);
	print_source(file, (uint32_t)line_no, 3);
}

// to get the base address of dyn executable
uintptr_t initialize_load_address(dbg_symbols *sym, pid_t pid) {
	uintptr_t load_address = 0;
	Elf64_Ehdr *header = elf64_getehdr(sym->elf_data);
	if (header->e_type == ET_DYN) {
		char path[100];
		snprintf(path, sizeof(path), "/proc/%d/maps", pid);
		FILE *file = fopen(path, "r");
		if (!file) {
			CRITICAL_PERROR("fopen");
		}
		char *line = NULL;
		size_t len = 0;
		uintptr_t addr = 0;

		while (getline(&line, &len, file) != -1) {
			uintptr_t start_addr, end_addr;
			int parsed = sscanf(line, "%lx-%lx %*s %*x %*s %*d %*s", &start_addr, &end_addr);
			// if it's parsed correctly
			if (parsed == 2) {
				addr = start_addr;
				break;
			}
		}
		if (addr != 0) {
			DBG_LOG("load_address initialized with 0x%lx", addr);
			load_address = addr;
		}
		free(line);
		fclose(file);
	}
	return load_address;
}

void symbols_free(dbg_symbols *sym) {
	dwfl_end(sym->dwarf_data);
	elf_end(sym->elf_data);
	free(sym);
}
