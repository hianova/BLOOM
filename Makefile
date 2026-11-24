CC ?= clang
CXX ?= clang++

CFLAGS = -std=c17 -O3 -Wall -Wextra -Werror -Iinclude
OBJCFLAGS = -std=c17 -O3 -Wall -Wextra -Werror -fobjc-arc -Iinclude
CXXFLAGS = -std=c++17 -ObjC++ -O3 -Wall -Wextra -Werror -fobjc-arc -Iinclude
FRAMEWORKS = -framework Metal -framework QuartzCore -framework Cocoa -framework CoreText -framework CoreGraphics -framework Foundation

BUILD_DIR = build
CORE_OBJS = $(BUILD_DIR)/bloom_bobj.o $(BUILD_DIR)/bloom_scene.o $(BUILD_DIR)/bloom_sdk.o $(BUILD_DIR)/bloom_ui_kit.o $(BUILD_DIR)/bloom_gesture.o $(BUILD_DIR)/bloom_camera.o
MAC_OBJS = $(BUILD_DIR)/bloom_atlas_mac.o $(BUILD_DIR)/bloom_metal.o $(BUILD_DIR)/bloom_vision_mac.o $(BUILD_DIR)/bloom_host_mac.o

BIN_CORE_TEST = $(BUILD_DIR)/test_bloom_core
BIN_METAL_TEST = $(BUILD_DIR)/test_bloom_metal
BIN_SDK_TEST = $(BUILD_DIR)/test_bloom_sdk
BIN_M2_TEST = $(BUILD_DIR)/test_bloom_m2
BIN_DEMO = $(BUILD_DIR)/bloom_studio_demo

.PHONY: all test gate-ci demo clean

all: $(BIN_CORE_TEST) $(BIN_METAL_TEST) $(BIN_SDK_TEST) $(BIN_M2_TEST) $(BIN_DEMO)

$(BUILD_DIR)/bloom_bobj.o: src/bloom_bobj.c include/bloom_bobj.h include/bloom_abi.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_scene.o: src/bloom_scene.c include/bloom_scene.h include/bloom_bobj.h include/bloom_abi.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_sdk.o: src/bloom_sdk.c include/bloom_sdk.h include/bloom_gpu.h include/bloom_scene.h include/bloom_abi.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_ui_kit.o: src/bloom_ui_kit.c include/bloom_ui_kit.h include/bloom_scene.h include/bloom_abi.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_gesture.o: src/bloom_gesture.c include/bloom_gesture.h include/bloom_abi.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_camera.o: src/bloom_camera.c include/bloom_camera.h include/bloom_abi.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_atlas_mac.o: src/bloom_atlas_mac.m include/bloom_atlas.h include/bloom_scene.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJCFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_metal.o: src/bloom_metal.mm include/bloom_gpu.h include/bloom_atlas.h include/bloom_scene.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_vision_mac.o: src/bloom_vision_mac.mm include/bloom_vision.h include/bloom_scene.h include/bloom_abi.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_host_mac.o: src/bloom_host_mac.mm include/bloom_host.h include/bloom_gpu.h include/bloom_scene.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_CORE_TEST): tests/test_bloom_core.c $(BUILD_DIR)/bloom_bobj.o $(BUILD_DIR)/bloom_scene.o Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/test_bloom_core.c $(BUILD_DIR)/bloom_bobj.o $(BUILD_DIR)/bloom_scene.o -lm -o $@

$(BIN_METAL_TEST): tests/test_bloom_metal.mm $(BUILD_DIR)/bloom_bobj.o $(BUILD_DIR)/bloom_scene.o $(BUILD_DIR)/bloom_atlas_mac.o $(BUILD_DIR)/bloom_metal.o Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tests/test_bloom_metal.mm $(BUILD_DIR)/bloom_bobj.o $(BUILD_DIR)/bloom_scene.o $(BUILD_DIR)/bloom_atlas_mac.o $(BUILD_DIR)/bloom_metal.o $(FRAMEWORKS) -o $@

