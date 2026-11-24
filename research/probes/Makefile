CXX ?= clang++
CXXFLAGS = -std=c++17 -ObjC++ -O3 -Wall -Wextra -fobjc-arc
FRAMEWORKS = -framework Metal -framework Foundation -framework QuartzCore

BUILD_DIR = ../build
BIN_SILICON = $(BUILD_DIR)/m1_silicon_probe
JSON_SILICON = $(BUILD_DIR)/m1_probe_results.json

BIN_GPUI = $(BUILD_DIR)/gpui_component_bench
JSON_GPUI = $(BUILD_DIR)/gpui_bench_results.json

.PHONY: all run run-gpui run-all clean

all: $(BIN_SILICON) $(BIN_GPUI)

$(BIN_SILICON): m1_silicon_probe.mm Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(FRAMEWORKS)

$(BIN_GPUI): gpui_component_bench.mm Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(FRAMEWORKS)

run: $(BIN_SILICON)
	$(BIN_SILICON) --json $(JSON_SILICON)

run-gpui: $(BIN_GPUI)
	$(BIN_GPUI) --json $(JSON_GPUI)

run-all: run run-gpui

clean:
	rm -rf $(BUILD_DIR)
