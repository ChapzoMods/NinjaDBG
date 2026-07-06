// NinjaDBG v1.0.3 - Entry point
// Closed Source - Free - by Chapzoo
//
// Mode selection:
//   ninjadb              → GUI (experimental) — default
//   ninjadb --cli        → Headless CLI (REPL)
//   ninjadb --cli -c "commands;quit"  → batch mode
//   ninjadb --help       → usage
#include "MainWindow.h"
#include "UITheme.h"
#include "WelcomeScreen.h"
#include "HeadlessCLI.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

int main(int argc, char** argv) {
    // Detect --cli flag
    bool cli_mode = false;
    bool no_eula = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--cli" || std::string(argv[i]) == "-c" ||
            std::string(argv[i]) == "--cli") {
            cli_mode = true;
        }
        if (std::string(argv[i]) == "--no-eula-check") no_eula = true;
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            std::cout <<
                "NinjaDBG v1.0.3 — Stealth Debugger\n"
                "Closed Source - Free - Created by Chapzoo\n\n"
                "Usage:\n"
                "  ninjadb                  Launch GUI (EXPERIMENTAL — under development)\n"
                "  ninjadb --cli            Headless CLI mode (recommended for production)\n"
                "  ninjadb --cli -c \"cmds\"  Execute commands in batch mode\n"
                "  ninjadb --no-eula-check  Skip EULA acceptance prompt\n"
                "  ninjadb --help           Show this help\n\n"
                "The GUI is experimental. For full feature coverage (patching, kernel\n"
                "stealth, conditional breakpoints, watchpoints, syscall stepping), use\n"
                "the headless CLI.\n";
            return 0;
        }
    }

    if (cli_mode) {
        ndbg::HeadlessCLI cli;
        return cli.run(argc, argv);
    }

    // GUI mode (experimental)
    std::cout << "NinjaDBG v1.0.3 - Stealth Debugger" << std::endl;
    std::cout << "Closed Source - Free - Created by Chapzoo" << std::endl;
    std::cout << std::endl;
    std::cout << "*** NOTE: The GUI is EXPERIMENTAL and still under active development. ***" << std::endl;
    std::cout << "*** For production use, run:  ninjadb --cli                              ***" << std::endl;
    std::cout << std::endl;

    // Show EULA acceptance in console before launching GUI
    if (!no_eula && !ndbg::ui::WelcomeScreen::isEulaAccepted()) {
        std::cout << ndbg::ui::WelcomeScreen::welcomeMessage() << std::endl;
        std::cout << ndbg::ui::WelcomeScreen::eulaText() << std::endl;
        std::cout << "\nDo you accept this EULA? [y/N]: " << std::flush;
        std::string resp;
        std::getline(std::cin, resp);
        if (resp != "y" && resp != "Y" && resp != "yes" && resp != "YES") {
            std::cerr << "EULA declined. Exiting." << std::endl;
            return 1;
        }
        ndbg::ui::WelcomeScreen::acceptEula();
        std::cout << "EULA accepted. Saved to " << ndbg::ui::WelcomeScreen::eulaPath() << std::endl;
    }

    ndbg::ui::MainWindow w;
    if (!w.init(ndbg::ui::lay::WIN_W, ndbg::ui::lay::WIN_H)) {
        std::cerr << "Failed to initialize window. Is DISPLAY set?" << std::endl;
        std::cerr << "Tip: run `ninjadb --cli` for the headless interface." << std::endl;
        return 1;
    }
    w.setStatus("Ready - click [Attach] to debug a process  -  GUI is EXPERIMENTAL");
    w.log("GUI is EXPERIMENTAL — for full features use: ninjadb --cli", "warn");
    w.log("v1.0.3 adds: headless CLI, kernel stealth, binary patching, "
          "conditional bps, watchpoints, step-over/step-out, cross-platform (Win/Mac)", "info");
    w.run();
    return 0;
}
