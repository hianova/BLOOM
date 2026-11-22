#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bloom.h"
#include "flow_ir.h"
#include "searu.h"

struct BloomGpuCtx {
    BloomEngineArena       *arena;
    SDL_GPUDevice          *device;
    SDL_GPUComputePipeline *pipeline;
    SDL_GPUComputePipeline *pipe_rc2;
    SDL_GPUComputePipeline *pipe_rc1;
    SDL_GPUComputePipeline *pipe_rc0;
    SDL_GPUComputePipeline *pipe_fir1;
    SDL_GPUComputePipeline *pipe_fir1_bs;
    SDL_GPUComputePipeline *pipe_searu;
    SDL_GPUComputePipeline *pipe_faultsim;
    SDL_GPUComputePipeline *pipe_info_sim;
    SDL_GPUComputePipeline *pipe_node_dst;
    SDL_GPUBuffer          *arena_gpu_buf;
    SDL_GPUBuffer          *mesh_gpu_buf;
    SDL_GPUBuffer          *rc_gpu_buf;
    SDL_GPUTransferBuffer  *upload_tbuf;
    SDL_GPUTransferBuffer  *mesh_upload_tbuf;
    SDL_GPUTransferBuffer  *rc_download_tbuf;
    SDL_GPUTexture         *frame_tex;
    SDL_GPUTransferBuffer  *download_tbuf;
    SDL_AudioStream        *audio_stream;
    uint32_t                width;
    uint32_t                height;
    uint32_t                dispatch_count;
    uint32_t                idle_skip_count;
    uint32_t                rc_cache_hits;
    uint32_t                mesh_epoch;
    uint64_t                rc_scene_hash;
    bool                    rc_valid;
    bool                    hardware_gpu;
    bool                    unified_memory;
    char                    driver_name[32];
    char                    device_name[160];
};

static bool bloom_gpu_ensure_sdl_video(void) {
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0U) {
        return true;
    }
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint("SDL_MAC_BACKGROUND_APP", "1");
    if (SDL_Init(SDL_INIT_VIDEO)) {
        return true;
    }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (SDL_Init(SDL_INIT_VIDEO)) {
        return true;
    }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    return SDL_Init(SDL_INIT_VIDEO);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void bloom_gpu_module_onload(void) {
    (void)bloom_gpu_ensure_sdl_video();
}
#endif

static bool bloom_str_ends_with(const char *s, const char *suffix) {
    size_t slen = strlen(s);
    size_t xlen = strlen(suffix);
    if (slen < xlen) {
        return false;
    }
    return memcmp(s + (slen - xlen), suffix, xlen) == 0;
}

static uint8_t *bloom_try_read_file(const char *path, size_t *out_size) {
    if (!path || path[0] == '\0' || !out_size) {
        return NULL;
    }
    size_t sz = 0U;
    void *raw = SDL_LoadFile(path, &sz);
    if (!raw) {
        return NULL;
    }
    if (sz == 0U) {
        SDL_free(raw);
        return NULL;
    }
    *out_size = sz;
    return (uint8_t *)raw;
}

static uint8_t *bloom_load_compiled_shader(const char *shader_path,
                                           const char *ext,
                                           char *resolved_out,
                                           size_t resolved_cap,
                                           size_t *out_size) {
    if (shader_path && shader_path[0] != '\0') {
        if (bloom_str_ends_with(shader_path, ext)) {
            uint8_t *direct = bloom_try_read_file(shader_path, out_size);
            if (direct) {
                snprintf(resolved_out, resolved_cap, "%s", shader_path);
                return direct;
            }
        }

        char stem[512];
        snprintf(stem, sizeof(stem), "%s", shader_path);
        const char *known_exts[3] = { ".wgsl", ".metal", ".spv" };
        for (size_t i = 0U; i < 3U; ++i) {
            if (bloom_str_ends_with(stem, known_exts[i])) {
                stem[strlen(stem) - strlen(known_exts[i])] = '\0';
                break;
            }
        }

        char cand1[544];
        snprintf(cand1, sizeof(cand1), "%s%s", stem, ext);
        uint8_t *b1 = bloom_try_read_file(cand1, out_size);
        if (b1) {
            snprintf(resolved_out, resolved_cap, "%s", cand1);
            return b1;
        }

        const char *slash = strrchr(stem, '/');
        const char *base = slash ? (slash + 1) : stem;
        if (base[0] != '\0') {
            char cand2[544];
            snprintf(cand2, sizeof(cand2), "build/%s%s", base, ext);
            uint8_t *b2 = bloom_try_read_file(cand2, out_size);
            if (b2) {
                snprintf(resolved_out, resolved_cap, "%s", cand2);
                return b2;
            }
        }
    }

    char fallback[128];
    snprintf(fallback, sizeof(fallback), "build/bloom_core%s", ext);
    uint8_t *bf = bloom_try_read_file(fallback, out_size);
    if (bf) {
        snprintf(resolved_out, resolved_cap, "%s", fallback);
        return bf;
    }
    return NULL;
}

static bool bloom_gpu_alloc_frame_targets(BloomGpuCtx *ctx, uint32_t w, uint32_t h) {
    if (!ctx || !ctx->device || w == 0U || h == 0U) {
        return false;
    }
    if (w > 8192U) w = 8192U;
    if (h > 8192U) h = 8192U;

    SDL_GPUTextureCreateInfo tex_info;
    memset(&tex_info, 0, sizeof(tex_info));
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_info.width = w;
    tex_info.height = h;
    tex_info.layer_count_or_depth = 1U;
    tex_info.num_levels = 1U;
    tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture *tex = SDL_CreateGPUTexture(ctx->device, &tex_info);
    if (!tex) {
        return false;
    }

    SDL_GPUTransferBufferCreateInfo dn_info;
    memset(&dn_info, 0, sizeof(dn_info));
    dn_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    dn_info.size = w * h * 4U;

    SDL_GPUTransferBuffer *dn_buf = SDL_CreateGPUTransferBuffer(ctx->device, &dn_info);
    if (!dn_buf) {
        SDL_ReleaseGPUTexture(ctx->device, tex);
        return false;
    }

    if (ctx->frame_tex) {
        SDL_ReleaseGPUTexture(ctx->device, ctx->frame_tex);
    }
    if (ctx->download_tbuf) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->download_tbuf);
    }
    ctx->frame_tex = tex;
    ctx->download_tbuf = dn_buf;
    ctx->width = w;
    ctx->height = h;
    return true;
}

BloomGpuCtx *bloom_gpu_init(BloomEngineArena *arena, const char *shader_path) {
    if (!arena) {
        return NULL;
    }
    if (!bloom_gpu_ensure_sdl_video()) {
        fprintf(stderr, "[BLOOM SDL3-GPU] SDL video init failed: %s\n", SDL_GetError());
        return NULL;
    }

    BloomGpuCtx *ctx = (BloomGpuCtx *)calloc(1U, sizeof(BloomGpuCtx));
    if (!ctx) {
        return NULL;
    }
    ctx->arena = arena;

    uint32_t w = arena->cells[BLOOM_SLOT_HEADER].frame_hdr.width_px;
    uint32_t h = arena->cells[BLOOM_SLOT_HEADER].frame_hdr.height_px;
    if (w == 0U) w = 1280U;
    if (h == 0U) h = 720U;

    SDL_GPUDevice *dev = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV,
        false,
        NULL);
    if (!dev) {
        fprintf(stderr, "[BLOOM SDL3-GPU] SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        free(ctx);
        return NULL;
    }
    ctx->device = dev;

    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(dev);
    SDL_GPUShaderFormat chosen_fmt = SDL_GPU_SHADERFORMAT_INVALID;
    const char *ext = NULL;
    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0U) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_MSL;
        ext = ".metal";
    } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0U) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_SPIRV;
        ext = ".spv";
    } else {
        fprintf(stderr, "[BLOOM SDL3-GPU] Unsupported GPU shader formats mask: 0x%x\n",
                (unsigned)formats);
        bloom_gpu_destroy(ctx);
        return NULL;
    }

    char resolved_path[544];
    resolved_path[0] = '\0';
    size_t code_size = 0U;
    uint8_t *code = bloom_load_compiled_shader(
        shader_path, ext, resolved_path, sizeof(resolved_path), &code_size);
    if (!code || code_size == 0U) {
        fprintf(stderr, "[BLOOM SDL3-GPU] Failed to load compiled %s shader (req='%s')\n",
                ext, shader_path ? shader_path : "");
        SDL_free(code);
        bloom_gpu_destroy(ctx);
        return NULL;
    }

    SDL_GPUComputePipelineCreateInfo pipe_info;
    memset(&pipe_info, 0, sizeof(pipe_info));
    pipe_info.code_size = code_size;
    pipe_info.code = code;
    pipe_info.entrypoint = "bloom_render_frame";
    pipe_info.format = chosen_fmt;
    pipe_info.num_samplers = 0U;
    pipe_info.num_readonly_storage_textures = 0U;
    pipe_info.num_readonly_storage_buffers = 2U;
    pipe_info.num_readwrite_storage_textures = 1U;
    pipe_info.num_readwrite_storage_buffers = 1U;
    pipe_info.num_uniform_buffers = 0U;
    pipe_info.threadcount_x = 8U;
    pipe_info.threadcount_y = 8U;
    pipe_info.threadcount_z = 1U;

    ctx->pipeline = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_rc_cascade2";
    ctx->pipe_rc2 = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_rc_cascade1";
    ctx->pipe_rc1 = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_rc_cascade0";
    ctx->pipe_rc0 = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_fir1_hunt";
    pipe_info.threadcount_x = 64U;
    pipe_info.threadcount_y = 1U;
    pipe_info.threadcount_z = 1U;
    ctx->pipe_fir1 = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_fir1_bitslice";
    ctx->pipe_fir1_bs = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_searu_solve";
    ctx->pipe_searu = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_fir1_faultsim";
    ctx->pipe_faultsim = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_info_sim_matrix";
    ctx->pipe_info_sim = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    pipe_info.entrypoint = "bloom_node_dst";
    ctx->pipe_node_dst = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    SDL_free(code);
    if (!ctx->pipeline || !ctx->pipe_rc2 || !ctx->pipe_rc1 || !ctx->pipe_rc0 ||
        !ctx->pipe_fir1 || !ctx->pipe_fir1_bs || !ctx->pipe_searu ||
        !ctx->pipe_faultsim || !ctx->pipe_info_sim || !ctx->pipe_node_dst) {
        fprintf(stderr, "[BLOOM SDL3-GPU] SDL_CreateGPUComputePipeline('%s') failed: %s\n",
                resolved_path, SDL_GetError());
        bloom_gpu_destroy(ctx);
        return NULL;
    }

    SDL_GPUBufferCreateInfo buf_info;
    memset(&buf_info, 0, sizeof(buf_info));
    buf_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    buf_info.size = (Uint32)BLOOM_ARENA_BYTES;
    ctx->arena_gpu_buf = SDL_CreateGPUBuffer(dev, &buf_info);

    buf_info.size = (Uint32)BLOOM_MESH_ARENA_BYTES;
    ctx->mesh_gpu_buf = SDL_CreateGPUBuffer(dev, &buf_info);

    buf_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    buf_info.size = (Uint32)BLOOM_RC_BUFFER_BYTES;
    ctx->rc_gpu_buf = SDL_CreateGPUBuffer(dev, &buf_info);

    SDL_GPUTransferBufferCreateInfo up_info;
    memset(&up_info, 0, sizeof(up_info));
    up_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    up_info.size = (Uint32)BLOOM_ARENA_BYTES;
    ctx->upload_tbuf = SDL_CreateGPUTransferBuffer(dev, &up_info);

    up_info.size = (Uint32)BLOOM_MESH_ARENA_BYTES;
    ctx->mesh_upload_tbuf = SDL_CreateGPUTransferBuffer(dev, &up_info);

    SDL_GPUTransferBufferCreateInfo rc_dl_info;
    memset(&rc_dl_info, 0, sizeof(rc_dl_info));
    rc_dl_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    rc_dl_info.size = (Uint32)BLOOM_RC_BUFFER_BYTES;
    ctx->rc_download_tbuf = SDL_CreateGPUTransferBuffer(dev, &rc_dl_info);

    if (!ctx->arena_gpu_buf || !ctx->mesh_gpu_buf || !ctx->rc_gpu_buf || !ctx->upload_tbuf ||
        !ctx->mesh_upload_tbuf || !ctx->rc_download_tbuf || !bloom_gpu_alloc_frame_targets(ctx, w, h) ||
        !bloom_gpu_upload_mesh(ctx, NULL)) {
        fprintf(stderr, "[BLOOM SDL3-GPU] GPU resource allocation failed: %s\n", SDL_GetError());
        bloom_gpu_destroy(ctx);
        return NULL;
    }

    const char *drv = SDL_GetGPUDeviceDriver(dev);
    snprintf(ctx->driver_name, sizeof(ctx->driver_name), "%s", (drv && drv[0]) ? drv : "gpu");

    const char *hw_name = NULL;
#ifdef SDL_PROP_GPU_DEVICE_NAME_STRING
    SDL_PropertiesID dev_props = SDL_GetGPUDeviceProperties(dev);
    if (dev_props != 0U) {
        hw_name = SDL_GetStringProperty(dev_props, SDL_PROP_GPU_DEVICE_NAME_STRING, NULL);
    }
#endif
    if (hw_name && hw_name[0] != '\0') {
        snprintf(ctx->device_name, sizeof(ctx->device_name), "%s (SDL3-GPU/%s)",
                 hw_name, ctx->driver_name);
    } else {
        snprintf(ctx->device_name, sizeof(ctx->device_name), "SDL3-GPU/%s", ctx->driver_name);
    }

    bool is_sw_vulkan = (hw_name != NULL) &&
                        (strstr(hw_name, "llvmpipe") != NULL ||
                         strstr(hw_name, "Lavapipe") != NULL ||
                         strstr(hw_name, "SwiftShader") != NULL);
    ctx->hardware_gpu = !is_sw_vulkan;
    ctx->unified_memory = (strcmp(ctx->driver_name, "metal") == 0) || is_sw_vulkan;
    return ctx;
}

bool bloom_gpu_is_hardware_gpu(const BloomGpuCtx *ctx) {
    return ctx ? ctx->hardware_gpu : false;
}

bool bloom_gpu_is_unified_memory(const BloomGpuCtx *ctx) {
    return ctx ? ctx->unified_memory : false;
}

const char *bloom_gpu_device_name(const BloomGpuCtx *ctx) {
    return ctx ? ctx->device_name : "none";
}

const char *bloom_gpu_driver_name(const BloomGpuCtx *ctx) {
    return ctx ? ctx->driver_name : "none";
}

uint32_t bloom_gpu_dispatch_count(const BloomGpuCtx *ctx) {
    return ctx ? ctx->dispatch_count : 0U;
}

uint32_t bloom_gpu_idle_skip_count(const BloomGpuCtx *ctx) {
    return ctx ? ctx->idle_skip_count : 0U;
}

uint32_t bloom_gpu_rc_cache_hits(const BloomGpuCtx *ctx) {
    return ctx ? ctx->rc_cache_hits : 0U;
}

static uint64_t bloom_gpu_fnv1a_u32(uint64_t h, uint32_t v) {
    for (uint32_t b = 0U; b < 4U; ++b) {
        h ^= (uint64_t)((v >> (b * 8U)) & 0xFFU);
        h *= UINT64_C(0x00000100000001B3);
    }
    return h;
}

static uint64_t bloom_gpu_hash_rc_scene(const BloomEngineArena *arena, uint32_t mesh_epoch) {
    const BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    const uint32_t *u32_cells = (const uint32_t *)(const void *)arena->cells;
    uint64_t h = UINT64_C(0xCBF29CE484222325);
    h = bloom_gpu_fnv1a_u32(h, hdr->frame_seq);
    h = bloom_gpu_fnv1a_u32(h, hdr->inst_count);
    h = bloom_gpu_fnv1a_u32(h, hdr->csg_count);
    h = bloom_gpu_fnv1a_u32(h, hdr->bvh_count);
    h = bloom_gpu_fnv1a_u32(h, hdr->flags & (BLOOM_FLAG_XRAY_ENABLED | BLOOM_FLAG_RADIANCE_CASCADES));
    h = bloom_gpu_fnv1a_u32(h, (uint32_t)hdr->xray_plane_q16);
    h = bloom_gpu_fnv1a_u32(h, mesh_epoch);
    for (size_t i = 8U * 16U; i < 152U * 16U; ++i) {
        h = bloom_gpu_fnv1a_u32(h, u32_cells[i]);
    }
    for (size_t i = 254U * 16U; i < 256U * 16U; ++i) {
        h = bloom_gpu_fnv1a_u32(h, u32_cells[i]);
    }
    return h;
}

static uint32_t bloom_fir_fmix32(uint32_t h) {
    h ^= h >> 16U;
    h *= UINT32_C(0x85EBCA6B);
    h ^= h >> 13U;
    h *= UINT32_C(0xC2B2AE35);
    h ^= h >> 16U;
    return h;
}

static void bloom_fir_build_trial_inputs(const flow_ir_prog_t *prog,
                                         const BloomFir1HuntConfig *cfg,
                                         uint32_t gtid,
                                         uint64_t out_inputs[FLOW_IR_MAX_IO]) {
    memset(out_inputs, 0, sizeof(uint64_t) * FLOW_IR_MAX_IO);
    uint64_t seed_full = ((uint64_t)cfg->seed_hi << 32U) | (uint64_t)cfg->seed_lo;
    uint64_t ex_val = seed_full ^ (uint64_t)gtid;
    uint32_t ex_bit = 0U;
    for (uint32_t i = 0U; i < prog->num_inputs; ++i) {
        uint64_t wm = flow_ir_mask_u64(prog->vals[i].width);
        uint64_t iv = cfg->base_inputs[i];
        uint32_t sh = (i < 31U) ? i : 31U;
        if (((cfg->vary_input_mask >> sh) & 1U) != 0U) {
            if (cfg->sweep_mode == 2U) {
                iv = (ex_bit < 64U) ? (ex_val >> ex_bit) : UINT64_C(0);
                ex_bit += (uint32_t)prog->vals[i].width;
            } else if (cfg->sweep_mode == 1U) {
                uint32_t h0 = bloom_fir_fmix32(cfg->seed_lo ^ gtid ^ (i * UINT32_C(0x9E3779B9)));
                uint32_t h1 = bloom_fir_fmix32(cfg->seed_hi ^ (gtid * UINT32_C(0x85EBCA6B)) ^
                                               (i * UINT32_C(0xC2B2AE35)) ^ h0);
                iv = ((uint64_t)h1 << 32U) | (uint64_t)h0;
            } else {
                if (i == 0U) {
                    iv += seed_full + (uint64_t)gtid;
                } else {
                    iv += (uint64_t)(gtid ^ (i * UINT32_C(0x9E3779B9)));
                }
            }
        }
        out_inputs[i] = iv & wm;
    }
}

