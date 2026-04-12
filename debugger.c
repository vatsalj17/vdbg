#define _GNU_SOURCE

#include "debugger.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <elfutils/libdwfl.h>
#include <libelf.h>
#include <signal.h>
#include <errno.h>

#include "commands.h"
#include "registers.h"
#include "map.h"
#include "util.h"

// enabling tab completion support for speed
char **my_completion(const char *text, int start, int end __attribute__((unused))) {
	rl_attempted_completion_over = 1;
	if (start == 0) {
		return rl_completion_matches(text, command_generator);
	}
	return NULL;
}

typedef struct DBG {
	char *process_name;     // the command
	pid_t pid;              // obvious
	map *breakpoints;       // hashtable of breakpoints
	uintptr_t load_address; // to be calc the offset in dyn executable
	int pendig_signal;      // for that irritating SIGSEGV only currently
	int file_descriptor;    // file opened to map in libelf
	Elf *elf_data;          // elf stuffs
	Dwfl *dwarf_data;       // unused
} debugger;

debugger *dbg_init(const char *pname, pid_t pid) {
	debugger *new = malloc(sizeof(debugger));
	if (!new) {
		perror("dbg_init");
		exit(EXIT_FAILURE);
	}
	int err = 0; // because i wan't see what libelf is doing

	elf_version(EV_CURRENT); // necessary because libelf just won't work

	new->pid = pid;
	new->process_name = strdup(pname);
	new->breakpoints = map_init(MAP_SIZE, bp_free);
	new->pendig_signal = 0;
	new->file_descriptor = open(new->process_name, O_RDONLY);
	if (new->file_descriptor == -1) {
		perror("open");
		abort();
	}
	new->elf_data = elf_begin(new->file_descriptor, ELF_C_READ_MMAP, NULL);
	if (new->elf_data == NULL) {
		printf("elf_begin: %s\n", elf_errmsg(err));
		abort();
	}
	new->load_address = 0; // initialized for static files initially

	return new;
}

// getter
pid_t dbg_get_pid(debugger *dbg) {
	return dbg->pid;
}

// to get the base address of dyn executable
void initialize_load_address(debugger *dbg) {
	Elf64_Ehdr *header = elf64_getehdr(dbg->elf_data);
	if (header->e_type == ET_DYN) {
		char path[100];
		snprintf(path, sizeof(path), "/proc/%d/maps", dbg->pid);
		FILE *file = fopen(path, "r");
		char *line = NULL;
		size_t len = 0;
		uintptr_t addr = 0;

		while (getline(&line, &len, file) != 0) {
			uintptr_t start_addr, end_addr;
			char perm[5];
			char executable_path[256] = {0};
			int parsed = sscanf(line,
			                    "%lx-%lx %4s %*x %*s %*d %255s",
			                    &start_addr,
			                    &end_addr,
			                    perm,
			                    executable_path);
			// if all of them are parsed correctly
			if (parsed == 4) {
				// r-xp
				if (perm[0] == 'r' && perm[2] == 'x') {
					addr = start_addr;
					break;
				}
			}
		}
		if (addr != 0) {
			dbg->load_address = addr;
		}
		free(line);
		fclose(file);
	}
}

uintptr_t offset_load_address(debugger *dbg, uintptr_t addr) {
	return addr - dbg->load_address;
}

void dbg_start(debugger *dbg) {
	// setting up my own commands for completion
	rl_attempted_completion_function = my_completion;

	wait_for_signal(dbg);
	initialize_load_address(dbg);

	char *input;
	while (1) {
		if ((input = readline("vdbg> ")) != NULL) {
			// avoid empty prompt
			if (input[0] != '\0') {
				add_history(input);
				handle_command(dbg, input);
			}
			free(input);
		} else {
			// if EOF
			if (dbg_kill_tracee(dbg)) {
				printf("Bye..\n");
				return;
			}
		}
	}
}

void set_breakpoint_at_addr(debugger *dbg, uintptr_t addr) {
	breakpoint *bp = bp_init(dbg->pid, addr);
	bp_enable(bp);
	if (map_insert(dbg->breakpoints, addr, bp)) {
		printf("Set breakpoint at addr 0x%lx ...\n", addr);
	} else {
		printf("Breakpoint already set at 0x%lx\n", addr);
		bp_free(bp);
	}
}

long read_memory(debugger *dbg, uintptr_t addr) {
	return ptrace(PTRACE_PEEKDATA, dbg->pid, addr, NULL);
}

