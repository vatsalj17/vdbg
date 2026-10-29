#include "completion.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>

#include "breakpoint.h"
#include "commands.h"
#include "list.h"
#include "macro.h"
#include "symbols.h"
#include "util.h"

static debugger_t *store_dbg = NULL;

static int get_word_index_at_start(int start) {
	int words = 0;
	bool in_token = false;
	for (int i = 0; i < start; i++) {
		unsigned char c = (unsigned char)rl_line_buffer[i];
		if (c == ' ' || c == '\t') {
			in_token = false;
		} else if (!in_token) {
			in_token = true;
			words++;
		}
	}
	return words;
}

static bool get_word_at_index(int target_idx, char *buf, size_t buf_len) {
	if (!buf || buf_len == 0) return false;
	buf[0] = '\0';
	int words = 0;
	bool in_token = false;
	int token_start = 0;
	int token_end = 0;

	for (int i = 0; rl_line_buffer[i]; i++) {
		unsigned char c = (unsigned char)rl_line_buffer[i];
		if (c == ' ' || c == '\t') {
			if (in_token) {
				in_token = false;
				if (words - 1 == target_idx) {
					token_end = i;
					break;
				}
			}
		} else {
			if (!in_token) {
				in_token = true;
				words++;
				if (words - 1 == target_idx) {
					token_start = i;
				}
			}
		}
	}
	if (in_token && words - 1 == target_idx) {
		token_end = (int)strlen(rl_line_buffer);
	}

	if (words > target_idx && token_end > token_start) {
		size_t len = (size_t)(token_end - token_start);
		if (len >= buf_len) len = buf_len - 1;
		memcpy(buf, rl_line_buffer + token_start, len);
		buf[len] = '\0';
		return true;
	}
	return false;
}

static const char *const *strlist = NULL;

static char *strlist_generator(const char *text, int state) {
	static size_t idx = 0;
	static size_t len = 0;
	if (!state) {
		idx = 0;
		len = strlen(text);
	}
	while (strlist && strlist[idx] != NULL) {
		const char *item = strlist[idx++];
		if (!strncmp(item, text, len)) {
			return strdup(item);
		}
	}
	return NULL;
}

static char **complete_from_strlist(const char *text, const char *const *list) {
	strlist = list;
	return rl_completion_matches(text, strlist_generator);
}

static char *function_generator(const char *text, int state) {
	static size_t idx = 0;
	static size_t len = 0;
	static struct func_entry *funcs = NULL;
	static size_t funcs_size = 0;

	if (!state) {
		idx = 0;
		len = strlen(text);
		funcs = get_valid_func_symbols(dbg_get_symbols(store_dbg), &funcs_size);
	}

	while (idx < funcs_size) {
		const char *name = funcs[idx++].func_name;
		if (!strncmp(name, text, len)) {
			return strdup(name);
		}
	}
	return NULL;
}

static char *break_target_generator(const char *text, int state) {
	static size_t func_idx = 0;
	static size_t file_idx = 0;
	static size_t len = 0;
	static struct func_entry *funcs = NULL;
	static size_t funcs_size = 0;
	static const char **files = NULL;
	static size_t files_size = 0;
	static bool has_func_match = false;

	if (!state) {
		func_idx = 0;
		file_idx = 0;
		len = strlen(text);
		funcs = get_valid_func_symbols(dbg_get_symbols(store_dbg), &funcs_size);
		files = get_source_files(dbg_get_symbols(store_dbg), &files_size);
		has_func_match = false;
		for (size_t i = 0; i < funcs_size; i++) {
			if (!strncmp(funcs[i].func_name, text, len)) {
				has_func_match = true;
				break;
			}
		}
	}

	while (func_idx < funcs_size) {
		const char *name = funcs[func_idx++].func_name;
		if (!strncmp(name, text, len)) {
			return strdup(name);
		}
	}

	// only recommend files when there is no functions matching the current context
	if (has_func_match) return NULL;

	while (file_idx < files_size) {
		const char *name = files[file_idx++];
		char file_with_colon[256];
		// adding an extra `:` at the end of file names
		snprintf(file_with_colon, sizeof(file_with_colon), "%s:", name);
		if (!strncmp(file_with_colon, text, len)) {
			return strdup(file_with_colon);
		}
	}

	return NULL;
}

static char **bp_candidates = NULL;
static size_t bp_cand_count = 0;
static size_t bp_cand_cap = 0;

static void bp_cand_add(char *s) {
	if (!s) return;
	if (bp_cand_count >= bp_cand_cap) {
		bp_cand_cap = (bp_cand_cap == 0) ? 16 : bp_cand_cap * 2;
		char **new_cands = realloc(bp_candidates, bp_cand_cap * sizeof(char *));
		if (!new_cands) {
			free(s);
			return;
		}
		bp_candidates = new_cands;
	}
	bp_candidates[bp_cand_count++] = s;
}

