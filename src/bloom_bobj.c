#include "bloom_bobj.h"

#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Internal Growable Byte Writer & Bounded Reader (C17, Zero Dependencies)
 * ============================================================================ */

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    bool oom;
} BufWriter;

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    bool err;
} BufReader;

static bool bw_reserve(BufWriter *bw, size_t extra) {
    if (bw->oom) return false;
    if (bw->len + extra <= bw->cap) return true;
    size_t ncap = bw->cap ? bw->cap * 2u : 256u;
    while (ncap < bw->len + extra) ncap *= 2u;
    uint8_t *np = (uint8_t *)realloc(bw->data, ncap);
    if (!np) {
        bw->oom = true;
        return false;
    }
    bw->data = np;
    bw->cap = ncap;
    return true;
}

static void bw_u8(BufWriter *bw, uint8_t v) {
    if (bw_reserve(bw, 1u)) bw->data[bw->len++] = v;
}

static void bw_u16(BufWriter *bw, uint16_t v) {
    if (!bw_reserve(bw, 2u)) return;
    bw->data[bw->len++] = (uint8_t)(v & 0xFFu);
    bw->data[bw->len++] = (uint8_t)((v >> 8u) & 0xFFu);
}

static void bw_u32(BufWriter *bw, uint32_t v) {
    if (!bw_reserve(bw, 4u)) return;
    for (int i = 0; i < 4; ++i) bw->data[bw->len++] = (uint8_t)((v >> (i * 8)) & 0xFFu);
}

static void bw_bytes(BufWriter *bw, const void *src, size_t n) {
    if (n == 0u) return;
    if (bw_reserve(bw, n)) {
        memcpy(bw->data + bw->len, src, n);
        bw->len += n;
    }
}

static void bw_uvarint(BufWriter *bw, uint64_t v) {
    while (v >= 0x80u) {
        bw_u8(bw, (uint8_t)((v & 0x7Fu) | 0x80u));
        v >>= 7u;
    }
    bw_u8(bw, (uint8_t)v);
}

static inline uint64_t zz_enc(int64_t x) {
    return ((uint64_t)x << 1u) ^ (uint64_t)(x >> 63);
}

static inline int64_t zz_dec(uint64_t u) {
    return (int64_t)((u >> 1u) ^ (~(u & 1u) + 1u));
}

static uint8_t br_u8(BufReader *br) {
    if (br->err || br->pos + 1u > br->len) {
        br->err = true;
        return 0u;
    }
    return br->data[br->pos++];
}

static uint16_t br_u16(BufReader *br) {
    if (br->err || br->pos + 2u > br->len) {
        br->err = true;
        return 0u;
    }
    uint16_t v = (uint16_t)br->data[br->pos] | ((uint16_t)br->data[br->pos + 1u] << 8u);
    br->pos += 2u;
    return v;
}

static uint32_t br_u32(BufReader *br) {
    if (br->err || br->pos + 4u > br->len) {
        br->err = true;
        return 0u;
    }
    uint32_t v = 0u;
    memcpy(&v, br->data + br->pos, 4u);
    br->pos += 4u;
    return v;
}

static void br_bytes(BufReader *br, void *dst, size_t n) {
    if (n == 0u) return;
    if (br->err || br->pos + n > br->len) {
        br->err = true;
        return;
    }
    memcpy(dst, br->data + br->pos, n);
    br->pos += n;
}

static uint64_t br_uvarint(BufReader *br) {
    uint64_t v = 0u;
    uint32_t sh = 0u;
    while (!br->err && br->pos < br->len) {
        uint8_t b = br->data[br->pos++];
        v |= (uint64_t)(b & 0x7Fu) << sh;
        if ((b & 0x80u) == 0u) return v;
        sh += 7u;
        if (sh >= 64u) break;
    }
    br->err = true;
    return 0u;
}

static char *bloom_strdup_n(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1u);
    if (!p) return NULL;
    if (n > 0u && s) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ============================================================================
 * Shared Columnar & Front-Coding Helpers
 * ============================================================================ */

static void put_front_strings(BufWriter *bw, const char *const *strs, uint32_t count) {
    const char *prev = "";
    size_t prev_len = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        const char *cur = strs[i] ? strs[i] : "";
        size_t cur_len = strlen(cur);
        size_t lim = prev_len < cur_len ? prev_len : cur_len;
        size_t lcp = 0u;
        while (lcp < lim && prev[lcp] == cur[lcp]) ++lcp;
        size_t suf = cur_len - lcp;
        bw_uvarint(bw, lcp);
        bw_uvarint(bw, suf);
        bw_bytes(bw, cur + lcp, suf);
        prev = cur;
        prev_len = cur_len;
    }
}

static bool get_front_strings(BufReader *br, char **out_strs, uint32_t count) {
    char *prev = bloom_strdup_n("", 0u);
    size_t prev_len = 0u;
    if (!prev) return false;
    for (uint32_t i = 0u; i < count; ++i) {
        size_t lcp = (size_t)br_uvarint(br);
        size_t suf = (size_t)br_uvarint(br);
        if (br->err || lcp > prev_len || suf > br->len - br->pos) {
            free(prev);
            return false;
        }
        char *cur = (char *)malloc(lcp + suf + 1u);
        if (!cur) {
            free(prev);
            return false;
        }
        if (lcp > 0u) memcpy(cur, prev, lcp);
        br_bytes(br, cur + lcp, suf);
        cur[lcp + suf] = '\0';
        out_strs[i] = cur;
        free(prev);
        prev = bloom_strdup_n(cur, lcp + suf);
        prev_len = lcp + suf;
        if (!prev) return false;
    }
    free(prev);
    return !br->err;
}

