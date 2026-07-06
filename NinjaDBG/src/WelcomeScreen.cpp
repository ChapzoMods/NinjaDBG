// NinjaDBG v1.1.0 - WelcomeScreen implementation
// Open Source (MIT License) - by Chapzoo
#include "WelcomeScreen.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

namespace ndbg::ui {

std::string WelcomeScreen::configDir() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    return std::string(home) + "/.config/ninjadb";
}

std::string WelcomeScreen::eulaPath() {
    return configDir() + "/eula_accepted";
}

bool WelcomeScreen::isEulaAccepted() {
    std::ifstream f(eulaPath());
    if (!f.good()) return false;
    std::string line;
    std::getline(f, line);
    return line == "1.1.0";
}

bool WelcomeScreen::acceptEula() {
    // Create config dir
    std::string cmd = "mkdir -p " + configDir();
    std::system(cmd.c_str());
    std::ofstream f(eulaPath());
    if (!f) return false;
    f << "1.1.0\n";
    f.close();
    return true;
}

std::string WelcomeScreen::welcomeMessage() {
    return R"WELCOME(
Welcome to NinjaDBG v1.1.0 — Stealth Debugger

NinjaDBG is an OPEN SOURCE (MIT License) native debugger for Linux x86-64
with experimental support for Windows (PE) and macOS (Mach-O) binaries.

This release adds:
  - Bug fixes: info b, patch undo, --no-eula-check, script run python, decomp angr
  - Pretty Printers per language (C, C++, Rust, Go, Python)
  - Switched from Closed Source to Open Source (MIT License)

Full feature list:
  - Headless CLI mode (recommended for production)
  - Kernel-level stealth techniques (loadable kernel module)
  - Binary patching (NOP, JMP, CALL→NOP, ASCII replace, custom bytes)
  - Conditional + temporary breakpoints, watchpoints, step-over/step-out
  - Cross-platform debugging via Wine + QEMU adapters
  - Full standalone x86-64 disassembler
  - Interactive TUI memory editor (hex+ASCII, no ncurses)
  - Lua + Python scripting via JSON-RPC subprocess bridge
  - Native C decompilation via RetDec + angr backends
  - Pretty printers: C strings, std::string, Rust String, Go string, Python str, structs
  - Welcome screen + MIT license

IMPORTANT: The graphical interface is EXPERIMENTAL and still under active
development. For production use, prefer the headless CLI (run `ninjadb --cli`).
)WELCOME";
}

std::string WelcomeScreen::eulaText() {
    return R"EULA(
==========================================================================
   NinjaDBG v1.1.0 — MIT License (Open Source)
==========================================================================

MIT License

Copyright (c) 2026 Chapzoo (ChapzoMods)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

==========================================================================
   Additional notes (not part of the MIT license)
==========================================================================

1. ANTI-DEBUG / STEALTH FEATURES

   NinjaDBG implements techniques to hide its presence from Target
   processes. These techniques are intended for:
     - legitimate reverse engineering and security research;
     - debugging software whose anti-debug routines interfere with normal
       development tools;
     - malware analysis in controlled environments.

   You are SOLELY RESPONSIBLE for ensuring your use complies with all
   applicable laws. The Author does NOT condone using the Software to:
     - circumvent DRM or license checks on software you did not author;
     - analyze commercial software with intent to crack or pirate it;
     - evade anti-cheat systems in online games;
     - bypass security controls on systems you do not own or control.

2. KERNEL MODULE (OPTIONAL)

   The Software can optionally load a kernel module (ninja_stealth.ko)
   for kernel-level stealth. Loading unsigned kernel modules has security
   implications and may destabilize your system. The Author:
     (a) does NOT sign the module;
     (b) does NOT guarantee compatibility with any specific kernel version;
     (c) is NOT liable for system instability, data loss, or security
         incidents resulting from loading the module.

3. EXPERIMENTAL FEATURES

   The graphical interface is EXPERIMENTAL and still under development.
   Bugs, layout issues, and missing features are expected. For production
   use, prefer the headless CLI.

4. NO WARRANTY

   THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED. THE AUTHOR DOES NOT WARRANT THAT THE SOFTWARE WILL FUNCTION
   CORRECTLY, WILL MEET YOUR REQUIREMENTS, OR WILL BE FREE FROM DEFECTS.

5. LIMITATION OF LIABILITY

   IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
   SPECIAL, CONSEQUENTIAL, OR PUNITIVE DAMAGES, INCLUDING BUT NOT LIMITED
   TO LOSS OF PROFITS, DATA, OR USE, EVEN IF THE AUTHOR HAS BEEN ADVISED
   OF THE POSSIBILITY OF SUCH DAMAGES.

==========================================================================
   By using NinjaDBG you accept the MIT License terms above.
   Press 'y' to accept, or any other key to decline.
==========================================================================
)EULA";
}

WelcomeScreen::WelcomeScreen() {}
WelcomeScreen::~WelcomeScreen() {}

} // namespace ndbg::ui
