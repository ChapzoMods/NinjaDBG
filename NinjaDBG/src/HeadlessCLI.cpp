// NinjaDBG v1.1.4 - HeadlessCLI implementation
// Open Source (Apache-2.0) - by Chapzoo
#include "HeadlessCLI.h"
#include "WelcomeScreen.h"
#include "InteractiveMemoryEditor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <map>
#include <iomanip>
#include <unistd.h>
#include <termios.h>

namespace ndbg {

HeadlessCLI::HeadlessCLI() {
    // Apply default anti-detect mask
    anti_.enable(AntiDetect::Technique::HWBreakpoints);
    anti_.enable(AntiDetect::Technique::ProcVmRW);
    anti_.enable(AntiDetect::Technique::MaskTracerPid);
    anti_.enable(AntiDetect::Technique::Int3ScanBypass);
    script_ = std::make_unique<ScriptEngine>(dbg_);
}
HeadlessCLI::~HeadlessCLI() {}

void HeadlessCLI::out(const std::string& s) { std::cout << s << std::endl; }
void HeadlessCLI::err(const std::string& s) { std::cerr << s << std::endl; }

void HeadlessCLI::printBanner() {
    std::cout <<
        "\n"
        "  _   _ _ _   _           _    ___  _____ \n"
        " | \\ | (_) |_| |__   __ _(_)  / _ \\/ ____|\n"
        " |  \\| | | __| '_ \\ / _` | | | | | | (___  \n"
        " | |\\  | | |_| | | | (_| | | | |_| |\\___ \\ \n"
        " |_| \\_|_|\\__|_| |_|\\__,_|_|  \\___/|____) |\n"
        "\n"
        "  v1.1.4 — Stealth Debugger  (Open Source (Apache-2.0) - by Chapzoo)\n"
        "  Headless CLI mode. Type 'help' for command list, 'quit' to exit.\n"
        "  New in v1.1.4: decomp (native C decompilation via RetDec/angr)\n"
        "\n";
}

bool HeadlessCLI::showEula() {
    using namespace ndbg::ui;
    if (WelcomeScreen::isEulaAccepted()) return true;
    std::cout << WelcomeScreen::welcomeMessage() << std::endl;
    std::cout << WelcomeScreen::eulaText() << std::endl;
    std::cout << "\nDo you accept this EULA? [y/N]: " << std::flush;
    std::string resp;
    std::getline(std::cin, resp);
    if (resp == "y" || resp == "Y" || resp == "yes" || resp == "YES") {
        WelcomeScreen::acceptEula();
        std::cout << "EULA accepted. Saved to " << WelcomeScreen::eulaPath() << std::endl;
        return true;
    }
    return false;
}

void HeadlessCLI::prompt() {
    std::string p = "(ninjadb) ";
    if (dbg_.pid() != 0) {
        p = "(ninjadb:" + std::to_string(dbg_.pid()) + ") ";
    }
    std::cout << p << std::flush;
}

std::string HeadlessCLI::readLine() {
    std::string s;
    std::getline(std::cin, s);
    return s;
}

void HeadlessCLI::addHistory(const std::string& line) {
    history_.push_back(line);
}

addr_t HeadlessCLI::parseAddr(const std::string& s, bool* ok) {
    errno = 0;
    char* end = nullptr;
    unsigned long long v = strtoull(s.c_str(), &end, 0);
    if (ok) *ok = (errno == 0 && end != s.c_str() && *end == '\0');
    return (addr_t)v;
}

std::vector<u8> HeadlessCLI::parseBytes(const std::vector<std::string>& tokens) {
    std::vector<u8> bytes;
    for (auto& t : tokens) {
        u8 b = (u8)strtoul(t.c_str(), nullptr, 0);
        bytes.push_back(b);
    }
    return bytes;
}

void HeadlessCLI::execute(const std::string& line) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty()) return;

    std::vector<std::string> args;
    std::string a;
    while (ss >> a) args.push_back(a);

    if (cmd == "help" || cmd == "h" || cmd == "?")      cmdHelp(args);
    else if (cmd == "quit" || cmd == "q" || cmd == "exit") cmdQuit(args);
    else if (cmd == "attach")    cmdAttach(args);
    else if (cmd == "launch")    cmdLaunch(args);
    else if (cmd == "detach")    cmdDetach(args);
    else if (cmd == "kill")      cmdKill(args);
    else if (cmd == "continue" || cmd == "cont" || cmd == "c") cmdContinue(args);
    else if (cmd == "step" || cmd == "s" || cmd == "stepi" || cmd == "si") cmdStep(args);
    else if (cmd == "next" || cmd == "n") cmdNext(args);
    else if (cmd == "finish" || cmd == "fo") {
        if (dbg_.pid() == 0) { err("Not attached"); return; }
        if (dbg_.stepOut()) out("Stepped out of current function");
        else err("step-out failed: " + dbg_.lastError());
    }
    else if (cmd == "break" || cmd == "b")  cmdBreak(args, false);
    else if (cmd == "tbreak" || cmd == "tb") cmdBreak(args, true);
    else if (cmd == "delete" || cmd == "d")  cmdDelete(args);
    else if (cmd == "watch") {
        // watch <addr> [len] [w|rw|x]
        if (args.size() < 1) { err("usage: watch <addr> [len] [w|rw|x]"); return; }
        bool ok; addr_t a = parseAddr(args[0], &ok);
        if (!ok) { err("bad address"); return; }
        size_t len = args.size() > 1 ? strtoul(args[1].c_str(), nullptr, 0) : 8;
        DebuggerCore::WatchType wt = DebuggerCore::WatchType::Write;
        if (args.size() > 2) {
            if (args[2] == "rw") wt = DebuggerCore::WatchType::ReadWrite;
            else if (args[2] == "x") wt = DebuggerCore::WatchType::Execute;
        }
        int id = dbg_.addWatchpoint(a, len, wt);
        out("Watchpoint " + std::to_string(id) + " set at " + hex(a));
    }
    else if (cmd == "info" || cmd == "i") cmdInfo(args);
    else if (cmd == "x") cmdExamine(args);
    else if (cmd == "set") cmdSet(args);
    else if (cmd == "disas" || cmd == "dis") cmdDisas(args);
    else if (cmd == "edit")   cmdEdit(args);
    else if (cmd == "decomp" || cmd == "dec") cmdDecomp(args);
    else if (cmd == "pretty" || cmd == "pp") cmdPretty(args);
    else if (cmd == "script") cmdScript(args);
    else if (cmd == "backtrace" || cmd == "bt") cmdBacktrace(args);
    else if (cmd == "patch") cmdPatch(args);
    else if (cmd == "stealth") cmdStealth(args);
    else if (cmd == "kernel") cmdKernel(args);
    else if (cmd == "target") cmdTarget(args);
    else if (cmd == "syscall-step") {
        int nr; bool entry;
        if (dbg_.stepToSyscall(nr, entry)) {
            out(std::string("Syscall ") + (entry ? "entry" : "exit") + " nr=" + std::to_string(nr));
        } else err("syscall-step failed");
    }
    else if (cmd == "sleep") {
        if (args.empty()) { err("usage: sleep <milliseconds>"); return; }
        int ms = atoi(args[0].c_str());
        if (ms <= 0) { err("sleep: invalid duration"); return; }
        usleep(ms * 1000);
    }
    // v1.1.4: x64dbg-like features
    else if (cmd == "modules" || cmd == "mod") { cmdModules(args); }
    else if (cmd == "handles") { cmdHandles(args); }
    else if (cmd == "dump") { cmdDump(args); }
    else if (cmd == "find" || cmd == "search") { cmdFind(args); }
    else if (cmd == "findstr") { cmdFindStr(args); }
    else if (cmd == "flags") { cmdFlags(args); }
    else if (cmd == "setreg") { cmdSetReg(args); }
    else if (cmd == "memmap") { cmdMemMap(args); }
    else if (cmd == "trace") { cmdTrace(args); }
    else {
        err("Unknown command: " + cmd + " (type 'help')");
    }
}

// ===== Command handlers =====

void HeadlessCLI::cmdAttach(const std::vector<std::string>& args) {
    if (args.empty()) { err("usage: attach <pid>"); return; }
    pid_t p = (pid_t)atoi(args[0].c_str());
    if (dbg_.attach(p)) {
        out("Attached to pid " + std::to_string(p));
        dbg_.readMaps();  // refresh cache
    } else {
        err("Attach failed: " + dbg_.lastError());
    }
}

void HeadlessCLI::cmdLaunch(const std::vector<std::string>& args) {
    if (args.empty()) { err("usage: launch <binary> [args...]"); return; }
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (dbg_.attachByLaunch(args[0], rest)) {
        out("Launched " + args[0] + " (pid " + std::to_string(dbg_.pid()) + ")");
    } else {
        err("Launch failed: " + dbg_.lastError());
    }
}

void HeadlessCLI::cmdDetach(const std::vector<std::string>&) {
    if (dbg_.detach()) out("Detached");
    else err("Detach failed");
}

