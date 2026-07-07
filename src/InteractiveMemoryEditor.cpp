// NinjaDBG v1.1.4 - InteractiveMemoryEditor implementation
// Open Source (Apache-2.0) - by Chapzoo
#include "InteractiveMemoryEditor.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdio>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace ndbg {

InteractiveMemoryEditor::InteractiveMemoryEditor(DebuggerCore& dbg)
    : dbg_(dbg) {}
InteractiveMemoryEditor::~InteractiveMemoryEditor() {
    disableRawMode();
}

// ===== TUI primitives =====

static termios orig_termios;
static bool raw_enabled = false;

void InteractiveMemoryEditor::enableRawMode() {
    if (raw_enabled) return;
    tcgetattr(STDIN_FILENO, &orig_termios);
    termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_enabled = true;

    // Get terminal size
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        cols_ = ws.ws_col;
        rows_ = ws.ws_row;
    }
}

void InteractiveMemoryEditor::disableRawMode() {
    if (!raw_enabled) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_enabled = false;
}

void InteractiveMemoryEditor::clearScreen() {
    std::cout << "\x1b[2J\x1b[H" << std::flush;
}

void InteractiveMemoryEditor::moveCursor(int x, int y) {
    std::cout << "\x1b[" << (y + 1) << ";" << (x + 1) << "H" << std::flush;
}

void InteractiveMemoryEditor::setColor(u32 fg, u32 bg) {
    // 24-bit color
    printf("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm",
           (fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF,
           (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
}

void InteractiveMemoryEditor::resetColor() {
    printf("\x1b[0m");
}

void InteractiveMemoryEditor::drawChar(int x, int y, char c, u32 fg, u32 bg) {
    moveCursor(x, y);
    setColor(fg, bg);
    std::cout << c;
    resetColor();
}

void InteractiveMemoryEditor::drawText(int x, int y, const std::string& s, u32 fg, u32 bg) {
    moveCursor(x, y);
    setColor(fg, bg);
    std::cout << s;
    resetColor();
}

void InteractiveMemoryEditor::refreshScreen() {
    std::cout << std::flush;
}

// ===== Key reading =====

InteractiveMemoryEditor::Key InteractiveMemoryEditor::readKey(int& ch) {
    char c = 0;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return Key::Unknown;
    ch = c;
    if (c == '\x1b') {
        // Escape sequence — try to read more
        char seq[3] = {0};
        if (read(STDIN_FILENO, &seq[0], 1) <= 0) return Key::Escape;
        if (read(STDIN_FILENO, &seq[1], 1) <= 0) return Key::Escape;
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) <= 0) return Key::Unknown;
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return Key::Home;
                        case '4': return Key::End;
                        case '5': return Key::PageUp;
                        case '6': return Key::PageDown;
                        case '7': return Key::Home;
                        case '8': return Key::End;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return Key::Up;
                    case 'B': return Key::Down;
                    case 'C': return Key::Right;
                    case 'D': return Key::Left;
                    case 'H': return Key::Home;
                    case 'F': return Key::End;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return Key::Home;
                case 'F': return Key::End;
            }
        }
        return Key::Escape;
    }
    if (c == '\r' || c == '\n') return Key::Enter;
    if (c == '\t') return Key::Tab;
    if (c == 127 || c == 8) return Key::Backspace;
    return Key::Char;
}

// ===== Cache management =====

void InteractiveMemoryEditor::ensureCache() {
    // Cache a window around base_addr_
    addr_t need_lo = base_addr_;
    size_t need_size = (size_t)row_size_ * (rows_ - 6) + 64;
    if (cache_base_ == need_lo && cache_size_ >= need_size) return;
    cache_base_ = need_lo;
    cache_size_ = need_size + 256;
    cache_ = dbg_.readMemoryVec(cache_base_, cache_size_);
}

void InteractiveMemoryEditor::refreshCache() {
    cache_.clear();
    cache_base_ = 0;
    cache_size_ = 0;
    ensureCache();
}

// ===== Byte access =====

u8 InteractiveMemoryEditor::readByte(addr_t a) {
    if (a >= cache_base_ && a < cache_base_ + cache_.size()) {
        return cache_[a - cache_base_];
    }
    u8 b = 0;
    dbg_.readMemory(a, &b, 1);
    return b;
}

