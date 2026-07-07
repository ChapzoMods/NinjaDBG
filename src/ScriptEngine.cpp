// NinjaDBG v1.1.4 - ScriptEngine implementation
// Open Source (Apache-2.0) - by Chapzoo
#include "ScriptEngine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>
#include <signal.h>
#include <sys/wait.h>
#include <chrono>
#include <thread>

namespace ndbg {

ScriptEngine::ScriptEngine(DebuggerCore& dbg) : dbg_(dbg) {}
ScriptEngine::~ScriptEngine() {}

bool ScriptEngine::binaryInPath(const std::string& name) {
    std::string cmd = "which " + name + " > /dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

bool ScriptEngine::isAvailable(Lang lang) const {
    switch (lang) {
        case Lang::Lua:    return binaryInPath("lua") || binaryInPath("lua5.4") ||
                                  binaryInPath("lua5.3") || binaryInPath("lua5.1");
        case Lang::Python: return binaryInPath("python3") || binaryInPath("python");
    }
    return false;
}

std::string ScriptEngine::installHint(Lang lang) const {
    switch (lang) {
        case Lang::Lua:    return "Install Lua: sudo apt-get install lua5.4";
        case Lang::Python: return "Install Python: sudo apt-get install python3";
    }
    return "";
}

std::vector<ScriptEngine::Lang> ScriptEngine::availableLanguages() const {
    std::vector<Lang> result;
    if (isAvailable(Lang::Lua))    result.push_back(Lang::Lua);
    if (isAvailable(Lang::Python)) result.push_back(Lang::Python);
    return result;
}

std::string ScriptEngine::apiDocs() {
    return R"DOCS(
NinjaDBG Scripting API (Lua + Python)
======================================

Both Lua and Python scripts have access to the `ndbg` module with these
functions. All addresses are integers (Lua 64-bit, Python int). All byte
lists are 0-255 ints.

Process control:
  ndbg.attach(pid)                    Attach to a running process
  ndbg.launch(path, [args])           Launch a new process under the debugger
  ndbg.detach()                       Detach from target
  ndbg.kill()                         Kill target
  ndbg.continue()                     Continue execution
  ndbg.step()                         Single-step one instruction
  ndbg.wait_stop()                    Wait for target to stop (returns {signal=, exited=, exit_code=})

Breakpoints:
  ndbg.breakpoint(addr)               Set a software breakpoint
  ndbg.tbreak(addr)                   Set a temporary breakpoint (auto-removed on first hit)
  ndbg.breakpoint_cond(addr, cond)    Set a conditional breakpoint (e.g. "rax == 0")
  ndbg.delete(id)                     Delete a breakpoint

Memory:
  ndbg.read_bytes(addr, n)            Read n bytes; returns a list
  ndbg.write_bytes(addr, bytes)       Write bytes (list of 0-255 ints)
  ndbg.read_u8/u16/u32/u64(addr)      Read unsigned integer
  ndbg.write_u8/u16/u32/u64(addr, v)  Write unsigned integer

Registers:
  ndbg.read_register(name)            Read register ("rax", "rip", etc.)
  ndbg.write_register(name, value)    Write register

Disassembly:
  ndbg.disassemble(addr, n)           Disassemble n instructions; returns list of strings

Other:
  ndbg.backtrace([max_frames])        Walk RBP chain; returns list of {rip=, symbol=}
  ndbg.log(msg)                       Print a log message
  ndbg.sleep(ms)                      Sleep for ms milliseconds
  ndbg.info_registers()               Returns dict of {name -> value}
  ndbg.info_breakpoints()             Returns list of {id=, addr=, hits=, cond=}

Example (Lua):
  local pid = tonumber(arg[1])
  ndbg.attach(pid)
  ndbg.breakpoint(0x401000)
  ndbg.continue()
  ndbg.wait_stop()
  local rip = ndbg.read_register("rip")
  ndbg.log("stopped at " .. string.format("0x%x", rip))
  local bytes = ndbg.read_bytes(rip, 16)
  for i, b in ipairs(bytes) do
      ndbg.log(string.format("  [%d] = 0x%02x", i, b))
  end
  ndbg.detach()

Example (Python):
  import sys
  pid = int(sys.argv[1])
  ndbg.attach(pid)
  ndbg.breakpoint(0x401000)
  ndbg.continue()
  ndbg.wait_stop()
  rip = ndbg.read_register("rip")
  ndbg.log(f"stopped at 0x{rip:x}")
  bytes_ = ndbg.read_bytes(rip, 16)
  for i, b in enumerate(bytes_):
      ndbg.log(f"  [{i}] = 0x{b:02x}")
  ndbg.detach()
)DOCS";
}

std::string ScriptEngine::luaBootstrap() const {
    return R"LUA(
-- NinjaDBG Lua bootstrap — exposes the `ndbg` module via JSON-RPC over stdin/stdout
local ndbg = {}

local function rpc(cmd, args)
    local req = '{"cmd": "' .. cmd .. '"'
    if args and #args > 0 then
        req = req .. ', "args": ['
        for i, a in ipairs(args) do
            if i > 1 then req = req .. ', ' end
            if type(a) == "string" then
                req = req .. '"' .. a:gsub('"', '\\"') .. '"'
            elseif type(a) == "number" then
                req = req .. tostring(a)
            elseif type(a) == "boolean" then
                req = req .. (a and "true" or "false")
            end
        end
        req = req .. ']'
    end
    req = req .. '}\n'
    io.write(req)
    io.flush()
    local line = io.read("*l")
    if not line then return nil end
    -- Very crude JSON parse: extract "result" or "error"
    local result = line:match('"result"%s*:%s*(.-)%s*[,}]')
    local err = line:match('"error"%s*:%s*"(.-)"')
    if err then error("ndbg." .. cmd .. " failed: " .. err) end
    return result
end

function ndbg.attach(pid)        rpc("attach", {pid}) end
function ndbg.launch(path, args) rpc("launch", {path, args or {}}) end
function ndbg.detach()           rpc("detach", {}) end
function ndbg.kill()             rpc("kill", {}) end
function ndbg.continue()         rpc("continue", {}) end
function ndbg.step()             rpc("step", {}) end
function ndbg.wait_stop()        rpc("wait_stop", {}) end
function ndbg.breakpoint(addr)   rpc("breakpoint", {addr}) end
function ndbg.tbreak(addr)       rpc("tbreak", {addr}) end
function ndbg.breakpoint_cond(addr, cond) rpc("breakpoint_cond", {addr, cond}) end
function ndbg.delete(id)         rpc("delete", {id}) end
function ndbg.read_bytes(addr, n) return rpc("read_bytes", {addr, n}) end
function ndbg.write_bytes(addr, bytes) rpc("write_bytes", {addr, bytes}) end
function ndbg.read_register(name) return rpc("read_register", {name}) end
function ndbg.write_register(name, value) rpc("write_register", {name, value}) end
function ndbg.disassemble(addr, n) return rpc("disassemble", {addr, n}) end
function ndbg.backtrace(max)     return rpc("backtrace", {max or 32}) end
function ndbg.log(msg)           rpc("log", {tostring(msg)}) end
function ndbg.sleep(ms)          rpc("sleep", {ms}) end
function ndbg.info_registers()   return rpc("info_registers", {}) end
function ndbg.info_breakpoints() return rpc("info_breakpoints", {}) end

_G.ndbg = ndbg
)LUA";
}