void HeadlessCLI::cmdKill(const std::vector<std::string>&) {
    if (dbg_.kill()) out("Target killed");
    else err("Kill failed");
}

void HeadlessCLI::cmdContinue(const std::vector<std::string>&) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    if (!dbg_.cont()) { err("cont failed"); return; }
    int sig; bool ex; int code;
    if (dbg_.waitForStop(sig, ex, code)) {
        if (ex) out("Target exited with code " + std::to_string(code));
        else out("Stopped — signal " + std::to_string(sig));
    }
}

void HeadlessCLI::cmdStep(const std::vector<std::string>&) {
    if (dbg_.step()) out("Stepped");
    else err("Step failed");
}

void HeadlessCLI::cmdNext(const std::vector<std::string>&) {
    if (dbg_.stepOver()) out("Stepped over");
    else err("Step-over failed");
}

void HeadlessCLI::cmdBreak(const std::vector<std::string>& args, bool temporary) {
    if (args.empty()) { err("usage: break <addr> [condition]"); return; }
    bool ok; addr_t a = parseAddr(args[0], &ok);
    if (!ok) { err("bad address"); return; }
    int id;
    std::string cond;
    if (args.size() > 1) {
        for (size_t i = 1; i < args.size(); i++) {
            if (i > 1) cond += " ";
            cond += args[i];
        }
    }
    if (cond.empty()) {
        id = temporary ? dbg_.addTempBreakpoint(a) : dbg_.addBreakpoint(a);
    } else {
        id = dbg_.addConditionalBreakpoint(a, cond);
    }
    if (id < 0) err("set breakpoint failed");
    else out("Breakpoint " + std::to_string(id) + " set at " + hex(a) +
             (temporary ? " (temporary)" : "") +
             (cond.empty() ? "" : " when " + cond));
}

void HeadlessCLI::cmdDelete(const std::vector<std::string>& args) {
    if (args.empty()) { err("usage: delete <id>"); return; }
    int id = atoi(args[0].c_str());
    if (dbg_.removeBreakpoint(id)) out("Deleted bp " + std::to_string(id));
    else err("Delete failed");
}

void HeadlessCLI::cmdInfo(const std::vector<std::string>& args) {
    if (args.empty()) {
        out("Usage: info <breakpoints|registers|threads|maps|target>");
        return;
    }
    if (args[0] == "breakpoints" || args[0] == "bp" || args[0] == "b") printBreakpoints();
    else if (args[0] == "registers" || args[0] == "reg" || args[0] == "r") printRegisters();
    else if (args[0] == "threads" || args[0] == "t") printThreads();
    else if (args[0] == "maps" || args[0] == "m") printMaps();
    else if (args[0] == "target") printTargetInfo();
    else err("Unknown info subcommand: " + args[0] + " (valid: b|bp|breakpoints|r|reg|registers|t|threads|m|maps|target)");
}

void HeadlessCLI::cmdExamine(const std::vector<std::string>& args) {
    // x /Nxb <addr>
    if (args.size() < 2) { err("usage: x /Nxb <addr>  (or  x /Nxw <addr>)"); return; }
    std::string fmt = args[0];  // /Nxb or /Nxw
    bool ok; addr_t a = parseAddr(args[1], &ok);
    if (!ok) { err("bad address"); return; }
    // Parse /Nxb or /Nxw
    int count = 16; int size = 1; bool word = false;
    if (fmt.size() > 1 && fmt[0] == '/') {
        // Find x
        size_t xpos = fmt.find('x');
        if (xpos != std::string::npos) {
            count = atoi(fmt.substr(1, xpos - 1).c_str());
            if (count <= 0) count = 16;
            if (xpos + 1 < fmt.size()) {
                char c = fmt[xpos + 1];
                if (c == 'w') { size = 4; word = true; }
                else if (c == 'g') { size = 8; word = true; }
                else if (c == 'h') { size = 2; word = true; }
            }
        }
    }
    auto bytes = dbg_.readMemoryVec(a, count * size);
    if (bytes.empty()) { err("read failed"); return; }
    for (int i = 0; i < count; i++) {
        if (i % 16 == 0) {
            if (i > 0) std::cout << "\n";
            std::cout << hex(a + i * size) << ": ";
        }
        if (word) {
            u64 v = 0;
            std::memcpy(&v, bytes.data() + i * size, size);
            std::cout << hex(v, size * 2) << " ";
        } else {
            std::cout << hex2(bytes[i], 2) << " ";
        }
    }
    std::cout << std::endl;
}

void HeadlessCLI::cmdSet(const std::vector<std::string>& args) {
    // set <addr> = <bytes>
    if (args.size() < 3 || args[1] != "=") {
        err("usage: set <addr> = <byte1> <byte2> ...");
        return;
    }
    bool ok; addr_t a = parseAddr(args[0], &ok);
    if (!ok) { err("bad address"); return; }
    std::vector<std::string> btokens(args.begin() + 2, args.end());
    auto bytes = parseBytes(btokens);
    if (dbg_.writeMemory(a, bytes.data(), bytes.size())) {
        out("Wrote " + std::to_string(bytes.size()) + " bytes at " + hex(a));
    } else err("write failed");
}

void HeadlessCLI::cmdDisas(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached. Use 'attach <pid>' first."); return; }
    addr_t addr = 0;
    size_t count = 20;
    // If no args, default to current RIP
    if (args.empty()) {
        RegisterSet r;
        if (!dbg_.readRegisters(r)) { err("readRegisters failed"); return; }
        addr = r.rip;
    } else {
        bool ok; addr = parseAddr(args[0], &ok);
        if (!ok) { err("bad address"); return; }
        if (args.size() > 1) count = (size_t)strtoul(args[1].c_str(), nullptr, 0);
    }
    // Read enough bytes
    auto bytes = dbg_.readMemoryVec(addr, count * 15 + 32);
    if (bytes.empty()) { err("readMemory failed at " + hex(addr)); return; }
    auto instrs = disas_.disassemble(addr, bytes.data(), bytes.size(), count);
    if (instrs.empty()) { err("no instructions decoded"); return; }
    // Header
    std::cout << "ADDRESS             BYTES               MNEMONIC OPERANDS\n";
    std::cout << "------------------- ------------------- ------------------------------\n";
    RegisterSet regs;
    bool have_regs = dbg_.readRegisters(regs);
    for (auto& ins : instrs) {
        bool at_rip = have_regs && (ins.address == regs.rip);
        std::string line = Disassembler::format(ins, true);
        // Annotate with symbol if it's a call/jmp
        if (ins.has_branch_target && ins.branch_target != 0) {
            std::string sym = resolveSymbol(ins.branch_target);
            if (!sym.empty()) {
                line += "  ; " + sym;
            }
        }
        if (at_rip) {
            std::cout << "\x1b[33m>> " << line << "\x1b[0m\n";  // yellow highlight
        } else {
            std::cout << "   " << line << "\n";
        }
    }
}

void HeadlessCLI::cmdEdit(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached. Use 'attach <pid>' first."); return; }
    addr_t addr = 0;
    if (args.empty()) {
        RegisterSet r;
        if (!dbg_.readRegisters(r)) { err("readRegisters failed"); return; }
        addr = r.rip;
    } else {
        bool ok; addr = parseAddr(args[0], &ok);
        if (!ok) { err("bad address"); return; }
    }
    out("Launching interactive memory editor at " + hex(addr) + "  (press 'q' to exit, 'h' for help)");
    // Create the editor and run it
    editor_ = std::make_unique<InteractiveMemoryEditor>(dbg_);
    editor_->run(addr, InteractiveMemoryEditor::Mode::Hex);
    editor_.reset();
    out("Returned from editor.");
}

