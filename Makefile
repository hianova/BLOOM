# ==============================================================================
# BLOOM v1.0.0: Clean-Slate 100% Pure C17 / Metal Engine Makefile (Zero C++)
# ==============================================================================
CC ?= clang
BLOOM_VERSION := $(shell tr -d '[:space:]' < VERSION 2>/dev/null || echo "1.0.0")

CFLAGS = -std=c17 -Wall -Wextra -Werror -O2 -pthread \
         -DBLOOM_VERSION_STRING=\"$(BLOOM_VERSION)\" \
         -Iinclude -I../FLOW/src -I../FLOW/build -I../Ricardian/build
OBJCFLAGS = -std=c17 -fobjc-arc -Wall -Wextra -Werror -O2 \
            -DBLOOM_VERSION_STRING=\"$(BLOOM_VERSION)\" \
            -Iinclude -I../FLOW/src -I../FLOW/build -I../Ricardian/build
FRAMEWORKS = -framework Metal -framework QuartzCore -framework Cocoa -framework Foundation -framework AudioToolbox -framework IOSurface

FLOW_DIR = ../FLOW
FLOW_LIB = $(FLOW_DIR)/build/libflow_rt.a
RICARDIAN_DIR = ../Ricardian
RICARDIAN_C = $(RICARDIAN_DIR)/build/ricardian.c
FLOWY_DIR = ../flowy
FLOWY_BIN = $(FLOWY_DIR)/build/flowy

TARGET_ENGINE_OBJ = build/engine_core.o
TARGET_PREFAB_OBJ = build/prefab_ssot.o
TARGET_PROC_OBJ   = build/procedural_assets.o
TARGET_CINE_OBJ   = build/cine_camera.o
TARGET_WP_OBJ     = build/wallpaper_core.o
TARGET_SVGUI_OBJ  = build/svg_ui_core.o
TARGET_SDL3_OBJ   = build/sdl3_gpu_host.o

TARGET_TEST_CORE   = build/test-bloom
TARGET_TEST_PREFAB = build/test-prefab-ssot
TARGET_TEST_PROC   = build/test-procedural-assets
TARGET_TEST_CINE   = build/test-cine-camera
TARGET_TEST_WP     = build/test-wallpaper-engine
TARGET_TEST_SVGUI  = build/test-svg-ui
TARGET_GAME        = build/bloom

.PHONY: all test clean bench storyboard headless wallpaper run dirs verify-flowy verify-lean-all

all: $(TARGET_TEST_CORE) $(TARGET_TEST_PREFAB) $(TARGET_TEST_PROC) $(TARGET_TEST_CINE) $(TARGET_TEST_WP) $(TARGET_TEST_SVGUI) $(TARGET_GAME)

dirs:
	@mkdir -p build build/.lean_stamps

$(FLOW_LIB):
	@echo "▶ Building FLOW v4 runtime in $(FLOW_DIR)..."
	$(MAKE) -C $(FLOW_DIR) all

$(RICARDIAN_C): $(FLOW_LIB)
	@echo "▶ Building Ricardian v4 in $(RICARDIAN_DIR)..."
	cd $(RICARDIAN_DIR) && ./build.sh build

$(FLOWY_BIN): $(FLOW_LIB)
	@echo "▶ Building FLOWY companion CLI in $(FLOWY_DIR)..."
	$(MAKE) -C $(FLOWY_DIR) all

LEAN_SRCS = research/NoteClaims.lean \
            research/SliceValidity.lean \
            research/physics/HighSpeedPhysics.lean \
            research/AudiovisualInvariants.lean \
            research/SovereignQualityGates.lean \
            research/ComposableTicketPipeline.lean \
            research/DopamineEconomy.lean \
            research/WallpaperEngine.lean \
            research/SvgUiFramework.lean \
            research/ZeroOverheadScaling.lean

LEAN_ALL_STAMP = build/.lean_stamps/.all_lean.stamp

$(LEAN_ALL_STAMP): $(LEAN_SRCS) $(FLOWY_BIN) | dirs
	@mkdir -p build/.lean_stamps
	@echo ""
	@echo "▶ Step 3: Lean 4 Parallel Batch Verification ($(words $(LEAN_SRCS)) specifications, 0 sorryAx)..."
	$(FLOWY_BIN) lean --verify-all $(LEAN_SRCS)
	@touch $@

verify-lean-all: $(FLOWY_BIN)
	$(FLOWY_BIN) lean --verify-all $(LEAN_SRCS)