bool InteractiveMemoryEditor::writeByte(addr_t a, u8 v) {
    if (dbg_.writeMemory(a, &v, 1)) {
        // Update cache
        if (a >= cache_base_ && a < cache_base_ + cache_.size()) {
            cache_[a - cache_base_] = v;
        }
        return true;
    }
    return false;
}

// ===== Cursor movement =====

void InteractiveMemoryEditor::moveCursorUp() {
    if (cursor_addr_ >= base_addr_ + (addr_t)row_size_) {
        cursor_addr_ -= row_size_;
    } else if (base_addr_ >= (addr_t)row_size_) {
        base_addr_ -= row_size_;
        refreshCache();
    }
}

void InteractiveMemoryEditor::moveCursorDown() {
    cursor_addr_ += row_size_;
    int visible_rows = rows_ - 6;
    if (addrToRow(cursor_addr_) >= visible_rows) {
        base_addr_ += row_size_;
        refreshCache();
    }
}

void InteractiveMemoryEditor::moveCursorLeft() {
    if (cursor_nibble_ == 1) {
        cursor_nibble_ = 0;
    } else if (cursor_addr_ > 0) {
        cursor_addr_--;
        cursor_nibble_ = 1;
        if (cursor_addr_ < base_addr_) {
            base_addr_ -= row_size_;
            refreshCache();
        }
    }
}

void InteractiveMemoryEditor::moveCursorRight() {
    if (cursor_nibble_ == 0) {
        cursor_nibble_ = 1;
    } else {
        cursor_addr_++;
        cursor_nibble_ = 0;
        int visible_rows = rows_ - 6;
        if (addrToRow(cursor_addr_) >= visible_rows) {
            base_addr_ += row_size_;
            refreshCache();
        }
    }
}

void InteractiveMemoryEditor::pageUp() {
    int visible_rows = rows_ - 6;
    addr_t delta = (addr_t)row_size_ * visible_rows;
    if (base_addr_ >= delta) base_addr_ -= delta;
    else base_addr_ = 0;
    if (cursor_addr_ >= delta) cursor_addr_ -= delta;
    else cursor_addr_ = base_addr_;
    refreshCache();
}

void InteractiveMemoryEditor::pageDown() {
    int visible_rows = rows_ - 6;
    addr_t delta = (addr_t)row_size_ * visible_rows;
    base_addr_ += delta;
    cursor_addr_ += delta;
    refreshCache();
}

// ===== Edit operations =====

void InteractiveMemoryEditor::startEdit() {
    submode_ = SubMode::Edit;
    input_buf_.clear();
    setStatus("EDIT: type 2 hex digits, Enter to commit, Esc to cancel");
}

void InteractiveMemoryEditor::applyEditInput() {
    if (input_buf_.size() >= 2) {
        u8 v = (u8)strtoul(input_buf_.substr(0, 2).c_str(), nullptr, 16);
        if (writeByte(cursor_addr_, v)) {
            setStatus("Wrote 0x" + input_buf_.substr(0, 2) + " at " + hex(cursor_addr_));
        } else {
            setStatus("Write failed");
        }
    }
    submode_ = SubMode::None;
    input_buf_.clear();
}

void InteractiveMemoryEditor::startSeek() {
    submode_ = SubMode::Seek;
    input_buf_.clear();
    setStatus("SEEK: type address (hex), Enter to go, Esc to cancel");
}

void InteractiveMemoryEditor::applySeekInput() {
    if (!input_buf_.empty()) {
        addr_t a = strtoull(input_buf_.c_str(), nullptr, 16);
        base_addr_ = a;
        cursor_addr_ = a;
        refreshCache();
        setStatus("Seeked to " + hex(a));
    }
    submode_ = SubMode::None;
    input_buf_.clear();
}

void InteractiveMemoryEditor::startSearch() {
    submode_ = SubMode::Search;
    input_buf_.clear();
    setStatus("SEARCH: type hex bytes (e.g. 90 90 90), Enter to search, Esc to cancel");
}

