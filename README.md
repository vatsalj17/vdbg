# vdbg
A Linux debugger built from scratch in C using `ptrace`. Made this to understand how debuggers actually work. It works well enough.

## Features
- **Breakpoints**: by address (`0xDEADC0DE`), function name, or line number (`42` / `file.c:42`). Pending breakpoints work too (set before running). Enable/disable/delete individually or all at once.
- **Stepping**: `stepi` (instruction), `step` (source line), `next` (step over), `finish` (step out)
- **Backtrace**: walks the call stack using `rbp`. breaks if you compiled with `-fomit-frame-pointer`, that's on you
- **Registers & Memory**: read/write CPU registers and raw memory
- **ELF/DWARF inspection**: elf header, section headers, symbol table, functions list
- **PIE/ASLR**: reads `/proc/<pid>/maps` and handles offsets automatically
- **CLI**: tab completion, command history, prefix matching (`c` works for `continue`), empty enter repeats last command
- **Scripting**: `--script <file>` or `-ex <cmd>` to automate stuff on startup

## Under the Hood
- `debugger.c`: main engine. tracee lifecycle, signal handling (`SIGTRAP`, `SIGSEGV`, `SIGABRT`), spawning, breakpoint resolution on startup
- `breakpoint.c`: two-layer design: `breakpoint_t` (the logical bp you set) and `bp_location_t` (the actual patched memory location). multiple breakpoints can share a location. handles the `0xcc` patching/restoring
- `stepping.c`: all the stepping logic. `next` sets temp breakpoints on every line in the current function then continues. `finish` sets a temp bp at the return address
- `symbols.c`: ELF and DWARF parsing. offline (before run) and live (via `libdwfl`) separated so address resolution works both ways
- `hashmap.c` + `list.c`: custom hashmap for breakpoint lookup by address/id, intrusive linked list for pending breakpoints

## Build
Needs `gcc`, `make`, `libreadline`, `elfutils`, `capstone`.
```bash
git clone https://github.com/vatsalj17/vdbg.git
cd vdbg
make         # dev build (ASan + UBSan)
make build   # release build *recommended*
make debug   # debug build (-g3 -DDEBUG)
make install # build + install to /usr/local/bin
```

## Usage
```bash
vdbg [options] <executable>
vdbg ./a.out
vdbg -ex "break main" -ex "run" ./a.out
vdbg --script commands.vdbg ./a.out
```

| Command | Description |
|---|---|
| `run` | Start the tracee |
| `restart` | Kill and relaunch |
| `arguments <arg..>` | Set tracee arguments |
| `break <addr/func/line>` | Set breakpoint |
| `delete [id/addr/func/line]` | Delete breakpoint(s) |
| `enable [id/addr/func/line]` | Enable breakpoint(s) |
| `disable [id/addr/func/line]` | Disable breakpoint(s) |
| `list` | List all breakpoints |
| `continue` | Resume execution |
| `stepi` | Step one instruction |
| `step` | Step one source line |
| `next` | Step over |
| `finish` | Step out of current function |
| `backtrace` | Print call stack |
| `context [lines]` | Show source at current PC |
| `register dump/read/write` | Manage registers |
| `memory read/write <addr>` | Read/write memory |
| `header` | Print ELF header |
| `sections [name]` | List section headers |
| `symbols [name]` | List symbols |
| `functions [name]` | List functions |
| `plt` | Print procedure linkage table |
| `disassemble <func>` | Print the disassembly |
| `vmmap` | Print virtual memory layout |
| `source <file>` | Execute commands from script |
| `help` | Show command list |
| `exit` | Exit |

## Working On
* Variable inspection
* Dynamic loading of shared libraries
* Some binary static analysis features
   * [x] elf header
   * [x] sections
   * [x] symbols
   * [x] functions
   * [ ] program headers
   * [ ] strings
   * [x] plt 
   * [ ] got
