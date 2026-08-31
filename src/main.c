#include <stdio.h>
#include <sys/ptrace.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/personality.h>
#include "debugger.h"
#include "macro.h"

// TODO: loading of dynamic libraires
// TODO: add disassembly feature

int main(int argc, char **argv) {
#if defined(__linux__) && defined(__x86_64__)
	if (argc != 2) {
		fprintf(stderr, "usage: %s <executable>\n", argv[0]);
		return 1;
	}

	printf(BHYEL "$$ " BHGRN "Loaded " BHWHT "%s" BHGRN " into the debugger..." RESET "\n",
	       argv[1]);
	debugger_t *dbg = dbg_init(argv[1]);
	dbg_start(dbg);
	dbg_free(dbg);

#else
	fprintf(stderr, "This debugger is not supported on your device\n");
	fprintf(stderr, "Only supported on => OS: linux & Arch: x86_64\n");
	return 1;
#endif
}
