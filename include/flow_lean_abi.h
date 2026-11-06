#ifndef FLOW_LEAN_ABI_H
#define FLOW_LEAN_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flow_hott_jet.h"
#include "ricardian.h"

/*
 * ============================================================================
 * BLOOM: Direct .lean <-> FLOW 64-Byte ABI Bridge & Unified 64B Capsule Kernel
 * ============================================================================
 * Enforces Lean 4 Formal Invariants (research/LeanFlowAbi.lean):
 *   1. Zero-Compile-Time .lean -> FLOW 64B ABI Routing:
 *      Parses `.lean` specifications at runtime (`flow_lean_abi_load_file`)
 *      and executes state transitions directly through `FLOW` primitives
 *      (`FlowAbiPoly1D_eval_hysteresis`, `flow_cubical_step_neighbor`,
 *      `flow_jet2_ema_step`).
 *   2. Unified 64-Byte Capsule MAC & Hex128 Codec:
 *      Single canonical implementation replacing duplicated `splitmix64` and
 *      `hex128` functions across engine modules.
 * ============================================================================
 */

/* 1D Half-Space Hysteresis Polyhedron for Universal .lean <-> FLOW ABI */
FLOW_QPOLY_DEFINE_EX(FlowAbiPoly1D, 1, 16, 2)

/* Universal .lean <-> FLOW 64-Byte ABI Operator Opcodes */
typedef enum {
    FLOW_LEAN_OP_NOP                = 0,
    FLOW_LEAN_OP_HYSTERESIS_HI      = 1, /* enter >= param_a, exit <= param_b (via FlowQPoly + Kan) */
    FLOW_LEAN_OP_HYSTERESIS_LO      = 2, /* enter <= param_a, exit >= param_b (via FlowQPoly + Kan) */
    FLOW_LEAN_OP_CLAMP_INTERVAL     = 3, /* clamp value into [param_a, param_b] */
    FLOW_LEAN_OP_JET2_EMA_DAMP      = 4, /* 2-Jet EMA step toward param_a with alpha param_b */
    FLOW_LEAN_OP_CONSERVATION_SPLIT = 5, /* exact bps split: (v * param_a)/10000, remainder */
    FLOW_LEAN_OP_FLOOR_SUB          = 6, /* max(param_a, v - param_b) monotone cooling/settlement */
    FLOW_LEAN_OP_QUOTA_SLA_GATE     = 7  /* multi-axis SLA quota & ternary mask gate */
} FlowLeanAbiOp;

/* Canonical Clause IDs Bound to BLOOM .lean Specifications */
enum {
    FLOW_CLAUSE_RICARDIAN_THERMAL_GATE = 1,  /* ricardian.lean: [800K enter, 760K exit] */
    FLOW_CLAUSE_RICARDIAN_HARBERGER    = 2,  /* ricardian.lean: 7000/3000 bps split */
    FLOW_CLAUSE_AV_TRC_STROBE_CLAMP    = 3,  /* AudiovisualInvariants.lean: [0, 196608] (3.0Hz) */
    FLOW_CLAUSE_AV_CAVITY_HYSTERESIS   = 4,  /* AudiovisualInvariants.lean: [12m enter, 20m exit] */
    FLOW_CLAUSE_AV_THERMAL_PURGE       = 5,  /* AudiovisualInvariants.lean: -120K floor 700K */
    FLOW_CLAUSE_SOV_OKLAB_CONTRAST     = 6,  /* SovereignQualityGates.lean: >= 26214 (0.40) */
    FLOW_CLAUSE_SOV_NEGATIVE_SPACE     = 7,  /* SovereignQualityGates.lean: [39321, 49152] */
    FLOW_CLAUSE_SOV_SANDBOX_QUOTA      = 8,  /* SovereignQualityGates.lean: ops<=64, steps<=256 */
    FLOW_CLAUSE_WP_OCCLUSION_PAUSE     = 9,  /* WallpaperEngine.lean: [58982 enter, 49152 exit] */
    FLOW_CLAUSE_WP_BATTERY_ECO         = 10, /* WallpaperEngine.lean: [40% enter, 50% exit] */
    FLOW_CLAUSE_WP_PARALLAX_CLAMP      = 11  /* WallpaperEngine.lean: [-16384, +16384] */
};

