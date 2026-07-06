// NinjaDBG v1.0.2 - MainWindow implementation (part 1: init, event loop, helpers)
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

// Local color helper (defined here, also re-defined in MainWindowPanels.cpp but file-static)
static void mw_set_color(cairo_t* cr, u32 c) {
    double a = ((c >> 24) & 0xFF) / 255.0;
    double r = ((c >> 16) & 0xFF) / 255.0;
    double g = ((c >>  8) & 0xFF) / 255.0;
    double b = ((c      ) & 0xFF) / 255.0;
    cairo_set_source_rgba(cr, r, g, b, a);
}

// Helper: ellipse path (cairo has no direct ellipse function in older versions)
static void mw_ellipse(cairo_t* cr, double cx, double cy, double rx, double ry) {
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, rx, ry);
    cairo_arc(cr, 0, 0, 1, 0, 2*M_PI);
    cairo_restore(cr);
}

MainWindow::MainWindow() {
    anti_.buildPreloadPayload();
    dbg_.setAntiDetect(&anti_);
}
MainWindow::~MainWindow() {
    if (surf_) cairo_surface_destroy(surf_);
    if (cr_) cairo_destroy(cr_);
    if (logo_surf_) cairo_surface_destroy(logo_surf_);
    for (auto& kv : icon_surfs_) if (kv.second) cairo_surface_destroy(kv.second);
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
    XStoreName(dpy_, win_, "NinjaDBG v1.0.2 - Stealth Debugger  [by Chapzoo]");

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
    loadToolbarIcons();
    buildToolbar();

    // Enable anti-detect defaults
    log("NinjaDBG v1.0.2 initialized", "info");
    log("Stealth subsystem online — 6 techniques active", "ok");
    log("Closed Source - Free - Created by Chapzoo", "info");
    log("Click [Attach] to debug a running process, or [Launch] to start a new target.", "info");

    return true;
}

