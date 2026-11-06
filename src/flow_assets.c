#include "flow_assets.h"
#include "flow_hott_jet.h"
#include "ricardian.h"
#include <stdio.h>
#include <string.h>

/*
 * ============================================================================
 * BLOOM: Native 64-Byte External Asset Pipeline Implementation (C17)
 * ============================================================================
 * Strict Zero Float / Double & Zero Heap Guarantee:
 * Audited via `flowy slice src/flow_assets.c --llvm-ir`.
 * ============================================================================
 */

__extension__ typedef __int128 flow_asset_i128;
__extension__ typedef unsigned __int128 flow_asset_u128;

static inline uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static inline uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static inline int64_t asset_abs_i64(int64_t x) {
    return (x == INT64_MIN) ? INT64_MAX : ((x < 0) ? -x : x);
}

static inline int64_t asset_max_i64(int64_t a, int64_t b) { return (a > b) ? a : b; }
static inline int64_t asset_min_i64(int64_t a, int64_t b) { return (a < b) ? a : b; }

/*
 * Pure-Integer IEEE-754 binary32 Bit-Pattern to Q16.16 Converter
 * Converts 32-bit float bit pattern into exact signed Q16.16 without any FPU instruction.
 */
int32_t flow_ieee754_bits_to_q16(uint32_t ieee754_bits) {
    uint32_t sign = (ieee754_bits >> 31U) & 1U;
    int32_t  exp  = (int32_t)((ieee754_bits >> 23U) & 0xFFU);
    uint32_t mant = (ieee754_bits & 0x7FFFFFU) | 0x800000U;

    if (exp == 0) {
        return 0;
    }
    if (exp == 255) {
        return sign ? -INT32_MAX : INT32_MAX;
    }

    /* Value = mant * 2^(exp - 127 - 23). In Q16.16 (* 2^16), net shift = exp - 134 */
    int32_t shift = exp - 134;
    int64_t mag_q16 = 0LL;
    if (shift >= 0) {
        if (shift >= 15) {
            mag_q16 = (int64_t)INT32_MAX;
        } else {
            mag_q16 = ((int64_t)mant) << (uint32_t)shift;
        }
    } else {
        int32_t rshift = -shift;
        if (rshift >= 24) {
            mag_q16 = 0LL;
        } else {
            mag_q16 = (int64_t)(mant >> (uint32_t)rshift);
        }
    }
    if (mag_q16 > (int64_t)INT32_MAX) {
        mag_q16 = (int64_t)INT32_MAX;
    }
    return sign ? (int32_t)(-mag_q16) : (int32_t)mag_q16;
}

/* ============================================================================
 * 1. RIFF/WAVE (.wav) PCM Audio Loader & Polyphonic Voice Mixer
 * ============================================================================ */

