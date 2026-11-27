#include "bloom_wire.h"

#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Canonical ULEB128 Varint Primitives (Strict Canonical Validation)
 * ============================================================================ */

bool bloom_wire_put_varu64(uint8_t *buf, size_t cap, size_t *pos, uint64_t v) {
    if (!buf || !pos) return false;
    size_t p = *pos;
    for (uint32_t i = 0u; i < 10u; ++i) {
        if (p >= cap) return false;
        uint8_t lo = (uint8_t)(v & UINT64_C(0x7F));
        v >>= 7u;
        if (v == UINT64_C(0)) {
            buf[p] = lo;
            *pos = p + 1u;
            return true;
        }
        buf[p] = (uint8_t)(lo | UINT8_C(0x80));
        p += 1u;
    }
    return false;
}

bool bloom_wire_get_varu64(const uint8_t *buf, size_t end, size_t *pos, uint64_t *out) {
    if (!buf || !pos || !out) return false;
    size_t p = *pos;
    uint64_t acc = UINT64_C(0);
    uint32_t shift = 0u;
    for (uint32_t i = 0u; i < 10u; ++i) {
        if (p >= end) return false;
        uint8_t b = buf[p++];
        if (i == 9u) {
            if (b != UINT8_C(1)) return false;
            acc |= UINT64_C(1) << 63u;
            *pos = p;
            *out = acc;
            return true;
        }
        uint64_t payload = (uint64_t)(b & UINT8_C(0x7F));
        if (i > 0u && b == UINT8_C(0)) return false;
        acc |= payload << shift;
        if ((b & UINT8_C(0x80)) == UINT8_C(0)) {
            *pos = p;
            *out = acc;
            return true;
        }
        shift += 7u;
    }
    return false;
}

/* ============================================================================
 * Internal Growable Byte Writer & Bounded Reader
 * ============================================================================ */

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
    bool     oom;
} WireWriter;

typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
    bool           err;
} WireReader;

static bool ww_reserve(WireWriter *ww, size_t extra) {
    if (ww->oom) return false;
    if (ww->data && extra <= ww->cap - ww->len) return true;
    if (extra > SIZE_MAX - ww->len) {
        ww->oom = true;
        return false;
    }
    size_t need = ww->len + extra;
    size_t ncap = ww->cap ? ww->cap : 256u;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2u) {
            ncap = need;
            break;
        }
        ncap *= 2u;
    }
    uint8_t *np = (uint8_t *)realloc(ww->data, ncap);
    if (!np) {
        ww->oom = true;
        return false;
    }
    ww->data = np;
    ww->cap = ncap;
    return true;
}

static void ww_u8(WireWriter *ww, uint8_t v) {
    if (ww_reserve(ww, 1u)) ww->data[ww->len++] = v;
}

static void ww_u16(WireWriter *ww, uint16_t v) {
    if (!ww_reserve(ww, 2u)) return;
    ww->data[ww->len++] = (uint8_t)(v & 0xFFu);
    ww->data[ww->len++] = (uint8_t)((v >> 8u) & 0xFFu);
}

static void ww_u32(WireWriter *ww, uint32_t v) {
    if (!ww_reserve(ww, 4u)) return;
    for (int i = 0; i < 4; ++i) ww->data[ww->len++] = (uint8_t)((v >> (i * 8)) & 0xFFu);
}

static void ww_u64(WireWriter *ww, uint64_t v) {
    if (!ww_reserve(ww, 8u)) return;
    for (int i = 0; i < 8; ++i) ww->data[ww->len++] = (uint8_t)((v >> (i * 8)) & 0xFFu);
}

static void ww_bytes(WireWriter *ww, const void *src, size_t n) {
    if (n == 0u) return;
    if (!src) { ww->oom = true; return; }
    if (ww_reserve(ww, n)) {
        memcpy(ww->data + ww->len, src, n);
        ww->len += n;
    }
}

static void ww_varu64(WireWriter *ww, uint64_t v) {
    if (!ww_reserve(ww, 10u)) return;
    if (!bloom_wire_put_varu64(ww->data, ww->cap, &ww->len, v)) ww->oom = true;
}

static void ww_header(WireWriter *ww, uint8_t kind, uint16_t flags) {
    ww_u32(ww, BLOOM_WIRE_MAGIC);
    ww_u8(ww, BLOOM_WIRE_VERSION);
    ww_u8(ww, kind);
    ww_u16(ww, flags);
}

static uint8_t wr_u8(WireReader *wr) {
    if (wr->err || wr->pos + 1u > wr->len) {
        wr->err = true;
        return 0u;
    }
    return wr->data[wr->pos++];
}

static uint16_t wr_u16(WireReader *wr) {
    if (wr->err || wr->len - wr->pos < 2u) {
        wr->err = true;
        return 0u;
    }
    uint16_t v = (uint16_t)wr->data[wr->pos] | ((uint16_t)wr->data[wr->pos + 1u] << 8u);
    wr->pos += 2u;
    return v;
}

static uint32_t wr_u32(WireReader *wr) {
    if (wr->err || wr->len - wr->pos < 4u) {
        wr->err = true;
        return 0u;
    }
    uint32_t v = (uint32_t)wr->data[wr->pos] |
                 ((uint32_t)wr->data[wr->pos + 1u] << 8u) |
                 ((uint32_t)wr->data[wr->pos + 2u] << 16u) |
                 ((uint32_t)wr->data[wr->pos + 3u] << 24u);
    wr->pos += 4u;
    return v;
}

static uint64_t wr_u64(WireReader *wr) {
    if (wr->err || wr->len - wr->pos < 8u) {
        wr->err = true;
        return 0u;
    }
    uint64_t v = 0u;
    for (int i = 0; i < 8; ++i) {
        v |= ((uint64_t)wr->data[wr->pos + (size_t)i]) << (i * 8);
    }
    wr->pos += 8u;
    return v;
}