std::string ScriptEngine::pythonBootstrap() const {
    return R"PY(
# NinjaDBG Python bootstrap — exposes the `ndbg` module via JSON-RPC over stdin/stdout
import sys, json

class _Ndbg:
    def _rpc(self, cmd, args=None):
        req = {"cmd": cmd, "args": args or []}
        sys.stdout.write(json.dumps(req) + "\n")
        sys.stdout.flush()
        line = sys.stdin.readline()
        if not line:
            return None
        resp = json.loads(line)
        if "error" in resp:
            raise RuntimeError("ndbg." + cmd + " failed: " + resp["error"])
        return resp.get("result")

    def attach(self, pid): self._rpc("attach", [pid])
    def launch(self, path, args=None): self._rpc("launch", [path, args or []])
    def detach(self): self._rpc("detach")
    def kill(self): self._rpc("kill")
    def continue_(self): self._rpc("continue")
    def step(self): self._rpc("step")
    def wait_stop(self): return self._rpc("wait_stop")
    def breakpoint(self, addr): self._rpc("breakpoint", [addr])
    def tbreak(self, addr): self._rpc("tbreak", [addr])
    def breakpoint_cond(self, addr, cond): self._rpc("breakpoint_cond", [addr, cond])
    def delete(self, id): self._rpc("delete", [id])
    def read_bytes(self, addr, n): return self._rpc("read_bytes", [addr, n])
    def write_bytes(self, addr, bytes_): self._rpc("write_bytes", [addr, list(bytes_)])
    def read_register(self, name): return self._rpc("read_register", [name])
    def write_register(self, name, value): self._rpc("write_register", [name, value])
    def disassemble(self, addr, n):
        result = self._rpc("disassemble", [addr, n])
        if isinstance(result, str):
            try: return json.loads(result)
            except: return [result]
        return result
    def backtrace(self, max_frames=32):
        result = self._rpc("backtrace", [max_frames])
        if isinstance(result, str):
            try: return json.loads(result)
            except: return []
        return result
    def log(self, msg): self._rpc("log", [str(msg)])
    def sleep(self, ms): self._rpc("sleep", [ms])
    def info_registers(self):
        result = self._rpc("info_registers")
        if isinstance(result, str):
            try: return json.loads(result)
            except: return {}
        return result
    def info_breakpoints(self):
        result = self._rpc("info_breakpoints")
        if isinstance(result, str):
            try: return json.loads(result)
            except: return []
        return result

# Create the ndbg instance and register it as a module so that
# user scripts can do `import ndbg` OR just use `ndbg` directly.
import sys, types
_inst = _Ndbg()
_mod = types.ModuleType('ndbg')
for _attr in dir(_inst):
    if not _attr.startswith('_'):
        setattr(_mod, _attr, getattr(_inst, _attr))
sys.modules['ndbg'] = _mod
ndbg = _inst
del _inst, _mod, _attr, sys, types
)PY";
}

