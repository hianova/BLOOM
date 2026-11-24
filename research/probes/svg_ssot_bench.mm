// svg_ssot_bench.mm — BLOOM vNext Tri-Layer SVG-SSOT Empirical Probe on Apple M1
// Standalone C++17 / Objective-C++ / Metal (zero FLOW, zero SDL3, zero external deps).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <array>
#include <cctype>
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

// ============================================================================
// Timing, Fixed-Point Packing & Statistics
// ============================================================================
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
    uint16_t bits = 0;
    std::memcpy(&bits, &h, sizeof(bits));
    return bits;
}

static inline uint32_t pack_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8u) |
           (static_cast<uint32_t>(b) << 16u) |
           (static_cast<uint32_t>(a) << 24u);
}

static inline uint32_t rgba_with_opacity(uint32_t rgb24_x, float opacity) {
    uint8_t r = static_cast<uint8_t>(rgb24_x & 0xFFu);
    uint8_t g = static_cast<uint8_t>((rgb24_x >> 8u) & 0xFFu);
    uint8_t b = static_cast<uint8_t>((rgb24_x >> 16u) & 0xFFu);
    float op = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    uint8_t a = static_cast<uint8_t>(op * 255.0f + 0.5f);
    return pack_rgba8(r, g, b, a);
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

static uint64_t fnv1a64(const char *s, size_t len, uint64_t seed = 14695981039346656037ULL) {
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint8_t>(s[i]);
        h *= 1099511628211ULL;
    }
    return h ? h : 1ULL;
}

static uint64_t fnv1a64_mix(uint64_t a, uint64_t b) {
    uint64_t x = a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return x ? x : 1ULL;
}

// ============================================================================
// GPU-Native Primitive Structs (Shared with gpui_component_bench / vNext)
// ============================================================================
struct alignas(4) BloomQuadHot {
    uint16_t bounds_q14_2[4]; // 8 B
    uint32_t rgba8_fill;      // 4 B
    uint32_t rgba8_border;    // 4 B
    uint8_t  radii_q6_2[4];   // 4 B
    uint8_t  borders_q6_2[4]; // 4 B
    uint16_t clip_id;         // 2 B
    uint16_t style_id;        // 2 B
    uint32_t z_flags;         // 4 B
};
static_assert(sizeof(BloomQuadHot) == 32, "BloomQuadHot must be 32 B");

struct alignas(4) BloomGlyphInst {
    uint16_t dst_q14_2[4];   // 8 B
    uint16_t uv_unorm16[4];  // 8 B
    uint32_t rgba8;          // 4 B
    uint16_t clip_id;        // 2 B
    uint16_t z_flags;        // 2 B
};
static_assert(sizeof(BloomGlyphInst) == 24, "BloomGlyphInst must be 24 B");

struct alignas(4) BloomShadowHot {
    uint16_t bounds_q14_2[4];    // 8 B
    uint16_t shape_params[4];    // 8 B
    uint16_t blur_r1_exp_sc[4];  // 8 B
    uint32_t rgba8;              // 4 B
    uint16_t clip_id;            // 2 B
    uint16_t z_flags;            // 2 B
};
static_assert(sizeof(BloomShadowHot) == 32, "BloomShadowHot must be 32 B");

struct alignas(16) BloomClipRect {
    float x, y, w, h;
};
static_assert(sizeof(BloomClipRect) == 16, "BloomClipRect must be 16 B");

// v9 Archive Reference Structures (for v9 Compute Fidelity Comparison)
struct alignas(16) V9UiNodeGpu {
    uint32_t node_id;
    uint32_t prim_type;
    uint32_t glass_style;
    uint32_t action_caps;
    float cx, cy, hw, hh;
    float rx;
    float bound_v;
    float spring_v;
    uint32_t pad0;
    uint32_t fill_rgba;
    uint32_t tile_lo;
    uint32_t tile_hi;
    uint32_t flags;
};
static_assert(sizeof(V9UiNodeGpu) == 64, "V9UiNodeGpu must be 64 B");

struct alignas(4) SlugCurveGpu {
    float p0x, p0y;
    float p1x, p1y;
    float ax, ay;
    float bx, by;
    float sign;
    uint32_t pad;
};
static_assert(sizeof(SlugCurveGpu) == 40, "SlugCurveGpu must be 40 B");

struct alignas(4) SlugTileGpu {
    uint32_t paint_idx;
    uint32_t curve_start;
    uint32_t curve_count;
    int32_t backdrop;
    float tlx, tly;
    float brx, bry;
    float refx, refy;
    float ctlx, ctly;
    float cbrx, cbry;
};
static_assert(sizeof(SlugTileGpu) == 56, "SlugTileGpu must be 56 B");

// ============================================================================
// Layer 1: Zero-Malloc Streaming W3C SVG Blueprint Engine (Reusable Arena)
// ============================================================================
enum SvgLayoutDir : uint8_t { LAYOUT_ABS = 0, LAYOUT_ROW = 1, LAYOUT_COL = 2 };
enum SvgAlignMode : uint8_t { ALIGN_START = 0, ALIGN_CENTER = 1, ALIGN_END = 2, ALIGN_STRETCH = 3 };
enum SvgJustifyMode : uint8_t { JUST_START = 0, JUST_CENTER = 1, JUST_END = 2, JUST_BETWEEN = 3 };
enum SvgNodeKind : uint8_t {
    NODE_GROUP = 0,
    NODE_RECT = 1,
    NODE_CIRCLE = 2,
    NODE_TEXT = 3,
    NODE_PATH = 4,
    NODE_CANVAS = 5
};

enum UiCapsBits : uint32_t {
    CAP_NONE        = 0u,
    CAP_FOCUSABLE   = 1u << 16,
    CAP_DISABLED    = 1u << 17,
    CAP_TOGGLE      = 1u << 18,
    CAP_CHECKED     = 1u << 19,
    CAP_AGENT_GUARD = 1u << 20,
    CAP_MODAL       = 1u << 21,
    CAP_SCRUB       = 1u << 22
};

enum CssStateMask : uint8_t {
    CSS_NONE     = 0u,
    CSS_HOVER    = 1u << 0,
    CSS_ACTIVE   = 1u << 1,
    CSS_FOCUS    = 1u << 2,
    CSS_DISABLED = 1u << 3,
    CSS_CHECKED  = 1u << 4
};

struct CssStyleRule {
    uint64_t selector_hash; // fnv1a64 of ".btn" or "#id" or "*rule"
    uint8_t  is_id;
    uint8_t  state_mask;    // bitmask of CssStateMask required
    uint8_t  has_fill;
    uint8_t  has_opacity;
    uint8_t  has_stroke;
    uint8_t  has_stroke_w;
    uint8_t  has_rx;
    uint8_t  has_shadow;
    uint32_t fill_rgb;
    float    fill_opacity;
    uint32_t stroke_rgb;
    float    stroke_w;
    float    rx;
    float    shadow_blur;
    uint32_t shadow_rgba;
};

struct SvgQuadCurveSeg {
    float p0x, p0y;
    float cx, cy;
    float p1x, p1y;
};

struct SvgBlueprintNode {
    uint64_t node_key;
    uint32_t str_id_off;
    uint16_t str_id_len;
    uint32_t label_off;
    uint16_t label_len;
    uint32_t class_off;
    uint16_t class_len;

    int16_t  parent_idx;     // -1 for root
    int16_t  first_child;    // linked sibling list for O(N) flex traversal
    int16_t  last_child;     // O(1) child append tail pointer
    int16_t  next_sibling;
    int16_t  clip_ref_idx;   // index into clips (-1 if none)
    uint16_t glyph_id;       // non-zero if vector path icon
    uint16_t prim_type;      // 1=rect/btn,2=ring,4=slider,5=latex,6=7seg,7=input
    uint16_t _pad16_top;

    uint8_t  kind;           // SvgNodeKind
    uint8_t  layout_dir;     // SvgLayoutDir
    uint8_t  align_items;    // SvgAlignMode
    uint8_t  justify;        // SvgJustifyMode
    uint8_t  scroll_y;
    uint8_t  virtual_list;
    uint8_t  has_shadow;
    uint8_t  pad_b;

    float    x, y, w, h, rx; // SVG viewBox coordinates
    float    gap, pad;
    float    flex_grow;
    float    init_val;       // [0..1] normalized
    float    fill_opacity;
    float    stroke_w;
    uint32_t fill_rgb;
    uint32_t stroke_rgb;
    float    shadow_dx, shadow_dy, shadow_blur;
    uint32_t shadow_rgba;

    uint32_t action_caps;    // low16=opcode, hi16=UiCapsBits
    int16_t  bind_channel;   // -1 if unbound
    uint16_t symbol_template_idx; // for virtual list <symbol>
    float    virtual_item_h;
    uint32_t virtual_total_count;
    uint32_t curve_start;    // slice in blueprint quad_curves
    uint16_t curve_count;
    uint16_t _pad16;
};

struct SvgSymbolDef {
    uint64_t id_hash;
    float vb_w, vb_h;
    uint16_t node_start;
    uint16_t node_count;
};

struct SvgClipDef {
    uint64_t id_hash;
    float x, y, w, h;
};

struct SvgFilterDef {
    uint64_t id_hash;
    float dx, dy, blur;
    uint32_t rgba;
};

struct KeyIndexSlot {
    uint64_t key;
    int32_t  node_idx;
};

static constexpr size_t BP_MAX_NODES   = 24576;
static constexpr size_t BP_MAX_CURVES  = 16384;
static constexpr size_t BP_MAX_RULES   = 256;
static constexpr size_t BP_MAX_SYMBOLS = 64;
static constexpr size_t BP_MAX_CLIPS   = 128;
static constexpr size_t BP_MAX_FILTERS = 64;
static constexpr size_t BP_STR_ARENA   = 1024 * 1024;
static constexpr size_t BP_KEY_MAP_CAP = 65536; // power of 2
static constexpr size_t BP_SUB_CH_MAX  = 64;
static constexpr size_t BP_SUB_PER_CH  = 64;

struct BloomSvgBlueprint {
    float vb_min_x = 0.0f, vb_min_y = 0.0f, vb_w = 1280.0f, vb_h = 720.0f;
    uint32_t node_count = 0;
    uint32_t curve_count = 0;
    uint32_t rule_count = 0;
    uint32_t symbol_count = 0;
    uint32_t clip_count = 0;
    uint32_t filter_count = 0;
    uint32_t str_used = 0;
    uint32_t epoch = 0;

    std::vector<SvgBlueprintNode> nodes;
    std::vector<SvgQuadCurveSeg>  curves;
    std::vector<CssStyleRule>     rules;
    std::vector<SvgSymbolDef>     symbols;
    std::vector<SvgClipDef>       clips;
    std::vector<SvgFilterDef>     filters;
    std::vector<char>             str_arena;
    std::vector<KeyIndexSlot>     key_map;

    // Bind channel -> node indices subscriber table for O(dirty) propagation
    uint16_t ch_sub_count[BP_SUB_CH_MAX]{};
    int16_t  ch_subs[BP_SUB_CH_MAX][BP_SUB_PER_CH]{};

    // Temporary parser stacks (zero malloc during parse)
    int16_t  grp_stack[64]{};
    float    grp_tx[64]{};
    float    grp_ty[64]{};
    int32_t  grp_depth = 0;
    bool     in_defs = false;
    bool     in_symbol = false;
    bool     in_clippath = false;
    int32_t  cur_symbol_idx = -1;
    int32_t  cur_clip_idx = -1;
    int32_t  cur_filter_idx = -1;

    void alloc_once() {
        nodes.resize(BP_MAX_NODES);
        curves.resize(BP_MAX_CURVES);
        rules.resize(BP_MAX_RULES);
        symbols.resize(BP_MAX_SYMBOLS);
        clips.resize(BP_MAX_CLIPS);
        filters.resize(BP_MAX_FILTERS);
        str_arena.resize(BP_STR_ARENA);
        key_map.resize(BP_KEY_MAP_CAP);
    }

    size_t memory_bytes() const {
        return static_cast<size_t>(node_count) * sizeof(SvgBlueprintNode) +
               static_cast<size_t>(curve_count) * sizeof(SvgQuadCurveSeg) +
               static_cast<size_t>(rule_count) * sizeof(CssStyleRule) +
               static_cast<size_t>(str_used) +
               BP_KEY_MAP_CAP * sizeof(KeyIndexSlot);
    }

    void reset() {
        vb_min_x = 0.0f; vb_min_y = 0.0f; vb_w = 1280.0f; vb_h = 720.0f;
        node_count = 0; curve_count = 0; rule_count = 0;
        symbol_count = 0; clip_count = 0; filter_count = 0;
        str_used = 0;
        grp_depth = 0;
        in_defs = false; in_symbol = false; in_clippath = false;
        cur_symbol_idx = -1; cur_clip_idx = -1; cur_filter_idx = -1;
        std::memset(key_map.data(), 0, key_map.size() * sizeof(KeyIndexSlot));
        std::memset(ch_sub_count, 0, sizeof(ch_sub_count));
    }

    int32_t find_by_key(uint64_t key) const {
        if (key == 0ULL) return -1;
        uint32_t mask = static_cast<uint32_t>(BP_KEY_MAP_CAP - 1);
        uint32_t slot = static_cast<uint32_t>(key ^ (key >> 32)) & mask;
        for (uint32_t probe = 0; probe < 128u; ++probe) {
            const KeyIndexSlot &e = key_map[(slot + probe) & mask];
            if (e.key == 0ULL) return -1;
            if (e.key == key) return e.node_idx;
        }
        return -1;
    }

    void insert_key(uint64_t key, int32_t node_idx) {
        if (key == 0ULL) return;
        uint32_t mask = static_cast<uint32_t>(BP_KEY_MAP_CAP - 1);
        uint32_t slot = static_cast<uint32_t>(key ^ (key >> 32)) & mask;
        for (uint32_t probe = 0; probe < 128u; ++probe) {
            KeyIndexSlot &e = key_map[(slot + probe) & mask];
            if (e.key == 0ULL || e.key == key) {
                e.key = key;
                e.node_idx = node_idx;
                return;
            }
        }
    }

    void push_str(const char *s, size_t len, uint32_t &out_off, uint16_t &out_len) {
        if (!s || len == 0 || str_used + len + 1 > BP_STR_ARENA) {
            out_off = 0; out_len = 0; return;
        }
        out_off = str_used;
        out_len = static_cast<uint16_t>(len > 65535u ? 65535u : len);
        std::memcpy(str_arena.data() + str_used, s, out_len);
        str_arena[str_used + out_len] = '\0';
        str_used += out_len + 1u;
    }

    const char *str_ptr(uint32_t off, uint16_t len) const {
        return (len > 0 && off < str_used) ? (str_arena.data() + off) : "";
    }
};

// --- Fast Zero-Malloc Parsing Utilities ---
static inline bool sv_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline const char *sv_skip_ws_comma(const char *p, const char *end) {
    while (p < end && (sv_space(*p) || *p == ',')) ++p;
    return p;
}

static const char *sv_parse_f32(const char *p, const char *end, float &out, bool &ok) {
    p = sv_skip_ws_comma(p, end);
    if (p >= end) { ok = false; return p; }
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    else if (*p == '+') { ++p; }
    if (p >= end || ((*p < '0' || *p > '9') && *p != '.')) { ok = false; return p; }
    double val = 0.0;
    bool has_digit = false;
    while (p < end && *p >= '0' && *p <= '9') {
        val = val * 10.0 + (*p - '0');
        has_digit = true;
        ++p;
    }
    if (p < end && *p == '.') {
        ++p;
        double scale = 0.1;
        while (p < end && *p >= '0' && *p <= '9') {
            val += (*p - '0') * scale;
            scale *= 0.1;
            has_digit = true;
            ++p;
        }
    }
    if (!has_digit) { ok = false; return p; }
    if (p + 1 < end && p[0] == 'p' && p[1] == 'x') p += 2;
    out = static_cast<float>(neg ? -val : val);
    ok = true;
    return p;
}

static const char *sv_find_attr(const char *ts, const char *te, const char *name, size_t &val_len) {
    size_t klen = std::strlen(name);
    const char *p = ts;
    while (p + klen + 2 < te) {
        char prev = (p == ts) ? ' ' : p[-1];
        if (sv_space(prev) && std::memcmp(p, name, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            if (v < te && (*v == '"' || *v == '\'')) {
                char q = *v++;
                const char *ve = static_cast<const char *>(std::memchr(v, q, static_cast<size_t>(te - v)));
                if (ve) {
                    val_len = static_cast<size_t>(ve - v);
                    return v;
                }
            }
        }
        ++p;
    }
    val_len = 0;
    return nullptr;
}

static float sv_attr_f32(const char *ts, const char *te, const char *name, float def_v) {
    size_t n = 0;
    const char *v = sv_find_attr(ts, te, name, n);
    if (!v || n == 0) return def_v;
    float out = def_v;
    bool ok = false;
    sv_parse_f32(v, v + n, out, ok);
    return ok ? out : def_v;
}

static bool sv_attr_eq(const char *ts, const char *te, const char *name, const char *lit) {
    size_t n = 0;
    const char *v = sv_find_attr(ts, te, name, n);
    return v && std::strlen(lit) == n && std::memcmp(v, lit, n) == 0;
}

static uint32_t sv_parse_hex_rgb(const char *v, size_t n, uint32_t def_rgb, bool &is_none) {
    is_none = false;
    if (!v || n == 0) return def_rgb;
    while (n > 0 && sv_space(*v)) { ++v; --n; }
    while (n > 0 && sv_space(v[n - 1])) --n;
    if (n == 4 && std::memcmp(v, "none", 4) == 0) { is_none = true; return 0u; }
    if (v[0] == '#') { ++v; --n; }
    if (n == 3) {
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };
        int r = hex(v[0]), g = hex(v[1]), b = hex(v[2]);
        if (r >= 0 && g >= 0 && b >= 0) {
            return static_cast<uint32_t>((r * 17) | ((g * 17) << 8) | ((b * 17) << 16));
        }
    }
    if (n >= 6) {
        uint32_t rgb24 = 0;
        for (int i = 0; i < 6; ++i) {
            char c = v[i];
            uint32_t nib = 0;
            if (c >= '0' && c <= '9') nib = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') nib = static_cast<uint32_t>(10 + (c - 'a'));
            else if (c >= 'A' && c <= 'F') nib = static_cast<uint32_t>(10 + (c - 'A'));
            else return def_rgb;
            rgb24 = (rgb24 << 4u) | nib;
        }
        uint32_t r = (rgb24 >> 16) & 0xFFu;
        uint32_t g = (rgb24 >> 8) & 0xFFu;
        uint32_t b = rgb24 & 0xFFu;
        return r | (g << 8) | (b << 16);
    }
    return def_rgb;
}

static void sv_flatten_cubic_to_quad(BloomSvgBlueprint &bp,
                                     float x0, float y0,
                                     float x1, float y1,
                                     float x2, float y2,
                                     float x3, float y3,
                                     int depth) {
    if (bp.curve_count >= BP_MAX_CURVES) return;
    // Flatness check against quadratic approximation midpoint
    float mx = (x0 + 3.0f * x1 + 3.0f * x2 + x3) * 0.125f;
    float my = (y0 + 3.0f * y1 + 3.0f * y2 + y3) * 0.125f;
    float qx = (3.0f * (x1 + x2) - (x0 + x3)) * 0.25f;
    float qy = (3.0f * (y1 + y2) - (y0 + y3)) * 0.25f;
    float lx = (x0 + 2.0f * qx + x3) * 0.25f;
    float ly = (y0 + 2.0f * qy + y3) * 0.25f;
    float dx = mx - lx, dy = my - ly;
    if (depth >= 4 || (dx * dx + dy * dy) <= (0.15f * 0.15f)) {
        bp.curves[bp.curve_count++] = {x0, y0, qx, qy, x3, y3};
        return;
    }
    float x01 = 0.5f * (x0 + x1), y01 = 0.5f * (y0 + y1);
    float x12 = 0.5f * (x1 + x2), y12 = 0.5f * (y1 + y2);
    float x23 = 0.5f * (x2 + x3), y23 = 0.5f * (y2 + y3);
    float x012 = 0.5f * (x01 + x12), y012 = 0.5f * (y01 + y12);
    float x123 = 0.5f * (x12 + x23), y123 = 0.5f * (y12 + y23);
    float xm = 0.5f * (x012 + x123), ym = 0.5f * (y012 + y123);
    sv_flatten_cubic_to_quad(bp, x0, y0, x01, y01, x012, y012, xm, ym, depth + 1);
    sv_flatten_cubic_to_quad(bp, xm, ym, x123, y123, x23, y23, x3, y3, depth + 1);
}