static void put_f32_byte_planes(BufWriter *bw, const float *vals, uint32_t count, size_t stride_bytes) {
    const uint8_t *base = (const uint8_t *)vals;
    for (int lane = 3; lane >= 0; --lane) {
        for (uint32_t i = 0u; i < count; ++i) {
            uint32_t u = 0u;
            memcpy(&u, base + i * stride_bytes, 4u);
            bw_u8(bw, (uint8_t)((u >> (lane * 8)) & 0xFFu));
        }
    }
}

static void get_f32_byte_planes(BufReader *br, float *vals, uint32_t count, size_t stride_bytes) {
    if (count == 0u) return;
    uint32_t *raw = (uint32_t *)calloc(count, sizeof(uint32_t));
    if (!raw) { br->err = true; return; }
    for (int lane = 3; lane >= 0; --lane) {
        for (uint32_t i = 0u; i < count; ++i) {
            raw[i] |= (uint32_t)br_u8(br) << (lane * 8);
        }
    }
    uint8_t *base = (uint8_t *)vals;
    for (uint32_t i = 0u; i < count; ++i) {
        memcpy(base + i * stride_bytes, &raw[i], 4u);
    }
    free(raw);
}

static void put_u16_byte_lanes(BufWriter *bw, const uint16_t *vals, uint32_t count, size_t stride_bytes) {
    const uint8_t *base = (const uint8_t *)vals;
    for (uint32_t i = 0u; i < count; ++i) {
        uint16_t v = 0u;
        memcpy(&v, base + i * stride_bytes, 2u);
        bw_u8(bw, (uint8_t)((v >> 8u) & 0xFFu));
    }
    for (uint32_t i = 0u; i < count; ++i) {
        uint16_t v = 0u;
        memcpy(&v, base + i * stride_bytes, 2u);
        bw_u8(bw, (uint8_t)(v & 0xFFu));
    }
}

static void get_u16_byte_lanes(BufReader *br, uint16_t *vals, uint32_t count, size_t stride_bytes) {
    if (count == 0u) return;
    uint16_t *tmp = (uint16_t *)calloc(count, sizeof(uint16_t));
    if (!tmp) { br->err = true; return; }
    for (uint32_t i = 0u; i < count; ++i) tmp[i] = (uint16_t)br_u8(br) << 8u;
    for (uint32_t i = 0u; i < count; ++i) tmp[i] |= br_u8(br);
    uint8_t *base = (uint8_t *)vals;
    for (uint32_t i = 0u; i < count; ++i) {
        memcpy(base + i * stride_bytes, &tmp[i], 2u);
    }
    free(tmp);
}

static void put_u32_byte_lanes(BufWriter *bw, const uint32_t *vals, uint32_t count, size_t stride_bytes) {
    const uint8_t *base = (const uint8_t *)vals;
    for (int b = 0; b < 4; ++b) {
        for (uint32_t i = 0u; i < count; ++i) {
            uint32_t v = 0u;
            memcpy(&v, base + i * stride_bytes, 4u);
            bw_u8(bw, (uint8_t)((v >> (b * 8)) & 0xFFu));
        }
    }
}

static void get_u32_byte_lanes(BufReader *br, uint32_t *vals, uint32_t count, size_t stride_bytes) {
    if (count == 0u) return;
    uint32_t *tmp = (uint32_t *)calloc(count, sizeof(uint32_t));
    if (!tmp) { br->err = true; return; }
    for (int b = 0; b < 4; ++b) {
        for (uint32_t i = 0u; i < count; ++i) {
            tmp[i] |= (uint32_t)br_u8(br) << (b * 8);
        }
    }
    uint8_t *base = (uint8_t *)vals;
    for (uint32_t i = 0u; i < count; ++i) memcpy(base + i * stride_bytes, &tmp[i], 4u);
    free(tmp);
}

/* ============================================================================
 * Row-Major GPU-Direct BOBR Codec
 * ============================================================================ */

