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

typedef struct {
	ssize_t strtab_idx;            // string table index saved so that it can accessed anytime
	ssize_t dynstr_idx;            // dynamic string table saved for same reason
	Elf_Scn *symbol_table;         // storing the symbol_table in the main struct for fast lookup
	Elf_Scn *dynamic_symbol_table; // .dynsym is also stored for same reason
	struct func_entry *funcs_list; // all the valid functions in the executable being cached here
	size_t funcs_list_size;        // size of this functions list array
	const char **file_names;       // all the file names mentioned in the executable
	size_t file_names_size;        // size of file_names array
} cached_data;

struct dbg_symbols {
	Elf *elf_data;           // elf stuffs
	Dwfl *dwfl_data_proc;    // dwarf parsing while running program
	Dwfl *dwfl_data_offline; // dwarf parsing while offline program
	bool has_dwarf_symbols;  // check for printing the src code
	time_t mtime;            // last modified to time of the executable
	Elf64_Ehdr *ehdr;        // elf header
	cached_data cache;       // caching pointers to frequently refernced data
};

// if shdr is passed then scn_idx is of no use
static inline const char *get_section_name_by_index(dbg_symbols *syms, size_t scn_idx,
                                                    Elf64_Shdr *shdr) {
	if (!shdr) {
		Elf_Scn *scn = elf_getscn(syms->elf_data, scn_idx);
		if (!scn) return "ABS";
		shdr = elf64_getshdr(scn);
	}
	assert(shdr);
	return elf_strptr(syms->elf_data, syms->ehdr->e_shstrndx, shdr->sh_name);
}

// returns true if debug symbols are present in the code
static inline bool check_for_debug_symbols(dbg_symbols *syms) {
	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(syms->elf_data, scn)) != NULL) {
		const char *name = get_section_name_by_index(syms, elf_ndxscn(scn), NULL);
		if (strncmp(name, ".debug_info", 11) == 0) {
			return true;
		}
	}
	return false;
}

static inline bool verif_elf_signature(int fd) {
	char bytes[4];
	read(fd, bytes, 4);
	char ELF[] = {0x7F, 0x45, 0x4C, 0x46};

	if (memcmp(bytes, ELF, 4) == 0)
		return true;
	else
		return false;
}

static void cache_all_functions_and_filenames(dbg_symbols *syms) {
	if (!syms->cache.symbol_table) {
		return;
	}

	size_t pltent_size;
	struct func_entry *plt_entries = get_plt_entries(syms, &pltent_size);

	size_t symlist_size = 0;
	Elf64_Sym *sym_list = get_symbols(
	    syms, NULL, syms->cache.symbol_table, (size_t)syms->cache.strtab_idx, &symlist_size);

	size_t idx = 0, final_cap = pltent_size + symlist_size;
	struct func_entry *final_list = malloc(final_cap * sizeof(struct func_entry));

	// copying all the plt entries to final list
	for (size_t i = 0; i < pltent_size; i++) {
		size_t func_name_size = strlen(plt_entries[i].func_name);

		// converting `function_name` to `function_name@plt`
		char *plt_func_name = calloc(func_name_size + 5, sizeof(char));
		memcpy(plt_func_name, plt_entries[i].func_name, func_name_size);
		memcpy(plt_func_name + func_name_size, "@plt", 4);

		final_list[idx].func_name = plt_func_name;
		final_list[idx].addr = plt_entries[i].addr;
		final_list[idx].size = 0x10; // since all plt entries have 0x10 size
		idx++;
	}

	for (size_t i = 0; i < symlist_size; i++) {
		unsigned char type = ELF64_ST_TYPE(sym_list[i].st_info);
		char *name =
		    elf_strptr(syms->elf_data, (size_t)syms->cache.strtab_idx, sym_list[i].st_name);
		if (type == STT_FUNC && sym_list[i].st_value) {
			final_list[idx].func_name = strdup(name);
			final_list[idx].addr = sym_list[i].st_value;
			final_list[idx].size = sym_list[i].st_size;
			idx++;
		}
	}

	free(plt_entries);

	const char **file_names = malloc(symlist_size * sizeof(char *));
	size_t file_idx = 0;

	Dwarf_Addr bias;
	Dwarf_Die *cudie = NULL;
	while ((cudie = dwfl_nextcu(syms->dwfl_data_offline, cudie, &bias)) != NULL) {
		const char *name = dwarf_diename(cudie);
		if (name) file_names[file_idx++] = name;
	}

	syms->cache.funcs_list_size = idx;
	syms->cache.funcs_list = final_list;
	syms->cache.file_names = file_names;
	syms->cache.file_names_size = file_idx;
}

