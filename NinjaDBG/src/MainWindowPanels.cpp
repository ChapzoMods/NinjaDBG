// NinjaDBG v1.0.1 - MainWindow implementation (part 2: painting & panels)
// Closed Source - Free - by Chapzoo
#include "MainWindow.h"
#include "UITheme.h"
#include <sstream>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ndbg::ui {

// Color helpers
static void set_color(cairo_t* cr, u32 c) {
    double a = ((c >> 24) & 0xFF) / 255.0;
    double r = ((c >> 16) & 0xFF) / 255.0;
    double g = ((c >>  8) & 0xFF) / 255.0;
    double b = ((c      ) & 0xFF) / 255.0;
    cairo_set_source_rgba(cr, r, g, b, a);
}

static void set_color_rgba(cairo_t* cr, double r, double g, double b, double a) {
    cairo_set_source_rgba(cr, r, g, b, a);
}

void MainWindow::fillRect(LayoutRect r, u32 color) {
    set_color(cr_, color);
    cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
    cairo_fill(cr_);
}
void MainWindow::strokeRect(LayoutRect r, u32 color, double lw) {
    set_color(cr_, color);
    cairo_set_line_width(cr_, lw);
    cairo_rectangle(cr_, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
    cairo_stroke(cr_);
}
void MainWindow::fillRounded(LayoutRect r, double radius, u32 color) {
    set_color(cr_, color);
    double x = r.x, y = r.y, w = r.w, h = r.h, rr = radius;
    cairo_new_sub_path(cr_);
    cairo_arc(cr_, x + w - rr, y + rr,     rr, -M_PI/2, 0);
    cairo_arc(cr_, x + w - rr, y + h - rr, rr, 0, M_PI/2);
    cairo_arc(cr_, x + rr,     y + h - rr, rr, M_PI/2, M_PI);
    cairo_arc(cr_, x + rr,     y + rr,     rr, M_PI, 1.5*M_PI);
    cairo_close_path(cr_);
    cairo_fill(cr_);
}
void MainWindow::strokeRounded(LayoutRect r, double radius, u32 color, double lw) {
    set_color(cr_, color);
    cairo_set_line_width(cr_, lw);
    double x = r.x, y = r.y, w = r.w, h = r.h, rr = radius;
    cairo_new_sub_path(cr_);
    cairo_arc(cr_, x + w - rr, y + rr,     rr, -M_PI/2, 0);
    cairo_arc(cr_, x + w - rr, y + h - rr, rr, 0, M_PI/2);
    cairo_arc(cr_, x + rr,     y + h - rr, rr, M_PI/2, M_PI);
    cairo_arc(cr_, x + rr,     y + rr,     rr, M_PI, 1.5*M_PI);
    cairo_close_path(cr_);
    cairo_stroke(cr_);
}

void MainWindow::drawText(const std::string& s, int x, int y, u32 color,
                          const char* family, int size) {
    set_color(cr_, color);
    PangoLayout* lay = pango_cairo_create_layout(cr_);
    PangoFontDescription* fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_size(fd, size * PANGO_SCALE);
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(lay, s.c_str(), s.size());
    cairo_move_to(cr_, x, y);
    pango_cairo_show_layout(cr_, lay);
    g_object_unref(lay);
}

void MainWindow::drawMono(const std::string& s, int x, int y, u32 color, int size) {
    drawText(s, x, y, color, font::Mono, size);
}

void MainWindow::drawPanel(LayoutRect r, const std::string& title) {
    fillRect(r, col::BG_Panel);
    // Title bar
    LayoutRect tb = { r.x, r.y, r.w, 24 };
    fillRect(tb, col::BG_Elevated);
    drawText(title, r.x + 10, r.y + 4, col::Accent, font::Title, 12);
    // Border
    strokeRect(r, col::Border);
    // Accent line under title
    set_color(cr_, col::Accent_Dim);
    cairo_set_line_width(cr_, 1);
    cairo_move_to(cr_, r.x, r.y + 24.5);
    cairo_line_to(cr_, r.x + r.w, r.y + 24.5);
    cairo_stroke(cr_);
}

// Minimal x86-64 disassembler (covers common opcodes — enough for a debugger UI)
std::vector<MainWindow::Instr> MainWindow::disassemble(addr_t start, size_t n) {
    std::vector<Instr> out;
    if (dbg_.pid() == 0) return out;
    std::vector<u8> code = dbg_.readMemoryVec(start, n * 15 + 32);
    if (code.empty()) return out;
    size_t i = 0;
    size_t made = 0;
    while (i < code.size() && made < n) {
        Instr ins;
        ins.addr = start + i;
        ins.len = 0;
        memset(ins.text, 0, sizeof(ins.text));

        // Skip prefixes
        size_t pfx = i;
        bool has_66 = false, has_67 = false, rex_w = false;
        while (pfx < code.size()) {
            u8 b = code[pfx];
            if (b == 0x66) { has_66 = true; pfx++; continue; }
            if (b == 0x67) { has_67 = true; pfx++; continue; }
            if (b >= 0xF0 && b <= 0xF3) { pfx++; continue; }
            if ((b & 0xF0) == 0x40) { rex_w = (b & 0x8) != 0; pfx++; continue; }
            break;
        }
        if (pfx >= code.size()) break;

        u8 op = code[pfx];
        u8* pb = ins.bytes;
        size_t len = 0;
        // Save prefix bytes
        for (size_t k = i; k < pfx; k++) pb[len++] = code[k];

        auto reg = [](int r) -> const char* {
            const char* n[] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
                               "r8","r9","r10","r11","r12","r13","r14","r15"};
            return n[r & 0xF];
        };

        const char* mnem = "";
        char buf[64];
        bool handled = false;

        if (op == 0xCC) {
            mnem = "int3"; len++; pb[len-1] = op;
            handled = true;
        } else if (op == 0xC3) {
            mnem = "ret"; len++; pb[len-1] = op;
            handled = true;
        } else if (op == 0x90) {
            mnem = "nop"; len++; pb[len-1] = op;
            handled = true;
        } else if (op == 0xE8) {
            // call rel32
            if (pfx + 5 <= code.size()) {
                int32_t rel = (int32_t)((u32)code[pfx+1] | ((u32)code[pfx+2]<<8) |
                              ((u32)code[pfx+3]<<16) | ((u32)code[pfx+4]<<24));
                addr_t tgt = ins.addr + (pfx - i) + 5 + rel;
                snprintf(buf, sizeof(buf), "call %s", hex(tgt).c_str());
                mnem = buf;
                for (int k = 0; k < 5; k++) { pb[len++] = code[pfx + k]; }
                handled = true;
            }
        } else if (op == 0xE9) {
            if (pfx + 5 <= code.size()) {
                int32_t rel = (int32_t)((u32)code[pfx+1] | ((u32)code[pfx+2]<<8) |
                              ((u32)code[pfx+3]<<16) | ((u32)code[pfx+4]<<24));
                addr_t tgt = ins.addr + (pfx - i) + 5 + rel;
                snprintf(buf, sizeof(buf), "jmp %s", hex(tgt).c_str());
                mnem = buf;
                for (int k = 0; k < 5; k++) { pb[len++] = code[pfx + k]; }
                handled = true;
            }
        } else if (op == 0xEB) {
            if (pfx + 2 <= code.size()) {
                int8_t rel = (int8_t)code[pfx+1];
                addr_t tgt = ins.addr + (pfx - i) + 2 + rel;
                snprintf(buf, sizeof(buf), "jmp short %s", hex(tgt).c_str());
                mnem = buf;
                pb[len++] = op; pb[len++] = code[pfx+1];
                handled = true;
            }
        } else if ((op & 0xF0) == 0x70) {
            // Jcc rel8
            static const char* cc[] = {"jo","jno","jb","jae","je","jne","jbe","ja",
                                       "js","jns","jp","jnp","jl","jge","jle","jg"};
            if (pfx + 2 <= code.size()) {
                int8_t rel = (int8_t)code[pfx+1];
                addr_t tgt = ins.addr + (pfx - i) + 2 + rel;
                snprintf(buf, sizeof(buf), "%s %s", cc[op & 0xF], hex(tgt).c_str());
                mnem = buf;
                pb[len++] = op; pb[len++] = code[pfx+1];
                handled = true;
            }
        } else if (op == 0x55) { mnem = "push rbp"; len++; pb[len-1]=op; handled=true; }
        else if (op == 0x5D) { mnem = "pop rbp"; len++; pb[len-1]=op; handled=true; }
        else if (op == 0x50 || op == 0x51 || op == 0x52 || op == 0x53 ||
                 op == 0x54 || op == 0x56 || op == 0x57) {
            static const char* rn[] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi"};
            snprintf(buf, sizeof(buf), "push %s", rn[op - 0x50]);
            mnem = buf; len++; pb[len-1] = op; handled = true;
        } else if (op == 0x48 && pfx + 3 <= code.size()) {
            // REX.W common
            u8 op2 = code[pfx+1];
            if (op2 == 0x89) {
                u8 modrm = code[pfx+2];
                int src = (modrm >> 3) & 7;
                int dst = modrm & 7;
                if ((modrm >> 6) == 3) {
                    snprintf(buf, sizeof(buf), "mov %s, %s", reg(dst), reg(src));
                    mnem = buf;
                    for (int k = 0; k < 3; k++) pb[len++] = code[pfx+k];
                    handled = true;
                }
            } else if (op2 == 0x8B) {
                u8 modrm = code[pfx+2];
                int dst = (modrm >> 3) & 7;
                int src = modrm & 7;
                if ((modrm >> 6) == 3) {
                    snprintf(buf, sizeof(buf), "mov %s, %s", reg(dst), reg(src));
                    mnem = buf;
                    for (int k = 0; k < 3; k++) pb[len++] = code[pfx+k];
                    handled = true;
                }
            } else if (op2 == 0x83) {
                if (pfx + 4 <= code.size()) {
                    u8 modrm = code[pfx+2];
                    int reg_id = (modrm >> 3) & 7;
                    static const char* sub[]={"add","or","adc","sbb","and","sub","xor","cmp"};
                    int8_t imm = (int8_t)code[pfx+3];
                    snprintf(buf, sizeof(buf), "%s %s, 0x%x", sub[reg_id], reg(modrm & 7), (u8)imm);
                    mnem = buf;
                    for (int k = 0; k < 4; k++) pb[len++] = code[pfx+k];
                    handled = true;
                }
            } else if (op2 == 0x81) {
                if (pfx + 7 <= code.size()) {
                    u8 modrm = code[pfx+2];
                    int reg_id = (modrm >> 3) & 7;
                    static const char* sub[]={"add","or","adc","sbb","and","sub","xor","cmp"};
                    u32 imm = (u32)code[pfx+3] | ((u32)code[pfx+4]<<8) |
                              ((u32)code[pfx+5]<<16) | ((u32)code[pfx+6]<<24);
                    snprintf(buf, sizeof(buf), "%s %s, 0x%x", sub[reg_id], reg(modrm & 7), imm);
                    mnem = buf;
                    for (int k = 0; k < 7; k++) pb[len++] = code[pfx+k];
                    handled = true;
                }
            } else if (op2 == 0xFF) {
                u8 modrm = code[pfx+2];
                int reg_id = (modrm >> 3) & 7;
                if (reg_id == 2) mnem = "call [reg]";
                else if (reg_id == 4) mnem = "jmp [reg]";
                else if (reg_id == 6) mnem = "push [reg]";
                else mnem = "ff /?";
                for (int k = 0; k < 3; k++) pb[len++] = code[pfx+k];
                handled = true;
            }
        } else if (op == 0xFF) {
            u8 modrm = code[pfx+1];
            int reg_id = (modrm >> 3) & 7;
            if (reg_id == 2) mnem = "call [reg]";
            else if (reg_id == 4) mnem = "jmp [reg]";
            else if (reg_id == 6) mnem = "push [reg]";
            else mnem = "ff /?";
            for (int k = 0; k < 2; k++) pb[len++] = code[pfx+k];
            handled = true;
        } else if (op == 0xC9) { mnem = "leave"; len++; pb[len-1]=op; handled = true; }

        if (!handled) {
            // Unknown — display as .byte
            snprintf(buf, sizeof(buf), "db 0x%02X", op);
            mnem = buf;
            pb[len++] = code[pfx];
        }

        ins.len = (u8)len;
        strncpy(ins.text, mnem, sizeof(ins.text) - 1);
        out.push_back(ins);
        i = pfx + (len - (pfx - i)); // advance past instruction
        if (i <= pfx) i = pfx + 1;   // safety
        made++;
    }
    return out;
}

