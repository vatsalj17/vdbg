#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>

typedef struct BreakPoint breakpoint;

breakpoint* bp_init(pid_t pid, uintptr_t addr);
bool bp_is_enabled(breakpoint* bp);
uintptr_t bp_get_addr(breakpoint* bp);
void bp_enable(breakpoint* bp);
void bp_disable(breakpoint* bp);
void bp_free(breakpoint* bp);

#endif