void HeadlessCLI::cmdDecomp(const std::vector<std::string>& args) {
    if (args.empty()) {
        // No args: decompile function at current RIP (live process)
        if (dbg_.pid() == 0) { printDecompStatus(); return; }
        RegisterSet r;
        if (!dbg_.readRegisters(r)) { err("readRegisters failed"); return; }
        out("Decompiling function at current RIP = " + hex(r.rip) + " ...");
        auto res = decompiler_.decompileFunction(dbg_, r.rip, 0x1000);
        if (!res.ok) {
            err("Decompile failed: " + res.error);
            if (res.error.find("not available") != std::string::npos ||
                res.error.find("No decompiler") != std::string::npos) {
                err("Run 'decomp list' to see backend status, or 'decomp api' for install hints.");
            }
            return;
        }
        out("Backend: " + res.backend_used + "  Elapsed: " + std::to_string((int)res.elapsed_ms) + " ms");
        out("Function: " + res.function_name);
        std::cout << "\n" << res.c_code << std::endl;
        return;
    }
    std::string sub = args[0];

    // decomp list
    if (sub == "list") {
        printDecompStatus();
        return;
    }
    // decomp api
    if (sub == "api") {
        std::cout << Decompiler::apiDocs() << std::endl;
        return;
    }
    // decomp set <backend>
    if (sub == "set") {
        if (args.size() < 2) { err("usage: decomp set <retdec-native|retdec-subprocess|angr|auto>"); return; }
        std::string b = args[1];
        if (b == "auto")              decompiler_.setBackend(Decompiler::Backend::Auto);
        else if (b == "retdec-native")decompiler_.setBackend(Decompiler::Backend::RetDecNative);
        else if (b == "retdec-subprocess") decompiler_.setBackend(Decompiler::Backend::RetDecSubprocess);
        else if (b == "retdec")       decompiler_.setBackend(Decompiler::Backend::RetDecNative);
        else if (b == "angr")         decompiler_.setBackend(Decompiler::Backend::Angr);
        else { err("unknown backend: " + b); return; }
        out("Decompiler backend set to: " + b);
        return;
    }
    // decomp file <binary> [addr]
    if (sub == "file") {
        if (args.size() < 2) {
            err("usage: decomp file <binary> [addr]");
            return;
        }
        std::string bin = args[1];
        if (args.size() >= 3) {
            // Decompile one function in a file
            bool ok; addr_t a = parseAddr(args[2], &ok);
            if (!ok) { err("bad address: " + args[2]); return; }
            out("Decompiling function at " + hex(a) + " in " + bin + " ...");
            auto r = decompiler_.decompileFunctionInFile(bin, a);
            if (!r.ok) { err("Decompile failed: " + r.error); return; }
            out("Backend: " + r.backend_used + "  Elapsed: " + std::to_string((int)r.elapsed_ms) + " ms");
            out("Function: " + r.function_name);
            std::cout << "\n" << r.c_code << std::endl;
        } else {
            // Decompile whole file
            out("Decompiling entire file: " + bin + " ...");
            auto r = decompiler_.decompileFile(bin);
            if (!r.ok) { err("Decompile failed: " + r.error); return; }
            out("Backend: " + r.backend_used + "  Elapsed: " + std::to_string((int)r.elapsed_ms) + " ms");
            std::cout << "\n" << r.c_code << std::endl;
        }
        return;
    }

    // decomp <addr> [max_bytes]  - decompile function in live process
    bool ok; addr_t addr = parseAddr(sub, &ok);
    if (!ok) { err("unknown decomp subcommand: " + sub + "\n  usage: decomp [addr|file|list|api|set]"); return; }
    if (dbg_.pid() == 0) { err("Not attached. Use 'attach <pid>' first."); return; }
    size_t max_bytes = 0x1000;
    if (args.size() > 1) {
        max_bytes = (size_t)strtoull(args[1].c_str(), nullptr, 0);
        if (max_bytes == 0) max_bytes = 0x1000;
    }
    out("Decompiling function at " + hex(addr) + " (max " + std::to_string(max_bytes) + " bytes) ...");
    auto r = decompiler_.decompileFunction(dbg_, addr, max_bytes);
    if (!r.ok) {
        err("Decompile failed: " + r.error);
        if (r.error.find("not available") != std::string::npos ||
            r.error.find("No decompiler") != std::string::npos) {
            err("Run 'decomp list' to see backend status, or 'decomp api' for install hints.");
        }
        return;
    }
    out("Backend: " + r.backend_used + "  Elapsed: " + std::to_string((int)r.elapsed_ms) + " ms");
    out("Function: " + r.function_name);
    std::cout << "\n" << r.c_code << std::endl;
}

void HeadlessCLI::printDecompStatus() {
    out("Decompiler backend status:");
    out(std::string("  retdec-native:     ") +
        (decompiler_.isRetDecNativeAvailable() ? "[AVAILABLE]" : "[NOT INSTALLED]"));
    out(std::string("  retdec-subprocess: ") +
        (decompiler_.isRetDecSubprocessAvailable() ? "[AVAILABLE]" : "[NOT INSTALLED]"));
    out(std::string("  angr:              ") +
        (decompiler_.isAngrAvailable() ? "[AVAILABLE]" : "[NOT INSTALLED]"));
    auto avail = decompiler_.detectAvailable();
    out(std::string("  current selection: ") + Decompiler::backendName(decompiler_.currentBackend()) +
        "  (auto-detected: " + Decompiler::backendName(avail) + ")");
    out("");
    out("Available commands:");
    out("  decomp <addr>                Decompile function at addr (live process)");
    out("  decomp <addr> <max_bytes>    Limit input size (default 4096)");
    out("  decomp file <binary>         Decompile whole binary file");
    out("  decomp file <binary> <addr>  Decompile one function in a file");
    out("  decomp list                  Show this status");
    out("  decomp api                   Show install hints + API docs");
    out("  decomp set <backend>         Force backend (auto/retdec-native/retdec-subprocess/angr)");
}

void HeadlessCLI::cmdPretty(const std::vector<std::string>& args) {
    if (args.empty()) {
        out("Active language: " + PrettyPrinter::languageName(pretty_.currentLanguage()));
        out("Available: c, cpp, rust, go, python");
        out("Use 'pretty api' for full documentation.");
        return;
    }
    std::string sub = args[0];

    if (sub == "list") {
        out("Pretty printers available:");
        for (auto l : PrettyPrinter::allLanguages()) {
            out("  " + PrettyPrinter::languageName(l));
        }
        out("Current: " + PrettyPrinter::languageName(pretty_.currentLanguage()));
        return;
    }
    if (sub == "api") {
        std::cout << PrettyPrinter::apiDocs() << std::endl;
        return;
    }
    if (sub == "set") {
        if (args.size() < 2) { err("usage: pretty set <c|cpp|rust|go|python|none>"); return; }
        std::string lang = args[1];
        if (lang == "c" || lang == "C")             pretty_.setLanguage(PrettyPrinter::Language::C);
        else if (lang == "cpp" || lang == "c++" || lang == "C++") pretty_.setLanguage(PrettyPrinter::Language::Cpp);
        else if (lang == "rust" || lang == "rs")    pretty_.setLanguage(PrettyPrinter::Language::Rust);
        else if (lang == "go" || lang == "golang")  pretty_.setLanguage(PrettyPrinter::Language::Go);
        else if (lang == "python" || lang == "py")  pretty_.setLanguage(PrettyPrinter::Language::Python);
        else if (lang == "none" || lang == "off")   pretty_.setLanguage(PrettyPrinter::Language::NoLanguage);
        else { err("unknown language: " + lang); return; }
        out("Pretty printer language set to: " + lang);
        return;
    }

    // All remaining subcommands need an attached process
    if (dbg_.pid() == 0) { err("Not attached. Use 'attach <pid>' first."); return; }

    if (sub == "auto") {
        if (args.size() < 2) { err("usage: pretty auto <addr>"); return; }
        bool ok; addr_t a = parseAddr(args[1], &ok);
        if (!ok) { err("bad address"); return; }
        out(pretty_.autoPrint(dbg_, a));
        return;
    }
    if (sub == "cstring") {
        if (args.size() < 2) { err("usage: pretty cstring <addr>"); return; }
        bool ok; addr_t a = parseAddr(args[1], &ok);
        if (!ok) { err("bad address"); return; }
        out(pretty_.printCString(dbg_, a));
        return;
    }
    if (sub == "cpp_string" || sub == "cppstring" || sub == "stdstring") {
        if (args.size() < 2) { err("usage: pretty cpp_string <addr>"); return; }
        bool ok; addr_t a = parseAddr(args[1], &ok);
        if (!ok) { err("bad address"); return; }
        out(pretty_.printCppString(dbg_, a));
        return;
    }
    if (sub == "rust_string" || sub == "ruststring") {
        if (args.size() < 2) { err("usage: pretty rust_string <addr>"); return; }
        bool ok; addr_t a = parseAddr(args[1], &ok);
        if (!ok) { err("bad address"); return; }
        out(pretty_.printRustString(dbg_, a));
        return;
    }
    if (sub == "go_string" || sub == "gostring") {
        if (args.size() < 2) { err("usage: pretty go_string <addr>"); return; }
        bool ok; addr_t a = parseAddr(args[1], &ok);
        if (!ok) { err("bad address"); return; }
        out(pretty_.printGoString(dbg_, a));
        return;
    }
    if (sub == "py_string" || sub == "pystring" || sub == "python_string") {
        if (args.size() < 2) { err("usage: pretty py_string <addr>"); return; }
        bool ok; addr_t a = parseAddr(args[1], &ok);
        if (!ok) { err("bad address"); return; }
        out(pretty_.printPyString(dbg_, a));
        return;
    }
    if (sub == "struct") {
        if (args.size() < 3) {
            err("usage: pretty struct <addr> <descriptor>");
            err("  descriptor: comma-separated types, e.g. i32,str,ptr,u64");
            err("  valid types: i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 ptr str hex<N>");
            return;
        }
        bool ok; addr_t a = parseAddr(args[1], &ok);
        if (!ok) { err("bad address"); return; }
        // Reconstruct descriptor (may contain commas that were split)
        std::string desc;
        for (size_t i = 2; i < args.size(); i++) {
            if (i > 2) desc += ",";
            desc += args[i];
        }
        std::cout << pretty_.printStruct(dbg_, a, desc) << std::endl;
        return;
    }
    err("unknown pretty subcommand: " + sub + " (valid: set|list|api|auto|cstring|cpp_string|rust_string|go_string|py_string|struct)");
}

