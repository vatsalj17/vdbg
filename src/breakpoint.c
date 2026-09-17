#include "breakpoint.h"

#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <unistd.h>

#include "macro.h"
#include "list.h"

// The problem:
// hashmap stores breakpoint as <running instr_addr, breakpoint_t>
// but since now i am changing the breakpoint architecture, breakpoint can be of three types (instr_addr, sym, file:line)
// so multiple breakpoint can be on same `running instr_addr`
// if i use id as the key then the lookup will get slow
// and my whole architecture is dependent on map lookup on the basis of instr_addr as i want to
// always lookup at the pc
//
// currently struct bp only contains the instr instr_addr as in the executable. as inserted in the
// pending breakpoint list. earlier the pending breakpoint list only consisted address so i used
// to add the load_address and create a breakpoint and insert in map. now i have to insert alread created
// bp in the map with the running instr_addr as the key. but bp isn't aware of running instr_addr so how will
// it even enable or disable it
//
// i think i have to add another field in the bp struct that contains the running address or other option is that
// i will modify the instr_addr by adding the load_address in it while adding it to the map but that will become
// fucking complex cause i have to make sure each bp in the map is subtracted by load_address before termination of
// the program.
//
// fuck! i thought it's going to be easy
//
// if i sperate the location data and breakpoint data where breakpoint points to the location struct its set at
// and the location struct hold an array of breakpoints pointing at that location
// so if this is done then in the map it will be stored as <running instr_addr, location struct>
// this means that now map will also be used when program is not running unlike currently. that means
// i have to init the breakpoint at the point user set it and insert it in the map.
// no wait! don't insert it in the map yet. we need the `running instr_addr` to insert it in the map
// as we run the program. we manually set that in the location struct and then insert it in map.
//
// but now we have another problem that how will we able to lookup with the `instr instr_addr`, that means
// we have to linear search.
// that means having two list, one for pending breakpoint along with the location
// another way is to have list of pending locations and iterate through it while resolving. that's it
// no, no. it's not the correct way. there's one more problem.
// we need another list else how will we be able to breakpoint using it's id
// to solve this we can create another map for <id, breakpoint> but this still doesn't solve the problem
// of iterating through the all breakpoints while listing them
//
// then enable disable thing will be messy. since, a breakpoint to be actually disable at 0xcc level
// all the breakpoints in the list of that location should be disabled.
// well i have two choices either i just disable all the breakpoint at the location even if just a single breakpoint
// is disabled pointing to that location or i wait to for the user to disable all the breakpoint at that location
// then actually disable (that gdb does)
//
// okay okay, i think i will have to make the hashmap generic and then make two hasmaps
// one will be <running_addr, bp_location> and other will be <id, breakpoint>
// and then there will be a list of pending breakpoints
// also since we are storing pre-allocated structs in the list, it would be
// better to switch to intrusive-list than what i am currently using a
// vector of pending addresses.
// when the tracee is run, we create the locations at which bp is required.
// and about that enable/disable thing. i will have an architecture such that
// for any location to be disabled or enabled, all the breakpoints in it's list 
// should be disabled or enabled respectively.
//
// i think it's solved (maybe)

uint64_t global_id_counter = 1;

// every runtime info is stored in bp_location_t
typedef struct BpLocation {
	pid_t pid;

	bool patched; // this keeps track of weather 0xcc is patched or not
	bool is_temp;

	uint8_t saved_data; // to save the data with which 0xcc is replaced
	uintptr_t running_addr;

	list_node bp_list; // head of the list of all the breakpoints at this location
} bp_location_t;

typedef struct BreakPoint {
	uint64_t id; // each breakpoint has an id
	bp_kind type;
	union {
		// either symbol or lineno or direct instr_addr(that means no value in union)
		char *symbol;
		file_line_pair fl_pair;
	};
	bool enable;
	uintptr_t instr_addr;    // address is always saved no matter what is the type
	list_node pending_node;  // node of the pending bp list
	list_node location_node; // node of the bp_location list
	bp_location_t *location;
} breakpoint_t;

// in the case of file line pair pass the address of the pair type casted into void*
// else just typecast into void* and pass
breakpoint_t *bp_init(bp_kind type, void *data, uintptr_t instr_addr) {
	DBG_LOG("Intializing breakpoint at 0x%lx", instr_addr);

	breakpoint_t *new = malloc(sizeof(breakpoint_t));
	if (!new) return NULL;

	new->id = global_id_counter++; // give the bp an id and increment the counter

	switch (type) {
	case BP_ADDR: {
		new->type = BP_ADDR;
		break;
	}
	case BP_SYMBOL: {
		new->symbol = strdup((char *)data);
		new->type = BP_SYMBOL;
		break;
	}
	case BP_LINENO: {
		new->fl_pair = *(file_line_pair *)data;
		new->type = BP_LINENO;
		break;
	}
	default:
		CRITICAL("unknown type");
	}

	new->instr_addr = instr_addr;
	new->location = NULL;
	new->enable = true;
	return new;
}

