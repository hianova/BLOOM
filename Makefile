export PATH := $(HOME)/.elan/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

LEAN    ?= lean
SWIFTC  ?= swiftc
UNAME_S := $(shell uname -s)
PREFIX  ?= /usr/local

FLOW_DIR   ?= $(abspath ../FLOW)
UNIT_DIR   ?= $(abspath ../UNIT)
FLOW_BUILD := $(FLOW_DIR)/build
UNIT_BUILD := $(UNIT_DIR)/build
FLOW_BIN   ?= $(FLOW_BUILD)/flow

BUILD_DIR   := build
BLOOM_OLEAN := $(BUILD_DIR)/Bloom.olean
METAL_COSIM := $(BUILD_DIR)/metal_cosim

SHADERS := shaders/bloom_core.metal shaders/bloom_core.wgsl

.DEFAULT_GOAL := all
.PHONY: all test clean install uninstall upstream-deps shader-gate

ifeq ($(UNAME_S),Darwin)
all: $(BLOOM_OLEAN) $(METAL_COSIM)
else
all: $(BLOOM_OLEAN)
endif

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

upstream-deps:
	@test -f "$(FLOW_BUILD)/Flow.olean" && test -x "$(FLOW_BIN)" || $(MAKE) -C "$(FLOW_DIR)" all
	@test -f "$(UNIT_BUILD)/Unit.olean" || $(MAKE) -C "$(UNIT_DIR)" all

$(BLOOM_OLEAN): Bloom.lean | $(BUILD_DIR) upstream-deps
	@echo "=== [Gate 1] BLOOM Pure Lean 4 Formal Proof Check (0 sorryAx, 0 C/C++/Obj-C files) ==="
	LEAN_PATH="$(FLOW_BUILD):$(UNIT_BUILD)" $(LEAN) -DwarningAsError=true -o $@ Bloom.lean
	@ax="$$(printf 'import Bloom\n#print axioms Bloom.Geometry.poly_smin_bounded_below_min\n#print axioms Bloom.Geometry.swept_segment_catches_midstep_tunneling\n#print axioms Bloom.Geometry.rigid_isometry_preserves_dist_sq\n#print axioms Bloom.Geometry.bvh_parent_union_encloses_children\n#print axioms Bloom.Optics.gladstone_dale_vacuum_and_monotone\n#print axioms Bloom.Optics.schlick_fresnel_grazing_and_bounds\n#print axioms Bloom.Optics.beer_lambert_energy_conserving\n#print axioms Bloom.Optics.thin_lens_in_focus_zero_coc_and_symmetric\n#print axioms Bloom.Optics.camera_pullback_isolates_collision_shocks\n#print axioms Bloom.Optics.strobe_trc_clamp_isolates_flash_spikes\n#print axioms Bloom.Ui.bento_split_disjoint_and_conserves_area\n#print axioms Bloom.Ui.wave64_u32x2_bijection\n#print axioms Bloom.GpuAbi.gpuPacket64_toBytes_size\n#print axioms Bloom.GpuAbi.q16MulNatSplit_exact\n' | LEAN_PATH="$(FLOW_BUILD):$(UNIT_BUILD):$(BUILD_DIR)" $(LEAN) --stdin)" && \
		! printf '%s\n' "$$ax" | grep -q 'sorryAx'
	@test -z "$$(find . -maxdepth 3 \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.cc' -o -name '*.m' -o -name '*.mm' \) -not -path './build/*')"
	@echo "  [PASS] $(BLOOM_OLEAN) compiled (0 sorryAx, 0 .c/.h/.cpp/.m files)"

$(METAL_COSIM): tests/metal_cosim.swift $(SHADERS) | $(BUILD_DIR)
	@echo "=== [Gate 2] Apple Metal Unified Memory Runner & MSL Kernel Compilation ==="
	$(SWIFTC) -O -warnings-as-errors -framework Metal -framework Foundation tests/metal_cosim.swift -o $(METAL_COSIM)
	./$(METAL_COSIM) --compile-only shaders/bloom_core.metal

shader-gate: $(SHADERS)
ifeq ($(UNAME_S),Darwin)
	@$(MAKE) $(METAL_COSIM)
endif
	@if command -v naga >/dev/null 2>&1; then \
		naga shaders/bloom_core.wgsl && echo "  [PASS] naga validated shaders/bloom_core.wgsl"; \
	fi

test: all shader-gate TestBloom.lean
	@echo "=== [Gate 3] .flow Runtime + 0-ULP GPU/CPU Co-Verification + PPM/Kitty Egress ==="
	FLOW_BIN="$(FLOW_BIN)" LEAN_PATH="$(FLOW_BUILD):$(UNIT_BUILD):$(BUILD_DIR)" \
		$(LEAN) -DwarningAsError=true --run TestBloom.lean
	@echo "================================================================================"
	@echo "   ALL BLOOM VERIFICATION GATES PASSED (0 .c/.h/.m, 0 sorryAx, 0-ULP GPU==CPU)"
	@echo "================================================================================"

install: $(BLOOM_OLEAN)
	mkdir -p $(DESTDIR)$(PREFIX)/lib/flow $(DESTDIR)$(PREFIX)/share/bloom/shaders
	install -m 644 Bloom.lean $(DESTDIR)$(PREFIX)/lib/flow/Bloom.lean
	install -m 644 $(BLOOM_OLEAN) $(DESTDIR)$(PREFIX)/lib/flow/Bloom.olean
	install -m 644 shaders/bloom_core.metal $(DESTDIR)$(PREFIX)/share/bloom/shaders/bloom_core.metal
	install -m 644 shaders/bloom_core.wgsl $(DESTDIR)$(PREFIX)/share/bloom/shaders/bloom_core.wgsl

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/flow/Bloom.lean $(DESTDIR)$(PREFIX)/lib/flow/Bloom.olean
	rm -rf $(DESTDIR)$(PREFIX)/share/bloom

clean:
	rm -rf $(BUILD_DIR)