dbg_symbols *symbols_init(const char *pname) {
	dbg_symbols *new = malloc(sizeof(dbg_symbols));
	int fd = open(pname, O_RDONLY);
	if (fd == -1) {
		perror("open");
		free(new);
		return NULL;
	}

	if (verif_elf_signature(fd) == false) {
		printf("Not an elf executable\n");
		close(fd);
		free(new);
		return NULL;
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

	new->ehdr = elf64_getehdr(new->elf_data);

	new->has_dwarf_symbols = check_for_debug_symbols(new);

	new->cache.strtab_idx = -1;
	new->cache.dynstr_idx = -1;
	new->cache.symbol_table = NULL;
	new->cache.dynamic_symbol_table = NULL;

	// fetch `.strtab`, `.dynstr`, `.symtab`, `.dynsym`
	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(new->elf_data, scn)) != NULL) {
		size_t index = elf_ndxscn(scn);
		const char *name = get_section_name_by_index(new, elf_ndxscn(scn), NULL);
		if (!strcmp(name, ".strtab"))
			new->cache.strtab_idx = (ssize_t)index;
		else if (!strcmp(name, ".dynstr"))
			new->cache.dynstr_idx = (ssize_t)index;
		else if (!strcmp(name, ".symtab"))
			new->cache.symbol_table = scn;
		else if (!strcmp(name, ".dynsym"))
			new->cache.dynamic_symbol_table = scn;
	}

	// caching all the functions in the main struct
	new->cache.funcs_list_size = 0;
	new->cache.funcs_list = NULL;
	new->cache.file_names_size = 0;
	new->cache.file_names = NULL;
	cache_all_functions_and_filenames(new);

	struct stat file_stats;
	if (stat(pname, &file_stats) == -1) {
		DBG_PERROR("stat");
		free(new);
		return NULL;
	}
	new->mtime = file_stats.st_mtim.tv_sec;

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

// returns all the valid functions including plt ones
struct func_entry *get_valid_func_symbols(dbg_symbols *syms, size_t *return_size) {
	*return_size = syms->cache.funcs_list_size;
	return syms->cache.funcs_list;
}

const char **get_source_files(dbg_symbols *syms, size_t *return_size) {
	*return_size = syms->cache.file_names_size;
	return syms->cache.file_names;
}

