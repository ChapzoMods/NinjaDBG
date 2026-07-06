// NinjaDBG v1.0.3 - Welcome screen + EULA
// Closed Source - Free - by Chapzoo
//
// First-run experience: a welcome screen introducing NinjaDBG, followed by
// the EULA (End User License Agreement). The EULA must be accepted before
// the debugger becomes usable. Acceptance is persisted to
//   ~/.config/ninjadb/eula_accepted
// so the user only sees it once per machine.
#pragma once

#include "Types.h"
#include <string>

namespace ndbg::ui {

class MainWindow;

class WelcomeScreen {
public:
    WelcomeScreen();
    ~WelcomeScreen();

    // Returns true if EULA has been previously accepted
    static bool isEulaAccepted();

    // Persist acceptance to disk
    static bool acceptEula();

    // Get the full EULA text
    static std::string eulaText();

    // Get a short welcome message
    static std::string welcomeMessage();

    // Get the config directory path
    static std::string configDir();
    static std::string eulaPath();
};

} // namespace ndbg::ui
