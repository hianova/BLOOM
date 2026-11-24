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

struct BloomObjectState48 {
    float px, py, pz;
    half qx, qy, qz, qw;
    half sx, sy, sz, dof0, dof1, dof2;
    uint rgba8_mat;
    half cp0, cp1, cp2, cp3;
    uint flags;
};

struct BloomMaterial32 {
    uint base_rgba8, emissive_rgba8;
    half metallic, roughness, ao, normal_scale;
    ushort albedo_tex, mr_tex, normal_tex, flags;
    uint pad0, pad1;
};

struct BloomLight32 {
    packed_float3 pos_or_dir;
    float range;
    uint rgba8_color;
    float intensity;
    uint kind, pad0;
};

struct BloomFrameUniforms {
    float vp_w, vp_h, focal, frustum_half_tan;
    packed_float3 cam_pos; float near_z;
    packed_float3 cam_right; float far_z;
    packed_float3 cam_up; uint total_meshlets;
    packed_float3 cam_fwd; uint prim_count;
};

static_assert(sizeof(BloomMaterial32) == 32, "MSL BloomMaterial32");
static_assert(sizeof(BloomLight32) == 32, "MSL BloomLight32");

inline float3 rotate_quat(float4 q, float3 v) {
    float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

inline float3 oct_decode_gpu(char ox, char oy) {
    float2 o = float2(float(ox), float(oy)) * (1.0f / 127.0f);
    float nz = 1.0f - abs(o.x) - abs(o.y);
    if (nz < 0.0f) {
        float2 s = float2(o.x >= 0.0f ? 1.0f : -1.0f, o.y >= 0.0f ? 1.0f : -1.0f);
        o = (1.0f - abs(o.yx)) * s;
    }
    return normalize(float3(o.x, o.y, nz));
}

struct PbrVertOut {
    float4 pos [[position]];
    float clip_d [[clip_distance]] [1];
    float3 wp;
    float2 uv;
    half3 nrm;
    half4 col;
    uint mat_id [[flat]];
};

struct PbrFragIn {
    float4 pos [[position]];
    float3 wp;
    float2 uv;
    half3 nrm;
    half4 col;
    uint mat_id [[flat]];
};

vertex PbrVertOut v_meshlet_pbr(uint vid [[vertex_id]], uint iid [[instance_id]],
                                device const uint *surviving_ids [[buffer(0)]],
                                device const BloomMeshletHeader32 *hdrs [[buffer(1)]],
                                device const BloomVtx12 *verts [[buffer(2)]],
                                device const uchar *local_idx [[buffer(3)]],
                                device const BloomObjectState48 *states [[buffer(4)]],
                                constant BloomFrameUniforms &u [[buffer(5)]]) {
    BloomMeshletHeader32 m = hdrs[surviving_ids[iid]];
    PbrVertOut o;
    if (vid >= uint(m.tri_cnt) * 3u) {
        o.pos = float4(0.0f, 0.0f, -2.0f, 1.0f);
        o.clip_d[0] = -1.0f; o.wp = float3(0.0f); o.uv = float2(0.0f);
        o.nrm = half3(0.0h, 1.0h, 0.0h); o.col = half4(0.0h); o.mat_id = 0u;
        return o;
    }
    BloomVtx12 v = verts[m.vtx_offset + uint(local_idx[m.idx_offset + vid])];
    BloomObjectState48 st = states[m.obj_id];
    float3 lp = (float3(-1.5f) + float3(float(v.px), float(v.py), float(v.pz)) * (3.0f / 65535.0f)) *
                float3(float(st.sx), float(st.sy), float(st.sz));
    uint jmod = uint(v.joint_id % 3u);
    float dof = float(jmod == 0u ? st.dof0 : (jmod == 1u ? st.dof1 : st.dof2));
    lp.y += sin(dof + lp.x) * 0.08f;
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    float3 d = wp - float3(u.cam_pos);
    float3 vp = float3(dot(d, float3(u.cam_right)), dot(d, float3(u.cam_up)), dot(d, float3(u.cam_fwd)));
    float z = max(vp.z, 0.25f);
    float aspect = u.vp_w / u.vp_h;
    o.pos = float4((vp.x * u.focal) / (z * aspect), (vp.y * u.focal) / z,
                   clamp(0.20f + (1.0f - (2.0f / z)) * 0.78f, 0.20f, 0.98f), 1.0f);
    o.clip_d[0] = ((st.flags & 1u) != 0u) ? (dot(lp, float3(float(st.cp0), float(st.cp1), float(st.cp2))) - float(st.cp3)) : 1.0f;
    o.wp = wp;
    o.uv = float2(float(v.u), float(v.v)) * (1.0f / 255.0f);
    o.nrm = half3(rotate_quat(q, oct_decode_gpu(v.oct_x, v.oct_y)));
    o.col = unpack_unorm4x8_to_half(st.rgba8_mat);
    o.mat_id = (st.flags >> 16u) & 0xFFFFu;
    return o;
}

inline float3 perturb_normal(float3 N, float3 wp, float2 uv, half3 map_n, float scale) {
    float3 dp1 = dfdx(wp), dp2 = dfdy(wp);
    float2 duv1 = dfdx(uv), duv2 = dfdy(uv);
    float3 dp2p = cross(dp2, N), dp1p = cross(N, dp1);
    float3 T = dp2p * duv1.x + dp1p * duv2.x;
    float3 B = dp2p * duv1.y + dp1p * duv2.y;
    float invmax = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-8f));
    float3 tn = float3(map_n) * 2.0f - 1.0f;
    tn.xy *= scale;
    return normalize(T * (tn.x * invmax) + B * (tn.y * invmax) + N * tn.z);
}

