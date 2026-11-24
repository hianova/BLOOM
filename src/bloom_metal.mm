#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "bloom_atlas.h"
#include "bloom_gpu.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static constexpr uint32_t kRingSlots = 3u;
static constexpr uint32_t kAtlasDim = 1024u;
static constexpr uint32_t kMaxGlyphsPerFrame = 32768u;

static inline size_t align256(size_t n) {
    return (n + 255u) & ~size_t(255u);
}

struct BloomGpu {
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> q = nil;
    CAMetalLayer *layer = nil;
    BloomAtlas *atlas = nullptr;

    id<MTLLibrary> lib = nil;
    id<MTLComputePipelineState> p_cull = nil;
    id<MTLComputePipelineState> p_chk = nil;
    id<MTLRenderPipelineState> p_mesh_cut = nil;
    id<MTLRenderPipelineState> p_proxy = nil;
    id<MTLRenderPipelineState> p_hud_opq = nil;
    id<MTLRenderPipelineState> p_hud_sh = nil;
    id<MTLRenderPipelineState> p_hud_alp = nil;
    id<MTLRenderPipelineState> p_hud_gl = nil;

    id<MTLDepthStencilState> ds_3d_write = nil;
    id<MTLDepthStencilState> ds_3d_nowrite = nil;
    id<MTLDepthStencilState> ds_ui_opq = nil;
    id<MTLDepthStencilState> ds_ui_blend = nil;

    dispatch_semaphore_t sem = nil;
    uint32_t slot = 0u;

    /* Triple-buffered WriteCombined hot state rings + Shared indirect counters */
    id<MTLBuffer> wc_ring[kRingSlots]{};
    id<MTLBuffer> stg_geom[kRingSlots]{};
    id<MTLBuffer> stg_atlas[kRingSlots]{};
    id<MTLBuffer> ind_args[kRingSlots]{};

    /* Private static meshlet & identity index buffers + culling survivors */
    id<MTLBuffer> priv_vtx = nil;
    id<MTLBuffer> priv_lidx = nil;
    id<MTLBuffer> priv_id_idx16 = nil;
    id<MTLBuffer> priv_surv_ids = nil;

    /* Private+Opt R8Unorm font atlas + Memoryless Depth32Float + optional headless RT */
    id<MTLTexture> atlas_tex = nil;
    id<MTLTexture> depth_tex = nil;
    id<MTLTexture> hl_color_tex = nil;
    uint32_t cur_w = 0u, cur_h = 0u;

    BloomGpuStats stats{};
    std::atomic<double> async_last_gpu_ms{0.0};
    std::vector<BloomGlyphInst> scratch_glyphs;
};

static id<MTLComputePipelineState> make_cpso(id<MTLDevice> dev, id<MTLLibrary> lib, const char *fn) {
    NSError *err = nil;
    id<MTLFunction> f = [lib newFunctionWithName:[NSString stringWithUTF8String:fn]];
    if (!f) { fprintf(stderr, "Missing Metal compute function: %s\n", fn); return nil; }
    id<MTLComputePipelineState> p = [dev newComputePipelineStateWithFunction:f error:&err];
    if (!p || err) {
        fprintf(stderr, "Compute PSO %s failed: %s\n", fn, err ? err.localizedDescription.UTF8String : "?");
        return nil;
    }
    return p;
}

