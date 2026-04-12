#include "breakpoint.h"

#include <stdlib.h>
#include <sys/ptrace.h>
#include <unistd.h>

typedef struct BreakPoint {
	pid_t pid;
	uintptr_t addr;
	bool enabled;
	uint8_t saved_data;
} breakpoint;

breakpoint *bp_init(pid_t pid, uintptr_t addr) {
	breakpoint *new = malloc(sizeof(breakpoint));
	new->pid = pid;
	new->addr = addr;
	return new;
}

bool bp_is_enabled(breakpoint *bp) {
	return bp->enabled;
}

uintptr_t bp_get_addr(breakpoint *bp) {
	return bp->addr;
}

void bp_enable(breakpoint *bp) {
	long data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
	bp->saved_data = (uint8_t)(data & 0xff);
	long int3 = 0xcc;
	long data_with_int3 = (data & ~0xff) | int3;
	ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, data_with_int3);
	bp->enabled = true;
}

void bp_disable(breakpoint *bp) {
	long data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
	long restored_data = (data & ~0xff) | bp->saved_data;
	ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, restored_data);
	bp->enabled = false;
}

void bp_free(breakpoint *bp) {
	free(bp);
}
