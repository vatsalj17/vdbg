#include <readline/rlstdc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/personality.h>

#include "commands.h"
#include "debugger.h"
#include "macro.h"

// TODO: loading of dynamic libraires
// TODO: add disassembly feature

void print_help(void) {
	printf("Usage: vdbg [options] <executable>\n\n"
	       "Options:\n"
	       "  -ex, --execute <command>  Execute a command on startup\n"
	       "       --script <file>      Execute commands from a script\n"
	       "  -h,  --help               Show this help message\n\n"
	       "Examples:\n"
	       "  vdbg program\n"
	       "  vdbg -ex \"break main\" program\n"
	       "  vdbg --script commands.vdbg program\n");
}

bool handle_arguments(int argc, char **argv, char **executable, char ***command_list, size_t *size,
                      char **script_path) {
	char **list = malloc((size_t)argc * sizeof(char *));
	if (!list) {
		printf("buy more ram! or pass less arguments\n");
		return false;
	}
	*command_list = list;
	size_t list_iter = 0;
	char *path = NULL;
	char *script = NULL;

	for (int i = 1; i < argc; i++) {
		// printf("argv[%d]: %s", i, argv[i]);
		if (argv[i][0] == '-') {
			if (!strcmp("-ex", argv[i]) || !strcmp("--execute", argv[i])) {
				if (script) {
					ARG_ERR("Can't have -ex along with script");
					goto err_cleanup;
				}
				if (i + 1 >= argc) {
					ARG_ERR("passing incomplete option: %s", argv[i]);
					goto err_cleanup;
				}
				if (argv[i + 1][0] == '-') {
					ARG_ERR("invalid commands passed along with %zuth -ex", list_iter + 1);
					goto err_cleanup;
				}
				list[list_iter++] = argv[++i];
			} else if (!strcmp("--script", argv[i])) {
				if (list_iter > 0) {
					ARG_ERR("Can't give script along with -ex");
					goto err_cleanup;
				}
				if (script) {
					ARG_ERR("Can't pass multiple scripts");
					goto err_cleanup;
				}
				if (i + 1 >= argc) {
					ARG_ERR("passing incomplete option: %s", argv[i]);
					goto err_cleanup;
				}
				if (argv[i + 1][0] == '-') {
					ARG_ERR("invalid script passed");
					goto err_cleanup;
				}
				script = argv[++i];
			} else if (!strcmp("--help", argv[i]) || !strcmp("-h", argv[i])) {
				print_help();
				free(list);
				return true;
			} else {
				ARG_ERR("wrong option: %s", argv[i]);
				goto err_cleanup;
			}
		} else {
			if (path) {
				ARG_ERR("Maybe you are passing multiple executable");
				goto err_cleanup;
			}
			path = argv[i];
		}
	}

	if (!path) {
		ARG_ERR("No executable found");
		goto err_cleanup;
	}
	if (script) {
		*script_path = script;
	}

	*size = list_iter;
	*executable = path;
	return true;

err_cleanup:
	free(list);
	return false;
}

void run_script(debugger_t *dbg, char *script_path) {
	FILE *fp = fopen(script_path, "r");
	if (!fp) {
		fprintf(stderr, "Invalid script path: %s\n", script_path);
		perror("fopen");
		return;
	}

	char *line = NULL;
	size_t len = 0;
	while (getline(&line, &len, fp) != EOF) {
		if (line[0] == '#') continue; // using # as comment
		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '\0') continue; // skip empty lines
		handle_command(dbg, line);
	}

	free(line);
	fclose(fp);
}

int main(int argc, char **argv) {
#if defined(__linux__) && defined(__x86_64__)
	if (argc < 2) {
		fprintf(stderr, "usage: %s [options] <executable>\n", argv[0]);
		fprintf(stderr, "try '%s --help' for more\n", argv[0]);
		return 1;
	}

	char **command_list = NULL;
	size_t list_size = 0;
	char *script = NULL;
	char *executable = NULL;
	bool ret_val = handle_arguments(argc, argv, &executable, &command_list, &list_size, &script);
	if (!executable) return !ret_val; // true(1) -> 0  & false(0) -> 1

	debugger_t *dbg = dbg_init(executable);
	if (!dbg) return 1;

	printf(BHYEL "$$ " BHGRN "Loaded " BHWHT "%s" BHGRN " into the debugger..." RESET "\n",
	       executable);

	if (script) {
		run_script(dbg, script);
	} else {
		for (size_t i = 0; i < list_size; i++) {
			handle_command(dbg, command_list[i]);
		}
	}
	free(command_list);

	dbg_start(dbg);
	dbg_free(dbg);

#else
	fprintf(stderr, "This debugger is not supported on your device\n");
	fprintf(stderr, "Only supported on => OS: linux & Arch: x86_64\n");
	return 1;
#endif
}
