#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <asm-generic/siginfo.h>

#include "util.h"
#include "macro.h"
#include "commands.h"

#define BUFSIZE 32

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

char **split(char *str, char delim) {
	char **tokens = calloc(BUFSIZE, sizeof(char *));
	char *delimiter = (char[]){delim, '\0'};
	char *token = strtok(str, delimiter);
	int count = 0;
	while (token != NULL) {
		tokens[count++] = token;
		token = strtok(NULL, delimiter);
	}
	// for (int i = 0; tokens[i] != NULL; i++) {
	//     printf("%s\n", tokens[i]);
	// }
	return tokens;
}

bool is_prefix(const char *input, const char *command) {
	if (!command || !input) return false;
	// printf("is_prefix: command: %s, input: %s\n", command, input);
	if (strncmp(command, input, strlen(input)) == 0) return true;
	return false;
}

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
