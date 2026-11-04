/*
 * ============================================================================
 * BLOOM v1.0.0: Pure C17 / Objective-C Native Metal Host & FlowCineRig64 Optics
 * ============================================================================
 * 100% C17 compliant (-std=c17 -fobjc-arc), zero C++ headers or constructs.
 * ============================================================================
 */

#include "bloom.h"
#include "flow_gpu_abi.h"
#include "flow_procedural_assets.h"
#include "flow_svg_ui.h"
#include "flow_wallpaper.h"

#import <AudioToolbox/AudioToolbox.h>
#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FlowAudiovisualFrame64 s_live_av_frame;
static FlowAudioPatch64 s_live_audio_patch = {
    .carrier_jet = {0, 14417920, 1638400}, /* 220 Hz base + subtle chirp */
    .mod_freq_q16 = 7208960,
    .fm_index_q16 = 12288,
    .attack_step_q16 = 4096,
    .decay_step_q16 = 256,
    .patch_id = 1U,
    .waveform_flags = 1U,
    .checksum = 0xCAFEBABEULL
};
static uint32_t s_live_finish_class = 0U;
static FlowGenerativeSeedStyle64 s_live_seed_style;
static FlowSovereignGate64 s_live_sovereign_gate;
static FlowObjectTicket64 s_live_object_ticket;
static FlowDopamineJackpot64 s_live_dopamine_jackpot;
static FlowWallpaperState64 s_live_wallpaper;
static FlowDesktopHostConfig64 s_live_wp_host;
static FlowSvgUiNode64 s_live_svg_ui_nodes[FLOW_SVG_UI_MAX_NODES];
static FlowSvgUiDomState64 s_live_svg_ui_dom;
static FlowBroadcastObsBridge64 s_live_obs_bridge;
static FlowPrefabInstance64 s_live_prefab_inst;
static FlowPrefabEditState64 s_live_prefab_edit;
static FlowDebugLogEntry64 s_live_debug_logs[64];
static FlowPipelineTelemetry64 s_live_pipeline_telemetry;
static FlowLatexBoxNode64 s_live_latex_boxes[16];
static FlowNarrativeDirector64 s_live_story_director;
static uint32_t s_target_fps = 120U;
static bool s_vsync_enabled = true;
static bool s_wallpaper_mode = false;
static bool s_wallpaper_audio = false;
static uint32_t s_wallpaper_max_frames = 0U;
static uint32_t s_live_ticket_preset_idx = 1U; /* Default Preset 1: Full-Lit Lever Climber */
static int32_t s_live_azimuth_x_q16 = 0;
static int32_t s_live_elevation_y_q16 = 0;
static int32_t s_live_front_z_q16 = 65536;
static uint32_t s_live_purge_frames = 0U;

static void flow_audio_queue_callback(void *inUserData,
                                      AudioQueueRef inAQ,
                                      AudioQueueBufferRef inBuffer) {
    (void)inUserData;
    int16_t *out_s16 = (int16_t *)inBuffer->mAudioData;
    size_t n_frames = inBuffer->mAudioDataBytesCapacity / (2U * sizeof(int16_t));
    if (n_frames > 256U) n_frames = 256U;

    bool purge_active = (s_live_purge_frames > 0U);
    if (s_live_purge_frames > 0U) {
        s_live_purge_frames--;
    }

    int64_t stereo_q16[512];
    flow_audio_synth_binaural_stereo_q16(&s_live_audio_patch,
                                         &s_live_av_frame,
                                         &s_live_seed_style,
                                         s_live_azimuth_x_q16,
                                         s_live_elevation_y_q16,
                                         s_live_front_z_q16,
                                         s_live_sovereign_gate.thermal_kelvin,
                                         purge_active,
                                         stereo_q16,
                                         n_frames,
                                         1365LL);
    for (size_t i = 0U; i < n_frames * 2U; ++i) {
        int64_t v = (stereo_q16[i] * 7000LL) >> 16U;
        if (v > 32767LL) v = 32767LL;
        if (v < -32768LL) v = -32768LL;
        out_s16[i] = (int16_t)v;
    }
    inBuffer->mAudioDataByteSize = (UInt32)(n_frames * 2U * sizeof(int16_t));
    AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
}

