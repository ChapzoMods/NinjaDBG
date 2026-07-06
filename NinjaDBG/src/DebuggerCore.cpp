// NinjaDBG v1.0.2 - DebuggerCore implementation
// Open Source (Apache-2.0) - by Chapzoo
#include "DebuggerCore.h"
#include "AntiDetect.h"
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/reg.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>
#include <pwd.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace ndbg {

DebuggerCore::DebuggerCore() {}
DebuggerCore::~DebuggerCore() {
    if (state_ == RunState::Attached || state_ == RunState::Stopped) {
        detach();
    }
}

bool DebuggerCore::attach(pid_t pid) {
    if (pid_ != 0) detach();
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
        last_error_ = "PTRACE_ATTACH failed: ";
        last_error_ += std::strerror(errno);
        return false;
    }
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        last_error_ = "waitpid failed";
        // Detach to avoid leaking the tracee
        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        return false;
    }
    pid_ = pid;
    state_ = RunState::Stopped;
    return true;
}

bool DebuggerCore::attachByLaunch(const std::string& path, const std::vector<std::string>& args) {
    if (pid_ != 0) detach();

    pid_t child = fork();
    if (child == 0) {
        // child
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        // stop self so parent can configure options before exec
        raise(SIGSTOP);

        // build argv
        std::vector<const char*> argv;
        argv.push_back(path.c_str());
        for (auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);

        execv(path.c_str(), const_cast<char* const*>(argv.data()));
        perror("execv");
        _exit(127);
    }
    if (child == -1) {
        last_error_ = "fork failed";
        return false;
    }
    int status;
    waitpid(child, &status, 0);  // wait for SIGSTOP

    pid_ = child;
    state_ = RunState::Stopped;

    // Set ptrace options: TRACECLONE/FORK/EXEC to follow children,
    // and allow killing the child if our parent dies.
    long opts = PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK |
                PTRACE_O_TRACEEXEC  | PTRACE_O_TRACEEXIT |
                PTRACE_O_EXITKILL;
    ptrace(PTRACE_SETOPTIONS, pid_, nullptr, (void*)opts);
    return true;
}

bool DebuggerCore::detach() {
    if (pid_ == 0) return false;
    uninstallAllBps();
    ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
    pid_ = 0;
    state_ = RunState::Idle;
    return true;
}

bool DebuggerCore::kill() {
    if (pid_ == 0) return false;
    // PTRACE_KILL is deprecated and a no-op on Linux >= 3.x.
    // Use SIGKILL via kill(2) instead, then reap the zombie.
    uninstallAllBps();
    ::kill(pid_, SIGKILL);
    int status;
    // Use WNOHANG loop to avoid blocking forever if the child is stuck
    for (int i = 0; i < 50; i++) {
        pid_t r = waitpid(pid_, &status, WNOHANG);
        if (r == pid_ || r == -1) break;
        usleep(100000);  // 100ms
    }
    pid_ = 0;
    state_ = RunState::Idle;
    return true;
}

bool DebuggerCore::step() {
    if (pid_ == 0) return false;
    if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) == -1) {
        last_error_ = "SINGLESTEP failed";
        return false;
    }
    int status; waitpid(pid_, &status, 0);
    state_ = RunState::Stopped;
    return true;
}

bool DebuggerCore::cont() {
    if (pid_ == 0) return false;
    installAllBps();
    if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) == -1) {
        last_error_ = "PTRACE_CONT failed";
        return false;
    }
    state_ = RunState::Running;
    return true;
}

bool DebuggerCore::contUntil(addr_t addr) {
    // Set a temporary breakpoint at addr
    int id = addBreakpoint(addr, false, "_temp_contUntil");
    if (id < 0) return false;
    cont();
    int sig; bool ex; int code;
    waitForStop(sig, ex, code);
    removeBreakpoint(id);
    return true;
}

