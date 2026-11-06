#include "flow_lean_abi.h"
#include "flow_hott_jet.h"
#include <stdio.h>
#include <string.h>

/* 1D Half-Space Hysteresis Polyhedron for Universal .lean <-> FLOW ABI (Internal RT Engine) */
FLOW_QPOLY_DEFINE_EX(FlowAbiPoly1D, 1, 16, 2)

/*
 * ============================================================================
 * BLOOM: Direct .lean <-> FLOW 64-Byte ABI Bridge Implementation
 * ============================================================================
 * Strict Zero Float / Double & Zero Heap Guarantee:
 * Audited via `flowy slice src/lean_abi_bridge.c --llvm-ir`.
 * ============================================================================
 */

__extension__ typedef __int128 flow_abi_i128;

uint64_t flow_capsule64_splitmix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

uint64_t flow_capsule64_calc_mac(const void *capsule64, uint64_t domain_seed) {
    if (!capsule64) {
        return 0ULL;
    }
    const uint8_t *bytes = (const uint8_t *)capsule64;
    uint64_t h = domain_seed ^ 0x5A7ECAFEF1092026ULL;
    /* Hash first 56 bytes (excluding 8-byte MAC at offset 56..63) */
    for (size_t i = 0U; i < 56U; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= 0x100000001B3ULL;
    }
    h = flow_capsule64_splitmix(h);
    return (h == 0ULL) ? 1ULL : h;
}

void flow_capsule64_encode_hex128(const void *capsule64, char out_hex[129]) {
    static const char kHexLut[16] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };
    if (!out_hex) {
        return;
    }
    if (!capsule64) {
        memset(out_hex, '0', 128U);
        out_hex[128] = '\0';
        return;
    }
    const uint8_t *bytes = (const uint8_t *)capsule64;
    for (size_t i = 0U; i < 64U; ++i) {
        uint8_t b = bytes[i];
        out_hex[i * 2U + 0U] = kHexLut[(b >> 4U) & 0x0FU];
        out_hex[i * 2U + 1U] = kHexLut[b & 0x0FU];
    }
    out_hex[128] = '\0';
}

static int hex_nibble_to_int(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    return -1;
}