void MainWindow::paint() {
    // Background
    fillRect({0, 0, win_w_, win_h_}, col::BG);

    // Layout
    LayoutRect toolbar = { 0, 0, win_w_, lay::TOOLBAR_H };
    LayoutRect statusbar = { 0, win_h_ - lay::STATUSBAR_H, win_w_, lay::STATUSBAR_H };
    LayoutRect left = { 0, lay::TOOLBAR_H, lay::LEFT_W, win_h_ - lay::TOOLBAR_H - lay::STATUSBAR_H - lay::BOTTOM_H };
    LayoutRect right = { win_w_ - lay::RIGHT_W, lay::TOOLBAR_H, lay::RIGHT_W, win_h_ - lay::TOOLBAR_H - lay::STATUSBAR_H - lay::BOTTOM_H };
    LayoutRect center = { lay::LEFT_W, lay::TOOLBAR_H, win_w_ - lay::LEFT_W - lay::RIGHT_W, win_h_ - lay::TOOLBAR_H - lay::STATUSBAR_H - lay::BOTTOM_H };
    LayoutRect bottom = { lay::LEFT_W, win_h_ - lay::STATUSBAR_H - lay::BOTTOM_H, win_w_ - lay::LEFT_W - lay::RIGHT_W, lay::BOTTOM_H };

    paintToolbar(toolbar);

    // Left panel: process list
    {
        LayoutRect r = left;
        drawPanel(r, "PROCESS LIST");
        // Refresh process list if empty
        if (proc_cache_.empty()) refreshProcessList();

        LayoutRect inner = { r.x + 8, r.y + 30, r.w - 16, r.h - 38 };
        // Column header
        drawMono("PID     NAME", inner.x, inner.y, col::Text_Dim, 11);
        int y = inner.y + 16;
        int max_rows = (inner.h - 16) / 16;
        for (int i = 0; i < (int)proc_cache_.size() && i < max_rows; i++) {
            auto& p = proc_cache_[i];
            bool sel = (p.pid == selected_pid_);
            if (sel) {
                fillRect({inner.x - 4, y - 2, inner.w + 8, 16}, col::BG_Selected);
            }
            std::ostringstream s;
            s << std::setw(6) << std::left << p.pid << "  " << p.name;
            drawMono(s.str(), inner.x, y, sel ? col::Accent : col::Text, 11);
            y += 16;
        }
        // Footer info
        std::ostringstream fc;
        fc << proc_cache_.size() << " processes";
        drawText(fc.str(), inner.x, r.y + r.h - 18, col::Text_Muted, font::Sans, 10);
    }

    // Center: split into disassembly + memory + registers + stack
    {
        LayoutRect r = center;
        int pad = 6;
        int reg_h = 130;
        int mem_h = (r.h - reg_h - pad*3) * 5 / 8;
        int stk_h = r.h - reg_h - mem_h - pad*3;

        LayoutRect disasm_r = { r.x, r.y, r.w, reg_h };
        LayoutRect mem_r     = { r.x, r.y + reg_h + pad, r.w, mem_h };
        LayoutRect stk_r     = { r.x, r.y + reg_h + mem_h + pad*2, r.w, stk_h };

        drawDisassembly(disasm_r);
        drawMemoryHex(mem_r);
        drawStack(stk_r);
    }

    // Right panel: anti-detect + registers + breakpoints
    {
        LayoutRect r = right;
        int pad = 6;
        int reg_h = 220;
        int ad_h = 280;
        LayoutRect reg_r = { r.x, r.y, r.w, reg_h };
        LayoutRect ad_r  = { r.x, r.y + reg_h + pad, r.w, ad_h };
        LayoutRect th_r  = { r.x, r.y + reg_h + ad_h + pad*2, r.w, r.h - reg_h - ad_h - pad*2 };

        drawRegisters(reg_r);
        drawAntiDetect(ad_r);
        drawThreads(th_r);
    }

    // Bottom: breakpoints + console
    paintStatusBar(statusbar);
    drawBreakpoints(bottom);

    // Modals on top
    if (modal_ == Modal::About)         paintAboutModal();
    if (modal_ == Modal::ProcessPicker) paintProcessPickerModal();
    if (modal_ == Modal::AttachInput)   paintAttachInputModal();
}

