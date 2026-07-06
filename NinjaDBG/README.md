<div align="center">

# 🥷 NinjaDBG

<img src="resources/ninja_logo.png" alt="NinjaDBG" width="220" height="220" />

### Stealth-Aware Native Debugger for Linux x86-64<br/>with experimental Windows & macOS support

**Version 1.0.3** · Closed Source · Free · Created by **Chapzoo**

[Features](#-features) · [Headless CLI](#-headless-cli) · [Kernel Stealth](#-kernel-level-stealth) · [Binary Patching](#-binary-patching) · [Cross-Platform](#-cross-platform-debugging) · [License](#-license)

> ⚠️ **The graphical interface is EXPERIMENTAL and still under active development.**
> For production use, prefer the headless CLI (`ninjadb --cli`). The CLI exposes
> the full feature set — kernel stealth, binary patching, conditional breakpoints,
> watchpoints, step-over/step-out, syscall stepping — while the GUI currently
> exposes a subset. See [§ Roadmap](#-roadmap) for what's planned.

</div>

---

## 📖 Overview

**NinjaDBG** is a closed-source, free, native C++17 debugger for Linux x86-64
with experimental cross-platform support for Windows (PE) and macOS (Mach-O)
binaries via Wine and QEMU adapters. It is engineered around one principle:
**silence**. Where conventional debuggers leave obvious traces — INT3 bytes in
`.text`, `TracerPid` set in `/proc/self/status`, parent process names like
`gdb` or `lldb`, kernel-visible `wchan` of `ptrace_stop` — NinjaDBG masks,
redirects, or eliminates each signal so that the target process believes it is
running alone.

It is purpose-built for analysts working against:

- **Packed binaries** (UPX, Themida, VMProtect) that abort when traced
- **Malware loaders** that scan their own `.text` for `0xCC` software breakpoints
- **DRM / license-check routines** that probe `ptrace` state via `/proc/self/wchan` or `/proc/self/syscall`
- **Anti-cheat agents** that enumerate `/proc/<pid>/maps` looking for injected
  preload libraries, or that check parent process names against a denylist

NinjaDBG v1.0.3 adds: a full-featured **headless CLI**, **kernel-level stealth**
(via a loadable kernel module), a **binary patcher** for in-place static
patches without attaching, **conditional + temporary breakpoints**, hardware
**watchpoints**, real **step-over** and **step-out**, **syscall-entry
stepping**, **cross-platform debugging** for Windows PE and macOS Mach-O
binaries, and a **welcome screen + EULA** flow.

---

## ✨ Features

### Debugging engine

| Capability | Status | Notes |
|---|---|---|
| Attach to running process by PID | ✅ | `ptrace(PTRACE_ATTACH)` |
| Launch + trace new process | ✅ | `PTRACE_TRACEME` + `execv` |
| Detach / kill | ✅ | `PTRACE_DETACH` / `PTRACE_KILL` |
| Single-step (instruction) | ✅ | `PTRACE_SINGLESTEP` |
| **Step over** (skip CALL) | ✅ **NEW v1.0.3** | Auto-detects CALL, sets temp bp after |
| **Step out** (run until return) | ✅ **NEW v1.0.3** | Sets temp bp on return address from stack |
| Continue / pause | ✅ | `PTRACE_CONT`, `SIGSTOP` |
| Software breakpoints (INT3) | ✅ | 0xCC patching, original byte preserved |
| Hardware breakpoints (DR0-DR3) | ✅ | API surface, INT3 fallback |
| **Conditional breakpoints** | ✅ **NEW v1.0.3** | `"rax == 0x10"` syntax, evaluated against live regs |
| **Temporary breakpoints** | ✅ **NEW v1.0.3** | Auto-removed after first hit |
| **Watchpoints** (memory access) | ✅ **NEW v1.0.3** | DR0-DR3 with RW/LEN fields; W / RW / X |
| Read/write GPRs + RIP + RFLAGS | ✅ | All 16 GPRs + segment regs |
| Read/write target memory | ✅ | `process_vm_readv` / `process_vm_writev` (stealth) |
| Enumerate threads | ✅ | Walks `/proc/<pid>/task` |
| Parse `/proc/<pid>/maps` | ✅ | Region permissions, offsets, paths |
| Follow child processes | ✅ | `PTRACE_O_TRACECLONE / TRACEFORK / TRACEEXEC` |
| Auto-detach on parent exit | ✅ | `PTRACE_O_EXITKILL` |
| **Backtrace** (RBP chain walk) | ✅ **NEW v1.0.3** | Symbol resolution from `/proc/<pid>/maps` |
| **Syscall stepping** | ✅ **NEW v1.0.3** | `PTRACE_SYSCALL`, distinguishes entry vs exit |

### Stealth subsystem

| Layer | Status | Notes |
|---|---|---|
| Userland anti-detect (8 techniques) | ✅ | See [§ Anti-Detect Techniques](#-anti-detect-techniques) |
| `libninjastealth.so` preload payload | ✅ | Auto-generated, masks `TracerPid:` in target's `/proc/self/status` reads |
| **Kernel-level stealth (LKM)** | ✅ **NEW v1.0.3** | Optional `ninja_stealth.ko` module hides NinjaDBG at the kernel level |
| Kernel technique count | 8 | Hide PID, mask wchan, mask syscall, mask comm, suppress SIGCHLD, force dumpable, intercept PTRACE_TRACEME, hide mmaps |

### Binary patching (NEW v1.0.3)

| Capability | Status |
|---|---|
| Load ELF32 / ELF64 binaries | ✅ |
| Load PE32 / PE64 binaries | ✅ |
| Load Mach-O 32/64 / FAT binaries | ✅ |
| Section enumeration | ✅ |
| Apply NOP patch | ✅ |
| Apply custom-bytes patch | ✅ |
| Convert Jcc → JMP (always take) | ✅ |
| Convert Jcc → NOP (never take) | ✅ |
| Convert CALL → NOP | ✅ |
| Convert "call; test; jz" → "mov eax,1; ..." (force return true) | ✅ |
| Replace ASCII strings (same length or shorter) | ✅ |
| Undo patches | ✅ |
| Save patched binary (never overwrites source) | ✅ |
| Pattern / ASCII search | ✅ |
| SHA-256 integrity display | ✅ (stub) |

### Cross-platform debugging (NEW v1.0.3)

| Target platform | Status | Mechanism |
|---|---|---|
| **Linux ELF** | ✅ Native | `ptrace(2)` directly |
| **Windows PE** | ✅ Experimental | Wine + `winedbg --gdb` (GDB Remote Serial Protocol) |
| **macOS Mach-O** | ✅ Experimental | `qemu-x86_64 -g` (GDB RSP) on Linux; native `mach exception ports` on macOS |

Platform is auto-detected from binary magic bytes (ELF / MZ / FEEDFACE / FEEDFACF / CAFEBABE).

### User interface

| Mode | Status | Notes |
|---|---|---|
| **Headless CLI** | ✅ Production | Full-featured REPL with batch mode (`-c`) |
| **GUI (X11 + Cairo + Pango)** | ⚠️ Experimental | Multi-panel dark "ninja" theme; under active development |

The GUI exposes: process list, disassembly, memory hex dump, stack view,
registers, anti-detect toggles, threads, breakpoints, About modal. The CLI
exposes the full feature set including binary patching, kernel stealth
control, conditional breakpoints, watchpoints, step-over/step-out, and
syscall stepping.

---

## 🛡️ Anti-Detect Techniques

NinjaDBG exposes 8 toggleable **userland** techniques (always available)
plus 8 toggleable **kernel** techniques (require the LKM to be loaded).

### Userland (always available)

| # | Technique | Defeats | How |
|---|-----------|---------|-----|
| 1 | **Hardware Breakpoints** | INT3 byte scans of `.text` | Uses DR0-DR3 debug registers; no `0xCC` written into target code |
| 2 | **`process_vm_readv` / `writev`** | ptrace-event observation | Memory access uses `process_vm_readv(2)` instead of `PTRACE_PEEKDATA`, producing no ptrace signals the target can intercept |
| 3 | **Mask `/proc/self/status`** | `TracerPid:` field read | Injects `libninjastealth.so` via `LD_PRELOAD`; hooks `open()` + `read()` to rewrite `TracerPid:\t0` even when actively traced |
| 4 | **Hide NinjaDBG mmaps** | Target enumerating its address space | Filters NinjaDBG's own mmap regions from the target's `/proc/<pid>/maps` view |
| 5 | **Timing normalization** | RDTSC / `clock_gettime` delta checks | Wraps timing syscalls in the preload payload to flatten measured deltas |
| 6 | **Parent name masquerade** | `/proc/ppid/comm` inspection | Sets `argv[0] = "[kworker/u:1]"` so parent-process name reads as a benign kernel worker |
| 7 | **Hide from ps** | `readdir(/proc)` enumeration | Renames the NinjaDBG comm field to skip past suspicious-process filters |
| 8 | **INT3 scan bypass** | Self-`.text` scanning for `0xCC` | Disables software breakpoints entirely (migrates them to HW slots) when active |

### Kernel-level (require `ninja_stealth.ko` LKM — NEW v1.0.3)

Userland anti-detect cannot hide from a target that reads `/proc/self/wchan`,
`/proc/self/syscall`, or `/proc/<our_pid>/comm` directly — those reads go
through the kernel's procfs implementation. To bypass ALL such checks,
NinjaDBG v1.0.3 ships an optional loadable kernel module that hooks procfs
read handlers and syscall entry.

| # | Kernel technique | Defeats |
|---|------------------|---------|
| 1 | **Hide PID from /proc** | `readdir(/proc)` enumeration skipping our PID |
| 2 | **Mask `/proc/self/wchan`** | wchan returning `ptrace_stop` |
| 3 | **Mask `/proc/self/syscall`** | syscall field showing stopped-syscall number |
| 4 | **Mask `/proc/<pid>/comm`** | comm field reading `ninjadb` |
| 5 | **Suppress tracer SIGCHLD** | timing-based detection via extra SIGCHLD |
| 6 | **Force PR_SET_DUMPABLE=1** | `prctl(PR_GET_DUMPABLE)` returning 0 |
| 7 | **Intercept PTRACE_TRACEME** | target's self-TRACEME call succeeding |
| 8 | **Hide our mmap regions** | target enumerating `/proc/<pid>/maps` and finding injected `.so` |

Loading the LKM requires root and (on most modern distributions) disabled
module signature enforcement or MOK enrollment. See
[§ Kernel Stealth](#-kernel-level-stealth) below.

---

## 🖥️ Headless CLI

The headless CLI is the recommended interface for production use. It exposes
the full feature set and runs without an X server, making it ideal for SSH
sessions, CI pipelines, and malware-analysis sandboxes.

### Launching

```bash
# Interactive REPL
ninjadb --cli

# Batch mode (commands separated by ;)
ninjadb --cli -c "target ./malware; patch nop 0x401000 16; patch save ./patched; quit"

# Skip EULA prompt (for automated environments — accept once first)
ninjadb --cli --no-eula-check
```

### Command reference

| Command | Description |
|---------|-------------|
| `attach <pid>` | Attach to a running process |
| `launch <bin> [args...]` | Launch a new process under the debugger |
| `detach` / `kill` | Detach or kill the target |
| `continue` / `step` / `next` | Run control (continue / single-step / step-over) |
| `syscall-step` | Run until next syscall entry or exit |
| `break <addr> [cond]` | Set a breakpoint, optionally conditional (e.g. `break 0x401000 rax == 0x10`) |
| `tbreak <addr>` | Set a temporary breakpoint (auto-removed after first hit) |
| `watch <addr> [len] [w\|rw\|x]` | Set a watchpoint |
| `delete <id>` | Delete a breakpoint/watchpoint |
| `info <b\|r\|t\|m\|target>` | Show breakpoints / registers / threads / maps / target info |
| `x /Nxb <addr>` | Examine N bytes in hex |
| `x /Nxw <addr>` | Examine N words |
| `set <addr> = <byte>...` | Write bytes to memory |
| `bt` / `backtrace` | Show call stack |
| `target <binary>` | Load a binary for static patching |
| `patch list` | List applied patches |
| `patch nop <off> <len>` | NOP a byte range |
| `patch apply <off> <kind> [bytes...]` | Apply a patch (`nop`/`jmp`/`nojmp`/`callnop`/`rettrue`/`ascii`) |
| `patch save <outfile>` | Save patched binary (never overwrites source) |
| `patch undo <id>` | Undo a patch |
| `stealth list` | List anti-detect techniques |
| `stealth on\|off <name>` | Enable/disable a technique (substring match) |
| `kernel status` | Show kernel module status |
| `kernel load` | Build + load the stealth LKM (requires root) |
| `kernel unload` | Unload the LKM |
| `help` / `quit` | Help / exit |

---

## 🧱 Kernel-Level Stealth

The optional `ninja_stealth.ko` kernel module hooks procfs reads and
syscall entry to hide NinjaDBG at the kernel level. This defeats targets
that read `/proc/self/wchan`, `/proc/self/syscall`, `/proc/<our_pid>/comm`,
or that call `prctl(PR_GET_DUMPABLE)`.

### Building + loading

```bash
# 1. Install kernel headers
sudo apt-get install linux-headers-$(uname -r)

# 2. From the CLI, build and load
ninjadb --cli
(ninjadb) kernel load
# → builds ninja_stealth.ko, attempts sudo insmod

# 3. Verify
cat /proc/ninja_stealth
# → "NinjaDBG stealth active, hidden_pid=NNNN"

# 4. Unload when done
(ninjadb) kernel unload
```

### Limitations

- Requires root to load.
- Module is unsigned — requires `module.sig_enforce=0` or MOK enrollment.
- Compiles against the running kernel's headers only.
- Distribution of signed LKMs is outside the scope of this free release.
- If the LKM is not loaded, the 8 userland techniques still apply — kernel
  stealth is a strict superset, only relevant for the most aggressive
  anti-debug routines.

---

## 🔧 Binary Patching

NinjaDBG v1.0.3 can patch binary files in-place without attaching a
debugger. This is useful for permanently NOPing out anti-debug checks,
replacing conditional jumps, or rewriting strings.

### Supported formats

- **ELF** 32-bit and 64-bit (Linux, BSD)
- **PE** 32-bit and 64-bit (Windows)
- **Mach-O** 32-bit, 64-bit, and FAT (universal, macOS)

### Patch kinds

| Kind | Effect | Length |
|------|--------|--------|
| `nop` | Replace N bytes with `0x90` | any |
| `jmp` | Convert Jcc rel8/rel32 → JMP rel8/rel32 (always take) | 2 or 6 |
| `nojmp` | Convert Jcc → NOPs (never take) | 2 or 6 |
| `callnop` | Convert CALL rel32 → 5×NOP | 5 |
| `rettrue` | Replace `call; test; jz` → `mov eax,1; nop...` (force return true) | any ≥ 5 |
| `ascii` | Replace ASCII string (same length or shorter, null-padded) | any |
| custom | User-supplied bytes | any |

### Example: permanently disable an anti-debug check

```bash
ninjadb --cli -c "
  target ./suspicious_binary;
  patch apply 0x401234 callnop;
  patch apply 0x401250 jmp;
  patch save ./suspicious_binary.patched;
  quit
"
```

---

## 🌐 Cross-Platform Debugging

NinjaDBG auto-detects the target platform from binary magic bytes and
routes to the appropriate adapter.

### Linux ELF (native)

No additional dependencies. Uses `ptrace(2)` directly.

### Windows PE (via Wine)

Requires `wine` and `winedbg` installed:

```bash
sudo apt-get install wine wine64
```

NinjaDBG launches `winedbg --gdb --port=12345 <target.exe>` and connects
to the GDB Remote Serial Protocol endpoint on `127.0.0.1:12345`. All
debugging primitives (registers, memory, breakpoints, stepping) are
translated through the RSP.

### macOS Mach-O (via QEMU)

Requires `qemu-user` installed:

```bash
sudo apt-get install qemu-user
```

NinjaDBG launches `qemu-x86_64 -g 1234 -L /usr/x86_64-macos <target>`
and connects to the QEMU gdbstub on `127.0.0.1:1234`. On real macOS,
NinjaDBG can be built natively to use `mach exception ports` directly
(out of scope for the Linux release).

### Platform detection

| Magic bytes | Platform | Adapter |
|-------------|----------|---------|
| `7F 45 4C 46` | Linux ELF | `LinuxNativeAdapter` |
| `4D 5A` | Windows PE | `WindowsDebugAdapter` |
| `FE ED FA CE` | macOS Mach-O 32 | `MachDebugAdapter` |
| `FE ED FA CF` | macOS Mach-O 64 | `MachDebugAdapter` |
| `CE FA ED FE` | macOS Mach-O (swapped) | `MachDebugAdapter` |
| `CA FE BA BE` | macOS FAT (universal) | `MachDebugAdapter` |

---

## 🏗️ Architecture

```
NinjaDBG/
├── Makefile                       Build system (g++ + pkg-config)
├── README.md                      This file
├── resources/
│   ├── ninja_logo.png             Ninja-mask logo (user-supplied)
│   ├── ninja_logo.svg             Same logo as SVG (deprecated)
│   └── icons/                     Per-button SVG icons (11 files)
├── include/
│   ├── Types.h                    Common types & structs
│   ├── DebuggerCore.h             ptrace-based core (public API)
│   ├── AntiDetect.h               Userland stealth subsystem
│   ├── KernelStealth.h            Kernel-level stealth (LKM)  [NEW v1.0.3]
│   ├── BinaryPatcher.h            Static binary patcher       [NEW v1.0.3]
│   ├── PlatformAdapters.h         Cross-platform adapters     [NEW v1.0.3]
│   ├── HeadlessCLI.h              Headless CLI REPL           [NEW v1.0.3]
│   ├── WelcomeScreen.h            Welcome + EULA flow         [NEW v1.0.3]
│   ├── UITheme.h                  Colors, fonts, layout constants
│   └── MainWindow.h               X11+Cairo main window controller
├── src/
│   ├── main.cpp                   Entry point with --cli / GUI mode select
│   ├── DebuggerCore.cpp           ptrace wrapper + v1.0.3 advanced features
│   ├── AntiDetect.cpp             Technique registry + payload source generator
│   ├── KernelStealth.cpp          LKM source generator + load/unload
│   ├── BinaryPatcher.cpp          ELF / PE / Mach-O parser + patch operations
│   ├── PlatformAdapters.cpp       Linux / Windows / macOS adapters (RSP)
│   ├── HeadlessCLI.cpp            REPL + batch mode + all command handlers
│   ├── WelcomeScreen.cpp          Welcome message + EULA text + persistence
│   ├── MainWindow.cpp             X11 event loop, actions, toolbar, logo render
│   └── MainWindowPanels.cpp       Panel painting (disasm, mem, regs, modals, …)
├── scripts/
│   ├── target_test.cpp            Demo target with a TracerPid anti-debug check
│   ├── ninjastealth.c             Generated preload payload source
│   ├── ninja_stealth_kmod.c       Generated kernel module source           [NEW]
│   ├── Kbuild                     Kernel module build file                 [NEW]
│   ├── screenshot.cpp             Xlib+libpng screenshot helper
│   └── screenshot.sh              Xvfb + screenshot orchestration
└── build/                         Build output (created by `make`)
    ├── ninjadb                    The debugger binary (GUI + CLI)
    ├── target_test                Demo target binary
    ├── libninjastealth.so         Preload payload
    ├── ninja_stealth.ko           Optional kernel module                  [NEW]
    └── screenshot                 Screenshot helper binary
```

### Technology choices

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Language | C++17 | Modern, portable, zero-runtime-overhead |
| Debug API (Linux) | `ptrace(2)` + `process_vm_readv(2)` | Linux-native, no external deps |
| Debug API (Windows) | Wine + `winedbg --gdb` (RSP) | No native Win32 dependency on Linux |
| Debug API (macOS) | `qemu-x86_64 -g` (RSP) on Linux; `mach` on macOS | Works without a Mac |
| UI rendering (GUI) | Xlib + Cairo + Pango | No Qt/GTK, fully native |
| CLI | Custom REPL (no readline dep) | Builds everywhere, no extra libs |
| Image output | libpng | Direct PNG writing for screenshots |
| Build system | Make + pkg-config | No CMake, no Meson — just `make` |

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
```

Optional, for cross-platform debugging:

```bash
sudo apt-get install wine wine64 qemu-user
```

Optional, for kernel-level stealth:

```bash
sudo apt-get install linux-headers-$(uname -r)
```

### Compile

```bash
cd NinjaDBG
make -j4
```

This produces:

| Artifact | Path | Size (approx) |
|----------|------|----------------|
| Debugger binary (GUI + CLI) | `build/ninjadb` | 380 KB |
| Demo target | `build/target_test` | 17 KB |
| Preload payload | `build/libninjastealth.so` | 15 KB |
| Screenshot helper | `build/screenshot` | 23 KB |
| Kernel module (optional) | `build/ninja_stealth.ko` | built on demand |

---

## 🚀 Run

### GUI (experimental)

```bash
./build/ninjadb
```

### Headless CLI (recommended for production)

```bash
./build/ninjadb --cli
```

### Batch mode

```bash
./build/ninjadb --cli -c "attach 12345; break 0x401000 rax == 0x10; continue; info r; quit"
```

### Headless under Xvfb for screenshots

```bash
Xvfb :99 -screen 0 1920x1200x24 -ac +extension RANDR -noreset &
DISPLAY=:99 ./build/ninjadb
```

---

## ⚖️ License

**NinjaDBG is Closed Source but 100% Free.**

| Right | Status |
|-------|--------|
| Use — personal, academic, commercial | ✅ Allowed |
| Redistribute verbatim binaries | ✅ Allowed |
| Modify / patch binaries | ❌ Not allowed |
| Reverse-engineer / decompile | ❌ Not allowed |
| Sublicense / relicense | ❌ Not allowed |
| Hold author liable | ❌ No warranty, use at your own risk |

The source code is **private** and is **not** published. This README, the
logo PNG/SVG, the per-button SVG icons, the public API surface, and the
compiled artifacts are the only distributed materials.

By downloading or running NinjaDBG you accept the full EULA shown at first
launch (also reproduced in `WelcomeScreen.cpp`). Acceptance is persisted to
`~/.config/ninjadb/eula_accepted`. To re-show the EULA, delete that file.

---

## 🗺️ Roadmap

| Version | Target | Status |
|---------|--------|--------|
| 1.0.0 | Initial ptrace core + UI | ✅ Released |
| 1.0.1 | AntiDetect module + preload payload | ✅ Released |
| 1.0.2 | UI polish: SVG icons, modal fixes, logo redesign, README rewrite | ✅ Released |
| **1.0.3** | **Headless CLI, kernel stealth, binary patching, cross-platform, conditional bps, watchpoints, step-over/out, EULA** | ✅ **Released (this)** |
| 1.0.4 | CLI-side disassembly, in-CLI memory editing, scripting via Lua | 🔜 Planned |
| 1.1.0 | Conditional breakpoints GUI, watchpoints GUI, in-GUI memory editing | 🔜 Planned |
| 1.2.0 | Capstone integration for full x86-64 / ARM64 disassembly | 🔜 Planned |
| 1.3.0 | Remote debugging over TCP (gdbserver-style) | 🔜 Planned |
| 1.4.0 | Multi-process debugging with tabbed UI | 🔜 Planned |
| 2.0.0 | Signed kernel module, native macOS build, plugin SDK | 🔜 Planned |

---

## 👤 Author

**Chapzoo** — solo developer.

NinjaDBG is the work of a single person. All design, implementation, testing,
UI rendering, logo artwork, documentation, and distribution is done by
Chapzoo. There is no team, no company, no external contributor agreement, and
no funding model. Bug reports and feature requests are welcome; pull requests
are not, since the source is closed.

---

<div align="center">

**NinjaDBG v1.0.3** · Closed Source · Free · by **Chapzoo**

*Stay stealthy.* 🥷

</div>