bool flow_capsule64_decode_hex128(const char *hex_str, void *out_capsule64) {
    if (!hex_str || !out_capsule64) {
        return false;
    }
    uint8_t tmp[64];
    for (size_t i = 0U; i < 64U; ++i) {
        int hi = hex_nibble_to_int(hex_str[i * 2U + 0U]);
        int lo = hex_nibble_to_int(hex_str[i * 2U + 1U]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        tmp[i] = (uint8_t)(((uint32_t)hi << 4U) | (uint32_t)lo);
    }
    memcpy(out_capsule64, tmp, 64U);
    return true;
}

static uint64_t hash_symbol_str(const char *s) {
    uint64_t h = 0xCBF29CE484222325ULL;
    if (!s) {
        return h;
    }
    while (*s) {
        h ^= (uint64_t)(uint8_t)(*s++);
        h *= 0x100000001B3ULL;
    }
    return flow_capsule64_splitmix(h);
}

FlowLeanClause64 flow_lean_abi_make_clause64(uint32_t clause_id,
                                             uint32_t op_kind,
                                             uint32_t src_slot,
                                             uint32_t dst_slot,
                                             int64_t param_a,
                                             int64_t param_b,
                                             uint64_t req_mask,
                                             uint64_t forbid_mask,
                                             const char *lean_symbol) {
    FlowLeanClause64 c;
    memset(&c, 0, sizeof(c));
    c.clause_id   = clause_id;
    c.op_kind     = op_kind;
    c.src_slot    = src_slot % 12U;
    c.dst_slot    = dst_slot % 12U;
    c.param_a     = param_a;
    c.param_b     = param_b;
    c.req_mask    = req_mask;
    c.forbid_mask = forbid_mask;
    c.spec_hash   = hash_symbol_str(lean_symbol) ^
                    flow_capsule64_splitmix((uint64_t)param_a ^ ((uint64_t)param_b << 13U));
    c.clause_mac  = flow_capsule64_calc_mac(&c, 0x4C45414E41424936ULL);
    return c;
}

bool flow_lean_abi_verify_clause64(const FlowLeanClause64 *clause) {
    if (!clause || clause->clause_mac == 0ULL) {
        return false;
    }
    return clause->clause_mac == flow_capsule64_calc_mac(clause, 0x4C45414E41424936ULL);
}

void flow_lean_abi_init_canonical_table(FlowLeanClause64 out_table[16]) {
    if (!out_table) {
        return;
    }
    memset(out_table, 0, sizeof(FlowLeanClause64) * 16U);

    out_table[FLOW_CLAUSE_RICARDIAN_THERMAL_GATE] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_RICARDIAN_THERMAL_GATE,
        FLOW_LEAN_OP_HYSTERESIS_HI,
        0U, 0U,
        801LL, 760LL, /* >800K (>=801K) enter shutdown, <=760K exit */
        0ULL, 0ULL,
        "evaluateThermalMeltdownGate"
    );
    out_table[FLOW_CLAUSE_RICARDIAN_HARBERGER] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_RICARDIAN_HARBERGER,
        FLOW_LEAN_OP_CONSERVATION_SPLIT,
        1U, 2U,
        7000LL, 10000LL, /* 70% dividend, 30% burn */
        0ULL, 0ULL,
        "splitHarbergerDividend"
    );
    out_table[FLOW_CLAUSE_AV_TRC_STROBE_CLAMP] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_AV_TRC_STROBE_CLAMP,
        FLOW_LEAN_OP_CLAMP_INTERVAL,
        2U, 2U,
        0LL, 196608LL, /* <= 3.0 Hz in Q16 */
        0ULL, 0ULL,
        "clampTrcStrobe"
    );
    out_table[FLOW_CLAUSE_AV_CAVITY_HYSTERESIS] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_AV_CAVITY_HYSTERESIS,
        FLOW_LEAN_OP_HYSTERESIS_LO,
        3U, 3U,
        12LL * FLOW_Q16_ONE, 20LL * FLOW_Q16_ONE, /* <=12m enter, >=20m exit */
        0ULL, 0ULL,
        "stepCavityHysteresis"
    );
    out_table[FLOW_CLAUSE_AV_THERMAL_PURGE] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_AV_THERMAL_PURGE,
        FLOW_LEAN_OP_FLOOR_SUB,
        4U, 4U,
        700LL, 120LL, /* ambientKelvin = 700, purgeCoolDelta = 120 */
        0ULL, 0ULL,
        "applyThermalPurge"
    );
    out_table[FLOW_CLAUSE_SOV_OKLAB_CONTRAST] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_SOV_OKLAB_CONTRAST,
        FLOW_LEAN_OP_CLAMP_INTERVAL,
        5U, 5U,
        26214LL, 65536LL, /* minOklabDeltaLQ16 = 26214 */
        0ULL, 0ULL,
        "enforceOklabContrast"
    );
    out_table[FLOW_CLAUSE_SOV_NEGATIVE_SPACE] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_SOV_NEGATIVE_SPACE,
        FLOW_LEAN_OP_CLAMP_INTERVAL,
        6U, 6U,
        39321LL, 49152LL, /* [60%, 75%] in Q16 */
        0ULL, 0ULL,
        "clampNegativeSpace"
    );
    out_table[FLOW_CLAUSE_SOV_SANDBOX_QUOTA] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_SOV_SANDBOX_QUOTA,
        FLOW_LEAN_OP_QUOTA_SLA_GATE,
        7U, 8U,
        64LL, 256LL, /* maxSdfOps = 64, maxScriptSteps = 256 */
        0x0FULL, 0ULL,
        "promoteTicketSLA"
    );
    out_table[FLOW_CLAUSE_WP_OCCLUSION_PAUSE] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_WP_OCCLUSION_PAUSE,
        FLOW_LEAN_OP_HYSTERESIS_HI,
        9U, 9U,
        58982LL, 49152LL, /* >=90% (58982) pause, <=75% (49152) resume */
        0ULL, 0ULL,
        "resolveOcclusionPause"
    );
    out_table[FLOW_CLAUSE_WP_BATTERY_ECO] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_WP_BATTERY_ECO,
        FLOW_LEAN_OP_HYSTERESIS_LO,
        10U, 10U,
        40LL, 50LL, /* <=40% eco enter, >=50% eco exit */
        0ULL, 0ULL,
        "resolveBatteryEco"
    );
    out_table[FLOW_CLAUSE_WP_PARALLAX_CLAMP] = flow_lean_abi_make_clause64(
        FLOW_CLAUSE_WP_PARALLAX_CLAMP,
        FLOW_LEAN_OP_CLAMP_INTERVAL,
        11U, 11U,
        -16384LL, 16384LL, /* [-0.25, +0.25] Q16 */
        0ULL, 0ULL,
        "clampParallaxMag"
    );
}