void MainWindow::paintToolbar(LayoutRect r) {
    fillRect(r, col::BG_Elevated);
    // Bottom accent
    set_color(cr_, col::Accent_Dim);
    cairo_set_line_width(cr_, 2);
    cairo_move_to(cr_, r.x, r.y + r.h - 1);
    cairo_line_to(cr_, r.x + r.w, r.y + r.h - 1);
    cairo_stroke(cr_);

    // Logo (left)
    if (logo_surf_) {
        int ls = 40;
        cairo_save(cr_);
        cairo_scale(cr_, (double)ls/128, (double)ls/128);
        cairo_set_source_surface(cr_, logo_surf_, 8.0*128/ls, 8.0*128/ls);
        cairo_paint(cr_);
        cairo_restore(cr_);
    }
    // Title
    drawText("NinjaDBG", 56, 12, col::Text, font::Title, 16);
    drawText("v1.0.1", 56, 32, col::Accent, font::Sans, 10);

    // Buttons
    int bx = 160;
    int by = (r.h - 32) / 2;
    int gap = 4;
    for (auto& b : toolbar_buttons_) {
        if (b.label == "|") {
            // separator
            set_color(cr_, col::Border);
            cairo_set_line_width(cr_, 1);
            cairo_move_to(cr_, bx + 4, by + 4);
            cairo_line_to(cr_, bx + 4, by + 28);
            cairo_stroke(cr_);
            bx += 12;
            continue;
        }
        bool en = (dbg_.pid() != 0) || (b.label == "Launch" || b.label == "Attach" || b.label == "About");
        b.enabled = en;
        LayoutRect br = { bx, by, 0, 32 };
        int text_w = b.label.size() * 7 + 16;
        br.w = text_w;
        b.rect = br;

        u32 bg = b.pressed ? col::BG_Hover : (b.hover ? col::BG_Hover : col::BG_Panel);
        u32 fg = en ? (b.hover ? col::Accent : col::Text) : col::Text_Muted;
        fillRounded(br, 5, bg);
        if (b.hover && en) {
            strokeRounded(br, 5, col::Accent_Dim, 1);
        }
        drawText(b.label, br.x + 8, br.y + 9, fg, font::Sans, 12);
        bx += br.w + gap;
    }
}