static id<MTLRenderPipelineState> make_rpso(id<MTLDevice> dev, id<MTLLibrary> lib,
                                            const char *vf, const char *ff, bool blend, bool premul) {
    NSError *err = nil;
    id<MTLFunction> v = [lib newFunctionWithName:[NSString stringWithUTF8String:vf]];
    id<MTLFunction> f = [lib newFunctionWithName:[NSString stringWithUTF8String:ff]];
    if (!v || !f) { fprintf(stderr, "Missing Metal shader %s/%s\n", vf, ff); return nil; }
    MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction = v;
    d.fragmentFunction = f;
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    if (blend) {
        d.colorAttachments[0].blendingEnabled = YES;
        d.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        d.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        d.colorAttachments[0].sourceRGBBlendFactor = premul ? MTLBlendFactorOne : MTLBlendFactorSourceAlpha;
        d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    id<MTLRenderPipelineState> p = [dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p || err) {
        fprintf(stderr, "Render PSO %s/%s failed: %s\n", vf, ff, err ? err.localizedDescription.UTF8String : "?");
        return nil;
    }
    return p;
}

static void ensure_targets(BloomGpu *g, uint32_t w, uint32_t h, bool need_headless) {
    if (g->cur_w == w && g->cur_h == h && g->depth_tex && (!need_headless || g->hl_color_tex)) return;
    g->cur_w = w;
    g->cur_h = h;
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                  width:w height:h mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModeMemoryless;
    dd.allowGPUOptimizedContents = YES;
    g->depth_tex = [g->dev newTextureWithDescriptor:dd];

    if (need_headless) {
        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:w height:h mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        cd.storageMode = MTLStorageModePrivate;
        cd.allowGPUOptimizedContents = YES;
        g->hl_color_tex = [g->dev newTextureWithDescriptor:cd];
    }
}

static void build_frame_uniforms(const BloomScene *sc, uint32_t vp_w, uint32_t vp_h,
                                 uint32_t meshlet_cnt, uint32_t prim_cnt, BloomFrameUniforms *out) {
    BloomCamera cam = bloom_scene_get_camera(sc);
    float cy = cosf(cam.yaw), sy = sinf(cam.yaw);
    float cp = cosf(cam.pitch), sp = sinf(cam.pitch);
    float fx = sy * cp, fy = -sp, fz = cy * cp;
    float rx = cy, ry = 0.0f, rz = -sy;
    /* up = cross(fwd, right) */
    float ux = fy * rz - fz * ry;
    float uy = fz * rx - fx * rz;
    float uz = fx * ry - fy * rx;

    out->vp_w = (float)vp_w;
    out->vp_h = (float)vp_h;
    out->focal = cam.focal > 0.1f ? cam.focal : 1.8f;
    out->frustum_half_tan = 1.0f / out->focal;
    out->cam_pos[0] = cam.pos[0];
    out->cam_pos[1] = cam.pos[1];
    out->cam_pos[2] = cam.pos[2];
    out->near_z = cam.near_z > 0.05f ? cam.near_z : 0.5f;
    out->cam_right[0] = rx; out->cam_right[1] = ry; out->cam_right[2] = rz;
    out->far_z = cam.far_z > 5.0f ? cam.far_z : 80.0f;
    out->cam_up[0] = ux; out->cam_up[1] = uy; out->cam_up[2] = uz;
    out->total_meshlets = meshlet_cnt;
    out->cam_fwd[0] = fx; out->cam_fwd[1] = fy; out->cam_fwd[2] = fz;
    out->prim_count = prim_cnt;
}

BloomGpu *bloom_gpu_create(void *ca_metal_layer, const char *shader_path) {
    NSError *err = nil;
    const char *spath = shader_path ? shader_path : "shaders/bloom_core.metal";
    NSString *src = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:spath]
                                              encoding:NSUTF8StringEncoding error:&err];
    if (!src || err) {
        fprintf(stderr, "Cannot read shader %s: %s\n", spath, err ? err.localizedDescription.UTF8String : "?");
        return nullptr;
    }

    BloomGpu *g = new BloomGpu();
    g->dev = MTLCreateSystemDefaultDevice();
    if (!g->dev) { delete g; return nullptr; }
    g->q = [g->dev newCommandQueue];
    g->layer = (__bridge CAMetalLayer *)ca_metal_layer;
    if (g->layer) {
        g->layer.device = g->dev;
        g->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g->layer.framebufferOnly = YES;
    }

    MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
    opts.languageVersion = MTLLanguageVersion3_0;
    g->lib = [g->dev newLibraryWithSource:src options:opts error:&err];
    if (!g->lib || err) {
        fprintf(stderr, "Metal library compile error: %s\n", err ? err.localizedDescription.UTF8String : "?");
        delete g;
        return nullptr;
    }

    g->p_cull = make_cpso(g->dev, g->lib, "k_cull_meshlets");
    g->p_chk = make_cpso(g->dev, g->lib, "k_tex_checksum");
    g->p_mesh_cut = make_rpso(g->dev, g->lib, "v_meshlet_cutaway", "f_obj_lit", false, false);
    g->p_proxy = make_rpso(g->dev, g->lib, "v_tight_proxy_analytic", "f_tight_proxy_analytic", true, true);
    g->p_hud_opq = make_rpso(g->dev, g->lib, "v_hud_opaque", "f_hud_opaque", false, false);
    g->p_hud_sh = make_rpso(g->dev, g->lib, "v_hud_shadow", "f_hud_shadow", true, false);
    g->p_hud_alp = make_rpso(g->dev, g->lib, "v_hud_alpha", "f_hud_alpha", true, false);
    g->p_hud_gl = make_rpso(g->dev, g->lib, "v_hud_glyph", "f_hud_glyph", true, false);
    if (!g->p_cull || !g->p_chk || !g->p_mesh_cut || !g->p_proxy ||
        !g->p_hud_opq || !g->p_hud_sh || !g->p_hud_alp || !g->p_hud_gl) {
        delete g;
        return nullptr;
    }

    MTLDepthStencilDescriptor *d3w = [[MTLDepthStencilDescriptor alloc] init];
    d3w.depthCompareFunction = MTLCompareFunctionLess; d3w.depthWriteEnabled = YES;
    g->ds_3d_write = [g->dev newDepthStencilStateWithDescriptor:d3w];

    MTLDepthStencilDescriptor *d3n = [[MTLDepthStencilDescriptor alloc] init];
    d3n.depthCompareFunction = MTLCompareFunctionLess; d3n.depthWriteEnabled = NO;
    g->ds_3d_nowrite = [g->dev newDepthStencilStateWithDescriptor:d3n];

    MTLDepthStencilDescriptor *duo = [[MTLDepthStencilDescriptor alloc] init];
    duo.depthCompareFunction = MTLCompareFunctionLessEqual; duo.depthWriteEnabled = YES;
    g->ds_ui_opq = [g->dev newDepthStencilStateWithDescriptor:duo];

    MTLDepthStencilDescriptor *dub = [[MTLDepthStencilDescriptor alloc] init];
    dub.depthCompareFunction = MTLCompareFunctionLessEqual; dub.depthWriteEnabled = NO;
    g->ds_ui_blend = [g->dev newDepthStencilStateWithDescriptor:dub];

    g->sem = dispatch_semaphore_create(kRingSlots);
    for (uint32_t s = 0u; s < kRingSlots; ++s) {
        g->wc_ring[s] = [g->dev newBufferWithLength:(2u * 1024u * 1024u)
                                            options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        g->stg_atlas[s] = [g->dev newBufferWithLength:(kAtlasDim * kAtlasDim)
                                              options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        g->ind_args[s] = [g->dev newBufferWithLength:32u options:MTLResourceStorageModeShared];
    }

    /* Static identity index buffer {0 .. BLOOM_CLUSTER_TRI*3 - 1} uploaded once to Private */
    std::vector<uint16_t> id_idx(BLOOM_CLUSTER_TRI * 3u);
    for (uint16_t i = 0u; i < BLOOM_CLUSTER_TRI * 3u; ++i) id_idx[i] = i;
    id<MTLBuffer> stg_idx = [g->dev newBufferWithBytes:id_idx.data() length:id_idx.size() * 2u
                                               options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    g->priv_id_idx16 = [g->dev newBufferWithLength:id_idx.size() * 2u options:MTLResourceStorageModePrivate];
    id<MTLCommandBuffer> cb = [g->q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromBuffer:stg_idx sourceOffset:0 toBuffer:g->priv_id_idx16 destinationOffset:0 size:id_idx.size() * 2u];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    /* Private+Opt R8Unorm CoreText Atlas */
    g->atlas = bloom_atlas_create(kAtlasDim, kAtlasDim);
    MTLTextureDescriptor *ad = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                  width:kAtlasDim height:kAtlasDim mipmapped:NO];
    ad.usage = MTLTextureUsageShaderRead;
    ad.storageMode = MTLStorageModePrivate;
    ad.allowGPUOptimizedContents = YES;
    g->atlas_tex = [g->dev newTextureWithDescriptor:ad];

    g->scratch_glyphs.resize(kMaxGlyphsPerFrame);
    g->stats.max_tg_cull = (uint32_t)g->p_cull.maxTotalThreadsPerThreadgroup;
    g->stats.max_tg_checksum = (uint32_t)g->p_chk.maxTotalThreadsPerThreadgroup;
    g->stats.memoryless_depth_active = true;
    g->stats.private_opt_atlas_active = true;
    return g;
}

void bloom_gpu_destroy(BloomGpu *gpu) {
    if (!gpu) return;
    for (uint32_t i = 0u; i < kRingSlots; ++i) {
        dispatch_semaphore_wait(gpu->sem, DISPATCH_TIME_FOREVER);
    }
    for (uint32_t i = 0u; i < kRingSlots; ++i) dispatch_semaphore_signal(gpu->sem);
    bloom_atlas_destroy(gpu->atlas);
    delete gpu;
}

static bool render_internal(BloomGpu *g, BloomScene *scene, uint32_t vp_w, uint32_t vp_h,
                            bool force, bool headless) {
    if (!g || !scene || vp_w == 0u || vp_h == 0u) return false;
    if (!bloom_scene_is_dirty(scene) && !force) {
        g->stats.idle_skipped_frames++;
        return true;
    }

    dispatch_semaphore_wait(g->sem, DISPATCH_TIME_FOREVER);
    uint32_t s = g->slot;
    g->slot = (g->slot + 1u) % kRingSlots;

    ensure_targets(g, vp_w, vp_h, headless);

    auto t_cpu0 = std::chrono::steady_clock::now();
    bloom_scene_sync_transforms(scene);
    bloom_scene_layout_ui(scene, (float)vp_w, (float)vp_h);

    const BloomObjectState48 *st_ptr = nullptr;
    const BloomMeshletHeader32 *mh_ptr = nullptr;
    const BloomAnalyticPrim32 *pr_ptr = nullptr;
    const BloomQuadHot *opq_ptr = nullptr, *alp_ptr = nullptr;
    const BloomShadowHot *sh_ptr = nullptr;
    const BloomClipRect *cl_ptr = nullptr;
    const BloomTextRun *tr_ptr = nullptr;

    uint32_t st_cnt = bloom_scene_get_gpu_states(scene, &st_ptr);
    uint32_t mh_cnt = bloom_scene_get_gpu_meshlets(scene, &mh_ptr);
    uint32_t pr_cnt = bloom_scene_get_analytics(scene, &pr_ptr);
    uint32_t opq_cnt = bloom_scene_get_opaque_quads(scene, &opq_ptr);
    uint32_t alp_cnt = bloom_scene_get_alpha_quads(scene, &alp_ptr);
    uint32_t sh_cnt = bloom_scene_get_shadows(scene, &sh_ptr);
    uint32_t cl_cnt = bloom_scene_get_clips(scene, &cl_ptr);
    uint32_t tr_cnt = bloom_scene_get_text_runs(scene, &tr_ptr);

    uint32_t gl_cnt = bloom_atlas_pack_runs(g->atlas, tr_ptr, tr_cnt,
                                            g->scratch_glyphs.data(), kMaxGlyphsPerFrame);

    size_t off_st = 0u;
    size_t sz_st = align256(std::max<size_t>(1u, st_cnt) * sizeof(BloomObjectState48));
    size_t off_mh = off_st + sz_st;
    size_t sz_mh = align256(std::max<size_t>(1u, mh_cnt) * sizeof(BloomMeshletHeader32));
    size_t off_pr = off_mh + sz_mh;
    size_t sz_pr = align256(std::max<size_t>(1u, pr_cnt) * sizeof(BloomAnalyticPrim32));
    size_t off_cl = off_pr + sz_pr;
    size_t sz_cl = align256(std::max<size_t>(1u, cl_cnt) * sizeof(BloomClipRect));
    size_t off_oq = off_cl + sz_cl;
    size_t sz_oq = align256(std::max<size_t>(1u, opq_cnt) * sizeof(BloomQuadHot));
    size_t off_aq = off_oq + sz_oq;
    size_t sz_aq = align256(std::max<size_t>(1u, alp_cnt) * sizeof(BloomQuadHot));
    size_t off_sh = off_aq + sz_aq;
    size_t sz_sh = align256(std::max<size_t>(1u, sh_cnt) * sizeof(BloomShadowHot));
    size_t off_gl = off_sh + sz_sh;
    size_t sz_gl = align256(std::max<size_t>(1u, gl_cnt) * sizeof(BloomGlyphInst));
    size_t total_wc = off_gl + sz_gl;

    if (total_wc > g->wc_ring[s].length) {
        g->wc_ring[s] = [g->dev newBufferWithLength:(total_wc * 2u)
                                            options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    }
    uint8_t *ring = static_cast<uint8_t *>(g->wc_ring[s].contents);
    if (st_cnt) memcpy(ring + off_st, st_ptr, st_cnt * sizeof(BloomObjectState48));
    if (mh_cnt) memcpy(ring + off_mh, mh_ptr, mh_cnt * sizeof(BloomMeshletHeader32));
    if (pr_cnt) memcpy(ring + off_pr, pr_ptr, pr_cnt * sizeof(BloomAnalyticPrim32));
    if (cl_cnt) memcpy(ring + off_cl, cl_ptr, cl_cnt * sizeof(BloomClipRect));
    if (opq_cnt) memcpy(ring + off_oq, opq_ptr, opq_cnt * sizeof(BloomQuadHot));
    if (alp_cnt) memcpy(ring + off_aq, alp_ptr, alp_cnt * sizeof(BloomQuadHot));
    if (sh_cnt) memcpy(ring + off_sh, sh_ptr, sh_cnt * sizeof(BloomShadowHot));
    if (gl_cnt) memcpy(ring + off_gl, g->scratch_glyphs.data(), gl_cnt * sizeof(BloomGlyphInst));

    /* Check static template geometry upload */
    bool upload_geom = false;
    uint32_t tpl_vtx_cnt = 0u, tpl_idx_bytes = 0u;
    size_t geom_v_bytes = 0u, geom_i_bytes = 0u;
    if (bloom_scene_geom_dirty(scene)) {
        tpl_vtx_cnt = bloom_scene_get_template_vtx_count(scene);
        tpl_idx_bytes = bloom_scene_get_template_idx_bytes(scene);
        if (tpl_vtx_cnt > 0u && tpl_idx_bytes > 0u) {
            geom_v_bytes = align256(tpl_vtx_cnt * sizeof(BloomVtx12));
            geom_i_bytes = align256(tpl_idx_bytes);
            if (!g->priv_vtx || g->priv_vtx.length < geom_v_bytes) {
                g->priv_vtx = [g->dev newBufferWithLength:geom_v_bytes options:MTLResourceStorageModePrivate];
            }
            if (!g->priv_lidx || g->priv_lidx.length < geom_i_bytes) {
                g->priv_lidx = [g->dev newBufferWithLength:geom_i_bytes options:MTLResourceStorageModePrivate];
            }
            if (!g->stg_geom[s] || g->stg_geom[s].length < geom_v_bytes + geom_i_bytes) {
                g->stg_geom[s] = [g->dev newBufferWithLength:(geom_v_bytes + geom_i_bytes)
                                                     options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
            }
            auto *sp = static_cast<uint8_t *>(g->stg_geom[s].contents);
            bloom_scene_copy_template_geom(scene, reinterpret_cast<BloomVtx12 *>(sp), sp + geom_v_bytes);
            upload_geom = true;
        }
        bloom_scene_clear_geom_dirty(scene);
    }

    /* Check incremental CoreText atlas upload */
    const uint8_t *atl_px = nullptr;
    uint32_t atl_w = 0u, atl_h = 0u, ay0 = 0u, ay1 = 0u;
    bool upload_atlas = bloom_atlas_take_dirty(g->atlas, &atl_px, &atl_w, &atl_h, &ay0, &ay1);
    if (upload_atlas && ay1 > ay0) {
        size_t copy_bytes = (size_t)(ay1 - ay0) * atl_w;
        memcpy(g->stg_atlas[s].contents, atl_px + (size_t)ay0 * atl_w, copy_bytes);
    } else {
        upload_atlas = false;
    }

    if (mh_cnt > 0u && (!g->priv_surv_ids || g->priv_surv_ids.length < mh_cnt * sizeof(uint32_t))) {
        g->priv_surv_ids = [g->dev newBufferWithLength:std::max<size_t>(mh_cnt, 1024u) * sizeof(uint32_t)
                                              options:MTLResourceStorageModePrivate];
    }

    uint32_t *ia = static_cast<uint32_t *>(g->ind_args[s].contents);
    ia[0] = BLOOM_CLUSTER_TRI * 3u;
    ia[1] = 0u; ia[2] = 0u; ia[3] = 0u; ia[4] = 0u;

    BloomFrameUniforms u{};
    build_frame_uniforms(scene, vp_w, vp_h, mh_cnt, pr_cnt, &u);
    auto t_cpu1 = std::chrono::steady_clock::now();

    /* Single CommandBuffer for Blit + Compute Cull + Single TBDR RenderPass */
    id<MTLCommandBuffer> cmd = [g->q commandBuffer];

    if (upload_geom || upload_atlas) {
        id<MTLBlitCommandEncoder> bl = [cmd blitCommandEncoder];
        if (upload_geom) {
            [bl copyFromBuffer:g->stg_geom[s] sourceOffset:0 toBuffer:g->priv_vtx destinationOffset:0 size:geom_v_bytes];
            [bl copyFromBuffer:g->stg_geom[s] sourceOffset:geom_v_bytes toBuffer:g->priv_lidx destinationOffset:0 size:tpl_idx_bytes];
        }
        if (upload_atlas) {
            [bl copyFromBuffer:g->stg_atlas[s] sourceOffset:0 sourceBytesPerRow:atl_w sourceBytesPerImage:0
                    sourceSize:MTLSizeMake(atl_w, ay1 - ay0, 1)
                     toTexture:g->atlas_tex destinationSlice:0 destinationLevel:0
             destinationOrigin:MTLOriginMake(0, ay0, 0)];
        }
        [bl endEncoding];
    }

    if (mh_cnt > 0u) {
        id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
        [ce setComputePipelineState:g->p_cull];
        [ce setBuffer:g->wc_ring[s] offset:off_mh atIndex:0];
        [ce setBuffer:g->wc_ring[s] offset:off_st atIndex:1];
        [ce setBytes:&u length:sizeof(u) atIndex:2];
        [ce setBuffer:g->priv_surv_ids offset:0 atIndex:3];
        [ce setBuffer:g->ind_args[s] offset:0 atIndex:4];
        [ce dispatchThreadgroups:MTLSizeMake((mh_cnt + 63u) / 64u, 1u, 1u)
           threadsPerThreadgroup:MTLSizeMake(64u, 1u, 1u)];
        [ce endEncoding];
    }

    id<CAMetalDrawable> drawable = nil;
    id<MTLTexture> color_target = g->hl_color_tex;
    if (!headless) {
        drawable = [g->layer nextDrawable];
        if (!drawable) {
            dispatch_semaphore_signal(g->sem);
            return false;
        }
        color_target = drawable.texture;
    }

    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = color_target;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.043, 0.059, 0.090, 1.0);
    rpd.depthAttachment.texture = g->depth_tex;
    rpd.depthAttachment.loadAction = MTLLoadActionClear;
    rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
    rpd.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
    if (mh_cnt > 0u && g->priv_vtx && g->priv_lidx) {
        [enc setDepthStencilState:g->ds_3d_write];
        [enc setCullMode:MTLCullModeBack];
        [enc setRenderPipelineState:g->p_mesh_cut];
        [enc setVertexBuffer:g->priv_surv_ids offset:0 atIndex:0];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_mh atIndex:1];
        [enc setVertexBuffer:g->priv_vtx offset:0 atIndex:2];
        [enc setVertexBuffer:g->priv_lidx offset:0 atIndex:3];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_st atIndex:4];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:5];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:MTLIndexTypeUInt16
                       indexBuffer:g->priv_id_idx16 indexBufferOffset:0
                    indirectBuffer:g->ind_args[s] indirectBufferOffset:0];
    }
    if (pr_cnt > 0u) {
        [enc setDepthStencilState:g->ds_3d_nowrite];
        [enc setCullMode:MTLCullModeNone];
        [enc setRenderPipelineState:g->p_proxy];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_pr atIndex:0];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:1];
        [enc setFragmentBuffer:g->wc_ring[s] offset:off_pr atIndex:0];
        [enc setFragmentBytes:&u length:sizeof(u) atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:pr_cnt];
    }
    [enc setCullMode:MTLCullModeNone];
    if (opq_cnt > 0u) {
        [enc setDepthStencilState:g->ds_ui_opq];
        [enc setRenderPipelineState:g->p_hud_opq];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_oq atIndex:0];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_cl atIndex:1];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:opq_cnt];
    }
    if (sh_cnt > 0u) {
        [enc setDepthStencilState:g->ds_ui_blend];
        [enc setRenderPipelineState:g->p_hud_sh];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_sh atIndex:0];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_cl atIndex:1];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
        [enc setFragmentBuffer:g->wc_ring[s] offset:off_sh atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:sh_cnt];
    }
    if (alp_cnt > 0u) {
        [enc setDepthStencilState:g->ds_ui_blend];
        [enc setRenderPipelineState:g->p_hud_alp];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_aq atIndex:0];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_cl atIndex:1];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
        [enc setFragmentBuffer:g->wc_ring[s] offset:off_aq atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:alp_cnt];
    }
    if (gl_cnt > 0u) {
        [enc setDepthStencilState:g->ds_ui_blend];
        [enc setRenderPipelineState:g->p_hud_gl];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_gl atIndex:0];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_cl atIndex:1];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
        [enc setFragmentBuffer:g->wc_ring[s] offset:off_gl atIndex:0];
        [enc setFragmentTexture:g->atlas_tex atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:gl_cnt];
    }
    [enc endEncoding];

    if (drawable) [cmd presentDrawable:drawable];

    dispatch_semaphore_t sem = g->sem;
    std::atomic<double> *gpu_ms_ptr = &g->async_last_gpu_ms;
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        gpu_ms_ptr->store((cb.GPUEndTime - cb.GPUStartTime) * 1000.0, std::memory_order_relaxed);
        dispatch_semaphore_signal(sem);
    }];
    [cmd commit];
    if (headless) {
        [cmd waitUntilCompleted];
        g->stats.surviving_meshlets = ia[1];
        g->stats.last_gpu_ms = (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
    } else {
        g->stats.last_gpu_ms = g->async_last_gpu_ms.load(std::memory_order_relaxed);
    }

    bloom_scene_clear_dirty(scene);
    g->stats.submitted_cbs++;
    g->stats.last_cpu_pack_us = std::chrono::duration<double, std::micro>(t_cpu1 - t_cpu0).count();
    g->stats.last_wc_bytes = (uint32_t)total_wc;
    g->stats.atlas_glyph_count = bloom_atlas_glyph_count(g->atlas);
    g->stats.total_meshlets = mh_cnt;
    return true;
}

