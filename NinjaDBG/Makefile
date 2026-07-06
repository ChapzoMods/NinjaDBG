# NinjaDBG v1.1.2 - Build System
# Open Source (Apache-2.0) - by Chapzoo
#
# v1.1.2: GUI removed. No more Cairo/Pango/X11 dependencies.
CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-but-set-variable \
            -Iinclude -Isrc
LDFLAGS  := -lstdc++ -lm -ldl -lpthread

SRC_DIR := src
OBJ_DIR := build/obj
BIN     := build/ninjadb

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

TARGET_SRC := scripts/target_test.cpp
TARGET_BIN := build/target_test

# Stealth preload payload. Auto-built by AntiDetect.cpp on first run, but
# we also build it here so `make install` and `make deb` can rely on it.
STEALTH_SRC := scripts/ninjastealth.c
STEALTH_LIB := build/libninjastealth.so

# Install layout (overridable for packaging).
DESTDIR     :=
PREFIX      := /usr/local
INSTALL_BIN := $(DESTDIR)$(PREFIX)/bin
INSTALL_LIB := $(DESTDIR)$(PREFIX)/lib
INSTALL_DAT := $(DESTDIR)$(PREFIX)/share/ninjadb

# Debian package metadata.
DEB_VERSION := 1.1.2
DEB_ARCH    := amd64
DEB_PKG     := build/ninjadb_v$(DEB_VERSION)_$(DEB_ARCH).deb

.PHONY: all clean target run cli install uninstall test deb

all: $(BIN) target $(STEALTH_LIB)

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	@echo "[build] NinjaDBG v1.1.2 ready -> $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Preload stealth library. Hand-maintained C source compiled as a shared
# object. See scripts/ninjastealth.c for the hook list.
$(STEALTH_LIB): $(STEALTH_SRC)
	@mkdir -p $(dir $@)
	gcc -shared -fPIC -O2 -Wall -o $@ $< -ldl
	@echo "[build] libninjastealth.so ready -> $@"

target: $(TARGET_BIN)

$(TARGET_BIN): $(TARGET_SRC)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 $(TARGET_SRC) -o $@

clean:
	rm -rf build/
	@echo "[clean] removed build/"

run: all
	./$(BIN)

cli: all
	./$(BIN) --no-eula-check

# ---- Install / uninstall into /usr/local ------------------------------------

# Copies the CLI binary, the preload library, and the stealth source into
# /usr/local. Run `sudo ldconfig` afterwards so the linker sees the new .so.
install: all
	install -d $(INSTALL_BIN)
	install -d $(INSTALL_LIB)
	install -d $(INSTALL_DAT)
	install -m 0755 $(BIN)          $(INSTALL_BIN)/ninjadb
	install -m 0755 $(STEALTH_LIB)  $(INSTALL_LIB)/libninjastealth.so
	install -m 0644 $(STEALTH_SRC)  $(INSTALL_DAT)/ninjastealth.c
	@echo "[install] NinjaDBG v1.1.2 installed into $(PREFIX)"
	@echo "[install] Run 'sudo ldconfig' to refresh the shared library cache."

# Removes everything `make install` created. Leaves ~/.config/ninjadb alone.
uninstall:
	rm -f $(INSTALL_BIN)/ninjadb
	rm -f $(INSTALL_LIB)/libninjastealth.so
	rm -rf $(INSTALL_DAT)
	@echo "[uninstall] NinjaDBG v1.1.2 removed from $(PREFIX)"

# ---- Tests ------------------------------------------------------------------

# Delegates to tests/Makefile. Exits non-zero if any test fails.
test: all
	$(MAKE) -C tests test

# ---- Debian package ---------------------------------------------------------

# Builds a .deb using dpkg-deb. The package installs into the same
# /usr/local paths as `make install`.
deb: all
	@rm -rf build/deb
	@mkdir -p build/deb/DEBIAN
	@mkdir -p build/deb$(PREFIX)/bin
	@mkdir -p build/deb$(PREFIX)/lib
	@mkdir -p build/deb$(PREFIX)/share/ninjadb
	install -m 0755 $(BIN)          build/deb$(PREFIX)/bin/ninjadb
	install -m 0755 $(STEALTH_LIB)  build/deb$(PREFIX)/lib/libninjastealth.so
	install -m 0644 $(STEALTH_SRC)  build/deb$(PREFIX)/share/ninjadb/ninjastealth.c
	@echo "Package: ninjadb"                    >  build/deb/DEBIAN/control
	@echo "Version: $(DEB_VERSION)"             >> build/deb/DEBIAN/control
	@echo "Architecture: $(DEB_ARCH)"           >> build/deb/DEBIAN/control
	@echo "Maintainer: Chapzoo <chapzo@github.com>" >> build/deb/DEBIAN/control
	@echo "Section: devel"                      >> build/deb/DEBIAN/control
	@echo "Priority: optional"                  >> build/deb/DEBIAN/control
	@echo "Depends: build-essential, pkg-config" >> build/deb/DEBIAN/control
	@echo "Description: NinjaDBG v1.1.2 - Stealth Debugger for Linux x86-64" >> build/deb/DEBIAN/control
	@echo " Open Source (Apache-2.0) - by Chapzoo" >> build/deb/DEBIAN/control
	@echo " CLI-only debugger with ptrace-based core, hardware breakpoints," >> build/deb/DEBIAN/control
	@echo " decompilation (RetDec/angr), pretty printers, scripting,"       >> build/deb/DEBIAN/control
	@echo " binary patching, and userland/kernel stealth."                  >> build/deb/DEBIAN/control
	@dpkg-deb --build --root-owner-group build/deb $(DEB_PKG)
	@echo "[deb] package ready: $(DEB_PKG)"
