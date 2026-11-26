#include <metal_stdlib>
using namespace metal;

struct BloomVtx12 {
    ushort px, py, pz;
    char oct_x, oct_y;
    uchar u, v, joint_id, mat_id;
};

struct BloomMeshletHeader32 {
    uint vtx_offset, idx_offset;
    ushort vtx_cnt, tri_cnt, obj_id, flags;
    half bs_x, bs_y, bs_z, bs_r;
    char cone_x, cone_y, cone_z, cone_c;
    uint aabb_scale_tag;
};

struct VisionHypGpu {
    float px, py, pz, yaw, pitch, roll, scale, dof0;
    float score_out, iou_out, lum_err, edge_err;
    uint tpl_id, pad0, pad1, pad2;
};

struct ObsCell64 {
    short sx, sy;
    float fg, lum;
};

struct ObsSummaryAtomic {
    atomic_uint sum_x, sum_y, fg_cnt, pad0;
};

struct VisionParamsGpu {
    uint img_w, img_h, first_m, hyps_base;
    float focal, aspect;
    uint tile_base, n_hyps;
    packed_float3 cam_pos;   float pad0;
    packed_float3 cam_right; float pad1;
    packed_float3 cam_up;    float pad2;
    packed_float3 cam_fwd;   float pad3;
};

static_assert(sizeof(VisionHypGpu) == 64, "MSL VisionHypGpu");
static_assert(sizeof(VisionParamsGpu) == 96, "MSL VisionParamsGpu");

inline float3 oct_dec(char ox, char oy) {
    float2 f = float2(float(ox), float(oy)) / 127.0f;
    float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    if (n.z < 0.0f) {
        float2 s = float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
        float2 w = (1.0f - abs(float2(n.y, n.x))) * s;
        n.x = w.x; n.y = w.y;
    }
    return normalize(n);
}

inline float3 mul_euler(float yaw, float pitch, float roll, float3 v) {
    float cy = cos(yaw), sy = sin(yaw), cp = cos(pitch), sp = sin(pitch), cr = cos(roll), sr = sin(roll);
    return float3(
        (cy * cr + sy * sp * sr) * v.x + (cr * sy * sp - cy * sr) * v.y + (cp * sy) * v.z,
        (cp * sr) * v.x                + (cp * cr) * v.y                + (-sp) * v.z,
        (cy * sp * sr - cr * sy) * v.x + (cy * cr * sp + sy * sr) * v.y + (cy * cp) * v.z
    );
}

inline float sample_avg4(texture2d<half, access::read> cam_tex, uint w, uint h, int bx, int by) {
    float acc = 0.0f;
    if (w == 256u && h == 256u) {
        for (int dy = 0; dy < 4; ++dy) {
            for (int dx = 0; dx < 4; ++dx) {
                uint2 p = uint2(clamp(bx * 4 + dx, 0, 255), clamp(by * 4 + dy, 0, 255));
                half4 c = cam_tex.read(p);
                acc += float(0.299h * c.r + 0.587h * c.g + 0.114h * c.b);
            }
        }
        return acc * (1.0f / 16.0f);
    }
    float sx = float(w) / 64.0f, sy = float(h) / 64.0f;
    for (int dy = 0; dy < 4; ++dy) {
        for (int dx = 0; dx < 4; ++dx) {
            int px = clamp(int((float(bx) + (float(dx) + 0.5f) * 0.25f) * sx), 0, int(w) - 1);
            int py = clamp(int((float(by) + (float(dy) + 0.5f) * 0.25f) * sy), 0, int(h) - 1);
            half4 c = cam_tex.read(uint2(uint(px), uint(py)));
            acc += float(0.299h * c.r + 0.587h * c.g + 0.114h * c.b);
        }
    }
    return acc * (1.0f / 16.0f);
}

