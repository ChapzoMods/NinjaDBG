// NinjaDBG v1.2.0 - Basic CLI test runner
// Open Source (Apache-2.0) - by Chapzoo
//
// Verifies the CLI binary exists, prints help, runs in batch mode, that the
// banner says "v1.2.0" and "Apache-2.0", and that the decomp / pretty /
// script list commands all work. The test runner does not link against the
// NinjaDBG library -- it launches the real CLI binary as a subprocess via
// popen() and asserts on the captured stdout.
//
// Build: see tests/Makefile
// Run:   ./test_basic
//
// Override the path to the ninjadb binary with the NINJADB_BIN environment
// variable; otherwise the runner looks in ../build/ninjadb, ./build/ninjadb,
// and finally the PATH.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------

static std::string g_ninjadb_path;
static int g_passed = 0;
static int g_failed = 0;

// Return true if `path` is a regular file that exists.
static bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

// Run `cmd` via /bin/sh, capture combined stdout+stderr, return the exit
// status from pclose(). On failure to spawn, returns -1 and sets out to "".
static int run_capture(const std::string& cmd, std::string& out) {
    out.clear();
    std::string full = cmd + " 2>&1";
    FILE* p = ::popen(full.c_str(), "r");
    if (!p) return -1;
    char buf[4096];
    size_t n;
    while ((n = ::fread(buf, 1, sizeof(buf), p)) > 0) {
        out.append(buf, n);
    }
    int rc = ::pclose(p);
    return rc;
}

// Print PASS / FAIL and update counters. `detail` is appended on FAIL only.
static void check(bool cond, const std::string& name,
                  const std::string& detail = "") {
    if (cond) {
        ::printf("PASS  %s\n", name.c_str());
        g_passed++;
    } else {
        if (detail.empty()) {
            ::printf("FAIL  %s\n", name.c_str());
        } else {
            ::printf("FAIL  %s  --  %s\n", name.c_str(), detail.c_str());
        }
        g_failed++;
    }
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Resolve the path to the ninjadb binary. Honor $NINJADB_BIN if set.
static std::string resolve_ninjadb() {
    const char* env = ::getenv("NINJADB_BIN");
    if (env && *env && file_exists(env)) return env;

    const char* candidates[] = {
        "../build/ninjadb",     // tests/ -> repo root
        "./build/ninjadb",      // repo root
        "../../build/ninjadb",  // deeper subdirs
        "ninjadb",              // PATH
        nullptr
    };
    for (int i = 0; candidates[i]; i++) {
        std::string p = candidates[i];
        if (file_exists(p)) return p;
    }
    // Fall back to "ninjadb" so the failure message is informative.
    return "ninjadb";
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

static void test_binary_exists() {
    check(file_exists(g_ninjadb_path),
          "CLI binary exists",
          "path=" + g_ninjadb_path);
}

static void test_help_flag() {
    std::string out;
    int rc = run_capture("'" + g_ninjadb_path + "' --help", out);
    char detail[64];
    ::snprintf(detail, sizeof(detail), "rc=%d", rc);
    check(rc == 0, "--help exits 0", detail);
    check(contains(out, "NinjaDBG"),   "--help output mentions NinjaDBG");
    check(contains(out, "v1.2.0"),     "--help output mentions v1.2.0");
    check(contains(out, "Apache-2.0"), "--help output mentions Apache-2.0");
    check(contains(out, "Usage:"),     "--help output has a Usage block");
}

static void test_batch_mode() {
    // The core smoke test: feed the REPL a couple of commands and exit.
    std::string out;
    int rc = run_capture(
        "'" + g_ninjadb_path + "' --no-eula-check -c \"help; quit\"", out);
    char detail[64];
    ::snprintf(detail, sizeof(detail), "rc=%d", rc);
    check(rc == 0, "batch mode (--no-eula-check -c) exits 0", detail);
    check(contains(out, "NinjaDBG v1.2.0 CLI commands"),
          "batch 'help' prints the command list");
    check(!contains(out, "Unknown command"),
          "batch mode reports no unknown-command errors");
    check(!contains(out, "EULA declined"),
          "batch mode does not bail out on the EULA");
}

static void test_banner() {
    std::string out;
    run_capture("'" + g_ninjadb_path + "' --no-eula-check -c \"quit\"", out);
    check(contains(out, "v1.2.0"),
          "banner contains the version string v1.2.0");
    check(contains(out, "Apache-2.0"),
          "banner contains the Apache-2.0 license tag");
    check(contains(out, "Stealth Debugger"),
          "banner contains 'Stealth Debugger'");
    check(contains(out, "Headless CLI mode"),
          "banner mentions Headless CLI mode");
}

static void test_decomp_list() {
    std::string out;
    int rc = run_capture(
        "'" + g_ninjadb_path + "' --no-eula-check -c \"decomp list\"", out);
    check(rc == 0, "decomp list exits 0");
    check(contains(out, "Decompiler backend status"),
          "decomp list prints the status header");
    check(contains(out, "retdec-native"),
          "decomp list mentions the retdec-native backend");
    check(contains(out, "retdec-subprocess"),
          "decomp list mentions the retdec-subprocess backend");
    check(contains(out, "angr"),
          "decomp list mentions the angr backend");
}

static void test_pretty_list() {
    std::string out;
    int rc = run_capture(
        "'" + g_ninjadb_path + "' --no-eula-check -c \"pretty list\"", out);
    check(rc == 0, "pretty list exits 0");
    check(contains(out, "Pretty printers available"),
          "pretty list prints the header");
    // The five supported languages should all appear.
    check(contains(out, "cpp"),  "pretty list mentions cpp");
    check(contains(out, "rust"), "pretty list mentions rust");
    check(contains(out, "go"),   "pretty list mentions go");
    check(contains(out, "python"), "pretty list mentions python");
}

static void test_script_list() {
    std::string out;
    int rc = run_capture(
        "'" + g_ninjadb_path + "' --no-eula-check -c \"script list\"", out);
    check(rc == 0, "script list exits 0");
    check(contains(out, "Scripting backends"),
          "script list prints the header");
    check(contains(out, "Lua"),    "script list mentions Lua");
    check(contains(out, "Python"), "script list mentions Python");
    check(contains(out, "script run"),
          "script list documents the run subcommand");
}

// ---------------------------------------------------------------------------

int main(int /*argc*/, char** /*argv*/) {
    g_ninjadb_path = resolve_ninjadb();
    ::printf("NinjaDBG v1.2.0 basic test runner\n");
    ::printf("Using ninjadb: %s\n\n", g_ninjadb_path.c_str());

    test_binary_exists();
    test_help_flag();
    test_batch_mode();
    test_banner();
    test_decomp_list();
    test_pretty_list();
    test_script_list();

    ::printf("\n========================================\n");
    ::printf("PASSED: %d    FAILED: %d\n", g_passed, g_failed);
    ::printf("========================================\n");

    return (g_failed == 0) ? 0 : 1;
}