void HeadlessCLI::cmdScript(const std::vector<std::string>& args) {
    if (args.empty()) { printScriptStatus(); return; }
    std::string sub = args[0];
    if (sub == "list") {
        printScriptStatus();
    } else if (sub == "api") {
        std::cout << ScriptEngine::apiDocs() << std::endl;
    } else if (sub == "run") {
        if (args.size() < 3) {
            err("usage: script run <lua|python> <file-or-code>");
            err("  If the third arg starts with a path separator or ./, treat");
            err("  as a file. Otherwise treat as inline code.");
            return;
        }
        std::string lang_str = args[1];
        std::string payload = args[2];
        ScriptEngine::Lang lang;
        if (lang_str == "lua" || lang_str == "Lua") lang = ScriptEngine::Lang::Lua;
        else if (lang_str == "python" || lang_str == "py" || lang_str == "Python") lang = ScriptEngine::Lang::Python;
        else { err("Unknown language: " + lang_str + " (use 'lua' or 'python')"); return; }

        if (!script_->isAvailable(lang)) {
            err(script_->installHint(lang));
            return;
        }

        // If payload looks like a file path that exists, read it; else treat as code
        std::string code;
        std::ifstream f(payload);
        if (f) {
            std::stringstream ss; ss << f.rdbuf();
            code = ss.str();
            out("Running " + lang_str + " script: " + payload);
        } else {
            code = payload;
            out("Running inline " + lang_str + " code");
        }

        std::string err_str;
        if (script_->runString(lang, code, err_str)) {
            out("Script completed.");
        } else {
            err("Script error: " + err_str);
        }
    } else {
        err("usage: script <list|run|api>");
    }
}

std::string HeadlessCLI::resolveSymbol(addr_t a) {
    auto maps = dbg_.readMaps();
    for (auto& m : maps) {
        if (a >= m.start && a < m.end) {
            size_t slash = m.path.rfind('/');
            std::string bn = (slash != std::string::npos) ? m.path.substr(slash + 1) : m.path;
            return "<" + bn + "+0x" + hex2(a - m.start, 0) + ">";
        }
    }
    return "";
}

void HeadlessCLI::printScriptStatus() {
    out("Scripting backends:");
    out(std::string("  Lua:    ") + (script_->isAvailable(ScriptEngine::Lang::Lua) ? "[AVAILABLE]" : "[NOT INSTALLED]"));
    out(std::string("  Python: ") + (script_->isAvailable(ScriptEngine::Lang::Python) ? "[AVAILABLE]" : "[NOT INSTALLED]"));
    out("");
    out("Available commands:");
    out("  script list                Show this status");
    out("  script api                 Print API documentation");
    out("  script run lua <file>      Run a Lua script");
    out("  script run python <file>   Run a Python script");
    out("  script run lua \"code\"      Run inline Lua code");
    out("  script run python \"code\"  Run inline Python code");
}

void HeadlessCLI::cmdBacktrace(const std::vector<std::string>&) {
    printBacktrace();
}

void HeadlessCLI::cmdPatch(const std::vector<std::string>& args) {
    if (args.empty()) { err("usage: patch <list|apply|nop|save|undo|info>"); return; }
    std::string sub = args[0];
    if (sub == "list") {
        printPatchList();
    } else if (sub == "info") {
        if (!patcher_.isLoaded()) { err("No target loaded. Use 'target <binary>'"); return; }
        out("Target: " + patcher_.formatName());
        out("Image size: " + std::to_string(patcher_.imageSize()) + " bytes");
        out("Entry point: " + hex(patcher_.entryPoint()));
        out("Sections: " + std::to_string(patcher_.sections().size()));
    } else if (sub == "nop") {
        if (args.size() < 3) { err("usage: patch nop <offset> <length>"); return; }
        u64 off = strtoull(args[1].c_str(), nullptr, 0);
        size_t len = strtoul(args[2].c_str(), nullptr, 0);
        int id = patcher_.nopRange(off, len);
        if (id < 0) err("patch failed: " + patcher_.lastError());
        else out("Patch " + std::to_string(id) + ": NOPed " + std::to_string(len) + " bytes at " + hex(off));
    } else if (sub == "apply") {
        if (args.size() < 3) { err("usage: patch apply <offset> <kind> [bytes...]"); return; }
        u64 off = strtoull(args[1].c_str(), nullptr, 0);
        std::string kind = args[2];
        BinaryPatcher::PatchKind k = BinaryPatcher::PatchKind::CustomBytes;
        if (kind == "nop") k = BinaryPatcher::PatchKind::NOP;
        else if (kind == "jmp") k = BinaryPatcher::PatchKind::JmpAlways;
        else if (kind == "nojmp") k = BinaryPatcher::PatchKind::JmpNever;
        else if (kind == "callnop") k = BinaryPatcher::PatchKind::CallToNop;
        else if (kind == "rettrue") k = BinaryPatcher::PatchKind::RetTrue;
        else if (kind == "ascii") k = BinaryPatcher::PatchKind::AsciiReplace;
        size_t len = args.size() > 3 ? args.size() - 3 : 1;
        std::vector<u8> bytes;
        if (args.size() > 3) {
            std::vector<std::string> btokens(args.begin() + 3, args.end());
            bytes = parseBytes(btokens);
            len = bytes.size();
        }
        int id = patcher_.applyPatch(off, k, len, bytes);
        if (id < 0) err("patch failed: " + patcher_.lastError());
        else out("Patch " + std::to_string(id) + " applied at " + hex(off));
    } else if (sub == "save") {
        if (args.size() < 2) { err("usage: patch save <outfile>"); return; }
        if (patcher_.save(args[1])) out("Saved patched binary to " + args[1]);
        else err("save failed: " + patcher_.lastError());
    } else if (sub == "undo") {
        // patch undo [id]  — if no id, undo the last applied patch
        if (args.size() < 2) {
            // Undo the last patch
            auto& patches = patcher_.patches();
            if (patches.empty()) { err("No patches to undo"); return; }
            int last = (int)patches.size() - 1;
            if (patcher_.undoPatch(last)) out("Undone patch " + std::to_string(last) + " (last)");
            else err("undo failed");
            return;
        }
        int id = atoi(args[1].c_str());
        if (id < 0 || id >= (int)patcher_.patches().size()) {
            err("Invalid patch id " + std::to_string(id) + " — valid range: 0.." +
                std::to_string(patcher_.patches().size() - 1) + " (use 'patch list' to see IDs)");
            return;
        }
        if (patcher_.undoPatch(id)) out("Undone patch " + std::to_string(id));
        else err("undo failed");
    } else {
        err("Unknown patch subcommand: " + sub);
    }
}

void HeadlessCLI::cmdStealth(const std::vector<std::string>& args) {
    if (args.empty()) { printStealthStatus(); return; }
    std::string sub = args[0];
    if (sub == "list") {
        printStealthStatus();
    } else if (sub == "on" || sub == "off") {
        if (args.size() < 2) { err("usage: stealth on|off <name>"); return; }
        std::string nm = args[1];
        for (auto t : AntiDetect::allTechniques()) {
            std::string tn = AntiDetect::name(t);
            // case-insensitive substring match
            std::string lower_tn = tn, lower_nm = nm;
            for (auto& c : lower_tn) c = tolower(c);
            for (auto& c : lower_nm) c = tolower(c);
            if (lower_tn.find(lower_nm) != std::string::npos) {
                if (sub == "on") anti_.enable(t);
                else anti_.disable(t);
                out("Stealth: " + tn + " = " + (sub == "on" ? "ON" : "OFF"));
                return;
            }
        }
        err("No stealth technique matching '" + nm + "'");
    } else {
        err("usage: stealth [list|on <name>|off <name>]");
    }
}

void HeadlessCLI::cmdKernel(const std::vector<std::string>& args) {
    if (args.empty()) { printKernelStatus(); return; }
    std::string sub = args[0];
    if (sub == "status") printKernelStatus();
    else if (sub == "load") {
        out("Building kernel module...");
        std::string path = kernel_.buildKernelModule();
        if (path.empty()) {
            err("Build failed. Install kernel headers: sudo apt-get install linux-headers-$(uname -r)");
        } else {
            out("Module built: " + path);
            out("Loading (requires root)...");
            if (kernel_.loadModule()) out("Module loaded. /proc/ninja_stealth available.");
            else err("Load failed. Try: sudo insmod " + path);
        }
    } else if (sub == "unload") {
        if (kernel_.unloadModule()) out("Module unloaded");
        else err("Unload failed");
    } else {
        err("usage: kernel [status|load|unload]");
    }
}

