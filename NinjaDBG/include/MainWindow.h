// NinjaDBG v1.0.2 - Main Window & App Controller
// Open Source (MIT) - by Chapzoo
#pragma once

#include "Types.h"
#include "DebuggerCore.h"
#include "AntiDetect.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
// #include <X11/extensions/Xrandr.h>  // not needed
#include <cairo.h>
#include <cairo-xlib.h>
#include <pango/pangocairo.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>

namespace ndbg::ui {

class Panel;

struct LayoutRect {
    int x, y, w, h;
};

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool init(int w, int h);
    void run();
    void close();

    // Repaint trigger
    void invalidate();

    // Mouse state
    int mouseX() const { return mx_; }
    int mouseY() const { return my_; }

    // Accessors
    Display* display() { return dpy_; }
    Window   window()  { return win_; }
    cairo_t* cairo()   { return cr_; }

    // Controller actions
    DebuggerCore& debugger() { return dbg_; }
    AntiDetect&   anti()     { return anti_; }

    void setStatus(const std::string& s) { status_ = s; invalidate(); }
    void log(const std::string& msg, const std::string& level = "info");

    // UI events dispatched to panels
    void onMouseMove(int x, int y);
    void onMouseDown(int x, int y, int button);
    void onMouseUp(int x, int y, int button);
    void onMouseWheel(int x, int y, int dir);
    void onKeyDown(KeySym k, unsigned int state);
    void onResize(int w, int h);

    // Action handlers (called by toolbar buttons)
    void actionAttach();
    void actionLaunch();
    void actionDetach();
    void actionContinue();
    void actionStep();
    void actionStepIn();
    void actionStepOut();
    void actionPause();
    void actionRestart();
    void actionKill();
    void actionAbout();

    // Demo helper: auto-attach to a pid and refresh all caches (for screenshots)
    bool demoAttachToPid(pid_t p);

    // Process-list dialog support
    void openProcessPicker();

    int width() const { return win_w_; }
    int height() const { return win_h_; }

    // Console log lines
    struct LogLine { std::string level; std::string text; };
    const std::vector<LogLine>& logLines() const { return log_lines_; }

    // Selected target
    pid_t selectedPid() const { return selected_pid_; }
    void  setSelectedPid(pid_t p) { selected_pid_ = p; }

    // Panels register themselves for custom paint/input
    void registerPanel(Panel* p);

private:
    Display* dpy_ = nullptr;
    Window   win_ = 0;
    int      screen_ = 0;
    Atom     wmDelete_ = 0;
    cairo_surface_t* surf_ = nullptr;
    cairo_t*  cr_ = nullptr;
    PangoFontDescription* font_title_ = nullptr;
    PangoFontDescription* font_body_  = nullptr;
    PangoFontDescription* font_mono_  = nullptr;

    int win_w_ = 0, win_h_ = 0;
    int mx_ = 0, my_ = 0;
    bool mouse_down_ = false;
    bool running_ = false;
    bool need_paint_ = true;

    DebuggerCore dbg_;
    AntiDetect   anti_;

    std::string status_;
    std::vector<LogLine> log_lines_;
    pid_t selected_pid_ = 0;

    std::vector<Panel*> panels_;

    // Modal dialogs
    enum class Modal { NoModal, About, ProcessPicker, AttachInput };
    Modal modal_ = Modal::NoModal;
    std::string attach_input_;
    int  modal_cursor_ = 0;
    std::vector<ProcessInfo> proc_list_;
    int  proc_selected_ = 0;

    bool createWindow();
    void handleEvent(XEvent& ev);
    void paint();
    void paintToolbar(LayoutRect r);
    void paintStatusBar(LayoutRect r);
    void paintLeft(LayoutRect r);
    void paintCenter(LayoutRect r);
    void paintRight(LayoutRect r);
    void paintBottom(LayoutRect r);
    void paintAboutModal();
    void paintProcessPickerModal();
    void paintAttachInputModal();

    // Buttons
    struct Button {
        std::string label;
        std::string icon;          // icon key (e.g. "launch", "attach")
        LayoutRect  rect;
        std::function<void()> action;
        bool enabled = true;
        bool hover = false;
        bool pressed = false;
    };
    std::vector<Button> toolbar_buttons_;

    void buildToolbar();
    Button* hitButton(int x, int y);

    // Logo surface (cairo) - large detailed version
    cairo_surface_t* logo_surf_ = nullptr;
    cairo_surface_t* loadLogo();

    // Toolbar icon surfaces (key -> cairo surface)
    std::map<std::string, cairo_surface_t*> icon_surfs_;
    void loadToolbarIcons();
    void drawIcon(const std::string& key, int x, int y, int size, u32 color);

    // Process list cache
    std::vector<ProcessInfo> proc_cache_;
    void refreshProcessList();

    // Register/state cache for display
    RegisterSet reg_cache_;
    std::vector<MemoryRegion> maps_cache_;
    std::vector<ThreadInfo> threads_cache_;
    std::vector<u8> mem_dump_cache_;
    addr_t mem_dump_addr_ = 0;
    size_t mem_dump_size_ = 256;
    addr_t disasm_addr_ = 0;
    size_t disasm_lines_ = 24;
    std::vector<Breakpoint> bps_cache_;

    // Render helpers
    void drawPanel(LayoutRect r, const std::string& title);
    void drawText(const std::string& s, int x, int y, u32 color, const char* font_family, int size);
    void drawMono(const std::string& s, int x, int y, u32 color, int size);
    int  textWidth(const std::string& s, const char* font_family, int size);
    void fillRect(LayoutRect r, u32 color);
    void strokeRect(LayoutRect r, u32 color, double lw = 1.0);
    void fillRounded(LayoutRect r, double radius, u32 color);
    void strokeRounded(LayoutRect r, double radius, u32 color, double lw);
    void drawSeparator(int x, int y, int h);

    // Disassembly & memory views
    void drawDisassembly(LayoutRect r);
    void drawMemoryHex(LayoutRect r);
    void drawRegisters(LayoutRect r);
    void drawBreakpoints(LayoutRect r);
    void drawStack(LayoutRect r);
    void drawThreads(LayoutRect r);
    void drawConsole(LayoutRect r);
    void drawAntiDetect(LayoutRect r);
    void drawMaps(LayoutRect r);

    // Simple software disassembler (x86-64) - minimal but real
    struct Instr {
        addr_t  addr;
        u8      len;
        char    text[64];
        u8      bytes[15];
    };
    std::vector<Instr> disassemble(addr_t start, size_t n);
};

} // namespace ndbg::ui