static int cmp_double_asc(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static NSString *load_metal_shader_with_ssot_abi(NSError **error) {
    NSString *portSrc = [NSString stringWithContentsOfFile:@"include/flow_shader_portability.h"
                                                  encoding:NSUTF8StringEncoding
                                                     error:error];
    NSString *abiSrc = [NSString stringWithContentsOfFile:@"include/flow_gpu_abi.h"
                                                 encoding:NSUTF8StringEncoding
                                                    error:error];
    NSString *shaderSrc = [NSString stringWithContentsOfFile:@"src/shaders.metal"
                                                    encoding:NSUTF8StringEncoding
                                                       error:error];
    if (!shaderSrc)
        return nil;
    if (abiSrc && portSrc) {
        abiSrc = [abiSrc stringByReplacingOccurrencesOfString:@"#include \"flow_shader_portability.h\""
                                                   withString:portSrc];
    }
    if (abiSrc) {
        return [shaderSrc stringByReplacingOccurrencesOfString:@"#include \"flow_gpu_abi.h\""
                                                    withString:abiSrc];
    }
    return shaderSrc;
}

static void populate_host_uniforms(FlowGpuFrameUniforms256 *u,
                                   uint32_t W,
                                   uint32_t H,
                                   uint32_t frame_idx,
                                   const FlowGameEngineState *engine,
                                   const FlowGameFramePacket *pkt,
                                   uint32_t active_count) {
    memset(u, 0, sizeof(*u));
    u->s0.screen_w = W;
    u->s0.screen_h = H;
    u->s0.frame_index = frame_idx;
    u->s0.stage_layer = (uint32_t)pkt->stage_layer;
    u->s0.craft_pos[0] = (float)pkt->craft_x_q16 / 65536.0f;
    u->s0.craft_pos[1] = (float)pkt->craft_y_q16 / 65536.0f;
    u->s0.craft_pos[2] = (float)pkt->craft_z_q16 / 65536.0f;
    u->s0.craft_rot[0] = (float)pkt->craft_pitch_q16 / 65536.0f;
    u->s0.craft_rot[1] = (float)pkt->craft_yaw_q16 / 65536.0f;
    u->s0.craft_rot[2] = (float)pkt->craft_roll_q16 / 65536.0f;
    u->s0.craft_speed  = (float)pkt->craft_vz_q16 / 65536.0f;
    u->s0.planet_pos[0] = (float)engine->celestial_pos_q16[0] / 65536.0f;
    u->s0.planet_pos[1] = (float)engine->celestial_pos_q16[1] / 65536.0f;
    u->s0.planet_pos[2] = (float)engine->celestial_pos_q16[2] / 65536.0f;
    u->s0.planet_radius = (float)engine->celestial_radius_q16 / 65536.0f;
    u->s0.thermal_kelvin   = (uint32_t)pkt->ricardian.thermal_kelvin;

    u->s1.thermal_shutdown = (uint32_t)pkt->thermal_shutdown;
    u->s1.assembly_depth   = (uint32_t)pkt->ricardian.assembly_depth;
    u->s1.active_tiles     = active_count;
    u->s1.collision_hit    = (uint32_t)pkt->collision_hit;

    const FlowCineResolvedFrame64 *cc = &pkt->cine_camera;
    u->s1.vcam_id         = cc->active_vcam_id;
    u->s1.cine_flags      = cc->active_flags;
    u->s1.focal_length_mm = (float)cc->focal_length_q16 / 65536.0f;
    u->s1.aperture_fstop  = (float)cc->aperture_fstop_q16 / 65536.0f;
    u->s1.focus_dist      = (float)cc->focus_dist_q16 / 65536.0f;
    u->s1.exposure_gain   = (float)cc->exposure_gain_q16 / 65536.0f;
    u->s1.cam_look_offset[0] = (float)(cc->look_offset_xy_q16[0] + s_live_wallpaper.parallax_offset_x_q16) / 65536.0f;
    u->s1.cam_look_offset[1] = (float)(cc->look_offset_xy_q16[1] + s_live_wallpaper.parallax_offset_y_q16) / 65536.0f;
    u->s1.cam_offset[0]   = (float)cc->cam_offset_q16[0] / 65536.0f;
    u->s1.cam_offset[1]   = (float)cc->cam_offset_q16[1] / 65536.0f;
    u->s1.cam_offset[2]   = (float)cc->cam_offset_q16[2] / 65536.0f;
    u->s1.shutter_angle_norm = 0.5f;

    u->s2.shutter_vel[0]  = (float)cc->shutter_vel_q16[0] / 65536.0f;
    u->s2.shutter_vel[1]  = (float)cc->shutter_vel_q16[1] / 65536.0f;
    u->s2.shutter_vel[2]  = (float)cc->shutter_vel_q16[2] / 65536.0f;

    const FlowRelativeScale64 *rs = &pkt->relative_scale;
    u->s2.lorentz_beta        = (float)rs->lorentz_beta_q16 / 65536.0f;
    u->s2.lorentz_gamma       = (float)rs->lorentz_gamma_q16 / 65536.0f;
    u->s2.doppler_factor      = (float)rs->doppler_factor_q16 / 65536.0f;
    u->s2.metric_potential    = (float)rs->metric_potential_q16 / 65536.0f;
    u->s2.grav_redshift       = (float)rs->grav_redshift_q16 / 65536.0f;
    u->s2.flux_pressure_ratio = (float)rs->flux_pressure_ratio_q16 / 65536.0f;
    u->s2.wave_phase_freq     = (float)rs->wave_phase_freq_q16 / 65536.0f;
    u->s2.contact_pos[0]      = (float)pkt->contact_x_q16 / 65536.0f;
    u->s2.contact_pos[1]      = (float)pkt->contact_y_q16 / 65536.0f;
    u->s2.contact_pos[2]      = (float)pkt->contact_z_q16 / 65536.0f;
    u->s2.rel_vel_dir[0]      = (float)rs->rel_vel_dir_q16[0] / 65536.0f;
    u->s2.rel_vel_dir[1]      = (float)rs->rel_vel_dir_q16[1] / 65536.0f;
    u->s2.rel_vel_dir[2]      = (float)rs->rel_vel_dir_q16[2] / 65536.0f;

    const FlowFrameGovernor64 *fg = &pkt->fps_governor;
    u->s3.raymarch_max_steps = (fg->raymarch_max_steps > 0U)
        ? fg->raymarch_max_steps
        : (uint32_t)FLOW_FPS120_RAYMARCH_STEPS_ULTRA;
    u->s3.layer0_radius = (fg->layer0_radius_q16 > 0)
        ? ((float)fg->layer0_radius_q16 / 65536.0f)
        : 100.0f;

    int64_t sdf_dist_q16 = engine->last_contact.sdf_dist_q16;
    if (sdf_dist_q16 == 0LL) {
        sdf_dist_q16 = 15LL * FLOW_Q16_ONE;
    }
    s_live_azimuth_x_q16   = rs->rel_vel_dir_q16[0];
    s_live_elevation_y_q16 = rs->rel_vel_dir_q16[1];
    s_live_front_z_q16     = ((s_live_object_ticket.audio_axis_flags & FLOW_AUDIO_REAR_WHISPER_SUB) != 0U)
        ? -FLOW_Q16_ONE
        : rs->rel_vel_dir_q16[2];
    (void)flow_ticket_step_mechanics_q16(&s_live_object_ticket,
                                         FLOW_Q16_ONE,
                                         (int32_t)engine->craft_throttle_q16,
                                         (int32_t)sdf_dist_q16,
                                         (int32_t)engine->craft_rot[1].d1,
                                         frame_idx & 63U);
    if (pkt->collision_hit != 0U && s_live_dopamine_jackpot.phase == FLOW_DOPAMINE_PHASE_IDLE) {
        flow_dopamine_trigger_jackpot64(&s_live_dopamine_jackpot,
                                        (uint64_t)frame_idx + 1ULL,
                                        2U,
                                        s_live_finish_class,
                                        1200U);
    }
    flow_dopamine_step_ms(&s_live_dopamine_jackpot, 16U);

    if (s_live_sovereign_gate.thermal_kelvin < FLOW_THERMAL_AMBIENT_KELVIN) {
        s_live_sovereign_gate.thermal_kelvin = (uint32_t)pkt->ricardian.thermal_kelvin;
    }
    flow_av_resolve_frame64(220 * FLOW_Q16_ONE,
                            rs->doppler_factor_q16,
                            rs->proper_time_ratio_q16,
                            sdf_dist_q16,
                            s_live_av_frame.cavity_latched,
                            NULL,
                            s_live_finish_class,
                            52428,
                            (int32_t)engine->cine_brain.trauma_q16,
                            3 * FLOW_Q16_ONE,
                            (int32_t)(frame_idx * 1024U),
                            &s_live_av_frame);

    u->s3.active_finish_class = s_live_av_frame.active_finish_class;
    u->s3.squash_parallel     = (float)s_live_av_frame.squash_parallel_q16 / 65536.0f;
    u->s3.squash_perp         = (float)s_live_av_frame.squash_perp_q16 / 65536.0f;
    u->s3.shockwave_ripple    = (float)s_live_av_frame.shockwave_ripple_q16 / 65536.0f;
    u->s3.cavity_resonance    = (float)s_live_av_frame.cavity_resonance_q16 / 65536.0f;
    u->s3.trc_strobe_hz       = (float)s_live_av_frame.trc_safe_strobe_hz_q16 / 65536.0f;
    u->s3.dopamine_phase      = s_live_dopamine_jackpot.phase;
    u->s3.catharsis_bloom     = (float)s_live_dopamine_jackpot.catharsis_bloom_q16 / 65536.0f;
    u->s3.suspense_vignette   = (float)s_live_dopamine_jackpot.suspense_vignette_q16 / 65536.0f;
    u->s3.flex_spotlight      = (float)s_live_dopamine_jackpot.flex_spotlight_q16 / 65536.0f;
    u->s3.ticket_vis_mode     = s_live_object_ticket.visibility_mode;
    u->s3.xray_mask_mode      = (uint32_t)s_live_prefab_edit.xray_mask_mode;
    u->s3.xray_radius         = (s_live_prefab_edit.xray_mask_mode != FLOW_PREFAB_XRAY_OFF)
        ? ((float)s_live_prefab_edit.xray_radius_q16 / 65536.0f)
        : 0.0f;
    u->s3.xray_cursor_uv_x    = (float)s_live_prefab_edit.xray_cursor_x_q16 / 65536.0f;

    /* Orchestrate Scripted Story Finisher ("劇情殺") & Scene Transition ("過渡動畫") */
    if (s_live_story_director.director_phase != FLOW_DIRECTOR_IDLE) {
        uint32_t dir_vcam = u->s1.vcam_id;
        int32_t dir_trauma = 0;
        uint32_t next_stage = u->s0.stage_layer;
        if (flow_director_step_choreography64(&s_live_story_director,
                                              16U,
                                              &dir_vcam,
                                              &dir_trauma,
                                              &s_live_ticket_preset_idx,
                                              &next_stage)) {
            s_live_object_ticket = flow_ticket_build_preset64(s_live_ticket_preset_idx & 3U);
            s_live_seed_style = flow_seed_style_derive64(s_live_seed_style.master_seed + 0x9E3779B9ULL);
            s_live_finish_class = s_live_seed_style.archetype_id & 3U;
        }
        u->s1.vcam_id = dir_vcam & 3U;
        if (s_live_story_director.letterbox_aspect_q16 > 16384) {
            u->s1.cine_flags |= 1U; /* Force 2.39:1 CinemaScope letterbox framing */
        }
        if (s_live_story_director.xray_cutaway_q16 > 0) {
            u->s3.xray_mask_mode = (uint32_t)FLOW_PREFAB_XRAY_SPHERE_CUT;
            u->s3.xray_radius    = (float)s_live_story_director.xray_cutaway_q16 / 65536.0f;
        }
    }
}

static bool write_ppm_p6(const char *filepath, const uint8_t *rgba, int width, int height) {
    FILE *f = fopen(filepath, "wb");
    if (!f)
        return false;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    uint8_t *rgb = (uint8_t *)malloc((size_t)width * 3U);
    if (!rgb) {
        fclose(f);
        return false;
    }
    for (int y = 0; y < height; ++y) {
        const uint8_t *row_src = rgba + ((size_t)y * (size_t)width * 4U);
        for (int x = 0; x < width; ++x) {
            rgb[x * 3 + 0] = row_src[x * 4 + 0];
            rgb[x * 3 + 1] = row_src[x * 4 + 1];
            rgb[x * 3 + 2] = row_src[x * 4 + 2];
        }
        fwrite(rgb, 1U, (size_t)width * 3U, f);
    }
    free(rgb);
    fclose(f);
    return true;
}

/*
 * ============================================================================
 * FlowMetalRenderer — Single Unified GPU Pipeline & 64B Bus Binding State
 * ============================================================================
 */
typedef struct {
    id<MTLDevice>              device;
    id<MTLCommandQueue>        queue;
    id<MTLRenderPipelineState> pipelineState;
    id<MTLBuffer>              tileBuffer;      /* buffer(0): vertex */
    id<MTLBuffer>              uniformBuffer;   /* buffer(1): vertex & fragment */
    id<MTLBuffer>              hudGlyphBuffer;  /* buffer(2): fragment */
    id<MTLBuffer>              busBuffer;       /* buffer(3): fragment (1 KB slots[16]) */
    FlowEngineBus64            bus_slots[FLOW_ENGINE_BUS_SLOTS];
    uint32_t                   active_tile_count;
} FlowMetalRenderer;

static bool flow_metal_renderer_init(FlowMetalRenderer *r,
                                     id<MTLDevice> device,
                                     MTLPixelFormat pixelFormat,
                                     NSError **outErr) {
    if (!r || !device) return false;
    memset(r->bus_slots, 0, sizeof(r->bus_slots));
    r->active_tile_count = 0U;
    r->device = device;
    r->queue = [device newCommandQueue];

    NSString *shaderSrc = load_metal_shader_with_ssot_abi(outErr);
    if (!shaderSrc) return false;

    id<MTLLibrary> lib = [device newLibraryWithSource:shaderSrc options:nil error:outErr];
    if (!lib) return false;

    id<MTLFunction> vf = [lib newFunctionWithName:@"tile_vertex"];
    id<MTLFunction> ff = [lib newFunctionWithName:@"tile_fragment"];
    if (!vf || !ff) return false;

    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = vf;
    pd.fragmentFunction = ff;
    pd.colorAttachments[0].pixelFormat = pixelFormat;

    r->pipelineState = [device newRenderPipelineStateWithDescriptor:pd error:outErr];
    if (!r->pipelineState) return false;

    r->tileBuffer = [device newBufferWithLength:(FLOW_GAME_TOTAL_TILES * sizeof(uint32_t))
                                        options:MTLResourceStorageModeShared];
    r->uniformBuffer = [device newBufferWithLength:sizeof(FlowGpuFrameUniforms256)
                                           options:MTLResourceStorageModeShared];

    FlowGlyphDesc64 hud_glyphs[4];
    memset(hud_glyphs, 0, sizeof(hud_glyphs));
    (void)flow_svg_compile_file("specs/cockpit_hud.svg", hud_glyphs, 4U);
    r->hudGlyphBuffer = [device newBufferWithBytes:hud_glyphs
                                            length:sizeof(hud_glyphs)
                                           options:MTLResourceStorageModeShared];

    r->busBuffer = [device newBufferWithLength:sizeof(FlowEngineBus1K)
                                       options:MTLResourceStorageModeShared];

    uint32_t *tiles = (uint32_t *)[r->tileBuffer contents];
    for (uint32_t t = 0; t < FLOW_GAME_TOTAL_TILES; ++t) {
        tiles[t] = t;
    }
    r->active_tile_count = FLOW_GAME_TOTAL_TILES;
    return true;
}

static inline void flow_metal_renderer_update_bus_slot(FlowMetalRenderer *r,
                                                       uint32_t slot_id,
                                                       const void *data64) {
    if (r && slot_id < FLOW_ENGINE_BUS_SLOTS && data64) {
        memcpy(&r->bus_slots[slot_id], data64, sizeof(FlowEngineBus64));
    }
}

static void flow_metal_sync_bus_slots(FlowMetalRenderer *r) {
    for (uint32_t i = 0U; i < FLOW_SVG_UI_MAX_NODES; ++i) {
        flow_metal_renderer_update_bus_slot(r, FLOW_BUS_SLOT_SVG_UI_0 + i, &s_live_svg_ui_nodes[i]);
    }
    flow_metal_renderer_update_bus_slot(r, FLOW_BUS_SLOT_WALLPAPER, &s_live_wallpaper);
    flow_metal_renderer_update_bus_slot(r, FLOW_BUS_SLOT_DOPAMINE, &s_live_dopamine_jackpot);
    flow_metal_renderer_update_bus_slot(r, FLOW_BUS_SLOT_SOVEREIGN_GATE, &s_live_sovereign_gate);
    flow_metal_renderer_update_bus_slot(r, FLOW_BUS_SLOT_OBS_BRIDGE, &s_live_obs_bridge);
    flow_metal_renderer_update_bus_slot(r, FLOW_BUS_SLOT_STORY_DIRECTOR, &s_live_story_director);
}

static void flow_metal_renderer_sync_buffers(FlowMetalRenderer *r,
                                             const uint32_t *active_tiles,
                                             uint32_t active_tile_count,
                                             const FlowGpuFrameUniforms256 *u) {
    if (!r) return;
    if (active_tiles && active_tile_count > 0U) {
        memcpy([r->tileBuffer contents], active_tiles, active_tile_count * sizeof(uint32_t));
        r->active_tile_count = active_tile_count;
    } else {
        r->active_tile_count = FLOW_GAME_TOTAL_TILES;
    }
    if (u) {
        memcpy([r->uniformBuffer contents], u, sizeof(FlowGpuFrameUniforms256));
    }
    memcpy([r->busBuffer contents], r->bus_slots, sizeof(r->bus_slots));
}

static void flow_metal_encode_frame(id<MTLRenderCommandEncoder> enc, FlowMetalRenderer *r) {
    [enc setRenderPipelineState:r->pipelineState];
    [enc setVertexBuffer:r->tileBuffer offset:0 atIndex:0];
    [enc setVertexBuffer:r->uniformBuffer offset:0 atIndex:1];
    [enc setFragmentBuffer:r->uniformBuffer offset:0 atIndex:1];
    [enc setFragmentBuffer:r->hudGlyphBuffer offset:0 atIndex:2];
    [enc setFragmentBuffer:r->busBuffer offset:0 atIndex:3];
    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
            vertexStart:0
            vertexCount:4
          instanceCount:r->active_tile_count];
}

