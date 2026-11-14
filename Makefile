export PATH := $(HOME)/.elan/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC          ?= clang
CXX         ?= clang++
AR          ?= ar
LEAN        ?= lean
LEANCHECKER ?= leanchecker
NAGA        ?= naga
UNAME_S     := $(shell uname -s)
PREFIX      ?= /usr/local

CLANG_TIDY := $(shell if [ -x /opt/homebrew/opt/llvm/bin/clang-tidy ]; then echo /opt/homebrew/opt/llvm/bin/clang-tidy; \
	elif command -v brew >/dev/null 2>&1 && [ -x "$$(brew --prefix llvm 2>/dev/null)/bin/clang-tidy" ]; then echo "$$(brew --prefix llvm)/bin/clang-tidy"; \
	else echo clang-tidy; fi)

SDL3_RAW_CFLAGS := $(shell if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl3; then pkg-config --cflags sdl3; \
	elif [ -d /opt/homebrew/include/SDL3 ]; then echo "-I/opt/homebrew/include"; \
	else echo "-I/usr/local/include"; fi)
SDL3_CFLAGS     := $(patsubst -I%,-isystem %,$(SDL3_RAW_CFLAGS))

SDL3_LIBS       := $(shell if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl3; then \
	libd="$$(pkg-config --variable=libdir sdl3)"; \
	printf '%s -Wl,-rpath,%s\n' "$$(pkg-config --libs sdl3)" "$$libd"; \
	elif [ -d /opt/homebrew/lib ]; then echo "-L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3"; \
	else echo "-L/usr/local/lib -Wl,-rpath,/usr/local/lib -lSDL3"; fi)

FLOW_DIR     ?= $(abspath ../FLOW)
FLOW_BUILD   := $(FLOW_DIR)/build
LEAN_SYSROOT := $(shell $(LEAN) --print-prefix)

BUILD_DIR    := build
LIBBLOOM_A   := $(BUILD_DIR)/libbloom.a
BLOOM_BIN    := $(BUILD_DIR)/bloom
BLOOM_OLEAN  := $(BUILD_DIR)/Bloom.olean
STUDIO_IMG   := $(BUILD_DIR)/studio.bloom
GEN_SPV      := $(BUILD_DIR)/bloom_core.spv
GEN_MSL      := $(BUILD_DIR)/bloom_core.metal
SHADER_STAMP := $(BUILD_DIR)/.shaders.ok
STATIC_STAMP := $(BUILD_DIR)/.static.ok

NOGPR_FLAG   := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)
UBTRAP_FLAGS := $(shell $(CC) -fsanitize=signed-integer-overflow,shift-exponent,bounds,null,return,unreachable -fsanitize-trap=all -x c -c /dev/null -o /dev/null 2>/dev/null && echo "-fsanitize=signed-integer-overflow,shift-exponent,bounds,null,return,unreachable -fsanitize-trap=all")

CORE_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror $(NOGPR_FLAG) $(UBTRAP_FLAGS) -O2 -fPIC -Iinclude -isystem "$(FLOW_DIR)"
HOST_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror $(UBTRAP_FLAGS) -O2 -fPIC -Iinclude -isystem "$(FLOW_DIR)" $(SDL3_CFLAGS)
CLI_CFLAGS  := -std=c17 -Wall -Wextra -Wpedantic -Werror $(UBTRAP_FLAGS) -O2 -Iinclude -isystem "$(FLOW_DIR)"
JSI_CFLAGS  := -std=c17 -Wall -Wextra -Werror $(UBTRAP_FLAGS) -O2 -fPIC -Iinclude -isystem "$(FLOW_DIR)" -isystem "$(LEAN_SYSROOT)/include"

ALL_C_SRCS := src/bloom_scene.c src/bloom_ui.c src/bloom_prefab.c src/bloom_lean_jsi.c src/bloom_gpu_host.c src/bloom_cli.c
CORE_OBJS  := $(BUILD_DIR)/bloom_scene.o $(BUILD_DIR)/bloom_ui.o $(BUILD_DIR)/bloom_prefab.o
JSI_OBJ    := $(BUILD_DIR)/bloom_lean_jsi.o
HOST_OBJ   := $(BUILD_DIR)/bloom_gpu_host.o
CLI_OBJ    := $(BUILD_DIR)/bloom_cli.o
WGSL_SRC   := shaders/bloom_core.wgsl