static void sv_parse_path_d(BloomSvgBlueprint &bp, const char *d, size_t dlen,
                            float &min_x, float &min_y, float &max_x, float &max_y) {
    const char *p = d;
    const char *end = d + dlen;
    float cx = 0, cy = 0, sx = 0, sy = 0;
    char cmd = 0;
    min_x = 1e9f; min_y = 1e9f; max_x = -1e9f; max_y = -1e9f;
    auto expand_pt = [&](float x, float y) {
        if (x < min_x) min_x = x;
        if (y < min_y) min_y = y;
        if (x > max_x) max_x = x;
        if (y > max_y) max_y = y;
    };
    while (p < end) {
        p = sv_skip_ws_comma(p, end);
        if (p >= end) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            cmd = *p++;
            if (cmd == 'Z' || cmd == 'z') {
                if ((std::fabs(cx - sx) > 1e-4f || std::fabs(cy - sy) > 1e-4f) && bp.curve_count < BP_MAX_CURVES) {
                    bp.curves[bp.curve_count++] = {cx, cy, 0.5f * (cx + sx), 0.5f * (cy + sy), sx, sy};
                }
                cx = sx; cy = sy;
                continue;
            }
        }
        bool rel = (cmd >= 'a' && cmd <= 'z');
        char uc = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
        bool ok = false;
        if (uc == 'M' || uc == 'L') {
            float x = 0, y = 0;
            p = sv_parse_f32(p, end, x, ok); if (!ok) break;
            p = sv_parse_f32(p, end, y, ok); if (!ok) break;
            if (rel) { x += cx; y += cy; }
            expand_pt(x, y);
            if (uc == 'M') {
                cx = sx = x; cy = sy = y;
                cmd = rel ? 'l' : 'L';
            } else {
                if (bp.curve_count < BP_MAX_CURVES) {
                    bp.curves[bp.curve_count++] = {cx, cy, 0.5f * (cx + x), 0.5f * (cy + y), x, y};
                }
                cx = x; cy = y;
            }
        } else if (uc == 'H') {
            float x = 0;
            p = sv_parse_f32(p, end, x, ok); if (!ok) break;
            if (rel) x += cx;
            expand_pt(x, cy);
            if (bp.curve_count < BP_MAX_CURVES) {
                bp.curves[bp.curve_count++] = {cx, cy, 0.5f * (cx + x), cy, x, cy};
            }
            cx = x;
        } else if (uc == 'V') {
            float y = 0;
            p = sv_parse_f32(p, end, y, ok); if (!ok) break;
            if (rel) y += cy;
            expand_pt(cx, y);
            if (bp.curve_count < BP_MAX_CURVES) {
                bp.curves[bp.curve_count++] = {cx, cy, cx, 0.5f * (cy + y), cx, y};
            }
            cy = y;
        } else if (uc == 'Q') {
            float qx = 0, qy = 0, x = 0, y = 0;
            p = sv_parse_f32(p, end, qx, ok); if (!ok) break;
            p = sv_parse_f32(p, end, qy, ok); if (!ok) break;
            p = sv_parse_f32(p, end, x, ok); if (!ok) break;
            p = sv_parse_f32(p, end, y, ok); if (!ok) break;
            if (rel) { qx += cx; qy += cy; x += cx; y += cy; }
            expand_pt(cx, cy); expand_pt(qx, qy); expand_pt(x, y);
            if (bp.curve_count < BP_MAX_CURVES) {
                bp.curves[bp.curve_count++] = {cx, cy, qx, qy, x, y};
            }
            cx = x; cy = y;
        } else if (uc == 'C') {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;
            p = sv_parse_f32(p, end, x1, ok); if (!ok) break;
            p = sv_parse_f32(p, end, y1, ok); if (!ok) break;
            p = sv_parse_f32(p, end, x2, ok); if (!ok) break;
            p = sv_parse_f32(p, end, y2, ok); if (!ok) break;
            p = sv_parse_f32(p, end, x, ok); if (!ok) break;
            p = sv_parse_f32(p, end, y, ok); if (!ok) break;
            if (rel) { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy; }
            expand_pt(cx, cy); expand_pt(x1, y1); expand_pt(x2, y2); expand_pt(x, y);
            sv_flatten_cubic_to_quad(bp, cx, cy, x1, y1, x2, y2, x, y, 0);
            cx = x; cy = y;
        } else {
            ++p;
        }
    }
    if (min_x > max_x) { min_x = max_x = 0.0f; min_y = max_y = 0.0f; }
}

static void sv_parse_style_block(BloomSvgBlueprint &bp, const char *css, const char *end) {
    const char *p = css;
    while (p < end) {
        p = sv_skip_ws_comma(p, end);
        if (p >= end) break;
        const char *lbrace = static_cast<const char *>(std::memchr(p, '{', static_cast<size_t>(end - p)));
        if (!lbrace) break;
        const char *rbrace = static_cast<const char *>(std::memchr(lbrace + 1, '}', static_cast<size_t>(end - (lbrace + 1))));
        if (!rbrace) break;

        if (bp.rule_count < BP_MAX_RULES) {
            const char *sel_s = p;
            const char *sel_e = lbrace;
            while (sel_e > sel_s && sv_space(sel_e[-1])) --sel_e;
            uint8_t state = CSS_NONE;
            if (const char *col = static_cast<const char *>(std::memchr(sel_s, ':', static_cast<size_t>(sel_e - sel_s)))) {
                size_t p_len = static_cast<size_t>(sel_e - col);
                if (p_len >= 6 && std::memcmp(col, ":hover", 6) == 0) state |= CSS_HOVER;
                else if (p_len >= 7 && std::memcmp(col, ":active", 7) == 0) state |= CSS_ACTIVE;
                else if (p_len >= 6 && std::memcmp(col, ":focus", 6) == 0) state |= CSS_FOCUS;
                else if (p_len >= 9 && std::memcmp(col, ":disabled", 9) == 0) state |= CSS_DISABLED;
                sel_e = col;
            }
            if (const char *br = static_cast<const char *>(std::memchr(sel_s, '[', static_cast<size_t>(sel_e - sel_s)))) {
                if (std::strstr(br, "data-checked")) state |= CSS_CHECKED;
                sel_e = br;
            }
            CssStyleRule &r = bp.rules[bp.rule_count++];
            std::memset(&r, 0, sizeof(r));
            r.is_id = (sel_s < sel_e && *sel_s == '#') ? 1u : 0u;
            if (sel_s < sel_e && (*sel_s == '.' || *sel_s == '#')) ++sel_s;
            r.selector_hash = fnv1a64(sel_s, static_cast<size_t>(sel_e - sel_s));
            r.state_mask = state;
            r.fill_opacity = 1.0f;

            // Parse declarations separated by ';'
            const char *dp = lbrace + 1;
            while (dp < rbrace) {
                while (dp < rbrace && sv_space(*dp)) ++dp;
                const char *colon = static_cast<const char *>(std::memchr(dp, ':', static_cast<size_t>(rbrace - dp)));
                if (!colon) break;
                const char *semi = static_cast<const char *>(std::memchr(colon + 1, ';', static_cast<size_t>(rbrace - (colon + 1))));
                const char *vend = semi ? semi : rbrace;
                const char *ke = colon;
                while (ke > dp && sv_space(ke[-1])) --ke;
                const char *vs = colon + 1;
                while (vs < vend && sv_space(*vs)) ++vs;
                while (vend > vs && sv_space(vend[-1])) --vend;
                size_t kl = static_cast<size_t>(ke - dp);
                size_t vl = static_cast<size_t>(vend - vs);
                bool none = false;
                if (kl == 4 && std::memcmp(dp, "fill", 4) == 0) {
                    r.fill_rgb = sv_parse_hex_rgb(vs, vl, 0x20D8FFu, none);
                    r.has_fill = 1u;
                    if (none) { r.has_opacity = 1u; r.fill_opacity = 0.0f; }
                } else if (kl == 12 && std::memcmp(dp, "fill-opacity", 12) == 0) {
                    bool ok = false;
                    sv_parse_f32(vs, vend, r.fill_opacity, ok);
                    r.has_opacity = ok ? 1u : 0u;
                } else if (kl == 6 && std::memcmp(dp, "stroke", 6) == 0) {
                    r.stroke_rgb = sv_parse_hex_rgb(vs, vl, 0u, none);
                    r.has_stroke = none ? 0u : 1u;
                } else if (kl == 12 && std::memcmp(dp, "stroke-width", 12) == 0) {
                    bool ok = false;
                    sv_parse_f32(vs, vend, r.stroke_w, ok);
                    r.has_stroke_w = ok ? 1u : 0u;
                } else if (kl == 2 && std::memcmp(dp, "rx", 2) == 0) {
                    bool ok = false;
                    sv_parse_f32(vs, vend, r.rx, ok);
                    r.has_rx = ok ? 1u : 0u;
                }
                dp = semi ? (semi + 1) : rbrace;
            }
        }
        p = rbrace + 1;
    }
}

static void bp_link_child(BloomSvgBlueprint &bp, int32_t parent_idx, uint32_t child_idx) {
    if (parent_idx < 0 || parent_idx >= static_cast<int32_t>(bp.node_count)) return;
    SvgBlueprintNode &p = bp.nodes[static_cast<uint32_t>(parent_idx)];
    if (p.first_child < 0) {
        p.first_child = static_cast<int16_t>(child_idx);
        p.last_child = static_cast<int16_t>(child_idx);
    } else {
        bp.nodes[static_cast<uint32_t>(p.last_child)].next_sibling = static_cast<int16_t>(child_idx);
        p.last_child = static_cast<int16_t>(child_idx);
    }
}

static void sv_apply_css_static(BloomSvgBlueprint &bp, SvgBlueprintNode &nd) {
    const char *cls = bp.str_ptr(nd.class_off, nd.class_len);
    for (uint32_t r = 0; r < bp.rule_count; ++r) {
        const CssStyleRule &rule = bp.rules[r];
        if (rule.state_mask != CSS_NONE) continue;
        bool match = false;
        if (rule.is_id && nd.str_id_len > 0) {
            const char *sid = bp.str_ptr(nd.str_id_off, nd.str_id_len);
            if (fnv1a64(sid, nd.str_id_len) == rule.selector_hash) match = true;
        } else if (!rule.is_id && nd.class_len > 0) {
            const char *p = cls;
            const char *e = cls + nd.class_len;
            while (p < e) {
                while (p < e && sv_space(*p)) ++p;
                const char *w = p;
                while (p < e && !sv_space(*p)) ++p;
                if (p > w && fnv1a64(w, static_cast<size_t>(p - w)) == rule.selector_hash) {
                    match = true;
                    break;
                }
            }
        }
        if (match) {
            if (rule.has_fill) nd.fill_rgb = rule.fill_rgb;
            if (rule.has_opacity) nd.fill_opacity = rule.fill_opacity;
            if (rule.has_stroke) nd.stroke_rgb = rule.stroke_rgb;
            if (rule.has_stroke_w) nd.stroke_w = rule.stroke_w;
            if (rule.has_rx) nd.rx = rule.rx;
        }
    }
}

