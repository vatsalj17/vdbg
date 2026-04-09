#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "util.h"

#define BUFSIZE 32

char** split(char* str, char delim) {
    char** tokens = calloc(BUFSIZE, sizeof(char*));
    char* delimiter = malloc(2);
    snprintf(delimiter, 2, "%c", delim);
    char* token = strtok(str, delimiter);
    int count = 0;
    while (token != NULL) {
        tokens[count++] = token;
        token = strtok(NULL, delimiter);
    }
    // for (int i = 0; tokens[i] != NULL; i++) {
    //     printf("%s\n", tokens[i]);
    // }
    free(delimiter);
    return tokens;
}

bool is_prefix(char* command, char* input) {
	if (!command || !input) return false;
	// printf("is_prefix: command: %s, input: %s\n", command, input);
	if (strncmp(command, input, strlen(command)) == 0) return true;
	return false;
}

void print_source(const char *file_name, unsigned line, unsigned lines_context) {
    FILE *f = fopen(file_name, "r");
    unsigned start_line = line <= lines_context ? 1 : line - lines_context;
    unsigned end_line = line + lines_context + (line < lines_context ? lines_context - line : 0) + 1;
    char c;
    unsigned current_line = 1u;
    while (current_line <= start_line && (c = fgetc(f)) != EOF) {
        if (c == '\n') current_line++;
    }
    printf("%s", (current_line == line) ? "> " : "  ");
    while (current_line < end_line && (c = fgetc(f)) != EOF) {
        putc(c, stdout);
        if (c == '\n') {
            current_line++;
            printf("%s", (current_line == line) ? "> " : "  ");
        }
    }
    fclose(f);
}