static bool bloom_fir_dispatch_reduce_chunk(BloomGpuCtx *ctx,
                                            SDL_GPUComputePipeline *pipe,
                                            bool bind_mesh,
                                            bool bind_out_tex,
                                            const BloomEngineArena *h_arena,
                                            uint32_t num_threads,
                                            uint32_t num_wgs,
                                            uint32_t base_off,
                                            BloomFir1HuntResult *out) {
    void *mapped_up = SDL_MapGPUTransferBuffer(ctx->device, ctx->upload_tbuf, false);
    if (!mapped_up) {
        return false;
    }
    memcpy(mapped_up, h_arena, BLOOM_ARENA_BYTES);
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upload_tbuf);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        return false;
    }

    SDL_GPUCopyPass *up_pass = SDL_BeginGPUCopyPass(cmd);
    if (!up_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    SDL_GPUTransferBufferLocation up_src;
    memset(&up_src, 0, sizeof(up_src));
    up_src.transfer_buffer = ctx->upload_tbuf;
    SDL_GPUBufferRegion up_dst;
    memset(&up_dst, 0, sizeof(up_dst));
    up_dst.buffer = ctx->arena_gpu_buf;
    up_dst.size = (Uint32)BLOOM_ARENA_BYTES;
    SDL_UploadToGPUBuffer(up_pass, &up_src, &up_dst, false);
    SDL_EndGPUCopyPass(up_pass);

    SDL_GPUStorageTextureReadWriteBinding rw_tex;
    memset(&rw_tex, 0, sizeof(rw_tex));
    rw_tex.texture = ctx->frame_tex;
    SDL_GPUStorageBufferReadWriteBinding rw_buf;
    memset(&rw_buf, 0, sizeof(rw_buf));
    rw_buf.buffer = ctx->rc_gpu_buf;
    SDL_GPUComputePass *cpass = SDL_BeginGPUComputePass(
        cmd,
        bind_out_tex ? &rw_tex : NULL,
        bind_out_tex ? 1U : 0U,
        &rw_buf,
        1U);
    if (!cpass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    SDL_GPUBuffer *ro_bufs[2] = { ctx->arena_gpu_buf, ctx->mesh_gpu_buf };
    SDL_BindGPUComputePipeline(cpass, pipe);
    SDL_BindGPUComputeStorageBuffers(cpass, 0U, ro_bufs, bind_mesh ? 2U : 1U);
    SDL_DispatchGPUCompute(cpass, num_wgs, 1U, 1U);
    SDL_EndGPUComputePass(cpass);

    SDL_GPUCopyPass *dl_pass = SDL_BeginGPUCopyPass(cmd);
    if (!dl_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    SDL_GPUBufferRegion dl_src;
    memset(&dl_src, 0, sizeof(dl_src));
    dl_src.buffer = ctx->rc_gpu_buf;
    dl_src.size = (Uint32)(num_threads * 16U);
    SDL_GPUTransferBufferLocation dl_dst;
    memset(&dl_dst, 0, sizeof(dl_dst));
    dl_dst.transfer_buffer = ctx->rc_download_tbuf;
    SDL_DownloadFromGPUBuffer(dl_pass, &dl_src, &dl_dst);
    SDL_EndGPUCopyPass(dl_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence || !SDL_WaitForGPUFences(ctx->device, true, &fence, 1U)) {
        if (fence) SDL_ReleaseGPUFence(ctx->device, fence);
        return false;
    }
    SDL_ReleaseGPUFence(ctx->device, fence);

    const uint32_t *wg_res = (const uint32_t *)SDL_MapGPUTransferBuffer(
        ctx->device, ctx->rc_download_tbuf, false);
    if (!wg_res) {
        return false;
    }
    for (uint32_t t = 0U; t < num_threads; ++t) {
        uint32_t min_local = wg_res[t * 4U + 0U];
        uint32_t t_hits = wg_res[t * 4U + 1U];
        uint64_t t_xor = ((uint64_t)wg_res[t * 4U + 3U] << 32U) | (uint64_t)wg_res[t * 4U + 2U];
        out->out0_xor_checksum ^= t_xor;
        out->hit_count += t_hits;
        if (min_local != 0xFFFFFFFFU) {
            uint32_t cand = base_off + min_local;
            if (!out->found || cand < out->first_hit_trial) {
                out->found = true;
                out->first_hit_trial = cand;
            }
        }
    }
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->rc_download_tbuf);
    return true;
}

static bool bloom_fir_verify_witness(const flow_ir_prog_t *prog,
                                     const BloomFir1HuntConfig *cfg,
                                     BloomFir1HuntResult *out) {
    if (!out->found) {
        return true;
    }
    bloom_fir_build_trial_inputs(prog, cfg, out->first_hit_trial, out->witness_inputs);
    if (!flow_ir_eval_u64(prog, out->witness_inputs, out->witness_outputs)) {
        return false;
    }
    uint64_t target = ((uint64_t)cfg->target_hi << 32U) | (uint64_t)cfg->target_lo;
    uint64_t got = (prog->num_outputs > 0U) ? out->witness_outputs[0] : 0ULL;
    bool match = (cfg->target_mode == 0U) ? (got == target) :
                 ((cfg->target_mode == 1U) ? (got != target) : true);
    out->cpu_verified = match;
    return match;
}

bool bloom_gpu_hunt_fir1(BloomGpuCtx *ctx,
                         const uint8_t *fir_bytes,
                         size_t fir_len,
                         const BloomFir1HuntConfig *cfg,
                         BloomFir1HuntResult *out) {
    if (!ctx || !ctx->device || !ctx->pipe_fir1 || !ctx->arena_gpu_buf || !ctx->mesh_gpu_buf ||
        !ctx->rc_gpu_buf || !ctx->upload_tbuf || !ctx->mesh_upload_tbuf || !ctx->rc_download_tbuf ||
        !fir_bytes || !cfg || !out || cfg->num_trials == 0U) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->first_hit_trial = 0xFFFFFFFFU;

    flow_ir_prog_t *prog = (flow_ir_prog_t *)calloc(1U, sizeof(flow_ir_prog_t));
    BloomEngineArena *h_arena = (BloomEngineArena *)calloc(1U, sizeof(BloomEngineArena));
    BloomMeshArena *h_mesh = (BloomMeshArena *)calloc(1U, sizeof(BloomMeshArena));
    if (!prog || !h_arena || !h_mesh) {
        free(h_mesh);
        free(h_arena);
        free(prog);
        return false;
    }

    if (!flow_ir_load_bytes(prog, fir_bytes, fir_len) ||
        prog->num_vals == 0U || prog->num_vals > 128U ||
        prog->num_ops > 512U || prog->num_lut_u64 > 1736U ||
        prog->num_inputs > 32U || prog->num_outputs > 32U) {
        free(h_mesh);
        free(h_arena);
        free(prog);
        return false;
    }
    for (uint32_t i = 0U; i < prog->num_vals; ++i) {
        if (prog->vals[i].width > 64U) {
            free(h_mesh);
            free(h_arena);
            free(prog);
            return false;
        }
    }

    out->num_inputs = prog->num_inputs;
    out->num_outputs = prog->num_outputs;

    uint32_t *au = (uint32_t *)(void *)h_arena;
    au[0] = UINT32_C(0x46495248); /* "FIRH" */
    au[1] = prog->num_vals;
    au[2] = prog->num_inputs;
    au[3] = prog->num_outputs;
    au[4] = prog->num_ops;
    au[5] = prog->num_lut_u64;
    au[7] = cfg->target_mode;
    au[8] = cfg->seed_lo;
    au[9] = cfg->seed_hi;
    au[10] = cfg->target_lo;
    au[11] = cfg->target_hi;
    au[12] = cfg->vary_input_mask;
    au[13] = cfg->sweep_mode;

    for (uint32_t i = 0U; i < prog->num_inputs; ++i) {
        au[16U + i * 2U + 0U] = (uint32_t)(cfg->base_inputs[i] & UINT64_C(0xFFFFFFFF));
        au[16U + i * 2U + 1U] = (uint32_t)(cfg->base_inputs[i] >> 32U);
    }
    for (uint32_t i = 0U; i < prog->num_outputs; ++i) {
        au[80U + i] = prog->outputs[i];
    }
    for (uint32_t i = 0U; i < prog->num_vals; ++i) {
        au[112U + i] = (uint32_t)prog->vals[i].width;
    }
    for (uint32_t i = 0U; i < prog->num_lut_u64; ++i) {
        au[624U + i * 2U + 0U] = (uint32_t)(prog->lut_pool[i] & UINT64_C(0xFFFFFFFF));
        au[624U + i * 2U + 1U] = (uint32_t)(prog->lut_pool[i] >> 32U);
    }
    uint32_t *mu = (uint32_t *)(void *)h_mesh;
    for (uint32_t i = 0U; i < prog->num_ops; ++i) {
        const flow_ir_op_t *op = &prog->ops[i];
        uint32_t *mo = mu + (i * 16U);
        mo[0] = (uint32_t)op->kind | ((uint32_t)op->num_ins << 16U);
        mo[1] = op->out_val;
        mo[2] = op->ins[0];
        mo[3] = op->ins[1];
        mo[4] = op->ins[2];
        mo[5] = op->ins[3];
        mo[6] = op->ins[4];
        mo[7] = op->ins[5];
        mo[8] = op->lut_off;
        mo[9] = op->lut_len;
        mo[10] = (uint32_t)(op->p0 & UINT64_C(0xFFFFFFFF));
        mo[11] = (uint32_t)(op->p0 >> 32U);
        mo[12] = (uint32_t)(op->p1 & UINT64_C(0xFFFFFFFF));
        mo[13] = (uint32_t)(op->p1 >> 32U);
        mo[14] = (uint32_t)(op->p2 & UINT64_C(0xFFFFFFFF));
        mo[15] = (uint32_t)(op->p3 & UINT64_C(0xFFFFFFFF));
    }

    if (!bloom_gpu_upload_mesh(ctx, h_mesh)) {
        free(h_mesh);
        free(h_arena);
        free(prog);
        return false;
    }

    const uint32_t max_chunk_trials = UINT32_C(4194304);
    uint64_t t_start_ns = SDL_GetTicksNS();
    uint32_t remaining = cfg->num_trials;
    uint32_t base_off = 0U;
    bool ok = true;

    while (remaining > 0U && ok) {
        uint32_t chunk_trials = (remaining > max_chunk_trials) ? max_chunk_trials : remaining;
        uint32_t num_threads = (chunk_trials < 32768U) ? chunk_trials : 32768U;
        uint32_t num_wgs = (num_threads + 63U) / 64U;
        au[6] = chunk_trials;
        au[14] = num_threads;
        au[15] = base_off;

        if (!bloom_fir_dispatch_reduce_chunk(ctx, ctx->pipe_fir1, true, true, h_arena,
                                             num_threads, num_wgs, base_off, out)) {
            ok = false;
            break;
        }
        out->trials_evaluated += chunk_trials;
        base_off += chunk_trials;
        remaining -= chunk_trials;
    }

    uint64_t t_end_ns = SDL_GetTicksNS();
    out->elapsed_us = (t_end_ns >= t_start_ns) ? ((t_end_ns - t_start_ns) / 1000ULL) : 1ULL;
    if (out->elapsed_us == 0ULL) {
        out->elapsed_us = 1ULL;
    }
    ctx->rc_valid = false;
    ctx->mesh_epoch++;

    if (ok && !bloom_fir_verify_witness(prog, cfg, out)) {
        ok = false;
    }

    free(h_mesh);
    free(h_arena);
    free(prog);
    return ok;
}

/* ----------------------------------------------------------------------------
 * 32-Way Bit-Blaster + SIMT Bit-Sliced Accelerator + AOT WGSL Compiler
 * ---------------------------------------------------------------------------- */

#define BLOOM_BS_TMP_WIRES  4096U
#define BLOOM_BS_TMP_GATES  8192U
#define BLOOM_BS_MAX_WIRES  512U
#define BLOOM_BS_MAX_INBITS 256U
#define BLOOM_BS_MAX_GATES  2048U

typedef enum {
    BLOOM_BS_AND = 1U,
    BLOOM_BS_OR  = 2U,
    BLOOM_BS_XOR = 3U,
    BLOOM_BS_NOT = 4U,
    BLOOM_BS_MUX = 5U
} BloomBsGateKind;

typedef struct {
    uint16_t kind;
    uint16_t out_w;
    uint16_t a;
    uint16_t b;
    uint16_t c;
} BloomBsGate;

typedef struct {
    uint16_t wire_id;
    uint8_t  in_idx;
    uint8_t  in_bit;
    uint8_t  src_bit;
    uint8_t  const_bit;
    bool     varying;
} BloomBsInput;

typedef struct {
    uint32_t     num_wires;
    uint32_t     num_inputs;
    uint32_t     num_gates;
    uint32_t     out0_width;
    BloomBsInput inputs[BLOOM_BS_MAX_INBITS];
    uint16_t     out0_wires[64];
    BloomBsGate  gates[BLOOM_BS_MAX_GATES];
} BloomBsNetlist;

typedef struct {
    uint32_t     num_wires;
    uint32_t     num_gates;
    uint32_t     num_inputs;
    BloomBsGate  gates[BLOOM_BS_TMP_GATES];
    int32_t      wire_gate[BLOOM_BS_TMP_WIRES];
    int32_t      wire_in[BLOOM_BS_TMP_WIRES];
    uint16_t     not_cache[BLOOM_BS_TMP_WIRES];
    BloomBsInput tmp_inputs[BLOOM_BS_TMP_WIRES];
    uint16_t     val_wires[FLOW_IR_MAX_VALS][64];
    uint8_t      used[BLOOM_BS_TMP_WIRES];
    int32_t      remap[BLOOM_BS_TMP_WIRES];
} BloomBsBuilder;

static uint16_t bb_push_gate(BloomBsBuilder *bb, uint16_t kind, uint16_t a, uint16_t b, uint16_t c) {
    if (bb->num_wires >= BLOOM_BS_TMP_WIRES || bb->num_gates >= BLOOM_BS_TMP_GATES) {
        return UINT16_C(0xFFFF);
    }
    uint16_t wid = (uint16_t)bb->num_wires++;
    bb->wire_gate[wid] = (int32_t)bb->num_gates;
    bb->wire_in[wid] = -1;
    bb->gates[bb->num_gates].kind = kind;
    bb->gates[bb->num_gates].out_w = wid;
    bb->gates[bb->num_gates].a = a;
    bb->gates[bb->num_gates].b = b;
    bb->gates[bb->num_gates].c = c;
    bb->num_gates++;
    return wid;
}

static uint16_t bb_not(BloomBsBuilder *bb, uint16_t a) {
    if (a == UINT16_C(0xFFFF)) return a;
    if (a == 0U) return 1U;
    if (a == 1U) return 0U;
    if (bb->not_cache[a] != UINT16_C(0xFFFF)) {
        return bb->not_cache[a];
    }
    uint16_t inv = bb_push_gate(bb, BLOOM_BS_NOT, a, 0U, 0U);
    if (inv != UINT16_C(0xFFFF)) {
        bb->not_cache[a] = inv;
        bb->not_cache[inv] = a;
    }
    return inv;
}

static uint16_t bb_and(BloomBsBuilder *bb, uint16_t a, uint16_t b) {
    if (a == UINT16_C(0xFFFF) || b == UINT16_C(0xFFFF)) return UINT16_C(0xFFFF);
    if (a == 0U || b == 0U) return 0U;
    if (a == 1U) return b;
    if (b == 1U || a == b) return a;
    return bb_push_gate(bb, BLOOM_BS_AND, a, b, 0U);
}

static uint16_t bb_or(BloomBsBuilder *bb, uint16_t a, uint16_t b) {
    if (a == UINT16_C(0xFFFF) || b == UINT16_C(0xFFFF)) return UINT16_C(0xFFFF);
    if (a == 1U || b == 1U) return 1U;
    if (a == 0U) return b;
    if (b == 0U || a == b) return a;
    return bb_push_gate(bb, BLOOM_BS_OR, a, b, 0U);
}

static uint16_t bb_xor(BloomBsBuilder *bb, uint16_t a, uint16_t b) {
    if (a == UINT16_C(0xFFFF) || b == UINT16_C(0xFFFF)) return UINT16_C(0xFFFF);
    if (a == 0U) return b;
    if (b == 0U) return a;
    if (a == b) return 0U;
    if (a == 1U) return bb_not(bb, b);
    if (b == 1U) return bb_not(bb, a);
    return bb_push_gate(bb, BLOOM_BS_XOR, a, b, 0U);
}

static uint16_t bb_mux(BloomBsBuilder *bb, uint16_t sel, uint16_t f_br, uint16_t t_br) {
    if (sel == UINT16_C(0xFFFF) || f_br == UINT16_C(0xFFFF) || t_br == UINT16_C(0xFFFF)) {
        return UINT16_C(0xFFFF);
    }
    if (sel == 0U) return f_br;
    if (sel == 1U || f_br == t_br) return t_br;
    if (f_br == 0U && t_br == 1U) return sel;
    if (f_br == 1U && t_br == 0U) return bb_not(bb, sel);
    if (f_br == 0U) return bb_and(bb, sel, t_br);
    if (t_br == 1U) return bb_or(bb, sel, f_br);
    if (f_br == 1U) return bb_or(bb, bb_not(bb, sel), t_br);
    if (t_br == 0U) return bb_and(bb, bb_not(bb, sel), f_br);
    return bb_push_gate(bb, BLOOM_BS_MUX, sel, f_br, t_br);
}

static uint16_t bb_or_reduce_bits(BloomBsBuilder *bb, const uint16_t *bits, uint32_t count) {
    if (count == 0U) {
        return 0U;
    }
    uint16_t acc = bits[0];
    for (uint32_t i = 1U; i < count; ++i) {
        acc = bb_or(bb, acc, bits[i]);
    }
    return acc;
}

static bool bb_blast_add(BloomBsBuilder *bb,
                         const uint16_t *a_bits,
                         const uint16_t *b_bits,
                         uint16_t cin,
                         bool invert_b,
                         uint32_t width,
                         uint16_t *out_bits) {
    for (uint32_t b = 0U; b < width; ++b) {
        uint16_t av = a_bits[b];
        uint16_t bv = invert_b ? bb_not(bb, b_bits[b]) : b_bits[b];
        uint16_t axb = bb_xor(bb, av, bv);
        uint16_t sum = bb_xor(bb, axb, cin);
        if (sum == UINT16_C(0xFFFF)) {
            return false;
        }
        out_bits[b] = sum;
        if (b + 1U < width) {
            uint16_t c1 = bb_and(bb, av, bv);
            uint16_t c2 = bb_and(bb, axb, cin);
            cin = bb_or(bb, c1, c2);
            if (cin == UINT16_C(0xFFFF)) {
                return false;
            }
        }
    }
    return true;
}

static uint16_t bb_lut_mux_tree(BloomBsBuilder *bb,
                                const uint64_t *lut,
                                uint32_t lut_len,
                                const uint16_t *addr_bits,
                                uint32_t depth,
                                uint32_t idx_base,
                                uint32_t out_bit) {
    if (depth == 0U) {
        if (idx_base < lut_len && ((lut[idx_base] >> out_bit) & 1ULL) != 0ULL) {
            return 1U;
        }
        return 0U;
    }
    uint32_t half = 1U << (depth - 1U);
    uint16_t lo = bb_lut_mux_tree(bb, lut, lut_len, addr_bits, depth - 1U, idx_base, out_bit);
    uint16_t hi = bb_lut_mux_tree(bb, lut, lut_len, addr_bits, depth - 1U, idx_base + half, out_bit);
    return bb_mux(bb, addr_bits[depth - 1U], lo, hi);
}

static bool bb_blast_shift(BloomBsBuilder *bb,
                           const uint16_t *in_bits,
                           uint32_t iw,
                           const uint16_t *sh_bits,
                           uint32_t sw,
                           bool shift_left,
                           uint32_t ow,
                           uint16_t *out_bits) {
    uint32_t width = shift_left ? ow : iw;
    if (width > 64U) width = 64U;
    uint16_t cur[64];
    for (uint32_t b = 0U; b < 64U; ++b) {
        cur[b] = (b < iw) ? in_bits[b] : 0U;
    }

    uint32_t active_sh = (sw < 6U) ? sw : 6U;
    for (uint32_t s = 0U; s < active_sh; ++s) {
        uint32_t amt = 1U << s;
        uint16_t nxt[64];
        for (uint32_t b = 0U; b < width; ++b) {
            uint16_t moved = 0U;
            if (shift_left) {
                moved = (b >= amt) ? cur[b - amt] : 0U;
            } else {
                moved = (b + amt < iw) ? cur[b + amt] : 0U;
            }
            nxt[b] = bb_mux(bb, sh_bits[s], cur[b], moved);
            if (nxt[b] == UINT16_C(0xFFFF)) {
                return false;
            }
        }
        for (uint32_t b = 0U; b < width; ++b) {
            cur[b] = nxt[b];
        }
    }

    uint16_t valid = 1U;
    if (sw > 6U) {
        uint16_t hi_any = bb_or_reduce_bits(bb, sh_bits + 6U, sw - 6U);
        valid = bb_not(bb, hi_any);
        if (valid == UINT16_C(0xFFFF)) {
            return false;
        }
    }
    for (uint32_t b = 0U; b < ow; ++b) {
        out_bits[b] = (b < width) ? bb_and(bb, valid, cur[b]) : 0U;
        if (out_bits[b] == UINT16_C(0xFFFF)) {
            return false;
        }
    }
    return true;
}

static bool bb_init_builder(BloomBsBuilder *bb,
                            const flow_ir_prog_t *prog,
                            uint32_t vary_mask,
                            const uint64_t base_inputs[32],
                            bool dynamic_inputs) {
    memset(bb->wire_gate, -1, sizeof(bb->wire_gate));
    memset(bb->wire_in, -1, sizeof(bb->wire_in));
    for (uint32_t i = 0U; i < BLOOM_BS_TMP_WIRES; ++i) {
        bb->not_cache[i] = UINT16_C(0xFFFF);
        bb->remap[i] = -1;
    }
    bb->num_wires = 2U; /* 0 = const 0, 1 = const 1 */

    uint32_t ex_bit = 0U;
    for (uint32_t i = 0U; i < prog->num_inputs; ++i) {
        uint32_t w = prog->vals[i].width;
        if (w == 0U || w > 64U) {
            return false;
        }
        uint32_t sh = (i < 31U) ? i : 31U;
        bool is_vary = ((vary_mask >> sh) & 1U) != 0U;
        for (uint32_t b = 0U; b < w; ++b) {
            uint8_t cbit = (uint8_t)((base_inputs[i] >> b) & 1ULL);
            if (!dynamic_inputs && !is_vary) {
                bb->val_wires[i][b] = (cbit != 0U) ? 1U : 0U;
                continue;
            }
            if (!dynamic_inputs && is_vary && (ex_bit + b >= 64U)) {
                bb->val_wires[i][b] = 0U;
                continue;
            }
            if (bb->num_wires >= BLOOM_BS_TMP_WIRES) {
                return false;
            }
            uint16_t wid = (uint16_t)bb->num_wires++;
            uint32_t sbit = is_vary ? (ex_bit + b) : 255U;
            if (sbit > 64U && is_vary) sbit = 64U;
            bb->wire_in[wid] = (int32_t)bb->num_inputs;
            BloomBsInput *bi = &bb->tmp_inputs[bb->num_inputs++];
            bi->wire_id = wid;
            bi->in_idx = (uint8_t)i;
            bi->in_bit = (uint8_t)b;
            bi->src_bit = (uint8_t)sbit;
            bi->const_bit = cbit;
            bi->varying = is_vary;
            bb->val_wires[i][b] = wid;
        }
        if (is_vary || dynamic_inputs) {
            ex_bit += w;
        }
    }
    return true;
}

static bool bb_blast_prog_ops(BloomBsBuilder *bb,
                              const flow_ir_prog_t *prog,
                              uint16_t val_wires[FLOW_IR_MAX_VALS][64]) {
    for (uint32_t oi = 0U; oi < prog->num_ops; ++oi) {
        const flow_ir_op_t *op = &prog->ops[oi];
        uint32_t oid = op->out_val;
        if (oid >= FLOW_IR_MAX_VALS) {
            return false;
        }
        uint32_t ow = prog->vals[oid].width;
        if (ow == 0U || ow > 64U) {
            return false;
        }
        uint16_t k = op->kind;
        uint16_t *dst = val_wires[oid];

        if (k == FLOW_IR_OP_CONST_U64) {
            for (uint32_t b = 0U; b < ow; ++b) {
                dst[b] = (((op->p0 >> b) & 1ULL) != 0ULL) ? 1U : 0U;
            }
        } else if (k == FLOW_IR_OP_SLICE) {
            if (op->num_ins < 1U || op->p0 >= 64ULL) { return false; }
            uint32_t in0 = op->ins[0];
            uint32_t iw0 = prog->vals[in0].width;
            uint32_t off = (uint32_t)op->p0;
            for (uint32_t b = 0U; b < ow; ++b) {
                dst[b] = (off + b < iw0) ? val_wires[in0][off + b] : 0U;
            }
        } else if (k == FLOW_IR_OP_CONCAT) {
            uint32_t pos = 0U;
            for (uint16_t j = 0U; j < op->num_ins && j < FLOW_IR_MAX_INS; ++j) {
                uint32_t iv = op->ins[j];
                uint32_t iw = prog->vals[iv].width;
                for (uint32_t b = 0U; b < iw; ++b) {
                    if (pos + b < ow) {
                        dst[pos + b] = val_wires[iv][b];
                    }
                }
                pos += iw;
            }
        } else if (k == FLOW_IR_OP_XOR_VEC) {
            if (op->num_ins < 2U) { return false; }
            uint32_t i0 = op->ins[0], i1 = op->ins[1];
            uint32_t w0 = prog->vals[i0].width, w1 = prog->vals[i1].width;
            for (uint32_t b = 0U; b < ow; ++b) {
                uint16_t a = (b < w0) ? val_wires[i0][b] : 0U;
                uint16_t bv = (b < w1) ? val_wires[i1][b] : 0U;
                dst[b] = bb_xor(bb, a, bv);
                if (dst[b] == UINT16_C(0xFFFF)) { return false; }
            }
        } else if (k == FLOW_IR_OP_AND_VEC) {
            if (op->num_ins < 2U) { return false; }
            uint32_t i0 = op->ins[0], i1 = op->ins[1];
            uint32_t w0 = prog->vals[i0].width, w1 = prog->vals[i1].width;
            for (uint32_t b = 0U; b < ow; ++b) {
                uint16_t a = (b < w0) ? val_wires[i0][b] : 0U;
                uint16_t bv = (b < w1) ? val_wires[i1][b] : 0U;
                dst[b] = bb_and(bb, a, bv);
                if (dst[b] == UINT16_C(0xFFFF)) { return false; }
            }
        } else if (k == FLOW_IR_OP_NOT_VEC) {
            if (op->num_ins < 1U) { return false; }
            uint32_t i0 = op->ins[0];
            uint32_t w0 = prog->vals[i0].width;
            for (uint32_t b = 0U; b < ow; ++b) {
                uint16_t a = (b < w0) ? val_wires[i0][b] : 0U;
                dst[b] = bb_not(bb, a);
                if (dst[b] == UINT16_C(0xFFFF)) { return false; }
            }
        } else if (k == FLOW_IR_OP_OR_REDUCE) {
            if (op->num_ins < 1U) { return false; }
            uint32_t i0 = op->ins[0];
            dst[0] = bb_or_reduce_bits(bb, val_wires[i0], prog->vals[i0].width);
            if (dst[0] == UINT16_C(0xFFFF)) { return false; }
        } else if (k == FLOW_IR_OP_EQ_ZERO) {
            if (op->num_ins < 1U) { return false; }
            uint32_t i0 = op->ins[0];
            uint16_t any1 = bb_or_reduce_bits(bb, val_wires[i0], prog->vals[i0].width);
            dst[0] = bb_not(bb, any1);
            if (dst[0] == UINT16_C(0xFFFF)) { return false; }
        } else if (k == FLOW_IR_OP_ADD || k == FLOW_IR_OP_SUB) {
            if (op->num_ins < 2U) { return false; }
            uint16_t abits[64], bbits[64];
            uint32_t i0 = op->ins[0], i1 = op->ins[1];
            uint32_t w0 = prog->vals[i0].width, w1 = prog->vals[i1].width;
            for (uint32_t b = 0U; b < ow; ++b) {
                abits[b] = (b < w0) ? val_wires[i0][b] : 0U;
                bbits[b] = (b < w1) ? val_wires[i1][b] : 0U;
            }
            bool is_sub = (k == FLOW_IR_OP_SUB);
            if (!bb_blast_add(bb, abits, bbits, is_sub ? 1U : 0U, is_sub, ow, dst)) {
                return false;
            }
        } else if (k == FLOW_IR_OP_CTRL_SEL) {
            if (op->num_ins < 3U) { return false; }
            uint16_t sel = val_wires[op->ins[0]][0];
            uint32_t i1 = op->ins[1], i2 = op->ins[2];
            uint32_t w1 = prog->vals[i1].width, w2 = prog->vals[i2].width;
            for (uint32_t b = 0U; b < ow; ++b) {
                uint16_t f_br = (b < w1) ? val_wires[i1][b] : 0U;
                uint16_t t_br = (b < w2) ? val_wires[i2][b] : 0U;
                dst[b] = bb_mux(bb, sel, f_br, t_br);
                if (dst[b] == UINT16_C(0xFFFF)) { return false; }
            }
        } else if (k == FLOW_IR_OP_SHL_VAR || k == FLOW_IR_OP_SHR_VAR) {
            if (op->num_ins < 2U) { return false; }
            uint32_t i0 = op->ins[0], i1 = op->ins[1];
            if (!bb_blast_shift(bb, val_wires[i0], prog->vals[i0].width,
                                val_wires[i1], prog->vals[i1].width,
                                (k == FLOW_IR_OP_SHL_VAR), ow, dst)) {
                return false;
            }
        } else if (k == FLOW_IR_OP_LUT_ROM) {
            if (op->num_ins < 1U || op->lut_len > 64U) { return false; }
            uint32_t i0 = op->ins[0];
            uint32_t iw = prog->vals[i0].width;
            uint32_t depth = (iw < 6U) ? iw : 6U;
            uint16_t in_range = 1U;
            if (iw > 6U) {
                uint16_t hi_any = bb_or_reduce_bits(bb, val_wires[i0] + 6U, iw - 6U);
                in_range = bb_not(bb, hi_any);
            }
            if (in_range == UINT16_C(0xFFFF)) { return false; }
            for (uint32_t b = 0U; b < ow; ++b) {
                uint16_t lv = bb_lut_mux_tree(bb, &prog->lut_pool[op->lut_off],
                                              op->lut_len, val_wires[i0], depth, 0U, b);
                dst[b] = bb_and(bb, in_range, lv);
                if (dst[b] == UINT16_C(0xFFFF)) { return false; }
            }
        } else {
            return false;
        }
    }
    return true;
}

static bool bb_compact_netlist(BloomBsBuilder *bb,
                               const uint16_t *out_roots,
                               uint32_t out0_w,
                               BloomBsNetlist *out) {
    if (out0_w == 0U || out0_w > 64U) {
        return false;
    }
    bb->used[0] = 1U;
    bb->used[1] = 1U;

    uint16_t stack[BLOOM_BS_TMP_WIRES];
    uint32_t sp = 0U;
    for (uint32_t b = 0U; b < out0_w; ++b) {
        uint16_t w = out_roots[b];
        if (!bb->used[w]) {
            bb->used[w] = 1U;
            stack[sp++] = w;
        }
    }
    while (sp > 0U) {
        uint16_t w = stack[--sp];
        int32_t gi = bb->wire_gate[w];
        if (gi >= 0) {
            const BloomBsGate *g = &bb->gates[gi];
            uint16_t ops3[3] = { g->a, g->b, g->c };
            uint32_t ar = (g->kind == BLOOM_BS_MUX) ? 3U : ((g->kind == BLOOM_BS_NOT) ? 1U : 2U);
            for (uint32_t k = 0U; k < ar; ++k) {
                uint16_t child = ops3[k];
                if (!bb->used[child]) {
                    bb->used[child] = 1U;
                    stack[sp++] = child;
                }
            }
        }
    }

    memset(out, 0, sizeof(*out));
    out->out0_width = out0_w;
    bb->remap[0] = 0;
    bb->remap[1] = 1;
    out->num_wires = 2U;

    for (uint32_t w = 2U; w < bb->num_wires; ++w) {
        if (!bb->used[w]) {
            continue;
        }
        if (bb->wire_in[w] >= 0) {
            if (out->num_wires >= BLOOM_BS_MAX_WIRES || out->num_inputs >= BLOOM_BS_MAX_INBITS) {
                return false;
            }
            uint16_t nw = (uint16_t)out->num_wires++;
            bb->remap[w] = (int32_t)nw;
            BloomBsInput bi = bb->tmp_inputs[bb->wire_in[w]];
            bi.wire_id = nw;
            out->inputs[out->num_inputs++] = bi;
        }
    }
    for (uint32_t gi = 0U; gi < bb->num_gates; ++gi) {
        const BloomBsGate *g = &bb->gates[gi];
        if (!bb->used[g->out_w]) {
            continue;
        }
        if (out->num_wires >= BLOOM_BS_MAX_WIRES || out->num_gates >= BLOOM_BS_MAX_GATES) {
            return false;
        }
        uint16_t nw = (uint16_t)out->num_wires++;
        bb->remap[g->out_w] = (int32_t)nw;
        BloomBsGate ng;
        ng.kind = g->kind;
        ng.out_w = nw;
        ng.a = (uint16_t)bb->remap[g->a];
        ng.b = (uint16_t)bb->remap[g->b];
        ng.c = (uint16_t)bb->remap[g->c];
        out->gates[out->num_gates++] = ng;
    }
    for (uint32_t b = 0U; b < out0_w; ++b) {
        out->out0_wires[b] = (uint16_t)bb->remap[out_roots[b]];
    }
    return true;
}

static bool bloom_fir1_bitblast(const flow_ir_prog_t *prog,
                                uint32_t vary_mask,
                                const uint64_t base_inputs[32],
                                bool dynamic_inputs,
                                BloomBsNetlist *out) {
    if (!prog || !out || prog->num_outputs == 0U || prog->num_inputs > 32U) {
        return false;
    }
    BloomBsBuilder *bb = (BloomBsBuilder *)calloc(1U, sizeof(BloomBsBuilder));
    if (!bb) {
        return false;
    }
    bool ok = bb_init_builder(bb, prog, vary_mask, base_inputs, dynamic_inputs) &&
              bb_blast_prog_ops(bb, prog, bb->val_wires) &&
              bb_compact_netlist(bb, bb->val_wires[prog->outputs[0]],
                                 prog->vals[prog->outputs[0]].width, out);
    free(bb);
    return ok;
}

static void bloom_bs_pack_netlist(const BloomBsNetlist *nl,
                                  uint32_t magic,
                                  uint32_t target_mode,
                                  uint32_t seed_lo,
                                  uint32_t seed_hi,
                                  uint32_t target_lo,
                                  uint32_t target_hi,
                                  BloomEngineArena *h_arena,
                                  BloomMeshArena *h_mesh) {
    memset(h_arena, 0, sizeof(*h_arena));
    memset(h_mesh, 0, sizeof(*h_mesh));
    uint32_t *au = (uint32_t *)(void *)h_arena;
    au[0] = magic;
    au[1] = nl->num_wires;
    au[2] = nl->num_inputs;
    au[3] = nl->out0_width;
    au[4] = nl->num_gates;
    au[7] = target_mode;
    au[8] = seed_lo;
    au[9] = seed_hi;
    au[10] = target_lo;
    au[11] = target_hi;

    for (uint32_t k = 0U; k < nl->num_inputs; ++k) {
        const BloomBsInput *bi = &nl->inputs[k];
        uint32_t spec = (bi->varying ? UINT32_C(0x80000000) : UINT32_C(0)) |
                        (((uint32_t)bi->wire_id & 0x1FFU) << 16U) |
                        (uint32_t)(bi->varying ? bi->src_bit : bi->const_bit);
        au[16U + k] = spec;
    }
    for (uint32_t b = 0U; b < nl->out0_width; ++b) {
        au[272U + b] = nl->out0_wires[b];
    }

    uint32_t *mu = (uint32_t *)(void *)h_mesh;
    for (uint32_t g = 0U; g < nl->num_gates; ++g) {
        const BloomBsGate *gt = &nl->gates[g];
        uint32_t *mo = mu + g * 4U;
        mo[0] = (uint32_t)gt->kind | (((uint32_t)gt->out_w & 0x1FFU) << 16U);
        mo[1] = (uint32_t)gt->a;
        mo[2] = (uint32_t)gt->b;
        mo[3] = (uint32_t)gt->c;
    }
}

static bool bloom_bs_run_netlist(BloomGpuCtx *ctx,
                                 const BloomBsNetlist *nl,
                                 const BloomFir1HuntConfig *cfg,
                                 BloomEngineArena *h_arena,
                                 BloomMeshArena *h_mesh,
                                 BloomFir1HuntResult *out) {
    bloom_bs_pack_netlist(nl, UINT32_C(0x46495242), cfg->target_mode,
                          cfg->seed_lo, cfg->seed_hi, cfg->target_lo, cfg->target_hi,
                          h_arena, h_mesh);
    if (!bloom_gpu_upload_mesh(ctx, h_mesh)) {
        return false;
    }

    uint32_t *au = (uint32_t *)(void *)h_arena;
    const uint32_t max_chunk_trials = UINT32_C(67108864);
    uint64_t t_start_ns = SDL_GetTicksNS();
    uint32_t remaining = cfg->num_trials;
    uint32_t base_off = 0U;
    bool ok = true;

    while (remaining > 0U && ok) {
        uint32_t chunk_trials = (remaining > max_chunk_trials) ? max_chunk_trials : remaining;
        uint32_t total_packs = (chunk_trials + 31U) >> 5U;
        uint32_t num_threads = (total_packs < 32768U) ? total_packs : 32768U;
        uint32_t num_wgs = (num_threads + 63U) / 64U;
        au[5] = total_packs;
        au[6] = chunk_trials;
        au[14] = num_threads;
        au[15] = base_off >> 5U;

        if (!bloom_fir_dispatch_reduce_chunk(ctx, ctx->pipe_fir1_bs, true, true, h_arena,
                                             num_threads, num_wgs, base_off, out)) {
            ok = false;
            break;
        }
        out->trials_evaluated += chunk_trials;
        base_off += chunk_trials;
        remaining -= chunk_trials;
    }

    uint64_t t_end_ns = SDL_GetTicksNS();
    out->elapsed_us = (t_end_ns >= t_start_ns) ? ((t_end_ns - t_start_ns) / 1000ULL) : 1ULL;
    if (out->elapsed_us == 0ULL) {
        out->elapsed_us = 1ULL;
    }
    ctx->rc_valid = false;
    ctx->mesh_epoch++;
    return ok;
}

bool bloom_gpu_hunt_fir1_bitslice(BloomGpuCtx *ctx,
                                  const uint8_t *fir_bytes,
                                  size_t fir_len,
                                  const BloomFir1HuntConfig *cfg,
                                  BloomFir1HuntResult *out) {
    if (!ctx || !ctx->device || !ctx->pipe_fir1_bs || !ctx->arena_gpu_buf || !ctx->mesh_gpu_buf ||
        !ctx->rc_gpu_buf || !ctx->upload_tbuf || !ctx->mesh_upload_tbuf || !ctx->rc_download_tbuf ||
        !fir_bytes || !cfg || !out || cfg->num_trials == 0U || cfg->sweep_mode != 2U) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->first_hit_trial = 0xFFFFFFFFU;

    flow_ir_prog_t *prog = (flow_ir_prog_t *)calloc(1U, sizeof(flow_ir_prog_t));
    BloomBsNetlist *nl = (BloomBsNetlist *)calloc(1U, sizeof(BloomBsNetlist));
    BloomEngineArena *h_arena = (BloomEngineArena *)calloc(1U, sizeof(BloomEngineArena));
    BloomMeshArena *h_mesh = (BloomMeshArena *)calloc(1U, sizeof(BloomMeshArena));
    if (!prog || !nl || !h_arena || !h_mesh) {
        free(h_mesh);
        free(h_arena);
        free(nl);
        free(prog);
        return false;
    }

    bool ok = flow_ir_load_bytes(prog, fir_bytes, fir_len) &&
              bloom_fir1_bitblast(prog, cfg->vary_input_mask, cfg->base_inputs, false, nl);
    if (ok) {
        out->num_inputs = prog->num_inputs;
        out->num_outputs = prog->num_outputs;
        ok = bloom_bs_run_netlist(ctx, nl, cfg, h_arena, h_mesh, out) &&
             bloom_fir_verify_witness(prog, cfg, out);
    }

    free(h_mesh);
    free(h_arena);
    free(nl);
    free(prog);
    return ok;
}

bool bloom_fir1_compile_wgsl(const uint8_t *fir_bytes,
                             size_t fir_len,
                             const char *wgsl_out_path,
                             uint32_t *out_gate_count,
                             uint32_t *out_in_bits,
                             uint32_t *out_out0_bits) {
    if (!fir_bytes || !wgsl_out_path || wgsl_out_path[0] == '\0') {
        return false;
    }
    flow_ir_prog_t *prog = (flow_ir_prog_t *)calloc(1U, sizeof(flow_ir_prog_t));
    BloomBsNetlist *nl = (BloomBsNetlist *)calloc(1U, sizeof(BloomBsNetlist));
    if (!prog || !nl) {
        free(nl);
        free(prog);
        return false;
    }

    uint64_t zero_base[32];
    memset(zero_base, 0, sizeof(zero_base));
    if (!flow_ir_load_bytes(prog, fir_bytes, fir_len) ||
        !bloom_fir1_bitblast(prog, UINT32_C(0xFFFFFFFF), zero_base, true, nl)) {
        free(nl);
        free(prog);
        return false;
    }

    FILE *fp = fopen(wgsl_out_path, "wb");
    if (!fp) {
        free(nl);
        free(prog);
        return false;
    }

    if (out_gate_count) *out_gate_count = nl->num_gates;
    if (out_in_bits) *out_in_bits = nl->num_inputs;
    if (out_out0_bits) *out_out0_bits = nl->out0_width;

    fprintf(fp,
            "// Auto-compiled Straight-Line AOT WGSL from FLOW FIR1 ('%s')\n"
            "// Zero workgroup globals, zero 64-bit integer tokens, 32-way SIMT bit-sliced\n\n"
            "@group(0) @binding(0) var<storage, read> arena : array<vec4<u32>, 1024>;\n"
            "@group(1) @binding(0) var<storage, read_write> rc_buf : array<vec4<f32>, 32768>;\n\n"
            "@compute @workgroup_size(64, 1, 1)\n"
            "fn bloom_fir1_aot(@builtin(global_invocation_id) gid : vec3<u32>) {\n"
            "    let tid = gid.x;\n"
            "    let h0 = arena[0];\n"
            "    let h1 = arena[1];\n"
            "    let h2 = arena[2];\n"
            "    let h3 = arena[3];\n"
            "    let total_packs  = h1.y;\n"
            "    let total_trials = h1.z;\n"
            "    let target_mode  = h1.w;\n"
            "    let seed_lo      = h2.x;\n"
            "    let seed_hi      = h2.y;\n"
            "    let target_lo    = h2.z;\n"
            "    let target_hi    = h2.w;\n"
            "    let vary_mask    = h3.x;\n"
            "    let num_threads  = min(h3.z, 32768u);\n"
            "    let pack_base    = h3.w;\n"
            "    if (h0.x != 0x46495241u || num_threads == 0u || tid >= num_threads) { return; }\n\n",
            prog->name);

    static const char lane_ch[4] = { 'x', 'y', 'z', 'w' };
    for (uint32_t k = 0U; k < nl->num_inputs; ++k) {
        const BloomBsInput *bi = &nl->inputs[k];
        uint32_t uid = 16U + (uint32_t)bi->in_idx * 2U + ((uint32_t)bi->in_bit >> 5U);
        uint32_t vec_i = uid >> 2U;
        char lc = lane_ch[uid & 3U];
        uint32_t b_mod = (uint32_t)bi->in_bit & 31U;
        uint32_t vsh = ((uint32_t)bi->in_idx < 31U) ? (uint32_t)bi->in_idx : 31U;
        fprintf(fp,
                "    let iv_%u = ((vary_mask >> %uu) & 1u) != 0u;\n"
                "    let bv_%u = select(0u, 0xFFFFFFFFu, ((arena[%u].%c >> %uu) & 1u) != 0u);\n",
                k, vsh, k, vec_i, lc, b_mod);
        if (bi->src_bit < 32U) {
            fprintf(fp,
                    "    let sm_%u = select(0u, 0xFFFFFFFFu, ((seed_lo >> %uu) & 1u) != 0u);\n",
                    k, (uint32_t)bi->src_bit);
        } else if (bi->src_bit < 64U) {
            fprintf(fp,
                    "    let sm_%u = select(0u, 0xFFFFFFFFu, ((seed_hi >> %uu) & 1u) != 0u);\n",
                    k, (uint32_t)(bi->src_bit - 32U));
        }
    }

    uint32_t mask_lo = (nl->out0_width >= 32U) ? UINT32_C(0xFFFFFFFF) : ((UINT32_C(1) << nl->out0_width) - 1U);
    uint32_t mask_hi = (nl->out0_width <= 32U) ? 0U :
                       ((nl->out0_width >= 64U) ? UINT32_C(0xFFFFFFFF) : ((UINT32_C(1) << (nl->out0_width - 32U)) - 1U));
    fprintf(fp,
            "    let high_mismatch = ((target_lo & 0x%08Xu) != 0u) || ((target_hi & 0x%08Xu) != 0u);\n"
            "    var best_local : u32 = 0xFFFFFFFFu;\n"
            "    var hit_acc    : u32 = 0u;\n",
            ~mask_lo, ~mask_hi);

    for (uint32_t b = 0U; b < nl->out0_width; ++b) {
        fprintf(fp, "    var xp_%u : u32 = 0u;\n", b);
        if (b < 32U) {
            fprintf(fp, "    let tb_%u = ((target_lo >> %uu) & 1u) != 0u;\n", b, b);
        } else {
            fprintf(fp, "    let tb_%u = ((target_hi >> %uu) & 1u) != 0u;\n", b, b - 32U);
        }
    }

    fprintf(fp,
            "\n    var local_pack : u32 = tid;\n"
            "    while (local_pack < total_packs) {\n"
            "        let gpack = pack_base + local_pack;\n"
            "        let rem = total_trials - local_pack * 32u;\n"
            "        let valid_mask = select((1u << min(rem, 31u)) - 1u, 0xFFFFFFFFu, rem >= 32u);\n"
            "        let w0 : u32 = 0u;\n"
            "        let w1 : u32 = 0xFFFFFFFFu;\n");

    static const uint32_t base_pat[5] = {
        UINT32_C(0xAAAAAAAA), UINT32_C(0xCCCCCCCC), UINT32_C(0xF0F0F0F0),
        UINT32_C(0xFF00FF00), UINT32_C(0xFFFF0000)
    };
    for (uint32_t k = 0U; k < nl->num_inputs; ++k) {
        const BloomBsInput *bi = &nl->inputs[k];
        if (bi->src_bit < 5U) {
            fprintf(fp, "        let p_%u = 0x%08Xu ^ sm_%u;\n", k, base_pat[bi->src_bit], k);
        } else if (bi->src_bit < 37U) {
            fprintf(fp, "        let p_%u = select(0u, 0xFFFFFFFFu, ((gpack >> %uu) & 1u) != 0u) ^ sm_%u;\n",
                    k, (uint32_t)(bi->src_bit - 5U), k);
        } else if (bi->src_bit < 64U) {
            fprintf(fp, "        let p_%u = sm_%u;\n", k, k);
        } else {
            fprintf(fp, "        let p_%u = 0u;\n", k);
        }
        fprintf(fp, "        let w%u : u32 = select(bv_%u, p_%u, iv_%u);\n",
                (unsigned)bi->wire_id, k, k, k);
    }

    for (uint32_t g = 0U; g < nl->num_gates; ++g) {
        const BloomBsGate *gt = &nl->gates[g];
        if (gt->kind == BLOOM_BS_AND) {
            fprintf(fp, "        let w%u : u32 = w%u & w%u;\n", gt->out_w, gt->a, gt->b);
        } else if (gt->kind == BLOOM_BS_OR) {
            fprintf(fp, "        let w%u : u32 = w%u | w%u;\n", gt->out_w, gt->a, gt->b);
        } else if (gt->kind == BLOOM_BS_XOR) {
            fprintf(fp, "        let w%u : u32 = w%u ^ w%u;\n", gt->out_w, gt->a, gt->b);
        } else if (gt->kind == BLOOM_BS_NOT) {
            fprintf(fp, "        let w%u : u32 = ~w%u;\n", gt->out_w, gt->a);
        } else if (gt->kind == BLOOM_BS_MUX) {
            fprintf(fp, "        let w%u : u32 = (w%u & w%u) | ((~w%u) & w%u);\n",
                    gt->out_w, gt->a, gt->c, gt->a, gt->b);
        }
    }

    fprintf(fp, "        var eq_mask : u32 = select(0xFFFFFFFFu, 0u, high_mismatch);\n");
    for (uint32_t b = 0U; b < nl->out0_width; ++b) {
        uint16_t ow = nl->out0_wires[b];
        fprintf(fp,
                "        xp_%u = xp_%u ^ (w%u & valid_mask);\n"
                "        eq_mask = eq_mask & select(~w%u, w%u, tb_%u);\n",
                b, b, ow, ow, ow, b);
    }

    fprintf(fp,
            "        var match_mask : u32 = valid_mask;\n"
            "        if (target_mode == 0u) { match_mask = eq_mask & valid_mask; }\n"
            "        else if (target_mode == 1u) { match_mask = (~eq_mask) & valid_mask; }\n"
            "        if (match_mask != 0u) {\n"
            "            hit_acc = hit_acc + countOneBits(match_mask);\n"
            "            let cand = local_pack * 32u + countTrailingZeros(match_mask);\n"
            "            if (cand < best_local) { best_local = cand; }\n"
            "        }\n"
            "        if (total_packs - local_pack <= num_threads) { break; }\n"
            "        local_pack = local_pack + num_threads;\n"
            "    }\n\n"
            "    var xor_lo : u32 = 0u;\n"
            "    var xor_hi : u32 = 0u;\n");

    for (uint32_t b = 0U; b < nl->out0_width; ++b) {
        if (b < 32U) {
            fprintf(fp, "    if ((countOneBits(xp_%u) & 1u) != 0u) { xor_lo = xor_lo | (1u << %uu); }\n", b, b);
        } else {
            fprintf(fp, "    if ((countOneBits(xp_%u) & 1u) != 0u) { xor_hi = xor_hi | (1u << %uu); }\n", b, b - 32U);
        }
    }

    fprintf(fp,
            "    rc_buf[tid] = vec4<f32>(bitcast<f32>(best_local), bitcast<f32>(hit_acc), bitcast<f32>(xor_lo), bitcast<f32>(xor_hi));\n"
            "}\n");

    fclose(fp);
    free(nl);
    free(prog);
    return true;
}

static char *bloom_patch_aot_metal(const char *src, size_t len, size_t *out_len) {
    size_t cap = len + 256U;
    char *buf = (char *)calloc(1U, cap);
    if (!buf) {
        return NULL;
    }
    static const char *const k_from[3] = {
        "arena [[user(fake0)]]",
        "rc_buf [[user(fake0)]]",
        "bloom_fir1_aot_("
    };
    static const char *const k_to[3] = {
        "arena [[buffer(0)]]",
        "rc_buf [[buffer(1)]]",
        "bloom_fir1_aot("
    };
    size_t i = 0U;
    size_t o = 0U;
    while (i < len) {
        bool matched = false;
        for (size_t r = 0U; r < 3U; ++r) {
            size_t fl = strlen(k_from[r]);
            size_t tl = strlen(k_to[r]);
            if (i + fl <= len && memcmp(src + i, k_from[r], fl) == 0) {
                if (o + tl + 1U > cap) {
                    cap *= 2U;
                    char *nb = (char *)realloc(buf, cap);
                    if (!nb) {
                        free(buf);
                        return NULL;
                    }
                    buf = nb;
                }
                memcpy(buf + o, k_to[r], tl);
                o += tl;
                i += fl;
                matched = true;
                break;
            }
        }
        if (!matched) {
            if (o + 2U > cap) {
                cap *= 2U;
                char *nb = (char *)realloc(buf, cap);
                if (!nb) {
                    free(buf);
                    return NULL;
                }
                buf = nb;
            }
            buf[o++] = src[i++];
        }
    }
    buf[o] = '\0';
    *out_len = o;
    return buf;
}

static bool bloom_vary_mask_is_prefix(uint32_t mask, uint32_t num_inputs) {
    uint32_t used_mask = (num_inputs >= 32U) ? UINT32_C(0xFFFFFFFF) : ((UINT32_C(1) << num_inputs) - 1U);
    uint32_t m = mask & used_mask;
    return (m & (m + 1U)) == 0U;
}

bool bloom_gpu_hunt_fir1_aot(BloomGpuCtx *ctx,
                             const uint8_t *fir_bytes,
                             size_t fir_len,
                             const char *aot_shader_path,
                             const BloomFir1HuntConfig *cfg,
                             BloomFir1HuntResult *out) {
    if (!ctx || !ctx->device || !ctx->arena_gpu_buf || !ctx->rc_gpu_buf ||
        !ctx->upload_tbuf || !ctx->rc_download_tbuf ||
        !fir_bytes || !aot_shader_path || !cfg || !out || cfg->num_trials == 0U ||
        cfg->sweep_mode != 2U) {
        return false;
    }

    flow_ir_prog_t *prog = (flow_ir_prog_t *)calloc(1U, sizeof(flow_ir_prog_t));
    if (!prog || !flow_ir_load_bytes(prog, fir_bytes, fir_len)) {
        free(prog);
        return false;
    }

    /* Non-prefix vary masks compact src_bit offsets across holes; handle via bitslice netlist */
    if (!bloom_vary_mask_is_prefix(cfg->vary_input_mask, prog->num_inputs)) {
        free(prog);
        return bloom_gpu_hunt_fir1_bitslice(ctx, fir_bytes, fir_len, cfg, out);
    }

    memset(out, 0, sizeof(*out));
    out->first_hit_trial = 0xFFFFFFFFU;
    out->num_inputs = prog->num_inputs;
    out->num_outputs = prog->num_outputs;

    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(ctx->device);
    SDL_GPUShaderFormat chosen_fmt = SDL_GPU_SHADERFORMAT_INVALID;
    const char *ext = NULL;
    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0U) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_MSL;
        ext = ".metal";
    } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0U) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_SPIRV;
        ext = ".spv";
    } else {
        free(prog);
        return false;
    }

    char resolved[544];
    resolved[0] = '\0';
    size_t code_size = 0U;
    uint8_t *code = bloom_load_compiled_shader(aot_shader_path, ext, resolved, sizeof(resolved), &code_size);
    if (!code || code_size == 0U) {
        SDL_free(code);
        free(prog);
        return false;
    }

    char *patched = NULL;
    size_t pipe_sz = code_size;
    const void *pipe_code = code;
    if (chosen_fmt == SDL_GPU_SHADERFORMAT_MSL) {
        patched = bloom_patch_aot_metal((const char *)code, code_size, &pipe_sz);
        if (!patched) {
            SDL_free(code);
            free(prog);
            return false;
        }
        pipe_code = patched;
    }

    SDL_GPUComputePipelineCreateInfo pi;
    memset(&pi, 0, sizeof(pi));
    pi.code_size = pipe_sz;
    pi.code = (const Uint8 *)pipe_code;
    pi.entrypoint = "bloom_fir1_aot";
    pi.format = chosen_fmt;
    pi.num_readonly_storage_buffers = 1U;
    pi.num_readwrite_storage_textures = 0U;
    pi.num_readwrite_storage_buffers = 1U;
    pi.threadcount_x = 64U;
    pi.threadcount_y = 1U;
    pi.threadcount_z = 1U;
    SDL_GPUComputePipeline *pipe = SDL_CreateGPUComputePipeline(ctx->device, &pi);
    free(patched);
    SDL_free(code);
    if (!pipe) {
        free(prog);
        return false;
    }

    BloomEngineArena *h_arena = (BloomEngineArena *)calloc(1U, sizeof(BloomEngineArena));
    if (!h_arena) {
        SDL_ReleaseGPUComputePipeline(ctx->device, pipe);
        free(prog);
        return false;
    }

    uint32_t *au = (uint32_t *)(void *)h_arena;
    au[0] = UINT32_C(0x46495241); /* "FIRA" */
    au[7] = cfg->target_mode;
    au[8] = cfg->seed_lo;
    au[9] = cfg->seed_hi;
    au[10] = cfg->target_lo;
    au[11] = cfg->target_hi;
    au[12] = cfg->vary_input_mask;
    for (uint32_t i = 0U; i < prog->num_inputs; ++i) {
        au[16U + i * 2U + 0U] = (uint32_t)(cfg->base_inputs[i] & UINT64_C(0xFFFFFFFF));
        au[16U + i * 2U + 1U] = (uint32_t)(cfg->base_inputs[i] >> 32U);
    }

    const uint32_t max_chunk_trials = UINT32_C(268435456); /* 256M trials per dispatch */
    uint64_t t_start_ns = SDL_GetTicksNS();
    uint32_t remaining = cfg->num_trials;
    uint32_t base_off = 0U;
    bool ok = true;

    while (remaining > 0U && ok) {
        uint32_t chunk_trials = (remaining > max_chunk_trials) ? max_chunk_trials : remaining;
        uint32_t total_packs = (chunk_trials + 31U) >> 5U;
        uint32_t num_threads = (total_packs < 32768U) ? total_packs : 32768U;
        uint32_t num_wgs = (num_threads + 63U) / 64U;
        au[5] = total_packs;
        au[6] = chunk_trials;
        au[14] = num_threads;
        au[15] = base_off >> 5U;

        if (!bloom_fir_dispatch_reduce_chunk(ctx, pipe, false, false, h_arena,
                                             num_threads, num_wgs, base_off, out)) {
            ok = false;
            break;
        }
        out->trials_evaluated += chunk_trials;
        base_off += chunk_trials;
        remaining -= chunk_trials;
    }

    uint64_t t_end_ns = SDL_GetTicksNS();
    out->elapsed_us = (t_end_ns >= t_start_ns) ? ((t_end_ns - t_start_ns) / 1000ULL) : 1ULL;
    if (out->elapsed_us == 0ULL) {
        out->elapsed_us = 1ULL;
    }
    ctx->rc_valid = false;
    ctx->mesh_epoch++;

    SDL_ReleaseGPUComputePipeline(ctx->device, pipe);
    if (ok && !bloom_fir_verify_witness(prog, cfg, out)) {
        ok = false;
    }

    free(h_arena);
    free(prog);
    return ok;
}

