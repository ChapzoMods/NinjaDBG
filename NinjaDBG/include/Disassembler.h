// NinjaDBG v1.1.0 - Standalone x86-64 Disassembler
// Open Source (MIT) - by Chapzoo
//
// A from-scratch length-disassembler + mnemonic decoder for x86-64.
// Covers legacy + SSE + AVX prefixes, REX, VEX, ModR/M, SIB, displacements,
// and produces human-readable text for the most common general-purpose,
// system, SIMD, and branch instructions. Not a full Capstone, but
// dramatically more complete than the inline decoder in DebuggerCore.
//
// The decoder is structured as:
//   1. Parse prefixes (legacy + REX)
//   2. Parse opcode (1, 2, or 3 bytes; possibly VEX-prefixed)
//   3. Parse ModR/M, SIB, displacement, immediate
//   4. Format mnemonic + operands
//
// Output is one Instruction per call. Call disassemble() repeatedly to walk
// a byte stream.
#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace ndbg {

class Disassembler {
public:
    struct Instruction {
        addr_t      address = 0;
        size_t      length = 0;
        u8          bytes[15] = {0};
        char        mnemonic[16] = {0};
        char        operands[80] = {0};
        char        text[128] = {0};      // "mnemonic operands"
        bool        is_call = false;
        bool        is_jmp = false;
        bool        is_conditional = false;
        bool        is_ret = false;
        bool        is_syscall = false;
        addr_t      branch_target = 0;    // valid if is_call || is_jmp
        bool        has_branch_target = false;
    };

    Disassembler();
    ~Disassembler();

    // Disassemble a single instruction at the given address.
    // `code` is the byte buffer; `offset` is the starting byte index.
    // Returns the decoded instruction (length field is 0 on failure).
    Instruction disassembleOne(addr_t address, const u8* code, size_t code_len, size_t offset = 0);

    // Disassemble N instructions starting at `address`.
    std::vector<Instruction> disassemble(addr_t address, const u8* code, size_t code_len, size_t n);

    // Convenience: disassemble N instructions from a target's memory
    // (caller supplies the bytes — the disassembler is pure / stateless)
    std::vector<Instruction> disassembleN(addr_t address, const std::vector<u8>& bytes, size_t n);

    // Format an instruction as a single human-readable line.
    // Format: "0xADDR  BYTES               MNEMONIC OPERANDS"
    static std::string format(const Instruction& ins, bool show_bytes = true);

    // Format with annotations (e.g. "  ; <target libc.so.6+0x1234>")
    static std::string formatAnnotated(const Instruction& ins,
                                        const std::string& annotation = "",
                                        bool show_bytes = true);

    // Group/category of an instruction (for filtering in the UI)
    enum class Category {
        General, System, SIMD, Branch, Stack, Compare, Arithmetic, Logic, Move, Other
    };
    Category category(const Instruction& ins) const;

private:
    // Internal decode state
    struct State {
        const u8* code;
        size_t    len;
        size_t    pos;          // current byte index
        addr_t    address;       // base address of this instruction

        bool      has_66 = false;  // operand-size override
        bool      has_67 = false;  // address-size override
        bool      has_F2 = false, has_F3 = false;
        u8        seg_override = 0;  // 0x26/0x2E/0x36/0x3E/0x64/0x65
        bool      rex = false;
        bool      rex_w = false;
        bool      rex_r = false;
        bool      rex_x = false;
        bool      rex_b = false;

        u8        opcode[3] = {0};
        size_t    opcode_len = 0;

        bool      has_modrm = false;
        u8        modrm = 0;
        bool      has_sib = false;
        u8        sib = 0;
        size_t    disp_size = 0;
        u32       disp = 0;

        size_t    imm_size = 0;
        u64       imm = 0;
    };

    bool parsePrefixes(State& s);
    bool parseOpcode(State& s);
    bool parseModRM(State& s, bool has_modrm);
    bool parseImmediate(State& s, size_t bytes);

    Instruction buildInstruction(State& s);

    // Helpers
    static const char* reg64(int i);
    static const char* reg32(int i);
    static const char* reg16(int i);
    static const char* reg8(int i, bool rex);
    static const char* xmm(int i);
    static const char* cc_name(u8 cc);  // condition code name for Jcc/SETcc/CMOVcc

    // Format ModR/M operand as memory or register
    std::string formatModRM(State& s, int operand_size);
};

} // namespace ndbg
