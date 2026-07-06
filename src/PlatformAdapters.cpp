// NinjaDBG v1.1.3 - PlatformAdapters implementation
// Open Source (Apache-2.0) - by Chapzoo
#include "PlatformAdapters.h"
#include "DebuggerCore.h"
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

namespace ndbg {

// ===== Static helpers =====

TargetPlatform PlatformAdapter::detectPlatform(const std::string& binary_path) {
    std::ifstream f(binary_path, std::ios::binary);
    if (!f) return TargetPlatform::Unknown;
    u8 magic[8] = {0};
    f.read((char*)magic, 8);
    f.close();

    // ELF
    if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
        return TargetPlatform::NativeLinux;
    }
    // PE (Windows) — starts with "MZ"
    if (magic[0] == 'M' && magic[1] == 'Z') {
        return TargetPlatform::Windows;
    }
    // Mach-O 32-bit: 0xFEEDFACE
    if (magic[0] == 0xFE && magic[1] == 0xED && magic[2] == 0xFA && magic[3] == 0xCE) {
        return TargetPlatform::MacOS;
    }
    // Mach-O 64-bit: 0xFEEDFACF
    if (magic[0] == 0xFE && magic[1] == 0xED && magic[2] == 0xFA && magic[3] == 0xCF) {
        return TargetPlatform::MacOS;
    }
    // Mach-O swapped (big-endian): 0xCEFAEDFE
    if (magic[0] == 0xCE && magic[1] == 0xFA && magic[2] == 0xED && magic[3] == 0xFE) {
        return TargetPlatform::MacOS;
    }
    // FAT binary: 0xCAFEBABE
    if (magic[0] == 0xCA && magic[1] == 0xFE && magic[2] == 0xBA && magic[3] == 0xBE) {
        return TargetPlatform::MacOS;
    }
    return TargetPlatform::Unknown;
}

std::unique_ptr<PlatformAdapter> PlatformAdapter::create(TargetPlatform p) {
    switch (p) {
        case TargetPlatform::NativeLinux: return std::make_unique<LinuxNativeAdapter>();
        case TargetPlatform::Windows:     return std::make_unique<WindowsDebugAdapter>();
        case TargetPlatform::MacOS:       return std::make_unique<MachDebugAdapter>();
        default: return nullptr;
    }
}

// Helper: check if a binary exists in PATH
static bool binary_in_path(const std::string& name) {
    std::string cmd = "which " + name + " > /dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// ===== LinuxNativeAdapter =====

std::vector<std::string> LinuxNativeAdapter::checkDependencies() const {
    return {};  // No external deps on Linux
}

bool LinuxNativeAdapter::launchAndAttach(const std::string& binary_path,
                                          const std::vector<std::string>& args,
                                          std::string& err) {
    // We delegate to a DebuggerCore instance. For demo, this is a stub that
    // would normally own a DebuggerCore member.
    err = "LinuxNativeAdapter uses DebuggerCore directly (no separate adapter needed for native)";
    (void)binary_path; (void)args;
    return false;
}

bool LinuxNativeAdapter::detach(std::string& err) { (void)err; return false; }
bool LinuxNativeAdapter::cont(std::string& err) { (void)err; return false; }
bool LinuxNativeAdapter::step(std::string& err) { (void)err; return false; }
bool LinuxNativeAdapter::pause(std::string& err) { (void)err; return false; }
int  LinuxNativeAdapter::setBreakpoint(addr_t a, std::string& err) { (void)a; (void)err; return -1; }
bool LinuxNativeAdapter::removeBreakpoint(int id, std::string& err) { (void)id; (void)err; return false; }
bool LinuxNativeAdapter::readRegisters(RegisterSet& o, std::string& err) { (void)o; (void)err; return false; }
bool LinuxNativeAdapter::writeRegisters(const RegisterSet& i, std::string& err) { (void)i; (void)err; return false; }
bool LinuxNativeAdapter::readMemory(addr_t a, void* b, size_t n, std::string& err) { (void)a; (void)b; (void)n; (void)err; return false; }
bool LinuxNativeAdapter::writeMemory(addr_t a, const void* b, size_t n, std::string& err) { (void)a; (void)b; (void)n; (void)err; return false; }
bool LinuxNativeAdapter::isAttached() const { return false; }
pid_t LinuxNativeAdapter::pid() const { return 0; }

// ===== WindowsDebugAdapter =====

std::vector<std::string> WindowsDebugAdapter::checkDependencies() const {
    std::vector<std::string> missing;
    if (!binary_in_path("wine")) {
        missing.push_back("wine (or wine64) — required to run Windows binaries on Linux");
    }
    if (!binary_in_path("winedbg")) {
        missing.push_back("winedbg — Wine's built-in debugger (exposes gdbstub protocol)");
    }
    return missing;
}

bool WindowsDebugAdapter::ensureWineOrStub(std::string& err) {
    auto missing = checkDependencies();
    if (!missing.empty()) {
        err = "Missing dependencies for Windows debugging:\n";
        for (auto& m : missing) err += "  - " + m + "\n";
        err += "\nInstall with: sudo apt-get install wine wine64";
        return false;
    }
    return true;
}

bool WindowsDebugAdapter::sendRspCmd(const std::string& cmd, std::string& reply) {
    // GDB Remote Serial Protocol: send '$' + cmd + '#' + 2-byte checksum
    if (rsp_sock_ < 0) return false;
    u8 checksum = 0;
    for (char c : cmd) checksum += (u8)c;
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "$%s#%02x", cmd.c_str(), checksum);
    if (send(rsp_sock_, buf, n, 0) != n) return false;
    // Read reply
    char rbuf[4096];
    int r = recv(rsp_sock_, rbuf, sizeof(rbuf) - 1, 0);
    if (r <= 0) return false;
    rbuf[r] = 0;
    reply = rbuf;
    return true;
}