kernel void k_vision_prep_64(texture2d<half, access::read> cam_tex [[texture(0)]],
                             device ObsCell64 *obs64 [[buffer(0)]],
                             device ObsSummaryAtomic *summary [[buffer(1)]],
                             constant VisionParamsGpu &vp [[buffer(2)]],
                             uint2 tid [[thread_position_in_threadgroup]],
                             uint2 gid [[thread_position_in_grid]]) {
    threadgroup float tg_lum[10][24];
    float c = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x), int(gid.y));
    tg_lum[tid.y + 1u][tid.x + 1u] = c;
    if (tid.x == 0u) tg_lum[tid.y + 1u][0u] = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x) - 1, int(gid.y));
    if (tid.x == 7u) tg_lum[tid.y + 1u][9u] = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x) + 1, int(gid.y));
    if (tid.y == 0u) tg_lum[0u][tid.x + 1u] = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x), int(gid.y) - 1);
    if (tid.y == 7u) tg_lum[9u][tid.x + 1u] = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x), int(gid.y) + 1);
    threadgroup_barrier(mem_flags::mem_threadgroup);

    bool in_b = (gid.x < 64u && gid.y < 64u);
    uint sx = 0u, sy = 0u, sc = 0u;
    if (in_b) {
        float fg = clamp((c - 0.095f) * 20.0f, 0.0f, 1.0f);
        float l_norm = clamp(c / 0.773f, 0.0f, 1.0f);
        float gx = -tg_lum[tid.y + 1u][tid.x] + tg_lum[tid.y + 1u][tid.x + 2u];
        float gy = -tg_lum[tid.y][tid.x + 1u] + tg_lum[tid.y + 2u][tid.x + 1u];
        bool is_edge = (sqrt(gx * gx + gy * gy) > 0.070f) && (gid.x > 8u || gid.y > 10u);
        uint idx = gid.y * 64u + gid.x;
        obs64[idx] = ObsCell64{is_edge ? short(gid.x) : short(-1), is_edge ? short(gid.y) : short(-1), fg, l_norm};
        if (fg > 0.45f && (gid.x > 12u || gid.y > 12u)) {
            sx = gid.x; sy = gid.y; sc = 1u;
        }
    }
    uint gs_x = simd_sum(sx), gs_y = simd_sum(sy), gs_c = simd_sum(sc);
    if (simd_is_first() && gs_c != 0u) {
        atomic_fetch_add_explicit(&summary->sum_x, gs_x, memory_order_relaxed);
        atomic_fetch_add_explicit(&summary->sum_y, gs_y, memory_order_relaxed);
        atomic_fetch_add_explicit(&summary->fg_cnt, gs_c, memory_order_relaxed);
    }
}

kernel void k_vision_jfa64(device const ObsCell64 *src [[buffer(0)]],
                           device ObsCell64 *dst [[buffer(1)]],
                           constant int &step [[buffer(2)]],
                           uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= 64u || gid.y >= 64u) return;
    ObsCell64 best = src[gid.y * 64u + gid.x];
    float best_d2 = (best.sx >= 0) ? float((int(gid.x) - int(best.sx)) * (int(gid.x) - int(best.sx)) +
                                           (int(gid.y) - int(best.sy)) * (int(gid.y) - int(best.sy))) : 1e9f;
    for (int dy = -1; dy <= 1; ++dy) {
        int ny = int(gid.y) + dy * step;
        if (ny < 0 || ny >= 64) continue;
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = int(gid.x) + dx * step;
            if (nx < 0 || nx >= 64) continue;
            ObsCell64 c = src[uint(ny) * 64u + uint(nx)];
            if (c.sx < 0) continue;
            float rx = float(int(gid.x) - int(c.sx)), ry = float(int(gid.y) - int(c.sy));
            float d2 = rx * rx + ry * ry;
            if (d2 < best_d2) { best_d2 = d2; best.sx = c.sx; best.sy = c.sy; }
        }
    }
    dst[gid.y * 64u + gid.x] = best;
}

struct VisionAtlasOut {
    float4 pos [[position]];
    float clip_d [[clip_distance]] [4];
    half3 nrm;
};