static void wr_bytes(WireReader *wr, void *dst, size_t n) {
    if (n == 0u) return;
    if (!dst || wr->err || wr->len - wr->pos < n) {
        wr->err = true;
        return;
    }
    memcpy(dst, wr->data + wr->pos, n);
    wr->pos += n;
}

static uint64_t wr_varu64(WireReader *wr) {
    uint64_t v = 0u;
    if (wr->err || !bloom_wire_get_varu64(wr->data, wr->len, &wr->pos, &v)) {
        wr->err = true;
        return 0u;
    }
    return v;
}

static bool wr_check_header(WireReader *wr, uint8_t expected_kind, uint16_t *out_flags) {
    if (!wr || wr->len < BLOOM_WIRE_HDR_B) return false;
    uint32_t magic = wr_u32(wr);
    uint8_t ver = wr_u8(wr);
    uint8_t kind = wr_u8(wr);
    uint16_t flags = wr_u16(wr);
    if (wr->err || magic != BLOOM_WIRE_MAGIC || ver != BLOOM_WIRE_VERSION || kind != expected_kind) {
        return false;
    }
    if (out_flags) *out_flags = flags;
    return true;
}

static char *wire_strdup_n(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1u);
    if (!p) return NULL;
    if (n > 0u && s) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static void put_front_strings_cb(WireWriter *ww, uint32_t count,
                                 const char *(*getter)(const void *ctx, uint32_t i),
                                 const void *ctx) {
    const char *prev = "";
    size_t prev_len = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        const char *cur = getter(ctx, i);
        if (!cur) cur = "";
        size_t cur_len = strlen(cur);
        size_t lim = prev_len < cur_len ? prev_len : cur_len;
        size_t lcp = 0u;
        while (lcp < lim && prev[lcp] == cur[lcp]) ++lcp;
        size_t suf = cur_len - lcp;
        ww_varu64(ww, (uint64_t)lcp);
        ww_varu64(ww, (uint64_t)suf);
        ww_bytes(ww, cur + lcp, suf);
        prev = cur;
        prev_len = cur_len;
    }
}

static bool get_front_strings_alloc(WireReader *wr, uint32_t count, char ***out_arr) {
    if (count == 0u) {
        *out_arr = NULL;
        return true;
    }
    if (count > wr->len - wr->pos) {
        wr->err = true;
        return false;
    }
    char **arr = (char **)calloc(count, sizeof(char *));
    if (!arr) {
        wr->err = true;
        return false;
    }
    char *prev = wire_strdup_n("", 0u);
    size_t prev_len = 0u;
    if (!prev) {
        free(arr);
        wr->err = true;
        return false;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        uint64_t lcp64 = wr_varu64(wr);
        uint64_t suf64 = wr_varu64(wr);
        if (wr->err || lcp64 > prev_len || suf64 > wr->len - wr->pos) {
            free(prev);
            for (uint32_t j = 0u; j < i; ++j) free(arr[j]);
            free(arr);
            wr->err = true;
            return false;
        }
        size_t lcp = (size_t)lcp64;
        size_t suf = (size_t)suf64;
        char *cur = (char *)malloc(lcp + suf + 1u);
        if (!cur) {
            free(prev);
            for (uint32_t j = 0u; j < i; ++j) free(arr[j]);
            free(arr);
            wr->err = true;
            return false;
        }
        if (lcp > 0u) memcpy(cur, prev, lcp);
        wr_bytes(wr, cur + lcp, suf);
        cur[lcp + suf] = '\0';
        arr[i] = cur;
        free(prev);
        prev = wire_strdup_n(cur, lcp + suf);
        prev_len = lcp + suf;
        if (!prev) {
            for (uint32_t j = 0u; j <= i; ++j) free(arr[j]);
            free(arr);
            wr->err = true;
            return false;
        }
    }
    free(prev);
    *out_arr = arr;
    return !wr->err;
}

static void put_f32_planes(WireWriter *ww, const float *vals, uint32_t count, size_t stride_bytes) {
    const uint8_t *base = (const uint8_t *)vals;
    for (int lane = 3; lane >= 0; --lane) {
        for (uint32_t i = 0u; i < count; ++i) {
            uint32_t u = 0u;
            memcpy(&u, base + (size_t)i * stride_bytes, 4u);
            ww_u8(ww, (uint8_t)((u >> (lane * 8)) & 0xFFu));
        }
    }
}

static void get_f32_planes(WireReader *wr, float *vals, uint32_t count, size_t stride_bytes) {
    if (count == 0u) return;
    if ((size_t)count * 4u > wr->len - wr->pos) { wr->err = true; return; }
    uint32_t *raw = (uint32_t *)calloc(count, sizeof(uint32_t));
    if (!raw) { wr->err = true; return; }
    for (int lane = 3; lane >= 0; --lane) {
        for (uint32_t i = 0u; i < count; ++i) {
            raw[i] |= (uint32_t)wr_u8(wr) << (lane * 8);
        }
    }
    uint8_t *base = (uint8_t *)vals;
    for (uint32_t i = 0u; i < count; ++i) {
        memcpy(base + (size_t)i * stride_bytes, &raw[i], 4u);
    }
    free(raw);
}

static void put_u32_lanes(WireWriter *ww, const uint32_t *vals, uint32_t count, size_t stride_bytes) {
    const uint8_t *base = (const uint8_t *)vals;
    for (int b = 0; b < 4; ++b) {
        for (uint32_t i = 0u; i < count; ++i) {
            uint32_t v = 0u;
            memcpy(&v, base + (size_t)i * stride_bytes, 4u);
            ww_u8(ww, (uint8_t)((v >> (b * 8)) & 0xFFu));
        }
    }
}

