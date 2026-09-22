#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <asm-generic/siginfo.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util.h"
#include "macro.h"
#include "commands.h"

void print_src_file_outdated_warning(void) {
	printf(RED "\n[" BYEL " Warning: " RESET "source code is modified" RED " \t]\n");
	printf("[" RESET " pls recompile the code and then debug " RED "]\n" RESET);
}

char *command_generator(const char *text, int state) {
	static unsigned long i, len;
	if (!state) {
		i = 0;
		len = strlen(text);
	}

	const char *cmd;
	while ((cmd = commands[i++].name)) {
		if (strncmp(cmd, text, len) == 0) {
			return strdup(cmd);
		}
	}

	return NULL;
}

char **split(char *str, char delim, char **free_it) {
	// making the copy of str so that it isn't modified
	char *to_parse = strdup(str);
	size_t tokens_list_cap = 32;
	char **tokens = malloc(tokens_list_cap * sizeof(char *));
	char *delimiter = (char[]){delim, '\0'};
	char *token = strtok(to_parse, delimiter);

	size_t count = 0;
	while (token != NULL) {
		if (count == tokens_list_cap) {
			tokens_list_cap *= 2;
			tokens = realloc(tokens, tokens_list_cap * sizeof(char *));
            if (!tokens) {
                printf("either buy more ram or give less argments");
                free(to_parse);
                return NULL;
            }
		}
		tokens[count++] = token;
		token = strtok(NULL, delimiter);
	}

	tokens[count] = NULL;
    *free_it = to_parse;
	return tokens;
}

bool is_prefix(const char *input, const char *command) {
	if (!command || !input) return false;
	// printf("is_prefix: command: %s, input: %s\n", command, input);
	if (strncmp(command, input, strlen(input)) == 0) return true;
	return false;
}

bool is_number(const char *str) {
	for (int i = 0; str[i] != '\0'; i++) {
		if (!isdigit(str[i])) return false;
	}
	return true;
}

#define BUFSIZE (1 << 20)

void print_memory_map(pid_t pid) {
	char path[32];
	snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }

    char buf[BUFSIZE]; // 1 mb should be enough that it reads in one go
    ssize_t r;
    while (1) {
        r = read(fd, buf, BUFSIZE);
        if (r < 0) {
            perror("read");
            break;
        }
        if (r == 0) break;

        ssize_t w = write(STDOUT_FILENO, buf, (size_t)r);
        if (w < 0) {
            perror("write");
            break;
        }
    }

    close(fd);
}
#undef BUFSIZE

static inline void print_line_prefix(unsigned line_no, unsigned target_line) {
	bool is_target = (line_no == target_line);
	printf("  %s%3u | %s", is_target ? HGRN : HBLK, line_no, is_target ? RESET : "");
}

void print_source(const char *file_name, unsigned line, unsigned lines_context) {
	assert(line);
	assert(lines_context);
	FILE *f = fopen(file_name, "r");
	if (f == NULL) {
		fprintf(stderr, "source file \"%s\" can't be opened\n", file_name);
		return;
	}
	printf(CYN "\n %s:\n" RESET, file_name);
	unsigned start_line = line <= lines_context ? 1 : line - lines_context + 1;
	// printf("line: %u & lines_context: %u\n", line, lines_context);
	// printf("startline: %u\n", start_line);
	// printf("start_line = (%u <= %u) ? 1 : %u - %u;\n", line, lines_context, line, lines_context);

	unsigned end_line = line + lines_context + (line < lines_context ? lines_context - line : 0);
	// printf("endline: %u\n", end_line);
	// printf("end_line = %u + %u + ((%u < %u) ? %u - %u : 0) + 1\n", line, lines_context, line, lines_context, lines_context, line);

	int c;
	unsigned current_line = 1u;
	while (current_line < start_line && (c = fgetc(f)) != EOF) {
		if (c == '\n') current_line++;
	}

	print_line_prefix(current_line, line);
	while (current_line < end_line && (c = fgetc(f)) != EOF) {
		putc(c, stdout);
		if (c == '\n') {
			current_line++;
			print_line_prefix(current_line, line);
		}
	}
	printf(RESET "\r          \n");
	fclose(f);
}

static size_t display_length(const char *s) {
	size_t length = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		switch (*p) {
		case '\n':
		case '\t':
		case '\r':
		case '\b':
		case '\\':
		case '\x1b':
			length += 2;
			break;
		default:
			if (*p < 0x20 || *p == 0x7f)
				length += 4;
			else
				length++;
			break;
		}
	}
	return length;
}

