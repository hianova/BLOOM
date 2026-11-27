#ifndef BLOOM_WIRE_H
#define BLOOM_WIRE_H

#include "bloom_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * BLOOM Canonical Packed Binary Wire Codec ("BLMW")
 * ============================================================================
 * Self-contained C17/C++20 binary wire codec matching the canonical ULEB128,
 * zigzag, byte-plane, and front-coded string discipline of flow_wire_pack.h
 * without introducing external header dependencies (Gate 1 zero-coupling).
 * ============================================================================ */

#define BLOOM_WIRE_MAGIC   UINT32_C(0x574D4C42) /* "BLMW" LE */
#define BLOOM_WIRE_VERSION UINT8_C(1)
#define BLOOM_WIRE_HDR_B   8U

typedef enum {
    BLOOM_WIRE_KIND_PBR_MANIFEST = 1,
    BLOOM_WIRE_KIND_SDK_SNAPSHOT = 2,
    BLOOM_WIRE_KIND_BENCH_RECORD = 3
} BloomWireKind;

typedef enum {
    BLOOM_ALPHA_OPAQUE = 0,
    BLOOM_ALPHA_MASK   = 1,
    BLOOM_ALPHA_BLEND  = 2
} BloomAlphaMode;

typedef struct {
    char *path;
} BloomPbrTextureRef;

typedef struct {
    float    base_color_factor[4];
    float    emissive_factor[3];
    float    metallic_factor;
    float    roughness_factor;
    float    normal_scale;
    int32_t  base_color_tex;
    int32_t  mr_tex;
    int32_t  normal_tex;
    int32_t  emissive_tex;
    int32_t  ao_tex;
    uint8_t  double_sided;
    uint8_t  alpha_mode; /* BloomAlphaMode */
} BloomPbrMaterialDesc;

typedef struct {
    char    *id;
    int32_t  material_index;
} BloomPbrObjectBinding;

typedef struct {
    BloomPbrTextureRef    *textures;
    uint32_t               texture_cnt;
    BloomPbrMaterialDesc  *materials;
    uint32_t               material_cnt;
    BloomPbrObjectBinding *objects;
    uint32_t               object_cnt;
} BloomPbrManifest;

typedef struct {
    char    *name;
    char    *unit;
    double   value;
    uint64_t bytes;
    uint32_t checksum;
} BloomBenchMetric;

typedef struct {
    char             *suite;
    uint64_t          timestamp_ns;
    BloomBenchMetric *metrics;
    uint32_t          metric_cnt;
} BloomBenchReport;

/* --- Canonical ULEB128 & ZigZag Primitives --- */

static inline uint64_t bloom_wire_zz_enc(int64_t x) {
    return ((uint64_t)x << 1u) ^ (uint64_t)(x >> 63);
}

static inline int64_t bloom_wire_zz_dec(uint64_t u) {
    return (int64_t)((u >> 1u) ^ (~(u & UINT64_C(1)) + UINT64_C(1)));
}

/** Encode canonical ULEB128 varint into fixed buffer. */
bool bloom_wire_put_varu64(uint8_t *buf, size_t cap, size_t *pos, uint64_t v);

/** Decode canonical ULEB128 varint (rejects overlong/non-canonical encodings). */
bool bloom_wire_get_varu64(const uint8_t *buf, size_t end, size_t *pos, uint64_t *out);

/* --- PBR Manifest ("BLMW" Kind 1) --- */

bool bloom_wire_encode_pbr_manifest(const BloomPbrManifest *m,
                                    uint8_t **out_buf, size_t *out_len);
bool bloom_wire_decode_pbr_manifest(const uint8_t *buf, size_t len,
                                    BloomPbrManifest *out_m);
bool bloom_wire_load_pbr_manifest_file(const char *path, BloomPbrManifest *out_m);
bool bloom_wire_save_pbr_manifest_file(const char *path, const BloomPbrManifest *m);
void bloom_pbr_manifest_free(BloomPbrManifest *m);
BloomDigest128 bloom_pbr_manifest_digest128(const BloomPbrManifest *m);

/* --- SDK Snapshot ("BLMW" Kind 2) --- */

bool bloom_wire_encode_sdk_snapshot(const BloomSdkSnapshot *snap,
                                    uint8_t **out_buf, size_t *out_len);
bool bloom_wire_decode_sdk_snapshot(const uint8_t *buf, size_t len,
                                    BloomSdkSnapshot *out_snap);
BloomDigest128 bloom_sdk_snapshot_digest128(const BloomSdkSnapshot *snap);

/* --- Benchmark / Telemetry Report ("BLMW" Kind 3) --- */

bool bloom_wire_encode_bench_report(const BloomBenchReport *rep,
                                    uint8_t **out_buf, size_t *out_len);
bool bloom_wire_decode_bench_report(const uint8_t *buf, size_t len,
                                    BloomBenchReport *out_rep);
bool bloom_wire_save_bench_report_file(const char *path, const BloomBenchReport *rep);
bool bloom_wire_load_bench_report_file(const char *path, BloomBenchReport *out_rep);
void bloom_bench_report_free(BloomBenchReport *rep);
BloomDigest128 bloom_bench_report_digest128(const BloomBenchReport *rep);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_WIRE_H */