static bool sv_compile_svg(BloomSvgBlueprint &bp, const char *svg, size_t svg_len) {
    bp.reset();
    bp.epoch++;
    const char *p = svg;
    const char *end = svg + svg_len;
    uint32_t ordinal = 0;

    while (p < end) {
        const char *lt = static_cast<const char *>(std::memchr(p, '<', static_cast<size_t>(end - p)));
        if (!lt) break;
        if (lt + 4 <= end && std::memcmp(lt, "<!--", 4) == 0) {
            const char *ce = static_cast<const char *>(std::strstr(lt + 4, "-->"));
            if (!ce || ce >= end) break;
            p = ce + 3;
            continue;
        }
        if (lt + 2 <= end && (lt[1] == '?' || lt[1] == '!')) {
            const char *gt = static_cast<const char *>(std::memchr(lt, '>', static_cast<size_t>(end - lt)));
            if (!gt) break;
            p = gt + 1;
            continue;
        }
        const char *gt = static_cast<const char *>(std::memchr(lt, '>', static_cast<size_t>(end - lt)));
        if (!gt) break;

        bool self_closing = (gt > lt + 1 && gt[-1] == '/');
        const char *te = self_closing ? (gt - 1) : gt;

        if (lt + 6 <= end && std::memcmp(lt, "<style", 6) == 0) {
            const char *se = std::strstr(gt + 1, "</style>");
            if (!se || se > end) break;
            sv_parse_style_block(bp, gt + 1, se);
            p = se + 8;
            continue;
        }
        if (lt + 7 <= end && std::memcmp(lt, "</defs>", 7) == 0) {
            bp.in_defs = false; p = gt + 1; continue;
        }
        if (lt + 9 <= end && std::memcmp(lt, "</symbol>", 9) == 0) {
            if (bp.in_symbol && bp.cur_symbol_idx >= 0) {
                SvgSymbolDef &sym = bp.symbols[static_cast<uint32_t>(bp.cur_symbol_idx)];
                sym.node_count = static_cast<uint16_t>(bp.node_count - sym.node_start);
            }
            bp.in_symbol = false; bp.cur_symbol_idx = -1;
            if (bp.grp_depth > 0) --bp.grp_depth;
            p = gt + 1; continue;
        }
        if (lt + 11 <= end && std::memcmp(lt, "</clipPath>", 11) == 0) {
            bp.in_clippath = false; bp.cur_clip_idx = -1; p = gt + 1; continue;
        }
        if (lt + 9 <= end && std::memcmp(lt, "</filter>", 9) == 0) {
            bp.cur_filter_idx = -1; p = gt + 1; continue;
        }
        if (lt + 4 <= end && std::memcmp(lt, "</g>", 4) == 0) {
            if (bp.grp_depth > 0) --bp.grp_depth;
            p = gt + 1; continue;
        }

        if (lt + 4 <= end && std::memcmp(lt, "<svg", 4) == 0) {
            size_t vbl = 0;
            const char *vb = sv_find_attr(lt + 4, te, "viewBox", vbl);
            if (vb && vbl > 0) {
                bool ok = false;
                const char *vp = vb;
                vp = sv_parse_f32(vp, vb + vbl, bp.vb_min_x, ok);
                vp = sv_parse_f32(vp, vb + vbl, bp.vb_min_y, ok);
                vp = sv_parse_f32(vp, vb + vbl, bp.vb_w, ok);
                sv_parse_f32(vp, vb + vbl, bp.vb_h, ok);
            } else {
                bp.vb_w = sv_attr_f32(lt + 4, te, "width", 1280.0f);
                bp.vb_h = sv_attr_f32(lt + 4, te, "height", 720.0f);
            }
            if (bp.vb_w <= 0.0f) bp.vb_w = 1280.0f;
            if (bp.vb_h <= 0.0f) bp.vb_h = 720.0f;
            p = gt + 1; continue;
        }

        if (lt + 5 <= end && std::memcmp(lt, "<defs", 5) == 0) {
            if (!self_closing) bp.in_defs = true;
            p = gt + 1; continue;
        }
        if (lt + 7 <= end && std::memcmp(lt, "<symbol", 7) == 0 && bp.symbol_count < BP_MAX_SYMBOLS) {
            const char *ts = lt + 7;
            size_t idl = 0;
            const char *ids = sv_find_attr(ts, te, "id", idl);
            SvgSymbolDef &sym = bp.symbols[bp.symbol_count];
            sym.id_hash = ids ? fnv1a64(ids, idl) : 0ULL;
            sym.vb_w = 100.0f; sym.vb_h = 28.0f;
            size_t vbl = 0;
            if (const char *vb = sv_find_attr(ts, te, "viewBox", vbl)) {
                float mx = 0, my = 0; bool ok = false;
                const char *vp = sv_parse_f32(vb, vb + vbl, mx, ok);
                vp = sv_parse_f32(vp, vb + vbl, my, ok);
                vp = sv_parse_f32(vp, vb + vbl, sym.vb_w, ok);
                sv_parse_f32(vp, vb + vbl, sym.vb_h, ok);
            }
            sym.node_start = static_cast<uint16_t>(bp.node_count);
            sym.node_count = 0;
            bp.cur_symbol_idx = static_cast<int32_t>(bp.symbol_count++);
            bp.in_symbol = !self_closing;
            if (!self_closing && bp.grp_depth < 63) {
                bp.grp_stack[bp.grp_depth] = -1;
                bp.grp_tx[bp.grp_depth] = 0.0f;
                bp.grp_ty[bp.grp_depth] = 0.0f;
                bp.grp_depth++;
            }
            p = gt + 1; continue;
        }
        if (lt + 9 <= end && std::memcmp(lt, "<clipPath", 9) == 0 && bp.clip_count < BP_MAX_CLIPS) {
            size_t idl = 0;
            const char *ids = sv_find_attr(lt + 9, te, "id", idl);
            SvgClipDef &cd = bp.clips[bp.clip_count];
            cd = {ids ? fnv1a64(ids, idl) : 0ULL, 0, 0, bp.vb_w, bp.vb_h};
            bp.cur_clip_idx = static_cast<int32_t>(bp.clip_count++);
            bp.in_clippath = !self_closing;
            p = gt + 1; continue;
        }
        if (lt + 7 <= end && std::memcmp(lt, "<filter", 7) == 0 && bp.filter_count < BP_MAX_FILTERS) {
            size_t idl = 0;
            const char *ids = sv_find_attr(lt + 7, te, "id", idl);
            SvgFilterDef &fd = bp.filters[bp.filter_count];
            fd = {ids ? fnv1a64(ids, idl) : 0ULL, 0, 4, 12, 0x59000000u};
            bp.cur_filter_idx = static_cast<int32_t>(bp.filter_count++);
            p = gt + 1; continue;
        }
        if (lt + 13 <= end && std::memcmp(lt, "<feDropShadow", 13) == 0 && bp.cur_filter_idx >= 0) {
            const char *ts = lt + 13;
            SvgFilterDef &fd = bp.filters[static_cast<uint32_t>(bp.cur_filter_idx)];
            fd.dx = sv_attr_f32(ts, te, "dx", 0.0f);
            fd.dy = sv_attr_f32(ts, te, "dy", 4.0f);
            fd.blur = sv_attr_f32(ts, te, "stdDeviation", 10.0f);
            size_t fcl = 0;
            bool none = false;
            const char *fc = sv_find_attr(ts, te, "flood-color", fcl);
            uint32_t rgb = fc ? sv_parse_hex_rgb(fc, fcl, 0x000000u, none) : 0x000000u;
            float fop = sv_attr_f32(ts, te, "flood-opacity", 0.35f);
            fd.rgba = rgba_with_opacity(rgb, fop);
            p = gt + 1; continue;
        }
        if (bp.in_clippath && lt + 5 <= end && std::memcmp(lt, "<rect", 5) == 0 && bp.cur_clip_idx >= 0) {
            const char *ts = lt + 5;
            SvgClipDef &cd = bp.clips[static_cast<uint32_t>(bp.cur_clip_idx)];
            cd.x = sv_attr_f32(ts, te, "x", 0.0f);
            cd.y = sv_attr_f32(ts, te, "y", 0.0f);
            cd.w = sv_attr_f32(ts, te, "width", bp.vb_w);
            cd.h = sv_attr_f32(ts, te, "height", bp.vb_h);
            p = gt + 1; continue;
        }

        // Handle <use href="#sym" .../>
        if (lt + 4 <= end && std::memcmp(lt, "<use", 4) == 0 && !bp.in_defs) {
            const char *ts = lt + 4;
            size_t hl = 0;
            const char *hv = sv_find_attr(ts, te, "href", hl);
            if (!hv) hv = sv_find_attr(ts, te, "xlink:href", hl);
            if (hv && hl > 1 && hv[0] == '#') {
                uint64_t sym_h = fnv1a64(hv + 1, hl - 1);
                float ux = sv_attr_f32(ts, te, "x", 0.0f);
                float uy = sv_attr_f32(ts, te, "y", 0.0f);
                float uw = sv_attr_f32(ts, te, "width", 0.0f);
                float uh = sv_attr_f32(ts, te, "height", 0.0f);
                size_t uidl = 0;
                const char *uids = sv_find_attr(ts, te, "id", uidl);
                uint64_t use_k = uids ? fnv1a64(uids, uidl) : (0xBEEFULL + ordinal);
                size_t lbl_l = 0;
                const char *lbl_s = sv_find_attr(ts, te, "data-label", lbl_l);
                for (uint32_t s = 0; s < bp.symbol_count; ++s) {
                    const SvgSymbolDef &sym = bp.symbols[s];
                    if (sym.id_hash == sym_h) {
                        float sx = (uw > 0.0f && sym.vb_w > 0.0f) ? (uw / sym.vb_w) : 1.0f;
                        float sy = (uh > 0.0f && sym.vb_h > 0.0f) ? (uh / sym.vb_h) : 1.0f;
                        float tx_p = bp.grp_depth > 0 ? bp.grp_tx[bp.grp_depth - 1] : 0.0f;
                        float ty_p = bp.grp_depth > 0 ? bp.grp_ty[bp.grp_depth - 1] : 0.0f;
                        int16_t par = bp.grp_depth > 0 ? bp.grp_stack[bp.grp_depth - 1] : -1;
                        for (uint16_t k = 0; k < sym.node_count && bp.node_count < BP_MAX_NODES; ++k) {
                            uint32_t dst_idx = bp.node_count++;
                            SvgBlueprintNode nd = bp.nodes[sym.node_start + k];
                            nd.parent_idx = par;
                            nd.first_child = -1;
                            nd.last_child = -1;
                            nd.next_sibling = -1;
                            nd.x = nd.x * sx + ux + tx_p;
                            nd.y = nd.y * sy + uy + ty_p;
                            nd.w *= sx;
                            nd.h *= sy;
                            nd.node_key = fnv1a64_mix(use_k, nd.node_key ? nd.node_key : (k + 1u));
                            if (lbl_s && lbl_l > 0 && nd.kind == NODE_TEXT) {
                                bp.push_str(lbl_s, lbl_l, nd.label_off, nd.label_len);
                            }
                            bp.nodes[dst_idx] = nd;
                            bp.insert_key(nd.node_key, static_cast<int32_t>(dst_idx));
                            bp_link_child(bp, par, dst_idx);
                        }
                        break;
                    }
                }
            }
            ++ordinal;
            p = gt + 1; continue;
        }

        bool is_g = (lt + 2 <= end && std::memcmp(lt, "<g", 2) == 0 && (lt[2] == ' ' || lt[2] == '>'));
        bool is_rect = (lt + 5 <= end && std::memcmp(lt, "<rect", 5) == 0);
        bool is_circle = (lt + 7 <= end && std::memcmp(lt, "<circle", 7) == 0);
        bool is_text = (lt + 5 <= end && std::memcmp(lt, "<text", 5) == 0);
        bool is_path = (lt + 5 <= end && std::memcmp(lt, "<path", 5) == 0);
        bool is_line = (lt + 5 <= end && std::memcmp(lt, "<line", 5) == 0);

        if ((is_g || is_rect || is_circle || is_text || is_path || is_line) && bp.node_count < BP_MAX_NODES) {
            const char *ts = lt + (is_g ? 2 : (is_circle ? 7 : 5));
            uint32_t idx = bp.node_count++;
            SvgBlueprintNode &nd = bp.nodes[idx];
            std::memset(&nd, 0, sizeof(nd));
            nd.parent_idx = bp.grp_depth > 0 ? bp.grp_stack[bp.grp_depth - 1] : -1;
            nd.first_child = -1;
            nd.last_child = -1;
            nd.next_sibling = -1;
            nd.clip_ref_idx = -1;
            nd.bind_channel = -1;
            nd.fill_opacity = 1.0f;
            nd.fill_rgb = 0x20D8FFu;

            size_t idl = 0;
            const char *ids = sv_find_attr(ts, te, "id", idl);
            if (!ids) ids = sv_find_attr(ts, te, "data-node-id", idl);
            if (ids && idl > 0) {
                bp.push_str(ids, idl, nd.str_id_off, nd.str_id_len);
                nd.node_key = fnv1a64(ids, idl);
            } else {
                nd.node_key = fnv1a64_mix(nd.parent_idx >= 0 ? bp.nodes[static_cast<uint32_t>(nd.parent_idx)].node_key : 0x51A7ULL, ordinal + 1u);
            }
            size_t cll = 0;
            if (const char *cls = sv_find_attr(ts, te, "class", cll)) {
                bp.push_str(cls, cll, nd.class_off, nd.class_len);
            }

            float tx_p = bp.grp_depth > 0 ? bp.grp_tx[bp.grp_depth - 1] : 0.0f;
            float ty_p = bp.grp_depth > 0 ? bp.grp_ty[bp.grp_depth - 1] : 0.0f;
            float cur_tx = tx_p, cur_ty = ty_p;
            size_t trl = 0;
            if (const char *tr = sv_find_attr(ts, te, "transform", trl)) {
                if (const char *trp = static_cast<const char *>(std::strstr(tr, "translate("))) {
                    if (trp < tr + trl) {
                        bool ok = false;
                        float dx = 0, dy = 0;
                        const char *q = sv_parse_f32(trp + 10, tr + trl, dx, ok);
                        if (ok) {
                            cur_tx += dx;
                            sv_parse_f32(q, tr + trl, dy, ok);
                            if (ok) cur_ty += dy;
                        }
                    }
                }
            }

            // Common fill/stroke/opacity
            size_t fl = 0;
            bool fill_none = false;
            if (const char *fv = sv_find_attr(ts, te, "fill", fl)) {
                nd.fill_rgb = sv_parse_hex_rgb(fv, fl, 0x20D8FFu, fill_none);
                if (fill_none) nd.fill_opacity = 0.0f;
            }
            nd.fill_opacity = sv_attr_f32(ts, te, "fill-opacity", fill_none ? 0.0f : 1.0f);
            size_t sl = 0;
            bool st_none = false;
            if (const char *svv = sv_find_attr(ts, te, "stroke", sl)) {
                nd.stroke_rgb = sv_parse_hex_rgb(svv, sl, 0u, st_none);
                if (!st_none) nd.stroke_w = sv_attr_f32(ts, te, "stroke-width", 1.5f);
            }
            nd.rx = sv_attr_f32(ts, te, "rx", 0.0f);
            nd.prim_type = static_cast<uint16_t>(sv_attr_f32(ts, te, "data-prim", 1.0f));
            float op_f = sv_attr_f32(ts, te, "data-action", 0.0f);
            uint32_t caps = static_cast<uint32_t>(sv_attr_f32(ts, te, "data-caps", 0.0f)) << 16u;
            if (sv_attr_eq(ts, te, "data-checked", "true")) caps |= CAP_CHECKED | CAP_TOGGLE;
            if (sv_attr_eq(ts, te, "data-disabled", "true")) caps |= CAP_DISABLED;
            if (sv_attr_eq(ts, te, "data-focusable", "true")) caps |= CAP_FOCUSABLE;
            if (sv_attr_eq(ts, te, "data-modal", "true")) caps |= CAP_MODAL;
            if (sv_attr_eq(ts, te, "data-guard", "true")) caps |= CAP_AGENT_GUARD;
            if (sv_attr_eq(ts, te, "data-toggle", "true")) caps |= CAP_TOGGLE;
            if (nd.prim_type == 4u || sv_attr_eq(ts, te, "data-scrub", "true")) caps |= CAP_SCRUB | CAP_FOCUSABLE;
            if (nd.prim_type == 7u || static_cast<uint32_t>(op_f) > 0u) caps |= CAP_FOCUSABLE;
            nd.action_caps = (static_cast<uint32_t>(op_f) & 0xFFFFu) | caps;

            float bch = sv_attr_f32(ts, te, "data-bind", -1.0f);
            nd.bind_channel = (bch >= 0.0f && bch < static_cast<float>(BP_SUB_CH_MAX)) ? static_cast<int16_t>(bch) : -1;
            if (nd.bind_channel >= 0) {
                uint32_t ch = static_cast<uint32_t>(nd.bind_channel);
                if (bp.ch_sub_count[ch] < BP_SUB_PER_CH) {
                    bp.ch_subs[ch][bp.ch_sub_count[ch]++] = static_cast<int16_t>(idx);
                }
            }
            float init_q16 = sv_attr_f32(ts, te, "data-init-q16", -1.0f);
            nd.init_val = (init_q16 >= 0.0f) ? (init_q16 / 65536.0f) : sv_attr_f32(ts, te, "data-init-val", 0.5f);

            // Filter / data-shadow
            nd.shadow_blur = sv_attr_f32(ts, te, "data-shadow", 0.0f);
            if (nd.shadow_blur > 0.0f) {
                nd.has_shadow = 1u;
                nd.shadow_dx = 0.0f; nd.shadow_dy = 4.0f;
                nd.shadow_rgba = pack_rgba8(0, 0, 0, 90);
            }
            size_t flt_l = 0;
            if (const char *fv = sv_find_attr(ts, te, "filter", flt_l)) {
                const char *h = static_cast<const char *>(std::memchr(fv, '#', flt_l));
                if (h) {
                    const char *rp = static_cast<const char *>(std::memchr(h + 1, ')', static_cast<size_t>(fv + flt_l - (h + 1))));
                    if (rp) {
                        uint64_t fh = fnv1a64(h + 1, static_cast<size_t>(rp - (h + 1)));
                        for (uint32_t fi = 0; fi < bp.filter_count; ++fi) {
                            if (bp.filters[fi].id_hash == fh) {
                                nd.has_shadow = 1u;
                                nd.shadow_dx = bp.filters[fi].dx;
                                nd.shadow_dy = bp.filters[fi].dy;
                                nd.shadow_blur = bp.filters[fi].blur;
                                nd.shadow_rgba = bp.filters[fi].rgba;
                                break;
                            }
                        }
                    }
                }
            }

            size_t lbl_l = 0;
            if (const char *lbl_s = sv_find_attr(ts, te, "data-label", lbl_l)) {
                bp.push_str(lbl_s, lbl_l, nd.label_off, nd.label_len);
            }

            if (is_g) {
                nd.kind = sv_attr_eq(ts, te, "data-role", "canvas") ? NODE_CANVAS : NODE_GROUP;
                nd.x = sv_attr_f32(ts, te, "x", 0.0f) + cur_tx;
                nd.y = sv_attr_f32(ts, te, "y", 0.0f) + cur_ty;
                nd.w = sv_attr_f32(ts, te, "width", 0.0f);
                nd.h = sv_attr_f32(ts, te, "height", 0.0f);
                if (sv_attr_eq(ts, te, "data-layout", "row")) nd.layout_dir = LAYOUT_ROW;
                else if (sv_attr_eq(ts, te, "data-layout", "col")) nd.layout_dir = LAYOUT_COL;
                nd.gap = sv_attr_f32(ts, te, "data-gap", 0.0f);
                nd.pad = sv_attr_f32(ts, te, "data-pad", 0.0f);
                nd.flex_grow = sv_attr_f32(ts, te, "data-grow", 0.0f);
                if (sv_attr_eq(ts, te, "data-align", "stretch")) nd.align_items = ALIGN_STRETCH;
                else if (sv_attr_eq(ts, te, "data-align", "center")) nd.align_items = ALIGN_CENTER;
                if (sv_attr_eq(ts, te, "data-justify", "between")) nd.justify = JUST_BETWEEN;
                else if (sv_attr_eq(ts, te, "data-justify", "center")) nd.justify = JUST_CENTER;
                if (sv_attr_eq(ts, te, "data-scroll", "y")) nd.scroll_y = 1u;
                size_t vt_l = 0;
                if (const char *vt = sv_find_attr(ts, te, "data-item-template", vt_l)) {
                    nd.virtual_list = 1u;
                    const char *vsym = (vt[0] == '#') ? vt + 1 : vt;
                    size_t vslen = (vt[0] == '#') ? (vt_l - 1) : vt_l;
                    uint64_t sh = fnv1a64(vsym, vslen);
                    for (uint16_t si = 0; si < bp.symbol_count; ++si) {
                        if (bp.symbols[si].id_hash == sh) { nd.symbol_template_idx = si; break; }
                    }
                    nd.virtual_item_h = sv_attr_f32(ts, te, "data-item-height", 28.0f);
                    nd.virtual_total_count = static_cast<uint32_t>(sv_attr_f32(ts, te, "data-total-count", 100.0f));
                }
                if (!self_closing && bp.grp_depth < 63) {
                    bp.grp_stack[bp.grp_depth] = static_cast<int16_t>(idx);
                    bp.grp_tx[bp.grp_depth] = cur_tx;
                    bp.grp_ty[bp.grp_depth] = cur_ty;
                    bp.grp_depth++;
                }
            } else if (is_rect) {
                nd.kind = NODE_RECT;
                nd.x = sv_attr_f32(ts, te, "x", 0.0f) + cur_tx;
                nd.y = sv_attr_f32(ts, te, "y", 0.0f) + cur_ty;
                nd.w = sv_attr_f32(ts, te, "width", 120.0f);
                nd.h = sv_attr_f32(ts, te, "height", 40.0f);
                nd.flex_grow = sv_attr_f32(ts, te, "data-grow", 0.0f);
            } else if (is_circle) {
                nd.kind = NODE_CIRCLE;
                float cx = sv_attr_f32(ts, te, "cx", 0.0f) + cur_tx;
                float cy = sv_attr_f32(ts, te, "cy", 0.0f) + cur_ty;
                float r  = sv_attr_f32(ts, te, "r", 24.0f);
                nd.x = cx - r; nd.y = cy - r; nd.w = 2.0f * r; nd.h = 2.0f * r;
                nd.rx = r;
            } else if (is_text) {
                nd.kind = NODE_TEXT;
                nd.x = sv_attr_f32(ts, te, "x", 0.0f) + cur_tx;
                nd.y = sv_attr_f32(ts, te, "y", 0.0f) + cur_ty;
                float fsz = sv_attr_f32(ts, te, "font-size", 14.0f);
                nd.h = fsz * 1.4f;
                if (!self_closing) {
                    const char *te_close = std::strstr(gt + 1, "</text>");
                    if (te_close && te_close <= end) {
                        const char *tx_s = gt + 1;
                        while (tx_s < te_close && sv_space(*tx_s)) ++tx_s;
                        const char *tx_e = te_close;
                        while (tx_e > tx_s && sv_space(tx_e[-1])) --tx_e;
                        bp.push_str(tx_s, static_cast<size_t>(tx_e - tx_s), nd.label_off, nd.label_len);
                        nd.w = static_cast<float>(nd.label_len) * (fsz * 0.6f);
                        p = te_close + 7;
                        sv_apply_css_static(bp, nd);
                        if (!bp.in_defs && !bp.in_symbol && !bp.in_clippath) {
                            bp.insert_key(nd.node_key, static_cast<int32_t>(idx));
                            bp_link_child(bp, nd.parent_idx, idx);
                        }
                        ++ordinal;
                        continue;
                    }
                }
            } else if (is_path) {
                nd.kind = NODE_PATH;
                nd.glyph_id = static_cast<uint16_t>(sv_attr_f32(ts, te, "data-glyph-id", static_cast<float>((idx % 5u) + 1u)));
                size_t dl = 0;
                const char *ds = sv_find_attr(ts, te, "d", dl);
                nd.curve_start = bp.curve_count;
                if (ds && dl > 0) {
                    float bx0, by0, bx1, by1;
                    sv_parse_path_d(bp, ds, dl, bx0, by0, bx1, by1);
                    nd.curve_count = static_cast<uint16_t>(bp.curve_count - nd.curve_start);
                    nd.x = bx0 + cur_tx; nd.y = by0 + cur_ty;
                    nd.w = std::max(bx1 - bx0, 12.0f); nd.h = std::max(by1 - by0, 12.0f);
                }
            } else if (is_line) {
                nd.kind = NODE_PATH;
                nd.glyph_id = static_cast<uint16_t>(sv_attr_f32(ts, te, "data-glyph-id", 1.0f));
                float x1 = sv_attr_f32(ts, te, "x1", 0.0f) + cur_tx;
                float y1 = sv_attr_f32(ts, te, "y1", 0.0f) + cur_ty;
                float x2 = sv_attr_f32(ts, te, "x2", 0.0f) + cur_tx;
                float y2 = sv_attr_f32(ts, te, "y2", 0.0f) + cur_ty;
                nd.curve_start = bp.curve_count;
                if (bp.curve_count < BP_MAX_CURVES) {
                    bp.curves[bp.curve_count++] = {x1, y1, 0.5f * (x1 + x2), 0.5f * (y1 + y2), x2, y2};
                    nd.curve_count = 1;
                }
                nd.x = std::min(x1, x2); nd.y = std::min(y1, y2);
                nd.w = std::max(std::fabs(x2 - x1), 2.0f); nd.h = std::max(std::fabs(y2 - y1), 2.0f);
            }

            sv_apply_css_static(bp, nd);
            if (!bp.in_defs && !bp.in_symbol && !bp.in_clippath) {
                bp.insert_key(nd.node_key, static_cast<int32_t>(idx));
                bp_link_child(bp, nd.parent_idx, idx);
            }
            ++ordinal;
        }
        p = gt + 1;
    }
    return true;
}

// ============================================================================
// Layer 2: Decoupled Reactive StateStore + Hot-Reload + Interaction Engine
// ============================================================================
static constexpr size_t STATE_CAP = 32768; // power of 2
enum InteractionStatus : uint32_t {
    STATUS_OK = 0,
    STATUS_NOT_FOUND = 1,
    STATUS_DISABLED = 2,
    STATUS_MODAL_TRAP = 3,
    STATUS_VETOED_GUARD = 4
};
enum AgentActKind : uint32_t {
    ACT_CLICK = 1,
    ACT_TOGGLE = 2,
    ACT_SET_VALUE = 3,
    ACT_FOCUS = 4,
    ACT_ROVE_NEXT = 5,
    ACT_ROVE_PREV = 6
};

struct NodeRuntimeState {
    uint64_t node_key;
    uint32_t seen_epoch;
    uint32_t flags;        // bit0=used, bit1=tombstone
    float    bound_value;
    bool     checked;
    float    scroll_y;
    float    spring_pos;
    float    spring_vel;
    uint8_t  text_len;
    uint8_t  ime_len;
    char     text_buf[128];
    char     ime_preedit[64];
};

typedef bool (*AgentGuardFn)(uint64_t node_key, uint32_t action_op, float value, void *user_ctx);

struct BloomStateStore {
    std::vector<NodeRuntimeState> slots;
    float    bind_channels[BP_SUB_CH_MAX]{};
    uint64_t dirty_ch_mask = 0;

    uint64_t hovered_key = 0;
    uint64_t active_key = 0;
    uint64_t captured_key = 0;
    uint64_t focused_key = 0;
    uint64_t modal_root_key = 0;
    float    last_cursor_x = 0.0f;
    uint32_t epoch = 0;
    uint32_t live_slots = 0;

    AgentGuardFn guard_fn = nullptr;
    void        *guard_ctx = nullptr;

    void alloc_once() {
        slots.resize(STATE_CAP);
        reset();
    }

    void reset() {
        std::memset(slots.data(), 0, slots.size() * sizeof(NodeRuntimeState));
        std::memset(bind_channels, 0, sizeof(bind_channels));
        dirty_ch_mask = 0;
        hovered_key = active_key = captured_key = focused_key = modal_root_key = 0;
        epoch = 0; live_slots = 0;
        guard_fn = nullptr; guard_ctx = nullptr;
    }

    NodeRuntimeState *find_slot(uint64_t key) {
        if (key == 0ULL) return nullptr;
        uint32_t mask = static_cast<uint32_t>(STATE_CAP - 1);
        uint32_t idx = static_cast<uint32_t>(key ^ (key >> 32)) & mask;
        for (uint32_t probe = 0; probe < 128u; ++probe) {
            NodeRuntimeState &s = slots[(idx + probe) & mask];
            if ((s.flags & 1u) == 0u && (s.flags & 2u) == 0u) return nullptr;
            if ((s.flags & 1u) != 0u && s.node_key == key) return &s;
        }
        return nullptr;
    }

    const NodeRuntimeState *find_slot_const(uint64_t key) const {
        if (key == 0ULL) return nullptr;
        uint32_t mask = static_cast<uint32_t>(STATE_CAP - 1);
        uint32_t idx = static_cast<uint32_t>(key ^ (key >> 32)) & mask;
        for (uint32_t probe = 0; probe < 128u; ++probe) {
            const NodeRuntimeState &s = slots[(idx + probe) & mask];
            if ((s.flags & 1u) == 0u && (s.flags & 2u) == 0u) return nullptr;
            if ((s.flags & 1u) != 0u && s.node_key == key) return &s;
        }
        return nullptr;
    }

    NodeRuntimeState *get_or_insert(uint64_t key, float init_val, bool init_checked) {
        if (key == 0ULL) return nullptr;
        uint32_t mask = static_cast<uint32_t>(STATE_CAP - 1);
        uint32_t idx = static_cast<uint32_t>(key ^ (key >> 32)) & mask;
        int32_t first_tomb = -1;
        for (uint32_t probe = 0; probe < 128u; ++probe) {
            uint32_t slot = (idx + probe) & mask;
            NodeRuntimeState &s = slots[slot];
            if ((s.flags & 1u) != 0u && s.node_key == key) {
                return &s;
            }
            if ((s.flags & 1u) == 0u) {
                if ((s.flags & 2u) == 0u) {
                    uint32_t target = (first_tomb >= 0) ? static_cast<uint32_t>(first_tomb) : slot;
                    NodeRuntimeState &t = slots[target];
                    std::memset(&t, 0, sizeof(t));
                    t.node_key = key;
                    t.seen_epoch = epoch;
                    t.flags = 1u;
                    t.bound_value = init_val;
                    t.checked = init_checked;
                    t.spring_pos = 1.0f;
                    ++live_slots;
                    return &t;
                } else if (first_tomb < 0) {
                    first_tomb = static_cast<int32_t>(slot);
                }
            }
        }
        return nullptr;
    }

    // O(dirty subscribers) channel write without scanning full DOM
    uint32_t set_bind_channel(const BloomSvgBlueprint &bp, uint32_t ch, float val) {
        if (ch >= BP_SUB_CH_MAX) return 0;
        bind_channels[ch] = val;
        dirty_ch_mask |= (1ULL << ch);
        uint32_t updated = 0;
        uint16_t sc = bp.ch_sub_count[ch];
        for (uint16_t i = 0; i < sc; ++i) {
            int16_t ni = bp.ch_subs[ch][i];
            if (ni >= 0 && static_cast<uint32_t>(ni) < bp.node_count) {
                if (NodeRuntimeState *s = find_slot(bp.nodes[static_cast<uint32_t>(ni)].node_key)) {
                    s->bound_value = val;
                    ++updated;
                }
            }
        }
        return updated;
    }
};

