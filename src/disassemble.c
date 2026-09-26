#include "disassemble.h"

#include <assert.h>
#include <capstone/capstone.h>
#include <capstone/x86.h>
#include <stdio.h>
#include <ctype.h>

#include "debugger.h"
#include "macro.h"
#include "registers.h"

#define REG_COLOR BLU
#define IMM_COLOR CYN
#define BRACKET_COLOR MAG
#define OPERATOR_COLOR GRN

#define cs_error(handle) cs_strerror(cs_errno(handle))

static inline void print_bytes(uint8_t bytes[24], uint16_t size, uint16_t max_size) {
	printf(HWHT);
	uint16_t i;
	for (i = 0; i < size; i++) {
		printf("%02x ", bytes[i]);
	}
	// adding the padding for uniformity
	while (i++ <= max_size)
		printf("   ");
	printf(RESET);
}

static uint8_t print_expression(char *expr) {
	uint8_t iter = 0;
	while (expr[iter] != ']') {
		if ((expr[iter] == '0' && expr[iter + 1] == 'x') || isdigit(expr[iter]))
			printf(IMM_COLOR);
		else if (expr[iter] == '+' || expr[iter] == '-' || expr[iter] == '*')
			printf(OPERATOR_COLOR);
		else
			printf(REG_COLOR);
		while (expr[iter] != ' ' && expr[iter] != ']') {
			putchar(expr[iter++]);
		}
		if (expr[iter] == ' ') putchar(expr[iter++]);
	}
	return iter;
}

static void print_oprand_string(char op_str[160], cs_x86_op operands[8], uint8_t op_count) {
	uint8_t str_iter = 0;
	for (uint8_t i = 0; i < op_count; i++) {
		cs_x86_op op = operands[i];
		switch (op.type) {
		case X86_OP_INVALID: {
			printf(WHT);
			break;
		}
		case X86_OP_REG: {
			printf(REG_COLOR);
			break;
		}
		case X86_OP_IMM: {
			printf(IMM_COLOR);
			break;
		}
		case X86_OP_MEM: {
			printf(WHT);
			// print till '['
			while (op_str[str_iter] != '[' && str_iter < 160) {
				putchar(op_str[str_iter++]);
			}
			assert(op_str[str_iter] == '[');
			printf(BRACKET_COLOR "%c" RESET, op_str[str_iter++]);
			uint8_t len = print_expression(op_str + str_iter);
			str_iter += len;
			assert(op_str[str_iter] == ']');
			printf(BRACKET_COLOR "%c" RESET, op_str[str_iter++]);
			break;
		}
		}
		// print till end or comma
		while (op_str[str_iter] != ',' && str_iter < 160) {
			putchar(op_str[str_iter++]);
		}
		printf(RESET);

		// print the comma
		if (op_str[str_iter] == ',') {
			putchar(op_str[str_iter++]);
		}
	}
}

static void disassemble(char *tracee, uintptr_t addr, size_t size, uintptr_t load_address,
                        uintptr_t pc) {
	csh handle;
	if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK) {
		cs_error(handle);
		return;
	}
	cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

	FILE *fp = fopen(tracee, "r");
	if (!fp) {
		DBG_PERROR("fopen");
		return;
	}
	fseek(fp, (long)addr, SEEK_SET);
	void *buf = malloc(size);
	fread(buf, 1, size, fp);

	cs_insn *instr;
	size_t count = cs_disasm(handle, buf, size, addr, 0, &instr);
	if (count <= 0) {
		cs_error(handle);
	}
	uint16_t max_size = 0;
	for (size_t i = 0; i < count; i++) {
		if (instr[i].size > max_size) max_size = instr[i].size;
	}

	for (size_t i = 0; i < count; i++) {
		cs_insn instruction = instr[i];
		cs_detail *det = instruction.detail;
		assert(det);
		cs_x86 xins = det->x86;

		uintptr_t curr_addr = instruction.address + load_address;

		// if curr_addr is the pc then show the arrow
		if (curr_addr == pc)
			printf(BGRN " > ");
		else
			printf("   " BWHT);

		printf("%lx" RESET ": ", curr_addr);
		print_bytes(instruction.bytes, instruction.size, max_size);
		printf(YEL "%-6s " RESET, instruction.mnemonic);
		print_oprand_string(instruction.op_str, xins.operands, xins.op_count);
		printf(RESET "\n");
	}
	printf("\n");

	cs_free(instr, count);
	free(buf);
	cs_close(&handle);
}

// just print the disassembly of a function
void print_disassembly_of_function(debugger_t *dbg, const char *function_name) {
	uintptr_t load_address = 0;
	uintptr_t pc = 0;
	if (!function_name) {
		if (!dbg_is_active(dbg)) {
			printf("to print disassembly, pls specify the function name\n");
			return;
		}

		load_address = dbg_get_load_address(dbg);
		pc = get_pc(dbg_get_pid(dbg));
		Dwarf_Die func_die = {0};
		get_func_die_from_pc(dbg_get_symbols(dbg), pc, &func_die, load_address);
		const char *func_name = dwarf_diename(&func_die);
		if (!func_name) {
			printf("no function symbol found at current pc\n");
			return;
		}
		function_name = func_name;
	}
	if (!load_address && dbg_is_active(dbg)) load_address = dbg_get_load_address(dbg);

	size_t size;
	Elf64_Sym *functions = get_valid_func_symbols(dbg_get_symbols(dbg), function_name, &size, NULL);
	if (size == 0) {
		printf("No function found with name %s\n", function_name);
	}
	for (size_t i = 0; i < size; i++) {
		printf("\nDisassembly of " BYEL "%s" RESET ":\n", function_name);
		disassemble(dbg_get_tracee_name(dbg),
		            functions[i].st_value,
		            functions[i].st_size,
		            load_address,
		            pc);
	}
	free(functions);
}
