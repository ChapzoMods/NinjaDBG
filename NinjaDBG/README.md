<div align="center">

# 🥷 NinjaDBG

<img src="resources/ninja_logo.png" alt="NinjaDBG" width="220" height="220" />

### Stealth-Aware Native Debugger for Linux x86-64<br/>with experimental Windows & macOS support

[![GitHub stars](https://img.shields.io/github/stars/ChapzoMods/NinjaDBG?style=social)](https://github.com/ChapzoMods/NinjaDBG/stargazers)
[![GitHub release](https://img.shields.io/github/v/release/ChapzoMods/NinjaDBG?color=00ffe1)](https://github.com/ChapzoMods/NinjaDBG/releases/latest)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-00ffe1.svg)](https://opensource.org/licenses/Apache-2.0)
[![Platform](https://img.shields.io/badge/Platform-Linux%20x86--64-252a40)](https://github.com/ChapzoMods/NinjaDBG)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599c)](https://isocpp.org/)
[![Lines of Code](https://img.shields.io/badge/LOC-9400%2B-00ffe1)](https://github.com/ChapzoMods/NinjaDBG)

**Version 1.1.0** · Open Source (Apache-2.0) · Created by **Chapzoo**

🌐 **[Live Demo & Docs](https://chapzomods.github.io/NinjaDBG/)** · 📦 **[Download](https://github.com/ChapzoMods/NinjaDBG/releases/latest)** · ⭐ **[Star](https://github.com/ChapzoMods/NinjaDBG/stargazers)**

[Features](#-features) · [Headless CLI](#-headless-cli) · [Pretty Printers](#-pretty-printers) · [Decompilation](#-decompilation) · [Scripting](#-scripting--lua--python) · [License](#-license)

> 🎉 **v1.1.0: Now Open Source under Apache License 2.0!**
>
> Previous versions (1.0.0–1.0.5) were Closed Source. Starting from v1.1.0,
> NinjaDBG is fully open source under Apache-2.0. See [LICENSE](LICENSE).
>
> 🆕 **New in v1.1.0**: 5 bug fixes, pretty printers (C/C++/Rust/Go/Python),
> 9 HIGH-severity code fixes from exhaustive review, Apache-2.0 license switch.

</div>

---

## ⭐ Star this project

If NinjaDBG helps you, please give it a star — it helps others discover it!

<div align="center">

[![Star History](https://api.star-history.com/svg?repos=ChapzoMods/NinjaDBG&type=Date)](https://star-history.com/#ChapzoMods/NinjaDBG&Date)

</div>

---

## 📖 Overview

**NinjaDBG** is an open-source (Apache-2.0) native C++17 debugger for Linux x86-64
with experimental cross-platform support for Windows (PE) and macOS (Mach-O)
binaries via Wine and QEMU adapters. It is engineered around one principle:
**silence**. Where conventional debuggers leave obvious traces — INT3 bytes
in `.text`, `TracerPid` set in `/proc/self/status`, parent process names
like `gdb` or `lldb`, kernel-visible `wchan` of `ptrace_stop` — NinjaDBG
masks, redirects, or eliminates each signal so that the target process
believes it is running alone.

### What's new in v1.1.0

- **🐛 5 bug fixes** from user feedback:
  1. `info b` now works (was "Unknown info subcommand")
  2. `patch undo` now supports 0-indexed IDs and `patch undo` (no arg) undoes the last
  3. `--no-eula-check` flag now actually skips the EULA prompt in CLI mode
  4. `script run python` now properly registers the `ndbg` module in `sys.modules` so `import ndbg` works
  5. `decomp file <bin> <addr>` with angr now calls `func.normalize()` before decompilation — fixes `ValueError: Decompilation must work on normalized function graphs`
- **🎨 Pretty Printers** per language (C, C++, Rust, Go, Python) — see [§ Pretty Printers](#-pretty-printers)
- **🔓 Switched from Closed Source to Open Source (Apache-2.0)**
- **🔍 Code review**: 9 HIGH-severity bugs fixed (memory corruption in `writeMemory`, `kill()` no-op on modern Linux, Cairo destructor UB, disassembler buffer overflow, segment-override OOB read, JmpAlways off-by-one, PE bounds check, attach leak, findFunctionStart underflow)

---

## ✨ Features

### Debugging engine

| Capability | Status | Notes |
|---|---|---|
| Attach to running process by PID | ✅ | `ptrace(PTRACE_ATTACH)` |
| Launch + trace new process | ✅ | `PTRACE_TRACEME` + `execv` |
| Detach / kill | ✅ | `PTRACE_DETACH` / `SIGKILL` (fixed: was using deprecated `PTRACE_KILL`) |
| Single-step (instruction) | ✅ | `PTRACE_SINGLESTEP` |
| Step over (skip CALL) | ✅ | Auto-detects CALL, sets temp bp after |
| Step out (run until return) | ✅ | Sets temp bp on return address from stack |
| Continue / pause | ✅ | `PTRACE_CONT`, `SIGSTOP` |
| Software breakpoints (INT3) | ✅ | 0xCC patching, original byte preserved |
| Hardware breakpoints (DR0-DR3) | ✅ | API surface, INT3 fallback |
| Conditional breakpoints | ✅ | `"rax == 0x10"` syntax, evaluated against live regs |
| Temporary breakpoints | ✅ | Auto-removed after first hit |
| Watchpoints (memory access) | ✅ | DR0-DR3 with RW/LEN fields; W / RW / X |
| Read/write GPRs + RIP + RFLAGS | ✅ | All 16 GPRs + segment regs |
| Read/write target memory | ✅ | `process_vm_readv` / `process_vm_writev` (stealth); `writeMemory` fixed to not corrupt adjacent bytes |
| Enumerate threads | ✅ | Walks `/proc/<pid>/task` |
| Parse `/proc/<pid>/maps` | ✅ | Region permissions, offsets, paths |
| Follow child processes | ✅ | `PTRACE_O_TRACECLONE / TRACEFORK / TRACEEXEC` |
| Auto-detach on parent exit | ✅ | `PTRACE_O_EXITKILL` |
| Backtrace (RBP chain walk) | ✅ | Symbol resolution from `/proc/<pid>/maps` |
| Syscall stepping | ✅ | `PTRACE_SYSCALL`, distinguishes entry vs exit |
| Full x86-64 disassembler (CLI) | ✅ | Standalone `Disassembler` module |
| Interactive TUI memory editor | ✅ | VT100 raw-mode editor; hex+ASCII, seek, search, follow-ptr |
| Lua + Python scripting | ✅ | `script run lua/python <file>`; JSON-RPC subprocess bridge |
| **Pretty printers by language** | ✅ **NEW v1.1.0** | C, C++, Rust, Go, Python string/struct printers |
| Native C decompilation (RetDec) | ✅ | `decomp` command; wraps Avast RetDec via dlopen + subprocess fallback |
| Alternative decompiler (angr) | ✅ | angr backend via `python3 -m angr` subprocess; **fixed: `func.normalize()` added** |

### Stealth subsystem

| Layer | Status | Notes |
|---|---|---|
| Userland anti-detect (8 techniques) | ✅ | See [§ Anti-Detect Techniques](#-anti-detect-techniques) |
| `libninjastealth.so` preload payload | ✅ | Auto-generated, masks `TracerPid:` in target's `/proc/self/status` reads |
| Kernel-level stealth (LKM) | ✅ | Optional `ninja_stealth.ko` module hides NinjaDBG at the kernel level |

### Binary patching

| Capability | Status |
|---|---|
| Load ELF32 / ELF64 binaries | ✅ |
| Load PE32 / PE64 binaries | ✅ (fixed: bounds check added) |
| Load Mach-O 32/64 / FAT binaries | ✅ |
| NOP, JmpAlways (fixed: off-by-one), JmpNever, CallToNop, RetTrue, AsciiReplace, CustomBytes | ✅ |
| Undo patches (fixed: 0-indexed + no-arg = last) | ✅ |

### Cross-platform debugging

| Target platform | Status | Mechanism |
|---|---|---|
| Linux ELF | ✅ Native | `ptrace(2)` directly |
| Windows PE | ✅ Experimental | Wine + `winedbg --gdb` (GDB Remote Serial Protocol) |
| macOS Mach-O | ✅ Experimental | `qemu-x86_64 -g` (GDB RSP) on Linux; native `mach exception ports` on macOS |

---

## 🎨 Pretty Printers (NEW v1.1.0)

Pretty printers interpret raw memory bytes as language-specific data
structures. Set the active language with `pretty set <lang>`.

### Supported languages and types

| Language | Printable types |
|----------|-----------------|
| **C** | `char*` (NUL-terminated string), structs via type descriptor |
| **C++** | `std::string` (libstdc++ SSO-aware), structs |
| **Rust** | `String` (= `Vec<u8>` = `{ptr, cap, len}`), structs |
| **Go** | `string` (`{Data *byte, Len int}`), structs |
| **Python** | `PyUnicodeObject` (CPython 3.12+ compact str) |

### CLI commands

| Command | Description |
|---------|-------------|
| `pretty set <lang>` | Set active language (`c`/`cpp`/`rust`/`go`/`python`/`none`) |
| `pretty cstring <addr>` | Print C-style NUL-terminated string |
| `pretty cpp_string <addr>` | Print `std::string` (auto-detects SSO vs heap) |
| `pretty rust_string <addr>` | Print Rust `String` |
| `pretty go_string <addr>` | Print Go `string` |
| `pretty py_string <addr>` | Print CPython `str` |
| `pretty struct <addr> <desc>` | Parse struct (e.g. `i32,str,ptr,u64`) |
| `pretty auto <addr>` | Auto-print using active language |
| `pretty list` / `pretty api` | Show printers / full API docs |

### Struct descriptor syntax

Comma-separated type codes with natural alignment:

| Code | Size | Description |
|------|------|-------------|
| `i8` `i16` `i32` `i64` | 1/2/4/8 | Signed integers |
| `u8` `u16` `u32` `u64` | 1/2/4/8 | Unsigned integers |
| `f32` `f64` | 4/8 | IEEE 754 floats |
| `ptr` | 8 | Pointer (printed as hex) |
| `str` | 8 | Pointer to C-string (dereferenced) |
| `hex<N>` | N | N raw bytes as hex |

### Example

```bash
(ninjadb) attach 12345
(ninjadb) pretty cstring 0x401234
(char*) 0x401234 = "Hello, world!"  (len=13)

(ninjadb) pretty cpp_string 0x7ffe1000
(std::string) 0x7ffe1000 = "Hello, world!"  (len=13, SSO, data=0x7ffe1010)

(ninjadb) pretty rust_string 0x7ffe2000
(String) 0x7ffe2000 = "Hello, world!"  (len=13, cap=13, ptr=0x55aabbccdd00)

(ninjadb) pretty struct 0x7ffe3000 i32,str,ptr,u64
struct at 0x7ffe3000:
  +0x0   i32  = 42  (0x2a)
  +0x8   ptr  = 0x401234 -> "hello world"
  +0x10  ptr  = 0x7ffe5678
  +0x18  u64  = 139832  (0x22238)
```

---

## 🖥️ Headless CLI

The headless CLI is the recommended interface for production use.

### Launching

```bash
# Interactive REPL
ninjadb --cli

# Batch mode (commands separated by ;)
ninjadb --cli -c "target ./malware; patch nop 0x401000 16; patch save ./patched; quit"

# Skip EULA prompt (now actually works in v1.1.0!)
ninjadb --cli --no-eula-check
```

### Command reference (v1.1.0)

| Command | Description |
|---------|-------------|
| `attach <pid>` | Attach to a running process |
| `launch <bin> [args...]` | Launch a new process under the debugger |
| `detach` / `kill` | Detach or kill the target |
| `continue` / `step` / `next` | Run control (continue / single-step / step-over) |
| `syscall-step` | Run until next syscall entry or exit |
| `break <addr> [cond]` | Set a breakpoint, optionally conditional |
| `tbreak <addr>` | Set a temporary breakpoint |
| `watch <addr> [len] [w\|rw\|x]` | Set a watchpoint |
| `delete <id>` | Delete a breakpoint/watchpoint |
| `info <b\|r\|t\|m\|target>` | Show breakpoints/registers/threads/maps/target (fixed: `b` now works) |
| `x /Nxb <addr>` | Examine N bytes in hex |
| `x /Nxw <addr>` | Examine N words |
| `set <addr> = <byte>...` | Write bytes to memory |
| `disas [addr] [count]` | Full x86-64 disassembly |
| `edit [addr]` | Interactive TUI memory editor |
| `decomp [addr] [max_bytes]` | Native C decompilation via RetDec/angr |
| `decomp file <bin> [addr]` | Decompile whole file or one function |
| `pretty set <lang>` | **NEW** Set pretty printer language |
| `pretty cstring/cpp_string/rust_string/go_string/py_string <addr>` | **NEW** Print language-specific strings |
| `pretty struct <addr> <desc>` | **NEW** Parse struct by type descriptor |
| `bt` / `backtrace` | Show call stack |
| `target <binary>` | Load a binary for static patching |
| `patch nop/apply/save/undo` | Binary patching (fixed: `undo` now 0-indexed + no-arg=last) |
| `stealth list/on/off` | Anti-detect technique management |
| `kernel status/load/unload` | Kernel module management |
| `script run lua/python <file>` | Run Lua/Python scripts (fixed: `ndbg` module now importable) |
| `help` / `quit` | Help / exit |

---

## 🔬 Decompilation — Native C via RetDec / angr

NinjaDBG integrates Avast's RetDec decompiler as a native backend, with
angr as an alternative.

### Backends

| Backend | Mechanism | Best for |
|---------|-----------|----------|
| **retdec-native** | `dlopen("libretdec.so")` | Per-function decompilation of live processes |
| **retdec-subprocess** | Shell to `retdec-decompiler` | Whole-file decompilation |
| **angr** | `python3 -c` subprocess | Stripped binaries; **fixed in v1.1.0: `func.normalize()` added** |

### Example (verified working in v1.1.0)

```bash
(ninjadb) decomp set angr
(ninjadb) decomp file /tmp/test_factorial 0x401139
Backend: angr  Elapsed: 1896 ms
Function: sub_0x401139

---- function factorial at 0x401139 ----
int factorial(int a0)
{
    return (a0 <= 1 ? 1 : a0 * factorial(a0 - 1));
}
```

---

## 🐍 Scripting — Lua + Python

Both backends expose the same `ndbg` module. **Fixed in v1.1.0**: the
`ndbg` module is now properly registered in `sys.modules`, so both
`import ndbg` and bare `ndbg.xxx()` work in Python scripts.

```python
# dump_regs.py — now works with `import ndbg` too!
import ndbg  # This works in v1.1.0 (was broken in v1.0.5)

pid = int(sys.argv[1])
ndbg.attach(pid)
regs = ndbg.info_registers()
rip = regs['rip']
ndbg.log(f'RIP = 0x{rip:x}')
instrs = ndbg.disassemble(rip, 10)
for i, ins in enumerate(instrs):
    ndbg.log(f'  [{i}] {ins}')
ndbg.detach()
```

---

## 🛡️ Anti-Detect Techniques

8 userland + 8 kernel-level techniques. See the full table in the source
code (`include/AntiDetect.h`, `include/KernelStealth.h`).

---

## 🏗️ Architecture

```
NinjaDBG/
├── Makefile
├── LICENSE                     Apache License 2.0 (NEW v1.1.0)
├── README.md
├── resources/
│   ├── ninja_logo.png
│   ├── ninja_logo.svg
│   └── icons/                  11 SVG icons
├── include/
│   ├── Types.h
│   ├── DebuggerCore.h          ptrace-based core
│   ├── AntiDetect.h            8 userland stealth techniques
│   ├── KernelStealth.h         8 kernel techniques + LKM
│   ├── BinaryPatcher.h         ELF/PE/Mach-O patcher
│   ├── PlatformAdapters.h      Linux/Windows/macOS adapters
│   ├── Disassembler.h          Standalone x86-64 decoder
│   ├── InteractiveMemoryEditor.h  TUI memory editor
│   ├── ScriptEngine.h          Lua + Python JSON-RPC bridge
│   ├── Decompiler.h            RetDec + angr wrapper
│   ├── PrettyPrinter.h         [NEW v1.1.0] C/C++/Rust/Go/Python printers
│   ├── HeadlessCLI.h           CLI REPL
│   ├── WelcomeScreen.h         Apache-2.0 license flow
│   ├── UITheme.h
│   └── MainWindow.h            X11+Cairo GUI (experimental)
├── src/
│   ├── main.cpp
│   ├── DebuggerCore.cpp        (fixed: writeMemory, kill, attach, findFunctionStart)
│   ├── AntiDetect.cpp
│   ├── KernelStealth.cpp
│   ├── BinaryPatcher.cpp       (fixed: JmpAlways off-by-one, PE bounds, tellg check)
│   ├── PlatformAdapters.cpp
│   ├── Disassembler.cpp        (fixed: segment-override OOB)
│   ├── InteractiveMemoryEditor.cpp
│   ├── ScriptEngine.cpp        (fixed: ndbg module registration)
│   ├── Decompiler.cpp          (fixed: angr func.normalize())
│   ├── PrettyPrinter.cpp       [NEW v1.1.0]
│   ├── HeadlessCLI.cpp         (fixed: info b, patch undo, --no-eula-check)
│   ├── WelcomeScreen.cpp       (rewritten: Apache-2.0 license)
│   ├── MainWindow.cpp          (fixed: Cairo destructor order)
│   └── MainWindowPanels.cpp    (fixed: disassemble buffer overflow)
└── scripts/
    ├── target_test.cpp
    ├── ninjastealth.c
    ├── ninja_stealth_kmod.c
    ├── screenshot.cpp
    └── ...
```

---

## 📦 Build

### Prerequisites (Debian 13 / Ubuntu 24.04+)

```bash
sudo apt-get install build-essential \
                     libx11-dev libxext-dev \
                     libcairo2-dev \
                     libpango1.0-dev \
                     libpng-dev \
                     xvfb \
                     pkg-config

# Optional, for full features:
sudo apt-get install wine wine64 qemu-user linux-headers-$(uname -r)
pip3 install angr
sudo apt-get install retdec-dev
```

### Compile

```bash
cd NinjaDBG
make -j4
```

---

## 🚀 Run

```bash
# GUI (experimental)
./build/ninjadb

# Headless CLI (recommended)
./build/ninjadb --cli

# Batch mode
./build/ninjadb --cli --no-eula-check -c "attach 12345; disas; decomp; quit"
```

---

## ⚖️ License

**NinjaDBG v1.1.0 is Open Source under the Apache License 2.0.**

See [LICENSE](LICENSE) for the full text.

Previous versions (1.0.0 – 1.0.5) were Closed Source. Starting from
v1.1.0, the full source code is available under Apache-2.0. You are free to
use, modify, distribute, and sublicense the code.

---

## 🗺️ Roadmap

| Version | Target | Status |
|---------|--------|--------|
| 1.0.0–1.0.5 | Core debugger + CLI + decompiler + scripting (Closed Source) | ✅ Released |
| **1.1.0** | **Bug fixes, pretty printers, Open Source (Apache-2.0)** | ✅ **Released (this)** |
| 1.2.0 | Capstone integration for full x86-64 / ARM64 disassembly | 🔜 Planned |
| 1.3.0 | Remote debugging over TCP (gdbserver-style) | 🔜 Planned |
| 1.4.0 | Multi-process debugging with tabbed UI | 🔜 Planned |
| 2.0.0 | Signed kernel module, native macOS build, plugin SDK | 🔜 Planned |

---

## 👤 Author

**Chapzoo** (GitHub: **ChapzoMods**) — solo developer.

NinjaDBG is the work of a single person. All design, implementation,
testing, UI rendering, logo artwork, documentation, and distribution is
done by Chapzoo.

---

## 🤝 Contributing

Now that NinjaDBG is open source, contributions are welcome!

1. Fork the repo
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Commit your changes
4. Push to the branch (`git push origin feature/my-feature`)
5. Open a Pull Request

Please report bugs via GitHub Issues.

---

<div align="center">

**NinjaDBG v1.1.0** · Open Source (Apache-2.0) · by **Chapzoo**

*Stay stealthy.* 🥷

</div>
