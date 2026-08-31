#include "debugger.h"

#include <assert.h>
#include <elfutils/libdw.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <sys/stat.h>
#include <libelf.h>
#include <elfutils/libdwfl.h>
#include <dwarf.h>

#include "commands.h"
#include "registers.h"
#include "hashmap.h"
#include "util.h"
#include "breakpoint.h"
#include "macro.h"
#include "symbols.h"
#include "stepping.h"

#define MAP_SIZE 1024
#define MAX_ARGS 50

// enabling tab completion support for speed
static char **my_completion(const char *text, int start, int end __attribute__((unused))) {
	rl_attempted_completion_over = 1;
	if (start == 0) {
		return rl_completion_matches(text, command_generator);
	}
	return NULL;
}

typedef struct Debugger {
	char *process_name;                  // the command
	char **args;                         // arguments of the tracee
	pid_t pid;                           // obvious
	dbg_state state;                     // tracking the state of debugger_t
	map_t *breakpoints;                  // hashtable of breakpoints
	struct bp_list *pending_breakpoints; // list of pending breakpoints
	uintptr_t load_address;              // to be calc the offset in dyn executable
	int pending_signal;                  // for that irritating SIGSEGV only currently
	dbg_symbols *syms;
} debugger_t;

debugger_t *dbg_init(const char *pname) {
	debugger_t *new = malloc(sizeof(debugger_t));
	if (!new) {
		perror("dbg_init");
		exit(EXIT_FAILURE);
	}

	new->pid = 0;
	new->process_name = strdup(pname);
	new->args = calloc(MAX_ARGS, sizeof(char *));
	new->args[0] = new->process_name;

	new->state = NOT_ACTIVE;
	new->breakpoints = map_init(MAP_SIZE, bp_free);
	new->pending_breakpoints = list_queue_init();
	new->pending_signal = 0;
	new->load_address = 0; // initialized for static files initially

	new->syms = symbols_init(pname);

	return new;
}

// simple getter
pid_t dbg_get_pid(debugger_t *dbg) {
	return dbg->pid;
}

uintptr_t dbg_get_load_address(debugger_t *dbg) {
	return dbg->load_address;
}

bool dbg_is_active(debugger_t *dbg) {
	return (dbg->state == ACTIVE);
}

map_t *dbg_get_breakpoints(debugger_t *dbg) {
    return dbg->breakpoints;
}

dbg_symbols *dbg_get_symbols(debugger_t *dbg) {
	return dbg->syms;
}

uintptr_t offset_load_address(debugger_t *dbg, uintptr_t addr) {
	return addr - dbg->load_address;
}

void add_arguments_for_tracee(debugger_t *dbg, char **args) {
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
	DBG_LOG("added %d arguments", i);
}