DYN_EXT     := $(if $(filter Darwin,$(UNAME_S)),dylib,so)
DYN_SHARED  := $(if $(filter Darwin,$(UNAME_S)),-dynamiclib,-shared -lm -lpthread -ldl)
DYN_LDFLAGS := $(DYN_SHARED) -L"$(LEAN_SYSROOT)/lib/lean" -Wl,-rpath,"$(LEAN_SYSROOT)/lib/lean" -lleanshared $(SDL3_LIBS)
CLI_LDFLAGS := $(if $(filter Darwin,$(UNAME_S)),,-lm -lpthread -ldl) $(SDL3_LIBS)

LIBBLOOM_DYN := $(BUILD_DIR)/libbloom.$(DYN_EXT)

.DEFAULT_GOAL := all
.PHONY: all verify test cpp-gate mutate-check studio run clean install uninstall upstream-deps

# ==============================================================================
# Pure Single-Track Cross-Platform Verified SDK & Zero-Lean Native Runner Build
# AOT Bloom.lean -> build/studio.bloom (16KB) -> Pure C17 build/bloom (SDL3 GPU)
# ==============================================================================
all: cpp-gate $(LIBBLOOM_A) $(LIBBLOOM_DYN) $(GEN_SPV) $(GEN_MSL) $(BLOOM_BIN) $(BLOOM_OLEAN) $(STUDIO_IMG)

verify: all test mutate-check

test: cpp-gate all
	./$(BLOOM_BIN) --headless $(STUDIO_IMG) --out $(BUILD_DIR)/.cli_smoke.ppm --steps 30 && rm -f $(BUILD_DIR)/.cli_smoke.ppm

mutate-check: $(LIBBLOOM_A)
	@if [ "$(UNAME_S)" = "Darwin" ] && [ "$$(uname -m)" = "arm64" ]; then \
		python3 tools/sabotage_check.py; \
	else \
		echo "[mutate-check] Skipping Mach-O ARM64 binary sabotage gate on $(UNAME_S)/$$(uname -m)"; \
	fi