static void get_u32_lanes(WireReader *wr, uint32_t *vals, uint32_t count, size_t stride_bytes) {
    if (count == 0u) return;
    if ((size_t)count * 4u > wr->len - wr->pos) { wr->err = true; return; }
    uint32_t *tmp = (uint32_t *)calloc(count, sizeof(uint32_t));
    if (!tmp) { wr->err = true; return; }
    for (int b = 0; b < 4; ++b) {
        for (uint32_t i = 0u; i < count; ++i) tmp[i] |= (uint32_t)wr_u8(wr) << (b * 8);
    }
    uint8_t *base = (uint8_t *)vals;
    for (uint32_t i = 0u; i < count; ++i) memcpy(base + (size_t)i * stride_bytes, &tmp[i], 4u);
    free(tmp);
}

static void put_u64_lanes(WireWriter *ww, const uint64_t *vals, uint32_t count, size_t stride_bytes) {
    const uint8_t *base = (const uint8_t *)vals;
    for (int b = 0; b < 8; ++b) {
        for (uint32_t i = 0u; i < count; ++i) {
            uint64_t v = 0u;
            memcpy(&v, base + (size_t)i * stride_bytes, 8u);
            ww_u8(ww, (uint8_t)((v >> (b * 8)) & 0xFFu));
        }
    }
}

static void get_u64_lanes(WireReader *wr, uint64_t *vals, uint32_t count, size_t stride_bytes) {
    if (count == 0u) return;
    if ((size_t)count * 8u > wr->len - wr->pos) { wr->err = true; return; }
    uint64_t *tmp = (uint64_t *)calloc(count, sizeof(uint64_t));
    if (!tmp) { wr->err = true; return; }
    for (int b = 0; b < 8; ++b) {
        for (uint32_t i = 0u; i < count; ++i) tmp[i] |= (uint64_t)wr_u8(wr) << (b * 8);
    }
    uint8_t *base = (uint8_t *)vals;
    for (uint32_t i = 0u; i < count; ++i) memcpy(base + (size_t)i * stride_bytes, &tmp[i], 8u);
    free(tmp);
}

static void wire_digest_mix(BloomDigest128 *d, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0u; i < n; ++i) {
        d->lo ^= b[i];
        d->lo *= UINT64_C(0x100000001B3);
        d->hi ^= (uint64_t)b[i] + UINT64_C(0x517CC1B727220A95);
        d->hi = (d->hi << 7u) | (d->hi >> 57u);
    }
}

static void wire_digest_str(BloomDigest128 *d, const char *s) {
    const char *t = s ? s : "";
    uint32_t len = (uint32_t)strlen(t);
    wire_digest_mix(d, &len, sizeof(len));
    wire_digest_mix(d, t, len);
}

/* ============================================================================
 * Kind 1: PBR Manifest Codec ("BLMW")
 * ============================================================================ */

static const char *pbr_tex_path_at(const void *ctx, uint32_t i) {
    return ((const BloomPbrManifest *)ctx)->textures[i].path;
}

static const char *pbr_obj_id_at(const void *ctx, uint32_t i) {
    return ((const BloomPbrManifest *)ctx)->objects[i].id;
}

bool bloom_wire_encode_pbr_manifest(const BloomPbrManifest *m,
                                    uint8_t **out_buf, size_t *out_len) {
    if (!m || !out_buf || !out_len) return false;
    WireWriter ww = {0};
    ww_header(&ww, (uint8_t)BLOOM_WIRE_KIND_PBR_MANIFEST, 0u);
    ww_varu64(&ww, m->texture_cnt);
    ww_varu64(&ww, m->material_cnt);
    ww_varu64(&ww, m->object_cnt);

    if (m->texture_cnt > 0u) {
        if (!m->textures) { free(ww.data); return false; }
        put_front_strings_cb(&ww, m->texture_cnt, pbr_tex_path_at, m);
    }

    if (m->material_cnt > 0u) {
        if (!m->materials) { free(ww.data); return false; }
        uint32_t mc = m->material_cnt;
        size_t st = sizeof(BloomPbrMaterialDesc);
        for (int k = 0; k < 4; ++k) put_f32_planes(&ww, &m->materials[0].base_color_factor[k], mc, st);
        for (int k = 0; k < 3; ++k) put_f32_planes(&ww, &m->materials[0].emissive_factor[k], mc, st);
        put_f32_planes(&ww, &m->materials[0].metallic_factor, mc, st);
        put_f32_planes(&ww, &m->materials[0].roughness_factor, mc, st);
        put_f32_planes(&ww, &m->materials[0].normal_scale, mc, st);
        for (uint32_t i = 0u; i < mc; ++i) {
            uint8_t fl = (uint8_t)((m->materials[i].double_sided ? 1u : 0u) |
                                   ((m->materials[i].alpha_mode & 3u) << 1u));
            ww_u8(&ww, fl);
        }
        for (uint32_t i = 0u; i < mc; ++i) ww_varu64(&ww, bloom_wire_zz_enc(m->materials[i].base_color_tex));
        for (uint32_t i = 0u; i < mc; ++i) ww_varu64(&ww, bloom_wire_zz_enc(m->materials[i].mr_tex));
        for (uint32_t i = 0u; i < mc; ++i) ww_varu64(&ww, bloom_wire_zz_enc(m->materials[i].normal_tex));
        for (uint32_t i = 0u; i < mc; ++i) ww_varu64(&ww, bloom_wire_zz_enc(m->materials[i].emissive_tex));
        for (uint32_t i = 0u; i < mc; ++i) ww_varu64(&ww, bloom_wire_zz_enc(m->materials[i].ao_tex));
    }

    if (m->object_cnt > 0u) {
        if (!m->objects) { free(ww.data); return false; }
        put_front_strings_cb(&ww, m->object_cnt, pbr_obj_id_at, m);
        for (uint32_t i = 0u; i < m->object_cnt; ++i) {
            ww_varu64(&ww, bloom_wire_zz_enc(m->objects[i].material_index));
        }
    }

    if (ww.oom) {
        free(ww.data);
        return false;
    }
    *out_buf = ww.data;
    *out_len = ww.len;
    return true;
}