bool bloom_bobj_encode_row(const BloomBobjDoc *doc, bool include_geom,
                           uint8_t **out_buf, size_t *out_len) {
    if (!doc || !out_buf || !out_len) return false;
    BufWriter bw = {0};
    bw_u32(&bw, BLOOM_BOBJ_MAGIC_ROW);
    bw_u32(&bw, include_geom ? BLOOM_BOBJ_FLAG_GEOM : 0u);
    bw_u32(&bw, doc->template_cnt);
    bw_u32(&bw, doc->object_cnt);
    bw_u32(&bw, doc->analytic_cnt);
    bw_u32(&bw, doc->hud_cnt);

    for (uint32_t i = 0u; i < doc->template_cnt; ++i) {
        const BloomBobjTemplate *t = &doc->templates[i];
        size_t nl = t->name ? strlen(t->name) : 0u;
        uint32_t il = include_geom ? t->idx_bytes : 0u;
        bw_u16(&bw, (uint16_t)nl);
        bw_u32(&bw, t->meshlet_cnt);
        bw_u32(&bw, il);
        bw_bytes(&bw, t->name, nl);
        if (include_geom && t->meshlet_cnt > 0u) {
            bw_bytes(&bw, t->hdrs, (size_t)t->meshlet_cnt * sizeof(BloomMeshletHeader32));
            bw_bytes(&bw, t->verts, (size_t)t->meshlet_cnt * BLOOM_CLUSTER_VTX * sizeof(BloomVtx12));
            bw_bytes(&bw, t->local_idx, il);
        }
    }
    for (uint32_t i = 0u; i < doc->object_cnt; ++i) {
        bw_bytes(&bw, &doc->objects[i].state, sizeof(BloomObjectState48));
        bw_bytes(&bw, &doc->objects[i].cspec, sizeof(BloomConstraintSpec));
    }
    for (uint32_t i = 0u; i < doc->analytic_cnt; ++i) {
        bw_bytes(&bw, &doc->analytics[i].prim, sizeof(BloomAnalyticPrim32));
    }
    for (uint32_t i = 0u; i < doc->hud_cnt; ++i) {
        BloomHudNodeBin hb = doc->huds[i].bin;
        hb.id_len = (uint16_t)(doc->huds[i].id ? strlen(doc->huds[i].id) : 0u);
        hb.label_len = (uint16_t)(doc->huds[i].label ? strlen(doc->huds[i].label) : 0u);
        bw_bytes(&bw, &hb, sizeof(BloomHudNodeBin));
    }
    for (uint32_t i = 0u; i < doc->object_cnt; ++i) {
        size_t sl = doc->objects[i].id ? strlen(doc->objects[i].id) : 0u;
        bw_u16(&bw, (uint16_t)sl);
        bw_bytes(&bw, doc->objects[i].id, sl);
    }
    for (uint32_t i = 0u; i < doc->analytic_cnt; ++i) {
        size_t sl = doc->analytics[i].id ? strlen(doc->analytics[i].id) : 0u;
        bw_u16(&bw, (uint16_t)sl);
        bw_bytes(&bw, doc->analytics[i].id, sl);
    }
    for (uint32_t i = 0u; i < doc->hud_cnt; ++i) {
        size_t il = doc->huds[i].id ? strlen(doc->huds[i].id) : 0u;
        size_t ll = doc->huds[i].label ? strlen(doc->huds[i].label) : 0u;
        bw_bytes(&bw, doc->huds[i].id, il);
        bw_bytes(&bw, doc->huds[i].label, ll);
    }
    if (bw.oom) {
        free(bw.data);
        return false;
    }
    *out_buf = bw.data;
    *out_len = bw.len;
    return true;
}

bool bloom_bobj_decode_row(const uint8_t *buf, size_t len, BloomBobjDoc *out_doc) {
    if (!buf || !out_doc || len < 24u) return false;
    memset(out_doc, 0, sizeof(*out_doc));
    BufReader br = {buf, len, 0u, false};
    if (br_u32(&br) != BLOOM_BOBJ_MAGIC_ROW) return false;
    bool has_geom = (br_u32(&br) & BLOOM_BOBJ_FLAG_GEOM) != 0u;
    uint32_t tc = br_u32(&br), oc = br_u32(&br), ac = br_u32(&br), hc = br_u32(&br);

    out_doc->template_cnt = tc;
    out_doc->object_cnt = oc;
    out_doc->analytic_cnt = ac;
    out_doc->hud_cnt = hc;
    if (tc) out_doc->templates = (BloomBobjTemplate *)calloc(tc, sizeof(BloomBobjTemplate));
    if (oc) out_doc->objects = (BloomBobjObject *)calloc(oc, sizeof(BloomBobjObject));
    if (ac) out_doc->analytics = (BloomBobjAnalytic *)calloc(ac, sizeof(BloomBobjAnalytic));
    if (hc) out_doc->huds = (BloomBobjHud *)calloc(hc, sizeof(BloomBobjHud));
    if ((tc && !out_doc->templates) || (oc && !out_doc->objects) ||
        (ac && !out_doc->analytics) || (hc && !out_doc->huds)) {
        bloom_bobj_free(out_doc);
        return false;
    }

    for (uint32_t i = 0u; i < tc; ++i) {
        BloomBobjTemplate *t = &out_doc->templates[i];
        uint16_t nl = br_u16(&br);
        t->meshlet_cnt = br_u32(&br);
        t->idx_bytes = br_u32(&br);
        if (br.err || nl > br.len - br.pos) { bloom_bobj_free(out_doc); return false; }
        t->name = (char *)malloc((size_t)nl + 1u);
        if (!t->name) { bloom_bobj_free(out_doc); return false; }
        br_bytes(&br, t->name, nl);
        t->name[nl] = '\0';
        if (has_geom && t->meshlet_cnt > 0u) {
            size_t hb = (size_t)t->meshlet_cnt * sizeof(BloomMeshletHeader32);
            size_t vb = (size_t)t->meshlet_cnt * BLOOM_CLUSTER_VTX * sizeof(BloomVtx12);
            if (br.err || hb + vb + t->idx_bytes > br.len - br.pos) { bloom_bobj_free(out_doc); return false; }
            t->hdrs = (BloomMeshletHeader32 *)malloc(hb);
            t->verts = (BloomVtx12 *)malloc(vb);
            t->local_idx = t->idx_bytes ? (uint8_t *)malloc(t->idx_bytes) : NULL;
            if (!t->hdrs || !t->verts || (t->idx_bytes && !t->local_idx)) {
                bloom_bobj_free(out_doc);
                return false;
            }
            br_bytes(&br, t->hdrs, hb);
            br_bytes(&br, t->verts, vb);
            br_bytes(&br, t->local_idx, t->idx_bytes);
        }
    }
    for (uint32_t i = 0u; i < oc; ++i) {
        br_bytes(&br, &out_doc->objects[i].state, sizeof(BloomObjectState48));
        br_bytes(&br, &out_doc->objects[i].cspec, sizeof(BloomConstraintSpec));
    }
    for (uint32_t i = 0u; i < ac; ++i) {
        br_bytes(&br, &out_doc->analytics[i].prim, sizeof(BloomAnalyticPrim32));
    }
    for (uint32_t i = 0u; i < hc; ++i) {
        br_bytes(&br, &out_doc->huds[i].bin, sizeof(BloomHudNodeBin));
    }
    for (uint32_t i = 0u; i < oc; ++i) {
        uint16_t sl = br_u16(&br);
        if (br.err || sl > br.len - br.pos) { bloom_bobj_free(out_doc); return false; }
        out_doc->objects[i].id = (char *)malloc((size_t)sl + 1u);
        if (!out_doc->objects[i].id) { bloom_bobj_free(out_doc); return false; }
        br_bytes(&br, out_doc->objects[i].id, sl);
        out_doc->objects[i].id[sl] = '\0';
    }
    for (uint32_t i = 0u; i < ac; ++i) {
        uint16_t sl = br_u16(&br);
        if (br.err || sl > br.len - br.pos) { bloom_bobj_free(out_doc); return false; }
        out_doc->analytics[i].id = (char *)malloc((size_t)sl + 1u);
        if (!out_doc->analytics[i].id) { bloom_bobj_free(out_doc); return false; }
        br_bytes(&br, out_doc->analytics[i].id, sl);
        out_doc->analytics[i].id[sl] = '\0';
    }
    for (uint32_t i = 0u; i < hc; ++i) {
        uint16_t il = out_doc->huds[i].bin.id_len;
        uint16_t ll = out_doc->huds[i].bin.label_len;
        if (br.err || (size_t)il + ll > br.len - br.pos) { bloom_bobj_free(out_doc); return false; }
        out_doc->huds[i].id = (char *)malloc((size_t)il + 1u);
        out_doc->huds[i].label = (char *)malloc((size_t)ll + 1u);
        if (!out_doc->huds[i].id || !out_doc->huds[i].label) { bloom_bobj_free(out_doc); return false; }
        br_bytes(&br, out_doc->huds[i].id, il);
        out_doc->huds[i].id[il] = '\0';
        br_bytes(&br, out_doc->huds[i].label, ll);
        out_doc->huds[i].label[ll] = '\0';
    }
    if (br.err) {
        bloom_bobj_free(out_doc);
        return false;
    }
    return true;
}

