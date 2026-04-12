#ifndef REGISTER_H
#define REGISTER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/user.h>

#define REGISTERS_NUM 27

typedef enum Registers {
	r15 = offsetof(struct user_regs_struct, r15),
	r14 = offsetof(struct user_regs_struct, r14),
	r13 = offsetof(struct user_regs_struct, r13),
	r12 = offsetof(struct user_regs_struct, r12),
	rbp = offsetof(struct user_regs_struct, rbp),
	rbx = offsetof(struct user_regs_struct, rbx),
	r11 = offsetof(struct user_regs_struct, r11),
	r10 = offsetof(struct user_regs_struct, r10),
	r9 = offsetof(struct user_regs_struct, r9),
	r8 = offsetof(struct user_regs_struct, r8),
	rax = offsetof(struct user_regs_struct, rax),
	rcx = offsetof(struct user_regs_struct, rcx),
	rdx = offsetof(struct user_regs_struct, rdx),
	rsi = offsetof(struct user_regs_struct, rsi),
	rdi = offsetof(struct user_regs_struct, rdi),
	orig_rax = offsetof(struct user_regs_struct, orig_rax),
	rip = offsetof(struct user_regs_struct, rip),
	cs = offsetof(struct user_regs_struct, cs),
	eflags = offsetof(struct user_regs_struct, eflags),
	rsp = offsetof(struct user_regs_struct, rsp),
	ss = offsetof(struct user_regs_struct, ss),
	fs_base = offsetof(struct user_regs_struct, fs_base),
	gs_base = offsetof(struct user_regs_struct, gs_base),
	ds = offsetof(struct user_regs_struct, ds),
	es = offsetof(struct user_regs_struct, es),
	fs = offsetof(struct user_regs_struct, fs),
	gs = offsetof(struct user_regs_struct, gs),
} reg;

typedef struct RegDescriptor reg_descriptor;

uintptr_t get_register_value(reg r, pid_t pid);
void set_register_value(reg r, pid_t pid, uintptr_t value);
uintptr_t get_register_value_from_dwarf_register(int regnum, pid_t pid);
char *get_register_name(reg r);
reg get_register_from_name(const char *str);
void dump_registers(pid_t pid);
uintptr_t get_pc(pid_t pid);
void set_pc(pid_t pid, uintptr_t value);

#endif