struct HotReloadReport {
    uint32_t surviving = 0;
    uint32_t added = 0;
    uint32_t deleted = 0;
    bool focus_cleared = false;
    bool capture_cleared = false;
};

static HotReloadReport reconcile_hot_reload(const BloomSvgBlueprint &bp, BloomStateStore &store) {
    HotReloadReport rep{};
    store.epoch++;
    for (uint32_t i = 0; i < bp.node_count; ++i) {
        const SvgBlueprintNode &nd = bp.nodes[i];
        bool is_new = (store.find_slot(nd.node_key) == nullptr);
        NodeRuntimeState *s = store.get_or_insert(nd.node_key, nd.init_val, (nd.action_caps & CAP_CHECKED) != 0u);
        if (s) {
            s->seen_epoch = store.epoch;
            if (is_new) ++rep.added;
            else ++rep.surviving;
        }
    }
    // Reclaim deleted slots & guard dangling interaction keys
    for (size_t i = 0; i < STATE_CAP; ++i) {
        NodeRuntimeState &s = store.slots[i];
        if ((s.flags & 1u) != 0u && s.seen_epoch != store.epoch) {
            s.flags = 2u; // tombstone preserves open-address chain
            if (store.live_slots > 0) --store.live_slots;
            ++rep.deleted;
        }
    }
    if (store.focused_key != 0ULL && bp.find_by_key(store.focused_key) < 0) {
        store.focused_key = 0ULL; rep.focus_cleared = true;
    }
    if (store.captured_key != 0ULL && bp.find_by_key(store.captured_key) < 0) {
        store.captured_key = 0ULL; rep.capture_cleared = true;
    }
    if (store.hovered_key != 0ULL && bp.find_by_key(store.hovered_key) < 0) store.hovered_key = 0ULL;
    if (store.active_key != 0ULL && bp.find_by_key(store.active_key) < 0) store.active_key = 0ULL;
    store.modal_root_key = 0ULL;
    for (uint32_t i = 0; i < bp.node_count; ++i) {
        if ((bp.nodes[i].action_caps & CAP_MODAL) != 0u && (bp.nodes[i].action_caps & CAP_DISABLED) == 0u) {
            store.modal_root_key = bp.nodes[i].node_key;
            break;
        }
    }
    return rep;
}

// ============================================================================
// Layer 3: Two-Pass Flexbox + Dirty-Tracked Layout + Virtual List + WC Packer
// ============================================================================
struct ComputedBox {
    float x, y, w, h;
    float content_h;
    float max_scroll_y;
    uint16_t clip_id;
    uint8_t  visible;
    uint8_t  dirty;
};

struct BloomLayoutEngine {
    std::vector<ComputedBox>   boxes;
    std::vector<BloomClipRect> clips;
    uint32_t clip_count = 1;
    bool     layout_dirty = true;

    void alloc_once() {
        boxes.resize(BP_MAX_NODES);
        clips.resize(512);
        std::memset(boxes.data(), 0, boxes.size() * sizeof(ComputedBox));
    }

    static BloomClipRect intersect_clip(const BloomClipRect &a, const BloomClipRect &b) {
        float x0 = std::max(a.x, b.x);
        float y0 = std::max(a.y, b.y);
        float x1 = std::min(a.x + a.w, b.x + b.w);
        float y1 = std::min(a.y + a.h, b.y + b.h);
        return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }

    void layout_subtree(const BloomSvgBlueprint &bp, const BloomStateStore &store,
                        int32_t idx, float avail_x, float avail_y, float avail_w, float avail_h,
                        uint16_t cur_clip, float sx, float sy) {
        if (idx < 0 || static_cast<uint32_t>(idx) >= bp.node_count) return;
        const SvgBlueprintNode &nd = bp.nodes[static_cast<uint32_t>(idx)];
        ComputedBox &cb = boxes[static_cast<uint32_t>(idx)];
        cb.x = avail_x;
        cb.y = avail_y;
        cb.w = (nd.w > 0.0f) ? (nd.w * sx) : avail_w;
        cb.h = (nd.h > 0.0f) ? (nd.h * sy) : avail_h;
        cb.clip_id = cur_clip;
        const BloomClipRect &cr = clips[cur_clip];
        cb.visible = (cb.w > 0.0f && cb.h > 0.0f &&
                      cb.x + cb.w > cr.x && cb.x < cr.x + cr.w &&
                      cb.y + cb.h > cr.y && cb.y < cr.y + cr.h) ? 1u : 0u;

        uint16_t child_clip = cur_clip;
        if ((nd.scroll_y || nd.virtual_list) && clip_count + 1u < clips.size()) {
            child_clip = static_cast<uint16_t>(clip_count++);
            clips[child_clip] = intersect_clip(cr, {cb.x, cb.y, cb.w, cb.h});
        }

        float scroll_off = 0.0f;
        if (const NodeRuntimeState *st = store.find_slot_const(nd.node_key)) {
            scroll_off = st->scroll_y;
        }

        if (nd.virtual_list && nd.virtual_total_count > 0u) {
            cb.content_h = static_cast<float>(nd.virtual_total_count) * nd.virtual_item_h * sy;
            cb.max_scroll_y = std::max(0.0f, cb.content_h - cb.h);
            return;
        }

        if (nd.first_child < 0) return;

        if (nd.layout_dir == LAYOUT_ABS) {
            float dx = cb.x - nd.x * sx;
            float dy = cb.y - nd.y * sy;
            for (int16_t c = nd.first_child; c >= 0; c = bp.nodes[static_cast<uint32_t>(c)].next_sibling) {
                const SvgBlueprintNode &cn = bp.nodes[static_cast<uint32_t>(c)];
                layout_subtree(bp, store, c, cn.x * sx + dx, cn.y * sy + dy - scroll_off, cn.w * sx, cn.h * sy, child_clip, sx, sy);
            }
            return;
        }

        // Two-pass Flex layout (row / col)
        bool is_row = (nd.layout_dir == LAYOUT_ROW);
        float pad = nd.pad * (is_row ? sx : sy);
        float gap = nd.gap * (is_row ? sx : sy);
        float inner_w = std::max(0.0f, cb.w - 2.0f * pad);
        float inner_h = std::max(0.0f, cb.h - 2.0f * pad);
        float inner_main = is_row ? inner_w : inner_h;
        float inner_cross = is_row ? inner_h : inner_w;

        uint32_t cnt = 0;
        float sum_fixed = 0.0f;
        float sum_grow = 0.0f;
        for (int16_t c = nd.first_child; c >= 0; c = bp.nodes[static_cast<uint32_t>(c)].next_sibling) {
            const SvgBlueprintNode &cn = bp.nodes[static_cast<uint32_t>(c)];
            float base_m = is_row ? (cn.w * sx) : (cn.h * sy);
            sum_fixed += base_m;
            sum_grow += cn.flex_grow;
            ++cnt;
        }
        float total_gaps = (cnt > 1u) ? (static_cast<float>(cnt - 1u) * gap) : 0.0f;
        cb.content_h = is_row ? cb.h : (sum_fixed + total_gaps + 2.0f * pad);
        cb.max_scroll_y = std::max(0.0f, cb.content_h - cb.h);

        float free_space = std::max(0.0f, inner_main - sum_fixed - total_gaps);
        float cursor = (is_row ? (cb.x + pad) : (cb.y + pad - scroll_off));
        float step_gap = gap;
        if (sum_grow == 0.0f && free_space > 0.0f) {
            if (nd.justify == JUST_CENTER) cursor += free_space * 0.5f;
            else if (nd.justify == JUST_END) cursor += free_space;
            else if (nd.justify == JUST_BETWEEN && cnt > 1u) step_gap += free_space / static_cast<float>(cnt - 1u);
        }

        for (int16_t c = nd.first_child; c >= 0; c = bp.nodes[static_cast<uint32_t>(c)].next_sibling) {
            const SvgBlueprintNode &cn = bp.nodes[static_cast<uint32_t>(c)];
            float c_main = is_row ? (cn.w * sx) : (cn.h * sy);
            if (sum_grow > 0.0f && cn.flex_grow > 0.0f) {
                c_main += free_space * (cn.flex_grow / sum_grow);
            }
            float c_cross = is_row ? (cn.h * sy) : (cn.w * sx);
            if (nd.align_items == ALIGN_STRETCH || c_cross <= 0.0f) c_cross = inner_cross;
            float cross_pos = is_row ? (cb.y + pad) : (cb.x + pad);
            if (nd.align_items == ALIGN_CENTER) cross_pos += 0.5f * (inner_cross - c_cross);
            else if (nd.align_items == ALIGN_END) cross_pos += (inner_cross - c_cross);

            float cx = is_row ? cursor : cross_pos;
            float cy = is_row ? cross_pos : cursor;
            float cw = is_row ? c_main : c_cross;
            float ch = is_row ? c_cross : c_main;

            // Temporarily set scaled dims for leaf or child flex group
            ComputedBox &ccb = boxes[static_cast<uint32_t>(c)];
            layout_subtree(bp, store, c, cx, cy, cw, ch, child_clip, sx, sy);
            // Honor parent-computed flex stretch overrides:
            ccb.x = cx; ccb.y = cy; ccb.w = cw; ccb.h = ch;
            cursor += c_main + step_gap;
        }
    }

    uint32_t compute(const BloomSvgBlueprint &bp, const BloomStateStore &store, float vp_w, float vp_h, bool force = false) {
        if (!layout_dirty && !force) return 0u;
        clip_count = 1;
        clips[0] = {0.0f, 0.0f, vp_w, vp_h};
        float sx = vp_w / bp.vb_w;
        float sy = vp_h / bp.vb_h;
        for (uint32_t i = 0; i < bp.node_count; ++i) {
            if (bp.nodes[i].parent_idx < 0) {
                layout_subtree(bp, store, static_cast<int32_t>(i),
                               bp.nodes[i].x * sx, bp.nodes[i].y * sy,
                               bp.nodes[i].w * sx, bp.nodes[i].h * sy, 0u, sx, sy);
            }
        }
        layout_dirty = false;
        return bp.node_count;
    }
};

// Interactive Hit-Test (Reverse Paint-Order + Clip Intersect + Modal Trap)
static uint64_t hit_test_reverse_z(const BloomSvgBlueprint &bp,
                                   const BloomLayoutEngine &layout,
                                   const BloomStateStore &store,
                                   float mx, float my) {
    bool modal_active = (store.modal_root_key != 0ULL);
    for (int32_t i = static_cast<int32_t>(bp.node_count) - 1; i >= 0; --i) {
        const SvgBlueprintNode &nd = bp.nodes[static_cast<uint32_t>(i)];
        if (nd.kind == NODE_GROUP || nd.kind == NODE_CANVAS) continue;
        if ((nd.action_caps & CAP_DISABLED) != 0u) continue;
        if (modal_active && (nd.action_caps & CAP_MODAL) == 0u) {
            // Check ancestor modal root
            bool in_modal = false;
            int16_t p = nd.parent_idx;
            while (p >= 0) {
                if (bp.nodes[static_cast<uint32_t>(p)].node_key == store.modal_root_key) { in_modal = true; break; }
                p = bp.nodes[static_cast<uint32_t>(p)].parent_idx;
            }
            if (!in_modal) continue;
        }
        const ComputedBox &cb = layout.boxes[static_cast<uint32_t>(i)];
        if (!cb.visible) continue;
        const BloomClipRect &cr = layout.clips[cb.clip_id];
        if (mx < cr.x || mx > cr.x + cr.w || my < cr.y || my > cr.y + cr.h) continue;
        if (mx >= cb.x && mx <= cb.x + cb.w && my >= cb.y && my <= cb.y + cb.h) {
            return nd.node_key;
        }
    }
    return 0ULL;
}

static uint32_t step_pointer_interaction(const BloomSvgBlueprint &bp,
                                         BloomLayoutEngine &layout,
                                         BloomStateStore &store,
                                         float mx, float my,
                                         bool mouse_down, bool just_clicked) {
    uint32_t fired_op = 0u;
    float drag_dx = mx - store.last_cursor_x;
    store.last_cursor_x = mx;

    if (!mouse_down) {
        store.captured_key = 0ULL;
        store.active_key = 0ULL;
    }

    if (mouse_down && store.captured_key != 0ULL) {
        int32_t idx = bp.find_by_key(store.captured_key);
        if (idx >= 0) {
            const SvgBlueprintNode &nd = bp.nodes[static_cast<uint32_t>(idx)];
            const ComputedBox &cb = layout.boxes[static_cast<uint32_t>(idx)];
            if (NodeRuntimeState *s = store.find_slot(store.captured_key)) {
                float span = std::max(cb.w, 16.0f);
                float next_v = s->bound_value + drag_dx / span;
                if (next_v < 0.0f) next_v = 0.0f;
                if (next_v > 1.0f) next_v = 1.0f;
                s->bound_value = next_v;
                if (nd.bind_channel >= 0) store.bind_channels[nd.bind_channel] = next_v;
                fired_op = nd.action_caps & 0xFFFFu;
            }
        }
    }

    uint64_t hit_k = hit_test_reverse_z(bp, layout, store, mx, my);
    store.hovered_key = hit_k;

    if (just_clicked && hit_k != 0ULL) {
        int32_t idx = bp.find_by_key(hit_k);
        if (idx >= 0) {
            const SvgBlueprintNode &nd = bp.nodes[static_cast<uint32_t>(idx)];
            store.active_key = hit_k;
            if ((nd.action_caps & CAP_SCRUB) != 0u) store.captured_key = hit_k;
            if ((nd.action_caps & CAP_FOCUSABLE) != 0u) store.focused_key = hit_k;
            if ((nd.action_caps & CAP_TOGGLE) != 0u) {
                if (NodeRuntimeState *s = store.find_slot(hit_k)) {
                    s->checked = !s->checked;
                    s->bound_value = s->checked ? 1.0f : 0.0f;
                }
            }
            fired_op = nd.action_caps & 0xFFFFu;
        }
    }
    return fired_op;
}

static uint32_t agent_act(const BloomSvgBlueprint &bp,
                          BloomStateStore &store,
                          uint64_t target_key,
                          AgentActKind kind,
                          float val,
                          bool allow_guarded) {
    if (kind == ACT_ROVE_NEXT || kind == ACT_ROVE_PREV) {
        int32_t cur_pos = -1;
        int32_t elig[256];
        int32_t ecnt = 0;
        for (uint32_t i = 0; i < bp.node_count && ecnt < 256; ++i) {
            const SvgBlueprintNode &nd = bp.nodes[i];
            if ((nd.action_caps & CAP_DISABLED) != 0u) continue;
            if (store.modal_root_key != 0ULL && (nd.action_caps & CAP_MODAL) == 0u) continue;
            if ((nd.action_caps & CAP_FOCUSABLE) == 0u) continue;
            if (nd.node_key == store.focused_key) cur_pos = ecnt;
            elig[ecnt++] = static_cast<int32_t>(i);
        }
        if (ecnt == 0) return STATUS_NOT_FOUND;
        int32_t nxt = (kind == ACT_ROVE_NEXT)
            ? ((cur_pos >= 0) ? (cur_pos + 1) % ecnt : 0)
            : ((cur_pos >= 0) ? (cur_pos + ecnt - 1) % ecnt : ecnt - 1);
        store.focused_key = bp.nodes[static_cast<uint32_t>(elig[nxt])].node_key;
        return STATUS_OK;
    }

    int32_t idx = bp.find_by_key(target_key);
    if (idx < 0) return STATUS_NOT_FOUND;
    const SvgBlueprintNode &nd = bp.nodes[static_cast<uint32_t>(idx)];
    if ((nd.action_caps & CAP_DISABLED) != 0u) return STATUS_DISABLED;
    if (store.modal_root_key != 0ULL && (nd.action_caps & CAP_MODAL) == 0u) {
        bool in_modal = (nd.node_key == store.modal_root_key);
        int16_t p = nd.parent_idx;
        while (!in_modal && p >= 0) {
            if (bp.nodes[static_cast<uint32_t>(p)].node_key == store.modal_root_key) in_modal = true;
            p = bp.nodes[static_cast<uint32_t>(p)].parent_idx;
        }
        if (!in_modal) return STATUS_MODAL_TRAP;
    }
    if ((nd.action_caps & CAP_AGENT_GUARD) != 0u && !allow_guarded) {
        if (!store.guard_fn || !store.guard_fn(target_key, nd.action_caps & 0xFFFFu, val, store.guard_ctx)) {
            return STATUS_VETOED_GUARD;
        }
    }
    NodeRuntimeState *s = store.find_slot(target_key);
    if (!s) return STATUS_NOT_FOUND;
    if (kind == ACT_SET_VALUE) {
        s->bound_value = val < 0.0f ? 0.0f : (val > 1.0f ? 1.0f : val);
        if (nd.bind_channel >= 0) store.set_bind_channel(bp, static_cast<uint32_t>(nd.bind_channel), s->bound_value);
    } else if (kind == ACT_TOGGLE) {
        s->checked = !s->checked;
        s->bound_value = s->checked ? 1.0f : 0.0f;
    } else if (kind == ACT_CLICK) {
        s->spring_vel = -2.5f;
        if ((nd.action_caps & CAP_TOGGLE) != 0u) { s->checked = !s->checked; }
    } else if (kind == ACT_FOCUS) {
        store.focused_key = target_key;
    }
    return STATUS_OK;
}

static size_t export_snapshot_json(const BloomSvgBlueprint &bp, const BloomStateStore &store, char *out, size_t cap) {
    size_t pos = 0;
    auto app = [&](const char *s, size_t n) {
        if (pos + n < cap) std::memcpy(out + pos, s, n);
        pos += n;
    };
    char hdr[160];
    int hn = std::snprintf(hdr, sizeof(hdr),
                           "{\"epoch\":%u,\"nodes\":%u,\"focused\":%llu,\"captured\":%llu,\"items\":[",
                           store.epoch, bp.node_count,
                           static_cast<unsigned long long>(store.focused_key),
                           static_cast<unsigned long long>(store.captured_key));
    if (hn > 0) app(hdr, static_cast<size_t>(hn));
    uint32_t lim = bp.node_count > 64u ? 64u : bp.node_count;
    for (uint32_t i = 0; i < lim; ++i) {
        const SvgBlueprintNode &nd = bp.nodes[i];
        const NodeRuntimeState *s = store.find_slot_const(nd.node_key);
        char item[160];
        int in = std::snprintf(item, sizeof(item),
                               "%s{\"id\":\"%.*s\",\"val\":%.3f,\"chk\":%d}",
                               (i > 0u ? "," : ""),
                               static_cast<int>(nd.str_id_len > 24 ? 24 : nd.str_id_len),
                               bp.str_ptr(nd.str_id_off, nd.str_id_len),
                               s ? s->bound_value : nd.init_val,
                               (s && s->checked) ? 1 : 0);
        if (in > 0) app(item, static_cast<size_t>(in));
    }
    app("]}", 2);
    size_t w = (pos < cap) ? pos : (cap - 1u);
    out[w] = '\0';
    return w;
}

// ============================================================================
// Frame Packer -> Direct WriteCombined Ring Emission (Zero CPU Readback!)
// ============================================================================
struct PackedFrameCounts {
    uint32_t opaque_quads = 0;
    uint32_t alpha_quads = 0;
    uint32_t shadows = 0;
    uint32_t glyphs = 0;
    uint32_t vlist_rows = 0;
    size_t   wc_bytes = 0;
};

static uint8_t compute_state_mask(const SvgBlueprintNode &nd, const NodeRuntimeState *s, const BloomStateStore &store) {
    uint8_t m = CSS_NONE;
    if ((nd.action_caps & CAP_DISABLED) != 0u) return CSS_DISABLED;
    if (store.hovered_key == nd.node_key) m |= CSS_HOVER;
    if (store.active_key == nd.node_key) m |= CSS_ACTIVE;
    if (store.focused_key == nd.node_key) m |= CSS_FOCUS;
    if (s && s->checked) m |= CSS_CHECKED;
    return m;
}

