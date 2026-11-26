#import <Metal/Metal.h>

#include "bloom_embedded_shaders.h"
#include "bloom_vision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

struct VisionHypGpu {
    float px, py, pz, yaw, pitch, roll, scale, dof0;
    float score_out, iou_out, lum_err, edge_err;
    uint32_t tpl_id, pad0, pad1, pad2;
};
BLOOM_STATIC_ASSERT(sizeof(VisionHypGpu) == 64, "VisionHypGpu must be 64 bytes");

struct ObsCell64 {
    int16_t sx, sy;
    float   fg, lum;
};
BLOOM_STATIC_ASSERT(sizeof(ObsCell64) == 12, "ObsCell64 must be 12 bytes");

struct VisionParamsGpu {
    uint32_t img_w, img_h, first_m, hyps_base;
    float    focal, aspect;
    uint32_t tile_base, n_hyps;
    float    cam_pos[3];   float pad0;
    float    cam_right[3]; float pad1;
    float    cam_up[3];    float pad2;
    float    cam_fwd[3];   float pad3;
};
BLOOM_STATIC_ASSERT(sizeof(VisionParamsGpu) == 96, "VisionParamsGpu must be 96 bytes");

struct RegisteredTpl {
    uint16_t template_id;
    char     name[32];
    uint32_t first_m;
    uint32_t mcnt;
    uint32_t vtx_draw_cnt;
    bool     has_dof;
};

struct BloomVision {
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> q = nil;
    id<MTLLibrary> lib = nil;
    id<MTLComputePipelineState> p_prep = nil;
    id<MTLComputePipelineState> p_jfa = nil;
    id<MTLComputePipelineState> p_score = nil;
    id<MTLRenderPipelineState> p_atlas = nil;
    id<MTLDepthStencilState> ds_3d = nil;

    id<MTLTexture> t_atlas = nil;
    id<MTLTexture> t_atlas_d = nil;
    id<MTLTexture> t_cam = nil;
    uint32_t cam_w = 0u, cam_h = 0u;

    id<MTLBuffer> b_obs64a = nil;
    id<MTLBuffer> b_obs64b = nil;
    id<MTLBuffer> b_sum = nil;
    id<MTLBuffer> b_hyps = nil;

    id<MTLBuffer> b_hdrs = nil;
    id<MTLBuffer> b_verts = nil;
    id<MTLBuffer> b_lidx = nil;

    std::vector<BloomMeshletHeader32> cpu_hdrs;
    std::vector<BloomVtx12> cpu_verts;
    std::vector<uint8_t> cpu_lidx;
    std::vector<RegisteredTpl> tpls;
    bool geom_dirty = false;
    uint32_t max_tg = 0u;
};

static id<MTLComputePipelineState> make_vis_cpso(id<MTLDevice> dev, id<MTLLibrary> lib, const char *fn) {
    NSError *err = nil;
    id<MTLFunction> f = [lib newFunctionWithName:[NSString stringWithUTF8String:fn]];
    if (!f) return nil;
    id<MTLComputePipelineState> p = [dev newComputePipelineStateWithFunction:f error:&err];
    if (!p || err) {
        fprintf(stderr, "Vision Compute PSO %s failed: %s\n", fn, err ? err.localizedDescription.UTF8String : "?");
        return nil;
    }
    return p;
}

static void euler_ypr_to_quat(float yaw, float pitch, float roll, float out_q[4]) {
    float cy = cosf(yaw * 0.5f), sy = sinf(yaw * 0.5f);
    float cp = cosf(pitch * 0.5f), sp = sinf(pitch * 0.5f);
    float cr = cosf(roll * 0.5f), sr = sinf(roll * 0.5f);
    /* q = q_y(yaw) * (q_x(pitch) * q_z(roll)) */
    float px = sp * cr, py = -sp * sr, pz = cp * sr, pw = cp * cr;
    out_q[0] = cy * px + sy * pz;
    out_q[1] = sy * pw + cy * py;
    out_q[2] = cy * pz - sy * px;
    out_q[3] = cy * pw - sy * py;
}