/*
 * Direct .lean Streaming Lexer (Comment/String Aware, Zero Heap, Zero Float)
 */
static bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '\'';
}

static bool parse_int64_token(const char *tok, int64_t *out_val) {
    if (!tok || !tok[0] || !out_val) {
        return false;
    }
    size_t i = 0U;
    bool neg = false;
    if (tok[i] == '-') {
        neg = true;
        i++;
    }
    if (tok[i] < '0' || tok[i] > '9') {
        return false;
    }
    int64_t acc = 0LL;
    while (tok[i] >= '0' && tok[i] <= '9') {
        acc = acc * 10LL + (int64_t)(tok[i] - '0');
        i++;
    }
    if (tok[i] != '\0') {
        return false;
    }
    *out_val = neg ? -acc : acc;
    return true;
}

static void bind_lean_constant_to_clauses(const char *ident,
                                          int64_t val,
                                          FlowLeanClause64 *clauses,
                                          uint32_t max_clauses,
                                          uint32_t *inout_bound_count) {
    if (!ident || !clauses || max_clauses == 0U) {
        return;
    }
    if (strcmp(ident, "trcMaxStrobeQ16") == 0 && FLOW_CLAUSE_AV_TRC_STROBE_CLAMP < max_clauses) {
        clauses[FLOW_CLAUSE_AV_TRC_STROBE_CLAMP] = flow_lean_abi_make_clause64(
            FLOW_CLAUSE_AV_TRC_STROBE_CLAMP, FLOW_LEAN_OP_CLAMP_INTERVAL,
            2U, 2U, 0LL, val, 0ULL, 0ULL, ident);
        (*inout_bound_count)++;
    } else if (strcmp(ident, "ambientKelvin") == 0 && FLOW_CLAUSE_AV_THERMAL_PURGE < max_clauses) {
        int64_t delta = clauses[FLOW_CLAUSE_AV_THERMAL_PURGE].param_b;
        if (delta <= 0LL) delta = 120LL;
        clauses[FLOW_CLAUSE_AV_THERMAL_PURGE] = flow_lean_abi_make_clause64(
            FLOW_CLAUSE_AV_THERMAL_PURGE, FLOW_LEAN_OP_FLOOR_SUB,
            4U, 4U, val, delta, 0ULL, 0ULL, ident);
        (*inout_bound_count)++;
    } else if (strcmp(ident, "purgeCoolDelta") == 0 && FLOW_CLAUSE_AV_THERMAL_PURGE < max_clauses) {
        int64_t floor_k = clauses[FLOW_CLAUSE_AV_THERMAL_PURGE].param_a;
        if (floor_k <= 0LL) floor_k = 700LL;
        clauses[FLOW_CLAUSE_AV_THERMAL_PURGE] = flow_lean_abi_make_clause64(
            FLOW_CLAUSE_AV_THERMAL_PURGE, FLOW_LEAN_OP_FLOOR_SUB,
            4U, 4U, floor_k, val, 0ULL, 0ULL, ident);
        (*inout_bound_count)++;
    } else if (strcmp(ident, "minOklabDeltaLQ16") == 0 && FLOW_CLAUSE_SOV_OKLAB_CONTRAST < max_clauses) {
        clauses[FLOW_CLAUSE_SOV_OKLAB_CONTRAST] = flow_lean_abi_make_clause64(
            FLOW_CLAUSE_SOV_OKLAB_CONTRAST, FLOW_LEAN_OP_CLAMP_INTERVAL,
            5U, 5U, val, 65536LL, 0ULL, 0ULL, ident);
        (*inout_bound_count)++;
    } else if (strcmp(ident, "minNegSpaceQ16") == 0 && FLOW_CLAUSE_SOV_NEGATIVE_SPACE < max_clauses) {
        int64_t hi = clauses[FLOW_CLAUSE_SOV_NEGATIVE_SPACE].param_b;
        if (hi <= 0LL) hi = 49152LL;
        clauses[FLOW_CLAUSE_SOV_NEGATIVE_SPACE] = flow_lean_abi_make_clause64(
            FLOW_CLAUSE_SOV_NEGATIVE_SPACE, FLOW_LEAN_OP_CLAMP_INTERVAL,
            6U, 6U, val, hi, 0ULL, 0ULL, ident);
        (*inout_bound_count)++;
    } else if (strcmp(ident, "maxNegSpaceQ16") == 0 && FLOW_CLAUSE_SOV_NEGATIVE_SPACE < max_clauses) {
        int64_t lo = clauses[FLOW_CLAUSE_SOV_NEGATIVE_SPACE].param_a;
        if (lo <= 0LL) lo = 39321LL;
        clauses[FLOW_CLAUSE_SOV_NEGATIVE_SPACE] = flow_lean_abi_make_clause64(
            FLOW_CLAUSE_SOV_NEGATIVE_SPACE, FLOW_LEAN_OP_CLAMP_INTERVAL,
            6U, 6U, lo, val, 0ULL, 0ULL, ident);
        (*inout_bound_count)++;
    }
}