/* ============================================================================
 * Columnar + Delta-Varint + Byte-Plane BOBC Codec (INFO MDL Winner)
 * ============================================================================ */

bool bloom_bobj_encode_col(const BloomBobjDoc *doc, bool include_geom,
                           uint8_t **out_buf, size_t *out_len) {
    if (!doc || !out_buf || !out_len) return false;
    BufWriter bw = {0};
    bw_u32(&bw, BLOOM_BOBJ_MAGIC_COL);
    bw_u32(&bw, include_geom ? BLOOM_BOBJ_FLAG_GEOM : 0u);
    bw_u32(&bw, doc->template_cnt);
    bw_u32(&bw, doc->object_cnt);
    bw_u32(&bw, doc->analytic_cnt);
    bw_u32(&bw, doc->hud_cnt);

    const char **tnames = doc->template_cnt ? (const char **)malloc(doc->template_cnt * sizeof(char *)) : NULL;
    for (uint32_t i = 0u; i < doc->template_cnt; ++i) {
        tnames[i] = doc->templates[i].name;
        bw_uvarint(&bw, doc->templates[i].meshlet_cnt);
        bw_uvarint(&bw, include_geom ? doc->templates[i].idx_bytes : 0u);
    }
    if (doc->template_cnt) {
        put_front_strings(&bw, tnames, doc->template_cnt);
        free(tnames);
    }

    if (include_geom) {
        for (uint32_t ti = 0u; ti < doc->template_cnt; ++ti) {
            const BloomBobjTemplate *t = &doc->templates[ti];
            uint32_t mc = t->meshlet_cnt;
            int64_t pv = 0, pi = 0, po = 0;
            for (uint32_t m = 0u; m < mc; ++m) {
                const BloomMeshletHeader32 *h = &t->hdrs[m];
                bw_uvarint(&bw, zz_enc((int64_t)h->vtx_offset - pv)); pv = h->vtx_offset;
                bw_uvarint(&bw, zz_enc((int64_t)h->idx_offset - pi)); pi = h->idx_offset;
                bw_uvarint(&bw, h->vtx_cnt);
                bw_uvarint(&bw, h->tri_cnt);
                bw_uvarint(&bw, zz_enc((int64_t)h->obj_id - po)); po = h->obj_id;
                bw_uvarint(&bw, h->flags);
                bw_uvarint(&bw, h->aabb_scale_tag);
            }
            if (mc > 0u) {
                for (int k = 0; k < 4; ++k) {
                    put_u16_byte_lanes(&bw, &t->hdrs[0].bsphere_f16[k], mc, sizeof(BloomMeshletHeader32));
                }
                for (int k = 0; k < 3; ++k) {
                    for (uint32_t m = 0u; m < mc; ++m) bw_u8(&bw, (uint8_t)t->hdrs[m].cone_axis[k]);
                }
                for (uint32_t m = 0u; m < mc; ++m) bw_u8(&bw, (uint8_t)t->hdrs[m].cone_cutoff);
            }
            uint32_t nv = mc * BLOOM_CLUSTER_VTX;
            for (int k = 0; k < 3; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0u; v < nv; ++v) {
                    int32_t cur = t->verts[v].pos_unorm16[k];
                    bw_uvarint(&bw, zz_enc(cur - prev));
                    prev = cur;
                }
            }
            for (int k = 0; k < 2; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0u; v < nv; ++v) {
                    int32_t cur = t->verts[v].oct_norm[k];
                    bw_uvarint(&bw, zz_enc(cur - prev));
                    prev = cur;
                }
            }
            for (int k = 0; k < 2; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0u; v < nv; ++v) {
                    int32_t cur = t->verts[v].uv_u8[k];
                    bw_uvarint(&bw, zz_enc(cur - prev));
                    prev = cur;
                }
            }
            for (uint32_t v = 0u; v < nv; ++v) bw_u8(&bw, t->verts[v].joint_id);
            for (uint32_t v = 0u; v < nv; ++v) bw_u8(&bw, t->verts[v].mat_id);
            int32_t pidx = 0;
            for (uint32_t k = 0u; k < t->idx_bytes; ++k) {
                int32_t cur = t->local_idx[k];
                bw_uvarint(&bw, zz_enc(cur - pidx));
                pidx = cur;
            }
        }
    }

    uint32_t oc = doc->object_cnt;
    if (oc > 0u) {
        size_t st = sizeof(BloomBobjObject);
        for (int k = 0; k < 3; ++k) put_f32_byte_planes(&bw, &doc->objects[0].state.pos[k], oc, st);
        for (int k = 0; k < 4; ++k) put_u16_byte_lanes(&bw, &doc->objects[0].state.quat_f16[k], oc, st);
        for (int k = 0; k < 3; ++k) put_u16_byte_lanes(&bw, &doc->objects[0].state.scale_f16[k], oc, st);
        for (int k = 0; k < 3; ++k) put_u16_byte_lanes(&bw, &doc->objects[0].state.dof_f16[k], oc, st);
        for (int k = 0; k < 4; ++k) put_u16_byte_lanes(&bw, &doc->objects[0].state.clip_plane_f16[k], oc, st);
        put_u32_byte_lanes(&bw, &doc->objects[0].state.rgba8_mat, oc, st);
        for (uint32_t i = 0u; i < oc; ++i) bw_uvarint(&bw, doc->objects[i].state.flags);
        for (int k = 0; k < 3; ++k) put_u16_byte_lanes(&bw, &doc->objects[0].cspec.dof_min_f16[k], oc, st);
        for (int k = 0; k < 3; ++k) put_u16_byte_lanes(&bw, &doc->objects[0].cspec.dof_max_f16[k], oc, st);
        for (uint32_t i = 0u; i < oc; ++i) bw_uvarint(&bw, doc->objects[i].cspec.template_id);
        for (uint32_t i = 0u; i < oc; ++i) bw_uvarint(&bw, doc->objects[i].cspec.joint_mask);
    }

    uint32_t ac = doc->analytic_cnt;
    if (ac > 0u) {
        size_t st = sizeof(BloomBobjAnalytic);
        for (int k = 0; k < 3; ++k) put_f32_byte_planes(&bw, &doc->analytics[0].prim.center[k], ac, st);
        put_f32_byte_planes(&bw, &doc->analytics[0].prim.radius, ac, st);
        for (int k = 0; k < 4; ++k) put_u16_byte_lanes(&bw, &doc->analytics[0].prim.axis_len_f16[k], ac, st);
        put_u32_byte_lanes(&bw, &doc->analytics[0].prim.rgba8, ac, st);
        for (uint32_t i = 0u; i < ac; ++i) bw_uvarint(&bw, doc->analytics[i].prim.prim_kind_flags);
    }

    uint32_t hc = doc->hud_cnt;
    if (hc > 0u) {
        size_t st = sizeof(BloomBobjHud);
        for (uint32_t i = 0u; i < hc; ++i) bw_u8(&bw, doc->huds[i].bin.kind);
        for (int k = 0; k < 4; ++k) {
            int32_t prev = 0;
            for (uint32_t i = 0u; i < hc; ++i) {
                int32_t cur = doc->huds[i].bin.bounds_q14_2[k];
                bw_uvarint(&bw, zz_enc(cur - prev));
                prev = cur;
            }
        }
        put_u32_byte_lanes(&bw, &doc->huds[0].bin.fill_rgba8, hc, st);
        put_u32_byte_lanes(&bw, &doc->huds[0].bin.border_rgba8, hc, st);
        for (uint32_t i = 0u; i < hc; ++i) bw_u8(&bw, doc->huds[i].bin.radius_q6_2);
        int32_t prev_b = 0;
        for (uint32_t i = 0u; i < hc; ++i) {
            int32_t cur = doc->huds[i].bin.bind_obj_idx;
            bw_uvarint(&bw, zz_enc(cur - prev_b));
            prev_b = cur;
        }
        for (uint32_t i = 0u; i < hc; ++i) bw_u8(&bw, doc->huds[i].bin.bind_dof_slot);
    }

    if (oc > 0u) {
        const char **ids = (const char **)malloc(oc * sizeof(char *));
        for (uint32_t i = 0u; i < oc; ++i) ids[i] = doc->objects[i].id;
        put_front_strings(&bw, ids, oc);
        free(ids);
    }
    if (ac > 0u) {
        const char **ids = (const char **)malloc(ac * sizeof(char *));
        for (uint32_t i = 0u; i < ac; ++i) ids[i] = doc->analytics[i].id;
        put_front_strings(&bw, ids, ac);
        free(ids);
    }
    if (hc > 0u) {
        const char **ids = (const char **)malloc(hc * sizeof(char *));
        const char **lbls = (const char **)malloc(hc * sizeof(char *));
        for (uint32_t i = 0u; i < hc; ++i) {
            ids[i] = doc->huds[i].id;
            lbls[i] = doc->huds[i].label;
        }
        put_front_strings(&bw, ids, hc);
        put_front_strings(&bw, lbls, hc);
        free(ids);
        free(lbls);
    }

    if (bw.oom) {
        free(bw.data);
        return false;
    }
    *out_buf = bw.data;
    *out_len = bw.len;
    return true;
}

