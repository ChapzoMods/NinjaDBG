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

NinjaDBG is an OPEN SOURCE (Apache-2.0) native debugger for Linux x86-64
with experimental support for Windows (PE) and macOS (Mach-O) binaries.

This release adds:
  - Bug fixes: info b, patch undo, --no-eula-check, script run python, decomp angr
  - Pretty Printers per language (C, C++, Rust, Go, Python)
  - Switched from Closed Source to Open Source (Apache-2.0)

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
   NinjaDBG v1.1.0 — Apache License 2.0 (Open Source)
==========================================================================

Apache License
Version 2.0, January 2004
http://www.apache.org/licenses/

Copyright 2026 Chapzoo (ChapzoMods)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at:

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

KEY TERMS (summary — see full License at http://www.apache.org/licenses/):

  - You are free to use, copy, modify, merge, publish, distribute,
    sublicense, and sell copies of the Software.
  - You must include the copyright notice and a copy of this License
    in all copies or substantial portions of the Software.
  - You must state any significant changes you made to the files.
  - If you institute patent litigation alleging the Software infringes
    a patent, your patent license terminates.
  - The Software is provided "AS IS" without warranties of any kind.
  - The Author is not liable for any damages arising from use of the
    Software.

==========================================================================
   Additional notes (not part of the Apache License)
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
   By using NinjaDBG you accept the Apache License 2.0 terms above.
   Press 'y' to accept, or any other key to decline.
==========================================================================
)EULA";
}

WelcomeScreen::WelcomeScreen() {}
WelcomeScreen::~WelcomeScreen() {}

} // namespace ndbg::ui