static void cam_basis_vis(const BloomCamera *cam, float r[3], float u[3], float f[3]) {
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    f[0] = sy * cp; f[1] = -sp; f[2] = cy * cp;
    r[0] = cy; r[1] = 0.0f; r[2] = -sy;
    u[0] = f[1] * r[2] - f[2] * r[1];
    u[1] = f[2] * r[0] - f[0] * r[2];
    u[2] = f[0] * r[1] - f[1] * r[0];
}

BloomVision *bloom_vision_create(const char *shader_path) {
    NSError *err = nil;
    NSString *src = nil;
    if (shader_path && shader_path[0]) {
        src = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:shader_path]
                                        encoding:NSUTF8StringEncoding error:&err];
    }
    if (!src) {
        err = nil;
        src = [NSString stringWithUTF8String:bloom_embedded_vision_shader()];
    }
    if (!src) {
        fprintf(stderr, "Cannot load vision shader source\n");
        return nullptr;
    }

    BloomVision *v = new BloomVision();
    v->dev = MTLCreateSystemDefaultDevice();
    if (!v->dev) { delete v; return nullptr; }
    v->q = [v->dev newCommandQueue];

    MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
    opts.languageVersion = MTLLanguageVersion3_0;
    v->lib = [v->dev newLibraryWithSource:src options:opts error:&err];
    if (!v->lib || err) {
        fprintf(stderr, "Vision shader compile failed: %s\n", err ? err.localizedDescription.UTF8String : "?");
        delete v;
        return nullptr;
    }

    v->p_prep = make_vis_cpso(v->dev, v->lib, "k_vision_prep_64");
    v->p_jfa = make_vis_cpso(v->dev, v->lib, "k_vision_jfa64");
    v->p_score = make_vis_cpso(v->dev, v->lib, "k_vision_score_tiles");

    MTLRenderPipelineDescriptor *rpd = [[MTLRenderPipelineDescriptor alloc] init];
    rpd.vertexFunction = [v->lib newFunctionWithName:@"v_vision_atlas"];
    rpd.fragmentFunction = [v->lib newFunctionWithName:@"f_vision_atlas"];
    rpd.colorAttachments[0].pixelFormat = MTLPixelFormatRG8Unorm;
    rpd.colorAttachments[0].blendingEnabled = NO;
    rpd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    v->p_atlas = [v->dev newRenderPipelineStateWithDescriptor:rpd error:&err];
    if (!v->p_prep || !v->p_jfa || !v->p_score || !v->p_atlas || err) {
        fprintf(stderr, "Vision PSO init failed: %s\n", err ? err.localizedDescription.UTF8String : "?");
        delete v;
        return nullptr;
    }

    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionLess;
    dsd.depthWriteEnabled = YES;
    v->ds_3d = [v->dev newDepthStencilStateWithDescriptor:dsd];

    MTLTextureDescriptor *atd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm
                                                                                   width:1024 height:512 mipmapped:NO];
    atd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    atd.storageMode = MTLStorageModePrivate;
    atd.allowGPUOptimizedContents = YES;
    v->t_atlas = [v->dev newTextureWithDescriptor:atd];

    MTLTextureDescriptor *add = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                   width:1024 height:512 mipmapped:NO];
    add.usage = MTLTextureUsageRenderTarget;
    add.storageMode = MTLStorageModeMemoryless;
    add.allowGPUOptimizedContents = YES;
    v->t_atlas_d = [v->dev newTextureWithDescriptor:add];

    v->b_obs64a = [v->dev newBufferWithLength:(64u * 64u * sizeof(ObsCell64)) options:MTLResourceStorageModePrivate];
    v->b_obs64b = [v->dev newBufferWithLength:(64u * 64u * sizeof(ObsCell64)) options:MTLResourceStorageModePrivate];
    v->b_sum = [v->dev newBufferWithLength:16u options:MTLResourceStorageModeShared];
    v->b_hyps = [v->dev newBufferWithLength:(1024u * sizeof(VisionHypGpu)) options:MTLResourceStorageModeShared];

    v->max_tg = std::min({
        (uint32_t)v->p_prep.maxTotalThreadsPerThreadgroup,
        (uint32_t)v->p_jfa.maxTotalThreadsPerThreadgroup,
        (uint32_t)v->p_score.maxTotalThreadsPerThreadgroup
    });
    return v;
}

