// NinjaDBG v1.0.1 - MainWindow implementation (part 1: init, event loop, helpers)
// Closed Source - Free - by Chapzoo
#include "MainWindow.h"
#include "UITheme.h"
#include <X11/keysym.h>
#include <unistd.h>
#include <signal.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace ndbg::ui {

MainWindow::MainWindow() {
    anti_.buildPreloadPayload();
    dbg_.setAntiDetect(&anti_);
}
MainWindow::~MainWindow() {
    if (surf_) cairo_surface_destroy(surf_);
    if (cr_) cairo_destroy(cr_);
    if (logo_surf_) cairo_surface_destroy(logo_surf_);
    if (font_title_) pango_font_description_free(font_title_);
    if (font_body_)  pango_font_description_free(font_body_);
    if (font_mono_)  pango_font_description_free(font_mono_);
    if (dpy_) XCloseDisplay(dpy_);
}

bool MainWindow::init(int w, int h) {
    win_w_ = w; win_h_ = h;

    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_) {
        std::cerr << "Cannot open X display" << std::endl;
        return false;
    }
    screen_ = DefaultScreen(dpy_);

    win_ = XCreateSimpleWindow(dpy_, RootWindow(dpy_, screen_),
                               0, 0, w, h, 0,
                               0x14161F, 0x14161F);
    XSelectInput(dpy_, win_, ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask | FocusChangeMask);
    XStoreName(dpy_, win_, "NinjaDBG v1.0.1 - Stealth Debugger  [by Chapzoo]");

    // Set window icon hint (best-effort)
    XClassHint ch;
    ch.res_name  = (char*)"NinjaDBG";
    ch.res_class = (char*)"NinjaDBG";
    XSetClassHint(dpy_, win_, &ch);

    XMapWindow(dpy_, win_);
    wmDelete_ = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy_, win_, &wmDelete_, 1);

    surf_ = cairo_xlib_surface_create(dpy_, win_, DefaultVisual(dpy_, screen_), w, h);
    cr_   = cairo_create(surf_);

    font_title_ = pango_font_description_new();
    pango_font_description_set_family(font_title_, font::Title);
    pango_font_description_set_weight(font_title_, PANGO_WEIGHT_BOLD);
    pango_font_description_set_size(font_title_, lay::FONT_TITLE * PANGO_SCALE);

    font_body_ = pango_font_description_new();
    pango_font_description_set_family(font_body_, font::Sans);
    pango_font_description_set_size(font_body_, lay::FONT_BODY * PANGO_SCALE);

    font_mono_ = pango_font_description_new();
    pango_font_description_set_family(font_mono_, font::Mono);
    pango_font_description_set_size(font_mono_, lay::FONT_MONO * PANGO_SCALE);

    logo_surf_ = loadLogo();
    buildToolbar();

    // Enable anti-detect defaults
    log("NinjaDBG v1.0.1 initialized", "info");
    log("Stealth subsystem online — 6 techniques active", "ok");
    log("Closed Source - Free - Created by Chapzoo", "info");
    log("Click [Attach] to debug a running process, or [Launch] to start a new target.", "info");

    return true;
}

