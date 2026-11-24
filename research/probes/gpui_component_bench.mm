// gpui_component_bench.mm — Empirical Head-to-Head Benchmark: Zed GPUI vs BLOOM vNext Components on Apple M1
// Standalone C++17 / Objective-C++ probe (zero FLOW, zero SDL3, zero external deps).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>

namespace {

static uint64_t now_ns() {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static inline uint16_t pack_q14_2(float px) {
    float v = px * 4.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 65535.0f) v = 65535.0f;
    return static_cast<uint16_t>(v + 0.5f);
}

static inline uint8_t pack_q6_2(float px) {
    float v = px * 4.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return static_cast<uint8_t>(v + 0.5f);
}

static inline uint16_t f32_to_f16_hw(float v) {
    _Float16 h = static_cast<_Float16>(v);
    uint16_t bits;
    std::memcpy(&bits, &h, sizeof(bits));
    return bits;
}

struct Stats {
    double min_val = 0.0;
    double median_val = 0.0;
    double p95_val = 0.0;
    double mean_val = 0.0;
};

static Stats compute_stats(std::vector<double> v) {
    Stats s{};
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.min_val = v.front();
    size_t n = v.size();
    s.median_val = (n & 1u) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    size_t idx95 = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(n))) - 1;
    if (idx95 >= n) idx95 = n - 1;
    s.p95_val = v[idx95];
    double sum = 0.0;
    for (double x : v) sum += x;
    s.mean_val = sum / static_cast<double>(n);
    return s;
}

// ============================================================================
// Exact GPUI #[repr(C)] Layouts Verified from zed-main/crates/gpui/src/scene.rs & color.rs
// ============================================================================
struct RawF2 {
    float x, y;
};
struct RawI2 {
    int32_t x, y;
};
struct RawF4 {
    float x, y, z, w;
};
struct RawI4 {
    int32_t x, y, w, h;
};

struct GpuiAtlasTile32 {
    uint32_t tex_index;
    uint32_t tex_kind;
    uint32_t tile_id;
    uint32_t padding;
    RawI4 bounds; // 16 B
};
static_assert(sizeof(GpuiAtlasTile32) == 32, "AtlasTile must be 32 B");

struct GpuiBackground72 {
    uint32_t tag;
    uint32_t color_space;
    RawF4 solid;
    float grad_angle;
    RawF4 stop0_color;
    float stop0_pct;
    RawF4 stop1_color;
    float stop1_pct;
    uint32_t pad;
};
static_assert(sizeof(GpuiBackground72) == 72, "Background must be 72 B");

struct GpuiQuad160 {
    uint32_t order;
    uint32_t border_style;
    RawF4 bounds;
    RawF4 content_mask;
    GpuiBackground72 background;
    RawF4 border_color;
    RawF4 corner_radii;
    RawF4 border_widths;
};
static_assert(sizeof(GpuiQuad160) == 160, "GpuiQuad160 must be exact 160 B");

struct GpuiMonoSprite112 {
    uint32_t order;
    uint32_t animation;
    GpuiAtlasTile32 tile;
    RawF4 bounds;
    RawF4 content_mask;
    RawF4 color;          // Hsla
    RawF4 rot_scale;      // [[f32;2];2]
    RawF2 translation;
};
static_assert(sizeof(GpuiMonoSprite112) == 112, "GpuiMonoSprite112 must be exact 112 B");

struct GpuiShadow112 {
    uint32_t order;
    float blur_radius;
    RawF4 bounds;
    RawF4 corner_radii;
    RawF4 content_mask;
    RawF4 color;
    RawF4 shape_bounds;
    RawF4 shape_corner_radii;
    uint32_t inset;
    uint32_t pad;
};
static_assert(sizeof(GpuiShadow112) == 112, "GpuiShadow112 must be exact 112 B");

struct GpuiUnderline64 {
    uint32_t order;
    uint32_t pad;
    RawF4 bounds;
    RawF4 content_mask;
    RawF4 color;
    float thickness;
    uint32_t wavy;
};
static_assert(sizeof(GpuiUnderline64) == 64, "GpuiUnderline64 must be exact 64 B");

struct GpuiPathCurve40 {
    RawF2 p0, p1, a, b;
    float sign;
    uint32_t pad;
};
static_assert(sizeof(GpuiPathCurve40) == 40, "GpuiPathCurve40 must be exact 40 B");

struct GpuiPathPaint112 {
    RawF4 bounds;
    RawF4 content_mask;
    GpuiBackground72 color;
    uint32_t even_odd;
    uint32_t pad;
};
static_assert(sizeof(GpuiPathPaint112) == 112, "GpuiPathPaint112 must be exact 112 B");

struct GpuiPathTile56 {
    uint32_t paint;
    uint32_t curve_start;
    uint32_t curve_count;
    int32_t backdrop;
    RawF2 top_left;
    RawF2 bottom_right;
    RawF2 reference;
    RawF2 curves_top_left;
    RawF2 curves_bottom_right;
};
static_assert(sizeof(GpuiPathTile56) == 56, "GpuiPathTile56 must be exact 56 B");

// ============================================================================
// BLOOM vNext Compact GPU-Native Primitives (Zero FLOW)
// ============================================================================
struct alignas(4) BloomQuadHot {
    uint16_t bounds_q14_2[4]; // 8 B: x, y, w, h @ 0.25px
    uint32_t rgba8_fill;      // 4 B
    uint32_t rgba8_border;    // 4 B
    uint8_t  radii_q6_2[4];   // 4 B: 4 corners @ 0.25px
    uint8_t  borders_q6_2[4]; // 4 B: 4 borders @ 0.25px
    uint16_t clip_id;         // 2 B
    uint16_t style_id;        // 2 B
    uint32_t z_flags;         // 4 B
};
static_assert(sizeof(BloomQuadHot) == 32, "BloomQuadHot must be 32 B");

struct alignas(4) BloomGlyphInst {
    uint16_t dst_q14_2[4];   // 8 B: x, y, w, h @ 0.25px
    uint16_t uv_unorm16[4];  // 8 B: u0, v0, du, dv
    uint32_t rgba8;          // 4 B
    uint16_t clip_id;        // 2 B
    uint16_t z_flags;        // 2 B
};
static_assert(sizeof(BloomGlyphInst) == 24, "BloomGlyphInst must be 24 B");

struct alignas(4) BloomShadowHot {
    uint16_t bounds_q14_2[4];    // 8 B: shadow screen bounds
    uint16_t shape_params[4];    // 8 B: fp16 (half_w, half_h, s_inv, min_edge)
    uint16_t blur_r1_exp_sc[4];  // 8 B: fp16 (r1, exp, scale, corner_r)
    uint32_t rgba8;              // 4 B
    uint16_t clip_id;            // 2 B
    uint16_t z_flags;            // 2 B
};
static_assert(sizeof(BloomShadowHot) == 32, "BloomShadowHot must be 32 B");

struct alignas(4) BloomUnderlineHot {
    uint16_t bounds_q14_2[4]; // 8 B
    uint32_t rgba8;           // 4 B
    uint16_t thickness_f16;   // 2 B
    uint16_t wavy_flag;       // 2 B
    uint16_t clip_id;         // 2 B
    uint16_t z_order;         // 2 B
    uint32_t pad;             // 4 B
};
static_assert(sizeof(BloomUnderlineHot) == 24, "BloomUnderlineHot must be 24 B");

struct alignas(16) BloomClipRect {
    float x, y, w, h;
};
static_assert(sizeof(BloomClipRect) == 16, "BloomClipRect must be 16 B");

// ============================================================================
// Embedded MSL Shaders (GPUI Faithful vs BLOOM vNext)
// ============================================================================
static const char *kGpuiBenchMetal = R"MSL(
#include <metal_stdlib>
using namespace metal;

constant float4 CULLED_POSITION = float4(0.0f, 0.0f, 0.0f, 1.0f);
constant uint SIMPLE_PRIMITIVE_FLAG = 1u << 31;

struct RawF2 { float x, y; };
struct RawI2 { int x, y; };
struct RawF4 { float x, y, z, w; };
struct RawI4 { int x, y, w, h; };

struct GpuiAtlasTile32 {
    uint tex_index, tex_kind, tile_id, padding;
    RawI4 bounds;
};

struct GpuiBackground72 {
    uint tag, color_space;
    RawF4 solid;
    float grad_angle;
    RawF4 stop0_color;
    float stop0_pct;
    RawF4 stop1_color;
    float stop1_pct;
    uint pad;
};

struct GpuiQuad160 {
    uint order, border_style;
    RawF4 bounds, content_mask;
    GpuiBackground72 background;
    RawF4 border_color, corner_radii, border_widths;
};

struct GpuiMonoSprite112 {
    uint order, animation;
    GpuiAtlasTile32 tile;
    RawF4 bounds, content_mask, color, rot_scale;
    RawF2 translation;
};

struct GpuiShadow112 {
    uint order;
    float blur_radius;
    RawF4 bounds, corner_radii, content_mask, color, shape_bounds, shape_corner_radii;
    uint inset, pad;
};

struct GpuiUnderline64 {
    uint order, pad;
    RawF4 bounds, content_mask, color;
    float thickness;
    uint wavy;
};

struct GpuiPathCurve40 {
    RawF2 p0, p1, a, b;
    float sign;
    uint pad;
};

struct GpuiPathPaint112 {
    RawF4 bounds, content_mask;
    GpuiBackground72 color;
    uint even_odd, pad;
};

struct GpuiPathTile56 {
    uint paint, curve_start, curve_count;
    int backdrop;
    RawF2 top_left, bottom_right, reference, curves_top_left, curves_bottom_right;
};

struct BloomQuadHot {
    ushort4 bounds_q14_2;
    uint rgba8_fill;
    uint rgba8_border;
    uchar4 radii_q6_2;
    uchar4 borders_q6_2;
    ushort clip_id;
    ushort style_id;
    uint z_flags;
};

struct BloomGlyphInst {
    ushort4 dst_q14_2;
    ushort4 uv_unorm16;
    uint rgba8;
    ushort clip_id;
    ushort z_flags;
};

struct BloomShadowHot {
    ushort4 bounds_q14_2;
    half4 shape_params;
    half4 blur_r1_exp_sc;
    uint rgba8;
    ushort clip_id;
    ushort z_flags;
};

struct BloomUnderlineHot {
    ushort4 bounds_q14_2;
    uint rgba8;
    half thickness;
    ushort wavy_flag;
    ushort clip_id;
    ushort z_order;
    uint pad;
};

struct BloomClipRect {
    float4 rect;
};

// --- Helpers matching zed-main/crates/gpui_apple/src/shaders.metal ---
float4 hsla_to_rgba(RawF4 hsla) {
    float h = hsla.x * 6.0f;
    float s = hsla.y;
    float l = hsla.z;
    float a = hsla.w;
    float c = (1.0f - fabs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabs(fmod(h, 2.0f) - 1.0f));
    float m = l - c * 0.5f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 1.0f) { r = c; g = x; }
    else if (h < 2.0f) { r = x; g = c; }
    else if (h < 3.0f) { g = c; b = x; }
    else if (h < 4.0f) { g = x; b = c; }
    else if (h < 5.0f) { r = x; b = c; }
    else { r = c; b = x; }
    return float4(r + m, g + m, b + m, a);
}

float2 to_ndc(float2 px, float2 vp) {
    return (px / vp) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
}

float erf7(float x) {
    x = clamp(x, -10.0f, 10.0f) * (2.0f / sqrt(M_PI_F));
    float xx = x * x;
    x = x + (0.24295f + (0.03395f + 0.0104f * xx) * xx) * (x * xx);
    return x / sqrt(1.0f + x * x);
}

half erf7_h(half x) {
    x = clamp(x, -10.0h, 10.0h) * half(2.0f / sqrt(M_PI_F));
    half xx = x * x;
    x = x + (0.24295h + (0.03395h + 0.0104h * xx) * xx) * (x * xx);
    return x * rsqrt(1.0h + x * x);
}

kernel void k_dvfs_warm(device const float4 *in_b [[buffer(0)]],
                        device float4 *out_b [[buffer(1)]],
                        constant uint &iters [[buffer(2)]],
                        uint gid [[thread_position_in_grid]]) {
    float4 a0 = in_b[gid & 1023u];
    float4 a1 = a0 + 0.1f, a2 = a0 + 0.2f, a3 = a0 + 0.3f;
    for (uint i = 0; i < iters; ++i) {
        a0 = fma(a0, float4(0.9999f), float4(0.0001f));
        a1 = fma(a1, float4(0.9999f), float4(0.0002f));
        a2 = fma(a2, float4(0.9999f), float4(0.0003f));
        a3 = fma(a3, float4(0.9999f), float4(0.0004f));
    }
    out_b[gid & 1023u] = (a0 + a1) + (a2 + a3);
}

// ----------------------------------------------------------------------------
// Test 1: Glyph Sprites (GPUI 112B + Shared A8 + 56B StageIn vs BLOOM 24B + Private R8)
// ----------------------------------------------------------------------------
struct GpuiSpriteIn {
    float4 pos [[position]];
    float2 tile_pos;
    float4 color [[flat]];
    float4 clip_distance;
};

vertex GpuiSpriteIn v_gpui_sprite(uint vid [[vertex_id]],
                                  uint iid [[instance_id]],
                                  device const GpuiMonoSprite112 *sprites [[buffer(0)]],
                                  constant float2 &viewport [[buffer(1)]],
                                  constant float2 &atlas_sz [[buffer(2)]]) {
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    GpuiMonoSprite112 s = sprites[iid];
    float2 b_xy = float2(s.bounds.x, s.bounds.y);
    float2 b_wh = float2(s.bounds.z, s.bounds.w);
    float2 p = uv * b_wh + b_xy;
    float2 tp = float2(
        p.x * s.rot_scale.x + p.y * s.rot_scale.y + s.translation.x,
        p.x * s.rot_scale.z + p.y * s.rot_scale.w + s.translation.y
    );
    float4 clip_d = float4(
        tp.x - s.content_mask.x,
        s.content_mask.x + s.content_mask.z - tp.x,
        tp.y - s.content_mask.y,
        s.content_mask.y + s.content_mask.w - tp.y
    );
    float2 tile_xy = float2(float(s.tile.bounds.x), float(s.tile.bounds.y));
    float2 tile_wh = float2(float(s.tile.bounds.w), float(s.tile.bounds.h));
    GpuiSpriteIn o;
    o.pos = float4(to_ndc(tp, viewport), clamp(float(s.order + 1u) / 65535.0f, 0.0f, 1.0f), 1.0f);
    o.tile_pos = (tile_xy + uv * tile_wh) / atlas_sz;
    o.color = hsla_to_rgba(s.color);
    o.clip_distance = clip_d;
    return o;
}

