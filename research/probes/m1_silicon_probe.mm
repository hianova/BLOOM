// m1_silicon_probe.mm — First-Principles Apple M1 Metal Silicon Microbenchmark Suite
// Standalone C++17 / Objective-C++ probe (zero FLOW, zero SDL3, zero external assets).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mach/mach_time.h>
#include <sstream>
#include <string>
#include <sys/sysctl.h>
#include <time.h>
#include <vector>

namespace {

static uint64_t now_ns() {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static uint16_t f32_to_f16(float val) {
    uint32_t x;
    std::memcpy(&x, &val, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = x & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return static_cast<uint16_t>(sign);
        mant = (mant | 0x800000u) >> (1 - exp);
        return static_cast<uint16_t>(sign | ((mant + 0x1000u) >> 13));
    } else if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | ((mant + 0x1000u) >> 13));
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

static std::string sysctl_str(const char *name) {
    size_t len = 0;
    if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0) return "unknown";
    std::string buf(len, '\0');
    if (sysctlbyname(name, buf.data(), &len, nullptr, 0) != 0) return "unknown";
    while (!buf.empty() && buf.back() == '\0') buf.pop_back();
    return buf;
}

static uint64_t sysctl_u64(const char *name) {
    uint64_t val = 0;
    size_t len = sizeof(val);
    if (sysctlbyname(name, &val, &len, nullptr, 0) != 0) return 0;
    if (len == 4) {
        uint32_t v32 = 0;
        std::memcpy(&v32, &val, 4);
        return v32;
    }
    return val;
}

struct alignas(8) BloomQuadHot {
    uint16_t bounds_xywh[4];
    uint32_t rgba8_fill;
    uint32_t rgba8_border;
    uint16_t radii_border[4];
    uint16_t clip_id;
    uint16_t style_id;
    uint32_t z_flags;
};
static_assert(sizeof(BloomQuadHot) == 32, "BloomQuadHot must be 32 bytes");

struct alignas(16) BloomClipRect {
    float x, y, w, h;
};
static_assert(sizeof(BloomClipRect) == 16, "BloomClipRect must be 16 bytes");

struct RawF4 {
    float x, y, z, w;
};

struct GpuiLikeQuad {
    uint32_t order;
    uint32_t border_style;
    RawF4 bounds;
    RawF4 content_mask;
    uint32_t bg_tag;
    uint32_t bg_color_space;
    RawF4 bg_solid;
    float bg_grad_angle;
    RawF4 bg_stop0_color;
    float bg_stop0_pct;
    RawF4 bg_stop1_color;
    float bg_stop1_pct;
    uint32_t bg_extra[5];
    RawF4 border_color;
    RawF4 corner_radii;
    RawF4 border_widths;
};
static_assert(sizeof(GpuiLikeQuad) == 176, "GpuiLikeQuad must be 176 bytes");

// ============================================================================
// Embedded Metal Shading Language (MSL) Kernels & Render Shaders
// ============================================================================
static const char *kMetalSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

// ----------------------------------------------------------------------------
// 1. Occupancy / ALU Comparison Compute Kernels
// ----------------------------------------------------------------------------
kernel void k_tiny_fp16(device const half4 *in_buf [[buffer(0)]],
                        device half4 *out_buf [[buffer(1)]],
                        uint gid [[thread_position_in_grid]]) {
    half4 v = in_buf[gid];
    out_buf[gid] = fma(v, half4(1.001h), half4(0.25h));
}

kernel void k_tiny_fp32(device const float4 *in_buf [[buffer(0)]],
                        device float4 *out_buf [[buffer(1)]],
                        uint gid [[thread_position_in_grid]]) {
    float4 v = in_buf[gid];
    out_buf[gid] = fma(v, float4(1.001f), float4(0.25f));
}

inline uint2 u64_add(uint2 a, uint2 b) {
    uint lo = a.x + b.x;
    uint carry = (lo < a.x) ? 1u : 0u;
    uint hi = a.y + b.y + carry;
    return uint2(lo, hi);
}

inline uint2 u64_mul(uint2 a, uint2 b) {
    uint lo = a.x * b.x;
    uint hi_mul = mulhi(a.x, b.x);
    uint hi = hi_mul + a.x * b.y + a.y * b.x;
    return uint2(lo, hi);
}

kernel void k_emulated_u64_heavy(device const uint2 *in_buf [[buffer(0)]],
                                 device uint2 *out_buf [[buffer(1)]],
                                 constant uint &iters [[buffer(2)]],
                                 uint gid [[thread_position_in_grid]]) {
    uint2 x0 = in_buf[gid];
    uint2 x1 = uint2(x0.x ^ 0x9E3779B9u, x0.y ^ 0x7F4A7C15u);
    uint2 x2 = uint2(x0.x + 17u, x0.y + 3u);
    uint2 x3 = uint2(x0.y, x0.x);
    uint2 mul_k = uint2(0x2545F491u, 0x4F6CDD1Du);
    for (uint i = 0; i < iters; ++i) {
        x0 = u64_add(u64_mul(x0, mul_k), x1);
        x1 = u64_add(u64_mul(x1, uint2(x2.x | 1u, x2.y)), uint2(i, i ^ 0xA5A5u));
        x2 = uint2(x2.x ^ (x0.y >> 3u), x2.y ^ (x1.x << 5u));
        x3 = u64_add(x3, x2);
    }
    out_buf[gid] = u64_add(u64_add(x0, x1), u64_add(x2, x3));
}

kernel void k_high_reg_megakernel(device const float4 *in_buf [[buffer(0)]],
                                    device float4 *out_buf [[buffer(1)]],
                                    constant uint &iters [[buffer(2)]],
                                    uint gid [[thread_position_in_grid]]) {
    // 24 distinct live float4 vectors (96 live scalar VGPRs) to trigger genuine register pressure
    float4 s = in_buf[gid];
    float4 v00 = s + 0.01f, v01 = s + 0.02f, v02 = s + 0.03f, v03 = s + 0.04f;
    float4 v04 = s + 0.05f, v05 = s + 0.06f, v06 = s + 0.07f, v07 = s + 0.08f;
    float4 v08 = s + 0.09f, v09 = s + 0.10f, v10 = s + 0.11f, v11 = s + 0.12f;
    float4 v12 = s + 0.13f, v13 = s + 0.14f, v14 = s + 0.15f, v15 = s + 0.16f;
    float4 v16 = s + 0.17f, v17 = s + 0.18f, v18 = s + 0.19f, v19 = s + 0.20f;
    float4 v20 = s + 0.21f, v21 = s + 0.22f, v22 = s + 0.23f, v23 = s + 0.24f;
    for (uint i = 0; i < iters; ++i) {
        v00 = fma(v00, float4(0.999f), sin(v23)); v01 = fma(v01, float4(0.999f), cos(v00));
        v02 = fma(v02, float4(0.999f), sin(v01)); v03 = fma(v03, float4(0.999f), cos(v02));
        v04 = fma(v04, float4(0.999f), sin(v03)); v05 = fma(v05, float4(0.999f), cos(v04));
        v06 = fma(v06, float4(0.999f), sin(v05)); v07 = fma(v07, float4(0.999f), cos(v06));
        v08 = fma(v08, float4(0.999f), sin(v07)); v09 = fma(v09, float4(0.999f), cos(v08));
        v10 = fma(v10, float4(0.999f), sin(v09)); v11 = fma(v11, float4(0.999f), cos(v10));
        v12 = fma(v12, float4(0.999f), sin(v11)); v13 = fma(v13, float4(0.999f), cos(v12));
        v14 = fma(v14, float4(0.999f), sin(v13)); v15 = fma(v15, float4(0.999f), cos(v14));
        v16 = fma(v16, float4(0.999f), sin(v15)); v17 = fma(v17, float4(0.999f), cos(v16));
        v18 = fma(v18, float4(0.999f), sin(v17)); v19 = fma(v19, float4(0.999f), cos(v18));
        v20 = fma(v20, float4(0.999f), sin(v19)); v21 = fma(v21, float4(0.999f), cos(v20));
        v22 = fma(v22, float4(0.999f), sin(v21)); v23 = fma(v23, float4(0.999f), cos(v22));
    }
    out_buf[gid] = (v00+v01+v02+v03) + (v04+v05+v06+v07) + (v08+v09+v10+v11) +
                   (v12+v13+v14+v15) + (v16+v17+v18+v19) + (v20+v21+v22+v23);
}

// ----------------------------------------------------------------------------
// 2. Streaming Memory Bandwidth & SLC Cliff Kernels
// ----------------------------------------------------------------------------
struct BWParams {
    uint n_vec4;
    uint reps;
    uint stride;
    uint pad;
};

kernel void k_bw_read(device const float4 *src [[buffer(0)]],
                      device float4 *tg_sums [[buffer(1)]],
                      constant BWParams &p [[buffer(2)]],
                      uint gid [[thread_position_in_grid]],
                      uint lid [[thread_position_in_threadgroup]],
                      uint tid [[threadgroup_position_in_grid]]) {
    threadgroup float4 shared_sum[256];
    uint n = p.n_vec4;
    uint stride = p.stride;
    uint grid = min(n, 65536u);
    float4 acc0 = float4(0.0f);
    float4 acc1 = float4(0.0f);
    uint reps = p.reps;
    for (uint r = 0; r < reps; ++r) {
        uint base_off = (r * stride) % n;
        for (uint i = gid; i < n; i += grid) {
            uint idx = i + base_off;
            if (idx >= n) idx -= n;
            float4 v = src[idx];
            if ((i & 1u) == 0u) acc0 += v;
            else acc1 += v;
        }
    }
    shared_sum[lid] = acc0 + acc1;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (lid < s) shared_sum[lid] += shared_sum[lid + s];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (lid == 0u) tg_sums[tid] = shared_sum[0];
}

kernel void k_bw_write(device float4 *dst [[buffer(0)]],
                       constant BWParams &p [[buffer(1)]],
                       uint gid [[thread_position_in_grid]]) {
    uint n = p.n_vec4;
    uint stride = p.stride;
    uint grid = min(n, 65536u);
    uint reps = p.reps;
    for (uint r = 0; r < reps; ++r) {
        uint base_off = (r * stride) % n;
        float4 val = float4(float(r & 255u) * 0.01f, float(gid & 255u) * 0.001f, 0.5f, 1.0f);
        for (uint i = gid; i < n; i += grid) {
            uint idx = i + base_off;
            if (idx >= n) idx -= n;
            dst[idx] = val;
        }
    }
}

kernel void k_bw_copy(device const float4 *src [[buffer(0)]],
                      device float4 *dst [[buffer(1)]],
                      constant BWParams &p [[buffer(2)]],
                      uint gid [[thread_position_in_grid]]) {
    uint n = p.n_vec4;
    uint stride = p.stride;
    uint grid = min(n, 65536u);
    uint reps = p.reps;
    for (uint r = 0; r < reps; ++r) {
        uint base_off = (r * stride) % n;
        float add_k = float(r & 15u) * 0.0001f;
        for (uint i = gid; i < n; i += grid) {
            uint idx = i + base_off;
            if (idx >= n) idx -= n;
            dst[idx] = src[idx] + float4(add_k);
        }
    }
}

// ----------------------------------------------------------------------------
// 3. ALU Precision Benchmarks (Register-Resident FMA Chains)
// ----------------------------------------------------------------------------
kernel void k_alu_fp16(device const half4 *in_buf [[buffer(0)]],
                       device half4 *out_buf [[buffer(1)]],
                       constant uint &iters [[buffer(2)]],
                       uint gid [[thread_position_in_grid]]) {
    half4 v = in_buf[gid & 1023u];
    half4 a0 = v + half4(0.1h), a1 = v + half4(0.2h), a2 = v + half4(0.3h), a3 = v + half4(0.4h);
    half4 a4 = v + half4(0.5h), a5 = v + half4(0.6h), a6 = v + half4(0.7h), a7 = v + half4(0.8h);
    half4 k = half4(0.9995h);
    half4 c = half4(0.0001h);
    for (uint i = 0; i < iters; ++i) {
        a0 = fma(a0, k, c);
        a1 = fma(a1, k, c);
        a2 = fma(a2, k, c);
        a3 = fma(a3, k, c);
        a4 = fma(a4, k, c);
        a5 = fma(a5, k, c);
        a6 = fma(a6, k, c);
        a7 = fma(a7, k, c);
    }
    out_buf[gid & 1023u] = (a0 + a1) + (a2 + a3) + (a4 + a5) + (a6 + a7);
}

kernel void k_alu_fp32(device const float4 *in_buf [[buffer(0)]],
                       device float4 *out_buf [[buffer(1)]],
                       constant uint &iters [[buffer(2)]],
                       uint gid [[thread_position_in_grid]]) {
    float4 v = in_buf[gid & 1023u];
    float4 a0 = v + float4(0.1f), a1 = v + float4(0.2f), a2 = v + float4(0.3f), a3 = v + float4(0.4f);
    float4 a4 = v + float4(0.5f), a5 = v + float4(0.6f), a6 = v + float4(0.7f), a7 = v + float4(0.8f);
    float4 k = float4(0.99995f);
    float4 c = float4(0.00001f);
    for (uint i = 0; i < iters; ++i) {
        a0 = fma(a0, k, c);
        a1 = fma(a1, k, c);
        a2 = fma(a2, k, c);
        a3 = fma(a3, k, c);
        a4 = fma(a4, k, c);
        a5 = fma(a5, k, c);
        a6 = fma(a6, k, c);
        a7 = fma(a7, k, c);
    }
    out_buf[gid & 1023u] = (a0 + a1) + (a2 + a3) + (a4 + a5) + (a6 + a7);
}

kernel void k_alu_u32(device const uint4 *in_buf [[buffer(0)]],
                      device uint4 *out_buf [[buffer(1)]],
                      constant uint &iters [[buffer(2)]],
                      uint gid [[thread_position_in_grid]]) {
    uint4 v = in_buf[gid & 1023u];
    uint4 a0 = v + 1u, a1 = v + 2u, a2 = v + 3u, a3 = v + 4u;
    uint4 a4 = v + 5u, a5 = v + 6u, a6 = v + 7u, a7 = v + 8u;
    uint4 k = uint4(1664525u);
    uint4 c = uint4(1013904223u);
    for (uint i = 0; i < iters; ++i) {
        a0 = a0 * k + c;
        a1 = a1 * k + c;
        a2 = a2 * k + c;
        a3 = a3 * k + c;
        a4 = a4 * k + c;
        a5 = a5 * k + c;
        a6 = a6 * k + c;
        a7 = a7 * k + c;
    }
    out_buf[gid & 1023u] = (a0 ^ a1) + (a2 ^ a3) + (a4 ^ a5) + (a6 ^ a7);
}

kernel void k_alu_fp16_lowreg(device const half4 *in_buf [[buffer(0)]],
                              device half4 *out_buf [[buffer(1)]],
                              constant uint &iters [[buffer(2)]],
                              uint gid [[thread_position_in_grid]]) {
    half4 v = in_buf[gid & 1023u];
    half4 a0 = v + half4(0.1h), a1 = v + half4(0.2h), a2 = v + half4(0.3h), a3 = v + half4(0.4h);
    for (uint i = 0; i < iters; ++i) {
        a0 = fma(a0, half4(0.9995h), half4(0.0001h));
        a1 = fma(a1, half4(0.9995h), half4(0.0002h));
        a2 = fma(a2, half4(0.9995h), half4(0.0003h));
        a3 = fma(a3, half4(0.9995h), half4(0.0004h));
        a0 = fma(a0, half4(1.0004h), half4(-0.0001h));
        a1 = fma(a1, half4(1.0004h), half4(-0.0002h));
        a2 = fma(a2, half4(1.0004h), half4(-0.0003h));
        a3 = fma(a3, half4(1.0004h), half4(-0.0004h));
    }
    out_buf[gid & 1023u] = (a0 + a1) + (a2 + a3);
}

kernel void k_alu_fp32_lowreg(device const float4 *in_buf [[buffer(0)]],
                              device float4 *out_buf [[buffer(1)]],
                              constant uint &iters [[buffer(2)]],
                              uint gid [[thread_position_in_grid]]) {
    float4 v = in_buf[gid & 1023u];
    float4 a0 = v + float4(0.1f), a1 = v + float4(0.2f), a2 = v + float4(0.3f), a3 = v + float4(0.4f);
    for (uint i = 0; i < iters; ++i) {
        a0 = fma(a0, float4(0.99995f), float4(0.00001f));
        a1 = fma(a1, float4(0.99995f), float4(0.00002f));
        a2 = fma(a2, float4(0.99995f), float4(0.00003f));
        a3 = fma(a3, float4(0.99995f), float4(0.00004f));
        a0 = fma(a0, float4(1.00004f), float4(-0.00001f));
        a1 = fma(a1, float4(1.00004f), float4(-0.00002f));
        a2 = fma(a2, float4(1.00004f), float4(-0.00003f));
        a3 = fma(a3, float4(1.00004f), float4(-0.00004f));
    }
    out_buf[gid & 1023u] = (a0 + a1) + (a2 + a3);
}

kernel void k_alu_u64_emu(device const uint2 *in_buf [[buffer(0)]],
                          device uint2 *out_buf [[buffer(1)]],
                          constant uint &iters [[buffer(2)]],
                          uint gid [[thread_position_in_grid]]) {
    uint2 v = in_buf[gid & 1023u];
    uint2 a0 = v + uint2(1u, 3u), a1 = v + uint2(5u, 7u);
    uint2 a2 = v + uint2(9u, 11u), a3 = v + uint2(13u, 15u);
    uint2 k = uint2(0x9E3779B9u, 0x2545F491u);
    uint2 c = uint2(0x6C078965u, 0x1B873593u);
    for (uint i = 0; i < iters; ++i) {
        a0 = u64_add(u64_mul(a0, k), c);
        a1 = u64_add(u64_mul(a1, k), c);
        a2 = u64_add(u64_mul(a2, k), c);
        a3 = u64_add(u64_mul(a3, k), c);
    }
    out_buf[gid & 1023u] = u64_add(u64_add(a0, a1), u64_add(a2, a3));
}

// ----------------------------------------------------------------------------
// 4. TBDR vs Compute Glass Layers & Texture Swizzle Sampling
// ----------------------------------------------------------------------------
struct GlassLayer {
    float4 bounds;      // x, y, w, h in px
    float4 color;       // r, g, b, a
    float4 radii;       // corner radius, border_w, pad, pad
};

struct GlassFragIn {
    float4 pos [[position]];
    half4 color [[flat]];
    half2 local_px;
    half2 half_sz [[flat]];
    half radius [[flat]];
    half border_w [[flat]];
};

vertex GlassFragIn v_glass_tbdr(uint vid [[vertex_id]],
                                uint iid [[instance_id]],
                                device const GlassLayer *layers [[buffer(0)]],
                                constant float2 &viewport [[buffer(1)]]) {
    GlassLayer L = layers[iid];
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = L.bounds.xy + uv * L.bounds.zw;
    float2 ndc = (px / viewport) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    GlassFragIn o;
    o.pos = float4(ndc, 0.5f, 1.0f);
    o.color = half4(L.color);
    o.half_sz = half2(L.bounds.zw * 0.5f);
    o.local_px = half2(px - L.bounds.xy) - o.half_sz;
    o.radius = half(L.radii.x);
    o.border_w = half(L.radii.y);
    return o;
}

fragment half4 f_glass_tbdr(GlassFragIn in [[stage_in]]) {
    half2 d = abs(in.local_px) - in.half_sz + in.radius;
    half dist = length(max(d, half2(0.0h))) + min(max(d.x, d.y), half(0.0h)) - in.radius;
    half aa = clamp(0.5h - dist, 0.0h, 1.0h);
    half ring = clamp(1.0h - abs(dist + in.border_w * 0.5h), 0.0h, 1.0h);
    half4 col = in.color;
    col.rgb += half3(0.06h) * ring;
    col.a *= aa;
    return col;
}

kernel void k_glass_compute_hdr(texture2d<half, access::write> hdr_tex [[texture(0)]],
                                  device const GlassLayer *layers [[buffer(0)]],
                                  constant uint &layer_count [[buffer(1)]],
                                  uint2 gid [[thread_position_in_grid]]) {
    uint w = hdr_tex.get_width();
    uint h = hdr_tex.get_height();
    if (gid.x >= w || gid.y >= h) return;
    float2 p = float2(gid) + 0.5f;
    float4 accum = float4(0.05f, 0.07f, 0.11f, 1.0f);
    for (uint i = 0; i < layer_count; ++i) {
        GlassLayer L = layers[i];
        float2 hs = L.bounds.zw * 0.5f;
        float2 c2p = p - L.bounds.xy - hs;
        float2 d = abs(c2p) - hs + L.radii.x;
        float dist = length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f) - L.radii.x;
        float aa = clamp(0.5f - dist, 0.0f, 1.0f);
        float a = L.color.a * aa;
        accum.rgb = L.color.rgb * a + accum.rgb * (1.0f - a);
    }
    hdr_tex.write(half4(accum), gid);
}