cairo_surface_t* MainWindow::loadLogo() {
    // Render the SVG to a cairo image surface via a simple RSVG approach.
    // Since librsvg may not be available, fall back to drawing the ninja
    // logo procedurally on a 128x128 surface.
    int S = 128;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, S, S);
    cairo_t* c = cairo_create(s);

    // Background
    cairo_set_source_rgba(c, 0.078, 0.086, 0.116, 1.0);
    cairo_paint(c);

    // Rounded rect border
    double r = 24;
    cairo_set_source_rgba(c, 0.0, 1.0, 0.882, 0.9);
    cairo_set_line_width(c, 1.5);
    cairo_move_to(c, r, 2);
    cairo_line_to(c, S-r, 2);
    cairo_arc(c, S-r, r, r-2, -M_PI/2, 0);
    cairo_line_to(c, S-2, S-r);
    cairo_arc(c, S-r, S-r, r-2, 0, M_PI/2);
    cairo_line_to(c, r, S-2);
    cairo_arc(c, r, S-r, r-2, M_PI/2, M_PI);
    cairo_line_to(c, 2, r);
    cairo_arc(c, r, r, r-2, M_PI, 1.5*M_PI);
    cairo_stroke(c);

    // Head (ellipse)
    cairo_set_source_rgba(c, 0.165, 0.184, 0.282, 1.0);
    cairo_arc(c, S/2, S/2 - 4, 32, 0, 2*M_PI);
    cairo_fill(c);

    // Headband
    cairo_set_source_rgba(c, 0.769, 0.118, 0.227, 1.0);
    cairo_rectangle(c, S/2 - 32, S/2 - 16, 64, 8);
    cairo_fill(c);

    // Headband knot tails
    cairo_move_to(c, S/2 + 32, S/2 - 14);
    cairo_line_to(c, S/2 + 42, S/2 - 18);
    cairo_line_to(c, S/2 + 40, S/2 - 11);
    cairo_line_to(c, S/2 + 48, S/2 - 9);
    cairo_line_to(c, S/2 + 36, S/2 - 8);
    cairo_close_path(c);
    cairo_fill(c);

    // Eye slit
    cairo_set_source_rgba(c, 0, 0, 0, 1);
    cairo_rectangle(c, S/2 - 22, S/2 - 2, 44, 8);
    cairo_fill(c);

    // Glowing eyes
    cairo_set_source_rgba(c, 0, 1.0, 0.882, 0.95);
    cairo_arc(c, S/2 - 11, S/2 + 2, 4, 0, 2*M_PI);
    cairo_fill(c);
    cairo_arc(c, S/2 + 11, S/2 + 2, 4, 0, 2*M_PI);
    cairo_fill(c);

    // Eye pupils (white)
    cairo_set_source_rgba(c, 1, 1, 1, 0.9);
    cairo_arc(c, S/2 - 11, S/2 + 2, 1.5, 0, 2*M_PI);
    cairo_fill(c);
    cairo_arc(c, S/2 + 11, S/2 + 2, 1.5, 0, 2*M_PI);
    cairo_fill(c);

    // Lower mask
    cairo_set_source_rgba(c, 0.165, 0.184, 0.282, 1.0);
    cairo_rectangle(c, S/2 - 26, S/2 + 12, 52, 18);
    cairo_fill(c);

    cairo_destroy(c);
    return s;
}

void MainWindow::run() {
    running_ = true;
    XEvent ev;
    // Pre-paint once
    paint();
    cairo_surface_flush(surf_);
    XFlush(dpy_);

    while (running_) {
        // Pump events with a small timeout via XPending + usleep
        while (XPending(dpy_)) {
            XNextEvent(dpy_, &ev);
            handleEvent(ev);
        }
        // Periodic repaint if needed (cheap check)
        if (need_paint_) {
            paint();
            cairo_surface_flush(surf_);
            need_paint_ = false;
            XFlush(dpy_);
        }
        usleep(8000); // ~120fps check
    }
}

void MainWindow::close() { running_ = false; }
void MainWindow::invalidate() { need_paint_ = true; }

void MainWindow::handleEvent(XEvent& ev) {
    switch (ev.type) {
        case Expose:
            if (ev.xexpose.count == 0) { paint(); }
            break;
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == wmDelete_) running_ = false;
            break;
        case ConfigureNotify:
            if (ev.xconfigure.width != win_w_ || ev.xconfigure.height != win_h_) {
                win_w_ = ev.xconfigure.width;
                win_h_ = ev.xconfigure.height;
                cairo_xlib_surface_set_size(surf_, win_w_, win_h_);
                invalidate();
            }
            break;
        case MotionNotify:
            mx_ = ev.xmotion.x; my_ = ev.xmotion.y;
            onMouseMove(mx_, my_);
            invalidate();
            break;
        case ButtonPress:
            mx_ = ev.xbutton.x; my_ = ev.xbutton.y;
            onMouseDown(mx_, my_, ev.xbutton.button);
            invalidate();
            break;
        case ButtonRelease:
            mx_ = ev.xbutton.x; my_ = ev.xbutton.y;
            onMouseUp(mx_, my_, ev.xbutton.button);
            invalidate();
            break;
        case KeyPress: {
            KeySym k = XLookupKeysym(&ev.xkey, 0);
            onKeyDown(k, ev.xkey.state);
            invalidate();
            break;
        }
    }
}