cpp-gate: include/*.h | $(BUILD_DIR)
	@printf '#include "flow.h"\n#include "searu.h"\n#include "bloom.h"\n#include "bloom_abi.h"\n#include "bloom_scene.h"\n#include "bloom_ui.h"\n#include "bloom_prefab.h"\n#include "bloom_host.h"\nint bloom_cpp20_abi_probe(void) { return (int)(sizeof(BloomEngineArena) + alignof(BloomEngineArena) + alignof(BloomCell64)); }\n' | \
		$(CXX) -x c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude -isystem "$(FLOW_DIR)" -c - -o $(BUILD_DIR)/.cpp_gate.o
	@rm -f $(BUILD_DIR)/.cpp_gate.o

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

upstream-deps:
	@test -f "$(FLOW_BUILD)/Flow.olean" || $(MAKE) -C "$(FLOW_DIR)" all

$(BUILD_DIR)/bloom_scene.o: src/bloom_scene.c include/bloom_abi.h include/bloom_scene.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_ui.o: src/bloom_ui.c include/bloom_abi.h include/bloom_ui.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/bloom_prefab.o: src/bloom_prefab.c include/bloom_abi.h include/bloom_prefab.h include/bloom_scene.h include/bloom_ui.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -c $< -o $@

# Gate 1: Single-Track Hygiene + C17 Symbolic Static Analysis + Zero-Float / Zero-Heap / ASM Audit
$(STATIC_STAMP): $(ALL_C_SRCS) include/*.h | $(BUILD_DIR) upstream-deps
	@bad="$$(find include src shaders specs -type f \( -name '*.metal' -o -name '*.m' -o -name '*.mm' -o -name '*.swift' -o -name '*.cpp' -o -name '*.cc' \) 2>/dev/null)"; \
		if [ -n "$$bad" ]; then echo "ERROR: Dual-track / non-C17/WGSL source files detected: $$bad"; exit 1; fi
	$(CLANG_TIDY) -checks='-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' \
		--header-filter='include/.*' -warnings-as-errors='*' \
		$(ALL_C_SRCS) \
		-- -std=c17 -Iinclude -isystem "$(FLOW_DIR)" -isystem "$(LEAN_SYSROOT)/include" $(SDL3_CFLAGS)
	@touch $@

$(LIBBLOOM_A): $(CORE_OBJS) $(STATIC_STAMP)
	@rm -f $@
	$(AR) rcs $@ $(CORE_OBJS)
	@! nm -u $@ | grep -Eq '(^|[[:space:]_])(malloc|calloc|realloc|free|strtod|strtof|atof|__ubsan)([[:space:]]|$$|@)' || \
		(echo "ERROR: Heap, float, or runtime symbol leaked into libbloom.a!" && nm -u $@ && rm -f $@ && exit 1)
	@if command -v otool >/dev/null 2>&1 && [ "$(UNAME_S)" = "Darwin" ]; then \
		fpu_cnt="$$(otool -tv $@ | awk '{print $$2}' | grep -Ec '^(fadd|fsub|fmul|fdiv|fsqrt|fmadd|fmsub|fcmp|fccmp|fcsel|scvtf|ucvtf|fcvtzs|fcvtzu|fmov|umov|smov|ins|dup|mov\.[bhsdq])$$' || true)"; \
		if [ "$$fpu_cnt" -ne 0 ]; then echo "ERROR: $$fpu_cnt FPU/SIMD opcodes leaked into libbloom.a!"; rm -f $@; exit 1; fi; \
		blr_cnt="$$(otool -tv $@ | awk '{print $$2}' | grep -Ec '^blr$$' || true)"; \
		if [ "$$blr_cnt" -ne 0 ]; then echo "ERROR: $$blr_cnt indirect BLR calls leaked into libbloom.a!"; rm -f $@; exit 1; fi; \
	fi

$(JSI_OBJ): src/bloom_lean_jsi.c include/*.h | $(BUILD_DIR)
	$(CC) $(JSI_CFLAGS) -c $< -o $@

$(HOST_OBJ): src/bloom_gpu_host.c include/*.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(CLI_OBJ): src/bloom_cli.c include/*.h | $(BUILD_DIR)
	$(CC) $(CLI_CFLAGS) -c $< -o $@

$(LIBBLOOM_DYN): $(JSI_OBJ) $(HOST_OBJ) $(LIBBLOOM_A)
	$(CC) -O2 $(JSI_OBJ) $(HOST_OBJ) $(LIBBLOOM_A) $(DYN_LDFLAGS) -o $@

# Standalone Zero-Lean-Runtime Pure C17 Native Studio & CLI Executable
$(BLOOM_BIN): $(CLI_OBJ) $(HOST_OBJ) $(LIBBLOOM_A)
	$(CC) -O2 $(CLI_OBJ) $(HOST_OBJ) $(LIBBLOOM_A) $(CLI_LDFLAGS) -o $@
	@if [ "$(UNAME_S)" = "Darwin" ] && command -v otool >/dev/null 2>&1; then \
		! otool -L $@ | grep -qi 'leanshared' || (echo "ERROR: leanshared leaked into standalone $@!" && otool -L $@ && rm -f $@ && exit 1); \
	elif command -v ldd >/dev/null 2>&1; then \
		! ldd $@ | grep -qi 'leanshared' || (echo "ERROR: leanshared leaked into standalone $@!" && ldd $@ && rm -f $@ && exit 1); \
	fi
	@! nm -u $@ | grep -Eq '(^|[[:space:]_])lean_' || (echo "ERROR: Lean runtime symbol leaked into $@!" && rm -f $@ && exit 1)

# Gate 2: Single-Source WGSL Audit & Cross-Compilation (SPIR-V + MSL 2.4)
$(SHADER_STAMP): $(WGSL_SRC) | $(BUILD_DIR)
	@! grep -Eq '\b(u64|i64)\b' $(WGSL_SRC)
	$(NAGA) $(WGSL_SRC) $(GEN_SPV)
	$(NAGA) --metal-version 2.4 $(WGSL_SRC) $(GEN_MSL)
	sed -e 's/arena \[\[user(fake0)\]\]/arena [[buffer(0)]]/g' \
	    -e 's/out_tex \[\[user(fake0)\]\]/out_tex [[texture(0)]]/g' \
	    $(GEN_MSL) > $(GEN_MSL).tmp && mv $(GEN_MSL).tmp $(GEN_MSL)
	@! grep -q 'user(fake' $(GEN_MSL)
	@touch $@

$(GEN_SPV) $(GEN_MSL): $(SHADER_STAMP)
	@test -f $@

# Gate 3: `Bloom.lean` Compile-Time Boundary (`bv_decide` SAT + Flexbox + Live SDL3 GPU `#eval` + `leanchecker`)
$(BLOOM_OLEAN): Bloom.lean $(LIBBLOOM_DYN) $(GEN_SPV) $(GEN_MSL) specs/studio_inspector.svg | $(BUILD_DIR) upstream-deps
	LEAN_PATH="$(FLOW_BUILD)" $(LEAN) --load-dynlib=$(LIBBLOOM_DYN) -DwarningAsError=true -o $@ Bloom.lean || (rm -f $@ && exit 1)
	LEAN_PATH="$(FLOW_BUILD):$(BUILD_DIR)" $(LEANCHECKER) Bloom || (rm -f $@ && exit 1)
	@ax="$$(printf 'import Bloom\n#print axioms Bloom.arena_page_bijection\n#print axioms Bloom.arena_slot_mask_inbounds\n#print axioms Bloom.poly_smin_le_operands\n#print axioms Bloom.bvh_parent_encloses_children\n#print axioms Bloom.beer_lambert_crystal_floor_bounded\n#print axioms Bloom.trc_strobe_clamp_safe\n#print axioms Bloom.bento_split_disjoint\n#print axioms Bloom.wave64_u32x2_bijection\n#print axioms Bloom.prefab_revert_restores_ssot\n#print axioms Bloom.prefab_revert_idempotent\n#print axioms Bloom.flex_child_span_bounded\n#print axioms Bloom.arena_slot_mask_bv32_inbounds\n#print axioms Bloom.wave64_set_test_exact_bv32\n#print axioms Bloom.wave64_set_frame_independence_bv32\n#print axioms Bloom.csg_mask_pack_roundtrip_bv32\n#print axioms Bloom.trc_strobe_clamp_bv32\n#print axioms Bloom.beer_lambert_clamp_bv32\n#print axioms Bloom.kan_lock_mode_preserve_bv16\n#print axioms Bloom.prefab_override_revert_flags_bv16\n#print axioms Bloom.ui_label_slot_inbounds_bv32\n#print axioms Bloom.searu_phase_gate_silence_bv64\n#print axioms Bloom.searu_kan_pin_flip_invariant_bv64\n' | LEAN_PATH="$(FLOW_BUILD):$(BUILD_DIR)" $(LEAN) --stdin)" && \
		! printf '%s\n' "$$ax" | grep -q 'sorryAx' || (rm -f $@ && exit 1)

$(STUDIO_IMG): $(BLOOM_OLEAN)
	@test -s $@ || (printf 'import Bloom\n#eval do let ok ← Bloom.exportStudioImage; if !ok then throw (IO.userError "exportStudioImage failed")\n' | \
		LEAN_PATH="$(FLOW_BUILD):$(BUILD_DIR)" $(LEAN) --load-dynlib=$(LIBBLOOM_DYN) --stdin)

studio: all
	./$(BLOOM_BIN) $(STUDIO_IMG)

run: studio

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/include/bloom $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/lib/flow \
		$(DESTDIR)$(PREFIX)/share/bloom/shaders $(DESTDIR)$(PREFIX)/share/bloom/specs
	install -m 755 $(BLOOM_BIN) $(DESTDIR)$(PREFIX)/bin/bloom
	install -m 644 include/*.h $(DESTDIR)$(PREFIX)/include/bloom/
	install -m 644 $(LIBBLOOM_A) $(DESTDIR)$(PREFIX)/lib/libbloom.a
	install -m 755 $(LIBBLOOM_DYN) $(DESTDIR)$(PREFIX)/lib/libbloom.$(DYN_EXT)
	install -m 644 Bloom.lean $(DESTDIR)$(PREFIX)/lib/flow/Bloom.lean
	install -m 644 $(BLOOM_OLEAN) $(DESTDIR)$(PREFIX)/lib/flow/Bloom.olean
	install -m 644 $(STUDIO_IMG) $(DESTDIR)$(PREFIX)/share/bloom/studio.bloom
	install -m 644 $(WGSL_SRC) $(DESTDIR)$(PREFIX)/share/bloom/shaders/bloom_core.wgsl
	install -m 644 $(GEN_SPV) $(DESTDIR)$(PREFIX)/share/bloom/shaders/bloom_core.spv
	install -m 644 $(GEN_MSL) $(DESTDIR)$(PREFIX)/share/bloom/shaders/bloom_core.metal
	install -m 644 specs/studio_inspector.svg $(DESTDIR)$(PREFIX)/share/bloom/specs/studio_inspector.svg

uninstall:
	rm -rf $(DESTDIR)$(PREFIX)/include/bloom $(DESTDIR)$(PREFIX)/share/bloom
	rm -f $(DESTDIR)$(PREFIX)/bin/bloom $(DESTDIR)$(PREFIX)/lib/libbloom.a $(DESTDIR)$(PREFIX)/lib/libbloom.$(DYN_EXT) \
		$(DESTDIR)$(PREFIX)/lib/flow/Bloom.lean $(DESTDIR)$(PREFIX)/lib/flow/Bloom.olean

clean:
	rm -rf $(BUILD_DIR)