kernel void k_tonemap_to_bgra(texture2d<half, access::read> hdr_tex [[texture(0)]],
                              texture2d<half, access::write> out_tex [[texture(1)]],
                              uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= out_tex.get_width() || gid.y >= out_tex.get_height()) return;
    half4 hdr = hdr_tex.read(gid);
    half3 mapped = hdr.rgb / (half3(1.0h) + hdr.rgb * half3(0.15h));
    out_tex.write(half4(mapped, 1.0h), gid);
}

struct TexSampOut {
    float4 pos [[position]];
    float2 uv;
};

vertex TexSampOut v_fullscreen_samp(uint vid [[vertex_id]],
                                    constant uint &rotated [[buffer(0)]]) {
    float2 pos = float2(float(vid & 1u) * 2.0f - 1.0f, float((vid >> 1u) & 1u) * -2.0f + 1.0f);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    if (rotated != 0u) {
        // Transpose/rotate UVs by 63 deg to probe DRAM page/tile swizzle penalty
        uv = float2(uv.x * 0.45399f - uv.y * 0.89101f + 0.5f,
                    uv.x * 0.89101f + uv.y * 0.45399f);
    }
    TexSampOut o;
    o.pos = float4(pos, 0.0f, 1.0f);
    o.uv = uv;
    return o;
}

fragment half4 f_tex_samp_4tap(TexSampOut in [[stage_in]],
                               texture2d<half, access::sample> atlas [[texture(0)]]) {
    constexpr sampler s(coord::normalized, filter::linear, address::repeat);
    half4 c0 = atlas.sample(s, in.uv);
    half4 c1 = atlas.sample(s, in.uv + float2(0.0013f, 0.0007f));
    half4 c2 = atlas.sample(s, in.uv + float2(-0.0009f, 0.0017f));
    half4 c3 = atlas.sample(s, in.uv + float2(0.0021f, -0.0011f));
    return (c0 + c1 + c2 + c3) * 0.25h;
}

// ----------------------------------------------------------------------------
// 5. Struct Layout Comparison: BloomQuadHot (32 B) vs GpuiLikeQuad (176 B)
// ----------------------------------------------------------------------------
struct BloomQuadHot {
    half4 bounds_xywh;
    uint rgba8_fill;
    uint rgba8_border;
    half4 radii_border;
    ushort clip_id;
    ushort style_id;
    uint z_flags;
};

struct BloomClipRect {
    float4 rect;
};

struct BloomFragIn {
    float4 pos [[position]];
    half4 fill [[flat]];
    half4 border [[flat]];
    half2 local_px;
    half2 half_sz [[flat]];
    half radius [[flat]];
    half border_w [[flat]];
};

