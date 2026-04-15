#include "registers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>

typedef struct RegDescriptor {
	reg r;
	int dwarf;
	char *name;
} reg_descriptor;

reg_descriptor register_descriptors[REGISTERS_NUM] = {
    {r15, 15, "r15"},
    {r14, 14, "r14"},
    {r13, 13, "r13"},
    {r12, 12, "r12"},
    {rbp, 6, "rbp"},
    {rbx, 3, "rbx"},
    {r11, 11, "r11"},
    {r10, 10, "r10"},
    {r9, 9, "r9"},
    {r8, 8, "r8"},
    {rax, 0, "rax"},
    {rcx, 2, "rcx"},
    {rdx, 1, "rdx"},
    {rsi, 4, "rsi"},
    {rdi, 5, "rdi"},
    {orig_rax, -1, "orig_rax"},
    {rip, -1, "rip"},
    {cs, 51, "cs"},
    {eflags, 49, "eflags"},
    {rsp, 7, "rsp"},
    {ss, 52, "ss"},
    {fs_base, 58, "fs_base"},
    {gs_base, 59, "gs_base"},
    {ds, 53, "ds"},
    {es, 50, "es"},
    {fs, 54, "fs"},
    {gs, 55, "gs"},
};

uintptr_t get_register_value(reg r, pid_t pid) {
	struct user_regs_struct regs;
	ptrace(PTRACE_GETREGS, pid, NULL, &regs);
	size_t offset = r;
	char *base = (char *)&regs;
	uintptr_t value = *(uintptr_t *)(base + offset);
	return value;
}

void set_register_value(reg r, pid_t pid, uintptr_t value) {
	struct user_regs_struct regs;
	ptrace(PTRACE_GETREGS, pid, NULL, &regs);
	size_t offset = r;
	char *base = (char *)&regs;
	char *dest = base + offset;
	memcpy(dest, &value, sizeof(uintptr_t));
	ptrace(PTRACE_SETREGS, pid, NULL, &regs);
}

uintptr_t get_register_value_from_dwarf_register(int regnum, pid_t pid) {
	reg r = (reg)-1;
	for (int i = 0; i < REGISTERS_NUM; i++) {
		if (register_descriptors[i].dwarf == regnum) {
			r = register_descriptors[i].r;
			break;
		}
	}
	if (r == (reg)-1) {
		fprintf(stderr, "get_registers_value_from_dwarf_register: invalid dwarf value\n");
		exit(EXIT_FAILURE);
	}
	return get_register_value(r, pid);
}

char *get_register_name(reg r) {
	for (int i = 0; i < REGISTERS_NUM; i++) {
		if (register_descriptors[i].r == r) {
			return register_descriptors[i].name;
		}
	}
	return "";
}

reg get_register_from_name(const char *str) {
	for (int i = 0; i < REGISTERS_NUM; i++) {
		if (strcmp(register_descriptors[i].name, str) == 0) {
			return register_descriptors[i].r;
		}
	}
	return (reg)-1;
}

uintptr_t get_pc(pid_t pid) {
	return get_register_value(rip, pid);
}

void set_pc(pid_t pid, uintptr_t value) {
	set_register_value(rip, pid, value);
}

void dump_registers(pid_t pid) {
	for (int i = 0; i < REGISTERS_NUM; i++) {
		printf("%-11s 0x%lx\n",
		       register_descriptors[i].name,
		       get_register_value(register_descriptors[i].r, pid));
	}
}

long read_memory(pid_t pid, uintptr_t addr) {
	return ptrace(PTRACE_PEEKDATA, pid, addr, NULL);
}

void write_memory(pid_t pid, uintptr_t addr, uintptr_t value) {
	ptrace(PTRACE_POKEDATA, pid, addr, value);
}
