// NinjaDBG v1.1.3 - Disassembler implementation
// Open Source (Apache-2.0) - by Chapzoo
#include "Disassembler.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <algorithm>

namespace ndbg {

Disassembler::Disassembler() {}
Disassembler::~Disassembler() {}

const char* Disassembler::reg64(int i) {
    static const char* r[] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
                              "r8","r9","r10","r11","r12","r13","r14","r15"};
    return r[i & 0xF];
}
const char* Disassembler::reg32(int i) {
    static const char* r[] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi",
                              "r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d"};
    return r[i & 0xF];
}
const char* Disassembler::reg16(int i) {
    static const char* r[] = {"ax","cx","dx","bx","sp","bp","si","di",
                              "r8w","r9w","r10w","r11w","r12w","r13w","r14w","r15w"};
    return r[i & 0xF];
}
const char* Disassembler::reg8(int i, bool rex) {
    if (rex) {
        static const char* r[] = {"al","cl","dl","bl","spl","bpl","sil","dil",
                                  "r8b","r9b","r10b","r11b","r12b","r13b","r14b","r15b"};
        return r[i & 0xF];
    } else {
        static const char* r[] = {"al","cl","dl","bl","ah","ch","dh","bh"};
        return r[i & 0x7];
    }
}
const char* Disassembler::xmm(int i) {
    static const char* r[] = {"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7",
                              "xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"};
    return r[i & 0xF];
}
const char* Disassembler::cc_name(u8 cc) {
    static const char* names[] = {"o","no","b","ae","e","ne","be","a",
                                   "s","ns","p","np","l","ge","le","g"};
    return names[cc & 0xF];
}

bool Disassembler::parsePrefixes(State& s) {
    bool got_prefix = true;
    while (got_prefix && s.pos < s.len) {
        got_prefix = false;
        u8 b = s.code[s.pos];
        switch (b) {
            case 0xF0: got_prefix = true; break;  // LOCK
            case 0xF2: s.has_F2 = true; got_prefix = true; break;
            case 0xF3: s.has_F3 = true; got_prefix = true; break;
            case 0x66: s.has_66 = true; got_prefix = true; break;
            case 0x67: s.has_67 = true; got_prefix = true; break;
            case 0x2E: case 0x36: case 0x3E: case 0x26: case 0x64: case 0x65:
                s.seg_override = b; got_prefix = true; break;
            default:
                if (b >= 0x40 && b <= 0x4F) {
                    s.rex = true;
                    s.rex_w = (b & 0x8) != 0;
                    s.rex_r = (b & 0x4) != 0;
                    s.rex_x = (b & 0x2) != 0;
                    s.rex_b = (b & 0x1) != 0;
                    got_prefix = true;
                }
                break;
        }
        if (got_prefix) s.pos++;
    }
    return true;
}

bool Disassembler::parseOpcode(State& s) {
    if (s.pos >= s.len) return false;
    s.opcode[0] = s.code[s.pos++];
    s.opcode_len = 1;
    if (s.opcode[0] == 0x0F) {
        if (s.pos >= s.len) return false;
        s.opcode[1] = s.code[s.pos++];
        s.opcode_len = 2;
        if (s.opcode[1] == 0x38 || s.opcode[1] == 0x3A) {
            if (s.pos >= s.len) return false;
            s.opcode[2] = s.code[s.pos++];
            s.opcode_len = 3;
        }
    }
    return true;
}

bool Disassembler::parseModRM(State& s, bool has_modrm) {
    if (!has_modrm) return true;
    if (s.pos >= s.len) return false;
    s.has_modrm = true;
    s.modrm = s.code[s.pos++];
    u8 mod = (s.modrm >> 6) & 3;
    u8 rm = s.modrm & 7;
    if (mod != 3) {
        // memory operand; may have SIB and displacement
        if (mod == 0 && rm == 5) {
            // disp32 (RIP-relative in 64-bit)
            s.disp_size = 4;
            if (s.pos + 4 > s.len) return false;
            s.disp = (u32)s.code[s.pos] | ((u32)s.code[s.pos+1]<<8) |
                     ((u32)s.code[s.pos+2]<<16) | ((u32)s.code[s.pos+3]<<24);
            s.pos += 4;
        } else if (rm == 4) {
            // SIB follows
            if (s.pos >= s.len) return false;
            s.has_sib = true;
            s.sib = s.code[s.pos++];
            u8 base = s.sib & 7;
            if (mod == 0 && base == 5) {
                // disp32
                s.disp_size = 4;
                if (s.pos + 4 > s.len) return false;
                s.disp = (u32)s.code[s.pos] | ((u32)s.code[s.pos+1]<<8) |
                         ((u32)s.code[s.pos+2]<<16) | ((u32)s.code[s.pos+3]<<24);
                s.pos += 4;
            } else if (mod == 1) {
                s.disp_size = 1;
                if (s.pos + 1 > s.len) return false;
                s.disp = (u8)s.code[s.pos++];
            } else if (mod == 2) {
                s.disp_size = 4;
                if (s.pos + 4 > s.len) return false;
                s.disp = (u32)s.code[s.pos] | ((u32)s.code[s.pos+1]<<8) |
                         ((u32)s.code[s.pos+2]<<16) | ((u32)s.code[s.pos+3]<<24);
                s.pos += 4;
            }
        } else if (mod == 1) {
            s.disp_size = 1;
            if (s.pos + 1 > s.len) return false;
            s.disp = (u8)s.code[s.pos++];
        } else if (mod == 2) {
            s.disp_size = 4;
            if (s.pos + 4 > s.len) return false;
            s.disp = (u32)s.code[s.pos] | ((u32)s.code[s.pos+1]<<8) |
                     ((u32)s.code[s.pos+2]<<16) | ((u32)s.code[s.pos+3]<<24);
            s.pos += 4;
        }
    }
    return true;
}