// ===== JSON helpers =====

std::string ScriptEngine::jsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
}

std::string ScriptEngine::jsonNumber(u64 v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
    return buf;
}

std::string ScriptEngine::jsonOk(const std::string& result_json) {
    return "{\"ok\":true,\"result\":" + result_json + "}";
}

std::string ScriptEngine::jsonError(const std::string& msg) {
    return "{\"ok\":false,\"error\":" + jsonString(msg) + "}";
}

std::string ScriptEngine::jsonOkTrue() {
    return "{\"ok\":true,\"result\":true}";
}

// ===== API implementations =====

std::string ScriptEngine::apiAttach(const std::vector<std::string>& args) {
    if (args.size() < 1) return jsonError("attach requires pid");
    pid_t pid = (pid_t)strtoll(args[0].c_str(), nullptr, 0);
    if (dbg_.attach(pid)) return jsonOkTrue();
    return jsonError(dbg_.lastError());
}

std::string ScriptEngine::apiLaunch(const std::vector<std::string>& args) {
    if (args.size() < 1) return jsonError("launch requires path");
    std::vector<std::string> rest;
    // args[1] would be the args list — simplified: pass empty
    if (dbg_.attachByLaunch(args[0], rest)) return jsonOkTrue();
    return jsonError(dbg_.lastError());
}

std::string ScriptEngine::apiDetach(const std::vector<std::string>&) {
    return dbg_.detach() ? jsonOkTrue() : jsonError(dbg_.lastError());
}