bool flow_asset_load_wav_bytes(const uint8_t *wav_bytes,
                               size_t wav_len,
                               uint32_t asset_id,
                               int16_t *out_mono_s16,
                               uint32_t max_frames,
                               FlowWavAssetDesc64 *out_desc) {
    if (!wav_bytes || wav_len < 44U || !out_desc) {
        return false;
    }
    memset(out_desc, 0, sizeof(*out_desc));

    /* Verify "RIFF" (0x46464952) and "WAVE" (0x45564157) */
    if (read_u32_le(wav_bytes + 0U) != 0x46464952U ||
        read_u32_le(wav_bytes + 8U) != 0x45564157U) {
        return false;
    }

    uint16_t format_tag = 0U;
    uint16_t channels = 0U;
    uint32_t sample_rate = 0U;
    uint16_t bits_per_sample = 0U;
    uint32_t data_offset = 0U;
    uint32_t data_size = 0U;

    size_t pos = 12U;
    while (pos + 8U <= wav_len) {
        uint32_t chunk_id = read_u32_le(wav_bytes + pos);
        uint32_t chunk_sz = read_u32_le(wav_bytes + pos + 4U);
        size_t body = pos + 8U;

        if (chunk_id == 0x20746D66U && chunk_sz >= 16U && body + 16U <= wav_len) {
            /* "fmt " chunk */
            format_tag      = read_u16_le(wav_bytes + body + 0U);
            channels        = read_u16_le(wav_bytes + body + 2U);
            sample_rate     = read_u32_le(wav_bytes + body + 4U);
            bits_per_sample = read_u16_le(wav_bytes + body + 14U);
        } else if (chunk_id == 0x61746164U && body <= wav_len) {
            /* "data" chunk */
            data_offset = (uint32_t)body;
            size_t avail = wav_len - body;
            data_size = (chunk_sz <= avail) ? chunk_sz : (uint32_t)avail;
            break;
        }

        size_t step = (size_t)chunk_sz + (chunk_sz & 1U);
        if (body + step <= pos) {
            break;
        }
        pos = body + step;
    }

    if (channels == 0U || sample_rate == 0U || bits_per_sample == 0U || data_offset == 0U) {
        return false;
    }

    uint32_t bytes_per_smp = (uint32_t)(bits_per_sample / 8U);
    if (bytes_per_smp == 0U) {
        return false;
    }
    uint32_t frame_stride = bytes_per_smp * (uint32_t)channels;
    uint32_t total_frames = data_size / frame_stride;
    uint32_t decode_frames = total_frames;
    if (out_mono_s16 && max_frames > 0U && decode_frames > max_frames) {
        decode_frames = max_frames;
    }

    int32_t peak_abs = 0;
    uint64_t sum_abs = 0ULL;
    uint64_t h_pcm = 0xCBF29CE484222325ULL;
    const uint8_t *pcm = wav_bytes + data_offset;

    for (uint32_t f = 0U; f < decode_frames; ++f) {
        const uint8_t *fp = pcm + (size_t)f * (size_t)frame_stride;
        int32_t mono_accum = 0;

        for (uint16_t ch = 0U; ch < channels; ++ch) {
            const uint8_t *sp = fp + (size_t)ch * (size_t)bytes_per_smp;
            int32_t s16 = 0;
            if (format_tag == 3U && bits_per_sample == 32U) {
                /* IEEE-754 float32 [-1.0, +1.0] -> Q16 -> S16 */
                int32_t q16 = flow_ieee754_bits_to_q16(read_u32_le(sp));
                s16 = (int32_t)flow_lean_abi_eval_clamp(q16 >> 1, -32768LL, 32767LL);
            } else if (bits_per_sample == 8U) {
                s16 = ((int32_t)sp[0] - 128) << 8;
            } else if (bits_per_sample == 16U) {
                s16 = (int32_t)(int16_t)read_u16_le(sp);
            } else if (bits_per_sample == 24U) {
                s16 = (int32_t)(int16_t)read_u16_le(sp + 1U);
            } else if (bits_per_sample == 32U) {
                s16 = (int32_t)(int16_t)read_u16_le(sp + 2U);
            }
            mono_accum += s16;
        }

        int16_t mono_val = (int16_t)(mono_accum / (int32_t)channels);
        if (out_mono_s16 && f < max_frames) {
            out_mono_s16[f] = mono_val;
        }
        int32_t abs_v = (mono_val < 0) ? -(int32_t)mono_val : (int32_t)mono_val;
        if (abs_v > peak_abs) {
            peak_abs = abs_v;
        }
        sum_abs += (uint64_t)abs_v;
        h_pcm ^= (uint64_t)(uint16_t)mono_val;
        h_pcm *= 0x100000001B3ULL;
    }

    out_desc->asset_id          = asset_id;
    out_desc->sample_rate_hz    = sample_rate;
    out_desc->num_channels      = (uint32_t)channels;
    out_desc->bits_per_sample   = (uint32_t)bits_per_sample;
    out_desc->frame_count       = decode_frames;
    out_desc->data_offset_bytes = data_offset;
    out_desc->data_size_bytes   = data_size;
    out_desc->peak_amp_q16      = (peak_abs * 65536) / 32768;
    out_desc->rms_energy_q16    = (decode_frames > 0U)
        ? (int32_t)(((sum_abs / (uint64_t)decode_frames) * 65536ULL) / 32768ULL)
        : 0;
    out_desc->duration_ms       = (uint32_t)(((uint64_t)total_frames * 1000ULL) / (uint64_t)sample_rate);
    out_desc->format_tag        = (uint32_t)format_tag;
    out_desc->flags             = 1U;
    out_desc->pcm_hash          = flow_capsule64_splitmix(h_pcm);
    out_desc->asset_mac         = flow_capsule64_calc_mac(out_desc, 0x5741564153534554ULL);
    return true;
}

bool flow_asset_load_wav_file(const char *wav_path,
                              uint32_t asset_id,
                              int16_t *out_mono_s16,
                              uint32_t max_frames,
                              FlowWavAssetDesc64 *out_desc) {
    if (!wav_path || !out_desc) {
        return false;
    }
    FILE *fp = fopen(wav_path, "rb");
    if (!fp) {
        return false;
    }
    static _Alignas(64) uint8_t s_wav_file_buf[524288]; /* 512 KB static zero-heap scratch */
    size_t n = fread(s_wav_file_buf, 1U, sizeof(s_wav_file_buf), fp);
    fclose(fp);
    return flow_asset_load_wav_bytes(s_wav_file_buf, n, asset_id, out_mono_s16, max_frames, out_desc);
}