bool Disassembler::parseImmediate(State& s, size_t bytes) {
    s.imm_size = bytes;
    if (bytes == 0) return true;
    if (s.pos + bytes > s.len) return false;
    s.imm = 0;
    for (size_t i = 0; i < bytes; i++) {
        s.imm |= ((u64)s.code[s.pos + i]) << (i * 8);
    }
    s.pos += bytes;
    return true;
}

std::string Disassembler::formatModRM(State& s, int operand_size) {
    u8 mod = (s.modrm >> 6) & 3;
    u8 reg = (s.modrm >> 3) & 7;
    u8 rm  = s.modrm & 7;

    char buf[128];
    if (mod == 3) {
        // register direct
        int full = rm | (s.rex_b << 3);
        const char* r = (operand_size == 8) ? reg64(full) :
                        (operand_size == 4) ? reg32(full) :
                        (operand_size == 2) ? reg16(full) :
                        reg8(full, s.rex);
        snprintf(buf, sizeof(buf), "%s", r);
        return buf;
    }

    // memory operand
    std::string m = "[";
    if (s.seg_override) {
        switch (s.seg_override) {
            case 0x2E: m += "cs:"; break;
            case 0x36: m += "ss:"; break;
            case 0x3E: m += "ds:"; break;
            case 0x26: m += "es:"; break;
            case 0x64: m += "fs:"; break;
            case 0x65: m += "gs:"; break;
            default: break;
        }
    }

    if (rm == 4 && s.has_sib) {
        u8 scale = (s.sib >> 6) & 3;
        u8 idx   = (s.sib >> 3) & 7;
        u8 base  = s.sib & 7;
        if (mod == 0 && base == 5) {
            // disp32 only (with optional index)
            snprintf(buf, sizeof(buf), "0x%x", s.disp);
            m += buf;
            if (idx != 4) {
                int full_idx = idx | (s.rex_x << 3);
                snprintf(buf, sizeof(buf), " + %s*%d", reg64(full_idx), 1 << scale);
                m += buf;
            }
        } else {
            int full_base = base | (s.rex_b << 3);
            m += reg64(full_base);
            if (idx != 4) {
                int full_idx = idx | (s.rex_x << 3);
                snprintf(buf, sizeof(buf), " + %s*%d", reg64(full_idx), 1 << scale);
                m += buf;
            }
            if (mod == 1) {
                int8_t d = (int8_t)(u8)s.disp;
                if (d >= 0) snprintf(buf, sizeof(buf), " + 0x%x", d);
                else        snprintf(buf, sizeof(buf), " - 0x%x", -d);
                m += buf;
            } else if (mod == 2) {
                snprintf(buf, sizeof(buf), " + 0x%x", s.disp);
                m += buf;
            }
        }
    } else if (mod == 0 && rm == 5) {
        // RIP-relative
        i32 disp = (i32)s.disp;
        addr_t target = s.address + s.pos - (s.opcode_len) - 0 + disp + (s.pos - s.opcode_len);
        // Simpler: target = address_of_next_instruction + disp
        // We don't know length yet — caller will compute it.
        snprintf(buf, sizeof(buf), "rip%s0x%x", disp >= 0 ? "+" : "-", disp >= 0 ? disp : -disp);
        m += buf;
    } else {
        int full_rm = rm | (s.rex_b << 3);
        m += reg64(full_rm);
        if (mod == 1) {
            int8_t d = (int8_t)(u8)s.disp;
            if (d >= 0) snprintf(buf, sizeof(buf), " + 0x%x", d);
            else        snprintf(buf, sizeof(buf), " - 0x%x", -d);
            m += buf;
        } else if (mod == 2) {
            snprintf(buf, sizeof(buf), " + 0x%x", s.disp);
            m += buf;
        }
    }
    m += "]";
    return m;
}

