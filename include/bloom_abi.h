#ifndef BLOOM_ABI_H
#define BLOOM_ABI_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#define BLOOM_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define BLOOM_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

/* Local quantization domain shared with shaders/bloom_core.metal */
#define BLOOM_CLUSTER_VTX 64u
#define BLOOM_CLUSTER_TRI 96u
#define BLOOM_LOCAL_BOX_LO (-1.5f)
#define BLOOM_LOCAL_BOX_SPAN 3.0f
#define BLOOM_RGBA(r, g, b, a) \
    (((uint32_t)(a) << 24u) | ((uint32_t)(b) << 16u) | ((uint32_t)(g) << 8u) | (uint32_t)(r))

/* Grounding Packed 32-bit ID: [domain:2 | slot:14 | part:8 | qdepth8:8] */
enum {
    BLOOM_GROUND_EMPTY    = 0u,
    BLOOM_GROUND_MESH     = 1u,
    BLOOM_GROUND_ANALYTIC = 2u,
    BLOOM_GROUND_UI       = 3u
};
#define BLOOM_QUAD_FLAG_GROUND_ONLY (UINT32_C(1) << 28)
#define BLOOM_GROUND_KEY_MASK       UINT32_C(0xFFFF0000)

static inline uint32_t bloom_ground_pack(uint32_t domain, uint32_t slot, uint32_t part) {
    return ((domain & 3u) << 30u) | ((slot & 0x3FFFu) << 16u) | ((part & 0xFFu) << 8u);
}
static inline uint32_t bloom_ground_domain(uint32_t g)   { return (g >> 30u) & 3u; }
static inline uint32_t bloom_ground_slot(uint32_t g)     { return (g >> 16u) & 0x3FFFu; }
static inline uint32_t bloom_ground_part(uint32_t g)     { return (g >> 8u)  & 0xFFu; }
static inline uint32_t bloom_ground_depth_q8(uint32_t g) { return g & 0xFFu; }

/* ============================================================================
 * GPU-Direct Compact POD Structures (Zero-Padding C17 / C++17 / MSL Parity)
 * ============================================================================ */

typedef struct {
    uint16_t pos_unorm16[3]; /* 6 B: local [-1.5, +1.5]^3 */
    int8_t   oct_norm[2];    /* 2 B: octahedral unit normal */
    uint8_t  uv_u8[2];       /* 2 B */
    uint8_t  joint_id;       /* 1 B */
    uint8_t  mat_id;         /* 1 B */
} BloomVtx12;
BLOOM_STATIC_ASSERT(sizeof(BloomVtx12) == 12, "BloomVtx12 must be 12 bytes");

typedef struct {
    uint32_t vtx_offset;     /*  4 B */
    uint32_t idx_offset;     /*  4 B */
    uint16_t vtx_cnt;        /*  2 B */
    uint16_t tri_cnt;        /*  2 B */
    uint16_t obj_id;         /*  2 B */
    uint16_t flags;          /*  2 B: bit0=cutaway */
    uint16_t bsphere_f16[4]; /*  8 B: local cx, cy, cz, r */
    int8_t   cone_axis[3];   /*  3 B */
    int8_t   cone_cutoff;    /*  1 B */
    uint32_t aabb_scale_tag; /*  4 B */
} BloomMeshletHeader32;
BLOOM_STATIC_ASSERT(sizeof(BloomMeshletHeader32) == 32, "BloomMeshletHeader32 must be 32 bytes");

typedef struct {
    float    pos[3];            /* 12 B */
    uint16_t quat_f16[4];       /*  8 B */
    uint16_t scale_f16[3];      /*  6 B */
    uint16_t dof_f16[3];        /*  6 B */
    uint32_t rgba8_mat;         /*  4 B */
    uint16_t clip_plane_f16[4]; /*  8 B: local plane dot(p, n) - d >= 0 */
    uint32_t flags;             /*  4 B: bit0=cutaway_active, bit1=hidden */
} BloomObjectState48;
BLOOM_STATIC_ASSERT(sizeof(BloomObjectState48) == 48, "BloomObjectState48 must be 48 bytes");

typedef struct {
    float    center[3];       /* 12 B */
    float    radius;          /*  4 B */
    uint16_t axis_len_f16[4]; /*  8 B: capsule axis xyz + half_len */
    uint32_t rgba8;           /*  4 B */
    uint32_t prim_kind_flags; /*  4 B: 1=sphere, 2=capsule */
} BloomAnalyticPrim32;
BLOOM_STATIC_ASSERT(sizeof(BloomAnalyticPrim32) == 32, "BloomAnalyticPrim32 must be 32 bytes");

