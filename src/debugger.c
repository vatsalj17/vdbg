#include "debugger.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>
#include <libelf.h>
#include <dwarf.h>

#include "commands.h"
#include "registers.h"
#include "hashmap.h"
#include "util.h"
#include "macro.h"
#include "symbols.h"
#include "stepping.h"
#include "list.h"

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
	char *process_name; // the command
	pid_t pid;          // pid of tracee
	struct {
		char **list;          // list of arguments of the tracee
		size_t list_capacity; // capacity of the argments list
	} args;
	dbg_state state;               // tracking the state of debugger_t
	map_t *breakpoints;            // hashtable of <id, breakpoint_t>
	map_t *bp_locations;           // hashtable of <running_addr, bp_location_t>
	list_node pending_breakpoints; // list of pending breakpoints
	uintptr_t load_address;        // to be calc the offset in dyn executable
	int pending_signal;            // for that irritating SIGSEGV only currently
	dbg_symbols *syms;
} debugger_t;

debugger_t *dbg_init(const char *pname) {
	debugger_t *new = malloc(sizeof(debugger_t));
	if (!new) {
		perror("dbg_init");
		exit(EXIT_FAILURE);
	}

	new->syms = symbols_init(pname);
	if (!new->syms) {
		free(new);
		return NULL;
	}

	new->process_name = strdup(pname);
	new->pid = 0;

	new->args.list = calloc(MAX_ARGS, sizeof(char *));
	new->args.list[0] = new->process_name;
	new->args.list_capacity = MAX_ARGS;

	new->state = NOT_ACTIVE;
	new->breakpoints = map_init(MAP_SIZE, (void (*)(void *))bp_free);
	new->bp_locations = NULL;

	list_init(&new->pending_breakpoints);

	new->pending_signal = 0;
	new->load_address = 0; // initialized for static files initially

	return new;
}

// simple getters
pid_t dbg_get_pid(debugger_t *dbg) {
	return dbg->pid;
}

uintptr_t dbg_get_load_address(debugger_t *dbg) {
	return dbg->load_address;
}

dbg_state dbg_get_state(debugger_t *dbg) {
	return dbg->state;
}

bool dbg_is_active(debugger_t *dbg) {
	return (dbg->state == ACTIVE);
}

map_t *dbg_get_breakpoints(debugger_t *dbg) {
	return dbg->breakpoints;
}

map_t *dbg_get_locations(debugger_t *dbg) {
	return dbg->bp_locations;
}

dbg_symbols *dbg_get_symbols(debugger_t *dbg) {
	return dbg->syms;
}

list_node *dbg_get_pending_list(debugger_t *dbg) {
	return &dbg->pending_breakpoints;
}

breakpoint_t *get_breakpoint_by_id(debugger_t *dbg, uint64_t id) {
	return map_lookup(dbg->breakpoints, id);
}

void add_arguments_for_tracee(debugger_t *dbg, char **args_list) {
	size_t i = 1;
	// clear the existing arguments first
	while (dbg->args.list[i] != NULL) {
		free(dbg->args.list[i]);
		dbg->args.list[i] = NULL;
		i++;
	}

	// then append the other argments
	i = 1; // ignoring the first argument as it is the command itself
	while (args_list[i] != NULL) {
		// if it has reached it's limit
		if (i == dbg->args.list_capacity) {
			size_t new_cap = i * 2;
			char **new_list = realloc(dbg->args.list, new_cap * sizeof(char *));
			if (!new_list) {
				DBG_ERR("either less ram or lots of args");
				break;
			}
			dbg->args.list = new_list;
			dbg->args.list_capacity = new_cap;
		}
		char *str = strdup(args_list[i]);
		dbg->args.list[i++] = str;
	}
	DBG_LOG("added %zu arguments", i - 1);
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
	char *last_input = NULL;
	// the cli infinite loop
	while (1) {
		if ((input = readline(HBLK "[" BHMAG "vdbg" HBLK "]" BHYEL "❯ " RESET)) != NULL) {
			// avoid empty prompt
			if (input[0] == '\0') {
				DBG_LOG("empty cmdline. trying to run the last one if any");
				if (last_input) handle_command(dbg, last_input);
			} else {
				add_history(input);
				if (last_input) free(last_input);
				// saving the input so that we can rerun it
				last_input = strdup(input);
				handle_command(dbg, input);
			}
			free(input);
		} else {
			// if EOF
			if (dbg_kill_tracee(dbg)) {
				printf("Bye..\n");
				break;
			}
		}
	}
	if (last_input) free(last_input);
}

void enable_all_breakpoints(debugger_t *dbg) {
	// if (dbg->state == NOT_ACTIVE) return;
	list_node *pos;

	DBG_LOG("disabling all the breakpoints");
	list_for_each(pos, &dbg->pending_breakpoints) {
		breakpoint_t *bp = get_pending_bp_by_pos(pos);
		bp_enable(bp); // it will handle if it's already disabled
	}
}