Disassembler::Instruction Disassembler::disassembleOne(addr_t address, const u8* code, size_t code_len, size_t offset) {
    State s;
    s.code = code;
    s.len = code_len;
    s.pos = offset;
    s.address = address;

    if (!parsePrefixes(s) || !parseOpcode(s)) {
        Instruction ins;
        ins.address = address;
        ins.length = 0;
        return ins;
    }

    Instruction ins;
    ins.address = address;

    u8 op = s.opcode[0];
    int operand_size = s.rex_w ? 8 : (s.has_66 ? 2 : 4);
    const char* mnem = "???";
    char ops[128] = {0};
    bool needs_modrm = false;
    size_t imm_bytes = 0;

    auto reg_field = [&](int size) -> const char* {
        int full = ((s.modrm >> 3) & 7) | (s.rex_r << 3);
        return (size == 8) ? reg64(full) :
               (size == 4) ? reg32(full) :
               (size == 2) ? reg16(full) :
               reg8(full, s.rex);
    };
    auto rm_field = [&](int size) -> const char* {
        int full = (s.modrm & 7) | (s.rex_b << 3);
        if (((s.modrm >> 6) & 3) == 3) {
            return (size == 8) ? reg64(full) :
                   (size == 4) ? reg32(full) :
                   (size == 2) ? reg16(full) :
                   reg8(full, s.rex);
        }
        return "mem";
    };

    // ===== 1-byte opcodes =====
    if (op == 0x00 || op == 0x01 || op == 0x02 || op == 0x03 ||
        op == 0x08 || op == 0x09 || op == 0x0A || op == 0x0B ||
        op == 0x10 || op == 0x11 || op == 0x12 || op == 0x13 ||
        op == 0x18 || op == 0x19 || op == 0x1A || op == 0x1B ||
        op == 0x20 || op == 0x21 || op == 0x22 || op == 0x23 ||
        op == 0x28 || op == 0x29 || op == 0x2A || op == 0x2B ||
        op == 0x30 || op == 0x31 || op == 0x32 || op == 0x33 ||
        op == 0x38 || op == 0x39 || op == 0x3A || op == 0x3B) {
        static const char* mnems[8] = {"add","or","adc","sbb","and","sub","xor","cmp"};
        int idx = (op >> 3) & 7;
        mnem = mnems[idx];
        // Bit 0 of opcode selects size: 0 = 8-bit, 1 = operand-size (16/32/64)
        bool is_8bit = (op & 1) == 0;
        // Bit 1 of opcode selects direction: 0 = r/m, reg ; 1 = reg, r/m
        bool mem_first = (op & 2) == 0;
        int dir_size = is_8bit ? 1 : operand_size;
        needs_modrm = true;
        if (!parseModRM(s, true)) { ins.length = 0; return ins; }
        std::string mem = formatModRM(s, dir_size);
        if (mem_first) {
            snprintf(ops, sizeof(ops), "%s, %s", mem.c_str(), reg_field(dir_size));
        } else {
            snprintf(ops, sizeof(ops), "%s, %s", reg_field(dir_size), mem.c_str());
        }
    }
    else if (op >= 0x50 && op <= 0x57) { mnem = "push"; snprintf(ops, sizeof(ops), "%s", reg64(op - 0x50)); ins.is_call=false; }
    else if (op >= 0x58 && op <= 0x5F) { mnem = "pop";  snprintf(ops, sizeof(ops), "%s", reg64(op - 0x58)); }
    else if (op == 0x90) {
        mnem = s.rex_b ? "xchg rax, r8" : "nop";
    }
    else if (op >= 0x91 && op <= 0x97) { mnem = "xchg"; snprintf(ops, sizeof(ops), "rax, %s", reg64(op - 0x90)); }
    else if (op == 0xCC) { mnem = "int3"; ins.is_call = false; }
    else if (op == 0xCD) { mnem = "int"; imm_bytes = 1; if (parseImmediate(s, 1)) snprintf(ops, sizeof(ops), "0x%02llx", (unsigned long long)s.imm); }
    else if (op == 0xCE) { mnem = "into"; }
    else if (op == 0xCF) { mnem = "iret"; }
    else if (op == 0xC3) { mnem = "ret"; ins.is_ret = true; }
    else if (op == 0xC2) { mnem = "ret"; imm_bytes = 2; if (parseImmediate(s, 2)) snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)s.imm); ins.is_ret = true; }
    else if (op == 0xCB) { mnem = "retf"; ins.is_ret = true; }
    else if (op == 0xCA) { mnem = "retf"; imm_bytes = 2; if (parseImmediate(s, 2)) snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)s.imm); ins.is_ret = true; }
    else if (op == 0xE8) {
        mnem = "call";
        imm_bytes = 4;
        if (parseImmediate(s, 4)) {
            i32 rel = (i32)s.imm;
            addr_t target = s.address + (s.pos - offset) + rel;
            snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)target);
            ins.branch_target = target;
            ins.has_branch_target = true;
            ins.is_call = true;
        }
    }
    else if (op == 0xE9) {
        mnem = "jmp";
        imm_bytes = 4;
        if (parseImmediate(s, 4)) {
            i32 rel = (i32)s.imm;
            addr_t target = s.address + (s.pos - offset) + rel;
            snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)target);
            ins.branch_target = target;
            ins.has_branch_target = true;
            ins.is_jmp = true;
        }
    }
    else if (op == 0xEB) {
        mnem = "jmp";
        imm_bytes = 1;
        if (parseImmediate(s, 1)) {
            i8 rel = (i8)s.imm;
            addr_t target = s.address + (s.pos - offset) + rel;
            snprintf(ops, sizeof(ops), "short 0x%llx", (unsigned long long)target);
            ins.branch_target = target;
            ins.has_branch_target = true;
            ins.is_jmp = true;
        }
    }
    else if (op >= 0x70 && op <= 0x7F) {
        static const char* ccs[] = {"jo","jno","jb","jae","je","jne","jbe","ja",
                                     "js","jns","jp","jnp","jl","jge","jle","jg"};
        mnem = ccs[op - 0x70];
        imm_bytes = 1;
        if (parseImmediate(s, 1)) {
            i8 rel = (i8)s.imm;
            addr_t target = s.address + (s.pos - offset) + rel;
            snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)target);
            ins.branch_target = target;
            ins.has_branch_target = true;
            ins.is_jmp = true;
            ins.is_conditional = true;
        }
    }
    else if (op >= 0x40 && op <= 0x47) { mnem = "inc"; snprintf(ops, sizeof(ops), "%s", reg32(op - 0x40)); }
    else if (op >= 0x48 && op <= 0x4F) { mnem = "dec"; snprintf(ops, sizeof(ops), "%s", reg32(op - 0x48)); }
    else if (op == 0xA8) { mnem = "test"; imm_bytes = 1; if (parseImmediate(s, 1)) snprintf(ops, sizeof(ops), "al, 0x%02llx", (unsigned long long)s.imm); }
    else if (op == 0xA9) { mnem = "test"; imm_bytes = (operand_size == 2) ? 2 : 4; if (parseImmediate(s, imm_bytes)) snprintf(ops, sizeof(ops), "%s, 0x%llx", operand_size==8?"rax":(operand_size==2?"ax":"eax"), (unsigned long long)s.imm); }
    else if (op == 0xB8 || op == 0xB9 || op == 0xBA || op == 0xBB ||
             op == 0xBC || op == 0xBD || op == 0xBE || op == 0xBF) {
        mnem = "mov";
        int r = (op - 0xB8) | (s.rex_b << 3);
        imm_bytes = operand_size == 8 ? 8 : (operand_size == 2 ? 2 : 4);
        if (parseImmediate(s, imm_bytes)) {
            snprintf(ops, sizeof(ops), "%s, 0x%llx",
                     operand_size == 8 ? reg64(r) :
                     operand_size == 4 ? reg32(r) : reg16(r),
                     (unsigned long long)s.imm);
        }
    }
    else if (op >= 0xB0 && op <= 0xB7) {
        mnem = "mov";
        int r = (op - 0xB0) | (s.rex_b << 3);
        imm_bytes = 1;
        if (parseImmediate(s, 1))
            snprintf(ops, sizeof(ops), "%s, 0x%llx", reg8(r, s.rex), (unsigned long long)s.imm);
    }
    else if (op == 0xC6) {
        mnem = "mov";
        needs_modrm = true;
        if (parseModRM(s, true)) {
            std::string mem = formatModRM(s, 1);
            imm_bytes = 1;
            if (parseImmediate(s, 1))
                snprintf(ops, sizeof(ops), "byte %s, 0x%llx", mem.c_str(), (unsigned long long)s.imm);
        }
    }
    else if (op == 0xC7) {
        mnem = "mov";
        needs_modrm = true;
        if (parseModRM(s, true)) {
            std::string mem = formatModRM(s, operand_size);
            imm_bytes = operand_size == 8 ? 4 : (operand_size == 2 ? 2 : 4);  // mov r/m64, imm32 (sign-extended)
            if (parseImmediate(s, imm_bytes))
                snprintf(ops, sizeof(ops), "%s, 0x%llx", mem.c_str(), (unsigned long long)s.imm);
        }
    }
    else if (op == 0x83) {
        // op r/m, imm8 — group 1
        needs_modrm = true;
        if (parseModRM(s, true)) {
            static const char* g1[] = {"add","or","adc","sbb","and","sub","xor","cmp"};
            int sub = (s.modrm >> 3) & 7;
            mnem = g1[sub];
            std::string mem = formatModRM(s, operand_size);
            imm_bytes = 1;
            if (parseImmediate(s, 1)) {
                i8 v = (i8)s.imm;
                snprintf(ops, sizeof(ops), "%s, 0x%x", mem.c_str(), (u8)v);
            }
        }
    }
    else if (op == 0x81) {
        needs_modrm = true;
        if (parseModRM(s, true)) {
            static const char* g1[] = {"add","or","adc","sbb","and","sub","xor","cmp"};
            int sub = (s.modrm >> 3) & 7;
            mnem = g1[sub];
            std::string mem = formatModRM(s, operand_size);
            imm_bytes = operand_size == 2 ? 2 : 4;
            if (parseImmediate(s, imm_bytes)) {
                snprintf(ops, sizeof(ops), "%s, 0x%llx", mem.c_str(), (unsigned long long)s.imm);
            }
        }
    }
    else if (op == 0xFF) {
        // group 5: inc / dec / call / jmp / push
        needs_modrm = true;
        if (parseModRM(s, true)) {
            int sub = (s.modrm >> 3) & 7;
            std::string mem = formatModRM(s, operand_size);
            switch (sub) {
                case 0: mnem = "inc"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
                case 1: mnem = "dec"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
                case 2: mnem = "call"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); ins.is_call = true; break;
                case 3: mnem = "callf"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); ins.is_call = true; break;
                case 4: mnem = "jmp"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); ins.is_jmp = true; break;
                case 5: mnem = "jmpf"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); ins.is_jmp = true; break;
                case 6: mnem = "push"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
                default: mnem = "(ff /?)";
            }
        }
    }
    else if (op == 0x88 || op == 0x89 || op == 0x8A || op == 0x8B) {
        mnem = "mov";
        needs_modrm = true;
        bool is_byte = (op & 1) == 0;
        int size = is_byte ? 1 : operand_size;
        if (parseModRM(s, true)) {
            std::string mem = formatModRM(s, size);
            if (op & 2) {
                // 8A / 8B: reg, r/m
                snprintf(ops, sizeof(ops), "%s, %s", reg_field(size), mem.c_str());
            } else {
                // 88 / 89: r/m, reg
                snprintf(ops, sizeof(ops), "%s, %s", mem.c_str(), reg_field(size));
            }
        }
    }
    else if (op == 0x8D) {
        mnem = "lea";
        needs_modrm = true;
        if (parseModRM(s, true)) {
            std::string mem = formatModRM(s, operand_size);
            snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), mem.c_str());
        }
    }
    else if (op == 0xF6 || op == 0xF7) {
        // group 3: test/not/neg/mul/imul/div/idiv
        needs_modrm = true;
        if (parseModRM(s, true)) {
            int sub = (s.modrm >> 3) & 7;
            std::string mem = formatModRM(s, op == 0xF6 ? 1 : operand_size);
            switch (sub) {
                case 0: case 1: {
                    mnem = "test";
                    imm_bytes = (op == 0xF6) ? 1 : (operand_size == 2 ? 2 : 4);
                    if (parseImmediate(s, imm_bytes))
                        snprintf(ops, sizeof(ops), "%s, 0x%llx", mem.c_str(), (unsigned long long)s.imm);
                    break;
                }
                case 2: mnem = "not"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
                case 3: mnem = "neg"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
                case 4: mnem = "mul"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
                case 5: mnem = "imul"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
                case 6: mnem = "div"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
                case 7: mnem = "idiv"; snprintf(ops, sizeof(ops), "%s", mem.c_str()); break;
            }
        }
    }
    else if (op == 0xC1 || op == 0xC0) {
        // shift r/m, imm8 — group 2
        needs_modrm = true;
        if (parseModRM(s, true)) {
            static const char* g2[] = {"rol","ror","rcl","rcr","shl","shr","shl","sar"};
            int sub = (s.modrm >> 3) & 7;
            mnem = g2[sub];
            int size = (op == 0xC0) ? 1 : operand_size;
            std::string mem = formatModRM(s, size);
            imm_bytes = 1;
            if (parseImmediate(s, 1))
                snprintf(ops, sizeof(ops), "%s, 0x%llx", mem.c_str(), (unsigned long long)s.imm);
        }
    }
    else if (op == 0xD1 || op == 0xD3) {
        // shift r/m, 1 / shift r/m, cl — group 2
        needs_modrm = true;
        if (parseModRM(s, true)) {
            static const char* g2[] = {"rol","ror","rcl","rcr","shl","shr","shl","sar"};
            int sub = (s.modrm >> 3) & 7;
            mnem = g2[sub];
            int size = (op == 0xD1) ? operand_size : operand_size;  // size depends on 66/REX
            std::string mem = formatModRM(s, size);
            if (op == 0xD1) snprintf(ops, sizeof(ops), "%s, 1", mem.c_str());
            else            snprintf(ops, sizeof(ops), "%s, cl", mem.c_str());
        }
    }
    else if (op == 0x85) {
        mnem = "test"; needs_modrm = true;
        if (parseModRM(s, true)) {
            std::string mem = formatModRM(s, operand_size);
            snprintf(ops, sizeof(ops), "%s, %s", mem.c_str(), reg_field(operand_size));
        }
    }
    else if (op == 0x84) { mnem = "test"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, 1); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(1)); } }
    else if (op == 0x86) { mnem = "xchg"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, 1); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(1)); } }
    else if (op == 0x87) { mnem = "xchg"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, operand_size); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(operand_size)); } }
    else if (op == 0x8E) { mnem = "mov"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, 2); static const char* sregs[] = {"es","cs","ss","ds","fs","gs","?","?"}; int sri = (s.modrm>>3)&7; snprintf(ops, sizeof(ops), "%s, %s", sregs[sri], m.c_str()); } }
    else if (op == 0x8C) { mnem = "mov"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, 2); static const char* sregs[] = {"es","cs","ss","ds","fs","gs","?","?"}; int sri = (s.modrm>>3)&7; snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), sregs[sri]); } }
    else if (op == 0xF4) { mnem = "hlt"; }
    else if (op == 0xF8) { mnem = "clc"; }
    else if (op == 0xF9) { mnem = "stc"; }
    else if (op == 0xFA) { mnem = "cli"; }
    else if (op == 0xFB) { mnem = "sti"; }
    else if (op == 0xFC) { mnem = "cld"; }
    else if (op == 0xFD) { mnem = "std"; }
    else if (op == 0x9C) { mnem = "pushf"; }
    else if (op == 0x9D) { mnem = "popf"; }
    else if (op == 0x9E) { mnem = "sahf"; }
    else if (op == 0x9F) { mnem = "lahf"; }
    else if (op == 0xC9) { mnem = "leave"; }
    else if (op == 0xE4) { mnem = "in"; imm_bytes = 1; if (parseImmediate(s, 1)) snprintf(ops, sizeof(ops), "al, 0x%llx", (unsigned long long)s.imm); }
    else if (op == 0xE5) { mnem = "in"; imm_bytes = 1; if (parseImmediate(s, 1)) snprintf(ops, sizeof(ops), "%s, 0x%llx", operand_size==4?"eax":"ax", (unsigned long long)s.imm); }
    else if (op == 0xE6) { mnem = "out"; imm_bytes = 1; if (parseImmediate(s, 1)) snprintf(ops, sizeof(ops), "0x%llx, al", (unsigned long long)s.imm); }
    else if (op == 0xE7) { mnem = "out"; imm_bytes = 1; if (parseImmediate(s, 1)) snprintf(ops, sizeof(ops), "0x%llx, %s", (unsigned long long)s.imm, operand_size==4?"eax":"ax"); }
    else if (op == 0xEC) { mnem = "in"; snprintf(ops, sizeof(ops), "al, dx"); }
    else if (op == 0xED) { mnem = "in"; snprintf(ops, sizeof(ops), "%s, dx", operand_size==4?"eax":"ax"); }
    else if (op == 0xEE) { mnem = "out"; snprintf(ops, sizeof(ops), "dx, al"); }
    else if (op == 0xEF) { mnem = "out"; snprintf(ops, sizeof(ops), "dx, %s", operand_size==4?"eax":"ax"); }
    else if (op == 0xA4) { mnem = "movsb"; }
    else if (op == 0xA5) { mnem = operand_size==4 ? "movsd" : (operand_size==8 ? "movsq" : "movsw"); }
    else if (op == 0xA6) { mnem = "cmpsb"; }
    else if (op == 0xA7) { mnem = operand_size==4 ? "cmpsd" : (operand_size==8 ? "cmpsq" : "cmpsw"); }
    else if (op == 0xAA) { mnem = "stosb"; }
    else if (op == 0xAB) { mnem = operand_size==4 ? "stosd" : (operand_size==8 ? "stosq" : "stosw"); }
    else if (op == 0xAC) { mnem = "lodsb"; }
    else if (op == 0xAD) { mnem = operand_size==4 ? "lodsd" : (operand_size==8 ? "lodsq" : "lodsw"); }
    else if (op == 0xAE) { mnem = "scasb"; }
    else if (op == 0xAF) { mnem = operand_size==4 ? "scasd" : (operand_size==8 ? "scasq" : "scasw"); }
    else if (op == 0xF2 && s.pos < s.len && s.code[s.pos] == 0xAE) {
        mnem = "repne scasb"; s.pos++;
    }
    else if (op == 0xF3 && s.pos < s.len && s.code[s.pos] == 0xA4) {
        mnem = "rep movsb"; s.pos++;
    }
    else if (op == 0xF3 && s.pos < s.len && s.code[s.pos] == 0xA6) {
        mnem = "repe cmpsb"; s.pos++;
    }
    else if (op == 0xE0) { mnem = "loopne"; imm_bytes = 1; if (parseImmediate(s, 1)) { i8 r = (i8)s.imm; addr_t t = s.address + (s.pos - offset) + r; snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)t); } }
    else if (op == 0xE1) { mnem = "loope";  imm_bytes = 1; if (parseImmediate(s, 1)) { i8 r = (i8)s.imm; addr_t t = s.address + (s.pos - offset) + r; snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)t); } }
    else if (op == 0xE2) { mnem = "loop";   imm_bytes = 1; if (parseImmediate(s, 1)) { i8 r = (i8)s.imm; addr_t t = s.address + (s.pos - offset) + r; snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)t); } }
    else if (op == 0xE3) { mnem = "jrcxz";  imm_bytes = 1; if (parseImmediate(s, 1)) { i8 r = (i8)s.imm; addr_t t = s.address + (s.pos - offset) + r; snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)t); } }

    // ===== Two-byte opcodes (0F xx) =====
    else if (s.opcode_len >= 2 && s.opcode[0] == 0x0F) {
        u8 op2 = s.opcode[1];
        if (op2 == 0x05) { mnem = "syscall"; ins.is_syscall = true; }
        else if (op2 == 0x07) { mnem = "sysret"; }
        else if (op2 == 0x0B) { mnem = "ud2"; }
        else if (op2 == 0x0D) { mnem = "prefetch"; needs_modrm = true; parseModRM(s, true); }
        else if (op2 == 0x0E) { mnem = "femms"; }
        else if (op2 == 0x10 || op2 == 0x11) {
            // movups/movupd (depends on F2/F3 prefix)
            mnem = s.has_F3 ? "movss" : (s.has_66 ? "movupd" : "movups");
            needs_modrm = true;
            if (parseModRM(s, true)) {
                auto m = formatModRM(s, 16);
                snprintf(ops, sizeof(ops), "%s, %s", reg_field(16), m.c_str());
                // Override reg name to xmm
                int full = ((s.modrm >> 3) & 7) | (s.rex_r << 3);
                snprintf(ops, sizeof(ops), "%s, %s", xmm(full), m.c_str());
            }
        }
        else if (op2 == 0x18) {
            // group 16: prefetch hints (rarely useful — treat as nop)
            mnem = "prefetch"; needs_modrm = true; parseModRM(s, true);
        }
        else if (op2 == 0x1F) {
            // multi-byte NOP (modrm form)
            mnem = "nop"; needs_modrm = true; parseModRM(s, true);
        }
        else if (op2 >= 0x80 && op2 <= 0x8F) {
            // Jcc rel32
            static const char* ccs[] = {"jo","jno","jb","jae","je","jne","jbe","ja",
                                         "js","jns","jp","jnp","jl","jge","jle","jg"};
            mnem = ccs[op2 - 0x80];
            imm_bytes = 4;
            if (parseImmediate(s, 4)) {
                i32 rel = (i32)s.imm;
                addr_t target = s.address + (s.pos - offset) + rel;
                snprintf(ops, sizeof(ops), "0x%llx", (unsigned long long)target);
                ins.branch_target = target;
                ins.has_branch_target = true;
                ins.is_jmp = true;
                ins.is_conditional = true;
            }
        }
        else if (op2 >= 0x40 && op2 <= 0x4F) {
            // CMOVcc
            static const char* ccs[] = {"cmovo","cmovno","cmovb","cmovae","cmove","cmovne","cmovbe","cmova",
                                         "cmovs","cmovns","cmovp","cmovnp","cmovl","cmovge","cmovle","cmovg"};
            mnem = ccs[op2 - 0x40];
            needs_modrm = true;
            if (parseModRM(s, true)) {
                auto m = formatModRM(s, operand_size);
                snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), m.c_str());
            }
        }
        else if (op2 >= 0x90 && op2 <= 0x9F) {
            // SETcc r/m8
            static const char* ccs[] = {"seto","setno","setb","setae","sete","setne","setbe","seta",
                                         "sets","setns","setp","setnp","setl","setge","setle","setg"};
            mnem = ccs[op2 - 0x90];
            needs_modrm = true;
            if (parseModRM(s, true)) {
                auto m = formatModRM(s, 1);
                snprintf(ops, sizeof(ops), "%s", m.c_str());
            }
        }
        else if (op2 == 0xA2) { mnem = "cpuid"; }
        else if (op2 == 0xA0) { mnem = "push fs"; }
        else if (op2 == 0xA1) { mnem = "pop fs"; }
        else if (op2 == 0xA8) { mnem = "push gs"; }
        else if (op2 == 0xA9) { mnem = "pop gs"; }
        else if (op2 == 0xB6) {
            // movzx r, r/m8
            mnem = "movzx"; needs_modrm = true;
            if (parseModRM(s, true)) {
                auto m = formatModRM(s, 1);
                snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), m.c_str());
            }
        }
        else if (op2 == 0xB7) {
            // movzx r, r/m16
            mnem = "movzx"; needs_modrm = true;
            if (parseModRM(s, true)) {
                auto m = formatModRM(s, 2);
                snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), m.c_str());
            }
        }
        else if (op2 == 0xBE) {
            // movsx r, r/m8
            mnem = "movsx"; needs_modrm = true;
            if (parseModRM(s, true)) {
                auto m = formatModRM(s, 1);
                snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), m.c_str());
            }
        }
        else if (op2 == 0xBF) {
            // movsx r, r/m16
            mnem = "movsx"; needs_modrm = true;
            if (parseModRM(s, true)) {
                auto m = formatModRM(s, 2);
                snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), m.c_str());
            }
        }
        else if (op2 == 0xAF) {
            // imul r, r/m
            mnem = "imul"; needs_modrm = true;
            if (parseModRM(s, true)) {
                auto m = formatModRM(s, operand_size);
                snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), m.c_str());
            }
        }
        else if (op2 == 0xB0 || op2 == 0xB1) {
            // cmpxchg
            mnem = "cmpxchg"; needs_modrm = true;
            if (parseModRM(s, true)) {
                int size = (op2 == 0xB0) ? 1 : operand_size;
                auto m = formatModRM(s, size);
                snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(size));
            }
        }
        else if (op2 == 0xC0) { mnem = "xadd"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, 1); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(1)); } }
        else if (op2 == 0xC1) { mnem = "xadd"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, operand_size); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(operand_size)); } }
        else if (op2 == 0xBA) {
            // group 8: bt/bts/btr/btc imm8
            needs_modrm = true;
            if (parseModRM(s, true)) {
                int sub = (s.modrm >> 3) & 7;
                static const char* g8[] = {"","","bt","bts","btr","btc"};
                mnem = (sub >= 2 && sub <= 5) ? g8[sub] : "???";
                auto m = formatModRM(s, operand_size);
                imm_bytes = 1;
                if (parseImmediate(s, 1))
                    snprintf(ops, sizeof(ops), "%s, 0x%llx", m.c_str(), (unsigned long long)s.imm);
            }
        }
        else if (op2 == 0xA3) { mnem = "bt"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, operand_size); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(operand_size)); } }
        else if (op2 == 0xAB) { mnem = "bts"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, operand_size); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(operand_size)); } }
        else if (op2 == 0xB3) { mnem = "btr"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, operand_size); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(operand_size)); } }
        else if (op2 == 0xBB) { mnem = "btc"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, operand_size); snprintf(ops, sizeof(ops), "%s, %s", m.c_str(), reg_field(operand_size)); } }
        else if (op2 == 0xBC) { mnem = "bsf"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, operand_size); snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), m.c_str()); } }
        else if (op2 == 0xBD) { mnem = "bsr"; needs_modrm = true; if (parseModRM(s, true)) { auto m = formatModRM(s, operand_size); snprintf(ops, sizeof(ops), "%s, %s", reg_field(operand_size), m.c_str()); } }
        else if (op2 == 0x31) { mnem = "rdtsc"; }
        else if (op2 == 0x32) { mnem = "rdmsr"; }
        else if (op2 == 0x33) { mnem = "rdpmc"; }
        else if (op2 == 0x34) { mnem = "sysenter"; }
        else if (op2 == 0x35) { mnem = "sysexit"; }
        else {
            // Unknown 0F xx — fall through to db
            snprintf((char*)ins.mnemonic, sizeof(ins.mnemonic), "(0F %02X)", op2);
        }
    }

    if (strcmp(mnem, "???") == 0) {
        // Unknown — emit as raw byte
        snprintf((char*)ins.mnemonic, sizeof(ins.mnemonic), "db");
        snprintf((char*)ins.operands, sizeof(ins.operands), "0x%02X", op);
    } else {
        strncpy((char*)ins.mnemonic, mnem, sizeof(ins.mnemonic) - 1);
        strncpy((char*)ins.operands, ops, sizeof(ins.operands) - 1);
    }

    // Compose text
    snprintf((char*)ins.text, sizeof(ins.text), "%s %s",
             ins.mnemonic, ins.operands);

    // Length = bytes consumed
    ins.length = s.pos - offset;

    // Save bytes
    if (ins.length > 0 && ins.length <= 15) {
        memcpy(ins.bytes, s.code + offset, ins.length);
    }

    return ins;
}