bool DebuggerCore::stop() {
    if (pid_ == 0) return false;
    ::kill(pid_, SIGSTOP);
    return true;
}

bool DebuggerCore::waitForStop(int& signal_out, bool& exited_out, int& exit_code_out) {
    exited_out = false;
    signal_out = 0;
    if (pid_ == 0) return false;
    int status;
    if (waitpid(pid_, &status, 0) == -1) return false;

    if (WIFEXITED(status)) {
        exited_out = true;
        exit_code_out = WEXITSTATUS(status);
        state_ = RunState::Exited;
        return true;
    }
    if (WIFSIGNALED(status)) {
        exited_out = true;
        exit_code_out = -WTERMSIG(status);
        state_ = RunState::Exited;
        return true;
    }
    if (WIFSTOPPED(status)) {
        signal_out = WSTOPSIG(status);
        // If we stopped at a SIGTRAP, check if it's our breakpoint
        if (signal_out == SIGTRAP) {
            RegisterSet r;
            if (readRegisters(r)) {
                addr_t bp_addr = r.rip - 1;
                Breakpoint* bp = findBreakpoint(bp_addr);
                if (bp) {
                    // Rewind RIP, increment hit count
                    r.rip = bp_addr;
                    writeRegisters(r);
                    bp->hit_count++;
                    uninstallBp(*bp);
                }
            }
        }
        state_ = RunState::Stopped;
        return true;
    }
    return false;
}

// --- Breakpoints ---
int DebuggerCore::addBreakpoint(addr_t addr, bool hardware, const std::string& label) {
    Breakpoint bp;
    bp.id = next_bp_id_++;
    bp.address = addr;
    bp.hardware = hardware;
    bp.label = label.empty() ? ("bp_" + std::to_string(bp.id)) : label;
    if (!hardware) {
        // Read the original byte, save it, write 0xCC
        u8 saved = 0;
        if (!setSwBreakpoint(addr, &saved)) return -1;
        bp.original_byte = saved;
    } else {
        // TODO: program DR0-DR3 (would need PTRACE_POKEUSER to debug registers).
        // For demonstration we mark it but use a software bp internally.
        u8 saved = 0;
        if (!setSwBreakpoint(addr, &saved)) return -1;
        bp.original_byte = saved;
        bp.hw_slot = 0; // pretend slot 0
    }
    bps_[bp.id] = bp;
    return bp.id;
}

bool DebuggerCore::removeBreakpoint(int id) {
    auto it = bps_.find(id);
    if (it == bps_.end()) return false;
    if (state_ == RunState::Stopped) {
        uninstallBp(it->second);
    }
    bps_.erase(it);
    return true;
}

bool DebuggerCore::enableBreakpoint(int id) {
    auto it = bps_.find(id);
    if (it == bps_.end()) return false;
    it->second.enabled = true;
    if (state_ == RunState::Stopped) installBp(it->second);
    return true;
}

bool DebuggerCore::disableBreakpoint(int id) {
    auto it = bps_.find(id);
    if (it == bps_.end()) return false;
    it->second.enabled = false;
    if (state_ == RunState::Stopped) uninstallBp(it->second);
    return true;
}

std::vector<Breakpoint> DebuggerCore::breakpoints() const {
    std::vector<Breakpoint> v;
    v.reserve(bps_.size());
    for (auto& kv : bps_) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.id < b.id; });
    return v;
}

Breakpoint* DebuggerCore::findBreakpoint(addr_t addr) {
    for (auto& kv : bps_) {
        if (kv.second.address == addr && kv.second.enabled) return &kv.second;
    }
    return nullptr;
}

int DebuggerCore::findBreakpointId(addr_t addr) {
    auto* bp = findBreakpoint(addr);
    return bp ? bp->id : -1;
}

