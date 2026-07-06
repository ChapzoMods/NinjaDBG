// NinjaDBG v1.1.1 - Decompiler Module (RetDec / angr backend)
// Open Source (Apache-2.0) - by Chapzoo
//
// Provides full C decompilation of native binaries by wrapping the
// Avast RetDec decompiler as a native library. We do NOT reimplement
// decompilation from scratch — that would be tens of thousands of lines.
// Instead we integrate RetDec in two layers:
//
//   1. NATIVE (preferred): dlopen("libretdec.so") and call the C++ API
//      retdec::decompile() in-process. This gives us:
//        - Per-function decompilation by address (no whole-binary run)
//        - In-memory decompilation (no temp files)
//        - Lowest latency
//      Requires the retdec-dev package (or a from-source build of RetDec).
//
//   2. SUBPROCESS (fallback): shell out to the `retdec-decompiler` binary.
//      Works without libretdec.so installed, but is slower (process spawn
//      + whole-file analysis per call) and writes to temp files.
//
// If neither is available, the module reports which package to install.
//
// We also support angr as an alternative backend (Python-based), invoked
// via `python3 -m angr` as a subprocess. angr is slower but supports more
// binary formats and has better code recovery on stripped binaries.
//
// USAGE FROM CLI:
//   decomp <addr>                  Decompile function at address (live process)
//   decomp <addr> [lines]          Limit output to N lines
//   decomp file <binary>           Decompile a whole binary file
//   decomp file <binary> <addr>    Decompile one function in a file
//   decomp list                    Show available backends
//   decomp api                     Show decompiler API
//   decomp set <retdec|angr|auto>  Force a specific backend
#pragma once

#include "Types.h"
#include "DebuggerCore.h"
#include "Disassembler.h"
#include <string>
#include <vector>
#include <memory>

namespace ndbg {

class Decompiler {
public:
    enum class Backend {
        Auto,       // Try RetDec native, then RetDec subprocess, then angr
        RetDecNative,
        RetDecSubprocess,
        Angr,
        NoBackend
    };

    struct Result {
        bool        ok = false;
        std::string c_code;             // the decompiled C source
        std::string backend_used;       // "retdec-native" / "retdec-subprocess" / "angr"
        std::string error;              // empty on success
        double      elapsed_ms = 0;
        std::string function_name;      // resolved name (or "sub_XXX")
    };

    Decompiler();
    ~Decompiler();

    // ---- Backend selection ----
    Backend currentBackend() const { return backend_; }
    void setBackend(Backend b) { backend_ = b; }
    static std::string backendName(Backend b) {
        switch (b) {
            case Backend::Auto:            return "auto";
            case Backend::RetDecNative:    return "retdec-native";
            case Backend::RetDecSubprocess:return "retdec-subprocess";
            case Backend::Angr:            return "angr";
            case Backend::NoBackend:       return "none";
        }
        return "?";
    }

    // Probe what's actually available on this system.
    // Returns the most-preferred available backend.
    Backend detectAvailable() const;

    // Per-backend availability checks
    bool isRetDecNativeAvailable() const;      // libretdec.so loadable?
    bool isRetDecSubprocessAvailable() const;  // retdec-decompiler in PATH?
    bool isAngrAvailable() const;              // python3 -c 'import angr' ok?

    // Install hint for missing backends
    std::string installHint(Backend b) const;

    // ---- Decompilation API ----

    // Decompile a single function in a loaded (live) process.
    // `addr` is the function's start address. Reads bytes from the
    // attached process via DebuggerCore.
    Result decompileFunction(DebuggerCore& dbg, addr_t addr, size_t max_bytes = 0x1000);

    // Decompile a single function in a static binary file.
    Result decompileFunctionInFile(const std::string& binary_path, addr_t addr);

    // Decompile an entire binary file (RetDec's default mode).
    Result decompileFile(const std::string& binary_path);

    // List all functions detected in a binary (RetDec can do this via
    // its function-listing mode; angr via CFG analysis).
    struct FunctionInfo {
        addr_t      address;
        std::string name;
        size_t      size;
    };
    std::vector<FunctionInfo> listFunctions(const std::string& binary_path,
                                             std::string& err);

    // ---- API documentation ----
    static std::string apiDocs();

private:
    Backend backend_ = Backend::Auto;
    Disassembler disas_;

    // libretdec.so handle (lazy-loaded on first use)
    mutable void* libretdec_handle_ = nullptr;
    mutable bool  libretdec_checked_ = false;
    mutable bool  libretdec_ok_ = false;

    // Cached subprocess availability
    mutable bool retdec_bin_checked_ = false;
    mutable bool retdec_bin_ok_ = false;
    mutable bool angr_checked_ = false;
    mutable bool angr_ok_ = false;

    // ---- Native RetDec (dlopen) ----
    bool loadRetDecNative() const;
    Result decompileRetDecNative(addr_t addr, const u8* bytes, size_t len,
                                  const std::string& binary_hint);

    // ---- Subprocess RetDec ----
    Result decompileRetDecSubprocess(const std::string& binary_path,
                                      addr_t addr,
                                      bool whole_file);

    // ---- Subprocess angr ----
    Result decompileAngrSubprocess(const std::string& binary_path,
                                    addr_t addr,
                                    bool whole_file);

    // ---- Helpers ----
    // Dump a function's bytes to a temp ELF stub suitable for RetDec input
    std::string writeTempElfStub(addr_t addr, const u8* bytes, size_t len,
                                  const std::string& basename) const;

    // Run a subprocess and capture stdout/stderr
    struct ProcResult {
        int exit_code = -1;
        std::string out;
        std::string err;
    };
    ProcResult runSubprocess(const std::string& cmd,
                              const std::vector<std::string>& args) const;

    // Check if binary exists in PATH
    static bool binaryInPath(const std::string& name);

    // Make a temp file path
    static std::string tempFilePath(const std::string& suffix);

    // Read entire file into string
    static std::string readFile(const std::string& path);
    static bool writeFile(const std::string& path, const std::string& content);
};

} // namespace ndbg