std::vector<Disassembler::Instruction> Disassembler::disassemble(addr_t address, const u8* code, size_t code_len, size_t n) {
    std::vector<Instruction> result;
    size_t offset = 0;
    addr_t cur = address;
    for (size_t i = 0; i < n && offset < code_len; i++) {
        Instruction ins = disassembleOne(cur, code, code_len, offset);
        if (ins.length == 0) {
            ins.length = 1;
            ins.bytes[0] = code[offset];
            snprintf((char*)ins.mnemonic, sizeof(ins.mnemonic), "db");
            snprintf((char*)ins.operands, sizeof(ins.operands), "0x%02X", code[offset]);
            snprintf((char*)ins.text, sizeof(ins.text), "db 0x%02X", code[offset]);
        }
        result.push_back(ins);
        offset += ins.length;
        cur += ins.length;
    }
    return result;
}

std::vector<Disassembler::Instruction> Disassembler::disassembleN(addr_t address, const std::vector<u8>& bytes, size_t n) {
    if (bytes.empty()) return {};
    return disassemble(address, bytes.data(), bytes.size(), n);
}

std::string Disassembler::format(const Instruction& ins, bool show_bytes) {
    char buf[256];
    if (show_bytes) {
        char hex[64] = {0};
        for (size_t i = 0; i < ins.length && i < 8; i++) {
            char b[4];
            snprintf(b, sizeof(b), "%02x ", ins.bytes[i]);
            strncat(hex, b, sizeof(hex) - strlen(hex) - 1);
        }
        snprintf(buf, sizeof(buf), "0x%016llx  %-24s %s %s",
                 (unsigned long long)ins.address, hex,
                 ins.mnemonic, ins.operands);
    } else {
        snprintf(buf, sizeof(buf), "0x%016llx  %s %s",
                 (unsigned long long)ins.address,
                 ins.mnemonic, ins.operands);
    }
    return buf;
}