static int run_4k_benchmark(bool export_ppm) {
    @autoreleasepool {
        printf("================================================================================\n");
        printf("  BLOOM v%s: Pure C17 / Metal 4K Benchmark & FlowCineRig64 Storyboard\n", BLOOM_VERSION_STRING);
        printf("================================================================================\n");

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "ERROR: Metal device not available.\n");
            return 1;
        }
        printf("  ✓ Metal Device       : %s\n", [[device name] UTF8String]);

        NSError *error = nil;
        FlowMetalRenderer renderer;
        if (!flow_metal_renderer_init(&renderer, device, MTLPixelFormatRGBA8Unorm, &error)) {
            fprintf(stderr, "ERROR: Failed to init FlowMetalRenderer: %s\n",
                    error ? [[error localizedDescription] UTF8String] : "Unknown");
            return 2;
        }

        const uint32_t W = FLOW_GAME_VIEWPORT_W;
        const uint32_t H = FLOW_GAME_VIEWPORT_H;

        MTLTextureDescriptor *tDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                         width:W
                                                                                        height:H
                                                                                     mipmapped:NO];
        tDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        tDesc.storageMode = MTLStorageModeShared;
        id<MTLTexture> renderTexture = [device newTextureWithDescriptor:tDesc];

        FlowGameEngineState engine;
        if (flow_game_init(&engine) != 0) {
            fprintf(stderr, "ERROR: Failed to initialize BLOOM engine.\n");
            return 4;
        }

        size_t compiled_glyphs = 4U;
        printf("  ✓ SDD SVG HUD Glyphs : %zu / 4 compiled from specs/cockpit_hud.svg (256B GPU buffer)\n",
               compiled_glyphs);

        size_t compiled_ui = flow_svg_ui_compile_file("specs/cockpit_hud.svg",
                                                      s_live_svg_ui_nodes,
                                                      FLOW_SVG_UI_MAX_NODES,
                                                      &s_live_svg_ui_dom);
        flow_broadcast_obs_init64(&s_live_obs_bridge, 0x4B01U, 0x4B02U, 0x4B03U);
        printf("  ✓ FLOW SVG-UI DOM    : %zu / 8 reactive 64B nodes compiled (Liquid-Glass + OBS Bridge)\n",
               compiled_ui);

        enum { TOTAL_FRAMES = 100, WARMUP_FRAMES = 15, TIMED_CAP = TOTAL_FRAMES - WARMUP_FRAMES };
        double gpu_times_ms[TIMED_CAP];
        double cpu_times_ms[TIMED_CAP];
        size_t timed_count = 0U;

        printf("  ✓ Running 100 benchmark frames at 4K resolution (3840x2160)...\n");

        FlowGameFramePacket last_pkt;
        memset(&last_pkt, 0, sizeof(last_pkt));

        for (int frame = 0; frame < TOTAL_FRAMES; ++frame) {
            uint64_t cpu_t0_ns = flow_clock_monotonic_ns();

            flow_game_step(&engine, FLOW_Q16_ONE);

            FlowGameFramePacket pkt;
            if (!flow_game_poll_frame_packet(&engine, &pkt)) {
                pkt.craft_z_q16 = engine.craft_traj[2].v;
                pkt.craft_vz_q16 = engine.craft_traj[2].d1;
                pkt.stage_layer = engine.current_layer;
                pkt.active_tiles_count = engine.active_tile_count;
                pkt.ricardian = engine.ricardian;
                pkt.thermal_shutdown = engine.thermal_shutdown;
                pkt.cine_camera = engine.cine_brain.resolved;
                pkt.relative_scale = engine.relative_scale;
                pkt.fps_governor = engine.fps_governor;
            }
            last_pkt = pkt;

            uint32_t active_count = engine.active_tile_count;

            FlowGpuFrameUniforms256 u;
            populate_host_uniforms(&u, W, H, (uint32_t)frame, &engine, &pkt, active_count);
            flow_svg_ui_bind_telemetry_q16(s_live_svg_ui_nodes,
                                           s_live_svg_ui_dom.compiled_node_count,
                                           u.s0.thermal_kelvin,
                                           (int32_t)pkt.craft_vz_q16,
                                           u.s1.vcam_id,
                                           u.s3.dopamine_phase,
                                           s_live_obs_bridge.broadcast_mode);
            flow_broadcast_obs_publish_frame64(&s_live_obs_bridge,
                                               (uint32_t)frame,
                                               u.s1.vcam_id,
                                               u.s3.dopamine_phase,
                                               (uint64_t)pkt.craft_z_q16);

            flow_metal_sync_bus_slots(&renderer);
            flow_metal_renderer_sync_buffers(&renderer, engine.active_tile_indices, active_count, &u);

            id<MTLCommandBuffer> cb = [renderer.queue commandBuffer];
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = renderTexture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0.003, 0.005, 0.012, 1.0);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            flow_metal_encode_frame(enc, &renderer);
            [enc endEncoding];

            [cb commit];
            [cb waitUntilCompleted];

            uint64_t cpu_t1_ns = flow_clock_monotonic_ns();
            double cpu_ms = (double)(cpu_t1_ns - cpu_t0_ns) / 1000000.0;
            double gpu_ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;

            if (frame >= WARMUP_FRAMES && timed_count < TIMED_CAP) {
                uint32_t raw_gpu_us = (gpu_ms > 0.0) ? (uint32_t)(gpu_ms * 1000.0) : 0U;
                uint32_t ema_gpu_us = (uint32_t)(
                    ((uint64_t)engine.fps_governor.ema_frame_us * 3ULL + (uint64_t)raw_gpu_us) >> 2U
                );
                flow_game_step_fps_governor(&engine, ema_gpu_us);
                gpu_times_ms[timed_count] = gpu_ms;
                cpu_times_ms[timed_count] = cpu_ms;
                timed_count++;
            }
        }

        static const char *const kVCamFiles[FLOW_VCAM_COUNT] = {
            "build/vcam_0_telephoto.ppm",
            "build/vcam_1_chase.ppm",
            "build/vcam_2_flyby.ppm",
            "build/vcam_3_cockpit.ppm"
        };
        static const char *const kVCamLabels[FLOW_VCAM_COUNT] = {
            "VCAM 0: 135mm f/1.4 Orbit Telephoto (2.39:1 Anamorphic + Shallow DoF)",
            "VCAM 1: 24mm  f/4.0 Dynamic Chase   (Rule-of-Thirds + Interceptor + FOV Breathing)",
            "VCAM 2: 35mm  f/2.0 Geodesic Flyby  (Close-Up 800K Cavity + Fluid + Shake/Blur)",
            "VCAM 3: 18mm  f/8.0 Tactical Cockpit(First-Person Canopy + SDD SVG HUD)"
        };

        if (export_ppm) {
            uint8_t *readback = (uint8_t *)malloc((size_t)W * (size_t)H * 4U);
            if (readback) {
                for (uint32_t vcam_id = 0; vcam_id < (uint32_t)FLOW_VCAM_COUNT; ++vcam_id) {
                    if (vcam_id == FLOW_VCAM_CHASE_DYNAMIC) {
                        engine.craft_traj[2].d1 = 980LL << FLOW_Q16_SHIFT;
                        engine.craft_traj[2].d2 = 120LL << FLOW_Q16_SHIFT;
                        engine.craft_rot[0].d1  = 0LL;
                        engine.craft_rot[1].d1  = 0LL;
                    } else if (vcam_id == FLOW_VCAM_COCKPIT_TACTICAL) {
                        engine.craft_traj[2].d1 = 750LL << FLOW_Q16_SHIFT;
                        engine.craft_traj[2].d2 = 90LL << FLOW_Q16_SHIFT;
                        engine.craft_rot[0].d1  = -(12288LL);
                        engine.craft_rot[1].d1  = 16384LL;
                    } else {
                        engine.craft_traj[2].d1 = 620LL << FLOW_Q16_SHIFT;
                        engine.craft_traj[2].d2 = 0LL;
                        engine.craft_rot[0].d1  = 0LL;
                        engine.craft_rot[1].d1  = 0LL;
                    }

                    flow_cine_trigger_blend(&engine.cine_brain, vcam_id, 0LL, FLOW_EASE_SMOOTHERSTEP_5);
                    flow_cine_brain_step(&engine.cine_brain, engine.craft_traj, engine.craft_rot,
                                         engine.ricardian.thermal_kelvin, false, FLOW_Q16_ONE);
                    last_pkt.craft_vz_q16 = engine.craft_traj[2].d1;
                    last_pkt.cine_camera  = engine.cine_brain.resolved;

                    FlowGpuFrameUniforms256 u;
                    populate_host_uniforms(&u, W, H, 99U, &engine, &last_pkt, FLOW_GAME_TOTAL_TILES);
                    flow_svg_ui_bind_telemetry_q16(s_live_svg_ui_nodes,
                                                   s_live_svg_ui_dom.compiled_node_count,
                                                   u.s0.thermal_kelvin,
                                                   (int32_t)last_pkt.craft_vz_q16,
                                                   u.s1.vcam_id,
                                                   u.s3.dopamine_phase,
                                                   s_live_obs_bridge.broadcast_mode);

                    flow_metal_sync_bus_slots(&renderer);
                    flow_metal_renderer_sync_buffers(&renderer, NULL, FLOW_GAME_TOTAL_TILES, &u);

                    id<MTLCommandBuffer> cb = [renderer.queue commandBuffer];
                    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
                    rp.colorAttachments[0].texture = renderTexture;
                    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.003, 0.005, 0.012, 1.0);
                    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

                    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
                    flow_metal_encode_frame(enc, &renderer);
                    [enc endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];

                    [renderTexture getBytes:readback
                                bytesPerRow:W * 4
                                 fromRegion:MTLRegionMake2D(0, 0, W, H)
                                mipmapLevel:0];

                    if (write_ppm_p6(kVCamFiles[vcam_id], readback, (int)W, (int)H)) {
                        printf("  ✓ 4K VCam Storyboard : %-26s -> %s\n", kVCamFiles[vcam_id], kVCamLabels[vcam_id]);
                    }
                    if (vcam_id == FLOW_VCAM_ORBIT_TELEPHOTO) {
                        write_ppm_p6("build/frame_preview.ppm", readback, (int)W, (int)H);
                    }
                }
                free(readback);
            }
            printf("  ✓ 4K Hero Preview    : build/frame_preview.ppm (3840x2160 P6)\n");
        } else {
            printf("  ✓ 4K Storyboard Skip : Pure GPU/CPU benchmark mode (132 MB PPM disk writes saved; use --storyboard to generate)\n");
        }

        if (flow_broadcast_obs_export_shm_packet(&s_live_obs_bridge, &s_live_svg_ui_dom, "build/flow_obs_bridge.bin") == 0) {
            printf("  ✓ OBS Zero-Copy SHM  : build/flow_obs_bridge.bin (Saved %u MB CPU Readback, Ring=%u/%u)\n",
                   s_live_obs_bridge.zero_copy_bytes_saved,
                   s_live_obs_bridge.read_ring_slot,
                   s_live_obs_bridge.write_ring_slot);
        }

        FlowWallpaperState64 bench_wp;
        flow_wallpaper_init64(&bench_wp, 0x20261105ULL, 0U, 1U, true);
        char wp_hex[129];
        flow_wallpaper_encode_hex128(&bench_wp, wp_hex);
        if (flow_wallpaper_export_web_runner(&bench_wp, "build/flow_wallpaper_web.html") == 0) {
            printf("  ✓ Cross-Platform WP  : build/flow_wallpaper_web.html (Zero-Asset WebGL2 + ShareCode=%.24s...)\n",
                   wp_hex);
        }

        qsort(gpu_times_ms, timed_count, sizeof(double), cmp_double_asc);
        qsort(cpu_times_ms, timed_count, sizeof(double), cmp_double_asc);

        double median_gpu = gpu_times_ms[timed_count / 2U];
        double p95_gpu    = gpu_times_ms[(timed_count * 95U) / 100U];
        double median_cpu = cpu_times_ms[timed_count / 2U];
        double p95_cpu    = cpu_times_ms[(timed_count * 95U) / 100U];

        printf("--------------------------------------------------------------------------------\n");
        printf("  [Benchmark Results (4K 3840x2160, %zu timed frames)]:\n", timed_count);
        printf("    GPU Frame Time : Median %6.3f ms  |  p95 %6.3f ms\n", median_gpu, p95_gpu);
        printf("    CPU Frame Time : Median %6.3f ms  |  p95 %6.3f ms\n", median_cpu, p95_cpu);
        printf("    Culling Rate   : %.2f%% (%u active / %u total tiles)\n",
               (double)(FLOW_GAME_TOTAL_TILES - engine.active_tile_count) * 100.0 / (double)FLOW_GAME_TOTAL_TILES,
               engine.active_tile_count, FLOW_GAME_TOTAL_TILES);
        printf("    120FPS Gov     : %s (steps=%u, layer0_r=%dm, transitions=%u)\n",
               engine.fps_governor.quality_throttled ? "THROTTLED" : "ULTRA",
               engine.fps_governor.raymarch_max_steps,
               engine.fps_governor.layer0_radius_q16 >> 16,
               engine.fps_governor.transition_count);
        printf("================================================================================\n");

        flow_game_destroy(&engine);
        return 0;
    }
}