struct VisionAtlasFragIn {
    float4 pos [[position]];
    half3 nrm;
};

vertex VisionAtlasOut v_vision_atlas(uint vid [[vertex_id]], uint iid [[instance_id]],
                                     device const VisionHypGpu *hyps [[buffer(0)]],
                                     device const BloomMeshletHeader32 *hdrs [[buffer(1)]],
                                     device const BloomVtx12 *verts [[buffer(2)]],
                                     device const uchar *lidx [[buffer(3)]],
                                     constant VisionParamsGpu &vp [[buffer(4)]]) {
    uint local_m = vid / 288u;
    uint lv_i = vid % 288u;
    BloomMeshletHeader32 m = hdrs[vp.first_m + local_m];
    VisionAtlasOut o;
    if (lv_i >= uint(m.tri_cnt) * 3u) {
        o.pos = float4(0.0f, 0.0f, -2.0f, 1.0f);
        o.nrm = half3(0.0h, 1.0h, 0.0h);
        o.clip_d[0] = -1.0f; o.clip_d[1] = -1.0f; o.clip_d[2] = -1.0f; o.clip_d[3] = -1.0f;
        return o;
    }
    VisionHypGpu h = hyps[vp.hyps_base + iid];
    BloomVtx12 v = verts[m.vtx_offset + uint(lidx[m.idx_offset + lv_i])];
    float3 lp = (float3(-1.5f) + float3(float(v.px), float(v.py), float(v.pz)) * (3.0f / 65535.0f)) * h.scale;
    float dof = ((v.joint_id % 3u) == 0u) ? h.dof0 : 0.0f;
    lp.y += sin(dof + lp.x) * 0.08f;
    float3 wp = mul_euler(h.yaw, h.pitch, h.roll, lp) + float3(h.px, h.py, h.pz);
    float3 wn = normalize(mul_euler(h.yaw, h.pitch, h.roll, oct_dec(v.oct_x, v.oct_y)));

    float3 d = wp - float3(vp.cam_pos);
    float3 cv = float3(dot(d, float3(vp.cam_right)), dot(d, float3(vp.cam_up)), dot(d, float3(vp.cam_fwd)));
    float z = max(cv.z, 0.25f);
    float2 loc = float2((cv.x * vp.focal) / (z * vp.aspect), (cv.y * vp.focal) / z);
    o.clip_d[0] = 1.0f + loc.x;
    o.clip_d[1] = 1.0f - loc.x;
    o.clip_d[2] = 1.0f + loc.y;
    o.clip_d[3] = 1.0f - loc.y;

    float pz = clamp(0.10f + (1.0f - (1.0f / z)) * 0.85f, 0.05f, 0.98f);
    uint tile_i = vp.tile_base + iid;
    uint tx = tile_i & 15u, ty = tile_i >> 4u;
    float gx = -1.0f + (2.0f / 16.0f) * (float(tx) + 0.5f + 0.5f * loc.x);
    float gy =  1.0f - (2.0f / 8.0f)  * (float(ty) + 0.5f - 0.5f * loc.y);
    o.pos = float4(gx, gy, pz, 1.0f);
    o.nrm = half3(wn);
    return o;
}

fragment half4 f_vision_atlas(VisionAtlasFragIn in [[stage_in]]) {
    half3 n = normalize(in.nrm);
    half ndotl = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.18h, 1.0h);
    half r1 = clamp(1.0h - abs(n.z), 0.0h, 1.0h);
    half rim = r1 * r1 * 0.28h;
    half lum = clamp(ndotl + rim * 0.28h, 0.18h, 1.0h);
    return half4(1.0h, lum, lum, 1.0h);
}

