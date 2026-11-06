#ifndef RICARDIAN_H
#define RICARDIAN_H

/*
 * ============================================================================
 * Ricardian v4 — 64-Byte External-Constraint Economic Kernel (Header-Only C17)
 * ============================================================================
 * Direct C17 mirror of research/ricardian.lean (Flow.RicardianV4):
 *   1. 64-Byte Cache-Line Aligned RicardianStateV4 & RicardianShadowStateV4
 *   2. Harberger 70/30 Tax Split Conservation (harberger_split_conservation)
 *   3. Assembly Theory Depth Monotonicity & Compression (joinAssemblyIndex)
 *   4. Constant-Product AMM Reserve Floor >= 1 (amm_reserve_never_depletes)
 * ============================================================================
 */

#include <stdint.h>

typedef struct __attribute__((aligned(64))) {
    uint64_t reserve_a;       /* 8B: Constant-product AMM reserve A */
    uint64_t reserve_b;       /* 8B: Constant-product AMM reserve B */
    uint64_t m0_vault;        /* 8B: M0 base vault balance */
    uint64_t iou_supply;      /* 8B: Outstanding credit IOU supply */
    uint64_t harberger_pool;  /* 8B: 70% Harberger dividend pool */
    uint64_t burned_supply;   /* 8B: 30% Harberger burned supply */
    uint64_t assembly_depth;  /* 8B: Assembly Theory complexity index */
    uint64_t thermal_kelvin;  /* 8B: Hull temperature in Kelvin (800K/760K gate) */
} RicardianStateV4;

typedef struct __attribute__((aligned(64))) {
    uint64_t words[8];
} RicardianShadowStateV4;

_Static_assert(sizeof(RicardianStateV4) == 64U, "RicardianStateV4 must be 64 bytes");
_Static_assert(_Alignof(RicardianStateV4) == 64U, "RicardianStateV4 must be 64-byte aligned");
_Static_assert(sizeof(RicardianShadowStateV4) == 64U, "RicardianShadowStateV4 must be 64 bytes");
_Static_assert(_Alignof(RicardianShadowStateV4) == 64U, "RicardianShadowStateV4 must be 64-byte aligned");

static inline uint64_t ricardian_harberger_tax(uint64_t valuation, uint64_t rate_bps) {
    return (valuation * rate_bps) / 10000ULL;
}

static inline uint64_t ricardian_harberger_div(uint64_t tax) {
    return (tax * 7000ULL) / 10000ULL;
}

static inline uint64_t ricardian_harberger_burn(uint64_t tax) {
    return tax - ((tax * 7000ULL) / 10000ULL);
}

static inline uint64_t ricardian_join_assembly(uint64_t at1,
                                               uint64_t at2,
                                               uint64_t shared_substructures) {
    uint64_t u = (at1 >= 1ULL) ? at1 : 1ULL;
    uint64_t v = (at2 >= 1ULL) ? at2 : 1ULL;
    uint64_t min_at = (u < v) ? u : v;
    uint64_t max_reuse = (min_at > 1ULL) ? (min_at - 1ULL) : 0ULL;
    uint64_t reuse = (shared_substructures < max_reuse) ? shared_substructures : max_reuse;
    return (u + v) - reuse;
}

static inline uint64_t ricardian_amm_after_swap(uint64_t reserve_in,
                                                uint64_t reserve_out,
                                                uint64_t amount_in,
                                                uint64_t fee_bps) {
    if (reserve_in == 0ULL || reserve_out == 0ULL || fee_bps >= 10000ULL) {
        return (reserve_out >= 1ULL) ? reserve_out : 1ULL;
    }
    __extension__ unsigned __int128 eff_in =
        (unsigned __int128)amount_in * (unsigned __int128)(10000ULL - fee_bps);
    __extension__ unsigned __int128 denom =
        (unsigned __int128)reserve_in * 10000ULL + eff_in;
    if (denom == 0U) {
        return 1ULL;
    }
    __extension__ unsigned __int128 num =
        (unsigned __int128)reserve_in * (unsigned __int128)reserve_out * 10000ULL;
    uint64_t next_out = (uint64_t)(num / denom);
    return (next_out <= 1ULL) ? 1ULL : next_out;
}

#endif /* RICARDIAN_H */