static PackedFrameCounts pack_svg_frame_wc(const BloomSvgBlueprint &bp,
                                           const BloomLayoutEngine &layout,
                                           BloomStateStore &store,
                                           float dt,
                                           float vp_w, float vp_h,
                                           BloomQuadHot *out_opq, uint32_t max_opq,
                                           BloomQuadHot *out_alp, uint32_t max_alp,
                                           BloomShadowHot *out_sh, uint32_t max_sh,
                                           BloomGlyphInst *out_gl, uint32_t max_gl,
                                           bool emit_sample_text) {
    PackedFrameCounts c{};
    const float scale_px = vp_w / bp.vb_w;

    for (uint32_t i = 0; i < bp.node_count; ++i) {
        const SvgBlueprintNode &nd = bp.nodes[i];
        const ComputedBox &cb = layout.boxes[i];
        if (!cb.visible) continue;

        NodeRuntimeState *s = store.find_slot(nd.node_key);
        uint8_t st_mask = compute_state_mask(nd, s, store);
        // Step 2-Jet fp32 damped tactile spring
        float sp_scale = 1.0f;
        if (s && (nd.action_caps & CAP_FOCUSABLE)) {
            float target = (st_mask & CSS_ACTIVE) ? 0.94f : ((st_mask & CSS_HOVER) ? 1.06f : 1.0f);
            float accel = (target - s->spring_pos) * 320.0f - s->spring_vel * 24.0f;
            s->spring_vel += accel * dt;
            s->spring_pos += s->spring_vel * dt;
            sp_scale = s->spring_pos;
        }

        // Virtual list row expansion directly into GPU buffers (O(visible rows))
        if (nd.virtual_list && nd.virtual_total_count > 0u) {
            float scroll_y = s ? s->scroll_y : 0.0f;
            float row_h = nd.virtual_item_h * (vp_h / bp.vb_h);
            uint32_t first_row = static_cast<uint32_t>(std::max(0.0f, scroll_y / std::max(row_h, 1.0f)));
            uint32_t vis_rows = static_cast<uint32_t>(std::ceil(cb.h / std::max(row_h, 1.0f))) + 1u;
            if (first_row + vis_rows > nd.virtual_total_count) {
                vis_rows = (nd.virtual_total_count > first_row) ? (nd.virtual_total_count - first_row) : 0u;
            }
            for (uint32_t r = 0; r < vis_rows; ++r) {
                uint32_t row_id = first_row + r;
                float ry = cb.y + (static_cast<float>(row_id) * row_h - scroll_y);
                if (c.opaque_quads < max_opq) {
                    uint32_t fill = (row_id & 1u) ? 0xFF282018u : 0xFF2E261Eu;
                    out_opq[c.opaque_quads++] = {
                        {pack_q14_2(cb.x + 2.0f), pack_q14_2(ry), pack_q14_2(cb.w - 4.0f), pack_q14_2(row_h - 2.0f)},
                        fill, 0u, {0,0,0,0}, {0,0,0,0}, cb.clip_id, 0u, 2u + i
                    };
                }
                for (uint32_t ch = 0; ch < 18u && c.glyphs < max_gl; ++ch) {
                    out_gl[c.glyphs++] = {
                        {pack_q14_2(cb.x + 12.0f + ch * 14.0f), pack_q14_2(ry + 6.0f), 48u, 96u},
                        {static_cast<uint16_t>(((row_id + ch) & 63u) * 896u), 0u, 768u, 1536u},
                        0xFFEAE2DCu, cb.clip_id, static_cast<uint16_t>(10u + i)
                    };
                }
                ++c.vlist_rows;
            }
            continue;
        }

        // CSS pseudo-state variant lookup
        uint32_t fill_rgb = nd.fill_rgb;
        float fill_op = nd.fill_opacity;
        uint32_t stroke_rgb = nd.stroke_rgb;
        float stroke_w = nd.stroke_w;
        float rx = nd.rx * scale_px;
        if (st_mask != CSS_NONE && nd.class_len > 0) {
            const char *cls = bp.str_ptr(nd.class_off, nd.class_len);
            for (uint32_t r = 0; r < bp.rule_count; ++r) {
                const CssStyleRule &rule = bp.rules[r];
                if (rule.state_mask == CSS_NONE || (st_mask & rule.state_mask) != rule.state_mask) continue;
                const char *p = cls, *e = cls + nd.class_len;
                bool m = false;
                while (p < e) {
                    while (p < e && sv_space(*p)) ++p;
                    const char *w = p;
                    while (p < e && !sv_space(*p)) ++p;
                    if (p > w && fnv1a64(w, static_cast<size_t>(p - w)) == rule.selector_hash) { m = true; break; }
                }
                if (m) {
                    if (rule.has_fill) fill_rgb = rule.fill_rgb;
                    if (rule.has_opacity) fill_op = rule.fill_opacity;
                    if (rule.has_stroke) stroke_rgb = rule.stroke_rgb;
                    if (rule.has_stroke_w) stroke_w = rule.stroke_w;
                }
            }
        }

        if (nd.has_shadow && c.shadows < max_sh) {
            float blur = std::max(nd.shadow_blur * scale_px, 4.0f);
            float sv = blur * 1.4142135f;
            float pad = blur * 2.5f;
            float r1 = std::min(std::sqrt(rx * rx + 4.0f * sv * sv), 0.5f * std::min(cb.w, cb.h));
            float r0 = std::min(std::sqrt(rx * rx + 1.3225f * sv * sv), 0.5f * std::min(cb.w, cb.h));
            out_sh[c.shadows++] = {
                {pack_q14_2(cb.x + nd.shadow_dx * scale_px - pad),
                 pack_q14_2(cb.y + nd.shadow_dy * scale_px - pad),
                 pack_q14_2(cb.w + 2.0f * pad),
                 pack_q14_2(cb.h + 2.0f * pad)},
                {f32_to_f16_hw(cb.w * 0.5f), f32_to_f16_hw(cb.h * 0.5f), f32_to_f16_hw(1.0f / sv), f32_to_f16_hw(std::min(cb.w, cb.h))},
                {f32_to_f16_hw(r1), f32_to_f16_hw(2.0f * r1 / std::max(r0, 1.0f)), f32_to_f16_hw(0.5f), f32_to_f16_hw(rx)},
                nd.shadow_rgba, cb.clip_id, static_cast<uint16_t>(1u + i)
            };
        }

        if (nd.kind == NODE_RECT || nd.kind == NODE_CIRCLE) {
            float dw = cb.w * sp_scale;
            float dh = cb.h * sp_scale;
            float dx = cb.x - 0.5f * (dw - cb.w);
            float dy = cb.y - 0.5f * (dh - cb.h);
            bool opaque = (fill_op >= 0.99f && rx <= 0.1f && stroke_w <= 0.1f && nd.kind == NODE_RECT);
            if (opaque && c.opaque_quads < max_opq) {
                out_opq[c.opaque_quads++] = {
                    {pack_q14_2(dx), pack_q14_2(dy), pack_q14_2(dw), pack_q14_2(dh)},
                    rgba_with_opacity(fill_rgb, fill_op), 0u,
                    {0,0,0,0}, {0,0,0,0}, cb.clip_id, nd.prim_type, 2u + i
                };
            } else if (!opaque && c.alpha_quads < max_alp) {
                uint8_t rq = pack_q6_2(rx);
                uint8_t bq = pack_q6_2(stroke_w * scale_px);
                out_alp[c.alpha_quads++] = {
                    {pack_q14_2(dx), pack_q14_2(dy), pack_q14_2(dw), pack_q14_2(dh)},
                    rgba_with_opacity(fill_rgb, fill_op),
                    rgba_with_opacity(stroke_rgb, (st_mask & CSS_FOCUS) ? 1.0f : 0.85f),
                    {rq, rq, rq, rq}, {bq, bq, bq, bq},
                    cb.clip_id, nd.prim_type, 4u + i
                };
            }
            if (nd.label_len > 0u && c.glyphs < max_gl) {
                uint32_t gl_cnt = std::min<uint32_t>(nd.label_len, max_gl - c.glyphs);
                float gx0 = dx + 14.0f;
                float gy0 = dy + 0.5f * std::max(0.0f, dh - 24.0f);
                const char *txt = bp.str_ptr(nd.label_off, nd.label_len);
                for (uint32_t g = 0; g < gl_cnt; ++g) {
                    uint8_t ch = static_cast<uint8_t>(txt[g]);
                    out_gl[c.glyphs++] = {
                        {pack_q14_2(gx0 + g * 14.0f), pack_q14_2(gy0), 48u, 96u},
                        {static_cast<uint16_t>((ch & 63u) * 896u), 0u, 768u, 1536u},
                        0xFFE8F4FFu, cb.clip_id, static_cast<uint16_t>(8u + i)
                    };
                }
            }
        } else if (nd.kind == NODE_TEXT && nd.label_len > 0u && c.glyphs < max_gl) {
            uint32_t gl_cnt = std::min<uint32_t>(nd.label_len, max_gl - c.glyphs);
            const char *txt = bp.str_ptr(nd.label_off, nd.label_len);
            for (uint32_t g = 0; g < gl_cnt; ++g) {
                uint8_t ch = static_cast<uint8_t>(txt[g]);
                out_gl[c.glyphs++] = {
                    {pack_q14_2(cb.x + g * 14.0f), pack_q14_2(cb.y), 48u, 96u},
                    {static_cast<uint16_t>((ch & 63u) * 896u), 0u, 768u, 1536u},
                    rgba_with_opacity(fill_rgb, fill_op), cb.clip_id, static_cast<uint16_t>(8u + i)
                };
            }
        } else if (nd.kind == NODE_PATH && c.glyphs < max_gl) {
            uint16_t gid = static_cast<uint16_t>((nd.glyph_id - 1u) & 7u);
            out_gl[c.glyphs++] = {
                {pack_q14_2(cb.x), pack_q14_2(cb.y), pack_q14_2(std::max(cb.w, 24.0f)), pack_q14_2(std::max(cb.h, 24.0f))},
                {static_cast<uint16_t>(gid * 4096u), 32768u, 4096u, 4096u},
                rgba_with_opacity(nd.stroke_w > 0.0f ? stroke_rgb : fill_rgb, 0.96f),
                cb.clip_id, static_cast<uint16_t>(8u + i)
            };
        }
    }

    if (emit_sample_text) {
        // Fill remaining realistic shaped IDE glyph budget (up to max_gl - c.glyphs) for 4K IDE stress parity
        while (c.glyphs < max_gl) {
            uint32_t g = c.glyphs;
            out_gl[c.glyphs++] = {
                {pack_q14_2(760.0f + static_cast<float>((g % 180u) * 14u)),
                 pack_q14_2(120.0f + static_cast<float>(((g / 180u) % 68u) * 26u)), 48u, 96u},
                {static_cast<uint16_t>((g & 63u) * 896u), 0u, 768u, 1536u},
                0xFFE0E8F0u, 0u, 200u
            };
        }
    }

    // Emit opaque quads Front-to-Back for Apple M1 TBDR HSR & Early-Z occlusion of root background
    if (c.opaque_quads > 1u) {
        std::reverse(out_opq, out_opq + c.opaque_quads);
    }

    c.wc_bytes = c.opaque_quads * sizeof(BloomQuadHot) +
                 c.alpha_quads * sizeof(BloomQuadHot) +
                 c.shadows * sizeof(BloomShadowHot) +
                 c.glyphs * sizeof(BloomGlyphInst);
    return c;
}

// ============================================================================
// CPU Scanline Rasterizer for SVG <path> Quadratic Curves -> Private R8 Atlas
// ============================================================================
static void rasterize_curves_r8(const SvgQuadCurveSeg *curves, uint32_t curve_count,
                                float vb_size, uint32_t tex_sz, uint8_t *out_r8) {
    // Rasterize closed filled + stroked shape using signed winding coverage with 4x4 supersampling
    std::memset(out_r8, 0, tex_sz * tex_sz);
    float inv_scale = vb_size / static_cast<float>(tex_sz);
    // Flatten quadratics to small polyline once for 4x4 subpixel scanline fill
    struct Seg { float x0, y0, x1, y1; };
    std::vector<Seg> segs;
    segs.reserve(curve_count * 6u);
    for (uint32_t c = 0; c < curve_count; ++c) {
        const SvgQuadCurveSeg &q = curves[c];
        float px = q.p0x, py = q.p0y;
        const int STEPS = 6;
        for (int s = 1; s <= STEPS; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(STEPS);
            float u = 1.0f - t;
            float nx = u * u * q.p0x + 2.0f * u * t * q.cx + t * t * q.p1x;
            float ny = u * u * q.p0y + 2.0f * u * t * q.cy + t * t * q.p1y;
            segs.push_back({px, py, nx, ny});
            px = nx; py = ny;
        }
    }

    for (uint32_t py = 0; py < tex_sz; ++py) {
        for (uint32_t px = 0; px < tex_sz; ++px) {
            uint32_t hits = 0;
            for (int sy = 0; sy < 4; ++sy) {
                float y = (static_cast<float>(py) + (sy + 0.5f) * 0.25f) * inv_scale;
                for (int sx = 0; sx < 4; ++sx) {
                    float x = (static_cast<float>(px) + (sx + 0.5f) * 0.25f) * inv_scale;
                    int winding = 0;
                    for (const Seg &sg : segs) {
                        bool c0 = (sg.y0 <= y);
                        bool c1 = (sg.y1 <= y);
                        if (c0 != c1) {
                            float ix = sg.x0 + (y - sg.y0) * (sg.x1 - sg.x0) / (sg.y1 - sg.y0);
                            if (x < ix) winding += (c0 ? 1 : -1);
                        }
                    }
                    if (winding != 0) ++hits;
                }
            }
            out_r8[py * tex_sz + px] = static_cast<uint8_t>((hits * 255u) / 16u);
        }
    }
}

// ============================================================================
// Embedded MSL Shaders (v9 Compute Fidelity vs BLOOM vNext TBDR Refetch20B)
// ============================================================================
static const char *kSvgBenchMetal = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct V9UiNodeGpu {
    uint node_id, prim_type, glass_style, action_caps;
    float cx, cy, hw, hh;
    float rx, bound_v, spring_v, pad0;
    uint fill_rgba, tile_lo, tile_hi, flags;
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

struct BloomClipRect {
    float4 rect;
};

struct SlugCurveGpu {
    float2 p0, p1, a, b;
    float sign;
    uint pad;
};

struct SlugTileGpu {
    uint paint_idx, curve_start, curve_count;
    int backdrop;
    float2 tl, br, ref_pt, ctl, cbr;
};

inline float2 to_ndc(float2 px, float2 vp) {
    float2 n = (px / vp) * 2.0f - 1.0f;
    return float2(n.x, -n.y);
}

inline half erf7_h(half x) {
    half s = sign(x);
    half a = abs(x);
    half p = 1.0h + a * (0.278393h + a * (0.230389h + a * (0.000972h + a * 0.078108h)));
    half p2 = p * p;
    return s * (1.0h - 1.0h / (p2 * p2));
}

kernel void k_dvfs_warm(device const float4 *in_a [[buffer(0)]],
                        device float4 *out_b [[buffer(1)]],
                        constant uint &iters [[buffer(2)]],
                        uint gid [[thread_position_in_grid]]) {
    float4 acc = in_a[gid & 1023u];
    for (uint i = 0; i < iters; ++i) acc = fma(acc, float4(1.0001f), float4(0.0002f));
    out_b[gid & 1023u] = acc;
}

// Compute checksum reduction over output texture (verifies non-zero rasterization without CPU WC readback)
kernel void k_tex_checksum(texture2d<half, access::read> tex [[texture(0)]],
                           device atomic_uint *out_acc [[buffer(0)]],
                           uint2 gid [[thread_position_in_grid]]) {
    uint w = tex.get_width(), h = tex.get_height();
    uint x = gid.x * 16u, y = gid.y * 16u;
    if (x >= w || y >= h) return;
    half4 p = tex.read(uint2(x, y));
    uint q = uint(clamp(p.r * 255.0h, 0.0h, 255.0h)) +
             (uint(clamp(p.g * 255.0h, 0.0h, 255.0h)) << 8u) +
             (uint(clamp(p.b * 255.0h, 0.0h, 255.0h)) << 16u);
    if (q != 0u) atomic_fetch_add_explicit(out_acc, q, memory_order_relaxed);
}

// Faithful port of archive/v9/shaders/bloom_core.wgsl: bloom_ui_overlay
kernel void k_v9_bloom_ui_overlay(texture2d<half, access::read> scene_hdr [[texture(0)]],
                                  texture2d<half, access::write> out_tex [[texture(1)]],
                                  device const V9UiNodeGpu *nodes [[buffer(0)]],
                                  constant uint &node_count [[buffer(1)]],
                                  constant uint2 &dom_mask [[buffer(2)]],
                                  uint2 gid [[thread_position_in_grid]]) {
    uint w = out_tex.get_width();
    uint h = out_tex.get_height();
    if (gid.x >= w || gid.y >= h) return;

    float2 fw = float2(float(w), float(h));
    float2 px = float2(gid) + 0.5f;
    float2 norm_xy = (px / fw) * 2.0f - 1.0f;
    float aspect = fw.x / fw.y;

    uint wc = uint(clamp((norm_xy.x + 1.0f) * 4.0f, 0.0f, 7.0f));
    uint wr = uint(clamp((norm_xy.y + 1.0f) * 4.0f, 0.0f, 7.0f));
    uint bit_idx = wr * 8u + wc;
    uint word = (bit_idx < 32u) ? dom_mask.x : dom_mask.y;
    bool dom_hit = ((word >> (bit_idx & 31u)) & 1u) != 0u;

    half3 col = scene_hdr.read(gid).rgb;
    if (dom_hit) {
        for (uint i = 0u; i < node_count; ++i) {
            V9UiNodeGpu nd = nodes[i];
            uint nmask = (bit_idx < 32u) ? nd.tile_lo : nd.tile_hi;
            if (((nmask >> (bit_idx & 31u)) & 1u) == 0u) continue;

            float2 center = float2(nd.cx, nd.cy);
            float2 hsz = float2(nd.hw, nd.hh) * nd.spring_v;
            float2 lp = norm_xy - center;
            if (abs(lp.x) > hsz.x + 0.02f || abs(lp.y) > hsz.y + 0.02f) continue;

            half3 fill_c = unpack_unorm4x8_to_half(nd.fill_rgba).rgb;
            if (nd.prim_type == 2u) {
                float2 lp_iso = float2(lp.x * aspect, lp.y);
                float r_mid = 0.75f * hsz.x * aspect;
                float d_ring = abs(length(lp_iso) - r_mid) - 0.008f;
                float aa = 2.0f / fw.y;
                if (d_ring < aa * 1.5f) {
                    half a = half(1.0f - smoothstep(-aa * 0.5f, aa * 1.2f, d_ring));
                    col = mix(col, fill_c * 1.18h, a * 0.94h);
                }
            } else {
                float2 qd = abs(lp) - max(hsz - float2(nd.rx), float2(0.001f));
                float sdf = min(max(qd.x, qd.y), 0.0f) + length(max(qd, float2(0.0f))) - nd.rx;
                if (sdf < 0.0035f) {
                    half bevel = half(1.0f - clamp(-sdf * 140.0f, 0.0f, 1.0f));
                    half3 trans = clamp(half3(1.0h) - (half3(1.0h) - fill_c) * (0.22h + 0.52h * bevel), half3(0.16h), half3(1.0h));
                    half3 glass = col * trans * 0.50h + fill_c * (0.27h + 0.24h * bevel);
                    if (nd.prim_type == 4u && sdf < -0.0015f) {
                        float prog = (lp.x + hsz.x) / max(2.0f * hsz.x, 1e-4f);
                        if (prog <= nd.bound_v) glass = mix(glass, fill_c * 0.70h, 0.68h);
                    }
                    col = mix(col, glass, (sdf < 0.0f) ? 0.85h : 0.0h);
                    half rim = half(1.0f - smoothstep(0.0f, 0.003f, abs(sdf)));
                    col = mix(col, fill_c * 1.15h, rim * 0.86h);
                }
            }
        }
    }
    out_tex.write(half4(col, 1.0h), gid);
}

// BLOOM vNext TBDR Specialized Short Shaders (Refetch20B, 1024 Occupancy)
struct BloomOpaqueIn {
    float4 pos [[position]];
    half4 fill [[flat]];
};

struct BloomRefetch20In {
    float4 pos [[position]];
    uint idx [[flat]];
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
    float z = 1.0f - clamp(float(q.z_flags & 0xFFFFu) / 65535.0f, 0.0001f, 0.9999f);
    return BloomOpaqueIn{float4(to_ndc(px, viewport), z, 1.0f), unpack_unorm4x8_to_half(q.rgba8_fill)};
}

fragment half4 f_bloom_opaque_32(BloomOpaqueIn in [[stage_in]]) {
    return in.fill;
}

vertex BloomRefetch20In v_bloom_quad32_refetch20(uint vid [[vertex_id]],
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
    float z = 1.0f - clamp(float(q.z_flags & 0xFFFFu) / 65535.0f, 0.0001f, 0.9999f);
    return BloomRefetch20In{float4(to_ndc(px, viewport), z, 1.0f), iid};
}

fragment half4 f_bloom_quad32_refetch20(BloomRefetch20In in [[stage_in]],
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
    return BloomRefetch20In{float4(to_ndc(px, viewport), 1.0f - clamp(float(sh.z_flags) / 65535.0f, 0.0001f, 0.9999f), 1.0f), iid};
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
    half alpha = sc * (erf7_h(sh.shape_params.z * (sh.shape_params.w + d)) - erf7_h(sh.shape_params.z * d));
    half4 col = unpack_unorm4x8_to_half(sh.rgba8);
    col.a *= alpha;
    return col;
}

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
    return BloomRefetch20In{float4(to_ndc(px, viewport), 1.0f - clamp(float(g.z_flags) / 65535.0f, 0.0001f, 0.9999f), 1.0f), iid};
}

fragment half4 f_bloom_glyph_refetch20(BloomRefetch20In in [[stage_in]],
                                       device const BloomGlyphInst *glyphs [[buffer(0)]],
                                       texture2d<half> atlas [[texture(0)]]) {
    BloomGlyphInst g = glyphs[in.idx];
    float4 b = float4(g.dst_q14_2) * 0.25f;
    float2 local_uv = (in.pos.xy - b.xy) / max(b.zw, float2(1e-4f));
    float4 uvr = float4(g.uv_unorm16) * (1.0f / 65535.0f);
    constexpr sampler smp(mag_filter::linear, min_filter::linear);
    half a = atlas.sample(smp, uvr.xy + local_uv * uvr.zw).r;
    half4 col = unpack_unorm4x8_to_half(g.rgba8);
    col.a *= a;
    return col;
}

