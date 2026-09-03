# vdbg
A custom, lightweight Linux debugger built from scratch in C using `ptrace`.

I built this project to learn how debuggers actually work under the hood in Linux. It's a from-scratch implementation using `ptrace` and `libelf`/`libdw`, mostly for my own learning and exploration.

## Features
* **Execution:** You can spawn, attach, restart, and kill target programs. It supports single stepping (`stepi`, `step`, `next`) and skipping functions (`finish`).
* **Breakpoints:** Injects `0xcc` (int 3) manually and restores original bytes. You can set breakpoints by hex address, function name, or even source line number (e.g., `break 42` or `break main.c:42`). It handles pending breakpoints too!
* **Registers & Memory:** Read/write CPU registers and raw memory addresses.
* **PIE / ASLR Support:** Automatically calculates the base load address by reading `/proc/<pid>/maps` and handles offsets.
* **DWARF & ELF:** Uses `libelf` and `libdw` for offline and live symbol parsing. Recently added support for offline dwarf parsing so we can resolve line numbers even before the program starts running.
* **CLI:** Tab completion (via `readline`), empty-input repetition, and short commands (like typing `c` for continue).

## Under the Hood
* `debugger.c`: This is the main engine. It's basically a state machine that catches signals (`SIGTRAP`, `SIGSEGV`) and uses temporary breakpoints to figure out stepping.
* `symbols.c`: Handles all the ELF and DWARF parsing stuff. It separates live process parsing from offline parsing to make address resolution cleaner.
* `hashmap.c`: A custom hash table to keep track of active breakpoints.

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
| `backtrace` | Print the trace of function calls |
| `break <addr/func/line>` | Set a breakpoint at a hex address, function name, or line number (e.g., `42` or `file.c:42`) |
| `delete <addr>` | Remove a breakpoint or all breakpoint if not specified |
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
| `functions <name>` | List all the matching functions |
| `register dump` | Dump all x86_64 register values |
| `register read <reg>` | Read a register value |
| `register write <reg> <val>` | Write a register value |
| `memory read <addr>` | Read 8 bytes at address |
| `memory write <addr> <val>` | Write a value to address |
| `help` | Show command menu |
| `exit` | Exit the debugger |


## Working On
* Variable inspection
* Dynamic loading of shared libraries
* Disassembly support
* Some binary static analysis features
   * [x] elf header
   * [x] sections
   * [x] symbols
   * [x] functions
   * [ ] program headers
   * [ ] strings
   * [ ] relocations
   * [ ] plt/got
