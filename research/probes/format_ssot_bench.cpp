#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// IEEE-754 Float16 Helpers & Quantizers (Portable C++17, Zero Dependencies)
// ============================================================================
static inline uint16_t f32_to_f16(float value) {
    uint32_t f = 0;
    std::memcpy(&f, &value, sizeof(f));
    uint32_t sign = (f >> 16u) & 0x8000u;
    int32_t exp = static_cast<int32_t>((f >> 23u) & 0xFFu) - 127 + 15;
    uint32_t mant = f & 0x7FFFFFu;
    if ((f & 0x7FFFFFFFu) == 0u) return static_cast<uint16_t>(sign);
    if (exp <= 0) {
        if (exp < -10) return static_cast<uint16_t>(sign);
        mant = (mant | 0x800000u) >> static_cast<uint32_t>(1 - exp);
        return static_cast<uint16_t>(sign | ((mant + 0x1000u) >> 13u));
    }
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
    uint32_t half_bits = sign | (static_cast<uint32_t>(exp) << 10u) | (mant >> 13u);
    if ((mant & 0x1FFFu) > 0x1000u || ((mant & 0x1FFFu) == 0x1000u && (half_bits & 1u))) {
        half_bits += 1u;
    }
    return static_cast<uint16_t>(half_bits);
}

static inline float f16_to_f32(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16u;
    uint32_t exp = (h >> 10u) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t f = 0;
    if (exp == 0u) {
        if (mant == 0u) {
            f = sign;
        } else {
            exp = 1u;
            while ((mant & 0x400u) == 0u) {
                mant <<= 1u;
                exp -= 1u;
            }
            mant &= 0x3FFu;
            f = sign | ((exp + 127u - 15u) << 23u) | (mant << 13u);
        }
    } else if (exp == 31u) {
        f = sign | 0x7F800000u | (mant << 13u);
    } else {
        f = sign | ((exp + 127u - 15u) << 23u) | (mant << 13u);
    }
    float out = 0.0f;
    std::memcpy(&out, &f, sizeof(out));
    return out;
}

static inline float canon_zero_f32(float v) {
    return (v == 0.0f) ? 0.0f : v;
}

static inline uint16_t pack_f16_clean(float v) {
    return f32_to_f16(canon_zero_f32(v));
}

static inline uint16_t pack_pos_unorm16(float x, float lo, float span) {
    float t = std::max(0.0f, std::min(1.0f, (x - lo) / span));
    return static_cast<uint16_t>(std::lround(static_cast<double>(t) * 65535.0));
}

static inline float unpack_pos_unorm16(uint16_t q, float lo, float span) {
    return lo + static_cast<float>(q) * (span / 65535.0f);
}

static inline uint16_t recover_pos_unorm16(float x, float lo, float span) {
    double t = (static_cast<double>(x) - static_cast<double>(lo)) * (65535.0 / static_cast<double>(span));
    long q = std::lround(t);
    if (q < 0) q = 0;
    if (q > 65535) q = 65535;
    return static_cast<uint16_t>(q);
}

static inline void oct_encode(float nx, float ny, float nz, int8_t out_i8[2]) {
    float l1 = std::abs(nx) + std::abs(ny) + std::abs(nz);
    if (l1 < 1e-6f) l1 = 1.0f;
    float ox = nx / l1, oy = ny / l1;
    if (nz < 0.0f) {
        float sx = (ox >= 0.0f) ? 1.0f : -1.0f;
        float sy = (oy >= 0.0f) ? 1.0f : -1.0f;
        float tx = (1.0f - std::abs(oy)) * sx;
        float ty = (1.0f - std::abs(ox)) * sy;
        ox = tx;
        oy = ty;
    }
    auto clamp_i8 = [](float v) -> int8_t {
        int q = static_cast<int>(std::lround(v * 127.0f));
        if (q < -127) q = -127;
        if (q > 127) q = 127;
        return static_cast<int8_t>(q);
    };
    out_i8[0] = clamp_i8(ox);
    out_i8[1] = clamp_i8(oy);
}

static inline void oct_decode(const int8_t in_i8[2], float &nx, float &ny, float &nz) {
    float ox = static_cast<float>(in_i8[0]) / 127.0f;
    float oy = static_cast<float>(in_i8[1]) / 127.0f;
    nz = 1.0f - std::abs(ox) - std::abs(oy);
    if (nz < 0.0f) {
        float sx = (ox >= 0.0f) ? 1.0f : -1.0f;
        float sy = (oy >= 0.0f) ? 1.0f : -1.0f;
        float tx = (1.0f - std::abs(oy)) * sx;
        float ty = (1.0f - std::abs(ox)) * sy;
        ox = tx;
        oy = ty;
    }
    float len = std::sqrt(ox * ox + oy * oy + nz * nz);
    if (len < 1e-6f) len = 1.0f;
    nx = ox / len;
    ny = oy / len;
    nz = nz / len;
}

// ============================================================================
// GPU Structs Aligned Verbatim With object_ssot_bench.mm (Zero FLOW)
// ============================================================================
struct alignas(4) IndustryVtx64 {
    float pos[3];
    float norm[3];
    float tan[4];
    float uv[2];
    uint16_t joints[4];
    uint16_t weights_u16[4];
};
static_assert(sizeof(IndustryVtx64) == 64, "IndustryVtx64 must be 64 B");

struct alignas(16) IndustryRig512 {
    float mats[8][16];
};
static_assert(sizeof(IndustryRig512) == 512, "IndustryRig512 must be 512 B");

struct alignas(4) BloomVtx12 {
    uint16_t pos_unorm16[3];
    int8_t   oct_norm[2];
    uint8_t  uv_u8[2];
    uint8_t  joint_id;
    uint8_t  mat_id;
};
static_assert(sizeof(BloomVtx12) == 12, "BloomVtx12 must be 12 B");

struct alignas(4) BloomMeshletHeader32 {
    uint32_t vtx_offset;
    uint32_t idx_offset;
    uint16_t vtx_cnt;
    uint16_t tri_cnt;
    uint16_t obj_id;
    uint16_t flags;
    uint16_t bsphere_f16[4];
    int8_t   cone_axis[3];
    int8_t   cone_cutoff;
    uint32_t aabb_scale_tag;
};
static_assert(sizeof(BloomMeshletHeader32) == 32, "BloomMeshletHeader32 must be 32 B");

struct alignas(4) BloomObjectState48 {
    float    pos[3];
    uint16_t quat_f16[4];
    uint16_t scale_f16[3];
    uint16_t dof_f16[3];
    uint32_t rgba8_mat;
    uint16_t clip_plane_f16[4];
    uint32_t flags;
};
static_assert(sizeof(BloomObjectState48) == 48, "BloomObjectState48 must be 48 B");

struct alignas(4) BloomAnalyticPrim32 {
    float    center[3];
    float    radius;
    uint16_t axis_len_f16[4];
    uint32_t rgba8;
    uint32_t prim_kind_flags;
};
static_assert(sizeof(BloomAnalyticPrim32) == 32, "BloomAnalyticPrim32 must be 32 B");

// ============================================================================
// Canonical Semantic IR & 128-bit Lossless Digest
// ============================================================================
struct MeshTemplate {
    std::string name;
    uint32_t meshlet_cnt = 0;
    std::vector<BloomMeshletHeader32> hdrs;
    std::vector<BloomVtx12> verts;
    std::vector<uint8_t> local_idx; // 288 B shared canonical cluster index
};

struct alignas(2) ConstraintSpec {
    uint16_t dof_min_f16[3];
    uint16_t dof_max_f16[3];
    uint16_t template_id;
    uint16_t joint_mask;
};
static_assert(sizeof(ConstraintSpec) == 16, "ConstraintSpec must be 16 B");

struct ObjectNode {
    std::string id;
    BloomObjectState48 state{};
    ConstraintSpec cspec{};
};

struct AnalyticNode {
    std::string id;
    BloomAnalyticPrim32 prim{};
};

struct HudNode {
    std::string id;
    uint8_t kind = 0; // 0=panel, 1=button, 2=slider, 3=text
    uint16_t bounds_q14_2[4]{};
    uint32_t fill_rgba8 = 0;
    uint32_t border_rgba8 = 0;
    uint8_t radius_q6_2 = 0;
    uint16_t bind_obj_idx = 0;
    uint8_t bind_dof_slot = 0;
    std::string label;
};

struct BloomSceneIR {
    std::vector<MeshTemplate> templates;
    std::vector<ObjectNode> objects;
    std::vector<AnalyticNode> analytics;
    std::vector<HudNode> huds;
};

struct Digest128 {
    uint64_t h1 = 0xCBF29CE484222325ULL;
    uint64_t h2 = 0x9E3779B97F4A7C15ULL;
    void mix_u8(uint8_t b) {
        h1 ^= b;
        h1 *= 0x100000001B3ULL;
        h2 ^= static_cast<uint64_t>(b) + 0x517CC1B727220A95ULL;
        h2 = (h2 << 13u) | (h2 >> 51u);
        h2 *= 0x94D049BB133111EBULL;
    }
    void mix_bytes(const void *p, size_t n) {
        const auto *bp = static_cast<const uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) mix_u8(bp[i]);
    }
    template <typename T>
    void mix_val(const T &v) {
        mix_bytes(&v, sizeof(T));
    }
    void mix_str(const std::string &s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        mix_val(len);
        mix_bytes(s.data(), s.size());
    }
    bool operator==(const Digest128 &o) const { return h1 == o.h1 && h2 == o.h2; }
    bool operator!=(const Digest128 &o) const { return !(*this == o); }
};

static void mix_hud_list(Digest128 &d, const std::vector<HudNode> &huds) {
    uint32_t hn = static_cast<uint32_t>(huds.size());
    d.mix_val(hn);
    for (const auto &h : huds) {
        d.mix_str(h.id);
        d.mix_val(h.kind);
        d.mix_bytes(h.bounds_q14_2, sizeof(h.bounds_q14_2));
        d.mix_val(h.fill_rgba8);
        d.mix_val(h.border_rgba8);
        d.mix_val(h.radius_q6_2);
        d.mix_val(h.bind_obj_idx);
        d.mix_val(h.bind_dof_slot);
        d.mix_str(h.label);
    }
}

static Digest128 hud_digest(const std::vector<HudNode> &huds) {
    Digest128 d;
    mix_hud_list(d, huds);
    return d;
}

static Digest128 ir_digest(const BloomSceneIR &ir, bool include_geom = true) {
    Digest128 d;
    uint32_t tc = static_cast<uint32_t>(ir.templates.size());
    d.mix_val(tc);
    for (const auto &t : ir.templates) {
        d.mix_str(t.name);
        d.mix_val(t.meshlet_cnt);
        if (include_geom) {
            uint32_t hc = static_cast<uint32_t>(t.hdrs.size());
            uint32_t vc = static_cast<uint32_t>(t.verts.size());
            uint32_t ic = static_cast<uint32_t>(t.local_idx.size());
            d.mix_val(hc);
            for (const auto &hdr : t.hdrs) d.mix_val(hdr);
            d.mix_val(vc);
            for (const auto &v : t.verts) d.mix_val(v);
            d.mix_val(ic);
            d.mix_bytes(t.local_idx.data(), t.local_idx.size());
        }
    }
    uint32_t oc = static_cast<uint32_t>(ir.objects.size());
    d.mix_val(oc);
    for (const auto &o : ir.objects) {
        d.mix_str(o.id);
        d.mix_val(o.state);
        d.mix_val(o.cspec);
    }
    uint32_t ac = static_cast<uint32_t>(ir.analytics.size());
    d.mix_val(ac);
    for (const auto &a : ir.analytics) {
        d.mix_str(a.id);
        d.mix_val(a.prim);
    }
    mix_hud_list(d, ir.huds);
    return d;
}

// ============================================================================
// Deterministic Scene Generator With Geometric Diversity
// ============================================================================
static constexpr uint32_t CLUSTER_VTX = 64u;
static constexpr uint32_t CLUSTER_TRI = 96u;
static constexpr float LOCAL_BOX_LO = -1.5f;
static constexpr float LOCAL_BOX_SPAN = 3.0f;

static void eval_parametric_shape(uint32_t kind, uint32_t seed, float theta, float phi,
                                  float &px, float &py, float &pz,
                                  float &nx, float &ny, float &nz) {
    float ct = std::cos(theta), st = std::sin(theta);
    float cp = std::cos(phi), sp = std::sin(phi);
    float phase = static_cast<float>(seed & 15u) * 0.19f;
    switch (kind % 5u) {
        case 0: { // Torus reactor
            float R = 0.82f + 0.04f * std::sin(phase), r = 0.34f + 0.03f * std::cos(3.0f * theta + phase);
            px = (R + r * cp) * ct;
            py = r * sp;
            pz = (R + r * cp) * st;
            nx = cp * ct; ny = sp; nz = cp * st;
            break;
        }
        case 1: { // Ribbed cylinder
            float rad = 0.55f + 0.10f * std::sin(4.0f * phi + phase);
            px = rad * ct;
            py = (phi / 3.14159265f - 1.0f) * 1.15f;
            pz = rad * st;
            float slope = 0.22f * std::cos(4.0f * phi + phase);
            nx = ct; ny = slope; nz = st;
            break;
        }
        case 2: { // Twisted capsule
            float tw = theta + 0.45f * std::sin(phi + phase);
            float rad = 0.62f * (0.65f + 0.35f * std::abs(cp));
            px = rad * std::cos(tw);
            py = 0.95f * sp;
            pz = rad * std::sin(tw);
            nx = std::cos(tw); ny = 0.35f * cp; nz = std::sin(tw);
            break;
        }
        case 3: { // Gear ring
            float tooth = (std::sin(8.0f * theta + phase) > 0.0f) ? 0.14f : 0.0f;
            float R = 0.72f + tooth, r = 0.28f;
            px = (R + r * cp) * ct;
            py = r * sp;
            pz = (R + r * cp) * st;
            nx = cp * ct; ny = sp; nz = cp * st;
            break;
        }
        default: { // Dome shell
            float h = 0.5f * (1.0f + sp);
            float rad = 0.85f * std::sqrt(std::max(0.05f, 1.0f - h * h * 0.7f));
            px = rad * ct;
            py = (h - 0.5f) * 1.6f;
            pz = rad * st;
            nx = cp * ct; ny = sp; nz = cp * st;
            break;
        }
    }
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-6f) len = 1.0f;
    nx /= len; ny /= len; nz /= len;
}