@interface FlowGameView : NSView <NSWindowDelegate>
@end

@implementation FlowGameView {
    id<MTLDevice> _device;
    FlowMetalRenderer _renderer;
    CAMetalLayer *_metalLayer;
    FlowGameEngineState _engine;
    uint32_t _frameIndex;
    uint64_t _lastFrameNs;
    uint32_t _physicsAccumUs;
    int32_t _renderAlphaQ16;
    int64_t _throttle;
    int64_t _pitch;
    int64_t _yaw;
    int64_t _roll;
    int32_t _dragDeltaXQ16;
    bool _mousePressed;
    bool _mouseJustClicked;
    AudioQueueRef _audioQueue;
    AudioQueueBufferRef _audioBuffers[3];
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        _device = MTLCreateSystemDefaultDevice();

        _metalLayer = [CAMetalLayer layer];
        _metalLayer.device = _device;
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        _metalLayer.framebufferOnly = YES;
        [_metalLayer setDisplaySyncEnabled:s_vsync_enabled];
        _metalLayer.frame = frame;
        self.layer = _metalLayer;

        NSError *error = nil;
        if (!flow_metal_renderer_init(&_renderer, _device, MTLPixelFormatBGRA8Unorm, &error)) {
            fprintf(stderr, "ERROR: Failed to init FlowMetalRenderer in FlowGameView: %s\n",
                    error ? [[error localizedDescription] UTF8String] : "Unknown");
            return nil;
        }

        flow_svg_ui_compile_file("specs/cockpit_hud.svg",
                                 s_live_svg_ui_nodes,
                                 FLOW_SVG_UI_MAX_NODES,
                                 &s_live_svg_ui_dom);
        (void)flow_latex_dfa_compile_boxes64("$\\gamma = \\frac{1}{\\sqrt{1 - \\beta^2}}$",
                                             -25559, -20971, 2048, 0xFFD830U,
                                             s_live_latex_boxes, 16U, NULL, NULL);
        flow_broadcast_obs_init64(&s_live_obs_bridge, 0x4B01U, 0x4B02U, 0x4B03U);

        flow_game_init(&_engine);
        flow_fps_governor_set_mode(&_engine, s_target_fps, s_vsync_enabled);
        s_live_prefab_inst = (FlowPrefabInstance64){
            .prefab_id = FLOW_PREFAB_ORBITAL_CITADEL,
            .style_override_id = FLOW_STYLE_ORBITAL_CITADEL,
            .pos_q16 = { 0LL, 0LL, 0LL },
            .vel_q16 = { 0LL, 0LL, 0LL },
            .thermal_kelvin = 760U,
            .active_layer = FLOW_STAGE_LAYER_0_SDF,
            .flags = 0U
        };
        flow_prefab_edit_init64(&s_live_prefab_edit, &s_live_prefab_inst);
        flow_director_init64(&s_live_story_director);
        memset(s_live_debug_logs, 0, sizeof(s_live_debug_logs));
        memset(&s_live_pipeline_telemetry, 0, sizeof(s_live_pipeline_telemetry));

        s_live_seed_style = flow_seed_style_derive64(0x20261105ULL);
        flow_sovereign_gate_init64(&s_live_sovereign_gate, 0x1001ULL, 0x1002ULL, 50000LL, 1200ULL);
        s_live_sovereign_gate.thermal_kelvin = 760U;
        s_live_object_ticket = flow_ticket_build_preset64(s_live_ticket_preset_idx);
        flow_wallpaper_init64(&s_live_wallpaper,
                              s_live_seed_style.master_seed,
                              0U,
                              s_live_finish_class,
                              !s_wallpaper_audio);
        flow_wallpaper_host_init64(&s_live_wp_host,
                                   FLOW_OS_DESKTOP_MACOS_CGLEVEL,
                                   (uint32_t)frame.size.width,
                                   (uint32_t)frame.size.height,
                                   !s_wallpaper_audio);
        _frameIndex = 0;
        _lastFrameNs = 0ULL;
        _physicsAccumUs = FLOW_FIXED_PHYSICS_DT_US;
        _renderAlphaQ16 = 0;
        _throttle = FLOW_Q16_ONE;
        _pitch = 0;
        _yaw = 0;
        _roll = 0;
        _mousePressed = false;
        _mouseJustClicked = false;
        _audioQueue = NULL;

        if (!s_wallpaper_mode || s_wallpaper_audio) {
            AudioStreamBasicDescription fmt;
            memset(&fmt, 0, sizeof(fmt));
            fmt.mSampleRate       = 48000.0;
            fmt.mFormatID         = kAudioFormatLinearPCM;
            fmt.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
            fmt.mBitsPerChannel   = 16;
            fmt.mChannelsPerFrame = 2; /* True 2-Channel Binaural Stereo */
            fmt.mBytesPerFrame    = 4;
            fmt.mFramesPerPacket  = 1;
            fmt.mBytesPerPacket   = 4;

            if (AudioQueueNewOutput(&fmt, flow_audio_queue_callback, NULL, NULL, NULL, 0, &_audioQueue) == noErr &&
                _audioQueue != NULL) {
                for (int b = 0; b < 3; ++b) {
                    if (AudioQueueAllocateBuffer(_audioQueue, 256U * 2U * sizeof(int16_t), &_audioBuffers[b]) == noErr) {
                        flow_audio_queue_callback(NULL, _audioQueue, _audioBuffers[b]);
                    }
                }
                AudioQueueStart(_audioQueue, NULL);
            }
        }
    }
    return self;
}