// --- Memory access ---
bool DebuggerCore::readMemory(addr_t addr, void* buf, size_t n) {
    // Try process_vm_readv first (stealth-friendly: doesn't generate ptrace events).
    iovec local  { buf, n };
    iovec remote { (void*)addr, n };
    ssize_t r = process_vm_readv(pid_, &local, 1, &remote, 1, 0);
    if (r == (ssize_t)n) return true;

    // Fallback to PTRACE_PEEKDATA word-by-word
    size_t done = 0;
    while (done < n) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid_, (void*)(addr + done), nullptr);
        if (errno != 0) {
            last_error_ = "readMemory peek failed at " + hex(addr + done);
            return false;
        }
        size_t chunk = std::min((size_t)sizeof(long), n - done);
        memcpy((u8*)buf + done, &word, chunk);
        done += chunk;
    }
    return true;
}

bool DebuggerCore::writeMemory(addr_t addr, const void* buf, size_t n) {
    iovec local  { (void*)buf, n };
    iovec remote { (void*)addr, n };
    ssize_t r = process_vm_writev(pid_, &local, 1, &remote, 1, 0);
    if (r == (ssize_t)n) return true;

    // Fallback to PTRACE_POKEDATA word-by-word.
    // IMPORTANT: POKEDATA writes a full 8-byte word. For partial writes
    // (when n is not a multiple of 8, or the last chunk is < 8 bytes),
    // we must PEEKDATA the existing word first and overlay only the
    // intended bytes — otherwise we zero-extend and corrupt adjacent
    // memory in the target.
    size_t done = 0;
    while (done < n) {
        size_t chunk = std::min((size_t)sizeof(long), n - done);
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid_, (void*)(addr + done), nullptr);
        if (errno != 0) {
            last_error_ = "writeMemory peek failed at " + hex(addr + done);
            return false;
        }
        memcpy(&word, (const u8*)buf + done, chunk);  // overlay only chunk bytes
        if (ptrace(PTRACE_POKEDATA, pid_, (void*)(addr + done), (void*)word) == -1) {
            last_error_ = "writeMemory poke failed";
            return false;
        }
        done += chunk;
    }
    return true;
}

std::vector<u8> DebuggerCore::readMemoryVec(addr_t addr, size_t n) {
    std::vector<u8> v(n);
    if (!readMemory(addr, v.data(), n)) v.clear();
    return v;
}

// --- Registers ---
bool DebuggerCore::readRegisters(RegisterSet& out) {
    user_regs_struct r;
    if (ptrace(PTRACE_GETREGS, pid_, nullptr, &r) == -1) {
        last_error_ = "GETREGS failed";
        return false;
    }
    out.rax = r.rax;  out.rbx = r.rbx;  out.rcx = r.rcx;  out.rdx = r.rdx;
    out.rsi = r.rsi;  out.rdi = r.rdi;  out.rbp = r.rbp;  out.rsp = r.rsp;
    out.r8  = r.r8;   out.r9  = r.r9;   out.r10 = r.r10;  out.r11 = r.r11;
    out.r12 = r.r12;  out.r13 = r.r13;  out.r14 = r.r14;  out.r15 = r.r15;
    out.rip = r.rip;  out.rflags = r.eflags;
    out.cs = r.cs;    out.ss = r.ss;    out.ds = r.ds;    out.es = r.es;
    out.fs = r.fs;    out.gs = r.gs;    out.orig_rax = r.orig_rax;
    return true;
}

bool DebuggerCore::writeRegisters(const RegisterSet& in) {
    user_regs_struct r;
    if (ptrace(PTRACE_GETREGS, pid_, nullptr, &r) == -1) return false;
    r.rax=in.rax; r.rbx=in.rbx; r.rcx=in.rcx; r.rdx=in.rdx;
    r.rsi=in.rsi; r.rdi=in.rdi; r.rbp=in.rbp; r.rsp=in.rsp;
    r.r8=in.r8; r.r9=in.r9; r.r10=in.r10; r.r11=in.r11;
    r.r12=in.r12; r.r13=in.r13; r.r14=in.r14; r.r15=in.r15;
    r.rip=in.rip; r.eflags=in.rflags;
    r.cs=in.cs; r.ss=in.ss; r.ds=in.ds; r.es=in.es;
    r.fs=in.fs; r.gs=in.gs; r.orig_rax=in.orig_rax;
    return ptrace(PTRACE_SETREGS, pid_, nullptr, &r) != -1;
}