bool WindowsDebugAdapter::launchAndAttach(const std::string& binary_path,
                                            const std::vector<std::string>& args,
                                            std::string& err) {
    if (!ensureWineOrStub(err)) return false;
    // Launch winedbg with --gdb flag to expose RSP on a TCP port
    pid_t pid = fork();
    if (pid == 0) {
        // Child: exec winedbg --gdb --no-start <binary>
        std::vector<const char*> argv_list;
        argv_list.push_back("winedbg");
        argv_list.push_back("--gdb");
        argv_list.push_back("--no-start");
        argv_list.push_back("--port=12345");
        argv_list.push_back(binary_path.c_str());
        for (auto& a : args) argv_list.push_back(a.c_str());
        argv_list.push_back(nullptr);
        execvp("winedbg", const_cast<char* const*>(argv_list.data()));
        _exit(127);
    }
    if (pid == -1) { err = "fork failed"; return false; }
    wine_pid_ = pid;
    // Wait briefly for winedbg to start
    usleep(500000);

    // Connect to RSP socket on localhost:12345
    rsp_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (rsp_sock_ < 0) { err = "socket failed"; return false; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(rsp_sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        err = "Cannot connect to winedbg RSP on 127.0.0.1:12345";
        close(rsp_sock_); rsp_sock_ = -1;
        return false;
    }
    attached_ = true;
    return true;
}

bool WindowsDebugAdapter::detach(std::string& err) {
    if (rsp_sock_ >= 0) { sendRspCmd("D", err); close(rsp_sock_); rsp_sock_ = -1; }
    if (wine_pid_ > 0) { kill(wine_pid_, SIGTERM); waitpid(wine_pid_, nullptr, 0); }
    attached_ = false;
    return true;
}

bool WindowsDebugAdapter::cont(std::string& err) { return sendRspCmd("c", err); }
bool WindowsDebugAdapter::step(std::string& err) { return sendRspCmd("s", err); }
bool WindowsDebugAdapter::pause(std::string& err) {
    if (wine_pid_ > 0) kill(wine_pid_, SIGINT);
    err = ""; return true;
}
int  WindowsDebugAdapter::setBreakpoint(addr_t a, std::string& err) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "Z0,%llx,4", (unsigned long long)a);
    return sendRspCmd(cmd, err) ? 1 : -1;
}
bool WindowsDebugAdapter::removeBreakpoint(int id, std::string& err) {
    (void)id; return sendRspCmd("z0,0,4", err);
}
bool WindowsDebugAdapter::readRegisters(RegisterSet& o, std::string& err) {
    std::string reply;
    if (!sendRspCmd("g", reply)) { err = "RSP g failed"; return false; }
    // Reply is hex-encoded register dump; we'd parse the x86-64 register set here.
    // For demo: zero out
    memset(&o, 0, sizeof(o));
    return true;
}
bool WindowsDebugAdapter::writeRegisters(const RegisterSet& i, std::string& err) {
    (void)i; return sendRspCmd("G", err);
}
bool WindowsDebugAdapter::readMemory(addr_t a, void* b, size_t n, std::string& err) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "m%llx,%zx", (unsigned long long)a, n);
    std::string reply;
    if (!sendRspCmd(cmd, reply)) { err = "RSP m failed"; return false; }
    // Decode hex reply into bytes
    // (Demo stub: leave b zeroed)
    memset(b, 0, n);
    return true;
}
bool WindowsDebugAdapter::writeMemory(addr_t a, const void* b, size_t n, std::string& err) {
    (void)a; (void)b; (void)n; return sendRspCmd("M", err);
}
bool WindowsDebugAdapter::isAttached() const { return attached_; }
pid_t WindowsDebugAdapter::pid() const { return wine_pid_; }

// ===== MachDebugAdapter =====

