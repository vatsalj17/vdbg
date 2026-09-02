#include "symbols.h"

#include <assert.h>
#include <dwarf.h>
#include <libelf.h>
#include <elfutils/libdwfl.h>
#include <elfutils/libdwelf.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ptrace.h>

#include "macro.h"
#include "util.h"

#define INITIAL_CAP 100

char *debuginfo_path;

// for initializing dwfl
static const Dwfl_Callbacks offline_callbacks = {
    .find_debuginfo = dwfl_standard_find_debuginfo,
    .debuginfo_path = &debuginfo_path,
    .section_address = dwfl_offline_section_address,
    .find_elf = dwfl_build_id_find_elf,
};

static const Dwfl_Callbacks proc_callbacks = {
    .find_debuginfo = dwfl_standard_find_debuginfo,
    .debuginfo_path = &debuginfo_path,
    .find_elf = dwfl_linux_proc_find_elf,
};

struct dbg_symbols {
	Elf *elf_data;           // elf stuffs
	Dwfl *dwfl_data_proc;    // dwarf parsing while running program
	Dwfl *dwfl_data_offline; // dwarf parsing while offline program
	bool has_dwarf_symbols;  // check for printing the src code
	time_t mtime;            // last modified to time of the executable
	ssize_t strtab_idx;      // string table index saved so that it can accessed anytime
	Elf_Scn *symbol_table;   // storing the symbol_table in the main struct for fast lookup
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

	new->dwfl_data_proc = dwfl_begin(&proc_callbacks);
	if (new->dwfl_data_proc == NULL) {
		CRITICAL("{proc} dwfl_begin: %s", dwfl_errmsg(dwfl_errno()));
	}

	new->dwfl_data_offline = dwfl_begin(&offline_callbacks);
	if (new->dwfl_data_proc == NULL) {
		CRITICAL("{offline} dwfl_begin: %s", dwfl_errmsg(dwfl_errno()));
	}

	// can use this offline_module in future if needed
	Dwfl_Module *offline_module = dwfl_report_offline(new->dwfl_data_offline, "", pname, -1);
	if (offline_module == NULL) {
		CRITICAL("dwfl_report_offline failed");
	}
	dwfl_report_end(new->dwfl_data_offline, NULL, NULL);

	new->has_dwarf_symbols = check_for_debug_symbols(new);
	new->strtab_idx = -1;
	new->symbol_table = NULL;

	return new;
}

bool has_dwarf_symbols(dbg_symbols *sym) {
	return sym->has_dwarf_symbols;
}

Dwfl *get_dwarf_data(dbg_symbols *sym) {
	return sym->dwfl_data_proc;
}

Elf *get_elf_data(dbg_symbols *sym) {
	return sym->elf_data;
}