/*
 * 1. FlowLeanClause64: 64-Byte Univalent .lean -> FLOW Operator Descriptor
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t clause_id;   /* 4B: Unique clause ID */
    uint32_t op_kind;     /* 4B: FlowLeanAbiOp (1..7) */
    uint32_t src_slot;    /* 4B: Input register index (0..11) */
    uint32_t dst_slot;    /* 4B: Output register index (0..11) */
    int64_t  param_a;     /* 8B: Primary threshold / lo bound / b_enter / bps / floor */
    int64_t  param_b;     /* 8B: Secondary threshold / hi bound / b_exit / delta */
    uint64_t req_mask;    /* 8B: Required ternary flag mask */
    uint64_t forbid_mask; /* 8B: Forbidden ternary flag mask */
    uint64_t spec_hash;   /* 8B: 64-bit hash of .lean symbol & specification */
    uint64_t clause_mac;  /* 8B: 64-bit cryptographic integrity MAC */
} FlowLeanClause64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowLeanClause64, RicardianStateV4);
_Static_assert(sizeof(FlowLeanClause64) == 64U, "FlowLeanClause64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowLeanClause64) == 64U, "FlowLeanClause64 must be 64-byte aligned");

/*
 * 2. FlowLeanRegBank64: 64-Byte Univalent Execution Register Bank
 */
typedef struct __attribute__((aligned(64))) {
    int32_t  regs_q16[12]; /* 48B: 12 general-purpose Q16 / integer registers */
    uint32_t active_mask;  /*  4B: Latched hysteresis / gate bitmask (bit k = clause k) */
    uint32_t step_count;   /*  4B: Monotonic ABI evaluation step counter */
    uint64_t bank_mac;     /*  8B: 64-bit register bank MAC */
} FlowLeanRegBank64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowLeanRegBank64, RicardianStateV4);
_Static_assert(sizeof(FlowLeanRegBank64) == 64U, "FlowLeanRegBank64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowLeanRegBank64) == 64U, "FlowLeanRegBank64 must be 64-byte aligned");

/*
 * 3. FlowLeanSpecMeta64: 64-Byte Parsed .lean Specification Metadata
 */
typedef struct __attribute__((aligned(64))) {
    uint64_t file_hash;     /*  8B: 64-bit content hash of the parsed .lean stream */
    uint32_t def_count;     /*  4B: Count of parsed `def` / `abbrev` declarations */
    uint32_t theorem_count; /*  4B: Count of parsed `theorem` / `lemma` declarations */
    uint32_t clause_count;  /*  4B: Count of bound ABI clauses extracted */
    uint32_t sorry_count;   /*  4B: Count of `sorry` tokens outside comments/strings (must be 0) */
    int64_t  const_vals[4]; /* 32B: First 4 extracted numeric constant definitions */
    uint64_t meta_mac;      /*  8B: 64-bit metadata seal MAC */
} FlowLeanSpecMeta64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowLeanSpecMeta64, RicardianStateV4);
_Static_assert(sizeof(FlowLeanSpecMeta64) == 64U, "FlowLeanSpecMeta64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowLeanSpecMeta64) == 64U, "FlowLeanSpecMeta64 must be 64-byte aligned");

/* Unified 64-Byte Capsule Hash, MAC & Hex128 Codec */
uint64_t flow_capsule64_splitmix(uint64_t x);
uint64_t flow_capsule64_calc_mac(const void *capsule64, uint64_t domain_seed);
void     flow_capsule64_encode_hex128(const void *capsule64, char out_hex[129]);
bool     flow_capsule64_decode_hex128(const char *hex_str, void *out_capsule64);

/* Construct & Verify 64-Byte .lean ABI Clause Descriptors */
FlowLeanClause64 flow_lean_abi_make_clause64(uint32_t clause_id,
                                             uint32_t op_kind,
                                             uint32_t src_slot,
                                             uint32_t dst_slot,
                                             int64_t param_a,
                                             int64_t param_b,
                                             uint64_t req_mask,
                                             uint64_t forbid_mask,
                                             const char *lean_symbol);

bool flow_lean_abi_verify_clause64(const FlowLeanClause64 *clause);

/* Load Canonical BLOOM .lean Clauses or Parse Directly from .lean Source Stream/File */
void flow_lean_abi_init_canonical_table(FlowLeanClause64 out_table[16]);

bool flow_lean_abi_parse_stream(const char *lean_src,
                                FlowLeanSpecMeta64 *out_meta,
                                FlowLeanClause64 *inout_clauses,
                                uint32_t max_clauses);

bool flow_lean_abi_load_file(const char *lean_path,
                             FlowLeanSpecMeta64 *out_meta,
                             FlowLeanClause64 *inout_clauses,
                             uint32_t max_clauses);

/* Direct FLOW 64-Byte ABI Execution Primitives (Backed by FlowQPoly + Kan + FlowJet2) */
bool    flow_lean_abi_eval_hysteresis_hi(int64_t val, int64_t b_enter, int64_t b_exit, bool prev_latched);
bool    flow_lean_abi_eval_hysteresis_lo(int64_t val, int64_t b_enter, int64_t b_exit, bool prev_latched);
int64_t flow_lean_abi_eval_clamp(int64_t val, int64_t lo, int64_t hi);
int64_t flow_lean_abi_eval_jet2_ema(int64_t cur_q16, int64_t target_q16, int64_t alpha_q16);
void    flow_lean_abi_eval_split_bps(uint64_t total, uint64_t bps, uint64_t *out_primary, uint64_t *out_remainder);
int64_t flow_lean_abi_eval_floor_sub(int64_t cur_val, int64_t delta_val, int64_t floor_val);
bool    flow_lean_abi_eval_quota_gate(const FlowLeanClause64 *clause,
                                      uint64_t active_flags,
                                      int64_t cost_a,
                                      int64_t cost_b);

/* Execute a 64-Byte .lean ABI Clause Against a 64-Byte Register Bank */
void flow_lean_abi_regbank_init64(FlowLeanRegBank64 *bank);
bool flow_lean_abi_step_clause64(const FlowLeanClause64 *clause, FlowLeanRegBank64 *bank);

#endif /* FLOW_LEAN_ABI_H */