void MainWindow::paintStatusBar(LayoutRect r) {
    fillRect(r, col::BG_Elevated);
    set_color(cr_, col::Accent_Dim);
    cairo_set_line_width(cr_, 1);
    cairo_move_to(cr_, r.x, r.y + 0.5);
    cairo_line_to(cr_, r.x + r.w, r.y + 0.5);
    cairo_stroke(cr_);

    // Status text
    u32 sc = col::Text_Dim;
    std::string icon = "\xE2\x97\x8F";
    if (dbg_.pid() == 0) { sc = col::Text_Muted; icon = "\xE2\x97\x8B"; }
    else if (dbg_.state() == DebuggerCore::RunState::Stopped) { sc = col::Warn; icon = "\xE2\x97\x89"; }
    else if (dbg_.state() == DebuggerCore::RunState::Running) { sc = col::OK; icon = "\xE2\x96\xB6"; }
    drawText(icon + "  " + status_, r.x + 12, r.y + 6, sc, font::Sans, 12);

    // PID
    if (dbg_.pid() != 0) {
        std::ostringstream s;
        s << "PID: " << dbg_.pid() << "  |  State: ";
        switch (dbg_.state()) {
            case DebuggerCore::RunState::Idle:    s << "Idle"; break;
            case DebuggerCore::RunState::Attached:s << "Attached"; break;
            case DebuggerCore::RunState::Running: s << "Running"; break;
            case DebuggerCore::RunState::Stopped: s << "Stopped"; break;
            case DebuggerCore::RunState::Exited:  s << "Exited"; break;
        }
        s << "  |  Anti-detect: ACTIVE";
        drawText(s.str(), r.x + r.w - 460, r.y + 6, col::Accent, font::Mono, 11);
    }

    // Right corner: author
    drawText("Closed Source - Free - by Chapzoo", r.x + r.w - 230, r.y + 6, col::Text_Muted, font::Sans, 11);
}

