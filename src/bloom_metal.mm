#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "bloom_atlas.h"
#include "bloom_embedded_shaders.h"
#include "bloom_gpu.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr uint32_t kRingSlots = 3u;
static constexpr uint32_t kAtlasDim = 1024u;
static constexpr uint32_t kSpriteAtlasDim = 512u;
static constexpr uint32_t kPbrTexDim = 512u;
static constexpr uint32_t kPbrTexSlices = 32u;
static constexpr uint32_t kMaxGlyphsPerFrame = 32768u;

struct GroundParamsGpu {
    uint32_t vp_w, vp_h, pick_x, pick_y;
    uint32_t max_slots, half_shift;
    float    pos_scale;
    uint32_t pad0;
};
BLOOM_STATIC_ASSERT(sizeof(GroundParamsGpu) == 32, "GroundParamsGpu must be 32 bytes");

struct GroundRequest {
    uint32_t pick_x, pick_y, max_slots, half_shift, id_w, id_h;
};

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
    id<MTLBuffer> stg_sprite[kRingSlots]{};
    id<MTLBuffer> ind_args[kRingSlots]{};

    /* Private static meshlet & identity index buffers + culling survivors */
    id<MTLBuffer> priv_vtx = nil;
    id<MTLBuffer> priv_lidx = nil;
    id<MTLBuffer> priv_id_idx16 = nil;
    id<MTLBuffer> priv_surv_ids = nil;

    /* Private+Opt R8Unorm font atlas + RGBA8 sprite atlas + Memoryless Depth32Float */
    id<MTLTexture> atlas_tex = nil;
    id<MTLTexture> sprite_atlas_tex = nil;
    id<MTLTexture> depth_tex = nil;
    id<MTLTexture> hl_color_tex = nil;
    uint32_t cur_w = 0u, cur_h = 0u;

    /* Lazy on-demand M2 PBR & Sprite pipelines */
    std::string pbr_shader_path;
    std::string sprite_shader_path;
    id<MTLLibrary> pbr_lib = nil;
    id<MTLLibrary> sprite_lib = nil;
    id<MTLRenderPipelineState> p_mesh_pbr = nil;
    id<MTLRenderPipelineState> p_hud_sprite = nil;
    id<MTLTexture> pbr_tex_array = nil;
    uint32_t pbr_tex_used = 0u;

    /* Lazy on-demand Adaptive Half-Res Grounding & Picking resources */
    std::string ground_shader_path;
    id<MTLLibrary> ground_lib = nil;
    id<MTLRenderPipelineState> p_id_mesh = nil;
    id<MTLRenderPipelineState> p_id_proxy = nil;
    id<MTLRenderPipelineState> p_id_hud = nil;
    id<MTLComputePipelineState> p_ground_red = nil;
    id<MTLTexture> ground_id_tex = nil;
    id<MTLTexture> ground_depth_tex = nil;
    id<MTLBuffer> b_ground_stats = nil;
    id<MTLBuffer> b_ground_pick = nil;
    uint32_t ground_id_w = 0u, ground_id_h = 0u;

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
    NSString *src = nil;
    if (shader_path && shader_path[0]) {
        src = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:shader_path]
                                        encoding:NSUTF8StringEncoding error:&err];
    }
    if (!src) {
        err = nil;
        src = [NSString stringWithUTF8String:bloom_embedded_core_shader()];
    }
    if (!src) {
        fprintf(stderr, "Cannot load core Metal shader source\n");
        return nullptr;
    }

    BloomGpu *g = new BloomGpu();
    if (shader_path && shader_path[0]) {
        std::string sp(shader_path);
        size_t pos = sp.rfind("bloom_core.metal");
        std::string dir = (pos != std::string::npos) ? sp.substr(0, pos) : "shaders/";
        g->ground_shader_path = dir + "bloom_grounding.metal";
        g->pbr_shader_path = dir + "bloom_pbr.metal";
        g->sprite_shader_path = dir + "bloom_sprite.metal";
    }
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
        g->stg_sprite[s] = [g->dev newBufferWithLength:(kSpriteAtlasDim * kSpriteAtlasDim * 4u)
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

    /* Private+Opt R8Unorm CoreText Atlas + RGBA8 Sprite Atlas */
    g->atlas = bloom_atlas_create(kAtlasDim, kAtlasDim);
    MTLTextureDescriptor *ad = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                  width:kAtlasDim height:kAtlasDim mipmapped:NO];
    ad.usage = MTLTextureUsageShaderRead;
    ad.storageMode = MTLStorageModePrivate;
    ad.allowGPUOptimizedContents = YES;
    g->atlas_tex = [g->dev newTextureWithDescriptor:ad];

    MTLTextureDescriptor *sd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:kSpriteAtlasDim height:kSpriteAtlasDim mipmapped:NO];
    sd.usage = MTLTextureUsageShaderRead;
    sd.storageMode = MTLStorageModePrivate;
    sd.allowGPUOptimizedContents = YES;
    g->sprite_atlas_tex = [g->dev newTextureWithDescriptor:sd];

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

