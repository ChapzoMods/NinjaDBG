// NinjaDBG v1.0.1 - Anti-Detect Module (Stealth)
// Closed Source - Free - by Chapzoo
//
// Implements techniques to hide NinjaDBG from processes that try to detect
// debugger presence. On Linux the target's anti-debug routines typically:
//   1. Call ptrace(PTRACE_TRACEME) — fails if already traced.
//   2. Read /proc/self/status and look at TracerPid.
//   3. Read /proc/self/stat and check wchan or signal fields.
//   4. Scan own .text for INT3 (0xCC) software breakpoints.
//   5. Use timing checks (RDTSC / clock_gettime).
//   6. Check parent process cmdline.
//
// NinjaDBG counters:
//   - Hardware breakpoints (DR0-DR3) — no INT3 in target's code.
//   - process_vm_readv / process_vm_writev for memory access — fewer
//     ptrace events visible to the target.
//   - Spoofed /proc/self/status reads by intercepting open() in target
//     via LD_PRELOAD payload (stealth.so) that we inject.
//   - Timing normalization: PTRACE_SINGLESTEP wrap with rdtsc offset.
//   - Parent name masquerading: NinjaDBG exec's itself via a wrapper
//     that rewrites argv[0] to "[kworker/u:1]" so reading /proc/ppid/cmdline
//     returns a benign kernel-worker string.
//
// NOTE: This is a demonstration build. The status-masking hook is implemented
// via a small preload library (built alongside) that we instruct the target
// to load when launched under NinjaDBG. Attaching to a running process can
// only mask ptrace-TracerPid if we use a kernel module — out of scope here,
// but the API exposes a `maskTracerPid()` toggle for future use.
#pragma once

#include "Types.h"
#include <string>
#include <vector>

namespace ndbg {

class AntiDetect {
public:
    enum class Technique : int {
        HWBreakpoints      = 1 << 0,  // Use DR0-DR3, no INT3
        ProcVmRW           = 1 << 1,  // process_vm_readv/writev
        MaskTracerPid      = 1 << 2,  // Spoof /proc/self/status TracerPid
        MaskSelfMaps       = 1 << 3,  // Hide ninjadb mmaps from target's view
        TimingNormalize    = 1 << 4,  // Patch RDTSC / clock_gettime
        ParentMasquerade   = 1 << 5,  // argv[0] = "[kworker/u:1]"
        HideFromPs         = 1 << 6,  // Rename self in process list
        Int3ScanBypass     = 1 << 7,  // Don't leave 0xCC in .text (uses HW bp)
    };

    AntiDetect();
    ~AntiDetect();

    // Enable/disable a technique
    void enable(Technique t);
    void disable(Technique t);
    bool isEnabled(Technique t) const;
    u32  enabledMask() const { return mask_; }

    // Generate a status report for the UI
    std::string report() const;

    // Get a human-readable name/desc per technique
    static std::string name(Technique t);
    static std::string description(Technique t);

    // Inject the preload payload into a target's environment.
    // Returns the path to the .so to put in LD_PRELOAD.
    std::string buildPreloadPayload();

    // Generate the wrapper argv[0] string
    std::string maskedArgv0() const { return "[kworker/u:1]"; }

    // The set of techniques available as a list for the UI
    static std::vector<Technique> allTechniques();

private:
    u32 mask_ = 0;
    std::string preload_path_;
};

} // namespace ndbg
