// NinjaDBG v1.0.3 - Headless CLI
// Closed Source - Free - by Chapzoo
//
// Full-featured command-line interface to NinjaDBG. Designed for:
//   - Scripted / automated debugging (CI, malware analysis pipelines)
//   - SSH-only environments where no X server is available
//   - Use as a gdbserver-style backend for other frontends
//
// The CLI supports an interactive REPL with command history, plus a
// batch mode (-c "commands") for one-shot scripts.
//
// Commands follow gdb-like syntax where possible:
//   attach <pid>
//   launch <binary> [args...]
//   detach
//   kill
//   continue | cont | c
//   step | s
//   stepi | si
//   next | n
//   break <addr>             (also: b <addr>)
//   tbreak <addr>            (temporary breakpoint)
//   delete <id>              (also: d <id>)
//   info breakpoints         (also: i b)
//   info registers           (also: i r)
//   info threads             (also: i t)
//   info maps                (also: i m)
//   x /Nxb <addr>            (examine N bytes in hex)
//   x /Nxw <addr>            (examine N words)
//   set <addr> = <bytes>
//   disas [addr] [count]
//   backtrace | bt
//   patch list
//   patch apply <offset> <kind> [bytes...]
//   patch nop <offset> <length>
//   patch save <outfile>
//   patch undo <id>
//   stealth list
//   stealth on <id|name>
//   stealth off <id|name>
//   kernel status
//   kernel load
//   kernel unload
//   target <binary>          (load binary for static patching)
//   info target
//   help [cmd]
//   quit | q
#pragma once

#include "Types.h"
#include "DebuggerCore.h"
#include "AntiDetect.h"
#include "KernelStealth.h"
#include "BinaryPatcher.h"
#include "PlatformAdapters.h"
#include <string>
#include <vector>
#include <memory>

namespace ndbg {

class HeadlessCLI {
public:
    HeadlessCLI();
    ~HeadlessCLI();

    // Run the REPL. Returns process exit code.
    int run(int argc, char** argv);

    // Print version banner
    void printBanner();

    // Print EULA and require acceptance (returns false if user declined)
    bool showEula();

private:
    DebuggerCore dbg_;
    AntiDetect   anti_;
    KernelStealth kernel_;
    BinaryPatcher patcher_;
    std::unique_ptr<PlatformAdapter> adapter_;

    bool running_ = true;
    bool eula_accepted_ = false;
    std::vector<std::string> history_;

    // REPL plumbing
    void prompt();
    std::string readLine();
    void execute(const std::string& line);
    void addHistory(const std::string& line);

    // Command handlers
    void cmdAttach(const std::vector<std::string>& args);
    void cmdLaunch(const std::vector<std::string>& args);
    void cmdDetach(const std::vector<std::string>& args);
    void cmdKill(const std::vector<std::string>& args);
    void cmdContinue(const std::vector<std::string>& args);
    void cmdStep(const std::vector<std::string>& args);
    void cmdNext(const std::vector<std::string>& args);
    void cmdBreak(const std::vector<std::string>& args, bool temporary);
    void cmdDelete(const std::vector<std::string>& args);
    void cmdInfo(const std::vector<std::string>& args);
    void cmdExamine(const std::vector<std::string>& args);
    void cmdSet(const std::vector<std::string>& args);
    void cmdDisas(const std::vector<std::string>& args);
    void cmdBacktrace(const std::vector<std::string>& args);
    void cmdPatch(const std::vector<std::string>& args);
    void cmdStealth(const std::vector<std::string>& args);
    void cmdKernel(const std::vector<std::string>& args);
    void cmdTarget(const std::vector<std::string>& args);
    void cmdHelp(const std::vector<std::string>& args);
    void cmdQuit(const std::vector<std::string>& args);

    // Helpers
    void printRegisters();
    void printBreakpoints();
    void printThreads();
    void printMaps();
    void printBacktrace();
    void printPatchList();
    void printStealthStatus();
    void printKernelStatus();
    void printTargetInfo();

    // Parse a hex address (0x... or pure hex)
    static addr_t parseAddr(const std::string& s, bool* ok = nullptr);
    // Parse N bytes from a list of hex tokens
    static std::vector<u8> parseBytes(const std::vector<std::string>& tokens);

    // Output helper (so we can redirect for batch mode in the future)
    void out(const std::string& s);
    void err(const std::string& s);
};

} // namespace ndbg
