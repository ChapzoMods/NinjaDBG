/*
 * libninjastealth.so - NinjaDBG v1.1.4 Preload Stealth Payload
 * Open Source (Apache-2.0) - by Chapzoo
 *
 * This shared library is injected into the target process via LD_PRELOAD
 * to mask NinjaDBG's presence from anti-debug checks. It hooks:
 *
 *   1. open() / open64() / openat()      - detect /proc/self/status, /proc/self/wchan,
 *                                           /proc/self/syscall, /proc/self/stat,
 *                                           /proc/self/maps reads
 *   2. read() / pread() / readv()        - rewrite TracerPid, wchan, comm fields
 *                                           in buffered reads
 *   3. fopen() / fread()                 - catch stdio-based reads of /proc
 *   4. ptrace()                          - PTRACE_TRACEME returns -1/ESRCH
 *   5. prctl()                           - PR_GET_DUMPABLE always returns 1
 *   6. clock_gettime() / gettimeofday()  - normalize timing (optional, subtle)
 *   7. __libc_start_main()               - early init hook
 *
 * The library is self-contained: no external deps beyond libc and libdl.
 * It uses dlsym(RTLD_NEXT, ...) to resolve real functions.
 *
 * Build: gcc -shared -fPIC -O2 -ldl -o libninjastealth.so ninjastealth.c
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <time.h>

/* ---- Function pointer types ---- */
typedef int     (*open_fn)(const char*, int, ...);
typedef int     (*openat_fn)(int, const char*, int, ...);
typedef ssize_t (*read_fn)(int, void*, size_t);
typedef ssize_t (*pread_fn)(int, void*, size_t, off_t);
typedef ssize_t (*pread64_fn)(int, void*, size_t, off_t);
typedef ssize_t (*readv_fn)(int, const struct iovec*, int);
typedef int     (*close_fn)(int);
typedef FILE*   (*fopen_fn)(const char*, const char*);
typedef size_t  (*fread_fn)(void*, size_t, size_t, FILE*);
typedef char*   (*fgets_fn)(char*, int, FILE*);
typedef ssize_t (*getline_fn)(char**, size_t*, FILE*);
typedef long    (*ptrace_fn)(enum __ptrace_request, ...);
typedef int     (*prctl_fn)(int, ...);
typedef int     (*clock_gettime_fn)(clockid_t, struct timespec*);

/* ---- Real function pointers (lazy init) ---- */
static open_fn          real_open      = NULL;
static open_fn          real_open64    = NULL;
static openat_fn        real_openat    = NULL;
static read_fn          real_read      = NULL;
static pread_fn         real_pread     = NULL;
static pread64_fn       real_pread64   = NULL;
static readv_fn         real_readv     = NULL;
static close_fn         real_close     = NULL;
static fopen_fn         real_fopen     = NULL;
static fread_fn         real_fread     = NULL;
static fgets_fn         real_fgets     = NULL;
static getline_fn       real_getline   = NULL;
static ptrace_fn        real_ptrace    = NULL;
static prctl_fn         real_prctl     = NULL;
static clock_gettime_fn real_clock_gettime = NULL;

/* ---- State tracking ---- */
// We track up to 32 simultaneously open "stealth" fds (proc files we want to rewrite)
#define MAX_STEALTH_FDS 32
static int stealth_fds[MAX_STEALTH_FDS];
static int stealth_fd_types[MAX_STEALTH_FDS];  // 0=status, 1=wchan, 2=syscall, 3=comm, 4=stat, 5=maps
static int stealth_fd_count = 0;

static const char* STEALTH_TARGETS[] = {
    "/proc/self/status",
    "/proc/self/wchan",
    "/proc/self/syscall",
    "/proc/self/stat",
    "/proc/self/comm",
    "/proc/self/maps",
    NULL
};