std::string Disassembler::formatAnnotated(const Instruction& ins, const std::string& annotation, bool show_bytes) {
    std::string s = format(ins, show_bytes);
    if (!annotation.empty()) {
        s += "  ; " + annotation;
    }
    return s;
}

Disassembler::Category Disassembler::category(const Instruction& ins) const {
    if (ins.is_call) return Category::Branch;
    if (ins.is_jmp) return Category::Branch;
    if (ins.is_ret) return Category::Stack;
    if (ins.is_syscall) return Category::System;
    std::string m = ins.mnemonic;
    if (m == "push" || m == "pop" || m == "leave" || m == "enter") return Category::Stack;
    if (m == "cmp" || m == "test") return Category::Compare;
    if (m == "add" || m == "sub" || m == "inc" || m == "dec" || m == "mul" || m == "imul" || m == "div" || m == "idiv" || m == "neg" || m == "adc" || m == "sbb") return Category::Arithmetic;
    if (m == "and" || m == "or" || m == "xor" || m == "not" || m == "shl" || m == "shr" || m == "sar" || m == "rol" || m == "ror" || m == "rcl" || m == "rcr") return Category::Logic;
    if (m == "mov" || m == "movzx" || m == "movsx" || m == "lea" || m == "xchg" || m == "push" || m == "pop") return Category::Move;
    if (m == "syscall" || m == "sysenter" || m == "sysexit" || m == "sysret" || m == "int" || m == "iret" || m == "cpuid" || m == "rdtsc" || m == "rdmsr" || m == "wrmsr" || m == "hlt") return Category::System;
    return Category::Other;
}

} // namespace ndbg