typedef struct {
    uint16_t bounds_q14_2[4]; /*  8 B */
    uint32_t rgba8_fill;      /*  4 B */
    uint32_t rgba8_border;    /*  4 B */
    uint8_t  radii_q6_2[4];   /*  4 B */
    uint8_t  borders_q6_2[4]; /*  4 B */
    uint16_t clip_id;         /*  2 B */
    uint16_t style_id;        /*  2 B */
    uint32_t z_flags;         /*  4 B */
} BloomQuadHot;
BLOOM_STATIC_ASSERT(sizeof(BloomQuadHot) == 32, "BloomQuadHot must be 32 bytes");

typedef struct {
    uint16_t bounds_q14_2[4];    /*  8 B */
    uint16_t shape_params[4];    /*  8 B: half_w, half_h, k1, k2 (f16) */
    uint16_t blur_r1_exp_sc[4];  /*  8 B: r1, exp, scale, pad (f16) */
    uint32_t rgba8;              /*  4 B */
    uint16_t clip_id;            /*  2 B */
    uint16_t z_flags;            /*  2 B */
} BloomShadowHot;
BLOOM_STATIC_ASSERT(sizeof(BloomShadowHot) == 32, "BloomShadowHot must be 32 bytes");

typedef struct {
    uint16_t dst_q14_2[4];  /*  8 B */
    uint16_t uv_unorm16[4]; /*  8 B */
    uint32_t rgba8;         /*  4 B */
    uint16_t clip_id;       /*  2 B */
    uint16_t z_flags;       /*  2 B */
} BloomGlyphInst;
BLOOM_STATIC_ASSERT(sizeof(BloomGlyphInst) == 24, "BloomGlyphInst must be 24 bytes");

typedef struct {
    uint16_t dst_q14_2[4];   /*  8 B: x, y, w, h */
    uint16_t uv_unorm16[4];  /*  8 B: u0, v0, uw, vh */
    uint32_t tint_rgba8;     /*  4 B */
    uint16_t tex_id;         /*  2 B: 0=sprite atlas, 1..N=tex array */
    uint16_t clip_id;        /*  2 B */
    uint32_t z_flags;        /*  4 B: z[15:0] | rad_q6_2[23:16] | flags[31:24] */
    uint16_t ui_slot;        /*  2 B */
    uint16_t pad0;           /*  2 B */
} BloomSpriteInst;
BLOOM_STATIC_ASSERT(sizeof(BloomSpriteInst) == 32, "BloomSpriteInst must be 32 bytes");

enum {
    BLOOM_MAT_FLAG_PBR          = 1u << 0,
    BLOOM_MAT_FLAG_DOUBLE_SIDED = 1u << 1
};

typedef struct {
    uint32_t base_rgba8;       /*  4 B */
    uint32_t emissive_rgba8;   /*  4 B */
    uint16_t metallic_f16;     /*  2 B */
    uint16_t roughness_f16;    /*  2 B */
    uint16_t ao_f16;           /*  2 B */
    uint16_t normal_scale_f16; /*  2 B */
    uint16_t albedo_tex;       /*  2 B: 0=none, 1..N=1-based tex array slice */
    uint16_t mr_tex;           /*  2 B: G=roughness, B=metallic */
    uint16_t normal_tex;       /*  2 B: tangent-space normal map */
    uint16_t flags;            /*  2 B */
    uint32_t pad0[2];          /*  8 B */
} BloomMaterial32;
BLOOM_STATIC_ASSERT(sizeof(BloomMaterial32) == 32, "BloomMaterial32 must be 32 bytes");

enum {
    BLOOM_LIGHT_DIR   = 0u,
    BLOOM_LIGHT_POINT = 1u
};

typedef struct {
    float    pos_or_dir[3];    /* 12 B */
    float    range;            /*  4 B */
    uint32_t rgba8_color;      /*  4 B */
    float    intensity;        /*  4 B */
    uint32_t kind;             /*  4 B */
    uint32_t pad0;             /*  4 B */
} BloomLight32;
BLOOM_STATIC_ASSERT(sizeof(BloomLight32) == 32, "BloomLight32 must be 32 bytes");

typedef struct {
    float x;
    float y;
    float w;
    float h;
} BloomClipRect;
BLOOM_STATIC_ASSERT(sizeof(BloomClipRect) == 16, "BloomClipRect must be 16 bytes");

typedef struct {
    uint16_t dof_min_f16[3]; /*  6 B */
    uint16_t dof_max_f16[3]; /*  6 B */
    uint16_t template_id;    /*  2 B */
    uint16_t joint_mask;     /*  2 B */
} BloomConstraintSpec;
BLOOM_STATIC_ASSERT(sizeof(BloomConstraintSpec) == 16, "BloomConstraintSpec must be 16 bytes");