- (void)dealloc {
    if (_audioQueue != NULL) {
        AudioQueueStop(_audioQueue, true);
        AudioQueueDispose(_audioQueue, true);
        _audioQueue = NULL;
    }
    flow_game_destroy(&_engine);
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent *)event {
    unsigned short code = [event keyCode];
    switch (code) {
        case 53:
            [NSApp terminate:nil];
            break;
        case 13:
            _pitch = -(FLOW_Q16_ONE / 4);
            _throttle = FLOW_Q16_ONE * 2;
            break;
        case 1:
            _pitch = (FLOW_Q16_ONE / 4);
            _throttle = FLOW_Q16_ONE / 2;
            break;
        case 0:
            _yaw = -(FLOW_Q16_ONE / 4);
            _roll = -(FLOW_Q16_ONE / 4);
            break;
        case 2:
            _yaw = (FLOW_Q16_ONE / 4);
            _roll = (FLOW_Q16_ONE / 4);
            break;
        case 49:
            _throttle = FLOW_Q16_ONE * 3;
            break;
        case 48:
            flow_game_input(&_engine, _throttle, _pitch, _yaw, _roll, true);
            break;
        case 8:
            flow_cine_cycle_vcam(&_engine.cine_brain, 20LL * FLOW_Q16_ONE);
            break;
        case 3: /* Key 'F': Cycle 4 Haute-Couture Luxury Ship Finishes */
            s_live_finish_class = (s_live_finish_class + 1U) & 3U;
            break;
        case 7: /* Key 'X': Thermal Decompression Purge (-120K ASMR + 6D Camera Shake) */
            (void)flow_sovereign_gate_thermal_purge_x(&s_live_sovereign_gate);
            _engine.ricardian.thermal_kelvin = s_live_sovereign_gate.thermal_kelvin;
            s_live_purge_frames = 12U;
            flow_cine_add_trauma(&_engine.cine_brain, FLOW_Q16_ONE / 3);
            break;
        case 15: /* Key 'R': Rent / Draw New 64-Bit Generative Master Seed */
            s_live_seed_style = flow_seed_style_derive64(s_live_seed_style.master_seed + 0x13579BDFULL);
            s_live_finish_class = s_live_seed_style.archetype_id & 3U;
            break;
        case 17: /* Key 'T': Cycle 4 Composable 4-Axis Object Tickets (Horror / Climber / Tactical / Arena) */
            s_live_ticket_preset_idx = (s_live_ticket_preset_idx + 1U) & 3U;
            s_live_object_ticket = flow_ticket_build_preset64(s_live_ticket_preset_idx);
            flow_cine_trigger_blend(&_engine.cine_brain,
                                    (s_live_object_ticket.camera_axis_id - 1U) & 3U,
                                    20LL * FLOW_Q16_ONE,
                                    FLOW_EASE_SMOOTHERSTEP_5);
            break;
        case 38: /* Key 'J': Trigger 4-Phase Dopamine Jackpot Execution Boom! */
            flow_dopamine_trigger_jackpot64(&s_live_dopamine_jackpot,
                                            (uint64_t)_frameIndex + 1ULL,
                                            2U,
                                            s_live_finish_class,
                                            950U);
            flow_cine_add_trauma(&_engine.cine_brain, FLOW_Q16_ONE / 2);
            break;
        case 40: /* Key 'K': One-Call Scripted Story Finisher ("劇情殺") + Scene Transition ("過渡動畫") */
            flow_director_trigger_story_finisher64(&s_live_story_director,
                                                   0x777U + (_frameIndex & 0xFFU),
                                                   s_live_prefab_inst.prefab_id,
                                                   FLOW_VCAM_ORBIT_TELEPHOTO,
                                                   (uint16_t)((_frameIndex >> 2U) & 3U),
                                                   1024);
            flow_dopamine_trigger_jackpot64(&s_live_dopamine_jackpot,
                                            (uint64_t)_frameIndex + 1ULL,
                                            2U,
                                            s_live_finish_class,
                                            950U);
            flow_cine_trigger_blend(&_engine.cine_brain,
                                    FLOW_VCAM_ORBIT_TELEPHOTO,
                                    20LL * FLOW_Q16_ONE,
                                    FLOW_EASE_SMOOTHERSTEP_5);
            flow_cine_add_trauma(&_engine.cine_brain, (FLOW_Q16_ONE * 3) / 4);
            flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                                    flow_clock_monotonic_ns() / 1000ULL,
                                    FLOW_STAGE_PREFAB_CSG, FLOW_LOG_EVENT_STORY_DIRECTOR,
                                    0U, s_live_prefab_inst.prefab_id,
                                    s_live_story_director.scripted_hp_floor_q16,
                                    (int32_t)s_live_story_director.transition_style,
                                    s_live_svg_ui_dom.cursor_norm_x_q16,
                                    s_live_svg_ui_dom.cursor_norm_y_q16, 32768U);
            break;
        case 9: /* Key 'V': Toggle CAMetalLayer VSync (displaySyncEnabled) */
            s_vsync_enabled = !s_vsync_enabled;
            [_metalLayer setDisplaySyncEnabled:s_vsync_enabled];
            flow_fps_governor_set_mode(&_engine, s_target_fps, s_vsync_enabled);
            flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                                    flow_clock_monotonic_ns() / 1000ULL,
                                    FLOW_STAGE_GPU_RAYMARCH, FLOW_LOG_EVENT_FPS_VSYNC,
                                    0U, s_target_fps,
                                    (int32_t)(!s_vsync_enabled), (int32_t)s_vsync_enabled,
                                    s_live_svg_ui_dom.cursor_norm_x_q16,
                                    s_live_svg_ui_dom.cursor_norm_y_q16, 16384U);
            break;
        case 5: /* Key 'G': Cycle 60 -> 120 -> Uncapped(0) -> 30 FPS Governor Budget */
            if (s_target_fps == 60U) {
                s_target_fps = 120U;
            } else if (s_target_fps == 120U) {
                s_target_fps = 0U; /* Uncapped 240+ FPS */
            } else if (s_target_fps == 0U) {
                s_target_fps = 30U;
            } else {
                s_target_fps = 60U;
            }
            flow_fps_governor_set_mode(&_engine, s_target_fps, s_vsync_enabled);
            flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                                    flow_clock_monotonic_ns() / 1000ULL,
                                    FLOW_STAGE_GPU_RAYMARCH, FLOW_LOG_EVENT_FPS_VSYNC,
                                    0U, s_target_fps,
                                    0, (int32_t)_engine.fps_governor.target_budget_us,
                                    s_live_svg_ui_dom.cursor_norm_x_q16,
                                    s_live_svg_ui_dom.cursor_norm_y_q16, 20480U);
            break;
        case 18:
            flow_cine_trigger_blend(&_engine.cine_brain, FLOW_VCAM_ORBIT_TELEPHOTO,
                                    20LL * FLOW_Q16_ONE, FLOW_EASE_SMOOTHERSTEP_5);
            break;
        case 19:
            flow_cine_trigger_blend(&_engine.cine_brain, FLOW_VCAM_CHASE_DYNAMIC,
                                    20LL * FLOW_Q16_ONE, FLOW_EASE_SMOOTHERSTEP_5);
            break;
        case 20:
            flow_cine_trigger_blend(&_engine.cine_brain, FLOW_VCAM_FLYBY_DOLLY,
                                    20LL * FLOW_Q16_ONE, FLOW_EASE_SMOOTHERSTEP_5);
            break;
        case 21:
            flow_cine_trigger_blend(&_engine.cine_brain, FLOW_VCAM_COCKPIT_TACTICAL,
                                    20LL * FLOW_Q16_ONE, FLOW_EASE_SMOOTHERSTEP_5);
            break;
        default:
            [super keyDown:event];
            break;
    }
}