bool bloom_wire_decode_pbr_manifest(const uint8_t *buf, size_t len, BloomPbrManifest *out_m) {
    if (!buf || !out_m) return false;
    memset(out_m, 0, sizeof(*out_m));
    WireReader wr = {buf, len, 0u, false};
    if (!wr_check_header(&wr, (uint8_t)BLOOM_WIRE_KIND_PBR_MANIFEST, NULL)) return false;

    uint64_t tc64 = wr_varu64(&wr);
    uint64_t mc64 = wr_varu64(&wr);
    uint64_t oc64 = wr_varu64(&wr);
    if (wr.err || tc64 > UINT32_MAX || mc64 > UINT32_MAX || oc64 > UINT32_MAX) return false;
    uint32_t tc = (uint32_t)tc64;
    uint32_t mc = (uint32_t)mc64;
    uint32_t oc = (uint32_t)oc64;

    if (tc > wr.len - wr.pos || mc > wr.len - wr.pos || oc > wr.len - wr.pos) return false;

    out_m->texture_cnt = tc;
    out_m->material_cnt = mc;
    out_m->object_cnt = oc;

    if (tc > 0u) {
        char **tpaths = NULL;
        if (!get_front_strings_alloc(&wr, tc, &tpaths)) {
            bloom_pbr_manifest_free(out_m);
            return false;
        }
        out_m->textures = (BloomPbrTextureRef *)calloc(tc, sizeof(BloomPbrTextureRef));
        if (!out_m->textures) {
            for (uint32_t i = 0u; i < tc; ++i) free(tpaths[i]);
            free(tpaths);
            bloom_pbr_manifest_free(out_m);
            return false;
        }
        for (uint32_t i = 0u; i < tc; ++i) out_m->textures[i].path = tpaths[i];
        free(tpaths);
    }

    if (mc > 0u) {
        out_m->materials = (BloomPbrMaterialDesc *)calloc(mc, sizeof(BloomPbrMaterialDesc));
        if (!out_m->materials) {
            bloom_pbr_manifest_free(out_m);
            return false;
        }
        size_t st = sizeof(BloomPbrMaterialDesc);
        for (int k = 0; k < 4; ++k) get_f32_planes(&wr, &out_m->materials[0].base_color_factor[k], mc, st);
        for (int k = 0; k < 3; ++k) get_f32_planes(&wr, &out_m->materials[0].emissive_factor[k], mc, st);
        get_f32_planes(&wr, &out_m->materials[0].metallic_factor, mc, st);
        get_f32_planes(&wr, &out_m->materials[0].roughness_factor, mc, st);
        get_f32_planes(&wr, &out_m->materials[0].normal_scale, mc, st);
        for (uint32_t i = 0u; i < mc; ++i) {
            uint8_t fl = wr_u8(&wr);
            out_m->materials[i].double_sided = (uint8_t)(fl & 1u);
            out_m->materials[i].alpha_mode = (uint8_t)((fl >> 1u) & 3u);
        }
        for (uint32_t i = 0u; i < mc; ++i) out_m->materials[i].base_color_tex = (int32_t)bloom_wire_zz_dec(wr_varu64(&wr));
        for (uint32_t i = 0u; i < mc; ++i) out_m->materials[i].mr_tex = (int32_t)bloom_wire_zz_dec(wr_varu64(&wr));
        for (uint32_t i = 0u; i < mc; ++i) out_m->materials[i].normal_tex = (int32_t)bloom_wire_zz_dec(wr_varu64(&wr));
        for (uint32_t i = 0u; i < mc; ++i) out_m->materials[i].emissive_tex = (int32_t)bloom_wire_zz_dec(wr_varu64(&wr));
        for (uint32_t i = 0u; i < mc; ++i) out_m->materials[i].ao_tex = (int32_t)bloom_wire_zz_dec(wr_varu64(&wr));
    }

    if (oc > 0u) {
        char **oids = NULL;
        if (!get_front_strings_alloc(&wr, oc, &oids)) {
            bloom_pbr_manifest_free(out_m);
            return false;
        }
        out_m->objects = (BloomPbrObjectBinding *)calloc(oc, sizeof(BloomPbrObjectBinding));
        if (!out_m->objects) {
            for (uint32_t i = 0u; i < oc; ++i) free(oids[i]);
            free(oids);
            bloom_pbr_manifest_free(out_m);
            return false;
        }
        for (uint32_t i = 0u; i < oc; ++i) out_m->objects[i].id = oids[i];
        free(oids);
        for (uint32_t i = 0u; i < oc; ++i) {
            out_m->objects[i].material_index = (int32_t)bloom_wire_zz_dec(wr_varu64(&wr));
        }
    }

    if (wr.err) {
        bloom_pbr_manifest_free(out_m);
        return false;
    }
    return true;
}

static bool wire_read_entire_file(const char *path, uint8_t **out_buf, size_t *out_len) {
    if (!path || !out_buf || !out_len) return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    long sz = ftell(fp);
    if (sz < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return false; }
    uint8_t *buf = (uint8_t *)malloc((size_t)(sz ? sz : 1));
    if (!buf) { fclose(fp); return false; }
    size_t rd = sz > 0 ? fread(buf, 1u, (size_t)sz, fp) : 0u;
    fclose(fp);
    if (rd != (size_t)sz) { free(buf); return false; }
    *out_buf = buf;
    *out_len = rd;
    return true;
}

static bool wire_write_entire_file(const char *path, const uint8_t *buf, size_t len) {
    if (!path || (!buf && len > 0u)) return false;
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    size_t wr = len > 0u ? fwrite(buf, 1u, len, fp) : 0u;
    fclose(fp);
    return wr == len;
}