void bloom_vision_destroy(BloomVision *v) {
    delete v;
}

uint32_t bloom_vision_get_max_tg(const BloomVision *v) {
    return v ? v->max_tg : 0u;
}

bool bloom_vision_register_template_mesh(BloomVision *v, uint16_t template_id, const char *name,
                                         uint32_t meshlet_cnt, const BloomMeshletHeader32 *hdrs,
                                         const BloomVtx12 *verts, const uint8_t *local_idx,
                                         uint32_t idx_bytes) {
    if (!v || meshlet_cnt == 0u || !hdrs || !verts || !local_idx || idx_bytes == 0u) return false;

    uint32_t base_v = (uint32_t)v->cpu_verts.size();
    uint32_t base_i = (uint32_t)v->cpu_lidx.size();
    uint32_t first_m = (uint32_t)v->cpu_hdrs.size();

    uint32_t max_v_end = 0u;
    for (uint32_t m = 0u; m < meshlet_cnt; ++m) {
        uint32_t end = hdrs[m].vtx_offset + hdrs[m].vtx_cnt;
        if (end > max_v_end) max_v_end = end;
        BloomMeshletHeader32 h = hdrs[m];
        h.vtx_offset += base_v;
        h.idx_offset += base_i;
        v->cpu_hdrs.push_back(h);
    }
    bool has_dof = false;
    for (uint32_t i = 0u; i < max_v_end; ++i) {
        if (verts[i].joint_id != 0u) {
            has_dof = true;
            break;
        }
    }
    v->cpu_verts.insert(v->cpu_verts.end(), verts, verts + max_v_end);
    v->cpu_lidx.insert(v->cpu_lidx.end(), local_idx, local_idx + idx_bytes);

    RegisteredTpl rt{};
    rt.template_id = template_id;
    snprintf(rt.name, sizeof(rt.name), "%s", name ? name : "template");
    rt.first_m = first_m;
    rt.mcnt = meshlet_cnt;
    rt.vtx_draw_cnt = (meshlet_cnt == 1u) ? ((uint32_t)hdrs[0].tri_cnt * 3u)
                                          : (meshlet_cnt * BLOOM_CLUSTER_TRI * 3u);
    rt.has_dof = has_dof;
    v->tpls.push_back(rt);
    v->geom_dirty = true;
    return true;
}

bool bloom_vision_register_from_scene(BloomVision *v, const BloomScene *scene,
                                      uint16_t template_id, const char *name) {
    if (!v || !scene) return false;
    uint32_t mcnt = 0u, idx_bytes = 0u;
    const BloomMeshletHeader32 *hdrs = nullptr;
    const BloomVtx12 *verts = nullptr;
    const uint8_t *lidx = nullptr;
    if (!bloom_scene_get_template_mesh(scene, template_id, &mcnt, &hdrs, &verts, &lidx, &idx_bytes)) {
        return false;
    }
    return bloom_vision_register_template_mesh(v, template_id, name, mcnt, hdrs, verts, lidx, idx_bytes);
}

static bool sync_vision_geom(BloomVision *v) {
    if (!v->geom_dirty && v->b_hdrs && v->b_verts && v->b_lidx) return true;
    if (v->cpu_hdrs.empty() || v->cpu_verts.empty() || v->cpu_lidx.empty()) return false;
    v->b_hdrs = [v->dev newBufferWithBytes:v->cpu_hdrs.data()
                                    length:(v->cpu_hdrs.size() * sizeof(BloomMeshletHeader32))
                                   options:MTLResourceStorageModeShared];
    v->b_verts = [v->dev newBufferWithBytes:v->cpu_verts.data()
                                     length:(v->cpu_verts.size() * sizeof(BloomVtx12))
                                    options:MTLResourceStorageModeShared];
    v->b_lidx = [v->dev newBufferWithBytes:v->cpu_lidx.data()
                                    length:v->cpu_lidx.size()
                                   options:MTLResourceStorageModeShared];
    v->geom_dirty = false;
    return true;
}