bool flow_lean_abi_parse_stream(const char *lean_src,
                                FlowLeanSpecMeta64 *out_meta,
                                FlowLeanClause64 *inout_clauses,
                                uint32_t max_clauses) {
    if (!lean_src || !out_meta) {
        return false;
    }
    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->file_hash = hash_symbol_str(lean_src);

    size_t i = 0U;
    int block_comment_depth = 0;
    bool in_string = false;
    uint32_t const_idx = 0U;
    uint32_t bound_count = 0U;

    char prev_kw[64] = {0};
    char def_ident[64] = {0};
    bool waiting_def_ident = false;
    bool waiting_def_val = false;

    while (lean_src[i] != '\0') {
        char c = lean_src[i];
        char next = lean_src[i + 1U];

        if (block_comment_depth > 0) {
            if (c == '/' && next == '-') {
                block_comment_depth++;
                i += 2U;
                continue;
            }
            if (c == '-' && next == '/') {
                block_comment_depth--;
                i += 2U;
                continue;
            }
            i++;
            continue;
        }
        if (in_string) {
            if (c == '\\' && next != '\0') {
                i += 2U;
                continue;
            }
            if (c == '"') {
                in_string = false;
            }
            i++;
            continue;
        }
        if (c == '-' && next == '-') {
            while (lean_src[i] != '\0' && lean_src[i] != '\n') {
                i++;
            }
            continue;
        }
        if (c == '/' && next == '-') {
            block_comment_depth = 1;
            i += 2U;
            continue;
        }
        if (c == '"') {
            in_string = true;
            i++;
            continue;
        }
        if (c == ':' && next == '=') {
            if (def_ident[0] != '\0') {
                waiting_def_val = true;
            }
            i += 2U;
            continue;
        }

        if (is_ident_char(c) || (c == '-' && next >= '0' && next <= '9')) {
            char tok[64];
            size_t tlen = 0U;
            if (c == '-') {
                tok[tlen++] = lean_src[i++];
            }
            while (lean_src[i] != '\0' && is_ident_char(lean_src[i])) {
                if (tlen + 1U < sizeof(tok)) {
                    tok[tlen++] = lean_src[i];
                }
                i++;
            }
            tok[tlen] = '\0';

            if (strcmp(tok, "sorry") == 0 || strcmp(tok, "sorryAx") == 0) {
                out_meta->sorry_count++;
            } else if (strcmp(tok, "def") == 0 || strcmp(tok, "abbrev") == 0) {
                out_meta->def_count++;
                waiting_def_ident = true;
                waiting_def_val = false;
                def_ident[0] = '\0';
            } else if (strcmp(tok, "theorem") == 0 || strcmp(tok, "lemma") == 0) {
                out_meta->theorem_count++;
                waiting_def_ident = false;
                waiting_def_val = false;
                def_ident[0] = '\0';
            } else if (waiting_def_ident) {
                memcpy(def_ident, tok, sizeof(def_ident));
                waiting_def_ident = false;
            } else if (waiting_def_val) {
                int64_t val = 0LL;
                if (parse_int64_token(tok, &val)) {
                    if (const_idx < 4U) {
                        out_meta->const_vals[const_idx++] = val;
                    }
                    if (inout_clauses && max_clauses > 0U) {
                        bind_lean_constant_to_clauses(def_ident, val, inout_clauses, max_clauses, &bound_count);
                    }
                }
                waiting_def_val = false;
                def_ident[0] = '\0';
            }
            memcpy(prev_kw, tok, sizeof(prev_kw));
            continue;
        }

        i++;
    }

    out_meta->clause_count = bound_count + out_meta->theorem_count;
    out_meta->meta_mac = flow_capsule64_calc_mac(out_meta, 0x4C45414E4D455441ULL);
    return (out_meta->sorry_count == 0U) && (block_comment_depth == 0) && (!in_string);
}

