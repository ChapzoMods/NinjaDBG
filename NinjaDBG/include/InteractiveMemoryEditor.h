// NinjaDBG v1.0.5 - Interactive Memory Editor (TUI)
// Closed Source - Free - by Chapzoo
//
// A curses-like interactive memory viewer/editor that runs in the terminal.
// Allows:
//   - Examine memory at any address, with hex + ASCII side-by-side
//   - Navigate with arrow keys, Page Up/Down, Home/End
//   - Edit bytes inline (press 'e', then type hex bytes)
//   - Seek to a new address (press 's', then type address)
//   - Follow pointer under cursor (press 'f')
//   - Search for byte pattern (press '/')
//   - Toggle between 8/16/32 byte rows (press 'w')
//   - Quit with 'q'
//
// The editor uses raw stdin/stdout (no ncurses dependency) so it builds
// everywhere. It supports VT100 escape sequences for cursor + color.
#pragma once

#include "Types.h"
#include "DebuggerCore.h"
#include "Disassembler.h"
#include <string>
#include <vector>
#include <functional>

namespace ndbg {

class InteractiveMemoryEditor {
public:
    enum class Mode { Hex, Disassembly };
    enum class SubMode { None, Edit, Seek, Search, Help };

    InteractiveMemoryEditor(DebuggerCore& dbg);
    ~InteractiveMemoryEditor();

    // Open the editor at the given address. Returns when user presses 'q'.
    void run(addr_t start_addr, Mode initial_mode = Mode::Hex);

    // Set a callback to be invoked before each redraw (e.g. for logging)
    void setRedrawCallback(std::function<void()> cb) { redraw_cb_ = cb; }

private:
    DebuggerCore& dbg_;
    Disassembler disas_;

    addr_t base_addr_ = 0;       // top-left displayed address
    addr_t cursor_addr_ = 0;     // current cursor position
    int cursor_nibble_ = 0;      // 0 = high, 1 = low (for hex editing)
    int row_size_ = 16;          // bytes per row (8/16/32)
    Mode mode_ = Mode::Hex;
    SubMode submode_ = SubMode::None;
    std::string input_buf_;
    std::string status_msg_;
    std::vector<u8> cache_;      // cached bytes around base_addr_
    addr_t cache_base_ = 0;
    size_t cache_size_ = 0;
    std::vector<u8> search_pattern_;
    std::vector<addr_t> search_hits_;
    size_t search_idx_ = 0;

    std::function<void()> redraw_cb_;

    // Screen dimensions (computed at startup)
    int cols_ = 80;
    int rows_ = 24;

    // TUI primitives
    void enableRawMode();
    void disableRawMode();
    void clearScreen();
    void moveCursor(int x, int y);
    void setColor(u32 fg, u32 bg);
    void resetColor();
    void drawChar(int x, int y, char c, u32 fg = 0xFFFFFF, u32 bg = 0x14161F);
    void drawText(int x, int y, const std::string& s, u32 fg = 0xFFFFFF, u32 bg = 0x14161F);
    void refreshScreen();

    // Read a single key (returns ASCII char or special key code)
    enum class Key {
        Unknown, Up, Down, Left, Right, PageUp, PageDown, Home, End,
        Escape, Enter, Backspace, Tab, Char
    };
    Key readKey(int& ch);

    // Render the editor
    void render();
    void renderHex();
    void renderDisassembly();
    void renderHeader();
    void renderFooter();
    void renderModal();

    // Cache management
    void ensureCache();
    void refreshCache();

    // Cursor movement
    void moveCursorUp();
    void moveCursorDown();
    void moveCursorLeft();
    void moveCursorRight();
    void pageUp();
    void pageDown();

    // Edit operations
    void startEdit();
    void applyEditInput();
    void startSeek();
    void applySeekInput();
    void startSearch();
    void applySearchInput();
    void nextSearchHit();
    void prevSearchHit();
    void followPointer();
    void toggleMode();
    void toggleRowSize();

    // Helpers
    u8 readByte(addr_t a);
    bool writeByte(addr_t a, u8 v);
    addr_t screenToAddr(int row, int col);
    void setStatus(const std::string& s) { status_msg_ = s; }

    // Address ↔ screen coordinate conversion
    int addrToRow(addr_t a) const { return (int)((a - base_addr_) / row_size_); }
    int addrToCol(addr_t a) const { return (int)((a - base_addr_) % row_size_); }
};

} // namespace ndbg
