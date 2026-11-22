#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bloom.h"
#include "flow_ir.h"

struct BloomGpuCtx {
    BloomEngineArena       *arena;
    SDL_GPUDevice          *device;
    SDL_GPUComputePipeline *pipeline;
    SDL_GPUComputePipeline *pipe_rc2;
    SDL_GPUComputePipeline *pipe_rc1;
    SDL_GPUComputePipeline *pipe_rc0;
    SDL_GPUComputePipeline *pipe_fir1;
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
    SDL_free(code);
    if (!ctx->pipeline || !ctx->pipe_rc2 || !ctx->pipe_rc1 || !ctx->pipe_rc0 || !ctx->pipe_fir1) {
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
    for (uint32_t i = 0U; i < prog->num_inputs; ++i) {
        uint64_t wm = flow_ir_mask_u64(prog->vals[i].width);
        uint64_t iv = cfg->base_inputs[i];
        uint32_t sh = (i < 31U) ? i : 31U;
        if (((cfg->vary_input_mask >> sh) & 1U) != 0U) {
            if (cfg->sweep_mode == 1U) {
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

    const uint32_t max_chunk_trials = UINT32_C(4194304); /* 4M trials per GPU command buffer */
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

        void *mapped_up = SDL_MapGPUTransferBuffer(ctx->device, ctx->upload_tbuf, false);
        if (!mapped_up) {
            ok = false;
            break;
        }
        memcpy(mapped_up, h_arena, BLOOM_ARENA_BYTES);
        SDL_UnmapGPUTransferBuffer(ctx->device, ctx->upload_tbuf);

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
        if (!cmd) {
            ok = false;
            break;
        }

        SDL_GPUCopyPass *up_pass = SDL_BeginGPUCopyPass(cmd);
        if (!up_pass) {
            SDL_CancelGPUCommandBuffer(cmd);
            ok = false;
            break;
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
        SDL_GPUComputePass *cpass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1U, &rw_buf, 1U);
        if (!cpass) {
            SDL_CancelGPUCommandBuffer(cmd);
            ok = false;
            break;
        }
        SDL_GPUBuffer *ro_bufs[2] = { ctx->arena_gpu_buf, ctx->mesh_gpu_buf };
        SDL_BindGPUComputePipeline(cpass, ctx->pipe_fir1);
        SDL_BindGPUComputeStorageBuffers(cpass, 0U, ro_bufs, 2U);
        SDL_DispatchGPUCompute(cpass, num_wgs, 1U, 1U);
        SDL_EndGPUComputePass(cpass);

        SDL_GPUCopyPass *dl_pass = SDL_BeginGPUCopyPass(cmd);
        if (!dl_pass) {
            SDL_CancelGPUCommandBuffer(cmd);
            ok = false;
            break;
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
            ok = false;
            break;
        }
        SDL_ReleaseGPUFence(ctx->device, fence);

        const uint32_t *wg_res = (const uint32_t *)SDL_MapGPUTransferBuffer(
            ctx->device, ctx->rc_download_tbuf, false);
        if (!wg_res) {
            ok = false;
            break;
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

    if (ok && out->found) {
        bloom_fir_build_trial_inputs(prog, cfg, out->first_hit_trial, out->witness_inputs);
        if (!flow_ir_eval_u64(prog, out->witness_inputs, out->witness_outputs)) {
            ok = false;
        } else {
            uint64_t target = ((uint64_t)cfg->target_hi << 32U) | (uint64_t)cfg->target_lo;
            uint64_t got = (prog->num_outputs > 0U) ? out->witness_outputs[0] : 0ULL;
            bool match = (cfg->target_mode == 0U) ? (got == target) :
                         ((cfg->target_mode == 1U) ? (got != target) : true);
            out->cpu_verified = match;
            if (!match) {
                ok = false;
            }
        }
    }

    free(h_mesh);
    free(h_arena);
    free(prog);
    return ok;
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
