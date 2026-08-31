#include "symbols.h"

#include <assert.h>
#include <dwarf.h>
#include <libelf.h>
#include <elfutils/libdwfl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ptrace.h>

#include "macro.h"
#include "util.h"

#define INITIAL_CAP 100

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
	ssize_t strtab_idx;     // string table index saved so that it can accessed anytime
	Elf_Scn *symbol_table;
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

dbg_symbols *symbols_init(const char *pname) {
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
	new->strtab_idx = -1;
	new->symbol_table = NULL;

	return new;
}

bool has_dwarf_symbols(dbg_symbols *sym) {
	return sym->has_dwarf_symbols;
}

Dwfl *get_dwarf_data(dbg_symbols *sym) {
	return sym->dwarf_data;
}

void print_elf_header(dbg_symbols *sym) {
	// magic
	Elf64_Ehdr *ehdr = elf64_getehdr(sym->elf_data);
	printf("Magic: ");
	for (int i = 0; i < EI_NIDENT; i++) {
		printf("%02hhx ", ehdr->e_ident[i]);
	}
	printf("\n");
	// TODO: complete it

	unsigned char class = ehdr->e_ident[EI_CLASS];
	printf("Class: %s\n", (class == ELFCLASS32) ? "ELF32" : "ELF64");
	unsigned char data = ehdr->e_ident[EI_DATA];
	printf(
	    "Date: %s%s\n", "2's complement, ", (data == ELFDATA2LSB) ? "Little Endian" : "Big Endian");
	unsigned char version = ehdr->e_ident[EI_VERSION];
	printf("Version: %hhd %s\n", version, (version == EV_CURRENT) ? "(current)" : "");

    CRITICAL(BRED "UNIMPLEMENTED" RESET);
	// unsigned char osabi = ehdr->e_ident[EI_OSABI];
	// unsigned char abiversion = ehdr->e_ident[EI_ABIVERSION];
}

static inline const char *get_section_name(Elf *elf, size_t scn_idx) {
	Elf64_Ehdr *ehdr = elf64_getehdr(elf);
	Elf_Scn *scn = elf_getscn(elf, scn_idx);
	if (!scn) return "ABS";
	Elf64_Shdr *shdr = elf64_getshdr(scn);
	assert(shdr);
	return elf_strptr(elf, ehdr->e_shstrndx, shdr->sh_name);
}

static Elf_Scn **get_sections(dbg_symbols *sym, char *section_name, size_t *return_size) {
	bool get_all = false;
	if (!section_name) get_all = true;
	Elf64_Ehdr *ehdr = elf64_getehdr(sym->elf_data);
	Elf_Scn *scn = NULL;
	size_t initialcap = 100;
	Elf_Scn **section_list = malloc(initialcap * sizeof(Elf_Scn *));
	size_t list_index = 0;
	while ((scn = elf_nextscn(sym->elf_data, scn)) != NULL) {
		Elf64_Shdr *shdr = elf64_getshdr(scn);
		char *name = elf_strptr(sym->elf_data, ehdr->e_shstrndx, shdr->sh_name);
		if (sym->strtab_idx == -1 && strncmp(name, ".strtab", 7) == 0) {
			sym->strtab_idx = (ssize_t)elf_ndxscn(scn);
		}
		if (!sym->symbol_table && strncmp(name, ".symtab", 7) == 0) {
			assert(shdr->sh_type == SHT_SYMTAB);
			sym->symbol_table = scn;
		}
		if (get_all || strstr(name, section_name) != NULL) {
			section_list[list_index++] = scn;
			// DBG_LOG("shdr type: %u\n", shdr->sh_type);
		}
	}
	*return_size = list_index;
	return section_list;
}

void print_section_headers(dbg_symbols *sym, char *header_name) {
	Elf64_Ehdr *ehdr = elf64_getehdr(sym->elf_data);
	size_t size = 0;
	Elf_Scn **section_list = get_sections(sym, header_name, &size);

	if (!header_name) {
		printf("\nTotal %zu sections\n\n", size);
	} else if (size) {
		printf("\nGot %zu sections containing \"%s\"\n\n", size, header_name);
	} else {
		printf("\nNo sections containing \"%s\" found\n\n", header_name);
	}
	if (size == 0) {
		free(section_list);
		return;
	}

	printf("[Nr] Name                 Type           Addr             Offset   Size     Flags Es "
	       "Link Info Align\n");

	for (size_t i = 0; i < size; i++) {
		Elf64_Shdr *shdr = elf64_getshdr(section_list[i]);
		char *name = elf_strptr(sym->elf_data, ehdr->e_shstrndx, shdr->sh_name);
		size_t index = elf_ndxscn(section_list[i]);
		char flagbuf[20] = {0};
		str_section_header_flag(shdr->sh_flags, flagbuf);
		// char *type =
		printf("[%02zu] %-20s %-14s %016lx %08lx %08lx %5s %2lu %4u %4u %5lu\n",
		       index,
		       name,
		       str_section_header_type(shdr->sh_type),
		       shdr->sh_addr,
		       shdr->sh_offset,
		       shdr->sh_size,
		       flagbuf,
		       shdr->sh_entsize,
		       shdr->sh_link,
		       shdr->sh_info,
		       shdr->sh_addralign);
	}

	free(section_list);
}