void HeadlessCLI::cmdTarget(const std::vector<std::string>& args) {
    if (args.empty()) { err("usage: target <binary>"); return; }
    if (patcher_.load(args[0])) {
        out("Loaded: " + args[0]);
        out("Format: " + patcher_.formatName());
        out("Size: " + std::to_string(patcher_.imageSize()) + " bytes");
        out("Entry: " + hex(patcher_.entryPoint()));
        out("Sections: " + std::to_string(patcher_.sections().size()));
    } else err("load failed: " + patcher_.lastError());
}

// ===== v1.1.4: x64dbg-like features =====

void HeadlessCLI::cmdModules(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    auto maps = dbg_.readMaps();
    // Group by path (each unique path = one module)
    std::map<std::string, std::pair<addr_t, addr_t>> modules;
    for (auto& m : maps) {
        if (m.path.empty()) continue;
        auto it = modules.find(m.path);
        if (it == modules.end()) {
            modules[m.path] = {m.start, m.end};
        } else {
            it->second.first = std::min(it->second.first, m.start);
            it->second.second = std::max(it->second.second, m.end);
        }
    }
    out("BASE               END                SIZE       MODULE");
    out("------------------ ------------------ ---------- --------------------------------");
    for (auto& kv : modules) {
        size_t sz = kv.second.second - kv.second.first;
        // Get basename
        std::string bn = kv.first;
        size_t slash = bn.rfind('/');
        if (slash != std::string::npos) bn = bn.substr(slash + 1);
        std::cout << hex(kv.second.first) << " " << hex(kv.second.second) << " "
                  << std::setw(10) << std::hex << sz << " " << kv.first << std::endl;
    }
    out("\n" + std::to_string(modules.size()) + " modules loaded");
}

void HeadlessCLI::cmdHandles(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    // Linux: read /proc/<pid>/fd
    std::string fd_path = "/proc/" + std::to_string(dbg_.pid()) + "/fd";
    out("FD   TARGET");
    out("---- --------------------------------------------------");
    int count = 0;
    std::string cmd = "ls -la " + fd_path + "/ 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe)) {
            // Parse ls -la output: skip "total" and "." and ".."
            std::string line(buf);
            if (line.empty() || line[0] == 't' || line.find(" -> ") == std::string::npos) continue;
            size_t arrow = line.find(" -> ");
            if (arrow == std::string::npos) continue;
            // Extract fd number from the filename column
            size_t fd_start = line.rfind(' ', arrow - 1);
            if (fd_start == std::string::npos) continue;
            std::string fd_str = line.substr(fd_start + 1, arrow - fd_start - 1);
            std::string target = line.substr(arrow + 4);
            // Trim trailing newline
            if (!target.empty() && target.back() == '\n') target.pop_back();
            std::cout << std::left << std::setw(5) << fd_str << " " << target << std::endl;
            count++;
        }
        pclose(pipe);
    }
    out("\n" + std::to_string(count) + " handles open");
}

void HeadlessCLI::cmdDump(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    if (args.size() < 3) {
        err("usage: dump <addr> <size> <filename>");
        err("  Dumps <size> bytes from <addr> to <filename>");
        return;
    }
    bool ok;
    addr_t addr = parseAddr(args[0], &ok);
    if (!ok) { err("bad address"); return; }
    size_t size = (size_t)strtoull(args[1].c_str(), nullptr, 0);
    if (size == 0 || size > 0x10000000) { err("invalid size (max 256MB)"); return; }

    auto data = dbg_.readMemoryVec(addr, size);
    if (data.empty()) { err("readMemory failed at " + hex(addr)); return; }

    std::ofstream f(args[2], std::ios::binary);
    if (!f) { err("cannot open file: " + args[2]); return; }
    f.write((const char*)data.data(), data.size());
    f.close();
    out("Dumped " + std::to_string(data.size()) + " bytes from " + hex(addr) + " to " + args[2]);
}

void HeadlessCLI::cmdFind(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    if (args.size() < 3) {
        err("usage: find <start_addr> <end_addr> <byte1> <byte2> [??] ...");
        err("  Use ?? as wildcard for any byte");
        err("  Example: find 0x400000 0x500000 48 89 E5 ?? C3");
        return;
    }
    bool ok;
    addr_t start = parseAddr(args[0], &ok);
    if (!ok) { err("bad start address"); return; }
    addr_t end = parseAddr(args[1], &ok);
    if (!ok) { err("bad end address"); return; }
    if (end <= start) { err("end must be > start"); return; }

    // Parse pattern (supports ?? wildcards)
    std::vector<std::pair<bool, u8>> pattern; // (is_wildcard, value)
    for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "??" || args[i] == "*") {
            pattern.push_back({true, 0});
        } else {
            unsigned long val = strtoul(args[i].c_str(), nullptr, 0);
            if (val > 0xFF) { err("byte value out of range: " + args[i]); return; }
            pattern.push_back({false, (u8)val});
        }
    }
    if (pattern.empty()) { err("no pattern specified"); return; }

    // Read memory in chunks and search
    size_t total_size = end - start;
    size_t chunk_size = std::min((size_t)0x100000, total_size); // 1MB chunks
    size_t found = 0;
    size_t max_results = 1000;

    for (addr_t base = start; base < end && found < max_results; base += chunk_size) {
        size_t read_size = std::min(chunk_size + pattern.size(), end - base);
        auto data = dbg_.readMemoryVec(base, read_size);
        if (data.empty()) continue;

        for (size_t i = 0; i + pattern.size() <= data.size() && found < max_results; i++) {
            bool match = true;
            for (size_t j = 0; j < pattern.size(); j++) {
                if (!pattern[j].first && data[i + j] != pattern[j].second) {
                    match = false;
                    break;
                }
            }
            if (match) {
                addr_t hit_addr = base + i;
                std::cout << "  " << hex(hit_addr) << ": ";
                for (size_t j = 0; j < pattern.size() && j < 16; j++) {
                    printf("%02x ", data[i + j]);
                }
                std::cout << std::endl;
                found++;
            }
        }
        // Back up to avoid missing matches at chunk boundaries
        if (base + chunk_size < end) base -= pattern.size();
    }
    out("\nFound " + std::to_string(found) + (found >= max_results ? "+ matches (max results reached)" : " matches"));
}

void HeadlessCLI::cmdFindStr(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    if (args.size() < 3) {
        err("usage: findstr <start_addr> <end_addr> <string>");
        err("  Searches for ASCII string in memory");
        return;
    }
    bool ok;
    addr_t start = parseAddr(args[0], &ok);
    if (!ok) { err("bad start address"); return; }
    addr_t end = parseAddr(args[1], &ok);
    if (!ok) { err("bad end address"); return; }

    // Reconstruct the search string (may contain spaces)
    std::string needle;
    for (size_t i = 2; i < args.size(); i++) {
        if (i > 2) needle += " ";
        needle += args[i];
    }
    if (needle.empty()) { err("empty search string"); return; }

    std::vector<u8> pattern(needle.begin(), needle.end());
    size_t total_size = end - start;
    size_t chunk_size = std::min((size_t)0x100000, total_size);
    size_t found = 0;
    size_t max_results = 1000;

    for (addr_t base = start; base < end && found < max_results; base += chunk_size) {
        size_t read_size = std::min(chunk_size + pattern.size(), end - base);
        auto data = dbg_.readMemoryVec(base, read_size);
        if (data.empty()) continue;

        for (size_t i = 0; i + pattern.size() <= data.size() && found < max_results; i++) {
            if (memcmp(data.data() + i, pattern.data(), pattern.size()) == 0) {
                addr_t hit_addr = base + i;
                // Read context around the hit
                size_t ctx_start = (i >= 16) ? i - 16 : 0;
                size_t ctx_end = std::min(i + pattern.size() + 16, data.size());
                std::string ctx_str;
                for (size_t j = ctx_start; j < ctx_end; j++) {
                    ctx_str += (data[j] >= 32 && data[j] < 127) ? (char)data[j] : '.';
                }
                std::cout << "  " << hex(hit_addr) << ": ..." << ctx_str << "..." << std::endl;
                found++;
            }
        }
        if (base + chunk_size < end) base -= pattern.size();
    }
    out("\nFound " + std::to_string(found) + (found >= max_results ? "+ matches" : " matches"));
}