cairo_surface_t* MainWindow::loadLogo() {
    // Render a highly detailed ninja-mask logo procedurally on a 256x256 surface.
    // This mirrors the SVG in resources/ninja_logo.svg.
    int S = 256;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, S, S);
    cairo_t* c = cairo_create(s);

    auto color_rgba = [&](double r, double g, double b, double a) {
        cairo_set_source_rgba(c, r, g, b, a);
    };

    // === Background: deep space radial gradient ===
    cairo_pattern_t* bg = cairo_pattern_create_radial(S/2.0, S*0.4, 10, S/2.0, S*0.4, S*0.7);
    cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.122, 0.145, 0.251);
    cairo_pattern_add_color_stop_rgb(bg, 0.55, 0.075, 0.086, 0.165);
    cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.027, 0.035, 0.071);
    cairo_pattern_set_extend(bg, CAIRO_EXTEND_PAD);
    cairo_set_source(c, bg);
    cairo_paint(c);
    cairo_pattern_destroy(bg);

    // === Grid pattern (subtle) ===
    color_rgba(0.0, 1.0, 0.882, 0.05);
    cairo_set_line_width(c, 0.5);
    for (int i = 32; i < S; i += 32) {
        cairo_move_to(c, i, 0); cairo_line_to(c, i, S);
        cairo_move_to(c, 0, i); cairo_line_to(c, S, i);
    }
    cairo_stroke(c);

    // === Outer neon frame ===
    color_rgba(0.0, 1.0, 0.882, 0.65);
    cairo_set_line_width(c, 1.5);
    double r = 48;
    cairo_new_sub_path(c);
    cairo_arc(c, S-r, r, r-2, -M_PI/2, 0);
    cairo_arc(c, S-r, S-r, r-2, 0, M_PI/2);
    cairo_arc(c, r, S-r, r-2, M_PI/2, M_PI);
    cairo_arc(c, r, r, r-2, M_PI, 1.5*M_PI);
    cairo_close_path(c);
    cairo_stroke(c);

    // === Corner brackets ===
    color_rgba(0.0, 1.0, 0.882, 0.9);
    cairo_set_line_width(c, 2.5);
    cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND);
    // top-left
    cairo_move_to(c, 22, 44); cairo_line_to(c, 22, 22); cairo_line_to(c, 44, 22);
    // top-right
    cairo_move_to(c, 212, 22); cairo_line_to(c, 234, 22); cairo_line_to(c, 234, 44);
    // bottom-left
    cairo_move_to(c, 22, 212); cairo_line_to(c, 22, 234); cairo_line_to(c, 44, 234);
    // bottom-right
    cairo_move_to(c, 212, 234); cairo_line_to(c, 234, 234); cairo_line_to(c, 234, 212);
    cairo_stroke(c);

    // === Crossed katanas (behind head) ===
    cairo_save(c);
    color_rgba(0.55, 0.58, 0.65, 0.55);
    cairo_set_line_width(c, 3.0);
    cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND);
    // Blade 1: BL to TR
    cairo_move_to(c, 44, 216); cairo_line_to(c, 216, 44);
    // Blade 2: TL to BR
    cairo_move_to(c, 40, 44); cairo_line_to(c, 212, 216);
    cairo_stroke(c);
    // Edge highlight
    color_rgba(1.0, 1.0, 1.0, 0.18);
    cairo_set_line_width(c, 0.5);
    cairo_move_to(c, 46, 214); cairo_line_to(c, 214, 46);
    cairo_move_to(c, 42, 46); cairo_line_to(c, 210, 214);
    cairo_stroke(c);
    // Center tsuba emblem
    color_rgba(0.10, 0.11, 0.18, 0.85);
    cairo_arc(c, 128, 130, 8, 0, 2*M_PI);
    cairo_fill(c);
    color_rgba(0.0, 1.0, 0.882, 0.85);
    cairo_set_line_width(c, 1.5);
    cairo_arc(c, 128, 130, 8, 0, 2*M_PI);
    cairo_stroke(c);
    color_rgba(0.0, 1.0, 0.882, 0.7);
    cairo_arc(c, 128, 130, 2.5, 0, 2*M_PI);
    cairo_fill(c);
    cairo_restore(c);

    // === Head shadow (drop) ===
    cairo_save(c);
    cairo_set_source_rgba(c, 0, 0, 0, 0.4);
    cairo_rectangle(c, 0, 0, S, S);
    cairo_mask(c, cairo_pattern_create_radial(128, 230, 4, 128, 230, 50));
    cairo_fill(c);
    cairo_restore(c);

    // === HEAD (mask cloth) ===
    // Head base
    cairo_pattern_t* cloth = cairo_pattern_create_linear(0, 36, 0, 200);
    cairo_pattern_add_color_stop_rgb(cloth, 0.0, 0.165, 0.184, 0.282);
    cairo_pattern_add_color_stop_rgb(cloth, 0.55, 0.102, 0.114, 0.180);
    cairo_pattern_add_color_stop_rgb(cloth, 1.0, 0.047, 0.055, 0.094);
    cairo_set_source(c, cloth);
    cairo_arc(c, 128, 118, 74, 0, 2*M_PI);
    cairo_fill(c);
    cairo_pattern_destroy(cloth);

    // Crown shadow (ellipse via save+scale+arc)
    cairo_save(c);
    cairo_set_source_rgba(c, 0.039, 0.051, 0.094, 0.55);
    cairo_translate(c, 128, 78);
    cairo_scale(c, 68, 14);
    cairo_arc(c, 0, 0, 1, 0, 2*M_PI);
    cairo_fill(c);
    cairo_restore(c);

    // === HEADBAND (silk wrap) ===
    cairo_pattern_t* hb = cairo_pattern_create_linear(0, 84, 0, 122);
    cairo_pattern_add_color_stop_rgb(hb, 0.0, 0.902, 0.227, 0.302);
    cairo_pattern_add_color_stop_rgb(hb, 0.4, 0.722, 0.125, 0.184);
    cairo_pattern_add_color_stop_rgb(hb, 1.0, 0.369, 0.051, 0.082);
    cairo_set_source(c, hb);
    cairo_move_to(c, 52, 96);
    cairo_curve_to(c, 70, 88, 100, 84, 128, 84);
    cairo_curve_to(c, 156, 84, 186, 88, 204, 96);
    cairo_line_to(c, 204, 122);
    cairo_curve_to(c, 186, 116, 156, 112, 128, 112);
    cairo_curve_to(c, 100, 112, 70, 116, 52, 122);
    cairo_close_path(c);
    cairo_fill(c);
    cairo_pattern_destroy(hb);

    // Headband fold lines
    cairo_set_source_rgba(c, 0.353, 0.055, 0.086, 0.4);
    cairo_set_line_width(c, 0.4);
    for (int x : {78, 108, 148, 178}) {
        cairo_move_to(c, x, 90); cairo_line_to(c, x, 118);
    }
    cairo_stroke(c);
    // Top edge highlight
    cairo_set_source_rgba(c, 1.0, 0.392, 0.471, 0.55);
    cairo_set_line_width(c, 0.6);
    cairo_move_to(c, 52, 96);
    cairo_curve_to(c, 70, 88, 100, 84, 128, 84);
    cairo_curve_to(c, 156, 84, 186, 88, 204, 96);
    cairo_stroke(c);

    // === Headband knot (right) ===
    cairo_pattern_t* knot = cairo_pattern_create_linear(204, 90, 240, 130);
    cairo_pattern_add_color_stop_rgb(knot, 0.0, 0.902, 0.227, 0.302);
    cairo_pattern_add_color_stop_rgb(knot, 1.0, 0.478, 0.063, 0.098);
    cairo_set_source(c, knot);
    cairo_move_to(c, 204, 100);
    cairo_line_to(c, 226, 90);
    cairo_line_to(c, 220, 110);
    cairo_line_to(c, 234, 114);
    cairo_line_to(c, 214, 118);
    cairo_line_to(c, 218, 106);
    cairo_close_path(c);
    cairo_fill(c);
    // Knot tail (flowing down)
    cairo_move_to(c, 220, 110);
    cairo_curve_to(c, 234, 120, 226, 138, 220, 138);
    cairo_curve_to(c, 216, 138, 222, 124, 218, 116);
    cairo_close_path(c);
    cairo_fill(c);
    // Knot tail (flowing up)
    cairo_move_to(c, 226, 90);
    cairo_curve_to(c, 240, 80, 246, 70, 246, 70);
    cairo_curve_to(c, 250, 76, 244, 88, 230, 96);
    cairo_close_path(c);
    cairo_fill(c);
    cairo_pattern_destroy(knot);

    // === Headband knot (left) ===
    cairo_pattern_t* knot2 = cairo_pattern_create_linear(52, 90, 16, 130);
    cairo_pattern_add_color_stop_rgb(knot2, 0.0, 0.902, 0.227, 0.302);
    cairo_pattern_add_color_stop_rgb(knot2, 1.0, 0.478, 0.063, 0.098);
    cairo_set_source(c, knot2);
    cairo_move_to(c, 52, 100);
    cairo_line_to(c, 30, 90);
    cairo_line_to(c, 36, 110);
    cairo_line_to(c, 22, 114);
    cairo_line_to(c, 42, 118);
    cairo_line_to(c, 38, 106);
    cairo_close_path(c);
    cairo_fill(c);
    cairo_move_to(c, 36, 110);
    cairo_curve_to(c, 22, 120, 30, 138, 36, 138);
    cairo_curve_to(c, 40, 138, 34, 124, 38, 116);
    cairo_close_path(c);
    cairo_fill(c);
    cairo_pattern_destroy(knot2);

    // === Golden shuriken emblem on headband ===
    cairo_save(c);
    cairo_translate(c, 128, 102);
    // Glow
    cairo_set_source_rgba(c, 1.0, 0.90, 0.50, 0.30);
    cairo_arc(c, 0, 0, 13, 0, 2*M_PI);
    cairo_fill(c);
    // Outer ring
    cairo_pattern_t* gold = cairo_pattern_create_radial(0, -2, 1, 0, 0, 10);
    cairo_pattern_add_color_stop_rgb(gold, 0.0, 1.0, 0.90, 0.50);
    cairo_pattern_add_color_stop_rgb(gold, 0.6, 0.831, 0.627, 0.090);
    cairo_pattern_add_color_stop_rgb(gold, 1.0, 0.478, 0.353, 0.0);
    cairo_set_source(c, gold);
    cairo_set_line_width(c, 1.2);
    cairo_arc(c, 0, 0, 9, 0, 2*M_PI);
    cairo_stroke(c);
    // 8-pointed shuriken
    cairo_move_to(c, 0, -9);
    cairo_line_to(c, 1.8, -3);
    cairo_line_to(c, 6, -6);
    cairo_line_to(c, 3, -1.8);
    cairo_line_to(c, 9, 0);
    cairo_line_to(c, 3, 1.8);
    cairo_line_to(c, 6, 6);
    cairo_line_to(c, 1.8, 3);
    cairo_line_to(c, 0, 9);
    cairo_line_to(c, -1.8, 3);
    cairo_line_to(c, -6, 6);
    cairo_line_to(c, -3, 1.8);
    cairo_line_to(c, -9, 0);
    cairo_line_to(c, -3, -1.8);
    cairo_line_to(c, -6, -6);
    cairo_line_to(c, -1.8, -3);
    cairo_close_path(c);
    cairo_fill(c);
    // Center stud
    cairo_set_source_rgba(c, 1.0, 0.96, 0.72, 1.0);
    cairo_arc(c, 0, 0, 1.8, 0, 2*M_PI);
    cairo_fill(c);
    cairo_pattern_destroy(gold);
    cairo_restore(c);

    // === EYES ===
    // Socket
    cairo_set_source_rgba(c, 0, 0, 0, 1.0);
    cairo_move_to(c, 64, 140);
    cairo_curve_to(c, 96, 124, 160, 124, 192, 140);
    cairo_line_to(c, 192, 168);
    cairo_curve_to(c, 160, 152, 96, 152, 64, 168);
    cairo_close_path(c);
    cairo_fill(c);

    // Inner socket glow (very subtle)
    cairo_set_source_rgba(c, 0.0, 1.0, 0.882, 0.06);
    mw_ellipse(c, 128, 156, 60, 10);
    cairo_fill(c);

    // Left eye
    cairo_save(c);
    cairo_pattern_t* eg = cairo_pattern_create_radial(100, 152, 1, 100, 152, 16);
    cairo_pattern_add_color_stop_rgba(eg, 0.0, 0.667, 1.0, 0.933, 1.0);
    cairo_pattern_add_color_stop_rgba(eg, 0.35, 0.0, 1.0, 0.882, 1.0);
    cairo_pattern_add_color_stop_rgba(eg, 0.70, 0.0, 0.659, 0.565, 1.0);
    cairo_pattern_add_color_stop_rgba(eg, 1.0, 0.0, 0.227, 0.208, 0.0);
    cairo_set_source(c, eg);
    mw_ellipse(c, 100, 152, 16, 6.5);
    cairo_fill(c);
    cairo_set_source_rgba(c, 0.667, 1.0, 0.933, 0.9);
    mw_ellipse(c, 100, 152, 9, 4);
    cairo_fill(c);
    cairo_set_source_rgba(c, 1.0, 1.0, 1.0, 1.0);
    mw_ellipse(c, 100, 152, 3, 2.8);
    cairo_fill(c);
    // Catchlight
    cairo_arc(c, 102, 150.5, 0.8, 0, 2*M_PI);
    cairo_fill(c);
    cairo_pattern_destroy(eg);
    cairo_restore(c);

    // Right eye
    cairo_save(c);
    cairo_pattern_t* eg2 = cairo_pattern_create_radial(156, 152, 1, 156, 152, 16);
    cairo_pattern_add_color_stop_rgba(eg2, 0.0, 0.667, 1.0, 0.933, 1.0);
    cairo_pattern_add_color_stop_rgba(eg2, 0.35, 0.0, 1.0, 0.882, 1.0);
    cairo_pattern_add_color_stop_rgba(eg2, 0.70, 0.0, 0.659, 0.565, 1.0);
    cairo_pattern_add_color_stop_rgba(eg2, 1.0, 0.0, 0.227, 0.208, 0.0);
    cairo_set_source(c, eg2);
    mw_ellipse(c, 156, 152, 16, 6.5);
    cairo_fill(c);
    cairo_set_source_rgba(c, 0.667, 1.0, 0.933, 0.9);
    mw_ellipse(c, 156, 152, 9, 4);
    cairo_fill(c);
    cairo_set_source_rgba(c, 1.0, 1.0, 1.0, 1.0);
    mw_ellipse(c, 156, 152, 3, 2.8);
    cairo_fill(c);
    cairo_arc(c, 158, 150.5, 0.8, 0, 2*M_PI);
    cairo_fill(c);
    cairo_pattern_destroy(eg2);
    cairo_restore(c);

    // Eye slit lower edge accent
    cairo_set_source_rgba(c, 0.0, 1.0, 0.882, 0.4);
    cairo_set_line_width(c, 0.6);
    cairo_move_to(c, 64, 168);
    cairo_curve_to(c, 96, 152, 160, 152, 192, 168);
    cairo_stroke(c);

    // === LOWER FACE MASK (nose down) ===
    cairo_pattern_t* cloth2 = cairo_pattern_create_linear(0, 172, 0, 224);
    cairo_pattern_add_color_stop_rgb(cloth2, 0.0, 0.165, 0.184, 0.282);
    cairo_pattern_add_color_stop_rgb(cloth2, 0.55, 0.102, 0.114, 0.180);
    cairo_pattern_add_color_stop_rgb(cloth2, 1.0, 0.047, 0.055, 0.094);
    cairo_set_source(c, cloth2);
    cairo_move_to(c, 68, 172);
    cairo_curve_to(c, 96, 188, 160, 188, 188, 172);
    cairo_line_to(c, 188, 208);
    cairo_curve_to(c, 160, 224, 96, 224, 68, 208);
    cairo_close_path(c);
    cairo_fill(c);
    cairo_pattern_destroy(cloth2);

    // Cloth fold lines
    cairo_set_source_rgba(c, 0.039, 0.051, 0.094, 0.7);
    cairo_set_line_width(c, 0.9);
    cairo_move_to(c, 76, 184); cairo_curve_to(c, 100, 196, 156, 196, 180, 184);
    cairo_move_to(c, 80, 196); cairo_curve_to(c, 100, 208, 156, 208, 176, 196);
    cairo_move_to(c, 84, 208); cairo_curve_to(c, 100, 218, 156, 218, 172, 208);
    cairo_stroke(c);

    // Vertical seam
    cairo_set_source_rgba(c, 0.039, 0.051, 0.094, 0.45);
    cairo_set_line_width(c, 0.5);
    cairo_move_to(c, 128, 172); cairo_line_to(c, 128, 216);
    cairo_stroke(c);

    // Breathing slits
    cairo_set_source_rgba(c, 0, 0, 0, 0.55);
    cairo_rectangle(c, 120, 208, 16, 1.2);
    cairo_fill(c);
    cairo_rectangle(c, 118, 212, 20, 1.2);
    cairo_fill(c);
    cairo_rectangle(c, 120, 216, 16, 1.2);
    cairo_fill(c);

    // Neck shadow
    cairo_set_source_rgba(c, 0, 0, 0, 0.4);
    mw_ellipse(c, 128, 226, 50, 6);
    cairo_fill(c);

    // === Foreground energy aura ===
    cairo_set_source_rgba(c, 0.0, 1.0, 0.882, 0.25);
    cairo_set_line_width(c, 0.8);
    double dashes[] = {2, 4};
    cairo_set_dash(c, dashes, 2, 0);
    cairo_move_to(c, 36, 160);
    cairo_curve_to(c, 80, 60, 176, 60, 220, 160);
    cairo_stroke(c);
    cairo_set_dash(c, nullptr, 0, 0);

    // === Bottom text ===
    cairo_set_source_rgba(c, 0.0, 1.0, 0.882, 0.8);
    cairo_select_font_face(c, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(c, 9);
    // Show centered text
    cairo_text_extents_t te;
    cairo_text_extents(c, "NINJADBG", &te);
    cairo_move_to(c, 128 - te.width/2 - te.x_bearing, 246);
    cairo_show_text(c, "NINJADBG");

    // === Scanline overlay (CRT) ===
    cairo_set_source_rgba(c, 0.0, 1.0, 0.882, 0.04);
    cairo_set_line_width(c, 1.0);
    for (int y = 0; y < S; y += 4) {
        cairo_move_to(c, 0, y); cairo_line_to(c, S, y);
    }
    cairo_stroke(c);

    cairo_destroy(c);
    return s;
}