// to handle those ugly $'a\nb' type files. any sane person would never do that but still i am handling it
static void print_filename(const char *name) {
	for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
		switch (*p) {
		case '\n':
			fputs("\\n", stdout);
			break;
		case '\t':
			fputs("\\t", stdout);
			break;
		case '\r':
			fputs("\\r", stdout);
			break;
		case '\b':
			fputs("\\b", stdout);
			break;
		case '\\':
			fputs("\\\\", stdout);
			break;
		default:
			if (*p < 0x20 || *p == 0x7f)
				printf("\\x%02x", *p);
			else
				putchar(*p);
		}
	}
}

void list_current_dir(void) {
	struct dirent **entries;
	int files_count = scandir(".", &entries, NULL, alphasort);
	if (files_count == -1) {
		DBG_PERROR("scandir");
		return;
	}
	if (files_count == 0) return;

	// getting the size of the terminal
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
		DBG_LOG("Couldn't get terminal size! printing everything in single column");
		for (int i = 0; i < files_count; i++) {
			printf("%s\n", entries[i]->d_name);
			free(entries[i]);
		}
		goto cleanup;
	}
	int width = ws.ws_col;

	// track the max length of the file in current dir
	size_t max_len = 0;
	for (int i = 0; i < files_count; i++) {
		size_t len = display_length(entries[i]->d_name);
		if (len > max_len) max_len = len;
	}
	if (max_len == 0) CRITICAL("Something went very wrong in your logic");

	int column_len = (int)max_len + 2;
	int no_of_columns = width / column_len;
	if (no_of_columns < 1) no_of_columns = 1;

	int no_of_rows =
	    (files_count / no_of_columns) + (no_of_columns - (files_count % no_of_columns));
	DBG_LOG("file: %d, clen: %d, cols: %d, rows: %d, width: %d",
	        files_count,
	        column_len,
	        no_of_columns,
	        no_of_rows,
	        width);

	for (int r = 0; r < no_of_rows; r++) {
		for (int c = 0; c < no_of_columns; c++) {
			int col_pos = (c * column_len) + 1;
			// move cursor the column position
			printf("\e[%dG", col_pos);

			// print the file name
			int index = (c * no_of_rows) + r;
			if (index >= files_count) break;
			// printf("index: %d", index);
			if (entries[index]->d_type == DT_DIR) {
				printf(BWHT);
				print_filename(entries[index]->d_name);
				printf("/ " RESET);
			} else {
				print_filename(entries[index]->d_name);
				printf("  ");
			}
			free(entries[index]);
		}
		printf("\n");
	}

cleanup:
	free(entries);
}

char *str_sigsegv_code(int si_code) {
	struct {
		int code;
		char *reason;
	} code_str[] = {
	    {SEGV_MAPERR, "address not mapped to object"},
	    {SEGV_ACCERR, "invalid permissions for mapped object"},
	    {SEGV_BNDERR, "failed address bound checks"},
#ifdef __ia64__
	    {__SEGV_PSTKOVF, "paragraph stack overflow"},
#else
	    {SEGV_PKUERR, "failed protection key checks"},
#endif
	    {SEGV_ACCADI, "ADI not enabled for mapped object"},
	    {SEGV_ADIDERR, "Disrupting MCD error"},
	    {SEGV_ADIPERR, "Precise MCD exception"},
	    {SEGV_MTEAERR, "Asynchronous ARM MTE error"},
	    {SEGV_MTESERR, "Synchronous ARM MTE exception"},
	    {SEGV_CPERR, "Control protection fault"},
	    {0, NULL},
	};
	for (int i = 0; code_str[i].code; i++) {
		if (si_code == code_str[i].code) return code_str[i].reason;
	}
	DBG_ERR("SIGSEGV si_code: %d", si_code);
	return "Unknown Code for SIGSEGV";
}

void str_section_header_flag(Elf64_Xword flag, char flagbuf[20]) {
	char *cp = flagbuf;
	if (flag & SHF_WRITE) *cp++ = 'W';
	if (flag & SHF_ALLOC) *cp++ = 'A';
	if (flag & SHF_EXECINSTR) *cp++ = 'X';
	if (flag & SHF_MERGE) *cp++ = 'M';
	if (flag & SHF_STRINGS) *cp++ = 'S';
	if (flag & SHF_INFO_LINK) *cp++ = 'I';
	if (flag & SHF_LINK_ORDER) *cp++ = 'L';
	if (flag & SHF_OS_NONCONFORMING) *cp++ = 'N';
	if (flag & SHF_GROUP) *cp++ = 'G';
	if (flag & SHF_TLS) *cp++ = 'T';
	if (flag & SHF_COMPRESSED) *cp++ = 'C';
	if (flag & SHF_ORDERED) *cp++ = 'O';
	if (flag & SHF_EXCLUDE) *cp++ = 'E';
	if (flag & SHF_GNU_RETAIN) *cp++ = 'R';
	*cp = '\0';
}