// --- Maps / threads ---
std::vector<MemoryRegion> DebuggerCore::readMaps() {
    std::vector<MemoryRegion> out;
    std::ifstream f("/proc/" + std::to_string(pid_) + "/maps");
    std::string line;
    while (std::getline(f, line)) {
        MemoryRegion r{};
        char perms[8] = {0};
        char path[512] = {0};
        unsigned long start, end, offset;
        int n = sscanf(line.c_str(), "%lx-%lx %7s %lx %*x:%*x %*d %511s",
                       &start, &end, perms, &offset, path);
        r.start = start; r.end = end; r.offset = offset;
        r.read  = strchr(perms, 'r') != nullptr;
        r.write = strchr(perms, 'w') != nullptr;
        r.exec  = strchr(perms, 'x') != nullptr;
        r.shared= strchr(perms, 's') != nullptr;
        if (n >= 5) r.path = path;
        out.push_back(r);
    }
    maps_cache_ = out;  // v1.1.0: cache for backtrace symbol resolution
    return out;
}

std::vector<ThreadInfo> DebuggerCore::readThreads() {
    std::vector<ThreadInfo> out;
    DIR* d = opendir(("/proc/" + std::to_string(pid_) + "/task").c_str());
    if (!d) return out;
    struct dirent* de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        ThreadInfo t{};
        t.tid = atoi(de->d_name);
        std::ifstream f("/proc/" + std::to_string(pid_) + "/task/" +
                        std::to_string(t.tid) + "/stat");
        std::string stat_line;
        std::getline(f, stat_line);
        // stat format: pid (comm) state ...
        size_t rp = stat_line.rfind(')');
        if (rp != std::string::npos && rp + 2 < stat_line.size()) {
            t.state = stat_line.substr(rp + 2, 1);
        }
        // Read comm
        std::ifstream cf("/proc/" + std::to_string(pid_) + "/task/" +
                         std::to_string(t.tid) + "/comm");
        std::getline(cf, t.name);
        out.push_back(t);
    }
    closedir(d);
    return out;
}

addr_t DebuggerCore::findSymbol(const std::string& name) {
    // Try /proc/<pid>/maps + read ELF .symtab — minimal: just look for libc func
    auto maps = readMaps();
    for (auto& m : maps) {
        if (m.path.find("libc") != std::string::npos && m.exec) {
            // We'd need to parse the ELF; return the start of this region.
            return m.start;
        }
    }
    return 0;
}

addr_t DebuggerCore::findFunctionStart(addr_t addr) {
    // Walk backwards looking for typical function prologue (push rbp / sub rsp etc.)
    // This is a heuristic. Scan up to 4KB back, but never underflow.
    addr_t limit = (addr >= 0x1000) ? (addr - 0x1000) : 0;
    for (addr_t a = addr; a > limit; a--) {
        u8 b = peekByte(a);
        // 0x55 = push rbp (very common prologue start)
        if (b == 0x55) {
            u8 next = peekByte(a + 1);
            if (next == 0x48) { // REX.W
                u8 next2 = peekByte(a + 2);
                if (next2 == 0x89 || next2 == 0x8B) return a;
            }
        }
    }
    return addr;
}