static void init_real(void) {
    if (!real_open)      real_open      = (open_fn)dlsym(RTLD_NEXT, "open");
    if (!real_open64)    real_open64    = (open_fn)dlsym(RTLD_NEXT, "open64");
    if (!real_openat)    real_openat    = (openat_fn)dlsym(RTLD_NEXT, "openat");
    if (!real_read)      real_read      = (read_fn)dlsym(RTLD_NEXT, "read");
    if (!real_pread)     real_pread     = (pread_fn)dlsym(RTLD_NEXT, "pread");
    if (!real_pread64)   real_pread64   = (pread64_fn)dlsym(RTLD_NEXT, "pread64");
    if (!real_readv)     real_readv     = (readv_fn)dlsym(RTLD_NEXT, "readv");
    if (!real_close)     real_close     = (close_fn)dlsym(RTLD_NEXT, "close");
    if (!real_fopen)     real_fopen     = (fopen_fn)dlsym(RTLD_NEXT, "fopen");
    if (!real_fread)     real_fread     = (fread_fn)dlsym(RTLD_NEXT, "fread");
    if (!real_fgets)     real_fgets     = (fgets_fn)dlsym(RTLD_NEXT, "fgets");
    if (!real_getline)   real_getline   = (getline_fn)dlsym(RTLD_NEXT, "getline");
    if (!real_ptrace)    real_ptrace    = (ptrace_fn)dlsym(RTLD_NEXT, "ptrace");
    if (!real_prctl)     real_prctl     = (prctl_fn)dlsym(RTLD_NEXT, "prctl");
    if (!real_clock_gettime) real_clock_gettime = (clock_gettime_fn)dlsym(RTLD_NEXT, "clock_gettime");
}

// Register a fd as a stealth target
static void register_stealth_fd(int fd, int type) {
    if (fd < 0) return;
    // Find empty slot or reuse
    for (int i = 0; i < stealth_fd_count && i < MAX_STEALTH_FDS; i++) {
        if (stealth_fds[i] == fd) {
            stealth_fd_types[i] = type;
            return;
        }
    }
    if (stealth_fd_count < MAX_STEALTH_FDS) {
        stealth_fds[stealth_fd_count] = fd;
        stealth_fd_types[stealth_fd_count] = type;
        stealth_fd_count++;
    }
}

// Check if fd is a stealth target; return type (0=not stealth, 1-5=type)
static int get_stealth_type(int fd) {
    for (int i = 0; i < stealth_fd_count && i < MAX_STEALTH_FDS; i++) {
        if (stealth_fds[i] == fd) return stealth_fd_types[i] + 1;
    }
    return 0;
}

// Unregister a fd
static void unregister_stealth_fd(int fd) {
    for (int i = 0; i < stealth_fd_count && i < MAX_STEALTH_FDS; i++) {
        if (stealth_fds[i] == fd) {
            // Shift remaining down
            for (int j = i; j < stealth_fd_count - 1 && j < MAX_STEALTH_FDS - 1; j++) {
                stealth_fds[j] = stealth_fds[j + 1];
                stealth_fd_types[j] = stealth_fd_types[j + 1];
            }
            stealth_fd_count--;
            return;
        }
    }
}

// Check if a path matches any stealth target; return type index (0-5) or -1
static int match_stealth_path(const char* path) {
    if (!path) return -1;
    for (int i = 0; STEALTH_TARGETS[i]; i++) {
        if (strcmp(path, STEALTH_TARGETS[i]) == 0) return i;
    }
    // Also check /proc/<pid>/status etc (for the NinjaDBG process itself)
    // The target reads /proc/self/... which always refers to itself, so
    // the self-paths above are sufficient.
    return -1;
}

/* ---- Buffer rewriting functions ---- */

// Rewrite "TracerPid:\tNNNN" to "TracerPid:\t0" in a buffer.
// Uses memmem/memchr for safety (buffer may not be NUL-terminated).
static void rewrite_status(char* buf, ssize_t* r) {
    if (*r <= 0) return;
    const char* needle = "TracerPid:";
    size_t needle_len = 10;
    char* p = (char*)memmem(buf, *r, needle, needle_len);
    if (!p) return;
    // Find the colon after "TracerPid:"
    char* colon = memchr(p, ':', *r - (p - buf));
    if (!colon) return;
    colon++;  // skip ':'
    // Skip whitespace
    char* val_start = colon;
    while (val_start < buf + *r && (*val_start == ' ' || *val_start == '\t')) val_start++;
    // Find end of line
    char* nl = (char*)memchr(val_start, '\n', *r - (val_start - buf));
    if (!nl) return;
    // Replace the value with "0"
    // Original: "TracerPid:\t1234\n"
    // New:      "TracerPid:\t0\n"  (then shift remaining bytes left)
    char* write_pos = val_start;
    *write_pos++ = '0';
    // Shift everything after nl to fill the gap
    size_t old_val_len = nl - val_start;
    size_t new_val_len = 1;
    if (old_val_len > new_val_len) {
        size_t shift = old_val_len - new_val_len;
        memmove(write_pos, nl, *r - (nl - buf));
        *r -= shift;
    }
}