bool bloom_wire_load_pbr_manifest_file(const char *path, BloomPbrManifest *out_m) {
    uint8_t *buf = NULL;
    size_t len = 0u;
    if (!wire_read_entire_file(path, &buf, &len)) return false;
    bool ok = bloom_wire_decode_pbr_manifest(buf, len, out_m);
    free(buf);
    return ok;
}

bool bloom_wire_save_pbr_manifest_file(const char *path, const BloomPbrManifest *m) {
    uint8_t *buf = NULL;
    size_t len = 0u;
    if (!bloom_wire_encode_pbr_manifest(m, &buf, &len)) return false;
    bool ok = wire_write_entire_file(path, buf, len);
    free(buf);
    return ok;
}

void bloom_pbr_manifest_free(BloomPbrManifest *m) {
    if (!m) return;
    if (m->textures) {
        for (uint32_t i = 0u; i < m->texture_cnt; ++i) free(m->textures[i].path);
        free(m->textures);
    }
    free(m->materials);
    if (m->objects) {
        for (uint32_t i = 0u; i < m->object_cnt; ++i) free(m->objects[i].id);
        free(m->objects);
    }
    memset(m, 0, sizeof(*m));
}

BloomDigest128 bloom_pbr_manifest_digest128(const BloomPbrManifest *m) {
    BloomDigest128 d = {UINT64_C(0xCBF29CE484222325), UINT64_C(0x9E3779B97F4A7C15)};
    if (!m) return d;
    wire_digest_mix(&d, &m->texture_cnt, sizeof(m->texture_cnt));
    wire_digest_mix(&d, &m->material_cnt, sizeof(m->material_cnt));
    wire_digest_mix(&d, &m->object_cnt, sizeof(m->object_cnt));
    for (uint32_t i = 0u; i < m->texture_cnt; ++i) wire_digest_str(&d, m->textures[i].path);
    for (uint32_t i = 0u; i < m->material_cnt; ++i) {
        const BloomPbrMaterialDesc *bm = &m->materials[i];
        wire_digest_mix(&d, bm->base_color_factor, sizeof(bm->base_color_factor));
        wire_digest_mix(&d, bm->emissive_factor, sizeof(bm->emissive_factor));
        wire_digest_mix(&d, &bm->metallic_factor, sizeof(bm->metallic_factor));
        wire_digest_mix(&d, &bm->roughness_factor, sizeof(bm->roughness_factor));
        wire_digest_mix(&d, &bm->normal_scale, sizeof(bm->normal_scale));
        wire_digest_mix(&d, &bm->base_color_tex, sizeof(bm->base_color_tex));
        wire_digest_mix(&d, &bm->mr_tex, sizeof(bm->mr_tex));
        wire_digest_mix(&d, &bm->normal_tex, sizeof(bm->normal_tex));
        wire_digest_mix(&d, &bm->emissive_tex, sizeof(bm->emissive_tex));
        wire_digest_mix(&d, &bm->ao_tex, sizeof(bm->ao_tex));
        wire_digest_mix(&d, &bm->double_sided, sizeof(bm->double_sided));
        wire_digest_mix(&d, &bm->alpha_mode, sizeof(bm->alpha_mode));
    }
    for (uint32_t i = 0u; i < m->object_cnt; ++i) {
        wire_digest_str(&d, m->objects[i].id);
        wire_digest_mix(&d, &m->objects[i].material_index, sizeof(m->objects[i].material_index));
    }
    return d;
}

/* ============================================================================
 * Kind 2: Embodied SDK Snapshot Codec ("BLMW")
 * ============================================================================ */

static const char *sdk_entity_id_at(const void *ctx, uint32_t i) {
    return ((const BloomSdkSnapshot *)ctx)->entities[i].id;
}

static const char *sdk_entity_label_at(const void *ctx, uint32_t i) {
    return ((const BloomSdkSnapshot *)ctx)->entities[i].label;
}