void MainWindow::drawDisassembly(LayoutRect r) {
    drawPanel(r, "DISASSEMBLY  (x86-64)");
    LayoutRect inner = { r.x + 8, r.y + 30, r.w - 16, r.h - 38 };

    if (dbg_.pid() == 0) {
        drawText("No process attached. Use [Attach] or [Launch] in the toolbar.",
                 inner.x, inner.y, col::Text_Muted, font::Sans, 12);
        return;
    }

    auto instrs = disassemble(disasm_addr_, std::max((size_t)1, (size_t)(inner.h / 16)));
    int y = inner.y;
    int row = 0;
    for (auto& ins : instrs) {
        // Highlight current RIP
        bool is_rip = (ins.addr == reg_cache_.rip);
        if (is_rip) {
            fillRect({inner.x - 4, y - 1, inner.w + 8, 16}, col::BG_Selected);
            drawText("\xE2\x96\xB6", inner.x - 12, y, col::Warn, font::Sans, 10);
        }
        // Check if breakpoint
        bool is_bp = false;
        for (auto& b : bps_cache_) {
            if (b.address == ins.addr) { is_bp = true; break; }
        }
        if (is_bp && !is_rip) {
            drawText("\xE2\x97\x8F", inner.x - 12, y, col::Error, font::Sans, 10);
        }
        // Address
        drawMono(hex(ins.addr), inner.x, y, col::Info, 11);
        // Bytes
        std::string bs;
        for (int k = 0; k < ins.len && k < 8; k++) {
            bs += hex2(ins.bytes[k]) + " ";
        }
        drawMono(bs, inner.x + 130, y, col::Text_Dim, 11);
        // Mnemonic
        drawMono(ins.text, inner.x + 260, y, is_rip ? col::Accent : col::Text, 11);
        y += 16;
        row++;
        if (y > inner.y + inner.h - 16) break;
    }
}

