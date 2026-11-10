# ==============================================================================
# BLOOM: 100% Pure C17 Visual, Optics, Procedural SDF/CSG & Semantic UI Engine
# ==============================================================================
CC ?= clang

CFLAGS = -std=c17 -Wall -Wextra -Werror -O2 -pthread \
         -Iinclude -I../FLOW/src -I../FLOW/build

FLOW_DIR = ../FLOW
FLOW_LIB = $(FLOW_DIR)/build/libflow_rt.a
FLOWY_DIR = ../FLOWY
FLOWY_BIN = $(FLOWY_DIR)/build/flowy

TARGET_ABI_OBJ    = build/lean_abi_bridge.o
TARGET_ASSETS_OBJ = build/flow_assets.o
TARGET_ENGINE_OBJ = build/engine_core.o
TARGET_PREFAB_OBJ = build/prefab_ssot.o
TARGET_CINE_OBJ   = build/cine_camera.o
TARGET_SVGUI_OBJ  = build/svg_ui_core.o

BLOOM_OBJS = $(TARGET_ABI_OBJ) $(TARGET_ASSETS_OBJ) $(TARGET_ENGINE_OBJ) \
             $(TARGET_PREFAB_OBJ) $(TARGET_CINE_OBJ) $(TARGET_SVGUI_OBJ)
BLOOM_LIB  = build/libbloom.a

TARGET_TEST_CORE   = build/test-bloom
TARGET_TEST_PREFAB = build/test-prefab-ssot
TARGET_TEST_ASSETS = build/test-flow-assets
TARGET_TEST_CINE   = build/test-cine-camera
TARGET_TEST_SVGUI  = build/test-svg-ui

.PHONY: all test clean dirs verify-flowy verify-lean-all

all: $(BLOOM_LIB) $(TARGET_TEST_CORE) $(TARGET_TEST_PREFAB) $(TARGET_TEST_ASSETS) $(TARGET_TEST_CINE) $(TARGET_TEST_SVGUI)

dirs:
	@mkdir -p build build/.lean_stamps

$(FLOW_LIB):
	@echo "▶ Building FLOW runtime in $(FLOW_DIR)..."
	$(MAKE) -C $(FLOW_DIR) all

$(FLOWY_BIN): $(FLOW_LIB)
	@echo "▶ Building FLOWY companion CLI in $(FLOWY_DIR)..."
	$(MAKE) -C $(FLOWY_DIR) all

LEAN_SRCS = research/LeanFlowAbi.lean \
            research/AssetPipeline.lean \
            research/physics/HighSpeedPhysics.lean \
            research/SvgUiFramework.lean \
            research/ricardian.lean

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
	@echo " [Gate 0/5] FLOWY Cross-Layer Audit (64B Matrix, LLVM IR & Lean 4)"
	@echo "================================================================================"
	@echo "▶ Step 1: Auditing 64-Byte Univalence Across All 8 Engine, Lean-ABI, Asset & GPU Headers..."
	$(FLOWY_BIN) slice include/ricardian.h --audit-all
	$(FLOWY_BIN) slice include/flow_lean_abi.h --audit-all
	$(FLOWY_BIN) slice include/flow_assets.h --audit-all
	$(FLOWY_BIN) slice include/flow_game.h --audit-all
	$(FLOWY_BIN) slice include/flow_prefab_ssot.h --audit-all
	$(FLOWY_BIN) slice include/flow_cine_camera.h --audit-all
	$(FLOWY_BIN) slice include/flow_gpu_abi.h --audit-all
	$(FLOWY_BIN) slice include/flow_svg_ui.h --audit-all
	@echo "▶ Step 2: LLVM IR Static Audit (100% Pure C17: 0-Float / 0-Heap)..."
	$(FLOWY_BIN) slice src/lean_abi_bridge.c --llvm-ir
	$(FLOWY_BIN) slice src/flow_assets.c --llvm-ir
	$(FLOWY_BIN) slice src/engine_core.c --llvm-ir
	$(FLOWY_BIN) slice src/prefab_ssot.c --llvm-ir
	$(FLOWY_BIN) slice src/cine_camera.c --llvm-ir
	$(FLOWY_BIN) slice src/svg_ui_core.c --llvm-ir
	@echo "▶ Step 3: Lean 4 SSOT Physics & Invariant Verification (Cached / Parallel: 0 sorryAx verified)"
	@echo "▶ Step 4: FLOWY Relativistic & Timing Wheel Algebraic Wind Tunnel..."
	$(FLOWY_BIN) diff --mode relativistic --trials 10000
	@echo "✓ GATE 0: ALL CROSS-LAYER DETERMINISTIC AUDITS PASSED!"