static Elf64_Sym *get_symbols(dbg_symbols *sym, const char *sym_name, Elf_Scn *scn,
                              size_t strtab_idx, size_t *return_size) {
	assert(scn);
	Elf_Data *data = elf_getdata(scn, NULL);
	Elf64_Sym *symbols = data->d_buf;
	size_t no_of_symbols = data->d_size / sizeof(Elf64_Sym);

	if (!sym_name) {
		*return_size = no_of_symbols;
		return symbols;
	}
	size_t idx = 0;
	Elf64_Sym *symbols_list = malloc(no_of_symbols * sizeof(Elf64_Sym));

	char *name;
	for (size_t j = 0; j < no_of_symbols; j++) {
		Elf64_Sym symbol = symbols[j];
		name = elf_strptr(sym->elf_data, strtab_idx, symbol.st_name);
		if (strstr(name, sym_name)) {
			symbols_list[idx++] = symbol;
		}
	}

	*return_size = idx;
	return symbols_list;
}

static inline void print_symbols_table_header(size_t sym_list_size, char *sym_name) {
	if (sym_list_size) {
		if (!sym_name) {
			printf("\nFound %zu symbols.\n\n", sym_list_size);
		} else {
			printf("\nFound %zu symbols matching \"%s\".\n\n", sym_list_size, sym_name);
		}
		printf("%s  %-40s %-7s %-6s %-18s %-4s %-9s %-10s\n",
		       "NUM",
		       "NAME",
		       "TYPE",
		       "BIND",
		       "VALUE",
		       "SIZE",
		       "VIS",
		       "SECTION");
	} else {
		if (!sym_name) {
			printf("No symbols found in .symtab\n");
		} else {
			printf("No symbols found containing \"%s\"\n", sym_name);
		}
	}
}

void print_symbols_table(dbg_symbols *sym, char *sym_name) {
	size_t size = 0;
	Elf_Scn **section_list = get_sections(sym, "sym", &size);
	if (size == 0) {
		printf("No symbols table found\n");
		free(section_list);
		return;
	}

	for (size_t i = 0; i < size; i++) {
		Elf_Scn *scn = section_list[i];
		Elf64_Shdr *shdr = elf64_getshdr(scn);
		if (shdr->sh_type == SHT_SYMTAB) {
			size_t sym_list_size = 0;
			assert(sym->strtab_idx != -1);
			Elf64_Sym *symbols_list =
			    get_symbols(sym, sym_name, scn, (size_t)sym->strtab_idx, &sym_list_size);

			print_symbols_table_header(sym_list_size, sym_name);
			for (size_t j = 0; j < sym_list_size; j++) {
				Elf64_Sym symbol = symbols_list[j];
				Elf64_Word nameidx = symbol.st_name;
				char *name = elf_strptr(sym->elf_data, (size_t)sym->strtab_idx, nameidx);
				unsigned char type = ELF64_ST_TYPE(symbol.st_info);
				unsigned char bind = ELF64_ST_BIND(symbol.st_info);
				unsigned char visibility = symbol.st_other;
				printf("%02zu:  %-40s %-7s %-6s %#016lx %4lu %-9s %-10s\n",
				       j,
				       name,
				       str_symbol_type(type),
				       str_symbol_bind(bind),
				       symbol.st_value,
				       symbol.st_size,
				       str_symbol_visibility(visibility),
				       (symbol.st_shndx) ? get_section_name(sym->elf_data, symbol.st_shndx)
				                         : "UNDEF");
			}
			if (sym_name) free(symbols_list);
		}
	}

	free(section_list);
}

Elf64_Sym *get_func_symbols(dbg_symbols *syms, const char *symbol_name, size_t *return_size) {
	assert(symbol_name);
	if (syms->strtab_idx == -1 || !syms->symbol_table) {
		size_t temps = 0;
		Elf_Scn **temp = get_sections(syms, NULL, &temps);
		free(temp);
	}
	size_t list_size = 0;
	Elf64_Sym *list =
	    get_symbols(syms, symbol_name, syms->symbol_table, (size_t)syms->strtab_idx, &list_size);
	size_t idx = 0;
	for (size_t i = 0; i < list_size; i++) {
		unsigned char type = ELF64_ST_TYPE(list[i].st_info);
		if (type == STT_FUNC) {
			list[idx++] = list[i];
		}
	}
	*return_size = idx;
	return list;
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

int dw_cb(Dwarf_Die *die, void *arg) {
	typedef struct {
		uintptr_t addr;
		Dwarf_Die *die;
	} payload_type;

	payload_type *payload = (payload_type *)arg;
	Dwarf_Addr func_entry, func_end;
	dwarf_lowpc(die, &func_entry);
	dwarf_highpc(die, &func_end);

	// printf("func_entry: %#lx, func_end: %#lx\n", func_entry, func_end);
	if (payload->addr >= func_entry && payload->addr < func_end) {
		// printf("got it\n");
		// printf("(cb) name: %s\n", dwarf_diename(die));
		memcpy(payload->die, die, sizeof(Dwarf_Die));
		return DWARF_CB_ABORT;
	} else
		return DWARF_CB_OK;
}

void get_func_die_from_pc(dbg_symbols *syms, uintptr_t pc, Dwarf_Die *func_die,
                          uintptr_t load_address) {
	Dwfl_Module *mod = dwfl_addrmodule(syms->dwarf_data, pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwarf_Addr bias;
	Dwarf_Die *cudie = dwfl_module_addrdie(mod, pc, &bias);
	DWFL_SANITY_CHECK(cudie, "dwfl_module_addrdie");

	DBG_LOG("cudie name: %s", dwarf_diename(cudie));
	DBG_LOG("Has children: %s", dwarf_haschildren(cudie) ? "True" : "False");

	struct {
		uintptr_t addr;
		Dwarf_Die *die;
	} payload = {
	    .addr = pc - load_address,
	    .die = func_die,
	};

	// printf("pc: %#lx\n", pc - load_address);

	// what it basically does is that it calls the callback function for
	// every die of tag DW_TAG_subprogram under the passed cudie
	dwarf_getfuncs(cudie, dw_cb, (void *)&payload, 0);
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
			DBG_LOG("load_address initialized with %#lx", addr);
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