void MainWindow::onMouseMove(int x, int y) {
    for (auto& b : toolbar_buttons_) {
        b.hover = (x >= b.rect.x && x < b.rect.x + b.rect.w &&
                   y >= b.rect.y && y < b.rect.y + b.rect.h);
    }
}

void MainWindow::onMouseDown(int x, int y, int button) {
    if (button != 1) return;
    if (modal_ != Modal::NoModal) {
        // Modal handlers
        if (modal_ == Modal::About) {
            modal_ = Modal::NoModal;
            return;
        }
        if (modal_ == Modal::ProcessPicker) {
            // Check if click is on process list area
            LayoutRect r = { lay::PAD*4, lay::TOOLBAR_H + 60, win_w_ - lay::PAD*8, win_h_ - lay::TOOLBAR_H - 100 };
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                int idx = (y - r.y) / 18;
                if (idx >= 0 && idx < (int)proc_list_.size()) {
                    proc_selected_ = idx;
                    selected_pid_ = proc_list_[idx].pid;
                }
            }
            // Check attach button
            LayoutRect ab = { r.x + r.w - 100, r.y + r.h + 8, 90, 28 };
            if (x >= ab.x && x < ab.x + ab.w && y >= ab.y && y < ab.y + ab.h) {
                if (selected_pid_ != 0) {
                    if (dbg_.attach(selected_pid_)) {
                        log("Attached to pid " + std::to_string(selected_pid_), "ok");
                        // Read initial state
                        dbg_.readRegisters(reg_cache_);
                        maps_cache_ = dbg_.readMaps();
                        threads_cache_ = dbg_.readThreads();
                        bps_cache_ = dbg_.breakpoints();
                        disasm_addr_ = reg_cache_.rip;
                        mem_dump_addr_ = reg_cache_.rsp;
                        setStatus("Attached to pid " + std::to_string(selected_pid_) + " — paused");
                    } else {
                        log("Attach failed: " + dbg_.lastError(), "error");
                    }
                }
                modal_ = Modal::NoModal;
            }
            // Close button
            LayoutRect cb = { r.x + r.w - 200, r.y + r.h + 8, 90, 28 };
            if (x >= cb.x && x < cb.x + cb.w && y >= cb.y && y < cb.y + cb.h) {
                modal_ = Modal::NoModal;
            }
            return;
        }
        if (modal_ == Modal::AttachInput) {
            // OK / Cancel buttons
            LayoutRect r = { win_w_/2 - 200, win_h_/2 - 40, 400, 110 };
            LayoutRect ok = { r.x + 200, r.y + 70, 80, 28 };
            LayoutRect ca = { r.x + 290, r.y + 70, 80, 28 };
            if (x >= ok.x && x < ok.x + ok.w && y >= ok.y && y < ok.y + ok.h) {
                pid_t p = (pid_t)strtol(attach_input_.c_str(), nullptr, 10);
                if (p > 0) {
                    if (dbg_.attach(p)) {
                        log("Attached to pid " + std::to_string(p), "ok");
                        dbg_.readRegisters(reg_cache_);
                        maps_cache_ = dbg_.readMaps();
                        threads_cache_ = dbg_.readThreads();
                        bps_cache_ = dbg_.breakpoints();
                        disasm_addr_ = reg_cache_.rip;
                        mem_dump_addr_ = reg_cache_.rsp;
                        setStatus("Attached to pid " + std::to_string(p) + " — paused");
                    } else {
                        log("Attach failed: " + dbg_.lastError(), "error");
                    }
                }
                modal_ = Modal::NoModal;
            } else if (x >= ca.x && x < ca.x + ca.w && y >= ca.y && y < ca.y + ca.h) {
                modal_ = Modal::NoModal;
            }
            return;
        }
        return;
    }

    // Toolbar buttons
    if (auto* b = hitButton(x, y)) {
        b->pressed = true;
        if (b->action) b->action();
        b->pressed = false;
        return;
    }

    // Click on left panel: process selection
    LayoutRect left = { 0, lay::TOOLBAR_H, lay::LEFT_W, win_h_ - lay::TOOLBAR_H - lay::STATUSBAR_H - lay::BOTTOM_H };
    if (x >= left.x && x < left.x + left.w && y >= left.y + 30 && y < left.y + left.h) {
        int idx = (y - left.y - 30) / 16;
        if (idx >= 0 && idx < (int)proc_cache_.size()) {
            selected_pid_ = proc_cache_[idx].pid;
        }
    }

    // Click on right panel: anti-detect toggles
    LayoutRect right = { win_w_ - lay::RIGHT_W, lay::TOOLBAR_H, lay::RIGHT_W, win_h_ - lay::TOOLBAR_H - lay::STATUSBAR_H - lay::BOTTOM_H };
    if (x >= right.x + 12 && x < right.x + right.w - 12) {
        // Each technique occupies 32px starting at right.y + 60
        int base_y = right.y + 60;
        auto techs = AntiDetect::allTechniques();
        for (size_t i = 0; i < techs.size(); i++) {
            int ry = base_y + (int)i * 30;
            if (y >= ry && y < ry + 26) {
                // Click on the toggle at left
                LayoutRect tg = { right.x + 12, ry, 28, 18 };
                if (x >= tg.x && x < tg.x + tg.w + 200) {
                    if (anti_.isEnabled(techs[i])) {
                        anti_.disable(techs[i]);
                        log("Disabled: " + AntiDetect::name(techs[i]), "warn");
                    } else {
                        anti_.enable(techs[i]);
                        log("Enabled: " + AntiDetect::name(techs[i]), "ok");
                    }
                }
                break;
            }
        }
    }

    // Click on breakpoint list (bottom)
    LayoutRect bot = { lay::LEFT_W, win_h_ - lay::STATUSBAR_H - lay::BOTTOM_H, win_w_ - lay::LEFT_W - lay::RIGHT_W, lay::BOTTOM_H };
    if (x >= bot.x + 12 && x < bot.x + bot.w - 12 && y >= bot.y + 30 && y < bot.y + bot.h) {
        int idx = (y - bot.y - 30) / 16;
        if (idx >= 0 && idx < (int)bps_cache_.size()) {
            int id = bps_cache_[idx].id;
            if (dbg_.findBreakpointId(bps_cache_[idx].address) >= 0) {
                dbg_.disableBreakpoint(id);
                log("Disabled breakpoint #" + std::to_string(id), "warn");
            } else {
                dbg_.enableBreakpoint(id);
                log("Enabled breakpoint #" + std::to_string(id), "ok");
            }
            bps_cache_ = dbg_.breakpoints();
        }
    }
}