void HeadlessCLI::cmdFlags(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    RegisterSet r;
    if (!dbg_.readRegisters(r)) { err("readRegisters failed"); return; }
    u64 flags = r.rflags;
    out("RFLAGS = " + hex(flags));
    out("");
    out("  CF (Carry)               = " + std::string((flags & 0x001) ? "1" : "0"));
    out("  PF (Parity)              = " + std::string((flags & 0x004) ? "1" : "0"));
    out("  AF (Auxiliary Carry)     = " + std::string((flags & 0x010) ? "1" : "0"));
    out("  ZF (Zero)                = " + std::string((flags & 0x040) ? "1" : "0"));
    out("  SF (Sign)                = " + std::string((flags & 0x080) ? "1" : "0"));
    out("  TF (Trap)                = " + std::string((flags & 0x100) ? "1" : "0"));
    out("  IF (Interrupt Enable)    = " + std::string((flags & 0x200) ? "1" : "0"));
    out("  DF (Direction)           = " + std::string((flags & 0x400) ? "1" : "0"));
    out("  OF (Overflow)            = " + std::string((flags & 0x800) ? "1" : "0"));
    out("");
    out("  IOPL (I/O Privilege)     = " + std::to_string((flags >> 12) & 3));
    out("  NT (Nested Task)         = " + std::string((flags & 0x4000) ? "1" : "0"));
    out("  RF (Resume Flag)         = " + std::string((flags & 0x10000) ? "1" : "0"));
    out("  VM (Virtual 8086)        = " + std::string((flags & 0x20000) ? "1" : "0"));
    out("  AC (Alignment Check)     = " + std::string((flags & 0x40000) ? "1" : "0"));
    out("  VIF (Virtual Interrupt)  = " + std::string((flags & 0x80000) ? "1" : "0"));
    out("  VIP (Virtual IP)         = " + std::string((flags & 0x100000) ? "1" : "0"));
    out("  ID (CPUID available)     = " + std::string((flags & 0x200000) ? "1" : "0"));
}

void HeadlessCLI::cmdSetReg(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    if (args.size() < 3 || args[1] != "=") {
        err("usage: setreg <regname> = <value>");
        err("  Example: setreg rax = 0xDEADBEEF");
        err("  Example: setreg rip = 0x401000");
        err("  Example: setreg zf = 1   (sets zero flag)");
        return;
    }
    RegisterSet r;
    if (!dbg_.readRegisters(r)) { err("readRegisters failed"); return; }

    std::string name = args[0];
    for (auto& c : name) c = tolower(c);
    bool ok;
    u64 val = parseAddr(args[2], &ok);
    if (!ok) { err("bad value"); return; }

    // Individual flag setting
    if (name == "cf") { r.rflags = (r.rflags & ~0x001ULL) | (val ? 0x001 : 0); goto write; }
    if (name == "pf") { r.rflags = (r.rflags & ~0x004ULL) | (val ? 0x004 : 0); goto write; }
    if (name == "af") { r.rflags = (r.rflags & ~0x010ULL) | (val ? 0x010 : 0); goto write; }
    if (name == "zf") { r.rflags = (r.rflags & ~0x040ULL) | (val ? 0x040 : 0); goto write; }
    if (name == "sf") { r.rflags = (r.rflags & ~0x080ULL) | (val ? 0x080 : 0); goto write; }
    if (name == "tf") { r.rflags = (r.rflags & ~0x100ULL) | (val ? 0x100 : 0); goto write; }
    if (name == "df") { r.rflags = (r.rflags & ~0x400ULL) | (val ? 0x400 : 0); goto write; }
    if (name == "of") { r.rflags = (r.rflags & ~0x800ULL) | (val ? 0x800 : 0); goto write; }
    if (name == "rflags" || name == "eflags") { r.rflags = val; goto write; }

    if      (name == "rax") r.rax = val;
    else if (name == "rbx") r.rbx = val;
    else if (name == "rcx") r.rcx = val;
    else if (name == "rdx") r.rdx = val;
    else if (name == "rsi") r.rsi = val;
    else if (name == "rdi") r.rdi = val;
    else if (name == "rbp") r.rbp = val;
    else if (name == "rsp") r.rsp = val;
    else if (name == "r8")  r.r8  = val;
    else if (name == "r9")  r.r9  = val;
    else if (name == "r10") r.r10 = val;
    else if (name == "r11") r.r11 = val;
    else if (name == "r12") r.r12 = val;
    else if (name == "r13") r.r13 = val;
    else if (name == "r14") r.r14 = val;
    else if (name == "r15") r.r15 = val;
    else if (name == "rip") r.rip = val;
    else { err("unknown register: " + name); return; }

write:
    if (dbg_.writeRegisters(r)) {
        out(name + " = " + hex(val));
    } else err("writeRegisters failed");
}

void HeadlessCLI::cmdMemMap(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    auto maps = dbg_.readMaps();
    out("START              END                PERMS  OFFSET     PATH");
    out("------------------ ------------------ -----  ---------- --------------------------------");
    u64 total_r = 0, total_rw = 0, total_rx = 0;
    for (auto& m : maps) {
        std::string perms;
        perms += m.read  ? 'r' : '-';
        perms += m.write ? 'w' : '-';
        perms += m.exec  ? 'x' : '-';
        perms += m.shared? 's' : 'p';
        u64 sz = m.end - m.start;
        if (m.read) total_r += sz;
        if (m.read && m.write) total_rw += sz;
        if (m.read && m.exec) total_rx += sz;
        std::cout << hex(m.start) << " " << hex(m.end) << " " << perms << "  "
                  << hex(m.offset, 8) << " " << (m.path.empty() ? "[anonymous]" : m.path) << std::endl;
    }
    out("");
    out("Total readable:       " + std::to_string(total_r / 1024) + " KB");
    out("Total read-write:     " + std::to_string(total_rw / 1024) + " KB");
    out("Total read-execute:   " + std::to_string(total_rx / 1024) + " KB");
    out(std::to_string(maps.size()) + " memory regions");
}

void HeadlessCLI::cmdTrace(const std::vector<std::string>& args) {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    if (args.empty()) {
        err("usage: trace <on|off|run <count>|save <file>>");
        err("  trace on          Enable run trace");
        err("  trace off         Disable run trace");
        err("  trace run <N>     Single-step N instructions, logging each");
        err("  trace save <file> Save trace log to file");
        return;
    }
    static bool tracing = false;
    static std::vector<std::string> trace_log;

    if (args[0] == "on") {
        tracing = true;
        trace_log.clear();
        out("Run trace enabled. Use 'trace run <N>' to execute.");
    } else if (args[0] == "off") {
        tracing = false;
        out("Run trace disabled. " + std::to_string(trace_log.size()) + " entries logged.");
    } else if (args[0] == "run") {
        if (args.size() < 2) { err("usage: trace run <count>"); return; }
        int count = atoi(args[1].c_str());
        if (count <= 0 || count > 100000) { err("invalid count (1-100000)"); return; }
        out("Tracing " + std::to_string(count) + " instructions...");
        for (int i = 0; i < count; i++) {
            RegisterSet r;
            if (!dbg_.readRegisters(r)) { err("readRegisters failed"); break; }
            auto bytes = dbg_.readMemoryVec(r.rip, 15);
            auto instrs = disas_.disassemble(r.rip, bytes.data(), bytes.size(), 1);
            if (!instrs.empty()) {
                std::ostringstream entry;
                entry << "#" << std::setw(6) << std::setfill('0') << i << "  "
                      << hex(r.rip) << "  "
                      << "rax=" << hex(r.rax) << " rbx=" << hex(r.rbx)
                      << " rcx=" << hex(r.rcx) << " rdx=" << hex(r.rdx)
                      << "  " << instrs[0].text;
                trace_log.push_back(entry.str());
            }
            if (!dbg_.step()) { out("Target exited during trace at step " + std::to_string(i)); break; }
        }
        out("Trace complete: " + std::to_string(trace_log.size()) + " entries logged.");
    } else if (args[0] == "save") {
        if (args.size() < 2) { err("usage: trace save <filename>"); return; }
        std::ofstream f(args[1]);
        if (!f) { err("cannot open file: " + args[1]); return; }
        for (auto& entry : trace_log) f << entry << "\n";
        f.close();
        out("Saved " + std::to_string(trace_log.size()) + " trace entries to " + args[1]);
    } else {
        err("unknown trace subcommand: " + args[0]);
    }
}