fragment float4 f_gpui_sprite(GpuiSpriteIn in [[stage_in]],
                              texture2d<float> atlas [[texture(0)]]) {
    if (any(in.clip_distance < float4(0.0f))) return float4(0.0f);
    constexpr sampler smp(mag_filter::linear, min_filter::linear);
    float4 samp = atlas.sample(smp, in.tile_pos);
    float4 col = in.color;
    col.a *= samp.a;
    return col;
}

struct BloomSpriteClipVertIn {
    float4 pos [[position]];
    half2 tile_uv;
    half4 color [[flat]];
};

vertex BloomSpriteClipVertIn v_bloom_glyph_clipvert(uint vid [[vertex_id]],
                                                    uint iid [[instance_id]],
                                                    device const BloomGlyphInst *glyphs [[buffer(0)]],
                                                    device const BloomClipRect *clips [[buffer(1)]],
                                                    constant float2 &viewport [[buffer(2)]]) {
    BloomGlyphInst g = glyphs[iid];
    BloomClipRect c = clips[g.clip_id];
    float4 b = float4(g.dst_q14_2) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy);
    float2 p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(p0, p1, uv);
    float4 uvr = float4(g.uv_unorm16) * (1.0f / 65535.0f);
    float2 local_uv = (px - b.xy) / max(b.zw, 1e-4f);
    BloomSpriteClipVertIn o;
    o.pos = float4(to_ndc(px, viewport), clamp(float(g.z_flags) / 65535.0f, 0.0f, 1.0f), 1.0f);
    o.tile_uv = half2(uvr.xy + local_uv * uvr.zw);
    o.color = unpack_unorm4x8_to_half(g.rgba8);
    return o;
}

fragment half4 f_bloom_glyph_clipvert(BloomSpriteClipVertIn in [[stage_in]],
                                      texture2d<half> atlas [[texture(0)]]) {
    constexpr sampler smp(mag_filter::linear, min_filter::linear);
    half a = atlas.sample(smp, float2(in.tile_uv)).r;
    half4 col = in.color;
    col.a *= a;
    return col;
}

struct BloomRefetch20In {
    float4 pos [[position]];
    uint idx [[flat]];
};

vertex BloomRefetch20In v_bloom_glyph_refetch20(uint vid [[vertex_id]],
                                                uint iid [[instance_id]],
                                                device const BloomGlyphInst *glyphs [[buffer(0)]],
                                                device const BloomClipRect *clips [[buffer(1)]],
                                                constant float2 &viewport [[buffer(2)]]) {
    BloomGlyphInst g = glyphs[iid];
    BloomClipRect c = clips[g.clip_id];
    float4 b = float4(g.dst_q14_2) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy);
    float2 p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(p0, p1, uv);
    BloomRefetch20In o;
    o.pos = float4(to_ndc(px, viewport), clamp(float(g.z_flags) / 65535.0f, 0.0f, 1.0f), 1.0f);
    o.idx = iid;
    return o;
}

fragment half4 f_bloom_glyph_refetch20(BloomRefetch20In in [[stage_in]],
                                       device const BloomGlyphInst *glyphs [[buffer(0)]],
                                       texture2d<half> atlas [[texture(0)]]) {
    BloomGlyphInst g = glyphs[in.idx];
    float4 b = float4(g.dst_q14_2) * 0.25f;
    float2 local_uv = (in.pos.xy - b.xy) / b.zw;
    float4 uvr = float4(g.uv_unorm16) * (1.0f / 65535.0f);
    constexpr sampler smp(mag_filter::linear, min_filter::linear);
    half a = atlas.sample(smp, uvr.xy + local_uv * uvr.zw).r;
    half4 col = unpack_unorm4x8_to_half(g.rgba8);
    col.a *= a;
    return col;
}

// ----------------------------------------------------------------------------
// Test 2: GPUI Dual-Pass Quads vs BLOOM Pre-Partitioned Opaque + Refetch20B Alpha
// ----------------------------------------------------------------------------
bool is_opaque_rect(GpuiQuad160 q) {
    return q.background.tag == 0u && q.background.solid.w >= 1.0f &&
           q.corner_radii.x == 0.0f && q.corner_radii.y == 0.0f &&
           q.corner_radii.z == 0.0f && q.corner_radii.w == 0.0f &&
           q.border_widths.x == 0.0f && q.border_widths.y == 0.0f &&
           q.border_widths.z == 0.0f && q.border_widths.w == 0.0f;
}

struct GpuiOpaqueIn {
    float4 pos [[position]];
    float4 color [[flat]];
};

vertex GpuiOpaqueIn v_gpui_opaque_quad(uint vid [[vertex_id]],
                                       uint iid [[instance_id]],
                                       device const GpuiQuad160 *quads [[buffer(0)]],
                                       constant uint &quad_count [[buffer(1)]],
                                       constant float2 &viewport [[buffer(2)]]) {
    uint quad_id = quad_count - 1u - iid;
    GpuiQuad160 q = quads[quad_id];
    if (!is_opaque_rect(q)) {
        return GpuiOpaqueIn{CULLED_POSITION, float4(0.0f)};
    }
    float2 b0 = max(float2(q.bounds.x, q.bounds.y), float2(q.content_mask.x, q.content_mask.y));
    float2 b1 = min(float2(q.bounds.x + q.bounds.z, q.bounds.y + q.bounds.w),
                    float2(q.content_mask.x + q.content_mask.z, q.content_mask.y + q.content_mask.w));
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    float z = clamp(float(q.order + 1u) / 65535.0f, 0.0f, 1.0f);
    return GpuiOpaqueIn{float4(to_ndc(px, viewport), z, 1.0f), hsla_to_rgba(q.background.solid)};
}

fragment float4 f_gpui_opaque_quad(GpuiOpaqueIn in [[stage_in]]) {
    return in.color;
}

struct GpuiAlphaIn {
    uint quad_id [[flat]];
    float4 pos [[position]];
    float4 border_color [[flat]];
    float4 bg_color [[flat]];
};

vertex GpuiAlphaIn v_gpui_alpha_quad(uint vid [[vertex_id]],
                                     uint iid [[instance_id]],
                                     device const GpuiQuad160 *quads [[buffer(0)]],
                                     constant float2 &viewport [[buffer(1)]]) {
    GpuiQuad160 q = quads[iid];
    if (is_opaque_rect(q)) {
        GpuiAlphaIn culled = {};
        culled.pos = CULLED_POSITION;
        return culled;
    }
    float2 b0 = max(float2(q.bounds.x, q.bounds.y), float2(q.content_mask.x, q.content_mask.y));
    float2 b1 = min(float2(q.bounds.x + q.bounds.z, q.bounds.y + q.bounds.w),
                    float2(q.content_mask.x + q.content_mask.z, q.content_mask.y + q.content_mask.w));
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    float z = clamp(float(q.order + 1u) / 65535.0f, 0.0f, 1.0f);
    GpuiAlphaIn o;
    o.quad_id = iid;
    o.pos = float4(to_ndc(px, viewport), z, 1.0f);
    o.border_color = hsla_to_rgba(q.border_color);
    o.bg_color = hsla_to_rgba(q.background.solid);
    return o;
}

fragment float4 f_gpui_alpha_quad(GpuiAlphaIn in [[stage_in]],
                                  device const GpuiQuad160 *quads [[buffer(0)]]) {
    GpuiQuad160 q = quads[in.quad_id];
    float2 size = float2(q.bounds.z, q.bounds.w);
    float2 half_size = size * 0.5f;
    float2 point = in.pos.xy - float2(q.bounds.x, q.bounds.y);
    float2 c2p = point - half_size;
    float r_top = c2p.x < 0.0f ? q.corner_radii.x : q.corner_radii.y;
    float r_bot = c2p.x < 0.0f ? q.corner_radii.w : q.corner_radii.z;
    float cr = c2p.y < 0.0f ? r_top : r_bot;
    float2 bw = float2(c2p.x < 0.0f ? q.border_widths.w : q.border_widths.y,
                       c2p.y < 0.0f ? q.border_widths.x : q.border_widths.z);
    float2 ctp = abs(c2p) - half_size + cr;
    float outer = length(max(ctp, 0.0f)) + min(max(ctp.x, ctp.y), 0.0f) - cr;
    float2 id = abs(c2p) - max(half_size - bw, 0.0f) + max(cr - bw.x, 0.0f);
    float inner = length(max(id, 0.0f)) + min(max(id.x, id.y), 0.0f) - max(cr - bw.x, 0.0f);
    float oa = saturate(0.5f - outer);
    float ia = saturate(0.5f - inner);
    float4 col = mix(in.border_color, in.bg_color, ia);
    col.a *= oa;
    return col;
}

struct BloomOpaqueIn {
    float4 pos [[position]];
    half4 fill [[flat]];
};