bool flow_lean_abi_load_file(const char *lean_path,
                             FlowLeanSpecMeta64 *out_meta,
                             FlowLeanClause64 *inout_clauses,
                             uint32_t max_clauses) {
    if (!lean_path || !out_meta) {
        return false;
    }
    FILE *fp = fopen(lean_path, "rb");
    if (!fp) {
        return false;
    }
    static char s_lean_buf[65536];
    size_t n = fread(s_lean_buf, 1U, sizeof(s_lean_buf) - 1U, fp);
    fclose(fp);
    s_lean_buf[n] = '\0';
    return flow_lean_abi_parse_stream(s_lean_buf, out_meta, inout_clauses, max_clauses);
}

/*
 * Direct FLOW 64-Byte ABI Execution Primitives
 * Backed by FlowAbiPoly1D_eval_hysteresis + flow_cubical_step_neighbor + flow_jet2_ema_step!
 */
bool flow_lean_abi_eval_hysteresis_hi(int64_t val, int64_t b_enter, int64_t b_exit, bool prev_latched) {
    /*
     * Upper Hysteresis via FLOW 1D Polyhedron + Kan Homotopy Step:
     * Plane 0 normal A[0][0] = +1:
     *   - Violated (latched = 1) when val > b_enter - 1  <=>  val >= b_enter
     *   - Recovers (latched = 0) when val <= b_exit
     */
    FlowAbiPoly1D poly;
    FlowAbiPoly1D_init(&poly);
    poly.A[0][0] = 1;
    FlowAbiPoly1D_set_hysteresis(&poly, 0U, b_enter - 1LL, b_exit);

    int32_t x[1] = {
        (val > (int64_t)INT32_MAX) ? INT32_MAX :
        ((val < (int64_t)INT32_MIN) ? INT32_MIN : (int32_t)val)
    };
    uint64_t prev_mask = prev_latched ? 1ULL : 0ULL;
    FlowQPolyEval ev = FlowAbiPoly1D_eval_hysteresis(&poly, x, prev_mask);
    uint64_t target_mask = ev.violated_mask & 1ULL;

    if (target_mask != prev_mask) {
        uint64_t next_mask = 0ULL;
        if (flow_cubical_step_neighbor(prev_mask, 0U, 0ULL, &next_mask) == FLOW_KAN_FILLED_HOMOTOPIC) {
            return (next_mask & 1ULL) != 0ULL;
        }
    }
    return prev_latched;
}

