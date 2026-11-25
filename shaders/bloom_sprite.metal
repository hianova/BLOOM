#include <metal_stdlib>
using namespace metal;

struct BloomSpriteInst {
    ushort d0, d1, d2, d3;
    ushort u0, u1, u2, u3;
    uint tint_rgba8;
    ushort tex_id, clip_id;
    uint z_flags;
    ushort ui_slot, pad0;
};

struct BloomClipRect { float4 rect; };

struct BloomFrameUniforms {
    float vp_w, vp_h, focal, frustum_half_tan;
    packed_float3 cam_pos; float near_z;
    packed_float3 cam_right; float far_z;
    packed_float3 cam_up; uint total_meshlets;
    packed_float3 cam_fwd; uint prim_count;
};

static_assert(sizeof(BloomSpriteInst) == 32, "MSL BloomSpriteInst");

struct SpriteIn {
    float4 pos [[position]];
    uint idx [[flat]];
};

vertex SpriteIn v_hud_sprite(uint vid [[vertex_id]], uint iid [[instance_id]],
                             device const BloomSpriteInst *sprites [[buffer(0)]],
                             device const BloomClipRect *clips [[buffer(1)]],
                             constant BloomFrameUniforms &u [[buffer(2)]]) {
    BloomSpriteInst sp = sprites[iid];
    BloomClipRect c = clips[sp.clip_id];
    float4 b = float4(float(sp.d0), float(sp.d1), float(sp.d2), float(sp.d3)) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy), p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    if (p1.x <= p0.x || p1.y <= p0.y) return SpriteIn{float4(0.0f, 0.0f, -2.0f, 1.0f), iid};
    float2 px = mix(p0, p1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float2 ndc = float2((px.x / u.vp_w) * 2.0f - 1.0f, 1.0f - (px.y / u.vp_h) * 2.0f);
    float z = 0.14f - clamp(float(sp.z_flags & 0xFFFFu) * 1e-5f, 0.0f, 0.12f);
    return SpriteIn{float4(ndc, z, 1.0f), iid};
}

fragment half4 f_hud_sprite(SpriteIn in [[stage_in]],
                            device const BloomSpriteInst *sprites [[buffer(0)]],
                            texture2d<half> sprite_atlas [[texture(0)]]) {
    BloomSpriteInst sp = sprites[in.idx];
    float4 b = float4(float(sp.d0), float(sp.d1), float(sp.d2), float(sp.d3)) * 0.25f;
    float2 luv = (in.pos.xy - b.xy) / max(b.zw, float2(1e-4f));
    float4 uvr = float4(float(sp.u0), float(sp.u1), float(sp.u2), float(sp.u3)) * (1.0f / 65535.0f);
    constexpr sampler smp_lin(mag_filter::linear, min_filter::linear);
    constexpr sampler smp_near(mag_filter::nearest, min_filter::nearest);
    float2 suv = uvr.xy + luv * uvr.zw;
    half4 texel = ((sp.z_flags & (1u << 24u)) != 0u)
                      ? sprite_atlas.sample(smp_near, suv)
                      : sprite_atlas.sample(smp_lin, suv);
    half4 col = texel * unpack_unorm4x8_to_half(sp.tint_rgba8);

    half r = half((sp.z_flags >> 16u) & 0xFFu) * 0.25h;
    if (r > 0.1h) {
        half2 hs = half2(b.zw) * 0.5h;
        half2 c2p = (half2(in.pos.xy) - half2(b.xy)) - hs;
        half2 d = abs(c2p) - hs + r;
        half outer = length(max(d, 0.0h)) + min(max(d.x, d.y), 0.0h) - r;
        col.a *= clamp(0.5h - outer, 0.0h, 1.0h);
    }
    return col;
}
