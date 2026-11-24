#ifndef BLOOM_BOBJ_H
#define BLOOM_BOBJ_H

#include "bloom_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Magic numbers for 3C Dual-Form Binary SSOT */
#define BLOOM_BOBJ_MAGIC_ROW UINT32_C(0x52424F42) /* "BOBR" LE */
#define BLOOM_BOBJ_MAGIC_COL UINT32_C(0x43424F42) /* "BOBC" LE */
#define BLOOM_BOBJ_FLAG_GEOM UINT32_C(1)

typedef struct {
    char *name;
    uint32_t meshlet_cnt;
    uint32_t idx_bytes;
    BloomMeshletHeader32 *hdrs;
    BloomVtx12 *verts;
    uint8_t *local_idx;
} BloomBobjTemplate;

typedef struct {
    char *id;
    BloomObjectState48 state;
    BloomConstraintSpec cspec;
} BloomBobjObject;

typedef struct {
    char *id;
    BloomAnalyticPrim32 prim;
} BloomBobjAnalytic;

typedef struct {
    char *id;
    char *label;
    BloomHudNodeBin bin;
} BloomBobjHud;

typedef struct {
    BloomBobjTemplate *templates;
    uint32_t template_cnt;
    BloomBobjObject *objects;
    uint32_t object_cnt;
    BloomBobjAnalytic *analytics;
    uint32_t analytic_cnt;
    BloomBobjHud *huds;
    uint32_t hud_cnt;
} BloomBobjDoc;

typedef struct {
    uint64_t lo;
    uint64_t hi;
} BloomDigest128;

/** Encode doc into Row-Major GPU-Direct BOBR format; caller frees *out_buf with free(). */
bool bloom_bobj_encode_row(const BloomBobjDoc *doc, bool include_geom,
                           uint8_t **out_buf, size_t *out_len);

/** Decode Row-Major BOBR payload into out_doc; caller frees via bloom_bobj_free(). */
bool bloom_bobj_decode_row(const uint8_t *buf, size_t len, BloomBobjDoc *out_doc);

/** Encode doc into Columnar/Delta-Varint/Byte-Plane BOBC format; caller frees *out_buf with free(). */
bool bloom_bobj_encode_col(const BloomBobjDoc *doc, bool include_geom,
                           uint8_t **out_buf, size_t *out_len);

/** Decode Columnar BOBC payload into out_doc; caller frees via bloom_bobj_free(). */
bool bloom_bobj_decode_col(const uint8_t *buf, size_t len, BloomBobjDoc *out_doc);

/** Auto-detect BOBR vs BOBC magic and decode into out_doc. */
bool bloom_bobj_decode_auto(const uint8_t *buf, size_t len, BloomBobjDoc *out_doc);

/** Load file from disk and decode BOBR/BOBC automatically. */
bool bloom_bobj_load_file(const char *path, BloomBobjDoc *out_doc);

/** Free all heap allocations owned by doc. */
void bloom_bobj_free(BloomBobjDoc *doc);

/** Deterministic 128-bit digest across every field of BloomBobjDoc. */
BloomDigest128 bloom_bobj_digest128(const BloomBobjDoc *doc, bool include_geom);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_BOBJ_H */