verify-flowy: $(FLOWY_BIN) $(LEAN_ALL_STAMP)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 0/7] FLOWY Cross-Layer Audit (64B Matrix, LLVM IR, Metal AIR & Lean 4)"
	@echo "================================================================================"
	@echo "▶ Step 1: Auditing 64-Byte Univalence Across All 8 Engine, SVG-UI, Platform Bridge & GPU ABI Headers..."
	$(FLOWY_BIN) slice include/flow_game.h --audit-all
	$(FLOWY_BIN) slice include/flow_prefab_ssot.h --audit-all
	$(FLOWY_BIN) slice include/flow_procedural_assets.h --audit-all
	$(FLOWY_BIN) slice include/flow_cine_camera.h --audit-all
	$(FLOWY_BIN) slice include/flow_gpu_abi.h --audit-all
	$(FLOWY_BIN) slice include/flow_wallpaper.h --audit-all
	$(FLOWY_BIN) slice include/flow_svg_ui.h --audit-all
	$(FLOWY_BIN) slice include/flow_platform_bridge.h --audit-all
	@echo "▶ Step 2: LLVM IR & Apple GPU Metal AIR Static Audit (0-Float/0-Heap C17 + 0-Spill GPU IR)..."
	$(FLOWY_BIN) slice src/engine_core.c --llvm-ir
	$(FLOWY_BIN) slice src/prefab_ssot.c --llvm-ir
	$(FLOWY_BIN) slice src/procedural_assets.c --llvm-ir
	$(FLOWY_BIN) slice src/cine_camera.c --llvm-ir
	$(FLOWY_BIN) slice src/wallpaper_core.c --llvm-ir
	$(FLOWY_BIN) slice src/svg_ui_core.c --llvm-ir
	$(FLOWY_BIN) slice src/sdl3_gpu_host.c --llvm-ir
	$(FLOWY_BIN) slice src/shaders.metal --metal-air
	@echo "▶ Step 3: Lean 4 SSOT Physics & Invariant Verification (Cached / Parallel: 0 sorryAx verified)"
	@echo "▶ Step 4: FLOWY Relativistic & Timing Wheel Algebraic Wind Tunnel..."
	$(FLOWY_BIN) diff --mode relativistic --trials 10000
	@echo "✓ GATE 0: ALL 4 CROSS-LAYER DETERMINISTIC AUDITS PASSED!"

HEADERS = include/bloom.h include/flow_game.h include/flow_prefab_ssot.h include/flow_procedural_assets.h include/flow_cine_camera.h include/flow_gpu_abi.h include/flow_wallpaper.h include/flow_svg_ui.h include/flow_shader_portability.h include/flow_platform_bridge.h

$(TARGET_ENGINE_OBJ): src/engine_core.c include/flow_game.h include/flow_prefab_ssot.h include/flow_cine_camera.h $(FLOW_LIB) $(RICARDIAN_C) | dirs
	$(CC) $(CFLAGS) -c src/engine_core.c -o $@

$(TARGET_PREFAB_OBJ): src/prefab_ssot.c include/flow_prefab_ssot.h $(FLOW_LIB) $(RICARDIAN_C) | dirs
	$(CC) $(CFLAGS) -c src/prefab_ssot.c -o $@

$(TARGET_PROC_OBJ): src/procedural_assets.c include/flow_procedural_assets.h include/flow_prefab_ssot.h $(FLOW_LIB) $(RICARDIAN_C) | dirs
	$(CC) $(CFLAGS) -c src/procedural_assets.c -o $@

$(TARGET_CINE_OBJ): src/cine_camera.c include/flow_cine_camera.h $(FLOW_LIB) $(RICARDIAN_C) | dirs
	$(CC) $(CFLAGS) -c src/cine_camera.c -o $@

$(TARGET_WP_OBJ): src/wallpaper_core.c include/flow_wallpaper.h $(FLOW_LIB) $(RICARDIAN_C) | dirs
	$(CC) $(CFLAGS) -c src/wallpaper_core.c -o $@

$(TARGET_SVGUI_OBJ): src/svg_ui_core.c include/flow_svg_ui.h include/flow_gpu_abi.h $(FLOW_LIB) $(RICARDIAN_C) | dirs
	$(CC) $(CFLAGS) -c src/svg_ui_core.c -o $@

$(TARGET_SDL3_OBJ): src/sdl3_gpu_host.c include/flow_platform_bridge.h include/flow_shader_portability.h $(FLOW_LIB) $(RICARDIAN_C) | dirs
	$(CC) $(CFLAGS) -c src/sdl3_gpu_host.c -o $@

$(TARGET_TEST_CORE): tests/test_flow_game.c $(HEADERS) $(TARGET_ENGINE_OBJ) $(TARGET_PREFAB_OBJ) $(TARGET_CINE_OBJ) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_flow_game.c $(TARGET_ENGINE_OBJ) $(TARGET_PREFAB_OBJ) $(TARGET_CINE_OBJ) $(FLOW_LIB) -o $@