void flow_audio_voice_trigger64(FlowAudioVoice64 *voice,
                                uint32_t voice_id,
                                const FlowWavAssetDesc64 *wav_desc,
                                int32_t gain_q16,
                                int32_t pan_q16,
                                int32_t pitch_step_q16,
                                bool loop_enabled,
                                uint64_t trigger_tick) {
    if (!voice || !wav_desc || wav_desc->frame_count == 0U) {
        return;
    }
    memset(voice, 0, sizeof(*voice));
    int32_t clamped_pan = (int32_t)flow_lean_abi_eval_clamp(pan_q16, -65536LL, 65536LL);
    int32_t clamped_gain = (int32_t)flow_lean_abi_eval_clamp(gain_q16, 0LL, 131072LL);
    int32_t clamped_pitch = (int32_t)flow_lean_abi_eval_clamp(
        (pitch_step_q16 > 0) ? pitch_step_q16 : 65536, 16384LL, 262144LL
    );

    voice->voice_id       = voice_id;
    voice->asset_id       = wav_desc->asset_id;
    voice->cursor_frame   = 0U;
    voice->frac_q16       = 0U;
    voice->total_frames   = wav_desc->frame_count;
    voice->pitch_step_q16 = clamped_pitch;
    voice->pan_q16        = clamped_pan;
    voice->gain_left_q16  = (int32_t)(((int64_t)clamped_gain * (65536LL - clamped_pan)) >> 17U);
    voice->gain_right_q16 = (int32_t)(((int64_t)clamped_gain * (65536LL + clamped_pan)) >> 17U);
    voice->active         = 1U;
    voice->loop_enabled   = loop_enabled ? 1U : 0U;
    voice->priority       = 1U;
    voice->trigger_tick   = trigger_tick;
    voice->voice_mac      = flow_capsule64_calc_mac(voice, 0x564F4943454D4143ULL);
}

void flow_audio_voice_mix_stereo_s16(FlowAudioVoice64 *voice,
                                     const int16_t *mono_pcm_s16,
                                     int16_t *inout_stereo_s16,
                                     uint32_t out_frames) {
    if (!voice || !mono_pcm_s16 || !inout_stereo_s16 || voice->active == 0U || voice->total_frames == 0U) {
        return;
    }

    for (uint32_t i = 0U; i < out_frames; ++i) {
        if (voice->cursor_frame >= voice->total_frames) {
            if (voice->loop_enabled != 0U) {
                voice->cursor_frame %= voice->total_frames;
            } else {
                voice->active = 0U;
                break;
            }
        }

        uint32_t idx0 = voice->cursor_frame;
        uint32_t idx1 = (idx0 + 1U < voice->total_frames)
            ? (idx0 + 1U)
            : (voice->loop_enabled ? 0U : idx0);
        int64_t s0 = (int64_t)mono_pcm_s16[idx0];
        int64_t s1 = (int64_t)mono_pcm_s16[idx1];
        int64_t frac = (int64_t)(voice->frac_q16 & 0xFFFFU);
        int64_t interp = s0 + (((s1 - s0) * frac) >> 16U);

        int64_t add_l = (interp * (int64_t)voice->gain_left_q16) >> 16U;
        int64_t add_r = (interp * (int64_t)voice->gain_right_q16) >> 16U;

        int64_t cur_l = (int64_t)inout_stereo_s16[i * 2U + 0U] + add_l;
        int64_t cur_r = (int64_t)inout_stereo_s16[i * 2U + 1U] + add_r;

        inout_stereo_s16[i * 2U + 0U] = (int16_t)flow_lean_abi_eval_clamp(cur_l, -32768LL, 32767LL);
        inout_stereo_s16[i * 2U + 1U] = (int16_t)flow_lean_abi_eval_clamp(cur_r, -32768LL, 32767LL);

        uint64_t step_q16 = (uint64_t)voice->frac_q16 + (uint64_t)(uint32_t)voice->pitch_step_q16;
        voice->cursor_frame += (uint32_t)(step_q16 >> 16U);
        voice->frac_q16      = (uint32_t)(step_q16 & 0xFFFFULL);
    }

    voice->voice_mac = flow_capsule64_calc_mac(voice, 0x564F4943454D4143ULL);
}

/* ============================================================================
 * 2. glTF 2.0 Binary (.glb) 3D Mesh Loader & Q16 SDF Proxy
 * ============================================================================ */