std::string ScriptEngine::apiContinue(const std::vector<std::string>&) {
    return dbg_.cont() ? jsonOkTrue() : jsonError(dbg_.lastError());
}

std::string ScriptEngine::apiStep(const std::vector<std::string>&) {
    return dbg_.step() ? jsonOkTrue() : jsonError(dbg_.lastError());
}

std::string ScriptEngine::apiBreakpoint(const std::vector<std::string>& args) {
    if (args.size() < 1) return jsonError("breakpoint requires addr");
    addr_t a = strtoull(args[0].c_str(), nullptr, 0);
    int id = dbg_.addBreakpoint(a);
    if (id < 0) return jsonError(dbg_.lastError());
    return jsonOk(jsonNumber((u64)id));
}

std::string ScriptEngine::apiReadBytes(const std::vector<std::string>& args) {
    if (args.size() < 2) return jsonError("read_bytes requires addr and length");
    addr_t a = strtoull(args[0].c_str(), nullptr, 0);
    size_t n = (size_t)strtoull(args[1].c_str(), nullptr, 0);
    auto bytes = dbg_.readMemoryVec(a, n);
    std::string result = "[";
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i > 0) result += ",";
        result += std::to_string(bytes[i]);
    }
    result += "]";
    return jsonOk(result);
}

std::string ScriptEngine::apiWriteBytes(const std::vector<std::string>& args) {
    if (args.size() < 2) return jsonError("write_bytes requires addr and bytes");
    addr_t a = strtoull(args[0].c_str(), nullptr, 0);
    std::vector<u8> bytes;
    // args[1] is a JSON array string like "[144,144,144]"
    std::string s = args[1];
    // Strip [ and ]
    size_t lb = s.find('[');
    size_t rb = s.find(']');
    if (lb == std::string::npos || rb == std::string::npos) {
        return jsonError("bytes must be a JSON array");
    }
    std::string body = s.substr(lb + 1, rb - lb - 1);
    std::istringstream ss(body);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        bytes.push_back((u8)strtoul(tok.c_str(), nullptr, 0));
    }
    if (dbg_.writeMemory(a, bytes.data(), bytes.size())) return jsonOkTrue();
    return jsonError(dbg_.lastError());
}

std::string ScriptEngine::apiReadRegister(const std::vector<std::string>& args) {
    if (args.size() < 1) return jsonError("read_register requires name");
    RegisterSet r;
    if (!dbg_.readRegisters(r)) return jsonError("readRegisters failed");
    std::string name = args[0];
    // Lowercase
    for (auto& c : name) c = tolower(c);
    u64 v = 0;
    if      (name == "rax") v = r.rax;
    else if (name == "rbx") v = r.rbx;
    else if (name == "rcx") v = r.rcx;
    else if (name == "rdx") v = r.rdx;
    else if (name == "rsi") v = r.rsi;
    else if (name == "rdi") v = r.rdi;
    else if (name == "rbp") v = r.rbp;
    else if (name == "rsp") v = r.rsp;
    else if (name == "r8")  v = r.r8;
    else if (name == "r9")  v = r.r9;
    else if (name == "r10") v = r.r10;
    else if (name == "r11") v = r.r11;
    else if (name == "r12") v = r.r12;
    else if (name == "r13") v = r.r13;
    else if (name == "r14") v = r.r14;
    else if (name == "r15") v = r.r15;
    else if (name == "rip") v = r.rip;
    else if (name == "rflags" || name == "eflags") v = r.rflags;
    else return jsonError("unknown register: " + name);
    return jsonOk(jsonNumber(v));
}

