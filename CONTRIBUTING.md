# Contributing to NinjaDBG

NinjaDBG is an open-source (Apache-2.0) stealth debugger for Linux x86-64,
maintained by a single person (Chapzoo). Community contributions are greatly
appreciated -- bug reports, fixes, new commands, new pretty printers, new
stealth hooks, and documentation improvements all help move the project
forward.

This document explains how to set up a development environment, the code
style conventions, how to add new features, and how to submit a pull
request.

---

## 1. Fork and clone

1. Fork the repository on GitHub:
   <https://github.com/ChapzoMods/NinjaDBG/fork>
2. Clone your fork locally:
   ```bash
   git clone https://github.com/<your-username>/NinjaDBG.git
   cd NinjaDBG
   ```
3. Add the upstream remote so you can sync with the official repo:
   ```bash
   git remote add upstream https://github.com/ChapzoMods/NinjaDBG.git
   git fetch upstream
   ```
4. Create a feature branch for your work:
   ```bash
   git checkout -b my-feature upstream/main
   ```

---

## 2. Build instructions

NinjaDBG v1.1.4 has no GUI dependencies. The only required packages are
`build-essential` and `pkg-config`:

```bash
sudo apt-get install build-essential pkg-config
```

Optional packages (see `INSTALL.md` for the full list):

- `wine`, `wine64`                -- Windows PE cross-debugging
- `qemu-user`                     -- macOS Mach-O cross-debugging
- `python3` + `angr`              -- angr decompilation backend (`pip3 install angr`)
- `retdec-dev`                    -- RetDec native decompilation backend
- `lua5.4` + `liblua5.4-dev`      -- Lua scripting backend
- `linux-headers-$(uname -r)`     -- kernel stealth LKM

Build everything:

```bash
make -j4
```

This produces:

- `build/ninjadb`              -- the CLI binary
- `build/target_test`          -- demo target program
- `build/libninjastealth.so`   -- preload stealth payload

Run the test suite:

```bash
make test
# or directly:
cd tests && make test
```

---

## 3. Code style

All contributed code must follow these rules.

### Language and standard

- C++17 for everything under `src/` and `include/`.
- C11 for `scripts/ninjastealth.c` and any other `.c` files.
- The Makefile uses `-std=c++17` for C++ and `-std=c11` for C.

### Indentation and formatting

- 4 spaces per indent level. No tabs.
- Opening brace on the same line as the control statement.
- One statement per line.
- Lines should generally stay under 100 columns.

### Naming

- Classes: `PascalCase` (`DebuggerCore`, `PrettyPrinter`).
- Methods and free functions: `camelCase` (`addBreakpoint`, `parseAddr`).
- Member variables: `trailing_underscore_` (`dbg_`, `running_`).
- Local variables: `snake_case` (`addr`, `byte_count`).
- Constants and enum values: `PascalCase` enumerators (`Language::Cpp`).

### License header

Every source file must begin with the Apache-2.0 header. Use this exact
template (replace the file name and description on the first line):

For C++ files:

```cpp
// NinjaDBG v1.1.4 - <one-line description>
// Open Source (Apache-2.0) - by Chapzoo
```

For C files:

```c
/*
 * NinjaDBG v1.1.4 - <one-line description>
 * Open Source (Apache-2.0) - by Chapzoo
 */
```

### Error handling

- Prefer returning a status code or `bool` and storing detail in a
  `lastError()`-style accessor over throwing exceptions.
- User-facing messages go through `HeadlessCLI::out()` / `err()` so they
  are testable in batch mode.
- No emojis anywhere -- in source, docs, or output.

### Warnings

The Makefile builds with `-Wall -Wextra`. New code must compile cleanly
under these flags. The two existing suppressions
(`-Wno-unused-parameter` and `-Wno-unused-but-set-variable`) are
grandfathered; do not introduce new categories of warnings.

---

## 4. How to add a new CLI command

This is the most common contribution. Adding a command called `foo`:

1. **Decide on syntax.** Pick a short command name (`foo`) and any
   subcommands. Decide whether it needs an attached process, a target
   binary, or neither.

