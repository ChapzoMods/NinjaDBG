# 🥷 NinjaDBG

<p align="center">
  <img src="resources/ninja_logo.svg" alt="NinjaDBG Logo" width="180" height="180" />
</p>

<p align="center">
  <strong>NinjaDBG v1.0.1</strong><br/>
  A stealth-aware C++17 debugger for Linux x86-64.<br/>
  Designed to evade common anti-debugging techniques used by packed binaries, malware, and license-protected software.
</p>

<p align="center">
  <em>Closed Source — Free for all uses — Created by <strong>Chapzoo</strong> (one person)</em>
</p>

---

## ⚖️ License

**NinjaDBG is Closed Source but 100% Free.**

| Right            | Status |
|------------------|--------|
| Use              | ✅ Allowed — personal, academic, commercial |
| Redistribute     | ✅ Allowed — verbatim binaries only |
| Modify           | ❌ Not allowed — source is not distributed |
| Sublicense       | ❌ Not allowed |
| Hold liable      | ❌ No warranty, use at your own risk |

You may use the compiled binaries freely. You may **not** redistribute modified
versions, reverse-engineer the binaries, or represent the work as your own.

The source code is private and is **not** published. This README, the SVG logo,
the public API surface, and the compiled artifacts are the only distributed
materials.

By downloading or running NinjaDBG you accept full responsibility for any
damage caused to your system or data. The author provides **no warranty**,
express or implied.

---

## 👤 Author

**Chapzoo** — solo developer.

NinjaDBG is the work of a single person. All design, implementation, testing,
and distribution is done by Chapzoo. There is no team, no company, and no
external contributor agreement. Bug reports and feature requests are welcome;
pull requests are not, since the source is closed.

---

## ✨ What is NinjaDBG?

NinjaDBG is a native C++17 debugger for Linux/x86-64 with a strong focus on
**stealth**: it is designed to attach to targets that actively try to detect
debugger presence and bail out, crash, or behave differently when traced.

Most modern packers, malware loaders, DRM wrappers, and anti-cheat agents
check for the presence of a debugger using one or more of these techniques:

1. **`ptrace(PTRACE_TRACEME)` self-check** — fails if the process is already traced.
2. **`/proc/self/status` → `TracerPid:` field** — non-zero indicates a tracer.
3. **`/proc/self/stat` wchan / signal inspection**.
4. **`INT3` (0xCC) byte scanning** — software breakpoints modify the .text section.
5. **`RDTSC` / `clock_gettime` timing checks** — single-stepping introduces measurable delay.
6. **Parent process name inspection** — `/proc/ppid/comm` reveals the debugger name.
7. **Self-mmap enumeration** — `/proc/self/maps` exposes injected libraries.

