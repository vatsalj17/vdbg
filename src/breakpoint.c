#include "breakpoint.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <unistd.h>

#include "macro.h"

#define INITIAL_CAPACITY 1024

// TODO: changing the breakpoint system when i will add dwarf support
// giving each breakpoint an id

typedef struct BreakPoint {
	pid_t pid;
	uintptr_t addr;
	bool enabled;
	uint8_t saved_data;
	bool is_temp;
} breakpoint_t;

struct bp_list *list_queue_init(void) {
	struct bp_list *new = malloc(sizeof(struct bp_list));
	new->capacity = INITIAL_CAPACITY;
	new->bps = malloc(new->capacity * sizeof(uintptr_t));
	new->no_of_bp = 0;
	return new;
}

void list_free(struct bp_list *list) {
	assert(list);
	free(list->bps);
	free(list);
}

void add_breakpoint_as_pending(struct bp_list *list, uintptr_t addr) {
	assert(list);
	if (list->no_of_bp == list->capacity) {
		size_t new_cap = (size_t)((double)list->capacity * 1.7);
		uintptr_t *new_list = realloc(list->bps, new_cap * sizeof(uintptr_t));
		if (new_list == NULL) {
			printf("breakpoints list is full\n");
			return;
		}
		list->bps = new_list;
		list->capacity = new_cap;
	}
	list->bps[list->no_of_bp++] = addr;
}

void delete_breakpoint_from_pending(struct bp_list *list, uintptr_t addr) {
	assert(list);
	for (size_t i = 0; i < list->no_of_bp; i++) {
		if (addr == list->bps[i]) {
			void *dest = (uintptr_t *)list->bps + i;
			void *src = (uintptr_t *)list->bps + i + 1;
			size_t bytes_to_move = (list->no_of_bp - i - 1) * sizeof(uintptr_t);
			memmove(dest, src, bytes_to_move);
			list->no_of_bp--;
			return;
		}
	}
}

uintptr_t list_addr_by_index(struct bp_list *list, size_t index) {
	assert(list);
	if (index >= list->no_of_bp) return END_OF_LIST;
	return list->bps[index];
}

void list_clear(struct bp_list *list) {
	assert(list);
	list->no_of_bp = 0;
}

breakpoint_t *bp_init(pid_t pid, uintptr_t addr, bool is_temp) {
	DBG_LOG("Intializing breakpoint at 0x%lx", addr);
	breakpoint_t *new = malloc(sizeof(breakpoint_t));
	new->pid = pid;
	new->addr = addr;
	new->enabled = false;
	new->is_temp = is_temp;
	return new;
}

bool bp_is_temp(breakpoint_t *bp) {
	assert(bp);
	return bp->is_temp;
}

void bp_set_pid(breakpoint_t *bp, pid_t pid) {
	assert(bp);
	bp->pid = pid;
}

bool bp_is_enabled(breakpoint_t *bp) {
	assert(bp);
	return bp->enabled;
}

uintptr_t bp_get_addr(breakpoint_t *bp) {
	assert(bp);
	return bp->addr;
}

void bp_enable(breakpoint_t *bp) {
	if (bp_is_enabled(bp)) {
		DBG_LOG("bp_enable called for addr: 0x%lx which is already enabled", bp->addr);
		return;
	}
	DBG_LOG("bp_enable called for addr: 0x%lx", bp->addr);
	long data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
	bp->saved_data = (uint8_t)(data & 0xff);
	long int3 = 0xcc;
	long data_with_int3 = (data & ~0xff) | int3;
	errno = 0;
	ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, data_with_int3);
	if (errno != 0) {
		DBG_PERROR("ptrace");
	}
	bp->enabled = true;
}

void bp_disable(breakpoint_t *bp) {
	if (!bp_is_enabled(bp)) return;
	DBG_LOG("bp_disable called for addr: 0x%lx", bp->addr);
	if (kill(bp->pid, 0) != 0) {
		bp->enabled = false;
		return;
	}
	long data = ptrace(PTRACE_PEEKDATA, bp->pid, bp->addr, NULL);
	long restored_data = (data & ~0xff) | bp->saved_data;
	errno = 0;
	ptrace(PTRACE_POKEDATA, bp->pid, bp->addr, restored_data);
	if (errno != 0) {
		DBG_PERROR("ptrace");
	}
	DBG_LOG("bp 0x%lx disabled", bp->addr);
	bp->enabled = false;
}

void bp_free(breakpoint_t *bp) {
	assert(bp);
	free(bp);
}