void HeadlessCLI::cmdHelp(const std::vector<std::string>& args) {
    // If a specific command is requested, show detailed help for it
    if (!args.empty()) {
        const std::string& cmd = args[0];
        if (cmd == "attach") {
            out("attach <pid>\n\n"
                "Attach to a running process by PID.\n\n"
                "The target process will be stopped (SIGSTOP) and placed under\n"
                "ptrace control. All existing threads are discovered automatically.\n\n"
                "Example:\n"
                "  (ninjadb) attach 12345\n"
                "  Attached to pid 12345");
            return;
        }
        if (cmd == "launch") {
            out("launch <binary> [args...]\n\n"
                "Launch a new process under the debugger. The binary is exec'd\n"
                "with PTRACE_TRACEME enabled, so the child is traced from birth.\n\n"
                "Example:\n"
                "  (ninjadb) launch ./my_program --flag value");
            return;
        }
        if (cmd == "break" || cmd == "b") {
            out("break <addr> [condition]\n"
                "b <addr> [condition]\n\n"
                "Set a software breakpoint (INT3) at the given address.\n\n"
                "Optional condition syntax: <register> <op> <value>\n"
                "  Registers: rax rbx rcx rdx rsi rdi rbp rsp r8-r15 rip\n"
                "  Operators: == != > < >= <=\n"
                "  Values: hex (0x...) or decimal\n\n"
                "Examples:\n"
                "  (ninjadb) break 0x401000\n"
                "  (ninjadb) break 0x401000 rax == 0x10\n"
                "  (ninjadb) break 0x401000 rip > 0x400000");
            return;
        }
        if (cmd == "tbreak" || cmd == "tb") {
            out("tbreak <addr>\n"
                "tb <addr>\n\n"
                "Set a temporary breakpoint. It is automatically removed after\n"
                "the first time it is hit. Useful for step-over, step-out, and\n"
                "one-shot continue-until scenarios.");
            return;
        }
        if (cmd == "watch") {
            out("watch <addr> [len] [w|rw|x]\n\n"
                "Set a hardware watchpoint at the given address.\n\n"
                "  len: 1, 2, 4, or 8 bytes (default: 8)\n"
                "  w:   write only (default)\n"
                "  rw:  read or write\n"
                "  x:   execute\n\n"
                "Uses debug registers DR0-DR3 (max 4 concurrent watchpoints).");
            return;
        }
        if (cmd == "disas" || cmd == "dis") {
            out("disas [addr] [count]\n"
                "dis [addr] [count]\n\n"
                "Disassemble instructions starting at addr (default: current RIP).\n"
                "Count defaults to 20 instructions.\n\n"
                "The current RIP is highlighted with '>>'. Branch targets are\n"
                "annotated with their resolved symbol (e.g. <libc.so.6+0x1234>).");
            return;
        }
        if (cmd == "decomp" || cmd == "dec") {
            out("decomp [addr] [max_bytes]\n"
                "decomp file <binary> [addr]\n"
                "decomp list\n"
                "decomp api\n"
                "decomp set <backend>\n\n"
                "Decompile native code to C source using RetDec or angr.\n\n"
                "Backends:\n"
                "  retdec-native      In-process via dlopen (fastest)\n"
                "  retdec-subprocess  Shell to retdec-decompiler binary\n"
                "  angr               Python subprocess (slowest, best for stripped)\n\n"
                "Without arguments, decompiles the function at the current RIP.\n"
                "Use 'decomp file <binary>' for whole-file decompilation.\n"
                "Use 'decomp set <backend>' to force a specific backend.");
            return;
        }
        if (cmd == "pretty" || cmd == "pp") {
            out("pretty set <lang>\n"
                "pretty cstring <addr>\n"
                "pretty cpp_string <addr>\n"
                "pretty rust_string <addr>\n"
                "pretty go_string <addr>\n"
                "pretty py_string <addr>\n"
                "pretty struct <addr> <descriptor>\n"
                "pretty auto <addr>\n"
                "pretty list\n"
                "pretty api\n\n"
                "Language-aware pretty printing of in-memory values.\n\n"
                "Languages: c, cpp, rust, go, python, none\n\n"
                "Struct descriptor types:\n"
                "  i8 i16 i32 i64  - signed integers\n"
                "  u8 u16 u32 u64  - unsigned integers\n"
                "  f32 f64         - floats\n"
                "  ptr             - pointer (hex)\n"
                "  str             - pointer to C-string (dereferenced)\n"
                "  hex<N>          - N raw bytes as hex");
            return;
        }
        if (cmd == "edit") {
            out("edit [addr]\n\n"
                "Launch the interactive TUI memory editor at the given address\n"
                "(default: current RIP).\n\n"
                "Keys:\n"
                "  arrows     Move cursor\n"
                "  PageUp/Dn  Scroll one page\n"
                "  e          Edit byte (type 2 hex digits, Enter)\n"
                "  s          Seek to new address\n"
                "  /          Search for byte pattern\n"
                "  n / N      Next / previous search hit\n"
                "  f          Follow pointer at cursor\n"
                "  m          Toggle Hex / Disassembly mode\n"
                "  w          Cycle row size (8/16/32)\n"
                "  q          Quit editor");
            return;
        }
        if (cmd == "x") {
            out("x /Nxb <addr>   Examine N bytes in hex\n"
                "x /Nxw <addr>   Examine N words (4 bytes each)\n"
                "x /Nxg <addr>   Examine N qwords (8 bytes each)\n"
                "x /Nxh <addr>   Examine N halfwords (2 bytes each)\n\n"
                "Reads memory from the attached process and displays it in hex.\n"
                "The format string is /<count>x<size> where size is b/w/g/h.");
            return;
        }
        if (cmd == "patch") {
            out("patch list\n"
                "patch nop <offset> <length>\n"
                "patch apply <offset> <kind> [bytes...]\n"
                "patch save <outfile>\n"
                "patch undo [id]\n"
                "patch info\n\n"
                "Static binary patching. Load a binary with 'target <file>' first.\n\n"
                "Patch kinds:\n"
                "  nop       Fill with 0x90\n"
                "  jmp       Convert Jcc to JMP (always take)\n"
                "  nojmp     Convert Jcc to NOPs (never take)\n"
                "  callnop   Convert CALL to 5x NOP\n"
                "  rettrue   Force function to return 1\n"
                "  ascii     Replace ASCII string\n"
                "  custom    User-supplied bytes\n\n"
                "IDs are 0-indexed. 'patch undo' with no arg undoes the last patch.");
            return;
        }
        if (cmd == "script") {
            out("script list\n"
                "script api\n"
                "script run lua <file|code>\n"
                "script run python <file|code>\n\n"
                "Run Lua or Python scripts that control the debugger via the\n"
                "ndbg module. The module is auto-injected; both 'import ndbg'\n"
                "and bare 'ndbg.xxx()' work.\n\n"
                "If the payload names an existing file, it is loaded; otherwise\n"
                "it is treated as inline code.");
            return;
        }
        if (cmd == "stealth") {
            out("stealth list\n"
                "stealth on <name>\n"
                "stealth off <name>\n\n"
                "Manage userland anti-detect techniques.\n\n"
                "Names (substring match):\n"
                "  Hardware Breakpoints\n"
                "  process_vm_readv/writev\n"
                "  Mask /proc/self/status\n"
                "  Hide NinjaDBG mmaps\n"
                "  Timing normalization\n"
                "  Parent name masquerade\n"
                "  Hide from ps\n"
                "  INT3 scan bypass");
            return;
        }
        if (cmd == "kernel") {
            out("kernel status\n"
                "kernel load\n"
                "kernel unload\n\n"
                "Manage the optional kernel-level stealth module (ninja_stealth.ko).\n\n"
                "kernel load   Builds the LKM from source and loads it via insmod.\n"
                "              Requires root and kernel headers installed.\n"
                "kernel unload Unloads the module via rmmod.\n\n"
                "The LKM hooks procfs reads to mask NinjaDBG's presence at the\n"
                "kernel level, defeating checks that userland hooks cannot bypass.");
            return;
        }
        err("No help available for: " + cmd + " (type 'help' for the command list)");
        return;
    }

    out("NinjaDBG v1.1.4 CLI commands:\n"
        "  attach <pid>                  Attach to a running process\n"
        "  launch <bin> [args...]        Launch a new process under the debugger\n"
        "  detach                        Detach from the target\n"
        "  kill                          Kill the target process\n"
        "  continue | cont | c           Continue execution\n"
        "  step | stepi | si | s         Single-step one instruction\n"
        "  next | n                      Step over (skip CALL)\n"
        "  finish | fo                   Step out of current function\n"
        "  syscall-step                  Run until next syscall entry/exit\n"
        "  sleep <ms>                    Sleep for N milliseconds\n"
        "  break | b <addr> [cond]       Set a breakpoint (optionally conditional)\n"
        "  tbreak | tb <addr>            Set a temporary breakpoint\n"
        "  watch <addr> [len] [w|rw|x]   Set a watchpoint\n"
        "  delete | d <id>               Delete a breakpoint/watchpoint\n"
        "  info | i <b|r|t|m|target>     Show breakpoints/registers/threads/maps/target\n"
        "  x /Nxb <addr>                 Examine N bytes in hex\n"
        "  x /Nxw <addr>                 Examine N words\n"
        "  set <addr> = <byte>...        Write bytes to memory\n"
        "  disas | dis [addr] [count]    Full x86-64 disassembly\n"
        "  edit [addr]                   Interactive TUI memory editor\n"
        "  decomp | dec [addr] [max]     Native C decompilation via RetDec/angr\n"
        "  decomp file <bin> [addr]      Decompile whole file or one function\n"
        "  decomp <list|api|set>         Decompiler backend management\n"
        "  pretty | pp set <lang>        Set pretty printer language (c|cpp|rust|go|python)\n"
        "  pretty cstring <addr>         Print C string at addr\n"
        "  pretty cpp_string <addr>      Print std::string at addr\n"
        "  pretty rust_string <addr>     Print Rust String at addr\n"
        "  pretty go_string <addr>       Print Go string at addr\n"
        "  pretty py_string <addr>       Print CPython str at addr\n"
        "  pretty struct <addr> <desc>   Parse struct (e.g. i32,str,ptr)\n"
        "  pretty auto <addr>            Auto-print using active language\n"
        "  pretty <list|api>             Show printers / API docs\n"
        "  bt | backtrace                Show call stack\n"
        "  target <binary>               Load a binary for static patching\n"
        "  patch list                    List applied patches\n"
        "  patch nop <off> <len>         NOP a byte range\n"
        "  patch apply <off> <kind> [b]  Apply a patch (nop/jmp/nojmp/callnop/rettrue/ascii)\n"
        "  patch save <outfile>          Save patched binary\n"
        "  patch undo [id]               Undo a patch (default: last)\n"
        "  patch info                    Show target binary info\n"
        "  stealth list                  List anti-detect techniques\n"
        "  stealth on|off <name>         Enable/disable a technique\n"
        "  kernel status                 Show kernel module status\n"
        "  kernel load                   Build + load the stealth LKM\n"
        "  kernel unload                 Unload the LKM\n"
        "  script list                   Show scripting backend status\n"
        "  script api                    Print Lua/Python API docs\n"
        "  script run lua <file|code>    Run a Lua script\n"
        "  script run python <file|code> Run a Python script\n"
        "  modules | mod                 [v1.1.4] List loaded modules (like x64dbg)\n"
        "  handles                       [v1.1.4] List open file descriptors/handles\n"
        "  dump <addr> <size> <file>     [v1.1.4] Dump memory to file\n"
        "  find <start> <end> <bytes...> [v1.1.4] Search memory (supports ?? wildcards)\n"
        "  findstr <start> <end> <str>   [v1.1.4] Search memory for ASCII string\n"
        "  flags                         [v1.1.4] Display CPU flags decoded (CF/ZF/SF/OF...)\n"
        "  setreg <name> = <value>       [v1.1.4] Modify register or flag (e.g. setreg zf = 1)\n"
        "  memmap                        [v1.1.4] Detailed memory map with sizes\n"
        "  trace <on|off|run|save>       [v1.1.4] Run trace (instruction logging)\n"
        "  help [cmd]                    Show help (optionally for a specific command)\n"
        "  quit | q | exit               Exit NinjaDBG");
}