/* ----------------------------------------------------------------------------
 * GPU Parallel SEARU Multi-Universe Solver (3-SAT & Graph Coloring)
 * ---------------------------------------------------------------------------- */

static bool bloom_gpu_searu_dispatch_rounds(BloomGpuCtx *ctx,
                                            uint32_t mode,
                                            uint32_t n_items,
                                            uint32_t n_cons,
                                            uint32_t n_colors,
                                            const uint64_t init_canvas[4],
                                            const uint64_t kan_pin[4],
                                            const BloomMeshArena *h_mesh,
                                            const BloomSearuGpuConfig *cfg,
                                            BloomSearuGpuResult *out) {
    BloomEngineArena *h_arena = (BloomEngineArena *)calloc(1U, sizeof(BloomEngineArena));
    if (!h_arena) {
        return false;
    }
    if (!bloom_gpu_upload_mesh(ctx, h_mesh)) {
        free(h_arena);
        return false;
    }

    uint32_t n_unis = (cfg && cfg->num_universes > 0U) ? cfg->num_universes : 4096U;
    if (n_unis > 8192U) n_unis = 8192U;
    uint32_t n_iters = (cfg && cfg->iters_per_round > 0U) ? cfg->iters_per_round : 256U;
    if (n_iters > 4096U) n_iters = 4096U;
    uint32_t max_rounds = (cfg && cfg->max_rounds > 0U) ? cfg->max_rounds : 4U;
    if (max_rounds > 32U) max_rounds = 32U;
    uint64_t seed = cfg ? cfg->seed : UINT64_C(0xB100BEEF12345678);

    uint32_t *au = (uint32_t *)(void *)h_arena;
    au[0] = UINT32_C(0x53454152); /* "SEAR" */
    au[1] = mode;
    au[2] = n_items;
    au[3] = n_cons;
    au[4] = n_colors;
    au[5] = n_unis;
    au[6] = n_iters;
    au[8] = (uint32_t)(seed & UINT64_C(0xFFFFFFFF));
    au[9] = (uint32_t)(seed >> 32U);
    au[11] = 7U;
    au[12] = 8192U;
    au[13] = 65200U;
    au[14] = 64U;
    au[15] = 2048U;

    for (uint32_t w = 0U; w < 4U; ++w) {
        uint64_t cw = init_canvas ? init_canvas[w] : UINT64_C(0);
        uint64_t pw = kan_pin ? kan_pin[w] : UINT64_C(0);
        au[16U + w * 2U + 0U] = (uint32_t)(cw & UINT64_C(0xFFFFFFFF));
        au[16U + w * 2U + 1U] = (uint32_t)(cw >> 32U);
        au[24U + w * 2U + 0U] = (uint32_t)(pw & UINT64_C(0xFFFFFFFF));
        au[24U + w * 2U + 1U] = (uint32_t)(pw >> 32U);
    }

    uint64_t elite_canvases[16][4];
    uint64_t elite_energies[16];
    uint32_t num_elites = 0U;
    for (uint32_t e = 0U; e < 16U; ++e) {
        memset(elite_canvases[e], 0, sizeof(elite_canvases[e]));
        elite_energies[e] = UINT64_MAX;
    }

    out->best_energy = UINT64_MAX;
    uint32_t num_wgs = (n_unis + 63U) / 64U;
    uint64_t t_start_ns = SDL_GetTicksNS();
    bool ok = true;

    for (uint32_t rnd = 0U; rnd < max_rounds && ok; ++rnd) {
        au[7] = rnd;
        au[10] = num_elites;
        for (uint32_t e = 0U; e < num_elites; ++e) {
            for (uint32_t w = 0U; w < 4U; ++w) {
                au[32U + e * 8U + w * 2U + 0U] = (uint32_t)(elite_canvases[e][w] & UINT64_C(0xFFFFFFFF));
                au[32U + e * 8U + w * 2U + 1U] = (uint32_t)(elite_canvases[e][w] >> 32U);
            }
        }

        void *mapped_up = SDL_MapGPUTransferBuffer(ctx->device, ctx->upload_tbuf, false);
        if (!mapped_up) { ok = false; break; }
        memcpy(mapped_up, h_arena, BLOOM_ARENA_BYTES);
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upload_tbuf);

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
        if (!cmd) { ok = false; break; }

        SDL_GPUCopyPass *up_pass = SDL_BeginGPUCopyPass(cmd);
        if (!up_pass) { SDL_CancelGPUCommandBuffer(cmd); ok = false; break; }
        SDL_GPUTransferBufferLocation up_src;
        memset(&up_src, 0, sizeof(up_src));
        up_src.transfer_buffer = ctx->upload_tbuf;
        SDL_GPUBufferRegion up_dst;
        memset(&up_dst, 0, sizeof(up_dst));
        up_dst.buffer = ctx->arena_gpu_buf;
        up_dst.size = (Uint32)BLOOM_ARENA_BYTES;
        SDL_UploadToGPUBuffer(up_pass, &up_src, &up_dst, false);
        SDL_EndGPUCopyPass(up_pass);

        SDL_GPUStorageTextureReadWriteBinding rw_tex;
        memset(&rw_tex, 0, sizeof(rw_tex));
        rw_tex.texture = ctx->frame_tex;
        SDL_GPUStorageBufferReadWriteBinding rw_buf;
        memset(&rw_buf, 0, sizeof(rw_buf));
        rw_buf.buffer = ctx->rc_gpu_buf;
        SDL_GPUComputePass *cpass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1U, &rw_buf, 1U);
        if (!cpass) { SDL_CancelGPUCommandBuffer(cmd); ok = false; break; }
        SDL_GPUBuffer *ro_bufs[2] = { ctx->arena_gpu_buf, ctx->mesh_gpu_buf };
        SDL_BindGPUComputePipeline(cpass, ctx->pipe_searu);
        SDL_BindGPUComputeStorageBuffers(cpass, 0U, ro_bufs, 2U);
        SDL_DispatchGPUCompute(cpass, num_wgs, 1U, 1U);
        SDL_EndGPUComputePass(cpass);

        SDL_GPUCopyPass *dl_pass = SDL_BeginGPUCopyPass(cmd);
        if (!dl_pass) { SDL_CancelGPUCommandBuffer(cmd); ok = false; break; }
        SDL_GPUBufferRegion dl_src;
        memset(&dl_src, 0, sizeof(dl_src));
        dl_src.buffer = ctx->rc_gpu_buf;
        dl_src.size = (Uint32)(n_unis * 48U);
        SDL_GPUTransferBufferLocation dl_dst;
        memset(&dl_dst, 0, sizeof(dl_dst));
        dl_dst.transfer_buffer = ctx->rc_download_tbuf;
        SDL_DownloadFromGPUBuffer(dl_pass, &dl_src, &dl_dst);
        SDL_EndGPUCopyPass(dl_pass);

        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!fence || !SDL_WaitForGPUFences(ctx->device, true, &fence, 1U)) {
            if (fence) SDL_ReleaseGPUFence(ctx->device, fence);
            ok = false;
            break;
        }
        SDL_ReleaseGPUFence(ctx->device, fence);

        const uint32_t *res = (const uint32_t *)SDL_MapGPUTransferBuffer(
            ctx->device, ctx->rc_download_tbuf, false);
        if (!res) { ok = false; break; }

        out->rounds_run = rnd + 1U;
        out->total_universes_eval += n_unis;

        for (uint32_t u = 0U; u < n_unis; ++u) {
            const uint32_t *ur = res + (size_t)u * 12U;
            uint64_t ue = (uint64_t)ur[0];
            uint32_t iters_u = ur[1];
            out->total_steps_eval += (uint64_t)iters_u;

            uint64_t cand[4];
            for (uint32_t w = 0U; w < 4U; ++w) {
                cand[w] = ((uint64_t)ur[4U + w * 2U + 1U] << 32U) | (uint64_t)ur[4U + w * 2U + 0U];
            }
            if (ue < out->best_energy) {
                out->best_energy = ue;
                out->winning_universe = u;
                memcpy(out->best_canvas, cand, sizeof(cand));
            }
            if (num_elites < 16U || ue < elite_energies[num_elites - 1U]) {
                bool dup = false;
                for (uint32_t e = 0U; e < num_elites; ++e) {
                    if (memcmp(elite_canvases[e], cand, sizeof(cand)) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    uint32_t pos = (num_elites < 16U) ? num_elites++ : 15U;
                    while (pos > 0U && ue < elite_energies[pos - 1U]) {
                        elite_energies[pos] = elite_energies[pos - 1U];
                        memcpy(elite_canvases[pos], elite_canvases[pos - 1U], sizeof(cand));
                        pos--;
                    }
                    elite_energies[pos] = ue;
                    memcpy(elite_canvases[pos], cand, sizeof(cand));
                }
            }
        }
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->rc_download_tbuf);

        if (out->best_energy == 0ULL) {
            out->solved = true;
            break;
        }
    }

    uint64_t t_end_ns = SDL_GetTicksNS();
    out->elapsed_us = (t_end_ns >= t_start_ns) ? ((t_end_ns - t_start_ns) / 1000ULL) : 1ULL;
    if (out->elapsed_us == 0ULL) {
        out->elapsed_us = 1ULL;
    }
    ctx->rc_valid = false;
    ctx->mesh_epoch++;

    free(h_arena);
    return ok;
}