// Slug Analytic PathTile Shader
float monotone_quad_root(float a, float b, float c, float dir) {
    float disc = max(b * b - 4.0f * a * c, 0.0f);
    float denom = b + dir * sqrt(disc);
    return (denom == 0.0f) ? 0.0f : saturate(-2.0f * c / denom);
}

vertex BloomRefetch20In v_bloom_slug_refetch20(uint vid [[vertex_id]],
                                               uint iid [[instance_id]],
                                               device const SlugTileGpu *tiles [[buffer(0)]],
                                               constant float2 &viewport [[buffer(1)]]) {
    SlugTileGpu t = tiles[iid];
    float2 pos = float2((vid & 1u) ? t.br.x : t.tl.x, (vid & 2u) ? t.br.y : t.tl.y);
    return BloomRefetch20In{float4(to_ndc(pos, viewport), 0.5f, 1.0f), iid};
}

fragment half4 f_bloom_slug_refetch20(BloomRefetch20In in [[stage_in]],
                                      device const SlugTileGpu *tiles [[buffer(0)]],
                                      device const SlugCurveGpu *curves [[buffer(1)]],
                                      device const uint *paint_rgba8 [[buffer(2)]]) {
    SlugTileGpu tile = tiles[in.idx];
    float2 center = floor(in.pos.xy) + 0.5f;
    half4 col = unpack_unorm4x8_to_half(paint_rgba8[tile.paint_idx]);
    float2 ref_pt = tile.ref_pt;
    float2 winding = float2(float(tile.backdrop));
    float2 weight = float2(0.0f);
    for (uint i = 0u; i < tile.curve_count; ++i) {
        SlugCurveGpu cv = curves[tile.curve_start + i];
        float2 p0 = cv.p0, p1 = cv.p1;
        if (min(p0.x, p1.x) >= center.x + 0.5f) continue;
        if (p0.y < center.y + 0.5f || p1.y < center.y + 0.5f) {
            float min_y = min(p0.y, p1.y), max_y = max(p0.y, p1.y);
            float min_x = min(p0.x, p1.x), max_x = max(p0.x, p1.x);
            if (min_y <= center.y && center.y < max_y) {
                if (max_x <= center.x - 0.5f) {
                    winding.x += cv.sign;
                } else {
                    float t = monotone_quad_root(cv.a.y, cv.b.y, p0.y - center.y, p1.y >= p0.y ? 1.0f : -1.0f);
                    float x = (cv.a.x * t + cv.b.x) * t + p0.x;
                    float d = center.x - x;
                    winding.x += cv.sign * saturate(d + 0.5f);
                    weight.x = max(weight.x, saturate(1.0f - 2.0f * abs(d)));
                }
            }
            if (min_x <= center.x && center.x < max_x) {
                float dir = p1.x > p0.x ? 1.0f : -1.0f;
                if (max_y <= center.y - 0.5f) {
                    winding.y -= cv.sign * dir;
                } else {
                    float t = monotone_quad_root(cv.a.x, cv.b.x, p0.x - center.x, dir);
                    float y = (cv.a.y * t + cv.b.y) * t + p0.y;
                    float d = center.y - y;
                    winding.y -= cv.sign * dir * saturate(d + 0.5f);
                    weight.y = max(weight.y, saturate(1.0f - 2.0f * abs(d)));
                }
            }
            if (p0.x <= ref_pt.x) winding.x -= cv.sign * (float(p0.y <= center.y) - float(p0.y <= ref_pt.y));
            if (p1.x <= ref_pt.x) winding.x += cv.sign * (float(p1.y <= center.y) - float(p1.y <= ref_pt.y));
        }
    }
    float cov = max(abs(dot(winding, weight)) / max(weight.x + weight.y, 1.0f / 65536.0f),
                    min(abs(winding.x), abs(winding.y)));
    col.a *= half(saturate(cov));
    return col;
}
)MSL";

// ============================================================================
// Metal Context & Helpers
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

static id<MTLRenderPipelineState> rpso(Ctx &ctx, const char *vf, const char *ff, bool blend) {
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
    if (!p || err) {
        fprintf(stderr, "render pso err %s/%s: %s\n", vf, ff, err ? err.localizedDescription.UTF8String : "?");
        exit(1);
    }
    return p;
}

static id<MTLTexture> tex2d(id<MTLDevice> dev, uint32_t w, uint32_t h, MTLPixelFormat fmt,
                            MTLTextureUsage usage, MTLStorageMode mode, bool opt) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];
    d.usage = usage;
    d.storageMode = mode;
    d.allowGPUOptimizedContents = opt ? YES : NO;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d];
    if (!t) { fprintf(stderr, "tex2d alloc failed\n"); exit(1); }
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

static uint32_t compute_tex_checksum(Ctx &ctx, id<MTLComputePipelineState> psum, id<MTLTexture> tex) {
    id<MTLBuffer> acc = [ctx.dev newBufferWithLength:4 options:MTLResourceStorageModeShared];
    *static_cast<uint32_t *>(acc.contents) = 0u;
    id<MTLCommandBuffer> c = [ctx.q commandBuffer];
    id<MTLComputeCommandEncoder> e = [c computeCommandEncoder];
    [e setComputePipelineState:psum];
    [e setTexture:tex atIndex:0];
    [e setBuffer:acc offset:0 atIndex:0];
    uint32_t gx = (static_cast<uint32_t>(tex.width) + 15u) / 16u;
    uint32_t gy = (static_cast<uint32_t>(tex.height) + 15u) / 16u;
    [e dispatchThreadgroups:MTLSizeMake((gx + 15u) / 16u, (gy + 15u) / 16u, 1u)
      threadsPerThreadgroup:MTLSizeMake(16u, 16u, 1u)];
    [e endEncoding];
    [c commit];
    [c waitUntilCompleted];
    return *static_cast<uint32_t *>(acc.contents);
}

// ============================================================================
// Synthetic W3C SVG Blueprint Generators (Bootstrap Templates + CSS + Flex)
// ============================================================================
static std::string read_text_file(const char *path) {
    FILE *fp = std::fopen(path, "rb");
    if (!fp) return "";
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::string out(static_cast<size_t>(std::max<long>(sz, 0)), '\0');
    if (sz > 0) {
        size_t nr = std::fread(&out[0], 1, static_cast<size_t>(sz), fp);
        out.resize(nr);
    }
    std::fclose(fp);
    return out;
}

static std::string make_synthetic_svg(uint32_t target_nodes, bool include_virtual_list) {
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1920 1080\" width=\"1920\" height=\"1080\">\n"
       << "  <defs>\n"
       << "    <filter id=\"card_shadow\"><feDropShadow dx=\"0\" dy=\"4\" stdDeviation=\"12\" flood-color=\"#000000\" flood-opacity=\"0.35\"/></filter>\n"
       << "    <clipPath id=\"list_clip\"><rect x=\"40\" y=\"120\" width=\"420\" height=\"896\"/></clipPath>\n"
       << "    <symbol id=\"btn_tpl\" viewBox=\"0 0 160 36\">\n"
       << "      <rect x=\"0\" y=\"0\" width=\"160\" height=\"36\" rx=\"8\" class=\"btn\" fill=\"#20D8FF\" fill-opacity=\"0.24\" stroke=\"#20D8FF\" stroke-width=\"1.5\"/>\n"
       << "      <text x=\"16\" y=\"23\" fill=\"#E8F4FF\" font-size=\"14\">Action</text>\n"
       << "    </symbol>\n"
       << "    <symbol id=\"row_tpl\" viewBox=\"0 0 400 28\">\n"
       << "      <rect x=\"0\" y=\"0\" width=\"400\" height=\"26\" fill=\"#1B2332\"/>\n"
       << "      <text x=\"12\" y=\"18\" fill=\"#B8D4EE\" font-size=\"13\">RowItem</text>\n"
       << "    </symbol>\n"
       << "  </defs>\n"
       << "  <style>\n"
       << "    .panel { fill: #121826; fill-opacity: 0.96; stroke: #28364A; stroke-width: 1; rx: 8; }\n"
       << "    .btn { fill: #1E3248; fill-opacity: 0.88; stroke: #38BDF8; stroke-width: 1.5; rx: 8; }\n"
       << "    .btn:hover { fill: #254868; stroke: #7DD3FC; stroke-width: 2; }\n"
       << "    .btn:active { fill: #0E7490; }\n"
       << "    .btn:focus { stroke: #22D3EE; stroke-width: 2.5; }\n"
       << "    .btn:disabled { fill: #1E2430; fill-opacity: 0.45; stroke: #475569; }\n"
       << "    .toggle[data-checked=\"true\"] { fill: #0EA5E9; stroke: #38BDF8; }\n"
       << "  </style>\n"
       << "  <rect id=\"root_bg\" x=\"0\" y=\"0\" width=\"1920\" height=\"1080\" fill=\"#0A0E17\"/>\n"
       << "  <g id=\"topbar\" data-layout=\"row\" data-gap=\"12\" data-pad=\"12\" data-align=\"center\" x=\"16\" y=\"14\" width=\"1888\" height=\"56\">\n"
       << "    <rect id=\"brand_pill\" data-prim=\"1\" data-label=\"BLOOM STUDIO\" x=\"28\" y=\"22\" width=\"240\" height=\"40\" rx=\"10\" class=\"panel\"/>\n"
       << "    <rect id=\"cmd_input\" data-prim=\"7\" data-action=\"6\" data-focusable=\"true\" data-label=\"Search...\" x=\"280\" y=\"22\" width=\"520\" height=\"40\" rx=\"10\" data-grow=\"1\" class=\"btn\"/>\n"
       << "    <rect id=\"main_slider\" data-prim=\"4\" data-bind=\"0\" data-action=\"1\" data-scrub=\"true\" data-init-val=\"0.5\" data-label=\"THRESHOLD\" x=\"812\" y=\"22\" width=\"260\" height=\"40\" rx=\"10\" class=\"btn\"/>\n"
       << "    <rect id=\"tog_xray\" data-prim=\"1\" data-toggle=\"true\" data-focusable=\"true\" data-label=\"X-RAY\" x=\"1084\" y=\"22\" width=\"120\" height=\"40\" rx=\"10\" class=\"btn toggle\"/>\n"
       << "  </g>\n";

    // Build nested sidebar + multi-column visible Bento cards inside 1920x1080 workspace
    ss << "  <g id=\"workspace\" data-layout=\"row\" data-gap=\"12\" data-pad=\"12\" x=\"16\" y=\"84\" width=\"1888\" height=\"980\">\n";
    if (include_virtual_list) {
        ss << "    <g id=\"vlist_sidebar\" data-scroll=\"y\" data-item-template=\"#row_tpl\" data-item-height=\"28\" data-total-count=\"10000\" x=\"28\" y=\"96\" width=\"420\" height=\"896\"/>\n";
    }
    ss << "    <g id=\"canvas_viewport\" data-role=\"canvas\" x=\"460\" y=\"96\" width=\"240\" height=\"896\"/>\n"
       << "    <g id=\"cards_cols\" data-layout=\"row\" data-gap=\"8\" data-pad=\"4\" x=\"712\" y=\"96\" width=\"1180\" height=\"896\">\n";

    uint32_t generated = 14;
    uint32_t idx = 0;
    static const char *kIcons[5] = {
        "M 12 4 C 16 4 20 8 20 12 C 20 16 16 20 12 20 C 8 20 4 16 4 12 C 4 8 8 4 12 4 Z",
        "M 6 4 L 20 12 L 6 20 Z",
        "M 11 4 Q 18 4 18 11 Q 18 18 11 18 Q 4 18 4 11 Q 4 4 11 4 L 20 20",
        "M 3 6 L 9 6 L 11 8 L 21 8 L 21 19 L 3 19 Z",
        "M 4 12 L 10 18 L 20 6"
    };
    const uint32_t ROWS_PER_COL = 12u;
    uint32_t col_idx = 0;
    while (generated + 5u <= target_nodes) {
        float col_x = 716.0f + static_cast<float>(col_idx * 144u);
        ss << "      <g id=\"col_" << col_idx << "\" data-layout=\"col\" data-gap=\"8\" data-pad=\"2\" x=\""
           << col_x << "\" y=\"100\" width=\"136\" height=\"888\">\n";
        ++generated;
        for (uint32_t r = 0; r < ROWS_PER_COL && generated + 5u <= target_nodes; ++r) {
            float px = col_x + 2.0f;
            float py = 102.0f + static_cast<float>(r * 68u);
            bool has_sh = (idx < 24u);
            ss << "        <g id=\"card_" << idx << "\" x=\"" << px << "\" y=\"" << py << "\" width=\"132\" height=\"60\">\n"
               << "          <rect id=\"w_" << idx << "\" class=\"panel\" x=\"" << px << "\" y=\"" << py << "\" width=\"132\" height=\"60\" rx=\"8\""
               << (has_sh ? " filter=\"url(#card_shadow)\"" : "")
               << " data-bind=\"" << (idx & 15u) << "\" data-focusable=\"true\" data-label=\"N" << idx << "\"/>\n"
               << "          <path id=\"ic_" << idx << "\" data-glyph-id=\"" << ((idx % 5u) + 1u) << "\" d=\"" << kIcons[idx % 5u] << "\" fill=\"#38BDF8\"/>\n"
               << "          <use href=\"#btn_tpl\" id=\"u_" << idx << "\" x=\"" << (px + 28.0f) << "\" y=\"" << (py + 26.0f) << "\" width=\"96\" height=\"26\"/>\n"
               << "        </g>\n";
            generated += 5u;
            ++idx;
        }
        ss << "      </g>\n";
        ++col_idx;
    }
    ss << "    </g>\n  </g>\n</svg>\n";
    return ss.str();
}

// ============================================================================
// Exp 1: Streaming SVG Parse & Hot-Reload Compilation Latency
// ============================================================================
static std::string bench_exp1_parse(BloomSvgBlueprint &bp) {
    printf("\n=== [1/6] svg_parse_and_hot_reload: Streaming W3C SVG Compilation ===\n");
    struct CaseSpec {
        const char *name;
        std::string svg;
        int iters;
    };
    const char *studio_path = "/Users/kuangtalin/Documents/BLOOM/research/probes/fixtures/studio_inspector_v9.svg";
    std::string studio_svg = read_text_file(studio_path);
    if (studio_svg.empty()) {
        fprintf(stderr, "FAILED to read %s\n", studio_path);
        exit(1);
    }

    std::vector<CaseSpec> cases = {
        {"studio_inspector_v9", studio_svg, 200},
        {"bootstrap_dashboard_100", make_synthetic_svg(100, false), 200},
        {"ide_workbench_500", make_synthetic_svg(500, true), 200},
        {"stress_blueprint_2000", make_synthetic_svg(2000, true), 60}
    };

    std::ostringstream js;
    js << "  \"svg_parse_and_hot_reload\": [\n";
    bool studio_labels_ok = false;

    for (size_t ci = 0; ci < cases.size(); ++ci) {
        const CaseSpec &cs = cases[ci];
        std::vector<double> us;
        us.reserve(static_cast<size_t>(cs.iters));
        for (int it = 0; it < cs.iters; ++it) {
            uint64_t t0 = now_ns();
            sv_compile_svg(bp, cs.svg.data(), cs.svg.size());
            uint64_t t1 = now_ns();
            if (it >= 5) us.push_back(static_cast<double>(t1 - t0) / 1e3);
        }
        Stats st = compute_stats(us);
        double mb_s = (static_cast<double>(cs.svg.size()) / (1024.0 * 1024.0)) / (st.median_val / 1e6);

        uint32_t label_cnt = 0, path_cnt = 0;
        bool has_bloom_studio_lbl = false;
        for (uint32_t k = 0; k < bp.node_count; ++k) {
            if (bp.nodes[k].label_len > 0u) {
                ++label_cnt;
                if (std::strcmp(bp.str_ptr(bp.nodes[k].label_off, bp.nodes[k].label_len), "BLOOM STUDIO") == 0) {
                    has_bloom_studio_lbl = true;
                }
            }
            if (bp.nodes[k].kind == NODE_PATH) ++path_cnt;
        }
        if (ci == 0) studio_labels_ok = has_bloom_studio_lbl && (label_cnt >= 6u);

        printf("  %-24s | bytes=%6zu | nodes=%4u | paths=%3u | labels=%4u | med=%6.2f us (p95=%6.2f us) | %6.1f MB/s | arena=%5.1f KB\n",
               cs.name, cs.svg.size(), bp.node_count, path_cnt, label_cnt, st.median_val, st.p95_val, mb_s, bp.memory_bytes() / 1024.0);

        if (ci > 0) js << ",\n";
        js << "    { \"name\": \"" << cs.name << "\", \"bytes\": " << cs.svg.size()
           << ", \"nodes\": " << bp.node_count << ", \"curves\": " << bp.curve_count
           << ", \"rules\": " << bp.rule_count << ", \"symbols\": " << bp.symbol_count
           << ", \"labels\": " << label_cnt
           << ", \"med_us\": " << st.median_val << ", \"p95_us\": " << st.p95_val
           << ", \"mb_per_sec\": " << mb_s << ", \"arena_kb\": " << (bp.memory_bytes() / 1024.0) << " }";
    }
    js << "\n  ],\n  \"studio_inspector_labels_verified\": " << (studio_labels_ok ? "true" : "false");
    printf("  [Verify studio_inspector.svg] data-label extraction & v9 backward-compat: %s\n",
           studio_labels_ok ? "PASS (6 labels incl. BLOOM STUDIO)" : "FAIL");
    if (!studio_labels_ok) { fprintf(stderr, "studio_inspector.svg label check failed!\n"); exit(1); }
    return js.str();
}

// ============================================================================
// Exp 2: Hot-Reload State Preservation & Interactive Verification Suite
// ============================================================================
static std::string bench_exp2_state_and_interaction(BloomSvgBlueprint &bp,
                                                   BloomLayoutEngine &layout,
                                                   BloomStateStore &store) {
    printf("\n=== [2/6] hot_reload_state_preservation: Stateful Reload & Interaction Matrix ===\n");
    const char *svg_v1 = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 720" width="1280" height="720">
  <g id="scroll_box" data-scroll="y" x="40" y="100" width="300" height="120">
    <rect id="scroll_item_top" x="40" y="100" width="280" height="40" data-focusable="true"/>
    <rect id="scroll_item_far" x="40" y="560" width="280" height="40" data-focusable="true"/>
  </g>
  <rect id="slider_gain" data-prim="4" data-bind="0" data-action="1" data-scrub="true" data-init-val="0.20" x="400" y="100" width="200" height="40" fill="#00E8C6"/>
  <rect id="switch_mode" data-prim="1" data-toggle="true" data-focusable="true" data-action="2" x="400" y="160" width="120" height="40" fill="#20D8FF"/>
  <rect id="input_cmd" data-prim="7" data-focusable="true" data-action="6" x="400" y="220" width="240" height="40" fill="#18B8E8"/>
  <rect id="btn_disabled" data-prim="1" data-disabled="true" data-action="9" x="400" y="280" width="120" height="40"/>
  <rect id="btn_guarded" data-prim="1" data-focusable="true" data-guard="true" data-action="10" x="540" y="280" width="120" height="40"/>
  <rect id="btn_legacy" data-prim="1" data-focusable="true" data-action="11" x="680" y="280" width="100" height="40"/>
  <rect id="overlap_back" data-prim="1" data-focusable="true" data-action="20" x="800" y="100" width="200" height="200" fill="#112233"/>
  <rect id="overlap_front" data-prim="1" data-focusable="true" data-action="21" x="840" y="140" width="120" height="120" fill="#336699"/>
</svg>
)SVG";

    const char *svg_v2 = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 720" width="1280" height="720">
  <!-- Reordered nodes, changed colors & positions, added btn_export, removed btn_legacy -->
  <rect id="overlap_back" data-prim="1" data-focusable="true" data-action="20" x="800" y="100" width="200" height="200" fill="#112233"/>
  <rect id="overlap_front" data-prim="1" data-focusable="true" data-action="21" x="840" y="140" width="120" height="120" fill="#4488CC"/>
  <rect id="btn_export" data-prim="1" data-focusable="true" data-action="12" data-init-val="0.15" x="680" y="280" width="120" height="40" fill="#FFB020"/>
  <rect id="input_cmd" data-prim="7" data-focusable="true" data-action="6" x="420" y="230" width="260" height="44" fill="#2299CC"/>
  <rect id="switch_mode" data-prim="1" data-toggle="true" data-focusable="true" data-action="2" x="420" y="170" width="120" height="40" fill="#33FFAA"/>
  <rect id="slider_gain" data-prim="4" data-bind="0" data-action="1" data-scrub="true" data-init-val="0.20" x="420" y="110" width="240" height="44" fill="#00FFCC"/>
  <g id="scroll_box" data-scroll="y" x="40" y="100" width="300" height="120">
    <rect id="scroll_item_far" x="40" y="560" width="280" height="40" data-focusable="true"/>
    <rect id="scroll_item_top" x="40" y="100" width="280" height="40" data-focusable="true"/>
  </g>