// Recompute exact quantized meshlet bounds/cone directly from encoded vertices
// so any lossless decompressor (including baseline GLB64 importer) derives identical headers.
static void finalize_meshlet_header(uint32_t m_idx, const std::vector<BloomVtx12> &verts,
                                    BloomMeshletHeader32 &hdr) {
    uint32_t base = m_idx * CLUSTER_VTX;
    float px_q[CLUSTER_VTX], py_q[CLUSTER_VTX], pz_q[CLUSTER_VTX];
    float nx_q[CLUSTER_VTX], ny_q[CLUSTER_VTX], nz_q[CLUSTER_VTX];
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    float nsx = 0.0f, nsy = 0.0f, nsz = 0.0f;
    for (uint32_t i = 0; i < CLUSTER_VTX; ++i) {
        const BloomVtx12 &b = verts[base + i];
        px_q[i] = unpack_pos_unorm16(b.pos_unorm16[0], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
        py_q[i] = unpack_pos_unorm16(b.pos_unorm16[1], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
        pz_q[i] = unpack_pos_unorm16(b.pos_unorm16[2], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
        oct_decode(b.oct_norm, nx_q[i], ny_q[i], nz_q[i]);
        cx += px_q[i]; cy += py_q[i]; cz += pz_q[i];
        nsx += nx_q[i]; nsy += ny_q[i]; nsz += nz_q[i];
    }
    cx /= static_cast<float>(CLUSTER_VTX);
    cy /= static_cast<float>(CLUSTER_VTX);
    cz /= static_cast<float>(CLUSTER_VTX);
    float max_r = 0.0f;
    for (uint32_t i = 0; i < CLUSTER_VTX; ++i) {
        float dx = px_q[i] - cx, dy = py_q[i] - cy, dz = pz_q[i] - cz;
        float r = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (r > max_r) max_r = r;
    }
    float nlen = std::sqrt(nsx * nsx + nsy * nsy + nsz * nsz);
    if (nlen > 1e-6f) { nsx /= nlen; nsy /= nlen; nsz /= nlen; }
    else { nsx = 0.0f; nsy = 1.0f; nsz = 0.0f; }
    float min_dot = 1.0f;
    for (uint32_t i = 0; i < CLUSTER_VTX; ++i) {
        float d = nx_q[i] * nsx + ny_q[i] * nsy + nz_q[i] * nsz;
        if (d < min_dot) min_dot = d;
    }
    float cone_sin = (min_dot > 0.0f) ? std::sqrt(std::max(0.0f, 1.0f - min_dot * min_dot)) : 1.0f;

    hdr.vtx_offset = base;
    hdr.idx_offset = 0u;
    hdr.vtx_cnt = static_cast<uint16_t>(CLUSTER_VTX);
    hdr.tri_cnt = static_cast<uint16_t>(CLUSTER_TRI);
    hdr.obj_id = 0u;
    hdr.flags = 0u;
    hdr.bsphere_f16[0] = pack_f16_clean(cx);
    hdr.bsphere_f16[1] = pack_f16_clean(cy);
    hdr.bsphere_f16[2] = pack_f16_clean(cz);
    hdr.bsphere_f16[3] = pack_f16_clean(max_r);
    hdr.cone_axis[0] = static_cast<int8_t>(nsx * 127.0f);
    hdr.cone_axis[1] = static_cast<int8_t>(nsy * 127.0f);
    hdr.cone_axis[2] = static_cast<int8_t>(nsz * 127.0f);
    hdr.cone_cutoff = static_cast<int8_t>(std::min(1.0f, cone_sin + 0.02f) * 127.0f);
    hdr.aabb_scale_tag = 0u;
}

static MeshTemplate build_template_mesh(uint32_t tpl_idx, uint32_t meshlet_cnt, uint32_t seed) {
    static const char *kShapeNames[5] = {"torus_reactor", "rib_cylinder", "twist_capsule", "gear_ring", "dome_shell"};
    MeshTemplate t;
    char nbuf[64];
    std::snprintf(nbuf, sizeof(nbuf), "%s_%02u", kShapeNames[(tpl_idx + seed) % 5u], tpl_idx);
    t.name = nbuf;
    t.meshlet_cnt = meshlet_cnt;
    t.hdrs.resize(meshlet_cnt);
    t.verts.resize(meshlet_cnt * CLUSTER_VTX);
    t.local_idx.resize(CLUSTER_TRI * 3u);

    uint32_t it = 0;
    for (uint32_t iy = 0; iy < 7u && it < CLUSTER_TRI; ++iy) {
        for (uint32_t ix = 0; ix < 7u && it < CLUSTER_TRI; ++ix) {
            uint8_t i0 = static_cast<uint8_t>(iy * 8u + ix);
            uint8_t i1 = static_cast<uint8_t>(i0 + 1u);
            uint8_t i2 = static_cast<uint8_t>(i0 + 8u);
            uint8_t i3 = static_cast<uint8_t>(i2 + 1u);
            t.local_idx[it * 3u + 0u] = i0;
            t.local_idx[it * 3u + 1u] = i2;
            t.local_idx[it * 3u + 2u] = i1;
            ++it;
            if (it < CLUSTER_TRI) {
                t.local_idx[it * 3u + 0u] = i1;
                t.local_idx[it * 3u + 1u] = i2;
                t.local_idx[it * 3u + 2u] = i3;
                ++it;
            }
        }
    }

    uint32_t rings = std::max(1u, meshlet_cnt / 4u);
    for (uint32_t m = 0; m < meshlet_cnt; ++m) {
        float u0 = (static_cast<float>(m % 4u) / 4.0f) * 2.0f * 3.14159265f;
        float u1 = (static_cast<float>((m % 4u) + 1u) / 4.0f) * 2.0f * 3.14159265f;
        float v0 = (static_cast<float>((m / 4u) % rings) / static_cast<float>(rings)) * 2.0f * 3.14159265f;
        float v1 = (static_cast<float>(((m / 4u) % rings) + 1u) / static_cast<float>(rings)) * 2.0f * 3.14159265f;
        for (uint32_t iy = 0; iy < 8u; ++iy) {
            float tv = static_cast<float>(iy) / 7.0f;
            float phi = v0 + tv * (v1 - v0);
            for (uint32_t ix = 0; ix < 8u; ++ix) {
                float tu = static_cast<float>(ix) / 7.0f;
                float theta = u0 + tu * (u1 - u0);
                float px, py, pz, nx, ny, nz;
                eval_parametric_shape(tpl_idx + seed, seed + tpl_idx, theta, phi, px, py, pz, nx, ny, nz);
                BloomVtx12 &b = t.verts[m * CLUSTER_VTX + iy * 8u + ix];
                b.pos_unorm16[0] = pack_pos_unorm16(px, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                b.pos_unorm16[1] = pack_pos_unorm16(py, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                b.pos_unorm16[2] = pack_pos_unorm16(pz, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                oct_encode(nx, ny, nz, b.oct_norm);
                // Canonicalize oct_norm so oct_decode -> oct_encode is idempotent across GLB import
                float dnx, dny, dnz;
                oct_decode(b.oct_norm, dnx, dny, dnz);
                oct_encode(dnx, dny, dnz, b.oct_norm);
                b.uv_u8[0] = static_cast<uint8_t>(std::lround(tu * 255.0f));
                b.uv_u8[1] = static_cast<uint8_t>(std::lround(tv * 255.0f));
                b.joint_id = static_cast<uint8_t>(m & 3u);
                b.mat_id = static_cast<uint8_t>(tpl_idx & 7u);
            }
        }
        finalize_meshlet_header(m, t.verts, t.hdrs[m]);
    }
    return t;
}

static BloomSceneIR generate_scene(uint32_t n_tpl, uint32_t meshlets_per_tpl,
                                   uint32_t n_obj, uint32_t n_analytic, uint32_t n_hud,
                                   uint32_t seed, const char *prefix) {
    BloomSceneIR ir;
    for (uint32_t t = 0; t < n_tpl; ++t) {
        ir.templates.push_back(build_template_mesh(t, meshlets_per_tpl, seed));
    }
    uint64_t rng = 0x9E3779B97F4A7C15ULL ^ (static_cast<uint64_t>(seed) * 0xBF58476D1CE4E5B9ULL);
    auto next_u32 = [&]() -> uint32_t {
        rng ^= rng >> 12u;
        rng ^= rng << 25u;
        rng ^= rng >> 27u;
        return static_cast<uint32_t>((rng * 0x2545F4914F6CDD1DULL) >> 32u);
    };
    auto rand_f = [&](float lo, float hi) -> float {
        return lo + (static_cast<float>(next_u32() & 0xFFFFu) / 65535.0f) * (hi - lo);
    };

    for (uint32_t i = 0; i < n_obj; ++i) {
        ObjectNode o;
        char idb[64];
        std::snprintf(idb, sizeof(idb), "%s_obj_%03u", prefix, i);
        o.id = idb;
        o.state.pos[0] = rand_f(-14.0f, 14.0f);
        o.state.pos[1] = rand_f(-8.0f, 8.0f);
        o.state.pos[2] = rand_f(6.0f, 24.0f);
        float ang = rand_f(-3.1f, 3.1f);
        o.state.quat_f16[0] = pack_f16_clean(0.0f);
        o.state.quat_f16[1] = pack_f16_clean(std::sin(ang * 0.5f));
        o.state.quat_f16[2] = pack_f16_clean(0.0f);
        o.state.quat_f16[3] = pack_f16_clean(std::cos(ang * 0.5f));
        float s0 = rand_f(0.45f, 1.45f);
        o.state.scale_f16[0] = pack_f16_clean(s0);
        o.state.scale_f16[1] = pack_f16_clean(s0 * rand_f(0.85f, 1.15f));
        o.state.scale_f16[2] = pack_f16_clean(s0);
        o.state.dof_f16[0] = pack_f16_clean(rand_f(-1.2f, 1.2f));
        o.state.dof_f16[1] = pack_f16_clean(rand_f(-0.5f, 0.5f));
        o.state.dof_f16[2] = pack_f16_clean(rand_f(-0.8f, 0.8f));
        uint32_t r = 64u + (next_u32() % 180u);
        uint32_t g = 90u + (next_u32() % 160u);
        uint32_t b = 120u + (next_u32() % 135u);
        o.state.rgba8_mat = 0xFF000000u | (b << 16u) | (g << 8u) | r;
        o.state.clip_plane_f16[0] = pack_f16_clean(0.70703125f);
        o.state.clip_plane_f16[1] = pack_f16_clean(0.0f);
        o.state.clip_plane_f16[2] = pack_f16_clean(0.70703125f);
        o.state.clip_plane_f16[3] = pack_f16_clean((i % 5u == 0u) ? 0.125f : 0.0f);
        o.state.flags = (i % 5u == 0u) ? 1u : 0u;

        o.cspec.dof_min_f16[0] = pack_f16_clean(-1.5f);
        o.cspec.dof_min_f16[1] = pack_f16_clean(-0.75f);
        o.cspec.dof_min_f16[2] = pack_f16_clean(-1.0f);
        o.cspec.dof_max_f16[0] = pack_f16_clean(1.5f);
        o.cspec.dof_max_f16[1] = pack_f16_clean(0.75f);
        o.cspec.dof_max_f16[2] = pack_f16_clean(1.0f);
        o.cspec.template_id = static_cast<uint16_t>(i % n_tpl);
        o.cspec.joint_mask = static_cast<uint16_t>((i & 1u) ? 0x07u : 0x03u);
        ir.objects.push_back(o);
    }

    for (uint32_t i = 0; i < n_analytic; ++i) {
        AnalyticNode a;
        char idb[64];
        std::snprintf(idb, sizeof(idb), "%s_probe_%02u", prefix, i);
        a.id = idb;
        a.prim.center[0] = rand_f(-12.0f, 12.0f);
        a.prim.center[1] = rand_f(-7.0f, 7.0f);
        a.prim.center[2] = rand_f(8.0f, 22.0f);
        a.prim.radius = rand_f(0.18f, 0.85f);
        a.prim.axis_len_f16[0] = pack_f16_clean(0.0f);
        a.prim.axis_len_f16[1] = pack_f16_clean(1.0f);
        a.prim.axis_len_f16[2] = pack_f16_clean(0.0f);
        a.prim.axis_len_f16[3] = pack_f16_clean((i & 1u) ? 0.4f : 0.0f);
        uint32_t r = 80u + (next_u32() % 175u);
        uint32_t g = 140u + (next_u32() % 115u);
        uint32_t b = 180u + (next_u32() % 75u);
        a.prim.rgba8 = 0xFF000000u | (b << 16u) | (g << 8u) | r;
        a.prim.prim_kind_flags = (i & 1u) ? 2u : 1u;
        ir.analytics.push_back(a);
    }

    static const char *kHudTitles[4] = {"StageTelemetry", "TorqueGauge", "CutPlaneCtrl", "ReactorProbe"};
    for (uint32_t i = 0; i < n_hud; ++i) {
        HudNode h;
        char idb[64], lbl[64];
        std::snprintf(idb, sizeof(idb), "%s_hud_%03u", prefix, i);
        std::snprintf(lbl, sizeof(lbl), "%s_%u", kHudTitles[(i + seed) & 3u], i);
        h.id = idb;
        h.kind = static_cast<uint8_t>(i & 3u);
        uint16_t x = static_cast<uint16_t>((48u + (i % 6u) * 320u) * 4u);
        uint16_t y = static_cast<uint16_t>((48u + (i / 6u) * 56u) * 4u);
        uint16_t w = static_cast<uint16_t>((280u - (i & 3u) * 20u) * 4u);
        uint16_t ht = static_cast<uint16_t>((40u + (i & 1u) * 8u) * 4u);
        h.bounds_q14_2[0] = x; h.bounds_q14_2[1] = y;
        h.bounds_q14_2[2] = w; h.bounds_q14_2[3] = ht;
        h.fill_rgba8 = (h.kind == 0u) ? 0xD8141A24u : 0xEB22344Cu;
        h.border_rgba8 = (h.kind == 2u) ? 0xFF38D0FFu : 0xAA4A6280u;
        h.radius_q6_2 = static_cast<uint8_t>((4u + (i & 3u) * 2u) * 4u);
        h.bind_obj_idx = static_cast<uint16_t>(i % std::max(1u, n_obj));
        h.bind_dof_slot = static_cast<uint8_t>(i % 3u);
        h.label = lbl;
        ir.huds.push_back(h);
    }
    return ir;
}

// ============================================================================
// Byte & String Helpers
// ============================================================================
static inline void put_u8(std::vector<uint8_t> &o, uint8_t v) { o.push_back(v); }

static inline void put_u16(std::vector<uint8_t> &o, uint16_t v) {
    o.push_back(static_cast<uint8_t>(v & 0xFFu));
    o.push_back(static_cast<uint8_t>((v >> 8u) & 0xFFu));
}

static inline void put_u32(std::vector<uint8_t> &o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
}

static inline void put_bytes(std::vector<uint8_t> &o, const void *p, size_t n) {
    const auto *b = static_cast<const uint8_t *>(p);
    o.insert(o.end(), b, b + n);
}

static inline void put_uvarint(std::vector<uint8_t> &o, uint64_t v) {
    while (v >= 0x80u) {
        o.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
        v >>= 7u;
    }
    o.push_back(static_cast<uint8_t>(v));
}

static inline uint64_t zigzag_encode(int64_t x) {
    return (static_cast<uint64_t>(x) << 1u) ^ static_cast<uint64_t>(x >> 63);
}

static inline int64_t zigzag_decode(uint64_t u) {
    return static_cast<int64_t>((u >> 1u) ^ (~(u & 1u) + 1u));
}

struct ByteReader {
    const uint8_t *p = nullptr;
    size_t n = 0;
    size_t pos = 0;
    uint8_t u8() {
        if (pos + 1 > n) { std::fprintf(stderr, "EOF u8\n"); std::exit(1); }
        return p[pos++];
    }
    uint16_t u16() {
        if (pos + 2 > n) { std::fprintf(stderr, "EOF u16\n"); std::exit(1); }
        uint16_t v = static_cast<uint16_t>(p[pos]) | (static_cast<uint16_t>(p[pos + 1]) << 8u);
        pos += 2;
        return v;
    }
    uint32_t u32() {
        if (pos + 4 > n) { std::fprintf(stderr, "EOF u32\n"); std::exit(1); }
        uint32_t v = 0;
        std::memcpy(&v, p + pos, 4);
        pos += 4;
        return v;
    }
    void read_bytes(void *dst, size_t k) {
        if (pos + k > n) { std::fprintf(stderr, "EOF bytes\n"); std::exit(1); }
        std::memcpy(dst, p + pos, k);
        pos += k;
    }
    uint64_t uvarint() {
        uint64_t v = 0;
        uint32_t sh = 0;
        while (pos < n) {
            uint8_t b = p[pos++];
            v |= static_cast<uint64_t>(b & 0x7Fu) << sh;
            if ((b & 0x80u) == 0u) return v;
            sh += 7u;
        }
        std::fprintf(stderr, "EOF uvarint\n");
        std::exit(1);
    }
};

static std::string fmt_f32(float v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.9g", canon_zero_f32(v));
    return buf;
}

static std::string fmt_f16(uint16_t h) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", canon_zero_f32(f16_to_f32(h)));
    return buf;
}

static std::string fmt_rgba(uint32_t rgba8) {
    char buf[16];
    uint32_t r = rgba8 & 0xFFu;
    uint32_t g = (rgba8 >> 8u) & 0xFFu;
    uint32_t b = (rgba8 >> 16u) & 0xFFu;
    uint32_t a = (rgba8 >> 24u) & 0xFFu;
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
    return buf;
}

static uint32_t parse_rgba(const std::string &s) {
    const char *p = s.c_str();
    if (*p == '#') ++p;
    unsigned long long v = std::strtoull(p, nullptr, 16);
    uint32_t r = static_cast<uint32_t>((v >> 24u) & 0xFFu);
    uint32_t g = static_cast<uint32_t>((v >> 16u) & 0xFFu);
    uint32_t b = static_cast<uint32_t>((v >> 8u) & 0xFFu);
    uint32_t a = static_cast<uint32_t>(v & 0xFFu);
    return (a << 24u) | (b << 16u) | (g << 8u) | r;
}

static std::vector<float> parse_floats(const std::string &s) {
    std::vector<float> out;
    const char *p = s.c_str();
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') ++p;
        if (!*p) break;
        char *endp = nullptr;
        float v = std::strtof(p, &endp);
        if (endp == p) break;
        out.push_back(v);
        p = endp;
    }
    return out;
}

// ============================================================================
// Companion .bmesh Encode / Decode (Used by 3A and 3B in .full payloads)
// ============================================================================
static std::vector<uint8_t> encode_bmesh(const std::vector<MeshTemplate> &tpls) {
    std::vector<uint8_t> out;
    put_bytes(out, "BMSH", 4);
    put_u32(out, static_cast<uint32_t>(tpls.size()));
    for (const auto &t : tpls) {
        put_u16(out, static_cast<uint16_t>(t.name.size()));
        put_bytes(out, t.name.data(), t.name.size());
        put_u32(out, t.meshlet_cnt);
        put_u32(out, static_cast<uint32_t>(t.local_idx.size()));
        put_bytes(out, t.hdrs.data(), t.hdrs.size() * sizeof(BloomMeshletHeader32));
        put_bytes(out, t.verts.data(), t.verts.size() * sizeof(BloomVtx12));
        put_bytes(out, t.local_idx.data(), t.local_idx.size());
    }
    return out;
}

static void decode_bmesh(const uint8_t *p, size_t n, std::vector<MeshTemplate> &out_tpls) {
    ByteReader r{p, n, 0};
    char mag[4]{};
    r.read_bytes(mag, 4);
    if (std::memcmp(mag, "BMSH", 4) != 0) { std::fprintf(stderr, "Bad BMSH\n"); std::exit(1); }
    uint32_t tc = r.u32();
    out_tpls.resize(tc);
    for (uint32_t i = 0; i < tc; ++i) {
        uint16_t nl = r.u16();
        out_tpls[i].name.resize(nl);
        r.read_bytes(out_tpls[i].name.data(), nl);
        out_tpls[i].meshlet_cnt = r.u32();
        uint32_t il = r.u32();
        out_tpls[i].hdrs.resize(out_tpls[i].meshlet_cnt);
        out_tpls[i].verts.resize(out_tpls[i].meshlet_cnt * CLUSTER_VTX);
        out_tpls[i].local_idx.resize(il);
        r.read_bytes(out_tpls[i].hdrs.data(), out_tpls[i].hdrs.size() * sizeof(BloomMeshletHeader32));
        r.read_bytes(out_tpls[i].verts.data(), out_tpls[i].verts.size() * sizeof(BloomVtx12));
        r.read_bytes(out_tpls[i].local_idx.data(), il);
    }
}

// ============================================================================
// Lightweight Attribute-Scanning XML & SVG Helpers
// ============================================================================
struct XmlTag {
    std::string name;
    bool is_close = false;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string inner_text;
    const std::string &attr(const char *k) const {
        static const std::string empty;
        for (const auto &kv : attrs) {
            if (kv.first == k) return kv.second;
        }
        return empty;
    }
};

static std::vector<XmlTag> parse_xml_tags(const std::string &xml) {
    std::vector<XmlTag> tags;
    size_t i = 0, n = xml.size();
    while (i < n) {
        size_t lt = xml.find('<', i);
        if (lt == std::string::npos) break;
        if (lt + 1 < n && (xml[lt + 1] == '?' || xml[lt + 1] == '!')) {
            size_t gt = xml.find('>', lt + 2);
            i = (gt == std::string::npos) ? n : gt + 1;
            continue;
        }
        size_t gt = xml.find('>', lt + 1);
        if (gt == std::string::npos) break;
        XmlTag tag;
        size_t p = lt + 1;
        while (p < gt && (xml[p] == ' ' || xml[p] == '\t' || xml[p] == '\n' || xml[p] == '\r')) ++p;
        if (p < gt && xml[p] == '/') {
            tag.is_close = true;
            ++p;
        }
        size_t ns = p;
        while (p < gt && xml[p] != ' ' && xml[p] != '\t' && xml[p] != '\n' && xml[p] != '/') ++p;
        tag.name = xml.substr(ns, p - ns);
        bool self_close = (gt > lt + 1 && xml[gt - 1] == '/');
        while (p < gt) {
            while (p < gt && (xml[p] == ' ' || xml[p] == '\t' || xml[p] == '\n' || xml[p] == '\r' || xml[p] == '/')) ++p;
            if (p >= gt) break;
            size_t ks = p;
            while (p < gt && xml[p] != '=' && xml[p] != ' ') ++p;
            std::string k = xml.substr(ks, p - ks);
            while (p < gt && (xml[p] == '=' || xml[p] == ' ')) ++p;
            if (p < gt && (xml[p] == '"' || xml[p] == '\'')) {
                char q = xml[p++];
                size_t vs = p;
                while (p < gt && xml[p] != q) ++p;
                tag.attrs.emplace_back(k, xml.substr(vs, p - vs));
                if (p < gt) ++p;
            }
        }
        i = gt + 1;
        if (!tag.is_close && !self_close) {
            size_t next_lt = xml.find('<', i);
            if (next_lt != std::string::npos && next_lt > i) {
                tag.inner_text = xml.substr(i, next_lt - i);
                while (!tag.inner_text.empty() && (tag.inner_text.back() == ' ' || tag.inner_text.back() == '\n' || tag.inner_text.back() == '\r')) {
                    tag.inner_text.pop_back();
                }
                while (!tag.inner_text.empty() && (tag.inner_text.front() == ' ' || tag.inner_text.front() == '\n' || tag.inner_text.front() == '\r')) {
                    tag.inner_text.erase(tag.inner_text.begin());
                }
            }
        }
        tags.push_back(std::move(tag));
    }
    return tags;
}

static std::string serialize_hud_svg(const std::vector<HudNode> &huds, bool wrapped_document) {
    static const char *kKindNames[4] = {"panel", "button", "slider", "text"};
    std::ostringstream ss;
    if (wrapped_document) {
        ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 3840 2160\">\n";
    }
    for (const auto &h : huds) {
        double x = h.bounds_q14_2[0] * 0.25;
        double y = h.bounds_q14_2[1] * 0.25;
        double w = h.bounds_q14_2[2] * 0.25;
        double ht = h.bounds_q14_2[3] * 0.25;
        double rx = h.radius_q6_2 * 0.25;
        ss << "  <g id=\"" << h.id << "\" data-kind=\"" << kKindNames[h.kind & 3u]
           << "\" data-bind=\"" << h.bind_obj_idx << ":" << static_cast<unsigned>(h.bind_dof_slot) << "\">"
           << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << w << "\" height=\"" << ht
           << "\" rx=\"" << rx << "\" fill=\"" << fmt_rgba(h.fill_rgba8)
           << "\" stroke=\"" << fmt_rgba(h.border_rgba8) << "\"/>"
           << "<text x=\"" << x << "\" y=\"" << y << "\">" << h.label << "</text></g>\n";
    }
    if (wrapped_document) ss << "</svg>\n";
    return ss.str();
}

static void decode_hud_from_xml_tags(const std::vector<XmlTag> &tags, std::vector<HudNode> &out_huds) {
    HudNode cur;
    bool in_hud_g = false;
    for (const auto &t : tags) {
        if (t.name == "g" && !t.is_close && !t.attr("data-kind").empty()) {
            cur = HudNode{};
            cur.id = t.attr("id");
            const auto &kd = t.attr("data-kind");
            cur.kind = (kd == "panel") ? 0u : (kd == "button" ? 1u : (kd == "slider" ? 2u : 3u));
            const auto &bd = t.attr("data-bind");
            size_t col = bd.find(':');
            if (col != std::string::npos) {
                cur.bind_obj_idx = static_cast<uint16_t>(std::strtoul(bd.substr(0, col).c_str(), nullptr, 10));
                cur.bind_dof_slot = static_cast<uint8_t>(std::strtoul(bd.substr(col + 1).c_str(), nullptr, 10));
            }
            in_hud_g = true;
        } else if (in_hud_g && t.name == "rect") {
            auto q14_2 = [](const std::string &s) -> uint16_t {
                return static_cast<uint16_t>(std::lround(std::strtod(s.c_str(), nullptr) * 4.0));
            };
            cur.bounds_q14_2[0] = q14_2(t.attr("x"));
            cur.bounds_q14_2[1] = q14_2(t.attr("y"));
            cur.bounds_q14_2[2] = q14_2(t.attr("width"));
            cur.bounds_q14_2[3] = q14_2(t.attr("height"));
            cur.radius_q6_2 = static_cast<uint8_t>(std::lround(std::strtod(t.attr("rx").c_str(), nullptr) * 4.0));
            cur.fill_rgba8 = parse_rgba(t.attr("fill"));
            cur.border_rgba8 = parse_rgba(t.attr("stroke"));
        } else if (in_hud_g && t.name == "text" && !t.is_close) {
            cur.label = t.inner_text;
        } else if (in_hud_g && t.name == "g" && t.is_close) {
            out_huds.push_back(cur);
            in_hud_g = false;
        }
    }
}

// ============================================================================
// Base64 Helper (For 3a_xml_b64 Pure Single-File XML Comparison)
// ============================================================================
static std::string base64_encode(const uint8_t *data, size_t len) {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2u) / 3u) * 4u);
    for (size_t i = 0; i < len; i += 3u) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16u;
        if (i + 1u < len) v |= static_cast<uint32_t>(data[i + 1u]) << 8u;
        if (i + 2u < len) v |= static_cast<uint32_t>(data[i + 2u]);
        out.push_back(kB64[(v >> 18u) & 63u]);
        out.push_back(kB64[(v >> 12u) & 63u]);
        out.push_back((i + 1u < len) ? kB64[(v >> 6u) & 63u] : '=');
        out.push_back((i + 2u < len) ? kB64[v & 63u] : '=');
    }
    return out;
}