bool bloom_gpu_searu_solve_3sat(BloomGpuCtx *ctx,
                                const uint32_t *clauses,
                                uint32_t num_clauses,
                                uint32_t num_vars,
                                const uint64_t init_canvas[4],
                                const uint64_t kan_pin[4],
                                const BloomSearuGpuConfig *cfg,
                                BloomSearuGpuResult *out) {
    if (!ctx || !ctx->device || !ctx->pipe_searu || !clauses || num_clauses == 0U ||
        num_clauses > 8192U || num_vars == 0U || num_vars > SEARU_CANVAS_BITS || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    BloomMeshArena *h_mesh = (BloomMeshArena *)calloc(1U, sizeof(BloomMeshArena));
    if (!h_mesh) {
        return false;
    }
    memcpy(h_mesh, clauses, (size_t)num_clauses * sizeof(uint32_t));

    bool ok = bloom_gpu_searu_dispatch_rounds(ctx, 0U, num_vars, num_clauses, 2U,
                                              init_canvas, kan_pin, h_mesh, cfg, out);
    free(h_mesh);
    if (!ok) {
        return false;
    }

    uint64_t frust[4];
    uint64_t cpu_e = searu_eval_3sat(out->best_canvas, clauses, num_clauses, frust);
    bool pin_ok = true;
    if (kan_pin && init_canvas) {
        for (uint32_t w = 0U; w < 4U; ++w) {
            if (((out->best_canvas[w] ^ init_canvas[w]) & kan_pin[w]) != 0ULL) {
                pin_ok = false;
            }
        }
    }
    out->cpu_verified = (cpu_e == out->best_energy) && pin_ok;
    out->solved = (cpu_e == 0ULL) && pin_ok;
    return out->cpu_verified;
}

bool bloom_gpu_searu_solve_graph_color(BloomGpuCtx *ctx,
                                       const uint16_t *edges,
                                       uint32_t num_edges,
                                       uint32_t num_nodes,
                                       uint32_t num_colors,
                                       const uint64_t init_canvas[4],
                                       uint64_t kan_node_pin,
                                       const BloomSearuGpuConfig *cfg,
                                       BloomSearuGpuResult *out) {
    if (!ctx || !ctx->device || !ctx->pipe_searu || !edges || num_edges > 16384U ||
        num_nodes == 0U || num_nodes > SEARU_GRAPH_MAX_NODES ||
        num_colors < 2U || num_colors > 16U || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    BloomMeshArena *h_mesh = (BloomMeshArena *)calloc(1U, sizeof(BloomMeshArena));
    if (!h_mesh) {
        return false;
    }
    if (num_edges > 0U) {
        memcpy(h_mesh, edges, (size_t)num_edges * sizeof(uint16_t));
    }
    uint64_t pin4[4] = { kan_node_pin, 0ULL, 0ULL, 0ULL };
    bool ok = bloom_gpu_searu_dispatch_rounds(ctx, 1U, num_nodes, num_edges, num_colors,
                                              init_canvas, pin4, h_mesh, cfg, out);
    free(h_mesh);
    if (!ok) {
        return false;
    }

    uint64_t conflict_nodes = 0ULL;
    uint64_t cpu_e = searu_eval_graph_color(out->best_canvas, edges, num_edges,
                                            num_nodes, num_colors, &conflict_nodes);
    bool pin_ok = true;
    if (init_canvas && kan_node_pin != 0ULL) {
        for (uint32_t u = 0U; u < num_nodes; ++u) {
            if (((kan_node_pin >> u) & 1ULL) != 0ULL) {
                if (searu_color_get(out->best_canvas, u) != searu_color_get(init_canvas, u)) {
                    pin_ok = false;
                }
            }
        }
    }
    out->cpu_verified = (cpu_e == out->best_energy) && pin_ok;
    out->solved = (cpu_e == 0ULL) && pin_ok;
    return out->cpu_verified;
}

/* ----------------------------------------------------------------------------
 * GPU Warp-Scale RTL Stuck-at-0/1 Fault Simulator & Equivalence Miter
 * ---------------------------------------------------------------------------- */

static uint64_t bloom_bs_eval_cpu_fault(const BloomBsNetlist *nl,
                                        uint32_t trial_id,
                                        uint64_t seed,
                                        int32_t fault_wire,
                                        uint32_t stuck_bit) {
    uint8_t wires[BLOOM_BS_MAX_WIRES];
    wires[0] = 0U;
    wires[1] = 1U;
    uint64_t xval = (uint64_t)trial_id ^ seed;

    for (uint32_t k = 0U; k < nl->num_inputs; ++k) {
        const BloomBsInput *bi = &nl->inputs[k];
        uint8_t v = bi->varying
                        ? (uint8_t)((bi->src_bit < 64U) ? ((xval >> bi->src_bit) & 1ULL) : 0ULL)
                        : bi->const_bit;
        if (fault_wire >= 2 && bi->wire_id == (uint16_t)fault_wire) {
            v = (uint8_t)(stuck_bit & 1U);
        }
        wires[bi->wire_id] = v;
    }

    for (uint32_t g = 0U; g < nl->num_gates; ++g) {
        const BloomBsGate *gt = &nl->gates[g];
        uint8_t a = wires[gt->a];
        uint8_t b = wires[gt->b];
        uint8_t c = wires[gt->c];
        uint8_t r = 0U;
        if (gt->kind == BLOOM_BS_AND) r = (uint8_t)(a & b);
        else if (gt->kind == BLOOM_BS_OR) r = (uint8_t)(a | b);
        else if (gt->kind == BLOOM_BS_XOR) r = (uint8_t)(a ^ b);
        else if (gt->kind == BLOOM_BS_NOT) r = (uint8_t)(a ^ 1U);
        else if (gt->kind == BLOOM_BS_MUX) r = (uint8_t)((a != 0U) ? c : b);
        if (fault_wire >= 2 && gt->out_w == (uint16_t)fault_wire) {
            r = (uint8_t)(stuck_bit & 1U);
        }
        wires[gt->out_w] = r;
    }

    uint64_t res = 0ULL;
    for (uint32_t b = 0U; b < nl->out0_width; ++b) {
        if ((wires[nl->out0_wires[b]] & 1U) != 0U) {
            res |= UINT64_C(1) << b;
        }
    }
    return res;
}

bool bloom_gpu_faultsim_fir1(BloomGpuCtx *ctx,
                             const uint8_t *fir_bytes,
                             size_t fir_len,
                             uint32_t num_trials,
                             uint64_t seed,
                             BloomFir1FaultSimResult *out) {
    if (!ctx || !ctx->device || !ctx->pipe_faultsim || !ctx->arena_gpu_buf || !ctx->mesh_gpu_buf ||
        !ctx->rc_gpu_buf || !ctx->upload_tbuf || !ctx->mesh_upload_tbuf || !ctx->rc_download_tbuf ||
        !fir_bytes || !out || num_trials == 0U) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->sample_atpg_vec = 0xFFFFFFFFU;

    flow_ir_prog_t *prog = (flow_ir_prog_t *)calloc(1U, sizeof(flow_ir_prog_t));
    BloomBsNetlist *nl = (BloomBsNetlist *)calloc(1U, sizeof(BloomBsNetlist));
    BloomEngineArena *h_arena = (BloomEngineArena *)calloc(1U, sizeof(BloomEngineArena));
    BloomMeshArena *h_mesh = (BloomMeshArena *)calloc(1U, sizeof(BloomMeshArena));
    if (!prog || !nl || !h_arena || !h_mesh) {
        free(h_mesh);
        free(h_arena);
        free(nl);
        free(prog);
        return false;
    }

    uint64_t zero_base[32];
    memset(zero_base, 0, sizeof(zero_base));
    if (!flow_ir_load_bytes(prog, fir_bytes, fir_len) ||
        !bloom_fir1_bitblast(prog, UINT32_C(0xFFFFFFFF), zero_base, false, nl)) {
        free(h_mesh);
        free(h_arena);
        free(nl);
        free(prog);
        return false;
    }

    out->total_wires = nl->num_wires;
    out->total_gates = nl->num_gates;
    out->trials_per_fault = num_trials;
    if (nl->num_wires <= 2U) {
        out->cpu_verified = true;
        free(h_mesh);
        free(h_arena);
        free(nl);
        free(prog);
        return true;
    }

    uint32_t num_faults = (nl->num_wires - 2U) * 2U;
    out->total_faults = num_faults;

    uint32_t seed_lo = (uint32_t)(seed & UINT64_C(0xFFFFFFFF));
    uint32_t seed_hi = (uint32_t)(seed >> 32U);
    bloom_bs_pack_netlist(nl, UINT32_C(0x46495246), 0U, seed_lo, seed_hi, 0U, 0U, h_arena, h_mesh);
    uint32_t *au = (uint32_t *)(void *)h_arena;
    au[7] = num_faults;

    if (!bloom_gpu_upload_mesh(ctx, h_mesh)) {
        free(h_mesh);
        free(h_arena);
        free(nl);
        free(prog);
        return false;
    }

    uint32_t *first_vecs = (uint32_t *)malloc((size_t)num_faults * sizeof(uint32_t));
    if (!first_vecs) {
        free(h_mesh);
        free(h_arena);
        free(nl);
        free(prog);
        return false;
    }
    for (uint32_t f = 0U; f < num_faults; ++f) {
        first_vecs[f] = 0xFFFFFFFFU;
    }

    const uint32_t max_chunk_trials = UINT32_C(65536);
    uint32_t num_wgs = (num_faults + 63U) / 64U;
    uint64_t t_start_ns = SDL_GetTicksNS();
    uint32_t remaining = num_trials;
    uint32_t base_off = 0U;
    bool ok = true;

    while (remaining > 0U && ok) {
        uint32_t chunk_trials = (remaining > max_chunk_trials) ? max_chunk_trials : remaining;
        uint32_t total_packs = (chunk_trials + 31U) >> 5U;
        au[5] = total_packs;
        au[6] = chunk_trials;
        au[15] = base_off >> 5U;

        void *mapped_up = SDL_MapGPUTransferBuffer(ctx->device, ctx->upload_tbuf, false);
        if (!mapped_up) { ok = false; break; }
        memcpy(mapped_up, h_arena, BLOOM_ARENA_BYTES);
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upload_tbuf);

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
        if (!cmd) { ok = false; break; }

        SDL_GPUCopyPass *up_pass = SDL_BeginGPUCopyPass(cmd);
        if (!up_pass) { SDL_CancelGPUCommandBuffer(cmd); ok = false; break; }
        SDL_GPUTransferBufferLocation up_src;
        memset(&up_src, 0, sizeof(up_src));
        up_src.transfer_buffer = ctx->upload_tbuf;
        SDL_GPUBufferRegion up_dst;
        memset(&up_dst, 0, sizeof(up_dst));
        up_dst.buffer = ctx->arena_gpu_buf;
        up_dst.size = (Uint32)BLOOM_ARENA_BYTES;
        SDL_UploadToGPUBuffer(up_pass, &up_src, &up_dst, false);
        SDL_EndGPUCopyPass(up_pass);

        SDL_GPUStorageTextureReadWriteBinding rw_tex;
        memset(&rw_tex, 0, sizeof(rw_tex));
        rw_tex.texture = ctx->frame_tex;
        SDL_GPUStorageBufferReadWriteBinding rw_buf;
        memset(&rw_buf, 0, sizeof(rw_buf));
        rw_buf.buffer = ctx->rc_gpu_buf;
        SDL_GPUComputePass *cpass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1U, &rw_buf, 1U);
        if (!cpass) { SDL_CancelGPUCommandBuffer(cmd); ok = false; break; }
        SDL_GPUBuffer *ro_bufs[2] = { ctx->arena_gpu_buf, ctx->mesh_gpu_buf };
        SDL_BindGPUComputePipeline(cpass, ctx->pipe_faultsim);
        SDL_BindGPUComputeStorageBuffers(cpass, 0U, ro_bufs, 2U);
        SDL_DispatchGPUCompute(cpass, num_wgs, 1U, 1U);
        SDL_EndGPUComputePass(cpass);

        SDL_GPUCopyPass *dl_pass = SDL_BeginGPUCopyPass(cmd);
        if (!dl_pass) { SDL_CancelGPUCommandBuffer(cmd); ok = false; break; }
        SDL_GPUBufferRegion dl_src;
        memset(&dl_src, 0, sizeof(dl_src));
        dl_src.buffer = ctx->rc_gpu_buf;
        dl_src.size = (Uint32)(num_faults * 16U);
        SDL_GPUTransferBufferLocation dl_dst;
        memset(&dl_dst, 0, sizeof(dl_dst));
        dl_dst.transfer_buffer = ctx->rc_download_tbuf;
        SDL_DownloadFromGPUBuffer(dl_pass, &dl_src, &dl_dst);
        SDL_EndGPUCopyPass(dl_pass);

        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!fence || !SDL_WaitForGPUFences(ctx->device, true, &fence, 1U)) {
            if (fence) SDL_ReleaseGPUFence(ctx->device, fence);
            ok = false;
            break;
        }
        SDL_ReleaseGPUFence(ctx->device, fence);

        const uint32_t *res = (const uint32_t *)SDL_MapGPUTransferBuffer(
            ctx->device, ctx->rc_download_tbuf, false);
        if (!res) { ok = false; break; }
        for (uint32_t f = 0U; f < num_faults; ++f) {
            uint32_t fv = res[f * 4U + 0U];
            if (fv != 0xFFFFFFFFU && first_vecs[f] == 0xFFFFFFFFU) {
                first_vecs[f] = base_off + fv;
            }
        }
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->rc_download_tbuf);

        out->total_fault_evals += (uint64_t)num_faults * (uint64_t)chunk_trials;
        base_off += chunk_trials;
        remaining -= chunk_trials;
    }

    uint64_t t_end_ns = SDL_GetTicksNS();
    out->elapsed_us = (t_end_ns >= t_start_ns) ? ((t_end_ns - t_start_ns) / 1000ULL) : 1ULL;
    if (out->elapsed_us == 0ULL) {
        out->elapsed_us = 1ULL;
    }
    ctx->rc_valid = false;
    ctx->mesh_epoch++;

    bool cpu_ok = ok;
    for (uint32_t f = 0U; f < num_faults && ok; ++f) {
        if (first_vecs[f] != 0xFFFFFFFFU) {
            uint32_t vec = first_vecs[f];
            int32_t fwire = (int32_t)(2U + (f >> 1U));
            uint32_t stuck = f & 1U;
            uint64_t golden = bloom_bs_eval_cpu_fault(nl, vec, seed, -1, 0U);
            uint64_t faulty = bloom_bs_eval_cpu_fault(nl, vec, seed, fwire, stuck);
            if (golden == faulty) {
                cpu_ok = false;
                break;
            }
            out->detected_faults++;
            if (out->sample_atpg_vec == 0xFFFFFFFFU) {
                out->sample_fault_wire = (uint32_t)fwire;
                out->sample_stuck_val = stuck;
                out->sample_atpg_vec = vec;
            }
        } else {
            out->undetected_faults++;
        }
    }
    out->cpu_verified = cpu_ok;

    free(first_vecs);
    free(h_mesh);
    free(h_arena);
    free(nl);
    free(prog);
    return ok && cpu_ok;
}