void print_elf_header(dbg_symbols *sym) {
	// magic
	Elf64_Ehdr *ehdr = elf64_getehdr(sym->elf_data);
	printf("Magic: ");
	for (int i = 0; i < EI_NIDENT; i++) {
		printf("%02hhx ", ehdr->e_ident[i]);
	}
	printf("\n");

	unsigned char class = ehdr->e_ident[EI_CLASS];
	printf("%-34s %s\n", "Class:", (class == ELFCLASS32) ? "ELF32" : "ELF64");
	unsigned char data = ehdr->e_ident[EI_DATA];
	printf("%-34s %s%s\n",
	       "Data:",
	       "2's complement, ",
	       (data == ELFDATA2LSB) ? "Little Endian" : "Big Endian");
	unsigned char version = ehdr->e_ident[EI_VERSION];
	printf(
	    "%-34s %hhd %s\n", "Ident Version:", version, (version == EV_CURRENT) ? "(current)" : "");

	unsigned char osabi = ehdr->e_ident[EI_OSABI];
	printf("%-34s %s\n", "OS/ABI:", str_osabi_name(osabi));

	unsigned char abiversion = ehdr->e_ident[EI_ABIVERSION];
	printf("%-34s %d\n", "ABI Version:", abiversion);

	printf("%-34s %s\n", "Type:", str_elf_filetype(ehdr->e_type));
	printf("%-34s %s\n", "Machine:", dwelf_elf_e_machine_string(ehdr->e_machine));
	printf("%-34s %hhd %s\n",
	       "Version:",
	       ehdr->e_version,
	       (ehdr->e_version == EV_CURRENT) ? "(current)" : "");

	printf("%-34s %#lx\n", "Entry point address:", ehdr->e_entry);
	printf("%-34s %#lx\n", "Start of program headers:", ehdr->e_phoff);
	printf("%-34s %#lx\n", "Start of section headers:", ehdr->e_shoff);

	// TODO: implement printing of flags

	printf("%-34s %d\n", "Size of this header:", ehdr->e_ehsize);
	printf("%-34s %d\n", "Size of program header entries:", ehdr->e_phentsize);
	printf("%-34s %d\n", "Number of program headers entries:", ehdr->e_phnum);
	printf("%-34s %d\n", "Size of section header entries:", ehdr->e_shentsize);
	printf("%-34s %d\n", "Number of section headers entries:", ehdr->e_shnum);
	printf("%-34s %d\n", "Section header string table index:", ehdr->e_shstrndx);
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

// this function returns the list of matching symbols as passed in sym_name
// if NULL is passed then it returns all the symbols
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

void print_symbols_table(dbg_symbols *syms, char *sym_name) {
	size_t size = 0;
	Elf_Scn **section_list = get_sections(syms, "sym", &size);
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
			assert(syms->strtab_idx != -1);
			Elf64_Sym *symbols_list =
			    get_symbols(syms, sym_name, scn, (size_t)syms->strtab_idx, &sym_list_size);

			print_symbols_table_header(sym_list_size, sym_name);
			for (size_t j = 0; j < sym_list_size; j++) {
				Elf64_Sym symbol = symbols_list[j];
				Elf64_Word nameidx = symbol.st_name;
				char *name = elf_strptr(syms->elf_data, (size_t)syms->strtab_idx, nameidx);
				unsigned char type = ELF64_ST_TYPE(symbol.st_info);
				unsigned char bind = ELF64_ST_BIND(symbol.st_info);
				unsigned char visibility = symbol.st_other;
				printf("%02zu:  %-40s %-7s %-6s 0x%016lx %4lu %-9s %-10s\n",
				       j,
				       name,
				       str_symbol_type(type),
				       str_symbol_bind(bind),
				       symbol.st_value,
				       symbol.st_size,
				       str_symbol_visibility(visibility),
				       (symbol.st_shndx) ? get_section_name(syms->elf_data, symbol.st_shndx)
				                         : "UNDEF");
			}
			if (sym_name) free(symbols_list);
		} else if (shdr->sh_type == SHT_DYNSYM) {
			// TODO: print relocation table too
		}
	}

	free(section_list);
}

// it calls get_symbols and filter out the valid function symbols
// if also sets the symbol table index if valid variable address is passed as an argument
Elf64_Sym *get_valid_func_symbols(dbg_symbols *syms, const char *symbol_name, size_t *return_size,
                                  size_t *set_symtab_idx) {
	assert(symbol_name);

    // if these values are not set the first set them up using get_sections
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
		if (type == STT_FUNC && list[i].st_value) {
			list[idx++] = list[i];
		}
	}

	if (set_symtab_idx) *set_symtab_idx = (size_t)syms->strtab_idx;
	*return_size = idx;
	return list;
}

void list_all_functions(dbg_symbols *syms, const char *symbol_name) {
    // if these values are not set the first set them up using get_sections
	if (syms->strtab_idx == -1 || !syms->symbol_table) {
		size_t temps = 0;
		Elf_Scn **temp = get_sections(syms, NULL, &temps);
		free(temp);
	}

	size_t list_size = 0;
	Elf64_Sym *list =
	    get_symbols(syms, symbol_name, syms->symbol_table, (size_t)syms->strtab_idx, &list_size);

	for (size_t i = 0; i < list_size; i++) {
		unsigned char type = ELF64_ST_TYPE(list[i].st_info);
        char *name = elf_strptr(syms->elf_data, (size_t)syms->strtab_idx, list[i].st_name);
		if (type == STT_FUNC) {
            printf("0x%08lx %s\n", list[i].st_value, name);
		}
	}

    if (symbol_name) free(list);
}

