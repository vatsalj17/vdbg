#include <stdio.h>
#include <sys/ptrace.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/personality.h>
#include "debugger.h"

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <executable>\n", argv[0]);
		return 1;
	}

	pid_t pid = fork();
	if (pid == 0) {
	    ptrace(PTRACE_TRACEME, 0, NULL, NULL);
	    personality(ADDR_NO_RANDOMIZE);
	    execl(argv[1], argv[1], NULL);
	} else {
	    printf("Debugging %s ....\n", argv[1]);
	    debugger* dbg = dbg_init(argv[1], pid);
	    dbg_start(dbg);
	    dbg_free(dbg);
	}
}