std::string ScriptEngine::apiWriteRegister(const std::vector<std::string>& args) {
    if (args.size() < 2) return jsonError("write_register requires name and value");
    RegisterSet r;
    if (!dbg_.readRegisters(r)) return jsonError("readRegisters failed");
    std::string name = args[0];
    u64 v = strtoull(args[1].c_str(), nullptr, 0);
    for (auto& c : name) c = tolower(c);
    if      (name == "rax") r.rax = v;
    else if (name == "rbx") r.rbx = v;
    else if (name == "rcx") r.rcx = v;
    else if (name == "rdx") r.rdx = v;
    else if (name == "rsi") r.rsi = v;
    else if (name == "rdi") r.rdi = v;
    else if (name == "rbp") r.rbp = v;
    else if (name == "rsp") r.rsp = v;
    else if (name == "rip") r.rip = v;
    else return jsonError("unknown or read-only register: " + name);
    if (dbg_.writeRegisters(r)) return jsonOkTrue();
    return jsonError(dbg_.lastError());
}

std::string ScriptEngine::apiDisassemble(const std::vector<std::string>& args) {
    if (args.size() < 2) return jsonError("disassemble requires addr and count");
    addr_t a = strtoull(args[0].c_str(), nullptr, 0);
    size_t n = (size_t)strtoull(args[1].c_str(), nullptr, 0);
    auto bytes = dbg_.readMemoryVec(a, n * 15 + 32);
    if (bytes.empty()) return jsonError("readMemory failed");
    auto instrs = disas_.disassemble(a, bytes.data(), bytes.size(), n);
    std::string result = "[";
    for (size_t i = 0; i < instrs.size(); i++) {
        if (i > 0) result += ",";
        result += jsonString(Disassembler::format(instrs[i]));
    }
    result += "]";
    return jsonOk(result);
}

std::string ScriptEngine::apiLog(const std::vector<std::string>& args) {
    if (!args.empty()) {
        std::cout << "[script] " << args[0] << std::endl;
    }
    return jsonOkTrue();
}