void write_memory(debugger *dbg, uintptr_t addr, uintptr_t value) {
	ptrace(PTRACE_POKEDATA, dbg->pid, addr, value);
}

void handle_sigtrap(debugger *dbg, siginfo_t siginfo) {
	switch (siginfo.si_code) {
	case 0:
		// wierd behaviour encountered
		// when program starts it catches sigtrap
		// but with si_code set to zero. idk why?
		// people say exec sent it
		return;
	case SI_KERNEL:
	case TRAP_BRKPT: {
		// putting the pc back where it should be
		// -1 because execution will go past the breakpoint
		set_pc(dbg->pid, get_pc(dbg->pid) - 1);
		printf("Hit breakpoint at 0x%lx\n", get_pc(dbg->pid));
		// TODO: print source lines
		return;
	}
	// this will trigger when signal was sent by single stepping
	case TRAP_TRACE:
		return;
	default:
		printf("Unknown sigtrap code: %d, %d\n", siginfo.si_signo, siginfo.si_code);
		// printf("Unknown sigtrap code: %d\n", siginfo.si_code);
		return;
	}
}

siginfo_t get_signal_info(debugger *dbg) {
	siginfo_t info;
	if (ptrace(PTRACE_GETSIGINFO, dbg->pid, NULL, &info) == -1) {
		perror("get_signal_info");
		abort();
	}
	return info;
}

// the main signal handler of this debugger
void wait_for_signal(debugger *dbg) {
	int wait_status, option = 0;
	errno = 0;
	if (waitpid(dbg->pid, &wait_status, option) == -1) {
		if (errno == ECHILD)
			printf("!No process being traced!\n");
		else
			perror("waitpid");
		return;
	}
	if (WIFEXITED(wait_status)) {
		printf("Program exited gracefully with code %d\n", WEXITSTATUS(wait_status));
		return;
	}
	// if killed by an uncatchable signal like sigkill
	if (WIFSIGNALED(wait_status)) {
		printf("Program was terminated by the signal: %s\n", strsignal(WTERMSIG(wait_status)));
		return;
	}

	siginfo_t siginfo = get_signal_info(dbg);
	switch (siginfo.si_signo) {
	case SIGTRAP:
		handle_sigtrap(dbg, siginfo);
		break;
	case SIGSEGV: {
		printf("Caught Segfault! Reason code: %d\n", siginfo.si_code);
		// saving the signal to pass it to the tracee
		dbg->pendig_signal = siginfo.si_signo;
		break;
	}

	// just avoid these signals and send those to tracee silently
	// without giving user the prompt
	case SIGWINCH:
	case SIGCHLD:
	case SIGURG:
	case SIGALRM:
		ptrace(PTRACE_CONT, dbg->pid, NULL, siginfo.si_signo);
		break;
	default:
		printf("Got unhandled signal: %s\n", strsignal(siginfo.si_signo));
	}
}

void step_over_breakpoint(debugger *dbg) {
	breakpoint *bp = map_lookup(dbg->breakpoints, get_pc(dbg->pid));
	// if not null means we are currently hitted the breakpoint
	if (bp == NULL) return;
	// else
	// 	printf("found bp\n");

	if (bp_is_enabled(bp)) {
		bp_disable(bp);
		// single step forward to jump through the breakpoint instruction
		ptrace(PTRACE_SINGLESTEP, dbg->pid, NULL, NULL);
		wait_for_signal(dbg);
		bp_enable(bp);
	}
}

void continue_execution(debugger *dbg) {
	step_over_breakpoint(dbg);

	// getting a signal to send to tracee
	int sig = dbg->pendig_signal;
	dbg->pendig_signal = 0; // reset
	ptrace(PTRACE_CONT, dbg->pid, NULL, sig);

	// continue trapping signals
	wait_for_signal(dbg);
}

bool dbg_kill_tracee(debugger *dbg) {
	// if the process is still running
	if (dbg->pid && kill(dbg->pid, 0) == 0) {
		char *ans = readline("The child process is still running. Kill it? (y/n) ");
		if (ans && ans[0] == 'y') {
			ptrace(PTRACE_KILL, dbg->pid, NULL, NULL);
			waitpid(dbg->pid, NULL, 0);
		} else {
			free(ans);
			// if no then don't kill the process
			// continue debugging
			return false;
		}
		free(ans);
		printf("Killed tracee\n");
	}
	return true;
}

void dbg_free(debugger *dbg) {
	elf_end(dbg->elf_data);
	close(dbg->file_descriptor);
	map_free(dbg->breakpoints);
	free(dbg->process_name);
	free(dbg);
}