inline float d_ggx(float ndh, float a2) {
    float d = ndh * ndh * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.1415926f * d * d, 1e-5f);
}

inline float g_smith(float ndv, float ndl, float r) {
    float k = (r + 1.0f) * (r + 1.0f) * 0.125f;
    float gv = ndv / (ndv * (1.0f - k) + k);
    float gl = ndl / (ndl * (1.0f - k) + k);
    return gv * gl;
}

fragment half4 f_meshlet_pbr(PbrFragIn in [[stage_in]],
                             device const BloomMaterial32 *materials [[buffer(0)]],
                             device const BloomLight32 *lights [[buffer(1)]],
                             constant uint4 &light_meta [[buffer(2)]],
                             constant BloomFrameUniforms &u [[buffer(3)]],
                             texture2d_array<half, access::sample> tex_arr [[texture(0)]]) {
    BloomMaterial32 mat = materials[in.mat_id];
    half3 N0 = normalize(in.nrm);
    if ((mat.flags & 1u) == 0u) {
        half ndotl = clamp(dot(N0, normalize(half3(0.4h, 0.7h, -0.5h))), 0.18h, 1.0h);
        half rim = pow(clamp(1.0h - abs(N0.z), 0.0h, 1.0h), 2.0h) * 0.28h;
        return half4(in.col.rgb * ndotl + half3(0.10h, 0.25h, 0.35h) * rim, 1.0h);
    }

    constexpr sampler s_pbr(coord::normalized, address::repeat, filter::linear, mip_filter::linear);
    float3 albedo = float3(in.col.rgb) * float3(unpack_unorm4x8_to_half(mat.base_rgba8).rgb);
    if (mat.albedo_tex > 0u) albedo *= float3(tex_arr.sample(s_pbr, in.uv, mat.albedo_tex - 1u).rgb);
    float rough = clamp(float(mat.roughness), 0.04f, 1.0f);
    float metal = clamp(float(mat.metallic), 0.0f, 1.0f);
    if (mat.mr_tex > 0u) {
        float3 mr = float3(tex_arr.sample(s_pbr, in.uv, mat.mr_tex - 1u).rgb);
        rough = clamp(rough * mr.y, 0.04f, 1.0f);
        metal = clamp(metal * mr.z, 0.0f, 1.0f);
    }
    float3 N = float3(N0);
    if (mat.normal_tex > 0u) {
        half3 nm = tex_arr.sample(s_pbr, in.uv, mat.normal_tex - 1u).xyz;
        N = perturb_normal(N, in.wp, in.uv, nm, float(mat.normal_scale));
    }

    float3 V = normalize(float3(u.cam_pos) - in.wp);
    float ndv = max(dot(N, V), 1e-4f);
    float3 F0 = mix(float3(0.04f), albedo, metal);
    float a2 = rough * rough * rough * rough;

    float3 Lo = float3(0.0f);
    uint lcnt = min(light_meta.x, 16u);
    for (uint li = 0u; li < lcnt; ++li) {
        BloomLight32 lt = lights[li];
        float3 L;
        float att = lt.intensity;
        if (lt.kind == 0u) {
            L = normalize(-float3(lt.pos_or_dir));
        } else {
            float3 to_l = float3(lt.pos_or_dir) - in.wp;
            float dist = length(to_l);
            L = to_l / max(dist, 1e-4f);
            float rg = max(lt.range, 0.1f);
            float f = clamp(1.0f - (dist * dist) / (rg * rg), 0.0f, 1.0f);
            att *= (f * f) / (1.0f + dist * dist * 0.25f);
        }
        float ndl = max(dot(N, L), 0.0f);
        if (ndl <= 0.0f) continue;
        float3 H = normalize(V + L);
        float ndh = max(dot(N, H), 0.0f);
        float hdv = max(dot(H, V), 0.0f);
        float D = d_ggx(ndh, a2);
        float G = g_smith(ndv, ndl, rough);
        float3 F = F0 + (1.0f - F0) * pow(clamp(1.0f - hdv, 0.0f, 1.0f), 5.0f);
        float3 spec = (D * G * F) / max(4.0f * ndv * ndl, 1e-4f);
        float3 kD = (1.0f - F) * (1.0f - metal);
        float3 lcol = float3(unpack_unorm4x8_to_half(lt.rgba8_color).rgb) * att;
        Lo += (kD * albedo * (1.0f / 3.1415926f) + spec) * lcol * ndl;
    }

    float3 R = reflect(-V, N);
    float3 sky_ir = mix(float3(0.10f, 0.12f, 0.16f), float3(0.42f, 0.54f, 0.72f), N.y * 0.5f + 0.5f);
    float3 env_col = mix(float3(0.08f, 0.10f, 0.14f), float3(0.85f, 0.92f, 1.0f), smoothstep(-0.2f, 0.8f, R.y));
    float3 Fr = F0 + (max(float3(1.0f - rough), F0) - F0) * pow(1.0f - ndv, 5.0f);
    float3 ambient = (1.0f - Fr) * (1.0f - metal) * sky_ir * albedo + Fr * env_col * (1.0f - rough * 0.65f);
    float3 em = float3(unpack_unorm4x8_to_half(mat.emissive_rgba8).rgb);
    float3 col = ambient * float(mat.ao) + Lo + em;
    col = (col * (2.51f * col + 0.03f)) / (col * (2.43f * col + 0.59f) + 0.14f);
    return half4(half3(clamp(col, 0.0f, 1.0f)), 1.0h);
}