bool bloom_bobj_decode_col(const uint8_t *buf, size_t len, BloomBobjDoc *out_doc) {
    if (!buf || !out_doc || len < 24u) return false;
    memset(out_doc, 0, sizeof(*out_doc));
    BufReader br = {buf, len, 0u, false};
    if (br_u32(&br) != BLOOM_BOBJ_MAGIC_COL) return false;
    bool has_geom = (br_u32(&br) & BLOOM_BOBJ_FLAG_GEOM) != 0u;
    uint32_t tc = br_u32(&br), oc = br_u32(&br), ac = br_u32(&br), hc = br_u32(&br);

    out_doc->template_cnt = tc;
    out_doc->object_cnt = oc;
    out_doc->analytic_cnt = ac;
    out_doc->hud_cnt = hc;
    if (tc) out_doc->templates = (BloomBobjTemplate *)calloc(tc, sizeof(BloomBobjTemplate));
    if (oc) out_doc->objects = (BloomBobjObject *)calloc(oc, sizeof(BloomBobjObject));
    if (ac) out_doc->analytics = (BloomBobjAnalytic *)calloc(ac, sizeof(BloomBobjAnalytic));
    if (hc) out_doc->huds = (BloomBobjHud *)calloc(hc, sizeof(BloomBobjHud));

    for (uint32_t i = 0u; i < tc; ++i) {
        out_doc->templates[i].meshlet_cnt = (uint32_t)br_uvarint(&br);
        out_doc->templates[i].idx_bytes = (uint32_t)br_uvarint(&br);
    }
    if (tc > 0u) {
        char **tnames = (char **)calloc(tc, sizeof(char *));
        if (!tnames || !get_front_strings(&br, tnames, tc)) {
            free(tnames);
            bloom_bobj_free(out_doc);
            return false;
        }
        for (uint32_t i = 0u; i < tc; ++i) out_doc->templates[i].name = tnames[i];
        free(tnames);
    }

    if (has_geom) {
        for (uint32_t ti = 0u; ti < tc; ++ti) {
            BloomBobjTemplate *t = &out_doc->templates[ti];
            uint32_t mc = t->meshlet_cnt;
            if (mc > 0u) {
                t->hdrs = (BloomMeshletHeader32 *)calloc(mc, sizeof(BloomMeshletHeader32));
                t->verts = (BloomVtx12 *)calloc((size_t)mc * BLOOM_CLUSTER_VTX, sizeof(BloomVtx12));
                if (!t->hdrs || !t->verts) { bloom_bobj_free(out_doc); return false; }
            }
            int64_t pv = 0, pi = 0, po = 0;
            for (uint32_t m = 0u; m < mc; ++m) {
                pv += zz_dec(br_uvarint(&br)); t->hdrs[m].vtx_offset = (uint32_t)pv;
                pi += zz_dec(br_uvarint(&br)); t->hdrs[m].idx_offset = (uint32_t)pi;
                t->hdrs[m].vtx_cnt = (uint16_t)br_uvarint(&br);
                t->hdrs[m].tri_cnt = (uint16_t)br_uvarint(&br);
                po += zz_dec(br_uvarint(&br)); t->hdrs[m].obj_id = (uint16_t)po;
                t->hdrs[m].flags = (uint16_t)br_uvarint(&br);
                t->hdrs[m].aabb_scale_tag = (uint32_t)br_uvarint(&br);
            }
            if (mc > 0u) {
                for (int k = 0; k < 4; ++k) {
                    get_u16_byte_lanes(&br, &t->hdrs[0].bsphere_f16[k], mc, sizeof(BloomMeshletHeader32));
                }
                for (int k = 0; k < 3; ++k) {
                    for (uint32_t m = 0u; m < mc; ++m) t->hdrs[m].cone_axis[k] = (int8_t)br_u8(&br);
                }
                for (uint32_t m = 0u; m < mc; ++m) t->hdrs[m].cone_cutoff = (int8_t)br_u8(&br);
            }
            uint32_t nv = mc * BLOOM_CLUSTER_VTX;
            for (int k = 0; k < 3; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0u; v < nv; ++v) {
                    prev += (int32_t)zz_dec(br_uvarint(&br));
                    t->verts[v].pos_unorm16[k] = (uint16_t)prev;
                }
            }
            for (int k = 0; k < 2; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0u; v < nv; ++v) {
                    prev += (int32_t)zz_dec(br_uvarint(&br));
                    t->verts[v].oct_norm[k] = (int8_t)prev;
                }
            }
            for (int k = 0; k < 2; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0u; v < nv; ++v) {
                    prev += (int32_t)zz_dec(br_uvarint(&br));
                    t->verts[v].uv_u8[k] = (uint8_t)prev;
                }
            }
            for (uint32_t v = 0u; v < nv; ++v) t->verts[v].joint_id = br_u8(&br);
            for (uint32_t v = 0u; v < nv; ++v) t->verts[v].mat_id = br_u8(&br);
            if (t->idx_bytes > 0u) {
                t->local_idx = (uint8_t *)malloc(t->idx_bytes);
                if (!t->local_idx) { bloom_bobj_free(out_doc); return false; }
                int32_t pidx = 0;
                for (uint32_t k = 0u; k < t->idx_bytes; ++k) {
                    pidx += (int32_t)zz_dec(br_uvarint(&br));
                    t->local_idx[k] = (uint8_t)pidx;
                }
            }
        }
    }

    if (oc > 0u) {
        size_t st = sizeof(BloomBobjObject);
        for (int k = 0; k < 3; ++k) get_f32_byte_planes(&br, &out_doc->objects[0].state.pos[k], oc, st);
        for (int k = 0; k < 4; ++k) get_u16_byte_lanes(&br, &out_doc->objects[0].state.quat_f16[k], oc, st);
        for (int k = 0; k < 3; ++k) get_u16_byte_lanes(&br, &out_doc->objects[0].state.scale_f16[k], oc, st);
        for (int k = 0; k < 3; ++k) get_u16_byte_lanes(&br, &out_doc->objects[0].state.dof_f16[k], oc, st);
        for (int k = 0; k < 4; ++k) get_u16_byte_lanes(&br, &out_doc->objects[0].state.clip_plane_f16[k], oc, st);
        get_u32_byte_lanes(&br, &out_doc->objects[0].state.rgba8_mat, oc, st);
        for (uint32_t i = 0u; i < oc; ++i) out_doc->objects[i].state.flags = (uint32_t)br_uvarint(&br);
        for (int k = 0; k < 3; ++k) get_u16_byte_lanes(&br, &out_doc->objects[0].cspec.dof_min_f16[k], oc, st);
        for (int k = 0; k < 3; ++k) get_u16_byte_lanes(&br, &out_doc->objects[0].cspec.dof_max_f16[k], oc, st);
        for (uint32_t i = 0u; i < oc; ++i) out_doc->objects[i].cspec.template_id = (uint16_t)br_uvarint(&br);
        for (uint32_t i = 0u; i < oc; ++i) out_doc->objects[i].cspec.joint_mask = (uint16_t)br_uvarint(&br);
    }

    if (ac > 0u) {
        size_t st = sizeof(BloomBobjAnalytic);
        for (int k = 0; k < 3; ++k) get_f32_byte_planes(&br, &out_doc->analytics[0].prim.center[k], ac, st);
        get_f32_byte_planes(&br, &out_doc->analytics[0].prim.radius, ac, st);
        for (int k = 0; k < 4; ++k) get_u16_byte_lanes(&br, &out_doc->analytics[0].prim.axis_len_f16[k], ac, st);
        get_u32_byte_lanes(&br, &out_doc->analytics[0].prim.rgba8, ac, st);
        for (uint32_t i = 0u; i < ac; ++i) out_doc->analytics[i].prim.prim_kind_flags = (uint32_t)br_uvarint(&br);
    }

    if (hc > 0u) {
        size_t st = sizeof(BloomBobjHud);
        for (uint32_t i = 0u; i < hc; ++i) out_doc->huds[i].bin.kind = br_u8(&br);
        for (int k = 0; k < 4; ++k) {
            int32_t prev = 0;
            for (uint32_t i = 0u; i < hc; ++i) {
                prev += (int32_t)zz_dec(br_uvarint(&br));
                out_doc->huds[i].bin.bounds_q14_2[k] = (uint16_t)prev;
            }
        }
        get_u32_byte_lanes(&br, &out_doc->huds[0].bin.fill_rgba8, hc, st);
        get_u32_byte_lanes(&br, &out_doc->huds[0].bin.border_rgba8, hc, st);
        for (uint32_t i = 0u; i < hc; ++i) out_doc->huds[i].bin.radius_q6_2 = br_u8(&br);
        int32_t prev_b = 0;
        for (uint32_t i = 0u; i < hc; ++i) {
            prev_b += (int32_t)zz_dec(br_uvarint(&br));
            out_doc->huds[i].bin.bind_obj_idx = (uint16_t)prev_b;
        }
        for (uint32_t i = 0u; i < hc; ++i) out_doc->huds[i].bin.bind_dof_slot = br_u8(&br);
    }

    if (oc > 0u) {
        char **oids = (char **)calloc(oc, sizeof(char *));
        if (!oids || !get_front_strings(&br, oids, oc)) { free(oids); bloom_bobj_free(out_doc); return false; }
        for (uint32_t i = 0u; i < oc; ++i) out_doc->objects[i].id = oids[i];
        free(oids);
    }
    if (ac > 0u) {
        char **aids = (char **)calloc(ac, sizeof(char *));
        if (!aids || !get_front_strings(&br, aids, ac)) { free(aids); bloom_bobj_free(out_doc); return false; }
        for (uint32_t i = 0u; i < ac; ++i) out_doc->analytics[i].id = aids[i];
        free(aids);
    }
    if (hc > 0u) {
        char **hids = (char **)calloc(hc, sizeof(char *));
        char **hlbls = (char **)calloc(hc, sizeof(char *));
        if (!hids || !hlbls || !get_front_strings(&br, hids, hc) || !get_front_strings(&br, hlbls, hc)) {
            free(hids); free(hlbls); bloom_bobj_free(out_doc); return false;
        }
        for (uint32_t i = 0u; i < hc; ++i) {
            out_doc->huds[i].id = hids[i];
            out_doc->huds[i].label = hlbls[i];
            out_doc->huds[i].bin.id_len = (uint16_t)strlen(hids[i]);
            out_doc->huds[i].bin.label_len = (uint16_t)strlen(hlbls[i]);
        }
        free(hids);
        free(hlbls);
    }
    if (br.err) {
        bloom_bobj_free(out_doc);
        return false;
    }
    return true;
}