bool flow_lean_abi_eval_hysteresis_lo(int64_t val, int64_t b_enter, int64_t b_exit, bool prev_latched) {
    /*
     * Lower Hysteresis via FLOW 1D Polyhedron + Kan Homotopy Step:
     * Plane 0 normal A[0][0] = -1:
     *   - Violated (latched = 1) when -val > -(b_enter + 1)  <=>  val <= b_enter
     *   - Recovers (latched = 0) when -val <= -(b_exit)      <=>  val >= b_exit
     */
    FlowAbiPoly1D poly;
    FlowAbiPoly1D_init(&poly);
    poly.A[0][0] = -1;
    FlowAbiPoly1D_set_hysteresis(&poly, 0U, -(b_enter + 1LL), -b_exit);

    int32_t x[1] = {
        (val > (int64_t)INT32_MAX) ? INT32_MAX :
        ((val < (int64_t)INT32_MIN) ? INT32_MIN : (int32_t)val)
    };
    uint64_t prev_mask = prev_latched ? 1ULL : 0ULL;
    FlowQPolyEval ev = FlowAbiPoly1D_eval_hysteresis(&poly, x, prev_mask);
    uint64_t target_mask = ev.violated_mask & 1ULL;

    if (target_mask != prev_mask) {
        uint64_t next_mask = 0ULL;
        if (flow_cubical_step_neighbor(prev_mask, 0U, 0ULL, &next_mask) == FLOW_KAN_FILLED_HOMOTOPIC) {
            return (next_mask & 1ULL) != 0ULL;
        }
    }
    return prev_latched;
}