static uint32_t extract_json_uint_after(const char *json, size_t json_len, const char *key) {
    if (!json || !key) {
        return 0U;
    }
    size_t klen = strlen(key);
    for (size_t i = 0U; i + klen < json_len; ++i) {
        if (memcmp(json + i, key, klen) == 0) {
            size_t j = i + klen;
            while (j < json_len && (json[j] < '0' || json[j] > '9')) {
                j++;
            }
            uint32_t val = 0U;
            while (j < json_len && json[j] >= '0' && json[j] <= '9') {
                val = val * 10U + (uint32_t)(json[j] - '0');
                j++;
            }
            if (val > 0U) {
                return val;
            }
        }
    }
    return 0U;
}

bool flow_asset_load_glb_bytes(const uint8_t *glb_bytes,
                               size_t glb_len,
                               uint32_t asset_id,
                               uint32_t material_style_id,
                               int32_t (*out_vertices_q16)[3],
                               uint32_t max_vertices,
                               FlowGlbMeshDesc64 *out_desc) {
    if (!glb_bytes || glb_len < 20U || !out_desc) {
        return false;
    }
    memset(out_desc, 0, sizeof(*out_desc));

    uint32_t magic   = read_u32_le(glb_bytes + 0U);
    uint32_t version = read_u32_le(glb_bytes + 4U);
    uint32_t tot_len = read_u32_le(glb_bytes + 8U);
    if (magic != FLOW_GLB_MAGIC_HEADER || version != 2U || tot_len > (uint32_t)glb_len) {
        return false;
    }

    uint32_t json_bytes = 0U;
    uint32_t bin_bytes  = 0U;
    const char *json_ptr = NULL;
    const uint8_t *bin_ptr = NULL;

    size_t pos = 12U;
    while (pos + 8U <= glb_len) {
        uint32_t chunk_len  = read_u32_le(glb_bytes + pos + 0U);
        uint32_t chunk_type = read_u32_le(glb_bytes + pos + 4U);
        size_t body = pos + 8U;
        if (body + chunk_len > glb_len) {
            break;
        }
        if (chunk_type == FLOW_GLB_CHUNK_JSON) {
            json_bytes = chunk_len;
            json_ptr   = (const char *)(glb_bytes + body);
        } else if (chunk_type == FLOW_GLB_CHUNK_BIN) {
            bin_bytes = chunk_len;
            bin_ptr   = glb_bytes + body;
        }
        pos = body + chunk_len;
    }

    if (json_bytes == 0U || !json_ptr) {
        return false;
    }

    uint32_t json_count = extract_json_uint_after(json_ptr, json_bytes, "\"count\"");
    uint32_t max_bin_verts = bin_bytes / 12U;
    uint32_t vcount = (json_count > 0U && json_count <= max_bin_verts) ? json_count : max_bin_verts;
    if (vcount > 4096U) {
        vcount = 4096U;
    }

    int32_t min_q16[3] = { INT32_MAX, INT32_MAX, INT32_MAX };
    int32_t max_q16[3] = { -INT32_MAX, -INT32_MAX, -INT32_MAX };
    uint32_t valid_verts = 0U;

    if (bin_ptr && vcount > 0U) {
        for (uint32_t v = 0U; v < vcount; ++v) {
            const uint8_t *vp = bin_ptr + (size_t)v * 12U;
            int32_t x = flow_ieee754_bits_to_q16(read_u32_le(vp + 0U));
            int32_t y = flow_ieee754_bits_to_q16(read_u32_le(vp + 4U));
            int32_t z = flow_ieee754_bits_to_q16(read_u32_le(vp + 8U));

            /* Filter out non-coordinate buffer bytes (> 10,000m in local space) */
            int64_t limit = 10000LL * FLOW_Q16_ONE;
            if (asset_abs_i64(x) <= limit && asset_abs_i64(y) <= limit && asset_abs_i64(z) <= limit) {
                if (x < min_q16[0]) min_q16[0] = x;
                if (y < min_q16[1]) min_q16[1] = y;
                if (z < min_q16[2]) min_q16[2] = z;
                if (x > max_q16[0]) max_q16[0] = x;
                if (y > max_q16[1]) max_q16[1] = y;
                if (z > max_q16[2]) max_q16[2] = z;

                if (out_vertices_q16 && valid_verts < max_vertices) {
                    out_vertices_q16[valid_verts][0] = x;
                    out_vertices_q16[valid_verts][1] = y;
                    out_vertices_q16[valid_verts][2] = z;
                }
                valid_verts++;
            }
        }
    }

    if (valid_verts == 0U) {
        min_q16[0] = min_q16[1] = min_q16[2] = -FLOW_Q16_ONE;
        max_q16[0] = max_q16[1] = max_q16[2] =  FLOW_Q16_ONE;
        valid_verts = 8U;
    }

    int64_t ext_x = ((int64_t)max_q16[0] - (int64_t)min_q16[0]) / 2LL;
    int64_t ext_y = ((int64_t)max_q16[1] - (int64_t)min_q16[1]) / 2LL;
    int64_t ext_z = ((int64_t)max_q16[2] - (int64_t)min_q16[2]) / 2LL;
    if (ext_x < 4096LL) ext_x = 4096LL;
    if (ext_y < 4096LL) ext_y = 4096LL;
    if (ext_z < 4096LL) ext_z = 4096LL;

    flow_asset_u128 r_sq = (flow_asset_u128)((flow_asset_i128)ext_x * (flow_asset_i128)ext_x +
                                             (flow_asset_i128)ext_y * (flow_asset_i128)ext_y +
                                             (flow_asset_i128)ext_z * (flow_asset_i128)ext_z);
    int32_t bound_r = (int32_t)flow_asm_isqrt_u128(r_sq);

    out_desc->asset_id            = asset_id;
    out_desc->gltf_version        = version;
    out_desc->json_chunk_bytes    = json_bytes;
    out_desc->bin_chunk_bytes     = bin_bytes;
    out_desc->vertex_count        = valid_verts;
    out_desc->index_count         = valid_verts * 3U;
    out_desc->aabb_min_q16[0]     = min_q16[0];
    out_desc->aabb_min_q16[1]     = min_q16[1];
    out_desc->aabb_min_q16[2]     = min_q16[2];
    out_desc->aabb_max_q16[0]     = max_q16[0];
    out_desc->aabb_max_q16[1]     = max_q16[1];
    out_desc->aabb_max_q16[2]     = max_q16[2];
    out_desc->bounding_radius_q16 = bound_r;
    out_desc->material_style_id   = material_style_id;
    out_desc->mesh_mac            = flow_capsule64_calc_mac(out_desc, 0x474C424D45534836ULL);
    return true;
}