void InteractiveMemoryEditor::applySearchInput() {
    // Parse input as space-separated hex bytes
    std::vector<u8> pat;
    std::istringstream ss(input_buf_);
    std::string tok;
    while (ss >> tok) {
        pat.push_back((u8)strtoul(tok.c_str(), nullptr, 16));
    }
    if (pat.empty()) {
        setStatus("Empty search pattern");
        submode_ = SubMode::None;
        return;
    }
    search_pattern_ = pat;
    search_hits_.clear();
    // Read a 64KB window from cursor_addr_ and search
    auto buf = dbg_.readMemoryVec(cursor_addr_, 0x10000);
    if (!buf.empty()) {
        for (size_t i = 0; i + pat.size() <= buf.size(); i++) {
            if (memcmp(buf.data() + i, pat.data(), pat.size()) == 0) {
                search_hits_.push_back(cursor_addr_ + i);
            }
        }
    }
    if (search_hits_.empty()) {
        setStatus("No hits");
    } else {
        search_idx_ = 0;
        addr_t hit = search_hits_[0];
        base_addr_ = hit & ~((addr_t)row_size_ - 1);
        cursor_addr_ = hit;
        refreshCache();
        setStatus("Hit 1/" + std::to_string(search_hits_.size()) + " at " + hex(hit) + "  (press 'n' for next)");
    }
    submode_ = SubMode::None;
    input_buf_.clear();
}

void InteractiveMemoryEditor::nextSearchHit() {
    if (search_hits_.empty()) { setStatus("No active search"); return; }
    search_idx_ = (search_idx_ + 1) % search_hits_.size();
    addr_t hit = search_hits_[search_idx_];
    base_addr_ = hit & ~((addr_t)row_size_ - 1);
    cursor_addr_ = hit;
    refreshCache();
    setStatus("Hit " + std::to_string(search_idx_ + 1) + "/" + std::to_string(search_hits_.size()) + " at " + hex(hit));
}

void InteractiveMemoryEditor::prevSearchHit() {
    if (search_hits_.empty()) { setStatus("No active search"); return; }
    search_idx_ = (search_idx_ + search_hits_.size() - 1) % search_hits_.size();
    addr_t hit = search_hits_[search_idx_];
    base_addr_ = hit & ~((addr_t)row_size_ - 1);
    cursor_addr_ = hit;
    refreshCache();
    setStatus("Hit " + std::to_string(search_idx_ + 1) + "/" + std::to_string(search_hits_.size()) + " at " + hex(hit));
}

void InteractiveMemoryEditor::followPointer() {
    // Read 8 bytes at cursor as a 64-bit pointer
    u64 ptr = 0;
    if (dbg_.readMemory(cursor_addr_, &ptr, 8)) {
        base_addr_ = ptr & ~((addr_t)row_size_ - 1);
        cursor_addr_ = ptr;
        refreshCache();
        setStatus("Followed pointer to " + hex(ptr));
    } else {
        setStatus("Read failed");
    }
}

void InteractiveMemoryEditor::toggleMode() {
    mode_ = (mode_ == Mode::Hex) ? Mode::Disassembly : Mode::Hex;
    refreshCache();
    setStatus(mode_ == Mode::Hex ? "Mode: Hex" : "Mode: Disassembly");
}

void InteractiveMemoryEditor::toggleRowSize() {
    if (row_size_ == 8) row_size_ = 16;
    else if (row_size_ == 16) row_size_ = 32;
    else row_size_ = 8;
    setStatus("Row size: " + std::to_string(row_size_));
}

// ===== Rendering =====

void InteractiveMemoryEditor::renderHeader() {
    // Top bar with mode + address + status
    setColor(0xE6E8F0, 0x252A40);
    std::cout << "\x1b[1;1H" << "NinjaDBG Memory Editor  -  ";
    setColor(0x00FFE1, 0x252A40);
    std::cout << (mode_ == Mode::Hex ? "[HEX]" : "[DISAS]");
    setColor(0xE6E8F0, 0x252A40);
    std::cout << "  -  base: " << hex(base_addr_)
              << "  cursor: " << hex(cursor_addr_)
              << "  row: " << std::to_string(row_size_);
    // Pad to end of line
    int used = 60 + 16 + 16 + 6;
    for (int i = used; i < cols_; i++) std::cout << ' ';
    resetColor();
}