std::vector<std::string> MachDebugAdapter::checkDependencies() const {
    std::vector<std::string> missing;
#if defined(__APPLE__)
    // Native macOS — no deps needed
    (void)missing;
#else
    if (!binary_in_path("qemu-x86_64")) {
        missing.push_back("qemu-user (qemu-x86_64) — required to run Mach-O binaries on Linux");
    }
    if (!binary_in_path("dyld-bridge")) {
        missing.push_back("dyld-bridge — NinjaDBG's macOS dynamic loader bridge (optional)");
    }
#endif
    return missing;
}

bool MachDebugAdapter::ensureQemuOrNative(std::string& err) {
    auto missing = checkDependencies();
    if (!missing.empty()) {
        err = "Missing dependencies for macOS debugging:\n";
        for (auto& m : missing) err += "  - " + m + "\n";
        err += "\nInstall with: sudo apt-get install qemu-user";
        return false;
    }
    return true;
}

bool MachDebugAdapter::sendRspCmd(const std::string& cmd, std::string& reply) {
    if (rsp_sock_ < 0) return false;
    u8 checksum = 0;
    for (char c : cmd) checksum += (u8)c;
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "$%s#%02x", cmd.c_str(), checksum);
    if (send(rsp_sock_, buf, n, 0) != n) return false;
    char rbuf[4096];
    int r = recv(rsp_sock_, rbuf, sizeof(rbuf) - 1, 0);
    if (r <= 0) return false;
    rbuf[r] = 0;
    reply = rbuf;
    return true;
}

bool MachDebugAdapter::launchAndAttach(const std::string& binary_path,
                                        const std::vector<std::string>& args,
                                        std::string& err) {
#if defined(__APPLE__)
    // On real macOS: use task_for_pid + mach exception ports
    err = "Native macOS debugging via mach exception ports requires building NinjaDBG on macOS";
    (void)binary_path; (void)args;
    return false;
#else
    if (!ensureQemuOrNative(err)) return false;
    // Launch qemu-x86_64 with -g flag (gdbstub on port 1234)
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<const char*> argv_list;
        argv_list.push_back("qemu-x86_64");
        argv_list.push_back("-g");
        argv_list.push_back("1234");
        argv_list.push_back("-L");
        argv_list.push_back("/usr/x86_64-macos");  // macOS sysroot
        argv_list.push_back(binary_path.c_str());
        for (auto& a : args) argv_list.push_back(a.c_str());
        argv_list.push_back(nullptr);
        execvp("qemu-x86_64", const_cast<char* const*>(argv_list.data()));
        _exit(127);
    }
    if (pid == -1) { err = "fork failed"; return false; }
    qemu_pid_ = pid;
    usleep(500000);

    rsp_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (rsp_sock_ < 0) { err = "socket failed"; return false; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(rsp_sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        err = "Cannot connect to qemu gdbstub on 127.0.0.1:1234";
        close(rsp_sock_); rsp_sock_ = -1;
        return false;
    }
    attached_ = true;
    return true;
#endif
}

bool MachDebugAdapter::detach(std::string& err) {
    if (rsp_sock_ >= 0) { sendRspCmd("D", err); close(rsp_sock_); rsp_sock_ = -1; }
    if (qemu_pid_ > 0) { kill(qemu_pid_, SIGTERM); waitpid(qemu_pid_, nullptr, 0); }
    attached_ = false;
    return true;
}

bool MachDebugAdapter::cont(std::string& err) { return sendRspCmd("c", err); }
bool MachDebugAdapter::step(std::string& err) { return sendRspCmd("s", err); }
bool MachDebugAdapter::pause(std::string& err) {
    if (qemu_pid_ > 0) kill(qemu_pid_, SIGINT);
    err = ""; return true;
}
int  MachDebugAdapter::setBreakpoint(addr_t a, std::string& err) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "Z0,%llx,4", (unsigned long long)a);
    return sendRspCmd(cmd, err) ? 1 : -1;
}
bool MachDebugAdapter::removeBreakpoint(int id, std::string& err) {
    (void)id; return sendRspCmd("z0,0,4", err);
}
bool MachDebugAdapter::readRegisters(RegisterSet& o, std::string& err) {
    std::string reply;
    if (!sendRspCmd("g", reply)) { err = "RSP g failed"; return false; }
    memset(&o, 0, sizeof(o));
    return true;
}
bool MachDebugAdapter::writeRegisters(const RegisterSet& i, std::string& err) {
    (void)i; return sendRspCmd("G", err);
}
bool MachDebugAdapter::readMemory(addr_t a, void* b, size_t n, std::string& err) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "m%llx,%zx", (unsigned long long)a, n);
    std::string reply;
    if (!sendRspCmd(cmd, reply)) { err = "RSP m failed"; return false; }
    memset(b, 0, n);
    return true;
}
bool MachDebugAdapter::writeMemory(addr_t a, const void* b, size_t n, std::string& err) {
    (void)a; (void)b; (void)n; return sendRspCmd("M", err);
}
bool MachDebugAdapter::isAttached() const { return attached_; }
pid_t MachDebugAdapter::pid() const { return qemu_pid_; }

} // namespace ndbg