bool bloom_gpu_equiv_fir1(BloomGpuCtx *ctx,
                          const uint8_t *fir_a,
                          size_t len_a,
                          const uint8_t *fir_b,
                          size_t len_b,
                          uint32_t num_trials,
                          uint64_t seed,
                          BloomFir1EquivResult *out) {
    if (!ctx || !ctx->device || !ctx->pipe_fir1_bs || !ctx->arena_gpu_buf || !ctx->mesh_gpu_buf ||
        !ctx->rc_gpu_buf || !ctx->upload_tbuf || !ctx->mesh_upload_tbuf || !ctx->rc_download_tbuf ||
        !fir_a || !fir_b || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->first_mismatch_trial = 0xFFFFFFFFU;

    flow_ir_prog_t *pa = (flow_ir_prog_t *)calloc(1U, sizeof(flow_ir_prog_t));
    flow_ir_prog_t *pb = (flow_ir_prog_t *)calloc(1U, sizeof(flow_ir_prog_t));
    BloomBsBuilder *bb = (BloomBsBuilder *)calloc(1U, sizeof(BloomBsBuilder));
    uint16_t (*vw_b)[64] = (uint16_t (*)[64])calloc((size_t)FLOW_IR_MAX_VALS, sizeof(*vw_b));
    BloomBsNetlist *nl = (BloomBsNetlist *)calloc(1U, sizeof(BloomBsNetlist));
    BloomEngineArena *h_arena = (BloomEngineArena *)calloc(1U, sizeof(BloomEngineArena));
    BloomMeshArena *h_mesh = (BloomMeshArena *)calloc(1U, sizeof(BloomMeshArena));

    if (!pa || !pb || !bb || !vw_b || !nl || !h_arena || !h_mesh) {
        free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
        return false;
    }

    if (!flow_ir_load_bytes(pa, fir_a, len_a) || !flow_ir_load_bytes(pb, fir_b, len_b) ||
        pa->num_inputs != pb->num_inputs || pa->num_outputs != pb->num_outputs || pa->num_outputs == 0U) {
        free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
        return false;
    }

    uint32_t tot_bits = 0U;
    for (uint32_t i = 0U; i < pa->num_inputs; ++i) {
        if (pa->vals[i].width != pb->vals[i].width) {
            free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
            return false;
        }
        tot_bits += (uint32_t)pa->vals[i].width;
    }
    for (uint32_t o = 0U; o < pa->num_outputs; ++o) {
        if (pa->vals[pa->outputs[o]].width != pb->vals[pb->outputs[o]].width) {
            free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
            return false;
        }
    }

    uint64_t zero_base[32];
    memset(zero_base, 0, sizeof(zero_base));
    if (!bb_init_builder(bb, pa, UINT32_C(0xFFFFFFFF), zero_base, false)) {
        free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
        return false;
    }
    memcpy(vw_b, bb->val_wires, (size_t)pa->num_inputs * 64U * sizeof(uint16_t));

    if (!bb_blast_prog_ops(bb, pa, bb->val_wires) || !bb_blast_prog_ops(bb, pb, vw_b)) {
        free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
        return false;
    }

    uint16_t miter_wire = 0U;
    for (uint32_t o = 0U; o < pa->num_outputs; ++o) {
        uint32_t oa = pa->outputs[o];
        uint32_t ob = pb->outputs[o];
        uint32_t ow = pa->vals[oa].width;
        for (uint32_t b = 0U; b < ow; ++b) {
            uint16_t dx = bb_xor(bb, bb->val_wires[oa][b], vw_b[ob][b]);
            miter_wire = bb_or(bb, miter_wire, dx);
            if (miter_wire == UINT16_C(0xFFFF)) {
                free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
                return false;
            }
        }
    }

    if (!bb_compact_netlist(bb, &miter_wire, 1U, nl)) {
        free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
        return false;
    }

    out->total_in_bits = tot_bits;
    out->miter_gates = nl->num_gates;
    if (num_trials == 0U) {
        out->exhaustive = (tot_bits <= 24U);
        num_trials = (tot_bits <= 24U) ? (UINT32_C(1) << tot_bits) : UINT32_C(16777216);
    } else {
        out->exhaustive = (tot_bits < 31U) && ((uint64_t)num_trials >= (UINT64_C(1) << tot_bits));
    }

    BloomFir1HuntConfig hcfg;
    memset(&hcfg, 0, sizeof(hcfg));
    hcfg.num_trials = num_trials;
    hcfg.target_mode = BLOOM_FIR_HUNT_NEQ;
    hcfg.sweep_mode = 2U;
    hcfg.vary_input_mask = UINT32_C(0xFFFFFFFF);
    hcfg.seed_lo = (uint32_t)(seed & UINT64_C(0xFFFFFFFF));
    hcfg.seed_hi = (uint32_t)(seed >> 32U);
    hcfg.target_lo = 0U;
    hcfg.target_hi = 0U;

    BloomFir1HuntResult hres;
    memset(&hres, 0, sizeof(hres));
    hres.first_hit_trial = 0xFFFFFFFFU;

    bool ok = true;
    if (nl->num_gates == 0U && nl->out0_wires[0] == 0U) {
        /* Constant-folded identical to 0 bit-for-bit across all inputs! */
        out->trials_evaluated = num_trials;
        out->elapsed_us = 1ULL;
    } else {
        ok = bloom_bs_run_netlist(ctx, nl, &hcfg, h_arena, h_mesh, &hres);
        out->trials_evaluated = hres.trials_evaluated;
        out->mismatch_count = hres.hit_count;
        out->first_mismatch_trial = hres.first_hit_trial;
        out->elapsed_us = hres.elapsed_us;
    }

    if (ok) {
        if (out->mismatch_count > 0U && out->first_mismatch_trial != 0xFFFFFFFFU) {
            bloom_fir_build_trial_inputs(pa, &hcfg, out->first_mismatch_trial, out->cex_inputs);
            bool ev_ok = flow_ir_eval_u64(pa, out->cex_inputs, out->cex_out_a) &&
                         flow_ir_eval_u64(pb, out->cex_inputs, out->cex_out_b);
            bool diff_seen = false;
            for (uint32_t o = 0U; o < pa->num_outputs && ev_ok; ++o) {
                if (out->cex_out_a[o] != out->cex_out_b[o]) {
                    diff_seen = true;
                }
            }
            out->equivalent = false;
            out->cpu_verified = ev_ok && diff_seen;
            ok = out->cpu_verified;
        } else {
            uint64_t tmp_in[FLOW_IR_MAX_IO];
            uint64_t oa[FLOW_IR_MAX_IO], ob[FLOW_IR_MAX_IO];
            uint32_t spot[3] = { 0U, (num_trials > 1U) ? (num_trials / 2U) : 0U, num_trials - 1U };
            bool spots_eq = true;
            for (uint32_t s = 0U; s < 3U && spots_eq; ++s) {
                bloom_fir_build_trial_inputs(pa, &hcfg, spot[s], tmp_in);
                if (!flow_ir_eval_u64(pa, tmp_in, oa) || !flow_ir_eval_u64(pb, tmp_in, ob)) {
                    spots_eq = false;
                    break;
                }
                for (uint32_t o = 0U; o < pa->num_outputs; ++o) {
                    if (oa[o] != ob[o]) {
                        spots_eq = false;
                        break;
                    }
                }
            }
            out->equivalent = spots_eq;
            out->cpu_verified = spots_eq;
            ok = spots_eq;
        }
    }

    free(h_mesh); free(h_arena); free(nl); free(vw_b); free(bb); free(pb); free(pa);
    return ok;
}

/* ----------------------------------------------------------------------------
 * GPU All-Pairs 4-Bit x 64-Perm b-bit MinHash / Structural Similarity Matrix
 * ---------------------------------------------------------------------------- */

void bloom_info_sketch_bytes(const uint8_t *data, size_t len, uint32_t out_sig8[8]) {
    memset(out_sig8, 0, 8U * sizeof(uint32_t));
    if (!data || len == 0U) {
        return;
    }
    uint32_t buckets[64];
    for (uint32_t b = 0U; b < 64U; ++b) {
        buckets[b] = 0xFFFFFFFFU;
    }

    if (len < 5U) {
        uint32_t acc = UINT32_C(0x811C9DC5);
        for (size_t i = 0U; i < len; ++i) {
            acc ^= (uint32_t)data[i];
            acc *= UINT32_C(0x01000193);
        }
        uint32_t bk = acc & 63U;
        buckets[bk] = bloom_fir_fmix32(acc);
    } else {
        for (size_t p = 0U; p + 5U <= len; ++p) {
            uint32_t w = ((uint32_t)data[p + 0U]) |
                         ((uint32_t)data[p + 1U] << 8U) |
                         ((uint32_t)data[p + 2U] << 16U) |
                         ((uint32_t)data[p + 3U] << 24U);
            uint32_t h = bloom_fir_fmix32(w ^ ((uint32_t)data[p + 4U] * UINT32_C(0x9E3779B9)));
            uint32_t bk = h & 63U;
            uint32_t val = bloom_fir_fmix32(h ^ UINT32_C(0x85EBCA6B));
            if (val < buckets[bk]) {
                buckets[bk] = val;
            }
        }
    }

    uint32_t densified[64];
    for (uint32_t b = 0U; b < 64U; ++b) {
        densified[b] = buckets[b];
        if (densified[b] == 0xFFFFFFFFU) {
            for (uint32_t c = 0U; c < 64U; ++c) {
                uint32_t src = (b + c * 17U + 1U) & 63U;
                if (buckets[src] != 0xFFFFFFFFU) {
                    densified[b] = bloom_fir_fmix32(buckets[src] ^ (b * UINT32_C(0x85EBCA6B)) ^ c);
                    break;
                }
            }
        }
    }
    for (uint32_t b = 0U; b < 64U; ++b) {
        uint32_t mixed = bloom_fir_fmix32(densified[b] ^ (b * UINT32_C(0x9E3779B9)) ^ UINT32_C(0x517CC1B7));
        uint32_t nib = (mixed >> 28U) & 0xFU;
        out_sig8[b >> 3U] |= nib << ((b & 7U) * 4U);
    }
}

static uint32_t bloom_swar_nibble_eq_u32(uint32_t a, uint32_t b) {
    uint32_t x = a ^ b;
    uint32_t nz = (x | (x >> 1U) | (x >> 2U) | (x >> 3U)) & UINT32_C(0x11111111);
    uint32_t cnt = 0U;
    for (uint32_t k = 0U; k < 8U; ++k) {
        cnt += (nz >> (k * 4U)) & 1U;
    }
    return 8U - cnt;
}

static uint32_t bloom_sig_matches_cpu(const uint32_t a[8], const uint32_t b[8]) {
    uint32_t m = 0U;
    for (uint32_t w = 0U; w < 8U; ++w) {
        m += bloom_swar_nibble_eq_u32(a[w], b[w]);
    }
    return m;
}

bool bloom_gpu_info_sim_matrix(BloomGpuCtx *ctx,
                               const uint32_t *sigs_8xu32,
                               uint32_t num_files,
                               uint32_t thr_matches,
                               BloomInfoSimRow *out_rows,
                               BloomInfoSimResult *out_summary) {
    if (!ctx || !ctx->device || !ctx->pipe_info_sim || !ctx->arena_gpu_buf || !ctx->rc_gpu_buf ||
        !ctx->upload_tbuf || !ctx->rc_download_tbuf || !sigs_8xu32 || !out_rows || !out_summary ||
        num_files < 2U || num_files > 8192U) {
        return false;
    }
    memset(out_summary, 0, sizeof(*out_summary));
    out_summary->num_files = num_files;
    out_summary->total_pairs_eval = ((uint64_t)num_files * (uint64_t)(num_files - 1U)) / 2ULL;

    BloomEngineArena *h_arena = (BloomEngineArena *)calloc(1U, sizeof(BloomEngineArena));
    if (!h_arena) {
        return false;
    }
    uint32_t *au = (uint32_t *)(void *)h_arena;
    au[0] = UINT32_C(0x494E4653); /* "INFS" */
    au[1] = num_files;
    au[2] = (thr_matches <= 64U) ? thr_matches : 64U;

    uint32_t sig_bytes = num_files * 32U;
    SDL_GPUTransferBufferCreateInfo sig_up_info;
    memset(&sig_up_info, 0, sizeof(sig_up_info));
    sig_up_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    sig_up_info.size = sig_bytes;
    SDL_GPUTransferBuffer *sig_tbuf = SDL_CreateGPUTransferBuffer(ctx->device, &sig_up_info);
    if (!sig_tbuf) {
        free(h_arena);
        return false;
    }

    void *m_sig = SDL_MapGPUTransferBuffer(ctx->device, sig_tbuf, false);
    void *m_ar = SDL_MapGPUTransferBuffer(ctx->device, ctx->upload_tbuf, false);
    if (!m_sig || !m_ar) {
        if (m_sig) SDL_UnmapGPUTransferBuffer(ctx->device, sig_tbuf);
        if (m_ar) SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upload_tbuf);
        SDL_ReleaseGPUTransferBuffer(ctx->device, sig_tbuf);
        free(h_arena);
        return false;
    }
    memcpy(m_sig, sigs_8xu32, sig_bytes);
    memcpy(m_ar, h_arena, BLOOM_ARENA_BYTES);
    SDL_UnmapGPUTransferBuffer(ctx->device, sig_tbuf);
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upload_tbuf);

    uint64_t t_start_ns = SDL_GetTicksNS();
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, sig_tbuf);
        free(h_arena);
        return false;
    }

    SDL_GPUCopyPass *up_pass = SDL_BeginGPUCopyPass(cmd);
    if (!up_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(ctx->device, sig_tbuf);
        free(h_arena);
        return false;
    }
    SDL_GPUTransferBufferLocation a_src;
    memset(&a_src, 0, sizeof(a_src));
    a_src.transfer_buffer = ctx->upload_tbuf;
    SDL_GPUBufferRegion a_dst;
    memset(&a_dst, 0, sizeof(a_dst));
    a_dst.buffer = ctx->arena_gpu_buf;
    a_dst.size = (Uint32)BLOOM_ARENA_BYTES;
    SDL_UploadToGPUBuffer(up_pass, &a_src, &a_dst, false);

    SDL_GPUTransferBufferLocation s_src;
    memset(&s_src, 0, sizeof(s_src));
    s_src.transfer_buffer = sig_tbuf;
    SDL_GPUBufferRegion s_dst;
    memset(&s_dst, 0, sizeof(s_dst));
    s_dst.buffer = ctx->rc_gpu_buf;
    s_dst.offset = 0U;
    s_dst.size = sig_bytes;
    SDL_UploadToGPUBuffer(up_pass, &s_src, &s_dst, false);
    SDL_EndGPUCopyPass(up_pass);

    SDL_GPUStorageTextureReadWriteBinding rw_tex;
    memset(&rw_tex, 0, sizeof(rw_tex));
    rw_tex.texture = ctx->frame_tex;
    SDL_GPUStorageBufferReadWriteBinding rw_buf;
    memset(&rw_buf, 0, sizeof(rw_buf));
    rw_buf.buffer = ctx->rc_gpu_buf;
    SDL_GPUComputePass *cpass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1U, &rw_buf, 1U);
    if (!cpass) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(ctx->device, sig_tbuf);
        free(h_arena);
        return false;
    }
    SDL_GPUBuffer *ro_bufs[2] = { ctx->arena_gpu_buf, ctx->mesh_gpu_buf };
    SDL_BindGPUComputePipeline(cpass, ctx->pipe_info_sim);
    SDL_BindGPUComputeStorageBuffers(cpass, 0U, ro_bufs, 2U);
    SDL_DispatchGPUCompute(cpass, (num_files + 63U) / 64U, 1U, 1U);
    SDL_EndGPUComputePass(cpass);

    SDL_GPUCopyPass *dl_pass = SDL_BeginGPUCopyPass(cmd);
    if (!dl_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(ctx->device, sig_tbuf);
        free(h_arena);
        return false;
    }
    SDL_GPUBufferRegion dl_src;
    memset(&dl_src, 0, sizeof(dl_src));
    dl_src.buffer = ctx->rc_gpu_buf;
    dl_src.offset = (Uint32)(24576U * 16U);
    dl_src.size = (Uint32)(num_files * 16U);
    SDL_GPUTransferBufferLocation dl_dst;
    memset(&dl_dst, 0, sizeof(dl_dst));
    dl_dst.transfer_buffer = ctx->rc_download_tbuf;
    SDL_DownloadFromGPUBuffer(dl_pass, &dl_src, &dl_dst);
    SDL_EndGPUCopyPass(dl_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    bool waited = (fence != NULL) && SDL_WaitForGPUFences(ctx->device, true, &fence, 1U);
    if (fence) SDL_ReleaseGPUFence(ctx->device, fence);
    SDL_ReleaseGPUTransferBuffer(ctx->device, sig_tbuf);
    free(h_arena);

    if (!waited) {
        return false;
    }

    uint64_t t_end_ns = SDL_GetTicksNS();
    out_summary->elapsed_us = (t_end_ns >= t_start_ns) ? ((t_end_ns - t_start_ns) / 1000ULL) : 1ULL;
    if (out_summary->elapsed_us == 0ULL) {
        out_summary->elapsed_us = 1ULL;
    }
    ctx->rc_valid = false;

    const uint32_t *res = (const uint32_t *)SDL_MapGPUTransferBuffer(
        ctx->device, ctx->rc_download_tbuf, false);
    if (!res) {
        return false;
    }

    uint32_t exact_deg_sum = 0U;
    bool cpu_ok = true;
    for (uint32_t i = 0U; i < num_files; ++i) {
        uint32_t bj = res[i * 4U + 0U];
        uint32_t bm = res[i * 4U + 1U];
        uint32_t nc = res[i * 4U + 2U];
        uint32_t ec = res[i * 4U + 3U];
        out_rows[i].best_match_idx = bj;
        out_rows[i].best_matches = bm;
        out_rows[i].near_dup_count = nc;
        out_rows[i].exact_dup_count = ec;
        if (nc > 0U) {
            out_summary->files_with_near_dup++;
        }
        exact_deg_sum += ec;
        if (bj >= num_files || bj == i ||
            bloom_sig_matches_cpu(&sigs_8xu32[i * 8U], &sigs_8xu32[bj * 8U]) != bm) {
            cpu_ok = false;
        }
    }
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->rc_download_tbuf);

    out_summary->exact_dup_pairs = exact_deg_sum / 2U;
    out_summary->cpu_verified = cpu_ok;
    return cpu_ok;
}

