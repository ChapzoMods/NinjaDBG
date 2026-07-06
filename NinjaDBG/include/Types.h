// NinjaDBG v1.0.2 - Common Types & Constants
// Open Source (MIT) - by Chapzoo
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream>
#include <iomanip>

namespace ndbg {

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using addr_t = uint64_t;

inline std::string hex(u64 v, int w = 16) {
    std::ostringstream s;
    s << "0x" << std::uppercase << std::setfill('0') << std::setw(w) << std::hex << v;
    return s.str();
}

inline std::string hex2(u64 v, int w = 2) {
    std::ostringstream s;
    s << std::uppercase << std::setfill('0') << std::setw(w) << std::hex << v;
    return s.str();
}

struct Breakpoint {
    int          id = 0;
    addr_t       address = 0;
    u8           original_byte = 0;
    bool         enabled = true;
    bool         hardware = false;
    int          hw_slot = -1;
    int          hit_count = 0;
    std::string  condition;
    std::string  label;
    bool         temporary = false;        // v1.1.0: auto-remove after first hit
    bool         is_watchpoint = false;    // v1.1.0: watchpoint flag
    int          watch_len = 0;            // v1.1.0: watch length (1/2/4/8)
    int          watch_type = 0;           // v1.1.0: 0=W 1=RW 2=X
};

struct RegisterSet {
    u64 rax = 0, rbx = 0, rcx = 0, rdx = 0;
    u64 rsi = 0, rdi = 0, rbp = 0, rsp = 0;
    u64 r8 = 0, r9 = 0, r10 = 0, r11 = 0;
    u64 r12 = 0, r13 = 0, r14 = 0, r15 = 0;
    u64 rip = 0;
    u64 rflags = 0;
    u64 cs = 0, ss = 0, ds = 0, es = 0, fs = 0, gs = 0;
    u64 orig_rax = 0;
};

struct ProcessInfo {
    pid_t       pid = 0;
    std::string name;
    std::string state;
    std::string user;
    u64         memory_kb = 0;
    u64         cpu_percent = 0;
    std::string cmdline;
};

struct MemoryRegion {
    addr_t       start;
    addr_t       end;
    bool         read = false;
    bool         write = false;
    bool         exec = false;
    bool         shared = false;
    std::string  path;
    u64          offset = 0;
};

struct ThreadInfo {
    pid_t        tid;
    std::string  state;
    u64          cpu;
    std::string  name;
};

static const char* kRegNames[] = {
    "RAX","RBX","RCX","RDX","RSI","RDI","RBP","RSP",
    "R8 ","R9 ","R10","R11","R12","R13","R14","R15",
    "RIP","RFL","CS ","SS ","DS ","ES ","FS ","GS "
};

} // namespace ndbg