vertex BloomOpaqueIn v_bloom_opaque_32(uint vid [[vertex_id]],
                                       uint iid [[instance_id]],
                                       device const BloomQuadHot *quads [[buffer(0)]],
                                       device const BloomClipRect *clips [[buffer(1)]],
                                       constant float2 &viewport [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(q.bounds_q14_2) * 0.25f;
    float2 b0 = max(b.xy, c.rect.xy);
    float2 b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    float z = clamp(float(q.z_flags & 0xFFFFu) / 65535.0f, 0.0f, 1.0f);
    return BloomOpaqueIn{float4(to_ndc(px, viewport), z, 1.0f), unpack_unorm4x8_to_half(q.rgba8_fill)};
}

fragment half4 f_bloom_opaque_32(BloomOpaqueIn in [[stage_in]]) {
    return in.fill;
}

vertex BloomRefetch20In v_bloom_quad32_q14_refetch20(uint vid [[vertex_id]],
                                                     uint iid [[instance_id]],
                                                     device const BloomQuadHot *quads [[buffer(0)]],
                                                     device const BloomClipRect *clips [[buffer(1)]],
                                                     constant float2 &viewport [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(q.bounds_q14_2) * 0.25f;
    float2 b0 = max(b.xy, c.rect.xy);
    float2 b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    float z = clamp(float(q.z_flags & 0xFFFFu) / 65535.0f, 0.0f, 1.0f);
    return BloomRefetch20In{float4(to_ndc(px, viewport), z, 1.0f), iid};
}

fragment half4 f_bloom_quad32_q14_refetch20(BloomRefetch20In in [[stage_in]],
                                            device const BloomQuadHot *quads [[buffer(0)]]) {
    BloomQuadHot q = quads[in.idx];
    half4 b = half4(q.bounds_q14_2) * 0.25h;
    half2 half_sz = b.zw * 0.5h;
    half2 c2p = (half2(in.pos.xy) - b.xy) - half_sz;
    half4 rr = half4(q.radii_q6_2) * 0.25h;
    half4 bb = half4(q.borders_q6_2) * 0.25h;
    half r = (c2p.x < 0.0h) ? (c2p.y < 0.0h ? rr.x : rr.w) : (c2p.y < 0.0h ? rr.y : rr.z);
    half bw = min(c2p.x < 0.0h ? bb.w : bb.y, c2p.y < 0.0h ? bb.x : bb.z);
    half2 d = abs(c2p) - half_sz + r;
    half outer = length(max(d, 0.0h)) + min(max(d.x, d.y), 0.0h) - r;
    half inner = outer + bw;
    half oa = clamp(0.5h - outer, 0.0h, 1.0h);
    half ia = clamp(0.5h - inner, 0.0h, 1.0h);
    half4 col = mix(unpack_unorm4x8_to_half(q.rgba8_border), unpack_unorm4x8_to_half(q.rgba8_fill), ia);
    col.a *= oa;
    return col;
}

// ----------------------------------------------------------------------------
// Test 3: Analytic erf7 Shadows (GPUI 112B + 100B StageIn vs BLOOM 32B + Refetch20B)
// ----------------------------------------------------------------------------
struct GpuiShadowFragIn {
    float4 position [[position]];
    float4 color [[flat]];
    uint shadow_id [[flat]];
    float4 blur_shape [[flat]];
    float4 blur_r1 [[flat]];
    float4 blur_exponent [[flat]];
    float4 blur_scale [[flat]];
};

vertex GpuiShadowFragIn v_gpui_shadow(uint vid [[vertex_id]],
                                      uint iid [[instance_id]],
                                      device const GpuiShadow112 *shadows [[buffer(0)]],
                                      constant float2 &viewport [[buffer(1)]]) {
    GpuiShadow112 sh = shadows[iid];
    float2 b0 = max(float2(sh.bounds.x, sh.bounds.y), float2(sh.content_mask.x, sh.content_mask.y));
    float2 b1 = min(float2(sh.bounds.x + sh.bounds.z, sh.bounds.y + sh.bounds.w),
                    float2(sh.content_mask.x + sh.content_mask.z, sh.content_mask.y + sh.content_mask.w));
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);

    float2 size = float2(sh.shape_bounds.z, sh.shape_bounds.w);
    float s = max(sh.blur_radius * sqrt(2.0f), 1e-6f);
    float s_inv = 1.0f / s;
    float min_edge = min(size.x, size.y);
    float r_max = 0.5f * min_edge;
    float4 radii = float4(sh.shape_corner_radii.x, sh.shape_corner_radii.y,
                          sh.shape_corner_radii.z, sh.shape_corner_radii.w);
    float4 r0 = min(sqrt(radii * radii + 1.15f * 1.15f * s * s), r_max);
    float4 r1 = min(sqrt(radii * radii + 4.0f * s * s), r_max);
    float2 half_extent = 0.5f * s_inv * size;
    float delta = 1.25f * s * (exp(-half_extent.x * half_extent.x) - exp(-half_extent.y * half_extent.y));
    float2 adj_sz = float2(size.x + min(delta, 0.0f), size.y - max(delta, 0.0f));
    float4 peak = s_inv * 0.5f * (max(adj_sz.x, adj_sz.y) - 0.5f * radii);
    float4 scale = 0.5f * float4(erf7(peak.x), erf7(peak.y), erf7(peak.z), erf7(peak.w));

    GpuiShadowFragIn o;
    o.position = float4(to_ndc(px, viewport), clamp(float(sh.order + 1u) / 65535.0f, 0.0f, 1.0f), 1.0f);
    o.color = hsla_to_rgba(sh.color);
    o.shadow_id = iid;
    o.blur_shape = float4(0.5f * adj_sz, s_inv, min_edge);
    o.blur_r1 = r1;
    o.blur_exponent = 2.0f * r1 / r0;
    o.blur_scale = scale;
    return o;
}

fragment float4 f_gpui_shadow(GpuiShadowFragIn in [[stage_in]],
                              device const GpuiShadow112 *shadows [[buffer(0)]]) {
    GpuiShadow112 sh = shadows[in.shadow_id];
    float2 center = float2(sh.shape_bounds.x, sh.shape_bounds.y) +
                    float2(sh.shape_bounds.z, sh.shape_bounds.w) * 0.5f;
    float2 c2p = in.position.xy - center;
    float4 r1v = in.blur_r1, expv = in.blur_exponent, scv = in.blur_scale;
    float r1 = (c2p.x < 0.0f) ? (c2p.y < 0.0f ? r1v.x : r1v.w) : (c2p.y < 0.0f ? r1v.y : r1v.z);
    float ex = (c2p.x < 0.0f) ? (c2p.y < 0.0f ? expv.x : expv.w) : (c2p.y < 0.0f ? expv.y : expv.z);
    float sc = (c2p.x < 0.0f) ? (c2p.y < 0.0f ? scv.x : scv.w) : (c2p.y < 0.0f ? scv.y : scv.z);
    float2 p0 = abs(c2p) - (in.blur_shape.xy - r1);
    float2 p1 = max(p0, 0.0f);
    float d_pos = (p1.x > 0.0f && p1.y > 0.0f) ? pow(pow(p1.x, ex) + pow(p1.y, ex), 1.0f / ex) : (p1.x + p1.y);
    float d = d_pos + min(max(p0.x, p0.y), 0.0f) - r1;
    float s_inv = in.blur_shape.z, min_edge = in.blur_shape.w;
    float alpha = sc * (erf7(s_inv * (min_edge + d)) - erf7(s_inv * d));
    return in.color * float4(1.0f, 1.0f, 1.0f, alpha);
}

vertex BloomRefetch20In v_bloom_shadow_refetch20(uint vid [[vertex_id]],
                                                 uint iid [[instance_id]],
                                                 device const BloomShadowHot *shadows [[buffer(0)]],
                                                 device const BloomClipRect *clips [[buffer(1)]],
                                                 constant float2 &viewport [[buffer(2)]]) {
    BloomShadowHot sh = shadows[iid];
    BloomClipRect c = clips[sh.clip_id];
    float4 b = float4(sh.bounds_q14_2) * 0.25f;
    float2 b0 = max(b.xy, c.rect.xy);
    float2 b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    return BloomRefetch20In{float4(to_ndc(px, viewport), clamp(float(sh.z_flags) / 65535.0f, 0.0f, 1.0f), 1.0f), iid};
}

fragment half4 f_bloom_shadow_refetch20(BloomRefetch20In in [[stage_in]],
                                        device const BloomShadowHot *shadows [[buffer(0)]]) {
    BloomShadowHot sh = shadows[in.idx];
    half4 b = half4(sh.bounds_q14_2) * 0.25h;
    half2 center = b.xy + b.zw * 0.5h;
    half2 c2p = half2(in.pos.xy) - center;
    half r1 = sh.blur_r1_exp_sc.x;
    half ex = sh.blur_r1_exp_sc.y;
    half sc = sh.blur_r1_exp_sc.z;
    half2 p0 = abs(c2p) - (sh.shape_params.xy - r1);
    half2 p1 = max(p0, 0.0h);
    half d_pos = (p1.x > 0.0h && p1.y > 0.0h)
        ? pow(pow(p1.x, ex) + pow(p1.y, ex), 1.0h / ex)
        : (p1.x + p1.y);
    half d = d_pos + min(max(p0.x, p0.y), 0.0h) - r1;
    half s_inv = sh.shape_params.z;
    half min_edge = sh.shape_params.w;
    half alpha = sc * (erf7_h(s_inv * (min_edge + d)) - erf7_h(s_inv * d));
    half4 col = unpack_unorm4x8_to_half(sh.rgba8);
    col.a *= alpha;
    return col;
}

// ----------------------------------------------------------------------------
// Test 4: Slug Path Tiles (GPUI 72B StageIn vs BLOOM Refetch20B)
// ----------------------------------------------------------------------------
struct GpuiPathTileIn {
    float4 position [[position]];
    uint tile_id [[flat]];
    float4 curve_box [[flat]];
    float4 solid_color [[flat]];
};

vertex GpuiPathTileIn v_gpui_path_tile(uint vid [[vertex_id]],
                                       uint iid [[instance_id]],
                                       device const GpuiPathTile56 *tiles [[buffer(0)]],
                                       device const GpuiPathPaint112 *paints [[buffer(1)]],
                                       constant float2 &viewport [[buffer(2)]]) {
    GpuiPathTile56 t = tiles[iid];
    GpuiPathPaint112 p = paints[t.paint];
    float2 tl = clamp(float2(t.top_left.x, t.top_left.y),
                      float2(p.content_mask.x, p.content_mask.y),
                      float2(p.content_mask.x + p.content_mask.z, p.content_mask.y + p.content_mask.w));
    float2 br = clamp(float2(t.bottom_right.x, t.bottom_right.y), tl,
                      float2(p.content_mask.x + p.content_mask.z, p.content_mask.y + p.content_mask.w));
    float2 pos = float2((vid & 1u) ? br.x : tl.x, (vid & 2u) ? br.y : tl.y);
    GpuiPathTileIn o;
    o.position = float4(to_ndc(pos, viewport), 0.5f, 1.0f);
    o.tile_id = iid;
    o.curve_box = float4(t.curves_top_left.x, t.curves_top_left.y,
                         t.curves_bottom_right.x, t.curves_bottom_right.y);
    o.solid_color = hsla_to_rgba(p.color.solid);
    return o;
}

float monotone_quad_root(float a, float b, float c, float dir) {
    float disc = max(b * b - 4.0f * a * c, 0.0f);
    float denom = b + dir * sqrt(disc);
    return (denom == 0.0f) ? 0.0f : saturate(-2.0f * c / denom);
}

float eval_slug_tile_coverage(GpuiPathTile56 tile,
                              device const GpuiPathCurve40 *curves,
                              float2 center,
                              uint even_odd) {
    float2 reference = float2(tile.reference.x, tile.reference.y);
    float2 winding = float2(float(tile.backdrop));
    float2 weight = float2(0.0f);
    for (uint i = 0u; i < tile.curve_count; ++i) {
        GpuiPathCurve40 cv = curves[tile.curve_start + i];
        float2 p0 = float2(cv.p0.x, cv.p0.y);
        float2 p1 = float2(cv.p1.x, cv.p1.y);
        if (min(p0.x, p1.x) >= center.x + 0.5f) break;
        if (p0.y < center.y + 0.5f) {
            float min_x = min(p0.x, p1.x), max_x = max(p0.x, p1.x);
            if (p0.y <= center.y && center.y < p1.y) {
                if (max_x <= center.x - 0.5f) {
                    winding.x += cv.sign;
                } else {
                    float t = monotone_quad_root(cv.a.y, cv.b.y, p0.y - center.y, 1.0f);
                    float x = (cv.a.x * t + cv.b.x) * t + p0.x;
                    float d = center.x - x;
                    winding.x += cv.sign * saturate(d + 0.5f);
                    weight.x = max(weight.x, saturate(1.0f - 2.0f * abs(d)));
                }
            }
            if (min_x <= center.x && center.x < max_x) {
                float dir = p1.x > p0.x ? 1.0f : -1.0f;
                if (p1.y <= center.y - 0.5f) {
                    winding.y -= cv.sign * dir;
                } else if (p0.y < center.y + 0.5f) {
                    float t = monotone_quad_root(cv.a.x, cv.b.x, p0.x - center.x, dir);
                    float y = (cv.a.y * t + cv.b.y) * t + p0.y;
                    float d = center.y - y;
                    winding.y -= cv.sign * dir * saturate(d + 0.5f);
                    weight.y = max(weight.y, saturate(1.0f - 2.0f * abs(d)));
                }
            }
            if (p0.x <= reference.x) winding.x -= cv.sign * (float(p0.y <= center.y) - float(p0.y <= reference.y));
            if (p0.y <= reference.y && p0.x <= center.x) winding.y += cv.sign;
            if (p1.x <= reference.x) winding.x += cv.sign * (float(p1.y <= center.y) - float(p1.y <= reference.y));
            if (p1.y <= reference.y && p1.x <= center.x) winding.y -= cv.sign;
        }
    }
    float cov = max(abs(dot(winding, weight)) / max(weight.x + weight.y, 1.0f / 65536.0f),
                    min(abs(winding.x), abs(winding.y)));
    return even_odd ? (1.0f - abs(1.0f - fract(cov * 0.5f) * 2.0f)) : saturate(cov);
}

fragment float4 f_gpui_path_tile(GpuiPathTileIn in [[stage_in]],
                                 device const GpuiPathTile56 *tiles [[buffer(0)]],
                                 device const GpuiPathCurve40 *curves [[buffer(1)]],
                                 device const GpuiPathPaint112 *paints [[buffer(2)]]) {
    float2 center = floor(in.position.xy) + 0.5f;
    if (any(center < in.curve_box.xy) || any(center > in.curve_box.zw)) {
        return in.solid_color;
    }
    GpuiPathTile56 tile = tiles[in.tile_id];
    GpuiPathPaint112 paint = paints[tile.paint];
    float cov = eval_slug_tile_coverage(tile, curves, center, paint.even_odd);
    return float4(in.solid_color.rgb, in.solid_color.a * cov);
}

vertex BloomRefetch20In v_bloom_path_tile_refetch20(uint vid [[vertex_id]],
                                                    uint iid [[instance_id]],
                                                    device const GpuiPathTile56 *tiles [[buffer(0)]],
                                                    constant float2 &viewport [[buffer(1)]]) {
    GpuiPathTile56 t = tiles[iid];
    float2 pos = float2((vid & 1u) ? t.bottom_right.x : t.top_left.x,
                        (vid & 2u) ? t.bottom_right.y : t.top_left.y);
    return BloomRefetch20In{float4(to_ndc(pos, viewport), 0.5f, 1.0f), iid};
}

fragment half4 f_bloom_path_tile_refetch20(BloomRefetch20In in [[stage_in]],
                                           device const GpuiPathTile56 *tiles [[buffer(0)]],
                                           device const GpuiPathCurve40 *curves [[buffer(1)]],
                                           device const uint *paint_rgba8 [[buffer(2)]]) {
    GpuiPathTile56 tile = tiles[in.idx];
    float2 center = floor(in.pos.xy) + 0.5f;
    half4 col = unpack_unorm4x8_to_half(paint_rgba8[tile.paint]);
    if (any(center < float2(tile.curves_top_left.x, tile.curves_top_left.y)) ||
        any(center > float2(tile.curves_bottom_right.x, tile.curves_bottom_right.y))) {
        return col;
    }
    float cov = eval_slug_tile_coverage(tile, curves, center, 0u);
    col.a *= half(cov);
    return col;
}

// ----------------------------------------------------------------------------
// Test 6 Underlines: GPUI 64B vs BLOOM 24B
// ----------------------------------------------------------------------------
struct GpuiUnderlineIn {
    float4 pos [[position]];
    float4 color [[flat]];
    uint uid [[flat]];
};

vertex GpuiUnderlineIn v_gpui_underline(uint vid [[vertex_id]],
                                        uint iid [[instance_id]],
                                        device const GpuiUnderline64 *uls [[buffer(0)]],
                                        constant float2 &viewport [[buffer(1)]]) {
    GpuiUnderline64 u = uls[iid];
    float2 b0 = max(float2(u.bounds.x, u.bounds.y), float2(u.content_mask.x, u.content_mask.y));
    float2 b1 = min(float2(u.bounds.x + u.bounds.z, u.bounds.y + u.bounds.w),
                    float2(u.content_mask.x + u.content_mask.z, u.content_mask.y + u.content_mask.w));
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    return GpuiUnderlineIn{
        float4(to_ndc(px, viewport), clamp(float(u.order + 1u) / 65535.0f, 0.0f, 1.0f), 1.0f),
        hsla_to_rgba(u.color),
        iid | (u.wavy == 0u ? SIMPLE_PRIMITIVE_FLAG : 0u)
    };
}

fragment float4 f_gpui_underline(GpuiUnderlineIn in [[stage_in]],
                                 device const GpuiUnderline64 *uls [[buffer(0)]]) {
    if ((in.uid & SIMPLE_PRIMITIVE_FLAG) != 0u) return in.color;
    GpuiUnderline64 u = uls[in.uid];
    float ht = u.thickness * 0.5f;
    float h = max(u.bounds.w, 1.0f);
    float2 st = ((in.pos.xy - float2(u.bounds.x, u.bounds.y)) / h) - float2(0.0f, 0.5f);
    float freq = (M_PI_F * 2.0f * u.thickness) / h;
    float amp = (u.thickness * 0.8f) / h;
    float s = sin(st.x * freq) * amp;
    float ds = cos(st.x * freq) * amp * freq;
    float dpx = ((st.y - s) * rsqrt(1.0f + ds * ds)) * h;
    float alpha = saturate(0.5f - max(-dpx - ht, dpx - ht));
    return in.color * float4(1.0f, 1.0f, 1.0f, alpha);
}

vertex BloomRefetch20In v_bloom_underline24(uint vid [[vertex_id]],
                                            uint iid [[instance_id]],
                                            device const BloomUnderlineHot *uls [[buffer(0)]],
                                            device const BloomClipRect *clips [[buffer(1)]],
                                            constant float2 &viewport [[buffer(2)]]) {
    BloomUnderlineHot u = uls[iid];
    BloomClipRect c = clips[u.clip_id];
    float4 b = float4(u.bounds_q14_2) * 0.25f;
    float2 b0 = max(b.xy, c.rect.xy);
    float2 b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    return BloomRefetch20In{float4(to_ndc(px, viewport), clamp(float(u.z_order) / 65535.0f, 0.0f, 1.0f), 1.0f), iid};
}

fragment half4 f_bloom_underline24(BloomRefetch20In in [[stage_in]],
                                   device const BloomUnderlineHot *uls [[buffer(0)]]) {
    BloomUnderlineHot u = uls[in.idx];
    half4 col = unpack_unorm4x8_to_half(u.rgba8);
    if (u.wavy_flag == 0u) return col;
    float4 b = float4(u.bounds_q14_2) * 0.25f;
    float th = float(u.thickness);
    float ht = th * 0.5f;
    float h = max(b.w, 1.0f);
    float2 st = ((in.pos.xy - b.xy) / h) - float2(0.0f, 0.5f);
    float freq = (M_PI_F * 2.0f * th) / h;
    float amp = (th * 0.8f) / h;
    float s = sin(st.x * freq) * amp;
    float ds = cos(st.x * freq) * amp * freq;
    float dpx = ((st.y - s) * rsqrt(1.0f + ds * ds)) * h;
    col.a *= half(saturate(0.5f - max(-dpx - ht, dpx - ht)));
    return col;
}
)MSL";

