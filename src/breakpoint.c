#include "breakpoint.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <unistd.h>

#define INITIAL_CAPACITY 1024

// TODO: changing the breakpoint system when i will add dwarf support

typedef struct BreakPoint {
	pid_t pid;
	uintptr_t addr;
	bool enabled;
	uint8_t saved_data;
} breakpoint;

typedef struct ListOfBreakpoints {
	uintptr_t *bps;
	size_t no_of_bp;
	size_t capacity;
} bp_list;

bp_list *list_queue_init(void) {
	bp_list *new = malloc(sizeof(bp_list));
	new->capacity = INITIAL_CAPACITY;
	new->bps = malloc(new->capacity * sizeof(uintptr_t));
	new->no_of_bp = 0;
	return new;
}

void list_free(bp_list *list) {
	free(list->bps);
	free(list);
}

void add_breakpoint_as_pending(bp_list *list, uintptr_t addr) {
	list->bps[list->no_of_bp++] = addr;
}

uintptr_t list_addr_by_index(bp_list *list, size_t index) {
    if (index >= list->no_of_bp) return END_OF_LIST;
    return list->bps[index];
}

void list_clear(bp_list *list) {
    list->no_of_bp = 0;
}

breakpoint *bp_init(pid_t pid, uintptr_t addr) {
	breakpoint *new = malloc(sizeof(breakpoint));
	new->pid = pid;
	new->addr = addr;
    new->enabled = false;
	return new;
}

void bp_set_pid(breakpoint *bp, pid_t pid) {
    bp->pid = pid;
}

bool bp_is_enabled(breakpoint *bp) {
	return bp->enabled;
}

uintptr_t bp_get_addr(breakpoint *bp) {
	return bp->addr;
}

void bp_enable(breakpoint *bp) {
	if (bp_is_enabled(bp)) {
#ifdef DEBUG
	printf("DEBUG: bp_enable called for addr: 0x%lx which is already enabled\n", bp->addr);
#endif
        return;
    }
#ifdef DEBUG
	printf("DEBUG: bp_enable called for addr: 0x%lx\n", bp->addr);
#endif
	long data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
	bp->saved_data = (uint8_t)(data & 0xff);
	long int3 = 0xcc;
	long data_with_int3 = (data & ~0xff) | int3;
    errno = 0;
	ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, data_with_int3);
    if (errno != 0) {
        perror("ERROR: bp_enable: ptrace");
    }
	bp->enabled = true;
}

void bp_disable(breakpoint *bp) {
	if (!bp_is_enabled(bp)) return;
#ifdef DEBUG
	printf("DEBUG: bp_disable called for addr: 0x%lx\n", bp->addr);
#endif
	long data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
	long restored_data = (data & ~0xff) | bp->saved_data;
    errno = 0;
	ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, restored_data);
#ifdef DEBUG
    if (errno != 0) {
        perror("ERROR: bp_disable: ptrace");
    }
	printf("DEBUG: bp 0x%lx disabled\n", bp->addr);
#endif
	bp->enabled = false;
}

void bp_free(breakpoint *bp) {
	free(bp);
}