static id<MTLLibrary> compile_aux_lib(id<MTLDevice> dev, const std::string &path, const char *embedded_src) {
    NSError *err = nil;
    NSString *src = nil;
    if (!path.empty()) {
        src = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]
                                        encoding:NSUTF8StringEncoding error:&err];
    }
    if (!src && embedded_src) {
        err = nil;
        src = [NSString stringWithUTF8String:embedded_src];
    }
    if (!src) {
        fprintf(stderr, "Cannot load shader %s\n", path.empty() ? "<embedded>" : path.c_str());
        return nil;
    }
    MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
    opts.languageVersion = MTLLanguageVersion3_0;
    id<MTLLibrary> lib = [dev newLibraryWithSource:src options:opts error:&err];
    if (!lib || err) {
        fprintf(stderr, "Shader %s compile failed: %s\n",
                path.empty() ? "<embedded>" : path.c_str(),
                err ? err.localizedDescription.UTF8String : "?");
        return nil;
    }
    return lib;
}

static bool ensure_grounding_ready(BloomGpu *g, uint32_t id_w, uint32_t id_h, uint32_t max_slots) {
    if (!g->ground_lib) {
        g->ground_lib = compile_aux_lib(g->dev, g->ground_shader_path, bloom_embedded_grounding_shader());
        if (!g->ground_lib) return false;
        auto make_id_r = [&](const char *vf, const char *ff) -> id<MTLRenderPipelineState> {
            MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
            d.vertexFunction = [g->ground_lib newFunctionWithName:[NSString stringWithUTF8String:vf]];
            d.fragmentFunction = [g->ground_lib newFunctionWithName:[NSString stringWithUTF8String:ff]];
            d.colorAttachments[0].pixelFormat = MTLPixelFormatR32Uint;
            d.colorAttachments[0].blendingEnabled = NO;
            d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            NSError *pe = nil;
            id<MTLRenderPipelineState> p = [g->dev newRenderPipelineStateWithDescriptor:d error:&pe];
            if (!p || pe) fprintf(stderr, "ID PSO %s/%s failed: %s\n", vf, ff, pe ? pe.localizedDescription.UTF8String : "?");
            return p;
        };
        g->p_id_mesh = make_id_r("v_meshlet_id", "f_meshlet_id");
        g->p_id_proxy = make_id_r("v_proxy_id", "f_proxy_id");
        g->p_id_hud = make_id_r("v_hud_id", "f_hud_id");
        g->p_ground_red = make_cpso(g->dev, g->ground_lib, "k_compute_ground_reduce");
        if (!g->p_id_mesh || !g->p_id_proxy || !g->p_id_hud || !g->p_ground_red) return false;
        g->stats.max_tg_ground = (uint32_t)g->p_ground_red.maxTotalThreadsPerThreadgroup;
        g->stats.grounding_active = true;
    }
    if (g->ground_id_w != id_w || g->ground_id_h != id_h || !g->ground_id_tex) {
        g->ground_id_w = id_w;
        g->ground_id_h = id_h;
        MTLTextureDescriptor *itd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint
                                                                                       width:id_w height:id_h mipmapped:NO];
        itd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        itd.storageMode = MTLStorageModePrivate;
        itd.allowGPUOptimizedContents = YES;
        g->ground_id_tex = [g->dev newTextureWithDescriptor:itd];

        MTLTextureDescriptor *gdd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                       width:id_w height:id_h mipmapped:NO];
        gdd.usage = MTLTextureUsageRenderTarget;
        gdd.storageMode = MTLStorageModeMemoryless;
        gdd.allowGPUOptimizedContents = YES;
        g->ground_depth_tex = [g->dev newTextureWithDescriptor:gdd];
    }
    size_t req_bytes = std::max<size_t>(1u, max_slots) * sizeof(BloomGroundedGpu32);
    if (!g->b_ground_stats || g->b_ground_stats.length < req_bytes) {
        g->b_ground_stats = [g->dev newBufferWithLength:req_bytes options:MTLResourceStorageModeShared];
    }
    if (!g->b_ground_pick) {
        g->b_ground_pick = [g->dev newBufferWithLength:sizeof(BloomPickGpuResult) options:MTLResourceStorageModeShared];
    }
    return true;
}

