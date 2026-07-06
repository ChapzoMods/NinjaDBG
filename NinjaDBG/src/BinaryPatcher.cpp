// NinjaDBG v1.1.0 - BinaryPatcher implementation
// Open Source (MIT) - by Chapzoo
#include "BinaryPatcher.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cstdint>

namespace ndbg {

// ELF/PE/Mach-O magic constants
static const u8 ELF_MAGIC[4]  = {0x7F, 'E', 'L', 'F'};
static const u8 PE_MAGIC[2]   = {'M', 'Z'};
static const u8 MACHO_MAGIC[4] = {0xFE, 0xED, 0xFA, 0xCE};  // MH_MAGIC
static const u8 MACHO_MAGIC_64[4] = {0xFE, 0xED, 0xFA, 0xCF};  // MH_MAGIC_64
static const u8 MACHO_CIGAM[4] = {0xCE, 0xFA, 0xED, 0xFE};  // MH_CIGAM (swapped)
static const u8 MACHO_FAT[4]   = {0xCA, 0xFE, 0xBA, 0xBE};  // FAT_MAGIC

BinaryPatcher::BinaryPatcher() {}
BinaryPatcher::~BinaryPatcher() {}

bool BinaryPatcher::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        last_error_ = "Cannot open file: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz < 0) {
        last_error_ = "tellg failed on: " + path;
        return false;
    }
    f.seekg(0, std::ios::beg);
    original_.resize((size_t)sz);
    working_.resize((size_t)sz);
    f.read((char*)original_.data(), sz);
    f.close();
    std::memcpy(working_.data(), original_.data(), (size_t)sz);
    loaded_path_ = path;
    patches_.clear();

    if (!detectFormat()) {
        last_error_ = "Unknown binary format";
        return false;
    }
    switch (fmt_) {
        case BinFormat::ELF32:
        case BinFormat::ELF64:  parseElf();  break;
        case BinFormat::PE32:
        case BinFormat::PE64:   parsePE();   break;
        case BinFormat::MachO32:
        case BinFormat::MachO64:
        case BinFormat::MachO_FAT: parseMachO(); break;
        default: break;
    }
    return true;
}

bool BinaryPatcher::detectFormat() {
    if (original_.size() < 4) { fmt_ = BinFormat::Unknown; return false; }
    if (std::memcmp(original_.data(), ELF_MAGIC, 4) == 0) {
        // Check EI_CLASS (5th byte): 1=32, 2=64
        fmt_ = (original_[4] == 2) ? BinFormat::ELF64 : BinFormat::ELF32;
        return true;
    }
    if (original_.size() >= 2 && std::memcmp(original_.data(), PE_MAGIC, 2) == 0) {
        // PE: MZ header at start, then PE header at offset e_lfanew
        // For demo just mark as PE64 (most modern Windows binaries)
        fmt_ = BinFormat::PE64;
        return true;
    }
    if (std::memcmp(original_.data(), MACHO_MAGIC_64, 4) == 0) {
        fmt_ = BinFormat::MachO64; return true;
    }
    if (std::memcmp(original_.data(), MACHO_MAGIC, 4) == 0) {
        fmt_ = BinFormat::MachO32; return true;
    }
    if (std::memcmp(original_.data(), MACHO_FAT, 4) == 0) {
        fmt_ = BinFormat::MachO_FAT; return true;
    }
    fmt_ = BinFormat::Unknown;
    return false;
}

bool BinaryPatcher::parseElf() {
    // For ELF64: e_entry at offset 0x18 (8 bytes), e_phoff at 0x20,
    // e_shoff at 0x28, e_shentsize at 0x3A, e_shnum at 0x3C, e_shstrndx at 0x3E
    bool is64 = (fmt_ == BinFormat::ELF64);
    if (is64 && original_.size() >= 0x40) {
        u64 entry;
        std::memcpy(&entry, original_.data() + 0x18, 8);
        entry_ = entry;
        // Section headers
        u64 shoff;
        std::memcpy(&shoff, original_.data() + 0x28, 8);
        u16 shentsize, shnum, shstrndx;
        std::memcpy(&shentsize, original_.data() + 0x3A, 2);
        std::memcpy(&shnum,     original_.data() + 0x3C, 2);
        std::memcpy(&shstrndx,  original_.data() + 0x3E, 2);

        if (shoff + (u64)shnum * shentsize > original_.size()) return true;
        // Read string table section header
        if (shstrndx >= shnum) return true;
        u8* shstrtab_hdr = original_.data() + shoff + (u64)shstrndx * shentsize;
        u64 shstrtab_off;
        std::memcpy(&shstrtab_off, shstrtab_hdr + 0x18, 8);  // sh_offset for ELF64

        for (u16 i = 0; i < shnum; i++) {
            u8* sh = original_.data() + shoff + (u64)i * shentsize;
            u32 name_idx;
            std::memcpy(&name_idx, sh, 4);
            u32 sh_type;
            std::memcpy(&sh_type, sh + 4, 4);
            u64 sh_off, sh_size;
            std::memcpy(&sh_off,  sh + 0x18, 8);
            std::memcpy(&sh_size, sh + 0x20, 8);
            u64 sh_addr;
            std::memcpy(&sh_addr, sh + 0x10, 8);
            u64 sh_flags;
            std::memcpy(&sh_flags, sh + 8, 8);

            // Resolve name from shstrtab
            std::string nm;
            if (shstrtab_off + name_idx < original_.size()) {
                nm = std::string((const char*)(original_.data() + shstrtab_off + name_idx));
            }
            Section sec;
            sec.name = nm;
            sec.file_offset = sh_off;
            sec.vaddr = sh_addr;
            sec.size = sh_size;
            sec.exec  = (sh_flags & 0x4) != 0;  // SHF_EXECINSTR
            sec.write = (sh_flags & 0x1) != 0;  // SHF_WRITE
            sec.read  = true;  // ELF doesn't really have a read flag
            sections_.push_back(sec);
        }
    } else if (!is64 && original_.size() >= 0x34) {
        u32 entry;
        std::memcpy(&entry, original_.data() + 0x18, 4);
        entry_ = entry;
    }
    return true;
}