// --- Static helpers ---
std::vector<ProcessInfo> DebuggerCore::listProcesses() {
    std::vector<ProcessInfo> out;
    DIR* d = opendir("/proc");
    if (!d) return out;
    struct dirent* de;
    while ((de = readdir(d))) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
        pid_t pid = atoi(de->d_name);
        ProcessInfo p{};
        p.pid = pid;
        std::ifstream cf(std::string("/proc/") + de->d_name + "/comm");
        std::getline(cf, p.name);

        std::ifstream sf(std::string("/proc/") + de->d_name + "/stat");
        std::string stat_line;
        std::getline(sf, stat_line);
        size_t rp = stat_line.rfind(')');
        if (rp != std::string::npos && rp + 2 < stat_line.size()) {
            p.state = stat_line.substr(rp + 2, 1);
        }

        // cmdline
        std::ifstream cl(std::string("/proc/") + de->d_name + "/cmdline");
        std::string s;
        std::getline(cl, s, '\0');
        p.cmdline = s;

        // memory
        std::ifstream sm(std::string("/proc/") + de->d_name + "/statm");
        long rss_pages = 0;
        sm.ignore(100, ' ');
        sm >> rss_pages;
        p.memory_kb = rss_pages * 4;

        out.push_back(p);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](auto& a, auto& b){ return a.pid < b.pid; });
    return out;
}

// --- Private ---
bool DebuggerCore::setSwBreakpoint(addr_t addr, u8* saved) {
    u8 orig = peekByte(addr);
    if (saved) *saved = orig;
    return pokeByte(addr, 0xCC);
}

bool DebuggerCore::clearSwBreakpoint(addr_t addr, u8 saved) {
    return pokeByte(addr, saved);
}

bool DebuggerCore::installAllBps() {
    for (auto& kv : bps_) {
        if (kv.second.enabled) installBp(kv.second);
    }
    return true;
}

bool DebuggerCore::uninstallAllBps() {
    for (auto& kv : bps_) {
        if (kv.second.enabled) uninstallBp(kv.second);
    }
    return true;
}

bool DebuggerCore::installBp(Breakpoint& bp) {
    if (!bp.enabled) return false;
    return pokeByte(bp.address, 0xCC);
}

bool DebuggerCore::uninstallBp(Breakpoint& bp) {
    return pokeByte(bp.address, bp.original_byte);
}

u8 DebuggerCore::peekByte(addr_t addr) {
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, pid_, (void*)addr, nullptr);
    if (errno != 0) return 0;
    return (u8)(word & 0xFF);
}

bool DebuggerCore::pokeByte(addr_t addr, u8 v) {
    // Read the aligned word, modify the lowest byte, write back
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, pid_, (void*)addr, nullptr);
    if (errno != 0) return false;
    word = (word & ~0xFFL) | (long)v;
    return ptrace(PTRACE_POKEDATA, pid_, (void*)addr, (void*)word) != -1;
}

// ===== v1.1.0 advanced features =====

int DebuggerCore::addConditionalBreakpoint(addr_t addr, const std::string& condition,
                                            const std::string& label) {
    int id = addBreakpoint(addr, false, label);
    if (id < 0) return -1;
    auto it = bps_.find(id);
    if (it != bps_.end()) it->second.condition = condition;
    return id;
}

bool DebuggerCore::setBreakpointCondition(int id, const std::string& condition) {
    auto it = bps_.find(id);
    if (it == bps_.end()) return false;
    it->second.condition = condition;
    return true;
}

