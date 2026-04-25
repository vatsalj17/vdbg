# vdbg
A custom, lightweight Linux debugger built from scratch in C using `ptrace`.

## Features (Current State)
* **Execution Control:** Spawns, attaches to, restarts, and kills a tracee using `ptrace`.
* **Software Breakpoints:** Manual `int 3` (`0xcc`) injection with original byte restoration. Supports set, delete, enable, disable, and clear operations.
* **Pending Breakpoints:** Breakpoints set before `run` are queued and resolved automatically on spawn.
* **Register Manipulation:** Read, write, and dump all x86_64 CPU registers.
* **Memory Inspection:** Peek and poke raw memory addresses.
* **PIE / ASLR Support:** Calculates base load addresses by parsing `/proc/<pid>/maps` and offsets all breakpoint addresses accordingly.
* **Tracee Arguments:** Pass arbitrary arguments to the tracee before launch via the `arguments` command.
* **Tab Completion:** readline-backed command completion.

## Under the Hood
* **The Engine (`debugger.c`):** Acts as a state machine, catching signals (`SIGTRAP`, `SIGSEGV`) and routing execution control.
* **State Management (`hashmap.c`):** Custom hash table to track active breakpoint state; a separate pending list handles pre-run breakpoints.
* **Clean Abstraction:** OS/hardware specifics (`user_regs_struct` offsets, `PTRACE_POKEDATA`) are hidden behind isolated APIs (`registers.c`, `breakpoint.c`).

## Build
Requires `gcc`, `make`, `libreadline`, and `elfutils`.
```bash
git clone https://github.com/vatsalj17/vdbg.git
cd vdbg
make          # dev build (ASan + UBSan)
make build    # release build (PIE, relro, now)
make debug    # debug build (symbols + -DDEBUG)
```

## Usage
```bash
./vdbg <executable>
```


| Command | Description |
|---|---|
| `run` | Start the tracee |
| `restart` | Kill and relaunch the tracee |
| `arguments <arg...>` | Set arguments to pass to the tracee |
| `break <addr>` | Set a breakpoint at a hex address |
| `delete <addr>` | Remove a breakpoint |
| `enable <addr>` | Re-enable a disabled breakpoint |
| `disable <addr>` | Disable a breakpoint without removing it |
| `clear` | Remove all breakpoints |
| `continue` | Resume execution |
| `register dump` | Dump all x86_64 register states |
| `register read <reg>` | Read a register value |
| `register write <reg> <val>` | Write a register value |
| `memory read <addr>` | Read 8 bytes at address |
| `memory write <addr> <val>` | Write a value to address |
| `help` | Show command menu |
| `exit` | Exit the debugger |


## Working On
* Source-level debugging (DWARF)
