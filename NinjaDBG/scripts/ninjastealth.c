
// NinjaDBG stealth preload payload — masks TracerPid in /proc/self/status
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>

static int (*real_open)(const char*, int, ...) = NULL;
static ssize_t (*real_read)(int, void*, size_t) = NULL;
static int status_fd = -1;

static void init_real(void) {
    if (!real_open)  real_open  = dlsym(RTLD_NEXT, "open");
    if (!real_read)  real_read  = dlsym(RTLD_NEXT, "read");
}

int open(const char* path, int flags, ...) {
    init_real();
    mode_t m = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags);
        m = va_arg(ap, mode_t); va_end(ap);
    }
    int fd = real_open(path, flags, m);
    if (path && strstr(path, "/proc/self/status")) {
        status_fd = fd;  // remember this fd
    }
    return fd;
}

ssize_t read(int fd, void* buf, size_t n) {
    init_real();
    ssize_t r = real_read(fd, buf, n);
    if (fd == status_fd && r > 0) {
        // Mask "TracerPid:\tNNNN" lines
        char* p = (char*)buf;
        char* tracer = strstr(p, "TracerPid:");
        if (tracer) {
            char* nl = strchr(tracer, '\n');
            if (nl) {
                // Replace the line with "TracerPid:\t0\n"
                const char* fake = "TracerPid:\t0\n";
                size_t fake_len = strlen(fake);
                size_t old_len = nl - tracer + 1;
                if (fake_len <= old_len) {
                    memcpy(tracer, fake, fake_len);
                    memmove(tracer + fake_len, nl + 1, p + r - nl - 1);
                    r -= (old_len - fake_len);
                }
            }
        }
    }
    return r;
}
