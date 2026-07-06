// NinjaDBG v1.1.1 - Platform Debug Adapters
// Open Source (Apache-2.0) - by Chapzoo
//
// NinjaDBG's DebuggerCore is Linux-native (ptrace). To debug Windows and
// macOS binaries we provide adapters that translate our internal API to
// each platform's debugging primitives:
//
//   - WindowsDebug: uses Wine's winedbg stub protocol OR (when running on
//                   real Windows) the Win32 DebugActiveProcess API.
//                   On Linux we use Wine + gdbstub, which exposes the target
//                   through a GDB Remote Serial Protocol endpoint.
//   - MachDebug:    uses mach exception ports (native macOS) or, on Linux,
//                   a QEMU-based stub that runs the Mach-O binary and
//                   exposes its state through a similar RSP endpoint.
//
// Both adapters present the same DebuggerCore-like interface to the rest
// of NinjaDBG, so the UI and CLI don't need to know which platform the
// target was compiled for.
//
// NOTE: In this free release, the adapters are STUBS that demonstrate the
// protocol layer. Actual cross-platform debugging requires either:
//   - Running NinjaDBG natively on the target OS, OR
//   - Running the target under Wine/QEMU with the appropriate stub
//
// The stubs auto-detect target format from ELF/PE/Mach-O headers and route
// to the correct adapter. If Wine/QEMU is not installed, the user is told
// what to install.
#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <memory>

namespace ndbg {

enum class TargetPlatform {
    NativeLinux,    // ELF, debug with ptrace
    Windows,        // PE, debug via Wine + gdbstub or Win32 API
    MacOS,          // Mach-O, debug via mach exception ports or QEMU stub
    Unknown
};

class PlatformAdapter {
public:
    virtual ~PlatformAdapter() = default;

    // Probe the target binary file to determine its platform
    static TargetPlatform detectPlatform(const std::string& binary_path);

    // Factory: returns the right adapter for a platform
    static std::unique_ptr<PlatformAdapter> create(TargetPlatform p);

    // Check whether this adapter's runtime dependencies are available
    // (e.g. Wine for Windows, QEMU for macOS). Returns a list of missing deps.
    virtual std::vector<std::string> checkDependencies() const = 0;

    // Human-readable name
    virtual std::string name() const = 0;

    // Launch + attach (returns the remote pid/tid, or 0 on failure)
    virtual bool launchAndAttach(const std::string& binary_path,
                                 const std::vector<std::string>& args,
                                 std::string& err) = 0;

    // Detach and let the target continue
    virtual bool detach(std::string& err) = 0;

    // Run control
    virtual bool cont(std::string& err) = 0;
    virtual bool step(std::string& err) = 0;
    virtual bool pause(std::string& err) = 0;

    // Breakpoints (returns bp id, -1 on failure)
    virtual int  setBreakpoint(addr_t addr, std::string& err) = 0;
    virtual bool removeBreakpoint(int id, std::string& err) = 0;

    // Register + memory access (unified RegisterSet type)
    virtual bool readRegisters(RegisterSet& out, std::string& err) = 0;
    virtual bool writeRegisters(const RegisterSet& in, std::string& err) = 0;
    virtual bool readMemory(addr_t addr, void* buf, size_t n, std::string& err) = 0;
    virtual bool writeMemory(addr_t addr, const void* buf, size_t n, std::string& err) = 0;

    // State
    virtual bool isAttached() const = 0;
    virtual pid_t pid() const = 0;
};

// Linux native adapter (wraps DebuggerCore)
class LinuxNativeAdapter : public PlatformAdapter {
public:
    std::string name() const override { return "Linux Native (ptrace)"; }
    std::vector<std::string> checkDependencies() const override;
    bool launchAndAttach(const std::string& binary_path,
                         const std::vector<std::string>& args,
                         std::string& err) override;
    bool detach(std::string& err) override;
    bool cont(std::string& err) override;
    bool step(std::string& err) override;
    bool pause(std::string& err) override;
    int  setBreakpoint(addr_t addr, std::string& err) override;
    bool removeBreakpoint(int id, std::string& err) override;
    bool readRegisters(RegisterSet& out, std::string& err) override;
    bool writeRegisters(const RegisterSet& in, std::string& err) override;
    bool readMemory(addr_t addr, void* buf, size_t n, std::string& err) override;
    bool writeMemory(addr_t addr, const void* buf, size_t n, std::string& err) override;
    bool isAttached() const override;
    pid_t pid() const override;
};

// Windows adapter (Wine + gdbstub on Linux, Win32 API on Windows)
class WindowsDebugAdapter : public PlatformAdapter {
public:
    std::string name() const override { return "Windows (Wine + gdbstub)"; }
    std::vector<std::string> checkDependencies() const override;
    bool launchAndAttach(const std::string& binary_path,
                         const std::vector<std::string>& args,
                         std::string& err) override;
    bool detach(std::string& err) override;
    bool cont(std::string& err) override;
    bool step(std::string& err) override;
    bool pause(std::string& err) override;
    int  setBreakpoint(addr_t addr, std::string& err) override;
    bool removeBreakpoint(int id, std::string& err) override;
    bool readRegisters(RegisterSet& out, std::string& err) override;
    bool writeRegisters(const RegisterSet& in, std::string& err) override;
    bool readMemory(addr_t addr, void* buf, size_t n, std::string& err) override;
    bool writeMemory(addr_t addr, const void* buf, size_t n, std::string& err) override;
    bool isAttached() const override;
    pid_t pid() const override;

private:
    pid_t wine_pid_ = 0;
    int   rsp_sock_ = -1;  // gdb Remote Serial Protocol socket
    bool  attached_ = false;
    bool ensureWineOrStub(std::string& err);
    bool sendRspCmd(const std::string& cmd, std::string& reply);
};

// macOS adapter (mach exception ports on macOS, QEMU stub on Linux)
class MachDebugAdapter : public PlatformAdapter {
public:
    std::string name() const override { return "macOS (mach exception ports / QEMU stub)"; }
    std::vector<std::string> checkDependencies() const override;
    bool launchAndAttach(const std::string& binary_path,
                         const std::vector<std::string>& args,
                         std::string& err) override;
    bool detach(std::string& err) override;
    bool cont(std::string& err) override;
    bool step(std::string& err) override;
    bool pause(std::string& err) override;
    int  setBreakpoint(addr_t addr, std::string& err) override;
    bool removeBreakpoint(int id, std::string& err) override;
    bool readRegisters(RegisterSet& out, std::string& err) override;
    bool writeRegisters(const RegisterSet& in, std::string& err) override;
    bool readMemory(addr_t addr, void* buf, size_t n, std::string& err) override;
    bool writeMemory(addr_t addr, const void* buf, size_t n, std::string& err) override;
    bool isAttached() const override;
    pid_t pid() const override;

private:
    pid_t qemu_pid_ = 0;
    int   rsp_sock_ = -1;
    bool  attached_ = false;
    bool ensureQemuOrNative(std::string& err);
    bool sendRspCmd(const std::string& cmd, std::string& reply);
};

} // namespace ndbg