// setting up dwfl to read modules later on in the code
void setup_dwfl(dbg_symbols *sym, pid_t pid) {
	// actually idk what the heck it is doing
	// adding this lead to finally being able to
	// get Dwfl_Module not null
	// hope libdwfl had documentation
	// figuring this out took hours
	if (dwfl_linux_proc_report(sym->dwfl_data_proc, pid) != 0) {
		fprintf(stderr, "dwfl_linux_proc_report: %s\n", dwfl_errmsg(-1));
		dwfl_end(sym->dwfl_data_proc);
		ptrace(PTRACE_CONT, pid, NULL, NULL);
	}
	dwfl_report_end(sym->dwfl_data_proc, NULL, NULL);
}

// returns the line number and the file
int get_line_from_pc(dbg_symbols *sym, Dwarf_Addr pc, const char **file) {
	Dwfl_Module *mod = dwfl_addrmodule(sym->dwfl_data_proc, pc);
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

// also updates the *file as recent file if file is passed as NULL
uintptr_t get_addr_from_lineno(dbg_symbols *syms, const char **file, int lineno) {
	// if it's a full path then extract the basename
	if (*file && strchr(*file, '/')) {
		*file = basename(*file);
	}

	Dwarf_Addr bias;
	Dwarf_Die *cudie = NULL;

	// iterating over all cudies to find the one while our filename
	while ((cudie = dwfl_nextcu(syms->dwfl_data_offline, cudie, &bias)) != NULL) {
		const char *name = dwarf_diename(cudie);
		if (!*file) {
			*file = name;
			break;
		}
		DBG_LOG("trying: %s", name);
		if (strcmp(name, *file) == 0) {
			break;
		}
	}

	if (!cudie) {
		printf("No such *file found as \"%s\"\n", *file);
		return 0;
	}

	// only for debugging
	// size_t nlines;
	// int returnval = dwfl_getsrclines(cudie, &nlines);
	// printf("nlines: %zu, ret: %d\n", nlines, returnval);
	// for (size_t i = 0; i < nlines; i++) {
	//     Dwfl_Line *line = dwfl_onesrcline(cudie, i);
	//     Dwarf_Addr addr;
	//     int linep;
	//     dwfl_lineinfo(line, &addr, &linep, NULL, NULL, NULL);
	//     // const char *name = dwfl_line_comp_dir(srcsp[i]);
	//     printf("%zu: %#lx, %d\n", i, addr - bias, linep);
	// }
	// printf("\n next \n");

	Dwfl_Module *module = dwfl_cumodule(cudie);
	Dwfl_Line **srcsp;
	size_t src_size = 0;
	if (dwfl_module_getsrc_file(module, *file, lineno, 0, &srcsp, &src_size) == -1 ||
	    src_size == 0) {
		printf("Invalid line no. \"%d\"\n", lineno);
		return 0;
	}

	// return the first addr got at that line number
	Dwarf_Addr addr;
	dwfl_lineinfo(srcsp[0], &addr, NULL, NULL, NULL, NULL);
	// const char *name = dwfl_line_comp_dir(srcsp[i]);
	Dwarf_Addr accurate_addr = addr - bias;
	DBG_LOG("found addr at line no. %d: %#lx", lineno, addr);
	free(srcsp);

	return accurate_addr;
}

Dwarf_Die *get_cudie_from_pc(dbg_symbols *syms, uintptr_t pc) {
	Dwfl_Module *mod = dwfl_addrmodule(syms->dwfl_data_proc, pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwarf_Addr bias;
	Dwarf_Die *cudie = dwfl_module_addrdie(mod, pc, &bias);
	DWFL_SANITY_CHECK(cudie, "dwfl_module_addrdie");

    return cudie;
}


static int function_die_callback(Dwarf_Die *die, void *arg) {
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
	Dwarf_Die *cudie = get_cudie_from_pc(syms, pc);

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
	dwarf_getfuncs(cudie, function_die_callback, (void *)&payload, 0);
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
	dwfl_end(sym->dwfl_data_proc);
	dwfl_end(sym->dwfl_data_offline);
	elf_end(sym->elf_data);
	free(sym);
}