void HeadlessCLI::cmdQuit(const std::vector<std::string>&) {
    if (dbg_.pid() != 0) {
        out("Detaching before quit...");
        dbg_.detach();
    }
    running_ = false;
}

// ===== Print helpers =====

void HeadlessCLI::printRegisters() {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    RegisterSet r;
    if (!dbg_.readRegisters(r)) { err("read registers failed"); return; }
    u64* vals[] = {&r.rax,&r.rbx,&r.rcx,&r.rdx,&r.rsi,&r.rdi,&r.rbp,&r.rsp,
                   &r.r8,&r.r9,&r.r10,&r.r11,&r.r12,&r.r13,&r.r14,&r.r15,
                   &r.rip,&r.rflags};
    for (int i = 0; i < 18; i++) {
        std::cout << kRegNames[i] << " = " << hex(*vals[i]) << "   ";
        if (i % 4 == 3) std::cout << "\n";
    }
    std::cout << std::endl;
}

void HeadlessCLI::printBreakpoints() {
    auto bps = dbg_.breakpoints();
    if (bps.empty()) { out("No breakpoints."); return; }
    out("ID  ADDR              TYPE  HITS  COND");
    for (auto& b : bps) {
        std::cout << std::left << std::setw(4) << b.id << " "
                  << hex(b.address) << "  "
                  << (b.is_watchpoint ? "WP" : (b.hardware ? "HW" : "SW")) << "   "
                  << std::setw(4) << b.hit_count << "  "
                  << (b.condition.empty() ? "-" : b.condition)
                  << (b.temporary ? " (temp)" : "")
                  << std::endl;
    }
}

void HeadlessCLI::printThreads() {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    auto ts = dbg_.readThreads();
    out("TID         STATE  NAME");
    for (auto& t : ts) {
        std::cout << std::left << std::setw(11) << t.tid << " "
                  << std::setw(6) << t.state << "  " << t.name << std::endl;
    }
}

void HeadlessCLI::printMaps() {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    auto ms = dbg_.readMaps();
    out("START              END                PERMS  PATH");
    for (auto& m : ms) {
        std::cout << hex(m.start) << "  " << hex(m.end) << "  "
                  << (m.read ? "r" : "-") << (m.write ? "w" : "-")
                  << (m.exec ? "x" : "-") << (m.shared ? "s" : "p") << "   "
                  << m.path << std::endl;
    }
}

void HeadlessCLI::printBacktrace() {
    if (dbg_.pid() == 0) { err("Not attached"); return; }
    auto frames = dbg_.backtrace();
    out("#  RIP               SYMBOL");
    for (size_t i = 0; i < frames.size(); i++) {
        std::cout << "#" << i << " " << hex(frames[i].rip) << "  "
                  << (frames[i].symbol.empty() ? "?" : frames[i].symbol) << std::endl;
    }
}

void HeadlessCLI::printPatchList() {
    if (!patcher_.isLoaded()) { err("No target loaded"); return; }
    auto& patches = patcher_.patches();
    if (patches.empty()) { out("No patches applied."); return; }
    out("ID  OFFSET            LEN  KIND");
    for (size_t i = 0; i < patches.size(); i++) {
        std::cout << std::left << std::setw(4) << i << " "
                  << hex(patches[i].file_offset, 8) << "       "
                  << std::setw(4) << patches[i].length << "  ";
        switch (patches[i].kind) {
            case BinaryPatcher::PatchKind::NOP:         std::cout << "NOP"; break;
            case BinaryPatcher::PatchKind::CustomBytes: std::cout << "BYTES"; break;
            case BinaryPatcher::PatchKind::JmpAlways:   std::cout << "JMP"; break;
            case BinaryPatcher::PatchKind::JmpNever:    std::cout << "NOJMP"; break;
            case BinaryPatcher::PatchKind::CallToNop:   std::cout << "CALLNOP"; break;
            case BinaryPatcher::PatchKind::RetTrue:     std::cout << "RETTRUE"; break;
            case BinaryPatcher::PatchKind::AsciiReplace:std::cout << "ASCII"; break;
        }
        std::cout << "  " << patches[i].note << std::endl;
    }
}

void HeadlessCLI::printStealthStatus() {
    out("Anti-detect techniques (userland):");
    for (auto t : AntiDetect::allTechniques()) {
        std::cout << "  [" << (anti_.isEnabled(t) ? "x" : " ") << "] "
                  << AntiDetect::name(t) << std::endl;
    }
}

void HeadlessCLI::printKernelStatus() {
    out("Kernel stealth: " + kernel_.moduleStatus());
    out("Techniques (require loaded LKM):");
    for (auto t : KernelStealth::allTechniques()) {
        std::cout << "  [" << (kernel_.isEnabled(t) ? "x" : " ") << "] "
                  << KernelStealth::name(t) << std::endl;
    }
}

void HeadlessCLI::printTargetInfo() {
    if (!patcher_.isLoaded()) { out("No target loaded"); return; }
    out("Format: " + patcher_.formatName());
    out("Size:   " + std::to_string(patcher_.imageSize()) + " bytes");
    out("Entry:  " + hex(patcher_.entryPoint()));
    out("Sections:");
    for (auto& s : patcher_.sections()) {
        std::cout << "  " << std::left << std::setw(20) << s.name
                  << " off=" << hex(s.file_offset, 8)
                  << " vaddr=" << hex(s.vaddr)
                  << " size=" << hex(s.size, 8)
                  << " " << (s.exec ? "x" : "-") << (s.write ? "w" : "-") << (s.read ? "r" : "-")
                  << std::endl;
    }
}

// ===== Main loop =====

int HeadlessCLI::run(int argc, char** argv, bool skip_eula) {
    printBanner();

    // v1.1.4: Parse -c BEFORE showEula() so batch mode auto-skips the EULA prompt.
    // Previously, batch mode would block on stdin waiting for EULA acceptance,
    // which users perceived as "attach hangs in batch mode".
    bool batch_mode = false;
    std::string batch_cmds;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-c" || a == "--commands") {
            batch_mode = true;
            if (i + 1 < argc) {
                batch_cmds = argv[++i];
            } else {
                err("error: -c requires a command string");
                return 1;
            }
        }
    }

    // In batch mode, auto-skip EULA (the user is scripting, not interacting)
    if (batch_mode) skip_eula = true;

    if (skip_eula) {
        eula_accepted_ = true;
    } else {
        if (!showEula()) {
            err("EULA declined. Exiting.");
            return 1;
        }
        eula_accepted_ = true;
    }

    if (batch_mode) {
        std::istringstream ss(batch_cmds);
        std::string line;
        while (std::getline(ss, line, ';')) {
            if (!line.empty()) execute(line);
        }
        return 0;
    }

    while (running_) {
        prompt();
        std::string line = readLine();
        if (line == "EOF") break;
        if (std::cin.eof()) break;
        if (line.empty()) continue;
        execute(line);
    }
    return 0;
}

} // namespace ndbg
