# NinjaDBG v1.2.0 - Tests Makefile
# Open Source (Apache-2.0) - by Chapzoo
#
# Builds and runs the NinjaDBG test suite:
#
#   test_basic   - C++ test runner that launches ../build/ninjadb as a
#                  subprocess via popen() and asserts on its output.
#                  Does not link against the NinjaDBG library.
#
#   test_stealth - Standalone C binary that probes /proc/self/status and
#                  /proc/self/comm to confirm the baseline (no tracer)
#                  behavior that libninjastealth.so hooks rely on.
#
# Usage:
#   make           - build both test binaries
#   make test      - build and run both
#   make clean     - remove built binaries

CXX      := g++
CC       := gcc
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS   := -std=c11 -O2 -Wall -Wextra

# Path to the freshly built ninjadb binary (one directory up).
NINJADB_BIN := ../build/ninjadb

TEST_BASIC_BIN  := test_basic
TEST_STEALTH_BIN := test_stealth

.PHONY: all test clean check-binary

all: $(TEST_BASIC_BIN) $(TEST_STEALTH_BIN)

# Refuse to build / run if the parent project has not been built yet.
check-binary:
	@if [ ! -x "$(NINJADB_BIN)" ]; then \
		echo "[tests] ERROR: $(NINJADB_BIN) not found or not executable."; \
		echo "[tests]        Run 'make' in the project root first."; \
		exit 1; \
	fi

# test_basic does not link against the NinjaDBG library -- it spawns the
# real CLI binary as a subprocess, so the only build input is test_basic.cpp.
$(TEST_BASIC_BIN): test_basic.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<
	@echo "[tests] built $@"

# test_stealth is a single-file C program with no external deps.
$(TEST_STEALTH_BIN): test_stealth.c
	$(CC) $(CFLAGS) -o $@ $<
	@echo "[tests] built $@"

# Build, then run both tests. The target exits non-zero if either test
# fails, so it is safe to use from CI.
test: check-binary all
	@echo ""
	@echo "==== Running test_basic ===="
	./$(TEST_BASIC_BIN)
	@basic_rc=$$?; \
	echo ""; \
	echo "==== Running test_stealth ===="; \
	./$(TEST_STEALTH_BIN); \
	stealth_rc=$$?; \
	echo ""; \
	if [ $$basic_rc -ne 0 ]; then \
		echo "[tests] test_basic FAILED (rc=$$basic_rc)"; \
		exit 1; \
	fi; \
	if [ $$stealth_rc -ne 0 ]; then \
		echo "[tests] test_stealth FAILED (rc=$$stealth_rc)"; \
		exit 1; \
	fi; \
	echo "[tests] ALL TESTS PASSED"

clean:
	rm -f $(TEST_BASIC_BIN) $(TEST_STEALTH_BIN)
	@echo "[tests] cleaned"
