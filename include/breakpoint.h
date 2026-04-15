#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>
#include <limits.h>

#define END_OF_LIST ULONG_MAX

typedef struct BreakPoint breakpoint;
typedef struct ListOfBreakpoints bp_list;

breakpoint* bp_init(pid_t pid, uintptr_t addr);
void bp_set_pid(breakpoint *bp, pid_t pid);
bool bp_is_enabled(breakpoint* bp);
uintptr_t bp_get_addr(breakpoint* bp);
void bp_enable(breakpoint* bp);
void bp_disable(breakpoint* bp);
void bp_free(breakpoint* bp);
bp_list *list_queue_init(void);
void add_breakpoint_as_pending(bp_list *list, uintptr_t addr);
void list_free(bp_list *list);
uintptr_t list_addr_by_index(bp_list *list, size_t index);
void list_clear(bp_list *list);

#endif
