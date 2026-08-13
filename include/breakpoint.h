#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>
#include <limits.h>

#define END_OF_LIST ULONG_MAX

// the breakpoint
typedef struct BreakPoint breakpoint_t;

// tracks list of pending breakpoints
struct bp_list {
	uintptr_t *bps;
	size_t no_of_bp;
	size_t capacity;
};


breakpoint_t* bp_init(pid_t pid, uintptr_t addr, bool is_temp);
void bp_set_pid(breakpoint_t *bp, pid_t pid);
bool bp_is_enabled(breakpoint_t* bp);
uintptr_t bp_get_addr(breakpoint_t* bp);
bool bp_is_temp(breakpoint_t *bp);
void bp_enable(breakpoint_t* bp);
void bp_disable(breakpoint_t* bp);
void bp_free(breakpoint_t* bp);
struct bp_list *list_queue_init(void);
void add_breakpoint_as_pending(struct bp_list *list, uintptr_t addr);
void delete_breakpoint_from_pending(struct bp_list *list, uintptr_t addr);
void list_free(struct bp_list *list);
uintptr_t list_addr_by_index(struct bp_list *list, size_t index);
void list_clear(struct bp_list *list);

#endif