bool bloom_bobj_decode_auto(const uint8_t *buf, size_t len, BloomBobjDoc *out_doc) {
    if (!buf || len < 4u) return false;
    uint32_t magic = 0u;
    memcpy(&magic, buf, 4u);
    if (magic == BLOOM_BOBJ_MAGIC_ROW) return bloom_bobj_decode_row(buf, len, out_doc);
    if (magic == BLOOM_BOBJ_MAGIC_COL) return bloom_bobj_decode_col(buf, len, out_doc);
    return false;
}

bool bloom_bobj_load_file(const char *path, BloomBobjDoc *out_doc) {
    if (!path || !out_doc) return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    long sz = ftell(fp);
    if (sz < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return false; }
    uint8_t *buf = (uint8_t *)malloc((size_t)(sz ? sz : 1));
    if (!buf) { fclose(fp); return false; }
    size_t rd = sz > 0 ? fread(buf, 1u, (size_t)sz, fp) : 0u;
    fclose(fp);
    bool ok = (rd == (size_t)sz) && bloom_bobj_decode_auto(buf, rd, out_doc);
    free(buf);
    return ok;
}

void bloom_bobj_free(BloomBobjDoc *doc) {
    if (!doc) return;
    for (uint32_t i = 0u; i < doc->template_cnt; ++i) {
        free(doc->templates[i].name);
        free(doc->templates[i].hdrs);
        free(doc->templates[i].verts);
        free(doc->templates[i].local_idx);
    }
    free(doc->templates);
    for (uint32_t i = 0u; i < doc->object_cnt; ++i) free(doc->objects[i].id);
    free(doc->objects);
    for (uint32_t i = 0u; i < doc->analytic_cnt; ++i) free(doc->analytics[i].id);
    free(doc->analytics);
    for (uint32_t i = 0u; i < doc->hud_cnt; ++i) {
        free(doc->huds[i].id);
        free(doc->huds[i].label);
    }
    free(doc->huds);
    memset(doc, 0, sizeof(*doc));
}