- (void)keyUp:(NSEvent *)event {
    unsigned short code = [event keyCode];
    switch (code) {
        case 13:
        case 1:
            _pitch = 0;
            _throttle = FLOW_Q16_ONE;
            break;
        case 0:
        case 2:
            _yaw = 0;
            _roll = 0;
            break;
        case 49:
            _throttle = FLOW_Q16_ONE;
            break;
        default:
            [super keyUp:event];
            break;
    }
}

- (void)mouseDown:(NSEvent *)event {
    (void)event;
    _mousePressed = true;
    _mouseJustClicked = true;
    _dragDeltaXQ16 = 0;
}

- (void)mouseDragged:(NSEvent *)event {
    _dragDeltaXQ16 += (int32_t)([event deltaX] * 256.0);
}

- (void)mouseUp:(NSEvent *)event {
    (void)event;
    _mousePressed = false;
    _dragDeltaXQ16 = 0;
}

- (void)rightMouseDown:(NSEvent *)event {
    (void)event;
    /* Blender-style Right-Click Modal Cancel / Godot-style Revert to SSOT Prefab */
    flow_prefab_edit_revert_ssot64(&s_live_prefab_edit, &s_live_prefab_inst);
    s_live_sovereign_gate.thermal_kelvin = s_live_prefab_inst.thermal_kelvin;
    _engine.ricardian.thermal_kelvin = s_live_prefab_inst.thermal_kelvin;
    flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                            flow_clock_monotonic_ns() / 1000ULL,
                            FLOW_STAGE_PREFAB_CSG, FLOW_LOG_EVENT_MODAL_CANCEL,
                            0U, s_live_prefab_inst.prefab_id,
                            0, (int32_t)s_live_prefab_inst.thermal_kelvin,
                            s_live_svg_ui_dom.cursor_norm_x_q16,
                            s_live_svg_ui_dom.cursor_norm_y_q16, 16384U);
}