void InteractiveMemoryEditor::renderHex() {
    int visible_rows = rows_ - 6;
    int hex_col = 22;
    int ascii_col = hex_col + row_size_ * 3 + 4;
    for (int r = 0; r < visible_rows; r++) {
        addr_t row_addr = base_addr_ + (addr_t)(r * row_size_);
        int y = r + 2;
        // Address column
        setColor(0x7AB7FF, 0x14161F);
        moveCursor(1, y);
        std::cout << hex(row_addr);
        resetColor();

        // Hex bytes
        for (int c = 0; c < row_size_; c++) {
            addr_t a = row_addr + c;
            u8 b = readByte(a);
            bool is_cursor = (a == cursor_addr_);
            char hex_buf[4];
            snprintf(hex_buf, sizeof(hex_buf), "%02x", b);
            int x = hex_col + c * 3;
            if (is_cursor) {
                setColor(0x14161F, 0x00FFE1);
                if (cursor_nibble_ == 0) {
                    std::cout << hex_buf[0];
                    setColor(0xE6E8F0, 0x14161F);
                    std::cout << hex_buf[1];
                } else {
                    setColor(0xE6E8F0, 0x14161F);
                    std::cout << hex_buf[0];
                    setColor(0x14161F, 0x00FFE1);
                    std::cout << hex_buf[1];
                }
                resetColor();
                std::cout << ' ';
            } else {
                setColor(0xE6E8F0, 0x14161F);
                moveCursor(x, y);
                std::cout << hex_buf << ' ';
            }
        }

        // ASCII column
        for (int c = 0; c < row_size_; c++) {
            addr_t a = row_addr + c;
            u8 b = readByte(a);
            char ch = (b >= 32 && b < 127) ? (char)b : '.';
            int x = ascii_col + c;
            bool is_cursor = (a == cursor_addr_);
            if (is_cursor) {
                setColor(0x14161F, 0x00FFE1);
            } else {
                setColor(0x8A8FA3, 0x14161F);
            }
            moveCursor(x, y);
            std::cout << ch;
            resetColor();
        }
    }
}

void InteractiveMemoryEditor::renderDisassembly() {
    // Read enough bytes from base_addr_ to fill visible_rows instructions
    int visible_rows = rows_ - 6;
    auto bytes = dbg_.readMemoryVec(base_addr_, visible_rows * 15 + 32);
    if (bytes.empty()) return;
    auto instrs = disas_.disassemble(base_addr_, bytes.data(), bytes.size(), visible_rows);
    for (int i = 0; i < (int)instrs.size() && i < visible_rows; i++) {
        int y = i + 2;
        auto& ins = instrs[i];
        bool is_cursor = (ins.address <= cursor_addr_ && ins.address + ins.length > cursor_addr_);
        if (is_cursor) {
            setColor(0x14161F, 0x003D3D);
        } else {
            setColor(0xE6E8F0, 0x14161F);
        }
        moveCursor(1, y);
        // Address
        setColor(is_cursor ? 0x00FFE1 : 0x7AB7FF, is_cursor ? 0x003D3D : 0x14161F);
        std::cout << hex(ins.address) << "  ";
        // Bytes
        setColor(is_cursor ? 0xE6E8F0 : 0x8A8FA3, is_cursor ? 0x003D3D : 0x14161F);
        char hex_buf[64] = {0};
        for (size_t k = 0; k < ins.length && k < 8; k++) {
            char b[4];
            snprintf(b, sizeof(b), "%02x ", ins.bytes[k]);
            strncat(hex_buf, b, sizeof(hex_buf) - strlen(hex_buf) - 1);
        }
        char padded[64];
        snprintf(padded, sizeof(padded), "%-24s", hex_buf);
        std::cout << padded;
        // Mnemonic
        setColor(is_cursor ? 0xFFB454 : 0xFFB454, is_cursor ? 0x003D3D : 0x14161F);
        std::cout << ins.mnemonic;
        // Padding to align operands
        for (int k = strlen(ins.mnemonic); k < 8; k++) std::cout << ' ';
        // Operands
        setColor(is_cursor ? 0x00FFE1 : 0xE6E8F0, is_cursor ? 0x003D3D : 0x14161F);
        std::cout << ins.operands;
        resetColor();
    }
}