static uint32_t bloom_dst_hash_u32(uint32_t x) {
    uint32_t v = x ^ (x >> 16U);
    v *= UINT32_C(0x7FEB352D);
    v ^= (v >> 15U);
    v *= UINT32_C(0x846CA68B);
    return v ^ (v >> 16U);
}

static uint32_t bloom_dst_xorshift32(uint32_t state) {
    uint32_t x = (state == 0U) ? UINT32_C(0x6D2B79F5) : state;
    x ^= (x << 13U);
    x ^= (x >> 17U);
    x ^= (x << 5U);
    return x;
}

static void bloom_node_dst_cpu_sim_one(uint32_t u_id,
                                         uint32_t ticks,
                                         uint32_t fault_ppm,
                                         uint32_t seed_lo,
                                         uint32_t seed_hi,
                                         bool buggy_mode,
                                         uint32_t *out_viol,
                                         uint32_t *out_commits,
                                         uint32_t *out_elections,
                                         uint32_t *out_digest) {
    uint32_t clamped_ticks = (ticks <= 4096U) ? ticks : 4096U;
    uint32_t clamped_ppm = (fault_ppm <= 1000000U) ? fault_ppm : 1000000U;
    uint32_t rng = bloom_dst_hash_u32(seed_lo ^ bloom_dst_hash_u32(seed_hi ^ (u_id + UINT32_C(0x9E3779B9))));
    if (rng == 0U) {
        rng = UINT32_C(0x6D2B79F5);
    }

    uint32_t term[5];
    uint32_t voted_for[5];
    uint32_t role[5];
    uint32_t log_len[5];
    uint32_t commit_idx[5];
    uint32_t alive[5];
    uint32_t part[5];
    for (uint32_t n = 0U; n < 5U; ++n) {
        term[n] = 0U;
        voted_for[n] = 5U;
        role[n] = 0U;
        log_len[n] = 0U;
        commit_idx[n] = 0U;
        alive[n] = 1U;
        part[n] = 0U;
    }

    uint32_t last_elected_term = 0U;
    uint32_t last_elected_leader = 5U;
    uint32_t safety_violations = 0U;
    uint32_t commits_total = 0U;
    uint32_t elections_total = 0U;

    for (uint32_t t = 0U; t < clamped_ticks; ++t) {
        for (uint32_t n = 0U; n < 5U; ++n) {
            rng = bloom_dst_xorshift32(rng);
            if ((rng % 1000000U) < clamped_ppm) {
                uint32_t kind = (rng >> 8U) & 3U;
                if (kind == 0U) {
                    alive[n] = 0U;
                    role[n] = 0U;
                } else if (kind == 1U) {
                    alive[n] = 1U;
                    role[n] = 0U;
                    if (buggy_mode) {
                        voted_for[n] = 5U;
                    }
                } else if (kind == 2U) {
                    part[n] = 1U;
                } else {
                    part[n] = 0U;
                    alive[n] = 1U;
                }
            } else if (alive[n] == 0U && ((rng & 1U) != 0U)) {
                alive[n] = 1U;
                if (buggy_mode) {
                    voted_for[n] = 5U;
                }
            }
        }

        for (uint32_t attempt = 0U; attempt < 2U; ++attempt) {
            rng = bloom_dst_xorshift32(rng);
            uint32_t cand = rng % 5U;
            if (alive[cand] != 0U && role[cand] == 0U) {
                uint32_t new_term = term[cand] + 1U;
                if (term[cand] > 0U && voted_for[cand] == 5U) {
                    new_term = term[cand];
                }
                term[cand] = new_term;
                voted_for[cand] = cand;
                role[cand] = 1U;

                uint32_t votes = 1U;
                for (uint32_t v = 0U; v < 5U; ++v) {
                    if (v != cand && alive[v] != 0U && part[v] == part[cand]) {
                        if (term[v] < new_term) {
                            term[v] = new_term;
                            voted_for[v] = 5U;
                            role[v] = 0U;
                        }
                        if (term[v] == new_term && (voted_for[v] == 5U || voted_for[v] == cand) &&
                            commit_idx[cand] >= commit_idx[v]) {
                            voted_for[v] = cand;
                            votes++;
                        }
                    }
                }

                if (votes >= 3U) {
                    role[cand] = 2U;
                    elections_total++;
                    if (new_term == last_elected_term && last_elected_leader != 5U && last_elected_leader != cand) {
                        safety_violations++;
                    }
                    if (new_term > last_elected_term) {
                        last_elected_term = new_term;
                        last_elected_leader = cand;
                    }
                } else {
                    role[cand] = 0U;
                }
            }
        }

        for (uint32_t a = 0U; a < 5U; ++a) {
            if (role[a] == 2U) {
                for (uint32_t b = a + 1U; b < 5U; ++b) {
                    if (role[b] == 2U && term[a] == term[b]) {
                        safety_violations++;
                    }
                }
            }
        }

        for (uint32_t ldr = 0U; ldr < 5U; ++ldr) {
            if (alive[ldr] != 0U && role[ldr] == 2U) {
                uint32_t acks = 1U;
                for (uint32_t p = 0U; p < 5U; ++p) {
                    if (p != ldr && alive[p] != 0U && part[p] == part[ldr]) {
                        if (term[p] <= term[ldr]) {
                            if (term[p] < term[ldr]) {
                                term[p] = term[ldr];
                                voted_for[p] = 5U;
                                role[p] = 0U;
                            }
                            acks++;
                        }
                    }
                }
                if (acks >= 3U) {
                    log_len[ldr] += 1U;
                    commit_idx[ldr] = log_len[ldr];
                    commits_total++;
                    for (uint32_t p2 = 0U; p2 < 5U; ++p2) {
                        if (p2 != ldr && alive[p2] != 0U && part[p2] == part[ldr] && term[p2] == term[ldr]) {
                            if (log_len[p2] < log_len[ldr]) {
                                log_len[p2] = log_len[ldr];
                            }
                            if (commit_idx[p2] < commit_idx[ldr]) {
                                commit_idx[p2] = commit_idx[ldr];
                            }
                        }
                    }
                } else {
                    role[ldr] = 0U;
                }
            }
        }
    }

    uint32_t digest = UINT32_C(0x811C9DC5) ^ u_id;
    for (uint32_t n = 0U; n < 5U; ++n) {
        digest = (digest ^ (term[n] + (voted_for[n] << 8U) + (role[n] << 12U) + (commit_idx[n] << 16U))) *
                 UINT32_C(0x01000193);
    }
    digest = (digest ^ safety_violations ^ (commits_total * UINT32_C(0x9E3779B9)) ^
              (elections_total * UINT32_C(0x85EBCA6B))) *
             UINT32_C(0x01000193);

    *out_viol = safety_violations;
    *out_commits = commits_total;
    *out_elections = elections_total;
    *out_digest = digest;
}

