// NinjaDBG v1.1.0 - Kernel-Level Stealth Techniques
// Open Source (Apache-2.0) - by Chapzoo
//
// Userland anti-detect can mask /proc/self/status, rewrite argv[0], and use
// process_vm_readv to avoid ptrace events. But it cannot hide from a target
// that does any of the following:
//
//   1. Read /proc/self/wchan — if it returns "ptrace_stop", we're caught.
//   2. Read /proc/self/syscall — shows the currently stopped syscall.
//   3. Read /proc/<our_pid>/comm — reveals "ninjadb".
//   4. Inspect SIGCHLD delivery timing (a tracer generates extra SIGCHLDs).
//   5. Call prctl(PR_GET_DUMPABLE) — a traced process has dumpable=0 in some cases.
//   6. Read /proc/self/stat field 19 (kstkeip = instruction pointer when stopped).
//   7. Use seccomp-bpf to filter ptrace events.
//   8. Use audit subsystem / netlink connectors to observe system events.
//
// To bypass ALL of these, NinjaDBG must hook at the kernel level. This module
// documents and provides a generator for a Linux kernel module (LKM) called
// "ninja_stealth.ko" that, when loaded, hooks:
//
//   - proc_readdir()   → hides our pid from /proc listings
//   - proc_pid_readdir → hides us from /proc/<other>/task/<our_tid> reads
//   - proc_single_show → rewrites TracerPid, wchan, comm, syscall on read
//   - sys_ptrace       → returns ESRCH for any PTRACE_TRACEME attempt by target
//   - sys_prctl        → PR_SET_DUMPABLE always returns 1
//   - do_notify_resume → suppresses the extra SIGCHLD that traces produce
//
// The LKM source is generated on first run into scripts/ninja_stealth_kmod.c
// and compiled (if kernel headers are available) into build/ninja_stealth.ko.
//
// LOADING THE MODULE REQUIRES ROOT. NinjaDBG will detect at startup whether
// the module is loaded (by reading /proc/ninja_stealth) and report its state
// in the UI. If not loaded, the userland techniques from AntiDetect still
// apply — kernel-level is a strict superset, only relevant for the most
// aggressive anti-debug routines.
//
// IMPORTANT: This is a demonstration build. The generated kernel module is
// NOT signed and will only load on kernels with module signature verification
// disabled, or with MOK enrollment. Distribution of signed LKMs is outside
// the scope of this free release.
#pragma once

#include "Types.h"
#include <string>
#include <vector>

namespace ndbg {

class KernelStealth {
public:
    enum class KTechnique : int {
        HidePidFromProc       = 1 << 0,  // hide our pid from /proc readdir
        MaskWchan             = 1 << 1,  // rewrite /proc/self/wchan
        MaskSyscallField      = 1 << 2,  // rewrite /proc/self/syscall
        MaskCommField         = 1 << 3,  // rewrite /proc/<our_pid>/comm
        SuppressTracerSigchld = 1 << 4,  // suppress extra SIGCHLD to parent
        ForceDumpable         = 1 << 5,  // prctl(PR_GET_DUMPABLE) returns 1
        InterceptPtraceSelf   = 1 << 6,  // PTRACE_TRACEME by target returns ESRCH
        HideMmapRegions       = 1 << 7,  // hide our mmap regions from /proc/<pid>/maps
    };

    KernelStealth();
    ~KernelStealth();

    void enable(KTechnique t);
    void disable(KTechnique t);
    bool isEnabled(KTechnique t) const;
    u32  enabledMask() const { return mask_; }

    // Check if the ninja_stealth.ko LKM is currently loaded
    bool isModuleLoaded() const;
    std::string moduleStatus() const;

    // Generate the LKM source and attempt to compile it.
    // Returns the path to the .ko if compilation succeeded, empty otherwise.
    std::string buildKernelModule();

    // Attempt to load the module via insmod (requires root).
    // Returns true on success.
    bool loadModule();

    // Attempt to unload via rmmod.
    bool unloadModule();

    // Human-readable names + descriptions (for UI display)
    static std::string name(KTechnique t);
    static std::string description(KTechnique t);
    static std::vector<KTechnique> allTechniques();

    // Status report string for the UI
    std::string report() const;

    // The /proc path the LKM exposes for status checks
    static constexpr const char* kProcPath = "/proc/ninja_stealth";

private:
    u32 mask_ = 0;
    std::string kmod_path_;
    std::string kmod_src_path_;
};

} // namespace ndbg
