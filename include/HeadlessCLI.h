// NinjaDBG v1.2.0 - Headless CLI
// Open Source (Apache-2.0) - by Chapzoo
//
// Full-featured command-line interface to NinjaDBG. Designed for:
//   - Scripted / automated debugging (CI, malware analysis pipelines)
//   - SSH-only environments where no X server is available
//   - Use as a gdbserver-style backend for other frontends
//
// v1.2.0 adds:
//   - disas: full x86-64 disassembly (standalone Disassembler module)
//   - edit:  interactive TUI memory editor (VT100, no ncurses)
//   - script run: Lua + Python scripting via JSON-RPC subprocess
//
// v1.2.0 adds:
//   - decomp: native C decompilation via RetDec / angr backends
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
//   break <addr> [cond]      (also: b <addr> [cond])
//   tbreak <addr>            (temporary breakpoint)
//   watch <addr> [len] [w|rw|x]
//   delete <id>              (also: d <id>)
//   info <b|r|t|m|target>
//   x /Nxb <addr>            (examine N bytes in hex)
//   x /Nxw <addr>            (examine N words)
//   set <addr> = <bytes>
//   disas [addr] [count]     [v1.2.0 — full x86-64 decoder]
//   edit [addr]              [v1.2.0 — interactive TUI memory editor]
//   decomp [addr]            [v1.2.0 — native C decompilation via RetDec/angr]
//   decomp file <bin> [addr] [v1.2.0 — decompile whole file or function]
//   decomp <list|api|set>    [v1.2.0 — backend management]
//   backtrace | bt
//   patch <list|apply|nop|save|undo|info>
//   stealth <list|on|off>
//   kernel <status|load|unload>
//   target <binary>          (load binary for static patching)
//   script <list|run|api>    [v1.2.0 — Lua + Python scripting]
//   help [cmd]
//   quit | q
#pragma once

#include "Types.h"
#include "DebuggerCore.h"
#include "AntiDetect.h"
#include "KernelStealth.h"
#include "BinaryPatcher.h"
#include "PlatformAdapters.h"
#include "Disassembler.h"
#include "ScriptEngine.h"
#include "Decompiler.h"
#include "PrettyPrinter.h"
#include <string>
#include <vector>
#include <memory>

namespace ndbg {

class InteractiveMemoryEditor;

class HeadlessCLI {
public:
    HeadlessCLI();
    ~HeadlessCLI();

    // Run the REPL. Returns process exit code.
    // If skip_eula is true, the EULA acceptance prompt is skipped (for --no-eula-check).
    int run(int argc, char** argv, bool skip_eula = false);

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
    Disassembler disas_;
    std::unique_ptr<ScriptEngine> script_;
    std::unique_ptr<InteractiveMemoryEditor> editor_;
    Decompiler decompiler_;
    PrettyPrinter pretty_;

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
    void cmdEdit(const std::vector<std::string>& args);
    void cmdDecomp(const std::vector<std::string>& args);
    void cmdPretty(const std::vector<std::string>& args);
    void cmdBacktrace(const std::vector<std::string>& args);
    void cmdPatch(const std::vector<std::string>& args);
    void cmdStealth(const std::vector<std::string>& args);
    void cmdKernel(const std::vector<std::string>& args);
    void cmdTarget(const std::vector<std::string>& args);
    void cmdScript(const std::vector<std::string>& args);
    void cmdHelp(const std::vector<std::string>& args);
    void cmdQuit(const std::vector<std::string>& args);

    // v1.2.0: x64dbg-like features
    void cmdModules(const std::vector<std::string>& args);
    void cmdHandles(const std::vector<std::string>& args);
    void cmdDump(const std::vector<std::string>& args);
    void cmdFind(const std::vector<std::string>& args);
    void cmdFindStr(const std::vector<std::string>& args);
    void cmdFlags(const std::vector<std::string>& args);
    void cmdSetReg(const std::vector<std::string>& args);
    void cmdMemMap(const std::vector<std::string>& args);
    void cmdTrace(const std::vector<std::string>& args);

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
    void printScriptStatus();
    void printDecompStatus();

    // Resolve an address to a symbol string for annotations
    std::string resolveSymbol(addr_t a);

    // Parse a hex address (0x... or pure hex)
    static addr_t parseAddr(const std::string& s, bool* ok = nullptr);
    // Parse N bytes from a list of hex tokens
    static std::vector<u8> parseBytes(const std::vector<std::string>& tokens);

    // Output helper (so we can redirect for batch mode in the future)
    void out(const std::string& s);
    void err(const std::string& s);
};

} // namespace ndbg

