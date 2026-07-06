// NinjaDBG v1.1.2 - Entry point
// Open Source (Apache-2.0) - by Chapzoo
//
// NinjaDBG v1.1.2 is CLI-only. The GUI was removed in v1.1.2.
//
// Usage:
//   ninjadb                        Launch headless CLI (default)
//   ninjadb -c "commands;quit"     Batch mode
//   ninjadb --no-eula-check        Skip EULA prompt
//   ninjadb --help                 Show usage
#include "WelcomeScreen.h"
#include "HeadlessCLI.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

int main(int argc, char** argv) {
    bool no_eula = false;
    bool show_help = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--no-eula-check") {
            no_eula = true;
        } else if (a == "--help" || a == "-h") {
            show_help = true;
        }
    }

    if (show_help) {
        std::cout <<
            "NinjaDBG v1.1.2 - Stealth Debugger\n"
            "Open Source (Apache-2.0) - Created by Chapzoo\n\n"
            "Usage:\n"
            "  ninjadb                        Launch headless CLI (interactive REPL)\n"
            "  ninjadb -c \"commands;quit\"    Execute commands in batch mode\n"
            "  ninjadb --no-eula-check        Skip EULA acceptance prompt\n"
            "  ninjadb --help                 Show this help\n\n"
            "The GUI was removed in v1.1.2. The headless CLI exposes the full\n"
            "feature set: disassembly, decompilation, pretty printers, scripting,\n"
            "binary patching, kernel stealth, and more.\n\n"
            "Run 'ninjadb' then type 'help' for the command list.\n";
        return 0;
    }

    // CLI is now the default (and only) mode.
    ndbg::HeadlessCLI cli;
    return cli.run(argc, argv, no_eula);
}