typedef struct {
    uint16_t bounds_q14_2[4]; /*  8 B */
    uint32_t fill_rgba8;      /*  4 B */
    uint32_t border_rgba8;    /*  4 B */
    uint16_t bind_obj_idx;    /*  2 B */
    uint8_t  kind;            /*  1 B: 0=panel, 1=button, 2=slider, 3=text, 4=vlist */
    uint8_t  radius_q6_2;     /*  1 B */
    uint8_t  bind_dof_slot;   /*  1 B */
    uint8_t  pad0;            /*  1 B */
    uint16_t id_len;          /*  2 B */
    uint16_t label_len;       /*  2 B */
} BloomHudNodeBin;
BLOOM_STATIC_ASSERT(sizeof(BloomHudNodeBin) == 28, "BloomHudNodeBin must be 28 bytes");

typedef struct {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t index_start;
    int32_t  base_vertex;
    uint32_t base_instance;
} BloomDrawIndirectArgs;
BLOOM_STATIC_ASSERT(sizeof(BloomDrawIndirectArgs) == 20, "BloomDrawIndirectArgs must be 20 bytes");

typedef struct {
    float    vp_w, vp_h;
    float    focal, frustum_half_tan;
    float    cam_pos[3];
    float    near_z;
    float    cam_right[3];
    float    far_z;
    float    cam_up[3];
    uint32_t total_meshlets;
    float    cam_fwd[3];
    uint32_t prim_count;
} BloomFrameUniforms;
BLOOM_STATIC_ASSERT(sizeof(BloomFrameUniforms) == 80, "BloomFrameUniforms must be 80 bytes");

typedef struct {
    uint32_t visible_px;
    uint32_t min_x;
    uint32_t min_y;
    uint32_t max_x;
    uint32_t max_y;
    uint32_t best_anchor;
    uint32_t part_sample;
    uint32_t min_depth_u32;
} BloomGroundedGpu32;
BLOOM_STATIC_ASSERT(sizeof(BloomGroundedGpu32) == 32, "BloomGroundedGpu32 must be 32 bytes");

typedef struct {
    uint32_t ground_id;
    uint32_t depth_bits;
    uint32_t px;
    uint32_t py;
} BloomPickGpuResult;
BLOOM_STATIC_ASSERT(sizeof(BloomPickGpuResult) == 16, "BloomPickGpuResult must be 16 bytes");

/* ============================================================================
 * Layer 0 Host / External Simulation Projection & Camera / Input Types
 * ============================================================================ */

enum {
    BLOOM_PROJ_POS   = 1u << 0,
    BLOOM_PROJ_QUAT  = 1u << 1,
    BLOOM_PROJ_SCALE = 1u << 2,
    BLOOM_PROJ_DOF   = 1u << 3,
    BLOOM_PROJ_COLOR = 1u << 4,
    BLOOM_PROJ_CLIP  = 1u << 5,
    BLOOM_PROJ_FLAGS = 1u << 6
};

enum {
    BLOOM_OBJ_FLAG_CUTAWAY = 1u << 0,
    BLOOM_OBJ_FLAG_HIDDEN  = 1u << 1,
    BLOOM_OBJ_FLAG_PBR     = 1u << 2
};

typedef struct {
    uint64_t node_key; /* bloom_fnv1a64(object_id) */
    float    pos[3];
    float    quat[4];
    float    scale[3];
    float    dof[3];
    float    clip_plane[4];
    uint32_t rgba8;
    uint32_t flags;
    uint32_t field_mask;
} BloomStateProjection;

typedef struct {
    float pos[3];
    float yaw;
    float pitch;
    float focal;
    float near_z;
    float far_z;
} BloomCamera;

typedef enum {
    BLOOM_EVENT_NONE = 0,
    BLOOM_EVENT_POINTER_MOVE = 1,
    BLOOM_EVENT_POINTER_DOWN = 2,
    BLOOM_EVENT_POINTER_UP = 3,
    BLOOM_EVENT_SCROLL = 4,
    BLOOM_EVENT_KEY_DOWN = 5,
    BLOOM_EVENT_RESIZE = 6,
    BLOOM_EVENT_KEY_UP = 7,
    BLOOM_EVENT_TEXT_INPUT = 8,
    BLOOM_EVENT_IME_PREEDIT = 9,
    BLOOM_EVENT_PINCH = 10
} BloomInputEventType;

enum {
    BLOOM_MOD_SHIFT = 1u << 0,
    BLOOM_MOD_CTRL  = 1u << 1,
    BLOOM_MOD_ALT   = 1u << 2,
    BLOOM_MOD_SUPER = 1u << 3
};

