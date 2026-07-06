// Target test program for NinjaDBG demonstration
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <ctime>
#include <cstring>

static int compute_checksum(const char* data, int len) {
    int sum = 0;
    for (int i = 0; i < len; i++) {
        sum = (sum * 31 + data[i]) & 0x7fffffff;
    }
    return sum;
}

static void anti_debug_check() {
    // Classic TracerPid check via /proc/self/status
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            int pid = atoi(line + 10);
            if (pid != 0) {
                printf("[target] Anti-debug: TracerPid=%d detected!\n", pid);
                fclose(f);
                return;
            }
        }
    }
    fclose(f);
    printf("[target] No debugger detected (stealth OK)\n");
}

int main() {
    printf("[target] NinjaDBG target sample v1.0 starting, pid=%d\n", getpid());

    const char* secret = "NINJA_SECRET_FLAG{4nt1_d3bug_byp4ss3d}";
    int counter = 0;

    while (counter < 1000000) {
        anti_debug_check();

        int cs = compute_checksum(secret, strlen(secret));
        counter += cs % 7;

        printf("[target] tick=%d counter=%d checksum=0x%08x\n",
               counter, counter, cs);
        usleep(800000);
    }

    printf("[target] Done\n");
    return 0;
}