void disable_all_breakpoints(debugger_t *dbg) {
	// if (dbg->state == NOT_ACTIVE) return;
	list_node *pos;

	DBG_LOG("disabling all the breakpoints");
	list_for_each(pos, &dbg->pending_breakpoints) {
		breakpoint_t *bp = get_pending_bp_by_pos(pos);
		bp_disable(bp); // it will handle if it's already disabled
	}
}

static void cleanup_at_tracee_death(debugger_t *dbg) {
	// deleting all breakpoints so that next time when it runs
	// i can enable it all
	// disable_all_locations(dbg);
	map_free(dbg->bp_locations);
	dbg->bp_locations = NULL;
	dbg->state = NOT_ACTIVE;
	dbg->load_address = 0;
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
		pc -= 1;
		bp_location_t *loc = map_lookup(dbg->bp_locations, pc);
		// printf("pc where we hit bp: %#lx\n", pc);
		set_pc(dbg->pid, pc);

		// print the message that breakpoint is hit
		print_bp_hit_message_at_loc(loc);

		print_source_at_current_pc(dbg->syms, pc, DEFAULT_LINE_CONTEXT);
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

static inline siginfo_t get_signal_info(debugger_t *dbg) {
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
		printf(BRED "!! " RESET "Caught " YEL "Segfault" RESET "! Reason: " CYN "%s" RESET "\n",
		       str_sigsegv_code(siginfo.si_code));
		print_source_at_current_pc(dbg->syms, get_pc(dbg->pid), 3);
		// saving the signal to pass it to the tracee
		dbg->pending_signal = siginfo.si_signo;
		break;
	}
	case SIGABRT: {
		// mirrored the SIGSEGV handling
		printf(BRED "!! " RESET "Tracee terminated by " YEL "SIGABRT" RESET "\n");
		// TODO: since this stops at the libc so can't print the source code yet
		// will make it work after adding support of shared library
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
	assert(dbg->bp_locations == NULL);

	// creating the map at the time of initialization of tracee
	// since we deleted it at cleanup
	dbg->bp_locations = map_init(MAP_SIZE, (void (*)(void *))loc_free);

	list_node *pos;

	list_for_each(pos, &dbg->pending_breakpoints) {
		breakpoint_t *bp = get_pending_bp_by_pos(pos);
		uintptr_t running_addr = bp_get_instr_addr(bp) + dbg->load_address;

		bp_location_t *loc = map_lookup(dbg->bp_locations, running_addr);
		if (loc) {
			loc_append_breakpoint_to_list(loc, bp);
		} else {
			loc = loc_init(dbg->pid, running_addr, bp, false);
			if (map_insert(dbg->bp_locations, running_addr, loc) == false) {
				// since we are already doing lookup before inserting
				// so map_insert shouldn't fail
				CRITICAL("Something is wrong in the logic");
			}
		}
	}
	DBG_LOG("resolved all");
}

static void spawn_tracee(debugger_t *dbg) {
	pid_t pid = fork();
	if (pid == 0) {
		ptrace(PTRACE_TRACEME, pid, NULL, NULL);
		personality(ADDR_NO_RANDOMIZE);
		execv(dbg->process_name, dbg->args.list);
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
	DBG_LOG("Restarting...");
	spawn_tracee(dbg);
}

static inline void add_and_set_bp_for_running_tracee(debugger_t *dbg, uintptr_t instr_addr,
                                                     breakpoint_t *bp) {
	if (dbg->state == NOT_ACTIVE) {
		return;
	}

	uintptr_t running_addr = instr_addr + dbg->load_address; // adding the offset fo pie
	bp_location_t *loc;

	// i think i should enable the breakpoint after inserting
	// instead of enabling before inserting
	// so that it's clear that we aren't doing it twice
	if ((loc = map_lookup(dbg->bp_locations, running_addr)) == NULL) {
		// if not found
		loc = loc_init(dbg->pid, running_addr, bp, false);
		map_insert(dbg->bp_locations, running_addr, loc);
	} else {
		// if location alread exist for a breakpoint
		loc_append_breakpoint_to_list(loc, bp);
	}
}

void set_breakpoint_at_addr(debugger_t *dbg, uintptr_t instr_addr, bool quiet) {
	// not calling this function during resolution of breakpoints

	breakpoint_t *bp = bp_init(BP_ADDR, NULL, instr_addr);

	add_breakpoint_as_pending(&dbg->pending_breakpoints, bp);
	uint64_t id = bp_get_id(bp);
	map_insert(dbg->breakpoints, id, bp);

	if (!quiet)
		printf("Set breakpoint at \"addr " YEL "%#lx" RESET "\" of id " BYEL "%lu" RESET " ...\n",
		       instr_addr,
		       id);

	add_and_set_bp_for_running_tracee(dbg, instr_addr, bp);
}

void set_breakpoint_at_func_symbol(debugger_t *dbg, const char *symbol_name) {
	size_t list_size = 0;
	size_t symtab_idx;
	Elf64_Sym *list = get_valid_func_symbols(dbg->syms, symbol_name, &list_size, &symtab_idx);
	if (list_size == 0) {
		printf("No function symbol found as \"%s\"\n", symbol_name);
	}
	for (size_t i = 0; i < list_size; i++) {
		// Dwarf_Addr *addrs;
		// int count = dwarf_entry_breakpoints(list[i], &addrs);
		// set_breakpoint_at_addr(dbg, list[i].st_value, true);
		char *func_name = elf_strptr(get_elf_data(dbg->syms), symtab_idx, list[i].st_name);
		uintptr_t instr_addr = list[i].st_value;
		breakpoint_t *bp = bp_init(BP_SYMBOL, (void *)symbol_name, instr_addr);
		uint64_t id = bp_get_id(bp);
		map_insert(dbg->breakpoints, id, bp);
		printf("Set breakpoint at \"function " YEL "%s" RESET "\" of id " BYEL "%lu" RESET " ...\n",
		       func_name,
		       id);
		add_breakpoint_as_pending(&dbg->pending_breakpoints, bp);
		add_and_set_bp_for_running_tracee(dbg, instr_addr, bp);
	}
	free(list);
}

void set_breakpoint_at_lineno(debugger_t *dbg, const char *filename, int lineno) {
	// if the file is not specified and the debugger is active then fetch the file
	// currently user is wandering around and set the breakpoint in that file
	if (!filename && dbg->state == ACTIVE) {
		Dwarf_Die *cudie = get_cudie_from_pc(dbg->syms, get_pc(dbg->pid));
		const char *name = dwarf_diename(cudie);
		if (name)
			filename = name;
		else
			return;
	}

	uintptr_t instr_addr = get_addr_from_lineno(dbg->syms, &filename, lineno);
	if (instr_addr == 0) return;

	file_line_pair pair = {
	    .file = strdup(filename),
	    .lineno = lineno,
	};
	breakpoint_t *bp = bp_init(BP_LINENO, (void *)&pair, instr_addr);
	uint64_t id = bp_get_id(bp);
	map_insert(dbg->breakpoints, id, bp);

	printf("Set breakpoint in \"file " YEL "%s" RESET " at line no. " YEL "%d" RESET
	       "\" of id " BYEL "%lu" RESET " ...\n",
	       filename,
	       lineno,
	       id);

	add_breakpoint_as_pending(&dbg->pending_breakpoints, bp);
	add_and_set_bp_for_running_tracee(dbg, instr_addr, bp);
}

bool set_temp_bp_location(debugger_t *dbg, uintptr_t running_addr) {
	assert(dbg->state == ACTIVE);
	bp_location_t *loc = loc_init(dbg->pid, running_addr, NULL, true);
	if (map_insert(dbg->bp_locations, running_addr, loc) == false) {
		DBG_LOG("bp_locations already set at %#lx", running_addr);
		DBG_LOG("freeing it");
		loc_free(loc);
		return false;
	}
	return true;
}

void unset_temp_bp_location(debugger_t *dbg, uintptr_t running_addr) {
	assert(dbg->state == ACTIVE);
	bp_location_t *loc = map_lookup(dbg->bp_locations, running_addr);
	if (loc == NULL) {
		fprintf(stderr, "wth are you disabling at: %#lx", running_addr);
		return;
	}
	DBG_LOG("Disabling temp bp_location at addr %#lx", running_addr);
	map_delete(dbg->bp_locations, running_addr);
}

void continue_execution(debugger_t *dbg) {
	assert(dbg->state == ACTIVE);

	step_over_breakpoint(dbg);

	// getting a signal to send to tracee
	int sig = dbg->pending_signal;
	dbg->pending_signal = 0; // RESET
	ptrace(PTRACE_CONT, dbg->pid, NULL, sig);

	// continue trapping signals
	wait_for_signal(dbg);
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

void remove_breakpoint(debugger_t *dbg, breakpoint_t *bp) {
	delete_breakpoint_from_pending(bp);
	map_delete(dbg->breakpoints, bp_get_id(bp));
	// remove breakpoint from it's location first
	// if the location becomes empty of breakpoints
	// then remove the location too (if running)
}

void remove_all_breakpoints(debugger_t *dbg) {
	list_node *pos, *n;
	// in this function we are literally deleting all breakpoints not just
	// disabling. so, we have to iterate through all the breakpoints, then
	// delete them

	DBG_LOG("removing all the breakpoints");
	list_for_each_safe(pos, n, &dbg->pending_breakpoints) {
		breakpoint_t *bp = get_pending_bp_by_pos(pos);
		uintptr_t running_addr = bp_get_instr_addr(bp) + dbg->load_address;

		delete_breakpoint_from_pending(bp);
		map_delete(dbg->bp_locations, running_addr);
		map_delete(dbg->breakpoints, bp_get_id(bp));
	}
}

void dbg_free(debugger_t *dbg) {
	DBG_LOG("freeing the debugger before exiting");
	map_free(dbg->breakpoints);

	assert(dbg->bp_locations == NULL); // assuming that it's freed

	for (int i = 1; dbg->args.list[i] != NULL; i++)
		free(dbg->args.list[i]);
	symbols_free(dbg->syms);
	free(dbg->process_name);
	free(dbg->args.list);
	free(dbg);
}