$(TARGET_TEST_PREFAB): tests/test_prefab_ssot.c include/flow_prefab_ssot.h $(TARGET_PREFAB_OBJ) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_prefab_ssot.c $(TARGET_PREFAB_OBJ) $(FLOW_LIB) -o $@

$(TARGET_TEST_PROC): tests/test_procedural_assets.c include/flow_procedural_assets.h include/flow_prefab_ssot.h $(TARGET_PROC_OBJ) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_procedural_assets.c $(TARGET_PROC_OBJ) $(FLOW_LIB) -o $@

$(TARGET_TEST_CINE): tests/test_cine_camera.c $(HEADERS) $(TARGET_ENGINE_OBJ) $(TARGET_PREFAB_OBJ) $(TARGET_CINE_OBJ) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_cine_camera.c $(TARGET_ENGINE_OBJ) $(TARGET_PREFAB_OBJ) $(TARGET_CINE_OBJ) $(FLOW_LIB) -o $@

$(TARGET_TEST_WP): tests/test_wallpaper_engine.c include/flow_wallpaper.h $(TARGET_WP_OBJ) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_wallpaper_engine.c $(TARGET_WP_OBJ) $(FLOW_LIB) -o $@

$(TARGET_TEST_SVGUI): tests/test_svg_ui_framework.c $(HEADERS) $(TARGET_SVGUI_OBJ) $(TARGET_PREFAB_OBJ) $(TARGET_ENGINE_OBJ) $(TARGET_CINE_OBJ) $(TARGET_WP_OBJ) $(TARGET_SDL3_OBJ) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_svg_ui_framework.c $(TARGET_SVGUI_OBJ) $(TARGET_PREFAB_OBJ) $(TARGET_ENGINE_OBJ) $(TARGET_CINE_OBJ) $(TARGET_WP_OBJ) $(TARGET_SDL3_OBJ) $(FLOW_LIB) -o $@

$(TARGET_GAME): src/metal_host.m src/shaders.metal $(HEADERS) $(TARGET_ENGINE_OBJ) $(TARGET_PREFAB_OBJ) $(TARGET_PROC_OBJ) $(TARGET_CINE_OBJ) $(TARGET_WP_OBJ) $(TARGET_SVGUI_OBJ) $(TARGET_SDL3_OBJ) $(FLOW_LIB) | dirs
	$(CC) $(OBJCFLAGS) src/metal_host.m $(TARGET_ENGINE_OBJ) $(TARGET_PREFAB_OBJ) $(TARGET_PROC_OBJ) $(TARGET_CINE_OBJ) $(TARGET_WP_OBJ) $(TARGET_SVGUI_OBJ) $(TARGET_SDL3_OBJ) $(FLOW_LIB) $(FRAMEWORKS) -o $@

test: verify-flowy all
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 1/7] Core Engine Verification (Worldline, Padding, Hitstop, Wave64, Ricardian)"
	@echo "================================================================================"
	./$(TARGET_TEST_CORE)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 2/7] SSOT + 64B Prefab + Art Bible (HTML/CSS/JS Triad)"
	@echo "================================================================================"
	./$(TARGET_TEST_PREFAB)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 3/7] Zero-Token Procedural Assets, 8x8 Atlas & SDD SVG Compiler"
	@echo "================================================================================"
	./$(TARGET_TEST_PROC)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 4/7] FlowCineRig64 Cinematography, Hysteresis & Physical Camera Optics"
	@echo "================================================================================"
	./$(TARGET_TEST_CINE)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 5/7] Cross-Platform Zero-Token Live Wallpaper Engine & Power Governor"
	@echo "================================================================================"
	./$(TARGET_TEST_WP)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 6/7] BLOOM SVG-UI 64-Byte Frontend Framework & Zero-Copy OBS Bridge"
	@echo "================================================================================"
	./$(TARGET_TEST_SVGUI)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 7/7] Pure C17 / Metal 4K Single-Pass Benchmark & 4-VCam Storyboard"
	@echo "================================================================================"
	./$(TARGET_GAME) --bench

bench: $(TARGET_GAME)
	./$(TARGET_GAME) --bench

storyboard: $(TARGET_GAME)
	./$(TARGET_GAME) --storyboard

headless: $(TARGET_GAME)
	./$(TARGET_GAME) --headless

wallpaper: $(TARGET_GAME)
	./$(TARGET_GAME) --wallpaper

run: $(TARGET_GAME)
	./$(TARGET_GAME)

clean:
	rm -rf build