2. **Add the handler declaration** in `include/HeadlessCLI.h` inside the
   `HeadlessCLI` class, next to the other `cmdXxx` declarations:

   ```cpp
   void cmdFoo(const std::vector<std::string>& args);
   ```

3. **Implement the handler** in `src/HeadlessCLI.cpp`. Place it near
   related handlers and follow the existing pattern. Always guard against
   missing arguments and missing target process:

   ```cpp
   void HeadlessCLI::cmdFoo(const std::vector<std::string>& args) {
       if (args.empty()) { err("usage: foo <something>"); return; }
       if (dbg_.pid() == 0) { err("Not attached"); return; }
       // ... actual work ...
       out("foo: done");
   }
   ```

4. **Wire it into the dispatcher** in `HeadlessCLI::execute()` (around
   line 100 of `src/HeadlessCLI.cpp`). Add an `else if` branch:

   ```cpp
   else if (cmd == "foo" || cmd == "f") cmdFoo(args);
   ```

5. **Document it in `cmdHelp()`** (around line 800 of the same file). Add
   a one-line entry to the help string with the same column alignment as
   the other entries.

6. **Document it in `README.md`** in the command reference table.

7. **Add a test** in `tests/test_basic.cpp` that runs
   `ninjadb --no-eula-check -c "foo ..."` and asserts on the output.

8. **Build and test:**

   ```bash
   make -j4 && make test
   ```

---

## 5. How to add a new pretty printer

Pretty printers live in `src/PrettyPrinter.cpp` and
`include/PrettyPrinter.h`. To add a printer for a new type (for example, a
`std::vector<int>`):

1. **Add a method** to the `PrettyPrinter` class in the header:

   ```cpp
   std::string printStdVectorInt(const DebuggerCore& dbg, addr_t addr);
   ```

2. **Implement it** in `src/PrettyPrinter.cpp`. Read the struct layout
   from the target process via `dbg.readMemoryVec(addr, n)` and format
   the output as plain text. Use `hex(addr)` for the address prefix.

3. **Add a CLI subcommand** under `cmdPretty` in `src/HeadlessCLI.cpp`.
   Pick a short subcommand name (e.g. `vec_int`):

   ```cpp
   if (sub == "vec_int") {
       if (args.size() < 2) { err("usage: pretty vec_int <addr>"); return; }
       bool ok; addr_t a = parseAddr(args[1], &ok);
       if (!ok) { err("bad address"); return; }
       out(pretty_.printStdVectorInt(dbg_, a));
       return;
   }
   ```

4. **Document it** in `cmdHelp()` (in `src/HeadlessCLI.cpp`),
   `PrettyPrinter::apiDocs()` (in `src/PrettyPrinter.cpp`), and
   `README.md`.

5. **Add a test** that creates a small target binary, attaches to it,
   and prints the vector. The existing `scripts/target_test.cpp` is a
   good template for new test targets.

---

## 6. How to add a new stealth hook to libninjastealth.c

`scripts/ninjastealth.c` is the LD_PRELOAD payload injected into the
target process. Each hook intercepts a libc function and rewrites the
result so anti-debug code cannot detect the debugger.

To add a new hook for, say, `readlink("/proc/self/exe")`:

1. **Add the function pointer type and storage** near the top of
   `scripts/ninjastealth.c`:

   ```c
   typedef ssize_t (*readlink_fn)(const char*, char*, size_t);
   static readlink_fn real_readlink = NULL;
   ```

2. **Resolve it in `init_real()`**:

   ```c
   if (!real_readlink) real_readlink = (readlink_fn)dlsym(RTLD_NEXT, "readlink");
   ```

3. **Add a buffer-rewriting helper** if the hook rewrites data. Use
   `memmem` / `memchr` (bounded by the read count) -- never `strstr` /
   `strchr` (which require NUL termination and over-read procfs data).

4. **Implement the hook function** with the same signature as the libc
   function it replaces:

   ```c
   ssize_t readlink(const char* path, char* buf, size_t bufsiz) {
       init_real();
       if (!real_readlink) { errno = ENOSYS; return -1; }
       ssize_t r = real_readlink(path, buf, bufsiz);
       if (r > 0 && path && strcmp(path, "/proc/self/exe") == 0) {
           /* Rewrite buf here, bounded by r. */
       }
       return r;
   }
   ```