// ============================================================================
// Faithfully Ported GPUI BoundsTree (MAX_CHILDREN = 12, zed-main/crates/gpui/src/bounds_tree.rs)
// ============================================================================
enum PrimKind : uint8_t {
    KIND_SHADOW = 0,
    KIND_QUAD = 1,
    KIND_PATH = 2,
    KIND_UNDERLINE = 3,
    KIND_MONO_SPRITE = 4,
};

static inline PrimKind max_shared_kind(PrimKind k) {
    if (k == KIND_MONO_SPRITE) return KIND_UNDERLINE;
    return k;
}

struct RectF {
    float x, y, w, h;
    bool intersects(const RectF &o) const {
        return x < (o.x + o.w) && (x + w) > o.x && y < (o.y + o.h) && (y + h) > o.y;
    }
    RectF rect_union(const RectF &o) const {
        float x0 = std::min(x, o.x), y0 = std::min(y, o.y);
        float x1 = std::max(x + w, o.x + o.w), y1 = std::max(y + h, o.y + o.h);
        return {x0, y0, x1 - x0, y1 - y0};
    }
    float half_perimeter() const { return w + h; }
};

struct TreeKey {
    uint32_t ordering = 0;
    PrimKind kind = KIND_SHADOW;
    uint32_t packed() const { return (ordering << 8) | static_cast<uint32_t>(kind); }
    bool operator<(const TreeKey &o) const { return packed() < o.packed(); }
    bool operator<=(const TreeKey &o) const { return packed() <= o.packed(); }
    bool operator>(const TreeKey &o) const { return packed() > o.packed(); }
};

static constexpr size_t BT_MAX_CHILDREN = 12;

struct BTNode {
    RectF bounds;
    TreeKey max_key;
    bool is_leaf;
    std::array<uint32_t, BT_MAX_CHILDREN> children;
    uint8_t children_len;
};

class GpuiBoundsTree {
public:
    void clear() {
        nodes_.clear();
        has_root_ = false;
        has_max_leaf_ = false;
        insert_path_.clear();
        search_stack_.clear();
    }

    uint32_t insert(const RectF &b, PrimKind k) {
        TreeKey found;
        bool has_found = find_max_key(b, found);
        uint32_t ordering = 1;
        if (has_found) {
            ordering = (found.kind <= max_shared_kind(k)) ? found.ordering : (found.ordering + 1);
        }
        TreeKey key{ordering, k};
        uint32_t new_leaf_idx = insert_leaf(b, key);
        if (!has_max_leaf_ || nodes_[max_leaf_idx_].max_key < key) {
            max_leaf_idx_ = new_leaf_idx;
            has_max_leaf_ = true;
        }
        return ordering;
    }

private:
    bool find_max_key(const RectF &query, TreeKey &out) {
        if (!has_root_) return false;
        if (has_max_leaf_ && query.intersects(nodes_[max_leaf_idx_].bounds)) {
            out = nodes_[max_leaf_idx_].max_key;
            return true;
        }
        search_stack_.clear();
        search_stack_.push_back(root_idx_);
        bool any = false;
        TreeKey max_found{0, KIND_SHADOW};
        while (!search_stack_.empty()) {
            uint32_t ni = search_stack_.back();
            search_stack_.pop_back();
            const BTNode &nd = nodes_[ni];
            if (any && nd.max_key <= max_found) continue;
            if (!query.intersects(nd.bounds)) continue;
            if (nd.is_leaf) {
                if (!any || nd.max_key > max_found) {
                    max_found = nd.max_key;
                    any = true;
                }
            } else {
                for (uint8_t c = 0; c < nd.children_len; ++c) {
                    uint32_t ci = nd.children[c];
                    if (!any || nodes_[ci].max_key > max_found) {
                        search_stack_.push_back(ci);
                    }
                }
            }
        }
        if (any) out = max_found;
        return any;
    }

    uint32_t insert_leaf(const RectF &b, TreeKey key) {
        uint32_t new_leaf = static_cast<uint32_t>(nodes_.size());
        BTNode leaf{};
        leaf.bounds = b;
        leaf.max_key = key;
        leaf.is_leaf = true;
        nodes_.push_back(leaf);

        if (!has_root_) {
            root_idx_ = new_leaf;
            has_root_ = true;
            return new_leaf;
        }
        if (nodes_[root_idx_].is_leaf) {
            BTNode root{};
            root.is_leaf = false;
            root.bounds = nodes_[root_idx_].bounds.rect_union(b);
            TreeKey rk = nodes_[root_idx_].max_key;
            root.max_key = (rk > key) ? rk : key;
            if (key > rk) {
                root.children[0] = root_idx_;
                root.children[1] = new_leaf;
            } else {
                root.children[0] = new_leaf;
                root.children[1] = root_idx_;
            }
            root.children_len = 2;
            uint32_t nroot = static_cast<uint32_t>(nodes_.size());
            nodes_.push_back(root);
            root_idx_ = nroot;
            return new_leaf;
        }

        insert_path_.clear();
        uint32_t cur = root_idx_;
        while (true) {
            insert_path_.push_back(cur);
            BTNode &cnode = nodes_[cur];
            uint32_t best_child = cnode.children[0];
            uint8_t best_pos = 0;
            float best_cost = b.rect_union(nodes_[best_child].bounds).half_perimeter();
            for (uint8_t p = 1; p < cnode.children_len; ++p) {
                uint32_t ci = cnode.children[p];
                float cost = b.rect_union(nodes_[ci].bounds).half_perimeter();
                if (cost < best_cost) {
                    best_cost = cost;
                    best_child = ci;
                    best_pos = p;
                }
            }
            if (nodes_[best_child].is_leaf) {
                if (cnode.children_len < BT_MAX_CHILDREN) {
                    uint8_t len = cnode.children_len;
                    cnode.children[len] = new_leaf;
                    if (key <= cnode.max_key && len > 0) {
                        std::swap(cnode.children[len - 1], cnode.children[len]);
                    }
                    cnode.children_len = len + 1;
                    cnode.bounds = cnode.bounds.rect_union(b);
                    if (key > cnode.max_key) cnode.max_key = key;
                    break;
                } else {
                    RectF sb = nodes_[best_child].bounds;
                    TreeKey sk = nodes_[best_child].max_key;
                    BTNode ni{};
                    ni.is_leaf = false;
                    ni.bounds = sb.rect_union(b);
                    ni.max_key = (sk > key) ? sk : key;
                    if (key > sk) {
                        ni.children[0] = best_child;
                        ni.children[1] = new_leaf;
                    } else {
                        ni.children[0] = new_leaf;
                        ni.children[1] = best_child;
                    }
                    ni.children_len = 2;
                    uint32_t ni_idx = static_cast<uint32_t>(nodes_.size());
                    nodes_.push_back(ni);
                    BTNode &parent = nodes_[cur];
                    parent.children[best_pos] = ni_idx;
                    if (ni.max_key > parent.max_key && parent.children_len > 1) {
                        std::swap(parent.children[best_pos], parent.children[parent.children_len - 1]);
                    }
                    break;
                }
            } else {
                cur = best_child;
            }
        }

        bool has_updated = false;
        uint32_t updated_child = 0;
        for (size_t ri = insert_path_.size(); ri > 0; --ri) {
            uint32_t ni = insert_path_[ri - 1];
            BTNode &nd = nodes_[ni];
            nd.bounds = nd.bounds.rect_union(b);
            if (nd.max_key < key) {
                nd.max_key = key;
                if (has_updated) {
                    for (uint8_t p = 0; p + 1 < nd.children_len; ++p) {
                        if (nd.children[p] == updated_child) {
                            std::swap(nd.children[p], nd.children[nd.children_len - 1]);
                            break;
                        }
                    }
                }
            }
            updated_child = ni;
            has_updated = true;
        }
        return new_leaf;
    }

    std::vector<BTNode> nodes_;
    uint32_t root_idx_ = 0;
    bool has_root_ = false;
    uint32_t max_leaf_idx_ = 0;
    bool has_max_leaf_ = false;
    std::vector<uint32_t> insert_path_;
    std::vector<uint32_t> search_stack_;
};

// ============================================================================
// Metal Probe Context
// ============================================================================
struct Ctx {
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> q = nil;
    id<MTLLibrary> lib = nil;
};

static id<MTLComputePipelineState> cpso(Ctx &ctx, const char *fn) {
    NSError *err = nil;
    id<MTLFunction> f = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:fn]];
    if (!f) { fprintf(stderr, "missing compute fn %s\n", fn); exit(1); }
    id<MTLComputePipelineState> p = [ctx.dev newComputePipelineStateWithFunction:f error:&err];
    if (!p || err) { fprintf(stderr, "compute pso err %s\n", fn); exit(1); }
    return p;
}

static id<MTLRenderPipelineState> rpso(Ctx &ctx,
                                       const char *vf,
                                       const char *ff,
                                       bool blend) {
    NSError *err = nil;
    id<MTLFunction> vfn = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:vf]];
    id<MTLFunction> ffn = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:ff]];
    if (!vfn || !ffn) { fprintf(stderr, "missing render fn %s/%s\n", vf, ff); exit(1); }
    MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction = vfn;
    d.fragmentFunction = ffn;
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    if (blend) {
        d.colorAttachments[0].blendingEnabled = YES;
        d.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        d.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    id<MTLRenderPipelineState> p = [ctx.dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p || err) { fprintf(stderr, "render pso err %s/%s: %s\n", vf, ff, err.localizedDescription.UTF8String); exit(1); }
    return p;
}

static id<MTLTexture> tex2d(id<MTLDevice> dev, uint32_t w, uint32_t h, MTLPixelFormat fmt,
                            MTLTextureUsage usage, MTLStorageMode mode, bool opt) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];
    d.usage = usage;
    d.storageMode = mode;
    d.allowGPUOptimizedContents = opt ? YES : NO;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d];
    if (!t) { fprintf(stderr, "tex alloc failed\n"); exit(1); }
    return t;
}

