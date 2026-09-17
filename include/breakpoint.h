#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>
#include <limits.h>

#include "list.h"

// the breakpoint
typedef struct BreakPoint breakpoint_t;

// the breakpoint location
typedef struct BpLocation bp_location_t;

typedef enum {
	BP_ADDR,
	BP_SYMBOL,
	BP_LINENO,
} bp_kind;

typedef struct {
	const char *file;
	int lineno;
} file_line_pair;

breakpoint_t *bp_init(bp_kind type, void *data, uintptr_t instr_addr);
uint64_t bp_get_id(breakpoint_t *bp);
uintptr_t bp_get_instr_addr(breakpoint_t *bp);
bool bp_is_enabled(breakpoint_t *bp);
void bp_free(breakpoint_t *bp);

void bp_enable(breakpoint_t *bp);
void bp_disable(breakpoint_t *bp);

breakpoint_t *get_pending_bp_by_pos(list_node *pos);

void add_breakpoint_as_pending(list_node *pending_list_head, breakpoint_t *bp);
void delete_breakpoint_from_pending(breakpoint_t *bp);

breakpoint_t *find_breakpoint_from_pending(list_node *pending_list_head, bp_kind type, void *data);

bp_location_t *loc_init(pid_t pid, uintptr_t running_addr, breakpoint_t *bp, bool is_temp);
bool loc_is_patched(bp_location_t *loc);
bool loc_is_temp(bp_location_t *loc);
uintptr_t loc_get_running_addr(bp_location_t *loc);
void loc_append_breakpoint_to_list(bp_location_t *loc, breakpoint_t *bp);
void loc_set_pid(bp_location_t *loc, pid_t pid);
void loc_enable(bp_location_t *loc);
void loc_disable(bp_location_t *loc);
void loc_free(bp_location_t *loc);

void print_bp_hit_message_at_loc(bp_location_t *loc);

void print_breakpoint_list(list_node *pending_list_head);

#endif