bool bloom_gpu_node_dst(BloomGpuCtx *ctx,
                        uint32_t num_universes,
                        uint32_t ticks,
                        uint32_t fault_prob_ppm,
                        uint64_t seed,
                        bool buggy_mode,
                        BloomNodeDstResult *out) {
    if (!ctx || !ctx->device || !ctx->pipe_node_dst || !ctx->arena_gpu_buf || !ctx->rc_gpu_buf ||
        !ctx->upload_tbuf || !ctx->rc_download_tbuf || !out ||
        num_universes == 0U || num_universes > 65536U || ticks == 0U || ticks > 4096U ||
        fault_prob_ppm > 1000000U) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->num_universes = num_universes;
    out->ticks = ticks;
    out->fault_prob_ppm = fault_prob_ppm;
    out->buggy_mode = buggy_mode;
    out->total_universe_ticks = (uint64_t)num_universes * (uint64_t)ticks;
    out->first_cex_universe = UINT32_MAX;

    BloomEngineArena *h_arena = (BloomEngineArena *)calloc(1U, sizeof(BloomEngineArena));
    if (!h_arena) {
        return false;
    }

    uint32_t seed_lo = (uint32_t)(seed & 0xFFFFFFFFULL);
    uint32_t seed_hi = (uint32_t)(seed >> 32U);
    uint32_t *au = (uint32_t *)(void *)h_arena;
    au[0] = UINT32_C(0x4E445354); /* "NDST" */
    au[2] = ticks;
    au[3] = fault_prob_ppm;
    au[4] = seed_lo;
    au[5] = seed_hi;
    au[7] = buggy_mode ? 1U : 0U;

    bool cpu_ok = true;
    uint64_t t_start_ns = SDL_GetTicksNS();

    for (uint32_t base_u = 0U; base_u < num_universes; base_u += 8192U) {
        uint32_t batch_n = (num_universes - base_u > 8192U) ? 8192U : (num_universes - base_u);
        au[1] = batch_n;
        au[6] = base_u;

        void *m_ar = SDL_MapGPUTransferBuffer(ctx->device, ctx->upload_tbuf, false);
        if (!m_ar) {
            free(h_arena);
            return false;
        }
        memcpy(m_ar, h_arena, BLOOM_ARENA_BYTES);
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upload_tbuf);

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
        if (!cmd) {
            free(h_arena);
            return false;
        }

        SDL_GPUCopyPass *up_pass = SDL_BeginGPUCopyPass(cmd);
        if (!up_pass) {
            SDL_CancelGPUCommandBuffer(cmd);
            free(h_arena);
            return false;
        }
        SDL_GPUTransferBufferLocation a_src;
        memset(&a_src, 0, sizeof(a_src));
        a_src.transfer_buffer = ctx->upload_tbuf;
        SDL_GPUBufferRegion a_dst;
        memset(&a_dst, 0, sizeof(a_dst));
        a_dst.buffer = ctx->arena_gpu_buf;
        a_dst.size = (Uint32)BLOOM_ARENA_BYTES;
        SDL_UploadToGPUBuffer(up_pass, &a_src, &a_dst, false);
        SDL_EndGPUCopyPass(up_pass);

        SDL_GPUStorageTextureReadWriteBinding rw_tex;
        memset(&rw_tex, 0, sizeof(rw_tex));
        rw_tex.texture = ctx->frame_tex;
        SDL_GPUStorageBufferReadWriteBinding rw_buf;
        memset(&rw_buf, 0, sizeof(rw_buf));
        rw_buf.buffer = ctx->rc_gpu_buf;
        SDL_GPUComputePass *cpass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1U, &rw_buf, 1U);
        if (!cpass) {
            SDL_CancelGPUCommandBuffer(cmd);
            free(h_arena);
            return false;
        }
        SDL_GPUBuffer *ro_bufs[2] = { ctx->arena_gpu_buf, ctx->mesh_gpu_buf };
        SDL_BindGPUComputePipeline(cpass, ctx->pipe_node_dst);
        SDL_BindGPUComputeStorageBuffers(cpass, 0U, ro_bufs, 2U);
        SDL_DispatchGPUCompute(cpass, (batch_n + 63U) / 64U, 1U, 1U);
        SDL_EndGPUComputePass(cpass);

        SDL_GPUCopyPass *dl_pass = SDL_BeginGPUCopyPass(cmd);
        if (!dl_pass) {
            SDL_CancelGPUCommandBuffer(cmd);
            free(h_arena);
            return false;
        }
        SDL_GPUBufferRegion dl_src;
        memset(&dl_src, 0, sizeof(dl_src));
        dl_src.buffer = ctx->rc_gpu_buf;
        dl_src.offset = (Uint32)(24576U * 16U);
        dl_src.size = (Uint32)(batch_n * 16U);
        SDL_GPUTransferBufferLocation dl_dst;
        memset(&dl_dst, 0, sizeof(dl_dst));
        dl_dst.transfer_buffer = ctx->rc_download_tbuf;
        SDL_DownloadFromGPUBuffer(dl_pass, &dl_src, &dl_dst);
        SDL_EndGPUCopyPass(dl_pass);

        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        bool waited = (fence != NULL) && SDL_WaitForGPUFences(ctx->device, true, &fence, 1U);
        if (fence) SDL_ReleaseGPUFence(ctx->device, fence);
        if (!waited) {
            free(h_arena);
            return false;
        }

        const uint32_t *res = (const uint32_t *)SDL_MapGPUTransferBuffer(
            ctx->device, ctx->rc_download_tbuf, false);
        if (!res) {
            free(h_arena);
            return false;
        }

        for (uint32_t i = 0U; i < batch_n; ++i) {
            uint32_t g_u = base_u + i;
            uint32_t viol = res[i * 4U + 0U];
            uint32_t commits = res[i * 4U + 1U];
            uint32_t elecs = res[i * 4U + 2U];
            uint32_t digest = res[i * 4U + 3U];
            if (viol > 0U) {
                out->violated_universes++;
                out->total_violations += (uint64_t)viol;
                if (out->first_cex_universe == UINT32_MAX) {
                    out->first_cex_universe = g_u;
                }
            }
            out->total_commits += (uint64_t)commits;
            out->total_elections += (uint64_t)elecs;
            out->state_digest_xor ^= digest;

            bool check_sample = (batch_n <= 256U) || (i < 32U) || (i + 32U >= batch_n) ||
                                (viol > 0U && g_u == out->first_cex_universe);
            if (check_sample && cpu_ok) {
                uint32_t c_v = 0U, c_c = 0U, c_e = 0U, c_d = 0U;
                bloom_node_dst_cpu_sim_one(g_u, ticks, fault_prob_ppm, seed_lo, seed_hi, buggy_mode,
                                           &c_v, &c_c, &c_e, &c_d);
                if (c_v != viol || c_c != commits || c_e != elecs || c_d != digest) {
                    cpu_ok = false;
                }
            }
        }
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->rc_download_tbuf);
    }

    uint64_t t_end_ns = SDL_GetTicksNS();
    out->elapsed_us = (t_end_ns >= t_start_ns) ? ((t_end_ns - t_start_ns) / 1000ULL) : 1ULL;
    if (out->elapsed_us == 0ULL) {
        out->elapsed_us = 1ULL;
    }
    ctx->rc_valid = false;
    free(h_arena);
    out->cpu_verified = cpu_ok;
    return cpu_ok;
}

bool bloom_gpu_upload_mesh(BloomGpuCtx *ctx, const BloomMeshArena *mesh) {
    if (!ctx || !ctx->device || !ctx->mesh_gpu_buf || !ctx->mesh_upload_tbuf) {
        return false;
    }
    ctx->mesh_epoch++;
    ctx->rc_valid = false;
    void *mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->mesh_upload_tbuf, false);
    if (!mapped) {
        return false;
    }
    if (mesh) {
        memcpy(mapped, mesh, BLOOM_MESH_ARENA_BYTES);
    } else {
        memset(mapped, 0, BLOOM_MESH_ARENA_BYTES);
    }
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->mesh_upload_tbuf);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        return false;
    }
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    SDL_GPUTransferBufferLocation src_loc;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.transfer_buffer = ctx->mesh_upload_tbuf;
    src_loc.offset = 0U;

    SDL_GPUBufferRegion dst_reg;
    memset(&dst_reg, 0, sizeof(dst_reg));
    dst_reg.buffer = ctx->mesh_gpu_buf;
    dst_reg.offset = 0U;
    dst_reg.size = (Uint32)BLOOM_MESH_ARENA_BYTES;

    SDL_UploadToGPUBuffer(copy_pass, &src_loc, &dst_reg, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) {
        return false;
    }
    bool ok = SDL_WaitForGPUFences(ctx->device, true, &fence, 1U);
    SDL_ReleaseGPUFence(ctx->device, fence);
    return ok;
}

bool bloom_gpu_render(BloomGpuCtx *ctx, bool force_render) {
    if (!ctx || !ctx->arena || !ctx->device || !ctx->pipeline || !ctx->pipe_rc2 ||
        !ctx->pipe_rc1 || !ctx->pipe_rc0 || !ctx->arena_gpu_buf ||
        !ctx->mesh_gpu_buf || !ctx->rc_gpu_buf || !ctx->upload_tbuf || !ctx->frame_tex) {
        return false;
    }

    const BloomFrameHeader64 *hdr = &ctx->arena->cells[BLOOM_SLOT_HEADER].frame_hdr;
    if (!force_render && (hdr->flags & BLOOM_FLAG_IDLE_SLEEP) != 0U && ctx->dispatch_count > 0U) {
        ctx->idle_skip_count++;
        return true;
    }

    void *mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->upload_tbuf, false);
    if (!mapped) {
        return false;
    }
    memcpy(mapped, ctx->arena, BLOOM_ARENA_BYTES);
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upload_tbuf);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        return false;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    SDL_GPUTransferBufferLocation src_loc;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.transfer_buffer = ctx->upload_tbuf;
    src_loc.offset = 0U;

    SDL_GPUBufferRegion dst_reg;
    memset(&dst_reg, 0, sizeof(dst_reg));
    dst_reg.buffer = ctx->arena_gpu_buf;
    dst_reg.offset = 0U;
    dst_reg.size = (Uint32)BLOOM_ARENA_BYTES;

    SDL_UploadToGPUBuffer(copy_pass, &src_loc, &dst_reg, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUStorageTextureReadWriteBinding rw_tex;
    memset(&rw_tex, 0, sizeof(rw_tex));
    rw_tex.texture = ctx->frame_tex;
    rw_tex.mip_level = 0U;
    rw_tex.layer = 0U;
    rw_tex.cycle = false;

    SDL_GPUStorageBufferReadWriteBinding rw_buf;
    memset(&rw_buf, 0, sizeof(rw_buf));
    rw_buf.buffer = ctx->rc_gpu_buf;
    rw_buf.cycle = false;

    SDL_GPUBuffer *ro_bufs[2] = { ctx->arena_gpu_buf, ctx->mesh_gpu_buf };
    if ((hdr->flags & BLOOM_FLAG_RADIANCE_CASCADES) != 0U) {
        uint64_t cur_rc_hash = bloom_gpu_hash_rc_scene(ctx->arena, ctx->mesh_epoch);
        if (!force_render && ctx->rc_valid && cur_rc_hash == ctx->rc_scene_hash) {
            ctx->rc_cache_hits++;
        } else {
            SDL_GPUComputePipeline *rc_pipes[3] = { ctx->pipe_rc2, ctx->pipe_rc1, ctx->pipe_rc0 };
            const Uint32 rc_gx[3] = { 64U, 64U, 32U };
            const Uint32 rc_gy[3] = { 1U,  4U,  1U };
            for (uint32_t p = 0U; p < 3U; ++p) {
                SDL_GPUComputePass *rc_pass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1U, &rw_buf, 1U);
                if (!rc_pass) {
                    SDL_CancelGPUCommandBuffer(cmd);
                    return false;
                }
                SDL_BindGPUComputePipeline(rc_pass, rc_pipes[p]);
                SDL_BindGPUComputeStorageBuffers(rc_pass, 0U, ro_bufs, 2U);
                SDL_DispatchGPUCompute(rc_pass, rc_gx[p], rc_gy[p], 1U);
                SDL_EndGPUComputePass(rc_pass);
            }
            ctx->rc_scene_hash = cur_rc_hash;
            ctx->rc_valid = true;
        }
    }

    SDL_GPUComputePass *comp_pass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1U, &rw_buf, 1U);
    if (!comp_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    SDL_BindGPUComputePipeline(comp_pass, ctx->pipeline);
    SDL_BindGPUComputeStorageBuffers(comp_pass, 0U, ro_bufs, 2U);
    SDL_DispatchGPUCompute(comp_pass, (ctx->width + 7U) / 8U, (ctx->height + 7U) / 8U, 1U);
    SDL_EndGPUComputePass(comp_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) {
        return false;
    }
    bool ok = SDL_WaitForGPUFences(ctx->device, true, &fence, 1U);
    SDL_ReleaseGPUFence(ctx->device, fence);
    if (!ok) {
        return false;
    }

    ctx->dispatch_count++;
    return true;
}

static const uint8_t *bloom_gpu_map_frame_rgba8(const BloomGpuCtx *ctx) {
    if (!ctx || !ctx->device || !ctx->frame_tex || !ctx->download_tbuf) {
        return NULL;
    }

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        return NULL;
    }
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return NULL;
    }

    SDL_GPUTextureRegion src_reg;
    memset(&src_reg, 0, sizeof(src_reg));
    src_reg.texture = ctx->frame_tex;
    src_reg.mip_level = 0U;
    src_reg.layer = 0U;
    src_reg.w = ctx->width;
    src_reg.h = ctx->height;
    src_reg.d = 1U;

    SDL_GPUTextureTransferInfo dst_xf;
    memset(&dst_xf, 0, sizeof(dst_xf));
    dst_xf.transfer_buffer = ctx->download_tbuf;
    dst_xf.offset = 0U;
    dst_xf.pixels_per_row = ctx->width;
    dst_xf.rows_per_layer = ctx->height;

    SDL_DownloadFromGPUTexture(copy_pass, &src_reg, &dst_xf);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) {
        return NULL;
    }
    bool waited = SDL_WaitForGPUFences(ctx->device, true, &fence, 1U);
    SDL_ReleaseGPUFence(ctx->device, fence);
    if (!waited) {
        return NULL;
    }

    return (const uint8_t *)SDL_MapGPUTransferBuffer(ctx->device, ctx->download_tbuf, false);
}

bool bloom_gpu_write_ppm(const BloomGpuCtx *ctx,
                         const char *ppm_path,
                         uint64_t *out_fnv_hash,
                         uint32_t *out_nonblack_px) {
    const uint8_t *rgba = bloom_gpu_map_frame_rgba8(ctx);
    if (!rgba) {
        return false;
    }

    bool want_file = (ppm_path != NULL && ppm_path[0] != '\0');
    FILE *fp = want_file ? fopen(ppm_path, "wb") : NULL;
    if (want_file && !fp) {
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->download_tbuf);
        return false;
    }
    if (fp) {
        fprintf(fp, "P6\n%u %u\n255\n", ctx->width, ctx->height);
    }

    uint64_t fnv = UINT64_C(0xCBF29CE484222325);
    uint32_t nonblack = 0U;
    uint8_t row_rgb[4096U * 3U];
    uint32_t row_w = (ctx->width > 4096U) ? 4096U : ctx->width;

    for (uint32_t y = 0U; y < ctx->height; ++y) {
        const uint8_t *src = rgba + (size_t)y * (size_t)ctx->width * 4U;
        for (uint32_t x = 0U; x < ctx->width; ++x) {
            uint8_t r = src[x * 4U + 0U];
            uint8_t g = src[x * 4U + 1U];
            uint8_t b = src[x * 4U + 2U];
            if (r > 10U || g > 10U || b > 14U) {
                ++nonblack;
            }
            fnv = (fnv ^ (uint64_t)r) * UINT64_C(0x100000001B3);
            fnv = (fnv ^ (uint64_t)g) * UINT64_C(0x100000001B3);
            fnv = (fnv ^ (uint64_t)b) * UINT64_C(0x100000001B3);
            if (fp && x < row_w) {
                row_rgb[x * 3U + 0U] = r;
                row_rgb[x * 3U + 1U] = g;
                row_rgb[x * 3U + 2U] = b;
            }
        }
        if (fp) {
            (void)fwrite(row_rgb, 1U, (size_t)row_w * 3U, fp);
        }
    }

    if (fp) {
        fclose(fp);
    }
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->download_tbuf);

    if (out_fnv_hash) {
        *out_fnv_hash = fnv;
    }
    if (out_nonblack_px) {
        *out_nonblack_px = nonblack;
    }
    return true;
}

static int32_t bloom_norm_coord_q16(double pos, double extent) {
    double denom = (extent > 1.0) ? extent : 1.0;
    double n = (pos * 2.0 / denom) - 1.0;
    if (n < -1.0) n = -1.0;
    if (n >  1.0) n =  1.0;
    return (int32_t)(n * 65536.0);
}

static void bloom_host_stamp_mac(void *block64, uint64_t seed, uint32_t *out_lo, uint32_t *out_hi) {
    uint64_t mac = bloom_hash56_fnv1a(block64, seed);
    *out_lo = (uint32_t)(mac & 0xFFFFFFFFULL);
    *out_hi = (uint32_t)(mac >> 32ULL);
}

static void bloom_host_put_u16_le(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xFFU);
    dst[1] = (uint8_t)((v >> 8U) & 0xFFU);
}

