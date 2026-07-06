// Screenshot helper using Xlib + libpng (NinjaDBG)
// Much faster than Magick++ per-pixel approach.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <png.h>
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <display> <out.png>" << std::endl;
        return 1;
    }

    Display* dpy = XOpenDisplay(argv[1]);
    if (!dpy) {
        std::cerr << "Cannot open display " << argv[1] << std::endl;
        return 2;
    }
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int w = DisplayWidth(dpy, screen);
    int h = DisplayHeight(dpy, screen);

    XImage* img = XGetImage(dpy, root, 0, 0, w, h, AllPlanes, ZPixmap);
    if (!img) {
        std::cerr << "XGetImage failed" << std::endl;
        XCloseDisplay(dpy);
        return 3;
    }

    FILE* fp = fopen(argv[2], "wb");
    if (!fp) {
        std::cerr << "Cannot open " << argv[2] << " for writing" << std::endl;
        XDestroyImage(img);
        XCloseDisplay(dpy);
        return 4;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        XDestroyImage(img);
        XCloseDisplay(dpy);
        return 5;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        XDestroyImage(img);
        XCloseDisplay(dpy);
        return 6;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        XDestroyImage(img);
        XCloseDisplay(dpy);
        return 7;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, w, h, 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // Allocate row buffer (RGB = 3 bytes per pixel)
    png_bytep row = (png_bytep)malloc(w * 3);
    if (!row) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        XDestroyImage(img);
        XCloseDisplay(dpy);
        return 8;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned long p = XGetPixel(img, x, y);
            row[x*3 + 0] = (p >> 16) & 0xFF;  // R
            row[x*3 + 1] = (p >> 8)  & 0xFF;  // G
            row[x*3 + 2] = (p >> 0)  & 0xFF;  // B
        }
        png_write_row(png, row);
    }

    free(row);
    png_write_end(png);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    XDestroyImage(img);
    XCloseDisplay(dpy);
    std::cout << "Saved " << w << "x" << h << " screenshot -> " << argv[2] << std::endl;
    return 0;
}
