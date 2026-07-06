// NinjaDBG v1.0.1 - AntiDetect implementation
// Closed Source - Free - by Chapzoo
#include "AntiDetect.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>

namespace ndbg {

AntiDetect::AntiDetect() {
    // Enable a sensible default set
    mask_ = (u32)Technique::HWBreakpoints
          | (u32)Technique::ProcVmRW
          | (u32)Technique::MaskTracerPid
          | (u32)Technique::Int3ScanBypass
          | (u32)Technique::ParentMasquerade
          | (u32)Technique::TimingNormalize;
}

AntiDetect::~AntiDetect() {}

void AntiDetect::enable(Technique t)  { mask_ |= (u32)t; }
void AntiDetect::disable(Technique t) { mask_ &= ~(u32)t; }
bool AntiDetect::isEnabled(Technique t) const { return (mask_ & (u32)t) != 0; }

std::string AntiDetect::name(Technique t) {
    switch (t) {
        case Technique::HWBreakpoints:    return "Hardware Breakpoints";
        case Technique::ProcVmRW:         return "process_vm_readv/writev";
        case Technique::MaskTracerPid:    return "Mask /proc/self/status";
        case Technique::MaskSelfMaps:     return "Hide NinjaDBG mmaps";
        case Technique::TimingNormalize:  return "Timing normalization";
        case Technique::ParentMasquerade: return "Parent name masquerade";
        case Technique::HideFromPs:       return "Hide from ps list";
        case Technique::Int3ScanBypass:   return "INT3 scan bypass";
    }
    return "?";
}

std::string AntiDetect::description(Technique t) {
    switch (t) {
        case Technique::HWBreakpoints:
            return "Use DR0-DR3 debug registers instead of INT3. No 0xCC bytes appear in target's .text, "
                   "so byte-pattern scans for software breakpoints are defeated.";
        case Technique::ProcVmRW:
            return "Memory reads/writes use process_vm_readv(2) and process_vm_writev(2) rather than "
                   "PTRACE_PEEKDATA/POKEDATA, reducing visible ptrace events the target can observe.";
        case Technique::MaskTracerPid:
            return "Spoofs /proc/self/status reads inside the target by hooking open() and read() via "
                   "an injected LD_PRELOAD payload. The TracerPid field returns 0 even when traced.";
        case Technique::MaskSelfMaps:
            return "Filter NinjaDBG's own mmap regions out of /proc/<pid>/maps so the target cannot "
                   "discover injected preload libraries by enumerating its own address space.";
        case Technique::TimingNormalize:
            return "Wraps RDTSC / clock_gettime in the target to normalize elapsed cycles, defeating "
                   "single-step timing checks that compare before/after delta to detect slowdowns.";
        case Technique::ParentMasquerade:
            return "Renames the NinjaDBG parent process to look like a benign kernel worker "
                   "([kworker/u:1]) so checks on /proc/ppid/comm and /proc/ppid/cmdline pass.";
        case Technique::HideFromPs:
            return "Proactively renames argv[0] and the comm field of the NinjaDBG process so that "
                   "enumerate-ps / readdir-on-/proc style anti-debug scans skip over it.";
        case Technique::Int3ScanBypass:
            return "Disables software breakpoints entirely (or migrates them to hardware slots) when "
                   "this technique is on, so target .text scans for 0xCC find nothing.";
    }
    return "";
}

std::vector<AntiDetect::Technique> AntiDetect::allTechniques() {
    return {
        Technique::HWBreakpoints,
        Technique::ProcVmRW,
        Technique::MaskTracerPid,
        Technique::MaskSelfMaps,
        Technique::TimingNormalize,
        Technique::ParentMasquerade,
        Technique::HideFromPs,
        Technique::Int3ScanBypass,
    };
}

std::string AntiDetect::report() const {
    std::ostringstream s;
    s << "Anti-detect subsystem: " << std::hex << mask_ << "\n";
    s << "Active techniques: ";
    bool first = true;
    for (auto t : allTechniques()) {
        if (isEnabled(t)) {
            if (!first) s << ", ";
            s << name(t);
            first = false;
        }
    }
    if (first) s << "(none)";
    s << "\n";
    s << "Stealth rating: " << (mask_ == 0 ? "NONE" :
        (mask_ < 0x20 ? "PARTIAL" :
         (mask_ < 0x80 ? "HIGH" : "MAXIMUM")));
    return s.str();
}

std::string AntiDetect::buildPreloadPayload() {
    if (!preload_path_.empty()) return preload_path_;

    // Generate a small C source for the preload .so that hooks open()/read()
    // to mask TracerPid: in /proc/self/status. The .so is compiled on demand.
    preload_path_ = "/home/z/my-project/NinjaDBG/build/libninjastealth.so";

    std::string src_path = "/home/z/my-project/NinjaDBG/scripts/ninjastealth.c";
    std::ofstream s(src_path);
    s << R"STEALTH(
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
)STEALTH";
    s.close();

    // Compile it (caller is expected to run this)
    std::string cmd = "gcc -shared -fPIC -O2 -ldl -o " + preload_path_ + " " + src_path;
    system(cmd.c_str());
    return preload_path_;
}

} // namespace ndbg
