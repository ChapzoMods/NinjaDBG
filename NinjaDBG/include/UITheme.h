// NinjaDBG v1.0.2 - UI Theme & Layout Constants
// Open Source (MIT) - by Chapzoo
#pragma once

#include <string>
#include <cstdint>

namespace ndbg::ui {

// Color palette (dark theme, ninja-inspired: charcoal + neon cyan)
namespace col {
    constexpr u32 BG          = 0xFF14161F;  // main background (ARGB)
    constexpr u32 BG_Panel    = 0xFF1B1E2C;
    constexpr u32 BG_PanelAlt = 0xFF252A40;
    constexpr u32 BG_Elevated = 0xFF252A40;
    constexpr u32 BG_Hover    = 0xFF2D3350;
    constexpr u32 BG_Selected = 0xFF003D3D;

    constexpr u32 Border      = 0xFF33384F;
    constexpr u32 BorderLite  = 0xFF262A3D;

    constexpr u32 Text        = 0xFFE6E8F0;
    constexpr u32 Text_Dim    = 0xFF8A8FA3;
    constexpr u32 Text_Muted  = 0xFF5A6075;

    constexpr u32 Accent      = 0xFF00FFE1;  // neon cyan
    constexpr u32 Accent2     = 0xFF00B8A0;
    constexpr u32 Accent_Dim  = 0xFF006B62;
    constexpr u32 Warn        = 0xFFFFB454;
    constexpr u32 Error       = 0xFFFF4D6D;
    constexpr u32 OK          = 0xFF4ADE80;
    constexpr u32 Info        = 0xFF7AB7FF;
    constexpr u32 Purple      = 0xFFB388FF;

    constexpr u32 Red_Headband = 0xFFC41E3A;
    constexpr u32 Gold         = 0xFFFFD700;
}

// Layout
namespace lay {
    constexpr int WIN_W = 1680;
    constexpr int WIN_H = 1050;
    constexpr int TOOLBAR_H = 64;       // taller for icon+label buttons
    constexpr int STATUSBAR_H = 30;
    constexpr int LEFT_W = 290;
    constexpr int RIGHT_W = 370;
    constexpr int BOTTOM_H = 190;
    constexpr int PAD = 8;

    // Button geometry
    constexpr int BTN_H = 40;
    constexpr int BTN_ICON_SZ = 20;
    constexpr int BTN_GAP = 6;
    constexpr int BTN_PAD_X = 12;
    constexpr int SEPARATOR_W = 12;

    constexpr int FONT_TITLE = 16;
    constexpr int FONT_BODY  = 13;
    constexpr int FONT_MONO  = 12;
    constexpr int FONT_SMALL = 11;
    constexpr int FONT_BIG   = 22;
    constexpr int FONT_HUGE  = 32;
}

namespace font {
    constexpr const char* Sans  = "DejaVu Sans";
    constexpr const char* Mono  = "DejaVu Sans Mono";
    constexpr const char* Title = "DejaVu Sans";
}

} // namespace ndbg::ui