static void bloom_host_put_u32_le(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xFFU);
    dst[1] = (uint8_t)((v >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((v >> 16U) & 0xFFU);
    dst[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

bool bloom_gpu_push_audio_i16(BloomGpuCtx *ctx,
                              const int16_t *stereo_pcm,
                              uint32_t sample_frames) {
    if (!ctx || !ctx->audio_stream || !stereo_pcm || sample_frames == 0U) {
        return false;
    }
    if (SDL_GetAudioStreamQueued(ctx->audio_stream) > (int)(BLOOM_AUDIO_SAMPLE_RATE / 4U) * 4) {
        return true;
    }
    return SDL_PutAudioStreamData(ctx->audio_stream, stereo_pcm, (int)(sample_frames * 2U * sizeof(int16_t)));
}

bool bloom_write_wav_i16(const char *wav_path,
                         const int16_t *stereo_pcm,
                         uint32_t sample_frames,
                         uint32_t sample_rate_hz) {
    if (!wav_path || wav_path[0] == '\0' || !stereo_pcm || sample_frames == 0U) {
        return false;
    }
    uint32_t sr = (sample_rate_hz >= 8000U) ? sample_rate_hz : BLOOM_AUDIO_SAMPLE_RATE;
    uint32_t data_bytes = sample_frames * 4U;
    uint8_t hdr_bytes[44];
    memcpy(hdr_bytes + 0U, "RIFF", 4U);
    bloom_host_put_u32_le(hdr_bytes + 4U, 36U + data_bytes);
    memcpy(hdr_bytes + 8U, "WAVEfmt ", 8U);
    bloom_host_put_u32_le(hdr_bytes + 16U, 16U);
    bloom_host_put_u16_le(hdr_bytes + 20U, 1U);  /* PCM */
    bloom_host_put_u16_le(hdr_bytes + 22U, 2U);  /* stereo */
    bloom_host_put_u32_le(hdr_bytes + 24U, sr);
    bloom_host_put_u32_le(hdr_bytes + 28U, sr * 4U);
    bloom_host_put_u16_le(hdr_bytes + 32U, 4U);
    bloom_host_put_u16_le(hdr_bytes + 34U, 16U);
    memcpy(hdr_bytes + 36U, "data", 4U);
    bloom_host_put_u32_le(hdr_bytes + 40U, data_bytes);

    FILE *fp = fopen(wav_path, "wb");
    if (!fp) return false;
    bool ok = (fwrite(hdr_bytes, 1U, 44U, fp) == 44U) &&
              (fwrite(stereo_pcm, 4U, sample_frames, fp) == sample_frames);
    fclose(fp);
    return ok;
}

bool bloom_export_webgpu_html(const char *html_path,
                              const BloomEngineArena *arena,
                              const char *wgsl_path) {
    if (!html_path || html_path[0] == '\0' || !arena) {
        return false;
    }
    size_t wgsl_sz = 0U;
    uint8_t *wgsl_raw = NULL;
    if (wgsl_path && wgsl_path[0] != '\0' && bloom_str_ends_with(wgsl_path, ".wgsl")) {
        wgsl_raw = bloom_try_read_file(wgsl_path, &wgsl_sz);
    }
    if (!wgsl_raw) {
        wgsl_raw = bloom_try_read_file("shaders/bloom_core.wgsl", &wgsl_sz);
    }
    if (!wgsl_raw) {
        return false;
    }

    FILE *fp = fopen(html_path, "wb");
    if (!fp) {
        SDL_free(wgsl_raw);
        return false;
    }

    uint32_t w_px = (arena->map.frame_hdr.width_px >= 160U && arena->map.frame_hdr.width_px <= 4096U)
        ? arena->map.frame_hdr.width_px : 1280U;
    uint32_t h_px = (arena->map.frame_hdr.height_px >= 120U && arena->map.frame_hdr.height_px <= 4096U)
        ? arena->map.frame_hdr.height_px : 720U;

    fprintf(fp,
        "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
        "<title>BLOOM v%s WebGPU Standalone</title>\n"
        "<style>html,body{margin:0;height:100%%;background:#050810;color:#d6f2ff;font:13px/1.4 ui-monospace,monospace;"
        "display:flex;flex-direction:column;align-items:center;justify-content:center;overflow:hidden}"
        "canvas{max-width:100vw;max-height:94vh;box-shadow:0 12px 48px rgba(0,220,255,.18);border-radius:8px;cursor:grab}"
        "#bar{padding:6px 12px;opacity:.85}</style></head><body>\n"
        "<canvas id=\"c\" width=\"%u\" height=\"%u\"></canvas>\n"
        "<div id=\"bar\">BLOOM v%s WebGPU (16KB Arena SSOT, Drag to Orbit, Wheel to Zoom)</div>\n"
        "<script type=\"module\">\nconst WGSL = `",
        BLOOM_VERSION_STRING, w_px, h_px, BLOOM_VERSION_STRING);

    for (size_t i = 0U; i < wgsl_sz; ++i) {
        uint8_t ch = wgsl_raw[i];
        if (ch == '\\' || ch == '`' || ch == '$') {
            fputc('\\', fp);
        }
        fputc((int)ch, fp);
    }
    SDL_free(wgsl_raw);

    fprintf(fp, "`;\nconst ARENA_U32 = new Uint32Array([\n");
    const uint32_t *u32p = (const uint32_t *)arena->cells;
    for (size_t i = 0U; i < 4096U; ++i) {
        fprintf(fp, "0x%08x%s", u32p[i], (i + 1U < 4096U) ? ((i & 15U) == 15U ? ",\n" : ",") : "\n");
    }

    fputs(
        "]);\nconst BLIT = `@group(0) @binding(0) var srcTex : texture_2d<f32>;\n"
        "@vertex fn vs(@builtin(vertex_index) vi : u32) -> @builtin(position) vec4<f32> {\n"
        "  let p = array<vec2<f32>,3>(vec2<f32>(-1.0,-3.0),vec2<f32>(3.0,1.0),vec2<f32>(-1.0,1.0));\n"
        "  return vec4<f32>(p[vi],0.0,1.0);\n}\n"
        "@fragment fn fs(@builtin(position) pos : vec4<f32>) -> @location(0) vec4<f32> {\n"
        "  return textureLoad(srcTex, vec2<i32>(i32(pos.x), i32(pos.y)), 0);\n}`;\n"
        "async function main() {\n"
        "  const bar = document.getElementById('bar');\n"
        "  if (!navigator.gpu) { bar.textContent = 'WebGPU unavailable in this browser'; return; }\n"
        "  const adapter = await navigator.gpu.requestAdapter();\n"
        "  if (!adapter) { bar.textContent = 'No WebGPU adapter found'; return; }\n"
        "  const device = await adapter.requestDevice();\n"
        "  const canvas = document.getElementById('c');\n"
        "  const ctx = canvas.getContext('webgpu');\n"
        "  const fmt = navigator.gpu.getPreferredCanvasFormat();\n"
        "  ctx.configure({ device, format: fmt, alphaMode: 'opaque' });\n",
        fp);

    fprintf(fp,
        "  const W = %u, H = %u;\n"
        "  const arenaBuf = device.createBuffer({ size: 16384, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });\n"
        "  const meshBuf  = device.createBuffer({ size: 32768, usage: GPUBufferUsage.STORAGE });\n"
        "  const rcBuf    = device.createBuffer({ size: 524288, usage: GPUBufferUsage.STORAGE });\n"
        "  const outTex   = device.createTexture({ size: [W, H], format: 'rgba8unorm', usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });\n"
        "  device.queue.writeBuffer(arenaBuf, 0, ARENA_U32);\n"
        "  const csMod = device.createShaderModule({ code: WGSL });\n"
        "  const bgl0 = device.createBindGroupLayout({ entries: [\n"
        "    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },\n"
        "    { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } }\n"
        "  ]});\n"
        "  const bgl1 = device.createBindGroupLayout({ entries: [\n"
        "    { binding: 0, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: 'write-only', format: 'rgba8unorm' } },\n"
        "    { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } }\n"
        "  ]});\n"
        "  const playout = device.createPipelineLayout({ bindGroupLayouts: [bgl0, bgl1] });\n"
        "  const mkPipe = (ep) => device.createComputePipeline({ layout: playout, compute: { module: csMod, entryPoint: ep } });\n"
        "  const pRc2 = mkPipe('bloom_rc_cascade2'), pRc1 = mkPipe('bloom_rc_cascade1'), pRc0 = mkPipe('bloom_rc_cascade0'), pMain = mkPipe('bloom_render_frame');\n"
        "  const bg0 = device.createBindGroup({ layout: bgl0, entries: [{ binding: 0, resource: { buffer: arenaBuf } }, { binding: 1, resource: { buffer: meshBuf } }] });\n"
        "  const bg1 = device.createBindGroup({ layout: bgl1, entries: [{ binding: 0, resource: outTex.createView() }, { binding: 1, resource: { buffer: rcBuf } }] });\n"
        "  const vsMod = device.createShaderModule({ code: BLIT });\n"
        "  const rPipe = device.createRenderPipeline({ layout: 'auto', vertex: { module: vsMod, entryPoint: 'vs' }, fragment: { module: vsMod, entryPoint: 'fs', targets: [{ format: fmt }] } });\n"
        "  const rBg = device.createBindGroup({ layout: rPipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: outTex.createView() }] });\n",
        w_px, h_px);

    fputs(
        "  let yaw = (ARENA_U32[16 + 13] | 0) / 65536.0, pitch = (ARENA_U32[16 + 14] | 0) / 65536.0, dist = Math.max((ARENA_U32[16 + 15] | 0) / 65536.0, 2.0);\n"
        "  let drag = false, lx = 0, ly = 0;\n"
        "  canvas.onpointerdown = (e) => { drag = true; lx = e.clientX; ly = e.clientY; };\n"
        "  window.onpointerup = () => { drag = false; };\n"
        "  window.onpointermove = (e) => { if (!drag) return; yaw += (e.clientX - lx) * 0.008; pitch = Math.max(-1.25, Math.min(1.25, pitch + (e.clientY - ly) * 0.006)); lx = e.clientX; ly = e.clientY; };\n"
        "  canvas.onwheel = (e) => { e.preventDefault(); dist = Math.max(2.0, Math.min(14.0, dist + e.deltaY * 0.004)); };\n"
        "  function frame() {\n"
        "    ARENA_U32[5] = (ARENA_U32[5] + 1) >>> 0;\n"
        "    const tx = (ARENA_U32[16 + 3] | 0) / 65536.0, ty = (ARENA_U32[16 + 4] | 0) / 65536.0, tz = (ARENA_U32[16 + 5] | 0) / 65536.0;\n"
        "    const cp = Math.cos(pitch), sp = Math.sin(pitch), cy = Math.cos(yaw), sy = Math.sin(yaw);\n"
        "    ARENA_U32[16] = Math.round((tx + dist * cp * sy) * 65536) | 0;\n"
        "    ARENA_U32[17] = Math.round((ty + dist * sp) * 65536) | 0;\n"
        "    ARENA_U32[18] = Math.round((tz - dist * cp * cy) * 65536) | 0;\n"
        "    device.queue.writeBuffer(arenaBuf, 0, ARENA_U32, 0, 32);\n"
        "    const enc = device.createCommandEncoder();\n"
        "    const cpPass = enc.beginComputePass();\n"
        "    cpPass.setBindGroup(0, bg0); cpPass.setBindGroup(1, bg1);\n"
        "    if ((ARENA_U32[14] & 32) !== 0) {\n"
        "      cpPass.setPipeline(pRc2); cpPass.dispatchWorkgroups(64, 1, 1);\n"
        "      cpPass.setPipeline(pRc1); cpPass.dispatchWorkgroups(64, 4, 1);\n"
        "      cpPass.setPipeline(pRc0); cpPass.dispatchWorkgroups(32, 1, 1);\n"
        "    }\n"
        "    cpPass.setPipeline(pMain); cpPass.dispatchWorkgroups(Math.ceil(W / 8), Math.ceil(H / 8), 1);\n"
        "    cpPass.end();\n"
        "    const rp = enc.beginRenderPass({ colorAttachments: [{ view: ctx.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: {r:0,g:0,b:0,a:1} }] });\n"
        "    rp.setPipeline(rPipe); rp.setBindGroup(0, rBg); rp.draw(3); rp.end();\n"
        "    device.queue.submit([enc.finish()]);\n"
        "    requestAnimationFrame(frame);\n"
        "  }\n"
        "  requestAnimationFrame(frame);\n"
        "}\nmain();\n</script></body></html>\n",
        fp);

    bool ok = (ferror(fp) == 0);
    fclose(fp);
    return ok;
}

static void bloom_fmt_smpte(uint32_t frame_idx, uint32_t fps, char *buf, size_t cap) {
    uint32_t rate = (fps > 0U) ? fps : 24U;
    uint32_t f = frame_idx % rate;
    uint32_t tot_sec = frame_idx / rate;
    uint32_t s = tot_sec % 60U;
    uint32_t m = (tot_sec / 60U) % 60U;
    uint32_t h = (tot_sec / 3600U) + 1U;
    snprintf(buf, cap, "%02u:%02u:%02u:%02u", h, m, s, f);
}

bool bloom_write_cmx3600_edl(const char *edl_path,
                             const char *reel_title,
                             uint32_t total_frames,
                             uint32_t fps) {
    if (!edl_path || edl_path[0] == '\0') {
        return false;
    }
    FILE *fp = fopen(edl_path, "wb");
    if (!fp) {
        return false;
    }
    uint32_t rate = (fps > 0U) ? fps : 24U;
    uint32_t frames = (total_frames >= 4U) ? total_frames : 96U;
    const char *title = (reel_title && reel_title[0] != '\0') ? reel_title : "BLOOM_V9_REEL";
    static const char *const k_vcam_clips[4] = {
        "VCAM_01_ORBIT_TELEPHOTO_135MM",
        "VCAM_02_CHASE_DYNAMIC_24MM",
        "VCAM_03_GEODESIC_DOLLY_35MM",
        "VCAM_04_TACTICAL_OPTICS_18MM"
    };

    fprintf(fp, "TITLE: %s\nFCM: NON-DROP FRAME\n\n", title);
    uint32_t rec_cur = 0U;
    for (uint32_t e = 0U; e < 4U; ++e) {
        uint32_t next_f = (e == 3U) ? frames : ((frames * (e + 1U)) / 4U);
        char s_in[16], s_out[16], r_in[16], r_out[16];
        bloom_fmt_smpte(0U, rate, s_in, sizeof(s_in));
        bloom_fmt_smpte(next_f - rec_cur, rate, s_out, sizeof(s_out));
        bloom_fmt_smpte(rec_cur, rate, r_in, sizeof(r_in));
        bloom_fmt_smpte(next_f, rate, r_out, sizeof(r_out));
        fprintf(fp, "%03u  AX       V     C        %s %s %s %s\n* FROM CLIP NAME: %s\n",
                e + 1U, s_in, s_out, r_in, r_out, k_vcam_clips[e]);
        rec_cur = next_f;
    }
    bool ok = (ferror(fp) == 0);
    fclose(fp);
    return ok;
}

bool bloom_gpu_write_kitty_tty(const BloomGpuCtx *ctx,
                               FILE *out_stream,
                               uint32_t max_cols_px) {
    if (!out_stream) {
        return false;
    }
    const uint8_t *rgba = bloom_gpu_map_frame_rgba8(ctx);
    if (!rgba) {
        return false;
    }

    uint32_t max_w = (max_cols_px >= 16U && max_cols_px <= 1920U) ? max_cols_px : 160U;
    uint32_t step = 1U;
    while (step < ctx->width && (ctx->width / step) > max_w) {
        step++;
    }
    uint32_t ow = ctx->width / step;
    uint32_t oh = ctx->height / step;
    if (ow == 0U) ow = 1U;
    if (oh == 0U) oh = 1U;

    size_t rgb_len = (size_t)ow * (size_t)oh * 3U;
    uint8_t *rgb = (uint8_t *)malloc(rgb_len);
    size_t b64_cap = ((rgb_len + 2U) / 3U) * 4U + 1U;
    char *b64 = (char *)malloc(b64_cap);
    if (!rgb || !b64) {
        free(rgb);
        free(b64);
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->download_tbuf);
        return false;
    }

    for (uint32_t y = 0U; y < oh; ++y) {
        uint32_t sy = y * step;
        if (sy >= ctx->height) sy = ctx->height - 1U;
        for (uint32_t x = 0U; x < ow; ++x) {
            uint32_t sx = x * step;
            if (sx >= ctx->width) sx = ctx->width - 1U;
            size_t si = ((size_t)sy * (size_t)ctx->width + (size_t)sx) * 4U;
            size_t di = ((size_t)y * (size_t)ow + (size_t)x) * 3U;
            rgb[di + 0U] = rgba[si + 0U];
            rgb[di + 1U] = rgba[si + 1U];
            rgb[di + 2U] = rgba[si + 2U];
        }
    }
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->download_tbuf);

    static const char k_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t bp = 0U;
    for (size_t i = 0U; i < rgb_len; i += 3U) {
        uint32_t b0 = rgb[i];
        uint32_t b1 = (i + 1U < rgb_len) ? rgb[i + 1U] : 0U;
        uint32_t b2 = (i + 2U < rgb_len) ? rgb[i + 2U] : 0U;
        uint32_t tri = (b0 << 16U) | (b1 << 8U) | b2;
        b64[bp++] = k_b64[(tri >> 18U) & 63U];
        b64[bp++] = k_b64[(tri >> 12U) & 63U];
        b64[bp++] = (i + 1U < rgb_len) ? k_b64[(tri >> 6U) & 63U] : '=';
        b64[bp++] = (i + 2U < rgb_len) ? k_b64[tri & 63U] : '=';
    }
    b64[bp] = '\0';

    size_t off = 0U;
    bool first = true;
    while (off < bp) {
        size_t chunk = bp - off;
        if (chunk > 4096U) chunk = 4096U;
        uint32_t more = (off + chunk < bp) ? 1U : 0U;
        if (first) {
            fprintf(out_stream, "\x1b_Ga=T,f=24,s=%u,v=%u,m=%u;", ow, oh, more);
            first = false;
        } else {
            fprintf(out_stream, "\x1b_Gm=%u;", more);
        }
        (void)fwrite(b64 + off, 1U, chunk, out_stream);
        fputs("\x1b\\\n", out_stream);
        off += chunk;
    }

    free(b64);
    free(rgb);
    return (ferror(out_stream) == 0);
}

int bloom_gpu_run_window(BloomGpuCtx *ctx,
                         BloomEngineArena *arena,
                         void (*step_cb)(void *),
                         void *user_ctx) {
    if (!ctx || !arena || !ctx->device) {
        return -1;
    }

    if (!ctx->audio_stream && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = (int)BLOOM_AUDIO_SAMPLE_RATE;
        ctx->audio_stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (ctx->audio_stream) {
            (void)SDL_ResumeAudioStreamDevice(ctx->audio_stream);
        }
    }

    SDL_Window *win = SDL_CreateWindow(
        "BLOOM v9.0.0 Studio",
        (int)ctx->width,
        (int)ctx->height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) {
        fprintf(stderr, "[BLOOM SDL3-GPU] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    if (!SDL_ClaimWindowForGPUDevice(ctx->device, win)) {
        fprintf(stderr, "[BLOOM SDL3-GPU] SDL_ClaimWindowForGPUDevice failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        return -1;
    }

    (void)SDL_StartTextInput(win);

    BloomFrameHeader64 *hdr = &arena->cells[BLOOM_SLOT_HEADER].frame_hdr;
    BloomDomState64 *dom = &arena->cells[BLOOM_SLOT_DOM].dom;
    BloomWindow64 *wcell = &arena->cells[BLOOM_SLOT_WINDOW].window;
    BloomTextInput64 *tinput = &arena->cells[BLOOM_SLOT_TEXT_INPUT].text_input;
    BloomIme64 *ime = &arena->cells[BLOOM_SLOT_IME].ime;

    int lw = (int)ctx->width;
    int lh = (int)ctx->height;
    int pw = lw;
    int ph = lh;
    (void)SDL_GetWindowSize(win, &lw, &lh);
    (void)SDL_GetWindowSizeInPixels(win, &pw, &ph);
    if (pw > 0 && ph > 0 && ((uint32_t)pw != ctx->width || (uint32_t)ph != ctx->height)) {
        if (bloom_gpu_alloc_frame_targets(ctx, (uint32_t)pw, (uint32_t)ph)) {
            hdr->width_px = (uint32_t)pw;
            hdr->height_px = (uint32_t)ph;
        }
    }
    wcell->logical_w_px = (lw > 0) ? (uint32_t)lw : ctx->width;
    wcell->logical_h_px = (lh > 0) ? (uint32_t)lh : ctx->height;
    wcell->fb_w_px = ctx->width;
    wcell->fb_h_px = ctx->height;

    (void)bloom_gpu_render(ctx, true);

    bool running = true;
    int32_t prev_px = 0;
    int32_t prev_py = 0;
    bool has_prev = false;

    while (running) {
        wcell->host_flags &= ~(BLOOM_WIN_FLAG_JUST_CLICKED | BLOOM_WIN_FLAG_RIGHT_CLICK);
        wcell->drag_dx_q16 = 0;
        wcell->drag_dy_q16 = 0;
        wcell->scroll_dy_q16 = 0;
        wcell->key_code = 0U;

        (void)SDL_GetWindowSize(win, &lw, &lh);
        if (lw > 0 && lh > 0) {
            wcell->logical_w_px = (uint32_t)lw;
            wcell->logical_h_px = (uint32_t)lh;
        }
        double vw = (lw > 1) ? (double)lw : (double)ctx->width;
        double vh = (lh > 1) ? (double)lh : (double)ctx->height;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            } else if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                int npw = ev.window.data1;
                int nph = ev.window.data2;
                if (npw > 0 && nph > 0 &&
                    ((uint32_t)npw != ctx->width || (uint32_t)nph != ctx->height)) {
                    (void)SDL_WaitForGPUIdle(ctx->device);
                    if (bloom_gpu_alloc_frame_targets(ctx, (uint32_t)npw, (uint32_t)nph)) {
                        hdr->width_px = (uint32_t)npw;
                        hdr->height_px = (uint32_t)nph;
                        wcell->fb_w_px = (uint32_t)npw;
                        wcell->fb_h_px = (uint32_t)nph;
                        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                    }
                }
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                int32_t nx = bloom_norm_coord_q16((double)ev.motion.x, vw);
                int32_t ny = bloom_norm_coord_q16((double)ev.motion.y, vh);
                if (!has_prev) {
                    prev_px = nx;
                    prev_py = ny;
                    has_prev = true;
                }
                if (nx != prev_px || ny != prev_py) {
                    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                }
                wcell->drag_dx_q16 += (nx - prev_px);
                wcell->drag_dy_q16 += (ny - prev_py);
                wcell->pointer_x_q16 = nx;
                wcell->pointer_y_q16 = ny;
                prev_px = nx;
                prev_py = ny;
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                int32_t nx = bloom_norm_coord_q16((double)ev.button.x, vw);
                int32_t ny = bloom_norm_coord_q16((double)ev.button.y, vh);
                wcell->pointer_x_q16 = nx;
                wcell->pointer_y_q16 = ny;
                prev_px = nx;
                prev_py = ny;
                has_prev = true;
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    wcell->host_flags |= (BLOOM_WIN_FLAG_MOUSE_DOWN | BLOOM_WIN_FLAG_JUST_CLICKED);
                } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                    wcell->host_flags |= BLOOM_WIN_FLAG_RIGHT_CLICK;
                }
                hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    wcell->host_flags &= ~BLOOM_WIN_FLAG_MOUSE_DOWN;
                }
                hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                double wy = (double)ev.wheel.y;
                if (wy < -32.0) wy = -32.0;
                if (wy >  32.0) wy =  32.0;
                wcell->scroll_dy_q16 = (int32_t)(wy * 1024.0);
                hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_ESCAPE) {
                    if (tinput->focused != 0U) {
                        tinput->focused = 0U;
                        bloom_host_stamp_mac(tinput, 0x7EC70064ULL, &tinput->hash_lo32, &tinput->hash_hi32);
                        dom->focused_input_id = 0U;
                        bloom_host_stamp_mac(dom, 0xD0A00064ULL, &dom->mac_lo32, &dom->mac_hi32);
                        (void)bloom_ime_set_preedit64(ime, "", 0U);
                        wcell->key_code = 27U;
                        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                    } else {
                        running = false;
                    }
                } else if (ev.key.key == SDLK_BACKSPACE) {
                    wcell->key_code = 8U;
                    if (tinput->focused != 0U) {
                        (void)bloom_text_input_backspace64(tinput);
                        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                    }
                } else if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) {
                    wcell->key_code = 13U;
                    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                } else {
                    wcell->key_code = (uint32_t)ev.key.key;
                    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                }
            } else if (ev.type == SDL_EVENT_TEXT_EDITING) {
                if (tinput->focused != 0U) {
                    const char *pre = ev.edit.text ? ev.edit.text : "";
                    (void)bloom_ime_set_preedit64(ime, pre, 0U);
                    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                }
            } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                if (tinput->focused != 0U) {
                    (void)bloom_ime_set_preedit64(ime, "", 0U);
                    if (ev.text.text) {
                        for (const char *p = ev.text.text; *p != '\0'; ++p) {
                            (void)bloom_text_input_insert_char64(tinput, *p);
                        }
                    }
                    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                }
            }
        }

        if (!running) {
            break;
        }

        if (step_cb) {
            step_cb(user_ctx);
        }

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
        if (cmd) {
            SDL_GPUTexture *swap_tex = NULL;
            Uint32 swap_w = 0U;
            Uint32 swap_h = 0U;
            if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &swap_tex, &swap_w, &swap_h)) {
                if (swap_tex && swap_w > 0U && swap_h > 0U) {
                    SDL_GPUBlitInfo blit;
                    memset(&blit, 0, sizeof(blit));
                    blit.source.texture = ctx->frame_tex;
                    blit.source.w = ctx->width;
                    blit.source.h = ctx->height;
                    blit.destination.texture = swap_tex;
                    blit.destination.w = swap_w;
                    blit.destination.h = swap_h;
                    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                    blit.filter = SDL_GPU_FILTER_LINEAR;
                    SDL_BlitGPUTexture(cmd, &blit);
                }
                (void)SDL_SubmitGPUCommandBuffer(cmd);
            } else {
                (void)SDL_CancelGPUCommandBuffer(cmd);
            }
        }
    }

    wcell->host_flags |= BLOOM_WIN_FLAG_CLOSED;
    (void)SDL_StopTextInput(win);
    (void)SDL_WaitForGPUIdle(ctx->device);
    SDL_ReleaseWindowFromGPUDevice(ctx->device, win);
    SDL_DestroyWindow(win);
    return 0;
}

void bloom_gpu_destroy(BloomGpuCtx *ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->audio_stream) {
        SDL_DestroyAudioStream(ctx->audio_stream);
    }
    if (ctx->device) {
        (void)SDL_WaitForGPUIdle(ctx->device);
        if (ctx->download_tbuf) {
            SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->download_tbuf);
        }
        if (ctx->frame_tex) {
            SDL_ReleaseGPUTexture(ctx->device, ctx->frame_tex);
        }
        if (ctx->mesh_upload_tbuf) {
            SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->mesh_upload_tbuf);
        }
        if (ctx->rc_download_tbuf) {
            SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->rc_download_tbuf);
        }
        if (ctx->upload_tbuf) {
            SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->upload_tbuf);
        }
        if (ctx->rc_gpu_buf) {
            SDL_ReleaseGPUBuffer(ctx->device, ctx->rc_gpu_buf);
        }
        if (ctx->mesh_gpu_buf) {
            SDL_ReleaseGPUBuffer(ctx->device, ctx->mesh_gpu_buf);
        }
        if (ctx->arena_gpu_buf) {
            SDL_ReleaseGPUBuffer(ctx->device, ctx->arena_gpu_buf);
        }
        if (ctx->pipe_node_dst) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_node_dst);
        }
        if (ctx->pipe_info_sim) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_info_sim);
        }
        if (ctx->pipe_faultsim) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_faultsim);
        }
        if (ctx->pipe_searu) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_searu);
        }
        if (ctx->pipe_fir1_bs) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_fir1_bs);
        }
        if (ctx->pipe_fir1) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_fir1);
        }
        if (ctx->pipe_rc0) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_rc0);
        }
        if (ctx->pipe_rc1) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_rc1);
        }
        if (ctx->pipe_rc2) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipe_rc2);
        }
        if (ctx->pipeline) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipeline);
        }
        SDL_DestroyGPUDevice(ctx->device);
    }
    free(ctx);
}