int64_t flow_lean_abi_eval_clamp(int64_t val, int64_t lo, int64_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

int64_t flow_lean_abi_eval_jet2_ema(int64_t cur_q16, int64_t target_q16, int64_t alpha_q16) {
    FlowJet2 a = flow_jet2_make(cur_q16, 0LL, 0LL);
    FlowJet2 b = flow_jet2_make(target_q16, 0LL, 0LL);
    FlowJet2 stepped = flow_jet2_ema_step(a, b, alpha_q16);
    return stepped.v;
}

void flow_lean_abi_eval_split_bps(uint64_t total,
                                  uint64_t bps,
                                  uint64_t *out_primary,
                                  uint64_t *out_remainder) {
    uint64_t clamped_bps = (bps > 10000ULL) ? 10000ULL : bps;
    uint64_t primary = (uint64_t)(((__uint128_t)total * (__uint128_t)clamped_bps) / 10000ULL);
    uint64_t rem = total - primary;
    if (out_primary)   *out_primary   = primary;
    if (out_remainder) *out_remainder = rem;
}

int64_t flow_lean_abi_eval_floor_sub(int64_t cur_val, int64_t delta_val, int64_t floor_val) {
    if (cur_val > floor_val + delta_val) {
        return cur_val - delta_val;
    }
    return floor_val;
}

bool flow_lean_abi_eval_quota_gate(const FlowLeanClause64 *clause,
                                   uint64_t active_flags,
                                   int64_t cost_a,
                                   int64_t cost_b) {
    if (!clause) {
        return false;
    }
    bool req_ok = ((active_flags & clause->req_mask) == clause->req_mask);
    bool forbid_ok = ((active_flags & clause->forbid_mask) == 0ULL);
    bool quota_ok = (cost_a <= clause->param_a) && (cost_b <= clause->param_b);
    return req_ok && forbid_ok && quota_ok;
}

void flow_lean_abi_regbank_init64(FlowLeanRegBank64 *bank) {
    if (!bank) {
        return;
    }
    memset(bank, 0, sizeof(*bank));
    bank->bank_mac = flow_capsule64_calc_mac(bank, 0x52454742414E4B36ULL);
}

bool flow_lean_abi_step_clause64(const FlowLeanClause64 *clause, FlowLeanRegBank64 *bank) {
    if (!clause || !bank || !flow_lean_abi_verify_clause64(clause)) {
        return false;
    }
    uint32_t src = clause->src_slot % 12U;
    uint32_t dst = clause->dst_slot % 12U;
    uint32_t bit = 1U << (clause->clause_id & 31U);
    int64_t in_val = (int64_t)bank->regs_q16[src];

    switch (clause->op_kind) {
        case FLOW_LEAN_OP_HYSTERESIS_HI: {
            bool prev = (bank->active_mask & bit) != 0U;
            bool next = flow_lean_abi_eval_hysteresis_hi(in_val, clause->param_a, clause->param_b, prev);
            if (next) bank->active_mask |= bit;
            else      bank->active_mask &= ~bit;
            bank->regs_q16[dst] = next ? 1 : 0;
            break;
        }
        case FLOW_LEAN_OP_HYSTERESIS_LO: {
            bool prev = (bank->active_mask & bit) != 0U;
            bool next = flow_lean_abi_eval_hysteresis_lo(in_val, clause->param_a, clause->param_b, prev);
            if (next) bank->active_mask |= bit;
            else      bank->active_mask &= ~bit;
            bank->regs_q16[dst] = next ? 1 : 0;
            break;
        }
        case FLOW_LEAN_OP_CLAMP_INTERVAL: {
            int64_t c = flow_lean_abi_eval_clamp(in_val, clause->param_a, clause->param_b);
            bank->regs_q16[dst] = (int32_t)c;
            break;
        }
        case FLOW_LEAN_OP_JET2_EMA_DAMP: {
            int64_t cur = (int64_t)bank->regs_q16[dst];
            int64_t stepped = flow_lean_abi_eval_jet2_ema(cur, in_val, clause->param_b);
            bank->regs_q16[dst] = (int32_t)stepped;
            break;
        }
        case FLOW_LEAN_OP_CONSERVATION_SPLIT: {
            uint64_t p = 0ULL, r = 0ULL;
            uint64_t tot = (in_val > 0LL) ? (uint64_t)in_val : 0ULL;
            flow_lean_abi_eval_split_bps(tot, (uint64_t)clause->param_a, &p, &r);
            bank->regs_q16[dst] = (int32_t)p;
            bank->regs_q16[(dst + 1U) % 12U] = (int32_t)r;
            break;
        }
        case FLOW_LEAN_OP_FLOOR_SUB: {
            int64_t cooled = flow_lean_abi_eval_floor_sub(in_val, clause->param_b, clause->param_a);
            bank->regs_q16[dst] = (int32_t)cooled;
            break;
        }
        case FLOW_LEAN_OP_QUOTA_SLA_GATE: {
            int64_t cost_b = (int64_t)bank->regs_q16[(src + 1U) % 12U];
            bool ok = flow_lean_abi_eval_quota_gate(clause, (uint64_t)bank->active_mask, in_val, cost_b);
            if (ok) bank->active_mask |= bit;
            else    bank->active_mask &= ~bit;
            bank->regs_q16[dst] = ok ? 1 : 0;
            break;
        }
        default:
            break;
    }

    bank->step_count++;
    bank->bank_mac = flow_capsule64_calc_mac(bank, 0x52454742414E4B36ULL);
    return true;
}