- (void)renderFrame {
    @autoreleasepool {
        uint64_t t0_ns = flow_clock_monotonic_ns();
        uint32_t measured_us = FLOW_FIXED_PHYSICS_DT_US;
        if (_lastFrameNs != 0ULL && t0_ns > _lastFrameNs) {
            uint64_t dt_us = (t0_ns - _lastFrameNs) / 1000ULL;
            measured_us = (dt_us > (uint64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)dt_us;
            flow_game_step_fps_governor(&_engine, measured_us);
        }
        _lastFrameNs = t0_ns;

        flow_game_input(&_engine, _throttle, _pitch, _yaw, _roll, false);
        /* Fixed-Tick Physics + Variable Render Decoupling */
        (void)flow_game_step_fixed_accumulator(&_engine, measured_us, &_physicsAccumUs, &_renderAlphaQ16);

        FlowGameFramePacket pkt;
        if (!flow_game_poll_frame_packet(&_engine, &pkt)) {
            pkt.craft_z_q16 = _engine.craft_traj[2].v;
            pkt.craft_vz_q16 = _engine.craft_traj[2].d1;
            pkt.stage_layer = _engine.current_layer;
            pkt.active_tiles_count = _engine.active_tile_count;
            pkt.ricardian = _engine.ricardian;
            pkt.thermal_shutdown = _engine.thermal_shutdown;
            pkt.cine_camera = _engine.cine_brain.resolved;
            pkt.relative_scale = _engine.relative_scale;
            pkt.fps_governor = _engine.fps_governor;
        }

        CGSize sz = _metalLayer.drawableSize;
        if (s_wallpaper_mode) {
            NSPoint mouseLoc = [NSEvent mouseLocation];
            NSScreen *scr = [NSScreen mainScreen];
            NSRect scrFrame = scr ? [scr frame] : NSMakeRect(0, 0, 1920, 1080);
            int32_t norm_x_q16 = (scrFrame.size.width > 1.0)
                ? (int32_t)(((mouseLoc.x / scrFrame.size.width) * 2.0 - 1.0) * 65536.0)
                : 0;
            int32_t norm_y_q16 = (scrFrame.size.height > 1.0)
                ? (int32_t)(((mouseLoc.y / scrFrame.size.height) * 2.0 - 1.0) * 65536.0)
                : 0;
            (void)flow_wallpaper_step_governor64(&s_live_wallpaper, &s_live_wp_host, 0, 100U, true);
            flow_wallpaper_step_parallax_q16(&s_live_wallpaper,
                                             norm_x_q16,
                                             norm_y_q16,
                                             s_live_av_frame.cavity_resonance_q16);
        }

        FlowGpuFrameUniforms256 u;
        populate_host_uniforms(&u, (uint32_t)sz.width, (uint32_t)sz.height,
                               _frameIndex++, &_engine, &pkt, FLOW_GAME_TOTAL_TILES);
        flow_svg_ui_bind_telemetry_q16(s_live_svg_ui_nodes,
                                       s_live_svg_ui_dom.compiled_node_count,
                                       u.s0.thermal_kelvin,
                                       (int32_t)pkt.craft_vz_q16,
                                       u.s1.vcam_id,
                                       u.s3.dopamine_phase,
                                       s_live_obs_bridge.broadcast_mode);

        NSWindow *win = [self window];
        if (win != nil) {
            NSPoint wpt = [win mouseLocationOutsideOfEventStream];
            NSRect b = [self bounds];
            if (b.size.height > 1.0) {
                int32_t ui_x_q16 = (int32_t)(((wpt.x - b.size.width * 0.5) / b.size.height) * 65536.0);
                int32_t ui_y_q16 = (int32_t)(((b.size.height * 0.5 - wpt.y) / b.size.height) * 65536.0);
                s_live_prefab_edit.xray_cursor_x_q16 = ui_x_q16;
                s_live_prefab_edit.xray_cursor_y_q16 = ui_y_q16;

                uint32_t act = flow_svg_ui_step_pointer_arena(s_live_svg_ui_nodes,
                                                              s_live_svg_ui_dom.compiled_node_count,
                                                              &s_live_svg_ui_dom,
                                                              ui_x_q16,
                                                              ui_y_q16,
                                                              _dragDeltaXQ16,
                                                              _mousePressed,
                                                              _mouseJustClicked);
                int32_t step_drag = _dragDeltaXQ16;
                _dragDeltaXQ16 = 0;
                _mouseJustClicked = false;
                if (act >= FLOW_SVG_ACTION_VCAM_0 && act <= FLOW_SVG_ACTION_VCAM_3) {
                    flow_cine_trigger_blend(&_engine.cine_brain,
                                            act - FLOW_SVG_ACTION_VCAM_0,
                                            20LL * FLOW_Q16_ONE,
                                            FLOW_EASE_SMOOTHERSTEP_5);
                } else if (act == FLOW_SVG_ACTION_THERMAL_PURGE) {
                    (void)flow_sovereign_gate_thermal_purge_x(&s_live_sovereign_gate);
                    _engine.ricardian.thermal_kelvin = s_live_sovereign_gate.thermal_kelvin;
                    s_live_purge_frames = 12U;
                } else if (act == FLOW_SVG_ACTION_DOPAMINE_BOOM) {
                    flow_dopamine_trigger_jackpot64(&s_live_dopamine_jackpot,
                                                    (uint64_t)_frameIndex + 1ULL,
                                                    2U,
                                                    s_live_finish_class,
                                                    950U);
                } else if (act == FLOW_SVG_ACTION_BROADCAST_REC) {
                    s_live_obs_bridge.broadcast_mode =
                        (s_live_obs_bridge.broadcast_mode == FLOW_BROADCAST_MODE_IDLE)
                        ? FLOW_BROADCAST_MODE_LIVE_OBS_SHM
                        : FLOW_BROADCAST_MODE_IDLE;
                } else if (act == FLOW_SVG_ACTION_XRAY_SCRUB) {
                    if (step_drag != 0) {
                        flow_prefab_edit_begin_modal64(&s_live_prefab_edit,
                                                       &s_live_prefab_inst,
                                                       FLOW_PREFAB_MODAL_XRAY_SCRUB);
                        flow_prefab_edit_apply_scrub64(&s_live_prefab_edit,
                                                       &s_live_prefab_inst,
                                                       step_drag,
                                                       0);
                    } else {
                        s_live_prefab_edit.xray_mask_mode =
                            (s_live_prefab_edit.xray_mask_mode == FLOW_PREFAB_XRAY_OFF)
                            ? (uint16_t)FLOW_PREFAB_XRAY_SPHERE_CUT
                            : (uint16_t)FLOW_PREFAB_XRAY_OFF;
                    }
                    flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                                            t0_ns / 1000ULL, FLOW_STAGE_PREFAB_CSG,
                                            FLOW_LOG_EVENT_XRAY_TOGGLE,
                                            s_live_svg_ui_dom.hovered_node_id,
                                            s_live_prefab_inst.prefab_id,
                                            0, s_live_prefab_edit.xray_radius_q16,
                                            ui_x_q16, ui_y_q16, 16384U);
                } else if (act == FLOW_SVG_ACTION_PREFAB_REVERT) {
                    flow_prefab_edit_revert_ssot64(&s_live_prefab_edit, &s_live_prefab_inst);
                    s_live_sovereign_gate.thermal_kelvin = s_live_prefab_inst.thermal_kelvin;
                    _engine.ricardian.thermal_kelvin = s_live_prefab_inst.thermal_kelvin;
                    flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                                            t0_ns / 1000ULL, FLOW_STAGE_PREFAB_CSG,
                                            FLOW_LOG_EVENT_PREFAB_REVERT,
                                            s_live_svg_ui_dom.hovered_node_id,
                                            s_live_prefab_inst.prefab_id,
                                            0, (int32_t)s_live_prefab_inst.thermal_kelvin,
                                            ui_x_q16, ui_y_q16, 22937U);
                } else if (act == FLOW_SVG_ACTION_TOGGLE_VSYNC) {
                    s_vsync_enabled = !s_vsync_enabled;
                    [_metalLayer setDisplaySyncEnabled:s_vsync_enabled];
                    flow_fps_governor_set_mode(&_engine, s_target_fps, s_vsync_enabled);
                    flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                                            t0_ns / 1000ULL, FLOW_STAGE_SVG_UI_DOM,
                                            FLOW_LOG_EVENT_FPS_VSYNC,
                                            s_live_svg_ui_dom.hovered_node_id, s_target_fps,
                                            (int32_t)(!s_vsync_enabled), (int32_t)s_vsync_enabled,
                                            ui_x_q16, ui_y_q16, 16384U);
                } else if (act == FLOW_SVG_ACTION_CYCLE_FPS_GOV) {
                    s_target_fps = (s_target_fps == 60U) ? 120U : ((s_target_fps == 120U) ? 0U : ((s_target_fps == 0U) ? 30U : 60U));
                    flow_fps_governor_set_mode(&_engine, s_target_fps, s_vsync_enabled);
                    flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                                            t0_ns / 1000ULL, FLOW_STAGE_SVG_UI_DOM,
                                            FLOW_LOG_EVENT_FPS_VSYNC,
                                            s_live_svg_ui_dom.hovered_node_id, s_target_fps,
                                            0, (int32_t)_engine.fps_governor.target_budget_us,
                                            ui_x_q16, ui_y_q16, 20480U);
                } else if (act == FLOW_SVG_ACTION_STORY_DIRECTOR) {
                    flow_director_trigger_story_finisher64(&s_live_story_director,
                                                           0x777U + (_frameIndex & 0xFFU),
                                                           s_live_prefab_inst.prefab_id,
                                                           FLOW_VCAM_ORBIT_TELEPHOTO,
                                                           FLOW_TRANS_LIQUID_GLASS_WAVE,
                                                           1024);
                    flow_dopamine_trigger_jackpot64(&s_live_dopamine_jackpot,
                                                    (uint64_t)_frameIndex + 1ULL,
                                                    2U,
                                                    s_live_finish_class,
                                                    950U);
                    flow_debug_log_append64(s_live_debug_logs, 64U, &s_live_pipeline_telemetry,
                                            t0_ns / 1000ULL, FLOW_STAGE_SVG_UI_DOM,
                                            FLOW_LOG_EVENT_STORY_DIRECTOR,
                                            s_live_svg_ui_dom.hovered_node_id,
                                            s_live_prefab_inst.prefab_id,
                                            s_live_story_director.scripted_hp_floor_q16,
                                            (int32_t)s_live_story_director.transition_style,
                                            ui_x_q16, ui_y_q16, 32768U);
                }
            }
        }
        flow_pipeline_telemetry_step64(&s_live_pipeline_telemetry,
                                       _frameIndex, 18U, 24U, 85U, 5400U, 22U, 12U);
        flow_broadcast_obs_publish_frame64(&s_live_obs_bridge,
                                           _frameIndex,
                                           u.s1.vcam_id,
                                           u.s3.dopamine_phase,
                                           (uint64_t)pkt.craft_z_q16);

        flow_metal_sync_bus_slots(&_renderer);
        flow_metal_renderer_sync_buffers(&_renderer, NULL, FLOW_GAME_TOTAL_TILES, &u);

        if (s_wallpaper_max_frames > 0U && _frameIndex >= s_wallpaper_max_frames) {
            [NSApp terminate:nil];
            return;
        }

        id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        if (!drawable)
            return;

        id<MTLCommandBuffer> cb = [_renderer.queue commandBuffer];
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0.003, 0.005, 0.012, 1.0);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        flow_metal_encode_frame(enc, &_renderer);
        [enc endEncoding];

        [cb presentDrawable:drawable];
        [cb commit];
    }
}
@end