bool bloom_wire_encode_sdk_snapshot(const BloomSdkSnapshot *snap,
                                    uint8_t **out_buf, size_t *out_len) {
    if (!snap || !out_buf || !out_len) return false;
    WireWriter ww = {0};
    ww_header(&ww, (uint8_t)BLOOM_WIRE_KIND_SDK_SNAPSHOT, 0u);
    ww_varu64(&ww, snap->vp_w);
    ww_varu64(&ww, snap->vp_h);
    uint64_t gms_bits = 0u;
    memcpy(&gms_bits, &snap->ground_ms, sizeof(gms_bits));
    ww_u64(&ww, gms_bits);
    put_f32_planes(&ww, snap->camera.pos, 3u, sizeof(float));
    put_f32_planes(&ww, &snap->camera.yaw, 1u, sizeof(float));
    put_f32_planes(&ww, &snap->camera.pitch, 1u, sizeof(float));
    put_f32_planes(&ww, &snap->camera.focal, 1u, sizeof(float));
    put_f32_planes(&ww, &snap->camera.near_z, 1u, sizeof(float));
    put_f32_planes(&ww, &snap->camera.far_z, 1u, sizeof(float));

    uint32_t n = snap->entity_cnt;
    ww_varu64(&ww, n);
    if (n > 0u) {
        if (!snap->entities) { free(ww.data); return false; }
        const BloomSdkEntity *es = snap->entities;
        size_t st = sizeof(BloomSdkEntity);

        for (uint32_t i = 0u; i < n; ++i) ww_u8(&ww, es[i].domain);
        int64_t prev_slot = 0;
        for (uint32_t i = 0u; i < n; ++i) {
            int64_t cur = (int64_t)es[i].slot;
            ww_varu64(&ww, bloom_wire_zz_enc(cur - prev_slot));
            prev_slot = cur;
        }
        for (uint32_t i = 0u; i < n; ++i) ww_u8(&ww, es[i].part_id);
        put_u32_lanes(&ww, &es[0].handle, n, st);
        put_u64_lanes(&ww, &es[0].key, n, st);
        put_front_strings_cb(&ww, n, sdk_entity_id_at, snap);
        put_front_strings_cb(&ww, n, sdk_entity_label_at, snap);

        for (uint32_t i = 0u; i < n; ++i) ww_varu64(&ww, (uint64_t)es[i].ui_kind);
        for (uint32_t i = 0u; i < n; ++i) {
            uint8_t bf = (uint8_t)(
                (es[i].disabled ? 1u : 0u) |
                (es[i].modal_trap ? 2u : 0u) |
                (es[i].focused ? 4u : 0u) |
                (es[i].hovered ? 8u : 0u) |
                (es[i].toggle_on ? 16u : 0u) |
                (es[i].occluded ? 32u : 0u) |
                (es[i].actionable ? 64u : 0u) |
                (es[i].cutaway ? 128u : 0u));
            ww_u8(&ww, bf);
        }
        put_f32_planes(&ww, &es[0].slider_t, n, st);
        put_f32_planes(&ww, &es[0].scroll_y, n, st);
        put_u32_lanes(&ww, &es[0].action_mask, n, st);
        for (int k = 0; k < 4; ++k) put_f32_planes(&ww, &es[0].layout_rect[k], n, st);
        for (uint32_t i = 0u; i < n; ++i) ww_varu64(&ww, es[i].visible_px);
        put_f32_planes(&ww, &es[0].visible_ratio, n, st);
        for (int k = 0; k < 4; ++k) {
            for (uint32_t i = 0u; i < n; ++i) ww_varu64(&ww, es[i].screen_bbox[k]);
        }
        put_f32_planes(&ww, &es[0].anchor_x, n, st);
        put_f32_planes(&ww, &es[0].anchor_y, n, st);
        put_f32_planes(&ww, &es[0].min_depth, n, st);
        for (int k = 0; k < 3; ++k) put_f32_planes(&ww, &es[0].world_pos[k], n, st);
        for (int k = 0; k < 3; ++k) put_f32_planes(&ww, &es[0].local_dof[k], n, st);
    }

    if (ww.oom) {
        free(ww.data);
        return false;
    }
    *out_buf = ww.data;
    *out_len = ww.len;
    return true;
}

static void wire_sdk_snapshot_reset(BloomSdkSnapshot *snap) {
    if (!snap) return;
    free(snap->entities);
    memset(snap, 0, sizeof(*snap));
}

bool bloom_wire_decode_sdk_snapshot(const uint8_t *buf, size_t len, BloomSdkSnapshot *out_snap) {
    if (!buf || !out_snap) return false;
    memset(out_snap, 0, sizeof(*out_snap));
    WireReader wr = {buf, len, 0u, false};
    if (!wr_check_header(&wr, (uint8_t)BLOOM_WIRE_KIND_SDK_SNAPSHOT, NULL)) return false;

    uint64_t vpw = wr_varu64(&wr);
    uint64_t vph = wr_varu64(&wr);
    if (wr.err || vpw > UINT32_MAX || vph > UINT32_MAX) return false;
    out_snap->vp_w = (uint32_t)vpw;
    out_snap->vp_h = (uint32_t)vph;
    uint64_t gms_bits = wr_u64(&wr);
    memcpy(&out_snap->ground_ms, &gms_bits, sizeof(gms_bits));
    get_f32_planes(&wr, out_snap->camera.pos, 3u, sizeof(float));
    get_f32_planes(&wr, &out_snap->camera.yaw, 1u, sizeof(float));
    get_f32_planes(&wr, &out_snap->camera.pitch, 1u, sizeof(float));
    get_f32_planes(&wr, &out_snap->camera.focal, 1u, sizeof(float));
    get_f32_planes(&wr, &out_snap->camera.near_z, 1u, sizeof(float));
    get_f32_planes(&wr, &out_snap->camera.far_z, 1u, sizeof(float));

    uint64_t n64 = wr_varu64(&wr);
    if (wr.err || n64 > UINT32_MAX || n64 > wr.len - wr.pos) return false;
    uint32_t n = (uint32_t)n64;
    out_snap->entity_cnt = n;
    if (n > 0u) {
        out_snap->entities = (BloomSdkEntity *)calloc(n, sizeof(BloomSdkEntity));
        if (!out_snap->entities) return false;
        BloomSdkEntity *es = out_snap->entities;
        size_t st = sizeof(BloomSdkEntity);

        for (uint32_t i = 0u; i < n; ++i) es[i].domain = wr_u8(&wr);
        int64_t prev_slot = 0;
        for (uint32_t i = 0u; i < n; ++i) {
            prev_slot += bloom_wire_zz_dec(wr_varu64(&wr));
            es[i].slot = (uint16_t)prev_slot;
        }
        for (uint32_t i = 0u; i < n; ++i) es[i].part_id = wr_u8(&wr);
        get_u32_lanes(&wr, &es[0].handle, n, st);
        get_u64_lanes(&wr, &es[0].key, n, st);

        char **ids = NULL;
        char **lbls = NULL;
        if (!get_front_strings_alloc(&wr, n, &ids) || !get_front_strings_alloc(&wr, n, &lbls)) {
            if (ids) { for (uint32_t i = 0u; i < n; ++i) free(ids[i]); free(ids); }
            if (lbls) { for (uint32_t i = 0u; i < n; ++i) free(lbls[i]); free(lbls); }
            wire_sdk_snapshot_reset(out_snap);
            return false;
        }
        for (uint32_t i = 0u; i < n; ++i) {
            snprintf(es[i].id, sizeof(es[i].id), "%s", ids[i] ? ids[i] : "");
            snprintf(es[i].label, sizeof(es[i].label), "%s", lbls[i] ? lbls[i] : "");
            free(ids[i]);
            free(lbls[i]);
        }
        free(ids);
        free(lbls);

        for (uint32_t i = 0u; i < n; ++i) es[i].ui_kind = (BloomUiKind)wr_varu64(&wr);
        for (uint32_t i = 0u; i < n; ++i) {
            uint8_t bf = wr_u8(&wr);
            es[i].disabled   = (bf & 1u) != 0u;
            es[i].modal_trap = (bf & 2u) != 0u;
            es[i].focused    = (bf & 4u) != 0u;
            es[i].hovered    = (bf & 8u) != 0u;
            es[i].toggle_on  = (bf & 16u) != 0u;
            es[i].occluded   = (bf & 32u) != 0u;
            es[i].actionable = (bf & 64u) != 0u;
            es[i].cutaway    = (bf & 128u) != 0u;
        }
        get_f32_planes(&wr, &es[0].slider_t, n, st);
        get_f32_planes(&wr, &es[0].scroll_y, n, st);
        get_u32_lanes(&wr, &es[0].action_mask, n, st);
        for (int k = 0; k < 4; ++k) get_f32_planes(&wr, &es[0].layout_rect[k], n, st);
        for (uint32_t i = 0u; i < n; ++i) es[i].visible_px = (uint32_t)wr_varu64(&wr);
        get_f32_planes(&wr, &es[0].visible_ratio, n, st);
        for (int k = 0; k < 4; ++k) {
            for (uint32_t i = 0u; i < n; ++i) es[i].screen_bbox[k] = (uint32_t)wr_varu64(&wr);
        }
        get_f32_planes(&wr, &es[0].anchor_x, n, st);
        get_f32_planes(&wr, &es[0].anchor_y, n, st);
        get_f32_planes(&wr, &es[0].min_depth, n, st);
        for (int k = 0; k < 3; ++k) get_f32_planes(&wr, &es[0].world_pos[k], n, st);
        for (int k = 0; k < 3; ++k) get_f32_planes(&wr, &es[0].local_dof[k], n, st);
    }

    if (wr.err) {
        wire_sdk_snapshot_reset(out_snap);
        return false;
    }
    return true;
}