5. **Rebuild the library** and run the stealth test:

   ```bash
   gcc -shared -fPIC -O2 -ldl -o build/libninjastealth.so scripts/ninjastealth.c
   cd tests && make test
   ```

6. **Document the hook** in `README.md` (the "libninjastealth.so hooks"
   table) and in the comment block at the top of `scripts/ninjastealth.c`.

Important: every hook must check that `real_<fn>` is non-NULL before
calling it. If `dlsym` fails, the hook must set `errno` and return an
error code rather than crash.

---

## 7. How to write tests

There are two test programs under `tests/`:

- `test_basic.cpp` -- exercises the CLI binary via `popen()` and asserts
  on the captured stdout. Use this for any CLI-facing change.
- `test_stealth.c` -- a standalone C binary that probes
  `/proc/self/status` and `/proc/self/comm`. Use this when adding or
  changing stealth hooks.

### Adding a test to test_basic.cpp

1. Add a new function `test_my_feature()` that runs the binary and checks
   the output:

   ```cpp
   static void test_my_feature() {
       std::string out;
       int rc = run_capture(
           g_ninjadb_path + " --no-eula-check -c \"my-cmd\"", out);
       check(rc == 0, "my-cmd exits 0");
       check(contains(out, "expected substring"), "my-cmd output");
   }
   ```

2. Call it from `main()`.

3. Each `check()` prints `PASS` or `FAIL` and increments `g_passed` /
   `g_failed`. The binary returns non-zero if any check failed.

### Running tests

From the repo root:

```bash
make test
```

Or from `tests/`:

```bash
make test
```

Both run `test_basic` then `test_stealth`. The Makefile target exits
non-zero if either test fails, so it is safe to use from CI.

### Test guidelines

- Tests must not require root.
- Tests must not leave background processes running.
- Tests must not modify files outside the build directory.
- Each test should be independent and idempotent.
- Use `--no-eula-check` so tests run non-interactively.

---

## 8. How to submit a pull request

1. **Sync with upstream** before starting work:

   ```bash
   git fetch upstream
   git checkout main
   git merge --ff-only upstream/main
   git checkout -b my-feature
   ```

2. **Make your changes** following the code style above. Keep commits
   focused: one logical change per commit.

3. **Write tests** for any new behavior. Run the full suite:

   ```bash
   make -j4 && make test
   ```

4. **Update documentation**: `README.md`, `INSTALL.md`, and the help text
   in `cmdHelp()` should all reflect the new behavior.

5. **Commit with a clear message**. Use the imperative mood
   (`Add foo command`, not `Added foo command`). Reference any related
   issue number (`Fixes #123`).

6. **Push to your fork**:

   ```bash
   git push -u origin my-feature
   ```

7. **Open a pull request** against the `main` branch of
   `ChapzoMods/NinjaDBG`. Fill in the PR template (if present) with:
   - What the change does.
   - Why it is needed.
   - How it was tested.
   - Any breaking changes.

8. **Respond to review feedback**. The maintainer may request changes;
   please push additional commits to the same branch (do not force-push
   unless asked).

### Pull request checklist

- Code compiles cleanly with `-Wall -Wextra`.
- Apache-2.0 header is on every new source file.
- No emojis in source, output, or docs.
- New commands are documented in `cmdHelp()` and `README.md`.
- New tests are added and `make test` passes.
- Commit messages follow the imperative-mood convention.

---

## 9. Reporting bugs

Open a GitHub issue at
<https://github.com/ChapzoMods/NinjaDBG/issues/new> with:

- NinjaDBG version (`./build/ninjadb --help`).
- Linux distribution and kernel version (`uname -a`).
- The exact command you ran.
- The expected vs actual output.
- A minimal reproducer if possible.

Security-sensitive bugs (stealth bypasses, crashes that could leak data)
can be reported privately by email to the maintainer before public
disclosure.

---

## 10. Maintainer note

NinjaDBG is maintained by one person in their spare time. Reviews may
take a few days. Large contributions (new subsystems, major refactors)
are best discussed in an issue before you start coding, so the design
can be agreed up front and your work does not go to waste.

Thank you for contributing.
