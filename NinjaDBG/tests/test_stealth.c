/*
 * test_stealth.c - NinjaDBG v1.1.3 stealth baseline self-test
 * Open Source (Apache-2.0) - by Chapzoo
 *
 * Standalone C program (no external libraries, no NinjaDBG headers) that
 * verifies a process NOT running under a tracer sees:
 *
 *   - TracerPid: 0 in /proc/self/status
 *   - a readable /proc/self/comm
 *
 * When this same binary is run under NinjaDBG with libninjastealth.so
 * preloaded, libninjastealth rewrites the TracerPid line so anti-debug
 * code cannot see the tracer. This test confirms the baseline behavior
 * (no tracer present) is sane -- it is the control case for the stealth
 * subsystem.
 *
 * Build:  gcc -o test_stealth test_stealth.c
 * Run:    ./test_stealth
 *
 * Exits 0 on success, 1 on any failure.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

static int fail(const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void) {
    /* ----------------------------------------------------------------
     * 1) Open /proc/self/status and verify TracerPid is 0.
     *    When the process is not being ptraced, TracerPid is 0. Under
     *    NinjaDBG (without the stealth preload), TracerPid would be the
     *    PID of the ninjadb process; with libninjastealth.so preloaded,
     *    the hook rewrites it back to 0.
     * ---------------------------------------------------------------- */
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) {
        return fail("could not open /proc/self/status");
    }

    char line[256];
    int tracer_pid = -1;
    int found_tracer = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            tracer_pid = atoi(line + 10);
            found_tracer = 1;
            break;
        }
    }
    fclose(f);

    if (!found_tracer) {
        return fail("TracerPid line missing from /proc/self/status");
    }
    if (tracer_pid != 0) {
        fprintf(stderr,
                "FAIL: TracerPid=%d (expected 0 when not under a tracer)\n",
                tracer_pid);
        return 1;
    }
    printf("PASS: /proc/self/status TracerPid is 0 (no tracer attached)\n");

    /* ----------------------------------------------------------------
     * 2) Open /proc/self/comm and print it. libninjastealth rewrites
     *    this to "kworker/u:1" when preloaded; without the preload we
     *    see the real binary name.
     * ---------------------------------------------------------------- */
    f = fopen("/proc/self/comm", "r");
    if (!f) {
        return fail("could not open /proc/self/comm");
    }

    char comm[256] = {0};
    if (!fgets(comm, sizeof(comm), f)) {
        fclose(f);
        return fail("could not read /proc/self/comm");
    }
    fclose(f);

    /* Strip trailing newline / carriage return. */
    size_t len = strlen(comm);
    while (len > 0 && (comm[len - 1] == '\n' || comm[len - 1] == '\r')) {
        comm[--len] = '\0';
    }
    if (len == 0) {
        return fail("/proc/self/comm is empty");
    }
    printf("PASS: /proc/self/comm = '%s'\n", comm);

    /* ----------------------------------------------------------------
     * 3) Sanity: confirm the test binary itself executed end-to-end.
     *    This catches gross environment failures (broken libc, no /proc
     *    mounted, etc.) that would otherwise masquerade as a stealth bug.
     * ---------------------------------------------------------------- */
    printf("PASS: test_stealth binary executed successfully (pid=%d)\n",
           (int)getpid());

    printf("\nAll stealth baseline checks passed.\n");
    return 0;
}