BloomDigest128 bloom_sdk_snapshot_digest128(const BloomSdkSnapshot *snap) {
    BloomDigest128 d = {UINT64_C(0xCBF29CE484222325), UINT64_C(0x9E3779B97F4A7C15)};
    if (!snap) return d;
    wire_digest_mix(&d, &snap->camera, sizeof(snap->camera));
    wire_digest_mix(&d, &snap->vp_w, sizeof(snap->vp_w));
    wire_digest_mix(&d, &snap->vp_h, sizeof(snap->vp_h));
    wire_digest_mix(&d, &snap->ground_ms, sizeof(snap->ground_ms));
    wire_digest_mix(&d, &snap->entity_cnt, sizeof(snap->entity_cnt));
    for (uint32_t i = 0u; i < snap->entity_cnt; ++i) {
        const BloomSdkEntity *e = &snap->entities[i];
        wire_digest_mix(&d, &e->domain, sizeof(e->domain));
        wire_digest_mix(&d, &e->slot, sizeof(e->slot));
        wire_digest_mix(&d, &e->part_id, sizeof(e->part_id));
        wire_digest_mix(&d, &e->handle, sizeof(e->handle));
        wire_digest_mix(&d, &e->key, sizeof(e->key));
        wire_digest_str(&d, e->id);
        wire_digest_str(&d, e->label);
        wire_digest_mix(&d, &e->ui_kind, sizeof(e->ui_kind));
        uint8_t bf = (uint8_t)(
            (e->disabled ? 1u : 0u) | (e->modal_trap ? 2u : 0u) |
            (e->focused ? 4u : 0u) | (e->hovered ? 8u : 0u) |
            (e->toggle_on ? 16u : 0u) | (e->occluded ? 32u : 0u) |
            (e->actionable ? 64u : 0u) | (e->cutaway ? 128u : 0u));
        wire_digest_mix(&d, &bf, 1u);
        wire_digest_mix(&d, &e->slider_t, sizeof(e->slider_t));
        wire_digest_mix(&d, &e->scroll_y, sizeof(e->scroll_y));
        wire_digest_mix(&d, &e->action_mask, sizeof(e->action_mask));
        wire_digest_mix(&d, e->layout_rect, sizeof(e->layout_rect));
        wire_digest_mix(&d, &e->visible_px, sizeof(e->visible_px));
        wire_digest_mix(&d, &e->visible_ratio, sizeof(e->visible_ratio));
        wire_digest_mix(&d, e->screen_bbox, sizeof(e->screen_bbox));
        wire_digest_mix(&d, &e->anchor_x, sizeof(e->anchor_x));
        wire_digest_mix(&d, &e->anchor_y, sizeof(e->anchor_y));
        wire_digest_mix(&d, &e->min_depth, sizeof(e->min_depth));
        wire_digest_mix(&d, e->world_pos, sizeof(e->world_pos));
        wire_digest_mix(&d, e->local_dof, sizeof(e->local_dof));
    }
    return d;
}

/* ============================================================================
 * Kind 3: Benchmark / Telemetry Report Codec ("BLMW")
 * ============================================================================ */

static const char *bench_name_at(const void *ctx, uint32_t i) {
    return ((const BloomBenchReport *)ctx)->metrics[i].name;
}

static const char *bench_unit_at(const void *ctx, uint32_t i) {
    return ((const BloomBenchReport *)ctx)->metrics[i].unit;
}

