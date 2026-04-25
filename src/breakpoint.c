#include "breakpoint.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <unistd.h>

#define INITIAL_CAPACITY 1024

// TODO: changing the breakpoint system when i will add dwarf support
// giving each breakpoint an id

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

void delete_breakpoint_from_pending(bp_list *list, uintptr_t addr) {
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

uintptr_t list_addr_by_index(bp_list *list, size_t index) {
	if (index >= list->no_of_bp) return END_OF_LIST;
	return list->bps[index];
}

void list_clear(bp_list *list) {
	list->no_of_bp = 0;
}

breakpoint *bp_init(pid_t pid, uintptr_t addr) {
	// #ifdef DEBUG
	// 	printf("DEBUG: Intializing breakpoint at 0x%lx\n", addr);
	// #endif
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
#ifdef DEBUG
	if (errno != 0) {
		perror("ERROR: bp_enable: ptrace");
	}
#endif
	bp->enabled = true;
}

void bp_disable(breakpoint *bp) {
	if (!bp_is_enabled(bp)) return;
#ifdef DEBUG
	printf("DEBUG: bp_disable called for addr: 0x%lx\n", bp->addr);
#endif
	if (kill(bp->pid, 0) != 0) {
		bp->enabled = false;
		return;
	}
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