void MainWindow::onMouseUp(int x, int y, int button) {
    (void)x; (void)y; (void)button;
    for (auto& b : toolbar_buttons_) b.pressed = false;
    invalidate();
}

void MainWindow::onMouseWheel(int x, int y, int dir) {
    (void)x; (void)y; (void)dir;
}

void MainWindow::onKeyDown(KeySym k, unsigned int state) {
    if (modal_ == Modal::AttachInput) {
        if (k == XK_Return) {
            pid_t p = (pid_t)strtol(attach_input_.c_str(), nullptr, 10);
            if (p > 0) {
                if (dbg_.attach(p)) {
                    log("Attached to pid " + std::to_string(p), "ok");
                    dbg_.readRegisters(reg_cache_);
                    maps_cache_ = dbg_.readMaps();
                    threads_cache_ = dbg_.readThreads();
                    bps_cache_ = dbg_.breakpoints();
                    disasm_addr_ = reg_cache_.rip;
                    mem_dump_addr_ = reg_cache_.rsp;
                    setStatus("Attached to pid " + std::to_string(p) + " — paused");
                } else {
                    log("Attach failed: " + dbg_.lastError(), "error");
                }
            }
            modal_ = Modal::NoModal;
            return;
        }
        if (k == XK_Escape) { modal_ = Modal::NoModal; return; }
        if (k == XK_BackSpace && !attach_input_.empty()) {
            attach_input_.pop_back();
            return;
        }
        if (k >= XK_0 && k <= XK_9) {
            attach_input_.push_back((char)k);
            return;
        }
        return;
    }

    if (modal_ == Modal::ProcessPicker) {
        if (k == XK_Escape) modal_ = Modal::NoModal;
        if (k == XK_Up && proc_selected_ > 0) proc_selected_--;
        if (k == XK_Down && proc_selected_ < (int)proc_list_.size()-1) proc_selected_++;
        return;
    }

    if (k == XK_F5) actionContinue();
    if (k == XK_F10) actionStep();
    if (k == XK_F11) actionStepIn();
    if (k == XK_F8)  actionPause();
    if (k == XK_Escape && modal_ != Modal::NoModal) modal_ = Modal::NoModal;
}