void dbg_start(debugger_t *dbg) {
	// setting up my own commands for completion
	rl_attempted_completion_function = my_completion;

	if (!has_dwarf_symbols(dbg->syms)) {
		printf(RED "\n[" BYEL " Warning: " RESET "this executable doesn't contain debug symbols" RED
		           " ]\n");
		printf("[\t " RESET "  pls recompile the code with -g flag " RED " \t ]\n" RESET);
	}

	char *input;
	char last_input[100] = {0};
	// the cli infinite loop
	while (1) {
		if ((input = readline(HBLK "[" BHMAG "vdbg" HBLK "]" BHYEL "❯ " RESET)) != NULL) {
			// avoid empty prompt
			if (input[0] == '\0') {
				DBG_LOG("empty cmdline. trying to run the last one if any");
				if (last_input[0]) handle_command(dbg, last_input);
			} else {
				add_history(input);
				// saving the input so that we can rerun it
				if (strlen(input) > 100)
					last_input[0] = 0;
				else
					strncpy(last_input, input, 100);
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

static void cleanup_at_tracee_death(debugger_t *dbg) {
	// disabling all breakpoints so that next time when it runs
	// i can enable it all
	dbg->state = NOT_ACTIVE;
	disable_all_breakpoints(dbg);
}

static void kill_tracee(debugger_t *dbg) {
	ptrace(PTRACE_KILL, dbg->pid, NULL, NULL);
	waitpid(dbg->pid, NULL, 0);
	cleanup_at_tracee_death(dbg);
	DBG_LOG("this tracee is killed. new one is going to start");
}

static void handle_sigtrap(debugger_t *dbg, siginfo_t siginfo) {
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
		uintptr_t pc = get_pc(dbg->pid);
		breakpoint_t *bp = map_lookup(dbg->breakpoints, pc - 1);
		set_pc(dbg->pid, pc - 1);

		// not printing the message if the breakpoint
		if (!bp_is_temp(bp))
			printf("Hit breakpoint at " BRED "%#lx" RESET "\n",
			       offset_load_address(dbg, get_pc(dbg->pid)));
		else
			DBG_LOG("Hit temperory breakpoint at %#lx",
			        offset_load_address(dbg, get_pc(dbg->pid)));

		print_source_at_current_pc(dbg->syms, get_pc(dbg->pid));

		return;
	}
	// this will trigger when signal was sent by single stepping
	case TRAP_TRACE:
		DBG_LOG("Single stepping caught");
		// printing the source code for single stepping in commands.c
		return;
	default:
		printf("Unknown sigtrap code: %d, %d\n", siginfo.si_signo, siginfo.si_code);
		return;
	}
}

static siginfo_t get_signal_info(debugger_t *dbg) {
	siginfo_t info;
	if (ptrace(PTRACE_GETSIGINFO, dbg->pid, NULL, &info) == -1) {
		CRITICAL_PERROR("get_signal_info");
	}
	return info;
}

// the main signal handler of this debugger_t
void wait_for_signal(debugger_t *dbg) {
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
		printf(BRED "!! " RESET "Caught " YEL "Segfault" RESET "! Reason: " CYN "%s\n" RESET,
		       str_sigsegv_code(siginfo.si_code));
		// saving the signal to pass it to the tracee
		dbg->pending_signal = siginfo.si_signo;
		break;
	}
	case SIGABRT: {
		// mirrored the SIGSEGV handling
		printf("Tracee terminated by SIGABRT\n");
		dbg->pending_signal = siginfo.si_signo;
		break;
	}

	// just avoid these signals and send to tracee silently
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

static void resolve_pending_breakpoints(debugger_t *dbg) {
	DBG_LOG("resolving all the breakpoints in the list");
	size_t i = 0;
	uintptr_t addr;
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		addr += dbg->load_address;
		DBG_LOG("resolving %#lx", addr);
		breakpoint_t *bp = map_lookup(dbg->breakpoints, addr);
		if (bp) {
			bp_set_pid(map_lookup(dbg->breakpoints, addr), dbg->pid);
			bp_enable(map_lookup(dbg->breakpoints, addr));
		} else {
			bp = bp_init(dbg->pid, addr, false);
			if (map_insert(dbg->breakpoints, addr, bp)) {
				bp_enable(bp);
			} else {
				DBG_LOG("Breakpoint already set at %#lx", addr);
				DBG_LOG("freeing this breakpoint");
				bp_free(bp);
			}
		}
		i++;
	}
}

static void spawn_tracee(debugger_t *dbg) {
	pid_t pid = fork();
	if (pid == 0) {
		ptrace(PTRACE_TRACEME, pid, NULL, NULL);
		personality(ADDR_NO_RANDOMIZE);
		execv(dbg->process_name, dbg->args);
	} else {
		DBG_LOG("Running %s ....", dbg->process_name);
		dbg->pid = pid;
		wait_for_signal(dbg);
		dbg->load_address = initialize_load_address(dbg->syms, dbg->pid);
		dbg->state = ACTIVE;
		resolve_pending_breakpoints(dbg);
		setup_dwfl(dbg->syms, dbg->pid);
		continue_execution(dbg);
	}
}

void run(debugger_t *dbg) {
	if (dbg->state == ACTIVE) {
		char *ans = readline(BRED "!! " RESET "Process is already being debugged.\n"
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

void restart(debugger_t *dbg) {
	kill_tracee(dbg);
	spawn_tracee(dbg);
}

// it takes the address of the instruction as shown in the
// disassembly of the executable
void set_breakpoint_at_addr(debugger_t *dbg, uintptr_t addr, bool quiet) {
	// not calling this function during resolution of breakpoints

	add_breakpoint_as_pending(dbg->pending_breakpoints, addr);
	if (!quiet) printf("Set breakpoint at addr " YEL "%#lx" RESET " ...\n", addr);

	// program is not running
	if (dbg->state == NOT_ACTIVE) {
		return;
	}

	addr += dbg->load_address; // adding the offset fo pie

	breakpoint_t *bp = bp_init(dbg->pid, addr, false);

	// i think i should enable the breakpoint after inserting
	// instead of enabling before inserting
	// so that it's clear that we aren't doing it twice
	if (map_insert(dbg->breakpoints, addr, bp)) {
		bp_enable(bp);
	} else {
		DBG_LOG("Breakpoint already set at %#lx", addr);
		DBG_LOG("freeing this breakpoint");
		bp_free(bp);
	}
}

void unset_breakpoint_at_addr(debugger_t *dbg, uintptr_t addr) {
	uintptr_t actual_addr = addr + dbg->load_address;
	breakpoint_t *found_bp = map_lookup(dbg->breakpoints, actual_addr);
	if (found_bp == NULL) {
		fprintf(stderr, "No breakpoint found at addr: %#lx\n", addr);
		return;
	}
	DBG_LOG("Disabling breakpint at addr %#lx", addr);
	bp_disable(found_bp);
	delete_breakpoint_from_pending(dbg->pending_breakpoints, addr);
}

void set_breakpoint_at_func_symbol(debugger_t *dbg, const char *symbol_name) {
    size_t list_size = 0;
    Elf64_Sym *list = get_func_symbols(dbg->syms, symbol_name, &list_size);
    for (size_t i = 0; i < list_size; i++) {
        set_breakpoint_at_addr(dbg, list[i].st_value, true);
    }
    free(list);
}

// it takes the actual virtual address of the running program
// it will return false if a breakpoint is already set at the
// place i am wanting to set the temp bp
bool set_temp_breakpoint(debugger_t *dbg, uintptr_t running_addr) {
	breakpoint_t *bp = bp_init(dbg->pid, running_addr, true);
	if (map_insert(dbg->breakpoints, running_addr, bp)) {
		bp_enable(bp);
	} else {
		DBG_LOG("Breakpoint already set at %#lx", running_addr);
		DBG_LOG("freeing this breakpoint");
		bp_free(bp);
		return false;
	}
	return true;
}

void unset_temp_breakpoint(debugger_t *dbg, uintptr_t running_addr) {
	breakpoint_t *found_bp = map_lookup(dbg->breakpoints, running_addr);
	if (found_bp == NULL) {
		fprintf(stderr, "wth are you disabling at: %#lx", running_addr);
		return;
	}
	DBG_LOG("Disabling breakpint at addr %#lx", running_addr);
	bp_disable(found_bp);
	map_delete(dbg->breakpoints, running_addr);
}

void enable_breakpoint(debugger_t *dbg, uintptr_t addr) {
	breakpoint_t *found_bp = map_lookup(dbg->breakpoints, addr);
	if (found_bp == NULL) {
		fprintf(stderr, "No breakpoint found at addr: %#lx\n", addr);
		return;
	}
	bp_enable(found_bp);
}

void disable_breakpoint(debugger_t *dbg, uintptr_t addr) {
	breakpoint_t *found_bp = map_lookup(dbg->breakpoints, addr);
	if (found_bp == NULL) {
		fprintf(stderr, "No breakpoint found at addr: %#lx\n", addr);
		return;
	}
	bp_disable(found_bp);
}

void continue_execution(debugger_t *dbg) {
	if (dbg->state == NOT_ACTIVE) {
		printf("No running debug session\n");
		return;
	}

	step_over_breakpoint(dbg);

	// getting a signal to send to tracee
	int sig = dbg->pending_signal;
	dbg->pending_signal = 0; // RESET
	ptrace(PTRACE_CONT, dbg->pid, NULL, sig);

	// continue trapping signals
	wait_for_signal(dbg);
}

void disable_all_breakpoints(debugger_t *dbg) {
	size_t i = 0;
	uintptr_t addr;
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		addr += dbg->load_address;
		breakpoint_t *bp = map_lookup(dbg->breakpoints, addr);
		bp_disable(bp);
		i++;
	}
}

bool dbg_kill_tracee(debugger_t *dbg) {
	// if the process is still running
	if (dbg->state == ACTIVE && kill(dbg->pid, 0) == 0) {
		char *ans =
		    readline(BRED "!! " RESET "The child process is still running. Kill it? (y/n) ");
		DBG_LOG("response: %s", ans);
		if (!ans || ans[0] == 'y') {
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

void remove_all_breakpoints(debugger_t *dbg) {
	size_t i = 0;
	uintptr_t addr;
	bool process_is_running = (kill(dbg->pid, 0) == 0);
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		addr += dbg->load_address;
		breakpoint_t *bp = map_lookup(dbg->breakpoints, addr);
		if (process_is_running) bp_disable(bp);
		map_delete(dbg->breakpoints, addr);
		i++;
	}

	list_clear(dbg->pending_breakpoints);
}

void dbg_free(debugger_t *dbg) {
	DBG_LOG("freeing the debugger_t before exiting");
	map_free(dbg->breakpoints);
	list_free(dbg->pending_breakpoints);
	for (int i = 1; dbg->args[i] != NULL; i++)
		free(dbg->args[i]);
	symbols_free(dbg->syms);
	free(dbg->process_name);
	free(dbg->args);
	free(dbg);
}