static void encode_render_and_score(BloomVision *v, id<MTLCommandBuffer> cb,
                                    const std::vector<VisionParamsGpu> &draws,
                                    uint32_t total_score_hyps) {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = v->t_atlas;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    rp.depthAttachment.texture = v->t_atlas_d;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
    [re setDepthStencilState:v->ds_3d];
    [re setCullMode:MTLCullModeNone];
    [re setRenderPipelineState:v->p_atlas];
    [re setVertexBuffer:v->b_hyps offset:0 atIndex:0];
    [re setVertexBuffer:v->b_hdrs offset:0 atIndex:1];
    [re setVertexBuffer:v->b_verts offset:0 atIndex:2];
    [re setVertexBuffer:v->b_lidx offset:0 atIndex:3];
    for (size_t i = 0u; i < draws.size(); ++i) {
        const auto &d = draws[i];
        [re setVertexBytes:&d length:sizeof(d) atIndex:4];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0
               vertexCount:v->tpls[d.pad0].vtx_draw_cnt instanceCount:d.n_hyps];
    }
    [re endEncoding];

    VisionParamsGpu vps = draws[0];
    vps.hyps_base = draws[0].hyps_base;
    vps.tile_base = 0u;
    vps.n_hyps = total_score_hyps;
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:v->p_score];
    [ce setTexture:v->t_atlas atIndex:0];
    [ce setBuffer:v->b_obs64b offset:0 atIndex:0];
    [ce setBuffer:v->b_hyps offset:0 atIndex:1];
    [ce setBytes:&vps length:sizeof(vps) atIndex:2];
    NSUInteger simd_w = std::max<NSUInteger>(1u, v->p_score.threadExecutionWidth);
    NSUInteger tg_w = std::min<NSUInteger>(256u, v->p_score.maxTotalThreadsPerThreadgroup);
    tg_w = std::max<NSUInteger>(simd_w, (tg_w / simd_w) * simd_w);
    [ce dispatchThreadgroups:MTLSizeMake(total_score_hyps, 1u, 1u)
       threadsPerThreadgroup:MTLSizeMake(tg_w, 1u, 1u)];
    [ce endEncoding];
}