void MainWindow::onResize(int w, int h) { win_w_ = w; win_h_ = h; invalidate(); }

void MainWindow::log(const std::string& msg, const std::string& level) {
    LogLine l; l.level = level; l.text = msg;
    log_lines_.push_back(l);
    if (log_lines_.size() > 200) log_lines_.erase(log_lines_.begin());
}

void MainWindow::registerPanel(Panel* p) { panels_.push_back(p); }

// Toolbar setup
void MainWindow::buildToolbar() {
    toolbar_buttons_.clear();
    auto add = [&](const std::string& label, const std::string& icon, std::function<void()> act) {
        Button b;
        b.label = label;
        b.icon  = icon;
        b.action = act;
        toolbar_buttons_.push_back(b);
    };
    add("Launch",  "\xE2\x96\xB6", [this]{ actionLaunch(); });
    add("Attach",  "\xE2\x9C\x94", [this]{ actionAttach(); });
    add("Detach",  "\xE2\x9C\x95", [this]{ actionDetach(); });
    add("|",       "",  nullptr);
    add("Run",     "\xE2\x96\xB6", [this]{ actionContinue(); });
    add("Pause",   "\xE2\x8F\xB8", [this]{ actionPause(); });
    add("Step",    "\xE2\x86\xB3", [this]{ actionStep(); });
    add("Step In", "\xE2\x86\x98", [this]{ actionStepIn(); });
    add("Step Out","\xE2\x86\x91", [this]{ actionStepOut(); });
    add("|",       "",  nullptr);
    add("Restart", "\xE2\x86\xBB", [this]{ actionRestart(); });
    add("Kill",    "\xE2\x98\xA0", [this]{ actionKill(); });
    add("|",       "",  nullptr);
    add("About",   "\xE2\x84\xB9", [this]{ actionAbout(); });
}

MainWindow::Button* MainWindow::hitButton(int x, int y) {
    for (auto& b : toolbar_buttons_) {
        if (x >= b.rect.x && x < b.rect.x + b.rect.w &&
            y >= b.rect.y && y < b.rect.y + b.rect.h) {
            return &b;
        }
    }
    return nullptr;
}