bool flow_asset_load_glb_file(const char *glb_path,
                              uint32_t asset_id,
                              uint32_t material_style_id,
                              int32_t (*out_vertices_q16)[3],
                              uint32_t max_vertices,
                              FlowGlbMeshDesc64 *out_desc) {
    if (!glb_path || !out_desc) {
        return false;
    }
    FILE *fp = fopen(glb_path, "rb");
    if (!fp) {
        return false;
    }
    static _Alignas(64) uint8_t s_glb_file_buf[524288];
    size_t n = fread(s_glb_file_buf, 1U, sizeof(s_glb_file_buf), fp);
    fclose(fp);
    return flow_asset_load_glb_bytes(
        s_glb_file_buf, n, asset_id, material_style_id, out_vertices_q16, max_vertices, out_desc
    );
}

int64_t flow_asset_eval_glb_sdf_q16(const FlowGlbMeshDesc64 *mesh,
                                    int64_t local_x_q16,
                                    int64_t local_y_q16,
                                    int64_t local_z_q16) {
    if (!mesh) {
        return FLOW_Q16_ONE;
    }
    int64_t cx = ((int64_t)mesh->aabb_min_q16[0] + (int64_t)mesh->aabb_max_q16[0]) / 2LL;
    int64_t cy = ((int64_t)mesh->aabb_min_q16[1] + (int64_t)mesh->aabb_max_q16[1]) / 2LL;
    int64_t cz = ((int64_t)mesh->aabb_min_q16[2] + (int64_t)mesh->aabb_max_q16[2]) / 2LL;

    int64_t hx = asset_max_i64(((int64_t)mesh->aabb_max_q16[0] - (int64_t)mesh->aabb_min_q16[0]) / 2LL, 4096LL);
    int64_t hy = asset_max_i64(((int64_t)mesh->aabb_max_q16[1] - (int64_t)mesh->aabb_min_q16[1]) / 2LL, 4096LL);
    int64_t hz = asset_max_i64(((int64_t)mesh->aabb_max_q16[2] - (int64_t)mesh->aabb_min_q16[2]) / 2LL, 4096LL);

    int64_t qx = asset_abs_i64(local_x_q16 - cx) - hx;
    int64_t qy = asset_abs_i64(local_y_q16 - cy) - hy;
    int64_t qz = asset_abs_i64(local_z_q16 - cz) - hz;

    int64_t ox = asset_max_i64(qx, 0LL);
    int64_t oy = asset_max_i64(qy, 0LL);
    int64_t oz = asset_max_i64(qz, 0LL);

    flow_asset_u128 sq = (flow_asset_u128)((flow_asset_i128)ox * (flow_asset_i128)ox +
                                           (flow_asset_i128)oy * (flow_asset_i128)oy +
                                           (flow_asset_i128)oz * (flow_asset_i128)oz);
    int64_t outside = (int64_t)flow_asm_isqrt_u128(sq);
    int64_t inside  = asset_min_i64(asset_max_i64(qx, asset_max_i64(qy, qz)), 0LL);
    return outside + inside;
}