bool bloom_gpu_render_drawable(BloomGpu *gpu, BloomScene *scene,
                               uint32_t vp_w, uint32_t vp_h, bool force) {
    return render_internal(gpu, scene, vp_w, vp_h, force, false);
}

bool bloom_gpu_render_headless(BloomGpu *gpu, BloomScene *scene,
                               uint32_t vp_w, uint32_t vp_h, bool force) {
    return render_internal(gpu, scene, vp_w, vp_h, force, true);
}

uint32_t bloom_gpu_headless_checksum(BloomGpu *g) {
    if (!g || !g->hl_color_tex) return 0u;
    id<MTLBuffer> acc = [g->dev newBufferWithLength:4 options:MTLResourceStorageModeShared];
    *static_cast<uint32_t *>(acc.contents) = 0u;
    id<MTLCommandBuffer> cb = [g->q commandBuffer];
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:g->p_chk];
    [ce setTexture:g->hl_color_tex atIndex:0];
    [ce setBuffer:acc offset:0 atIndex:0];
    uint32_t gx = ((uint32_t)g->hl_color_tex.width + 7u) / 8u;
    uint32_t gy = ((uint32_t)g->hl_color_tex.height + 7u) / 8u;
    [ce dispatchThreadgroups:MTLSizeMake((gx + 15u) / 16u, (gy + 15u) / 16u, 1u)
       threadsPerThreadgroup:MTLSizeMake(16u, 16u, 1u)];
    [ce endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    return *static_cast<uint32_t *>(acc.contents);
}