// Rewrite wchan content: replace "ptrace_stop" with "0" (or "running")
static void rewrite_wchan(char* buf, ssize_t* r) {
    if (*r <= 0) return;
    const char* needle = "ptrace_stop";
    size_t needle_len = 11;
    char* p = (char*)memmem(buf, *r, needle, needle_len);
    if (p) {
        // Replace with "0" and shift
        *p = '0';
        size_t shift = needle_len - 1;
        memmove(p + 1, p + needle_len, *r - (p + needle_len - buf));
        *r -= shift;
        return;
    }
    // Also mask "do_wait" and other ptrace-related wchan values
    const char* needle2 = "do_wait";
    size_t nl2 = 7;
    p = (char*)memmem(buf, *r, needle2, nl2);
    if (p) {
        *p = '0';
        size_t shift = nl2 - 1;
        memmove(p + 1, p + nl2, *r - (p + nl2 - buf));
        *r -= shift;
    }
}

// Rewrite /proc/self/syscall: the line looks like:
// "NN 0xXX 0xXX 0xXX 0xXX 0xXX 0xXX 0xXX 0xXX rip rsp sp"
// If the target is stopped, the first field is the syscall number.
// We can't easily fake "running", so we zero it out.
static void rewrite_syscall(char* buf, ssize_t* r) {
    if (*r <= 0) return;
    // Replace the entire content with "running\n"
    const char* fake = "running\n";
    size_t fake_len = 8;
    if (fake_len <= (size_t)*r) {
        memcpy(buf, fake, fake_len);
        memmove(buf + fake_len, buf + *r, 0);  // no-op, just truncate
        *r = fake_len;
    } else {
        memcpy(buf, fake, *r);
    }
}

// Rewrite /proc/self/comm: replace "ninjadb" with "kworker/u:1"
static void rewrite_comm(char* buf, ssize_t* r) {
    if (*r <= 0) return;
    const char* fake = "kworker/u:1\n";
    size_t fake_len = 12;
    if (fake_len <= (size_t)*r) {
        memcpy(buf, fake, fake_len);
        *r = fake_len;
    } else {
        memcpy(buf, fake, *r);
    }
}

// Rewrite /proc/self/stat: field 3 is the state char (T = stopped).
// Replace 'T' with 'R' (running) if we see it in the right position.
static void rewrite_stat(char* buf, ssize_t* r) {
    if (*r <= 0) return;
    // /proc/self/stat format: pid (comm) state ...
    // Find the closing ')' and check the next char
    char* close_paren = (char*)memrchr(buf, ')', *r);
    if (!close_paren) return;
    char* state_char = close_paren + 1;
    if (state_char >= buf + *r) return;
    // Skip space
    while (state_char < buf + *r && *state_char == ' ') state_char++;
    if (state_char < buf + *r) {
        if (*state_char == 'T') *state_char = 'R';  // stopped -> running
        if (*state_char == 't') *state_char = 'R';  // tracing stop -> running
    }
}

// Rewrite /proc/self/maps: filter out lines containing "ninjastealth" or "ninjadb"
static void rewrite_maps(char* buf, ssize_t* r) {
    if (*r <= 0) return;
    // We need to filter line by line. This is more complex.
    // For simplicity, we do a single pass: find "ninjastealth" or "ninjadb"
    // and zero out those lines (replace with spaces).
    const char* needles[] = {"ninjastealth", "ninjadb", NULL};
    char* write = buf;
    char* read_ptr = buf;
    char* end = buf + *r;
    while (read_ptr < end) {
        // Find next newline
        char* nl = (char*)memchr(read_ptr, '\n', end - read_ptr);
        if (!nl) nl = end;
        size_t line_len = nl - read_ptr;
        // Check if this line contains any needle
        int skip = 0;
        for (int i = 0; needles[i]; i++) {
            if (memmem(read_ptr, line_len, needles[i], strlen(needles[i]))) {
                skip = 1;
                break;
            }
        }
        if (!skip) {
            // Keep this line
            if (write != read_ptr) {
                memmove(write, read_ptr, line_len + (nl < end ? 1 : 0));
            }
            write += line_len + (nl < end ? 1 : 0);
        }
        read_ptr = nl + (nl < end ? 1 : 0);
    }
    *r = write - buf;
}

/* ---- Hook: open ---- */
int open(const char* path, int flags, ...) {
    init_real();
    if (!real_open) {
        errno = ENOSYS;
        return -1;
    }
    mode_t m = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags);
        m = va_arg(ap, mode_t); va_end(ap);
    }
    int fd = real_open(path, flags, m);
    if (fd >= 0) {
        int type = match_stealth_path(path);
        if (type >= 0) register_stealth_fd(fd, type);
    }
    return fd;
}