vertex BloomFragIn v_bloom_quad32(uint vid [[vertex_id]],
                                  uint iid [[instance_id]],
                                  device const BloomQuadHot *quads [[buffer(0)]],
                                  device const BloomClipRect *clips [[buffer(1)]],
                                  constant float2 &viewport [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(q.bounds_xywh);
    float2 b0 = max(b.xy, c.rect.xy);
    float2 b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    float2 ndc = (px / viewport) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    float z = clamp(float(q.z_flags & 0xFFFFu) / 65535.0f, 0.0f, 1.0f);
    BloomFragIn o;
    o.pos = float4(ndc, z, 1.0f);
    o.fill = unpack_unorm4x8_to_half(q.rgba8_fill);
    o.border = unpack_unorm4x8_to_half(q.rgba8_border);
    o.half_sz = half2(b.zw * 0.5f);
    o.local_px = half2(px - b.xy) - o.half_sz;
    o.radius = q.radii_border.x;
    o.border_w = q.radii_border.z;
    return o;
}

fragment half4 f_bloom_quad32(BloomFragIn in [[stage_in]]) {
    half2 d = abs(in.local_px) - in.half_sz + in.radius;
    half dist = length(max(d, half2(0.0h))) + min(max(d.x, d.y), half(0.0h)) - in.radius;
    half alpha = clamp(0.5h - dist, 0.0h, 1.0h);
    half inner = clamp(0.5h - (dist + in.border_w), 0.0h, 1.0h);
    half4 col = mix(in.border, in.fill, inner);
    col.a *= alpha;
    return col;
}

struct BloomF32FragIn {
    float4 pos [[position]];
    float4 fill [[flat]];
    float4 border [[flat]];
    float4 origin_half [[flat]]; // xy=origin, zw=half_sz
    float2 radii_bw [[flat]];    // x=radius, y=border_w
};

vertex BloomF32FragIn v_bloom_quad32_f32pos(uint vid [[vertex_id]],
                                            uint iid [[instance_id]],
                                            device const BloomQuadHot *quads [[buffer(0)]],
                                            device const BloomClipRect *clips [[buffer(1)]],
                                            constant float2 &viewport [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(q.bounds_xywh);
    float2 b0 = max(b.xy, c.rect.xy);
    float2 b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    float2 ndc = (px / viewport) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    float z = clamp(float(q.z_flags & 0xFFFFu) / 65535.0f, 0.0f, 1.0f);
    BloomF32FragIn o;
    o.pos = float4(ndc, z, 1.0f);
    o.fill = float4(unpack_unorm4x8_to_half(q.rgba8_fill));
    o.border = float4(unpack_unorm4x8_to_half(q.rgba8_border));
    o.origin_half = float4(b.xy, b.zw * 0.5f);
    o.radii_bw = float2(float(q.radii_border.x), float(q.radii_border.z));
    return o;
}

fragment float4 f_bloom_quad32_f32pos(BloomF32FragIn in [[stage_in]]) {
    float2 half_size = in.origin_half.zw;
    float2 center_to_point = (in.pos.xy - in.origin_half.xy) - half_size;
    float radius = in.radii_bw.x;
    float bw = in.radii_bw.y;
    float2 d = abs(center_to_point) - half_size + radius;
    float outer_sdf = length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f) - radius;
    float inner_sdf = outer_sdf + bw;
    float alpha = clamp(0.5f - outer_sdf, 0.0f, 1.0f);
    float inner = clamp(0.5f - inner_sdf, 0.0f, 1.0f);
    float4 col = mix(in.border, in.fill, inner);
    col.a *= alpha;
    return col;
}

struct BloomRefetchIn {
    float4 pos [[position]];
    uint quad_id [[flat]];
};

vertex BloomRefetchIn v_bloom_quad32_refetch(uint vid [[vertex_id]],
                                             uint iid [[instance_id]],
                                             device const BloomQuadHot *quads [[buffer(0)]],
                                             device const BloomClipRect *clips [[buffer(1)]],
                                             constant float2 &viewport [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(q.bounds_xywh);
    float2 b0 = max(b.xy, c.rect.xy);
    float2 b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    float2 ndc = (px / viewport) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    BloomRefetchIn o;
    o.pos = float4(ndc, clamp(float(q.z_flags & 0xFFFFu) / 65535.0f, 0.0f, 1.0f), 1.0f);
    o.quad_id = iid;
    return o;
}

fragment half4 f_bloom_quad32_refetch(BloomRefetchIn in [[stage_in]],
                                        device const BloomQuadHot *quads [[buffer(0)]]) {
    BloomQuadHot q = quads[in.quad_id];
    half2 b_xy = q.bounds_xywh.xy;
    half2 half_size = q.bounds_xywh.zw * 0.5h;
    half2 center_to_point = (half2(in.pos.xy) - b_xy) - half_size;
    half radius = q.radii_border.x;
    half bw = q.radii_border.z;
    half2 d = abs(center_to_point) - half_size + radius;
    half outer_sdf = length(max(d, 0.0h)) + min(max(d.x, d.y), 0.0h) - radius;
    half inner_sdf = outer_sdf + bw;
    half alpha = clamp(0.5h - outer_sdf, 0.0h, 1.0h);
    half inner = clamp(0.5h - inner_sdf, 0.0h, 1.0h);
    half4 fill = unpack_unorm4x8_to_half(q.rgba8_fill);
    half4 border = unpack_unorm4x8_to_half(q.rgba8_border);
    half4 col = mix(border, fill, inner);
    col.a *= alpha;
    return col;
}

struct RawF4 {
    float x, y, z, w;
};

struct GpuiLikeQuad {
    uint order;
    uint border_style;
    RawF4 bounds;
    RawF4 content_mask;
    uint bg_tag;
    uint bg_color_space;
    RawF4 bg_solid;
    float bg_grad_angle;
    RawF4 bg_stop0_color;
    float bg_stop0_pct;
    RawF4 bg_stop1_color;
    float bg_stop1_pct;
    uint bg_extra[5];
    RawF4 border_color;
    RawF4 corner_radii;
    RawF4 border_widths;
};

struct GpuiFragIn {
    uint quad_id [[flat]];
    float4 pos [[position]];
    float4 bg_color [[flat]];
    float4 border_color [[flat]];
};

vertex GpuiFragIn v_gpui_quad176(uint vid [[vertex_id]],
                                 uint iid [[instance_id]],
                                 device const GpuiLikeQuad *quads [[buffer(0)]],
                                 constant float2 &viewport [[buffer(1)]]) {
    GpuiLikeQuad q = quads[iid];
    float2 b_xy = float2(q.bounds.x, q.bounds.y);
    float2 b_wh = float2(q.bounds.z, q.bounds.w);
    float2 m_xy = float2(q.content_mask.x, q.content_mask.y);
    float2 m_wh = float2(q.content_mask.z, q.content_mask.w);
    float2 p0 = max(b_xy, m_xy);
    float2 p1 = min(b_xy + b_wh, m_xy + m_wh);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(p0, p1, uv);
    float2 ndc = (px / viewport) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    float z = clamp(float(q.order + 1u) / 65535.0f, 0.0f, 1.0f);
    GpuiFragIn o;
    o.quad_id = iid;
    o.pos = float4(ndc, z, 1.0f);
    o.bg_color = float4(q.bg_solid.x, q.bg_solid.y, q.bg_solid.z, q.bg_solid.w);
    o.border_color = float4(q.border_color.x, q.border_color.y, q.border_color.z, q.border_color.w);
    return o;
}

fragment float4 f_gpui_quad176(GpuiFragIn in [[stage_in]],
                               device const GpuiLikeQuad *quads [[buffer(0)]]) {
    // Re-fetch monolithic quad in fragment shader like GPUI's quad_fragment
    GpuiLikeQuad q = quads[in.quad_id];
    float2 size = float2(q.bounds.z, q.bounds.w);
    float2 half_size = size * 0.5f;
    float2 point = in.pos.xy - float2(q.bounds.x, q.bounds.y);
    float2 center_to_point = point - half_size;
    float r_top = (center_to_point.x < 0.0f) ? q.corner_radii.x : q.corner_radii.y;
    float r_bot = (center_to_point.x < 0.0f) ? q.corner_radii.w : q.corner_radii.z;
    float corner_radius = (center_to_point.y < 0.0f) ? r_top : r_bot;
    float2 border = float2(
        center_to_point.x < 0.0f ? q.border_widths.w : q.border_widths.y,
        center_to_point.y < 0.0f ? q.border_widths.x : q.border_widths.z
    );
    float2 corner_to_point = abs(center_to_point) - half_size;
    float2 corner_center_to_point = corner_to_point + corner_radius;
    float outer_sdf = length(max(corner_center_to_point, 0.0f)) + min(max(corner_center_to_point.x, corner_center_to_point.y), 0.0f) - corner_radius;
    float2 inner_d = abs(center_to_point) - max(half_size - border, 0.0f) + max(corner_radius - border.x, 0.0f);
    float inner_sdf = length(max(inner_d, 0.0f)) + min(max(inner_d.x, inner_d.y), 0.0f) - max(corner_radius - border.x, 0.0f);
    float outer_a = clamp(0.5f - outer_sdf, 0.0f, 1.0f);
    float inner_a = clamp(0.5f - inner_sdf, 0.0f, 1.0f);
    float4 col = mix(in.border_color, in.bg_color, inner_a);
    col.a *= outer_a;
    return col;
}

// ----------------------------------------------------------------------------
// 6. Early-Z Depth Rejection & Compute -> Render SLC Handoff
// ----------------------------------------------------------------------------
struct OpaqueLayer {
    float4 bounds;
    float4 color;
    float z;
    float pad0, pad1, pad2;
};

struct OpaqueFragIn {
    float4 pos [[position]];
    half4 color [[flat]];
    float2 uv;
};

vertex OpaqueFragIn v_opaque_overdraw(uint vid [[vertex_id]],
                                      uint iid [[instance_id]],
                                      device const OpaqueLayer *layers [[buffer(0)]],
                                      constant float2 &viewport [[buffer(1)]]) {
    OpaqueLayer L = layers[iid];
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = L.bounds.xy + uv * L.bounds.zw;
    float2 ndc = (px / viewport) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    OpaqueFragIn o;
    o.pos = float4(ndc, L.z, 1.0f);
    o.color = half4(L.color);
    o.uv = uv;
    return o;
}

fragment half4 f_opaque_cheap(OpaqueFragIn in [[stage_in]]) {
    return half4(in.color.rgb + half3(half(in.uv.x) * 0.05h), 1.0h);
}

fragment half4 f_opaque_alu_heavy(OpaqueFragIn in [[stage_in]]) {
    half2 p = half2(in.uv) * 6.0h - 3.0h;
    half acc = 0.0h;
    for (int i = 0; i < 16; ++i) {
        p = abs(p) / max(dot(p, p), 0.15h) - half2(0.85h, 0.72h);
        acc += sin(p.x) * cos(p.y);
    }
    return half4(in.color.rgb * (0.7h + 0.02h * acc), 1.0h);
}

fragment half4 f_opaque_discard(OpaqueFragIn in [[stage_in]]) {
    half2 p = half2(in.uv) * 6.0h - 3.0h;
    half acc = 0.0h;
    for (int i = 0; i < 16; ++i) {
        p = abs(p) / max(dot(p, p), 0.15h) - half2(0.85h, 0.72h);
        acc += sin(p.x) * cos(p.y);
    }
    if (acc > 999.0h) discard_fragment();
    return half4(in.color.rgb * (0.7h + 0.02h * acc), 1.0h);
}

vertex BloomFragIn v_bloom_handoff_strided(uint vid [[vertex_id]],
                                           uint iid [[instance_id]],
                                           device const BloomQuadHot *quads [[buffer(0)]],
                                           device const BloomClipRect *clips [[buffer(1)]],
                                           constant float2 &viewport [[buffer(2)]],
                                           constant uint &stride_elems [[buffer(3)]]) {
    uint base = iid * stride_elems;
    BloomQuadHot q = quads[base];
    half accum_x = 0.0h;
    for (uint s = 1u; s < stride_elems; ++s) {
        accum_x += quads[base + s].bounds_xywh.x * 0.0001h;
    }
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(q.bounds_xywh);
    b.x += float(accum_x) * 0.001f;
    float2 b0 = max(b.xy, c.rect.xy);
    float2 b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 uv = float2(float(vid & 1u), float((vid >> 1u) & 1u));
    float2 px = mix(b0, b1, uv);
    float2 ndc = (px / viewport) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    BloomFragIn o;
    o.pos = float4(ndc, clamp(float(q.z_flags & 0xFFFFu) / 65535.0f, 0.0f, 1.0f), 1.0f);
    o.fill = unpack_unorm4x8_to_half(q.rgba8_fill);
    o.border = unpack_unorm4x8_to_half(q.rgba8_border);
    o.half_sz = half2(b.zw * 0.5f);
    o.local_px = half2(px - b.xy) - o.half_sz;
    o.radius = q.radii_border.x;
    o.border_w = q.radii_border.z;
    return o;
}

kernel void k_transform_quads_32b(device const BloomQuadHot *src [[buffer(0)]],
                                  device BloomQuadHot *dst [[buffer(1)]],
                                  constant float &phase [[buffer(2)]],
                                  uint gid [[thread_position_in_grid]]) {
    BloomQuadHot q = src[gid];
    half dx = half(sin(phase + float(gid & 63u) * 0.1f) * 2.0f);
    q.bounds_xywh.x += dx;
    dst[gid] = q;
}
)MSL";

// ============================================================================
// JSON Output Helpers
// ============================================================================
struct JsonBuf {
    std::ostringstream ss;
    void raw(const std::string &s) { ss << s; }
};

struct ProbeContext {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLLibrary> lib = nil;
};

static id<MTLComputePipelineState> make_compute_pso(ProbeContext &ctx, const char *name) {
    NSError *err = nil;
    id<MTLFunction> fn = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:name]];
    if (!fn) {
        fprintf(stderr, "FATAL: missing Metal compute function '%s'\n", name);
        exit(1);
    }
    id<MTLComputePipelineState> pso = [ctx.device newComputePipelineStateWithFunction:fn error:&err];
    if (!pso || err) {
        fprintf(stderr, "FATAL: failed creating compute PSO '%s': %s\n",
                name, err ? err.localizedDescription.UTF8String : "unknown");
        exit(1);
    }
    return pso;
}

static id<MTLRenderPipelineState> make_render_pso(ProbeContext &ctx,
                                                  const char *vname,
                                                  const char *fname,
                                                  MTLPixelFormat color_fmt,
                                                  MTLPixelFormat depth_fmt,
                                                  bool blend) {
    NSError *err = nil;
    id<MTLFunction> vf = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:vname]];
    id<MTLFunction> ff = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:fname]];
    if (!vf || !ff) {
        fprintf(stderr, "FATAL: missing Metal render function '%s' or '%s'\n", vname, fname);
        exit(1);
    }
    MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vf;
    desc.fragmentFunction = ff;
    desc.colorAttachments[0].pixelFormat = color_fmt;
    if (blend) {
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    if (depth_fmt != MTLPixelFormatInvalid) {
        desc.depthAttachmentPixelFormat = depth_fmt;
    }
    id<MTLRenderPipelineState> pso = [ctx.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!pso || err) {
        fprintf(stderr, "FATAL: failed creating render PSO '%s'/'%s': %s\n",
                vname, fname, err ? err.localizedDescription.UTF8String : "unknown");
        exit(1);
    }
    return pso;
}

static id<MTLTexture> make_tex2d(id<MTLDevice> dev,
                                 uint32_t w,
                                 uint32_t h,
                                 MTLPixelFormat fmt,
                                 MTLTextureUsage usage,
                                 MTLStorageMode mode,
                                 bool opt_contents) {
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    td.usage = usage;
    td.storageMode = mode;
    td.allowGPUOptimizedContents = opt_contents ? YES : NO;
    id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
    if (!tex) {
        fprintf(stderr, "FATAL: failed allocating %ux%u texture (mode=%lu)\n",
                w, h, (unsigned long)mode);
        exit(1);
    }
    return tex;
}

