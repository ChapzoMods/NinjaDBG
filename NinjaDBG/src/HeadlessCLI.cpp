// NinjaDBG v1.1.0 - HeadlessCLI implementation
// Open Source (Apache-2.0) - by Chapzoo
#include "HeadlessCLI.h"
#include "WelcomeScreen.h"
#include "InteractiveMemoryEditor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
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
        "  v1.1.0 — Stealth Debugger  (Open Source (Apache-2.0) - by Chapzoo)\n"
        "  Headless CLI mode. Type 'help' for command list, 'quit' to exit.\n"
        "  New in v1.1.0: decomp (native C decompilation via RetDec/angr)\n"
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
    if (ok) *ok = true;
    return (addr_t)strtoull(s.c_str(), nullptr, 0);
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
    else if (cmd == "finish" || cmd == "fo") { /* step out */ }
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

void HeadlessCLI::cmdHelp(const std::vector<std::string>&) {
    out("NinjaDBG v1.1.0 CLI commands:\n"
        "  attach <pid>                  Attach to a running process\n"
        "  launch <bin> [args...]        Launch a new process under the debugger\n"
        "  detach                        Detach from the target\n"
        "  kill                          Kill the target process\n"
        "  continue | c                  Continue execution\n"
        "  step | s                      Single-step one instruction\n"
        "  next | n                      Step over (skip CALL)\n"
        "  syscall-step                  Run until next syscall entry/exit\n"
        "  break <addr> [cond]           Set a breakpoint (optionally conditional)\n"
        "  tbreak <addr>                 Set a temporary breakpoint\n"
        "  watch <addr> [len] [w|rw|x]   Set a watchpoint\n"
        "  delete <id>                   Delete a breakpoint/watchpoint\n"
        "  info <b|r|t|m|target>         Show breakpoints/registers/threads/maps/target\n"
        "  x /Nxb <addr>                 Examine N bytes in hex\n"
        "  x /Nxw <addr>                 Examine N words\n"
        "  set <addr> = <byte>...        Write bytes to memory\n"
        "  disas [addr] [count]          Full x86-64 disassembly\n"
        "  edit [addr]                   Interactive TUI memory editor\n"
        "  decomp [addr] [max_bytes]     Native C decompilation via RetDec/angr\n"
        "  decomp file <bin> [addr]      Decompile whole file or one function\n"
        "  decomp <list|api|set>         Decompiler backend management\n"
        "  pretty set <lang>             [v1.1.0] Set pretty printer language (c|cpp|rust|go|python)\n"
        "  pretty cstring <addr>         [v1.1.0] Print C string at addr\n"
        "  pretty cpp_string <addr>      [v1.1.0] Print std::string at addr\n"
        "  pretty rust_string <addr>     [v1.1.0] Print Rust String at addr\n"
        "  pretty go_string <addr>       [v1.1.0] Print Go string at addr\n"
        "  pretty py_string <addr>       [v1.1.0] Print CPython str at addr\n"
        "  pretty struct <addr> <desc>   [v1.1.0] Parse struct (e.g. i32,str,ptr)\n"
        "  pretty <list|api>             [v1.1.0] Show printers / API docs\n"
        "  bt | backtrace                Show call stack\n"
        "  target <binary>               Load a binary for static patching\n"
        "  patch list                    List applied patches\n"
        "  patch nop <off> <len>         NOP a byte range\n"
        "  patch apply <off> <kind> [b]  Apply a patch (nop/jmp/nojmp/callnop/rettrue/ascii)\n"
        "  patch save <outfile>          Save patched binary\n"
        "  patch undo [id]               Undo a patch (default: last)\n"
        "  stealth list                  List anti-detect techniques\n"
        "  stealth on|off <name>         Enable/disable a technique\n"
        "  kernel status                 Show kernel module status\n"
        "  kernel load                   Build + load the stealth LKM\n"
        "  kernel unload                 Unload the LKM\n"
        "  script list                   Show scripting backend status\n"
        "  script api                    Print Lua/Python API docs\n"
        "  script run lua <file|code>    Run a Lua script\n"
        "  script run python <file|code> Run a Python script\n"
        "  help                          Show this help\n"
        "  quit | q                      Exit NinjaDBG");
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
    if (skip_eula) {
        eula_accepted_ = true;
    } else {
        if (!showEula()) {
            err("EULA declined. Exiting.");
            return 1;
        }
        eula_accepted_ = true;
    }

    // Parse CLI args: --cli mode just enters REPL
    bool batch_mode = false;
    std::string batch_cmds;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-c" || a == "--commands") {
            batch_mode = true;
            if (i + 1 < argc) batch_cmds = argv[++i];
        } else if (a == "--no-eula-check") {
            // Already handled by skip_eula; ignore here
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: ninjadb --cli [-c \"commands\"] [--no-eula-check]\n"
                      << "  --cli              Run in headless CLI mode\n"
                      << "  -c \"commands\"      Execute commands and exit (separated by ;)\n"
                      << "  --no-eula-check    Skip EULA acceptance prompt\n";
            return 0;
        }
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
