// NinjaDBG v1.1.4 - Binary Patcher
// Open Source (Apache-2.0) - by Chapzoo
//
// Allows in-place patching of binary files (ELF / PE / Mach-O) without
// needing to attach a debugger. Useful for:
//   - NOPing out anti-debug checks permanently
//   - Replacing CALL instructions with NOPs
//   - Patching conditional jumps (JZ -> JMP, JNZ -> JMP)
//   - Replacing hardcoded strings / serial checks
//   - Injecting a JMP to a code cave
//
// The patcher operates on file bytes directly. It can:
//   - Read a binary and present a hex view of arbitrary offsets
//   - Apply patches via several patch kinds (NOP, JMP, CALL→NOP, custom bytes)
//   - Save the patched binary to a new file (never overwrites source)
//   - Compute a checksum diff between original and patched
//
// ELF parsing is implemented natively. PE and Mach-O parsing is supported
// to the extent of identifying the format and locating the entry point and
// .text section, sufficient for most patching workflows. Full relocations
// are not applied — this is a static byte patcher, not a loader.
#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace ndbg {

class BinaryPatcher {
public:
    enum class BinFormat {
        Unknown,
        ELF32, ELF64,
        PE32,  PE64,
        MachO32, MachO64, MachO_FAT
    };

    enum class PatchKind {
        NOP,           // replace N bytes with 0x90 (x86) or 0x00 (arm)
        CustomBytes,   // user-supplied byte sequence
        JmpAlways,     // replace Jcc with JMP rel8/rel32 (x86 only)
        JmpNever,      // replace Jcc with 6 bytes of NOP
        CallToNop,     // replace CALL rel32 with 5 bytes of NOP
        RetTrue,       // replace "call func; test eax,eax; jz ..." with "mov eax,1; ..."
        AsciiReplace   // replace ASCII string with same-length new string
    };

    struct Patch {
        u64          file_offset;     // byte offset in file
        u64          vaddr;           // virtual address (for display)
        size_t       length;
        PatchKind    kind;
        std::vector<u8> original_bytes;
        std::vector<u8> patched_bytes;
        std::string  note;
    };

    BinaryPatcher();
    ~BinaryPatcher();

    // Load a binary file into memory (read-only copy preserved)
    bool load(const std::string& path);
    bool isLoaded() const { return !original_.empty(); }

    // Save the patched binary to a new file
    bool save(const std::string& out_path);

    // Reset to original state
    void reset();

    // Format detection + metadata
    BinFormat format() const { return fmt_; }
    std::string formatName() const;
    u64 entryPoint() const { return entry_; }
    u64 imageSize() const { return original_.size(); }
    std::vector<u8>& workingCopy() { return working_; }
    const std::vector<u8>& originalCopy() const { return original_; }

    // Section info
    struct Section {
        std::string name;
        u64 file_offset;
        u64 vaddr;
        u64 size;
        bool exec;
        bool write;
        bool read;
    };
    std::vector<Section> sections() const { return sections_; }

    // Apply a patch. Returns the patch index on success, -1 on failure.
    int applyPatch(u64 file_offset, PatchKind kind, size_t length,
                   const std::vector<u8>& custom_bytes = {},
                   const std::string& note = "");

    // Convenience: NOP a range
    int nopRange(u64 file_offset, size_t length, const std::string& note = "");

    // Convenience: replace ASCII string (must be same length or shorter)
    int replaceAscii(u64 file_offset, const std::string& new_str,
                     const std::string& note = "");

    // List applied patches
    const std::vector<Patch>& patches() const { return patches_; }

    // Undo a patch by index
    bool undoPatch(int idx);

    // Read N bytes at offset
    std::vector<u8> readBytes(u64 offset, size_t n) const;

    // Find byte pattern (returns first match offset, or -1)
    i64 findPattern(const std::vector<u8>& pattern) const;
    i64 findAscii(const std::string& needle) const;

    // Last error string
    std::string lastError() const { return last_error_; }

    // Compute SHA-256 of original and working copy for integrity display
    std::string sha256Original() const;
    std::string sha256Patched() const;

private:
    std::vector<u8> original_;
    std::vector<u8> working_;
    BinFormat       fmt_ = BinFormat::Unknown;
    u64             entry_ = 0;
    std::vector<Section> sections_;
    std::vector<Patch>   patches_;
    std::string     last_error_;
    std::string     loaded_path_;

    bool detectFormat();
    bool parseElf();
    bool parsePE();
    bool parseMachO();
};

} // namespace ndbg
