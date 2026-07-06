// NinjaDBG v1.1.3 - Decompiler implementation (RetDec / angr backend)
// Open Source (Apache-2.0) - by Chapzoo
#include "Decompiler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <elf.h>

namespace ndbg {

Decompiler::Decompiler() {}
Decompiler::~Decompiler() {
    if (libretdec_handle_) dlclose(libretdec_handle_);
}

// ===== Backend name + detection =====

bool Decompiler::binaryInPath(const std::string& name) {
    std::string cmd = "which " + name + " > /dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

bool Decompiler::isRetDecSubprocessAvailable() const {
    if (retdec_bin_checked_) return retdec_bin_ok_;
    retdec_bin_checked_ = true;
    retdec_bin_ok_ = binaryInPath("retdec-decompiler") ||
                     binaryInPath("retdec");
    return retdec_bin_ok_;
}

bool Decompiler::isAngrAvailable() const {
    if (angr_checked_) return angr_ok_;
    angr_checked_ = true;
    // Check if python3 can import angr
    int rc = std::system("python3 -c 'import angr' > /dev/null 2>&1");
    angr_ok_ = (rc == 0);
    return angr_ok_;
}

bool Decompiler::loadRetDecNative() const {
    if (libretdec_checked_) return libretdec_ok_;
    libretdec_checked_ = true;

    // Try common sonames
    const char* candidates[] = {
        "libretdec.so",
        "libretdec.so.4",
        "libretdec.so.4.0",
        "/usr/lib/x86_64-linux-gnu/libretdec.so",
        "/usr/local/lib/libretdec.so",
        "/opt/retdec/lib/libretdec.so",
        nullptr
    };
    for (int i = 0; candidates[i]; i++) {
        void* h = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (h) {
            // Sanity: check for a known symbol
            void* sym = dlsym(h, "retdec_decompile");
            if (sym) {
                libretdec_handle_ = h;
                libretdec_ok_ = true;
                return true;
            }
            dlclose(h);
        }
    }
    return false;
}

bool Decompiler::isRetDecNativeAvailable() const {
    return loadRetDecNative();
}

Decompiler::Backend Decompiler::detectAvailable() const {
    if (isRetDecNativeAvailable())     return Backend::RetDecNative;
    if (isRetDecSubprocessAvailable()) return Backend::RetDecSubprocess;
    if (isAngrAvailable())             return Backend::Angr;
    return Backend::NoBackend;
}

std::string Decompiler::installHint(Backend b) const {
    switch (b) {
        case Backend::RetDecNative:
            return "Install RetDec native library:\n"
                   "  Ubuntu/Debian:  sudo apt-get install retdec-dev\n"
                   "  From source:    git clone https://github.com/avast/retdec && "
                   "cd retdec && cmake -DCMAKE_INSTALL_PREFIX=/usr/local . && "
                   "make -j$(nproc) && sudo make install\n"
                   "  This provides libretdec.so for in-process decompilation.";
        case Backend::RetDecSubprocess:
            return "Install RetDec command-line tool:\n"
                   "  Ubuntu/Debian:  sudo apt-get install retdec-utils\n"
                   "  From source:    see https://github.com/avast/retdec\n"
                   "  This provides the 'retdec-decompiler' binary.";
        case Backend::Angr:
            return "Install angr (Python):\n"
                   "  pip3 install angr\n"
                   "  (requires Python 3.8+ and ~500MB of dependencies)";
        default:
            return "No decompiler backend available.";
    }
}

// ===== Subprocess runner =====

Decompiler::ProcResult Decompiler::runSubprocess(const std::string& cmd,
                                                   const std::vector<std::string>& args) const {
    ProcResult r;
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
        r.err = "pipe failed";
        return r;
    }
    pid_t pid = fork();
    if (pid < 0) {
        r.err = "fork failed";
        return r;
    }
    if (pid == 0) {
        close(out_pipe[0]); close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]); close(err_pipe[1]);
        std::vector<const char*> argv;
        argv.push_back(cmd.c_str());
        for (auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execvp(cmd.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    close(out_pipe[1]); close(err_pipe[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
        r.out.append(buf, n);
    }
    while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0) {
        r.err.append(buf, n);
    }
    close(out_pipe[0]); close(err_pipe[0]);
    int status;
    waitpid(pid, &status, 0);
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

std::string Decompiler::tempFilePath(const std::string& suffix) {
    char tmpl[] = "/tmp/ninjadb_decomp_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return "";
    close(fd);
    std::string path(tmpl);
    path += suffix;
    rename(tmpl, path.c_str());
    return path;
}

std::string Decompiler::readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Decompiler::writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << content;
    return f.good();
}

// ===== Native RetDec decompilation (dlopen) =====
//
// RetDec's native API (libretdec) exposes:
//   retdec::Config config;
//   config.setInputFile(...);
//   config.setOutputFile(...);
//   retdec::decompile(config);
//
// Since we can't include RetDec headers (it's an optional dependency),
// we use dlsym to grab the C-callable wrapper that some builds expose:
//   int retdec_decompile(const char* input, const char* output,
//                        const char* selected_range);
//
// If the C wrapper isn't available, we fall back to the subprocess path.

Decompiler::Result Decompiler::decompileRetDecNative(addr_t addr,
                                                       const u8* bytes, size_t len,
                                                       const std::string& binary_hint) {
    Result res;
    res.backend_used = "retdec-native";

    if (!loadRetDecNative()) {
        res.error = "libretdec.so not loadable";
        return res;
    }

    // Function pointer type for the C wrapper
    using retdec_decompile_fn = int (*)(const char*, const char*, const char*);
    auto fn = (retdec_decompile_fn)dlsym(libretdec_handle_, "retdec_decompile");
    if (!fn) {
        res.error = "retdec_decompile symbol not found in libretdec.so";
        return res;
    }

    // Write bytes to a temp ELF stub
    std::string stub_path = writeTempElfStub(addr, bytes, len, "func");
    if (stub_path.empty()) {
        res.error = "failed to write temp ELF stub";
        return res;
    }
    std::string out_path = tempFilePath(".c");
    char range[64];
    snprintf(range, sizeof(range), "0x%llx", (unsigned long long)addr);

    auto t0 = std::chrono::steady_clock::now();
    int rc = fn(stub_path.c_str(), out_path.c_str(), range);
    auto t1 = std::chrono::steady_clock::now();
    res.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (rc != 0) {
        res.error = "retdec_decompile returned " + std::to_string(rc);
        unlink(stub_path.c_str());
        unlink(out_path.c_str());
        return res;
    }

    res.c_code = readFile(out_path);
    res.ok = !res.c_code.empty();
    res.function_name = "sub_" + std::string(range);
    unlink(stub_path.c_str());
    unlink(out_path.c_str());
    return res;
}

// ===== Subprocess RetDec decompilation =====

Decompiler::Result Decompiler::decompileRetDecSubprocess(const std::string& binary_path,
                                                            addr_t addr,
                                                            bool whole_file) {
    Result res;
    res.backend_used = "retdec-subprocess";

    if (!isRetDecSubprocessAvailable()) {
        res.error = installHint(Backend::RetDecSubprocess);
        return res;
    }

    std::string binary = binary_path;
    std::string out_path = tempFilePath(".c");
    std::vector<std::string> args;
    args.push_back("-o"); args.push_back(out_path);
    if (!whole_file && addr != 0) {
        char range[64];
        snprintf(range, sizeof(range), "0x%llx", (unsigned long long)addr);
        args.push_back("--select-ranges"); args.push_back(range);
    }
    args.push_back(binary);

    auto t0 = std::chrono::steady_clock::now();
    ProcResult pr = runSubprocess("retdec-decompiler", args);
    auto t1 = std::chrono::steady_clock::now();
    res.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (pr.exit_code != 0) {
        res.error = "retdec-decompiler exited " + std::to_string(pr.exit_code) + ": " + pr.err;
        unlink(out_path.c_str());
        return res;
    }

    res.c_code = readFile(out_path);
    res.ok = !res.c_code.empty();
    if (addr != 0) {
        char nm[64]; snprintf(nm, sizeof(nm), "sub_0x%llx", (unsigned long long)addr);
        res.function_name = nm;
    }
    unlink(out_path.c_str());
    return res;
}

// ===== Subprocess angr decompilation =====
//
// angr doesn't ship a CLI binary, so we invoke it via python3 -c.

Decompiler::Result Decompiler::decompileAngrSubprocess(const std::string& binary_path,
                                                          addr_t addr,
                                                          bool whole_file) {
    Result res;
    res.backend_used = "angr";

    if (!isAngrAvailable()) {
        res.error = installHint(Backend::Angr);
        return res;
    }

    // Python script that uses angr + angr's decompiler (analyses/decompiler.py)
    std::string py_script;
    if (whole_file) {
        py_script =
            "import sys, angr\n"
            "p = angr.Project(sys.argv[1], auto_load_libs=False)\n"
            "cfg = p.analyses.CFGFast()\n"
            "out = []\n"
            "for fn in cfg.kb.functions.values():\n"
            "    if fn.is_simprocedure or fn.is_plt or fn.is_syscall:\n"
            "        continue\n"
            "    try:\n"
            "        fn.normalize()\n"
            "        dec = p.analyses.Decompiler(fn)\n"
            "        if dec.codegen is not None:\n"
            "            out.append('---- function ' + fn.name + ' at 0x%x ----' % fn.addr)\n"
            "            out.append(dec.codegen.text)\n"
            "            out.append('')\n"
            "    except Exception as e:\n"
            "        out.append('---- function %s failed: %s' % (fn.name, e))\n"
            "sys.stdout.write('\\n'.join(out))\n";
    } else {
        char addr_str[32];
        snprintf(addr_str, sizeof(addr_str), "0x%llx", (unsigned long long)addr);
        py_script =
            "import sys, angr\n"
            "p = angr.Project(sys.argv[1], auto_load_libs=False)\n"
            "cfg = p.analyses.CFGFast()\n"
            "target = int(sys.argv[2], 16)\n"
            "fn = cfg.kb.functions.get(target)\n"
            "if fn is None:\n"
            "    sys.stderr.write('No function at 0x%x' % target); sys.exit(1)\n"
            "fn.normalize()\n"
            "dec = p.analyses.Decompiler(fn)\n"
            "if dec.codegen is None:\n"
            "    sys.stderr.write('decompiler returned no code'); sys.exit(1)\n"
            "sys.stdout.write('---- function %s at 0x%x ----\\n' % (fn.name, fn.addr))\n"
            "sys.stdout.write(dec.codegen.text)\n";
    }

    std::vector<std::string> args;
    args.push_back("-c"); args.push_back(py_script);
    args.push_back(binary_path);
    if (!whole_file) {
        char addr_str[32];
        snprintf(addr_str, sizeof(addr_str), "0x%llx", (unsigned long long)addr);
        args.push_back(addr_str);
    }

    auto t0 = std::chrono::steady_clock::now();
    ProcResult pr = runSubprocess("python3", args);
    auto t1 = std::chrono::steady_clock::now();
    res.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (pr.exit_code != 0) {
        res.error = "angr exited " + std::to_string(pr.exit_code) + ": " + pr.err;
        return res;
    }

    res.c_code = pr.out;
    res.ok = !res.c_code.empty();
    if (addr != 0) {
        char nm[64]; snprintf(nm, sizeof(nm), "sub_0x%llx", (unsigned long long)addr);
        res.function_name = nm;
    }
    return res;
}

// ===== ELF stub writer =====
//
// RetDec's native API expects a loadable ELF (or PE/Mach-O). For in-process
// decompilation of a function we read from the target's memory and wrap
// those bytes into a minimal ELF64 stub with a single executable segment.
//
// This is a minimal ELF — no sections, no dynamic linking, just one PT_LOAD
// segment at vaddr 0x400000 with the function's bytes mapped at the correct
// virtual offset so RetDec's address references match the live process.

std::string Decompiler::writeTempElfStub(addr_t addr, const u8* bytes, size_t len,
                                          const std::string& basename) const {
    // Align the function address down to a page boundary for the segment base
    const u64 SEG_BASE = 0x400000;
    const u64 PAGE = 0x1000;
    u64 func_offset_in_seg = addr & (PAGE - 1);  // offset within first page
    u64 seg_vaddr = addr & ~(PAGE - 1);
    u64 seg_file_off = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    // We pad before the function bytes to keep vaddr alignment correct
    u64 pad_before = func_offset_in_seg;
    u64 total_seg_size = pad_before + len;
    // Pad total to page size
    if (total_seg_size < PAGE) total_seg_size = PAGE;

    std::string out_path = tempFilePath(".elf");
    std::ofstream f(out_path, std::ios::binary);
    if (!f) return "";

    // ELF64 header
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = 0x7F;
    ehdr.e_ident[1] = 'E'; ehdr.e_ident[2] = 'L'; ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2;  // ELFCLASS64
    ehdr.e_ident[5] = 1;  // ELFDATA2LSB
    ehdr.e_ident[6] = 1;  // EV_CURRENT
    ehdr.e_ident[7] = 0;  // ELFOSABI_NONE
    ehdr.e_type = 2;      // ET_EXEC
    ehdr.e_machine = 0x3E; // EM_X86_64
    ehdr.e_version = 1;
    ehdr.e_entry = addr;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = 0;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = 1;
    ehdr.e_shentsize = 0;
    ehdr.e_shnum = 0;
    ehdr.e_shstrndx = 0;
    f.write((char*)&ehdr, sizeof(ehdr));

    // Program header (one PT_LOAD)
    Elf64_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = 1;  // PT_LOAD
    phdr.p_flags = 5; // R+X
    phdr.p_offset = seg_file_off;
    phdr.p_vaddr = seg_vaddr;
    phdr.p_paddr = seg_vaddr;
    phdr.p_filesz = total_seg_size;
    phdr.p_memsz = total_seg_size;
    phdr.p_align = PAGE;
    f.write((char*)&phdr, sizeof(phdr));

    // Pad bytes before the function
    std::vector<u8> pad(pad_before, 0x90);  // NOP padding
    f.write((char*)pad.data(), pad.size());
    // Function bytes
    f.write((const char*)bytes, len);
    // Pad to fill the page
    size_t remaining = total_seg_size - pad_before - len;
    if (remaining > 0) {
        std::vector<u8> tail(remaining, 0x90);
        f.write((char*)tail.data(), remaining);
    }
    f.close();
    return out_path;
}

// ===== Public decompilation API =====

Decompiler::Result Decompiler::decompileFunction(DebuggerCore& dbg, addr_t addr, size_t max_bytes) {
    Result res;
    // Read function bytes from live process
    auto bytes = dbg.readMemoryVec(addr, max_bytes);
    if (bytes.empty()) {
        res.error = "readMemory failed at " + hex(addr);
        return res;
    }

    Backend b = backend_;
    if (b == Backend::Auto) b = detectAvailable();
    if (b == Backend::NoBackend) {
        res.error = "No decompiler backend available.\n" + installHint(Backend::RetDecNative);
        return res;
    }

    if (b == Backend::RetDecNative) {
        // Try native first
        res = decompileRetDecNative(addr, bytes.data(), bytes.size(), "");
        if (!res.ok) {
            // Fall through to subprocess if native failed
            if (isRetDecSubprocessAvailable()) {
                std::string stub = writeTempElfStub(addr, bytes.data(), bytes.size(), "func");
                if (!stub.empty()) {
                    res = decompileRetDecSubprocess(stub, addr, false);
                    unlink(stub.c_str());
                }
            }
        }
        return res;
    }
    if (b == Backend::RetDecSubprocess) {
        std::string stub = writeTempElfStub(addr, bytes.data(), bytes.size(), "func");
        if (stub.empty()) {
            res.error = "failed to write temp ELF stub";
            return res;
        }
        res = decompileRetDecSubprocess(stub, addr, false);
        unlink(stub.c_str());
        return res;
    }
    if (b == Backend::Angr) {
        std::string stub = writeTempElfStub(addr, bytes.data(), bytes.size(), "func");
        if (stub.empty()) {
            res.error = "failed to write temp ELF stub";
            return res;
        }
        res = decompileAngrSubprocess(stub, addr, false);
        unlink(stub.c_str());
        return res;
    }

    res.error = "no backend";
    return res;
}

Decompiler::Result Decompiler::decompileFunctionInFile(const std::string& binary_path, addr_t addr) {
    Backend b = backend_;
    if (b == Backend::Auto) b = detectAvailable();
    if (b == Backend::NoBackend) {
        Result res;
        res.error = "No decompiler backend available.\n" + installHint(Backend::RetDecNative);
        return res;
    }
    if (b == Backend::RetDecNative) {
        // Native doesn't need a stub for a real binary file
        // But we need to read the function bytes from the file first.
        // Easier: fall through to subprocess for file-based decompilation.
        if (isRetDecSubprocessAvailable()) {
            return decompileRetDecSubprocess(binary_path, addr, false);
        }
    }
    if (b == Backend::RetDecSubprocess) {
        return decompileRetDecSubprocess(binary_path, addr, false);
    }
    if (b == Backend::Angr) {
        return decompileAngrSubprocess(binary_path, addr, false);
    }
    Result res; res.error = "no backend"; return res;
}

Decompiler::Result Decompiler::decompileFile(const std::string& binary_path) {
    Backend b = backend_;
    if (b == Backend::Auto) b = detectAvailable();
    if (b == Backend::NoBackend) {
        Result res;
        res.error = "No decompiler backend available.\n" + installHint(Backend::RetDecNative);
        return res;
    }
    if (b == Backend::RetDecNative && isRetDecSubprocessAvailable()) {
        // For whole-file decompilation the subprocess is preferred (RetDec's
        // native API is mainly designed for per-function work).
        return decompileRetDecSubprocess(binary_path, 0, true);
    }
    if (b == Backend::RetDecSubprocess) {
        return decompileRetDecSubprocess(binary_path, 0, true);
    }
    if (b == Backend::Angr) {
        return decompileAngrSubprocess(binary_path, 0, true);
    }
    Result res; res.error = "no backend"; return res;
}

std::vector<Decompiler::FunctionInfo> Decompiler::listFunctions(const std::string& binary_path,
                                                                  std::string& err) {
    std::vector<FunctionInfo> result;
    Backend b = backend_;
    if (b == Backend::Auto) b = detectAvailable();

    if (b == Backend::Angr || b == Backend::Auto) {
        if (isAngrAvailable()) {
            std::string py =
                "import sys, angr, json\n"
                "p = angr.Project(sys.argv[1], auto_load_libs=False)\n"
                "cfg = p.analyses.CFGFast()\n"
                "out = []\n"
                "for fn in cfg.kb.functions.values():\n"
                "    if fn.is_simprocedure or fn.is_plt or fn.is_syscall: continue\n"
                "    out.append({'addr': fn.addr, 'name': fn.name, 'size': fn.size})\n"
                "sys.stdout.write(json.dumps(out))\n";
            std::vector<std::string> args;
            args.push_back("-c"); args.push_back(py);
            args.push_back(binary_path);
            ProcResult pr = runSubprocess("python3", args);
            if (pr.exit_code == 0) {
                // Crude JSON parse: extract {addr, name, size} tuples
                std::string s = pr.out;
                size_t pos = 0;
                while ((pos = s.find("{", pos)) != std::string::npos) {
                    size_t end = s.find("}", pos);
                    if (end == std::string::npos) break;
                    std::string obj = s.substr(pos, end - pos + 1);
                    FunctionInfo fi;
                    auto a = obj.find("\"addr\"");
                    if (a != std::string::npos) {
                        auto colon = obj.find(":", a);
                        fi.address = strtoull(obj.c_str() + colon + 1, nullptr, 0);
                    }
                    auto n = obj.find("\"name\"");
                    if (n != std::string::npos) {
                        auto q1 = obj.find("\"", n + 6);
                        auto q2 = obj.find("\"", q1 + 1);
                        if (q1 != std::string::npos && q2 != std::string::npos)
                            fi.name = obj.substr(q1 + 1, q2 - q1 - 1);
                    }
                    auto sz = obj.find("\"size\"");
                    if (sz != std::string::npos) {
                        auto colon = obj.find(":", sz);
                        fi.size = strtoull(obj.c_str() + colon + 1, nullptr, 0);
                    }
                    result.push_back(fi);
                    pos = end + 1;
                }
                return result;
            }
            err = pr.err;
            return result;
        }
    }

    if (b == Backend::RetDecSubprocess || b == Backend::RetDecNative) {
        if (isRetDecSubprocessAvailable()) {
            // RetDec doesn't have a clean "list functions" CLI mode;
            // we can run with --stop-after gen-selector and parse its log.
            // For simplicity, just decompile and let the user see functions.
            err = "RetDec subprocess doesn't support function listing directly. "
                  "Use 'decomp file <binary>' to decompile all functions.";
            return result;
        }
    }
    err = "No backend available for function listing";
    return result;
}

std::string Decompiler::apiDocs() {
    return R"DOCS(
NinjaDBG v1.1.3 Decompilation API
==================================

NinjaDBG integrates Avast's RetDec decompiler (and optionally angr) to
convert native binaries back into readable C source code.

Backends
--------

  1. retdec-native    (preferred)  — dlopen("libretdec.so"), in-process
  2. retdec-subprocess            — shell to `retdec-decompiler` binary
  3. angr                         — `python3 -m angr` subprocess

Backend selection is automatic; you can force one with `decomp set <name>`.

CLI commands
------------

  decomp <addr>                   Decompile function at addr in live process
  decomp <addr> [max_bytes]       Limit input size (default 4096 bytes)
  decomp file <binary>            Decompile an entire binary file to C
  decomp file <binary> <addr>     Decompile one function in a file
  decomp list                     Show available backends
  decomp api                      Show this help
  decomp set <retdec-native|retdec-subprocess|angr|auto>
                                  Force a specific backend

Installation
------------

  RetDec native:
    sudo apt-get install retdec-dev
    # OR from source:
    git clone https://github.com/avast/retdec
    cd retdec && cmake . && make -j$(nproc) && sudo make install

  RetDec subprocess:
    sudo apt-get install retdec-utils

  angr:
    pip3 install angr

Examples
--------

  # Decompile the function at the current RIP (live process)
  (ninjadb) attach 12345
  (ninjadb) decomp

  # Decompile a specific function by address
  (ninjadb) decomp 0x401234

  # Decompile a whole binary file
  (ninjadb) decomp file ./suspicious_binary

  # Decompile one function in a file
  (ninjadb) decomp file ./suspicious_binary 0x401234
)DOCS";
}

} // namespace ndbg