void InteractiveMemoryEditor::renderFooter() {
    int y = rows_ - 3;
    setColor(0xE6E8F0, 0x252A40);
    moveCursor(1, y);
    std::cout << "Keys: arrows=move  e=edit  s=seek  /=search  n/N=next/prev hit  f=follow ptr  m=mode  w=row size  q=quit";
    // Pad
    int used = 105;
    for (int i = used; i < cols_; i++) std::cout << ' ';
    resetColor();

    y = rows_ - 2;
    moveCursor(1, y);
    setColor(0x00FFE1, 0x14161F);
    std::cout << "Status: ";
    setColor(0xE6E8F0, 0x14161F);
    std::cout << status_msg_;
    for (int i = 9 + status_msg_.size(); i < cols_; i++) std::cout << ' ';
    resetColor();
}

void InteractiveMemoryEditor::renderModal() {
    if (submode_ == SubMode::None) return;
    int y = rows_ - 1;
    moveCursor(1, y);
    setColor(0x14161F, 0xFFB454);
    std::string prompt;
    switch (submode_) {
        case SubMode::Edit:   prompt = "EDIT > "; break;
        case SubMode::Seek:   prompt = "SEEK > "; break;
        case SubMode::Search: prompt = "SEARCH > "; break;
        default: return;
    }
    std::cout << prompt << input_buf_ << '_';
    for (int i = prompt.size() + input_buf_.size() + 1; i < cols_; i++) std::cout << ' ';
    resetColor();
}

void InteractiveMemoryEditor::render() {
    renderHeader();
    if (mode_ == Mode::Hex) renderHex();
    else renderDisassembly();
    renderFooter();
    renderModal();
    refreshScreen();
}

// ===== Main loop =====

void InteractiveMemoryEditor::run(addr_t start_addr, Mode initial_mode) {
    base_addr_ = start_addr & ~((addr_t)row_size_ - 1);
    cursor_addr_ = start_addr;
    mode_ = initial_mode;
    enableRawMode();
    clearScreen();
    setStatus("Ready  -  press 'h' for help");

    bool running = true;
    while (running) {
        render();
        int ch;
        Key k = readKey(ch);
        if (submode_ != SubMode::None) {
            // Modal input handling
            if (k == Key::Escape) {
                submode_ = SubMode::None;
                input_buf_.clear();
                setStatus("Cancelled");
                continue;
            }
            if (k == Key::Enter) {
                switch (submode_) {
                    case SubMode::Edit:   applyEditInput(); break;
                    case SubMode::Seek:   applySeekInput(); break;
                    case SubMode::Search: applySearchInput(); break;
                    default: break;
                }
                continue;
            }
            if (k == Key::Backspace && !input_buf_.empty()) {
                input_buf_.pop_back();
                continue;
            }
            if (k == Key::Char) {
                // Accept hex chars / digits / x for 0x prefix / spaces
                char c = (char)ch;
                if (submode_ == SubMode::Search) {
                    if (isxdigit(c) || c == ' ' || c == 'x' || c == 'X') input_buf_.push_back(c);
                } else {
                    if (isxdigit(c) || c == 'x' || c == 'X') input_buf_.push_back(c);
                }
                continue;
            }
            continue;
        }

        switch (k) {
            case Key::Up:    moveCursorUp(); break;
            case Key::Down:  moveCursorDown(); break;
            case Key::Left:  moveCursorLeft(); break;
            case Key::Right: moveCursorRight(); break;
            case Key::PageUp:   pageUp(); break;
            case Key::PageDown: pageDown(); break;
            case Key::Home: cursor_addr_ = base_addr_; cursor_nibble_ = 0; break;
            case Key::End:  cursor_addr_ = base_addr_ + (addr_t)(row_size_ * (rows_ - 7)); cursor_nibble_ = 0; break;
            case Key::Char:
                switch (ch) {
                    case 'q': case 'Q': running = false; break;
                    case 'e': case 'E': startEdit(); break;
                    case 's': case 'S': startSeek(); break;
                    case '/': startSearch(); break;
                    case 'n': nextSearchHit(); break;
                    case 'N': prevSearchHit(); break;
                    case 'f': case 'F': followPointer(); break;
                    case 'm': case 'M': toggleMode(); break;
                    case 'w': case 'W': toggleRowSize(); break;
                    case 'h': case 'H':
                        setStatus("Help: arrows=move, e=edit, s=seek, /=search, n/N=hits, f=follow, m=mode, w=rowsize, q=quit");
                        break;
                }
                break;
            default: break;
        }
    }
    disableRawMode();
    clearScreen();
}

} // namespace ndbg
