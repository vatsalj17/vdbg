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

#include "commands.h"
#include "registers.h"
#include "map.h"

typedef struct DBG {
	char *process_name;
	pid_t pid;
	int file_descriptor;
	map *breakpoints;
	long load_address;
	Elf *elf_data;
	Dwfl *dwarf_data;
} debugger;

debugger *dbg_init(char *pname, pid_t pid) {
	debugger *new = malloc(sizeof(debugger));
	if (!new) {
		perror("dbg_init");
		exit(EXIT_FAILURE);
	}
	new->pid = pid;
	new->process_name = strdup(pname);
	new->breakpoints = map_init(BREAKPOINTS_SIZE, bp_free);
	new->file_descriptor = open(new->process_name, O_RDONLY);
	new->elf_data = elf_begin(new->file_descriptor, ELF_C_READ_MMAP, NULL);
    if (new->elf_data == NULL) {
        perror("elf_begin");
        abort();
    }
	new->load_address = 0;
	return new;
}

// to get the base address of the executable
void initialize_load_address(debugger *dbg) {
	Elf64_Ehdr *header = elf64_getehdr(dbg->elf_data);
	if (header->e_type == ET_DYN) {
		char path[100];
		snprintf(path, sizeof(path), "/proc/%d/maps", dbg->pid);
		FILE *file = fopen(path, "r");
		char *line = NULL;
		size_t len = 0;
		long addr = 0;
		while (getline(&line, &len, file) != 0) {
			long start_addr, end_addr;
			char perm[5];
			char executable_path[256] = {0};
			int parsed = sscanf(line, "%lx-%lx %4s %*x %*s %*d %255s",
			                    &start_addr, &end_addr, perm, executable_path);
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
		fclose(file);
	}
}

long offset_load_address(debugger *dbg, long addr) {
	return addr - dbg->load_address;
}

void dbg_run(debugger *dbg) {
	wait_for_signal(dbg);
	initialize_load_address(dbg);

	char *input;
	while ((input = readline("vdbg> ")) != NULL) {
		add_history(input);
		handle_command(dbg, input);
		free(input);
	}
}

pid_t dbg_get_pid(debugger *dbg) {
	return dbg->pid;
}

void set_breakpoint_at_addr(debugger *dbg, long addr) {
	printf("Setting breakpoint at addr 0x%lx ...\n", addr);
	breakpoint *bp = bp_init(dbg->pid, addr);
	bp_enable(bp);
	map_insert(dbg->breakpoints, addr, bp);
}

long read_memory(debugger *dbg, long addr) {
	return ptrace(PTRACE_PEEKDATA, dbg->pid, addr, NULL);
}

void write_memory(debugger *dbg, long addr, long value) {
	ptrace(PTRACE_POKEDATA, dbg->pid, addr, value);
}

long get_pc(debugger *dbg) {
	return get_register_value(rip, dbg->pid);
}

void set_pc(debugger *dbg, long value) {
	set_register_value(rip, dbg->pid, value);
}

void handle_sigtrap(debugger *dbg, siginfo_t siginfo) {
	// TODO
}

siginfo_t get_signal_info(debugger *dbg) {
	siginfo_t info;
	if (ptrace(PTRACE_GETSIGINFO, dbg->pid, NULL, &info) == -1) {
        perror("get_signal_info");
        abort();
    }
	return info;
}

void wait_for_signal(debugger *dbg) {
	int wait_status, option = 0;
	if (waitpid(dbg->pid, &wait_status, option) == -1) {
        perror("waitpid");
        return;
    }

	siginfo_t siginfo = get_signal_info(dbg);
	switch (siginfo.si_signo) {
	case SIGTRAP:
		handle_sigtrap(dbg, siginfo);
		break;
	case SIGSEGV:
		printf("Caught Segfault! Reason: %d\n", siginfo.si_code);
		break;
	default:
		printf("Got signal: %s\n", strsignal(siginfo.si_signo));
	}
}

void step_over_breakpoint(debugger *dbg) {
	// -1 because execution will go past the breakpoint
	long possible_breakpoint_location = get_pc(dbg) - 1;

	breakpoint *bp = map_lookup(dbg->breakpoints, possible_breakpoint_location);
	if (bp == NULL) return;

	if (bp_is_enabled(bp)) {
		long previous_instruction_address = possible_breakpoint_location;
		set_pc(dbg, previous_instruction_address);

		bp_disable(bp);
		ptrace(PTRACE_SINGLESTEP, dbg->pid, NULL, NULL);
		wait_for_signal(dbg);
		bp_enable(bp);
	}
}

void continue_execution(debugger *dbg) {
	step_over_breakpoint(dbg);
	ptrace(PTRACE_CONT, dbg->pid, NULL, NULL);
	wait_for_signal(dbg);
}

void dbg_free(debugger *dbg) {
	elf_end(dbg->elf_data);
	close(dbg->file_descriptor);
	map_free(dbg->breakpoints);
	free(dbg->process_name);
	free(dbg);
}