</svg>
)SVG";

    const char *svg_v3_del_focus = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 720" width="1280" height="720">
  <rect id="btn_only" data-prim="1" data-focusable="true" x="100" y="100" width="160" height="40"/>
  <rect id="modal_dlg" data-prim="1" data-modal="true" data-focusable="true" data-action="50" x="300" y="200" width="400" height="200"/>
</svg>
)SVG";

    store.reset();
    sv_compile_svg(bp, svg_v1, std::strlen(svg_v1));
    reconcile_hot_reload(bp, store);
    layout.compute(bp, store, 1280.0f, 720.0f, true);

    auto key_of = [](const char *s) { return fnv1a64(s, std::strlen(s)); };
    uint64_t k_slider = key_of("slider_gain");
    uint64_t k_switch = key_of("switch_mode");
    uint64_t k_input  = key_of("input_cmd");
    uint64_t k_scroll = key_of("scroll_box");
    uint64_t k_dis    = key_of("btn_disabled");
    uint64_t k_guard  = key_of("btn_guarded");
    uint64_t k_legacy = key_of("btn_legacy");
    uint64_t k_export = key_of("btn_export");
    uint64_t k_front  = key_of("overlap_front");
    uint64_t k_sfar   = key_of("scroll_item_far");

    // 1. Reverse-Z overlap hit test
    uint64_t hit_ov = hit_test_reverse_z(bp, layout, store, 880.0f, 180.0f);
    bool check_reverse_z = (hit_ov == k_front);

    // 2. Clipped scroll hit test (scroll_item_far at y=560 is clipped outside scroll_box [100..220])
    uint64_t hit_clipped = hit_test_reverse_z(bp, layout, store, 100.0f, 570.0f);
    bool check_clipped_miss = (hit_clipped == 0ULL);

    // 3. Pointer capture cross-bounds scrubbing on slider (w=200: 0.20 -> +106px outside bounds => 0.73)
    store.last_cursor_x = 450.0f;
    step_pointer_interaction(bp, layout, store, 450.0f, 120.0f, true, true);
    step_pointer_interaction(bp, layout, store, 556.0f, 690.0f, true, false); // way outside widget!
    bool check_capture = (store.captured_key == k_slider) &&
                         (std::fabs(store.find_slot(k_slider)->bound_value - 0.73f) < 1e-4f);

    // 4. Switch toggle + scroll offset + text/IME + spring state
    step_pointer_interaction(bp, layout, store, 450.0f, 120.0f, false, false); // release capture
    step_pointer_interaction(bp, layout, store, 440.0f, 180.0f, true, true);
    bool check_toggle = store.find_slot(k_switch)->checked;

    store.find_slot(k_scroll)->scroll_y = 420.0f;
    store.focused_key = k_input;
    NodeRuntimeState *in_s = store.find_slot(k_input);
    const char *sample_txt = "BLOOM \xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87";
    std::snprintf(in_s->text_buf, sizeof(in_s->text_buf), "%s", sample_txt);
    in_s->text_len = static_cast<uint8_t>(std::strlen(in_s->text_buf));
    in_s->spring_vel = -1.75f;

    // 5. Disabled veto & Guarded veto -> allow
    uint32_t st_dis = agent_act(bp, store, k_dis, ACT_CLICK, 0.0f, true);
    bool guard_flag = false;
    store.guard_fn = [](uint64_t, uint32_t, float, void *ctx) { return *static_cast<bool *>(ctx); };
    store.guard_ctx = &guard_flag;
    uint32_t st_g_veto = agent_act(bp, store, k_guard, ACT_CLICK, 0.0f, false);
    guard_flag = true;
    uint32_t st_g_allow = agent_act(bp, store, k_guard, ACT_CLICK, 0.0f, false);
    bool check_policy = (st_dis == STATUS_DISABLED) && (st_g_veto == STATUS_VETOED_GUARD) && (st_g_allow == STATUS_OK);

    // 6. Live Hot-Reload #1 (preserve state across reorder, edits, add, delete)
    uint64_t r0 = now_ns();
    sv_compile_svg(bp, svg_v2, std::strlen(svg_v2));
    HotReloadReport rep = reconcile_hot_reload(bp, store);
    layout.compute(bp, store, 1280.0f, 720.0f, true);
    uint64_t r1 = now_ns();
    double reload_us = static_cast<double>(r1 - r0) / 1e3;

    bool check_reload_preserve =
        (std::fabs(store.find_slot(k_slider)->bound_value - 0.73f) < 1e-4f) &&
        (store.find_slot(k_switch)->checked == true) &&
        (std::fabs(store.find_slot(k_scroll)->scroll_y - 420.0f) < 1e-4f) &&
        (std::strcmp(store.find_slot(k_input)->text_buf, sample_txt) == 0) &&
        (std::fabs(store.find_slot(k_input)->spring_vel - (-1.75f)) < 1e-4f) &&
        (store.focused_key == k_input) &&
        (store.find_slot(k_legacy) == nullptr) &&
        (store.find_slot(k_export) != nullptr) &&
        (std::fabs(store.find_slot(k_export)->bound_value - 0.15f) < 1e-4f) &&
        (rep.added >= 1u && rep.deleted >= 3u);

    // After scrolling scroll_box by 420px, scroll_item_far (y=560 -> 560-420=140) is now inside [100..220] & hittable!
    uint64_t hit_after_scroll = hit_test_reverse_z(bp, layout, store, 100.0f, 155.0f);
    bool check_scroll_reveal = (hit_after_scroll == k_sfar);

    // 7. Live Hot-Reload #2: deleting currently focused/captured node clears stale IDs + Modal trap
    store.captured_key = k_slider;
    sv_compile_svg(bp, svg_v3_del_focus, std::strlen(svg_v3_del_focus));
    HotReloadReport rep2 = reconcile_hot_reload(bp, store);
    uint32_t st_modal_trap = agent_act(bp, store, key_of("btn_only"), ACT_CLICK, 0.0f, true);
    uint32_t st_modal_ok   = agent_act(bp, store, key_of("modal_dlg"), ACT_CLICK, 0.0f, true);
    uint32_t st_rove       = agent_act(bp, store, 0ULL, ACT_ROVE_NEXT, 0.0f, true);
    bool check_delete_and_modal = rep2.focus_cleared && rep2.capture_cleared &&
                                  (store.focused_key == key_of("modal_dlg")) &&
                                  (st_modal_trap == STATUS_MODAL_TRAP) &&
                                  (st_modal_ok == STATUS_OK) &&
                                  (st_rove == STATUS_OK);

    // 8. JSON snapshot export latency
    char json_snap[4096];
    uint64_t j0 = now_ns();
    size_t snap_bytes = 0;
    for (int i = 0; i < 100; ++i) snap_bytes = export_snapshot_json(bp, store, json_snap, sizeof(json_snap));
    uint64_t j1 = now_ns();
    double snap_us = static_cast<double>(j1 - j0) / 1e5;

    bool all_passed = check_reverse_z && check_clipped_miss && check_capture && check_toggle &&
                      check_policy && check_reload_preserve && check_scroll_reveal && check_delete_and_modal;

    printf("  reverse_z=%d clipped_miss=%d capture_scrub=%d toggle=%d policy_guard=%d reload_preserve=%d scroll_reveal=%d del_modal=%d\n",
           check_reverse_z, check_clipped_miss, check_capture, check_toggle,
           check_policy, check_reload_preserve, check_scroll_reveal, check_delete_and_modal);
    printf("  reconcile+relayout=%.2f us | snapshot_json=%.2f us (%zu B) | ALL PASSED=%s\n",
           reload_us, snap_us, snap_bytes, all_passed ? "YES" : "NO");

    if (!all_passed) { fprintf(stderr, "Exp 2 assertion failed!\n"); exit(1); }

    std::ostringstream js;
    js << "  \"hot_reload_state_preservation\": {\n"
       << "    \"all_passed\": " << (all_passed ? "true" : "false") << ",\n"
       << "    \"reverse_z_overlap\": " << (check_reverse_z ? "true" : "false") << ",\n"
       << "    \"clipped_scroll_miss\": " << (check_clipped_miss ? "true" : "false") << ",\n"
       << "    \"pointer_capture_cross_bounds\": " << (check_capture ? "true" : "false") << ",\n"
       << "    \"policy_guard_veto_allow\": " << (check_policy ? "true" : "false") << ",\n"
       << "    \"hot_reload_state_surviving\": " << (check_reload_preserve ? "true" : "false") << ",\n"
       << "    \"scroll_clip_reveal\": " << (check_scroll_reveal ? "true" : "false") << ",\n"
       << "    \"deleted_focus_capture_cleanup_and_modal_trap\": " << (check_delete_and_modal ? "true" : "false") << ",\n"
       << "    \"reload_reconcile_relayout_us\": " << reload_us << ",\n"
       << "    \"snapshot_json_us\": " << snap_us << "\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 3: Nested Flexbox & 10,000-Row Virtual List vs Flat DOM