const char *str_section_header_type(Elf64_Word type) {
	// elfutils hasn't made macro for type GNU_SFRAME so i have to
	// explicitely define it as it was in binutils
#define SHT_GNU_SFRAME SHT_GNU_ATTRIBUTES - 1

	switch (type) {
#define TYPECASE(name)                                                                             \
	case (SHT_##name):                                                                             \
		return #name

		TYPECASE(NULL);
		TYPECASE(PROGBITS);
		TYPECASE(SYMTAB);
		TYPECASE(STRTAB);
		TYPECASE(RELA);
		TYPECASE(HASH);
		TYPECASE(DYNAMIC);
		TYPECASE(NOTE);
		TYPECASE(NOBITS);
		TYPECASE(REL);
		TYPECASE(SHLIB);
		TYPECASE(DYNSYM);
		TYPECASE(INIT_ARRAY);
		TYPECASE(FINI_ARRAY);
		TYPECASE(PREINIT_ARRAY);
		TYPECASE(GROUP);
		TYPECASE(SYMTAB_SHNDX);
		TYPECASE(RELR);
		TYPECASE(NUM);
		TYPECASE(GNU_SFRAME);
		TYPECASE(GNU_ATTRIBUTES);
		TYPECASE(GNU_HASH);
		TYPECASE(GNU_LIBLIST);
		TYPECASE(CHECKSUM);
		TYPECASE(GNU_verdef);
		TYPECASE(GNU_verneed);
		TYPECASE(GNU_versym);

#undef TYPECASE
	default: {
		// DBG_ERR("Unknown type: %u\n", type);
		return "UNKNOWN TYPE";
		// FIX: use the HI and LO in the types to properly handle all types
		// handle LOOS HIOS LOPROC HIPROC LOUSER HIUSER
	}
	}
}

const char *str_symbol_type(unsigned char type) {
	static const char *stt_names[STT_NUM] = {
	    [STT_NOTYPE] = "NOTYPE",
	    [STT_OBJECT] = "OBJECT",
	    [STT_FUNC] = "FUNC",
	    [STT_SECTION] = "SECTION",
	    [STT_FILE] = "FILE",
	    [STT_COMMON] = "COMMON",
	    [STT_TLS] = "TLS",
	};
	return stt_names[type];
}

const char *str_symbol_bind(unsigned char bind) {
	static const char *stb_names[STB_NUM] = {"LOCAL", "GLOBAL", "WEAK"};
	return stb_names[bind];
}

const char *str_symbol_visibility(unsigned char vis) {
	static const char *stv_others[4] = {
	    "DEFAULT",
	    "INTERNAL",
	    "HIDDEN",
	    "PROTECTED",
	};
	return stv_others[vis];
}

const char *str_osabi_name(unsigned char osabi) {
	switch (osabi) {
	case ELFOSABI_SYSV:
		return "System V";
	case ELFOSABI_HPUX:
		return "HP-UX";
	case ELFOSABI_NETBSD:
		return "NetBSD";
	case ELFOSABI_GNU:
		return "GNU/Linux";
	case ELFOSABI_SOLARIS:
		return "Sun Solaris";
	case ELFOSABI_AIX:
		return "IBM AIX";
	case ELFOSABI_IRIX:
		return "SGI Irix";
	case ELFOSABI_FREEBSD:
		return "FreeBSD.";
	case ELFOSABI_TRU64:
		return "Compaq TRU64 Unix";
	case ELFOSABI_MODESTO:
		return "Novell Modesto";
	case ELFOSABI_OPENBSD:
		return "OpenBSD.";
	case ELFOSABI_ARM_AEABI:
		return "ARM EABI";
	case ELFOSABI_ARM:
		return "ARM";
	case ELFOSABI_STANDALONE:
		return "Standalone (Embedded) Application";
	default:
		return "";
	}
}

const char *str_elf_filetype(Elf64_Half type) {
	const char *types[ET_NUM];
	types[ET_NONE] = "NONE (No file type)";
	types[ET_REL] = "REL (Relocatable file)";
	types[ET_EXEC] = "EXEC (Executable file)";
	types[ET_DYN] = "DYN (Shared object file)";
	types[ET_CORE] = "CORE (Core file)";
	if (type < ET_NUM)
		return types[type];
	else
		return "Unknown type";
}