void MainWindow::drawMemoryHex(LayoutRect r) {
    drawPanel(r, "MEMORY DUMP  (hex)");
    LayoutRect inner = { r.x + 8, r.y + 30, r.w - 16, r.h - 38 };

    if (dbg_.pid() == 0) {
        drawText("Attach to a process to view memory.", inner.x, inner.y, col::Text_Muted, font::Sans, 12);
        return;
    }

    size_t bytes_per_row = 16;
    size_t rows = std::max((size_t)1, (size_t)(inner.h / 16));
    size_t total = bytes_per_row * rows;
    auto data = dbg_.readMemoryVec(mem_dump_addr_, total);
    if (data.empty()) {
        drawText("Failed to read memory at " + hex(mem_dump_addr_), inner.x, inner.y, col::Error, font::Mono, 11);
        return;
    }
    int y = inner.y;
    for (size_t row = 0; row < rows; row++) {
        addr_t addr = mem_dump_addr_ + row * bytes_per_row;
        drawMono(hex(addr), inner.x, y, col::Info, 11);
        std::string hexs, asciis;
        for (size_t c = 0; c < bytes_per_row; c++) {
            size_t idx = row * bytes_per_row + c;
            if (idx < data.size()) {
                u8 b = data[idx];
                hexs += hex2(b) + " ";
                asciis += (b >= 32 && b < 127) ? (char)b : '.';
            } else {
                hexs += "   ";
                asciis += ' ';
            }
            if (c == 7) hexs += " ";
        }
        drawMono(hexs, inner.x + 130, y, col::Text, 11);
        drawMono(asciis, inner.x + 130 + bytes_per_row*3*8 + 18, y, col::Text_Dim, 11);
        y += 16;
        if (y > inner.y + inner.h - 16) break;
    }
}

void MainWindow::drawStack(LayoutRect r) {
    drawPanel(r, "STACK  (RSP)");
    LayoutRect inner = { r.x + 8, r.y + 30, r.w - 16, r.h - 38 };
    if (dbg_.pid() == 0) {
        drawText("No stack to display.", inner.x, inner.y, col::Text_Muted, font::Sans, 12);
        return;
    }
    size_t rows = std::max((size_t)1, (size_t)(inner.h / 16));
    auto data = dbg_.readMemoryVec(reg_cache_.rsp, rows * 8);
    int y = inner.y;
    for (size_t i = 0; i < rows; i++) {
        addr_t a = reg_cache_.rsp + i * 8;
        drawMono(hex(a), inner.x, y, col::Info, 11);
        u64 v = 0;
        if (i * 8 + 8 <= data.size()) {
            memcpy(&v, data.data() + i * 8, 8);
        }
        // Try to resolve symbol (just check if it falls in any mapped region)
        std::string sym;
        for (auto& m : maps_cache_) {
            if (v >= m.start && v < m.end) {
                sym = " <" + m.path + "+0x" + hex2(v - m.start, 0) + ">";
                break;
            }
        }
        drawMono(hex(v), inner.x + 130, y, col::Text, 11);
        drawMono(sym, inner.x + 280, y, col::Purple, 11);
        y += 16;
        if (y > inner.y + inner.h - 16) break;
    }
}

void MainWindow::drawRegisters(LayoutRect r) {
    drawPanel(r, "REGISTERS");
    LayoutRect inner = { r.x + 8, r.y + 30, r.w - 16, r.h - 38 };
    if (dbg_.pid() == 0) {
        drawText("Attach to view registers.", inner.x, inner.y, col::Text_Muted, font::Sans, 12);
        return;
    }
    u64* vals[] = {
        &reg_cache_.rax, &reg_cache_.rbx, &reg_cache_.rcx, &reg_cache_.rdx,
        &reg_cache_.rsi, &reg_cache_.rdi, &reg_cache_.rbp, &reg_cache_.rsp,
        &reg_cache_.r8,  &reg_cache_.r9,  &reg_cache_.r10, &reg_cache_.r11,
        &reg_cache_.r12, &reg_cache_.r13, &reg_cache_.r14, &reg_cache_.r15,
        &reg_cache_.rip, &reg_cache_.rflags
    };
    int cols = 2;
    int rows_per_col = 9;
    int x[2] = { inner.x, inner.x + inner.w / 2 };
    for (int i = 0; i < 18; i++) {
        int col = i / rows_per_col;
        int row = i % rows_per_col;
        int px = x[col];
        int py = inner.y + row * 16;
        drawMono(kRegNames[i], px, py, col::Accent2, 11);
        drawMono(hex(*vals[i]), px + 36, py,
                 (i == 16) ? col::Warn : col::Text, 11);  // RIP highlighted
    }
}

