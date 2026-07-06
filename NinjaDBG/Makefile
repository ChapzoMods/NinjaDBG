# NinjaDBG v1.1.1 - Build System
# Open Source (Apache-2.0) - by Chapzoo
#
# v1.1.1: GUI removed. No more Cairo/Pango/X11 dependencies.
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

.PHONY: all clean target run cli

all: $(BIN) target

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	@echo "[build] NinjaDBG v1.1.1 ready -> $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

target: $(TARGET_BIN)

$(TARGET_BIN): $(TARGET_SRC)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 $(TARGET_SRC) -o $@

clean:
	rm -rf build/

run: all
	./$(BIN)

cli: all
	./$(BIN) --no-eula-check