static bool ensure_pbr_tex_array(BloomGpu *g) {
    if (g->pbr_tex_array) return true;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:kPbrTexDim height:kPbrTexDim mipmapped:YES];
    td.textureType = MTLTextureType2DArray;
    td.arrayLength = kPbrTexSlices;
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    td.allowGPUOptimizedContents = YES;
    g->pbr_tex_array = [g->dev newTextureWithDescriptor:td];
    return g->pbr_tex_array != nil;
}

static bool ensure_pbr_ready(BloomGpu *g) {
    if (!ensure_pbr_tex_array(g)) return false;
    if (!g->pbr_lib) {
        g->pbr_lib = compile_aux_lib(g->dev, g->pbr_shader_path, bloom_embedded_pbr_shader());
        if (!g->pbr_lib) return false;
        g->p_mesh_pbr = make_rpso(g->dev, g->pbr_lib, "v_meshlet_pbr", "f_meshlet_pbr", false, false);
        if (!g->p_mesh_pbr) return false;
        g->stats.pbr_active = true;
    }
    return true;
}

static bool ensure_sprite_ready(BloomGpu *g) {
    if (!g->sprite_lib) {
        g->sprite_lib = compile_aux_lib(g->dev, g->sprite_shader_path, bloom_embedded_sprite_shader());
        if (!g->sprite_lib) return false;
        g->p_hud_sprite = make_rpso(g->dev, g->sprite_lib, "v_hud_sprite", "f_hud_sprite", true, false);
        if (!g->p_hud_sprite) return false;
        g->stats.sprite_active = true;
    }
    return true;
}

