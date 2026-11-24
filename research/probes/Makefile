CXX ?= clang++
CXXFLAGS = -std=c++17 -ObjC++ -O3 -Wall -Wextra -fobjc-arc
FRAMEWORKS = -framework Metal -framework Foundation -framework QuartzCore

BUILD_DIR = ../build
BIN_SILICON = $(BUILD_DIR)/m1_silicon_probe
JSON_SILICON = $(BUILD_DIR)/m1_probe_results.json

BIN_GPUI = $(BUILD_DIR)/gpui_component_bench
JSON_GPUI = $(BUILD_DIR)/gpui_bench_results.json

BIN_SVG = $(BUILD_DIR)/svg_ssot_bench
JSON_SVG = $(BUILD_DIR)/svg_ssot_results.json

BIN_OBJ = $(BUILD_DIR)/object_ssot_bench
JSON_OBJ = $(BUILD_DIR)/object_ssot_results.json

.PHONY: all run run-gpui run-svg run-obj run-all clean

all: $(BIN_SILICON) $(BIN_GPUI) $(BIN_SVG) $(BIN_OBJ)

$(BIN_SILICON): m1_silicon_probe.mm Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(FRAMEWORKS)

$(BIN_GPUI): gpui_component_bench.mm Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(FRAMEWORKS)

$(BIN_SVG): svg_ssot_bench.mm Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(FRAMEWORKS)

$(BIN_OBJ): object_ssot_bench.mm Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(FRAMEWORKS)

run: $(BIN_SILICON)
	$(BIN_SILICON) --json $(JSON_SILICON)

run-gpui: $(BIN_GPUI)
	$(BIN_GPUI) --json $(JSON_GPUI)

run-svg: $(BIN_SVG)
	$(BIN_SVG) --json $(JSON_SVG)

run-obj: $(BIN_OBJ)
	$(BIN_OBJ) --json $(JSON_OBJ)

run-all: run run-gpui run-svg run-obj

clean:
	rm -rf $(BUILD_DIR)
