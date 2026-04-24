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
#include <sys/personality.h>

#include "commands.h"
#include "registers.h"
#include "hashmap.h"
#include "util.h"
#include "breakpoint.h"
#include "colors.h"

#define MAP_SIZE 1024
#define MAX_ARGS 50

// enabling tab completion support for speed
char **my_completion(const char *text, int start, int end __attribute__((unused))) {
	rl_attempted_completion_over = 1;
	if (start == 0) {
		return rl_completion_matches(text, command_generator);
	}
	return NULL;
}

// TODO: dwarf parsing

typedef struct DBG {
	char *process_name;           // the command
	char **args;                  // arguments of the tracee
	pid_t pid;                    // obvious
	state state;                  // tracking the state of debugger
	map *breakpoints;             // hashtable of breakpoints
	bp_list *pending_breakpoints; // list of pending breakpoints
	uintptr_t load_address;       // to be calc the offset in dyn executable
	int pendig_signal;            // for that irritating SIGSEGV only currently
	int file_descriptor;          // file opened to map in libelf
	Elf *elf_data;                // elf stuffs
	Dwfl *dwarf_data;             // unused yet, for future purpose
} debugger;

debugger *dbg_init(const char *pname) {
	debugger *new = malloc(sizeof(debugger));
	if (!new) {
		perror("dbg_init");
		exit(EXIT_FAILURE);
	}
	int err = 0; // because i wan't to see what libelf is doing

	elf_version(EV_CURRENT); // necessary because libelf just won't work

	new->pid = 0;
	new->process_name = strdup(pname);
    new->args = calloc(MAX_ARGS, sizeof(char *));
    new->args[0] = new->process_name;
	new->state = NOT_ACTIVE;
	new->breakpoints = map_init(MAP_SIZE, bp_free);
	new->pending_breakpoints = list_queue_init();
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

// simple getter
pid_t dbg_get_pid(debugger *dbg) {
	return dbg->pid;
}

bool dbg_is_active(debugger *dbg) {
	return (dbg->state == ACTIVE);
}

// to get the base address of dyn executable
static void initialize_load_address(debugger *dbg) {
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

void add_arguments_for_tracee(debugger *dbg, char **args) {
    int i = 1;
    while (dbg->args[i] != NULL) { 
        free(dbg->args[i]);
        dbg->args[i] = NULL;
        i++;
    }
    i = 0;
    int count = 1;
    while (args[i] != NULL) {
        char *str = strdup(args[i]);
        dbg->args[count++] = str;
        i++;
    }
#ifdef DEBUG 
    printf("DEBUG: added %d arguments\n", i);
#endif
}

void dbg_start(debugger *dbg) {
	// setting up my own commands for completion
	rl_attempted_completion_function = my_completion;

	char *input;
	// the cli infinite loop
	while (1) {
		if ((input = readline(HBLK "[" BHMAG "vdbg" HBLK "]" BHYEL "❯ " reset)) != NULL) {
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

static void kill_tracee(debugger *dbg) {
	ptrace(PTRACE_KILL, dbg->pid, NULL, NULL);
	waitpid(dbg->pid, NULL, 0);
	cleanup_at_tracee_death(dbg);
#ifdef DEBUG
	printf("DEBUG: this tracee is killed. new one is going to start\n");
#endif
}

static void spawn_tracee(debugger *dbg) {
	pid_t pid = fork();
	if (pid == 0) {
		ptrace(PTRACE_TRACEME, pid, NULL, NULL);
		personality(ADDR_NO_RANDOMIZE);
		execv(dbg->process_name, dbg->args);
	} else {
#ifdef DEBUG
		printf("Running %s ....\n", dbg->process_name);
#endif
		dbg->pid = pid;
		wait_for_signal(dbg);
		initialize_load_address(dbg);
		dbg->state = ACTIVE;
		resolve_pending_breakpoints(dbg);
		continue_execution(dbg);
	}
}

void run(debugger *dbg) {
	if (dbg->state == ACTIVE) {
		char *ans = readline(BRED "!! " reset "Process is already being debugged.\n"
		                          "   Would you like to restart? (y/n) ");
		if (ans && ans[0] == 'y') {
			kill_tracee(dbg);
		} else {
			free(ans);
			return;
		}
		free(ans);
	}

	spawn_tracee(dbg);
}

void restart(debugger *dbg) {
	kill_tracee(dbg);
	spawn_tracee(dbg);
}

void resolve_pending_breakpoints(debugger *dbg) {
	size_t i = 0;
	uintptr_t addr;
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		if (map_it_exsists(dbg->breakpoints, addr)) {
			bp_set_pid(map_lookup(dbg->breakpoints, addr), dbg->pid);
			bp_enable(map_lookup(dbg->breakpoints, addr));
		} else {
			breakpoint *bp = bp_init(dbg->pid, addr);
			if (map_insert(dbg->breakpoints, addr, bp)) {
				bp_enable(bp);
			} else {
#ifdef DEBUG
				printf("DEBUG: Breakpoint already set at 0x%lx\n", addr);
				printf("DEBUG: freeing this breakpoint\n");
#endif
				bp_free(bp);
			}
		}
		i++;
	}
}

void set_breakpoint_at_addr(debugger *dbg, uintptr_t addr) {
	// not calling this function during resolution of breakpoints

	// TODO: add support of breakpoints in pie

	add_breakpoint_as_pending(dbg->pending_breakpoints, addr);
	printf("Set breakpoint at addr " YEL "0x%lx" reset " ...\n", addr);

	// program is not running
	if (dbg->state == NOT_ACTIVE) {
		return;
	}

	breakpoint *bp = bp_init(dbg->pid, addr);

	// i think i should enable the breakpoint after inserting
	// instead of enabling before inserting
	// so that it's clear that we aren't doing it twice
	if (map_insert(dbg->breakpoints, addr, bp)) {
		bp_enable(bp);
	} else {
#ifdef DEBUG
		printf("DEBUG: Breakpoint already set at 0x%lx\n", addr);
		printf("DEBUG: freeing this breakpoint\n");
#endif
		bp_free(bp);
	}
}

void unset_breakpoint_at_addr(debugger *dbg, uintptr_t addr) {
	breakpoint *found_bp = map_lookup(dbg->breakpoints, addr);
	if (found_bp == NULL) {
		fprintf(stderr, "No breakpoint found at addr: 0x%lx\n", addr);
		return;
	}
#ifdef DEBUG
	printf("DEBUG: Disabling breakpint at addr 0x%lx\n", addr);
#endif
	bp_disable(found_bp);
	delete_breakpoing_from_pending(dbg->pending_breakpoints, addr);
}

void enable_breakpoint(debugger *dbg, uintptr_t addr) {
	breakpoint *found_bp = map_lookup(dbg->breakpoints, addr);
	if (found_bp == NULL) {
		fprintf(stderr, "No breakpoint found at addr: 0x%lx\n", addr);
		return;
	}
	bp_enable(found_bp);
}

void disable_breakpoint(debugger *dbg, uintptr_t addr) {
	breakpoint *found_bp = map_lookup(dbg->breakpoints, addr);
	if (found_bp == NULL) {
		fprintf(stderr, "No breakpoint found at addr: 0x%lx\n", addr);
		return;
	}
	bp_disable(found_bp);
}

static void handle_sigtrap(debugger *dbg, siginfo_t siginfo) {
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
		printf("Hit breakpoint at " BRED "0x%lx\n" reset, get_pc(dbg->pid));
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

static siginfo_t get_signal_info(debugger *dbg) {
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
			printf("!! No process being traced !!\n");
		else
			perror("waitpid");
		return;
	}
	if (WIFEXITED(wait_status)) {
		printf("Program exited gracefully with code %d\n", WEXITSTATUS(wait_status));
		cleanup_at_tracee_death(dbg);
		return;
	}
	// if killed by an uncatchable signal like sigkill
	if (WIFSIGNALED(wait_status)) {
		printf("Program was terminated by the signal: %s\n", strsignal(WTERMSIG(wait_status)));
		cleanup_at_tracee_death(dbg);
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
	// if not null means we have currently hitted the breakpoint
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
	if (dbg->state == NOT_ACTIVE) {
		printf("No running debug session\n");
		return;
	}

	step_over_breakpoint(dbg);

	// getting a signal to send to tracee
	int sig = dbg->pendig_signal;
	dbg->pendig_signal = 0; // reset
	ptrace(PTRACE_CONT, dbg->pid, NULL, sig);

	// continue trapping signals
	wait_for_signal(dbg);
}

void disable_all_breakpoints(debugger *dbg) {
	size_t i = 0;
	uintptr_t addr;
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		breakpoint *bp = map_lookup(dbg->breakpoints, addr);
		bp_disable(bp);
		i++;
	}
}

bool dbg_kill_tracee(debugger *dbg) {
	// if the process is still running
	if (dbg->state == ACTIVE && kill(dbg->pid, 0) == 0) {
		char *ans =
		    readline(BRED "!! " reset "The child process is still running. Kill it? (y/n) ");
		if (ans && ans[0] == 'y') {
			kill_tracee(dbg);
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

void cleanup_at_tracee_death(debugger *dbg) {
	disable_all_breakpoints(dbg);
	dbg->state = NOT_ACTIVE;
}

void remove_all_breakpoints(debugger *dbg) {
	size_t i = 0;
	uintptr_t addr;
	bool process_is_running = (kill(dbg->pid, 0) == 0);
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		breakpoint *bp = map_lookup(dbg->breakpoints, addr);
		if (process_is_running) bp_disable(bp);
		map_delete(dbg->breakpoints, addr);
		i++;
	}

	list_clear(dbg->pending_breakpoints);
}

void dbg_free(debugger *dbg) {
#ifdef DEBUG
	printf("DEBUG: freeing the debugger before exiting\n");
#endif
	elf_end(dbg->elf_data);
	close(dbg->file_descriptor);
	map_free(dbg->breakpoints);
	list_free(dbg->pending_breakpoints);
    for (int i = 1; dbg->args[i] != NULL; i++) free(dbg->args[i]);
	free(dbg->process_name);
    free(dbg->args);
	free(dbg);
}