static void warm_gpu_dvfs(Ctx &ctx) {
    id<MTLComputePipelineState> pw = cpso(ctx, "k_dvfs_warm");
    id<MTLBuffer> b0 = [ctx.dev newBufferWithLength:16384 options:MTLResourceStorageModeShared];
    id<MTLBuffer> b1 = [ctx.dev newBufferWithLength:16384 options:MTLResourceStorageModeShared];
    uint32_t iters = 4096u;
    for (int i = 0; i < 6; ++i) {
        id<MTLCommandBuffer> c = [ctx.q commandBuffer];
        id<MTLComputeCommandEncoder> e = [c computeCommandEncoder];
        [e setComputePipelineState:pw];
        [e setBuffer:b0 offset:0 atIndex:0];
        [e setBuffer:b1 offset:0 atIndex:1];
        [e setBytes:&iters length:sizeof(iters) atIndex:2];
        [e dispatchThreadgroups:MTLSizeMake(1024, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
        [e endEncoding];
        [c commit];
        [c waitUntilCompleted];
    }
}

// ============================================================================
// [1/6] Glyph Atlas Upload + Monochrome Text Sprites @ 4K
// ============================================================================
static std::string bench_glyphs(Ctx &ctx) {
    printf("\n=== [1/6] glyph_atlas_and_sprites: Atlas Upload & Text @ 4K ===\n");

    // 1A: Atlas upload benchmark (Shared replaceRegion vs Private + WC Blit)
    auto bench_atlas = [&](uint32_t tile_w, uint32_t tile_h, uint32_t count, bool bgra, const char *label) {
        uint32_t bpp = bgra ? 4u : 1u;
        size_t tile_bytes = tile_w * tile_h * bpp;
        std::vector<uint8_t> tile_src(tile_bytes, 0xA5);

        id<MTLTexture> tex_shared = tex2d(ctx.dev, 1024, 1024,
                                          bgra ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatA8Unorm,
                                          MTLTextureUsageShaderRead, MTLStorageModeShared, false);
        id<MTLTexture> tex_priv   = tex2d(ctx.dev, 1024, 1024,
                                          bgra ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatR8Unorm,
                                          MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
        id<MTLBuffer> stage = [ctx.dev newBufferWithLength:tile_bytes * count
                                                   options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

        std::vector<double> shared_us, priv_wall_us, priv_gpu_us;
        for (int it = 0; it < 24; ++it) {
            uint64_t t0 = now_ns();
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t x = (i * tile_w) % 1024u;
                uint32_t y = ((i * tile_w) / 1024u) * tile_h;
                [tex_shared replaceRegion:MTLRegionMake2D(x, y, tile_w, tile_h)
                              mipmapLevel:0 withBytes:tile_src.data() bytesPerRow:tile_w * bpp];
            }
            uint64_t t1 = now_ns();
            if (it >= 4) shared_us.push_back(static_cast<double>(t1 - t0) / 1e3);

            uint64_t m0 = now_ns();
            uint8_t *dst = static_cast<uint8_t *>(stage.contents);
            for (uint32_t i = 0; i < count; ++i) {
                std::memcpy(dst + i * tile_bytes, tile_src.data(), tile_bytes);
            }
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t x = (i * tile_w) % 1024u;
                uint32_t y = ((i * tile_w) / 1024u) * tile_h;
                [blit copyFromBuffer:stage sourceOffset:i * tile_bytes sourceBytesPerRow:tile_w * bpp
                   sourceBytesPerImage:tile_bytes sourceSize:MTLSizeMake(tile_w, tile_h, 1)
                   toTexture:tex_priv destinationSlice:0 destinationLevel:0
                   destinationOrigin:MTLOriginMake(x, y, 0)];
            }
            [blit endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
            uint64_t m1 = now_ns();
            if (it >= 4) {
                priv_wall_us.push_back(static_cast<double>(m1 - m0) / 1e3);
                priv_gpu_us.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1e6);
            }
        }
        Stats ss = compute_stats(shared_us), spw = compute_stats(priv_wall_us), spg = compute_stats(priv_gpu_us);
        printf("  Atlas Upload %-18s : Shared replaceRegion=%6.1f us | Private WC+Blit wall=%6.1f us (GPU=%5.1f us)\n",
               label, ss.median_val, spw.median_val, spg.median_val);
        return std::make_tuple(ss.median_val, spw.median_val, spg.median_val);
    };

    auto [a8_sh, a8_pw, a8_pg]     = bench_atlas(16, 16, 128, false, "128x 16x16 A8/R8");
    auto [bgra_sh, bgra_pw, bgra_pg] = bench_atlas(256, 256, 8, true, "8x 256x256 BGRA");

    // 1B: 4K Text Glyph Sprites Rendering
    id<MTLRenderPipelineState> p_gpui  = rpso(ctx, "v_gpui_sprite", "f_gpui_sprite", true);
    id<MTLRenderPipelineState> p_clipv = rpso(ctx, "v_bloom_glyph_clipvert", "f_bloom_glyph_clipvert", true);
    id<MTLRenderPipelineState> p_ref20 = rpso(ctx, "v_bloom_glyph_refetch20", "f_bloom_glyph_refetch20", true);

    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_off = [ctx.dev newDepthStencilStateWithDescriptor:dsd];

    const uint32_t W = 3840, H = 2160;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    float atlas_sz[2] = {1024.0f, 1024.0f};
    id<MTLTexture> rt = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> depth = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);

    id<MTLTexture> atlas_shared = tex2d(ctx.dev, 1024, 1024, MTLPixelFormatA8Unorm, MTLTextureUsageShaderRead, MTLStorageModeShared, false);
    id<MTLTexture> atlas_priv   = tex2d(ctx.dev, 1024, 1024, MTLPixelFormatR8Unorm, MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    {
        std::vector<uint8_t> init_a(1024 * 1024);
        for (size_t i = 0; i < init_a.size(); ++i) init_a[i] = static_cast<uint8_t>((i * 29u) & 0xFFu);
        [atlas_shared replaceRegion:MTLRegionMake2D(0, 0, 1024, 1024) mipmapLevel:0 withBytes:init_a.data() bytesPerRow:1024];
        id<MTLBuffer> st = [ctx.dev newBufferWithBytes:init_a.data() length:init_a.size() options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> c = [ctx.q commandBuffer];
        id<MTLBlitCommandEncoder> b = [c blitCommandEncoder];
        [b copyFromBuffer:st sourceOffset:0 sourceBytesPerRow:1024 sourceBytesPerImage:1024*1024
            sourceSize:MTLSizeMake(1024, 1024, 1) toTexture:atlas_priv destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [b endEncoding]; [c commit]; [c waitUntilCompleted];
    }

    id<MTLBuffer> clips = [ctx.dev newBufferWithLength:sizeof(BloomClipRect) * 16 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    static_cast<BloomClipRect *>(clips.contents)[0] = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};

    const uint32_t counts[] = {2000u, 20000u, 50000u};
    std::ostringstream js;
    js << "  \"glyph_atlas_and_sprites\": {\n"
       << "    \"atlas_upload\": {\n"
       << "      \"a8_128x16x16\": { \"shared_replace_us\": " << a8_sh << ", \"private_wc_blit_wall_us\": " << a8_pw << ", \"private_blit_gpu_us\": " << a8_pg << " },\n"
       << "      \"bgra_8x256x256\": { \"shared_replace_us\": " << bgra_sh << ", \"private_wc_blit_wall_us\": " << bgra_pw << ", \"private_blit_gpu_us\": " << bgra_pg << " }\n"
       << "    },\n    \"sprites_4k\": [\n";

    for (size_t ci = 0; ci < 3; ++ci) {
        @autoreleasepool {
            uint32_t N = counts[ci];
            size_t b_gpui = N * sizeof(GpuiMonoSprite112);
            size_t b_bloom = N * sizeof(BloomGlyphInst);
            id<MTLBuffer> buf_g = [ctx.dev newBufferWithLength:b_gpui options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
            id<MTLBuffer> buf_b = [ctx.dev newBufferWithLength:b_bloom options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

            std::vector<double> cpu_g_us, cpu_b_us;
            for (int it = 0; it < 12; ++it) {
                uint64_t t0 = now_ns();
                GpuiMonoSprite112 *sp = static_cast<GpuiMonoSprite112 *>(buf_g.contents);
                for (uint32_t i = 0; i < N; ++i) {
                    float x = static_cast<float>((i % 280u) * 13u + 16u);
                    float y = static_cast<float>(((i / 280u) % 75u) * 28u + 16u);
                    GpuiMonoSprite112 item{};
                    item.order = 1;
                    item.animation = UINT32_MAX;
                    item.tile = {0, 0, i & 63u, 0, {static_cast<int32_t>((i & 63u) * 14), 16, 12, 24}};
                    item.bounds = {x, y, 12.0f, 24.0f};
                    item.content_mask = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};
                    item.color = {0.6f, 0.25f, 0.85f, 1.0f};
                    item.rot_scale = {1.0f, 0.0f, 0.0f, 1.0f};
                    item.translation = {0.0f, 0.0f};
                    sp[i] = item;
                }
                uint64_t t1 = now_ns();
                if (it >= 2) cpu_g_us.push_back(static_cast<double>(t1 - t0) / 1e3);

                uint64_t m0 = now_ns();
                BloomGlyphInst *bg = static_cast<BloomGlyphInst *>(buf_b.contents);
                uint16_t u_w = static_cast<uint16_t>((12.0f / 1024.0f) * 65535.0f);
                uint16_t u_h = static_cast<uint16_t>((24.0f / 1024.0f) * 65535.0f);
                uint16_t u_v = static_cast<uint16_t>((16.0f / 1024.0f) * 65535.0f);
                for (uint32_t i = 0; i < N; ++i) {
                    float x = static_cast<float>((i % 280u) * 13u + 16u);
                    float y = static_cast<float>(((i / 280u) % 75u) * 28u + 16u);
                    BloomGlyphInst item;
                    item.dst_q14_2[0] = pack_q14_2(x);
                    item.dst_q14_2[1] = pack_q14_2(y);
                    item.dst_q14_2[2] = 48u; // 12 * 4
                    item.dst_q14_2[3] = 96u; // 24 * 4
                    item.uv_unorm16[0] = static_cast<uint16_t>((static_cast<float>((i & 63u) * 14u) / 1024.0f) * 65535.0f);
                    item.uv_unorm16[1] = u_v;
                    item.uv_unorm16[2] = u_w;
                    item.uv_unorm16[3] = u_h;
                    item.rgba8 = 0xFFE8DCE0u;
                    item.clip_id = 0;
                    item.z_flags = 1;
                    bg[i] = item;
                }
                uint64_t m1 = now_ns();
                if (it >= 2) cpu_b_us.push_back(static_cast<double>(m1 - m0) / 1e3);
            }

            std::vector<double> g_ms, cv_ms, r20_ms;
            for (int it = 0; it < 24; ++it) {
                auto draw = [&](id<MTLRenderPipelineState> pso, id<MTLBuffer> buf, id<MTLTexture> atl, int mode) -> double {
                    id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
                    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                    rpd.colorAttachments[0].texture = rt;
                    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                    rpd.depthAttachment.texture = depth;
                    rpd.depthAttachment.loadAction = MTLLoadActionClear;
                    rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
                    [enc setRenderPipelineState:pso];
                    [enc setDepthStencilState:ds_off];
                    [enc setVertexBuffer:buf offset:0 atIndex:0];
                    if (mode == 0) {
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
                        [enc setVertexBytes:atlas_sz length:sizeof(atlas_sz) atIndex:2];
                    } else {
                        [enc setVertexBuffer:clips offset:0 atIndex:1];
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                    }
                    if (mode == 2) [enc setFragmentBuffer:buf offset:0 atIndex:0];
                    [enc setFragmentTexture:atl atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N];
                    [enc endEncoding];
                    [cmd commit]; [cmd waitUntilCompleted];
                    return (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
                };
                double dg = draw(p_gpui, buf_g, atlas_shared, 0);
                double dc = draw(p_clipv, buf_b, atlas_priv, 1);
                double dr = draw(p_ref20, buf_b, atlas_priv, 2);
                if (it >= 4) { g_ms.push_back(dg); cv_ms.push_back(dc); r20_ms.push_back(dr); }
            }
            Stats scg = compute_stats(cpu_g_us), scb = compute_stats(cpu_b_us);
            Stats sg = compute_stats(g_ms), sc = compute_stats(cv_ms), sr = compute_stats(r20_ms);
            printf("  %5u Glyphs @ 4K | GPUI(112B,%.0fKB,CPU %.1fus)=%.3f ms | BLOOM(24B,%.0fKB,CPU %.1fus): ClipVert=%.3f ms Refetch20B=%.3f ms\n",
                   N, b_gpui / 1024.0, scg.median_val, sg.median_val,
                   b_bloom / 1024.0, scb.median_val, sc.median_val, sr.median_val);
            if (ci > 0) js << ",\n";
            js << "      { \"count\": " << N
               << ", \"gpui_112b\": { \"buf_bytes\": " << b_gpui << ", \"cpu_us\": " << scg.median_val << ", \"gpu_ms\": " << sg.median_val << " }"
               << ", \"bloom_24b\": { \"buf_bytes\": " << b_bloom << ", \"cpu_us\": " << scb.median_val
               << ", \"clipvert_gpu_ms\": " << sc.median_val << ", \"refetch20b_gpu_ms\": " << sr.median_val << " } }";
        }
    }
    js << "\n    ]\n  }";
    return js.str();
}

// ============================================================================
// [2/6] Dual-Pass GPUI (160B x 2 vertex-cull) vs Pre-Partitioned BLOOM (32B x 1)
// ============================================================================
static std::string bench_dual_pass(Ctx &ctx) {
    printf("\n=== [2/6] dual_pass_vs_prepartitioned: 20,000 Mixed Quads (50%% Opaque / 50%% Alpha) @ 4K ===\n");
    id<MTLRenderPipelineState> p_g_opq = rpso(ctx, "v_gpui_opaque_quad", "f_gpui_opaque_quad", false);
    id<MTLRenderPipelineState> p_g_alp = rpso(ctx, "v_gpui_alpha_quad", "f_gpui_alpha_quad", true);
    id<MTLRenderPipelineState> p_b_opq = rpso(ctx, "v_bloom_opaque_32", "f_bloom_opaque_32", false);
    id<MTLRenderPipelineState> p_b_alp = rpso(ctx, "v_bloom_quad32_q14_refetch20", "f_bloom_quad32_q14_refetch20", true);

    MTLDepthStencilDescriptor *dwd = [[MTLDepthStencilDescriptor alloc] init];
    dwd.depthCompareFunction = MTLCompareFunctionLess;
    dwd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_write = [ctx.dev newDepthStencilStateWithDescriptor:dwd];

    MTLDepthStencilDescriptor *dtd = [[MTLDepthStencilDescriptor alloc] init];
    dtd.depthCompareFunction = MTLCompareFunctionLessEqual;
    dtd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_test = [ctx.dev newDepthStencilStateWithDescriptor:dtd];

    const uint32_t W = 3840, H = 2160, N = 20000u;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    id<MTLTexture> rt = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> depth = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    id<MTLBuffer> clips = [ctx.dev newBufferWithLength:sizeof(BloomClipRect) * 16 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    static_cast<BloomClipRect *>(clips.contents)[0] = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};

    size_t g_bytes = N * sizeof(GpuiQuad160);
    size_t b_part_bytes = (N / 2) * sizeof(BloomQuadHot);
    id<MTLBuffer> buf_g = [ctx.dev newBufferWithLength:g_bytes options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> buf_bo = [ctx.dev newBufferWithLength:b_part_bytes options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> buf_ba = [ctx.dev newBufferWithLength:b_part_bytes options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

    std::vector<double> cpu_g_us, cpu_b_us;
    for (int it = 0; it < 12; ++it) {
        uint64_t t0 = now_ns();
        GpuiQuad160 *qg = static_cast<GpuiQuad160 *>(buf_g.contents);
        for (uint32_t i = 0; i < N; ++i) {
            float x = static_cast<float>((i * 124u) % (W - 140u));
            float y = static_cast<float>(((i * 37u) % 64u) * 33u);
            GpuiQuad160 item{};
            item.order = (i & 16383u) + 1u;
            item.bounds = {x, y, 120.0f, 32.0f};
            item.content_mask = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};
            bool opq = (i & 1u) == 0u;
            item.background.solid = {0.55f, 0.6f, 0.45f, opq ? 1.0f : 0.88f};
            item.border_color = {0.55f, 0.4f, 0.7f, 1.0f};
            item.corner_radii = opq ? RawF4{0, 0, 0, 0} : RawF4{6, 6, 6, 6};
            item.border_widths = opq ? RawF4{0, 0, 0, 0} : RawF4{1.5f, 1.5f, 1.5f, 1.5f};
            qg[i] = item;
        }
        uint64_t t1 = now_ns();
        if (it >= 2) cpu_g_us.push_back(static_cast<double>(t1 - t0) / 1e3);

        uint64_t m0 = now_ns();
        BloomQuadHot *qo = static_cast<BloomQuadHot *>(buf_bo.contents);
        BloomQuadHot *qa = static_cast<BloomQuadHot *>(buf_ba.contents);
        uint32_t io = 0, ia = 0;
        for (uint32_t i = 0; i < N; ++i) {
            float x = static_cast<float>((i * 124u) % (W - 140u));
            float y = static_cast<float>(((i * 37u) % 64u) * 33u);
            BloomQuadHot item;
            item.bounds_q14_2[0] = pack_q14_2(x);
            item.bounds_q14_2[1] = pack_q14_2(y);
            item.bounds_q14_2[2] = 480u;
            item.bounds_q14_2[3] = 128u;
            item.rgba8_border = 0xFF88AACC;
            item.clip_id = 0;
            item.style_id = 0;
            item.z_flags = (i & 16383u) + 1u;
            if ((i & 1u) == 0u) {
                item.rgba8_fill = 0xFF334455u;
                std::memset(item.radii_q6_2, 0, 4);
                std::memset(item.borders_q6_2, 0, 4);
                qo[io++] = item;
            } else {
                item.rgba8_fill = 0xE0334455u;
                std::memset(item.radii_q6_2, pack_q6_2(6.0f), 4);
                std::memset(item.borders_q6_2, pack_q6_2(1.5f), 4);
                qa[ia++] = item;
            }
        }
        uint64_t m1 = now_ns();
        if (it >= 2) cpu_b_us.push_back(static_cast<double>(m1 - m0) / 1e3);
    }

    std::vector<double> gpu_g_ms, gpu_b_ms;
    for (int it = 0; it < 24; ++it) {
        {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt;
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = depth;
            rpd.depthAttachment.loadAction = MTLLoadActionClear;
            rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
            rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            [enc setRenderPipelineState:p_g_opq];
            [enc setDepthStencilState:ds_write];
            [enc setVertexBuffer:buf_g offset:0 atIndex:0];
            [enc setVertexBytes:&N length:sizeof(N) atIndex:1];
            [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N];

            [enc setRenderPipelineState:p_g_alp];
            [enc setDepthStencilState:ds_test];
            [enc setVertexBuffer:buf_g offset:0 atIndex:0];
            [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
            [enc setFragmentBuffer:buf_g offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N];
            [enc endEncoding];
            [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 4) gpu_g_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
        }
        {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt;
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = depth;
            rpd.depthAttachment.loadAction = MTLLoadActionClear;
            rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
            rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            [enc setRenderPipelineState:p_b_opq];
            [enc setDepthStencilState:ds_write];
            [enc setVertexBuffer:buf_bo offset:0 atIndex:0];
            [enc setVertexBuffer:clips offset:0 atIndex:1];
            [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N / 2];

            [enc setRenderPipelineState:p_b_alp];
            [enc setDepthStencilState:ds_test];
            [enc setVertexBuffer:buf_ba offset:0 atIndex:0];
            [enc setVertexBuffer:clips offset:0 atIndex:1];
            [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
            [enc setFragmentBuffer:buf_ba offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N / 2];
            [enc endEncoding];
            [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 4) gpu_b_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
        }
    }

    Stats scg = compute_stats(cpu_g_us), scb = compute_stats(cpu_b_us);
    Stats sgg = compute_stats(gpu_g_ms), sgb = compute_stats(gpu_b_ms);
    printf("  GPUI DualPass-Cull (2x20k @ 160B=%.0fKB) : CPU=%5.1fus | GPU=%6.3f ms\n",
           g_bytes / 1024.0, scg.median_val, sgg.median_val);
    printf("  BLOOM Pre-Part     (2x10k @  32B=%.0fKB) : CPU=%5.1fus | GPU=%6.3f ms (%.2fx faster GPU, %.1fx smaller buf)\n",
           (b_part_bytes * 2) / 1024.0, scb.median_val, sgb.median_val,
           sgg.median_val / sgb.median_val, static_cast<double>(g_bytes) / (b_part_bytes * 2));

    std::ostringstream js;
    js << "  \"dual_pass_vs_prepartitioned\": {\n"
       << "    \"total_quads\": " << N << ",\n"
       << "    \"gpui_dual_cull\": { \"buf_bytes\": " << g_bytes << ", \"cpu_us\": " << scg.median_val << ", \"gpu_ms\": " << sgg.median_val << " },\n"
       << "    \"bloom_prepartitioned\": { \"buf_bytes\": " << (b_part_bytes * 2) << ", \"cpu_us\": " << scb.median_val << ", \"gpu_ms\": " << sgb.median_val << " }\n"
       << "  }";
    return js.str();
}

// ============================================================================
// [3/6] Analytic erf7 Shadows (GPUI 112B+100B StageIn vs BLOOM 32B Refetch20B)
// ============================================================================
static std::string bench_shadows(Ctx &ctx) {
    printf("\n=== [3/6] analytic_shadows: Raph Levien erf7 Shadows (240x120, blur=12) @ 4K ===\n");
    id<MTLRenderPipelineState> p_g = rpso(ctx, "v_gpui_shadow", "f_gpui_shadow", true);
    id<MTLRenderPipelineState> p_b = rpso(ctx, "v_bloom_shadow_refetch20", "f_bloom_shadow_refetch20", true);

    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_off = [ctx.dev newDepthStencilStateWithDescriptor:dsd];

    const uint32_t W = 3840, H = 2160;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    id<MTLTexture> rt = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> depth = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    id<MTLBuffer> clips = [ctx.dev newBufferWithLength:sizeof(BloomClipRect) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    *static_cast<BloomClipRect *>(clips.contents) = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};

    auto cpu_erf7 = [](float x) -> float {
        x = std::clamp(x, -10.0f, 10.0f) * (2.0f / std::sqrt(static_cast<float>(M_PI)));
        float xx = x * x;
        x = x + (0.24295f + (0.03395f + 0.0104f * xx) * xx) * (x * xx);
        return x / std::sqrt(1.0f + x * x);
    };

    const uint32_t counts[] = {64u, 512u, 2000u};
    std::ostringstream js;
    js << "  \"analytic_shadows\": [\n";
    for (size_t ci = 0; ci < 3; ++ci) {
        @autoreleasepool {
            uint32_t N = counts[ci];
            id<MTLBuffer> buf_g = [ctx.dev newBufferWithLength:N * sizeof(GpuiShadow112) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
            id<MTLBuffer> buf_b = [ctx.dev newBufferWithLength:N * sizeof(BloomShadowHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

            std::vector<double> cpu_g_us, cpu_b_us;
            for (int it = 0; it < 12; ++it) {
                uint64_t t0 = now_ns();
                GpuiShadow112 *sg = static_cast<GpuiShadow112 *>(buf_g.contents);
                for (uint32_t i = 0; i < N; ++i) {
                    float x = static_cast<float>((i * 170u) % (W - 260u));
                    float y = static_cast<float>(((i * 53u) % 16u) * 120u + 20u);
                    GpuiShadow112 item{};
                    item.order = 1;
                    item.blur_radius = 12.0f;
                    item.bounds = {x, y, 240.0f, 120.0f};
                    item.corner_radii = {12.0f, 12.0f, 12.0f, 12.0f};
                    item.content_mask = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};
                    item.color = {0.0f, 0.0f, 0.0f, 0.35f};
                    item.shape_bounds = {x + 12.0f, y + 12.0f, 216.0f, 96.0f};
                    item.shape_corner_radii = {10.0f, 10.0f, 10.0f, 10.0f};
                    sg[i] = item;
                }
                uint64_t t1 = now_ns();
                if (it >= 2) cpu_g_us.push_back(static_cast<double>(t1 - t0) / 1e3);

                uint64_t m0 = now_ns();
                BloomShadowHot *sb = static_cast<BloomShadowHot *>(buf_b.contents);
                for (uint32_t i = 0; i < N; ++i) {
                    float x = static_cast<float>((i * 170u) % (W - 260u));
                    float y = static_cast<float>(((i * 53u) % 16u) * 120u + 20u);
                    float sw = 216.0f, sh = 96.0f, blur = 12.0f, r = 10.0f;
                    float s = blur * std::sqrt(2.0f), s_inv = 1.0f / s, min_e = std::min(sw, sh), rmax = 0.5f * min_e;
                    float r0 = std::min(std::sqrt(r * r + 1.15f * 1.15f * s * s), rmax);
                    float r1 = std::min(std::sqrt(r * r + 4.0f * s * s), rmax);
                    float hx = 0.5f * s_inv * sw, hy = 0.5f * s_inv * sh;
                    float delta = 1.25f * s * (std::exp(-hx * hx) - std::exp(-hy * hy));
                    float ax = sw + std::min(delta, 0.0f), ay = sh - std::max(delta, 0.0f);
                    float peak = s_inv * 0.5f * (std::max(ax, ay) - 0.5f * r);
                    float sc = 0.5f * cpu_erf7(peak);

                    BloomShadowHot item;
                    item.bounds_q14_2[0] = pack_q14_2(x);
                    item.bounds_q14_2[1] = pack_q14_2(y);
                    item.bounds_q14_2[2] = 960u;
                    item.bounds_q14_2[3] = 480u;
                    item.shape_params[0] = f32_to_f16_hw(0.5f * ax);
                    item.shape_params[1] = f32_to_f16_hw(0.5f * ay);
                    item.shape_params[2] = f32_to_f16_hw(s_inv);
                    item.shape_params[3] = f32_to_f16_hw(min_e);
                    item.blur_r1_exp_sc[0] = f32_to_f16_hw(r1);
                    item.blur_r1_exp_sc[1] = f32_to_f16_hw(2.0f * r1 / r0);
                    item.blur_r1_exp_sc[2] = f32_to_f16_hw(sc);
                    item.blur_r1_exp_sc[3] = f32_to_f16_hw(r);
                    item.rgba8 = 0x59000000u;
                    item.clip_id = 0;
                    item.z_flags = 1;
                    sb[i] = item;
                }
                uint64_t m1 = now_ns();
                if (it >= 2) cpu_b_us.push_back(static_cast<double>(m1 - m0) / 1e3);
            }

            std::vector<double> g_ms, b_ms;
            for (int it = 0; it < 24; ++it) {
                auto run_p = [&](id<MTLRenderPipelineState> pso, id<MTLBuffer> buf, bool has_clip) -> double {
                    id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
                    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                    rpd.colorAttachments[0].texture = rt;
                    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                    rpd.depthAttachment.texture = depth;
                    rpd.depthAttachment.loadAction = MTLLoadActionClear;
                    rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
                    [enc setRenderPipelineState:pso];
                    [enc setDepthStencilState:ds_off];
                    [enc setVertexBuffer:buf offset:0 atIndex:0];
                    if (has_clip) {
                        [enc setVertexBuffer:clips offset:0 atIndex:1];
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                    } else {
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
                    }
                    [enc setFragmentBuffer:buf offset:0 atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N];
                    [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
                    return (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
                };
                double dg = run_p(p_g, buf_g, false);
                double db = run_p(p_b, buf_b, true);
                if (it >= 4) { g_ms.push_back(dg); b_ms.push_back(db); }
            }
            Stats scg = compute_stats(cpu_g_us), scb = compute_stats(cpu_b_us);
            Stats sgg = compute_stats(g_ms), sgb = compute_stats(b_ms);
            printf("  %4u Shadows @ 4K | GPUI(112B+100B StageIn, CPU %.1fus)=%.3f ms | BLOOM(32B+Refetch20B, CPU %.1fus)=%.3f ms (%.2fx faster GPU)\n",
                   N, scg.median_val, sgg.median_val, scb.median_val, sgb.median_val, sgg.median_val / sgb.median_val);
            if (ci > 0) js << ",\n";
            js << "    { \"count\": " << N
               << ", \"gpui_112b\": { \"cpu_us\": " << scg.median_val << ", \"gpu_ms\": " << sgg.median_val << " }"
               << ", \"bloom_32b_refetch20\": { \"cpu_us\": " << scb.median_val << ", \"gpu_ms\": " << sgb.median_val << " } }";
        }
    }
    js << "\n  ]";
    return js.str();
}

// ============================================================================
// [4/6] Slug PathTiles vs Cached Private R8Unorm Atlas @ 4K
// ============================================================================
static std::string bench_slug_paths(Ctx &ctx) {
    printf("\n=== [4/6] slug_path_tiles_vs_atlas: Curved Vector Icons (48x48, 16 curves/icon) @ 4K ===\n");
    id<MTLRenderPipelineState> p_gslug = rpso(ctx, "v_gpui_path_tile", "f_gpui_path_tile", true);
    id<MTLRenderPipelineState> p_bslug = rpso(ctx, "v_bloom_path_tile_refetch20", "f_bloom_path_tile_refetch20", true);
    id<MTLRenderPipelineState> p_atlas = rpso(ctx, "v_bloom_glyph_refetch20", "f_bloom_glyph_refetch20", true);

    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_off = [ctx.dev newDepthStencilStateWithDescriptor:dsd];

    const uint32_t W = 3840, H = 2160;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    id<MTLTexture> rt = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> depth = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    id<MTLTexture> icon_atlas = tex2d(ctx.dev, 1024, 1024, MTLPixelFormatR8Unorm, MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLBuffer> clips = [ctx.dev newBufferWithLength:sizeof(BloomClipRect) options:MTLResourceStorageModeShared];
    *static_cast<BloomClipRect *>(clips.contents) = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};

    const uint32_t icon_counts[] = {256u, 2048u};
    std::ostringstream js;
    js << "  \"slug_path_tiles_vs_atlas\": [\n";

    for (size_t ci = 0; ci < 2; ++ci) {
        @autoreleasepool {
            uint32_t icons = icon_counts[ci];
            uint32_t n_tiles = icons * 4u;    // 4 24x24 tiles per 48x48 icon
            uint32_t n_curves = n_tiles * 4u; // 4 monotone quadratic curves per tile = 16/icon

            id<MTLBuffer> buf_paints = [ctx.dev newBufferWithLength:icons * sizeof(GpuiPathPaint112) options:MTLResourceStorageModeShared];
            id<MTLBuffer> buf_rgba   = [ctx.dev newBufferWithLength:icons * sizeof(uint32_t) options:MTLResourceStorageModeShared];
            id<MTLBuffer> buf_tiles  = [ctx.dev newBufferWithLength:n_tiles * sizeof(GpuiPathTile56) options:MTLResourceStorageModeShared];
            id<MTLBuffer> buf_curves = [ctx.dev newBufferWithLength:n_curves * sizeof(GpuiPathCurve40) options:MTLResourceStorageModeShared];
            id<MTLBuffer> buf_icons  = [ctx.dev newBufferWithLength:icons * sizeof(BloomGlyphInst) options:MTLResourceStorageModeShared];

            GpuiPathPaint112 *pp = static_cast<GpuiPathPaint112 *>(buf_paints.contents);
            uint32_t *pr = static_cast<uint32_t *>(buf_rgba.contents);
            GpuiPathTile56 *pt = static_cast<GpuiPathTile56 *>(buf_tiles.contents);
            GpuiPathCurve40 *pc = static_cast<GpuiPathCurve40 *>(buf_curves.contents);
            BloomGlyphInst *pi = static_cast<BloomGlyphInst *>(buf_icons.contents);

            for (uint32_t i = 0; i < icons; ++i) {
                float x = static_cast<float>((i % 75u) * 50u + 12u);
                float y = static_cast<float>(((i / 75u) % 40u) * 52u + 12u);
                pp[i] = {{x, y, 48.0f, 48.0f}, {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)},
                         {0, 0, {0.55f, 0.7f, 0.6f, 1.0f}, 0.0f, {0,0,0,0}, 0, {0,0,0,0}, 0, 0}, 0, 0};
                pr[i] = 0xFFEE9944u;
                pi[i].dst_q14_2[0] = pack_q14_2(x);
                pi[i].dst_q14_2[1] = pack_q14_2(y);
                pi[i].dst_q14_2[2] = 192u;
                pi[i].dst_q14_2[3] = 192u;
                pi[i].uv_unorm16[0] = static_cast<uint16_t>((i & 15u) * 3072u);
                pi[i].uv_unorm16[1] = static_cast<uint16_t>(((i >> 4) & 15u) * 3072u);
                pi[i].uv_unorm16[2] = 3072u;
                pi[i].uv_unorm16[3] = 3072u;
                pi[i].rgba8 = 0xFFEE9944u;
                pi[i].clip_id = 0;
                pi[i].z_flags = 1;

                for (uint32_t q = 0; q < 4u; ++q) {
                    float tx = x + ((q & 1u) ? 24.0f : 0.0f);
                    float ty = y + ((q & 2u) ? 24.0f : 0.0f);
                    uint32_t tidx = i * 4u + q;
                    uint32_t cstart = tidx * 4u;
                    pt[tidx] = {i, cstart, 4u, 0, {tx, ty}, {tx + 24.0f, ty + 24.0f},
                                {tx, ty}, {tx, ty}, {tx + 24.0f, ty + 24.0f}};
                    for (uint32_t k = 0; k < 4u; ++k) {
                        float x0 = tx + 2.0f + k * 4.0f, y0 = ty + 2.0f;
                        float x1 = x0 + 12.0f, y1 = ty + 22.0f;
                        pc[cstart + k] = {{x0, y0}, {x1, y1}, {2.0f, 3.0f}, {10.0f, 17.0f}, (k & 1u) ? -1.0f : 1.0f, 0};
                    }
                }
            }

            std::vector<double> gs_ms, bs_ms, atl_ms;
            for (int it = 0; it < 24; ++it) {
                auto draw = [&](int kind) -> double {
                    id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
                    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                    rpd.colorAttachments[0].texture = rt;
                    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                    rpd.depthAttachment.texture = depth;
                    rpd.depthAttachment.loadAction = MTLLoadActionClear;
                    rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
                    [enc setDepthStencilState:ds_off];
                    if (kind == 0) {
                        [enc setRenderPipelineState:p_gslug];
                        [enc setVertexBuffer:buf_tiles offset:0 atIndex:0];
                        [enc setVertexBuffer:buf_paints offset:0 atIndex:1];
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                        [enc setFragmentBuffer:buf_tiles offset:0 atIndex:0];
                        [enc setFragmentBuffer:buf_curves offset:0 atIndex:1];
                        [enc setFragmentBuffer:buf_paints offset:0 atIndex:2];
                        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_tiles];
                    } else if (kind == 1) {
                        [enc setRenderPipelineState:p_bslug];
                        [enc setVertexBuffer:buf_tiles offset:0 atIndex:0];
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
                        [enc setFragmentBuffer:buf_tiles offset:0 atIndex:0];
                        [enc setFragmentBuffer:buf_curves offset:0 atIndex:1];
                        [enc setFragmentBuffer:buf_rgba offset:0 atIndex:2];
                        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_tiles];
                    } else {
                        [enc setRenderPipelineState:p_atlas];
                        [enc setVertexBuffer:buf_icons offset:0 atIndex:0];
                        [enc setVertexBuffer:clips offset:0 atIndex:1];
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                        [enc setFragmentBuffer:buf_icons offset:0 atIndex:0];
                        [enc setFragmentTexture:icon_atlas atIndex:0];
                        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:icons];
                    }
                    [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
                    return (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
                };
                double d0 = draw(0), d1 = draw(1), d2 = draw(2);
                if (it >= 4) { gs_ms.push_back(d0); bs_ms.push_back(d1); atl_ms.push_back(d2); }
            }
            Stats s0 = compute_stats(gs_ms), s1 = compute_stats(bs_ms), s2 = compute_stats(atl_ms);
            printf("  %4u Vector Icons (%5u tiles) | GPUI Slug=%.3f ms | BLOOM Refetch20 Slug=%.3f ms | Cached Private Atlas=%.3f ms (Atlas %.1fx vs Slug)\n",
                   icons, n_tiles, s0.median_val, s1.median_val, s2.median_val, s0.median_val / s2.median_val);
            if (ci > 0) js << ",\n";
            js << "    { \"icons\": " << icons << ", \"tiles\": " << n_tiles
               << ", \"gpui_slug_ms\": " << s0.median_val
               << ", \"bloom_slug_refetch20_ms\": " << s1.median_val
               << ", \"cached_private_atlas_ms\": " << s2.median_val << " }";
        }
    }
    js << "\n  ]";
    return js.str();
}

// ============================================================================
// [5/6] BoundsTree CPU Order & Batch Merge Benchmark (Zed primitives.rs patterns)
// ============================================================================
static std::string bench_bounds_tree() {
    printf("\n=== [5/6] bounds_tree_cpu_batching: GPUI R-Tree Batching (primitives.rs patterns) ===\n");
    struct Item { RectF b; PrimKind k; uint32_t order; uint32_t seq; };
    const char *pat_names[] = {"grid", "stripes", "overlap", "nested", "clusters", "overlay", "random"};
    const uint32_t counts[] = {256u, 1024u, 4096u};
    const float extent = 2000.0f;

    std::ostringstream js;
    js << "  \"bounds_tree_cpu_batching\": [\n";
    bool first = true;
    GpuiBoundsTree tree;

    for (uint32_t count : counts) {
        for (int p = 0; p < 7; ++p) {
            std::vector<Item> items(count);
            uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(count))));
            float cell = extent / static_cast<float>(cols);
            uint32_t stripe_cnt = static_cast<uint32_t>(std::floor(extent / 4.0f));
            uint64_t rng = 0x20261124ULL ^ (static_cast<uint64_t>(count) * 1315423911ULL) ^ static_cast<uint64_t>(p);
            auto next_f = [&]() -> float {
                rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
                return static_cast<float>((rng * 2685821657736338717ULL) >> 40) / static_cast<float>(1u << 24);
            };
            float lo_ln = std::log(2.0f), hi_ln = std::log(extent * 0.6f);

            for (uint32_t i = 0; i < count; ++i) {
                RectF r{};
                PrimKind k = KIND_QUAD;
                if (p == 0) { // Grid
                    r = {32.0f + (i % cols) * cell, 32.0f + (i / cols) * cell, cell * 0.4f, cell * 0.4f};
                } else if (p == 1) { // Stripes
                    r = {32.0f, 32.0f + (i % stripe_cnt) * 4.0f, extent, 2.0f};
                } else if (p == 2) { // Overlap
                    r = {32.0f, 32.0f, extent, extent};
                } else if (p == 3) { // Nested
                    float ins = extent * 0.3f * static_cast<float>(i) / static_cast<float>(count);
                    r = {32.0f + ins, 32.0f + ins, extent - 2.0f * ins, extent - 2.0f * ins};
                } else if (p == 4) { // Clusters
                    float orig = 32.0f + (i % 2u) * extent * 0.55f;
                    r = {orig, orig, extent * 0.4f, extent * 0.4f};
                } else if (p == 5) { // Overlay
                    if (i % 64u == 0u) r = {32.0f, 32.0f, extent, extent};
                    else r = {32.0f + (i % cols) * cell, 32.0f + (i / cols) * cell, cell * 0.4f, cell * 0.4f};
                } else { // Random mixed (Shadow/Quad/Underline/MonoSprite)
                    float w = std::exp(lo_ln + next_f() * (hi_ln - lo_ln));
                    float h = std::exp(lo_ln + next_f() * (hi_ln - lo_ln));
                    r = {32.0f + next_f() * (extent - w), 32.0f + next_f() * (extent - h), w, h};
                    uint32_t roll = static_cast<uint32_t>(next_f() * 20.0f);
                    if (roll < 9) k = KIND_QUAD;
                    else if (roll < 11) k = KIND_SHADOW;
                    else if (roll < 13) k = KIND_UNDERLINE;
                    else if (roll < 15) k = KIND_PATH;
                    else k = KIND_MONO_SPRITE;
                }
                items[i] = {r, k, 0, i};
            }

            uint32_t naive_batches = 1;
            for (uint32_t i = 1; i < count; ++i) {
                if (items[i].k != items[i - 1].k) ++naive_batches;
            }

            std::vector<double> us_list;
            uint32_t bt_batches = 0;
            std::vector<Item> sorted = items;
            for (int it = 0; it < 25; ++it) {
                sorted = items;
                uint64_t t0 = now_ns();
                tree.clear();
                for (uint32_t i = 0; i < count; ++i) {
                    sorted[i].order = tree.insert(sorted[i].b, sorted[i].k);
                }
                std::sort(sorted.begin(), sorted.end(), [](const Item &a, const Item &b) {
                    if (a.order != b.order) return a.order < b.order;
                    if (a.k != b.k) return a.k < b.k;
                    return a.seq < b.seq;
                });
                uint32_t batches = 1;
                for (uint32_t i = 1; i < count; ++i) {
                    if (sorted[i].k != sorted[i - 1].k) ++batches;
                }
                uint64_t t1 = now_ns();
                bt_batches = batches;
                if (it >= 5) us_list.push_back(static_cast<double>(t1 - t0) / 1e3);
            }
            Stats st = compute_stats(us_list);
            if (p == 0 || p == 2 || p == 5 || p == 6) {
                printf("  N=%4u %-8s : BoundsTree+Sort=%6.1f us | naive_batches=%4u -> merged_batches=%4u (%.1fx)\n",
                       count, pat_names[p], st.median_val, naive_batches, bt_batches,
                       static_cast<double>(naive_batches) / std::max(1u, bt_batches));
            }
            if (!first) js << ",\n";
            first = false;
            js << "    { \"count\": " << count << ", \"pattern\": \"" << pat_names[p]
               << "\", \"cpu_us\": " << st.median_val << ", \"naive_batches\": " << naive_batches
               << ", \"merged_batches\": " << bt_batches << " }";
        }
    }
    js << "\n  ]";
    return js.str();
}

// ============================================================================
// [6/6] End-to-End 4K IDE + Bento HUD Composite Frame
// ============================================================================
static std::string bench_full_composite(Ctx &ctx) {
    printf("\n=== [6/6] full_4k_ide_hud_composite: Realistic 4K Editor + HUD Frame ===\n");
    const uint32_t W = 3840, H = 2160;
    const uint32_t n_opq = 120u, n_alp = 80u, n_sh = 24u, n_gl = 15000u, n_ul = 40u;
    const uint32_t n_quad_all = n_opq + n_alp;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    float atlas_sz[2] = {1024.0f, 1024.0f};

    id<MTLTexture> rt = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> depth = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    id<MTLTexture> atl_sh = tex2d(ctx.dev, 1024, 1024, MTLPixelFormatA8Unorm, MTLTextureUsageShaderRead, MTLStorageModeShared, false);
    id<MTLTexture> atl_pr = tex2d(ctx.dev, 1024, 1024, MTLPixelFormatR8Unorm, MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLBuffer> clips = [ctx.dev newBufferWithLength:sizeof(BloomClipRect) * 16 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    static_cast<BloomClipRect *>(clips.contents)[0] = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};

    // Pipelines
    id<MTLRenderPipelineState> pg_opq = rpso(ctx, "v_gpui_opaque_quad", "f_gpui_opaque_quad", false);
    id<MTLRenderPipelineState> pg_alp = rpso(ctx, "v_gpui_alpha_quad", "f_gpui_alpha_quad", true);
    id<MTLRenderPipelineState> pg_sh  = rpso(ctx, "v_gpui_shadow", "f_gpui_shadow", true);
    id<MTLRenderPipelineState> pg_sp  = rpso(ctx, "v_gpui_sprite", "f_gpui_sprite", true);
    id<MTLRenderPipelineState> pg_ul  = rpso(ctx, "v_gpui_underline", "f_gpui_underline", true);

    id<MTLRenderPipelineState> pb_opq = rpso(ctx, "v_bloom_opaque_32", "f_bloom_opaque_32", false);
    id<MTLRenderPipelineState> pb_alp = rpso(ctx, "v_bloom_quad32_q14_refetch20", "f_bloom_quad32_q14_refetch20", true);
    id<MTLRenderPipelineState> pb_sh  = rpso(ctx, "v_bloom_shadow_refetch20", "f_bloom_shadow_refetch20", true);
    id<MTLRenderPipelineState> pb_sp  = rpso(ctx, "v_bloom_glyph_refetch20", "f_bloom_glyph_refetch20", true);
    id<MTLRenderPipelineState> pb_ul  = rpso(ctx, "v_bloom_underline24", "f_bloom_underline24", true);

    MTLDepthStencilDescriptor *dwd = [[MTLDepthStencilDescriptor alloc] init];
    dwd.depthCompareFunction = MTLCompareFunctionLess;
    dwd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_write = [ctx.dev newDepthStencilStateWithDescriptor:dwd];
    MTLDepthStencilDescriptor *dtd = [[MTLDepthStencilDescriptor alloc] init];
    dtd.depthCompareFunction = MTLCompareFunctionLessEqual;
    dtd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_test = [ctx.dev newDepthStencilStateWithDescriptor:dtd];

    size_t g_bytes = n_quad_all * sizeof(GpuiQuad160) + n_sh * sizeof(GpuiShadow112) +
                     n_gl * sizeof(GpuiMonoSprite112) + n_ul * sizeof(GpuiUnderline64);
    size_t b_bytes = n_quad_all * sizeof(BloomQuadHot) + n_sh * sizeof(BloomShadowHot) +
                     n_gl * sizeof(BloomGlyphInst) + n_ul * sizeof(BloomUnderlineHot);

    id<MTLBuffer> g_q = [ctx.dev newBufferWithLength:n_quad_all * sizeof(GpuiQuad160) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> g_s = [ctx.dev newBufferWithLength:n_sh * sizeof(GpuiShadow112) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> g_g = [ctx.dev newBufferWithLength:n_gl * sizeof(GpuiMonoSprite112) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> g_u = [ctx.dev newBufferWithLength:n_ul * sizeof(GpuiUnderline64) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

    id<MTLBuffer> b_qo = [ctx.dev newBufferWithLength:n_opq * sizeof(BloomQuadHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> b_qa = [ctx.dev newBufferWithLength:n_alp * sizeof(BloomQuadHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> b_s  = [ctx.dev newBufferWithLength:n_sh * sizeof(BloomShadowHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> b_g  = [ctx.dev newBufferWithLength:n_gl * sizeof(BloomGlyphInst) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> b_u  = [ctx.dev newBufferWithLength:n_ul * sizeof(BloomUnderlineHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

    std::vector<double> cpu_g_us, cpu_b_us;
    for (int it = 0; it < 12; ++it) {
        uint64_t t0 = now_ns();
        auto *gq = static_cast<GpuiQuad160 *>(g_q.contents);
        for (uint32_t i = 0; i < n_quad_all; ++i) {
            bool opq = i < n_opq;
            gq[i] = {};
            gq[i].order = opq ? 2u : 4u;
            gq[i].bounds = {static_cast<float>((i % 12u) * 310u), static_cast<float>((i / 12u) * 190u), 300.0f, 180.0f};
            gq[i].content_mask = {0, 0, static_cast<float>(W), static_cast<float>(H)};
            gq[i].background.solid = {0.6f, 0.25f, 0.14f, opq ? 1.0f : 0.9f};
            gq[i].corner_radii = opq ? RawF4{0,0,0,0} : RawF4{8,8,8,8};
            gq[i].border_widths = opq ? RawF4{0,0,0,0} : RawF4{1.5f,1.5f,1.5f,1.5f};
        }
        auto *gs = static_cast<GpuiShadow112 *>(g_s.contents);
        for (uint32_t i = 0; i < n_sh; ++i) {
            float x = static_cast<float>((i % 6u) * 600u + 40u), y = static_cast<float>((i / 6u) * 500u + 40u);
            gs[i] = {1u, 12.0f, {x, y, 420.0f, 260.0f}, {10,10,10,10}, {0,0,float(W),float(H)},
                     {0,0,0,0.35f}, {x+12, y+12, 396, 236}, {8,8,8,8}, 0, 0};
        }
        auto *gg = static_cast<GpuiMonoSprite112 *>(g_g.contents);
        for (uint32_t i = 0; i < n_gl; ++i) {
            float x = static_cast<float>((i % 200u) * 14u + 320u);
            float y = static_cast<float>((i / 200u) * 26u + 40u);
            gg[i] = {5u, UINT32_MAX, {0, 0, i & 63u, 0, {static_cast<int32_t>((i & 63u)*14), 0, 12, 24}},
                     {x, y, 12, 24}, {0,0,float(W),float(H)}, {0.5f, 0.2f, 0.88f, 1.0f}, {1,0,0,1}, {0,0}};
        }
        auto *gu = static_cast<GpuiUnderline64 *>(g_u.contents);
        for (uint32_t i = 0; i < n_ul; ++i) {
            gu[i] = {6u, 0, {320.0f, 64.0f + i * 48.0f, 900.0f, 4.0f}, {0,0,float(W),float(H)},
                     {0.1f, 0.8f, 0.6f, 1.0f}, 2.0f, (i & 1u)};
        }
        uint64_t t1 = now_ns();
        if (it >= 2) cpu_g_us.push_back(static_cast<double>(t1 - t0) / 1e3);

        uint64_t m0 = now_ns();
        auto *bqo = static_cast<BloomQuadHot *>(b_qo.contents);
        auto *bqa = static_cast<BloomQuadHot *>(b_qa.contents);
        for (uint32_t i = 0; i < n_opq; ++i) {
            bqo[i] = {{pack_q14_2((i%12u)*310.f), pack_q14_2((i/12u)*190.f), 1200, 720}, 0xFF282420u, 0, {0,0,0,0}, {0,0,0,0}, 0, 0, 2u};
        }
        for (uint32_t i = 0; i < n_alp; ++i) {
            uint32_t k = n_opq + i;
            bqa[i] = {{pack_q14_2((k%12u)*310.f), pack_q14_2((k/12u)*190.f), 1200, 720}, 0xE6303844u, 0xFF6688AAu, {32,32,32,32}, {6,6,6,6}, 0, 0, 4u};
        }
        auto *bs = static_cast<BloomShadowHot *>(b_s.contents);
        uint16_t h_hw = f32_to_f16_hw(198.0f), h_hh = f32_to_f16_hw(118.0f), h_sinv = f32_to_f16_hw(0.0589f), h_me = f32_to_f16_hw(236.0f);
        uint16_t h_r1 = f32_to_f16_hw(35.0f), h_ex = f32_to_f16_hw(3.2f), h_sc = f32_to_f16_hw(0.5f), h_r = f32_to_f16_hw(8.0f);
        for (uint32_t i = 0; i < n_sh; ++i) {
            float x = static_cast<float>((i % 6u) * 600u + 40u), y = static_cast<float>((i / 6u) * 500u + 40u);
            bs[i] = {{pack_q14_2(x), pack_q14_2(y), 1680, 1040}, {h_hw, h_hh, h_sinv, h_me}, {h_r1, h_ex, h_sc, h_r}, 0x59000000u, 0, 1u};
        }
        auto *bg = static_cast<BloomGlyphInst *>(b_g.contents);
        for (uint32_t i = 0; i < n_gl; ++i) {
            bg[i] = {{pack_q14_2((i%200u)*14.f+320.f), pack_q14_2((i/200u)*26.f+40.f), 48, 96},
                     {static_cast<uint16_t>((i&63u)*896u), 0, 768, 1536}, 0xFFE8DCE0u, 0, 5u};
        }
        auto *bu = static_cast<BloomUnderlineHot *>(b_u.contents);
        uint16_t h_th = f32_to_f16_hw(2.0f);
        for (uint32_t i = 0; i < n_ul; ++i) {
            bu[i] = {{pack_q14_2(320.f), pack_q14_2(64.f + i * 48.f), 3600, 16}, 0xFFCC8844u, h_th, static_cast<uint16_t>(i & 1u), 0, 6u, 0};
        }
        uint64_t m1 = now_ns();
        if (it >= 2) cpu_b_us.push_back(static_cast<double>(m1 - m0) / 1e3);
    }

    std::vector<double> gpu_g_ms, gpu_b_ms;
    for (int it = 0; it < 24; ++it) {
        // GPUI Composite
        {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = depth; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

            [enc setRenderPipelineState:pg_opq]; [enc setDepthStencilState:ds_write];
            [enc setVertexBuffer:g_q offset:0 atIndex:0]; [enc setVertexBytes:&n_quad_all length:sizeof(n_quad_all) atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_quad_all];

            [enc setDepthStencilState:ds_test];
            [enc setRenderPipelineState:pg_sh]; [enc setVertexBuffer:g_s offset:0 atIndex:0]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:1]; [enc setFragmentBuffer:g_s offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_sh];

            [enc setRenderPipelineState:pg_alp]; [enc setVertexBuffer:g_q offset:0 atIndex:0]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:1]; [enc setFragmentBuffer:g_q offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_quad_all];

            [enc setRenderPipelineState:pg_sp]; [enc setVertexBuffer:g_g offset:0 atIndex:0]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:1]; [enc setVertexBytes:atlas_sz length:sizeof(atlas_sz) atIndex:2]; [enc setFragmentTexture:atl_sh atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_gl];

            [enc setRenderPipelineState:pg_ul]; [enc setVertexBuffer:g_u offset:0 atIndex:0]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:1]; [enc setFragmentBuffer:g_u offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_ul];

            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 4) gpu_g_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
        }
        // BLOOM vNext Composite
        {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = depth; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

            [enc setRenderPipelineState:pb_opq]; [enc setDepthStencilState:ds_write];
            [enc setVertexBuffer:b_qo offset:0 atIndex:0]; [enc setVertexBuffer:clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_opq];

            [enc setDepthStencilState:ds_test];
            [enc setRenderPipelineState:pb_sh]; [enc setVertexBuffer:b_s offset:0 atIndex:0]; [enc setVertexBuffer:clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2]; [enc setFragmentBuffer:b_s offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_sh];

            [enc setRenderPipelineState:pb_alp]; [enc setVertexBuffer:b_qa offset:0 atIndex:0]; [enc setVertexBuffer:clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2]; [enc setFragmentBuffer:b_qa offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_alp];

            [enc setRenderPipelineState:pb_sp]; [enc setVertexBuffer:b_g offset:0 atIndex:0]; [enc setVertexBuffer:clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2]; [enc setFragmentBuffer:b_g offset:0 atIndex:0]; [enc setFragmentTexture:atl_pr atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_gl];

            [enc setRenderPipelineState:pb_ul]; [enc setVertexBuffer:b_u offset:0 atIndex:0]; [enc setVertexBuffer:clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2]; [enc setFragmentBuffer:b_u offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n_ul];

            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 4) gpu_b_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
        }
    }

    Stats scg = compute_stats(cpu_g_us), scb = compute_stats(cpu_b_us);
    Stats sgg = compute_stats(gpu_g_ms), sgb = compute_stats(gpu_b_ms);
    printf("  GPUI Composite  @ 4K : buf=%6.1f KB | CPU=%5.1f us | GPU=%6.3f ms (%6.1f fps)\n",
           g_bytes / 1024.0, scg.median_val, sgg.median_val, 1000.0 / sgg.median_val);
    printf("  BLOOM Composite @ 4K : buf=%6.1f KB | CPU=%5.1f us | GPU=%6.3f ms (%6.1f fps) | %.2fx faster GPU, %.1fx smaller buf\n",
           b_bytes / 1024.0, scb.median_val, sgb.median_val, 1000.0 / sgb.median_val,
           sgg.median_val / sgb.median_val, static_cast<double>(g_bytes) / static_cast<double>(b_bytes));

    std::ostringstream js;
    js << "  \"full_4k_ide_hud_composite\": {\n"
       << "    \"primitives\": { \"opaque_quads\": " << n_opq << ", \"alpha_quads\": " << n_alp
       << ", \"shadows\": " << n_sh << ", \"glyphs\": " << n_gl << ", \"underlines\": " << n_ul << " },\n"
       << "    \"gpui\": { \"buf_bytes\": " << g_bytes << ", \"cpu_us\": " << scg.median_val << ", \"gpu_ms\": " << sgg.median_val << ", \"gpu_p95_ms\": " << sgg.p95_val << " },\n"
       << "    \"bloom\": { \"buf_bytes\": " << b_bytes << ", \"cpu_us\": " << scb.median_val << ", \"gpu_ms\": " << sgb.median_val << ", \"gpu_p95_ms\": " << sgb.p95_val << " }\n"
       << "  }";
    return js.str();
}

} // namespace

int main(int argc, char **argv) {
    @autoreleasepool {
        std::string json_path;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_path = argv[++i];
        }

        Ctx ctx;
        ctx.dev = MTLCreateSystemDefaultDevice();
        if (!ctx.dev) { fprintf(stderr, "No Metal device\n"); return 1; }
        ctx.q = [ctx.dev newCommandQueue];
        NSError *err = nil;
        ctx.lib = [ctx.dev newLibraryWithSource:[NSString stringWithUTF8String:kGpuiBenchMetal]
                                        options:[[MTLCompileOptions alloc] init] error:&err];
        if (!ctx.lib || err) {
            fprintf(stderr, "MSL compile error: %s\n", err ? err.localizedDescription.UTF8String : "unknown");
            return 1;
        }

        warm_gpu_dvfs(ctx);

        std::vector<std::string> sections;
        sections.push_back(bench_glyphs(ctx));
        sections.push_back(bench_dual_pass(ctx));
        sections.push_back(bench_shadows(ctx));
        sections.push_back(bench_slug_paths(ctx));
        sections.push_back(bench_bounds_tree());
        sections.push_back(bench_full_composite(ctx));

        if (!json_path.empty()) {
            FILE *fp = std::fopen(json_path.c_str(), "w");
            if (!fp) { fprintf(stderr, "Cannot open %s\n", json_path.c_str()); return 1; }
            std::fprintf(fp, "{\n");
            for (size_t i = 0; i < sections.size(); ++i) {
                std::fprintf(fp, "%s%s\n", sections[i].c_str(), (i + 1 < sections.size()) ? "," : "");
            }
            std::fprintf(fp, "}\n");
            std::fclose(fp);
            printf("\nWrote GPUI component JSON results to %s\n", json_path.c_str());
        }
        return 0;
    }
}