// Helper: build a 24x24 cairo surface from a simple vector icon spec.
// Each icon is drawn with strokes in currentColor style — we use a single color
// passed at draw time via the surface's premultiplied ARGB. To allow recoloring,
// we draw the icon in white and apply color via cairo_set_source at draw time.
static cairo_surface_t* make_icon_surface(const std::string& key) {
    int S = 24;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, S, S);
    cairo_t* c = cairo_create(s);

    // Draw in white (will be tinted at paint time by drawing the surface
    // through a mask with the desired color as source).
    cairo_set_source_rgba(c, 1, 1, 1, 1);
    cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_width(c, 2.0);

    if (key == "launch") {
        // Triangle play
        cairo_move_to(c, 6, 4); cairo_line_to(c, 20, 12); cairo_line_to(c, 6, 20);
        cairo_close_path(c);
        cairo_stroke(c);
    } else if (key == "attach") {
        // Shield + check
        cairo_move_to(c, 12, 3); cairo_line_to(c, 4, 6); cairo_line_to(c, 4, 12);
        cairo_curve_to(c, 4, 18, 8, 20, 12, 21);
        cairo_curve_to(c, 16, 20, 20, 18, 20, 12); cairo_line_to(c, 20, 6);
        cairo_close_path(c);
        cairo_stroke(c);
        cairo_move_to(c, 8, 12); cairo_line_to(c, 11, 15); cairo_line_to(c, 16, 9);
        cairo_stroke(c);
    } else if (key == "detach") {
        // Shield + X
        cairo_move_to(c, 12, 3); cairo_line_to(c, 4, 6); cairo_line_to(c, 4, 12);
        cairo_curve_to(c, 4, 18, 8, 20, 12, 21);
        cairo_curve_to(c, 16, 20, 20, 18, 20, 12); cairo_line_to(c, 20, 6);
        cairo_close_path(c);
        cairo_stroke(c);
        cairo_move_to(c, 9, 9); cairo_line_to(c, 15, 15);
        cairo_move_to(c, 15, 9); cairo_line_to(c, 9, 15);
        cairo_stroke(c);
    } else if (key == "run") {
        // Fast-forward: triangle + bar
        cairo_move_to(c, 4, 4); cairo_line_to(c, 14, 12); cairo_line_to(c, 4, 20);
        cairo_close_path(c);
        cairo_stroke(c);
        cairo_move_to(c, 18, 5); cairo_line_to(c, 18, 19);
        cairo_stroke(c);
    } else if (key == "pause") {
        // Two bars
        cairo_rectangle(c, 6, 5, 4, 14);
        cairo_stroke(c);
        cairo_rectangle(c, 14, 5, 4, 14);
        cairo_stroke(c);
    } else if (key == "step") {
        // Arrow over dot
        cairo_move_to(c, 4, 12); cairo_curve_to(c, 8, 4, 16, 4, 20, 12);
        cairo_stroke(c);
        cairo_move_to(c, 16, 8); cairo_line_to(c, 20, 12); cairo_line_to(c, 16, 16);
        cairo_stroke(c);
        cairo_arc(c, 12, 18, 2, 0, 2*M_PI);
        cairo_fill(c);
    } else if (key == "stepin") {
        // Arrow down into dot
        cairo_move_to(c, 12, 3); cairo_line_to(c, 12, 14);
        cairo_stroke(c);
        cairo_move_to(c, 8, 10); cairo_line_to(c, 12, 14); cairo_line_to(c, 16, 10);
        cairo_stroke(c);
        cairo_arc(c, 12, 20, 2, 0, 2*M_PI);
        cairo_fill(c);
    } else if (key == "stepout") {
        // Arrow up out of dot
        cairo_move_to(c, 12, 21); cairo_line_to(c, 12, 10);
        cairo_stroke(c);
        cairo_move_to(c, 8, 14); cairo_line_to(c, 12, 10); cairo_line_to(c, 16, 14);
        cairo_stroke(c);
        cairo_arc(c, 12, 4, 2, 0, 2*M_PI);
        cairo_fill(c);
    } else if (key == "restart") {
        // Circular arrow
        cairo_move_to(c, 3, 12);
        cairo_arc(c, 12, 12, 9, M_PI, 0.6 * M_PI);
        cairo_stroke(c);
        cairo_move_to(c, 3, 12); cairo_line_to(c, 3, 6); cairo_line_to(c, 8, 9);
        cairo_stroke(c);
    } else if (key == "kill") {
        // Skull simplified
        cairo_arc(c, 12, 10, 7, M_PI, 0);
        cairo_line_to(c, 19, 16); cairo_line_to(c, 16, 16); cairo_line_to(c, 16, 19);
        cairo_line_to(c, 8, 19); cairo_line_to(c, 8, 16); cairo_line_to(c, 5, 16);
        cairo_close_path(c);
        cairo_stroke(c);
        cairo_arc(c, 9, 10, 1.5, 0, 2*M_PI); cairo_fill(c);
        cairo_arc(c, 15, 10, 1.5, 0, 2*M_PI); cairo_fill(c);
        cairo_move_to(c, 10, 17); cairo_line_to(c, 14, 17);
        cairo_stroke(c);
    } else if (key == "about") {
        // Info circle
        cairo_arc(c, 12, 12, 9, 0, 2*M_PI);
        cairo_stroke(c);
        cairo_move_to(c, 12, 16); cairo_line_to(c, 12, 11);
        cairo_stroke(c);
        cairo_arc(c, 12, 8, 0.8, 0, 2*M_PI);
        cairo_fill(c);
    }

    cairo_destroy(c);
    return s;
}

