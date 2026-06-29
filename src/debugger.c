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

#define MAP_SIZE 1024
#define MAX_ARGS 50

// for initializing dwfl
static const Dwfl_Callbacks callbacks = {
    .find_elf = dwfl_linux_proc_find_elf,
    .find_debuginfo = dwfl_standard_find_debuginfo,
    .debuginfo_path = NULL,
};

// enabling tab completion support for speed
static char **my_completion(const char *text, int start, int end __attribute__((unused))) {
	rl_attempted_completion_over = 1;
	if (start == 0) {
		return rl_completion_matches(text, command_generator);
	}
	return NULL;
}

typedef struct DBG {
	char *process_name;           // the command
	char **args;                  // arguments of the tracee
	time_t mtime;                 // last modified to time of the executable
	pid_t pid;                    // obvious
	dbg_state state;              // tracking the state of debugger
	map *breakpoints;             // hashtable of breakpoints
	bp_list *pending_breakpoints; // list of pending breakpoints
	uintptr_t load_address;       // to be calc the offset in dyn executable
	int pending_signal;           // for that irritating SIGSEGV only currently
	Elf *elf_data;                // elf stuffs
	Dwfl *dwarf_data;             // dwarf parsing
	bool has_dwarf_symbols;       // check for printing the src code
} debugger;

// returns true if debug symbols are present in the code
static bool check_for_debug_symbols(debugger *dbg) {
	// after hours of finding how to read the name of section headers
	// found the solution in this book
	// https://sourceforge.net/projects/elftoolchain/files/Documentation/libelf-by-example/20120308/
	Elf64_Ehdr *ehdr = elf64_getehdr(dbg->elf_data);
	for (size_t i = 1;; i++) {
		Elf_Scn *scnhdr = elf_getscn(dbg->elf_data, i);
		if (scnhdr == NULL) break;
		Elf64_Shdr *shdr = elf64_getshdr(scnhdr);
		char *name = elf_strptr(dbg->elf_data, ehdr->e_shstrndx, shdr->sh_name);
		if (strncmp(name, ".debug_info", 11) == 0) {
			return true;
		}
	}
	return false;
}

debugger *dbg_init(const char *pname) {
	debugger *new = malloc(sizeof(debugger));
	if (!new) {
		perror("dbg_init");
		exit(EXIT_FAILURE);
	}

	new->pid = 0;
	new->process_name = strdup(pname);
	new->args = calloc(MAX_ARGS, sizeof(char *));
	new->args[0] = new->process_name;

	struct stat file_stats;
	stat(pname, &file_stats);
	new->mtime = file_stats.st_mtim.tv_sec;

	new->state = NOT_ACTIVE;
	new->breakpoints = map_init(MAP_SIZE, bp_free);
	new->pending_breakpoints = list_queue_init();
	new->pending_signal = 0;
	new->load_address = 0; // initialized for static files initially

	int fd = open(new->process_name, O_RDONLY);
	if (fd == -1) {
		CRITICAL_PERROR("open");
	}

	elf_version(EV_CURRENT); // necessary because libelf just won't work
	new->elf_data = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (new->elf_data == NULL) {
		CRITICAL("elf_begin: %s", elf_errmsg(elf_errno()));
	}
	close(fd);

	new->dwarf_data = dwfl_begin(&callbacks);
	if (new->dwarf_data == NULL) {
		CRITICAL("dwfl_begin: %s", dwfl_errmsg(dwfl_errno()));
	}

	new->has_dwarf_symbols = check_for_debug_symbols(new);

	return new;
}

// simple getter
pid_t dbg_get_pid(debugger *dbg) {
	return dbg->pid;
}

uintptr_t dbg_get_load_address(debugger *dbg) {
	return dbg->load_address;
}

bool dbg_is_active(debugger *dbg) {
	return (dbg->state == ACTIVE);
}

bool dbg_has_dwarf_symbols(debugger *dbg) {
	return dbg->has_dwarf_symbols;
}

