// NinjaDBG v1.1.2 - AntiDetect implementation
// Open Source (Apache-2.0) - by Chapzoo
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

    // v1.1.2: The stealth payload source is maintained as a hand-written file
    // at scripts/ninjastealth.c. We do NOT regenerate it (which would destroy
    // the carefully maintained hooks). We only compile it.
    //
    // Resolve paths relative to the executable to avoid hardcoded absolute paths.
    // Try /proc/self/exe first, then fall back to a compile-time default.
    std::string base_dir;
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = 0;
        std::string ep(exe_path);
        size_t slash = ep.rfind('/');
        if (slash != std::string::npos) {
            base_dir = ep.substr(0, slash);
            // Go up one level (from build/ to project root)
            size_t slash2 = base_dir.rfind('/');
            if (slash2 != std::string::npos) {
                base_dir = base_dir.substr(0, slash2);
            }
        }
    }
    if (base_dir.empty()) {
        base_dir = ".";  // fallback: current working directory
    }

    std::string src_path = base_dir + "/scripts/ninjastealth.c";
    preload_path_ = base_dir + "/build/libninjastealth.so";

    // Check that the source file exists
    std::ifstream check(src_path);
    if (!check.good()) {
        // Source not found — can't build
        return "";
    }
    check.close();

    // Create build directory if needed
    std::string mkdir_cmd = "mkdir -p " + base_dir + "/build";
    system(mkdir_cmd.c_str());

    // Compile the payload
    std::string cmd = "gcc -shared -fPIC -O2 -ldl -o " + preload_path_ + " " + src_path + " 2>/dev/null";
    int rc = system(cmd.c_str());
    if (rc != 0) {
        // Compilation failed
        preload_path_.clear();
        return "";
    }
    return preload_path_;
}

} // namespace ndbg
