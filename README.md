# vdbg
A custom, lightweight Linux debugger built from scratch in C using `ptrace`.

## Features (Current State)
* **Execution Control:** Spawns, attaches to, restarts, and kills a tracee. Supports fine-grained stepping (`stepi`, `step`, `next`) and function skipping (`finish`).
* **Software Breakpoints:** Manual `int 3` (`0xcc`) injection with original byte restoration. Set breakpoints by hex address or function name. Supports pending breakpoints (resolved on spawn), enabling/disabling, and clearing.
* **State Inspection:** Read, write, and dump x86_64 CPU registers. Peek and poke raw memory addresses.
* **Binary Analysis:** Inspect ELF headers, list section headers, and parse symbol tables dynamically.
* **PIE / ASLR Support:** Calculates base load addresses by parsing `/proc/<pid>/maps` and offsets breakpoint addresses dynamically.
* **DWARF Support:** Parses ELF/DWARF via `libelf` and `libdw` for automatic source line mapping and symbol validation.
* **Usability:** `readline`-backed tab completion, tracee arguments passing, two-pass command resolution for unique prefixes (e.g., `c` for `continue`), and empty-input command repetition.

## Under the Hood
* **The Engine (`debugger.c`):** A state machine that routes execution control by catching signals (`SIGTRAP`, `SIGSEGV`, `SIGABRT`) and using temporary breakpoints to step over/out of functions.
* **Binary & Symbol Parsing (`symbols.c`):** Isolated ELF and DWARF parsing handling address resolution to source files, section headers, and symbol tables using `libdw` and `libelf`.
* **State Management (`hashmap.c`):** A custom hash table tracks active breakpoint state, while a separate list handles pending pre-run breakpoints.
* **Clean Abstraction:** OS and hardware specifics are hidden behind focused APIs (`registers.c`, `breakpoint.c`), with centralized macro-based logging for clean debugging output.

## Build
Requires `gcc`, `make`, `libreadline`, and `elfutils`.
```bash
git clone https://github.com/vatsalj17/vdbg.git
cd vdbg
```

Now you have 3 options:- 
```bash
make          # dev build (ASan + UBSan)
make build    # release build (PIE, relro, now) *recommended*
make debug    # debug build (symbols + -DDEBUG)

sudo make install # build and install
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
| `break <addr/func>` | Set a breakpoint at a hex address or function name |
| `delete <addr>` | Remove a breakpoint |
| `enable <addr>` | Re-enable a disabled breakpoint |
| `disable <addr>` | Disable a breakpoint without removing it |
| `continue` | Resume execution |
| `stepi` | Single step through instructions |
| `step` | Single step through source code |
| `next` | Step over current instruction |
| `finish` | Skip the current function |
| `header` | Print the elf header |
| `sections <name>` | List all the matching section headers |
| `symbols <name>` | List all the matching symbols |
| `register dump` | Dump all x86_64 register states |
| `register read <reg>` | Read a register value |
| `register write <reg> <val>` | Write a register value |
| `memory read <addr>` | Read 8 bytes at address |
| `memory write <addr> <val>` | Write a value to address |
| `help` | Show command menu |
| `exit` | Exit the debugger |


## Working On
* Source-level breakpoints (by file:line)
* Stack unwinding
* Variable inspection
* Dynamic loading of shared libraries
* Disassembly support
* Some binary static analysis features
   * [x] elf header
   * [x] sections
   * [x] symbols
   * [ ] functions
   * [ ] program headers
   * [ ] strings
   * [ ] relocations
   * [ ] plt/got
