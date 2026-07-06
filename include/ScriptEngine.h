// NinjaDBG v1.1.3 - Script Engine (Lua + Python)
// Open Source (Apache-2.0) - by Chapzoo
//
// Allows users to script NinjaDBG via either Lua or Python. The engine
// exposes a NinjaDBG API to the script with functions like:
//   ndbg.attach(pid)
//   ndbg.launch(path)
//   ndbg.detach()
//   ndbg.continue()
//   ndbg.step()
//   ndbg.breakpoint(addr)
//   ndbg.read_bytes(addr, n)  -> table/list of bytes
//   ndbg.write_bytes(addr, bytes)
//   ndbg.read_register(name)  -> integer
//   ndbg.write_register(name, value)
//   ndbg.disassemble(addr, n) -> list of strings
//   ndbg.log(msg)
//   ndbg.sleep(ms)
//
// Lua execution: we attempt to link against lua5.4 / lua5.3 / lua5.1.
// If unavailable, we fall back to running `lua` as a subprocess that
// communicates via stdin/stdout JSON lines.
//
// Python execution: we run `python3` as a subprocess with a small
// bootstrap that exposes the same API via JSON-RPC over stdin/stdout.
//
// Both backends use the same wire protocol so the script doesn't care
// which language it is in. The engine runs scripts line-by-line:
//   - send a JSON request: {"cmd": "attach", "args": [1234]}
//   - receive a JSON response: {"ok": true, "result": ...}
#pragma once

#include "Types.h"
#include "DebuggerCore.h"
#include "Disassembler.h"
#include <string>
#include <vector>
#include <memory>

namespace ndbg {

class ScriptEngine {
public:
    enum class Lang { Lua, Python };

    ScriptEngine(DebuggerCore& dbg);
    ~ScriptEngine();

    // Run a script file. Returns true on success.
    bool runFile(Lang lang, const std::string& path, std::string& err);

    // Run a script string. Returns true on success.
    bool runString(Lang lang, const std::string& code, std::string& err);

    // Check if a language backend is available
    bool isAvailable(Lang lang) const;

    // Get install hint for a missing language
    std::string installHint(Lang lang) const;

    // Get the list of supported languages (those available)
    std::vector<Lang> availableLanguages() const;

    // Print the API documentation
    static std::string apiDocs();

private:
    DebuggerCore& dbg_;
    Disassembler disas_;

    // Generate the Lua bootstrap that exposes the NinjaDBG API
    std::string luaBootstrap() const;

    // Generate the Python bootstrap that exposes the NinjaDBG API
    std::string pythonBootstrap() const;

    // Execute a script in a subprocess, communicating via JSON-RPC
    bool executeViaJsonRpc(const std::string& interpreter,
                            const std::string& bootstrap,
                            const std::string& user_code,
                            std::string& err);

    // Handle a single JSON-RPC request from the script.
    // Returns the JSON response string.
    std::string handleRequest(const std::string& json_req);

    // API implementations (called by handleRequest)
    std::string apiAttach(const std::vector<std::string>& args);
    std::string apiLaunch(const std::vector<std::string>& args);
    std::string apiDetach(const std::vector<std::string>& args);
    std::string apiContinue(const std::vector<std::string>& args);
    std::string apiStep(const std::vector<std::string>& args);
    std::string apiBreakpoint(const std::vector<std::string>& args);
    std::string apiReadBytes(const std::vector<std::string>& args);
    std::string apiWriteBytes(const std::vector<std::string>& args);
    std::string apiReadRegister(const std::vector<std::string>& args);
    std::string apiWriteRegister(const std::vector<std::string>& args);
    std::string apiDisassemble(const std::vector<std::string>& args);
    std::string apiLog(const std::vector<std::string>& args);
    std::string apiSleep(const std::vector<std::string>& args);
    std::string apiWaitStop(const std::vector<std::string>& args);
    std::string apiBacktrace(const std::vector<std::string>& args);
    std::string apiInfo(const std::vector<std::string>& args);

    // Tiny JSON helpers (we don't link a JSON library)
    static std::string jsonString(const std::string& s);
    static std::string jsonNumber(u64 v);
    static std::string jsonOk(const std::string& result_json);
    static std::string jsonError(const std::string& msg);
    static std::string jsonOkTrue();

    // Check if a binary exists in PATH
    static bool binaryInPath(const std::string& name);
};

} // namespace ndbg