typedef struct {
    BloomInputEventType type;
    float    x;
    float    y;
    float    dx;
    float    dy;
    uint32_t button;
    uint32_t key_code;
    uint32_t width;
    uint32_t height;
    uint32_t modifiers;
    float    scale_delta;
    char     text[64];
    int32_t  preedit_cursor;
    int32_t  preedit_len;
} BloomInputEvent;

/* ============================================================================
 * Inline Scalar Math & Conversion Utilities
 * ============================================================================ */

static inline uint64_t bloom_fnv1a64(const char *s) {
    uint64_t h = UINT64_C(0xCBF29CE484222325);
    if (!s) return h;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        h ^= (uint64_t)(*p);
        h *= UINT64_C(0x100000001B3);
    }
    return h;
}

static inline uint64_t bloom_fnv1a64_bytes(const void *data, size_t n) {
    uint64_t h = UINT64_C(0xCBF29CE484222325);
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; ++i) {
        h ^= (uint64_t)p[i];
        h *= UINT64_C(0x100000001B3);
    }
    return h;
}

static inline uint16_t bloom_f32_to_f16(float value) {
    uint32_t f = 0;
    if (value == 0.0f) value = 0.0f;
    memcpy(&f, &value, sizeof(f));
    uint32_t sign = (f >> 16u) & 0x8000u;
    int32_t exp = (int32_t)((f >> 23u) & 0xFFu) - 127 + 15;
    uint32_t mant = f & 0x7FFFFFu;
    if ((f & 0x7FFFFFFFu) == 0u) return (uint16_t)sign;
    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        mant = (mant | 0x800000u) >> (uint32_t)(1 - exp);
        return (uint16_t)(sign | ((mant + 0x1000u) >> 13u));
    }
    if (exp >= 31) return (uint16_t)(sign | 0x7C00u);
    uint32_t h = sign | ((uint32_t)exp << 10u) | (mant >> 13u);
    if ((mant & 0x1FFFu) > 0x1000u || ((mant & 0x1FFFu) == 0x1000u && (h & 1u))) h += 1u;
    return (uint16_t)h;
}

static inline float bloom_f16_to_f32(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16u;
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
    memcpy(&out, &f, sizeof(out));
    return out;
}

static inline float bloom_q16_to_f32(int32_t q16) {
    return (float)((double)q16 / 65536.0);
}

static inline uint16_t bloom_pack_pos_unorm16(float x, float lo, float span) {
    float t = (x - lo) / span;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    long q = lround((double)t * 65535.0);
    if (q < 0) q = 0;
    if (q > 65535) q = 65535;
    return (uint16_t)q;
}

static inline float bloom_unpack_pos_unorm16(uint16_t q, float lo, float span) {
    return lo + (float)q * (span / 65535.0f);
}

static inline int8_t bloom_clamp_i8_sym(float v) {
    long q = lround((double)v * 127.0);
    if (q < -127) q = -127;
    if (q > 127) q = 127;
    return (int8_t)q;
}

static inline void bloom_oct_encode(float nx, float ny, float nz, int8_t out_i8[2]) {
    float l1 = fabsf(nx) + fabsf(ny) + fabsf(nz);
    if (l1 < 1e-6f) l1 = 1.0f;
    float ox = nx / l1;
    float oy = ny / l1;
    if (nz < 0.0f) {
        float sx = (ox >= 0.0f) ? 1.0f : -1.0f;
        float sy = (oy >= 0.0f) ? 1.0f : -1.0f;
        float tx = (1.0f - fabsf(oy)) * sx;
        float ty = (1.0f - fabsf(ox)) * sy;
        ox = tx;
        oy = ty;
    }
    out_i8[0] = bloom_clamp_i8_sym(ox);
    out_i8[1] = bloom_clamp_i8_sym(oy);
}

static inline void bloom_oct_decode(const int8_t in_i8[2], float out_n[3]) {
    float ox = (float)in_i8[0] / 127.0f;
    float oy = (float)in_i8[1] / 127.0f;
    float nz = 1.0f - fabsf(ox) - fabsf(oy);
    if (nz < 0.0f) {
        float sx = (ox >= 0.0f) ? 1.0f : -1.0f;
        float sy = (oy >= 0.0f) ? 1.0f : -1.0f;
        float tx = (1.0f - fabsf(oy)) * sx;
        float ty = (1.0f - fabsf(ox)) * sy;
        ox = tx;
        oy = ty;
    }
    float len = sqrtf(ox * ox + oy * oy + nz * nz);
    if (len < 1e-6f) len = 1.0f;
    out_n[0] = ox / len;
    out_n[1] = oy / len;
    out_n[2] = nz / len;
}

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_ABI_H */