bool BinaryPatcher::parsePE() {
    // PE: at offset 0x3C is e_lfanew (offset to PE header)
    if (original_.size() < 0x40) return true;
    u32 lfanew;
    std::memcpy(&lfanew, original_.data() + 0x3C, 4);
    if (lfanew + 4 > original_.size()) return true;
    // PE signature "PE\0\0"
    if (std::memcmp(original_.data() + lfanew, "PE\0\0", 4) != 0) return true;
    // COFF header at lfanew+4, optional header at lfanew+24
    // AddressOfEntryPoint is at optional header offset 16 (PE32) or 16 (PE32+)
    // Total offset from start of file: lfanew + 24 + 16 = lfanew + 40, read 4 bytes
    if (lfanew + 44 > original_.size()) return true;  // bounds check
    u32 entry_rva;
    std::memcpy(&entry_rva, original_.data() + lfanew + 24 + 16, 4);
    entry_ = entry_rva;  // PE uses RVA, not absolute
    return true;
}

bool BinaryPatcher::parseMachO() {
    // Mach-O header: magic(4) + cputype(4) + cpusubtype(4) + filetype(4)
    // + ncmds(4) + sizeofcmds(4) + flags(4) [+ reserved(4) for 64-bit]
    // Entry point is in LC_UNIXTHREAD or LC_MAIN load command
    if (original_.size() < 28) return true;
    bool is64 = (fmt_ == BinFormat::MachO64);
    u32 magic;
    std::memcpy(&magic, original_.data(), 4);
    u32 ncmds;
    std::memcpy(&ncmds, original_.data() + 16, 4);
    size_t hdr_sz = is64 ? 32 : 28;
    size_t off = hdr_sz;
    for (u32 i = 0; i < ncmds && off + 8 <= original_.size(); i++) {
        u32 cmd, cmdsize;
        std::memcpy(&cmd, original_.data() + off, 4);
        std::memcpy(&cmdsize, original_.data() + off + 4, 4);
        if (cmdsize == 0) break;
        // LC_MAIN = 0x80000002 (entrypoint)
        // LC_UNIXTHREAD = 0x05
        if (cmd == 0x80000002 && off + 24 <= original_.size()) {
            u64 entryoff;
            std::memcpy(&entryoff, original_.data() + off + 8, 8);
            entry_ = entryoff;
        }
        off += cmdsize;
    }
    return true;
}

std::string BinaryPatcher::formatName() const {
    switch (fmt_) {
        case BinFormat::ELF32:     return "ELF 32-bit (Linux/BSD)";
        case BinFormat::ELF64:     return "ELF 64-bit (Linux/BSD)";
        case BinFormat::PE32:      return "PE 32-bit (Windows)";
        case BinFormat::PE64:      return "PE 64-bit (Windows)";
        case BinFormat::MachO32:   return "Mach-O 32-bit (macOS)";
        case BinFormat::MachO64:   return "Mach-O 64-bit (macOS)";
        case BinFormat::MachO_FAT: return "Mach-O FAT (universal macOS)";
        default:                   return "Unknown";
    }
}

bool BinaryPatcher::save(const std::string& out_path) {
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        last_error_ = "Cannot open output file: " + out_path;
        return false;
    }
    f.write((const char*)working_.data(), working_.size());
    f.close();
    return true;
}

void BinaryPatcher::reset() {
    if (original_.empty()) return;
    std::memcpy(working_.data(), original_.data(), original_.size());
    patches_.clear();
}