// ============================================================================
// Benchmark 1: Hardware Capabilities & PSO Occupancy Limits
// ============================================================================
static std::string run_caps(ProbeContext &ctx) {
    printf("\n=== [1/7] caps: Apple M1 Hardware & PSO Occupancy Limits ===\n");
    std::string cpu_brand = sysctl_str("machdep.cpu.brand_string");
    uint64_t mem_bytes = sysctl_u64("hw.memsize");
    uint64_t page_size = sysctl_u64("hw.pagesize");
    uint64_t cacheline = sysctl_u64("hw.cachelinesize");
    uint64_t pcores = sysctl_u64("hw.perflevel0.physicalcpu");
    uint64_t ecores = sysctl_u64("hw.perflevel1.physicalcpu");

    bool fam_apple7 = [ctx.device supportsFamily:MTLGPUFamilyApple7];
    bool fam_apple8 = [ctx.device supportsFamily:MTLGPUFamilyApple8];
    bool fam_metal3 = [ctx.device supportsFamily:MTLGPUFamilyMetal3];
    bool fam_metal4 = false;
#if defined(__MAC_26_0)
    if (@available(macOS 26.0, *)) {
        fam_metal4 = [ctx.device supportsFamily:MTLGPUFamilyMetal4];
    }
#endif

    id<MTLComputePipelineState> p_fp16 = make_compute_pso(ctx, "k_tiny_fp16");
    id<MTLComputePipelineState> p_fp32 = make_compute_pso(ctx, "k_tiny_fp32");
    id<MTLComputePipelineState> p_u64  = make_compute_pso(ctx, "k_emulated_u64_heavy");
    id<MTLComputePipelineState> p_mega = make_compute_pso(ctx, "k_high_reg_megakernel");

    printf("  GPU Device : %s (UMA=%s)\n", ctx.device.name.UTF8String, ctx.device.hasUnifiedMemory ? "YES" : "NO");
    printf("  CPU / RAM  : %s (%lluP+%lluE), %.2f GB UMA, Page=%llu B, CacheLine=%llu B\n",
           cpu_brand.c_str(), pcores, ecores,
           static_cast<double>(mem_bytes) / (1024.0 * 1024.0 * 1024.0), page_size, cacheline);
    printf("  Limits     : WorkingSet=%.1f MB, MaxBuf=%.1f MB, MaxTG=(%lu,%lu,%lu), TGMem=%lu KB\n",
           static_cast<double>(ctx.device.recommendedMaxWorkingSetSize) / (1024.0 * 1024.0),
           static_cast<double>(ctx.device.maxBufferLength) / (1024.0 * 1024.0),
           (unsigned long)ctx.device.maxThreadsPerThreadgroup.width,
           (unsigned long)ctx.device.maxThreadsPerThreadgroup.height,
           (unsigned long)ctx.device.maxThreadsPerThreadgroup.depth,
           (unsigned long)(ctx.device.maxThreadgroupMemoryLength / 1024));
    printf("  Families   : Apple7=%s Apple8=%s Metal3=%s Metal4=%s\n",
           fam_apple7 ? "YES" : "NO", fam_apple8 ? "YES" : "NO",
           fam_metal3 ? "YES" : "NO", fam_metal4 ? "YES" : "NO");
    printf("  PSO Occup  : tiny_fp16=%lu (simd=%lu) | tiny_fp32=%lu | emu_u64=%lu | megakernel=%lu\n",
           (unsigned long)p_fp16.maxTotalThreadsPerThreadgroup,
           (unsigned long)p_fp16.threadExecutionWidth,
           (unsigned long)p_fp32.maxTotalThreadsPerThreadgroup,
           (unsigned long)p_u64.maxTotalThreadsPerThreadgroup,
           (unsigned long)p_mega.maxTotalThreadsPerThreadgroup);

    std::ostringstream js;
    js << "  \"caps\": {\n"
       << "    \"gpu_name\": \"" << ctx.device.name.UTF8String << "\",\n"
       << "    \"has_unified_memory\": " << (ctx.device.hasUnifiedMemory ? "true" : "false") << ",\n"
       << "    \"cpu_brand\": \"" << cpu_brand << "\",\n"
       << "    \"mem_bytes\": " << mem_bytes << ",\n"
       << "    \"page_size_bytes\": " << page_size << ",\n"
       << "    \"cache_line_bytes\": " << cacheline << ",\n"
       << "    \"p_cores\": " << pcores << ",\n"
       << "    \"e_cores\": " << ecores << ",\n"
       << "    \"recommended_max_working_set_bytes\": " << (unsigned long long)ctx.device.recommendedMaxWorkingSetSize << ",\n"
       << "    \"max_buffer_length_bytes\": " << (unsigned long long)ctx.device.maxBufferLength << ",\n"
       << "    \"max_tg_width\": " << (unsigned long)ctx.device.maxThreadsPerThreadgroup.width << ",\n"
       << "    \"max_tg_memory_bytes\": " << (unsigned long)ctx.device.maxThreadgroupMemoryLength << ",\n"
       << "    \"families\": { \"apple7\": " << (fam_apple7 ? "true" : "false")
       << ", \"apple8\": " << (fam_apple8 ? "true" : "false")
       << ", \"metal3\": " << (fam_metal3 ? "true" : "false")
       << ", \"metal4\": " << (fam_metal4 ? "true" : "false") << " },\n"
       << "    \"pso_occupancy\": {\n"
       << "      \"tiny_fp16\": { \"max_threads\": " << (unsigned long)p_fp16.maxTotalThreadsPerThreadgroup << ", \"simd_width\": " << (unsigned long)p_fp16.threadExecutionWidth << " },\n"
       << "      \"tiny_fp32\": { \"max_threads\": " << (unsigned long)p_fp32.maxTotalThreadsPerThreadgroup << ", \"simd_width\": " << (unsigned long)p_fp32.threadExecutionWidth << " },\n"
       << "      \"emulated_u64_heavy\": { \"max_threads\": " << (unsigned long)p_u64.maxTotalThreadsPerThreadgroup << ", \"simd_width\": " << (unsigned long)p_u64.threadExecutionWidth << " },\n"
       << "      \"high_reg_megakernel\": { \"max_threads\": " << (unsigned long)p_mega.maxTotalThreadsPerThreadgroup << ", \"simd_width\": " << (unsigned long)p_mega.threadExecutionWidth << " }\n"
       << "    }\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Benchmark 2: GPU Streaming Bandwidth & 8 MB SLC Cliff
// ============================================================================
struct BWParamsHost {
    uint32_t n_vec4;
    uint32_t reps;
    uint32_t stride;
    uint32_t pad;
};

static std::string run_bandwidth_slc(ProbeContext &ctx) {
    printf("\n=== [2/7] bandwidth_slc: GPU Streaming Bandwidth & 8 MB SLC Cliff ===\n");
    id<MTLComputePipelineState> p_read = make_compute_pso(ctx, "k_bw_read");
    id<MTLComputePipelineState> p_write = make_compute_pso(ctx, "k_bw_write");
    id<MTLComputePipelineState> p_copy = make_compute_pso(ctx, "k_bw_copy");

    // Pre-boost M1 GPU DVFS P-state (~150ms sustained work) so first mode isn't penalized by idle clock ramp
    {
        id<MTLComputePipelineState> p_warm = make_compute_pso(ctx, "k_alu_fp32_lowreg");
        id<MTLBuffer> wb0 = [ctx.device newBufferWithLength:16384 options:MTLResourceStorageModeShared];
        id<MTLBuffer> wb1 = [ctx.device newBufferWithLength:16384 options:MTLResourceStorageModeShared];
        uint32_t warm_iters = 4096u;
        for (int w = 0; w < 6; ++w) {
            id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
            [enc setComputePipelineState:p_warm];
            [enc setBuffer:wb0 offset:0 atIndex:0];
            [enc setBuffer:wb1 offset:0 atIndex:1];
            [enc setBytes:&warm_iters length:sizeof(warm_iters) atIndex:2];
            [enc dispatchThreadgroups:MTLSizeMake(1024, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
        }
    }

    const uint64_t sizes_kb[] = {128, 512, 1024, 2048, 4096, 6144, 8192, 12288, 16384, 32768, 65536};
    const size_t n_sizes = sizeof(sizes_kb) / sizeof(sizes_kb[0]);

    struct ModeSpec {
        const char *name;
        MTLResourceOptions opts;
    };
    const ModeSpec modes[] = {
        {"shared_default", MTLResourceStorageModeShared},
        {"private", MTLResourceStorageModePrivate},
        {"shared_wc", MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined}
    };

    printf("  %-14s %-9s | %9s %9s %9s | %7s\n",
           "Mode", "WorkSet", "Read GB/s", "Write GB/s", "Copy GB/s", "Copy_ms");
    printf("  ---------------+-----------+-------------------------------+--------\n");

    std::ostringstream js;
    js << "  \"bandwidth_slc\": [\n";
    bool first_row = true;

    for (size_t m = 0; m < 3; ++m) {
        for (size_t si = 0; si < n_sizes; ++si) {
            @autoreleasepool {
                uint64_t size_bytes = sizes_kb[si] * 1024ULL;
                uint32_t n_vec4 = static_cast<uint32_t>(size_bytes / 16ULL);
                // Keep total traffic >= 64 MB per command buffer so GPU duration >> dispatch overhead
                uint32_t target_mb = 64;
                uint32_t reps = static_cast<uint32_t>(std::max<uint64_t>(2, (target_mb * 1024ULL) / sizes_kb[si]));
                uint32_t grid = std::min<uint32_t>(n_vec4, 65536u);
                uint32_t tg_size = 256u;
                uint32_t num_tg = (grid + tg_size - 1u) / tg_size;
                uint32_t stride = std::min<uint32_t>(256u, std::max<uint32_t>(1u, n_vec4 / 8u));

                id<MTLBuffer> bufA = [ctx.device newBufferWithLength:size_bytes options:modes[m].opts];
                id<MTLBuffer> bufB = [ctx.device newBufferWithLength:size_bytes options:modes[m].opts];
                id<MTLBuffer> tgSums = [ctx.device newBufferWithLength:num_tg * 16ULL options:MTLResourceStorageModeShared];

                // Seed source buffer via blit from tiny shared staging or direct write kernel
                BWParamsHost wp{n_vec4, 1, stride, 0};
                {
                    id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                    [enc setComputePipelineState:p_write];
                    [enc setBuffer:bufA offset:0 atIndex:0];
                    [enc setBytes:&wp length:sizeof(wp) atIndex:1];
                    [enc dispatchThreadgroups:MTLSizeMake(num_tg, 1, 1)
                        threadsPerThreadgroup:MTLSizeMake(tg_size, 1, 1)];
                    [enc endEncoding];
                    [cmd commit];
                    [cmd waitUntilCompleted];
                }

                BWParamsHost bp{n_vec4, reps, stride, 0};

                auto bench_kernel = [&](int kind) -> Stats {
                    std::vector<double> gpu_ms;
                    const int warmup = 3;
                    const int iters = 21;
                    gpu_ms.reserve(iters);
                    for (int it = 0; it < warmup + iters; ++it) {
                        id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                        if (kind == 0) {
                            [enc setComputePipelineState:p_read];
                            [enc setBuffer:bufA offset:0 atIndex:0];
                            [enc setBuffer:tgSums offset:0 atIndex:1];
                            [enc setBytes:&bp length:sizeof(bp) atIndex:2];
                        } else if (kind == 1) {
                            [enc setComputePipelineState:p_write];
                            [enc setBuffer:bufB offset:0 atIndex:0];
                            [enc setBytes:&bp length:sizeof(bp) atIndex:1];
                        } else {
                            [enc setComputePipelineState:p_copy];
                            [enc setBuffer:bufA offset:0 atIndex:0];
                            [enc setBuffer:bufB offset:0 atIndex:1];
                            [enc setBytes:&bp length:sizeof(bp) atIndex:2];
                        }
                        [enc dispatchThreadgroups:MTLSizeMake(num_tg, 1, 1)
                            threadsPerThreadgroup:MTLSizeMake(tg_size, 1, 1)];
                        [enc endEncoding];
                        [cmd commit];
                        [cmd waitUntilCompleted];
                        double dt = (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
                        if (it >= warmup) gpu_ms.push_back(dt > 0.0 ? dt : 0.0001);
                    }
                    return compute_stats(gpu_ms);
                };

                Stats s_read = bench_kernel(0);
                // Verify read sink is non-zero
                const float *sums_ptr = static_cast<const float *>(tgSums.contents);
                volatile float chk = sums_ptr[0];
                if (chk == -999999.0f) printf("sink");

                Stats s_write = bench_kernel(1);
                // Copy reads + writes: 2x buffer size per rep. Note: total GPU footprint for copy is 2 * size_bytes!
                Stats s_copy = bench_kernel(2);

                double bytes_rw = static_cast<double>(size_bytes) * static_cast<double>(reps);
                double read_gbs = (bytes_rw / (s_read.median_val * 1e-3)) / 1e9;
                double write_gbs = (bytes_rw / (s_write.median_val * 1e-3)) / 1e9;
                double copy_gbs = ((bytes_rw * 2.0) / (s_copy.median_val * 1e-3)) / 1e9;

                printf("  %-14s %6llu KB | %9.2f %9.2f %9.2f | %7.3f\n",
                       modes[m].name, (unsigned long long)sizes_kb[si],
                       read_gbs, write_gbs, copy_gbs, s_copy.median_val);

                if (!first_row) js << ",\n";
                first_row = false;
                js << "    { \"mode\": \"" << modes[m].name << "\", \"size_kb\": " << sizes_kb[si]
                   << ", \"reps\": " << reps
                   << ", \"read_ms_med\": " << s_read.median_val
                   << ", \"write_ms_med\": " << s_write.median_val
                   << ", \"copy_ms_med\": " << s_copy.median_val
                   << ", \"read_gbs\": " << read_gbs
                   << ", \"write_gbs\": " << write_gbs
                   << ", \"copy_gbs\": " << copy_gbs << " }";
            }
        }
    }
    js << "\n  ]";
    return js.str();
}

// ============================================================================
// Benchmark 3: CPU-GPU Synchronization, WriteCombined & Zero-Copy NoCopy
// ============================================================================
static std::string run_cpu_gpu_sync(ProbeContext &ctx) {
    printf("\n=== [3/7] cpu_gpu_sync: Submit Latency, Triple-Buffer, WriteCombined & NoCopy ===\n");
    id<MTLComputePipelineState> p_fp32 = make_compute_pso(ctx, "k_tiny_fp32");
    id<MTLComputePipelineState> p_bw_read = make_compute_pso(ctx, "k_bw_read");

    // 3A. Empty command buffer vs trivial 1-TG dispatch roundtrip
    auto measure_cb = [&](bool with_noop_dispatch) -> std::pair<Stats, Stats> {
        id<MTLBuffer> a = [ctx.device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        id<MTLBuffer> b = [ctx.device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        std::vector<double> wall_us, gpu_us;
        const int warmup = 10, iters = 51;
        for (int i = 0; i < warmup + iters; ++i) {
            uint64_t t0 = now_ns();
            id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
            if (with_noop_dispatch) {
                id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                [enc setComputePipelineState:p_fp32];
                [enc setBuffer:a offset:0 atIndex:0];
                [enc setBuffer:b offset:0 atIndex:1];
                [enc dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                [enc endEncoding];
            }
            [cmd commit];
            [cmd waitUntilCompleted];
            uint64_t t1 = now_ns();
            if (i >= warmup) {
                wall_us.push_back(static_cast<double>(t1 - t0) / 1e3);
                double g = (cmd.GPUEndTime - cmd.GPUStartTime) * 1e6;
                gpu_us.push_back(g > 0.0 ? g : 0.0);
            }
        }
        return {compute_stats(wall_us), compute_stats(gpu_us)};
    };

    auto [empty_wall, empty_gpu] = measure_cb(false);
    auto [noop_wall, noop_gpu] = measure_cb(true);
    printf("  Empty CB roundtrip : wall med=%.1f us (p95=%.1f us) | GPU=%.1f us\n",
           empty_wall.median_val, empty_wall.p95_val, empty_gpu.median_val);
    printf("  1-TG  CB roundtrip : wall med=%.1f us (p95=%.1f us) | GPU=%.1f us\n",
           noop_wall.median_val, noop_wall.p95_val, noop_gpu.median_val);

    // 3B. Serial Sync vs Triple-Buffered Async (60 frames of moderate ~0.3ms compute work)
    const uint32_t ring_n = 16384u; // 256 KB
    id<MTLBuffer> srcRing[3], dstRing[3];
    for (int k = 0; k < 3; ++k) {
        srcRing[k] = [ctx.device newBufferWithLength:ring_n * 16ULL
                                            options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        dstRing[k] = [ctx.device newBufferWithLength:64 * 16ULL options:MTLResourceStorageModeShared];
    }
    BWParamsHost p_sync{ring_n, 48, 256, 0}; // 256KB * 48 = 12 MB SLC work -> ~0.2-0.3ms

    auto run_frames = [&](bool async_triple) -> std::pair<double, double> {
        const int frames = 60;
        dispatch_semaphore_t sem = dispatch_semaphore_create(async_triple ? 3 : 1);
        // Warmup 3
        for (int w = 0; w < 3; ++w) {
            id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
            [enc setComputePipelineState:p_bw_read];
            [enc setBuffer:srcRing[w % 3] offset:0 atIndex:0];
            [enc setBuffer:dstRing[w % 3] offset:0 atIndex:1];
            [enc setBytes:&p_sync length:sizeof(p_sync) atIndex:2];
            [enc dispatchThreadgroups:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
        }
        uint64_t submit_ns_sum = 0;
        uint64_t t_all0 = now_ns();
        id<MTLCommandBuffer> last_cmd = nil;
        for (int f = 0; f < frames; ++f) {
            if (async_triple) dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
            uint64_t ts0 = now_ns();
            int slot = f % 3;
            uint32_t *ptr = static_cast<uint32_t *>(srcRing[slot].contents);
            ptr[0] = static_cast<uint32_t>(f);
            id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
            [enc setComputePipelineState:p_bw_read];
            [enc setBuffer:srcRing[slot] offset:0 atIndex:0];
            [enc setBuffer:dstRing[slot] offset:0 atIndex:1];
            [enc setBytes:&p_sync length:sizeof(p_sync) atIndex:2];
            [enc dispatchThreadgroups:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [enc endEncoding];
            if (async_triple) {
                [cmd addCompletedHandler:^(id<MTLCommandBuffer> _cb) {
                    (void)_cb;
                    dispatch_semaphore_signal(sem);
                }];
            }
            [cmd commit];
            if (!async_triple) [cmd waitUntilCompleted];
            uint64_t ts1 = now_ns();
            submit_ns_sum += (ts1 - ts0);
            last_cmd = cmd;
        }
        if (async_triple && last_cmd) [last_cmd waitUntilCompleted];
        uint64_t t_all1 = now_ns();
        double per_frame_wall_us = static_cast<double>(t_all1 - t_all0) / (frames * 1e3);
        double cpu_encode_us = static_cast<double>(submit_ns_sum) / (frames * 1e3);
        return {per_frame_wall_us, cpu_encode_us};
    };

    auto [serial_frame_us, serial_cpu_us] = run_frames(false);
    auto [triple_frame_us, triple_cpu_us] = run_frames(true);
    printf("  Sync Serial (60f)  : %.1f us/frame (%.1f fps)\n",
           serial_frame_us, 1e6 / serial_frame_us);
    printf("  Triple-Buf (60f)   : %.1f us/frame (%.1f fps) | CPU encode+commit=%.1f us/frame\n",
           triple_frame_us, 1e6 / triple_frame_us, triple_cpu_us);

    // 3C. CPU Bandwidth on 16 MB Shared DefaultCache vs WriteCombined
    const size_t cpu_buf_bytes = 16ULL * 1024ULL * 1024ULL;
    const size_t n_u64 = cpu_buf_bytes / sizeof(uint64_t);
    id<MTLBuffer> buf_dc = [ctx.device newBufferWithLength:cpu_buf_bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> buf_wc = [ctx.device newBufferWithLength:cpu_buf_bytes
                                                   options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    std::vector<uint64_t> staging(n_u64, 0x123456789ABCDEF0ULL);
    volatile uint64_t sink = 0;

    auto bench_cpu_mem = [&](id<MTLBuffer> buf, const char *label) {
        uint64_t *p = static_cast<uint64_t *>(buf.contents);
        // Warmup
        std::memcpy(p, staging.data(), cpu_buf_bytes);

        std::vector<double> seq_w, seq_r, rand_w;
        for (int it = 0; it < 7; ++it) {
            uint64_t t0 = now_ns();
            std::memcpy(p, staging.data(), cpu_buf_bytes);
            uint64_t t1 = now_ns();
            seq_w.push_back((static_cast<double>(cpu_buf_bytes) / (static_cast<double>(t1 - t0) * 1e-9)) / 1e9);

            uint64_t t2 = now_ns();
            uint64_t acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;
            for (size_t i = 0; i < n_u64; i += 4) {
                acc0 += p[i];
                acc1 += p[i + 1];
                acc2 += p[i + 2];
                acc3 += p[i + 3];
            }
            uint64_t t3 = now_ns();
            sink ^= (acc0 + acc1 + acc2 + acc3);
            seq_r.push_back((static_cast<double>(cpu_buf_bytes) / (static_cast<double>(t3 - t2) * 1e-9)) / 1e9);

            // Random 8-byte stores across 16 MB (524,288 scattered writes -> 4 MB touched bytes / 33 MB cachelines)
            const size_t rand_ops = 524288;
            const size_t mask = n_u64 - 1;
            uint64_t t4 = now_ns();
            for (size_t i = 0; i < rand_ops; ++i) {
                size_t idx = (i * 2654435761ULL + 1013904223ULL) & mask;
                p[idx] = i;
            }
            uint64_t t5 = now_ns();
            rand_w.push_back(static_cast<double>(t5 - t4) / 1e3); // microseconds for 512K scattered 8B stores
        }
        Stats sw = compute_stats(seq_w), sr = compute_stats(seq_r), rw = compute_stats(rand_w);
        printf("  CPU %-15s: SeqWrite=%.2f GB/s | SeqRead=%.2f GB/s | 512K-RandWrite=%.1f us\n",
               label, sw.median_val, sr.median_val, rw.median_val);
        return std::make_tuple(sw.median_val, sr.median_val, rw.median_val);
    };

    auto [dc_sw, dc_sr, dc_rw] = bench_cpu_mem(buf_dc, "Shared_Default");
    auto [wc_sw, wc_sr, wc_rw] = bench_cpu_mem(buf_wc, "Shared_WC");

    // 3D. 16 KB page-aligned NoCopy vs Persistent WC Ring memcpy + GPU read
    const size_t pay_sizes[] = {16384, 65536, 1048576};
    std::ostringstream no_copy_js;
    no_copy_js << "[\n";
    for (size_t pi = 0; pi < 3; ++pi) {
        size_t sz = pay_sizes[pi];
        void *aligned_mem = nullptr;
        if (posix_memalign(&aligned_mem, 16384, sz) != 0) {
            fprintf(stderr, "posix_memalign failed\n");
            exit(1);
        }
        std::memset(aligned_mem, 0x3F, sz);
        std::vector<uint8_t> src_stage(sz, 0x3F);
        id<MTLBuffer> ring_wc = [ctx.device newBufferWithLength:sz
                                                       options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        id<MTLBuffer> tg_out = [ctx.device newBufferWithLength:64 * 16ULL options:MTLResourceStorageModeShared];
        BWParamsHost pbw{static_cast<uint32_t>(sz / 16ULL), 1, 1, 0};

        std::vector<double> nocopy_wrap_us, nocopy_total_us, wc_memcpy_us, wc_total_us;
        for (int it = 0; it < 25; ++it) {
            // Path 1: Page-aligned newBufferWithBytesNoCopy
            uint64_t t0 = now_ns();
            id<MTLBuffer> nc_buf = [ctx.device newBufferWithBytesNoCopy:aligned_mem
                                                                 length:sz
                                                                options:MTLResourceStorageModeShared
                                                            deallocator:nil];
            uint64_t t1 = now_ns();
            id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
            [enc setComputePipelineState:p_bw_read];
            [enc setBuffer:nc_buf offset:0 atIndex:0];
            [enc setBuffer:tg_out offset:0 atIndex:1];
            [enc setBytes:&pbw length:sizeof(pbw) atIndex:2];
            [enc dispatchThreadgroups:MTLSizeMake(16, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
            uint64_t t2 = now_ns();
            if (it >= 4) {
                nocopy_wrap_us.push_back(static_cast<double>(t1 - t0) / 1e3);
                nocopy_total_us.push_back(static_cast<double>(t2 - t0) / 1e3);
            }

            // Path 2: memcpy into persistent WC ring + GPU read
            uint64_t m0 = now_ns();
            std::memcpy(ring_wc.contents, src_stage.data(), sz);
            uint64_t m1 = now_ns();
            id<MTLCommandBuffer> cmd2 = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc2 = [cmd2 computeCommandEncoder];
            [enc2 setComputePipelineState:p_bw_read];
            [enc2 setBuffer:ring_wc offset:0 atIndex:0];
            [enc2 setBuffer:tg_out offset:0 atIndex:1];
            [enc2 setBytes:&pbw length:sizeof(pbw) atIndex:2];
            [enc2 dispatchThreadgroups:MTLSizeMake(16, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [enc2 endEncoding];
            [cmd2 commit];
            [cmd2 waitUntilCompleted];
            uint64_t m2 = now_ns();
            if (it >= 4) {
                wc_memcpy_us.push_back(static_cast<double>(m1 - m0) / 1e3);
                wc_total_us.push_back(static_cast<double>(m2 - m0) / 1e3);
            }
        }
        free(aligned_mem);
        Stats ncw = compute_stats(nocopy_wrap_us), nct = compute_stats(nocopy_total_us);
        Stats wcm = compute_stats(wc_memcpy_us), wct = compute_stats(wc_total_us);
        printf("  Payload %4zu KB    : NoCopy(wrap=%.2fus, e2e=%.1fus) vs WC_Ring(memcpy=%.2fus, e2e=%.1fus)\n",
               sz / 1024, ncw.median_val, nct.median_val, wcm.median_val, wct.median_val);
        if (pi > 0) no_copy_js << ",\n";
        no_copy_js << "      { \"bytes\": " << sz
                   << ", \"nocopy_wrap_us\": " << ncw.median_val
                   << ", \"nocopy_e2e_us\": " << nct.median_val
                   << ", \"wc_memcpy_us\": " << wcm.median_val
                   << ", \"wc_e2e_us\": " << wct.median_val << " }";
    }
    no_copy_js << "\n    ]";
    if (sink == 0xFFFFFFFFFFFFFFULL) printf("sink");

    std::ostringstream js;
    js << "  \"cpu_gpu_sync\": {\n"
       << "    \"empty_cb_wall_med_us\": " << empty_wall.median_val << ",\n"
       << "    \"empty_cb_wall_p95_us\": " << empty_wall.p95_val << ",\n"
       << "    \"noop_1tg_wall_med_us\": " << noop_wall.median_val << ",\n"
       << "    \"noop_1tg_gpu_med_us\": " << noop_gpu.median_val << ",\n"
       << "    \"serial_60f_us_per_frame\": " << serial_frame_us << ",\n"
       << "    \"triple_buf_60f_us_per_frame\": " << triple_frame_us << ",\n"
       << "    \"triple_buf_cpu_encode_us\": " << triple_cpu_us << ",\n"
       << "    \"cpu_16mb_shared_default\": { \"seq_write_gbs\": " << dc_sw << ", \"seq_read_gbs\": " << dc_sr << ", \"rand_write_512k_us\": " << dc_rw << " },\n"
       << "    \"cpu_16mb_shared_wc\": { \"seq_write_gbs\": " << wc_sw << ", \"seq_read_gbs\": " << wc_sr << ", \"rand_write_512k_us\": " << wc_rw << " },\n"
       << "    \"upload_16kb_nocopy_vs_wc\": " << no_copy_js.str() << "\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Benchmark 4: ALU Precision & Scalar/Integer Throughput
// ============================================================================
static std::string run_alu_precision(ProbeContext &ctx) {
    printf("\n=== [4/7] alu_precision: fp16 vs fp32 vs u32 vs Emulated u64 ===\n");
    const uint32_t threads = 262144u; // 1024 threadgroups x 256
    const uint32_t tg_size = 256u;
    const uint32_t num_tg = threads / tg_size;

    id<MTLBuffer> in_b = [ctx.device newBufferWithLength:1024 * 16ULL options:MTLResourceStorageModeShared];
    id<MTLBuffer> out_b = [ctx.device newBufferWithLength:1024 * 16ULL options:MTLResourceStorageModeShared];
    uint32_t *ip = static_cast<uint32_t *>(in_b.contents);
    for (uint32_t i = 0; i < 1024 * 4; ++i) ip[i] = 0x3F000000u + (i & 255u);

    struct AluSpec {
        const char *name;
        const char *fn;
        uint32_t iters;
        double ops_per_iter_per_thread; // FLOPs for floats (FMA=2), ops for u32/u64
        const char *unit;
    };
    const AluSpec specs[] = {
        {"fp16_half4_fma", "k_alu_fp16", 1024u, 8.0 * 4.0 * 2.0, "GFLOPS"},
        {"fp16_half4_lowreg", "k_alu_fp16_lowreg", 1024u, 8.0 * 4.0 * 2.0, "GFLOPS"},
        {"fp32_float4_fma", "k_alu_fp32", 1024u, 8.0 * 4.0 * 2.0, "GFLOPS"},
        {"fp32_float4_lowreg", "k_alu_fp32_lowreg", 1024u, 8.0 * 4.0 * 2.0, "GFLOPS"},
        {"u32_uint4_mad", "k_alu_u32", 1024u, 8.0 * 4.0 * 2.0, "GOPS"},
        {"u64_emulated_uint2", "k_alu_u64_emu", 512u, 4.0 * 2.0, "G_u64_OPS"}
    };

    std::ostringstream js;
    js << "  \"alu_precision\": [\n";
    for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); ++i) {
        id<MTLComputePipelineState> pso = make_compute_pso(ctx, specs[i].fn);
        std::vector<double> gpu_ms;
        for (int it = 0; it < 23; ++it) {
            id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
            [enc setComputePipelineState:pso];
            [enc setBuffer:in_b offset:0 atIndex:0];
            [enc setBuffer:out_b offset:0 atIndex:1];
            [enc setBytes:&specs[i].iters length:sizeof(uint32_t) atIndex:2];
            [enc dispatchThreadgroups:MTLSizeMake(num_tg, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg_size, 1, 1)];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
            double dt = (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
            if (it >= 3) gpu_ms.push_back(dt > 0.0 ? dt : 0.001);
        }
        Stats st = compute_stats(gpu_ms);
        double total_ops = static_cast<double>(threads) * static_cast<double>(specs[i].iters) * specs[i].ops_per_iter_per_thread;
        double rate = (total_ops / (st.median_val * 1e-3)) / 1e9;
        printf("  %-20s : med=%7.3f ms | %8.1f %-9s | PSO maxTG=%lu\n",
               specs[i].name, st.median_val, rate, specs[i].unit,
               (unsigned long)pso.maxTotalThreadsPerThreadgroup);
        if (i > 0) js << ",\n";
        js << "    { \"name\": \"" << specs[i].name << "\", \"gpu_ms_med\": " << st.median_val
           << ", \"rate\": " << rate << ", \"unit\": \"" << specs[i].unit
           << "\", \"max_tg_threads\": " << (unsigned long)pso.maxTotalThreadsPerThreadgroup << " }";
    }
    js << "\n  ]";
    return js.str();
}

// ============================================================================
// Benchmark 5: TBDR RenderPass vs Compute HDR+Blit & Private vs Shared Texture
// ============================================================================
struct GlassLayerHost {
    float bounds[4];
    float color[4];
    float radii[4];
};

static std::string run_tbdr_vs_compute(ProbeContext &ctx) {
    printf("\n=== [5/7] tbdr_vs_compute: 16x Glass Layers (1080p/4K) & Texture Storage ===\n");
    id<MTLRenderPipelineState> r_glass = make_render_pso(ctx, "v_glass_tbdr", "f_glass_tbdr", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, true);
    id<MTLComputePipelineState> c_glass = make_compute_pso(ctx, "k_glass_compute_hdr");
    id<MTLComputePipelineState> c_tone = make_compute_pso(ctx, "k_tonemap_to_bgra");
    id<MTLRenderPipelineState> r_tex = make_render_pso(ctx, "v_fullscreen_samp", "f_tex_samp_4tap", MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid, false);

    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_always = [ctx.device newDepthStencilStateWithDescriptor:dsd];

    const uint32_t layer_cnt = 16u;
    id<MTLBuffer> layer_buf = [ctx.device newBufferWithLength:sizeof(GlassLayerHost) * layer_cnt options:MTLResourceStorageModeShared];

    struct Res { uint32_t w, h; const char *label; };
    const Res res_list[] = {{1920, 1080, "1080p"}, {3840, 2160, "4K"}};

    std::ostringstream js;
    js << "  \"tbdr_vs_compute\": {\n    \"glass_16_layers\": [\n";

    for (size_t ri = 0; ri < 2; ++ri) {
        @autoreleasepool {
            uint32_t W = res_list[ri].w, H = res_list[ri].h;
            GlassLayerHost *lp = static_cast<GlassLayerHost *>(layer_buf.contents);
            for (uint32_t i = 0; i < layer_cnt; ++i) {
                float inset = static_cast<float>(i) * 4.0f;
                lp[i].bounds[0] = inset;
                lp[i].bounds[1] = inset;
                lp[i].bounds[2] = static_cast<float>(W) - inset * 2.0f;
                lp[i].bounds[3] = static_cast<float>(H) - inset * 2.0f;
                lp[i].color[0] = 0.12f + 0.04f * static_cast<float>(i & 3u);
                lp[i].color[1] = 0.25f + 0.03f * static_cast<float>(i & 7u);
                lp[i].color[2] = 0.45f;
                lp[i].color[3] = 0.18f;
                lp[i].radii[0] = 24.0f;
                lp[i].radii[1] = 2.0f;
                lp[i].radii[2] = 0.0f;
                lp[i].radii[3] = 0.0f;
            }
            float vp[2] = {static_cast<float>(W), static_cast<float>(H)};

            id<MTLTexture> rt_bgra = make_tex2d(ctx.device, W, H, MTLPixelFormatBGRA8Unorm,
                                                MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite,
                                                MTLStorageModePrivate, true);
            id<MTLTexture> rt_depth_ml = make_tex2d(ctx.device, W, H, MTLPixelFormatDepth32Float,
                                                    MTLTextureUsageRenderTarget,
                                                    MTLStorageModeMemoryless, true);
            id<MTLTexture> hdr_16f = make_tex2d(ctx.device, W, H, MTLPixelFormatRGBA16Float,
                                                MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite,
                                                MTLStorageModePrivate, true);

            // Path A: TBDR RenderPass (single pass, Memoryless Depth, hardware blend)
            std::vector<double> tbdr_ms, comp_ms;
            for (int it = 0; it < 24; ++it) {
                id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                rpd.colorAttachments[0].texture = rt_bgra;
                rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.05, 0.07, 0.11, 1.0);
                rpd.depthAttachment.texture = rt_depth_ml;
                rpd.depthAttachment.loadAction = MTLLoadActionClear;
                rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                rpd.depthAttachment.clearDepth = 1.0;

                id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
                [enc setRenderPipelineState:r_glass];
                [enc setDepthStencilState:ds_always];
                [enc setVertexBuffer:layer_buf offset:0 atIndex:0];
                [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:layer_cnt];
                [enc endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
                if (it >= 4) tbdr_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            }

            // Path B: Compute HDR RGBA16F + Tonemap/Write BGRA8
            for (int it = 0; it < 24; ++it) {
                id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                MTLSize tg = MTLSizeMake(16, 16, 1);
                MTLSize grid = MTLSizeMake(W, H, 1);
                [enc setComputePipelineState:c_glass];
                [enc setTexture:hdr_16f atIndex:0];
                [enc setBuffer:layer_buf offset:0 atIndex:0];
                [enc setBytes:&layer_cnt length:sizeof(layer_cnt) atIndex:1];
                [enc dispatchThreads:grid threadsPerThreadgroup:tg];

                [enc setComputePipelineState:c_tone];
                [enc setTexture:hdr_16f atIndex:0];
                [enc setTexture:rt_bgra atIndex:1];
                [enc dispatchThreads:grid threadsPerThreadgroup:tg];
                [enc endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
                if (it >= 4) comp_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            }

            Stats st_t = compute_stats(tbdr_ms), st_c = compute_stats(comp_ms);
            printf("  %-5s 16x Glass   : TBDR RenderPass = %6.3f ms (%6.1f fps) | Compute HDR+Copy = %6.3f ms (%6.1f fps) | %.2fx\n",
                   res_list[ri].label, st_t.median_val, 1000.0 / st_t.median_val,
                   st_c.median_val, 1000.0 / st_c.median_val, st_c.median_val / st_t.median_val);

            if (ri > 0) js << ",\n";
            js << "      { \"res\": \"" << res_list[ri].label << "\", \"width\": " << W << ", \"height\": " << H
               << ", \"tbdr_ms_med\": " << st_t.median_val
               << ", \"tbdr_ms_p95\": " << st_t.p95_val
               << ", \"compute_hdr_ms_med\": " << st_c.median_val
               << ", \"compute_hdr_ms_p95\": " << st_c.p95_val << " }";
        }
    }
    // Also benchmark 256 scattered 120x32 UI widgets at 4K: TBDR O(covered px) vs Compute O(W*H*N)
    {
        @autoreleasepool {
            const uint32_t W = 3840, H = 2160, widget_cnt = 256u;
            id<MTLBuffer> wbuf = [ctx.device newBufferWithLength:sizeof(GlassLayerHost) * widget_cnt options:MTLResourceStorageModeShared];
            GlassLayerHost *wp = static_cast<GlassLayerHost *>(wbuf.contents);
            for (uint32_t i = 0; i < widget_cnt; ++i) {
                wp[i].bounds[0] = static_cast<float>((i * 130u) % (W - 140u));
                wp[i].bounds[1] = static_cast<float>(((i * 37u) % 50u) * 40u);
                wp[i].bounds[2] = 120.0f;
                wp[i].bounds[3] = 32.0f;
                wp[i].color[0] = 0.20f; wp[i].color[1] = 0.35f; wp[i].color[2] = 0.55f; wp[i].color[3] = 0.85f;
                wp[i].radii[0] = 6.0f; wp[i].radii[1] = 1.5f; wp[i].radii[2] = 0.0f; wp[i].radii[3] = 0.0f;
            }
            float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
            id<MTLTexture> rt_bgra = make_tex2d(ctx.device, W, H, MTLPixelFormatBGRA8Unorm,
                                                MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite,
                                                MTLStorageModePrivate, true);
            id<MTLTexture> rt_depth_ml = make_tex2d(ctx.device, W, H, MTLPixelFormatDepth32Float,
                                                    MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
            id<MTLTexture> hdr_16f = make_tex2d(ctx.device, W, H, MTLPixelFormatRGBA16Float,
                                                MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite,
                                                MTLStorageModePrivate, true);
            std::vector<double> wt_ms, wc_ms;
            for (int it = 0; it < 16; ++it) {
                id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                rpd.colorAttachments[0].texture = rt_bgra;
                rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                rpd.depthAttachment.texture = rt_depth_ml;
                rpd.depthAttachment.loadAction = MTLLoadActionClear;
                rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
                [enc setRenderPipelineState:r_glass];
                [enc setDepthStencilState:ds_always];
                [enc setVertexBuffer:wbuf offset:0 atIndex:0];
                [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:widget_cnt];
                [enc endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
                if (it >= 3) wt_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            }
            for (int it = 0; it < 8; ++it) {
                id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                MTLSize tg = MTLSizeMake(16, 16, 1);
                MTLSize grid = MTLSizeMake(W, H, 1);
                [enc setComputePipelineState:c_glass];
                [enc setTexture:hdr_16f atIndex:0];
                [enc setBuffer:wbuf offset:0 atIndex:0];
                [enc setBytes:&widget_cnt length:sizeof(widget_cnt) atIndex:1];
                [enc dispatchThreads:grid threadsPerThreadgroup:tg];
                [enc setComputePipelineState:c_tone];
                [enc setTexture:hdr_16f atIndex:0];
                [enc setTexture:rt_bgra atIndex:1];
                [enc dispatchThreads:grid threadsPerThreadgroup:tg];
                [enc endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
                if (it >= 2) wc_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            }
            Stats st_wt = compute_stats(wt_ms), st_wc = compute_stats(wc_ms);
            printf("  4K   256x Widgets : TBDR RenderPass = %6.3f ms (%6.1f fps) | Compute HDR+Copy = %6.3f ms (%6.1f fps) | TBDR %.2fx faster\n",
                   st_wt.median_val, 1000.0 / st_wt.median_val,
                   st_wc.median_val, 1000.0 / st_wc.median_val,
                   st_wc.median_val / st_wt.median_val);
            js << ",\n      { \"res\": \"4K_256_widgets_120x32\", \"width\": " << W << ", \"height\": " << H
               << ", \"tbdr_ms_med\": " << st_wt.median_val
               << ", \"tbdr_ms_p95\": " << st_wt.p95_val
               << ", \"compute_hdr_ms_med\": " << st_wc.median_val
               << ", \"compute_hdr_ms_p95\": " << st_wc.p95_val << " }";
        }
    }
    js << "\n    ],\n    \"texture_4k_samp_4096_atlas\": [\n";

    // Path C: 4096x4096 RGBA8Unorm Private (allowGPUOptimizedContents=YES) vs Shared (NO)
    @autoreleasepool {
        const uint32_t tex_dim = 4096u;
        const size_t tex_bytes = tex_dim * tex_dim * 4ULL;
        id<MTLBuffer> stage = [ctx.device newBufferWithLength:tex_bytes options:MTLResourceStorageModeShared];
        uint32_t *tp = static_cast<uint32_t *>(stage.contents);
        for (size_t i = 0; i < tex_dim * tex_dim; ++i) {
            tp[i] = 0xFF000000u | (static_cast<uint32_t>((i ^ (i >> 8)) & 0xFFu) << 16) |
                    (static_cast<uint32_t>((i >> 4) & 0xFFu) << 8) | static_cast<uint32_t>(i & 0xFFu);
        }

        id<MTLTexture> tex_priv = make_tex2d(ctx.device, tex_dim, tex_dim, MTLPixelFormatRGBA8Unorm,
                                             MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
        id<MTLTexture> tex_shared = make_tex2d(ctx.device, tex_dim, tex_dim, MTLPixelFormatRGBA8Unorm,
                                               MTLTextureUsageShaderRead, MTLStorageModeShared, false);
        id<MTLTexture> rt4k = make_tex2d(ctx.device, 3840, 2160, MTLPixelFormatBGRA8Unorm,
                                         MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
        {
            id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
            [blit copyFromBuffer:stage sourceOffset:0 sourceBytesPerRow:tex_dim * 4 sourceBytesPerImage:tex_bytes
                      sourceSize:MTLSizeMake(tex_dim, tex_dim, 1)
                       toTexture:tex_priv destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
        }
        [tex_shared replaceRegion:MTLRegionMake2D(0, 0, tex_dim, tex_dim) mipmapLevel:0 withBytes:tp bytesPerRow:tex_dim * 4];

        auto bench_tex = [&](id<MTLTexture> tex, uint32_t rotated, const char *label) -> Stats {
            std::vector<double> ms;
            for (int it = 0; it < 24; ++it) {
                id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                rpd.colorAttachments[0].texture = rt4k;
                rpd.colorAttachments[0].loadAction = MTLLoadActionDontCare;
                rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
                [enc setRenderPipelineState:r_tex];
                [enc setVertexBytes:&rotated length:sizeof(rotated) atIndex:0];
                [enc setFragmentTexture:tex atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
                [enc endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
                if (it >= 4) ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            }
            Stats st = compute_stats(ms);
            printf("  4K Tex 4-Tap %-24s : med=%6.3f ms (p95=%6.3f ms)\n", label, st.median_val, st.p95_val);
            return st;
        };

        Stats sp0 = bench_tex(tex_priv, 0, "Private+Opt (Coherent)");
        Stats ss0 = bench_tex(tex_shared, 0, "Shared+Linear (Coherent)");
        Stats sp1 = bench_tex(tex_priv, 1, "Private+Opt (Rotated)");
        Stats ss1 = bench_tex(tex_shared, 1, "Shared+Linear (Rotated)");

        js << "      { \"mode\": \"private_opt_coherent\", \"ms_med\": " << sp0.median_val << ", \"ms_p95\": " << sp0.p95_val << " },\n"
           << "      { \"mode\": \"shared_linear_coherent\", \"ms_med\": " << ss0.median_val << ", \"ms_p95\": " << ss0.p95_val << " },\n"
           << "      { \"mode\": \"private_opt_rotated\", \"ms_med\": " << sp1.median_val << ", \"ms_p95\": " << sp1.p95_val << " },\n"
           << "      { \"mode\": \"shared_linear_rotated\", \"ms_med\": " << ss1.median_val << ", \"ms_p95\": " << ss1.p95_val << " }\n";
    }
    js << "    ]\n  }";
    return js.str();
}

// ============================================================================
// Benchmark 6: Struct Layout (BloomQuadHot 32B vs GpuiLikeQuad 176B @ 4K)
// ============================================================================
static std::string run_struct_layout(ProbeContext &ctx) {
    printf("\n=== [6/7] struct_layout: BloomQuadHot (32 B Half vs F32Pos vs Refetch20B) vs GpuiLikeQuad (176 B) @ 4K ===\n");
    id<MTLRenderPipelineState> r_bloom = make_render_pso(ctx, "v_bloom_quad32", "f_bloom_quad32", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, true);
    id<MTLRenderPipelineState> r_bpos  = make_render_pso(ctx, "v_bloom_quad32_f32pos", "f_bloom_quad32_f32pos", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, true);
    id<MTLRenderPipelineState> r_bref  = make_render_pso(ctx, "v_bloom_quad32_refetch", "f_bloom_quad32_refetch", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, true);
    id<MTLRenderPipelineState> r_gpui  = make_render_pso(ctx, "v_gpui_quad176", "f_gpui_quad176", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, true);

    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_always = [ctx.device newDepthStencilStateWithDescriptor:dsd];

    const uint32_t W = 3840, H = 2160;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    id<MTLTexture> rt = make_tex2d(ctx.device, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> depth = make_tex2d(ctx.device, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);

    id<MTLBuffer> clip_buf = [ctx.device newBufferWithLength:sizeof(BloomClipRect) * 16 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    BloomClipRect *cp = static_cast<BloomClipRect *>(clip_buf.contents);
    for (int i = 0; i < 16; ++i) cp[i] = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};

    struct CaseSpec { uint32_t n; float qw; float qh; const char *label; };
    const CaseSpec cases[] = {
        {2000u, 120.0f, 32.0f, "2000_ui_120x32"},
        {20000u, 120.0f, 32.0f, "20000_ui_120x32"},
        {100000u, 120.0f, 32.0f, "100000_ui_120x32"},
        {100000u, 4.0f, 4.0f, "100000_tiny_4x4"}
    };
    std::ostringstream js;
    js << "  \"struct_layout\": [\n";

    for (size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ++ci) {
        @autoreleasepool {
            uint32_t N = cases[ci].n;
            float qw = cases[ci].qw, qh = cases[ci].qh;
            size_t b32_bytes = N * sizeof(BloomQuadHot);
            size_t b176_bytes = N * sizeof(GpuiLikeQuad);

            id<MTLBuffer> buf32 = [ctx.device newBufferWithLength:b32_bytes options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
            id<MTLBuffer> buf176 = [ctx.device newBufferWithLength:b176_bytes options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

            std::vector<double> cpu32_us, cpu176_us;
            uint16_t hw = f32_to_f16(qw), hh = f32_to_f16(qh);
            uint16_t hr = f32_to_f16(std::min(6.0f, qw * 0.25f)), hbw = f32_to_f16(1.5f), hz = f32_to_f16(0.0f);

            for (int it = 0; it < 12; ++it) {
                uint64_t t0 = now_ns();
                BloomQuadHot *q32 = static_cast<BloomQuadHot *>(buf32.contents);
                for (uint32_t i = 0; i < N; ++i) {
                    float x = static_cast<float>((i * 124u) % (W - 128u));
                    float y = static_cast<float>(((i * 37u) % 64u) * 33u);
                    BloomQuadHot item;
                    item.bounds_xywh[0] = f32_to_f16(x);
                    item.bounds_xywh[1] = f32_to_f16(y);
                    item.bounds_xywh[2] = hw;
                    item.bounds_xywh[3] = hh;
                    item.rgba8_fill = 0xE6334455u + (i & 63u);
                    item.rgba8_border = 0xFF88AACC;
                    item.radii_border[0] = hr;
                    item.radii_border[1] = hr;
                    item.radii_border[2] = hbw;
                    item.radii_border[3] = hz;
                    item.clip_id = 0;
                    item.style_id = 0;
                    item.z_flags = i & 0xFFFFu;
                    q32[i] = item;
                }
                uint64_t t1 = now_ns();
                if (it >= 2) cpu32_us.push_back(static_cast<double>(t1 - t0) / 1e3);

                uint64_t t2 = now_ns();
                GpuiLikeQuad *q176 = static_cast<GpuiLikeQuad *>(buf176.contents);
                for (uint32_t i = 0; i < N; ++i) {
                    float x = static_cast<float>((i * 124u) % (W - 128u));
                    float y = static_cast<float>(((i * 37u) % 64u) * 33u);
                    GpuiLikeQuad item{};
                    item.order = i & 0xFFFFu;
                    item.bounds = {x, y, qw, qh};
                    item.content_mask = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};
                    item.bg_solid = {0.20f, 0.27f, 0.34f, 0.90f};
                    item.border_color = {0.53f, 0.67f, 0.80f, 1.0f};
                    item.corner_radii = {6.0f, 6.0f, 6.0f, 6.0f};
                    item.border_widths = {1.5f, 1.5f, 1.5f, 1.5f};
                    q176[i] = item;
                }
                uint64_t t3 = now_ns();
                if (it >= 2) cpu176_us.push_back(static_cast<double>(t3 - t2) / 1e3);
            }

            std::vector<double> gpu32_half_ms, gpu32_fpos_ms, gpu32_ref_ms, gpu176_ms;
            for (int it = 0; it < 24; ++it) {
                auto draw_once = [&](id<MTLRenderPipelineState> pso, id<MTLBuffer> buf, bool pass_clip, bool frag_buf) -> double {
                    id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                    rpd.colorAttachments[0].texture = rt;
                    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                    rpd.depthAttachment.texture = depth;
                    rpd.depthAttachment.loadAction = MTLLoadActionClear;
                    rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                    rpd.depthAttachment.clearDepth = 1.0;

                    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
                    [enc setRenderPipelineState:pso];
                    [enc setDepthStencilState:ds_always];
                    [enc setVertexBuffer:buf offset:0 atIndex:0];
                    if (pass_clip) {
                        [enc setVertexBuffer:clip_buf offset:0 atIndex:1];
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                    } else {
                        [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
                    }
                    if (frag_buf) [enc setFragmentBuffer:buf offset:0 atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N];
                    [enc endEncoding];
                    [cmd commit];
                    [cmd waitUntilCompleted];
                    return (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
                };
                double d_half = draw_once(r_bloom, buf32, true, false);
                double d_fpos = draw_once(r_bpos, buf32, true, false);
                double d_ref  = draw_once(r_bref, buf32, true, true);
                double d_gpui = draw_once(r_gpui, buf176, false, true);
                if (it >= 4) {
                    gpu32_half_ms.push_back(d_half);
                    gpu32_fpos_ms.push_back(d_fpos);
                    gpu32_ref_ms.push_back(d_ref);
                    gpu176_ms.push_back(d_gpui);
                }
            }

            Stats sc32 = compute_stats(cpu32_us), sc176 = compute_stats(cpu176_us);
            Stats sg32h = compute_stats(gpu32_half_ms), sg32f = compute_stats(gpu32_fpos_ms), sg32r = compute_stats(gpu32_ref_ms), sg176 = compute_stats(gpu176_ms);
            printf("  %-18s | 32B(%.1fKB, CPU %.1fus): Half=%6.3fms F32Pos=%6.3fms Refetch20B=%6.3fms | 176B(%.1fKB, CPU %.1fus): GPUI=%6.3fms\n",
                   cases[ci].label, b32_bytes / 1024.0, sc32.median_val, sg32h.median_val, sg32f.median_val, sg32r.median_val,
                   b176_bytes / 1024.0, sc176.median_val, sg176.median_val);

            if (ci > 0) js << ",\n";
            js << "    { \"case\": \"" << cases[ci].label << "\", \"count\": " << N << ", \"w\": " << qw << ", \"h\": " << qh
               << ", \"bloom32\": { \"buf_bytes\": " << b32_bytes << ", \"cpu_pack_us\": " << sc32.median_val
               << ", \"gpu_half_interp_ms_med\": " << sg32h.median_val
               << ", \"gpu_f32pos_ms_med\": " << sg32f.median_val
               << ", \"gpu_refetch20b_ms_med\": " << sg32r.median_val << " }"
               << ", \"gpui176\": { \"buf_bytes\": " << b176_bytes << ", \"cpu_pack_us\": " << sc176.median_val
               << ", \"gpu_ms_med\": " << sg176.median_val << " } }";
        }
    }
    js << "\n  ]";
    return js.str();
}

// ============================================================================
// Benchmark 7: Early-Z Draw Order & Compute->Render 8 MB SLC Handoff
// ============================================================================
struct OpaqueLayerHost {
    float bounds[4];
    float color[4];
    float z;
    float pad0, pad1, pad2;
};

static std::string run_draw_order_and_slc_handoff(ProbeContext &ctx) {
    printf("\n=== [7/7] draw_order_and_slc_handoff: HSR, Early-Z & Compute->Render SLC Handoff ===\n");
    const uint32_t W = 3840, H = 2160;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    id<MTLTexture> rt = make_tex2d(ctx.device, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> depth = make_tex2d(ctx.device, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);

    id<MTLRenderPipelineState> r_heavy_opaque = make_render_pso(ctx, "v_opaque_overdraw", "f_opaque_alu_heavy", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, false);
    id<MTLRenderPipelineState> r_heavy_blend  = make_render_pso(ctx, "v_opaque_overdraw", "f_opaque_alu_heavy", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, true);
    id<MTLRenderPipelineState> r_discard      = make_render_pso(ctx, "v_opaque_overdraw", "f_opaque_discard", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, false);

    MTLDepthStencilDescriptor *ds_off_d = [[MTLDepthStencilDescriptor alloc] init];
    ds_off_d.depthCompareFunction = MTLCompareFunctionAlways;
    ds_off_d.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_off = [ctx.device newDepthStencilStateWithDescriptor:ds_off_d];

    MTLDepthStencilDescriptor *ds_ez_d = [[MTLDepthStencilDescriptor alloc] init];
    ds_ez_d.depthCompareFunction = MTLCompareFunctionLess;
    ds_ez_d.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_ez = [ctx.device newDepthStencilStateWithDescriptor:ds_ez_d];

    auto bench_overdraw = [&](uint32_t count, id<MTLRenderPipelineState> pso, bool front_to_back) -> Stats {
        id<MTLBuffer> buf = [ctx.device newBufferWithLength:sizeof(OpaqueLayerHost) * count options:MTLResourceStorageModeShared];
        OpaqueLayerHost *p = static_cast<OpaqueLayerHost *>(buf.contents);
        for (uint32_t i = 0; i < count; ++i) {
            float z = front_to_back ? (0.05f + 0.85f * (static_cast<float>(i) / static_cast<float>(count)))
                                    : (0.90f - 0.85f * (static_cast<float>(i) / static_cast<float>(count)));
            p[i].bounds[0] = 0.0f;
            p[i].bounds[1] = 0.0f;
            p[i].bounds[2] = static_cast<float>(W);
            p[i].bounds[3] = static_cast<float>(H);
            p[i].color[0] = 0.2f + 0.01f * static_cast<float>(i);
            p[i].color[1] = 0.4f;
            p[i].color[2] = 0.6f;
            p[i].color[3] = 1.0f;
            p[i].z = z;
        }
        std::vector<double> ms;
        for (int it = 0; it < 20; ++it) {
            id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt;
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = depth;
            rpd.depthAttachment.loadAction = MTLLoadActionClear;
            rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
            rpd.depthAttachment.clearDepth = 1.0;

            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            [enc setRenderPipelineState:pso];
            [enc setDepthStencilState:front_to_back ? ds_ez : ds_off];
            [enc setVertexBuffer:buf offset:0 atIndex:0];
            [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:count];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];
            if (it >= 4) ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
        }
        return compute_stats(ms);
    };

    Stats ez_1x        = bench_overdraw(1, r_heavy_opaque, false);
    Stats ez_op_btf    = bench_overdraw(16, r_heavy_opaque, false);
    Stats ez_op_ftb    = bench_overdraw(16, r_heavy_opaque, true);
    Stats ez_bl_btf    = bench_overdraw(16, r_heavy_blend, false);
    Stats ez_bl_ftb    = bench_overdraw(16, r_heavy_blend, true);
    Stats ez_disc_btf  = bench_overdraw(16, r_discard, false);
    Stats ez_disc_ftb  = bench_overdraw(16, r_discard, true);

    printf("  1x  Fullscreen Heavy (baseline)   : %6.3f ms\n", ez_1x.median_val);
    printf("  16x Opaque (blend=NO, HSR)        : Back->Front=%6.3f ms | Front->Back EarlyZ=%6.3f ms\n",
           ez_op_btf.median_val, ez_op_ftb.median_val);
    printf("  16x Blended UI (blend=YES, a=1.0) : Back->Front=%6.3f ms | Front->Back EarlyZ=%6.3f ms (%.2fx)\n",
           ez_bl_btf.median_val, ez_bl_ftb.median_val, ez_bl_btf.median_val / ez_bl_ftb.median_val);
    printf("  16x Discard Fragment (breaks HSR) : Back->Front=%6.3f ms | Front->Back EarlyZ=%6.3f ms\n",
           ez_disc_btf.median_val, ez_disc_ftb.median_val);

    // 7B: Compute -> Render SLC Handoff (constant 65,536 rendered 4x4 quads, 2 MB vs 24 MB Compute->Render traffic)
    id<MTLComputePipelineState> c_xform = make_compute_pso(ctx, "k_transform_quads_32b");
    id<MTLRenderPipelineState> r_bloom = make_render_pso(ctx, "v_bloom_handoff_strided", "f_bloom_quad32", MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float, true);
    id<MTLBuffer> clip_buf = [ctx.device newBufferWithLength:sizeof(BloomClipRect) options:MTLResourceStorageModeShared];
    *static_cast<BloomClipRect *>(clip_buf.contents) = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};

    const uint32_t render_quads = 65536u;
    const uint32_t strides[] = {1u, 12u}; // 65,536 * 32 B = 2.0 MB vs 65,536 * 12 * 32 B = 24.0 MB
    std::ostringstream handoff_js;
    handoff_js << "[\n";

    for (size_t hi = 0; hi < 2; ++hi) {
        @autoreleasepool {
            uint32_t stride_elems = strides[hi];
            uint32_t N = render_quads * stride_elems;
            size_t bytes = N * sizeof(BloomQuadHot);
            double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);

            id<MTLBuffer> src = [ctx.device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
            BloomQuadHot *sq = static_cast<BloomQuadHot *>(src.contents);
            uint16_t hw = f32_to_f16(4.0f), hh = f32_to_f16(4.0f), hr = f32_to_f16(1.0f), hbw = f32_to_f16(0.0f);
            for (uint32_t i = 0; i < N; ++i) {
                sq[i].bounds_xywh[0] = f32_to_f16(static_cast<float>(((i / stride_elems) * 5u) % (W - 8u)));
                sq[i].bounds_xywh[1] = f32_to_f16(static_cast<float>((((i / stride_elems) * 7u) % (H - 8u))));
                sq[i].bounds_xywh[2] = hw;
                sq[i].bounds_xywh[3] = hh;
                sq[i].rgba8_fill = 0xFF4499EEu;
                sq[i].rgba8_border = 0xFFFFFFFFu;
                sq[i].radii_border[0] = hr;
                sq[i].radii_border[1] = hr;
                sq[i].radii_border[2] = hbw;
                sq[i].radii_border[3] = hbw;
                sq[i].clip_id = 0;
                sq[i].style_id = 0;
                sq[i].z_flags = i & 0xFFFFu;
            }
            id<MTLBuffer> priv_src = [ctx.device newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
            id<MTLBuffer> priv_mid_tracked = [ctx.device newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
            id<MTLBuffer> priv_mid_untracked = [ctx.device newBufferWithLength:bytes options:MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeUntracked];
            {
                id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
                [blit copyFromBuffer:src sourceOffset:0 toBuffer:priv_src destinationOffset:0 size:bytes];
                [blit endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
            }
            id<MTLFence> fence = [ctx.device newFence];

            auto bench_handoff = [&](int mode) -> std::pair<Stats, Stats> {
                std::vector<double> wall_ms, gpu_ms;
                float phase = 0.25f;
                for (int it = 0; it < 23; ++it) {
                    id<MTLBuffer> mid = (mode == 1) ? priv_mid_untracked : priv_mid_tracked;
                    uint64_t t0 = now_ns();
                    if (mode == 2) {
                        id<MTLCommandBuffer> c1 = [ctx.queue commandBuffer];
                        id<MTLComputeCommandEncoder> ce = [c1 computeCommandEncoder];
                        [ce setComputePipelineState:c_xform];
                        [ce setBuffer:priv_src offset:0 atIndex:0];
                        [ce setBuffer:mid offset:0 atIndex:1];
                        [ce setBytes:&phase length:sizeof(phase) atIndex:2];
                        [ce dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                        [ce endEncoding];
                        [c1 commit];
                        [c1 waitUntilCompleted];
                        double g1 = (c1.GPUEndTime - c1.GPUStartTime) * 1000.0;

                        id<MTLCommandBuffer> c2 = [ctx.queue commandBuffer];
                        MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                        rpd.colorAttachments[0].texture = rt;
                        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                        rpd.depthAttachment.texture = depth;
                        rpd.depthAttachment.loadAction = MTLLoadActionClear;
                        rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                        id<MTLRenderCommandEncoder> re = [c2 renderCommandEncoderWithDescriptor:rpd];
                        [re setRenderPipelineState:r_bloom];
                        [re setDepthStencilState:ds_off];
                        [re setVertexBuffer:mid offset:0 atIndex:0];
                        [re setVertexBuffer:clip_buf offset:0 atIndex:1];
                        [re setVertexBytes:vp length:sizeof(vp) atIndex:2];
                        [re setVertexBytes:&stride_elems length:sizeof(stride_elems) atIndex:3];
                        [re drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:render_quads];
                        [re endEncoding];
                        [c2 commit];
                        [c2 waitUntilCompleted];
                        uint64_t t1 = now_ns();
                        double g2 = (c2.GPUEndTime - c2.GPUStartTime) * 1000.0;
                        if (it >= 3) {
                            wall_ms.push_back(static_cast<double>(t1 - t0) / 1e6);
                            gpu_ms.push_back(g1 + g2);
                        }
                    } else {
                        id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
                        id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
                        [ce setComputePipelineState:c_xform];
                        [ce setBuffer:priv_src offset:0 atIndex:0];
                        [ce setBuffer:mid offset:0 atIndex:1];
                        [ce setBytes:&phase length:sizeof(phase) atIndex:2];
                        [ce dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                        if (mode == 1) [ce updateFence:fence];
                        [ce endEncoding];

                        MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                        rpd.colorAttachments[0].texture = rt;
                        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                        rpd.depthAttachment.texture = depth;
                        rpd.depthAttachment.loadAction = MTLLoadActionClear;
                        rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                        id<MTLRenderCommandEncoder> re = [cmd renderCommandEncoderWithDescriptor:rpd];
                        if (mode == 1) [re waitForFence:fence beforeStages:MTLRenderStageVertex];
                        [re setRenderPipelineState:r_bloom];
                        [re setDepthStencilState:ds_off];
                        [re setVertexBuffer:mid offset:0 atIndex:0];
                        [re setVertexBuffer:clip_buf offset:0 atIndex:1];
                        [re setVertexBytes:vp length:sizeof(vp) atIndex:2];
                        [re setVertexBytes:&stride_elems length:sizeof(stride_elems) atIndex:3];
                        [re drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:render_quads];
                        [re endEncoding];
                        [cmd commit];
                        [cmd waitUntilCompleted];
                        uint64_t t1 = now_ns();
                        if (it >= 3) {
                            wall_ms.push_back(static_cast<double>(t1 - t0) / 1e6);
                            gpu_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
                        }
                    }
                }
                return {compute_stats(wall_ms), compute_stats(gpu_ms)};
            };

            auto [w0, g0] = bench_handoff(0);
            auto [w1, g1] = bench_handoff(1);
            auto [w2, g2] = bench_handoff(2);
            printf("  SLC Handoff %4.1f MB (65536 q, s=%2u) | SameCB_Tracked=%6.3f ms (wall %6.3f) | SameCB_Fence=%6.3f ms (wall %6.3f) | SplitCB=%6.3f ms (wall %6.3f)\n",
                   mb, stride_elems, g0.median_val, w0.median_val, g1.median_val, w1.median_val, g2.median_val, w2.median_val);

            if (hi > 0) handoff_js << ",\n";
            handoff_js << "      { \"mb\": " << mb << ", \"rendered_quads\": " << render_quads << ", \"stride\": " << stride_elems
                       << ", \"same_cb_tracked_gpu_ms\": " << g0.median_val << ", \"same_cb_tracked_wall_ms\": " << w0.median_val
                       << ", \"same_cb_fence_gpu_ms\": " << g1.median_val << ", \"same_cb_fence_wall_ms\": " << w1.median_val
                       << ", \"split_cb_gpu_ms\": " << g2.median_val << ", \"split_cb_wall_ms\": " << w2.median_val << " }";
        }
    }
    handoff_js << "\n    ]";

    std::ostringstream js;
    js << "  \"draw_order_and_slc_handoff\": {\n"
       << "    \"early_z\": [\n"
       << "      { \"case\": \"1x_fullscreen_heavy_baseline\", \"ms\": " << ez_1x.median_val << " },\n"
       << "      { \"case\": \"16x_opaque_no_blend\", \"back_to_front_ms\": " << ez_op_btf.median_val << ", \"front_to_back_ms\": " << ez_op_ftb.median_val << " },\n"
       << "      { \"case\": \"16x_blended_ui_pass\", \"back_to_front_ms\": " << ez_bl_btf.median_val << ", \"front_to_back_ms\": " << ez_bl_ftb.median_val << " },\n"
       << "      { \"case\": \"16x_discard_breaks_hsr\", \"back_to_front_ms\": " << ez_disc_btf.median_val << ", \"front_to_back_ms\": " << ez_disc_ftb.median_val << " }\n"
       << "    ],\n"
       << "    \"slc_handoff\": " << handoff_js.str() << "\n"
       << "  }";
    return js.str();
}

} // namespace

int main(int argc, char **argv) {
    @autoreleasepool {
        std::string json_path;
        std::string only;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
                json_path = argv[++i];
            } else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
                only = argv[++i];
            } else if (std::strcmp(argv[i], "--list") == 0) {
                printf("caps bandwidth_slc cpu_gpu_sync alu_precision tbdr_vs_compute struct_layout draw_order_and_slc_handoff\n");
                return 0;
            }
        }

        ProbeContext ctx;
        ctx.device = MTLCreateSystemDefaultDevice();
        if (!ctx.device) {
            fprintf(stderr, "FATAL: MTLCreateSystemDefaultDevice() returned nil\n");
            return 1;
        }
        ctx.queue = [ctx.device newCommandQueue];
        if (!ctx.queue) {
            fprintf(stderr, "FATAL: newCommandQueue() returned nil\n");
            return 1;
        }

        NSError *err = nil;
        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
        ctx.lib = [ctx.device newLibraryWithSource:[NSString stringWithUTF8String:kMetalSource]
                                           options:opts
                                             error:&err];
        if (!ctx.lib || err) {
            fprintf(stderr, "FATAL: Metal shader compile failed: %s\n",
                    err ? err.localizedDescription.UTF8String : "unknown");
            return 1;
        }

        std::vector<std::string> sections;
        auto want = [&](const char *n) { return only.empty() || only == n; };

        if (want("caps")) sections.push_back(run_caps(ctx));
        if (want("bandwidth_slc")) sections.push_back(run_bandwidth_slc(ctx));
        if (want("cpu_gpu_sync")) sections.push_back(run_cpu_gpu_sync(ctx));
        if (want("alu_precision")) sections.push_back(run_alu_precision(ctx));
        if (want("tbdr_vs_compute")) sections.push_back(run_tbdr_vs_compute(ctx));
        if (want("struct_layout")) sections.push_back(run_struct_layout(ctx));
        if (want("draw_order_and_slc_handoff")) sections.push_back(run_draw_order_and_slc_handoff(ctx));

        if (!json_path.empty()) {
            FILE *fp = std::fopen(json_path.c_str(), "w");
            if (!fp) {
                fprintf(stderr, "FATAL: cannot write %s\n", json_path.c_str());
                return 1;
            }
            std::fprintf(fp, "{\n");
            for (size_t i = 0; i < sections.size(); ++i) {
                std::fprintf(fp, "%s%s\n", sections[i].c_str(), (i + 1 < sections.size()) ? "," : "");
            }
            std::fprintf(fp, "}\n");
            std::fclose(fp);
            printf("\nWrote JSON probe results to %s\n", json_path.c_str());
        }
        return 0;
    }
}