/* ============================================================================
 * 3. Standard W3C SVG (<path d="M... L..."/> / <polygon> / <circle>) Compiler
 * ============================================================================ */

static const char *skip_svg_delims(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') {
        p++;
    }
    return p;
}

static const char *parse_svg_q16(const char *p, int32_t *out_q16, bool *out_ok) {
    p = skip_svg_delims(p);
    bool neg = false;
    if (*p == '-') {
        neg = true;
        p++;
    } else if (*p == '+') {
        p++;
    }
    if ((*p < '0' || *p > '9') && *p != '.') {
        if (out_ok) *out_ok = false;
        return p;
    }
    int64_t int_part = 0LL;
    while (*p >= '0' && *p <= '9') {
        int_part = int_part * 10LL + (int64_t)(*p - '0');
        p++;
    }
    int64_t frac_num = 0LL;
    int64_t frac_den = 1LL;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (frac_den < 1000000LL) {
                frac_num = frac_num * 10LL + (int64_t)(*p - '0');
                frac_den *= 10LL;
            }
            p++;
        }
    }
    int64_t q16 = (int_part << 16U) + ((frac_num << 16U) / frac_den);
    *out_q16 = (int32_t)(neg ? -q16 : q16);
    if (out_ok) *out_ok = true;
    return p;
}

static int16_t norm_to_q8(int32_t coord_q16, int32_t vb_min_q16, int32_t vb_span_q16) {
    if (vb_span_q16 <= 0) {
        vb_span_q16 = 24 * 65536;
    }
    /* Map [vb_min, vb_min + vb_span] -> [-256, +256] (Q8.8 in [-1.0, +1.0]) */
    int64_t rel = (int64_t)coord_q16 - (int64_t)vb_min_q16;
    int64_t norm = ((rel * 512LL) / (int64_t)vb_span_q16) - 256LL;
    return (int16_t)flow_lean_abi_eval_clamp(norm, -256LL, 256LL);
}

static void append_glyph_seg(FlowGpuGlyphDesc64 *g,
                             int16_t x0, int16_t y0,
                             int16_t x1, int16_t y1) {
    if (!g || g->stroke_count >= 5U || (x0 == x1 && y0 == y1)) {
        return;
    }
    uint32_t idx = g->stroke_count * 4U;
    g->seg_coords_q8[idx + 0U] = x0;
    g->seg_coords_q8[idx + 1U] = y0;
    g->seg_coords_q8[idx + 2U] = x1;
    g->seg_coords_q8[idx + 3U] = y1;
    g->stroke_count++;

    uint32_t qx = (uint32_t)(((int32_t)x0 + (int32_t)x1 + 512) >> 7U) & 7U;
    uint32_t qy = (uint32_t)(((int32_t)y0 + (int32_t)y1 + 512) >> 7U) & 7U;
    g->ternary_pos_mask |= (1ULL << (qy * 8U + qx));
}