$(BIN_SDK_TEST): tests/test_bloom_sdk.mm $(CORE_OBJS) $(BUILD_DIR)/bloom_atlas_mac.o $(BUILD_DIR)/bloom_metal.o $(BUILD_DIR)/bloom_vision_mac.o Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tests/test_bloom_sdk.mm $(CORE_OBJS) $(BUILD_DIR)/bloom_atlas_mac.o $(BUILD_DIR)/bloom_metal.o $(BUILD_DIR)/bloom_vision_mac.o $(FRAMEWORKS) -lm -o $@

$(BIN_M2_TEST): tests/test_bloom_m2.mm $(CORE_OBJS) $(BUILD_DIR)/bloom_atlas_mac.o $(BUILD_DIR)/bloom_metal.o Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tests/test_bloom_m2.mm $(CORE_OBJS) $(BUILD_DIR)/bloom_atlas_mac.o $(BUILD_DIR)/bloom_metal.o $(FRAMEWORKS) -lm -o $@

$(BIN_DEMO): examples/bloom_studio_demo.mm $(CORE_OBJS) $(MAC_OBJS) Makefile
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) examples/bloom_studio_demo.mm $(CORE_OBJS) $(MAC_OBJS) $(FRAMEWORKS) -o $@

test: all
	@mkdir -p $(BUILD_DIR)
	python3 tools/bloom_import.py emit-sample-glb $(BUILD_DIR)/sample_mesh.glb
	printf '<svg xmlns="http://www.w3.org/2000/svg"><g id="h_0" data-kind="slider" data-bind="0:0"><rect x="16" y="16" width="240" height="44" rx="6" fill="#22344CEB" stroke="#38D0FFFF"/><text x="20" y="34">ImportedSlider</text></g></svg>\n' > $(BUILD_DIR)/sample_ui.svg
	python3 tools/bloom_import.py import --glb $(BUILD_DIR)/sample_mesh.glb --svg $(BUILD_DIR)/sample_ui.svg --format bobc --out $(BUILD_DIR)/imported_sample.bobc
	python3 tools/bloom_import.py inspect $(BUILD_DIR)/imported_sample.bobc
	$(BIN_CORE_TEST) --imported-bobc $(BUILD_DIR)/imported_sample.bobc
	$(BIN_METAL_TEST)
	$(BIN_SDK_TEST)
	python3 tools/bloom_import.py emit-multi-glb $(BUILD_DIR)/multi_sample.glb
	python3 tools/bloom_import.py import --glb $(BUILD_DIR)/multi_sample.glb --format bobc --out $(BUILD_DIR)/multi_sample.bobc
	python3 tools/bloom_import.py inspect $(BUILD_DIR)/multi_sample.bobc
	$(BIN_M2_TEST) --multi-bobc $(BUILD_DIR)/multi_sample.bobc

gate-ci: test
	@echo "=== Gate 1: Zero Coupling Audit (Zero FLOW/INFO/SDL/ge_host_if) ==="
	! grep -rnE "#include.*(flow|searu|ge_host_if|SDL|info\.h)" include/ src/ shaders/ tests/ examples/
	@echo "=== Gate 2b: Shader Line Budget <= 400 per file ==="
	@for f in shaders/*.metal; do n=$$(wc -l < "$$f" | tr -d ' '); echo "  $$f: $$n lines"; test $$n -le 400; done
	@echo "=== Gate 7b: Headless Studio Demo Sanity Snapshot ==="
	$(BIN_DEMO) --headless-frames 5 --w 1920 --h 1080 --ppm $(BUILD_DIR)/studio_demo.ppm
	test -s $(BUILD_DIR)/studio_demo.ppm
	@echo "ALL MILESTONE-1 & MILESTONE-2 CI GATES PASSED!"

demo: $(BIN_DEMO)
	$(BIN_DEMO)

clean:
	rm -rf $(BUILD_DIR)