std::string ScriptEngine::apiSleep(const std::vector<std::string>& args) {
    if (args.size() < 1) return jsonError("sleep requires ms");
    int ms = atoi(args[0].c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return jsonOkTrue();
}

std::string ScriptEngine::apiWaitStop(const std::vector<std::string>&) {
    int sig; bool ex; int code;
    if (dbg_.waitForStop(sig, ex, code)) {
        std::string result = "{\"signal\":" + std::to_string(sig) +
                             ",\"exited\":" + (ex ? "true" : "false") +
                             ",\"exit_code\":" + std::to_string(code) + "}";
        return jsonOk(result);
    }
    return jsonError("wait_stop failed");
}

std::string ScriptEngine::apiBacktrace(const std::vector<std::string>& args) {
    size_t max = args.size() > 0 ? (size_t)strtoull(args[0].c_str(), nullptr, 0) : 32;
    auto frames = dbg_.backtrace(max);
    std::string result = "[";
    for (size_t i = 0; i < frames.size(); i++) {
        if (i > 0) result += ",";
        result += "{\"rip\":" + jsonNumber(frames[i].rip) +
                  ",\"symbol\":" + jsonString(frames[i].symbol) + "}";
    }
    result += "]";
    return jsonOk(result);
}

std::string ScriptEngine::apiInfo(const std::vector<std::string>& args) {
    if (args.empty()) return jsonError("info requires subcommand");
    if (args[0] == "registers") {
        RegisterSet r;
        if (!dbg_.readRegisters(r)) return jsonError("readRegisters failed");
        u64* vals[] = {&r.rax,&r.rbx,&r.rcx,&r.rdx,&r.rsi,&r.rdi,&r.rbp,&r.rsp,
                       &r.r8,&r.r9,&r.r10,&r.r11,&r.r12,&r.r13,&r.r14,&r.r15,
                       &r.rip,&r.rflags};
        std::string result = "{";
        for (int i = 0; i < 18; i++) {
            if (i > 0) result += ",";
            // Lowercase the name
            std::string n = kRegNames[i];
            for (auto& c : n) c = tolower(c);
            // Trim trailing space
            while (!n.empty() && n.back() == ' ') n.pop_back();
            result += jsonString(n) + ":" + jsonNumber(*vals[i]);
        }
        result += "}";
        return jsonOk(result);
    }
    if (args[0] == "breakpoints") {
        auto bps = dbg_.breakpoints();
        std::string result = "[";
        for (size_t i = 0; i < bps.size(); i++) {
            if (i > 0) result += ",";
            result += "{\"id\":" + std::to_string(bps[i].id) +
                      ",\"addr\":" + jsonNumber(bps[i].address) +
                      ",\"hits\":" + std::to_string(bps[i].hit_count) +
                      ",\"cond\":" + jsonString(bps[i].condition) + "}";
        }
        result += "]";
        return jsonOk(result);
    }
    return jsonError("unknown info subcommand: " + args[0]);
}

// ===== Request dispatch =====

std::string ScriptEngine::handleRequest(const std::string& json_req) {
    // Crude JSON parse: extract "cmd" and "args"
    std::string cmd;
    auto cmd_pos = json_req.find("\"cmd\"");
    if (cmd_pos == std::string::npos) return jsonError("missing cmd");
    auto colon = json_req.find(':', cmd_pos);
    auto quote1 = json_req.find('"', colon + 1);
    auto quote2 = json_req.find('"', quote1 + 1);
    if (quote1 == std::string::npos || quote2 == std::string::npos) return jsonError("bad cmd");
    cmd = json_req.substr(quote1 + 1, quote2 - quote1 - 1);

    // Parse args array (very crude)
    std::vector<std::string> args;
    auto args_pos = json_req.find("\"args\"");
    if (args_pos != std::string::npos) {
        auto lb = json_req.find('[', args_pos);
        auto rb = json_req.find(']', lb);
        if (lb != std::string::npos && rb != std::string::npos) {
            std::string body = json_req.substr(lb + 1, rb - lb - 1);
            // Tokenize: split on commas, but respect quotes
            size_t i = 0;
            while (i < body.size()) {
                while (i < body.size() && (body[i] == ' ' || body[i] == ',')) i++;
                if (i >= body.size()) break;
                if (body[i] == '"') {
                    // String
                    size_t end = body.find('"', i + 1);
                    if (end == std::string::npos) break;
                    args.push_back(body.substr(i + 1, end - i - 1));
                    i = end + 1;
                } else {
                    // Number or other — read until comma
                    size_t end = body.find(',', i);
                    if (end == std::string::npos) end = body.size();
                    std::string tok = body.substr(i, end - i);
                    // Trim
                    while (!tok.empty() && isspace(tok.back())) tok.pop_back();
                    while (!tok.empty() && isspace(tok.front())) tok.erase(0, 1);
                    args.push_back(tok);
                    i = end;
                }
            }
        }
    }

    if (cmd == "attach")         return apiAttach(args);
    if (cmd == "launch")         return apiLaunch(args);
    if (cmd == "detach")         return apiDetach(args);
    if (cmd == "continue")       return apiContinue(args);
    if (cmd == "step")           return apiStep(args);
    if (cmd == "breakpoint")     return apiBreakpoint(args);
    if (cmd == "delete") {
        if (args.empty()) return jsonError("delete requires id");
        int id = atoi(args[0].c_str());
        return dbg_.removeBreakpoint(id) ? jsonOkTrue() : jsonError(dbg_.lastError());
    }
    if (cmd == "kill") {
        return dbg_.kill() ? jsonOkTrue() : jsonError(dbg_.lastError());
    }
    if (cmd == "tbreak") {
        if (args.empty()) return jsonError("tbreak requires addr");
        addr_t a = strtoull(args[0].c_str(), nullptr, 0);
        int id = dbg_.addTempBreakpoint(a);
        if (id < 0) return jsonError(dbg_.lastError());
        return jsonOk(jsonNumber((u64)id));
    }
    if (cmd == "breakpoint_cond") {
        if (args.size() < 2) return jsonError("breakpoint_cond requires addr and condition");
        addr_t a = strtoull(args[0].c_str(), nullptr, 0);
        int id = dbg_.addConditionalBreakpoint(a, args[1]);
        if (id < 0) return jsonError(dbg_.lastError());
        return jsonOk(jsonNumber((u64)id));
    }
    if (cmd == "read_bytes")     return apiReadBytes(args);
    if (cmd == "write_bytes")    return apiWriteBytes(args);
    if (cmd == "read_register")  return apiReadRegister(args);
    if (cmd == "write_register") return apiWriteRegister(args);
    if (cmd == "disassemble")    return apiDisassemble(args);
    if (cmd == "log")            return apiLog(args);
    if (cmd == "sleep")          return apiSleep(args);
    if (cmd == "wait_stop")      return apiWaitStop(args);
    if (cmd == "backtrace")      return apiBacktrace(args);
    if (cmd == "info")           return apiInfo(args);
    // Convenience: scripts send info_registers / info_breakpoints directly
    if (cmd == "info_registers") {
        std::vector<std::string> a; a.push_back("registers");
        return apiInfo(a);
    }
    if (cmd == "info_breakpoints") {
        std::vector<std::string> a; a.push_back("breakpoints");
        return apiInfo(a);
    }
    return jsonError("unknown command: " + cmd);
}

// ===== Subprocess execution =====

bool ScriptEngine::executeViaJsonRpc(const std::string& interpreter,
                                       const std::string& bootstrap,
                                       const std::string& user_code,
                                       std::string& err) {
    // Create two pipes: parent->child (stdin) and child->parent (stdout)
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) {
        err = "pipe failed";
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        err = "fork failed";
        return false;
    }
    if (pid == 0) {
        // Child
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);
        // Build the script: bootstrap + user code
        std::string full = bootstrap + "\n" + user_code;
        // Pass via -e (Lua) or -c (Python)
        std::vector<const char*> argv;
        if (interpreter.find("python") != std::string::npos) {
            argv.push_back(interpreter.c_str());
            argv.push_back("-c");
            argv.push_back(full.c_str());
        } else {
            argv.push_back(interpreter.c_str());
            argv.push_back("-e");
            argv.push_back(full.c_str());
        }
        argv.push_back(nullptr);
        execvp(interpreter.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    // Parent
    close(in_pipe[0]);
    close(out_pipe[1]);

    // Write loop: respond to RPC requests
    FILE* in = fdopen(in_pipe[1], "w");
    FILE* out = fdopen(out_pipe[0], "r");
    if (!in || !out) { err = "fdopen failed"; return false; }

    char line[8192];
    while (fgets(line, sizeof(line), out)) {
        // Strip newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (len == 0) continue;

        std::string req(line);
        std::string resp = handleRequest(req);
        fprintf(in, "%s\n", resp.c_str());
        fflush(in);
    }

    fclose(in);
    fclose(out);
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        err = "script exited with code " + std::to_string(WEXITSTATUS(status));
        return false;
    }
    return true;
}

bool ScriptEngine::runFile(Lang lang, const std::string& path, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "Cannot open script: " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    return runString(lang, ss.str(), err);
}

bool ScriptEngine::runString(Lang lang, const std::string& code, std::string& err) {
    if (!isAvailable(lang)) {
        err = installHint(lang);
        return false;
    }
    std::string interpreter;
    std::string bootstrap;
    if (lang == Lang::Lua) {
        if (binaryInPath("lua5.4"))      interpreter = "lua5.4";
        else if (binaryInPath("lua5.3")) interpreter = "lua5.3";
        else if (binaryInPath("lua5.1")) interpreter = "lua5.1";
        else                              interpreter = "lua";
        bootstrap = luaBootstrap();
    } else {
        if (binaryInPath("python3")) interpreter = "python3";
        else                          interpreter = "python";
        bootstrap = pythonBootstrap();
    }
    return executeViaJsonRpc(interpreter, bootstrap, code, err);
}

} // namespace ndbg
