// NinjaDBG v1.0.2 - Entry point
// Closed Source - Free - by Chapzoo
#include "MainWindow.h"
#include "UITheme.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// We launch the app twice — once normally, once with NINJADBG_SHOW_ABOUT=1
// env var which auto-opens the About modal.

int main(int argc, char** argv) {
    bool show_about = (getenv("NINJADBG_SHOW_ABOUT") != nullptr);
    bool attach_demo = (getenv("NINJADBG_DEMO_ATTACH") != nullptr);

    std::cout << "NinjaDBG v1.0.2 - Stealth Debugger" << std::endl;
    std::cout << "Closed Source - Free - Created by Chapzoo" << std::endl;
    std::cout << std::endl;

    ndbg::ui::MainWindow w;
    if (!w.init(ndbg::ui::lay::WIN_W, ndbg::ui::lay::WIN_H)) {
        std::cerr << "Failed to initialize window. Is DISPLAY set?" << std::endl;
        return 1;
    }
    w.setStatus("Ready - click [Attach] to debug a process");

    if (show_about) {
        // Auto-open About after 200ms
        std::thread([&w]{
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            w.actionAbout();
        }).detach();
    }

    if (attach_demo) {
        // Try to auto-attach to the target_test process for a "live debugging" screenshot.
        std::thread([&w]{
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            auto procs = ndbg::DebuggerCore::listProcesses();
            for (auto& p : procs) {
                if (p.cmdline.find("target_test") != std::string::npos ||
                    p.name.find("target_test") != std::string::npos) {
                    w.demoAttachToPid(p.pid);
                    break;
                }
            }
        }).detach();
    }

    w.run();
    return 0;
}