static bool render_internal(BloomGpu *g, BloomScene *scene, uint32_t vp_w, uint32_t vp_h,
                            bool force, bool headless, const GroundRequest *greq) {
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
    const BloomSpriteInst *sp_ptr = nullptr;
    const BloomMaterial32 *mat_ptr = nullptr;
    const BloomLight32 *lt_ptr = nullptr;

    uint32_t st_cnt = bloom_scene_get_gpu_states(scene, &st_ptr);
    uint32_t mh_cnt = bloom_scene_get_gpu_meshlets(scene, &mh_ptr);
    uint32_t pr_cnt = bloom_scene_get_analytics(scene, &pr_ptr);
    uint32_t opq_cnt = bloom_scene_get_opaque_quads(scene, &opq_ptr);
    uint32_t alp_cnt = bloom_scene_get_alpha_quads(scene, &alp_ptr);
    uint32_t sh_cnt = bloom_scene_get_shadows(scene, &sh_ptr);
    uint32_t cl_cnt = bloom_scene_get_clips(scene, &cl_ptr);
    uint32_t tr_cnt = bloom_scene_get_text_runs(scene, &tr_ptr);
    uint32_t sp_cnt = bloom_scene_get_sprites(scene, &sp_ptr);
    uint32_t mat_cnt = bloom_scene_get_materials(scene, &mat_ptr);
    uint32_t lt_cnt = bloom_scene_get_lights(scene, &lt_ptr);
    bool use_pbr = bloom_scene_has_pbr_objects(scene);
    if (use_pbr && !ensure_pbr_ready(g)) use_pbr = false;
    if (sp_cnt > 0u && !ensure_sprite_ready(g)) sp_cnt = 0u;

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
    size_t off_sp = off_gl + sz_gl;
    size_t sz_sp = align256(std::max<size_t>(1u, sp_cnt) * sizeof(BloomSpriteInst));
    size_t off_mat = off_sp + sz_sp;
    size_t sz_mat = align256(std::max<size_t>(1u, mat_cnt) * sizeof(BloomMaterial32));
    size_t off_lt = off_mat + sz_mat;
    size_t sz_lt = align256(std::max<size_t>(1u, lt_cnt) * sizeof(BloomLight32));
    size_t total_wc = off_lt + sz_lt;

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
    if (sp_cnt) memcpy(ring + off_sp, sp_ptr, sp_cnt * sizeof(BloomSpriteInst));
    if (mat_cnt) memcpy(ring + off_mat, mat_ptr, mat_cnt * sizeof(BloomMaterial32));
    if (lt_cnt) memcpy(ring + off_lt, lt_ptr, lt_cnt * sizeof(BloomLight32));

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

    /* Check incremental CoreText atlas & RGBA8 Sprite Atlas uploads */
    const uint8_t *atl_px = nullptr, *sp_px = nullptr;
    uint32_t atl_w = 0u, atl_h = 0u, ay0 = 0u, ay1 = 0u;
    bool upload_atlas = bloom_atlas_take_dirty(g->atlas, &atl_px, &atl_w, &atl_h, &ay0, &ay1);
    if (upload_atlas && ay1 > ay0) {
        size_t copy_bytes = (size_t)(ay1 - ay0) * atl_w;
        memcpy(g->stg_atlas[s].contents, atl_px + (size_t)ay0 * atl_w, copy_bytes);
    } else {
        upload_atlas = false;
    }

    uint32_t spr_w = 0u, spr_h = 0u, sy0 = 0u, sy1 = 0u;
    bool upload_sprite = bloom_atlas_take_sprite_dirty(g->atlas, &sp_px, &spr_w, &spr_h, &sy0, &sy1);
    if (upload_sprite && sy1 > sy0) {
        size_t copy_bytes = (size_t)(sy1 - sy0) * spr_w * 4u;
        memcpy(g->stg_sprite[s].contents, sp_px + (size_t)sy0 * spr_w * 4u, copy_bytes);
    } else {
        upload_sprite = false;
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

    if (upload_geom || upload_atlas || upload_sprite) {
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
        if (upload_sprite) {
            [bl copyFromBuffer:g->stg_sprite[s] sourceOffset:0 sourceBytesPerRow:(spr_w * 4u) sourceBytesPerImage:0
                    sourceSize:MTLSizeMake(spr_w, sy1 - sy0, 1)
                     toTexture:g->sprite_atlas_tex destinationSlice:0 destinationLevel:0
             destinationOrigin:MTLOriginMake(0, sy0, 0)];
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
        [enc setRenderPipelineState:(use_pbr ? g->p_mesh_pbr : g->p_mesh_cut)];
        [enc setVertexBuffer:g->priv_surv_ids offset:0 atIndex:0];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_mh atIndex:1];
        [enc setVertexBuffer:g->priv_vtx offset:0 atIndex:2];
        [enc setVertexBuffer:g->priv_lidx offset:0 atIndex:3];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_st atIndex:4];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:5];
        if (use_pbr) {
            uint32_t lmeta[4] = {lt_cnt, 0u, 0u, 0u};
            [enc setFragmentBuffer:g->wc_ring[s] offset:off_mat atIndex:0];
            [enc setFragmentBuffer:g->wc_ring[s] offset:off_lt atIndex:1];
            [enc setFragmentBytes:lmeta length:sizeof(lmeta) atIndex:2];
            [enc setFragmentBytes:&u length:sizeof(u) atIndex:3];
            [enc setFragmentTexture:g->pbr_tex_array atIndex:0];
        }
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
    if (sp_cnt > 0u && g->p_hud_sprite) {
        [enc setDepthStencilState:g->ds_ui_blend];
        [enc setRenderPipelineState:g->p_hud_sprite];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_sp atIndex:0];
        [enc setVertexBuffer:g->wc_ring[s] offset:off_cl atIndex:1];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
        [enc setFragmentBuffer:g->wc_ring[s] offset:off_sp atIndex:0];
        [enc setFragmentTexture:g->sprite_atlas_tex atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:sp_cnt];
    }
    [enc endEncoding];

    if (greq) {
        MTLRenderPassDescriptor *irpd = [MTLRenderPassDescriptor renderPassDescriptor];
        irpd.colorAttachments[0].texture = g->ground_id_tex;
        irpd.colorAttachments[0].loadAction = MTLLoadActionClear;
        irpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        irpd.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
        irpd.depthAttachment.texture = g->ground_depth_tex;
        irpd.depthAttachment.loadAction = MTLLoadActionClear;
        irpd.depthAttachment.storeAction = MTLStoreActionDontCare;
        irpd.depthAttachment.clearDepth = 1.0;

        id<MTLRenderCommandEncoder> genc = [cmd renderCommandEncoderWithDescriptor:irpd];
        if (mh_cnt > 0u && g->priv_vtx && g->priv_lidx) {
            [genc setDepthStencilState:g->ds_3d_write];
            [genc setCullMode:MTLCullModeBack];
            [genc setRenderPipelineState:g->p_id_mesh];
            [genc setVertexBuffer:g->priv_surv_ids offset:0 atIndex:0];
            [genc setVertexBuffer:g->wc_ring[s] offset:off_mh atIndex:1];
            [genc setVertexBuffer:g->priv_vtx offset:0 atIndex:2];
            [genc setVertexBuffer:g->priv_lidx offset:0 atIndex:3];
            [genc setVertexBuffer:g->wc_ring[s] offset:off_st atIndex:4];
            [genc setVertexBytes:&u length:sizeof(u) atIndex:5];
            [genc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:MTLIndexTypeUInt16
                            indexBuffer:g->priv_id_idx16 indexBufferOffset:0
                         indirectBuffer:g->ind_args[s] indirectBufferOffset:0];
        }
        if (pr_cnt > 0u) {
            BloomFrameUniforms hfu = u;
            hfu.vp_w = (float)greq->id_w;
            hfu.vp_h = (float)greq->id_h;
            [genc setDepthStencilState:g->ds_3d_write];
            [genc setCullMode:MTLCullModeNone];
            [genc setRenderPipelineState:g->p_id_proxy];
            [genc setVertexBuffer:g->wc_ring[s] offset:off_pr atIndex:0];
            [genc setVertexBytes:&u length:sizeof(u) atIndex:1];
            [genc setFragmentBuffer:g->wc_ring[s] offset:off_pr atIndex:0];
            [genc setFragmentBytes:&hfu length:sizeof(hfu) atIndex:1];
            [genc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:pr_cnt];
        }
        [genc setCullMode:MTLCullModeNone];
        [genc setDepthStencilState:g->ds_ui_opq];
        GroundParamsGpu gp{vp_w, vp_h, greq->pick_x, greq->pick_y, greq->max_slots, greq->half_shift,
                           (float)(1u << greq->half_shift), 0u};
        if (opq_cnt > 0u) {
            [genc setRenderPipelineState:g->p_id_hud];
            [genc setVertexBuffer:g->wc_ring[s] offset:off_oq atIndex:0];
            [genc setVertexBuffer:g->wc_ring[s] offset:off_cl atIndex:1];
            [genc setVertexBytes:&u length:sizeof(u) atIndex:2];
            [genc setFragmentBuffer:g->wc_ring[s] offset:off_oq atIndex:0];
            [genc setFragmentBytes:&gp length:sizeof(gp) atIndex:1];
            [genc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:opq_cnt];
        }
        if (alp_cnt > 0u) {
            [genc setRenderPipelineState:g->p_id_hud];
            [genc setVertexBuffer:g->wc_ring[s] offset:off_aq atIndex:0];
            [genc setVertexBuffer:g->wc_ring[s] offset:off_cl atIndex:1];
            [genc setVertexBytes:&u length:sizeof(u) atIndex:2];
            [genc setFragmentBuffer:g->wc_ring[s] offset:off_aq atIndex:0];
            [genc setFragmentBytes:&gp length:sizeof(gp) atIndex:1];
            [genc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:alp_cnt];
        }
        [genc endEncoding];

        id<MTLComputeCommandEncoder> rce = [cmd computeCommandEncoder];
        [rce setComputePipelineState:g->p_ground_red];
        [rce setTexture:g->ground_id_tex atIndex:0];
        [rce setBuffer:g->b_ground_stats offset:0 atIndex:0];
        [rce setBuffer:g->b_ground_pick offset:0 atIndex:1];
        [rce setBytes:&gp length:sizeof(gp) atIndex:2];
        [rce dispatchThreadgroups:MTLSizeMake((greq->id_w + 15u) / 16u, (greq->id_h + 15u) / 16u, 1u)
            threadsPerThreadgroup:MTLSizeMake(16u, 16u, 1u)];
        [rce endEncoding];
    }

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
        double ms = (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
        g->stats.last_gpu_ms = ms;
        if (greq) g->stats.last_ground_ms = ms;
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
    return render_internal(gpu, scene, vp_w, vp_h, force, false, nullptr);
}

