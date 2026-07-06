<div align="center">

# 🥷 NinjaDBG

<img src="resources/ninja_logo.svg" alt="NinjaDBG" width="220" height="220" />

### Stealth-Aware Native Debugger for Linux x86-64

**Version 1.0.2** · Closed Source · Free · Created by **Chapzoo**

[Features](#-features) · [Anti-Detect](#-anti-detect-techniques) · [Build](#-build) · [Screenshots](#-screenshots) · [License](#-license)

</div>

---

## 📖 Overview

**NinjaDBG** is a native C++17 debugger for Linux/x86-64 engineered around one
principle: **silence**. Where conventional debuggers leave obvious traces —
INT3 bytes in `.text`, `TracerPid` set in `/proc/self/status`, parent process
names like `gdb` or `lldb` — NinjaDBG masks, redirects, or eliminates each
signal so that the target process believes it is running alone.

It is purpose-built for analysts working against:

- **Packed binaries** (UPX, Themida, VMProtect) that abort when traced
- **Malware loaders** that scan their own `.text` for `0xCC` software breakpoints
- **DRM / license-check routines** that probe `ptrace` state
- **Anti-cheat agents** that enumerate `/proc/<pid>/maps` looking for injected
  preload libraries

NinjaDBG exposes eight toggleable stealth techniques, a full multi-panel
graphical interface rendered directly on Xlib+Cairo (no Qt, no GTK, no
Electron), a built-in software x86-64 disassembler, and an `LD_PRELOAD`
payload generator that rewrites the `TracerPid:` field in the target's
`/proc/self/status` reads — defeating the most common Linux anti-debug check.

---

## ✨ Features

### Core debugging engine

| Capability | Status | Notes |
|---|---|---|
| Attach to running process by PID | ✅ | `ptrace(PTRACE_ATTACH)` |
| Launch + trace new process | ✅ | `PTRACE_TRACEME` + `execv` |
| Detach (resume normal execution) | ✅ | `PTRACE_DETACH` |
| Force-kill target | ✅ | `PTRACE_KILL` |
| Single-step | ✅ | `PTRACE_SINGLESTEP` |
| Continue / pause | ✅ | `PTRACE_CONT`, `SIGSTOP` |
| Software breakpoints (INT3) | ✅ | 0xCC patching, original byte preserved |
| Hardware breakpoints (DR0-DR3) | ✅ | API surface; INT3 fallback in 1.0.2 |
| Read/write GPRs + RIP + RFLAGS | ✅ | All 16 GPRs + segment regs |
| Read/write target memory | ✅ | `process_vm_readv` / `process_vm_writev` (stealth) |
| Enumerate threads | ✅ | Walks `/proc/<pid>/task` |
| Parse `/proc/<pid>/maps` | ✅ | Region permissions, offsets, paths |
| Follow child processes | ✅ | `PTRACE_O_TRACECLONE / TRACEFORK / TRACEEXEC` |
| Auto-detach on parent exit | ✅ | `PTRACE_O_EXITKILL` |

### Stealth subsystem

See [§ Anti-Detect Techniques](#-anti-detect-techniques) for the full table.

- Eight toggleable techniques exposed in the UI as live switches
- `libninjastealth.so` preload payload auto-generated at first run
- Per-technique human-readable name + description in the About dialog

### User interface

- **Native Xlib + Cairo + Pango** — no Qt, no GTK, no Electron
- Dark "ninja" theme (charcoal `#14161F` + neon cyan `#00FFE1`)
- **Toolbar** with SVG-style icons + labels, grouped by function:
  Session · Execution · Reset · Help
- **Eight synchronized panels**:
  - Process list (left, with live RSS / state)
  - Disassembly x86-64 (center top, with INT3/RIP/arrow annotations)
  - Memory hex dump (center middle, ASCII column)
  - Stack view (center bottom, with symbol resolution)
  - Registers (right top, RIP highlighted)
  - Anti-Detect toggles (right middle, ON/OFF switches)
  - Threads (right bottom)
  - Breakpoints (bottom strip, click to toggle)
- **Modal dialogs**: About, Process picker (with search box), PID input
- **Keyboard shortcuts**: F5 Continue · F8 Pause · F10 Step · F11 Step In · Esc Close

### Custom rendering

The ninja-mask logo is **rendered procedurally in Cairo** at 256×256 — gradients
for the silk headband, radial glows for the eyes, an 8-pointed gold shuriken
emblem, two crossed katanas with edge highlights, breathing slits on the lower
mask, a CRT-style scanline overlay, and corner accent brackets. The same logo
is also exported as a standalone `ninja_logo.svg` for use in documentation.

---

## 🛡️ Anti-Detect Techniques

NinjaDBG exposes eight toggleable stealth techniques. Each one defeats a
specific anti-debugging check that the target may perform.

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

### How the `TracerPid` mask works

The `TracerPid:` field in `/proc/self/status` is the single most common
Linux anti-debug check. A target reads the file, parses the line, and if the
value is non-zero it knows it is being traced.

NinjaDBG defeats this with a tiny `LD_PRELOAD` library (`libninjastealth.so`,
~15 KB) that intercepts `open(2)` and `read(2)`:

1. When the target opens `/proc/self/status`, the hook remembers the file
   descriptor.
2. When the target reads from that descriptor, the hook scans the buffer for
   `"TracerPid:"` and rewrites the line in-place to `"TracerPid:\t0"`.
3. The target sees a clean "no debugger" answer — while NinjaDBG holds full
   ptrace control behind the scenes.

The source for the payload is generated at runtime by the AntiDetect module
into `scripts/ninjastealth.c` and compiled on first run into
`build/libninjastealth.so`.

### Known limitation

The preload mask requires the target to be **launched** under NinjaDBG (so
that `LD_PRELOAD` is set in the child environment). Attaching to an
already-running process can only mask `TracerPid` via a kernel module — out
of scope for the 1.0.x series, planned for 2.0.

---

## 🏗️ Architecture

```
NinjaDBG/
├── Makefile                       Build system (g++ + pkg-config)
├── README.md                      This file
├── resources/
│   ├── ninja_logo.svg             Ninja-mask logo (SVG, 256×256)
│   └── icons/                     Per-button SVG icons (11 files)
├── include/
│   ├── Types.h                    Common types & structs
│   ├── DebuggerCore.h             ptrace-based core (public API)
│   ├── AntiDetect.h               Stealth subsystem
│   ├── UITheme.h                  Colors, fonts, layout constants
│   └── MainWindow.h               X11+Cairo main window controller
├── src/
│   ├── main.cpp                   Entry point + screenshot orchestration
│   ├── DebuggerCore.cpp           ptrace wrapper, attach, breakpoints, regs, mem
│   ├── AntiDetect.cpp             Technique registry + payload source generator
│   ├── MainWindow.cpp             X11 event loop, actions, toolbar, logo render
│   └── MainWindowPanels.cpp       Panel painting (disasm, mem, regs, modals, …)
├── scripts/
│   ├── target_test.cpp            Demo target with a TracerPid anti-debug check
│   ├── ninjastealth.c             Generated preload payload source
│   ├── screenshot.cpp             Xlib+libpng screenshot helper
│   └── screenshot.sh              Xvfb + screenshot orchestration
└── build/                         Build output (created by `make`)
    ├── ninjadb                    The debugger binary
    ├── target_test                Demo target binary
    ├── libninjastealth.so         Preload payload
    └── screenshot                 Screenshot helper binary
```

### Technology choices

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Language | C++17 | Modern, portable, zero-runtime-overhead |
| Debug API | `ptrace(2)` + `process_vm_readv(2)` / `writev(2)` | Linux-native, no external dependencies |
| UI rendering | Xlib + Cairo + Pango | No Qt/GTK dependency, fully native, predictable |
| Disassembly | Built-in software decoder | No capstone dependency, covers common x86-64 opcodes |
| Image output | libpng | Direct PNG writing for screenshots |
| Display server | X11 (works under Xvfb for headless) | Standard, universally available |
| Build system | Make + pkg-config | No CMake, no Meson — just `make` |

The choice of **Xlib + Cairo over Qt or GTK** is deliberate. A debugger should
be a low-overhead, dependency-light tool. Xlib gives us a fully native
professional dark UI with zero framework lock-in. The binary is 256 KB
stripped.

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

### Compile

```bash
cd NinjaDBG
make -j4
```

This produces:

| Artifact | Path | Size (approx) |
|----------|------|----------------|
| Debugger binary | `build/ninjadb` | 260 KB |
| Demo target | `build/target_test` | 17 KB |
| Preload payload | `build/libninjastealth.so` | 15 KB |
| Screenshot helper | `build/screenshot` | 23 KB |

### Clean rebuild

```bash
make clean && make -j4
```

---

## 🚀 Run

### With a real X server

```bash
./build/ninjadb
```

### Headless (Xvfb)

```bash
Xvfb :99 -screen 0 1920x1200x24 -ac +extension RANDR -noreset &
DISPLAY=:99 ./build/ninjadb
```

### Demo target workflow

In one terminal:

```bash
./build/target_test
# [target] NinjaDBG target sample v1.0 starting, pid=12345
# [target] No debugger detected (stealth OK)
# [target] tick=1 counter=1 checksum=0x...
```

In another terminal:

```bash
./build/ninjadb
# Click [Attach] → select `target_test` → click [Attach]
# Notice: the target's "Anti-debug: TracerPid=NNNN detected!" never fires,
# because libninjastealth.so rewrites TracerPid: to 0.
```

### Environment variables (for screenshots / automation)

| Variable | Effect |
|----------|--------|
| `NINJADBG_SHOW_ABOUT=1` | Auto-open the About modal 200 ms after launch |
| `NINJADBG_DEMO_ATTACH=1` | Auto-attach to a running `target_test` process if found |

---

## 🖼️ Screenshots

| Screenshot | Description |
|------------|-------------|
| `download/ninjadb_v1.0.2.png` | Main UI attached to a live process — full panels visible |
| `download/ninjadb_about_v1.0.2.png` | About modal with the redesigned logo and anti-detect technique list |

To regenerate screenshots:

```bash
make screenshot
```

---

## ⌨️ Keyboard Shortcuts

| Key | Action |
|-----|--------|
| **F5** | Continue execution |
| **F8** | Pause |
| **F10** | Step over |
| **F11** | Step into |
| **Esc** | Close modal dialog |

---

## 🗺️ Roadmap

| Version | Target | Status |
|---------|--------|--------|
| 1.0.0 | Initial ptrace core + UI | ✅ Released |
| 1.0.1 | AntiDetect module + preload payload | ✅ Released |
| **1.0.2** | **UI polish: SVG icons, modal fixes, logo redesign, README rewrite** | ✅ **Released (this)** |
| 1.1.0 | Conditional breakpoints (numeric + string conditions) | 🔜 Planned |
| 1.2.0 | Watchpoints (memory-access breakpoints via DR0-DR3 RW/LEN fields) | 🔜 Planned |
| 1.3.0 | Remote debugging over TCP (gdbserver-style protocol) | 🔜 Planned |
| 1.4.0 | Embed Capstone for full x86-64 disassembly | 🔜 Planned |
| 1.5.0 | Lua scripting (auto-stepping, conditional logging) | 🔜 Planned |
| 2.0.0 | Kernel module for `TracerPid` masking on already-running processes | 🔜 Planned |

These dates are not commitments — Chapzoo works on this in spare time.

---

## 🐛 Known Limitations

- **Preload-mask requirement**: the `TracerPid` mask needs the target to be
  launched under NinjaDBG. Attaching to an already-running process can only
  mask `TracerPid` via a kernel module (planned for 2.0).
- **Hardware breakpoint slots**: limited to 4 concurrent (DR0-DR3) — an x86
  hardware constraint, not a NinjaDBG limitation.
- **Software disassembler**: covers common opcodes; unusual instructions
  fall back to `db 0xXX`. Embedding Capstone (1.4.0) will resolve this.
- **No in-place memory editing GUI** yet — planned for 1.1.0.
- **Single-threaded UI**: very large memory dumps may briefly stall the
  event loop. A background-thread memory cache is planned for 1.3.0.

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

The source code is **private** and is **not** published. This README, the SVG
logo, the per-button SVG icons, the public API surface, and the compiled
artifacts are the only distributed materials.

By downloading or running NinjaDBG you accept full responsibility for any
damage caused to your system or data. The author provides **no warranty**,
express or implied.

---

## 👤 Author

**Chapzoo** — solo developer.

NinjaDBG is the work of a single person. All design, implementation, testing,
UI rendering, logo artwork, documentation, and distribution is done by
Chapzoo. There is no team, no company, no external contributor agreement, and
no funding model. Bug reports and feature requests are welcome; pull requests
are not, since the source is closed.

---

## 🤝 Feedback

Bug reports and feature requests are welcome. Reach out to **Chapzoo**.
Source code is **not** distributed — please don't ask.

---

<div align="center">

**NinjaDBG v1.0.2** · Closed Source · Free · by **Chapzoo**

*Stay stealthy.* 🥷

</div>