NinjaDBG counters each of these with a dedicated counter-technique (see
[§ Anti-Detect Techniques](#-anti-detect-techniques) below).

---

## 🧩 Features

### Core debugging
- ✅ Attach to running process by PID, or launch+trace a new process
- ✅ Software (`INT3`) and hardware (DR0-DR3) breakpoints
- ✅ Single-step, continue, step-into, step-out, pause, restart, kill
- ✅ Read/write target memory
- ✅ Read/write CPU registers (all 16 GPRs + RIP + RFLAGS + segment regs)
- ✅ Enumerate threads (`/proc/<pid>/task`)
- ✅ Parse `/proc/<pid>/maps` to display memory regions
- ✅ Built-in software x86-64 disassembler (covers common opcodes)

### Stealth subsystem
- ✅ `process_vm_readv` / `process_vm_writev` for memory access (no ptrace events)
- ✅ Hardware breakpoint support (DR0-DR3) — no `0xCC` left in target .text
- ✅ `LD_PRELOAD` injection payload that masks `TracerPid:` in `/proc/self/status`
- ✅ Parent process name masquerade (`argv[0] = "[kworker/u:1]"`)
- ✅ Timing normalization hook (RDTSC / clock_gettime)
- ✅ Per-technique enable/disable toggles in the UI

### User interface
- ✅ Professional dark "ninja" theme (charcoal + neon cyan)
- ✅ Multi-panel layout:
  - Process list (left)
  - Disassembly (center top)
  - Memory hex dump (center middle)
  - Stack view (center bottom)
  - Registers (right top)
  - Anti-Detect toggles (right middle)
  - Threads (right bottom)
  - Breakpoints (bottom)
- ✅ Toolbar with Launch / Attach / Detach / Run / Pause / Step / Step In / Step Out / Restart / Kill / About
- ✅ Status bar with live state, PID, anti-detect status
- ✅ Modal dialogs: About, Process picker, PID input
- ✅ Procedurally rendered ninja-mask logo (also available as SVG)
- ✅ Keyboard shortcuts: F5 = Continue, F10 = Step, F11 = Step In, F8 = Pause

---

## 🛡️ Anti-Detect Techniques

NinjaDBG exposes 8 toggleable stealth techniques. Each one defeats a specific
anti-debugging check that the target may perform.

| # | Technique | What it defeats |
|---|-----------|-----------------|
| 1 | **Hardware Breakpoints** | INT3 (0xCC) byte scanning of `.text` |
| 2 | **process_vm_readv / writev** | ptrace-event observation via PTRACE_PEEKDATA counters |
| 3 | **Mask /proc/self/status** | `TracerPid:` field read by target |
| 4 | **Hide NinjaDBG mmaps** | Target enumerating its own address space looking for injected .so |
| 5 | **Timing normalization** | RDTSC / clock_gettime delta checks |
| 6 | **Parent name masquerade** | Reading `/proc/ppid/comm` to spot the debugger name |
| 7 | **Hide from ps** | `readdir(/proc)` enumeration skipping suspicious parent processes |
| 8 | **INT3 scan bypass** | Target scanning its own code for `0xCC` |

### How the `TracerPid` mask works

When NinjaDBG launches a target, it injects a small `LD_PRELOAD` payload
(`libninjastealth.so`) that hooks `open(2)` and `read(2)`. When the target
opens `/proc/self/status`, the hook remembers the file descriptor. When the
target reads from that descriptor, the hook rewrites the `TracerPid:` line
to `TracerPid:\t0` — even when the process is actively being traced.

This means the most common Linux anti-debug check — `TracerPid != 0` —
returns a clean "no debugger" answer to the target, even while NinjaDBG
holds full ptrace control.

The source for the preload payload is generated at runtime by the
AntiDetect module (in `scripts/ninjastealth.c` after first run) and
compiled to `build/libninjastealth.so`.

---

## 🏗️ Architecture

```
NinjaDBG/
├── Makefile                  — build system (g++, pkg-config)
├── README.md                 — this file
├── include/
│   ├── Types.h               — common types (u8/u16/u32/u64/addr_t, structs)
│   ├── DebuggerCore.h        — ptrace-based debugger core (public API)
│   ├── AntiDetect.h          — stealth subsystem (technique mask + payload builder)
│   ├── UITheme.h             — colors, fonts, layout constants
│   └── MainWindow.h          — main X11+Cairo window controller
├── src/
│   ├── main.cpp              — entry point
│   ├── DebuggerCore.cpp      — ptrace wrapper, attach/detach, breakpoints, regs, mem
│   ├── AntiDetect.cpp        — technique registry, payload source generator
│   ├── MainWindow.cpp        — X11 event loop, actions, toolbar, modals
│   └── MainWindowPanels.cpp  — all panel painting (disasm, mem, regs, stack, etc.)
├── resources/
│   └── ninja_logo.svg        — ninja-mask logo (SVG, scalable)
├── scripts/
│   ├── target_test.cpp       — demo target with anti-debug check (TracerPid scan)
│   ├── screenshot.cpp        — Xlib+libpng screenshot helper
│   └── screenshot.sh         — Xvfb + screenshot orchestration
└── build/                    — build output (created by make)
```

### Technology stack

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Language | C++17 | Modern, portable, low-overhead |
| Debug API | `ptrace(2)` + `process_vm_readv/writev(2)` | Linux-native, no external deps |
| UI | Xlib + Cairo + Pango | No Qt/GTK dependency, fully native |
| Disassembly | Built-in (software) | No capstone dependency, predictable output |
| Image output | libpng | Direct PNG writing for screenshots |
| Display server | X11 (Xvfb for headless) | Standard, universally available |

The choice of Xlib over Qt/GTK is deliberate: a debugger should be a
low-overhead, dependency-light tool. Xlib + Cairo gives us a fully native,
professional dark UI with zero framework lock-in.

---

## 🚀 Building

### Prerequisites (Debian/Ubuntu)

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
- `build/ninjadb` — the debugger binary
- `build/target_test` — a small demo target with a `TracerPid:` anti-debug check
- `build/libninjastealth.so` — preload payload (auto-built on first run)
- `build/screenshot` — Xlib+libpng screenshot helper

---

## 🎮 Running

### With a real X server

```bash
./build/ninjadb
```

### Headless (Xvfb)

```bash
Xvfb :99 -screen 0 1920x1200x24 -ac +extension RANDR -noreset &
DISPLAY=:99 ./build/ninjadb
```

### Demo target

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
# Click [Attach] in the toolbar, select `target_test`, click [Attach]
# Notice: the target's "Anti-debug: TracerPid=NNNN detected!" message
# never fires, because the preload payload masks TracerPid: to 0.
```

---

## 🎨 Screenshot

A screenshot of the v1.0.1 UI (attached to the demo target) is included
at `download/ninjadb_attached.png`.

The About modal (with the ninja-mask logo) is at
`download/ninjadb_about.png`.

---

## ⌨️ Keyboard Shortcuts

| Key | Action |
|-----|--------|
| F5  | Continue execution |
| F8  | Pause |
| F10 | Step over |
| F11 | Step into |
| Esc | Close modal dialog |

---

## 🗺️ Roadmap

Future versions may include:
- v1.1: Conditional breakpoints (string + numeric conditions)
- v1.2: Watchpoints (memory access breakpoints via DR0-DR3 with RW/LEN fields)
- v1.3: Remote debugging over TCP (gdbserver-style protocol)
- v1.4: Embedded Capstone for full x86-64 disassembly
- v1.5: Scripting via Lua (auto-stepping, conditional logging)
- v2.0: Multi-process debugging with tabbed UI

These dates are not commitments — Chapzoo works on this in spare time.

---

## 🐛 Known Limitations

- The `TracerPid` mask requires the target to be launched under NinjaDBG
  with `LD_PRELOAD`. Attaching to an already-running process can only
  mask `TracerPid` via a kernel module (out of scope for v1.0.1).
- Hardware breakpoints are limited to 4 concurrent slots (DR0-DR3, x86
  hardware constraint, not a NinjaDBG limitation).
- The built-in disassembler covers common opcodes but is not a complete
  x86-64 decoder. Unusual instructions fall back to `db 0xXX`.
- No GUI for editing memory in-place yet (use the console command `set`
  once it lands in v1.1).
- The UI is single-threaded; very large memory dumps may briefly stall
  the event loop.

---

## 🤝 Feedback

Bug reports and feature requests are welcome. Reach out to **Chapzoo**.
Source code is **not** distributed — please don't ask.

---

<p align="center">
  <em>NinjaDBG v1.0.1 — Closed Source, Free, by Chapzoo</em><br/>
  <em>Stay stealthy. 🥷</em>
</p>