static void digest_mix(BloomDigest128 *d, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0u; i < n; ++i) {
        d->lo ^= b[i];
        d->lo *= UINT64_C(0x100000001B3);
        d->hi ^= (uint64_t)b[i] + UINT64_C(0x517CC1B727220A95);
        d->hi = (d->hi << 13u) | (d->hi >> 51u);
        d->hi *= UINT64_C(0x94D049BB133111EB);
    }
}

static void digest_str(BloomDigest128 *d, const char *s) {
    uint32_t n = s ? (uint32_t)strlen(s) : 0u;
    digest_mix(d, &n, sizeof(n));
    if (n > 0u) digest_mix(d, s, n);
}

BloomDigest128 bloom_bobj_digest128(const BloomBobjDoc *doc, bool include_geom) {
    BloomDigest128 d = {UINT64_C(0xCBF29CE484222325), UINT64_C(0x9E3779B97F4A7C15)};
    if (!doc) return d;
    digest_mix(&d, &doc->template_cnt, 4u);
    for (uint32_t i = 0u; i < doc->template_cnt; ++i) {
        const BloomBobjTemplate *t = &doc->templates[i];
        digest_str(&d, t->name);
        digest_mix(&d, &t->meshlet_cnt, 4u);
        if (include_geom && t->meshlet_cnt > 0u) {
            digest_mix(&d, t->hdrs, (size_t)t->meshlet_cnt * sizeof(BloomMeshletHeader32));
            digest_mix(&d, t->verts, (size_t)t->meshlet_cnt * BLOOM_CLUSTER_VTX * sizeof(BloomVtx12));
            digest_mix(&d, &t->idx_bytes, 4u);
            digest_mix(&d, t->local_idx, t->idx_bytes);
        }
    }
    digest_mix(&d, &doc->object_cnt, 4u);
    for (uint32_t i = 0u; i < doc->object_cnt; ++i) {
        digest_str(&d, doc->objects[i].id);
        digest_mix(&d, &doc->objects[i].state, sizeof(BloomObjectState48));
        digest_mix(&d, &doc->objects[i].cspec, sizeof(BloomConstraintSpec));
    }
    digest_mix(&d, &doc->analytic_cnt, 4u);
    for (uint32_t i = 0u; i < doc->analytic_cnt; ++i) {
        digest_str(&d, doc->analytics[i].id);
        digest_mix(&d, &doc->analytics[i].prim, sizeof(BloomAnalyticPrim32));
    }
    digest_mix(&d, &doc->hud_cnt, 4u);
    for (uint32_t i = 0u; i < doc->hud_cnt; ++i) {
        const BloomBobjHud *h = &doc->huds[i];
        digest_str(&d, h->id);
        digest_mix(&d, &h->bin.kind, 1u);
        digest_mix(&d, h->bin.bounds_q14_2, sizeof(h->bin.bounds_q14_2));
        digest_mix(&d, &h->bin.fill_rgba8, 4u);
        digest_mix(&d, &h->bin.border_rgba8, 4u);
        digest_mix(&d, &h->bin.radius_q6_2, 1u);
        digest_mix(&d, &h->bin.bind_obj_idx, 2u);
        digest_mix(&d, &h->bin.bind_dof_slot, 1u);
        digest_str(&d, h->label);
    }
    return d;
}