int BinaryPatcher::applyPatch(u64 file_offset, PatchKind kind, size_t length,
                                const std::vector<u8>& custom_bytes,
                                const std::string& note) {
    if (file_offset + length > working_.size()) {
        last_error_ = "Patch extends beyond end of file";
        return -1;
    }
    Patch p;
    p.file_offset = file_offset;
    p.length = length;
    p.kind = kind;
    p.note = note;
    p.original_bytes.assign(working_.data() + file_offset,
                             working_.data() + file_offset + length);

    switch (kind) {
        case PatchKind::NOP:
            p.patched_bytes.assign(length, 0x90);  // x86 NOP
            break;
        case PatchKind::CustomBytes:
            if (custom_bytes.size() != length) {
                last_error_ = "Custom bytes length mismatch";
                return -1;
            }
            p.patched_bytes = custom_bytes;
            break;
        case PatchKind::JmpAlways: {
            // Replace Jcc rel8/rel32 with JMP rel8/rel32
            // For Jcc rel8 (2 bytes: 7x xx): replace 1st byte with 0xEB (JMP rel8)
            // For Jcc rel32 (6 bytes: 0F 8x xx xx xx xx): replace with
            //   JMP rel32 (5 bytes: E9 xx xx xx xx) + 1 NOP
            // IMPORTANT: Jcc rel32 is RIP-relative to RIP+6, JMP rel32 is
            // RIP-relative to RIP+5. To land on the same target, decrement
            // the 32-bit displacement by 1 (little-endian).
            p.patched_bytes = p.original_bytes;
            if (length == 2) {
                p.patched_bytes[0] = 0xEB;
            } else if (length == 6) {
                p.patched_bytes[0] = 0xE9;
                // Decrement the rel32 displacement by 1 to account for
                // the 1-byte shorter instruction (5 vs 6 bytes).
                u32 disp = (u32)p.original_bytes[2]
                         | ((u32)p.original_bytes[3] << 8)
                         | ((u32)p.original_bytes[4] << 16)
                         | ((u32)p.original_bytes[5] << 24);
                disp -= 1;
                p.patched_bytes[1] = (u8)(disp & 0xFF);
                p.patched_bytes[2] = (u8)((disp >> 8) & 0xFF);
                p.patched_bytes[3] = (u8)((disp >> 16) & 0xFF);
                p.patched_bytes[4] = (u8)((disp >> 24) & 0xFF);
                p.patched_bytes[5] = 0x90;  // NOP padding
            }
            break;
        }
        case PatchKind::JmpNever:
            p.patched_bytes.assign(length, 0x90);
            break;
        case PatchKind::CallToNop:
            // 5-byte CALL → 5-byte NOP
            p.patched_bytes.assign(length, 0x90);
            break;
        case PatchKind::RetTrue:
            // Replace "call func; test eax,eax; jz L" with "mov eax,1; nop; nop; jz L"
            // Just put mov eax,1 (5 bytes: B8 01 00 00 00) and NOP the rest
            p.patched_bytes.assign(length, 0x90);
            p.patched_bytes[0] = 0xB8;
            p.patched_bytes[1] = 0x01;
            p.patched_bytes[2] = 0x00;
            p.patched_bytes[3] = 0x00;
            p.patched_bytes[4] = 0x00;
            break;
        case PatchKind::AsciiReplace:
            if (custom_bytes.size() > length) {
                last_error_ = "ASCII replacement longer than original";
                return -1;
            }
            p.patched_bytes.assign(length, 0x00);  // null-pad
            std::copy(custom_bytes.begin(), custom_bytes.end(), p.patched_bytes.begin());
            break;
    }

    // Apply
    std::memcpy(working_.data() + file_offset, p.patched_bytes.data(), length);
    patches_.push_back(p);
    return (int)patches_.size() - 1;
}

int BinaryPatcher::nopRange(u64 file_offset, size_t length, const std::string& note) {
    return applyPatch(file_offset, PatchKind::NOP, length, {}, note);
}

int BinaryPatcher::replaceAscii(u64 file_offset, const std::string& new_str,
                                  const std::string& note) {
    std::vector<u8> bytes(new_str.begin(), new_str.end());
    return applyPatch(file_offset, PatchKind::AsciiReplace, bytes.size(), bytes, note);
}

bool BinaryPatcher::undoPatch(int idx) {
    if (idx < 0 || idx >= (int)patches_.size()) return false;
    auto& p = patches_[idx];
    std::memcpy(working_.data() + p.file_offset, p.original_bytes.data(), p.length);
    patches_.erase(patches_.begin() + idx);
    return true;
}

std::vector<u8> BinaryPatcher::readBytes(u64 offset, size_t n) const {
    if (offset + n > working_.size()) return {};
    return std::vector<u8>(working_.data() + offset, working_.data() + offset + n);
}

i64 BinaryPatcher::findPattern(const std::vector<u8>& pattern) const {
    if (pattern.empty() || pattern.size() > original_.size()) return -1;
    for (size_t i = 0; i + pattern.size() <= original_.size(); i++) {
        if (std::memcmp(original_.data() + i, pattern.data(), pattern.size()) == 0) {
            return (i64)i;
        }
    }
    return -1;
}

i64 BinaryPatcher::findAscii(const std::string& needle) const {
    if (needle.empty()) return -1;
    std::vector<u8> p(needle.begin(), needle.end());
    return findPattern(p);
}

std::string BinaryPatcher::sha256Original() const {
    // Minimal SHA-256 placeholder — real implementation would use openssl or similar
    return "(sha256: " + std::to_string(original_.size()) + " bytes)";
}

std::string BinaryPatcher::sha256Patched() const {
    return "(sha256: " + std::to_string(working_.size()) + " bytes)";
}

} // namespace ndbg
