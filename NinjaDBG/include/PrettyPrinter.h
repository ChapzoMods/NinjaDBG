// NinjaDBG v1.1.0 - Pretty Printers by Language
// Open Source (MIT) - by Chapzoo
//
// Provides language-aware pretty printing of in-memory values. When
// debugging a program written in C, C++, Rust, Go, or Python, the
// user can ask NinjaDBG to interpret a chunk of memory as a struct,
// string, vector, etc. according to the target language's ABI.
//
// Supported languages and what they can pretty-print:
//
//   C       - basic types (int, long, char*, struct layout by field desc)
//   C++     - std::string (libstdc++ and libc++), std::vector, std::map
//   Rust    - String, &str, Vec<T>, Option<T>, Box<T>
//   Go      - string, []byte, slices, maps, interfaces
//   Python  - PyUnicodeObject, PyBytesObject, PyListObject, PyDictObject
//
// Usage from CLI:
//   pretty set <lang>         Set the active language
//   pretty cstring <addr>     Print a C-style NUL-terminated string
//   pretty cpp_string <addr>  Print a std::string at addr
//   pretty rust_string <addr> Print a Rust String at addr
//   pretty go_string <addr>   Print a Go string at addr
//   pretty py_string <addr>   Print a CPython str at addr
//   pretty struct <addr> <desc>  Parse memory as a struct (e.g. "i32,ptr,i64")
//   pretty list               Show available printers
//   pretty api                Show this documentation
//
// The struct descriptor uses type codes:
//   i8 i16 i32 i64 - signed integers
//   u8 u16 u32 u64 - unsigned integers
//   f32 f64        - floats
//   ptr            - pointer (u64)
//   str            - C-string (ptr + dereference)
//   hex<N>         - N bytes as hex
#pragma once

#include "Types.h"
#include "DebuggerCore.h"
#include <string>
#include <vector>

namespace ndbg {

class PrettyPrinter {
public:
    enum class Language {
        NoLanguage,
        C,
        Cpp,
        Rust,
        Go,
        Python
    };

    PrettyPrinter();
    ~PrettyPrinter();

    // Set the active language (affects which auto-detection is used)
    void setLanguage(Language l) { lang_ = l; }
    Language currentLanguage() const { return lang_; }
    static std::string languageName(Language l);
    static std::vector<Language> allLanguages();

    // ---- String printers ----
    // Each reads from the live process via DebuggerCore.

    // C-style NUL-terminated string at addr
    std::string printCString(DebuggerCore& dbg, addr_t addr, size_t max_len = 4096);

    // C++ std::string at addr (libstdc++ SSO layout)
    // Layout (libstdc++, 64-bit):
    //   struct basic_string {
    //     char* _M_dataplus._M_p;   // offset 0
    //     size_t _M_string_length;  // offset 8
    //     union { char _M_local_buf[16]; size_t _M_allocated_capacity; };  // offset 16
    //   };
    // If _M_p points to _M_local_buf (i.e. addr+16), it's SSO.
    std::string printCppString(DebuggerCore& dbg, addr_t addr);

    // Rust String at addr
    // Layout:
    //   struct String { Vec<u8> };
    //   struct Vec<T> { ptr: NonNull<T>, cap: usize, len: usize };
    // So String is: { data_ptr: u64, cap: u64, len: u64 } (24 bytes)
    std::string printRustString(DebuggerCore& dbg, addr_t addr);

    // Go string at addr
    // Layout:
    //   type stringHeader struct { Data *byte; Len int; }
    // (16 bytes on 64-bit)
    std::string printGoString(DebuggerCore& dbg, addr_t addr);

    // CPython PyUnicodeObject (Python 3.12+ compact str)
    // This is a simplified reader — full PyUnicode parsing is complex.
    std::string printPyString(DebuggerCore& dbg, addr_t addr);

    // ---- Generic struct printer ----
    // Descriptor syntax: comma-separated type codes, e.g. "i32,ptr,i64,str"
    // Returns a formatted multi-line string.
    std::string printStruct(DebuggerCore& dbg, addr_t addr, const std::string& descriptor);

    // ---- Auto-detect ----
    // Try to guess what's at addr based on the current language.
    std::string autoPrint(DebuggerCore& dbg, addr_t addr);

    // ---- API documentation ----
    static std::string apiDocs();

private:
    Language lang_ = Language::NoLanguage;

    // Read a NUL-terminated string of up to max_len bytes from the target
    std::string readCString(DebuggerCore& dbg, addr_t addr, size_t max_len);
    // Read N bytes as a UTF-8 string (may contain non-printable chars as \xNN)
    std::string readBytesAsString(DebuggerCore& dbg, addr_t addr, size_t len);
    // Read a u64 from the target
    u64 readU64(DebuggerCore& dbg, addr_t addr);
    // Escape a string for display
    static std::string escape(const std::string& s);
};

} // namespace ndbg