void print_elf_header(dbg_symbols *sym) {
	Elf64_Ehdr *ehdr = sym->ehdr;

	// magic
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

static Elf_Scn **get_sections(dbg_symbols *syms, char *section_name, size_t *return_size) {
	bool get_all = false;
	if (!section_name) get_all = true;
	Elf_Scn *scn = NULL;
	size_t initialcap = 100;
	Elf_Scn **section_list = malloc(initialcap * sizeof(Elf_Scn *));
	size_t list_index = 0;
	while ((scn = elf_nextscn(syms->elf_data, scn)) != NULL) {
		const char *name = get_section_name_by_index(syms, elf_ndxscn(scn), NULL);
		if (get_all || strstr(name, section_name) != NULL) {
			section_list[list_index++] = scn;
			// DBG_LOG("shdr type: %u\n", shdr->sh_type);
		}
	}
	*return_size = list_index;
	return section_list;
}

void print_section_headers(dbg_symbols *syms, char *header_name) {
	size_t size = 0;
	Elf_Scn **section_list = get_sections(syms, header_name, &size);

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

	printf(BWHT
	       "[Nr] Name                 Type           Addr             Offset   Size     Flags Es "
	       "Link Info Align" RESET "\n");

	for (size_t i = 0; i < size; i++) {
		Elf64_Shdr *shdr = elf64_getshdr(section_list[i]);
		const char *name = get_section_name_by_index(syms, 0, shdr);
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
Elf64_Sym *get_symbols(dbg_symbols *syms, const char *sym_name, Elf_Scn *scn,
                       size_t string_table_idx, size_t *return_size) {
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
		name = elf_strptr(syms->elf_data, (size_t)string_table_idx, symbol.st_name);
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
		printf(BWHT "%s  %-40s %-7s %-6s %-18s %-4s %-9s %-10s" RESET "\n",
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

static void print_symbols(dbg_symbols *syms, char *sym_name, Elf_Scn *scn) {
	ssize_t string_table_idx = -1;
	Elf64_Shdr *shdr = elf64_getshdr(scn);
	if (shdr->sh_type == SHT_SYMTAB) {
		string_table_idx = syms->cache.strtab_idx;
	} else if (shdr->sh_type == SHT_DYNSYM) {
		string_table_idx = syms->cache.dynstr_idx;
	} else {
		printf("UNIMPLEMENTED\n");
		abort();
	}

	size_t sym_list_size = 0;
	Elf64_Sym *symbols_list = NULL;

	// if the string table index is not found that means the binary is stripped
	// so we leave sym_list_size to set to zero so that `print_symbols_table_header`
	// will tell the user that no symbols found
	if (string_table_idx != -1) {
		symbols_list = get_symbols(syms, sym_name, scn, (size_t)string_table_idx, &sym_list_size);
	}

	print_symbols_table_header(sym_list_size, sym_name);
	for (size_t j = 0; j < sym_list_size; j++) {
		Elf64_Sym symbol = symbols_list[j];
		Elf64_Word nameidx = symbol.st_name;
		char *name = NULL;
		name = elf_strptr(syms->elf_data, (size_t)string_table_idx, nameidx);
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
		       (symbol.st_shndx) ? get_section_name_by_index(syms, symbol.st_shndx, NULL)
		                         : "UNDEF");
	}
	printf("\n");

	if (sym_name) free(symbols_list); // only freeing the list if it's allocated
	                                  // it won't be if sym_name is not given
	                                  // since we already get a allocated list in libelf
}

void print_symbols_table(dbg_symbols *syms, char *sym_name) {
	// first print `.dynsym`
	if (syms->cache.dynamic_symbol_table) {
		printf("\n.dynsym: ");
		print_symbols(syms, sym_name, syms->cache.dynamic_symbol_table);
	}

	// then `.symtab`
	if (syms->cache.symbol_table) {
		printf("\n.symtab: ");
		print_symbols(syms, sym_name, syms->cache.symbol_table);
	}
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

// returns the line number and the file also set's if the src file is outdated
// returns 0 if something is wrong
int get_line_from_pc(dbg_symbols *sym, Dwarf_Addr pc, const char **file, const char **comp_dir,
                     bool *is_old) {
	Dwfl_Module *mod = dwfl_addrmodule(sym->dwfl_data_proc, pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwfl_Line *src = dwfl_module_getsrc(mod, pc);
	if (!src) return 0;
	DWFL_SANITY_CHECK(src, "dwfl_module_getsrc");

	if (comp_dir) *comp_dir = dwfl_line_comp_dir(src);

	int line_number = 0, column = 0;
	*file = dwfl_lineinfo(src, &pc, &line_number, &column, NULL, NULL);
	DWFL_SANITY_CHECK(*file, "dwfl_lineinfo");

	DBG_LOG("file: %s", *file);
	DBG_LOG("pc: %lx, line no.: %d, column: %d", pc, line_number, column);

	struct stat statbuf;
	stat(*file, &statbuf);
	DBG_LOG("times: %lu, %lu ", sym->mtime, statbuf.st_mtim.tv_sec);
	if (sym->mtime < statbuf.st_mtim.tv_sec) {
		if (is_old) *is_old = true;
	}

	return line_number;
}

// also updates the *file as recent file if file is passed as NULL
uintptr_t get_addr_from_lineno(dbg_symbols *syms, const char **file, int lineno) {
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
		printf("No such file found as \"%s\"\n", *file);
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
	// DWFL_SANITY_CHECK(cudie, "dwfl_module_addrdie");

	return cudie;
}

static int function_die_callback(Dwarf_Die *die, void *arg) {
	typedef struct {
		uintptr_t addr;
		Dwarf_Die *die;
	} payload_type;

	payload_type *payload = (payload_type *)arg;
	Dwarf_Addr func_start, func_end;
	dwarf_lowpc(die, &func_start);
	dwarf_highpc(die, &func_end);

	// printf("func_start: %#lx, func_end: %#lx\n", func_start, func_end);
	if (payload->addr >= func_start && payload->addr < func_end) {
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
	if (!cudie) return;

	DBG_LOG("cudie name: %s", dwarf_diename(cudie));
	DBG_LOG("Has children: %s", dwarf_haschildren(cudie) ? "True" : "False");

	// creating a payload to pass to the callback as void pointer
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

struct func_entry *get_plt_entries(dbg_symbols *syms, size_t *return_size) {
	size_t sections_size;
	Elf_Scn **sections = get_sections(syms, "plt", &sections_size);

	// finding `.plt` and `.rela.plt`
	Elf_Scn *scn_plt = NULL;
	Elf_Scn *scn_rela_plt = NULL;
	for (size_t i = 0; i < sections_size; i++) {
		const char *scn_name = get_section_name_by_index(syms, elf_ndxscn(sections[i]), NULL);
		if (!strcmp(scn_name, ".plt"))
			scn_plt = sections[i];
		else if (!strcmp(scn_name, ".rela.plt"))
			scn_rela_plt = sections[i];
	}

	// `if not found just return`
	if (!scn_plt || !scn_rela_plt || !syms->cache.dynamic_symbol_table) {
		free(sections);
		*return_size = 0;
		return NULL;
	}

	// getting the .plt address for the starting point
	Elf64_Addr plt_addr = elf64_getshdr(scn_plt)->sh_addr;

	// getting .rela.plt entries to get the list of functions in plt
	Elf_Data *rela_data = elf_getdata(scn_rela_plt, NULL);
	Elf64_Rela *relocs = rela_data->d_buf;
	size_t relocs_size = rela_data->d_size / elf64_getshdr(scn_rela_plt)->sh_entsize;

	// getting .dynsym because .rela.plt entries point to this section
	Elf_Data *dynsym_data = elf_getdata(syms->cache.dynamic_symbol_table, NULL);
	Elf64_Sym *dynamic_symbols = dynsym_data->d_buf;
	size_t dynsyms_size = dynsym_data->d_size / sizeof(Elf64_Sym);

	struct func_entry *plt_list = malloc(relocs_size * sizeof(struct func_entry));
	for (size_t i = 0; i < relocs_size; i++) {
		Elf64_Rela rel = relocs[i];
		Elf64_Xword dynsym_idx = ELF64_R_SYM(rel.r_info); // .rela.plt gives index of .dynsym
		assert(dynsym_idx < dynsyms_size);

		// fetching it's name using the .dynstr
		plt_list[i].func_name = elf_strptr(
		    syms->elf_data, (size_t)syms->cache.dynstr_idx, dynamic_symbols[dynsym_idx].st_name);

		// FIX: idk if this is even the correct way to do it. but as far as i have observed
		// gcc generated binaries it's always the case
		//
		// what i am doing is considering that every plt entry is of size `0x10` and multiplying it with
		// the index. adding 1 because there is an extra `0x10` slot at the beginning of plt that every
		// entry jumps to
		plt_list[i].addr = plt_addr + (0x10 * (i + 1));
	}

	free(sections);

	*return_size = relocs_size;
	return plt_list;
}

void print_source_at_current_pc(dbg_symbols *syms, uintptr_t pc, unsigned int lines_context) {
	// it it does not have dwarf symbols than just return
	// don't try to print the source code
	if (!syms->has_dwarf_symbols) return;

	bool is_outdated = false;
	const char *file;
	int line_no = get_line_from_pc(syms, pc, &file, NULL, &is_outdated);
	if (line_no == 0) return;

	if (is_outdated) print_src_file_outdated_warning();
	print_source(file, (uint32_t)line_no, lines_context);
}

// to get the base address of dyn executable
uintptr_t initialize_load_address(dbg_symbols *syms, pid_t pid) {
	Elf64_Ehdr *header = syms->ehdr;
	if (header->e_type != ET_DYN) return 0;

	uintptr_t load_address = 0;
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
	return load_address;
}

void symbols_free(dbg_symbols *syms) {
	for (size_t i = 0; i < syms->cache.funcs_list_size; i++) {
		free(syms->cache.funcs_list[i].func_name);
	}
	free(syms->cache.funcs_list);
	free(syms->cache.file_names);

	dwfl_end(syms->dwfl_data_proc);
	dwfl_end(syms->dwfl_data_offline);
	elf_end(syms->elf_data);
	free(syms);
}