bool bloom_gpu_save_headless_ppm(BloomGpu *g, const char *ppm_path) {
    if (!g || !g->hl_color_tex || !ppm_path) return false;
    uint32_t w = (uint32_t)g->hl_color_tex.width;
    uint32_t h = (uint32_t)g->hl_color_tex.height;
    size_t row_bytes = (size_t)w * 4u;
    id<MTLBuffer> rb = [g->dev newBufferWithLength:(row_bytes * h) options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g->q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:g->hl_color_tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(w, h, 1) toBuffer:rb destinationOffset:0
      destinationBytesPerRow:row_bytes destinationBytesPerImage:(row_bytes * h)];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    FILE *fp = fopen(ppm_path, "wb");
    if (!fp) return false;
    fprintf(fp, "P6\n%u %u\n255\n", w, h);
    const uint8_t *bgra = static_cast<const uint8_t *>(rb.contents);
    std::vector<uint8_t> rgb((size_t)w * 3u);
    for (uint32_t y = 0u; y < h; ++y) {
        const uint8_t *row = bgra + (size_t)y * row_bytes;
        for (uint32_t x = 0u; x < w; ++x) {
            rgb[x * 3u + 0u] = row[x * 4u + 2u];
            rgb[x * 3u + 1u] = row[x * 4u + 1u];
            rgb[x * 3u + 2u] = row[x * 4u + 0u];
        }
        fwrite(rgb.data(), 1u, rgb.size(), fp);
    }
    fclose(fp);
    return true;
}

BloomGpuStats bloom_gpu_get_stats(const BloomGpu *gpu) {
    BloomGpuStats empty{};
    return gpu ? gpu->stats : empty;
}
