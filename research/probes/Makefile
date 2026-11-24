CXX ?= clang++
CXXFLAGS = -std=c++17 -ObjC++ -O3 -Wall -Wextra -fobjc-arc
FRAMEWORKS = -framework Metal -framework Foundation -framework QuartzCore

BUILD_DIR = ../build
BIN = $(BUILD_DIR)/m1_silicon_probe
JSON_OUT = $(BUILD_DIR)/m1_probe_results.json

.PHONY: all run clean

all: $(BIN)

$(BIN): m1_silicon_probe.mm Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(FRAMEWORKS)

run: $(BIN)
	$(BIN) --json $(JSON_OUT)

clean:
	rm -rf $(BUILD_DIR)
