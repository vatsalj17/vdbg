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

	char c;
	unsigned current_line = 1u;
	while (current_line < start_line && (c = (char)fgetc(f)) != EOF) {
		if (c == '\n') current_line++;
	}

	printf("\n  %s%3d | ", (current_line == line) ? HGRN "> " GRN : HBLK "  ", current_line);
	while (current_line < end_line && (c = (char)fgetc(f)) != EOF) {
		putc(c, stdout);
		if (c == '\n') {
			current_line++;
			printf("  %s%3d | ", (current_line == line) ? BGRN "> " GRN : HBLK "  ", current_line);
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
