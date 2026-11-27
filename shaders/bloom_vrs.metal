#include <metal_stdlib>
using namespace metal;

struct BloomVrsUniforms {
    float2 screen_size;
    float2 phys_size;
};

struct VrsResolveOut {
    float4 pos [[position]];
};

vertex VrsResolveOut v_vrs_resolve(uint vid [[vertex_id]]) {
    float2 pos[3] = {
        float2(-1.0f, -1.0f),
        float2( 3.0f, -1.0f),
        float2(-1.0f,  3.0f)
    };
    VrsResolveOut o;
    o.pos = float4(pos[vid], 0.9999f, 1.0f);
    return o;
}

fragment half4 f_vrs_resolve(VrsResolveOut in [[stage_in]],
                             texture2d<half, access::sample> phys_tex [[texture(0)]],
                             constant BloomVrsUniforms &vu [[buffer(0)]],
                             constant rasterization_rate_map_data &rrm_data [[buffer(1)]]) {
    constexpr sampler s_lin(coord::normalized, address::clamp_to_edge, filter::linear);
    rasterization_rate_map_decoder dec(rrm_data);
    float2 phys_px = dec.map_screen_to_physical_coordinates(in.pos.xy);
    float2 puv = phys_px / max(vu.phys_size, float2(1.0f));
    return phys_tex.sample(s_lin, puv);
}
