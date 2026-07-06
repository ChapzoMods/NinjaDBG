// NinjaDBG v1.1.2 - PrettyPrinter implementation
// Open Source (Apache-2.0) - by Chapzoo
#include "PrettyPrinter.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>

namespace ndbg {

PrettyPrinter::PrettyPrinter() {}
PrettyPrinter::~PrettyPrinter() {}

std::string PrettyPrinter::languageName(Language l) {
    switch (l) {
        case Language::NoLanguage:    return "none";
        case Language::C:       return "c";
        case Language::Cpp:     return "cpp";
        case Language::Rust:    return "rust";
        case Language::Go:      return "go";
        case Language::Python:  return "python";
    }
    return "?";
}

std::vector<PrettyPrinter::Language> PrettyPrinter::allLanguages() {
    return {Language::C, Language::Cpp, Language::Rust, Language::Go, Language::Python};
}

// ===== Helpers =====

u64 PrettyPrinter::readU64(DebuggerCore& dbg, addr_t addr) {
    u64 v = 0;
    if (!dbg.readMemory(addr, &v, 8)) return 0;
    return v;
}

std::string PrettyPrinter::readCString(DebuggerCore& dbg, addr_t addr, size_t max_len) {
    std::string result;
    result.reserve(256);
    // Read in 64-byte chunks to be efficient
    u8 buf[64];
    size_t read = 0;
    while (read < max_len) {
        size_t chunk = std::min((size_t)sizeof(buf), max_len - read);
        if (!dbg.readMemory(addr + read, buf, chunk)) break;
        for (size_t i = 0; i < chunk; i++) {
            if (buf[i] == 0) return result;
            result.push_back((char)buf[i]);
            read++;
            if (read >= max_len) break;
        }
    }
    return result;
}

std::string PrettyPrinter::readBytesAsString(DebuggerCore& dbg, addr_t addr, size_t len) {
    std::vector<u8> bytes = dbg.readMemoryVec(addr, len);
    if (bytes.empty()) return "";
    return std::string((const char*)bytes.data(), bytes.size());
}

std::string PrettyPrinter::escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if ((unsigned char)c < 32 || (unsigned char)c >= 127) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\x%02x", (unsigned char)c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

// ===== String printers =====

std::string PrettyPrinter::printCString(DebuggerCore& dbg, addr_t addr, size_t max_len) {
    std::string s = readCString(dbg, addr, max_len);
    std::ostringstream out;
    out << "(char*) 0x" << std::hex << addr << " = \"" << escape(s) << "\"";
    out << "  (len=" << std::dec << s.size() << ")";
    return out.str();
}

std::string PrettyPrinter::printCppString(DebuggerCore& dbg, addr_t addr) {
    // libstdc++ std::string layout (64-bit):
    //   offset 0:  char* _M_p           (data pointer)
    //   offset 8:  size_t _M_length     (string length)
    //   offset 16: char[16] _M_local_buf (SSO buffer) / size_t _M_capacity
    //
    // If _M_p == addr + 16, the string is in SSO mode (data is in the local buffer).
    // Otherwise, _M_p points to heap-allocated storage.
    u64 data_ptr = readU64(dbg, addr);
    u64 length   = readU64(dbg, addr + 8);

    if (length > (1ULL << 30)) {  // sanity: >1GB is almost certainly garbage
        return "(std::string) 0x" + hex(addr) + " = <invalid: length too large: " +
               std::to_string(length) + ">";
    }

    addr_t sso_base = addr + 16;
    addr_t actual_data = (addr_t)data_ptr;
    if (data_ptr == sso_base) {
        // SSO mode — data is in the inline buffer
        actual_data = sso_base;
    }

    std::string s = readBytesAsString(dbg, actual_data, (size_t)std::min((u64)4096, length));
    std::ostringstream out;
    out << "(std::string) 0x" << std::hex << addr << " = \"" << escape(s) << "\"";
    out << "  (len=" << std::dec << length
        << ", " << (data_ptr == sso_base ? "SSO" : "heap")
        << ", data=0x" << std::hex << actual_data << ")";
    return out.str();
}

std::string PrettyPrinter::printRustString(DebuggerCore& dbg, addr_t addr) {
    // Rust String = Vec<u8> = { ptr: NonNull<u8>, cap: usize, len: usize }
    // Total 24 bytes on 64-bit.
    u64 data_ptr = readU64(dbg, addr);
    u64 capacity = readU64(dbg, addr + 8);
    u64 length   = readU64(dbg, addr + 16);

    if (length > (1ULL << 30)) {
        return "(String) 0x" + hex(addr) + " = <invalid: length too large: " +
               std::to_string(length) + ">";
    }
    if (length > capacity) {
        return "(String) 0x" + hex(addr) + " = <invalid: length > capacity>";
    }

    std::string s = readBytesAsString(dbg, (addr_t)data_ptr, (size_t)std::min((u64)4096, length));
    std::ostringstream out;
    out << "(String) 0x" << std::hex << addr << " = \"" << escape(s) << "\"";
    out << "  (len=" << std::dec << length
        << ", cap=" << capacity
        << ", ptr=0x" << std::hex << data_ptr << ")";
    return out.str();
}

std::string PrettyPrinter::printGoString(DebuggerCore& dbg, addr_t addr) {
    // Go string header: { Data *byte; Len int; }  (16 bytes on 64-bit)
    u64 data_ptr = readU64(dbg, addr);
    u64 length   = readU64(dbg, addr + 8);

    if (length > (1ULL << 30)) {
        return "(go.string) 0x" + hex(addr) + " = <invalid: length too large: " +
               std::to_string(length) + ">";
    }

    std::string s = readBytesAsString(dbg, (addr_t)data_ptr, (size_t)std::min((u64)4096, length));
    std::ostringstream out;
    out << "(go.string) 0x" << std::hex << addr << " = \"" << escape(s) << "\"";
    out << "  (len=" << std::dec << length
        << ", data=0x" << std::hex << data_ptr << ")";
    return out.str();
}

std::string PrettyPrinter::printPyString(DebuggerCore& dbg, addr_t addr) {
    // CPython 3.12+ PyUnicodeObject is complex. We do a simplified read:
    //   PyASCIIObject:  refcnt: u64, type: ptr, length: u64, hash: u64, state: u32, ...
    //   If ASCII flag is set (state & 0x40), data follows immediately after the
    //   compact header. Otherwise it's a PyCompactUnicodeObject + UCS1/2/4 data.
    //
    // This is a best-effort reader that works for common ASCII strings.
    u64 length = readU64(dbg, addr + 16);  // PyASCIIObject.length at offset 16
    u32 state  = 0;
    dbg.readMemory(addr + 32, &state, 4);  // state at offset 32

    bool is_ascii = (state & 0x40) != 0;
    bool is_compact = (state & 0x01) != 0;

    if (length > (1ULL << 30)) {
        return "(PyUnicodeObject) 0x" + hex(addr) + " = <invalid: length too large: " +
               std::to_string(length) + ">";
    }

    if (!is_compact) {
        return "(PyUnicodeObject) 0x" + hex(addr) + " = <non-compact: legacy layout not supported>";
    }

    // For compact ASCII: data starts at offset 48 (sizeof PyASCIIObject)
    // For compact non-ASCII: data starts at offset 56 (sizeof PyCompactUnicodeObject)
    addr_t data_off = is_ascii ? 48 : 56;
    size_t read_len = (size_t)std::min((u64)4096, length);
    if (!is_ascii) read_len *= 2;  // UCS2
    if (!is_ascii && (state & 0x80)) read_len *= 2;  // UCS4

    std::string s = readBytesAsString(dbg, addr + data_off, read_len);
    std::ostringstream out;
    out << "(PyUnicodeObject) 0x" << std::hex << addr << " = \"" << escape(s) << "\"";
    out << "  (len=" << std::dec << length
        << ", ascii=" << (is_ascii ? "true" : "false")
        << ", state=0x" << std::hex << state << ")";
    return out.str();
}

// ===== Struct printer =====

std::string PrettyPrinter::printStruct(DebuggerCore& dbg, addr_t addr, const std::string& descriptor) {
    std::ostringstream out;
    out << "struct at 0x" << std::hex << addr << ":\n";

    // Parse descriptor: comma-separated type codes
    std::vector<std::string> fields;
    std::string cur;
    for (char c : descriptor) {
        if (c == ',' || c == ';' || c == ' ') {
            if (!cur.empty()) { fields.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) fields.push_back(cur);

    u64 offset = 0;
    for (size_t i = 0; i < fields.size(); i++) {
        const std::string& f = fields[i];
        std::string lower_f = f;
        for (auto& c : lower_f) c = tolower(c);

        out << "  +0x" << std::hex << offset << "  ";

        size_t size = 0;
        if (lower_f == "i8" || lower_f == "u8")   size = 1;
        else if (lower_f == "i16" || lower_f == "u16") size = 2;
        else if (lower_f == "i32" || lower_f == "u32" || lower_f == "f32") size = 4;
        else if (lower_f == "i64" || lower_f == "u64" || lower_f == "f64" || lower_f == "ptr") size = 8;
        else if (lower_f == "str") size = 8;  // pointer to cstring
        else if (lower_f.substr(0, 3) == "hex") {
            size = (size_t)strtoull(lower_f.c_str() + 3, nullptr, 10);
            if (size == 0) size = 16;
        } else {
            out << f << " = <unknown type>\n";
            continue;
        }

        // Align offset to size (natural alignment)
        if (size > 1) {
            offset = (offset + size - 1) & ~(size - 1);
        }

        if (lower_f == "str") {
            u64 ptr = readU64(dbg, addr + offset);
            std::string s = (ptr != 0) ? readCString(dbg, (addr_t)ptr, 256) : "<null>";
            out << "ptr  = 0x" << std::hex << ptr << " -> \"" << escape(s) << "\"\n";
        } else if (lower_f == "ptr") {
            u64 v = readU64(dbg, addr + offset);
            out << "ptr  = 0x" << std::hex << v << "\n";
        } else if (lower_f[0] == 'i') {
            // signed
            u64 raw = 0;
            dbg.readMemory(addr + offset, &raw, size);
            if (size == 1) out << "i8   = " << std::dec << (int)*(int8_t*)&raw;
            else if (size == 2) out << "i16  = " << std::dec << *(int16_t*)&raw;
            else if (size == 4) out << "i32  = " << std::dec << *(int32_t*)&raw;
            else if (size == 8) out << "i64  = " << std::dec << *(int64_t*)&raw;
            out << "  (0x" << std::hex << raw << ")\n";
        } else if (lower_f[0] == 'u') {
            u64 raw = 0;
            dbg.readMemory(addr + offset, &raw, size);
            out << lower_f << " = " << std::dec << raw << "  (0x" << std::hex << raw << ")\n";
        } else if (lower_f[0] == 'f') {
            u64 raw = 0;
            dbg.readMemory(addr + offset, &raw, size);
            if (size == 4) out << "f32  = " << *(float*)&raw << "\n";
            else out << "f64  = " << *(double*)&raw << "\n";
        } else if (lower_f.substr(0, 3) == "hex") {
            std::vector<u8> bytes = dbg.readMemoryVec(addr + offset, size);
            out << "hex  = ";
            for (u8 b : bytes) {
                char buf[4]; snprintf(buf, sizeof(buf), "%02x ", b);
                out << buf;
            }
            out << "\n";
        }

        offset += size;
    }
    return out.str();
}

// ===== Auto-detect =====

std::string PrettyPrinter::autoPrint(DebuggerCore& dbg, addr_t addr) {
    switch (lang_) {
        case Language::C:       return printCString(dbg, addr);
        case Language::Cpp:     return printCppString(dbg, addr);
        case Language::Rust:    return printRustString(dbg, addr);
        case Language::Go:      return printGoString(dbg, addr);
        case Language::Python:  return printPyString(dbg, addr);
        default:
            // Try C string as a sensible default
            return printCString(dbg, addr);
    }
}

std::string PrettyPrinter::apiDocs() {
    return R"DOCS(
NinjaDBG v1.1.2 Pretty Printers API
====================================

Pretty printers interpret raw memory bytes as language-specific data
structures. Set the active language with `pretty set <lang>`.

Supported languages
-------------------

  c       - C-style NUL-terminated strings, structs
  cpp     - C++ std::string (libstdc++ SSO), structs
  rust    - Rust String, &str, structs
  go      - Go string, structs
  python  - CPython PyUnicodeObject

CLI commands
------------

  pretty set <lang>              Set the active language (c|cpp|rust|go|python|none)
  pretty cstring <addr>          Print a C-style NUL-terminated string
  pretty cpp_string <addr>       Print a std::string (libstdc++ SSO aware)
  pretty rust_string <addr>      Print a Rust String
  pretty go_string <addr>        Print a Go string
  pretty py_string <addr>        Print a CPython str
  pretty struct <addr> <desc>    Parse memory as a struct using a type descriptor
  pretty auto <addr>             Auto-print using the active language
  pretty list                    Show available printers
  pretty api                     Show this help

Struct descriptor
-----------------

Comma-separated type codes. Natural alignment is applied.

  i8 i16 i32 i64   - signed integers
  u8 u16 u32 u64   - unsigned integers
  f32 f64          - floats (IEEE 754)
  ptr              - pointer (u64, printed as hex)
  str              - pointer to C-string (dereferenced and printed)
  hex<N>           - N raw bytes as hex (e.g. hex16)

Example:
  pretty struct 0x7ffe1234 i32,str,ptr,u64

  Output:
    struct at 0x7ffe1234:
      +0x0   i32  = 42  (0x2a)
      +0x8   ptr  = 0x401234 -> "hello world"
      +0x10  ptr  = 0x7ffe5678
      +0x18  u64  = 139832  (0x22238)

Examples
--------

  # C string
  (ninjadb) pretty cstring 0x401234
  (char*) 0x401234 = "Hello, world!"  (len=13)

  # C++ std::string (auto-detects SSO vs heap)
  (ninjadb) pretty cpp_string 0x7ffe1000
  (std::string) 0x7ffe1000 = "Hello, world!"  (len=13, SSO, data=0x7ffe1010)

  # Rust String
  (ninjadb) pretty rust_string 0x7ffe2000
  (String) 0x7ffe2000 = "Hello, world!"  (len=13, cap=13, ptr=0x55aabbccdd00)

  # Go string
  (ninjadb) pretty go_string 0x7ffe3000
  (go.string) 0x7ffe3000 = "Hello, world!"  (len=13, data=0x401234)

  # Python str (CPython 3.12+)
  (ninjadb) pretty py_string 0x7ffe4000
  (PyUnicodeObject) 0x7ffe4000 = "Hello, world!"  (len=13, ascii=true, state=0x40)
)DOCS";
}

} // namespace ndbg