uint64_t bp_get_id(breakpoint_t *bp) {
	assert(bp);
	return bp->id;
}

bp_kind bp_get_type(breakpoint_t *bp) {
	return bp->type;
}

bool bp_is_enabled(breakpoint_t *bp) {
	return bp->enable;
}

// if all breakpoints in the list of bp->location are enabled then patch
void bp_enable(breakpoint_t *bp) {
	bp->enable = true;
	if (!bp->location) return;
	bp_location_t *loc = bp->location;
	list_node *pos;
	bool to_patch = true;
	list_for_each(pos, &loc->bp_list) {
		breakpoint_t *found_bp = list_entry(pos, breakpoint_t, location_node);
		if (!found_bp->enable) to_patch = false;
	}
	if (to_patch) loc_enable(bp->location);
}

void bp_disable(breakpoint_t *bp) {
	bp->enable = false;
	if (!bp->location) return;
	bp_location_t *loc = bp->location;
	list_node *pos;
	bool to_remove_patch = true;
	list_for_each(pos, &loc->bp_list) {
		breakpoint_t *found_bp = list_entry(pos, breakpoint_t, location_node);
		if (found_bp->enable) to_remove_patch = false;
	}
	if (to_remove_patch) loc_disable(bp->location);
}

uintptr_t bp_get_instr_addr(breakpoint_t *bp) {
	assert(bp);
	return bp->instr_addr;
}

void bp_free(breakpoint_t *bp) {
	assert(bp);
	DBG_LOG("freeing %#lx", bp->instr_addr);
	if (bp->location) {
		list_delete(&bp->location_node);
		if (list_empty(&bp->location->bp_list)) {
			DBG_LOG("loc empty! disabling it");
			// if the list is empty just disable to loc so that
			// it doesn't hit the bp unnecessarily
			loc_disable(bp->location);
		}
	}
	if (bp->type == BP_SYMBOL) free(bp->symbol);
	free(bp);
}

breakpoint_t *get_pending_bp_by_pos(list_node *pos) {
	assert(pos);
	return list_entry(pos, breakpoint_t, pending_node);
}

void add_breakpoint_as_pending(list_node *pending_list_head, breakpoint_t *bp) {
	list_add_back(pending_list_head, &bp->pending_node);
}

void delete_breakpoint_from_pending(breakpoint_t *bp) {
	list_delete(&bp->pending_node);
}

breakpoint_t *find_breakpoint_from_pending(list_node *pending_list_head, bp_kind type, void *data) {
	if (!data) return NULL;
	list_node *pos;

	list_for_each(pos, pending_list_head) {
		breakpoint_t *bp = get_pending_bp_by_pos(pos);
		if (bp->type == type) {
			switch (type) {
			case BP_ADDR: {
				if ((uintptr_t)data == bp->instr_addr) return bp;
				break;
			}
			case BP_SYMBOL: {
				if (strncmp((char *)data, bp->symbol, strlen(bp->symbol)) == 0) return bp;
				break;
			}
			case BP_LINENO: {
				file_line_pair *pair = (file_line_pair *)data;
				if (pair->lineno == bp->fl_pair.lineno &&
				    strncmp(pair->file, bp->fl_pair.file, strlen(bp->fl_pair.file)) == 0) {
					return bp;
				}
				break;
			}
			default:
				return NULL;
			}
		}
	}
	return NULL;
}

bp_location_t *loc_init(pid_t pid, uintptr_t running_addr, breakpoint_t *bp, bool is_temp) {
	assert(pid && running_addr);
	if (is_temp) {
		DBG_LOG("creating new temp loc at running_addr: %#lx", running_addr);
		assert(!bp);
	} else {
		DBG_LOG("creating new loc at running_addr: %#lx", running_addr);
		assert(bp);
	}

	bp_location_t *new = malloc(sizeof(bp_location_t));

	new->pid = pid;
	new->running_addr = running_addr;
	new->is_temp = is_temp;
	new->patched = false;

	if (!is_temp) {
		// initializing list of breakpoints this locations is going to point
		list_init(&new->bp_list);

		list_add_back(&new->bp_list, &bp->location_node);
		bp->location = new;
	}

	return new;
}

void loc_append_breakpoint_to_list(bp_location_t *loc, breakpoint_t *bp) {
	bp->location = loc;

	// checking if the breakpoint already exists in the list or not
	list_node *pos;
	list_for_each(pos, &loc->bp_list) {
		breakpoint_t *existing_bp = list_entry(pos, breakpoint_t, location_node);
		if (existing_bp == bp) {
			DBG_LOG("breakpoint already exist at this location");
			return; // if exists then just return
		}
	}

	DBG_LOG("appending breakpoint %#lx at loc %#lx", bp->instr_addr, loc->running_addr);
	list_add_back(&loc->bp_list, &bp->location_node);

	// managing the enable disable thing while resolving the breakpoints at runtime
	if (bp->enable && !loc->patched)
		loc_enable(loc);
	else if (!bp->enable && loc->patched)
		loc_disable(loc);
}

bool loc_is_patched(bp_location_t *loc) {
	return loc->patched;
}