HEADERS = include/bloom.h include/ricardian.h include/flow_lean_abi.h include/flow_assets.h include/flow_game.h include/flow_prefab_ssot.h include/flow_cine_camera.h include/flow_gpu_abi.h include/flow_svg_ui.h include/flow_shader_portability.h

$(TARGET_ABI_OBJ): src/lean_abi_bridge.c include/flow_lean_abi.h $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) -c src/lean_abi_bridge.c -o $@

$(TARGET_ASSETS_OBJ): src/flow_assets.c include/flow_assets.h include/flow_lean_abi.h $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) -c src/flow_assets.c -o $@

$(TARGET_ENGINE_OBJ): src/engine_core.c include/flow_game.h include/flow_lean_abi.h include/flow_prefab_ssot.h include/flow_cine_camera.h $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) -c src/engine_core.c -o $@

$(TARGET_PREFAB_OBJ): src/prefab_ssot.c include/flow_prefab_ssot.h $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) -c src/prefab_ssot.c -o $@

$(TARGET_CINE_OBJ): src/cine_camera.c include/flow_cine_camera.h $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) -c src/cine_camera.c -o $@

$(TARGET_SVGUI_OBJ): src/svg_ui_core.c include/flow_svg_ui.h include/flow_gpu_abi.h $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) -c src/svg_ui_core.c -o $@

$(BLOOM_LIB): $(BLOOM_OBJS) | dirs
	ar rcs $@ $(BLOOM_OBJS)

$(TARGET_TEST_CORE): tests/test_flow_game.c $(HEADERS) $(BLOOM_LIB) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_flow_game.c $(BLOOM_LIB) $(FLOW_LIB) -o $@

$(TARGET_TEST_PREFAB): tests/test_prefab_ssot.c $(HEADERS) $(BLOOM_LIB) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_prefab_ssot.c $(BLOOM_LIB) $(FLOW_LIB) -o $@

$(TARGET_TEST_ASSETS): tests/test_flow_assets.c $(HEADERS) $(BLOOM_LIB) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_flow_assets.c $(BLOOM_LIB) $(FLOW_LIB) -o $@

$(TARGET_TEST_CINE): tests/test_cine_camera.c $(HEADERS) $(BLOOM_LIB) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_cine_camera.c $(BLOOM_LIB) $(FLOW_LIB) -o $@

$(TARGET_TEST_SVGUI): tests/test_svg_ui_framework.c $(HEADERS) $(BLOOM_LIB) $(FLOW_LIB) | dirs
	$(CC) $(CFLAGS) tests/test_svg_ui_framework.c $(BLOOM_LIB) $(FLOW_LIB) -o $@

test: verify-flowy all
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 1/5] Core Engine Verification (Worldline, Padding, Hitstop, Wave64, Ricardian)"
	@echo "================================================================================"
	./$(TARGET_TEST_CORE)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 2/5] SSOT + 64B Prefab + Art Bible + Wave64 + TRC Accessibility"
	@echo "================================================================================"
	./$(TARGET_TEST_PREFAB)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 3/5] 64B Asset Pipeline (WAV/GLB/BVH/Brick), Lean-ABI, Matter & Cloud Room"
	@echo "================================================================================"
	./$(TARGET_TEST_ASSETS)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 4/5] FlowCineRig64 Cinematography, Hysteresis & Physical Camera Optics"
	@echo "================================================================================"
	./$(TARGET_TEST_CINE)
	@echo ""
	@echo "================================================================================"
	@echo " [Gate 5/5] BLOOM Constraint-Projected 64B Semantic UI, Glass, LaTeX & Auto-Bento"
	@echo "================================================================================"
	./$(TARGET_TEST_SVGUI)

clean:
	rm -rf build