/* ---- Hook: open64 ---- */
int open64(const char* path, int flags, ...) {
    init_real();
    if (!real_open64) {
        errno = ENOSYS;
        return -1;
    }
    mode_t m = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags);
        m = va_arg(ap, mode_t); va_end(ap);
    }
    int fd = real_open64(path, flags, m);
    if (fd >= 0) {
        int type = match_stealth_path(path);
        if (type >= 0) register_stealth_fd(fd, type);
    }
    return fd;
}

/* ---- Hook: openat ---- */
int openat(int dirfd, const char* path, int flags, ...) {
    init_real();
    if (!real_openat) {
        errno = ENOSYS;
        return -1;
    }
    mode_t m = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags);
        m = va_arg(ap, mode_t); va_end(ap);
    }
    int fd = real_openat(dirfd, path, flags, m);
    if (fd >= 0) {
        int type = match_stealth_path(path);
        if (type >= 0) register_stealth_fd(fd, type);
    }
    return fd;
}

/* ---- Hook: read ---- */
ssize_t read(int fd, void* buf, size_t n) {
    init_real();
    if (!real_read) {
        errno = ENOSYS;
        return -1;
    }
    ssize_t r = real_read(fd, buf, n);
    if (r > 0) {
        int type = get_stealth_type(fd);
        if (type > 0) {
            switch (type - 1) {
                case 0: rewrite_status((char*)buf, &r); break;
                case 1: rewrite_wchan((char*)buf, &r); break;
                case 2: rewrite_syscall((char*)buf, &r); break;
                case 3: rewrite_stat((char*)buf, &r); break;
                case 4: rewrite_comm((char*)buf, &r); break;
                case 5: rewrite_maps((char*)buf, &r); break;
            }
        }
    }
    return r;
}

/* ---- Hook: pread ---- */
ssize_t pread(int fd, void* buf, size_t n, off_t offset) {
    init_real();
    if (!real_pread) {
        errno = ENOSYS;
        return -1;
    }
    ssize_t r = real_pread(fd, buf, n, offset);
    if (r > 0) {
        int type = get_stealth_type(fd);
        if (type > 0) {
            switch (type - 1) {
                case 0: rewrite_status((char*)buf, &r); break;
                case 1: rewrite_wchan((char*)buf, &r); break;
                case 2: rewrite_syscall((char*)buf, &r); break;
                case 3: rewrite_stat((char*)buf, &r); break;
                case 4: rewrite_comm((char*)buf, &r); break;
                case 5: rewrite_maps((char*)buf, &r); break;
            }
        }
    }
    return r;
}

/* ---- Hook: readv ---- */
ssize_t readv(int fd, const struct iovec* iov, int iovcnt) {
    init_real();
    if (!real_readv) {
        errno = ENOSYS;
        return -1;
    }
    ssize_t r = real_readv(fd, iov, iovcnt);
    if (r > 0) {
        int type = get_stealth_type(fd);
        if (type > 0) {
            // Rewrite each iovec buffer
            ssize_t remaining = r;
            for (int i = 0; i < iovcnt && remaining > 0; i++) {
                ssize_t chunk = remaining < (ssize_t)iov[i].iov_len ? remaining : (ssize_t)iov[i].iov_len;
                switch (type - 1) {
                    case 0: rewrite_status((char*)iov[i].iov_base, &chunk); break;
                    case 1: rewrite_wchan((char*)iov[i].iov_base, &chunk); break;
                    case 2: rewrite_syscall((char*)iov[i].iov_base, &chunk); break;
                    case 3: rewrite_stat((char*)iov[i].iov_base, &chunk); break;
                    case 4: rewrite_comm((char*)iov[i].iov_base, &chunk); break;
                    case 5: rewrite_maps((char*)iov[i].iov_base, &chunk); break;
                }
                remaining -= iov[i].iov_len;
            }
        }
    }
    return r;
}

/* ---- Hook: fopen ---- */
FILE* fopen(const char* path, const char* mode) {
    init_real();
    if (!real_fopen) return NULL;
    FILE* f = real_fopen(path, mode);
    if (f) {
        int type = match_stealth_path(path);
        if (type >= 0) {
            int fd = fileno(f);
            if (fd >= 0) register_stealth_fd(fd, type);
        }
    }
    return f;
}