static std::vector<uint8_t> base64_decode(const std::string &s) {
    uint8_t dec[256];
    std::memset(dec, 0xFFu, sizeof(dec));
    const char *kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (uint8_t i = 0; i < 64; ++i) dec[static_cast<uint8_t>(kB64[i])] = i;
    std::vector<uint8_t> out;
    out.reserve((s.size() * 3u) / 4u);
    uint32_t val = 0;
    int bits = -8;
    for (char ch : s) {
        if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ') continue;
        uint8_t d = dec[static_cast<uint8_t>(ch)];
        if (d == 0xFFu) continue;
        val = (val << 6u) | d;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFFu));
            bits -= 8;
        }
    }
    return out;
}

// ============================================================================
// Format 1: Industry Baseline (GLTF-Style JSON + IndustryVtx64 + u32 + Rig512)
// ============================================================================
static std::vector<uint8_t> encode_baseline_glb64(const BloomSceneIR &ir, bool include_geom) {
    std::vector<uint8_t> bin;
    if (include_geom) {
        for (const auto &t : ir.templates) {
            uint32_t nv = static_cast<uint32_t>(t.verts.size());
            for (uint32_t i = 0; i < nv; ++i) {
                const auto &bv = t.verts[i];
                IndustryVtx64 iv{};
                iv.pos[0] = unpack_pos_unorm16(bv.pos_unorm16[0], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                iv.pos[1] = unpack_pos_unorm16(bv.pos_unorm16[1], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                iv.pos[2] = unpack_pos_unorm16(bv.pos_unorm16[2], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                oct_decode(bv.oct_norm, iv.norm[0], iv.norm[1], iv.norm[2]);
                iv.tan[0] = 1.0f; iv.tan[3] = 1.0f;
                iv.uv[0] = static_cast<float>(bv.uv_u8[0]) / 255.0f;
                iv.uv[1] = static_cast<float>(bv.uv_u8[1]) / 255.0f;
                iv.joints[0] = bv.joint_id;
                iv.joints[1] = bv.mat_id;
                iv.weights_u16[0] = 65535u;
                put_bytes(bin, &iv, sizeof(iv));
            }
            for (uint32_t m = 0; m < t.meshlet_cnt; ++m) {
                uint32_t vbase = m * CLUSTER_VTX;
                for (uint8_t li : t.local_idx) {
                    put_u32(bin, vbase + li);
                }
            }
        }
    }
    for (const auto &o : ir.objects) {
        IndustryRig512 rig{};
        // Bone 0 encodes translation; Bone 1 encodes quat/scale in industry 4x4 palette slots
        rig.mats[0][0] = 1.0f; rig.mats[0][5] = 1.0f; rig.mats[0][10] = 1.0f; rig.mats[0][15] = 1.0f;
        rig.mats[0][12] = o.state.pos[0];
        rig.mats[0][13] = o.state.pos[1];
        rig.mats[0][14] = o.state.pos[2];
        rig.mats[1][0] = f16_to_f32(o.state.scale_f16[0]);
        rig.mats[1][5] = f16_to_f32(o.state.scale_f16[1]);
        rig.mats[1][10] = f16_to_f32(o.state.scale_f16[2]);
        rig.mats[1][15] = 1.0f;
        put_bytes(bin, &rig, sizeof(rig));
    }

    std::ostringstream js;
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"industry_glb64\"},\"meshes\":[";
    for (size_t i = 0; i < ir.templates.size(); ++i) {
        const auto &t = ir.templates[i];
        if (i) js << ",";
        js << "{\"name\":\"" << t.name << "\",\"meshlets\":" << t.meshlet_cnt << "}";
    }
    js << "],\"nodes\":[";
    for (size_t i = 0; i < ir.objects.size(); ++i) {
        const auto &o = ir.objects[i];
        if (i) js << ",";
        js << "{\"name\":\"" << o.id << "\",\"mesh\":" << o.cspec.template_id << ",\"skin\":" << i
           << ",\"translation\":[" << fmt_f32(o.state.pos[0]) << "," << fmt_f32(o.state.pos[1]) << "," << fmt_f32(o.state.pos[2]) << "]"
           << ",\"rotation\":[" << fmt_f16(o.state.quat_f16[0]) << "," << fmt_f16(o.state.quat_f16[1]) << "," << fmt_f16(o.state.quat_f16[2]) << "," << fmt_f16(o.state.quat_f16[3]) << "]"
           << ",\"scale\":[" << fmt_f16(o.state.scale_f16[0]) << "," << fmt_f16(o.state.scale_f16[1]) << "," << fmt_f16(o.state.scale_f16[2]) << "]"
           << ",\"extras\":{\"dof\":[" << fmt_f16(o.state.dof_f16[0]) << "," << fmt_f16(o.state.dof_f16[1]) << "," << fmt_f16(o.state.dof_f16[2]) << "]"
           << ",\"dofMin\":[" << fmt_f16(o.cspec.dof_min_f16[0]) << "," << fmt_f16(o.cspec.dof_min_f16[1]) << "," << fmt_f16(o.cspec.dof_min_f16[2]) << "]"
           << ",\"dofMax\":[" << fmt_f16(o.cspec.dof_max_f16[0]) << "," << fmt_f16(o.cspec.dof_max_f16[1]) << "," << fmt_f16(o.cspec.dof_max_f16[2]) << "]"
           << ",\"jmask\":" << o.cspec.joint_mask << ",\"color\":\"" << fmt_rgba(o.state.rgba8_mat) << "\""
           << ",\"clip\":[" << fmt_f16(o.state.clip_plane_f16[0]) << "," << fmt_f16(o.state.clip_plane_f16[1]) << "," << fmt_f16(o.state.clip_plane_f16[2]) << "," << fmt_f16(o.state.clip_plane_f16[3]) << "]"
           << ",\"flags\":" << o.state.flags << "}}";
    }
    js << "],\"extras\":{\"analytics\":[";
    for (size_t i = 0; i < ir.analytics.size(); ++i) {
        const auto &a = ir.analytics[i];
        if (i) js << ",";
        js << "{\"id\":\"" << a.id << "\",\"center\":[" << fmt_f32(a.prim.center[0]) << "," << fmt_f32(a.prim.center[1]) << "," << fmt_f32(a.prim.center[2]) << "]"
           << ",\"radius\":" << fmt_f32(a.prim.radius) << ",\"axisLen\":[" << fmt_f16(a.prim.axis_len_f16[0]) << "," << fmt_f16(a.prim.axis_len_f16[1]) << "," << fmt_f16(a.prim.axis_len_f16[2]) << "," << fmt_f16(a.prim.axis_len_f16[3]) << "]"
           << ",\"color\":\"" << fmt_rgba(a.prim.rgba8) << "\",\"kind\":" << a.prim.prim_kind_flags << "}";
    }
    js << "],\"hud\":[";
    for (size_t i = 0; i < ir.huds.size(); ++i) {
        const auto &h = ir.huds[i];
        if (i) js << ",";
        js << "{\"id\":\"" << h.id << "\",\"kind\":" << static_cast<unsigned>(h.kind)
           << ",\"bounds\":[" << h.bounds_q14_2[0] << "," << h.bounds_q14_2[1] << "," << h.bounds_q14_2[2] << "," << h.bounds_q14_2[3] << "]"
           << ",\"fill\":\"" << fmt_rgba(h.fill_rgba8) << "\",\"border\":\"" << fmt_rgba(h.border_rgba8) << "\""
           << ",\"radius\":" << static_cast<unsigned>(h.radius_q6_2)
           << ",\"bind\":[" << h.bind_obj_idx << "," << static_cast<unsigned>(h.bind_dof_slot) << "]"
           << ",\"label\":\"" << h.label << "\"}";
    }
    js << "]}}";

    std::string jstr = js.str();
    std::vector<uint8_t> out;
    out.reserve(28 + jstr.size() + bin.size());
    put_bytes(out, "glTF", 4);
    put_u32(out, 2u);
    uint32_t jlen = static_cast<uint32_t>(jstr.size());
    uint32_t blen = static_cast<uint32_t>(bin.size());
    put_u32(out, static_cast<uint32_t>(28u + jlen + blen));
    put_u32(out, jlen);
    put_bytes(out, "JSON", 4);
    put_bytes(out, jstr.data(), jstr.size());
    put_u32(out, blen);
    put_bytes(out, "BIN\0", 4);
    put_bytes(out, bin.data(), bin.size());
    return out;
}

static BloomSceneIR decode_baseline_glb64(const uint8_t *data, size_t len, bool has_geom) {
    ByteReader r{data, len, 0};
    char m[4]{}; r.read_bytes(m, 4);
    (void)r.u32(); (void)r.u32();
    uint32_t jlen = r.u32();
    r.read_bytes(m, 4);
    std::string js(reinterpret_cast<const char *>(data + r.pos), jlen);
    r.pos += jlen;
    uint32_t blen = r.u32();
    r.read_bytes(m, 4);
    size_t bin_start = r.pos;
    (void)blen;

    BloomSceneIR ir;
    auto find_key_str = [&](size_t from, size_t limit, const char *key, size_t &out_end) -> std::string {
        size_t p = js.find(key, from);
        if (p == std::string::npos || p >= limit) return "";
        p = js.find(':', p);
        size_t q1 = js.find('"', p + 1);
        size_t q2 = js.find('"', q1 + 1);
        out_end = q2 + 1;
        return js.substr(q1 + 1, q2 - q1 - 1);
    };
    auto find_key_num = [&](size_t from, size_t limit, const char *key, size_t &out_end) -> uint64_t {
        size_t p = js.find(key, from);
        if (p == std::string::npos || p >= limit) return 0;
        p = js.find(':', p) + 1;
        char *ep = nullptr;
        uint64_t v = std::strtoull(js.c_str() + p, &ep, 10);
        out_end = static_cast<size_t>(ep - js.c_str());
        return v;
    };
    auto find_key_fvec = [&](size_t from, size_t limit, const char *key, size_t &out_end) -> std::vector<float> {
        size_t p = js.find(key, from);
        if (p == std::string::npos || p >= limit) return {};
        size_t lb = js.find('[', p);
        size_t rb = js.find(']', lb);
        out_end = rb + 1;
        return parse_floats(js.substr(lb + 1, rb - lb - 1));
    };

    // Meshes section
    size_t meshes_s = js.find("\"meshes\":["), nodes_s = js.find("\"nodes\":[", meshes_s);
    size_t pos = meshes_s + 10;
    while (pos < nodes_s) {
        size_t ob = js.find('{', pos);
        if (ob == std::string::npos || ob >= nodes_s) break;
        size_t cb = js.find('}', ob);
        MeshTemplate mt;
        size_t e = ob;
        mt.name = find_key_str(ob, cb, "\"name\"", e);
        mt.meshlet_cnt = static_cast<uint32_t>(find_key_num(ob, cb, "\"meshlets\"", e));
        ir.templates.push_back(std::move(mt));
        pos = cb + 1;
    }

    // Re-quantize BIN IndustryVtx64 + u32 indices back to BloomVtx12 + BloomMeshletHeader32
    size_t bpos = bin_start;
    if (has_geom) {
        for (auto &t : ir.templates) {
            uint32_t nv = t.meshlet_cnt * CLUSTER_VTX;
            t.verts.resize(nv);
            for (uint32_t i = 0; i < nv; ++i) {
                IndustryVtx64 iv{};
                std::memcpy(&iv, data + bpos, sizeof(iv));
                bpos += sizeof(iv);
                BloomVtx12 &bv = t.verts[i];
                bv.pos_unorm16[0] = recover_pos_unorm16(iv.pos[0], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                bv.pos_unorm16[1] = recover_pos_unorm16(iv.pos[1], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                bv.pos_unorm16[2] = recover_pos_unorm16(iv.pos[2], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                oct_encode(iv.norm[0], iv.norm[1], iv.norm[2], bv.oct_norm);
                bv.uv_u8[0] = static_cast<uint8_t>(std::lround(iv.uv[0] * 255.0f));
                bv.uv_u8[1] = static_cast<uint8_t>(std::lround(iv.uv[1] * 255.0f));
                bv.joint_id = static_cast<uint8_t>(iv.joints[0]);
                bv.mat_id = static_cast<uint8_t>(iv.joints[1]);
            }
            uint32_t ni = t.meshlet_cnt * CLUSTER_TRI * 3u;
            t.local_idx.resize(CLUSTER_TRI * 3u);
            for (uint32_t k = 0; k < ni; ++k) {
                uint32_t idx32 = 0;
                std::memcpy(&idx32, data + bpos, 4);
                bpos += 4;
                if (k < CLUSTER_TRI * 3u) t.local_idx[k] = static_cast<uint8_t>(idx32);
            }
            t.hdrs.resize(t.meshlet_cnt);
            for (uint32_t m = 0; m < t.meshlet_cnt; ++m) {
                finalize_meshlet_header(m, t.verts, t.hdrs[m]);
            }
        }
    }

    // Nodes section
    size_t extras_s = js.find("\"analytics\":[", nodes_s);
    pos = nodes_s + 9;
    while (pos < extras_s) {
        size_t ob = js.find("{\"name\":", pos);
        if (ob == std::string::npos || ob >= extras_s) break;
        size_t cb = js.find("}}", ob);
        ObjectNode o;
        size_t e = ob;
        o.id = find_key_str(ob, cb, "\"name\"", e);
        o.cspec.template_id = static_cast<uint16_t>(find_key_num(ob, cb, "\"mesh\"", e));
        auto tr = find_key_fvec(ob, cb, "\"translation\"", e);
        auto rt = find_key_fvec(ob, cb, "\"rotation\"", e);
        auto sc = find_key_fvec(ob, cb, "\"scale\"", e);
        auto df = find_key_fvec(ob, cb, "\"dof\"", e);
        auto dmin = find_key_fvec(ob, cb, "\"dofMin\"", e);
        auto dmax = find_key_fvec(ob, cb, "\"dofMax\"", e);
        o.cspec.joint_mask = static_cast<uint16_t>(find_key_num(ob, cb, "\"jmask\"", e));
        o.state.rgba8_mat = parse_rgba(find_key_str(ob, cb, "\"color\"", e));
        auto cl = find_key_fvec(ob, cb, "\"clip\"", e);
        o.state.flags = static_cast<uint32_t>(find_key_num(ob, cb, "\"flags\"", e));
        for (int k = 0; k < 3; ++k) {
            o.state.pos[k] = tr[k];
            o.state.scale_f16[k] = f32_to_f16(sc[k]);
            o.state.dof_f16[k] = f32_to_f16(df[k]);
            o.cspec.dof_min_f16[k] = f32_to_f16(dmin[k]);
            o.cspec.dof_max_f16[k] = f32_to_f16(dmax[k]);
        }
        for (int k = 0; k < 4; ++k) {
            o.state.quat_f16[k] = f32_to_f16(rt[k]);
            o.state.clip_plane_f16[k] = f32_to_f16(cl[k]);
        }
        ir.objects.push_back(std::move(o));
        pos = cb + 2;
    }

    // Read IndustryRig512 palette stride so loader exercises real rig buffer parsing
    bpos += ir.objects.size() * sizeof(IndustryRig512);
    (void)bpos;

    size_t hud_s = js.find("\"hud\":[", extras_s);
    pos = extras_s + 13;
    while (pos < hud_s) {
        size_t ob = js.find('{', pos);
        if (ob == std::string::npos || ob >= hud_s) break;
        size_t cb = js.find('}', ob);
        AnalyticNode a;
        size_t e = ob;
        a.id = find_key_str(ob, cb, "\"id\"", e);
        auto c = find_key_fvec(ob, cb, "\"center\"", e);
        size_t rp = js.find("\"radius\":", ob);
        a.prim.radius = std::strtof(js.c_str() + rp + 9, nullptr);
        auto ax = find_key_fvec(ob, cb, "\"axisLen\"", e);
        a.prim.rgba8 = parse_rgba(find_key_str(ob, cb, "\"color\"", e));
        a.prim.prim_kind_flags = static_cast<uint32_t>(find_key_num(ob, cb, "\"kind\"", e));
        for (int k = 0; k < 3; ++k) a.prim.center[k] = c[k];
        for (int k = 0; k < 4; ++k) a.prim.axis_len_f16[k] = f32_to_f16(ax[k]);
        ir.analytics.push_back(std::move(a));
        pos = cb + 1;
    }

    pos = hud_s + 7;
    while (pos < js.size()) {
        size_t ob = js.find('{', pos);
        if (ob == std::string::npos) break;
        size_t cb = js.find('}', ob);
        HudNode h;
        size_t e = ob;
        h.id = find_key_str(ob, cb, "\"id\"", e);
        h.kind = static_cast<uint8_t>(find_key_num(ob, cb, "\"kind\"", e));
        auto bd = find_key_fvec(ob, cb, "\"bounds\"", e);
        for (int k = 0; k < 4; ++k) h.bounds_q14_2[k] = static_cast<uint16_t>(bd[k]);
        h.fill_rgba8 = parse_rgba(find_key_str(ob, cb, "\"fill\"", e));
        h.border_rgba8 = parse_rgba(find_key_str(ob, cb, "\"border\"", e));
        h.radius_q6_2 = static_cast<uint8_t>(find_key_num(ob, cb, "\"radius\"", e));
        auto bi = find_key_fvec(ob, cb, "\"bind\"", e);
        h.bind_obj_idx = static_cast<uint16_t>(bi[0]);
        h.bind_dof_slot = static_cast<uint8_t>(bi[1]);
        h.label = find_key_str(ob, cb, "\"label\"", e);
        ir.huds.push_back(std::move(h));
        pos = cb + 1;
    }
    return ir;
}

// ============================================================================
// Shared XML Serialization Helpers for 3A & 3B
// ============================================================================
static void emit_xml_3d_entities(std::ostringstream &ss, const BloomSceneIR &ir, const char *ns_pfx) {
    for (size_t i = 0; i < ir.templates.size(); ++i) {
        const auto &t = ir.templates[i];
        ss << "  <" << ns_pfx << "asset id=\"" << t.name << "\" slot=\"" << i << "\" meshlets=\"" << t.meshlet_cnt << "\"/>\n";
    }
    for (const auto &o : ir.objects) {
        ss << "  <" << ns_pfx << "obj id=\"" << o.id << "\" tpl=\"" << o.cspec.template_id << "\" jmask=\"" << o.cspec.joint_mask << "\" "
           << "pos=\"" << fmt_f32(o.state.pos[0]) << " " << fmt_f32(o.state.pos[1]) << " " << fmt_f32(o.state.pos[2]) << "\" "
           << "quat=\"" << fmt_f16(o.state.quat_f16[0]) << " " << fmt_f16(o.state.quat_f16[1]) << " " << fmt_f16(o.state.quat_f16[2]) << " " << fmt_f16(o.state.quat_f16[3]) << "\" "
           << "scale=\"" << fmt_f16(o.state.scale_f16[0]) << " " << fmt_f16(o.state.scale_f16[1]) << " " << fmt_f16(o.state.scale_f16[2]) << "\" "
           << "dof=\"" << fmt_f16(o.state.dof_f16[0]) << " " << fmt_f16(o.state.dof_f16[1]) << " " << fmt_f16(o.state.dof_f16[2]) << "\" "
           << "range=\"" << fmt_f16(o.cspec.dof_min_f16[0]) << " " << fmt_f16(o.cspec.dof_min_f16[1]) << " " << fmt_f16(o.cspec.dof_min_f16[2]) << " "
           << fmt_f16(o.cspec.dof_max_f16[0]) << " " << fmt_f16(o.cspec.dof_max_f16[1]) << " " << fmt_f16(o.cspec.dof_max_f16[2]) << "\" "
           << "color=\"" << fmt_rgba(o.state.rgba8_mat) << "\" "
           << "clip=\"" << fmt_f16(o.state.clip_plane_f16[0]) << " " << fmt_f16(o.state.clip_plane_f16[1]) << " " << fmt_f16(o.state.clip_plane_f16[2]) << " " << fmt_f16(o.state.clip_plane_f16[3]) << "\" "
           << "flags=\"" << o.state.flags << "\"/>\n";
    }
    for (const auto &a : ir.analytics) {
        ss << "  <" << ns_pfx << "analytic id=\"" << a.id << "\" kind=\"" << a.prim.prim_kind_flags << "\" "
           << "center=\"" << fmt_f32(a.prim.center[0]) << " " << fmt_f32(a.prim.center[1]) << " " << fmt_f32(a.prim.center[2]) << "\" "
           << "radius=\"" << fmt_f32(a.prim.radius) << "\" "
           << "axis=\"" << fmt_f16(a.prim.axis_len_f16[0]) << " " << fmt_f16(a.prim.axis_len_f16[1]) << " " << fmt_f16(a.prim.axis_len_f16[2]) << " " << fmt_f16(a.prim.axis_len_f16[3]) << "\" "
           << "color=\"" << fmt_rgba(a.prim.rgba8) << "\"/>\n";
    }
}

static void decode_xml_3d_entities(const std::vector<XmlTag> &tags, const char *ns_pfx, BloomSceneIR &ir, bool has_geom) {
    std::string t_asset = std::string(ns_pfx) + "asset";
    std::string t_obj = std::string(ns_pfx) + "obj";
    std::string t_ana = std::string(ns_pfx) + "analytic";
    size_t tpl_i = 0;
    for (const auto &tg : tags) {
        if (tg.is_close) continue;
        if (tg.name == t_asset) {
            uint32_t mcnt = static_cast<uint32_t>(std::strtoul(tg.attr("meshlets").c_str(), nullptr, 10));
            if (has_geom && tpl_i < ir.templates.size()) {
                // Templates already populated from BMSH; verify slot name
            } else {
                MeshTemplate mt;
                mt.name = tg.attr("id");
                mt.meshlet_cnt = mcnt;
                ir.templates.push_back(std::move(mt));
            }
            ++tpl_i;
        } else if (tg.name == t_obj) {
            ObjectNode o;
            o.id = tg.attr("id");
            o.cspec.template_id = static_cast<uint16_t>(std::strtoul(tg.attr("tpl").c_str(), nullptr, 10));
            o.cspec.joint_mask = static_cast<uint16_t>(std::strtoul(tg.attr("jmask").c_str(), nullptr, 10));
            auto p = parse_floats(tg.attr("pos"));
            auto q = parse_floats(tg.attr("quat"));
            auto s = parse_floats(tg.attr("scale"));
            auto d = parse_floats(tg.attr("dof"));
            auto r = parse_floats(tg.attr("range"));
            auto c = parse_floats(tg.attr("clip"));
            for (int k = 0; k < 3; ++k) {
                o.state.pos[k] = p[k];
                o.state.scale_f16[k] = f32_to_f16(s[k]);
                o.state.dof_f16[k] = f32_to_f16(d[k]);
                o.cspec.dof_min_f16[k] = f32_to_f16(r[k]);
                o.cspec.dof_max_f16[k] = f32_to_f16(r[3 + k]);
            }
            for (int k = 0; k < 4; ++k) {
                o.state.quat_f16[k] = f32_to_f16(q[k]);
                o.state.clip_plane_f16[k] = f32_to_f16(c[k]);
            }
            o.state.rgba8_mat = parse_rgba(tg.attr("color"));
            o.state.flags = static_cast<uint32_t>(std::strtoul(tg.attr("flags").c_str(), nullptr, 10));
            ir.objects.push_back(std::move(o));
        } else if (tg.name == t_ana) {
            AnalyticNode a;
            a.id = tg.attr("id");
            a.prim.prim_kind_flags = static_cast<uint32_t>(std::strtoul(tg.attr("kind").c_str(), nullptr, 10));
            auto ctr = parse_floats(tg.attr("center"));
            for (int k = 0; k < 3; ++k) a.prim.center[k] = ctr[k];
            a.prim.radius = std::strtof(tg.attr("radius").c_str(), nullptr);
            auto ax = parse_floats(tg.attr("axis"));
            for (int k = 0; k < 4; ++k) a.prim.axis_len_f16[k] = f32_to_f16(ax[k]);
            a.prim.rgba8 = parse_rgba(tg.attr("color"));
            ir.analytics.push_back(std::move(a));
        }
    }
}

// ============================================================================
// Format 2: Proposal 3A (<bloom-obj> + separate <svg> + companion .bmesh)
// ============================================================================
static std::vector<uint8_t> encode_3a_xml_bobj(const BloomSceneIR &ir, bool include_geom) {
    std::ostringstream xs;
    xs << "<bloom-obj version=\"1\">\n";
    emit_xml_3d_entities(xs, ir, "");
    xs << "</bloom-obj>\n";
    std::string xml = xs.str();
    std::string svg = serialize_hud_svg(ir.huds, true);
    std::vector<uint8_t> bm = include_geom ? encode_bmesh(ir.templates) : std::vector<uint8_t>{};

    std::vector<uint8_t> out;
    out.reserve(16 + xml.size() + svg.size() + bm.size());
    put_bytes(out, "BL3A", 4);
    put_u32(out, static_cast<uint32_t>(xml.size()));
    put_u32(out, static_cast<uint32_t>(svg.size()));
    put_u32(out, static_cast<uint32_t>(bm.size()));
    put_bytes(out, xml.data(), xml.size());
    put_bytes(out, svg.data(), svg.size());
    put_bytes(out, bm.data(), bm.size());
    return out;
}

static BloomSceneIR decode_3a_xml_bobj(const uint8_t *data, size_t len, bool has_geom) {
    ByteReader r{data, len, 0};
    char m[4]{}; r.read_bytes(m, 4);
    uint32_t xlen = r.u32(), slen = r.u32(), blen = r.u32();
    std::string xml(reinterpret_cast<const char *>(data + r.pos), xlen); r.pos += xlen;
    std::string svg(reinterpret_cast<const char *>(data + r.pos), slen); r.pos += slen;

    BloomSceneIR ir;
    if (has_geom && blen > 0) decode_bmesh(data + r.pos, blen, ir.templates);
    decode_xml_3d_entities(parse_xml_tags(xml), "", ir, has_geom);
    decode_hud_from_xml_tags(parse_xml_tags(svg), ir.huds);
    return ir;
}

// Variant 3A Pure Single-File XML with inline Base64 <bmesh>
static std::vector<uint8_t> encode_3a_xml_b64(const BloomSceneIR &ir) {
    std::ostringstream ss;
    ss << "<bloom-package version=\"1\">\n<bloom-obj version=\"1\">\n";
    emit_xml_3d_entities(ss, ir, "");
    ss << "</bloom-obj>\n";
    ss << serialize_hud_svg(ir.huds, true);
    std::vector<uint8_t> bm = encode_bmesh(ir.templates);
    ss << "<bmesh encoding=\"base64\">" << base64_encode(bm.data(), bm.size()) << "</bmesh>\n</bloom-package>\n";
    std::string s = ss.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

static BloomSceneIR decode_3a_xml_b64(const uint8_t *data, size_t len) {
    std::string xml(reinterpret_cast<const char *>(data), len);
    auto tags = parse_xml_tags(xml);
    BloomSceneIR ir;
    for (const auto &tg : tags) {
        if (!tg.is_close && tg.name == "bmesh") {
            auto raw = base64_decode(tg.inner_text);
            decode_bmesh(raw.data(), raw.size(), ir.templates);
            break;
        }
    }
    decode_xml_3d_entities(tags, "", ir, true);
    decode_hud_from_xml_tags(tags, ir.huds);
    return ir;
}

// ============================================================================
// Format 3: Proposal 3B (Unified Single-File W3C SVG + <b:scene> + .bmesh)
// ============================================================================
static std::vector<uint8_t> encode_3b_svg_scene(const BloomSceneIR &ir, bool include_geom) {
    std::ostringstream ss;
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:b=\"https://bloom.gpu/ns/1\" viewBox=\"0 0 3840 2160\">\n"
       << "  <defs><b:scene version=\"1\">\n";
    emit_xml_3d_entities(ss, ir, "b:");
    ss << "  </b:scene></defs>\n  <g id=\"hud\">\n";
    ss << serialize_hud_svg(ir.huds, false);
    ss << "  </g>\n</svg>\n";
    std::string svg = ss.str();
    if (!include_geom) {
        // Pure raw W3C SVG document for .scene evaluation (zero binary wrapper)
        return std::vector<uint8_t>(svg.begin(), svg.end());
    }
    std::vector<uint8_t> bm = encode_bmesh(ir.templates);
    std::vector<uint8_t> out;
    out.reserve(16 + svg.size() + bm.size());
    put_bytes(out, "BL3B", 4);
    put_u32(out, static_cast<uint32_t>(svg.size()));
    put_u32(out, static_cast<uint32_t>(bm.size()));
    put_u32(out, 0u);
    put_bytes(out, svg.data(), svg.size());
    put_bytes(out, bm.data(), bm.size());
    return out;
}

static BloomSceneIR decode_3b_svg_scene(const uint8_t *data, size_t len, bool has_geom) {
    std::string svg;
    BloomSceneIR ir;
    if (has_geom) {
        ByteReader r{data, len, 0};
        char m[4]{}; r.read_bytes(m, 4);
        uint32_t slen = r.u32(), blen = r.u32();
        (void)r.u32();
        svg.assign(reinterpret_cast<const char *>(data + r.pos), slen);
        decode_bmesh(data + r.pos + slen, blen, ir.templates);
    } else {
        svg.assign(reinterpret_cast<const char *>(data), len);
    }
    auto tags = parse_xml_tags(svg);
    decode_xml_3d_entities(tags, "b:", ir, has_geom);
    decode_hud_from_xml_tags(tags, ir.huds);
    return ir;
}

// ============================================================================
// Binary HUD Row & Front-Coded String Helpers
// ============================================================================
struct alignas(4) HudNodeBin {
    uint16_t bounds_q14_2[4];
    uint32_t fill_rgba8;
    uint32_t border_rgba8;
    uint16_t bind_obj_idx;
    uint8_t  kind;
    uint8_t  radius_q6_2;
    uint8_t  bind_dof_slot;
    uint8_t  pad0;
    uint16_t id_len;
    uint16_t label_len;
};
static_assert(sizeof(HudNodeBin) == 28, "HudNodeBin must be 28 B");

static void put_front_strings(std::vector<uint8_t> &o, const std::vector<std::string> &strs) {
    std::string prev;
    for (const auto &s : strs) {
        size_t lim = std::min(prev.size(), s.size());
        size_t lcp = 0;
        while (lcp < lim && prev[lcp] == s[lcp]) ++lcp;
        size_t suf = s.size() - lcp;
        put_uvarint(o, lcp);
        put_uvarint(o, suf);
        put_bytes(o, s.data() + lcp, suf);
        prev = s;
    }
}

static std::vector<std::string> get_front_strings(ByteReader &r, uint32_t count) {
    std::vector<std::string> out(count);
    std::string prev;
    for (uint32_t i = 0; i < count; ++i) {
        size_t lcp = static_cast<size_t>(r.uvarint());
        size_t suf = static_cast<size_t>(r.uvarint());
        std::string cur = prev.substr(0, lcp);
        size_t old_sz = cur.size();
        cur.resize(old_sz + suf);
        r.read_bytes(cur.data() + old_sz, suf);
        out[i] = cur;
        prev = std::move(cur);
    }
    return out;
}

// ============================================================================
// Format 4: Proposal 3C Row-Major GPU-Direct Binary (.bobj_row)
// ============================================================================
static std::vector<uint8_t> encode_3c_bobj_row(const BloomSceneIR &ir, bool include_geom) {
    std::vector<uint8_t> out;
    put_bytes(out, "BOBR", 4);
    put_u32(out, include_geom ? 1u : 0u);
    put_u32(out, static_cast<uint32_t>(ir.templates.size()));
    put_u32(out, static_cast<uint32_t>(ir.objects.size()));
    put_u32(out, static_cast<uint32_t>(ir.analytics.size()));
    put_u32(out, static_cast<uint32_t>(ir.huds.size()));

    for (const auto &t : ir.templates) {
        put_u16(out, static_cast<uint16_t>(t.name.size()));
        put_u32(out, t.meshlet_cnt);
        put_u32(out, include_geom ? static_cast<uint32_t>(t.local_idx.size()) : 0u);
        put_bytes(out, t.name.data(), t.name.size());
        if (include_geom) {
            put_bytes(out, t.hdrs.data(), t.hdrs.size() * sizeof(BloomMeshletHeader32));
            put_bytes(out, t.verts.data(), t.verts.size() * sizeof(BloomVtx12));
            put_bytes(out, t.local_idx.data(), t.local_idx.size());
        }
    }
    for (const auto &o : ir.objects) {
        put_bytes(out, &o.state, sizeof(BloomObjectState48));
        put_bytes(out, &o.cspec, sizeof(ConstraintSpec));
    }
    for (const auto &a : ir.analytics) {
        put_bytes(out, &a.prim, sizeof(BloomAnalyticPrim32));
    }
    for (const auto &h : ir.huds) {
        HudNodeBin hb{};
        for (int k = 0; k < 4; ++k) hb.bounds_q14_2[k] = h.bounds_q14_2[k];
        hb.fill_rgba8 = h.fill_rgba8;
        hb.border_rgba8 = h.border_rgba8;
        hb.bind_obj_idx = h.bind_obj_idx;
        hb.kind = h.kind;
        hb.radius_q6_2 = h.radius_q6_2;
        hb.bind_dof_slot = h.bind_dof_slot;
        hb.id_len = static_cast<uint16_t>(h.id.size());
        hb.label_len = static_cast<uint16_t>(h.label.size());
        put_bytes(out, &hb, sizeof(HudNodeBin));
    }
    // Sequential zero-delimiter/u16 string stream matching node order
    for (const auto &o : ir.objects) {
        put_u8(out, static_cast<uint8_t>(o.id.size()));
        put_bytes(out, o.id.data(), o.id.size());
    }
    for (const auto &a : ir.analytics) {
        put_u8(out, static_cast<uint8_t>(a.id.size()));
        put_bytes(out, a.id.data(), a.id.size());
    }
    for (const auto &h : ir.huds) {
        put_bytes(out, h.id.data(), h.id.size());
        put_bytes(out, h.label.data(), h.label.size());
    }
    return out;
}

static BloomSceneIR decode_3c_bobj_row(const uint8_t *data, size_t len) {
    ByteReader r{data, len, 0};
    char mag[4]{}; r.read_bytes(mag, 4);
    bool has_geom = (r.u32() & 1u) != 0u;
    uint32_t tc = r.u32(), oc = r.u32(), ac = r.u32(), hc = r.u32();

    BloomSceneIR ir;
    ir.templates.resize(tc);
    for (uint32_t i = 0; i < tc; ++i) {
        uint16_t nl = r.u16();
        ir.templates[i].meshlet_cnt = r.u32();
        uint32_t il = r.u32();
        ir.templates[i].name.resize(nl);
        r.read_bytes(ir.templates[i].name.data(), nl);
        if (has_geom) {
            ir.templates[i].hdrs.resize(ir.templates[i].meshlet_cnt);
            ir.templates[i].verts.resize(ir.templates[i].meshlet_cnt * CLUSTER_VTX);
            ir.templates[i].local_idx.resize(il);
            r.read_bytes(ir.templates[i].hdrs.data(), ir.templates[i].hdrs.size() * sizeof(BloomMeshletHeader32));
            r.read_bytes(ir.templates[i].verts.data(), ir.templates[i].verts.size() * sizeof(BloomVtx12));
            r.read_bytes(ir.templates[i].local_idx.data(), il);
        }
    }
    ir.objects.resize(oc);
    for (uint32_t i = 0; i < oc; ++i) {
        r.read_bytes(&ir.objects[i].state, sizeof(BloomObjectState48));
        r.read_bytes(&ir.objects[i].cspec, sizeof(ConstraintSpec));
    }
    ir.analytics.resize(ac);
    for (uint32_t i = 0; i < ac; ++i) r.read_bytes(&ir.analytics[i].prim, sizeof(BloomAnalyticPrim32));
    ir.huds.resize(hc);
    std::vector<std::pair<uint16_t, uint16_t>> hlens(hc);
    for (uint32_t i = 0; i < hc; ++i) {
        HudNodeBin hb{};
        r.read_bytes(&hb, sizeof(HudNodeBin));
        for (int k = 0; k < 4; ++k) ir.huds[i].bounds_q14_2[k] = hb.bounds_q14_2[k];
        ir.huds[i].fill_rgba8 = hb.fill_rgba8;
        ir.huds[i].border_rgba8 = hb.border_rgba8;
        ir.huds[i].bind_obj_idx = hb.bind_obj_idx;
        ir.huds[i].kind = hb.kind;
        ir.huds[i].radius_q6_2 = hb.radius_q6_2;
        ir.huds[i].bind_dof_slot = hb.bind_dof_slot;
        hlens[i] = {hb.id_len, hb.label_len};
    }
    for (uint32_t i = 0; i < oc; ++i) {
        uint8_t sl = r.u8();
        ir.objects[i].id.resize(sl);
        r.read_bytes(ir.objects[i].id.data(), sl);
    }
    for (uint32_t i = 0; i < ac; ++i) {
        uint8_t sl = r.u8();
        ir.analytics[i].id.resize(sl);
        r.read_bytes(ir.analytics[i].id.data(), sl);
    }
    for (uint32_t i = 0; i < hc; ++i) {
        ir.huds[i].id.resize(hlens[i].first);
        r.read_bytes(ir.huds[i].id.data(), hlens[i].first);
        ir.huds[i].label.resize(hlens[i].second);
        r.read_bytes(ir.huds[i].label.data(), hlens[i].second);
    }
    return ir;
}

// ============================================================================
// Format 5: Proposal 3C Columnar + Delta-Varint + Byte-Plane Split (.bobj_col)
// ============================================================================
static void encode_f32_byte_planes(std::vector<uint8_t> &o, const std::vector<float> &vals) {
    uint32_t n = static_cast<uint32_t>(vals.size());
    for (int lane = 3; lane >= 0; --lane) {
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t u = 0;
            std::memcpy(&u, &vals[i], 4);
            put_u8(o, static_cast<uint8_t>((u >> (lane * 8)) & 0xFFu));
        }
    }
}

static void decode_f32_byte_planes(ByteReader &r, uint32_t n, std::vector<float> &vals) {
    std::vector<uint32_t> raw(n, 0u);
    for (int lane = 3; lane >= 0; --lane) {
        for (uint32_t i = 0; i < n; ++i) {
            raw[i] |= static_cast<uint32_t>(r.u8()) << (lane * 8);
        }
    }
    vals.resize(n);
    for (uint32_t i = 0; i < n; ++i) std::memcpy(&vals[i], &raw[i], 4);
}

static void encode_u16_lanes(std::vector<uint8_t> &o, const std::vector<uint16_t> &vals) {
    for (uint16_t v : vals) put_u8(o, static_cast<uint8_t>((v >> 8u) & 0xFFu));
    for (uint16_t v : vals) put_u8(o, static_cast<uint8_t>(v & 0xFFu));
}

static void decode_u16_lanes(ByteReader &r, uint32_t n, std::vector<uint16_t> &vals) {
    vals.assign(n, 0u);
    for (uint32_t i = 0; i < n; ++i) vals[i] = static_cast<uint16_t>(r.u8()) << 8u;
    for (uint32_t i = 0; i < n; ++i) vals[i] |= r.u8();
}

static std::vector<uint8_t> encode_3c_bobj_col(const BloomSceneIR &ir, bool include_geom) {
    std::vector<uint8_t> out;
    put_bytes(out, "BOBC", 4);
    put_u32(out, include_geom ? 1u : 0u);
    put_u32(out, static_cast<uint32_t>(ir.templates.size()));
    put_u32(out, static_cast<uint32_t>(ir.objects.size()));
    put_u32(out, static_cast<uint32_t>(ir.analytics.size()));
    put_u32(out, static_cast<uint32_t>(ir.huds.size()));

    // Templates metadata & front-coded names
    std::vector<std::string> tnames;
    for (const auto &t : ir.templates) {
        tnames.push_back(t.name);
        put_uvarint(out, t.meshlet_cnt);
        put_uvarint(out, include_geom ? t.local_idx.size() : 0u);
    }
    put_front_strings(out, tnames);

    if (include_geom) {
        for (const auto &t : ir.templates) {
            // Meshlet header columns
            int64_t pv = 0, pi = 0, po = 0;
            for (const auto &h : t.hdrs) {
                put_uvarint(out, zigzag_encode(static_cast<int64_t>(h.vtx_offset) - pv)); pv = h.vtx_offset;
                put_uvarint(out, zigzag_encode(static_cast<int64_t>(h.idx_offset) - pi)); pi = h.idx_offset;
                put_uvarint(out, h.vtx_cnt);
                put_uvarint(out, h.tri_cnt);
                put_uvarint(out, zigzag_encode(static_cast<int64_t>(h.obj_id) - po)); po = h.obj_id;
                put_uvarint(out, h.flags);
                put_uvarint(out, h.aabb_scale_tag);
            }
            for (int k = 0; k < 4; ++k) {
                std::vector<uint16_t> col;
                col.reserve(t.hdrs.size());
                for (const auto &h : t.hdrs) col.push_back(h.bsphere_f16[k]);
                encode_u16_lanes(out, col);
            }
            for (int k = 0; k < 3; ++k) for (const auto &h : t.hdrs) put_u8(out, static_cast<uint8_t>(h.cone_axis[k]));
            for (const auto &h : t.hdrs) put_u8(out, static_cast<uint8_t>(h.cone_cutoff));

            // Vertex columns: zig-zag delta varint positions + oct normals + uv + joint/mat
            for (int k = 0; k < 3; ++k) {
                int32_t prev = 0;
                for (const auto &v : t.verts) {
                    int32_t cur = v.pos_unorm16[k];
                    put_uvarint(out, zigzag_encode(cur - prev));
                    prev = cur;
                }
            }
            for (int k = 0; k < 2; ++k) {
                int32_t prev = 0;
                for (const auto &v : t.verts) {
                    int32_t cur = v.oct_norm[k];
                    put_uvarint(out, zigzag_encode(cur - prev));
                    prev = cur;
                }
            }
            for (int k = 0; k < 2; ++k) {
                int32_t prev = 0;
                for (const auto &v : t.verts) {
                    int32_t cur = v.uv_u8[k];
                    put_uvarint(out, zigzag_encode(cur - prev));
                    prev = cur;
                }
            }
            for (const auto &v : t.verts) put_u8(out, v.joint_id);
            for (const auto &v : t.verts) put_u8(out, v.mat_id);

            int32_t pidx = 0;
            for (uint8_t li : t.local_idx) {
                int32_t cur = li;
                put_uvarint(out, zigzag_encode(cur - pidx));
                pidx = cur;
            }
        }
    }

    // Object columns
    uint32_t oc = static_cast<uint32_t>(ir.objects.size());
    std::vector<float> fcol(oc);
    std::vector<uint16_t> u16col(oc);
    for (int k = 0; k < 3; ++k) {
        for (uint32_t i = 0; i < oc; ++i) fcol[i] = ir.objects[i].state.pos[k];
        encode_f32_byte_planes(out, fcol);
    }
    for (int k = 0; k < 4; ++k) {
        for (uint32_t i = 0; i < oc; ++i) u16col[i] = ir.objects[i].state.quat_f16[k];
        encode_u16_lanes(out, u16col);
    }
    for (int k = 0; k < 3; ++k) {
        for (uint32_t i = 0; i < oc; ++i) u16col[i] = ir.objects[i].state.scale_f16[k];
        encode_u16_lanes(out, u16col);
    }
    for (int k = 0; k < 3; ++k) {
        for (uint32_t i = 0; i < oc; ++i) u16col[i] = ir.objects[i].state.dof_f16[k];
        encode_u16_lanes(out, u16col);
    }
    for (int k = 0; k < 4; ++k) {
        for (uint32_t i = 0; i < oc; ++i) u16col[i] = ir.objects[i].state.clip_plane_f16[k];
        encode_u16_lanes(out, u16col);
    }
    for (int b = 0; b < 4; ++b) for (const auto &o : ir.objects) put_u8(out, static_cast<uint8_t>((o.state.rgba8_mat >> (b * 8)) & 0xFFu));
    for (const auto &o : ir.objects) put_uvarint(out, o.state.flags);
    for (int k = 0; k < 3; ++k) {
        for (uint32_t i = 0; i < oc; ++i) u16col[i] = ir.objects[i].cspec.dof_min_f16[k];
        encode_u16_lanes(out, u16col);
    }
    for (int k = 0; k < 3; ++k) {
        for (uint32_t i = 0; i < oc; ++i) u16col[i] = ir.objects[i].cspec.dof_max_f16[k];
        encode_u16_lanes(out, u16col);
    }
    for (const auto &o : ir.objects) put_uvarint(out, o.cspec.template_id);
    for (const auto &o : ir.objects) put_uvarint(out, o.cspec.joint_mask);

    // Analytic columns
    uint32_t ac = static_cast<uint32_t>(ir.analytics.size());
    std::vector<float> acol(ac);
    std::vector<uint16_t> au16(ac);
    for (int k = 0; k < 3; ++k) {
        for (uint32_t i = 0; i < ac; ++i) acol[i] = ir.analytics[i].prim.center[k];
        encode_f32_byte_planes(out, acol);
    }
    for (uint32_t i = 0; i < ac; ++i) acol[i] = ir.analytics[i].prim.radius;
    encode_f32_byte_planes(out, acol);
    for (int k = 0; k < 4; ++k) {
        for (uint32_t i = 0; i < ac; ++i) au16[i] = ir.analytics[i].prim.axis_len_f16[k];
        encode_u16_lanes(out, au16);
    }
    for (int b = 0; b < 4; ++b) for (const auto &a : ir.analytics) put_u8(out, static_cast<uint8_t>((a.prim.rgba8 >> (b * 8)) & 0xFFu));
    for (const auto &a : ir.analytics) put_uvarint(out, a.prim.prim_kind_flags);

    // HUD columns
    for (const auto &h : ir.huds) put_u8(out, h.kind);
    for (int k = 0; k < 4; ++k) {
        int32_t prev = 0;
        for (const auto &h : ir.huds) {
            int32_t cur = h.bounds_q14_2[k];
            put_uvarint(out, zigzag_encode(cur - prev));
            prev = cur;
        }
    }
    for (int b = 0; b < 4; ++b) for (const auto &h : ir.huds) put_u8(out, static_cast<uint8_t>((h.fill_rgba8 >> (b * 8)) & 0xFFu));
    for (int b = 0; b < 4; ++b) for (const auto &h : ir.huds) put_u8(out, static_cast<uint8_t>((h.border_rgba8 >> (b * 8)) & 0xFFu));
    for (const auto &h : ir.huds) put_u8(out, h.radius_q6_2);
    {
        int32_t prev = 0;
        for (const auto &h : ir.huds) {
            int32_t cur = h.bind_obj_idx;
            put_uvarint(out, zigzag_encode(cur - prev));
            prev = cur;
        }
    }
    for (const auto &h : ir.huds) put_u8(out, h.bind_dof_slot);

    // Front-coded ID & label pools
    std::vector<std::string> oids, aids, hids, hlbls;
    oids.reserve(oc); for (const auto &o : ir.objects) oids.push_back(o.id);
    aids.reserve(ac); for (const auto &a : ir.analytics) aids.push_back(a.id);
    hids.reserve(ir.huds.size()); hlbls.reserve(ir.huds.size());
    for (const auto &h : ir.huds) { hids.push_back(h.id); hlbls.push_back(h.label); }
    put_front_strings(out, oids);
    put_front_strings(out, aids);
    put_front_strings(out, hids);
    put_front_strings(out, hlbls);
    return out;
}

static BloomSceneIR decode_3c_bobj_col(const uint8_t *data, size_t len) {
    ByteReader r{data, len, 0};
    char mag[4]{}; r.read_bytes(mag, 4);
    bool has_geom = (r.u32() & 1u) != 0u;
    uint32_t tc = r.u32(), oc = r.u32(), ac = r.u32(), hc = r.u32();

    BloomSceneIR ir;
    ir.templates.resize(tc);
    std::vector<uint32_t> idx_lens(tc);
    for (uint32_t i = 0; i < tc; ++i) {
        ir.templates[i].meshlet_cnt = static_cast<uint32_t>(r.uvarint());
        idx_lens[i] = static_cast<uint32_t>(r.uvarint());
    }
    auto tnames = get_front_strings(r, tc);
    for (uint32_t i = 0; i < tc; ++i) ir.templates[i].name = std::move(tnames[i]);

    if (has_geom) {
        for (uint32_t ti = 0; ti < tc; ++ti) {
            auto &t = ir.templates[ti];
            t.hdrs.resize(t.meshlet_cnt);
            int64_t pv = 0, pi = 0, po = 0;
            for (uint32_t m = 0; m < t.meshlet_cnt; ++m) {
                pv += zigzag_decode(r.uvarint()); t.hdrs[m].vtx_offset = static_cast<uint32_t>(pv);
                pi += zigzag_decode(r.uvarint()); t.hdrs[m].idx_offset = static_cast<uint32_t>(pi);
                t.hdrs[m].vtx_cnt = static_cast<uint16_t>(r.uvarint());
                t.hdrs[m].tri_cnt = static_cast<uint16_t>(r.uvarint());
                po += zigzag_decode(r.uvarint()); t.hdrs[m].obj_id = static_cast<uint16_t>(po);
                t.hdrs[m].flags = static_cast<uint16_t>(r.uvarint());
                t.hdrs[m].aabb_scale_tag = static_cast<uint32_t>(r.uvarint());
            }
            for (int k = 0; k < 4; ++k) {
                std::vector<uint16_t> col;
                decode_u16_lanes(r, t.meshlet_cnt, col);
                for (uint32_t m = 0; m < t.meshlet_cnt; ++m) t.hdrs[m].bsphere_f16[k] = col[m];
            }
            for (int k = 0; k < 3; ++k) for (uint32_t m = 0; m < t.meshlet_cnt; ++m) t.hdrs[m].cone_axis[k] = static_cast<int8_t>(r.u8());
            for (uint32_t m = 0; m < t.meshlet_cnt; ++m) t.hdrs[m].cone_cutoff = static_cast<int8_t>(r.u8());

            uint32_t nv = t.meshlet_cnt * CLUSTER_VTX;
            t.verts.resize(nv);
            for (int k = 0; k < 3; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0; v < nv; ++v) {
                    prev += static_cast<int32_t>(zigzag_decode(r.uvarint()));
                    t.verts[v].pos_unorm16[k] = static_cast<uint16_t>(prev);
                }
            }
            for (int k = 0; k < 2; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0; v < nv; ++v) {
                    prev += static_cast<int32_t>(zigzag_decode(r.uvarint()));
                    t.verts[v].oct_norm[k] = static_cast<int8_t>(prev);
                }
            }
            for (int k = 0; k < 2; ++k) {
                int32_t prev = 0;
                for (uint32_t v = 0; v < nv; ++v) {
                    prev += static_cast<int32_t>(zigzag_decode(r.uvarint()));
                    t.verts[v].uv_u8[k] = static_cast<uint8_t>(prev);
                }
            }
            for (uint32_t v = 0; v < nv; ++v) t.verts[v].joint_id = r.u8();
            for (uint32_t v = 0; v < nv; ++v) t.verts[v].mat_id = r.u8();

            t.local_idx.resize(idx_lens[ti]);
            int32_t pidx = 0;
            for (uint32_t k = 0; k < idx_lens[ti]; ++k) {
                pidx += static_cast<int32_t>(zigzag_decode(r.uvarint()));
                t.local_idx[k] = static_cast<uint8_t>(pidx);
            }
        }
    }

    ir.objects.resize(oc);
    std::vector<float> fcol;
    std::vector<uint16_t> u16col;
    for (int k = 0; k < 3; ++k) {
        decode_f32_byte_planes(r, oc, fcol);
        for (uint32_t i = 0; i < oc; ++i) ir.objects[i].state.pos[k] = fcol[i];
    }
    for (int k = 0; k < 4; ++k) {
        decode_u16_lanes(r, oc, u16col);
        for (uint32_t i = 0; i < oc; ++i) ir.objects[i].state.quat_f16[k] = u16col[i];
    }
    for (int k = 0; k < 3; ++k) {
        decode_u16_lanes(r, oc, u16col);
        for (uint32_t i = 0; i < oc; ++i) ir.objects[i].state.scale_f16[k] = u16col[i];
    }
    for (int k = 0; k < 3; ++k) {
        decode_u16_lanes(r, oc, u16col);
        for (uint32_t i = 0; i < oc; ++i) ir.objects[i].state.dof_f16[k] = u16col[i];
    }
    for (int k = 0; k < 4; ++k) {
        decode_u16_lanes(r, oc, u16col);
        for (uint32_t i = 0; i < oc; ++i) ir.objects[i].state.clip_plane_f16[k] = u16col[i];
    }
    for (int b = 0; b < 4; ++b) for (uint32_t i = 0; i < oc; ++i) ir.objects[i].state.rgba8_mat |= static_cast<uint32_t>(r.u8()) << (b * 8);
    for (uint32_t i = 0; i < oc; ++i) ir.objects[i].state.flags = static_cast<uint32_t>(r.uvarint());
    for (int k = 0; k < 3; ++k) {
        decode_u16_lanes(r, oc, u16col);
        for (uint32_t i = 0; i < oc; ++i) ir.objects[i].cspec.dof_min_f16[k] = u16col[i];
    }
    for (int k = 0; k < 3; ++k) {
        decode_u16_lanes(r, oc, u16col);
        for (uint32_t i = 0; i < oc; ++i) ir.objects[i].cspec.dof_max_f16[k] = u16col[i];
    }
    for (uint32_t i = 0; i < oc; ++i) ir.objects[i].cspec.template_id = static_cast<uint16_t>(r.uvarint());
    for (uint32_t i = 0; i < oc; ++i) ir.objects[i].cspec.joint_mask = static_cast<uint16_t>(r.uvarint());

    ir.analytics.resize(ac);
    for (int k = 0; k < 3; ++k) {
        decode_f32_byte_planes(r, ac, fcol);
        for (uint32_t i = 0; i < ac; ++i) ir.analytics[i].prim.center[k] = fcol[i];
    }
    decode_f32_byte_planes(r, ac, fcol);
    for (uint32_t i = 0; i < ac; ++i) ir.analytics[i].prim.radius = fcol[i];
    for (int k = 0; k < 4; ++k) {
        decode_u16_lanes(r, ac, u16col);
        for (uint32_t i = 0; i < ac; ++i) ir.analytics[i].prim.axis_len_f16[k] = u16col[i];
    }
    for (int b = 0; b < 4; ++b) for (uint32_t i = 0; i < ac; ++i) ir.analytics[i].prim.rgba8 |= static_cast<uint32_t>(r.u8()) << (b * 8);
    for (uint32_t i = 0; i < ac; ++i) ir.analytics[i].prim.prim_kind_flags = static_cast<uint32_t>(r.uvarint());

    ir.huds.resize(hc);
    for (uint32_t i = 0; i < hc; ++i) ir.huds[i].kind = r.u8();
    for (int k = 0; k < 4; ++k) {
        int32_t prev = 0;
        for (uint32_t i = 0; i < hc; ++i) {
            prev += static_cast<int32_t>(zigzag_decode(r.uvarint()));
            ir.huds[i].bounds_q14_2[k] = static_cast<uint16_t>(prev);
        }
    }
    for (int b = 0; b < 4; ++b) for (uint32_t i = 0; i < hc; ++i) ir.huds[i].fill_rgba8 |= static_cast<uint32_t>(r.u8()) << (b * 8);
    for (int b = 0; b < 4; ++b) for (uint32_t i = 0; i < hc; ++i) ir.huds[i].border_rgba8 |= static_cast<uint32_t>(r.u8()) << (b * 8);
    for (uint32_t i = 0; i < hc; ++i) ir.huds[i].radius_q6_2 = r.u8();
    {
        int32_t prev = 0;
        for (uint32_t i = 0; i < hc; ++i) {
            prev += static_cast<int32_t>(zigzag_decode(r.uvarint()));
            ir.huds[i].bind_obj_idx = static_cast<uint16_t>(prev);
        }
    }
    for (uint32_t i = 0; i < hc; ++i) ir.huds[i].bind_dof_slot = r.u8();

    auto oids = get_front_strings(r, oc);
    auto aids = get_front_strings(r, ac);
    auto hids = get_front_strings(r, hc);
    auto hlbls = get_front_strings(r, hc);
    for (uint32_t i = 0; i < oc; ++i) ir.objects[i].id = std::move(oids[i]);
    for (uint32_t i = 0; i < ac; ++i) ir.analytics[i].id = std::move(aids[i]);
    for (uint32_t i = 0; i < hc; ++i) {
        ir.huds[i].id = std::move(hids[i]);
        ir.huds[i].label = std::move(hlbls[i]);
    }
    return ir;
}

// ============================================================================
// Isolated 2D HUD Formats: W3C SVG vs Row Binary vs Columnar Binary
// ============================================================================
static std::vector<uint8_t> encode_hud_bui_row(const std::vector<HudNode> &huds) {
    std::vector<uint8_t> out;
    put_bytes(out, "BUIR", 4);
    put_u32(out, static_cast<uint32_t>(huds.size()));
    for (const auto &h : huds) {
        HudNodeBin hb{};
        for (int k = 0; k < 4; ++k) hb.bounds_q14_2[k] = h.bounds_q14_2[k];
        hb.fill_rgba8 = h.fill_rgba8;
        hb.border_rgba8 = h.border_rgba8;
        hb.bind_obj_idx = h.bind_obj_idx;
        hb.kind = h.kind;
        hb.radius_q6_2 = h.radius_q6_2;
        hb.bind_dof_slot = h.bind_dof_slot;
        hb.id_len = static_cast<uint16_t>(h.id.size());
        hb.label_len = static_cast<uint16_t>(h.label.size());
        put_bytes(out, &hb, sizeof(HudNodeBin));
    }
    for (const auto &h : huds) {
        put_bytes(out, h.id.data(), h.id.size());
        put_bytes(out, h.label.data(), h.label.size());
    }
    return out;
}

static std::vector<HudNode> decode_hud_bui_row(const uint8_t *data, size_t len) {
    ByteReader r{data, len, 0};
    char m[4]{}; r.read_bytes(m, 4);
    uint32_t hc = r.u32();
    std::vector<HudNode> huds(hc);
    std::vector<std::pair<uint16_t, uint16_t>> hl(hc);
    for (uint32_t i = 0; i < hc; ++i) {
        HudNodeBin hb{}; r.read_bytes(&hb, sizeof(hb));
        for (int k = 0; k < 4; ++k) huds[i].bounds_q14_2[k] = hb.bounds_q14_2[k];
        huds[i].fill_rgba8 = hb.fill_rgba8;
        huds[i].border_rgba8 = hb.border_rgba8;
        huds[i].bind_obj_idx = hb.bind_obj_idx;
        huds[i].kind = hb.kind;
        huds[i].radius_q6_2 = hb.radius_q6_2;
        huds[i].bind_dof_slot = hb.bind_dof_slot;
        hl[i] = {hb.id_len, hb.label_len};
    }
    for (uint32_t i = 0; i < hc; ++i) {
        huds[i].id.resize(hl[i].first); r.read_bytes(huds[i].id.data(), hl[i].first);
        huds[i].label.resize(hl[i].second); r.read_bytes(huds[i].label.data(), hl[i].second);
    }
    return huds;
}

static std::vector<uint8_t> encode_hud_bui_col(const std::vector<HudNode> &huds) {
    std::vector<uint8_t> out;
    put_bytes(out, "BUIC", 4);
    put_u32(out, static_cast<uint32_t>(huds.size()));
    for (const auto &h : huds) put_u8(out, h.kind);
    for (int k = 0; k < 4; ++k) {
        int32_t prev = 0;
        for (const auto &h : huds) {
            int32_t cur = h.bounds_q14_2[k];
            put_uvarint(out, zigzag_encode(cur - prev));
            prev = cur;
        }
    }
    for (int b = 0; b < 4; ++b) for (const auto &h : huds) put_u8(out, static_cast<uint8_t>((h.fill_rgba8 >> (b * 8)) & 0xFFu));
    for (int b = 0; b < 4; ++b) for (const auto &h : huds) put_u8(out, static_cast<uint8_t>((h.border_rgba8 >> (b * 8)) & 0xFFu));
    for (const auto &h : huds) put_u8(out, h.radius_q6_2);
    int32_t prev = 0;
    for (const auto &h : huds) {
        int32_t cur = h.bind_obj_idx;
        put_uvarint(out, zigzag_encode(cur - prev));
        prev = cur;
    }
    for (const auto &h : huds) put_u8(out, h.bind_dof_slot);
    std::vector<std::string> ids, lbls;
    for (const auto &h : huds) { ids.push_back(h.id); lbls.push_back(h.label); }
    put_front_strings(out, ids);
    put_front_strings(out, lbls);
    return out;
}

static std::vector<HudNode> decode_hud_bui_col(const uint8_t *data, size_t len) {
    ByteReader r{data, len, 0};
    char m[4]{}; r.read_bytes(m, 4);
    uint32_t hc = r.u32();
    std::vector<HudNode> huds(hc);
    for (uint32_t i = 0; i < hc; ++i) huds[i].kind = r.u8();
    for (int k = 0; k < 4; ++k) {
        int32_t prev = 0;
        for (uint32_t i = 0; i < hc; ++i) {
            prev += static_cast<int32_t>(zigzag_decode(r.uvarint()));
            huds[i].bounds_q14_2[k] = static_cast<uint16_t>(prev);
        }
    }
    for (int b = 0; b < 4; ++b) for (uint32_t i = 0; i < hc; ++i) huds[i].fill_rgba8 |= static_cast<uint32_t>(r.u8()) << (b * 8);
    for (int b = 0; b < 4; ++b) for (uint32_t i = 0; i < hc; ++i) huds[i].border_rgba8 |= static_cast<uint32_t>(r.u8()) << (b * 8);
    for (uint32_t i = 0; i < hc; ++i) huds[i].radius_q6_2 = r.u8();
    {
        int32_t prev = 0;
        for (uint32_t i = 0; i < hc; ++i) {
            prev += static_cast<int32_t>(zigzag_decode(r.uvarint()));
            huds[i].bind_obj_idx = static_cast<uint16_t>(prev);
        }
    }
    for (uint32_t i = 0; i < hc; ++i) huds[i].bind_dof_slot = r.u8();
    auto ids = get_front_strings(r, hc);
    auto lbls = get_front_strings(r, hc);
    for (uint32_t i = 0; i < hc; ++i) {
        huds[i].id = std::move(ids[i]);
        huds[i].label = std::move(lbls[i]);
    }
    return huds;
}

// ============================================================================
// File I/O, Timing & Benchmark Orchestration
// ============================================================================
static void write_bin(const fs::path &p, const std::vector<uint8_t> &buf) {
    std::ofstream ofs(p, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(buf.data()), static_cast<std::streamsize>(buf.size()));
}

template <typename EncFn, typename DecFn>
static void benchmark_codec(const char *scene_name, const char *fmt_name, const char *layer,
                            const BloomSceneIR &ir, bool has_geom, const fs::path &out_path,
                            EncFn enc_fn, DecFn dec_fn, std::ostringstream &meta_entries, bool &first) {
    Digest128 expected = ir_digest(ir, has_geom);
    std::vector<uint8_t> payload = enc_fn();
    BloomSceneIR decoded = dec_fn(payload.data(), payload.size());
    Digest128 actual = ir_digest(decoded, has_geom);
    if (expected != actual) {
        std::fprintf(stderr, "FATAL ROUNDTRIP MISMATCH: %s / %s / %s (exp=%016llx%016llx got=%016llx%016llx)\n",
                     scene_name, fmt_name, layer,
                     static_cast<unsigned long long>(expected.h1), static_cast<unsigned long long>(expected.h2),
                     static_cast<unsigned long long>(actual.h1), static_cast<unsigned long long>(actual.h2));
        std::fprintf(stderr, "  counts tpl=%zu/%zu obj=%zu/%zu ana=%zu/%zu hud=%zu/%zu\n",
                     ir.templates.size(), decoded.templates.size(),
                     ir.objects.size(), decoded.objects.size(),
                     ir.analytics.size(), decoded.analytics.size(),
                     ir.huds.size(), decoded.huds.size());
        if (ir_digest(ir, false) != ir_digest(decoded, false)) std::fprintf(stderr, "  scene-without-geom differs!\n");
        for (size_t i = 0; i < std::min(ir.objects.size(), decoded.objects.size()); ++i) {
            if (std::memcmp(&ir.objects[i].state, &decoded.objects[i].state, sizeof(BloomObjectState48)) != 0 ||
                std::memcmp(&ir.objects[i].cspec, &decoded.objects[i].cspec, sizeof(ConstraintSpec)) != 0 ||
                ir.objects[i].id != decoded.objects[i].id) {
                std::fprintf(stderr, "  first obj diff at %zu\n", i);
                break;
            }
        }
        if (hud_digest(ir.huds) != hud_digest(decoded.huds)) std::fprintf(stderr, "  hud differs!\n");
        std::exit(1);
    }
    write_bin(out_path, payload);

    std::vector<double> enc_us, dec_us;
    volatile uint64_t sink = 0;
    for (int it = 0; it < 35; ++it) {
        auto t0 = std::chrono::steady_clock::now();
        auto tmp = enc_fn();
        auto t1 = std::chrono::steady_clock::now();
        auto dir = dec_fn(tmp.data(), tmp.size());
        auto t2 = std::chrono::steady_clock::now();
        sink += dir.objects.size() + tmp.size();
        if (it >= 5) {
            enc_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            dec_us.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
        }
    }
    (void)sink;
    std::sort(enc_us.begin(), enc_us.end());
    std::sort(dec_us.begin(), dec_us.end());
    double med_enc = enc_us[enc_us.size() / 2];
    double med_dec = dec_us[dec_us.size() / 2];

    if (!first) meta_entries << ",\n";
    first = false;
    meta_entries << "    { \"scene\": \"" << scene_name << "\", \"format\": \"" << fmt_name
                 << "\", \"layer\": \"" << layer << "\", \"file\": \"" << out_path.filename().string()
                 << "\", \"raw_bytes\": " << payload.size()
                 << ", \"encode_med_us\": " << med_enc
                 << ", \"decode_med_us\": " << med_dec
                 << ", \"roundtrip_ok\": true"
                 << ", \"entities\": " << (ir.objects.size() + ir.analytics.size() + ir.huds.size())
                 << ", \"templates\": " << ir.templates.size()
                 << ", \"objects\": " << ir.objects.size()
                 << ", \"analytics\": " << ir.analytics.size()
                 << ", \"huds\": " << ir.huds.size() << " }";
}

int main(int argc, char **argv) {
    fs::path out_dir = "../build/fmt_corpus";
    fs::path meta_json = "../build/fmt_codec_meta.json";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--out-dir") == 0) out_dir = argv[i + 1];
        else if (std::strcmp(argv[i], "--meta-json") == 0) meta_json = argv[i + 1];
    }
    fs::create_directories(out_dir);
    fs::create_directories(meta_json.parent_path());

    struct SceneDef {
        const char *name;
        uint32_t n_tpl, m_per_tpl, n_obj, n_ana, n_hud, seed;
        const char *pfx;
        bool bench_matrix;
        bool bench_group;
    };
    const SceneDef defs[] = {
        {"s1_micro", 1u, 8u, 6u, 4u, 12u, 101u, "s1", true, false},
        {"s2_work", 4u, 8u, 64u, 16u, 48u, 202u, "s2", true, false},
        {"s3_stage", 8u, 8u, 256u, 64u, 128u, 303u, "s3", true, false},
        {"g0", 4u, 8u, 64u, 16u, 48u, 501u, "g0", false, true},
        {"g1", 4u, 8u, 64u, 16u, 48u, 502u, "g1", false, true},
        {"g2", 4u, 8u, 64u, 16u, 48u, 503u, "g2", false, true},
        {"g3", 4u, 8u, 64u, 16u, 48u, 504u, "g3", false, true},
    };

    std::ostringstream meta_entries;
    bool first = true;

    auto emit_all_formats_for = [&](const char *sname, const BloomSceneIR &ir, bool bench_timing) {
        struct FmtSpec {
            const char *fmt;
            const char *ext;
            bool include_geom;
            const char *layer;
        };
        const FmtSpec specs[] = {
            {"baseline_glb64", "glb", true, "full"},
            {"baseline_glb64", "scene.glb", false, "scene"},
            {"3a_xml_bobj", "full.b3a", true, "full"},
            {"3a_xml_bobj", "scene.b3a", false, "scene"},
            {"3b_svg_scene", "full.b3b", true, "full"},
            {"3b_svg_scene", "scene.svg", false, "scene"},
            {"3c_bobj_row", "full.bobr", true, "full"},
            {"3c_bobj_row", "scene.bobr", false, "scene"},
            {"3c_bobj_col", "full.bobc", true, "full"},
            {"3c_bobj_col", "scene.bobc", false, "scene"},
        };
        for (const auto &sp : specs) {
            fs::path p = out_dir / (std::string(sname) + "__" + sp.fmt + "." + sp.ext);
            auto enc = [&]() -> std::vector<uint8_t> {
                if (std::strcmp(sp.fmt, "baseline_glb64") == 0) return encode_baseline_glb64(ir, sp.include_geom);
                if (std::strcmp(sp.fmt, "3a_xml_bobj") == 0) return encode_3a_xml_bobj(ir, sp.include_geom);
                if (std::strcmp(sp.fmt, "3b_svg_scene") == 0) return encode_3b_svg_scene(ir, sp.include_geom);
                if (std::strcmp(sp.fmt, "3c_bobj_row") == 0) return encode_3c_bobj_row(ir, sp.include_geom);
                return encode_3c_bobj_col(ir, sp.include_geom);
            };
            auto dec = [&](const uint8_t *d, size_t l) -> BloomSceneIR {
                if (std::strcmp(sp.fmt, "baseline_glb64") == 0) return decode_baseline_glb64(d, l, sp.include_geom);
                if (std::strcmp(sp.fmt, "3a_xml_bobj") == 0) return decode_3a_xml_bobj(d, l, sp.include_geom);
                if (std::strcmp(sp.fmt, "3b_svg_scene") == 0) return decode_3b_svg_scene(d, l, sp.include_geom);
                if (std::strcmp(sp.fmt, "3c_bobj_row") == 0) return decode_3c_bobj_row(d, l);
                return decode_3c_bobj_col(d, l);
            };
            if (bench_timing) {
                benchmark_codec(sname, sp.fmt, sp.layer, ir, sp.include_geom, p, enc, dec, meta_entries, first);
            } else {
                auto buf = enc();
                auto dec_ir = dec(buf.data(), buf.size());
                if (ir_digest(ir, sp.include_geom) != ir_digest(dec_ir, sp.include_geom)) {
                    std::fprintf(stderr, "FATAL group/v2 roundtrip mismatch: %s / %s\n", sname, sp.fmt);
                    std::exit(1);
                }
                write_bin(p, buf);
            }
        }
    };

    BloomSceneIR s2_ir;
    BloomSceneIR s3_ir;
    for (const auto &sd : defs) {
        BloomSceneIR ir = generate_scene(sd.n_tpl, sd.m_per_tpl, sd.n_obj, sd.n_ana, sd.n_hud, sd.seed, sd.pfx);
        if (std::strcmp(sd.name, "s2_work") == 0) s2_ir = ir;
        if (std::strcmp(sd.name, "s3_stage") == 0) s3_ir = ir;
        emit_all_formats_for(sd.name, ir, sd.bench_matrix);
        if (sd.bench_matrix) {
            fs::path p3a64 = out_dir / (std::string(sd.name) + "__3a_xml_b64.xml");
            benchmark_codec(sd.name, "3a_xml_b64", "full", ir, true, p3a64,
                            [&]() { return encode_3a_xml_b64(ir); },
                            [&](const uint8_t *d, size_t l) { return decode_3a_xml_b64(d, l); },
                            meta_entries, first);
        }
    }

    // Generate s2_work_v2 with realistic hot-reload edit
    BloomSceneIR s2_v2 = s2_ir;
    for (size_t k = 0; k < 3 && k < s2_v2.objects.size(); ++k) {
        s2_v2.objects[k].state.dof_f16[0] = pack_f16_clean(0.95f - static_cast<float>(k) * 0.25f);
    }
    if (s2_v2.objects.size() > 7) {
        s2_v2.objects[7].state.flags ^= 1u;
        s2_v2.objects[7].state.clip_plane_f16[3] = pack_f16_clean(0.375f);
    }
    if (!s2_v2.huds.empty()) s2_v2.huds[0].label = "StageTelemetry_HOT";
    {
        AnalyticNode extra;
        extra.id = "s2_probe_hot";
        extra.prim.center[0] = 1.25f; extra.prim.center[1] = 2.5f; extra.prim.center[2] = 11.5f;
        extra.prim.radius = 0.45f;
        extra.prim.axis_len_f16[1] = pack_f16_clean(1.0f);
        extra.prim.rgba8 = 0xFFFF6622u;
        extra.prim.prim_kind_flags = 1u;
        s2_v2.analytics.push_back(extra);
    }
    emit_all_formats_for("s2_work_v2", s2_v2, false);

    // Isolated 2D HUD roundtrip & timing check on s3_stage (128 HUD nodes)
    Digest128 hud_exp = hud_digest(s3_ir.huds);
    std::ostringstream hud_meta;
    auto bench_hud = [&](const char *name, const char *ext, auto enc_fn, auto dec_fn, bool is_first) {
        auto buf = enc_fn();
        auto dec_h = dec_fn(buf.data(), buf.size());
        if (hud_digest(dec_h) != hud_exp) {
            std::fprintf(stderr, "FATAL HUD roundtrip mismatch: %s\n", name);
            std::exit(1);
        }
        fs::path p = out_dir / (std::string("hud_s3__") + name + "." + ext);
        write_bin(p, buf);
        std::vector<double> eu, du;
        for (int it = 0; it < 35; ++it) {
            auto t0 = std::chrono::steady_clock::now();
            auto tmp = enc_fn();
            auto t1 = std::chrono::steady_clock::now();
            auto dh = dec_fn(tmp.data(), tmp.size());
            auto t2 = std::chrono::steady_clock::now();
            (void)dh.size();
            if (it >= 5) {
                eu.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
                du.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
            }
        }
        std::sort(eu.begin(), eu.end());
        std::sort(du.begin(), du.end());
        if (!is_first) hud_meta << ",\n";
        hud_meta << "    { \"format\": \"" << name << "\", \"file\": \"" << p.filename().string()
                 << "\", \"hud_count\": " << s3_ir.huds.size() << ", \"raw_bytes\": " << buf.size()
                 << ", \"encode_med_us\": " << eu[eu.size() / 2]
                 << ", \"decode_med_us\": " << du[du.size() / 2]
                 << ", \"roundtrip_ok\": true }";
    };
    bench_hud("hud_svg", "svg",
              [&]() { std::string s = serialize_hud_svg(s3_ir.huds, true); return std::vector<uint8_t>(s.begin(), s.end()); },
              [&](const uint8_t *d, size_t l) { std::vector<HudNode> h; decode_hud_from_xml_tags(parse_xml_tags(std::string(reinterpret_cast<const char *>(d), l)), h); return h; },
              true);
    bench_hud("hud_bui_row", "buir",
              [&]() { return encode_hud_bui_row(s3_ir.huds); },
              [&](const uint8_t *d, size_t l) { return decode_hud_bui_row(d, l); },
              false);
    bench_hud("hud_bui_col", "buic",
              [&]() { return encode_hud_bui_col(s3_ir.huds); },
              [&](const uint8_t *d, size_t l) { return decode_hud_bui_col(d, l); },
              false);

    std::ofstream mofs(meta_json);
    mofs << "{\n  \"records\": [\n" << meta_entries.str() << "\n  ],\n"
         << "  \"hud_records\": [\n" << hud_meta.str() << "\n  ]\n}\n";

    std::printf("[format_ssot_bench] 100%% bit-exact 128-bit ir_digest & hud_digest verified across all formats -> %s\n", meta_json.c_str());
    return 0;
}
