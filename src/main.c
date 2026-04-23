#include <stdio.h>
#include <sys/ptrace.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/personality.h>
#include "debugger.h"
#include "colors.h"

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <executable>\n", argv[0]);
		return 1;
	}

	printf(BHYEL "$$ " BHGRN "Loaded " BHWHT "%s" BHGRN " into the debugger..." CRESET "\n",
	       argv[1]);
	debugger *dbg = dbg_init(argv[1]);
	dbg_start(dbg);
	dbg_free(dbg);
}