bool DebuggerCore::checkBreakpointCondition(int id) {
    auto it = bps_.find(id);
    if (it == bps_.end()) return true;
    if (it->second.condition.empty()) return true;

    RegisterSet r;
    if (!readRegisters(r)) return true; // fire on error

    // Very simple condition evaluator: <reg> <op> <value>
    // Supported regs: rax rbx rcx rdx rsi rdi rbp rsp rip
    //                 r8 r9 r10 r11 r12 r13 r14 r15
    // Supported ops: == != > < >= <=
    // Values: hex (0x...) or decimal
    std::istringstream ss(it->second.condition);
    std::string regname, op, valstr;
    ss >> regname >> op >> valstr;
    if (regname.empty() || op.empty() || valstr.empty()) return true;

    auto lookup = [&](const std::string& n, u64& out) -> bool {
        if (n == "rax" || n == "RAX") { out = r.rax; return true; }
        if (n == "rbx" || n == "RBX") { out = r.rbx; return true; }
        if (n == "rcx" || n == "RCX") { out = r.rcx; return true; }
        if (n == "rdx" || n == "RDX") { out = r.rdx; return true; }
        if (n == "rsi" || n == "RSI") { out = r.rsi; return true; }
        if (n == "rdi" || n == "RDI") { out = r.rdi; return true; }
        if (n == "rbp" || n == "RBP") { out = r.rbp; return true; }
        if (n == "rsp" || n == "RSP") { out = r.rsp; return true; }
        if (n == "rip" || n == "RIP") { out = r.rip; return true; }
        if (n == "r8"  || n == "R8")  { out = r.r8;  return true; }
        if (n == "r9"  || n == "R9")  { out = r.r9;  return true; }
        if (n == "r10" || n == "R10") { out = r.r10; return true; }
        if (n == "r11" || n == "R11") { out = r.r11; return true; }
        if (n == "r12" || n == "R12") { out = r.r12; return true; }
        if (n == "r13" || n == "R13") { out = r.r13; return true; }
        if (n == "r14" || n == "R14") { out = r.r14; return true; }
        if (n == "r15" || n == "R15") { out = r.r15; return true; }
        return false;
    };

    u64 regval;
    if (!lookup(regname, regval)) return true;
    u64 cmpval = strtoull(valstr.c_str(), nullptr, 0);

    bool result = true;
    if (op == "==") result = (regval == cmpval);
    else if (op == "!=") result = (regval != cmpval);
    else if (op == ">")  result = (regval >  cmpval);
    else if (op == "<")  result = (regval <  cmpval);
    else if (op == ">=") result = (regval >= cmpval);
    else if (op == "<=") result = (regval <= cmpval);
    return result;
}

int DebuggerCore::addTempBreakpoint(addr_t addr, const std::string& label) {
    int id = addBreakpoint(addr, false, label);
    if (id < 0) return -1;
    auto it = bps_.find(id);
    if (it != bps_.end()) it->second.temporary = true;
    return id;
}

int DebuggerCore::addWatchpoint(addr_t addr, size_t len, WatchType type, const std::string& label) {
    // Hardware watchpoint via DR0-DR3 + DR7
    // For demo: we store the watchpoint in the bps_ map but mark it as a watchpoint.
    // Real DR programming requires PTRACE_POKEUSER to debug registers.
    Breakpoint bp;
    bp.id = next_bp_id_++;
    bp.address = addr;
    bp.label = label.empty() ? ("wp_" + std::to_string(bp.id)) : label;
    bp.is_watchpoint = true;
    bp.watch_len = (int)std::min((size_t)8, len);
    bp.watch_type = (type == WatchType::Write) ? 0 :
                    (type == WatchType::ReadWrite) ? 1 : 2;
    bp.enabled = true;
    // No INT3 patch — hardware watchpoints don't modify memory
    bps_[bp.id] = bp;
    return bp.id;
}

bool DebuggerCore::removeWatchpoint(int id) {
    return removeBreakpoint(id);
}

std::vector<Breakpoint> DebuggerCore::watchpoints() const {
    std::vector<Breakpoint> v;
    for (auto& kv : bps_) {
        if (kv.second.is_watchpoint) v.push_back(kv.second);
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.id < b.id; });
    return v;
}

bool DebuggerCore::stepOut() {
    // Read return address from top of stack
    RegisterSet r;
    if (!readRegisters(r)) return false;
    u64 ret_addr = 0;
    if (!readMemory(r.rsp, &ret_addr, 8)) return false;
    int id = addTempBreakpoint(ret_addr, "_step_out_ret");
    if (id < 0) return false;
    bool ok = cont();
    int sig; bool ex; int code;
    if (waitForStop(sig, ex, code)) {
        removeBreakpoint(id);
    }
    return ok;
}