// setting up dwfl to read modules later on in the code
static void setup_dwfl(debugger *dbg) {
	// actually idk what the heck it is doing
	// adding this lead to finally being able to
	// get Dwfl_Module not null
	// hope libdwfl had documentation
	// figuring this out took hours
	if (dwfl_linux_proc_report(dbg->dwarf_data, dbg->pid) != 0) {
		fprintf(stderr, "dwfl_linux_proc_report: %s\n", dwfl_errmsg(-1));
		dwfl_end(dbg->dwarf_data);
		ptrace(PTRACE_CONT, dbg->pid, NULL, NULL);
	}
	dwfl_report_end(dbg->dwarf_data, NULL, NULL);
}

// returns the line number and the file
static int get_line_from_pc(debugger *dbg, Dwarf_Addr pc, const char **file) {
	Dwfl_Module *mod = dwfl_addrmodule(dbg->dwarf_data, pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwfl_Line *src = dwfl_module_getsrc(mod, pc);
	DWFL_SANITY_CHECK(src, "dwfl_module_getsrc");

	int line_number = 0, column = 0;
	*file = dwfl_lineinfo(src, &pc, &line_number, &column, NULL, NULL);
	DWFL_SANITY_CHECK(*file, "dwfl_lineinfo");

	DBG_LOG("file: %s", *file);
	DBG_LOG("pc: %lx, line no.: %d, column: %d", pc, line_number, column);

	struct stat statbuf;
	stat(*file, &statbuf);
	DBG_LOG("times: %lu, %lu ", dbg->mtime, statbuf.st_mtim.tv_sec);
	if (dbg->mtime < statbuf.st_mtim.tv_sec) {
		printf(RED "\n[" BYEL " Warning: " RESET "source code is modified" RED " \t]\n");
		printf("[" RESET " pls recompile the code and then debug " RED "]\n" RESET);
	}
	return line_number;
}

static void get_func_die_from_pc(debugger *dbg, uintptr_t pc, Dwarf_Die *func_die) {
	Dwfl_Module *mod = dwfl_addrmodule(dbg->dwarf_data, pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwarf_Addr bias;
	Dwarf_Die *die = dwfl_module_addrdie(mod, pc, &bias);
	DWFL_SANITY_CHECK(die, "dwfl_module_addrdie");

	int tag = dwarf_tag(die);

	// finally figured out how to get the function die
	Dwarf_Die *scopes;
	int count = dwarf_getscopes(die, offset_load_address(dbg, pc), &scopes);
	for (int i = 0; i < count; i++) {
		tag = dwarf_tag(&scopes[i]);
		if (tag == DW_TAG_subprogram) {
			memcpy(func_die, &scopes[i], sizeof(Dwarf_Die));
		}
	}

	free(scopes);
}

void print_source_at_current_pc(debugger *dbg) {
	// it it does not have dwarf symbols than just return
	// don't try to print the source code
	if (!dbg->has_dwarf_symbols) return;

	const char *file;
	int line_no = get_line_from_pc(dbg, get_pc(dbg->pid), &file);
	print_source(file, (uint32_t)line_no, 3);
}

// to get the base address of dyn executable
static void initialize_load_address(debugger *dbg) {
	Elf64_Ehdr *header = elf64_getehdr(dbg->elf_data);
	if (header->e_type == ET_DYN) {
		char path[100];
		snprintf(path, sizeof(path), "/proc/%d/maps", dbg->pid);
		FILE *file = fopen(path, "r");
		if (!file) {
			CRITICAL_PERROR("fopen");
		}
		char *line = NULL;
		size_t len = 0;
		uintptr_t addr = 0;

		while (getline(&line, &len, file) != -1) {
			uintptr_t start_addr, end_addr;
			int parsed = sscanf(line, "%lx-%lx %*s %*x %*s %*d %*s", &start_addr, &end_addr);
			// if it's parsed correctly
			if (parsed == 2) {
				addr = start_addr;
				break;
			}
		}
		if (addr != 0) {
			DBG_LOG("load_address initialized with 0x%lx", addr);
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
	DBG_LOG("added %d arguments", i);
}

void dbg_start(debugger *dbg) {
	// setting up my own commands for completion
	rl_attempted_completion_function = my_completion;

	if (!dbg->has_dwarf_symbols) {
		printf(RED "\n[" BYEL " Warning: " RESET "this executable doesn't contain debug symbols" RED
		           " ]\n");
		printf("[\t " RESET "  pls recompile the code with -g flag " RED " \t]\n" RESET);
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

static void cleanup_at_tracee_death(debugger *dbg) {
	// disabling all breakpoints so that next time when it runs
	// i can enable it all
	dbg->state = NOT_ACTIVE;
	disable_all_breakpoints(dbg);
}

static void kill_tracee(debugger *dbg) {
	ptrace(PTRACE_KILL, dbg->pid, NULL, NULL);
	waitpid(dbg->pid, NULL, 0);
	cleanup_at_tracee_death(dbg);
	DBG_LOG("this tracee is killed. new one is going to start");
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
		uintptr_t pc = get_pc(dbg->pid);
		breakpoint *bp = map_lookup(dbg->breakpoints, pc - 1);
		set_pc(dbg->pid, pc - 1);

		// not printing the message if the breakpoint is temperory
		if (!bp_is_temp(bp))
			printf("Hit breakpoint at " BRED "0x%lx" RESET "\n",
			       offset_load_address(dbg, get_pc(dbg->pid)));
		else
			DBG_LOG("Hit temperory breakpoint at 0x%lx",
			        offset_load_address(dbg, get_pc(dbg->pid)));

		print_source_at_current_pc(dbg);

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

static siginfo_t get_signal_info(debugger *dbg) {
	siginfo_t info;
	if (ptrace(PTRACE_GETSIGINFO, dbg->pid, NULL, &info) == -1) {
		CRITICAL_PERROR("get_signal_info");
	}
	return info;
}

// the main signal handler of this debugger
static void wait_for_signal(debugger *dbg) {
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

static void resolve_pending_breakpoints(debugger *dbg) {
	DBG_LOG("resolving all the breakpoints in the list");
	size_t i = 0;
	uintptr_t addr;
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		addr += dbg->load_address;
		DBG_LOG("resolving 0x%lx", addr);
		breakpoint *bp = map_lookup(dbg->breakpoints, addr);
		if (bp) {
			bp_set_pid(map_lookup(dbg->breakpoints, addr), dbg->pid);
			bp_enable(map_lookup(dbg->breakpoints, addr));
		} else {
			bp = bp_init(dbg->pid, addr, false);
			if (map_insert(dbg->breakpoints, addr, bp)) {
				bp_enable(bp);
			} else {
				DBG_LOG("Breakpoint already set at 0x%lx", addr);
				DBG_LOG("freeing this breakpoint");
				bp_free(bp);
			}
		}
		i++;
	}
}

static void spawn_tracee(debugger *dbg) {
	pid_t pid = fork();
	if (pid == 0) {
		ptrace(PTRACE_TRACEME, pid, NULL, NULL);
		personality(ADDR_NO_RANDOMIZE);
		execv(dbg->process_name, dbg->args);
	} else {
		DBG_LOG("Running %s ....", dbg->process_name);
		dbg->pid = pid;
		wait_for_signal(dbg);
		initialize_load_address(dbg);
		dbg->state = ACTIVE;
		resolve_pending_breakpoints(dbg);
		setup_dwfl(dbg);
		continue_execution(dbg);
	}
}

void run(debugger *dbg) {
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

void restart(debugger *dbg) {
	kill_tracee(dbg);
	spawn_tracee(dbg);
}

// it takes the address of the instruction as shown in the
// disassembly of the executable
void set_breakpoint_at_addr(debugger *dbg, uintptr_t addr) {
	// not calling this function during resolution of breakpoints

	add_breakpoint_as_pending(dbg->pending_breakpoints, addr);
	printf("Set breakpoint at addr " YEL "0x%lx" RESET " ...\n", addr);

	// program is not running
	if (dbg->state == NOT_ACTIVE) {
		return;
	}

	addr += dbg->load_address; // adding the offset fo pie

	breakpoint *bp = bp_init(dbg->pid, addr, false);

	// i think i should enable the breakpoint after inserting
	// instead of enabling before inserting
	// so that it's clear that we aren't doing it twice
	if (map_insert(dbg->breakpoints, addr, bp)) {
		bp_enable(bp);
	} else {
		DBG_LOG("Breakpoint already set at 0x%lx", addr);
		DBG_LOG("freeing this breakpoint");
		bp_free(bp);
	}
}

void unset_breakpoint_at_addr(debugger *dbg, uintptr_t addr) {
	uintptr_t actual_addr = addr + dbg->load_address;
	breakpoint *found_bp = map_lookup(dbg->breakpoints, actual_addr);
	if (found_bp == NULL) {
		fprintf(stderr, "No breakpoint found at addr: 0x%lx\n", addr);
		return;
	}
	DBG_LOG("Disabling breakpint at addr 0x%lx", addr);
	bp_disable(found_bp);
	delete_breakpoint_from_pending(dbg->pending_breakpoints, addr);
}

// it takes the actual virtual address of the running program
// it will return false if a breakpoint is already set at the
// place i am wanting to set the temp bp
static bool set_temp_breakpoint(debugger *dbg, uintptr_t running_addr) {
	breakpoint *bp = bp_init(dbg->pid, running_addr, true);
	if (map_insert(dbg->breakpoints, running_addr, bp)) {
		bp_enable(bp);
	} else {
		DBG_LOG("Breakpoint already set at 0x%lx", running_addr);
		DBG_LOG("freeing this breakpoint");
		bp_free(bp);
		return false;
	}
	return true;
}

static void unset_temp_breakpoint(debugger *dbg, uintptr_t running_addr) {
	breakpoint *found_bp = map_lookup(dbg->breakpoints, running_addr);
	if (found_bp == NULL) {
		fprintf(stderr, "wth are you disabling at: 0x%lx", running_addr);
		return;
	}
	DBG_LOG("Disabling breakpint at addr 0x%lx", running_addr);
	bp_disable(found_bp);
	map_delete(dbg->breakpoints, running_addr);
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

static void single_step_instruction(debugger *dbg) {
	DBG_LOG("single stepping instruction");
	// DBG_LOG("pc before single stepping: 0x%lx", get_pc(dbg->pid));
	ptrace(PTRACE_SINGLESTEP, dbg->pid, NULL, NULL);
	// DBG_LOG("pc after single stepping: 0x%lx", get_pc(dbg->pid));
	wait_for_signal(dbg);
}

static void execute_step_over_bp(debugger *dbg, breakpoint *bp) {
	// if not null means we have currently hitted the breakpoint
	if (bp == NULL) return;
	// else
	// 	printf("found bp\n");

	if (bp_is_enabled(bp)) {
		bp_disable(bp);
		// single step forward to jump through the breakpoint instruction
		single_step_instruction(dbg);
		bp_enable(bp);
	}
}

static void step_over_breakpoint(debugger *dbg) {
	breakpoint *bp = map_lookup(dbg->breakpoints, get_pc(dbg->pid));
	execute_step_over_bp(dbg, bp);
}

void single_step_instruction_with_breakpoint_check(debugger *dbg) {
	breakpoint *bp = map_lookup(dbg->breakpoints, get_pc(dbg->pid));
	if (bp != NULL) {
		DBG_LOG("bp found here now stepping over it");
		execute_step_over_bp(dbg, bp);
	} else {
		single_step_instruction(dbg);
	}
}

// the finish instruction
void step_out(debugger *dbg) {
	// getting the base pointer of the current stackframe from the data stored in rbp
	uint64_t frame_pointer = get_register_value(rbp, dbg->pid);

	// fetching return address from the value just above it
	uint64_t return_address = read_memory(dbg->pid, frame_pointer + 8);
	DBG_LOG("got return_address: 0x%lx", return_address);

	bool should_remove_breakpoint = false;
	// adding a temp breakpoint
	if (set_temp_breakpoint(dbg, return_address)) {
		should_remove_breakpoint = true;
	}

	continue_execution(dbg);

	// if added that temp bp then remove it
	if (should_remove_breakpoint) {
		unset_temp_breakpoint(dbg, return_address);
	}
}

void step_in(debugger *dbg) {
	const char *file;
	int next_line;
	int line = get_line_from_pc(dbg, get_pc(dbg->pid), &file);
	if (line == 0) {
		CRITICAL("Something is wrong");
	}

	// loop until we are on the same line
	while ((next_line = get_line_from_pc(dbg, get_pc(dbg->pid), &file)) == line) {
		single_step_instruction_with_breakpoint_check(dbg);
	}

	if (dbg->has_dwarf_symbols) print_source(file, (unsigned)next_line, 3);
}

// the next instruction
void step_over(debugger *dbg) {
	uintptr_t pc = get_pc(dbg->pid);
	Dwarf_Die func;
	get_func_die_from_pc(dbg, pc, &func);

	Dwarf_Addr func_entry, func_end;
	const char *func_name = dwarf_diename(&func);
	DBG_LOG("diename: %s", func_name);
	dwarf_lowpc(&func, &func_entry);
	dwarf_highpc(&func, &func_end);
	DBG_LOG("func_entry: %lx, func_end: %lx", func_entry, func_end);
	func_entry += dbg->load_address;
	func_end += dbg->load_address;

	size_t to_delete_cap = 100;
	uintptr_t *to_delete = malloc(to_delete_cap * sizeof(uintptr_t));
	size_t to_delete_size = 0;

	Dwfl_Module *mod = dwfl_addrmodule(dbg->dwarf_data, pc);
	DWFL_SANITY_CHECK(mod, "dwfl_addrmodule");

	Dwfl_Line *src = dwfl_module_getsrc(mod, pc);
	DWFL_SANITY_CHECK(src, "dwfl_module_getsrc");

	// setting breakpoints on all the lines from current to the end of the function
	int current_line = 0, line = 0;
	dwfl_lineinfo(src, NULL, &current_line, NULL, NULL, NULL);
	assert(current_line != 0);

	while (pc < func_end) {
		pc++; // TODO: now this is a problem (checking every byte is just so bad)
		      //       i have to figure out the correct way
		src = dwfl_module_getsrc(mod, pc);
		dwfl_lineinfo(src, NULL, &line, NULL, NULL, NULL);
		// printf("line %d, current_line %d, pc: %lx\n", line, current_line, pc);
		if (line != current_line) {
			if (set_temp_breakpoint(dbg, pc)) {
				to_delete[to_delete_size++] = pc;
				if (to_delete_size >= to_delete_cap - 1) {
					to_delete_cap *= 2;
					to_delete = realloc(to_delete, to_delete_cap * sizeof(uintptr_t));
				}
			}
			// print_source(file, (uint32_t)current_line, 1);
			current_line = line;
		}
	}

	// ignoring to set breakpoint on return address of main so that it doesn't
	// set bp on libc, and try to print the source
	if (strcmp(func_name, "main") != 0) {
		// setting breakpoint at return address
		uint64_t frame_pointer = get_register_value(rbp, dbg->pid);
		uint64_t return_address = read_memory(dbg->pid, frame_pointer + 8);
		if (set_temp_breakpoint(dbg, return_address)) {
			to_delete[to_delete_size++] = return_address;
		}
	}
	// DBG_LOG("to_delete_size: %zu", to_delete_size);

	continue_execution(dbg);

	DBG_LOG("cleaning up all the temp breakpoints");
	for (size_t i = 0; i < to_delete_size; i++) {
		unset_temp_breakpoint(dbg, to_delete[i]);
	}
	free(to_delete);
}

void continue_execution(debugger *dbg) {
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

void disable_all_breakpoints(debugger *dbg) {
	size_t i = 0;
	uintptr_t addr;
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		addr += dbg->load_address;
		breakpoint *bp = map_lookup(dbg->breakpoints, addr);
		bp_disable(bp);
		i++;
	}
}

bool dbg_kill_tracee(debugger *dbg) {
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

void remove_all_breakpoints(debugger *dbg) {
	size_t i = 0;
	uintptr_t addr;
	bool process_is_running = (kill(dbg->pid, 0) == 0);
	while ((addr = list_addr_by_index(dbg->pending_breakpoints, i)) != END_OF_LIST) {
		addr += dbg->load_address;
		breakpoint *bp = map_lookup(dbg->breakpoints, addr);
		if (process_is_running) bp_disable(bp);
		map_delete(dbg->breakpoints, addr);
		i++;
	}

	list_clear(dbg->pending_breakpoints);
}

void dbg_free(debugger *dbg) {
	DBG_LOG("freeing the debugger before exiting");
	dwfl_end(dbg->dwarf_data);
	elf_end(dbg->elf_data);
	map_free(dbg->breakpoints);
	list_free(dbg->pending_breakpoints);
	for (int i = 1; dbg->args[i] != NULL; i++)
		free(dbg->args[i]);
	free(dbg->process_name);
	free(dbg->args);
	free(dbg);
}