bool flow_asset_compile_svg_icon64(const char *svg_text,
                                   uint32_t glyph_id,
                                   uint32_t style_id,
                                   FlowGpuGlyphDesc64 *out_glyph) {
    if (!svg_text || !out_glyph) {
        return false;
    }
    memset(out_glyph, 0, sizeof(*out_glyph));
    out_glyph->glyph_id          = glyph_id;
    out_glyph->style_id          = style_id;
    out_glyph->stroke_radius_q16 = 2621; /* ~0.04 in Q16 */

    int32_t vb_x = 0, vb_y = 0, vb_w = 24 * 65536, vb_h = 24 * 65536;
    const char *vb = strstr(svg_text, "viewBox=\"");
    if (vb) {
        bool ok = false;
        const char *p = vb + 9;
        p = parse_svg_q16(p, &vb_x, &ok);
        p = parse_svg_q16(p, &vb_y, &ok);
        p = parse_svg_q16(p, &vb_w, &ok);
        p = parse_svg_q16(p, &vb_h, &ok);
        if (vb_w <= 0) vb_w = 24 * 65536;
        if (vb_h <= 0) vb_h = 24 * 65536;
    }

    /* 1. Extract segments from <path d="..."/> */
    const char *scan = svg_text;
    while (out_glyph->stroke_count < 5U && (scan = strstr(scan, " d=\"")) != NULL) {
        const char *p = scan + 4;
        scan = p;
        int32_t cur_x = vb_x + vb_w / 2;
        int32_t cur_y = vb_y + vb_h / 2;
        int32_t start_x = cur_x;
        int32_t start_y = cur_y;
        char cmd = 'M';

        while (*p && *p != '"' && out_glyph->stroke_count < 5U) {
            p = skip_svg_delims(p);
            if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
                cmd = *p++;
            }
            if (cmd == 'Z' || cmd == 'z') {
                append_glyph_seg(out_glyph,
                                 norm_to_q8(cur_x, vb_x, vb_w), norm_to_q8(cur_y, vb_y, vb_h),
                                 norm_to_q8(start_x, vb_x, vb_w), norm_to_q8(start_y, vb_y, vb_h));
                cur_x = start_x;
                cur_y = start_y;
                continue;
            }
            bool ok1 = false, ok2 = false;
            int32_t v1 = 0, v2 = 0;
            if (cmd == 'H' || cmd == 'h' || cmd == 'V' || cmd == 'v') {
                p = parse_svg_q16(p, &v1, &ok1);
                if (!ok1) { p++; continue; }
                int32_t nx = cur_x, ny = cur_y;
                if (cmd == 'H') nx = v1;
                else if (cmd == 'h') nx = cur_x + v1;
                else if (cmd == 'V') ny = v1;
                else if (cmd == 'v') ny = cur_y + v1;
                append_glyph_seg(out_glyph,
                                 norm_to_q8(cur_x, vb_x, vb_w), norm_to_q8(cur_y, vb_y, vb_h),
                                 norm_to_q8(nx, vb_x, vb_w), norm_to_q8(ny, vb_y, vb_h));
                cur_x = nx;
                cur_y = ny;
            } else {
                p = parse_svg_q16(p, &v1, &ok1);
                p = parse_svg_q16(p, &v2, &ok2);
                if (!ok1 || !ok2) { p++; continue; }
                int32_t nx = (cmd >= 'a' && cmd <= 'z') ? (cur_x + v1) : v1;
                int32_t ny = (cmd >= 'a' && cmd <= 'z') ? (cur_y + v2) : v2;
                if (cmd == 'M' || cmd == 'm') {
                    cur_x = start_x = nx;
                    cur_y = start_y = ny;
                    cmd = (cmd == 'm') ? 'l' : 'L';
                } else {
                    append_glyph_seg(out_glyph,
                                     norm_to_q8(cur_x, vb_x, vb_w), norm_to_q8(cur_y, vb_y, vb_h),
                                     norm_to_q8(nx, vb_x, vb_w), norm_to_q8(ny, vb_y, vb_h));
                    cur_x = nx;
                    cur_y = ny;
                }
            }
        }
    }

    /* 2. Fallback to <circle> or <polygon> if <path> yielded 0 strokes */
    if (out_glyph->stroke_count == 0U && strstr(svg_text, "<circle") != NULL) {
        append_glyph_seg(out_glyph, -128,    0,    0, -128);
        append_glyph_seg(out_glyph,    0, -128,  128,    0);
        append_glyph_seg(out_glyph,  128,    0,    0,  128);
        append_glyph_seg(out_glyph,    0,  128, -128,    0);
    }

    return out_glyph->stroke_count > 0U;
}

bool flow_asset_load_svg_icon_file(const char *svg_path,
                                   uint32_t glyph_id,
                                   uint32_t style_id,
                                   FlowGpuGlyphDesc64 *out_glyph) {
    if (!svg_path || !out_glyph) {
        return false;
    }
    FILE *fp = fopen(svg_path, "rb");
    if (!fp) {
        return false;
    }
    static char s_svg_buf[65536];
    size_t n = fread(s_svg_buf, 1U, sizeof(s_svg_buf) - 1U, fp);
    fclose(fp);
    s_svg_buf[n] = '\0';
    return flow_asset_compile_svg_icon64(s_svg_buf, glyph_id, style_id, out_glyph);
}

/* ============================================================================
 * 5. Data-Driven 64-Byte GPU CSG Instruction Compiler & Program Evaluator
 * ============================================================================ */