bool bloom_gpu_render_headless(BloomGpu *gpu, BloomScene *scene,
                               uint32_t vp_w, uint32_t vp_h, bool force) {
    return render_internal(gpu, scene, vp_w, vp_h, force, true, nullptr);
}

bool bloom_gpu_render_grounded(BloomGpu *gpu, BloomScene *scene,
                               uint32_t vp_w, uint32_t vp_h,
                               uint32_t pick_x, uint32_t pick_y,
                               BloomGroundedGpu32 *out_stats, uint32_t max_slots,
                               BloomPickGpuResult *out_pick) {
    if (!gpu || !scene || vp_w == 0u || vp_h == 0u || max_slots < 3u) return false;
    uint32_t half_shift = (vp_w > 2048u) ? 1u : 0u;
    uint32_t id_w = vp_w >> half_shift;
    uint32_t id_h = vp_h >> half_shift;
    if (!ensure_grounding_ready(gpu, id_w, id_h, max_slots)) return false;

    auto *gs = static_cast<BloomGroundedGpu32 *>(gpu->b_ground_stats.contents);
    for (uint32_t i = 0u; i < max_slots; ++i) {
        gs[i].visible_px = 0u;
        gs[i].min_x = UINT32_MAX;
        gs[i].min_y = UINT32_MAX;
        gs[i].max_x = 0u;
        gs[i].max_y = 0u;
        gs[i].best_anchor = 0u;
        gs[i].part_sample = 0u;
        gs[i].min_depth_u32 = UINT32_MAX;
    }
    auto *pk = static_cast<BloomPickGpuResult *>(gpu->b_ground_pick.contents);
    pk->ground_id = 0u; pk->depth_bits = 0u; pk->px = pick_x; pk->py = pick_y;

    GroundRequest req{pick_x, pick_y, max_slots, half_shift, id_w, id_h};
    if (!render_internal(gpu, scene, vp_w, vp_h, true, true, &req)) return false;

    if (out_stats) memcpy(out_stats, gs, (size_t)max_slots * sizeof(BloomGroundedGpu32));
    if (out_pick) *out_pick = *pk;
    return true;
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

bool bloom_gpu_readback_headless_rgba8(BloomGpu *g, uint8_t *out_rgba8, uint32_t max_bytes,
                                       uint32_t *out_w, uint32_t *out_h) {
    if (!g || !g->hl_color_tex || !out_rgba8) return false;
    uint32_t w = (uint32_t)g->hl_color_tex.width;
    uint32_t h = (uint32_t)g->hl_color_tex.height;
    size_t row_bytes = (size_t)w * 4u;
    size_t tot_bytes = row_bytes * (size_t)h;
    if ((size_t)max_bytes < tot_bytes) return false;

    id<MTLBuffer> rb = [g->dev newBufferWithLength:tot_bytes options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g->q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:g->hl_color_tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(w, h, 1) toBuffer:rb destinationOffset:0
      destinationBytesPerRow:row_bytes destinationBytesPerImage:tot_bytes];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    const uint8_t *bgra = static_cast<const uint8_t *>(rb.contents);
    size_t npx = (size_t)w * (size_t)h;
    for (size_t i = 0u; i < npx; ++i) {
        out_rgba8[i * 4u + 0u] = bgra[i * 4u + 2u];
        out_rgba8[i * 4u + 1u] = bgra[i * 4u + 1u];
        out_rgba8[i * 4u + 2u] = bgra[i * 4u + 0u];
        out_rgba8[i * 4u + 3u] = bgra[i * 4u + 3u];
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return true;
}

BloomAtlas *bloom_gpu_get_atlas(BloomGpu *gpu) {
    return gpu ? gpu->atlas : nullptr;
}

uint16_t bloom_gpu_upload_texture_rgba8(BloomGpu *g, const uint8_t *rgba8,
                                        uint32_t w, uint32_t h, bool generate_mips) {
    if (!g || !rgba8 || w == 0u || h == 0u || !ensure_pbr_tex_array(g)) return 0u;
    if (g->pbr_tex_used >= kPbrTexSlices) return 0u;

    uint32_t slice = g->pbr_tex_used++;
    size_t row_bytes = (size_t)kPbrTexDim * 4u;
    size_t tot_bytes = row_bytes * (size_t)kPbrTexDim;
    id<MTLBuffer> stg = [g->dev newBufferWithLength:tot_bytes
                                            options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    uint8_t *dst = static_cast<uint8_t *>(stg.contents);
    for (uint32_t y = 0u; y < kPbrTexDim; ++y) {
        uint32_t sy = (y * h) / kPbrTexDim;
        for (uint32_t x = 0u; x < kPbrTexDim; ++x) {
            uint32_t sx = (x * w) / kPbrTexDim;
            memcpy(dst + ((size_t)y * kPbrTexDim + x) * 4u,
                   rgba8 + ((size_t)sy * w + sx) * 4u, 4u);
        }
    }
    id<MTLCommandBuffer> cb = [g->q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromBuffer:stg sourceOffset:0 sourceBytesPerRow:row_bytes sourceBytesPerImage:tot_bytes
            sourceSize:MTLSizeMake(kPbrTexDim, kPbrTexDim, 1)
             toTexture:g->pbr_tex_array destinationSlice:slice destinationLevel:0
     destinationOrigin:MTLOriginMake(0, 0, 0)];
    if (generate_mips) [bl generateMipmapsForTexture:g->pbr_tex_array];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    g->stats.uploaded_textures = g->pbr_tex_used;
    return (uint16_t)(slice + 1u);
}

uint16_t bloom_gpu_upload_ktx2(BloomGpu *gpu, const uint8_t *ktx2_bytes, size_t len) {
    if (!gpu || !ktx2_bytes || len < 104u) return 0u;
    static const uint8_t kMagic[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(ktx2_bytes, kMagic, 12u) != 0) return 0u;
    uint32_t vk_fmt = 0u, px_w = 0u, px_h = 0u, lvl_cnt = 0u, sc = 0u;
    memcpy(&vk_fmt, ktx2_bytes + 12u, 4u);
    memcpy(&px_w, ktx2_bytes + 20u, 4u);
    memcpy(&px_h, ktx2_bytes + 24u, 4u);
    memcpy(&lvl_cnt, ktx2_bytes + 40u, 4u);
    memcpy(&sc, ktx2_bytes + 44u, 4u);
    if ((vk_fmt != 37u && vk_fmt != 43u) || sc != 0u || px_w == 0u || px_h == 0u || lvl_cnt == 0u) return 0u;
    uint64_t lvl0_off = 0u, lvl0_len = 0u;
    memcpy(&lvl0_off, ktx2_bytes + 80u, 8u);
    memcpy(&lvl0_len, ktx2_bytes + 88u, 8u);
    size_t req = (size_t)px_w * (size_t)px_h * 4u;
    if (lvl0_off + lvl0_len > len || lvl0_len < req) return 0u;
    return bloom_gpu_upload_texture_rgba8(gpu, ktx2_bytes + lvl0_off, px_w, px_h, true);
}

BloomGpuStats bloom_gpu_get_stats(const BloomGpu *gpu) {
    BloomGpuStats empty{};
    return gpu ? gpu->stats : empty;
}