kernel void k_vision_score_tiles(texture2d<half, access::read> atlas [[texture(0)]],
                                 device const ObsCell64 *obs64 [[buffer(0)]],
                                 device VisionHypGpu *hyps [[buffer(1)]],
                                 constant VisionParamsGpu &vp [[buffer(2)]],
                                 uint tid [[thread_index_in_threadgroup]],
                                 uint tgid [[threadgroup_position_in_grid]],
                                 uint s_lid [[thread_index_in_simdgroup]],
                                 uint s_gid [[simdgroup_index_in_threadgroup]]) {
    if (tgid >= vp.n_hyps) return;
    threadgroup float tg_inter[8], tg_union[8], tg_lum[8], tg_dt[8], tg_ecnt[8];
    uint tile_i = vp.tile_base + tgid;
    uint tx = tile_i & 15u, ty = tile_i >> 4u;
    uint bx = tx * 64u, by = ty * 64u;
    float s_inter = 0.0f, s_union = 0.0f, s_lum = 0.0f, s_dt = 0.0f, s_ecnt = 0.0f;
    for (uint p = tid; p < 4096u; p += 256u) {
        uint lx = p & 63u, ly = p >> 6u;
        half2 rv = atlas.read(uint2(bx + lx, by + ly)).rg;
        float rm = float(rv.r), rl = float(rv.g);
        ObsCell64 o = obs64[p];
        s_inter += min(rm, o.fg);
        s_union += max(rm, o.fg);
        if (rm > 0.5f && o.fg > 0.5f) s_lum += abs(rl - o.lum);
        if (rm > 0.5f) {
            bool bnd = (lx == 0u || lx == 63u || ly == 0u || ly == 63u) ||
                       (atlas.read(uint2(bx + lx - 1u, by + ly)).r < 0.5h) ||
                       (atlas.read(uint2(bx + lx + 1u, by + ly)).r < 0.5h) ||
                       (atlas.read(uint2(bx + lx, by + ly - 1u)).r < 0.5h) ||
                       (atlas.read(uint2(bx + lx, by + ly + 1u)).r < 0.5h);
            if (bnd) {
                float d = (o.sx >= 0) ? sqrt(float((int(lx) - int(o.sx)) * (int(lx) - int(o.sx)) +
                                                   (int(ly) - int(o.sy)) * (int(ly) - int(o.sy)))) : 16.0f;
                s_dt += min(d, 16.0f);
                s_ecnt += 1.0f;
            }
        }
    }
    float w_inter = simd_sum(s_inter), w_union = simd_sum(s_union), w_lum = simd_sum(s_lum);
    float w_dt = simd_sum(s_dt), w_ecnt = simd_sum(s_ecnt);
    if (s_lid == 0u) {
        tg_inter[s_gid] = w_inter; tg_union[s_gid] = w_union; tg_lum[s_gid] = w_lum;
        tg_dt[s_gid] = w_dt; tg_ecnt[s_gid] = w_ecnt;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (s_gid == 0u) {
        bool v = (s_lid < 8u);
        float tot_inter = simd_sum(v ? tg_inter[s_lid] : 0.0f);
        float tot_union = simd_sum(v ? tg_union[s_lid] : 0.0f);
        float tot_lum   = simd_sum(v ? tg_lum[s_lid]   : 0.0f);
        float tot_dt    = simd_sum(v ? tg_dt[s_lid]    : 0.0f);
        float tot_ecnt  = simd_sum(v ? tg_ecnt[s_lid]  : 0.0f);
        if (s_lid == 0u) {
            float iou = (tot_union > 4.0f) ? (tot_inter / tot_union) : 0.0f;
            float lum_m = (tot_inter > 4.0f) ? (tot_lum / tot_inter) : 1.0f;
            float dt_m = (tot_ecnt > 4.0f) ? (tot_dt / tot_ecnt) : 16.0f;
            float loss = (1.0f - iou) * 24.0f + dt_m * 1.5f + lum_m * 8.0f;
            uint hi = vp.hyps_base + tgid;
            hyps[hi].iou_out = iou;
            hyps[hi].lum_err = lum_m;
            hyps[hi].edge_err = dt_m;
            hyps[hi].score_out = loss;
        }
    }
}