void MainWindow::drawAntiDetect(LayoutRect r) {
    drawPanel(r, "ANTI-DETECT  (stealth subsystem)");
    LayoutRect inner = { r.x + 8, r.y + 30, r.w - 16, r.h - 38 };

    auto techs = AntiDetect::allTechniques();
    int y = inner.y;
    for (size_t i = 0; i < techs.size(); i++) {
        bool on = anti_.isEnabled(techs[i]);
        // Toggle switch
        LayoutRect tg = { inner.x, y, 28, 16 };
        fillRounded(tg, 8, on ? col::Accent_Dim : col::BG_Hover);
        // Knob
        LayoutRect knob = { on ? tg.x + 14 : tg.x + 2, tg.y + 2, 12, 12 };
        fillRounded(knob, 6, on ? col::Accent : col::Text_Muted);

        drawText(AntiDetect::name(techs[i]), inner.x + 36, y, on ? col::Text : col::Text_Muted, font::Sans, 11);
        // Status badge
        drawText(on ? "ON" : "OFF", inner.x + inner.w - 36, y,
                 on ? col::OK : col::Text_Muted, font::Mono, 10);
        y += 22;
        // Description (smaller, dimmer) - only show 1 line
        std::string desc = AntiDetect::description(techs[i]);
        if (desc.size() > 60) desc = desc.substr(0, 57) + "...";
        drawText(desc, inner.x + 36, y, col::Text_Muted, font::Sans, 9);
        y += 16;
        if (y > inner.y + inner.h - 16) break;
    }
}

void MainWindow::drawThreads(LayoutRect r) {
    drawPanel(r, "THREADS");
    LayoutRect inner = { r.x + 8, r.y + 30, r.w - 16, r.h - 38 };
    if (dbg_.pid() == 0 || threads_cache_.empty()) {
        drawText("No threads.", inner.x, inner.y, col::Text_Muted, font::Sans, 12);
        return;
    }
    drawMono("TID      STATE  NAME", inner.x, inner.y, col::Text_Dim, 10);
    int y = inner.y + 16;
    for (auto& t : threads_cache_) {
        std::ostringstream s;
        s << std::left << std::setw(8) << t.tid << " " << std::setw(6) << t.state << "  " << t.name;
        drawMono(s.str(), inner.x, y, col::Text, 11);
        y += 16;
        if (y > inner.y + inner.h - 16) break;
    }
}

void MainWindow::drawBreakpoints(LayoutRect r) {
    drawPanel(r, "BREAKPOINTS  (click to toggle)");
    LayoutRect inner = { r.x + 8, r.y + 30, r.w - 16, r.h - 38 };
    if (bps_cache_.empty()) {
        drawText("No breakpoints set. Set one by right-clicking in disassembly (not implemented in this demo) "
                 "or use the console command 'bp <addr>'.",
                 inner.x, inner.y, col::Text_Muted, font::Sans, 11);
        return;
    }
    drawMono("ID  ADDR              TYPE    HITS  LABEL", inner.x, inner.y, col::Text_Dim, 11);
    int y = inner.y + 16;
    for (auto& b : bps_cache_) {
        std::ostringstream s;
        s << std::left << std::setw(4) << b.id << " "
          << hex(b.address) << "  "
          << (b.hardware ? "HW    " : "SW    ") << " "
          << std::setw(5) << b.hit_count << "  "
          << b.label;
        u32 c = b.enabled ? col::Text : col::Text_Muted;
        drawMono(s.str(), inner.x, y, c, 11);
        if (b.enabled) {
            drawText("\xE2\x97\x8F", inner.x - 12, y, col::Error, font::Sans, 10);
        }
        y += 16;
        if (y > inner.y + inner.h - 16) break;
    }
}

void MainWindow::paintAboutModal() {
    // Dim background
    set_color_rgba(cr_, 0, 0, 0, 0.6);
    cairo_paint(cr_);

    LayoutRect r = { win_w_/2 - 250, win_h_/2 - 200, 500, 400 };
    fillRounded(r, 12, col::BG_Panel);
    strokeRounded(r, 12, col::Accent, 2);

    // Logo
    if (logo_surf_) {
        int ls = 120;
        cairo_save(cr_);
        cairo_scale(cr_, (double)ls/128, (double)ls/128);
        cairo_set_source_surface(cr_, logo_surf_,
            (r.x + (r.w - ls)/2) * 128.0/ls, (r.y + 24) * 128.0/ls);
        cairo_paint(cr_);
        cairo_restore(cr_);
    }

    int y = r.y + 160;
    drawText("NinjaDBG", r.x + r.w/2 - 60, y, col::Text, font::Title, 28);
    y += 36;
    drawText("Version 1.0.1  -  Stealth Debugger", r.x + r.w/2 - 100, y, col::Accent, font::Sans, 14);
    y += 28;
    drawText("Closed Source  -  Free for all uses", r.x + r.w/2 - 110, y, col::Text_Dim, font::Sans, 12);
    y += 22;
    drawText("Created by Chapzoo (one person)", r.x + r.w/2 - 110, y, col::Text, font::Sans, 12);
    y += 30;
    drawText("Anti-detect techniques:", r.x + r.w/2 - 100, y, col::Warn, font::Sans, 12);
    y += 20;
    auto techs = AntiDetect::allTechniques();
    for (size_t i = 0; i < techs.size(); i++) {
        std::string s = std::string("  \xE2\x9C\x93  ") + AntiDetect::name(techs[i]);
        drawText(s, r.x + r.w/2 - 100, y,
                 anti_.isEnabled(techs[i]) ? col::OK : col::Text_Muted, font::Sans, 11);
        y += 16;
    }

    drawText("Click anywhere to close", r.x + r.w/2 - 60, r.y + r.h - 24,
             col::Text_Muted, font::Sans, 10);
}