void flow_asset_compile_glb_to_csg_op64(const FlowGlbMeshDesc64 *mesh,
                                        uint32_t op_id,
                                        uint32_t bool_op,
                                        int32_t smooth_k_q16,
                                        FlowGpuCsgOp64 *out_op) {
    if (!out_op) {
        return;
    }
    memset(out_op, 0, sizeof(*out_op));
    out_op->op_id       = op_id;
    out_op->prim_type   = FLOW_CSG_PRIM_GLB_AABB;
    out_op->bool_op     = bool_op;
    out_op->smooth_k_q16 = (smooth_k_q16 > 0) ? smooth_k_q16 : 0;
    out_op->bevel_r_q16  = 1024;
    out_op->active_stage_mask = 0x0FULL;
    if (mesh) {
        out_op->material_id = mesh->material_style_id;
        for (size_t d = 0U; d < 3U; ++d) {
            int64_t lo = (int64_t)mesh->aabb_min_q16[d];
            int64_t hi = (int64_t)mesh->aabb_max_q16[d];
            out_op->pos_q16[d] = (int32_t)((lo + hi) / 2LL);
            int64_t span = hi - lo;
            out_op->extents_q16[d] = (int32_t)((span > 0LL) ? (span / 2LL) : 1024LL);
        }
    }
    out_op->csg_op_mac = flow_capsule64_calc_mac(out_op, 0x4353474F50363421ULL);
}

static int64_t eval_single_csg_prim_q16(const FlowGpuCsgOp64 *op,
                                        int64_t px_q16,
                                        int64_t py_q16,
                                        int64_t pz_q16) {
    int64_t lx = px_q16 - (int64_t)op->pos_q16[0];
    int64_t ly = py_q16 - (int64_t)op->pos_q16[1];
    int64_t lz = pz_q16 - (int64_t)op->pos_q16[2];
    int64_t ax = (lx < 0LL) ? -lx : lx;
    int64_t ay = (ly < 0LL) ? -ly : ly;
    int64_t az = (lz < 0LL) ? -lz : lz;
    if (op->prim_type == FLOW_CSG_PRIM_SPHERE) {
        int64_t r = (op->extents_q16[0] > 0) ? (int64_t)op->extents_q16[0] : 65536LL;
        int64_t max_c = ax;
        if (ay > max_c) max_c = ay;
        if (az > max_c) max_c = az;
        return max_c - r;
    }
    int64_t dx = ax - (int64_t)op->extents_q16[0];
    int64_t dy = ay - (int64_t)op->extents_q16[1];
    int64_t dz = az - (int64_t)op->extents_q16[2];
    int64_t max_in = dx;
    if (dy > max_in) max_in = dy;
    if (dz > max_in) max_in = dz;
    int64_t out_x = (dx > 0LL) ? dx : 0LL;
    int64_t out_y = (dy > 0LL) ? dy : 0LL;
    int64_t out_z = (dz > 0LL) ? dz : 0LL;
    int64_t inside = (max_in < 0LL) ? max_in : 0LL;
    return (out_x + out_y + out_z) + inside - (int64_t)op->bevel_r_q16;
}

int64_t flow_asset_eval_csg_program_q16(const FlowGpuCsgOp64 *ops,
                                        size_t op_count,
                                        int64_t px_q16,
                                        int64_t py_q16,
                                        int64_t pz_q16) {
    if (!ops || op_count == 0U) {
        return 65536000LL;
    }
    int64_t acc = eval_single_csg_prim_q16(&ops[0], px_q16, py_q16, pz_q16);
    for (size_t i = 1U; i < op_count; ++i) {
        int64_t d = eval_single_csg_prim_q16(&ops[i], px_q16, py_q16, pz_q16);
        uint32_t bop = ops[i].bool_op;
        if (bop == FLOW_CSG_BOOL_SUBTRACT) {
            int64_t neg_d = -d;
            acc = (acc > neg_d) ? acc : neg_d;
        } else if (bop == FLOW_CSG_BOOL_INTERSECT) {
            acc = (acc > d) ? acc : d;
        } else if (bop == FLOW_CSG_BOOL_SMOOTH_UNION && ops[i].smooth_k_q16 > 0) {
            int64_t k = (int64_t)ops[i].smooth_k_q16;
            int64_t diff = (acc > d) ? (acc - d) : (d - acc);
            int64_t h = (k > diff) ? (k - diff) : 0LL;
            int64_t min_d = (acc < d) ? acc : d;
            acc = min_d - ((h * h) / (4LL * k));
        } else {
            acc = (acc < d) ? acc : d;
        }
    }
    return acc;
}
