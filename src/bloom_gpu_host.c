#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bloom.h"

struct BloomGpuCtx {
    BloomEngineArena       *arena;
    SDL_GPUDevice          *device;
    SDL_GPUComputePipeline *pipeline;
    SDL_GPUBuffer          *arena_gpu_buf;
    SDL_GPUTransferBuffer  *upload_tbuf;
    SDL_GPUTexture         *frame_tex;
    SDL_GPUTransferBuffer  *download_tbuf;
    uint32_t                width;
    uint32_t                height;
    uint32_t                dispatch_count;
    uint32_t                idle_skip_count;
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
    pipe_info.num_readonly_storage_buffers = 1U;
    pipe_info.num_readwrite_storage_textures = 1U;
    pipe_info.num_readwrite_storage_buffers = 0U;
    pipe_info.num_uniform_buffers = 0U;
    pipe_info.threadcount_x = 8U;
    pipe_info.threadcount_y = 8U;
    pipe_info.threadcount_z = 1U;

    ctx->pipeline = SDL_CreateGPUComputePipeline(dev, &pipe_info);
    SDL_free(code);
    if (!ctx->pipeline) {
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

    SDL_GPUTransferBufferCreateInfo up_info;
    memset(&up_info, 0, sizeof(up_info));
    up_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    up_info.size = (Uint32)BLOOM_ARENA_BYTES;
    ctx->upload_tbuf = SDL_CreateGPUTransferBuffer(dev, &up_info);

    if (!ctx->arena_gpu_buf || !ctx->upload_tbuf || !bloom_gpu_alloc_frame_targets(ctx, w, h)) {
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

bool bloom_gpu_render(BloomGpuCtx *ctx, bool force_render) {
    if (!ctx || !ctx->arena || !ctx->device || !ctx->pipeline ||
        !ctx->arena_gpu_buf || !ctx->upload_tbuf || !ctx->frame_tex) {
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

    SDL_GPUComputePass *comp_pass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1U, NULL, 0U);
    if (!comp_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    SDL_BindGPUComputePipeline(comp_pass, ctx->pipeline);
    SDL_GPUBuffer *ro_bufs[1] = { ctx->arena_gpu_buf };
    SDL_BindGPUComputeStorageBuffers(comp_pass, 0U, ro_bufs, 1U);
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

bool bloom_gpu_write_ppm(const BloomGpuCtx *ctx,
                         const char *ppm_path,
                         uint64_t *out_fnv_hash,
                         uint32_t *out_nonblack_px) {
    if (!ctx || !ctx->device || !ctx->frame_tex || !ctx->download_tbuf) {
        return false;
    }

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        return false;
    }
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
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
        return false;
    }
    bool waited = SDL_WaitForGPUFences(ctx->device, true, &fence, 1U);
    SDL_ReleaseGPUFence(ctx->device, fence);
    if (!waited) {
        return false;
    }

    const uint8_t *rgba = (const uint8_t *)SDL_MapGPUTransferBuffer(
        ctx->device, ctx->download_tbuf, false);
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

int bloom_gpu_run_window(BloomGpuCtx *ctx,
                         BloomEngineArena *arena,
                         void (*step_cb)(void *),
                         void *user_ctx) {
    if (!ctx || !arena || !ctx->device) {
        return -1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "BLOOM v6.0.0 Studio",
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
                wcell->key_code = (uint32_t)ev.key.key;
                if (ev.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (ev.key.key == SDLK_BACKSPACE) {
                    (void)bloom_text_input_backspace64(tinput);
                    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                }
            } else if (ev.type == SDL_EVENT_TEXT_EDITING) {
                const char *pre = ev.edit.text ? ev.edit.text : "";
                (void)bloom_ime_set_preedit64(ime, pre, 0U);
                hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
            } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                (void)bloom_ime_set_preedit64(ime, "", 0U);
                if (ev.text.text) {
                    for (const char *p = ev.text.text; *p != '\0'; ++p) {
                        (void)bloom_text_input_insert_char64(tinput, *p);
                    }
                }
                hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
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
    if (ctx->device) {
        (void)SDL_WaitForGPUIdle(ctx->device);
        if (ctx->download_tbuf) {
            SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->download_tbuf);
        }
        if (ctx->frame_tex) {
            SDL_ReleaseGPUTexture(ctx->device, ctx->frame_tex);
        }
        if (ctx->upload_tbuf) {
            SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->upload_tbuf);
        }
        if (ctx->arena_gpu_buf) {
            SDL_ReleaseGPUBuffer(ctx->device, ctx->arena_gpu_buf);
        }
        if (ctx->pipeline) {
            SDL_ReleaseGPUComputePipeline(ctx->device, ctx->pipeline);
        }
        SDL_DestroyGPUDevice(ctx->device);
    }
    free(ctx);
}