void MainWindow::paintProcessPickerModal() {
    set_color_rgba(cr_, 0, 0, 0, 0.7);
    cairo_paint(cr_);

    LayoutRect r = { lay::PAD*4, lay::TOOLBAR_H + 60, win_w_ - lay::PAD*8, win_h_ - lay::TOOLBAR_H - 100 };
    fillRounded(r, 8, col::BG_Panel);
    strokeRounded(r, 8, col::Accent, 1.5);

    // Title
    drawText("Select Process to Attach", r.x + 16, r.y + 12, col::Accent, font::Title, 16);
    drawText("Use Up/Down to navigate, click Attach or press Esc to cancel",
             r.x + 16, r.y + 34, col::Text_Dim, font::Sans, 11);

    LayoutRect inner = { r.x + 16, r.y + 60, r.w - 32, r.h - 100 };
    drawMono("PID     NAME                                            MEM(kB)  STATE",
             inner.x, inner.y, col::Text_Dim, 11);
    int y = inner.y + 16;
    int max_rows = (inner.h - 16) / 16;
    for (int i = 0; i < (int)proc_list_.size() && i < max_rows; i++) {
        auto& p = proc_list_[i];
        bool sel = (i == proc_selected_);
        if (sel) {
            fillRect({inner.x - 4, y - 2, inner.w + 8, 16}, col::BG_Selected);
        }
        std::ostringstream s;
        s << std::left << std::setw(6) << p.pid << "  "
          << std::setw(46) << (p.name.size() > 46 ? p.name.substr(0, 43) + "..." : p.name) << "  "
          << std::right << std::setw(8) << p.memory_kb << "  "
          << std::left << std::setw(4) << p.state;
        drawMono(s.str(), inner.x, y, sel ? col::Accent : col::Text, 11);
        y += 16;
    }

    // Footer buttons
    LayoutRect ab = { r.x + r.w - 220, r.y + r.h - 36, 90, 28 };
    LayoutRect cb = { r.x + r.w - 110, r.y + r.h - 36, 90, 28 };
    fillRounded(ab, 5, col::Accent_Dim);
    drawText("Attach", ab.x + 20, ab.y + 7, col::Text, font::Sans, 12);
    fillRounded(cb, 5, col::BG_Hover);
    drawText("Cancel", cb.x + 20, cb.y + 7, col::Text, font::Sans, 12);
}

void MainWindow::paintAttachInputModal() {
    set_color_rgba(cr_, 0, 0, 0, 0.7);
    cairo_paint(cr_);

    LayoutRect r = { win_w_/2 - 200, win_h_/2 - 60, 400, 130 };
    fillRounded(r, 8, col::BG_Panel);
    strokeRounded(r, 8, col::Accent, 1.5);

    drawText("Enter PID to attach", r.x + 16, r.y + 12, col::Accent, font::Title, 14);
    drawText("(Type digits, Enter to confirm, Esc to cancel)", r.x + 16, r.y + 36, col::Text_Dim, font::Sans, 10);

    LayoutRect ib = { r.x + 16, r.y + 56, r.w - 32, 26 };
    fillRounded(ib, 4, col::BG);
    strokeRounded(ib, 4, col::Border, 1);
    drawMono(attach_input_ + "_", ib.x + 8, ib.y + 6, col::Accent, 13);

    LayoutRect ok = { r.x + 200, r.y + 92, 80, 26 };
    LayoutRect ca = { r.x + 290, r.y + 92, 80, 26 };
    fillRounded(ok, 5, col::Accent_Dim);
    drawText("OK", ok.x + 30, ok.y + 5, col::Text, font::Sans, 12);
    fillRounded(ca, 5, col::BG_Hover);
    drawText("Cancel", ca.x + 18, ca.y + 5, col::Text, font::Sans, 12);
}

} // namespace ndbg::ui