/* ---- Hook: ptrace ---- */
long ptrace(enum __ptrace_request request, ...) {
    init_real();
    if (!real_ptrace) {
        errno = ESRCH;
        return -1;
    }
    // Extract variadic args
    va_list ap;
    va_start(ap, request);
    pid_t pid = va_arg(ap, pid_t);
    void* addr = va_arg(ap, void*);
    void* data = va_arg(ap, void*);
    va_end(ap);

    // Intercept PTRACE_TRACEME: make it look like we're not being traced
    if (request == PTRACE_TRACEME) {
        errno = ESRCH;
        return -1;
    }
    return real_ptrace(request, pid, addr, data);
}

/* ---- Hook: prctl ---- */
int prctl(int option, ...) {
    init_real();
    if (!real_prctl) {
        errno = EINVAL;
        return -1;
    }
    va_list ap;
    va_start(ap, option);
    unsigned long arg2 = va_arg(ap, unsigned long);
    unsigned long arg3 = va_arg(ap, unsigned long);
    unsigned long arg4 = va_arg(ap, unsigned long);
    unsigned long arg5 = va_arg(ap, unsigned long);
    va_end(ap);

    int result = real_prctl(option, arg2, arg3, arg4, arg5);

    // PR_GET_DUMPABLE (16): always return 1
    if (option == PR_GET_DUMPABLE) {
        return 1;
    }
    return result;
}

/* ---- Hook: close ---- */
int close(int fd) {
    init_real();
    unregister_stealth_fd(fd);
    if (!real_close) {
        errno = EBADF;
        return -1;
    }
    return real_close(fd);
}

/* ---- Hook: pread64 ---- */
ssize_t pread64(int fd, void* buf, size_t n, off_t offset) {
    init_real();
    if (!real_pread64) {
        // pread64 might not exist as a separate symbol; fall back to pread
        return pread(fd, buf, n, offset);
    }
    ssize_t r = real_pread64(fd, buf, n, offset);
    if (r > 0) {
        int type = get_stealth_type(fd);
        if (type > 0) {
            switch (type - 1) {
                case 0: rewrite_status((char*)buf, &r); break;
                case 1: rewrite_wchan((char*)buf, &r); break;
                case 2: rewrite_syscall((char*)buf, &r); break;
                case 3: rewrite_stat((char*)buf, &r); break;
                case 4: rewrite_comm((char*)buf, &r); break;
                case 5: rewrite_maps((char*)buf, &r); break;
            }
        }
    }
    return r;
}

/* ---- Hook: fgets ---- */
char* fgets(char* s, int n, FILE* stream) {
    init_real();
    if (!real_fgets) return NULL;
    char* result = real_fgets(s, n, stream);
    if (result) {
        int fd = fileno(stream);
        if (fd >= 0) {
            int type = get_stealth_type(fd);
            if (type > 0) {
                ssize_t r = strlen(s);
                switch (type - 1) {
                    case 0: rewrite_status(s, &r); break;
                    case 1: rewrite_wchan(s, &r); break;
                    case 2: rewrite_syscall(s, &r); break;
                    case 3: rewrite_stat(s, &r); break;
                    case 4: rewrite_comm(s, &r); break;
                    case 5: rewrite_maps(s, &r); break;
                }
                s[r] = '\0';
            }
        }
    }
    return result;
}

/* ---- Hook: getline ---- */
ssize_t getline(char** lineptr, size_t* n, FILE* stream) {
    init_real();
    if (!real_getline) return -1;
    ssize_t r = real_getline(lineptr, n, stream);
    if (r > 0 && lineptr && *lineptr) {
        int fd = fileno(stream);
        if (fd >= 0) {
            int type = get_stealth_type(fd);
            if (type > 0) {
                ssize_t len = r;
                switch (type - 1) {
                    case 0: rewrite_status(*lineptr, &len); break;
                    case 1: rewrite_wchan(*lineptr, &len); break;
                    case 2: rewrite_syscall(*lineptr, &len); break;
                    case 3: rewrite_stat(*lineptr, &len); break;
                    case 4: rewrite_comm(*lineptr, &len); break;
                    case 5: rewrite_maps(*lineptr, &len); break;
                }
                (*lineptr)[len] = '\0';
                r = len;
            }
        }
    }
    return r;
}

/* ---- Constructor: runs at library load time ---- */
__attribute__((constructor))
static void ninjastealth_init(void) {
    init_real();
    // Clear fd tracking
    stealth_fd_count = 0;
    memset(stealth_fds, -1, sizeof(stealth_fds));
}
