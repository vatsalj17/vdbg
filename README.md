# vdbg

A custom, lightweight Linux debugger built from scratch in C using `ptrace`. 

## Features (Current State)
* **Execution Control:** Spawns and attaches to a tracee using `ptrace`.
* **Hardware Breakpoints:** Manual `int 3` (`0xcc`) instruction injection and original byte restoration.
* **Register Manipulation:** Read, write, and dump x86_64 CPU registers.
* **Memory Inspection:** Peek and poke raw memory addresses.
* **Dynamic Executable Support:** Calculates base load addresses by parsing `/proc/<pid>/maps` to support PIE binaries.

## Under the Hood
This project avoids monolithic design in favor of strict, system-level modularity:
* **The Engine (`debugger.c`):** Acts as a state machine, catching signals (like `SIGTRAP` and `SIGSEGV`) and routing execution control.
* **State Management (`hashmap.c`):** Uses a custom, generic hash table to track breakpoint state in memory
* **Clean Abstraction:** Hardware/OS specifics (`user_regs_struct` offsets, `PTRACE_POKEDATA`) are hidden behind isolated APIs (`registers.c`, `breakpoint.c`).

## Build Instructions

Requires `gcc`, `make`, `libreadline`, and `elfutils`.

```bash
git clone https://github.com/vatsalj17/vdbg.git
cd vdbg

# Build the development version (with ASan/UBSan)
make

# Or build the release version (hardened with PIE, relro, now)
make build
```

## Usage

Start the debugger by passing an executable:

```bash
./vdbg <executable>
```

### Supported Commands
* `run` - Start the tracee.
* `break <address>` - Set a breakpoint at a hex address.
* `continue` - Resume execution until the next trap.
* `register read <reg>` / `register write <reg> <val>` - Manipulate CPU registers.
* `register dump` - Dump all x86_64 register states.
* `memory read <addr>` / `memory write <addr> <val>` - Manipulate raw memory.
* `help` - Show the interactive command menu.

## Working On
* PIE / ASLR support
* Source-level debugging (DWARF)