static char *bp_generator(const char *text, int state) {
	static size_t idx = 0;
	static size_t len = 0;

	if (!state) {
		idx = 0;
		len = strlen(text);
		for (size_t i = 0; i < bp_cand_count; i++) {
			free(bp_candidates[i]);
		}
		bp_cand_count = 0;

		list_node *pending = dbg_get_pending_list(store_dbg);
		list_node *pos;
		list_for_each(pos, pending) {
			breakpoint_t *bp = get_pending_bp_by_pos(pos);
			if (!bp) continue;

			// breakpoint id
			char id_buf[32];
			snprintf(id_buf, sizeof(id_buf), "%lu", bp_get_id(bp));
			bp_cand_add(strdup(id_buf));

			// symbol name
			char *sym = bp_get_symbol(bp);
			if (sym) {
				bp_cand_add(strdup(sym));
			}

			// file and line number
			file_line_pair *fl = bp_get_flpair(bp);
			if (fl && fl->file) {
				char fl_buf[256];
				snprintf(fl_buf, sizeof(fl_buf), "%s:%d", fl->file, fl->lineno);
				bp_cand_add(strdup(fl_buf));
			}

			// address
			if (!sym && !fl) {
				char addr_buf[32];
				snprintf(addr_buf, sizeof(addr_buf), "%#lx", bp_get_instr_addr(bp));
				bp_cand_add(strdup(addr_buf));
			}
		}
	}

	while (idx < bp_cand_count) {
		char *cand = bp_candidates[idx++];
		if (cand && !strncmp(cand, text, len)) {
			return strdup(cand);
		}
	}

	return NULL;
}

// command generator for word 0
static char *command_generator(const char *text, int state) {
	static size_t i = 0;
	static size_t len = 0;
	if (!state) {
		i = 0;
		len = strlen(text);
	}

	const char *cmd;
	while ((cmd = commands[i++].name)) {
		if (!strncmp(cmd, text, len)) {
			return strdup(cmd);
		}
	}

	return NULL;
}

char **complete_break(const char *text, int word_index) {
	if (word_index >= 1) {
		char **matches = rl_completion_matches(text, break_target_generator);
		if (matches && matches[0]) {
			// stopping rl from appending a space on file name completion
			if (matches[0][strlen(matches[0]) - 1] == ':') {
				rl_completion_suppress_append = 1;
			}
		}
		return matches;
	}
	return NULL;
}

char **complete_delete(const char *text, int word_index) {
	if (word_index >= 1) {
		return rl_completion_matches(text, bp_generator);
	}
	return NULL;
}

char **complete_disable(const char *text, int word_index) {
	if (word_index >= 1) {
		return rl_completion_matches(text, bp_generator);
	}
	return NULL;
}

char **complete_disassemble(const char *text, int word_index) {
	if (word_index == 1) {
		return rl_completion_matches(text, function_generator);
	}
	return NULL;
}

char **complete_enable(const char *text, int word_index) {
	if (word_index >= 1) {
		return rl_completion_matches(text, bp_generator);
	}
	return NULL;
}

char **complete_memory(const char *text, int word_index) {
	static const char *const subcmds[] = {"read", "write", NULL};
	if (word_index == 1) {
		return complete_from_strlist(text, subcmds);
	}
	return NULL;
}

char **complete_register(const char *text, int word_index) {
	static const char *const subcmds[] = {"dump", "read", "write", NULL};
	static const char *const reg_names[] = {
	    "r15", "r14",     "r13",     "r12", "rbp", "rbx",      "r11", "r10", "r9",     "r8",
	    "rax", "rcx",     "rdx",     "rsi", "rdi", "orig_rax", "rip", "cs",  "eflags", "rsp",
	    "ss",  "fs_base", "gs_base", "ds",  "es",  "fs",       "gs",  NULL,
	};

	if (word_index == 1) {
		return complete_from_strlist(text, subcmds);
	}
	if (word_index == 2) {
		char subcmd[32];
		if (get_word_at_index(1, subcmd, sizeof(subcmd))) {
			if (is_prefix(subcmd, "read") || is_prefix(subcmd, "write")) {
				return complete_from_strlist(text, reg_names);
			}
		}
	}
	return NULL;
}

char **complete_source(const char *text, int word_index) {
	if (word_index == 1) {
		return rl_completion_matches(text, rl_filename_completion_function);
	}
	return NULL;
}

static char **dbg_completion(const char *text, int start, UNUSED int end) {
	rl_attempted_completion_over = 1;
	rl_completion_suppress_append = 0;

	int word_index = get_word_index_at_start(start);

	// complete the command name
	if (word_index == 0 || start == 0) {
		return rl_completion_matches(text, command_generator);
	}

	// find the command and call its completer
	char cmd_name[128];
	if (!get_word_at_index(0, cmd_name, sizeof(cmd_name))) {
		return NULL;
	}

	// check for an exact match first
	for (int i = 0; commands[i].name != NULL; i++) {
		if (strcmp(commands[i].name, cmd_name) == 0) {
			if (commands[i].completer) {
				return commands[i].completer(text, word_index);
			}
			return NULL;
		}
	}

	// check for a prefix match
	for (int i = 0; commands[i].name != NULL; i++) {
		if (is_prefix(cmd_name, commands[i].name)) {
			if (commands[i].completer) {
				return commands[i].completer(text, word_index);
			}
			return NULL;
		}
	}

	return NULL;
}

void completion_init(debugger_t *dbg) {
	store_dbg = dbg;
	rl_attempted_completion_function = dbg_completion;
}