void MainWindow::refreshProcessList() {
    proc_cache_ = DebuggerCore::listProcesses();
    // Filter to user processes (skip kernel threads)
    proc_cache_.erase(
        std::remove_if(proc_cache_.begin(), proc_cache_.end(),
            [](const ProcessInfo& p) {
                return p.memory_kb == 0 && p.cmdline.empty();
            }),
        proc_cache_.end());
}

// Actions
void MainWindow::actionAttach() {
    refreshProcessList();
    proc_list_ = proc_cache_;
    proc_selected_ = 0;
    modal_ = Modal::ProcessPicker;
    log("Opening process picker", "info");
}
void MainWindow::actionLaunch() {
    attach_input_.clear();
    modal_ = Modal::AttachInput;
    log("Enter PID to attach to (typing a number won't launch, only attach)", "info");
}
void MainWindow::actionDetach() {
    if (dbg_.pid() == 0) { log("Not attached", "warn"); return; }
    dbg_.detach();
    log("Detached", "info");
    setStatus("Idle");
    bps_cache_.clear();
}
void MainWindow::actionContinue() {
    if (dbg_.pid() == 0) { log("Not attached", "warn"); return; }
    log("Continuing execution", "info");
    if (dbg_.cont()) {
        setStatus("Running...");
        int sig; bool ex; int code;
        if (dbg_.waitForStop(sig, ex, code)) {
            if (ex) {
                log("Target exited with code " + std::to_string(code), "warn");
                setStatus("Target exited");
            } else {
                log("Stopped — signal " + std::to_string(sig), "info");
                setStatus("Stopped (signal " + std::to_string(sig) + ")");
                dbg_.readRegisters(reg_cache_);
                bps_cache_ = dbg_.breakpoints();
                disasm_addr_ = reg_cache_.rip;
                mem_dump_addr_ = reg_cache_.rsp;
            }
        }
    }
}
void MainWindow::actionStep() {
    if (dbg_.pid() == 0) return;
    if (dbg_.step()) {
        dbg_.readRegisters(reg_cache_);
        disasm_addr_ = reg_cache_.rip;
        setStatus("Single-stepped");
        log("Step", "info");
    }
}
void MainWindow::actionStepIn() { actionStep(); }
void MainWindow::actionStepOut() {
    if (dbg_.pid() == 0) return;
    log("Step-out: not yet implemented (would set bp on return addr)", "warn");
}
void MainWindow::actionPause() {
    if (dbg_.pid() == 0) return;
    if (dbg_.stop()) {
        int sig; bool ex; int code;
        if (dbg_.waitForStop(sig, ex, code)) {
            dbg_.readRegisters(reg_cache_);
            disasm_addr_ = reg_cache_.rip;
            setStatus("Paused");
            log("Paused by user", "warn");
        }
    }
}
void MainWindow::actionRestart() {
    log("Restart: detach + relaunch target", "info");
    if (dbg_.pid() != 0) dbg_.kill();
}
void MainWindow::actionKill() {
    if (dbg_.pid() == 0) return;
    if (dbg_.kill()) {
        log("Target killed", "warn");
        setStatus("Idle");
        bps_cache_.clear();
    }
}
void MainWindow::actionAbout() {
    modal_ = Modal::About;
    log("About: NinjaDBG v1.0.1 — Closed Source, Free, by Chapzoo", "info");
    invalidate();
}

bool MainWindow::demoAttachToPid(pid_t p) {
    if (!dbg_.attach(p)) {
        log("Auto-attach failed: " + dbg_.lastError(), "error");
        invalidate();
        return false;
    }
    log("Auto-attached to pid " + std::to_string(p), "ok");
    dbg_.readRegisters(reg_cache_);
    maps_cache_ = dbg_.readMaps();
    threads_cache_ = dbg_.readThreads();
    bps_cache_ = dbg_.breakpoints();
    disasm_addr_ = reg_cache_.rip;
    mem_dump_addr_ = reg_cache_.rsp;
    setStatus("Attached to pid " + std::to_string(p) + " — paused");
    invalidate();
    return true;
}

} // namespace ndbg::ui
