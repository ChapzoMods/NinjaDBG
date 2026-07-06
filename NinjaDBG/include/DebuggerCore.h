// NinjaDBG v1.1.3 - Debugger Core (ptrace-based)
// Open Source (Apache-2.0) - by Chapzoo
#pragma once

#include "Types.h"
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>
#include <elf.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <dirent.h>

namespace ndbg {

class AntiDetect;

class DebuggerCore {
public:
    enum class RunState {
        Idle,
        Attached,
        Running,
        Stopped,
        Exited
    };

    DebuggerCore();
    ~DebuggerCore();

    // --- Attach / detach ---
    bool attach(pid_t pid);
    bool attachByLaunch(const std::string& path, const std::vector<std::string>& args);
    bool detach();
    bool kill();

    // --- Run control ---
    bool step();
    bool cont();
    bool contUntil(addr_t addr);
    bool stop();

    // --- Wait for stop event ---
    bool waitForStop(int& signal_out, bool& exited_out, int& exit_code_out);

    // --- Breakpoints ---
    int  addBreakpoint(addr_t addr, bool hardware = false, const std::string& label = "");
    bool removeBreakpoint(int id);
    bool enableBreakpoint(int id);
    bool disableBreakpoint(int id);
    std::vector<Breakpoint> breakpoints() const;
    Breakpoint* findBreakpoint(addr_t addr);
    int  findBreakpointId(addr_t addr);

    // v1.1.3 — conditional breakpoints: only fire when condition evaluates true
    // Condition syntax: "rax == 0x10", "rdi != 0", "rip > 0x400000" etc.
    // Empty condition = always fire (normal breakpoint behavior)
    int  addConditionalBreakpoint(addr_t addr, const std::string& condition,
                                  const std::string& label = "");
    bool setBreakpointCondition(int id, const std::string& condition);
    // Check a breakpoint's condition against current register state. Returns
    // true if the breakpoint should fire (i.e. condition is empty or true).
    bool checkBreakpointCondition(int id);

    // v1.1.3 — temporary breakpoints (auto-remove after first hit)
    int  addTempBreakpoint(addr_t addr, const std::string& label = "_temp");

    // v1.1.3 — watchpoints (memory access breakpoints via DR0-DR3 + DR7)
    enum class WatchType { Write, ReadWrite, Execute };
    int  addWatchpoint(addr_t addr, size_t len, WatchType type, const std::string& label = "");
    bool removeWatchpoint(int id);
    std::vector<Breakpoint> watchpoints() const;

    // v1.1.3 — step out: continue until current function returns
    // (sets a temp bp at the return address on top of stack)
    bool stepOut();

    // v1.1.3 — step over: if current instruction is a CALL, set temp bp after it
    bool stepOver();

    // v1.1.3 — run until next syscall entry/exit (PTRACE_SYSCALL)
    bool stepToSyscall(int& syscall_nr, bool& is_entry);

    // v1.1.3 — backtrace (walks RBP chain, returns instruction pointers)
    struct Frame {
        addr_t rip;
        addr_t rbp;
        addr_t rsp;
        std::string symbol;
    };
    std::vector<Frame> backtrace(size_t max_frames = 32);

    // v1.1.3 — get current syscall number (from orig_rax)
    long currentSyscall() const;

    // --- Registers ---
    bool readRegisters(RegisterSet& out);
    bool writeRegisters(const RegisterSet& in);

    // --- Memory (stealth: uses process_vm_readv when possible) ---
    bool readMemory(addr_t addr, void* buf, size_t n);
    bool writeMemory(addr_t addr, const void* buf, size_t n);
    std::vector<u8> readMemoryVec(addr_t addr, size_t n);

    // --- Maps / threads / info ---
    std::vector<MemoryRegion> readMaps();
    std::vector<ThreadInfo>   readThreads();
    addr_t findSymbol(const std::string& name);
    addr_t findFunctionStart(addr_t addr);

    // --- State ---
    pid_t     pid() const { return pid_; }
    RunState  state() const { return state_; }
    std::string lastError() const { return last_error_; }

    void setAntiDetect(AntiDetect* a) { anti_ = a; }

    static std::vector<ProcessInfo> listProcesses();

private:
    pid_t       pid_ = 0;
    RunState    state_ = RunState::Idle;
    std::unordered_map<int, Breakpoint> bps_;
    int         next_bp_id_ = 1;
    std::string last_error_;
    AntiDetect* anti_ = nullptr;
    std::vector<MemoryRegion> maps_cache_;   // v1.1.3: cached for backtrace symbol resolution

    bool setSwBreakpoint(addr_t addr, u8* saved);
    bool clearSwBreakpoint(addr_t addr, u8 saved);
    bool installAllBps();
    bool uninstallAllBps();
    bool installBp(Breakpoint& bp);
    bool uninstallBp(Breakpoint& bp);

    u8   peekByte(addr_t addr);
    bool pokeByte(addr_t addr, u8 v);
};

} // namespace ndbg