bool bloom_vision_localize_rgba8(BloomVision *v, const uint8_t *rgba8_pixels,
                                 uint32_t img_w, uint32_t img_h,
                                 const BloomCamera *cam,
                                 const float hint_pos[3], float search_radius,
                                 BloomVisionMatch *out_match) {
    if (!v || !rgba8_pixels || img_w < 64u || img_h < 64u || !cam || !out_match) return false;
    if (!sync_vision_geom(v) || v->tpls.empty()) return false;
    memset(out_match, 0, sizeof(*out_match));

    if (!v->t_cam || v->cam_w != img_w || v->cam_h != img_h) {
        v->cam_w = img_w;
        v->cam_h = img_h;
        MTLTextureDescriptor *ctd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                       width:img_w height:img_h mipmapped:NO];
        ctd.usage = MTLTextureUsageShaderRead;
        ctd.storageMode = MTLStorageModeShared;
        v->t_cam = [v->dev newTextureWithDescriptor:ctd];
    }
    [v->t_cam replaceRegion:MTLRegionMake2D(0, 0, img_w, img_h) mipmapLevel:0
                  withBytes:rgba8_pixels bytesPerRow:(size_t)img_w * 4u];

    uint32_t n_tpl = (uint32_t)v->tpls.size();
    uint32_t req_hyps = std::max<uint32_t>(256u, n_tpl * 128u);
    if (v->b_hyps.length < req_hyps * sizeof(VisionHypGpu)) {
        v->b_hyps = [v->dev newBufferWithLength:(req_hyps * sizeof(VisionHypGpu)) options:MTLResourceStorageModeShared];
    }
    auto *hy = static_cast<VisionHypGpu *>(v->b_hyps.contents);

    float cr[3], cu[3], cf[3];
    cam_basis_vis(cam, cr, cu, cf);
    float aspect = (float)img_w / (float)img_h;
    float focal = cam->focal > 0.05f ? cam->focal : 1.0f;

    auto make_params = [&](uint32_t t_idx, uint32_t hyps_base, uint32_t tile_base, uint32_t n_hyps) {
        VisionParamsGpu p{};
        p.img_w = img_w; p.img_h = img_h;
        p.first_m = v->tpls[t_idx].first_m;
        p.hyps_base = hyps_base;
        p.focal = focal; p.aspect = aspect;
        p.tile_base = tile_base; p.n_hyps = n_hyps;
        memcpy(p.cam_pos, cam->pos, 12u);
        memcpy(p.cam_right, cr, 12u);
        memcpy(p.cam_up, cu, 12u);
        memcpy(p.cam_fwd, cf, 12u);
        p.pad0 = t_idx;
        return p;
    };

    uint64_t t_gpu_ns = 0u;
    memset(v->b_sum.contents, 0, 16u);
    {
        VisionParamsGpu vp = make_params(0u, 0u, 0u, 0u);
        id<MTLCommandBuffer> cb0 = [v->q commandBuffer];
        id<MTLComputeCommandEncoder> ce0 = [cb0 computeCommandEncoder];
        [ce0 setComputePipelineState:v->p_prep];
        [ce0 setTexture:v->t_cam atIndex:0];
        [ce0 setBuffer:v->b_obs64a offset:0 atIndex:0];
        [ce0 setBuffer:v->b_sum offset:0 atIndex:1];
        [ce0 setBytes:&vp length:sizeof(vp) atIndex:2];
        [ce0 dispatchThreadgroups:MTLSizeMake(8u, 8u, 1u) threadsPerThreadgroup:MTLSizeMake(8u, 8u, 1u)];

        id<MTLBuffer> jfa_src = v->b_obs64a, jfa_dst = v->b_obs64b;
        const int jsteps[5] = {16, 8, 4, 2, 1};
        [ce0 setComputePipelineState:v->p_jfa];
        for (int js : jsteps) {
            [ce0 setBuffer:jfa_src offset:0 atIndex:0];
            [ce0 setBuffer:jfa_dst offset:0 atIndex:1];
            [ce0 setBytes:&js length:4 atIndex:2];
            [ce0 dispatchThreadgroups:MTLSizeMake(8u, 8u, 1u) threadsPerThreadgroup:MTLSizeMake(8u, 8u, 1u)];
            std::swap(jfa_src, jfa_dst);
        }
        [ce0 endEncoding];
        [cb0 commit];
        [cb0 waitUntilCompleted];
        t_gpu_ns += uint64_t((cb0.GPUEndTime - cb0.GPUStartTime) * 1e9);
        /* Note: 5 swaps leave final JFA result in b_obs64b */
    }

    const uint32_t *sum_u = static_cast<const uint32_t *>(v->b_sum.contents);
    float cx_ndc = (sum_u[2] > 4u) ? ((float(sum_u[0]) / float(sum_u[2]) + 0.5f) / 64.0f * 2.0f - 1.0f) : 0.0f;
    float cy_ndc = (sum_u[2] > 4u) ? (1.0f - (float(sum_u[1]) / float(sum_u[2]) + 0.5f) / 64.0f * 2.0f) : 0.0f;
    float rx = cx_ndc * aspect / focal;
    float ry = cy_ndc / focal;

    float z_min = 3.70f, z_max = 4.80f;
    if (hint_pos) {
        float hd = (hint_pos[0] - cam->pos[0]) * cf[0] +
                     (hint_pos[1] - cam->pos[1]) * cf[1] +
                     (hint_pos[2] - cam->pos[2]) * cf[2];
        float sr = search_radius > 0.1f ? search_radius : 0.65f;
        z_min = std::max(0.6f, hd - sr);
        z_max = hd + sr;
    }

    std::mt19937 rng(42u);
    const uint32_t kPerTplC = 64u;
    uint32_t kCTot = n_tpl * kPerTplC;
    for (uint32_t h = 0u; h < kCTot; ++h) {
        uint32_t t_idx = h / kPerTplC;
        uint32_t lh = h % kPerTplC;
        float syaw = -0.80f + (float(lh % 8u) / 7.0f) * 1.60f;
        float spitch = -0.60f + (float((lh / 8u) % 4u) / 3.0f) * 1.20f;
        float sroll = ((lh / 32u) == 0u) ? -0.18f : 0.18f;
        std::uniform_real_distribution<float> dz(z_min, z_max), dj(-0.07f, 0.07f),
            dja(-0.14f, 0.14f), dr(-0.16f, 0.16f), dd(0.0f, 1.5f);
        float z_c = dz(rng);
        float px = cam->pos[0] + z_c * (cr[0] * rx + cu[0] * ry + cf[0]) + (cr[0] * dj(rng) + cu[0] * dj(rng));
        float py = cam->pos[1] + z_c * (cr[1] * rx + cu[1] * ry + cf[1]) + (cr[1] * dj(rng) + cu[1] * dj(rng));
        float pz = cam->pos[2] + z_c * (cr[2] * rx + cu[2] * ry + cf[2]) + (cr[2] * dj(rng) + cu[2] * dj(rng));
        hy[h] = {px, py, pz, syaw + dja(rng), spitch + dja(rng), sroll + dr(rng), 1.0f,
                 v->tpls[t_idx].has_dof ? dd(rng) : 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, t_idx, 0u, 0u, 0u};
    }

    {
        id<MTLCommandBuffer> cb1 = [v->q commandBuffer];
        for (uint32_t t0 = 0u; t0 < n_tpl; t0 += 2u) {
            std::vector<VisionParamsGpu> draws;
            uint32_t batch_hyps = 0u;
            for (uint32_t t = t0; t < std::min(n_tpl, t0 + 2u); ++t) {
                draws.push_back(make_params(t, t * kPerTplC, batch_hyps, kPerTplC));
                batch_hyps += kPerTplC;
            }
            encode_render_and_score(v, cb1, draws, batch_hyps);
        }
        [cb1 commit];
        [cb1 waitUntilCompleted];
        t_gpu_ns += uint64_t((cb1.GPUEndTime - cb1.GPUStartTime) * 1e9);
    }

    std::vector<VisionHypGpu> bank(hy, hy + kCTot);
    uint32_t kPerT = std::max(8u, std::min(42u, 128u / n_tpl));
    uint32_t kRefTot = n_tpl * kPerT;

    for (int r = 0; r < 2; ++r) {
        std::vector<VisionHypGpu> elites;
        for (uint32_t t = 0u; t < n_tpl; ++t) {
            std::vector<VisionHypGpu> sub;
            for (const auto &c : bank) if (c.tpl_id == t) sub.push_back(c);
            std::sort(sub.begin(), sub.end(), [](const VisionHypGpu &a, const VisionHypGpu &b) {
                return a.score_out < b.score_out;
            });
            for (size_t k = 0u; k < std::min<size_t>(4u, sub.size()); ++k) elites.push_back(sub[k]);
        }
        float spread = (r == 0) ? 0.32f : 0.11f;
        for (uint32_t i = 0u; i < kRefTot; ++i) {
            uint32_t t = i / kPerT;
            VisionHypGpu ch = elites[t * 4u + (i % 4u)];
            if ((i % kPerT) >= 4u) {
                std::normal_distribution<float> nd(0.0f, 1.0f);
                ch.px += nd(rng) * 0.15f * spread;
                ch.py += nd(rng) * 0.15f * spread;
                ch.pz += nd(rng) * 0.30f * spread;
                ch.yaw += nd(rng) * 0.50f * spread;
                ch.pitch += nd(rng) * 0.50f * spread;
                ch.roll += nd(rng) * 0.40f * spread;
                if (v->tpls[t].has_dof) {
                    ch.dof0 = std::max(0.0f, std::min(1.5f, ch.dof0 + nd(rng) * 0.50f * spread));
                } else {
                    ch.dof0 = 0.0f;
                }
            }
            hy[i] = ch;
        }
        id<MTLCommandBuffer> cbr = [v->q commandBuffer];
        std::vector<VisionParamsGpu> draws;
        draws.reserve(n_tpl);
        for (uint32_t t = 0u; t < n_tpl; ++t) {
            draws.push_back(make_params(t, t * kPerT, t * kPerT, kPerT));
        }
        encode_render_and_score(v, cbr, draws, kRefTot);
        [cbr commit];
        [cbr waitUntilCompleted];
        t_gpu_ns += uint64_t((cbr.GPUEndTime - cbr.GPUStartTime) * 1e9);
        bank.assign(hy, hy + kRefTot);
    }

    std::sort(bank.begin(), bank.end(), [](const VisionHypGpu &a, const VisionHypGpu &b) {
        return a.score_out < b.score_out;
    });

    uint32_t win_t = bank[0].tpl_id;
    std::vector<VisionHypGpu> win_elites;
    for (const auto &c : bank) {
        if (c.tpl_id == win_t && win_elites.size() < 4u) win_elites.push_back(c);
    }

    auto run_polish = [&](uint32_t cnt) {
        id<MTLCommandBuffer> cbf = [v->q commandBuffer];
        std::vector<VisionParamsGpu> draws = {make_params(win_t, 0u, 0u, cnt)};
        encode_render_and_score(v, cbf, draws, cnt);
        [cbf commit];
        [cbf waitUntilCompleted];
        t_gpu_ns += uint64_t((cbf.GPUEndTime - cbf.GPUStartTime) * 1e9);
        std::sort(hy, hy + cnt, [](const VisionHypGpu &a, const VisionHypGpu &b) {
            return a.score_out < b.score_out;
        });
    };

    const uint32_t kPol1 = 128u;
    size_t n_el = win_elites.size();
    for (size_t k = 0u; k < n_el; ++k) hy[k] = win_elites[k];
    for (uint32_t i = (uint32_t)n_el; i < kPol1; ++i) {
        VisionHypGpu ch = win_elites[i % n_el];
        std::normal_distribution<float> nd(0.0f, 1.0f);
        ch.px += nd(rng) * 0.035f;
        ch.py += nd(rng) * 0.035f;
        ch.pz += nd(rng) * 0.08f;
        ch.yaw += nd(rng) * 0.12f;
        ch.pitch += nd(rng) * 0.12f;
        ch.roll += nd(rng) * 0.12f;
        if (v->tpls[win_t].has_dof) {
            ch.dof0 = std::max(0.0f, std::min(1.5f, ch.dof0 + nd(rng) * 0.15f));
        } else {
            ch.dof0 = 0.0f;
        }
        hy[i] = ch;
    }
    run_polish(kPol1);

    const uint32_t kPol2 = 64u;
    VisionHypGpu best1 = hy[0];
    hy[0] = best1;
    for (uint32_t i = 1u; i < kPol2; ++i) {
        VisionHypGpu ch = best1;
        std::normal_distribution<float> nd(0.0f, 1.0f);
        ch.px += nd(rng) * 0.015f;
        ch.py += nd(rng) * 0.015f;
        ch.pz += nd(rng) * 0.035f;
        ch.yaw += nd(rng) * 0.04f;
        ch.pitch += nd(rng) * 0.04f;
        ch.roll += nd(rng) * 0.04f;
        if (v->tpls[win_t].has_dof) {
            ch.dof0 = float(i - 1u) * (1.5f / float(kPol2 - 2u));
        } else {
            ch.dof0 = 0.0f;
        }
        hy[i] = ch;
    }
    run_polish(kPol2);

    const VisionHypGpu &best = hy[0];
    const RegisteredTpl &rt = v->tpls[best.tpl_id];
    out_match->found = (best.iou_out > 0.15f);
    out_match->template_id = rt.template_id;
    snprintf(out_match->template_name, sizeof(out_match->template_name), "%s", rt.name);
    out_match->pos[0] = best.px;
    out_match->pos[1] = best.py;
    out_match->pos[2] = best.pz;
    out_match->euler_ypr[0] = best.yaw;
    out_match->euler_ypr[1] = best.pitch;
    out_match->euler_ypr[2] = best.roll;
    euler_ypr_to_quat(best.yaw, best.pitch, best.roll, out_match->quat);
    out_match->scale = best.scale;
    out_match->dof0 = best.dof0;
    out_match->iou = best.iou_out;
    out_match->edge_err = best.edge_err;
    out_match->lum_err = best.lum_err;
    out_match->score = best.score_out;
    out_match->gpu_ms = double(t_gpu_ns) * 1e-6;

    out_match->projection.field_mask = BLOOM_PROJ_POS | BLOOM_PROJ_QUAT | BLOOM_PROJ_DOF;
    memcpy(out_match->projection.pos, out_match->pos, 12u);
    memcpy(out_match->projection.quat, out_match->quat, 16u);
    out_match->projection.dof[0] = out_match->dof0;
    return out_match->found;
}