bool loc_is_temp(bp_location_t *loc) {
	assert(loc);
	return loc->is_temp;
}

uintptr_t loc_get_running_addr(bp_location_t *loc) {
	return loc->running_addr;
}

void loc_set_pid(bp_location_t *loc, pid_t pid) {
	loc->pid = pid;
}

void loc_enable(bp_location_t *loc) {
	if (loc_is_patched(loc)) {
		DBG_LOG("loc_enable called for running_addr: 0x%lx which is already patched",
		        loc->running_addr);
		return;
	}
	DBG_LOG("loc_enable called for running_addr: 0x%lx", loc->running_addr);
	long data = ptrace(PTRACE_PEEKDATA, loc->pid, loc->running_addr, NULL);
	loc->saved_data = (uint8_t)(data & 0xff);
	long int3 = 0xcc;
	long data_with_int3 = (data & ~0xff) | int3;
	errno = 0;
	ptrace(PTRACE_POKEDATA, loc->pid, loc->running_addr, data_with_int3);
	if (errno != 0) {
		DBG_PERROR("ptrace");
	}
	loc->patched = true;
}

void loc_disable(bp_location_t *loc) {
	if (!loc_is_patched(loc)) return;
	DBG_LOG("loc_disable called for running_addr: 0x%lx", loc->running_addr);
	if (kill(loc->pid, 0) != 0) {
		loc->patched = false;
		return;
	}
	long data = ptrace(PTRACE_PEEKDATA, loc->pid, loc->running_addr, NULL);
	long restored_data = (data & ~0xff) | loc->saved_data;
	errno = 0;
	ptrace(PTRACE_POKEDATA, loc->pid, loc->running_addr, restored_data);
	if (errno != 0) {
		DBG_PERROR("ptrace");
	}
	DBG_LOG("loc 0x%lx disabled", loc->running_addr);
	loc->patched = false;
}

void loc_free(bp_location_t *loc) {
	assert(loc);
	// assert(list_empty(&loc->bp_list));
	if (!loc->is_temp) {
		list_node *pos;
		list_for_each(pos, &loc->bp_list) {
			breakpoint_t *bp = list_entry(pos, breakpoint_t, location_node);
			bp->location = NULL;
		}
	}

	loc_disable(loc); // disable the location, in case it's left enabled
	free(loc);
}

void print_bp_hit_message_at_loc(bp_location_t *loc) {
	if (loc->is_temp) {
        DBG_LOG("Hit temperory breakpoint at %#lx", loc->running_addr);
        return;
    }

	list_node *pos;
	breakpoint_t *got_bp = NULL;
	breakpoint_t *got_bp_with_sym = NULL;
	breakpoint_t *got_bp_with_line = NULL;
	list_for_each(pos, &loc->bp_list) {
		breakpoint_t *bp = list_entry(pos, breakpoint_t, location_node);
		assert(bp && got_bp != bp);
		if (!got_bp) got_bp = bp;

		if (bp->type == BP_SYMBOL && !got_bp_with_sym)
			got_bp_with_sym = bp;
		else if (bp->type == BP_LINENO && !got_bp_with_line)
			got_bp_with_line = bp;
	}

	if (!got_bp) {
		CRITICAL("how is it possible that a bp_location doesn't have a bp");
	}

	printf("[" BYEL "%lu" RESET "] Hit breakpoint", got_bp->id);
	if (got_bp_with_sym && got_bp_with_line) {
		printf(" at " YEL "%s(%s:%d)" RESET,
		       got_bp_with_sym->symbol,
		       got_bp_with_line->fl_pair.file,
		       got_bp_with_line->fl_pair.lineno);
	} else if (got_bp_with_sym) {
		printf(" at " YEL "%s" RESET, got_bp_with_sym->symbol);
	} else if (got_bp_with_line) {
		printf(" at " YEL "%s:%d" RESET,
		       got_bp_with_line->fl_pair.file,
		       got_bp_with_line->fl_pair.lineno);
	}
	printf(" (" BRED "%#lx" RESET ")\n", got_bp->instr_addr);
}

void print_breakpoint_list(list_node *pending_list_head) {
    if (list_empty(pending_list_head)) {
        printf("No breakpoints set\n");
        return;
    }

	printf(BWHT "Id   Enabled Address          Type   Desc " RESET "\n");

	list_node *pos;

	list_for_each(pos, pending_list_head) {
		breakpoint_t *bp = get_pending_bp_by_pos(pos);
		printf("%lu    %-7s %#016lx ", bp->id, (bp->enable) ? "True" : "False", bp->instr_addr);
		switch (bp->type) {
		case BP_ADDR: {
			printf("ADDR   -");
			break;
		}
		case BP_SYMBOL: {
			printf("SYMBOL %s", bp->symbol);
			break;
		}
		case BP_LINENO: {
			printf("LINENO %s:%d", bp->fl_pair.file, bp->fl_pair.lineno);
			break;
		}
		default:
			CRITICAL("Something is wrong");
		}
		printf("\n");
	}
}
