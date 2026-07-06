# NinjaDBG v1.0.3 - Build System
# Closed Source - Free - by Chapzoo
CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-but-set-variable \
	    -Iinclude -Isrc \
	    $(shell pkg-config --cflags cairo pangocairo x11)
LDFLAGS  := $(shell pkg-config --libs cairo pangocairo x11) \
	    -lXext -lstdc++ -lm -ldl -lpthread

SRC_DIR := src
OBJ_DIR := build/obj
BIN     := build/ninjadb

SRCS := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/UI/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

TARGET_SRC := scripts/target_test.cpp
TARGET_BIN := build/target_test

.PHONY: all clean target run screenshot cli

all: $(BIN) target

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	@echo "[build] NinjaDBG v1.0.3 ready -> $@"

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
	./$(BIN) --cli

screenshot: all target
	bash scripts/screenshot.sh
