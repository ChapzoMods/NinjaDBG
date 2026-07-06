// NinjaDBG v1.0.4 - WelcomeScreen implementation
// Closed Source - Free - by Chapzoo
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
    return line == "1.0.4";
}

bool WelcomeScreen::acceptEula() {
    // Create config dir
    std::string cmd = "mkdir -p " + configDir();
    std::system(cmd.c_str());
    std::ofstream f(eulaPath());
    if (!f) return false;
    f << "1.0.4\n";
    f.close();
    return true;
}

std::string WelcomeScreen::welcomeMessage() {
    return R"WELCOME(
Welcome to NinjaDBG v1.0.4 — Stealth Debugger

NinjaDBG is a closed-source, free, native debugger for Linux x86-64 with
experimental support for Windows (PE) and macOS (Mach-O) binaries.

This release adds:
  - Headless CLI mode (for SSH-only environments and scripting)
  - Kernel-level stealth techniques (loadable kernel module)
  - Binary patching (NOP, JMP, CALL→NOP, ASCII replace, custom bytes)
  - Conditional + temporary breakpoints, watchpoints, step-over/step-out
  - Cross-platform debugging via Wine + QEMU adapters
  - Welcome screen + EULA
  - [v1.0.4] Full x86-64 disassembler in CLI (disas command)
  - [v1.0.4] Interactive TUI memory editor (edit command)
  - [v1.0.4] Lua + Python scripting (script run command)

IMPORTANT: The graphical interface is EXPERIMENTAL and still under active
development. For production use, prefer the headless CLI (run `ninjadb --cli`).
)WELCOME";
}

std::string WelcomeScreen::eulaText() {
    return R"EULA(
==========================================================================
   NinjaDBG v1.0.4 — END USER LICENSE AGREEMENT (EULA)
==========================================================================

Copyright (c) 2026 Chapzoo (one person). All rights reserved.

BY DOWNLOADING, INSTALLING, OR USING NINJADBG, YOU AGREE TO BE BOUND BY
THE TERMS OF THIS EULA. IF YOU DO NOT AGREE, DO NOT USE THE SOFTWARE.

1. DEFINITIONS

   "Software" means NinjaDBG v1.0.4, including the debugger binary
   (ninjadb), the headless CLI, the graphical interface (experimental),
   the libninjastealth.so preload payload, the optional ninja_stealth.ko
   kernel module, and all accompanying documentation.

   "Author" means Chapzoo, the sole creator and rights holder.

   "Target" means any binary or process you debug with the Software.

2. LICENSE GRANT

   The Author grants you a non-exclusive, non-transferable, revocable
   license to:
     (a) install and run the Software on machines you own or control;
     (b) use the Software for personal, academic, or commercial purposes;
     (c) redistribute the Software in its original unmodified archive,
         including this EULA.

3. CLOSED SOURCE

   The Software is distributed in binary form only. The source code is
   private and is NOT distributed. You may NOT:
     (a) reverse-engineer, decompile, or disassemble the binaries;
     (b) modify, patch, or create derivative works of the binaries;
     (c) redistribute modified versions;
     (d) remove or alter any copyright notice or EULA reference;
     (e) represent the Software, in whole or in part, as your own work.

4. ANTI-DEBUG / STEALTH FEATURES

   The Software implements techniques to hide its presence from Target
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

5. KERNEL MODULE (OPTIONAL)

   The Software can optionally load a kernel module (ninja_stealth.ko)
   for kernel-level stealth. Loading unsigned kernel modules has security
   implications and may destabilize your system. The Author:
     (a) does NOT sign the module;
     (b) does NOT guarantee compatibility with any specific kernel version;
     (c) is NOT liable for system instability, data loss, or security
         incidents resulting from loading the module.

6. NO WARRANTY

   THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
   NONINFRINGEMENT. THE AUTHOR DOES NOT WARRANT THAT THE SOFTWARE WILL
   FUNCTION CORRECTLY, WILL MEET YOUR REQUIREMENTS, OR WILL BE FREE FROM
   DEFECTS.

7. LIMITATION OF LIABILITY

   IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
   SPECIAL, CONSEQUENTIAL, OR PUNITIVE DAMAGES, INCLUDING BUT NOT LIMITED
   TO LOSS OF PROFITS, DATA, OR USE, EVEN IF THE AUTHOR HAS BEEN ADVISED
   OF THE POSSIBILITY OF SUCH DAMAGES. YOUR SOLE REMEDY FOR DISSATISFACTION
   WITH THE SOFTWARE IS TO STOP USING IT.

8. EXPERIMENTAL FEATURES

   The graphical interface is EXPERIMENTAL and still under development.
   Bugs, layout issues, and missing features are expected. For production
   use, prefer the headless CLI. The Author does not guarantee any
   specific roadmap or release schedule for GUI improvements.

9. TERMINATION

   This license terminates automatically if you violate any term. Upon
   termination you must destroy all copies of the Software.

10. GOVERNING LAW

    This EULA is governed by the laws of the Author's jurisdiction.
    Disputes shall be resolved in the courts of that jurisdiction.

11. CHANGES

    The Author reserves the right to update this EULA in future releases.
    Continued use after an update constitutes acceptance of the new terms.

12. ENTIRE AGREEMENT

    This EULA constitutes the entire agreement between you and the Author
    regarding the Software, superseding any prior agreements.

==========================================================================
   If you do not agree, do not use NinjaDBG. Press ESC or close the
   application now.
==========================================================================
)EULA";
}

WelcomeScreen::WelcomeScreen() {}
WelcomeScreen::~WelcomeScreen() {}

} // namespace ndbg::ui