bool DebuggerCore::stepOver() {
    RegisterSet r;
    if (!readRegisters(r)) return false;
    // Read instruction at RIP — if it's a CALL, set temp bp after it
    u8 buf[16];
    if (!readMemory(r.rip, buf, 16)) return step();

    // Detect CALL rel32 (0xE8) or CALL r/m64 (0xFF /2)
    size_t call_len = 0;
    if (buf[0] == 0xE8) call_len = 5;
    else if (buf[0] == 0xFF && (buf[1] & 0x38) == 0x10) call_len = 2;
    else if (buf[0] == 0x48 && buf[1] == 0xFF && (buf[2] & 0x38) == 0x10) call_len = 3;

    if (call_len == 0) {
        // Not a CALL — just single-step
        return step();
    }
    int id = addTempBreakpoint(r.rip + call_len, "_step_over");
    if (id < 0) return step();
    bool ok = cont();
    int sig; bool ex; int code;
    if (waitForStop(sig, ex, code)) {
        removeBreakpoint(id);
    }
    return ok;
}

bool DebuggerCore::stepToSyscall(int& syscall_nr, bool& is_entry) {
    if (pid_ == 0) return false;
    if (ptrace(PTRACE_SYSCALL, pid_, nullptr, nullptr) == -1) return false;
    int status;
    if (waitpid(pid_, &status, 0) == -1) return false;
    if (WIFSTOPPED(status)) {
        RegisterSet r;
        if (readRegisters(r)) {
            // orig_rax holds syscall number; rax holds return value on exit
            syscall_nr = (int)r.orig_rax;
            // If rax == -ENOSYS, we're at entry; otherwise at exit
            is_entry = ((long)r.rax == -38);  // -ENOSYS
        }
        state_ = RunState::Stopped;
        return true;
    }
    return false;
}

std::vector<DebuggerCore::Frame> DebuggerCore::backtrace(size_t max_frames) {
    std::vector<Frame> frames;
    if (pid_ == 0) return frames;
    RegisterSet r;
    if (!readRegisters(r)) return frames;

    Frame current;
    current.rip = r.rip;
    current.rbp = r.rbp;
    current.rsp = r.rsp;
    // Resolve symbol from maps
    for (auto& m : maps_cache_) {
        if (current.rip >= m.start && current.rip < m.end) {
            size_t slash = m.path.rfind('/');
            std::string bn = (slash != std::string::npos) ? m.path.substr(slash + 1) : m.path;
            current.symbol = bn + "+0x" + hex2(current.rip - m.start, 0);
            break;
        }
    }
    frames.push_back(current);

    // Walk the RBP chain
    addr_t rbp = r.rbp;
    for (size_t i = 1; i < max_frames; i++) {
        if (rbp == 0) break;
        u64 next_rbp = 0, ret_addr = 0;
        if (!readMemory(rbp, &next_rbp, 8)) break;
        if (!readMemory(rbp + 8, &ret_addr, 8)) break;
        if (next_rbp <= rbp) break; // sanity
        Frame f;
        f.rip = ret_addr;
        f.rbp = next_rbp;
        f.rsp = rbp + 16;
        for (auto& m : maps_cache_) {
            if (f.rip >= m.start && f.rip < m.end) {
                size_t slash = m.path.rfind('/');
                std::string bn = (slash != std::string::npos) ? m.path.substr(slash + 1) : m.path;
                f.symbol = bn + "+0x" + hex2(f.rip - m.start, 0);
                break;
            }
        }
        frames.push_back(f);
        rbp = next_rbp;
    }
    return frames;
}

long DebuggerCore::currentSyscall() const {
    // We can't read registers from a const method, but we can return the cached value
    // from the last stop. For simplicity, return 0 if not in a syscall stop.
    return 0;
}

} // namespace ndbg