// ============================================================================
static std::string bench_exp3_layout_and_vlist(BloomSvgBlueprint &bp,
                                               BloomLayoutEngine &layout,
                                               BloomStateStore &store) {
    printf("\n=== [3/6] layout_and_virtual_list: Nested Flexbox + 10,000-Row Virtual List ===\n");
    std::string ide_svg = make_synthetic_svg(500, true);
    store.reset();
    sv_compile_svg(bp, ide_svg.data(), ide_svg.size());
    reconcile_hot_reload(bp, store);

    std::vector<double> full_us, skip_us, hit_us;
    for (int it = 0; it < 200; ++it) {
        layout.layout_dirty = true;
        uint64_t t0 = now_ns();
        layout.compute(bp, store, 3840.0f, 2160.0f, false);
        uint64_t t1 = now_ns();
        uint64_t t2 = now_ns();
        layout.compute(bp, store, 3840.0f, 2160.0f, false); // dirty == false skip
        uint64_t t3 = now_ns();
        uint64_t t4 = now_ns();
        volatile uint64_t hk = hit_test_reverse_z(bp, layout, store, 1500.0f, 300.0f);
        (void)hk;
        uint64_t t5 = now_ns();
        if (it >= 10) {
            full_us.push_back(static_cast<double>(t1 - t0) / 1e3);
            skip_us.push_back(static_cast<double>(t3 - t2) / 1e3);
            hit_us.push_back(static_cast<double>(t5 - t4) / 1e3);
        }
    }
    Stats sf = compute_stats(full_us), ss_s = compute_stats(skip_us), sh = compute_stats(hit_us);

    // Virtual 10,000-row packing vs naive 10,000 flat SVG nodes estimate/measure
    layout.compute(bp, store, 1920.0f, 1080.0f, true);
    std::vector<BloomQuadHot> opq(1024), alp(1024);
    std::vector<BloomShadowHot> shb(128);
    std::vector<BloomGlyphInst> glb(2048);
    store.find_slot(fnv1a64("vlist_sidebar", 13))->scroll_y = 28000.0f; // middle of 10k rows
    PackedFrameCounts vc = pack_svg_frame_wc(bp, layout, store, 0.0083f, 1920.0f, 1080.0f,
                                             opq.data(), 1024, alp.data(), 1024, shb.data(), 128, glb.data(), 2048, false);

    // Measure naive 10,000 flat rect rows compilation & layout in bp
    std::ostringstream flat_ss;
    flat_ss << "<svg viewBox=\"0 0 1920 1080\"><g id=\"flat\" data-layout=\"col\" x=\"0\" y=\"0\" width=\"420\" height=\"896\">";
    for (uint32_t r = 0; r < 10000u; ++r) {
        flat_ss << "<rect id=\"r" << r << "\" x=\"0\" y=\"" << (r * 28) << "\" width=\"400\" height=\"26\"/>";
    }
    flat_ss << "</g></svg>";
    std::string flat_str = flat_ss.str();
    double flat_parse_us = 0.0, flat_layout_us = 0.0;
    uint32_t flat_nodes = 0;
    size_t flat_arena_b = 0;
    {
        uint64_t p0 = now_ns();
        sv_compile_svg(bp, flat_str.data(), flat_str.size());
        uint64_t p1 = now_ns();
        reconcile_hot_reload(bp, store);
        uint64_t l0 = now_ns();
        layout.compute(bp, store, 1920.0f, 1080.0f, true);
        uint64_t l1 = now_ns();
        flat_parse_us = static_cast<double>(p1 - p0) / 1e3;
        flat_layout_us = static_cast<double>(l1 - l0) / 1e3;
        flat_nodes = bp.node_count;
        flat_arena_b = bp.memory_bytes();
    }

    printf("  ide_500 nested flex | full=%.2f us | dirty_skip=%.3f us | hit_test=%.2f us\n",
           sf.median_val, ss_s.median_val, sh.median_val);
    printf("  vlist 10k rows      | vis_rows=%u | vlist_wc=%zu B vs flat_10k (nodes=%u, parse=%.1f us, layout=%.1f us, arena=%.1f KB)\n",
           vc.vlist_rows, vc.wc_bytes, flat_nodes, flat_parse_us, flat_layout_us, flat_arena_b / 1024.0);

    std::ostringstream js;
    js << "  \"layout_and_virtual_list\": {\n"
       << "    \"ide_500_full_flex_us\": " << sf.median_val << ",\n"
       << "    \"ide_500_dirty_skip_us\": " << ss_s.median_val << ",\n"
       << "    \"ide_500_reverse_z_hit_us\": " << sh.median_val << ",\n"
       << "    \"virtual_10k_visible_rows\": " << vc.vlist_rows << ",\n"
       << "    \"virtual_10k_frame_wc_kb\": " << (vc.wc_bytes / 1024.0) << ",\n"
       << "    \"naive_flat_10k_nodes\": " << flat_nodes << ",\n"
       << "    \"naive_flat_10k_parse_us\": " << flat_parse_us << ",\n"
       << "    \"naive_flat_10k_layout_us\": " << flat_layout_us << ",\n"
       << "    \"naive_flat_10k_arena_kb\": " << (flat_arena_b / 1024.0) << "\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 4: Reactive Bind Propagation + CSS Variant + Direct WC Ring Packing
// ============================================================================
static std::string bench_exp4_reactive_wc_pack(Ctx &ctx,
                                               BloomSvgBlueprint &bp,
                                               BloomLayoutEngine &layout,
                                               BloomStateStore &store) {
    printf("\n=== [4/6] css_state_and_wc_pack: O(dirty) Bind + CSS Variant + WC Ring Pack ===\n");
    std::string ide_svg = make_synthetic_svg(500, true);
    store.reset();
    sv_compile_svg(bp, ide_svg.data(), ide_svg.size());
    reconcile_hot_reload(bp, store);
    layout.compute(bp, store, 3840.0f, 2160.0f, true);
    store.hovered_key = fnv1a64("w_0", 3);
    store.focused_key = fnv1a64("cmd_input", 9);

    id<MTLBuffer> buf_opq = [ctx.dev newBufferWithLength:2048 * sizeof(BloomQuadHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> buf_alp = [ctx.dev newBufferWithLength:2048 * sizeof(BloomQuadHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> buf_sh  = [ctx.dev newBufferWithLength:256  * sizeof(BloomShadowHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> buf_gl  = [ctx.dev newBufferWithLength:12288 * sizeof(BloomGlyphInst) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

    std::vector<double> bind_us, pack_us;
    PackedFrameCounts last_c{};
    uint32_t total_subs_updated = 0;

    for (int it = 0; it < 120; ++it) {
        uint64_t b0 = now_ns();
        uint32_t upd = 0;
        for (uint32_t ch = 0; ch < 16u; ++ch) {
            upd += store.set_bind_channel(bp, ch, 0.1f + 0.05f * ch);
        }
        uint64_t b1 = now_ns();

        uint64_t p0 = now_ns();
        last_c = pack_svg_frame_wc(bp, layout, store, 0.0083f, 3840.0f, 2160.0f,
                                   static_cast<BloomQuadHot *>(buf_opq.contents), 2048,
                                   static_cast<BloomQuadHot *>(buf_alp.contents), 2048,
                                   static_cast<BloomShadowHot *>(buf_sh.contents), 256,
                                   static_cast<BloomGlyphInst *>(buf_gl.contents), 12288, true);
        uint64_t p1 = now_ns();
        if (it >= 10) {
            bind_us.push_back(static_cast<double>(b1 - b0) / 1e3);
            pack_us.push_back(static_cast<double>(p1 - p0) / 1e3);
            total_subs_updated = upd;
        }
    }
    Stats sb = compute_stats(bind_us), sp = compute_stats(pack_us);
    printf("  16-ch O(dirty) bind=%5.2f us (%u nodes touched) | WC Pack (opq=%u, alp=%u, sh=%u, gl=%u)=%5.2f us | %.1f KB/frame\n",
           sb.median_val, total_subs_updated, last_c.opaque_quads, last_c.alpha_quads,
           last_c.shadows, last_c.glyphs, sp.median_val, last_c.wc_bytes / 1024.0);

    std::ostringstream js;
    js << "  \"css_state_and_wc_pack\": {\n"
       << "    \"bind_16ch_us\": " << sb.median_val << ",\n"
       << "    \"bind_nodes_updated\": " << total_subs_updated << ",\n"
       << "    \"wc_pack_us\": " << sp.median_val << ",\n"
       << "    \"wc_bytes\": " << last_c.wc_bytes << ",\n"
       << "    \"opaque_quads\": " << last_c.opaque_quads << ",\n"
       << "    \"alpha_quads\": " << last_c.alpha_quads << ",\n"
       << "    \"shadows\": " << last_c.shadows << ",\n"
       << "    \"glyphs_and_icons\": " << last_c.glyphs << "\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 5: 4K Metal SVG-UI Rendering — Archived v9 Compute vs BLOOM vNext TBDR
// ============================================================================
static std::string bench_exp5_gpu_4k(Ctx &ctx,
                                     BloomSvgBlueprint &bp,
                                     BloomLayoutEngine &layout,
                                     BloomStateStore &store) {
    printf("\n=== [5/6] gpu_4k_svg_tbdr_vs_v9_compute: 4K SVG UI Frame Time ===\n");
    const uint32_t W = 3840, H = 2160;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};

    id<MTLComputePipelineState> p_v9   = cpso(ctx, "k_v9_bloom_ui_overlay");
    id<MTLComputePipelineState> p_chk  = cpso(ctx, "k_tex_checksum");
    id<MTLRenderPipelineState>  p_opq  = rpso(ctx, "v_bloom_opaque_32", "f_bloom_opaque_32", false);
    id<MTLRenderPipelineState>  p_alp  = rpso(ctx, "v_bloom_quad32_refetch20", "f_bloom_quad32_refetch20", true);
    id<MTLRenderPipelineState>  p_sh   = rpso(ctx, "v_bloom_shadow_refetch20", "f_bloom_shadow_refetch20", true);
    id<MTLRenderPipelineState>  p_gl   = rpso(ctx, "v_bloom_glyph_refetch20", "f_bloom_glyph_refetch20", true);

    printf("  Occupancy Check | v9_compute_maxTG=%lu (render passes use specialized short fp16 shaders)\n",
           static_cast<unsigned long>(p_v9.maxTotalThreadsPerThreadgroup));

    id<MTLTexture> scene_hdr = tex2d(ctx.dev, W, H, MTLPixelFormatRGBA16Float,
                                     MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> comp_out  = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm,
                                     MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLTexture> tbdr_rt   = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm,
                                     MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLTexture> tbdr_dep  = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float,
                                     MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    id<MTLTexture> atlas_r8  = tex2d(ctx.dev, 1024, 1024, MTLPixelFormatR8Unorm,
                                     MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    // Populate atlas_r8 with valid checkerboard/glyph ink via WC blit once so glyphs produce non-zero alpha!
    {
        std::vector<uint8_t> ink(1024 * 1024);
        for (size_t i = 0; i < ink.size(); ++i) ink[i] = static_cast<uint8_t>(((i ^ (i >> 10)) & 7u) ? 210u : 80u);
        id<MTLBuffer> stg = [ctx.dev newBufferWithBytes:ink.data() length:ink.size() options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        id<MTLCommandBuffer> cb = [ctx.q commandBuffer];
        id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
        [bl copyFromBuffer:stg sourceOffset:0 sourceBytesPerRow:1024 sourceBytesPerImage:ink.size()
                sourceSize:MTLSizeMake(1024, 1024, 1) toTexture:atlas_r8 destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    }

    MTLDepthStencilDescriptor *dwd = [[MTLDepthStencilDescriptor alloc] init];
    dwd.depthCompareFunction = MTLCompareFunctionLess; dwd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_write = [ctx.dev newDepthStencilStateWithDescriptor:dwd];
    MTLDepthStencilDescriptor *dtd = [[MTLDepthStencilDescriptor alloc] init];
    dtd.depthCompareFunction = MTLCompareFunctionLessEqual; dtd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_test = [ctx.dev newDepthStencilStateWithDescriptor:dtd];

    std::string studio_svg = read_text_file("/Users/kuangtalin/Documents/BLOOM/research/probes/fixtures/studio_inspector_v9.svg");
    std::string ide_svg = make_synthetic_svg(500, true);

    auto run_v9_compute = [&](uint32_t n_widgets, const char *label, double &med_ms, double &p95_ms, uint32_t &chk) {
        id<MTLBuffer> nb = [ctx.dev newBufferWithLength:n_widgets * sizeof(V9UiNodeGpu) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        auto *p = static_cast<V9UiNodeGpu *>(nb.contents);
        uint64_t dom_mask = 0ULL;
        for (uint32_t i = 0; i < n_widgets; ++i) {
            float cx = -0.85f + static_cast<float>(i % 16u) * 0.11f;
            float cy = -0.75f + static_cast<float>((i / 16u) % 16u) * 0.10f;
            float hw = 0.08f, hh = 0.04f;
            int x0 = std::max(0, std::min(7, static_cast<int>((cx - hw + 1.0f) * 4.0f)));
            int x1 = std::max(0, std::min(7, static_cast<int>((cx + hw + 1.0f) * 4.0f)));
            int y0 = std::max(0, std::min(7, static_cast<int>((cy - hh + 1.0f) * 4.0f)));
            int y1 = std::max(0, std::min(7, static_cast<int>((cy + hh + 1.0f) * 4.0f)));
            uint64_t nm = 0ULL;
            for (int r = y0; r <= y1; ++r) for (int c = x0; c <= x1; ++c) nm |= (1ULL << (r * 8 + c));
            dom_mask |= nm;
            p[i] = {i + 1u, (i % 5u == 0u) ? 4u : 1u, 1u, 1u, cx, cy, hw, hh, 0.01f, 0.6f, 1.0f, 0u,
                    0xFF20D8FFu, static_cast<uint32_t>(nm & 0xFFFFFFFFULL), static_cast<uint32_t>(nm >> 32), 0u};
        }
        uint32_t dom_pair[2] = {static_cast<uint32_t>(dom_mask & 0xFFFFFFFFULL), static_cast<uint32_t>(dom_mask >> 32)};
        std::vector<double> ms;
        MTLSize tgs = MTLSizeMake(16, 16, 1);
        MTLSize ntg = MTLSizeMake((W + 15u) / 16u, (H + 15u) / 16u, 1);
        for (int it = 0; it < 36; ++it) {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
            [enc setComputePipelineState:p_v9];
            [enc setTexture:scene_hdr atIndex:0];
            [enc setTexture:comp_out atIndex:1];
            [enc setBuffer:nb offset:0 atIndex:0];
            [enc setBytes:&n_widgets length:sizeof(n_widgets) atIndex:1];
            [enc setBytes:dom_pair length:sizeof(dom_pair) atIndex:2];
            [enc dispatchThreadgroups:ntg threadsPerThreadgroup:tgs];
            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 6) ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
        }
        Stats s = compute_stats(ms);
        med_ms = s.median_val; p95_ms = s.p95_val;
        chk = compute_tex_checksum(ctx, p_chk, comp_out);
        printf("  %-24s | med=%6.3f ms (%6.1f fps) | p95=%6.3f ms | chk=0x%08X\n",
               label, med_ms, 1000.0 / med_ms, p95_ms, chk);
    };

    auto run_vnext_tbdr = [&](const std::string &svg_src, bool extra_ide_glyphs, const char *label,
                              double &med_ms, double &p95_ms, size_t &wc_bytes, uint32_t &chk) {
        store.reset();
        sv_compile_svg(bp, svg_src.data(), svg_src.size());
        reconcile_hot_reload(bp, store);
        layout.compute(bp, store, static_cast<float>(W), static_cast<float>(H), true);

        id<MTLBuffer> buf_opq   = [ctx.dev newBufferWithLength:2048 * sizeof(BloomQuadHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        id<MTLBuffer> buf_alp   = [ctx.dev newBufferWithLength:2048 * sizeof(BloomQuadHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        id<MTLBuffer> buf_sh    = [ctx.dev newBufferWithLength:256  * sizeof(BloomShadowHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        id<MTLBuffer> buf_gl    = [ctx.dev newBufferWithLength:14000 * sizeof(BloomGlyphInst) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        id<MTLBuffer> buf_clips = [ctx.dev newBufferWithLength:512  * sizeof(BloomClipRect) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

        std::memcpy(buf_clips.contents, layout.clips.data(), layout.clip_count * sizeof(BloomClipRect));
        PackedFrameCounts fc = pack_svg_frame_wc(bp, layout, store, 0.0083f, static_cast<float>(W), static_cast<float>(H),
                                                 static_cast<BloomQuadHot *>(buf_opq.contents), 2048,
                                                 static_cast<BloomQuadHot *>(buf_alp.contents), 2048,
                                                 static_cast<BloomShadowHot *>(buf_sh.contents), 256,
                                                 static_cast<BloomGlyphInst *>(buf_gl.contents), extra_ide_glyphs ? 12000u : 512u,
                                                 extra_ide_glyphs);
        wc_bytes = fc.wc_bytes;

        std::vector<double> ms;
        for (int it = 0; it < 44; ++it) {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = tbdr_rt;
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.05, 0.09, 1.0);
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = tbdr_dep;
            rpd.depthAttachment.loadAction = MTLLoadActionClear;
            rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
            rpd.depthAttachment.clearDepth = 1.0;

            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            if (fc.opaque_quads > 0u) {
                [enc setRenderPipelineState:p_opq]; [enc setDepthStencilState:ds_write];
                [enc setVertexBuffer:buf_opq offset:0 atIndex:0]; [enc setVertexBuffer:buf_clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:fc.opaque_quads];
            }
            [enc setDepthStencilState:ds_test];
            if (fc.shadows > 0u) {
                [enc setRenderPipelineState:p_sh];
                [enc setVertexBuffer:buf_sh offset:0 atIndex:0]; [enc setVertexBuffer:buf_clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                [enc setFragmentBuffer:buf_sh offset:0 atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:fc.shadows];
            }
            if (fc.alpha_quads > 0u) {
                [enc setRenderPipelineState:p_alp];
                [enc setVertexBuffer:buf_alp offset:0 atIndex:0]; [enc setVertexBuffer:buf_clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                [enc setFragmentBuffer:buf_alp offset:0 atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:fc.alpha_quads];
            }
            if (fc.glyphs > 0u) {
                [enc setRenderPipelineState:p_gl];
                [enc setVertexBuffer:buf_gl offset:0 atIndex:0]; [enc setVertexBuffer:buf_clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                [enc setFragmentBuffer:buf_gl offset:0 atIndex:0]; [enc setFragmentTexture:atlas_r8 atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:fc.glyphs];
            }
            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 8) ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
        }
        Stats s = compute_stats(ms);
        med_ms = s.median_val; p95_ms = s.p95_val;
        chk = compute_tex_checksum(ctx, p_chk, tbdr_rt);
        printf("  %-24s | med=%6.3f ms (%6.1f fps) | p95=%6.3f ms | wc=%6.1f KB | chk=0x%08X\n",
               label, med_ms, 1000.0 / med_ms, p95_ms, wc_bytes / 1024.0, chk);
    };

    double v9_16_med = 0, v9_16_p95 = 0, v9_256_med = 0, v9_256_p95 = 0;
    double vn_st_med = 0, vn_st_p95 = 0, vn_id_med = 0, vn_id_p95 = 0;
    size_t vn_st_wc = 0, vn_id_wc = 0;
    uint32_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;

    run_v9_compute(16u,  "v9_compute_studio16",  v9_16_med,  v9_16_p95,  c0);
    run_v9_compute(256u, "v9_compute_256",       v9_256_med, v9_256_p95, c1);
    run_vnext_tbdr(studio_svg, false, "vnext_tbdr_studio16", vn_st_med, vn_st_p95, vn_st_wc, c2);
    run_vnext_tbdr(ide_svg,    true,  "vnext_tbdr_ide500",   vn_id_med, vn_id_p95, vn_id_wc, c3);

    if (c0 == 0u || c2 == 0u || c3 == 0u) {
        fprintf(stderr, "Zero pixel checksum detected! Rendering did not touch output pixels.\n");
        exit(1);
    }

    std::ostringstream js;
    js << "  \"gpu_4k_svg_tbdr_vs_v9_compute\": {\n"
       << "    \"max_threads_per_tg_v9\": " << p_v9.maxTotalThreadsPerThreadgroup << ",\n"
       << "    \"v9_compute_studio16\": { \"med_ms\": " << v9_16_med << ", \"p95_ms\": " << v9_16_p95 << ", \"fps\": " << (1000.0 / v9_16_med) << ", \"checksum\": " << c0 << " },\n"
       << "    \"v9_compute_256\": { \"med_ms\": " << v9_256_med << ", \"p95_ms\": " << v9_256_p95 << ", \"fps\": " << (1000.0 / v9_256_med) << ", \"checksum\": " << c1 << " },\n"
       << "    \"vnext_tbdr_studio16\": { \"med_ms\": " << vn_st_med << ", \"p95_ms\": " << vn_st_p95 << ", \"fps\": " << (1000.0 / vn_st_med) << ", \"wc_bytes\": " << vn_st_wc << ", \"checksum\": " << c2 << " },\n"
       << "    \"vnext_tbdr_ide500\": { \"med_ms\": " << vn_id_med << ", \"p95_ms\": " << vn_id_p95 << ", \"fps\": " << (1000.0 / vn_id_med) << ", \"wc_bytes\": " << vn_id_wc << ", \"checksum\": " << c3 << " }\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 6: SVG <path> Cubic/Quad Rasterized Private Atlas vs Live Slug Crossover
// ============================================================================
static std::string bench_exp6_path_crossover(Ctx &ctx, BloomSvgBlueprint &bp) {
    printf("\n=== [6/6] svg_path_atlas_vs_slug: 512 SVG Icons across 24..192 px @ 4K ===\n");
    static const char *kIcons[5] = {
        "M 12 4 C 16 4 20 8 20 12 C 20 16 16 20 12 20 C 8 20 4 16 4 12 C 4 8 8 4 12 4 Z",
        "M 6 4 L 20 12 L 6 20 Z",
        "M 11 4 Q 18 4 18 11 Q 18 18 11 18 Q 4 18 4 11 Q 4 4 11 4 Z",
        "M 3 6 L 9 6 L 11 8 L 21 8 L 21 19 L 3 19 Z",
        "M 4 12 L 10 18 L 12 16 L 20 6 L 18 4 L 10 14 L 6 10 Z"
    };

    // Parse 5 SVG icons once into curve slices
    std::ostringstream icon_svg;
    icon_svg << "<svg viewBox=\"0 0 24 24\">";
    for (int i = 0; i < 5; ++i) icon_svg << "<path id=\"ic" << i << "\" d=\"" << kIcons[i] << "\"/>";
    icon_svg << "</svg>";
    std::string isrc = icon_svg.str();
    sv_compile_svg(bp, isrc.data(), isrc.size());

    // Measure one-time CPU scanline raster + Blit upload @ 32, 64, 128 px
    std::vector<uint8_t> tile_pix(128 * 128);
    id<MTLTexture> atlas = tex2d(ctx.dev, 1024, 1024, MTLPixelFormatR8Unorm, MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    const uint32_t bake_sizes[] = {32u, 64u, 128u};
    double bake_us_per_icon[3]{};

    for (int bi = 0; bi < 3; ++bi) {
        uint32_t sz = bake_sizes[bi];
        id<MTLBuffer> stg = [ctx.dev newBufferWithLength:5 * sz * sz options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        std::vector<double> t_us;
        for (int it = 0; it < 20; ++it) {
            uint64_t t0 = now_ns();
            uint8_t *dst = static_cast<uint8_t *>(stg.contents);
            for (int k = 0; k < 5; ++k) {
                const SvgBlueprintNode &pn = bp.nodes[static_cast<uint32_t>(k)];
                rasterize_curves_r8(&bp.curves[pn.curve_start], pn.curve_count, 24.0f, sz, tile_pix.data());
                std::memcpy(dst + k * sz * sz, tile_pix.data(), sz * sz);
            }
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            id<MTLBlitCommandEncoder> bl = [cmd blitCommandEncoder];
            for (int k = 0; k < 5; ++k) {
                [bl copyFromBuffer:stg sourceOffset:static_cast<NSUInteger>(k * sz * sz) sourceBytesPerRow:sz
                 sourceBytesPerImage:sz * sz sourceSize:MTLSizeMake(sz, sz, 1)
                         toTexture:atlas destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(k * 128u, 0, 0)];
            }
            [bl endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            uint64_t t1 = now_ns();
            if (it >= 2) t_us.push_back(static_cast<double>(t1 - t0) / 5e3);
        }
        bake_us_per_icon[bi] = compute_stats(t_us).median_val;
        printf("  One-time Bake + Blit (%3ux%-3u R8): %6.1f us / icon\n", sz, sz, bake_us_per_icon[bi]);
    }

    // 4K Render Crossover: 512 Icons at 24px, 48px, 96px, 192px (Atlas vs Slug)
    const uint32_t W = 3840, H = 2160;
    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    id<MTLTexture> rt = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget, MTLStorageModePrivate, true);
    id<MTLTexture> dep = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    id<MTLBuffer> clips = [ctx.dev newBufferWithLength:sizeof(BloomClipRect) options:MTLResourceStorageModeShared];
    *static_cast<BloomClipRect *>(clips.contents) = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};
    id<MTLRenderPipelineState> p_atl  = rpso(ctx, "v_bloom_glyph_refetch20", "f_bloom_glyph_refetch20", true);
    id<MTLRenderPipelineState> p_slug = rpso(ctx, "v_bloom_slug_refetch20", "f_bloom_slug_refetch20", true);
    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionAlways; dsd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_off = [ctx.dev newDepthStencilStateWithDescriptor:dsd];

    const uint32_t N_ICONS = 512u;
    const uint32_t N_TILES = N_ICONS * 4u;
    const float sizes_px[] = {24.0f, 48.0f, 96.0f, 192.0f};

    std::ostringstream js;
    js << "  \"svg_path_atlas_vs_slug\": {\n"
       << "    \"bake_us_per_icon\": { \"px32\": " << bake_us_per_icon[0]
       << ", \"px64\": " << bake_us_per_icon[1] << ", \"px128\": " << bake_us_per_icon[2] << " },\n"
       << "    \"render_crossover\": [\n";

    for (int si = 0; si < 4; ++si) {
        float px_sz = sizes_px[si];
        uint32_t total_curves = 0;
        for (uint32_t i = 0; i < N_ICONS; ++i) total_curves += bp.nodes[i % 5u].curve_count * 4u;

        id<MTLBuffer> b_icons = [ctx.dev newBufferWithLength:N_ICONS * sizeof(BloomGlyphInst) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_tiles = [ctx.dev newBufferWithLength:N_TILES * sizeof(SlugTileGpu) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_curv  = [ctx.dev newBufferWithLength:std::max<uint32_t>(total_curves, 16u) * sizeof(SlugCurveGpu) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_rgba  = [ctx.dev newBufferWithLength:N_ICONS * sizeof(uint32_t) options:MTLResourceStorageModeShared];

        auto *gi = static_cast<BloomGlyphInst *>(b_icons.contents);
        auto *st = static_cast<SlugTileGpu *>(b_tiles.contents);
        auto *sc = static_cast<SlugCurveGpu *>(b_curv.contents);
        auto *pr = static_cast<uint32_t *>(b_rgba.contents);

        uint32_t c_cursor = 0;
        uint32_t cols = std::max(1u, W / static_cast<uint32_t>(px_sz + 8.0f));
        for (uint32_t i = 0; i < N_ICONS; ++i) {
            const SvgBlueprintNode &pn = bp.nodes[i % 5u];
            float x = static_cast<float>(i % cols) * (px_sz + 4.0f) + 8.0f;
            float y = static_cast<float>((i / cols) % 16u) * (px_sz + 4.0f) + 8.0f;
            pr[i] = 0xFF38BDF8u;
            gi[i] = {{pack_q14_2(x), pack_q14_2(y), pack_q14_2(px_sz), pack_q14_2(px_sz)},
                     {static_cast<uint16_t>((i % 5u) * 8192u), 0u, 8192u, 8192u}, 0xFF38BDF8u, 0u, 1u};
            float half_p = px_sz * 0.5f;
            float scale_vb = px_sz / 24.0f;
            for (uint32_t q = 0; q < 4u; ++q) {
                float tx = x + ((q & 1u) ? half_p : 0.0f);
                float ty = y + ((q & 2u) ? half_p : 0.0f);
                uint32_t ti = i * 4u + q;
                uint32_t cstart = c_cursor;
                for (uint16_t k = 0; k < pn.curve_count; ++k) {
                    const SvgQuadCurveSeg &q0 = bp.curves[pn.curve_start + k];
                    float x0 = x + q0.p0x * scale_vb, y0 = y + q0.p0y * scale_vb;
                    float cx = x + q0.cx  * scale_vb, cy = y + q0.cy  * scale_vb;
                    float x1 = x + q0.p1x * scale_vb, y1 = y + q0.p1y * scale_vb;
                    float ax = x0 - 2.0f * cx + x1, ay = y0 - 2.0f * cy + y1;
                    float bx = 2.0f * (cx - x0), by = 2.0f * (cy - y0);
                    sc[c_cursor++] = {x0, y0, x1, y1, ax, ay, bx, by, (y1 >= y0 ? 1.0f : -1.0f), 0u};
                }
                st[ti] = {i, cstart, pn.curve_count, 0, tx, ty, tx + half_p, ty + half_p, tx, ty, tx, ty, tx + half_p, ty + half_p};
            }
        }

        std::vector<double> atl_ms, slg_ms;
        for (int it = 0; it < 28; ++it) {
            auto run_one = [&](bool use_slug) -> double {
                id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
                MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                rpd.depthAttachment.texture = dep; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
                id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
                [enc setDepthStencilState:ds_off];
                if (!use_slug) {
                    [enc setRenderPipelineState:p_atl];
                    [enc setVertexBuffer:b_icons offset:0 atIndex:0]; [enc setVertexBuffer:clips offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
                    [enc setFragmentBuffer:b_icons offset:0 atIndex:0]; [enc setFragmentTexture:atlas atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N_ICONS];
                } else {
                    [enc setRenderPipelineState:p_slug];
                    [enc setVertexBuffer:b_tiles offset:0 atIndex:0]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
                    [enc setFragmentBuffer:b_tiles offset:0 atIndex:0]; [enc setFragmentBuffer:b_curv offset:0 atIndex:1]; [enc setFragmentBuffer:b_rgba offset:0 atIndex:2];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N_TILES];
                }
                [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
                return (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
            };
            double da = run_one(false), ds = run_one(true);
            if (it >= 4) { atl_ms.push_back(da); slg_ms.push_back(ds); }
        }
        Stats sa = compute_stats(atl_ms), ss_m = compute_stats(slg_ms);
        printf("  %3.0f px x 512 SVG Paths | Private R8 Atlas=%6.3f ms | Live Slug=%6.3f ms | Atlas %.2fx faster\n",
               px_sz, sa.median_val, ss_m.median_val, ss_m.median_val / sa.median_val);
        if (si > 0) js << ",\n";
        js << "      { \"px\": " << static_cast<int>(px_sz)
           << ", \"private_atlas_ms\": " << sa.median_val
           << ", \"live_slug_ms\": " << ss_m.median_val
           << ", \"atlas_speedup\": " << (ss_m.median_val / sa.median_val) << " }";
    }
    js << "\n    ]\n  }";
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
        ctx.lib = [ctx.dev newLibraryWithSource:[NSString stringWithUTF8String:kSvgBenchMetal]
                                        options:[[MTLCompileOptions alloc] init] error:&err];
        if (!ctx.lib || err) {
            fprintf(stderr, "MSL compile error: %s\n", err ? err.localizedDescription.UTF8String : "unknown");
            return 1;
        }

        warm_gpu_dvfs(ctx);

        BloomSvgBlueprint bp;
        BloomLayoutEngine layout;
        BloomStateStore   store;
        bp.alloc_once();
        layout.alloc_once();
        store.alloc_once();

        std::vector<std::string> sections;
        sections.push_back(bench_exp1_parse(bp));
        sections.push_back(bench_exp2_state_and_interaction(bp, layout, store));
        sections.push_back(bench_exp3_layout_and_vlist(bp, layout, store));
        sections.push_back(bench_exp4_reactive_wc_pack(ctx, bp, layout, store));
        sections.push_back(bench_exp5_gpu_4k(ctx, bp, layout, store));
        sections.push_back(bench_exp6_path_crossover(ctx, bp));

        if (!json_path.empty()) {
            FILE *fp = std::fopen(json_path.c_str(), "w");
            if (!fp) { fprintf(stderr, "Cannot open %s\n", json_path.c_str()); return 1; }
            std::fprintf(fp, "{\n");
            for (size_t i = 0; i < sections.size(); ++i) {
                std::fprintf(fp, "%s%s\n", sections[i].c_str(), (i + 1 < sections.size()) ? "," : "");
            }
            std::fprintf(fp, "}\n");
            std::fclose(fp);
            printf("\nWrote SVG-SSOT JSON results to %s\n", json_path.c_str());
        }
        return 0;
    }
}