bool bloom_wire_encode_bench_report(const BloomBenchReport *rep,
                                    uint8_t **out_buf, size_t *out_len) {
    if (!rep || !out_buf || !out_len) return false;
    WireWriter ww = {0};
    ww_header(&ww, (uint8_t)BLOOM_WIRE_KIND_BENCH_RECORD, 0u);
    const char *suite = rep->suite ? rep->suite : "";
    size_t slen = strlen(suite);
    ww_varu64(&ww, (uint64_t)slen);
    ww_bytes(&ww, suite, slen);
    ww_u64(&ww, rep->timestamp_ns);
    uint32_t n = rep->metric_cnt;
    ww_varu64(&ww, n);
    if (n > 0u) {
        if (!rep->metrics) { free(ww.data); return false; }
        size_t st = sizeof(BloomBenchMetric);
        put_front_strings_cb(&ww, n, bench_name_at, rep);
        put_front_strings_cb(&ww, n, bench_unit_at, rep);
        put_u64_lanes(&ww, (const uint64_t *)&rep->metrics[0].value, n, st);
        for (uint32_t i = 0u; i < n; ++i) ww_varu64(&ww, rep->metrics[i].bytes);
        put_u32_lanes(&ww, &rep->metrics[0].checksum, n, st);
    }
    if (ww.oom) {
        free(ww.data);
        return false;
    }
    *out_buf = ww.data;
    *out_len = ww.len;
    return true;
}

bool bloom_wire_decode_bench_report(const uint8_t *buf, size_t len, BloomBenchReport *out_rep) {
    if (!buf || !out_rep) return false;
    memset(out_rep, 0, sizeof(*out_rep));
    WireReader wr = {buf, len, 0u, false};
    if (!wr_check_header(&wr, (uint8_t)BLOOM_WIRE_KIND_BENCH_RECORD, NULL)) return false;

    uint64_t slen64 = wr_varu64(&wr);
    if (wr.err || slen64 > wr.len - wr.pos) return false;
    size_t slen = (size_t)slen64;
    out_rep->suite = (char *)malloc(slen + 1u);
    if (!out_rep->suite) return false;
    wr_bytes(&wr, out_rep->suite, slen);
    out_rep->suite[slen] = '\0';
    out_rep->timestamp_ns = wr_u64(&wr);

    uint64_t n64 = wr_varu64(&wr);
    if (wr.err || n64 > UINT32_MAX || n64 > wr.len - wr.pos) {
        bloom_bench_report_free(out_rep);
        return false;
    }
    uint32_t n = (uint32_t)n64;
    out_rep->metric_cnt = n;
    if (n > 0u) {
        char **names = NULL;
        char **units = NULL;
        if (!get_front_strings_alloc(&wr, n, &names) || !get_front_strings_alloc(&wr, n, &units)) {
            if (names) { for (uint32_t i = 0u; i < n; ++i) free(names[i]); free(names); }
            if (units) { for (uint32_t i = 0u; i < n; ++i) free(units[i]); free(units); }
            bloom_bench_report_free(out_rep);
            return false;
        }
        out_rep->metrics = (BloomBenchMetric *)calloc(n, sizeof(BloomBenchMetric));
        if (!out_rep->metrics) {
            for (uint32_t i = 0u; i < n; ++i) { free(names[i]); free(units[i]); }
            free(names); free(units);
            bloom_bench_report_free(out_rep);
            return false;
        }
        for (uint32_t i = 0u; i < n; ++i) {
            out_rep->metrics[i].name = names[i];
            out_rep->metrics[i].unit = units[i];
        }
        free(names);
        free(units);
        size_t st = sizeof(BloomBenchMetric);
        get_u64_lanes(&wr, (uint64_t *)&out_rep->metrics[0].value, n, st);
        for (uint32_t i = 0u; i < n; ++i) out_rep->metrics[i].bytes = wr_varu64(&wr);
        get_u32_lanes(&wr, &out_rep->metrics[0].checksum, n, st);
    }

    if (wr.err) {
        bloom_bench_report_free(out_rep);
        return false;
    }
    return true;
}

bool bloom_wire_save_bench_report_file(const char *path, const BloomBenchReport *rep) {
    uint8_t *buf = NULL;
    size_t len = 0u;
    if (!bloom_wire_encode_bench_report(rep, &buf, &len)) return false;
    bool ok = wire_write_entire_file(path, buf, len);
    free(buf);
    return ok;
}

bool bloom_wire_load_bench_report_file(const char *path, BloomBenchReport *out_rep) {
    uint8_t *buf = NULL;
    size_t len = 0u;
    if (!wire_read_entire_file(path, &buf, &len)) return false;
    bool ok = bloom_wire_decode_bench_report(buf, len, out_rep);
    free(buf);
    return ok;
}

void bloom_bench_report_free(BloomBenchReport *rep) {
    if (!rep) return;
    free(rep->suite);
    if (rep->metrics) {
        for (uint32_t i = 0u; i < rep->metric_cnt; ++i) {
            free(rep->metrics[i].name);
            free(rep->metrics[i].unit);
        }
        free(rep->metrics);
    }
    memset(rep, 0, sizeof(*rep));
}

BloomDigest128 bloom_bench_report_digest128(const BloomBenchReport *rep) {
    BloomDigest128 d = {UINT64_C(0xCBF29CE484222325), UINT64_C(0x9E3779B97F4A7C15)};
    if (!rep) return d;
    wire_digest_str(&d, rep->suite);
    wire_digest_mix(&d, &rep->timestamp_ns, sizeof(rep->timestamp_ns));
    wire_digest_mix(&d, &rep->metric_cnt, sizeof(rep->metric_cnt));
    for (uint32_t i = 0u; i < rep->metric_cnt; ++i) {
        const BloomBenchMetric *bm = &rep->metrics[i];
        wire_digest_str(&d, bm->name);
        wire_digest_str(&d, bm->unit);
        wire_digest_mix(&d, &bm->value, sizeof(bm->value));
        wire_digest_mix(&d, &bm->bytes, sizeof(bm->bytes));
        wire_digest_mix(&d, &bm->checksum, sizeof(bm->checksum));
    }
    return d;
}