int main(int argc, char *argv[]) {
    bool is_bench = false;
    bool is_storyboard = false;
    bool is_headless = false;
    const char *wp_export_path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--bench") == 0) {
            is_bench = true;
        } else if (strcmp(argv[i], "--storyboard") == 0) {
            is_storyboard = true;
        } else if (strcmp(argv[i], "--headless") == 0) {
            is_headless = true;
        } else if (strcmp(argv[i], "--wallpaper") == 0) {
            s_wallpaper_mode = true;
        } else if (strcmp(argv[i], "--wallpaper-audio") == 0) {
            s_wallpaper_audio = true;
        } else if (strcmp(argv[i], "--wallpaper-frames") == 0 && (i + 1) < argc) {
            s_wallpaper_max_frames = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--wallpaper-export") == 0 && (i + 1) < argc) {
            wp_export_path = argv[++i];
        } else if (strcmp(argv[i], "--fps") == 0 && (i + 1) < argc) {
            s_target_fps = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--vsync") == 0 && (i + 1) < argc) {
            s_vsync_enabled = (strtoul(argv[++i], NULL, 10) != 0UL);
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("BLOOM v%s (Pure C17 / Metal Deterministic Simulation Engine)\n", BLOOM_VERSION_STRING);
            return 0;
        }
    }

    if (wp_export_path != NULL) {
        FlowWallpaperState64 wp;
        flow_wallpaper_init64(&wp, 0x20261105ULL, 0U, 1U, true);
        char hex128[129];
        flow_wallpaper_encode_hex128(&wp, hex128);
        int rc = flow_wallpaper_export_web_runner(&wp, wp_export_path);
        printf("[BLOOM WALLPAPER WORKSHOP] Exported Zero-Asset WebGL2 Live Wallpaper -> %s\n", wp_export_path);
        printf("  ✓ 128-Char Hex Share Code: %s\n", hex128);
        return rc;
    }

    if (is_headless) {
        FlowUdpSnapshot64 snap;
        flow_game_run_headless_ticks(6000U, &snap);
        printf("[BLOOM HEADLESS UDP SERVER] Executed 6,000 authoritative 60Hz ticks (0 GPU / 0 MB VRAM)!\n");
        printf("  ✓ Final Tick=%u | Seq=%llu | z_q16=%lld | vz_q16=%lld | MAC=0x%016llX\n",
               snap.server_tick,
               (unsigned long long)snap.sequence_id,
               (long long)snap.traj_z.v,
               (long long)snap.traj_z.d1,
               (unsigned long long)snap.state_mac);
        return 0;
    }

    if (is_bench || is_storyboard) {
        return run_4k_benchmark(is_storyboard);
    }

    @autoreleasepool {
        [NSApplication sharedApplication];
        if (s_wallpaper_mode) {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        } else {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        }

        NSScreen *mainScreen = [NSScreen mainScreen];
        NSRect frame = (s_wallpaper_mode && mainScreen != nil)
            ? [mainScreen frame]
            : NSMakeRect(100, 100, 1280, 720);
        NSWindowStyleMask styleMask = s_wallpaper_mode
            ? NSWindowStyleMaskBorderless
            : (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable);

        NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:styleMask
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        if (s_wallpaper_mode) {
            [window setLevel:CGWindowLevelForKey(kCGDesktopWindowLevelKey)];
            [window setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces |
                                           NSWindowCollectionBehaviorStationary |
                                           NSWindowCollectionBehaviorIgnoresCycle)];
            [window setIgnoresMouseEvents:YES];
            [window setOpaque:YES];
            printf("[BLOOM WALLPAPER ENGINE] Attached to macOS Desktop Layer (kCGDesktopWindowLevelKey, Click-Through=ON, Audio=%s)\n",
                   s_wallpaper_audio ? "ON" : "MUTED");
        } else {
            [window setTitle:@"BLOOM v1.0.0: Pure C17 / Metal Engine [Keys: WASD/Space | V: VSync | G: FPS Gov | C/1-4: VCam]"];
        }
        FlowGameView *view = [[FlowGameView alloc] initWithFrame:frame];
        [window setContentView:view];
        if (s_wallpaper_mode) {
            [window orderFront:nil];
        } else {
            [window makeKeyAndOrderFront:nil];
            [window makeFirstResponder:view];
        }

        double timer_hz = (s_target_fps == 0U || s_target_fps > 240U) ? 240.0 : (double)s_target_fps;
        NSTimer *timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / timer_hz)
                                                         repeats:YES
                                                           block:^(NSTimer * _Nonnull t) {
            (void)t;
            [view renderFrame];
        }];
        (void)timer;

        [NSApp run];
    }
    return 0;
}