void MainWindow::loadToolbarIcons() {
    const char* keys[] = {"launch","attach","detach","run","pause",
                          "step","stepin","stepout","restart","kill","about"};
    for (auto k : keys) {
        icon_surfs_[k] = make_icon_surface(k);
    }
}

void MainWindow::drawIcon(const std::string& key, int x, int y, int size, u32 color) {
    auto it = icon_surfs_.find(key);
    if (it == icon_surfs_.end() || !it->second) return;
    cairo_save(cr_);
    // Scale to desired size
    double sc = (double)size / 24.0;
    cairo_translate(cr_, x, y);
    cairo_scale(cr_, sc, sc);
    // Use the icon as a mask, fill with the desired color
    mw_set_color(cr_, color);
    cairo_mask_surface(cr_, it->second, 0, 0);
    cairo_fill(cr_);
    cairo_restore(cr_);
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
            // Compute modal rect matching paintProcessPickerModal
            int MW = std::min(1100, win_w_ - 80);
            int MH = std::min(720, win_h_ - lay::TOOLBAR_H - 80);
            LayoutRect r = { (win_w_ - MW) / 2, (win_h_ - MH) / 2, MW, MH };
            LayoutRect inner = { r.x + 24, r.y + 104, r.w - 48, r.h - 104 - 60 };
            // Click inside the list selects a row
            if (x >= inner.x && x < inner.x + inner.w && y >= inner.y + 22 && y < inner.y + inner.h) {
                int row_h = 20;
                int idx = (y - (inner.y + 22)) / row_h;
                if (idx >= 0 && idx < (int)proc_list_.size()) {
                    proc_selected_ = idx;
                    selected_pid_ = proc_list_[idx].pid;
                }
            }
            // Attach button (bottom-right)
            LayoutRect ab = { r.x + r.w - 220, r.y + r.h - 40, 90, 30 };
            if (x >= ab.x && x < ab.x + ab.w && y >= ab.y && y < ab.y + ab.h) {
                if (selected_pid_ != 0) {
                    demoAttachToPid(selected_pid_);
                }
                modal_ = Modal::NoModal;
                return;
            }
            // Cancel button
            LayoutRect cb = { r.x + r.w - 120, r.y + r.h - 40, 90, 30 };
            if (x >= cb.x && x < cb.x + cb.w && y >= cb.y && y < cb.y + cb.h) {
                modal_ = Modal::NoModal;
            }
            return;
        }
        if (modal_ == Modal::AttachInput) {
            int MW = 460;
            int MH = 200;
            LayoutRect r = { (win_w_ - MW) / 2, (win_h_ - MH) / 2, MW, MH };
            LayoutRect ok = { r.x + r.w - 220, r.y + r.h - 44, 90, 30 };
            LayoutRect ca = { r.x + r.w - 120, r.y + r.h - 44, 90, 30 };
            if (x >= ok.x && x < ok.x + ok.w && y >= ok.y && y < ok.y + ok.h) {
                pid_t p = (pid_t)strtol(attach_input_.c_str(), nullptr, 10);
                if (p > 0) {
                    demoAttachToPid(p);
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
    add("Launch",  "launch",  [this]{ actionLaunch(); });
    add("Attach",  "attach",  [this]{ actionAttach(); });
    add("Detach",  "detach",  [this]{ actionDetach(); });
    add("|",       "",        nullptr);
    add("Run",     "run",     [this]{ actionContinue(); });
    add("Pause",   "pause",   [this]{ actionPause(); });
    add("Step",    "step",    [this]{ actionStep(); });
    add("Step In", "stepin",  [this]{ actionStepIn(); });
    add("Step Out","stepout", [this]{ actionStepOut(); });
    add("|",       "",        nullptr);
    add("Restart", "restart", [this]{ actionRestart(); });
    add("Kill",    "kill",    [this]{ actionKill(); });
    add("|",       "",        nullptr);
    add("About",   "about",   [this]{ actionAbout(); });
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
    log("About: NinjaDBG v1.0.2 — Closed Source, Free, by Chapzoo", "info");
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
