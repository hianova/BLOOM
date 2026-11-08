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

/* ============================================================================
 * 6. 64-Byte Dual-Quaternion Q16 Skeletal Skinning & Capsule Rig
 * ============================================================================ */

void flow_joint_dq64_make(FlowGpuJointDq64 *out_joint,
                          uint32_t joint_id,
                          uint32_t parent_id,
                          const int32_t q_real_q16[4],
                          const int32_t trans_q16[3],
                          int32_t bone_len_q16,
                          int32_t bone_radius_q16,
                          int32_t smooth_k_q16,
                          uint32_t material_id) {
    if (!out_joint) {
        return;
    }
    memset(out_joint, 0, sizeof(*out_joint));
    out_joint->joint_id        = joint_id;
    out_joint->parent_id       = parent_id;
    out_joint->bone_len_q16    = (bone_len_q16 > 0) ? bone_len_q16 : 65536;
    out_joint->bone_radius_q16 = (bone_radius_q16 > 0) ? bone_radius_q16 : 8192;
    out_joint->smooth_k_q16    = (smooth_k_q16 > 0) ? smooth_k_q16 : 4096;
    out_joint->material_id     = material_id;

    int64_t rw = q_real_q16 ? (int64_t)q_real_q16[0] : 65536LL;
    int64_t rx = q_real_q16 ? (int64_t)q_real_q16[1] : 0LL;
    int64_t ry = q_real_q16 ? (int64_t)q_real_q16[2] : 0LL;
    int64_t rz = q_real_q16 ? (int64_t)q_real_q16[3] : 0LL;

    flow_asset_u128 sq = (flow_asset_u128)((flow_asset_i128)rw * rw +
                                           (flow_asset_i128)rx * rx +
                                           (flow_asset_i128)ry * ry +
                                           (flow_asset_i128)rz * rz);
    int64_t norm = (int64_t)flow_asm_isqrt_u128(sq);
    if (norm <= 0LL) {
        rw = 65536LL;
        rx = ry = rz = 0LL;
    } else {
        rw = (rw * 65536LL) / norm;
        rx = (rx * 65536LL) / norm;
        ry = (ry * 65536LL) / norm;
        rz = (rz * 65536LL) / norm;
    }
    out_joint->q_real_q16[0] = (int32_t)rw;
    out_joint->q_real_q16[1] = (int32_t)rx;
    out_joint->q_real_q16[2] = (int32_t)ry;
    out_joint->q_real_q16[3] = (int32_t)rz;

    int64_t tx = trans_q16 ? (int64_t)trans_q16[0] : 0LL;
    int64_t ty = trans_q16 ? (int64_t)trans_q16[1] : 0LL;
    int64_t tz = trans_q16 ? (int64_t)trans_q16[2] : 0LL;

    /* Dual quaternion q_d = 0.5 * (0, tx, ty, tz) * (rw, rx, ry, rz) in Q16.16 */
    out_joint->q_dual_q16[0] = (int32_t)((-tx * rx - ty * ry - tz * rz) / 131072LL);
    out_joint->q_dual_q16[1] = (int32_t)(( tx * rw + ty * rz - tz * ry) / 131072LL);
    out_joint->q_dual_q16[2] = (int32_t)((-tx * rz + ty * rw + tz * rx) / 131072LL);
    out_joint->q_dual_q16[3] = (int32_t)(( tx * ry - ty * rx + tz * rw) / 131072LL);
    out_joint->joint_mac     = flow_capsule64_calc_mac(out_joint, 0x4A4F494E54445136ULL);
}

void flow_joint_dq64_blend(const FlowGpuJointDq64 *joint_a,
                           const FlowGpuJointDq64 *joint_b,
                           int32_t weight_a_q16,
                           FlowGpuJointDq64 *out_blended) {
    if (!joint_a || !joint_b || !out_blended) {
        return;
    }
    *out_blended = *joint_a;
    int64_t wa = flow_lean_abi_eval_clamp((int64_t)weight_a_q16, 0LL, 65536LL);
    int64_t wb = 65536LL - wa;

    /* Shortest-arc hemisphere alignment on S^3 */
    int64_t dot = 0LL;
    for (size_t k = 0U; k < 4U; ++k) {
        dot += (int64_t)joint_a->q_real_q16[k] * (int64_t)joint_b->q_real_q16[k];
    }
    int64_t sign_b = (dot < 0LL) ? -1LL : 1LL;

    int64_t raw_r[4];
    int64_t raw_d[4];
    flow_asset_u128 sq = 0U;
    for (size_t k = 0U; k < 4U; ++k) {
        raw_r[k] = ((int64_t)joint_a->q_real_q16[k] * wa +
                    sign_b * (int64_t)joint_b->q_real_q16[k] * wb) / 65536LL;
        raw_d[k] = ((int64_t)joint_a->q_dual_q16[k] * wa +
                    sign_b * (int64_t)joint_b->q_dual_q16[k] * wb) / 65536LL;
        sq += (flow_asset_u128)((flow_asset_i128)raw_r[k] * (flow_asset_i128)raw_r[k]);
    }
    int64_t norm = (int64_t)flow_asm_isqrt_u128(sq);
    if (norm <= 0LL) {
        norm = 65536LL;
        raw_r[0] = 65536LL;
        raw_r[1] = raw_r[2] = raw_r[3] = 0LL;
    }
    for (size_t k = 0U; k < 4U; ++k) {
        out_blended->q_real_q16[k] = (int32_t)((raw_r[k] * 65536LL) / norm);
        out_blended->q_dual_q16[k] = (int32_t)((raw_d[k] * 65536LL) / norm);
    }
    out_blended->bone_len_q16    = (int32_t)(((int64_t)joint_a->bone_len_q16 * wa +
                                              (int64_t)joint_b->bone_len_q16 * wb) / 65536LL);
    out_blended->bone_radius_q16 = (int32_t)(((int64_t)joint_a->bone_radius_q16 * wa +
                                              (int64_t)joint_b->bone_radius_q16 * wb) / 65536LL);
    out_blended->joint_mac       = flow_capsule64_calc_mac(out_blended, 0x4A4F494E54445136ULL);
}

void flow_joint_dq64_transform_point_q16(const FlowGpuJointDq64 *joint,
                                         const int32_t in_pt_q16[3],
                                         int32_t out_pt_q16[3]) {
    if (!in_pt_q16 || !out_pt_q16) {
        return;
    }
    if (!joint) {
        out_pt_q16[0] = in_pt_q16[0];
        out_pt_q16[1] = in_pt_q16[1];
        out_pt_q16[2] = in_pt_q16[2];
        return;
    }
    int64_t rw = (int64_t)joint->q_real_q16[0];
    int64_t rx = (int64_t)joint->q_real_q16[1];
    int64_t ry = (int64_t)joint->q_real_q16[2];
    int64_t rz = (int64_t)joint->q_real_q16[3];

    int64_t dw = (int64_t)joint->q_dual_q16[0];
    int64_t dx = (int64_t)joint->q_dual_q16[1];
    int64_t dy = (int64_t)joint->q_dual_q16[2];
    int64_t dz = (int64_t)joint->q_dual_q16[3];

    /* Reconstruct translation t = 2 * q_dual * conj(q_real) in Q16.16 */
    int64_t tx = ((-dw * rx + dx * rw - dy * rz + dz * ry) * 2LL) / 65536LL;
    int64_t ty = ((-dw * ry + dx * rz + dy * rw - dz * rx) * 2LL) / 65536LL;
    int64_t tz = ((-dw * rz - dx * ry + dy * rx + dz * rw) * 2LL) / 65536LL;

    int64_t px = (int64_t)in_pt_q16[0];
    int64_t py = (int64_t)in_pt_q16[1];
    int64_t pz = (int64_t)in_pt_q16[2];

    /* Rotate p by unit quaternion q_real: p_rot = p + 2 * u x (u x p + rw * p) */
    int64_t cx = (ry * pz - rz * py) / 65536LL;
    int64_t cy = (rz * px - rx * pz) / 65536LL;
    int64_t cz = (rx * py - ry * px) / 65536LL;

    int64_t wx = cx + (rw * px) / 65536LL;
    int64_t wy = cy + (rw * py) / 65536LL;
    int64_t wz = cz + (rw * pz) / 65536LL;

    int64_t rpx = px + ((ry * wz - rz * wy) * 2LL) / 65536LL;
    int64_t rpy = py + ((rz * wx - rx * wz) * 2LL) / 65536LL;
    int64_t rpz = pz + ((rx * wy - ry * wx) * 2LL) / 65536LL;

    out_pt_q16[0] = (int32_t)(rpx + tx);
    out_pt_q16[1] = (int32_t)(rpy + ty);
    out_pt_q16[2] = (int32_t)(rpz + tz);
}

static int64_t eval_capsule_seg_q16(int64_t px, int64_t py, int64_t pz,
                                    const int32_t a[3], const int32_t b[3],
                                    int32_t radius_q16) {
    int64_t pax = px - (int64_t)a[0];
    int64_t pay = py - (int64_t)a[1];
    int64_t paz = pz - (int64_t)a[2];
    int64_t bax = (int64_t)b[0] - (int64_t)a[0];
    int64_t bay = (int64_t)b[1] - (int64_t)a[1];
    int64_t baz = (int64_t)b[2] - (int64_t)a[2];

    flow_asset_i128 dot_pa_ba = (flow_asset_i128)pax * bax +
                                (flow_asset_i128)pay * bay +
                                (flow_asset_i128)paz * baz;
    flow_asset_i128 dot_ba_ba = (flow_asset_i128)bax * bax +
                                (flow_asset_i128)bay * bay +
                                (flow_asset_i128)baz * baz;
    int64_t h_q16 = 0LL;
    if (dot_ba_ba > 0) {
        flow_asset_i128 raw_h = (dot_pa_ba * (flow_asset_i128)65536LL) / dot_ba_ba;
        if (raw_h < 0) {
            h_q16 = 0LL;
        } else if (raw_h > 65536) {
            h_q16 = 65536LL;
        } else {
            h_q16 = (int64_t)raw_h;
        }
    }
    int64_t dx = pax - (bax * h_q16) / 65536LL;
    int64_t dy = pay - (bay * h_q16) / 65536LL;
    int64_t dz = paz - (baz * h_q16) / 65536LL;
    flow_asset_u128 sq = (flow_asset_u128)((flow_asset_i128)dx * dx +
                                           (flow_asset_i128)dy * dy +
                                           (flow_asset_i128)dz * dz);
    int64_t dist = (int64_t)flow_asm_isqrt_u128(sq);
    return dist - (int64_t)radius_q16;
}

int64_t flow_skeleton_eval_sdf_q16(const FlowGpuJointDq64 *joints,
                                   size_t joint_count,
                                   int64_t px_q16,
                                   int64_t py_q16,
                                   int64_t pz_q16) {
    if (!joints || joint_count == 0U) {
        return 65536000LL;
    }
    int64_t acc = 65536000LL;
    for (size_t i = 0U; i < joint_count; ++i) {
        int32_t local_zero[3] = { 0, 0, 0 };
        int32_t local_tip[3]  = { 0, joints[i].bone_len_q16, 0 };
        int32_t world_a[3], world_b[3];
        flow_joint_dq64_transform_point_q16(&joints[i], local_zero, world_a);
        flow_joint_dq64_transform_point_q16(&joints[i], local_tip,  world_b);

        int64_t d = eval_capsule_seg_q16(
            px_q16, py_q16, pz_q16, world_a, world_b, joints[i].bone_radius_q16
        );
        if (i == 0U) {
            acc = d;
        } else {
            int64_t k = (joints[i].smooth_k_q16 > 0) ? (int64_t)joints[i].smooth_k_q16 : 0LL;
            if (k > 0LL) {
                int64_t diff = (acc > d) ? (acc - d) : (d - acc);
                int64_t h = (k > diff) ? (k - diff) : 0LL;
                int64_t min_d = (acc < d) ? acc : d;
                acc = min_d - ((h * h) / (4LL * k));
            } else {
                acc = (acc < d) ? acc : d;
            }
        }
    }
    return acc;
}

/* ============================================================================
 * 7. 64-Byte Pointer-Free Linear BVH Spatial Acceleration Tree
 * ============================================================================ */

static uint64_t expand_bits_10_to_30(uint32_t v) {
    uint64_t x = (uint64_t)(v & 0x3FFU);
    x = (x | (x << 16U)) & 0x030000FFULL;
    x = (x | (x <<  8U)) & 0x0300F00FULL;
    x = (x | (x <<  4U)) & 0x030C30C3ULL;
    x = (x | (x <<  2U)) & 0x09249249ULL;
    return x;
}

uint64_t flow_bvh_morton3d_q16(int32_t x_q16, int32_t y_q16, int32_t z_q16) {
    /* Normalize Q16 coordinates in [-512.0, +512.0] to 10-bit [0..1023] per axis */
    int64_t nx = ((int64_t)x_q16 >> 16U) + 512LL;
    int64_t ny = ((int64_t)y_q16 >> 16U) + 512LL;
    int64_t nz = ((int64_t)z_q16 >> 16U) + 512LL;
    uint32_t ux = (uint32_t)flow_lean_abi_eval_clamp(nx, 0LL, 1023LL);
    uint32_t uy = (uint32_t)flow_lean_abi_eval_clamp(ny, 0LL, 1023LL);
    uint32_t uz = (uint32_t)flow_lean_abi_eval_clamp(nz, 0LL, 1023LL);
    return (expand_bits_10_to_30(ux) << 0U) |
           (expand_bits_10_to_30(uy) << 1U) |
           (expand_bits_10_to_30(uz) << 2U);
}

static uint32_t build_bvh_recursive(const FlowGpuCsgOp64 *ops,
                                    uint32_t first_prim,
                                    uint32_t prim_count,
                                    FlowGpuBvhNode64 *out_nodes,
                                    uint32_t *inout_used,
                                    uint32_t max_nodes) {
    if (*inout_used >= max_nodes || prim_count == 0U) {
        return 0U;
    }
    uint32_t idx = (*inout_used)++;
    FlowGpuBvhNode64 *node = &out_nodes[idx];
    memset(node, 0, sizeof(*node));
    node->node_id = idx;

    if (prim_count == 1U || (*inout_used + 2U > max_nodes)) {
        node->is_leaf        = 1U;
        node->left_or_first  = first_prim;
        node->right_or_count = prim_count;
        for (size_t d = 0U; d < 3U; ++d) {
            int32_t pad = (ops[first_prim].bevel_r_q16 > 0) ? ops[first_prim].bevel_r_q16 : 0;
            node->aabb_min_q16[d] = ops[first_prim].pos_q16[d] - ops[first_prim].extents_q16[d] - pad;
            node->aabb_max_q16[d] = ops[first_prim].pos_q16[d] + ops[first_prim].extents_q16[d] + pad;
            for (uint32_t k = 1U; k < prim_count; ++k) {
                const FlowGpuCsgOp64 *op = &ops[first_prim + k];
                int32_t p_pad = (op->bevel_r_q16 > 0) ? op->bevel_r_q16 : 0;
                int32_t lo = op->pos_q16[d] - op->extents_q16[d] - p_pad;
                int32_t hi = op->pos_q16[d] + op->extents_q16[d] + p_pad;
                if (lo < node->aabb_min_q16[d]) node->aabb_min_q16[d] = lo;
                if (hi > node->aabb_max_q16[d]) node->aabb_max_q16[d] = hi;
            }
        }
        node->morton_key64 = flow_bvh_morton3d_q16(
            ops[first_prim].pos_q16[0], ops[first_prim].pos_q16[1], ops[first_prim].pos_q16[2]
        );
        node->active_stage_mask = ops[first_prim].active_stage_mask;
        node->bvh_mac = flow_capsule64_calc_mac(node, 0x4256484E4F444536ULL);
        return idx;
    }

    uint32_t half = prim_count / 2U;
    uint32_t left_idx  = build_bvh_recursive(ops, first_prim, half, out_nodes, inout_used, max_nodes);
    uint32_t right_idx = build_bvh_recursive(ops, first_prim + half, prim_count - half, out_nodes, inout_used, max_nodes);

    node->is_leaf        = 0U;
    node->left_or_first  = left_idx;
    node->right_or_count = right_idx;
    for (size_t d = 0U; d < 3U; ++d) {
        int32_t l_min = out_nodes[left_idx].aabb_min_q16[d];
        int32_t r_min = out_nodes[right_idx].aabb_min_q16[d];
        int32_t l_max = out_nodes[left_idx].aabb_max_q16[d];
        int32_t r_max = out_nodes[right_idx].aabb_max_q16[d];
        node->aabb_min_q16[d] = (l_min <= r_min) ? l_min : r_min;
        node->aabb_max_q16[d] = (l_max >= r_max) ? l_max : r_max;
    }
    int32_t cx = (int32_t)(((int64_t)node->aabb_min_q16[0] + (int64_t)node->aabb_max_q16[0]) / 2LL);
    int32_t cy = (int32_t)(((int64_t)node->aabb_min_q16[1] + (int64_t)node->aabb_max_q16[1]) / 2LL);
    int32_t cz = (int32_t)(((int64_t)node->aabb_min_q16[2] + (int64_t)node->aabb_max_q16[2]) / 2LL);
    node->morton_key64      = flow_bvh_morton3d_q16(cx, cy, cz);
    node->active_stage_mask = out_nodes[left_idx].active_stage_mask | out_nodes[right_idx].active_stage_mask;
    node->bvh_mac           = flow_capsule64_calc_mac(node, 0x4256484E4F444536ULL);
    return idx;
}

size_t flow_bvh_build_from_csg64(const FlowGpuCsgOp64 *ops,
                                 size_t op_count,
                                 FlowGpuBvhNode64 *out_nodes,
                                 size_t max_nodes) {
    if (!ops || op_count == 0U || !out_nodes || max_nodes == 0U) {
        return 0U;
    }
    uint32_t used = 0U;
    (void)build_bvh_recursive(ops, 0U, (uint32_t)op_count, out_nodes, &used, (uint32_t)max_nodes);
    return (size_t)used;
}

static int64_t bvh_node_lower_bound_q16(const FlowGpuBvhNode64 *node,
                                        int64_t px_q16,
                                        int64_t py_q16,
                                        int64_t pz_q16) {
    int64_t dx = 0LL, dy = 0LL, dz = 0LL;
    if (px_q16 < (int64_t)node->aabb_min_q16[0]) dx = (int64_t)node->aabb_min_q16[0] - px_q16;
    else if (px_q16 > (int64_t)node->aabb_max_q16[0]) dx = px_q16 - (int64_t)node->aabb_max_q16[0];

    if (py_q16 < (int64_t)node->aabb_min_q16[1]) dy = (int64_t)node->aabb_min_q16[1] - py_q16;
    else if (py_q16 > (int64_t)node->aabb_max_q16[1]) dy = py_q16 - (int64_t)node->aabb_max_q16[1];

    if (pz_q16 < (int64_t)node->aabb_min_q16[2]) dz = (int64_t)node->aabb_min_q16[2] - pz_q16;
    else if (pz_q16 > (int64_t)node->aabb_max_q16[2]) dz = pz_q16 - (int64_t)node->aabb_max_q16[2];

    /* Max-norm lower bound <= Euclidean/Manhattan distance outside AABB */
    int64_t m = dx;
    if (dy > m) m = dy;
    if (dz > m) m = dz;
    return m;
}

int64_t flow_bvh_eval_csg_q16(const FlowGpuBvhNode64 *nodes,
                              size_t node_count,
                              const FlowGpuCsgOp64 *ops,
                              size_t op_count,
                              int64_t px_q16,
                              int64_t py_q16,
                              int64_t pz_q16,
                              uint32_t *out_visited_leaves) {
    if (out_visited_leaves) {
        *out_visited_leaves = 0U;
    }
    if (!nodes || node_count == 0U || !ops || op_count == 0U) {
        return 65536000LL;
    }
    int64_t best_d = 65536000LL;
    uint32_t stack[64];
    uint32_t sp = 0U;
    stack[sp++] = 0U;
    uint32_t visited = 0U;

    while (sp > 0U) {
        uint32_t idx = stack[--sp];
        if (idx >= (uint32_t)node_count) {
            continue;
        }
        const FlowGpuBvhNode64 *node = &nodes[idx];
        int64_t lb = bvh_node_lower_bound_q16(node, px_q16, py_q16, pz_q16);
        if (lb > best_d && best_d < 65536000LL) {
            continue; /* O(log N) branch pruning! */
        }
        if (node->is_leaf != 0U) {
            visited++;
            uint32_t first = node->left_or_first;
            uint32_t count = node->right_or_count;
            for (uint32_t k = 0U; k < count && (first + k) < (uint32_t)op_count; ++k) {
                int64_t d = eval_single_csg_prim_q16(&ops[first + k], px_q16, py_q16, pz_q16);
                if (d < best_d) {
                    best_d = d;
                }
            }
        } else {
            uint32_t l_idx = node->left_or_first;
            uint32_t r_idx = node->right_or_count;
            int64_t lb_l = (l_idx < (uint32_t)node_count)
                               ? bvh_node_lower_bound_q16(&nodes[l_idx], px_q16, py_q16, pz_q16)
                               : 65536000LL;
            int64_t lb_r = (r_idx < (uint32_t)node_count)
                               ? bvh_node_lower_bound_q16(&nodes[r_idx], px_q16, py_q16, pz_q16)
                               : 65536000LL;
            /* Push farther child first so nearer child is popped & narrows best_d first */
            if (lb_l <= lb_r) {
                if (sp < 64U) stack[sp++] = r_idx;
                if (sp < 64U) stack[sp++] = l_idx;
            } else {
                if (sp < 64U) stack[sp++] = l_idx;
                if (sp < 64U) stack[sp++] = r_idx;
            }
        }
    }
    if (out_visited_leaves) {
        *out_visited_leaves = visited;
    }
    return best_d;
}

/* ============================================================================
 * 8. 64-Byte Sparse 4x4x4 High-Frequency SDF Voxel Brick
 * ============================================================================ */

void flow_sdf_brick64_pack(FlowGpuSdfBrick64 *out_brick,
                           uint32_t brick_id,
                           uint32_t material_id,
                           const int32_t origin_q16[3],
                           int32_t voxel_step_q16,
                           const int8_t sdf_offsets_i8[64]) {
    if (!out_brick) {
        return;
    }
    memset(out_brick, 0, sizeof(*out_brick));
    out_brick->brick_id       = brick_id;
    out_brick->material_id    = material_id;
    out_brick->origin_q16[0]  = origin_q16 ? origin_q16[0] : 0;
    out_brick->origin_q16[1]  = origin_q16 ? origin_q16[1] : 0;
    out_brick->origin_q16[2]  = origin_q16 ? origin_q16[2] : 0;
    out_brick->voxel_step_q16 = (voxel_step_q16 > 0) ? voxel_step_q16 : 16384;

    for (uint32_t z = 0U; z < 4U; ++z) {
        uint64_t plane = 0ULL;
        for (uint32_t y = 0U; y < 4U; ++y) {
            for (uint32_t x = 0U; x < 4U; ++x) {
                int64_t raw = sdf_offsets_i8 ? (int64_t)sdf_offsets_i8[z * 16U + y * 4U + x] : 0LL;
                int64_t clamped = flow_lean_abi_eval_clamp(raw, -8LL, 7LL);
                uint64_t nibble = (uint64_t)(clamped + 8LL) & 0xFULL;
                uint32_t shift  = (y * 4U + x) * 4U;
                plane |= (nibble << shift);
            }
        }
        out_brick->sdf_z_planes[z] = plane;
    }
    out_brick->brick_mac = flow_capsule64_calc_mac(out_brick, 0x534446425249434BULL);
}

static int64_t sample_brick_voxel_q16(const FlowGpuSdfBrick64 *brick,
                                      uint32_t x, uint32_t y, uint32_t z) {
    uint32_t shift = (y * 4U + x) * 4U;
    int64_t nibble = (int64_t)((brick->sdf_z_planes[z] >> shift) & 0xFULL) - 8LL;
    /* Scale [-8, +7] by (voxel_step_q16 / 4) so 1 nibble = 1/4 voxel step */
    return (nibble * (int64_t)brick->voxel_step_q16) / 4LL;
}

int64_t flow_sdf_brick64_sample_trilinear_q16(const FlowGpuSdfBrick64 *brick,
                                              int64_t px_q16,
                                              int64_t py_q16,
                                              int64_t pz_q16) {
    if (!brick || brick->voxel_step_q16 <= 0) {
        return 65536000LL;
    }
    int64_t step = (int64_t)brick->voxel_step_q16;
    int64_t u_q16 = ((px_q16 - (int64_t)brick->origin_q16[0]) * 65536LL) / step;
    int64_t v_q16 = ((py_q16 - (int64_t)brick->origin_q16[1]) * 65536LL) / step;
    int64_t w_q16 = ((pz_q16 - (int64_t)brick->origin_q16[2]) * 65536LL) / step;

    int64_t max_grid = 3LL * 65536LL;
    int64_t cu = flow_lean_abi_eval_clamp(u_q16, 0LL, max_grid);
    int64_t cv = flow_lean_abi_eval_clamp(v_q16, 0LL, max_grid);
    int64_t cw = flow_lean_abi_eval_clamp(w_q16, 0LL, max_grid);

    uint32_t ix = (uint32_t)(cu >> 16U);
    uint32_t iy = (uint32_t)(cv >> 16U);
    uint32_t iz = (uint32_t)(cw >> 16U);
    if (ix > 2U) ix = 2U;
    if (iy > 2U) iy = 2U;
    if (iz > 2U) iz = 2U;

    int64_t fx = cu - ((int64_t)ix << 16U);
    int64_t fy = cv - ((int64_t)iy << 16U);
    int64_t fz = cw - ((int64_t)iz << 16U);

    int64_t c000 = sample_brick_voxel_q16(brick, ix,     iy,     iz);
    int64_t c100 = sample_brick_voxel_q16(brick, ix + 1, iy,     iz);
    int64_t c010 = sample_brick_voxel_q16(brick, ix,     iy + 1, iz);
    int64_t c110 = sample_brick_voxel_q16(brick, ix + 1, iy + 1, iz);
    int64_t c001 = sample_brick_voxel_q16(brick, ix,     iy,     iz + 1);
    int64_t c101 = sample_brick_voxel_q16(brick, ix + 1, iy,     iz + 1);
    int64_t c011 = sample_brick_voxel_q16(brick, ix,     iy + 1, iz + 1);
    int64_t c111 = sample_brick_voxel_q16(brick, ix + 1, iy + 1, iz + 1);

    int64_t c00 = (c000 * (65536LL - fx) + c100 * fx) / 65536LL;
    int64_t c10 = (c010 * (65536LL - fx) + c110 * fx) / 65536LL;
    int64_t c01 = (c001 * (65536LL - fx) + c101 * fx) / 65536LL;
    int64_t c11 = (c011 * (65536LL - fx) + c111 * fx) / 65536LL;

    int64_t c0 = (c00 * (65536LL - fy) + c10 * fy) / 65536LL;
    int64_t c1 = (c01 * (65536LL - fy) + c11 * fy) / 65536LL;

    return (c0 * (65536LL - fz) + c1 * fz) / 65536LL;
}

/* ============================================================================
 * 9. 64-Byte Color-Gamut & Streaming Egress (OKLab -> Rec.709/P3/Rec.2020 & YUV)
 * ============================================================================ */

void flow_stream_egress64_init(FlowStreamEgress64 *out_egress,
                               uint32_t stream_id,
                               uint32_t target_gamut,
                               uint32_t stream_format,
                               uint32_t target_fps) {
    if (!out_egress) {
        return;
    }
    memset(out_egress, 0, sizeof(*out_egress));
    out_egress->stream_id     = stream_id;
    out_egress->target_gamut  = target_gamut;
    out_egress->stream_format = stream_format;
    out_egress->target_fps    = (target_fps > 0U) ? target_fps : 60U;

    if (target_gamut == FLOW_GAMUT_DISPLAY_P3) {
        /* LMS -> Display P3 (each row sums to 65536 = D65 white point preserved) */
        out_egress->gamut_row0_q16[0] =  204976;
        out_egress->gamut_row0_q16[1] = -150580;
        out_egress->gamut_row0_q16[2] =   11140;
        out_egress->gamut_row1_q16[0] =  -69206;
        out_egress->gamut_row1_q16[1] =  156238;
        out_egress->gamut_row1_q16[2] =  -21496;
        out_egress->gamut_row2_q16[0] =   -7995;
        out_egress->gamut_row2_q16[1] =  -37290;
        out_egress->gamut_row2_q16[2] =  110821;
        out_egress->peak_nits         = 500;
    } else if (target_gamut == FLOW_GAMUT_REC2020_PQ) {
        /* LMS -> ITU-R BT.2020 (each row sums to 65536 = D65 white point preserved) */
        out_egress->gamut_row0_q16[0] =  159646;
        out_egress->gamut_row0_q16[1] = -104202;
        out_egress->gamut_row0_q16[2] =   10092;
        out_egress->gamut_row1_q16[0] =  -57344;
        out_egress->gamut_row1_q16[1] =  141558;
        out_egress->gamut_row1_q16[2] =  -18678;
        out_egress->gamut_row2_q16[0] =   -6554;
        out_egress->gamut_row2_q16[1] =  -28180;
        out_egress->gamut_row2_q16[2] =  100270;
        out_egress->peak_nits         = 1000;
    } else if (target_gamut == FLOW_GAMUT_ACES_CG) {
        /* LMS -> Academy ACEScg AP1 (each row sums to 65536 = D65 white point preserved) */
        out_egress->gamut_row0_q16[0] =  163840;
        out_egress->gamut_row0_q16[1] = -108134;
        out_egress->gamut_row0_q16[2] =    9830;
        out_egress->gamut_row1_q16[0] =  -58982;
        out_egress->gamut_row1_q16[1] =  144179;
        out_egress->gamut_row1_q16[2] =  -19661;
        out_egress->gamut_row2_q16[0] =   -6554;
        out_egress->gamut_row2_q16[1] =  -29491;
        out_egress->gamut_row2_q16[2] =  101581;
        out_egress->peak_nits         = 1000;
    } else {
        /* Default: LMS -> ITU-R BT.709 / sRGB (each row sums to 65536) */
        out_egress->gamut_row0_q16[0] =  267181;
        out_egress->gamut_row0_q16[1] = -216740;
        out_egress->gamut_row0_q16[2] =   15095;
        out_egress->gamut_row1_q16[0] =  -83129;
        out_egress->gamut_row1_q16[1] =  170914;
        out_egress->gamut_row1_q16[2] =  -22249;
        out_egress->gamut_row2_q16[0] =    -275;
        out_egress->gamut_row2_q16[1] =  -46099;
        out_egress->gamut_row2_q16[2] =  111910;
        out_egress->peak_nits         = 100;
    }
    out_egress->egress_mac = flow_capsule64_calc_mac(out_egress, 0x4547524553533634ULL);
}

static int64_t cube_q16(int64_t x_q16) {
    int64_t sq = (x_q16 * x_q16) / 65536LL;
    return (sq * x_q16) / 65536LL;
}

void flow_stream_egress64_map_oklab_to_rgb_q16(const FlowStreamEgress64 *egress,
                                               int32_t l_q16,
                                               int32_t a_q16,
                                               int32_t b_q16,
                                               int32_t out_rgb_q16[3]) {
    if (!out_rgb_q16) {
        return;
    }
    int64_t L = (int64_t)l_q16;
    int64_t A = (int64_t)a_q16;
    int64_t B = (int64_t)b_q16;

    /* OKLab (L, a, b) -> non-linear cone response (l', m', s') in Q16.16 */
    int64_t lp = L + ( 25974LL * A + 14143LL * B) / 65536LL;
    int64_t mp = L + ( -6918LL * A -  4185LL * B) / 65536LL;
    int64_t sp = L + ( -5864LL * A - 84639LL * B) / 65536LL;

    int64_t l_lin = cube_q16(lp);
    int64_t m_lin = cube_q16(mp);
    int64_t s_lin = cube_q16(sp);

    if (!egress) {
        out_rgb_q16[0] = (int32_t)flow_lean_abi_eval_clamp(l_lin, 0LL, 65536LL);
        out_rgb_q16[1] = (int32_t)flow_lean_abi_eval_clamp(m_lin, 0LL, 65536LL);
        out_rgb_q16[2] = (int32_t)flow_lean_abi_eval_clamp(s_lin, 0LL, 65536LL);
        return;
    }

    int64_t r = ((int64_t)egress->gamut_row0_q16[0] * l_lin +
                 (int64_t)egress->gamut_row0_q16[1] * m_lin +
                 (int64_t)egress->gamut_row0_q16[2] * s_lin) / 65536LL;
    int64_t g = ((int64_t)egress->gamut_row1_q16[0] * l_lin +
                 (int64_t)egress->gamut_row1_q16[1] * m_lin +
                 (int64_t)egress->gamut_row1_q16[2] * s_lin) / 65536LL;
    int64_t b = ((int64_t)egress->gamut_row2_q16[0] * l_lin +
                 (int64_t)egress->gamut_row2_q16[1] * m_lin +
                 (int64_t)egress->gamut_row2_q16[2] * s_lin) / 65536LL;

    out_rgb_q16[0] = (int32_t)flow_lean_abi_eval_clamp(r, 0LL, 65536LL);
    out_rgb_q16[1] = (int32_t)flow_lean_abi_eval_clamp(g, 0LL, 65536LL);
    out_rgb_q16[2] = (int32_t)flow_lean_abi_eval_clamp(b, 0LL, 65536LL);
}

void flow_stream_egress64_rgb_to_yuv420_bt709(const int32_t rgb_q16[3],
                                              uint8_t *out_y,
                                              uint8_t *out_u,
                                              uint8_t *out_v) {
    int64_t r8 = rgb_q16 ? ((flow_lean_abi_eval_clamp((int64_t)rgb_q16[0], 0LL, 65536LL) * 255LL) / 65536LL) : 0LL;
    int64_t g8 = rgb_q16 ? ((flow_lean_abi_eval_clamp((int64_t)rgb_q16[1], 0LL, 65536LL) * 255LL) / 65536LL) : 0LL;
    int64_t b8 = rgb_q16 ? ((flow_lean_abi_eval_clamp((int64_t)rgb_q16[2], 0LL, 65536LL) * 255LL) / 65536LL) : 0LL;

    /* ITU-R BT.709 Studio-Swing YCbCr: Y in [16..235], Cb/Cr in [16..240] */
    int64_t y = 16LL  + (( 47LL * r8 + 157LL * g8 +  16LL * b8 + 128LL) >> 8U);
    int64_t u = 128LL + ((-25LL * r8 -  87LL * g8 + 112LL * b8 + 128LL) >> 8U);
    int64_t v = 128LL + ((112LL * r8 - 102LL * g8 -  10LL * b8 + 128LL) >> 8U);

    if (out_y) *out_y = (uint8_t)flow_lean_abi_eval_clamp(y, 16LL, 235LL);
    if (out_u) *out_u = (uint8_t)flow_lean_abi_eval_clamp(u, 16LL, 240LL);
    if (out_v) *out_v = (uint8_t)flow_lean_abi_eval_clamp(v, 16LL, 240LL);
}

static size_t append_u32_dec(char *dst, size_t pos, size_t cap, uint32_t val) {
    char tmp[10];
    size_t n = 0U;
    if (val == 0U) {
        tmp[n++] = '0';
    } else {
        while (val > 0U && n < sizeof(tmp)) {
            tmp[n++] = (char)('0' + (val % 10U));
            val /= 10U;
        }
    }
    while (n > 0U && pos < cap) {
        dst[pos++] = tmp[--n];
    }
    return pos;
}

size_t flow_stream_egress64_encode_kitty_rgb24(const uint8_t *rgb24_pixels,
                                               uint32_t width,
                                               uint32_t height,
                                               char *out_buf,
                                               size_t max_buf_len) {
    static const char b64_lut[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!rgb24_pixels || !out_buf || width == 0U || height == 0U || max_buf_len < 32U) {
        return 0U;
    }
    size_t raw_bytes = (size_t)width * (size_t)height * 3U;
    size_t b64_len = ((raw_bytes + 2U) / 3U) * 4U;
    /* Header: "\x1b_Ga=T,f=24,s=<w>,v=<h>,q=2;" (max ~36B) + b64_len + "\x1b\\" (2B) + NUL */
    if (b64_len + 40U > max_buf_len) {
        return 0U;
    }

    size_t pos = 0U;
    const char *prefix = "\x1b_Ga=T,f=24,s=";
    for (size_t i = 0U; prefix[i] != '\0'; ++i) {
        out_buf[pos++] = prefix[i];
    }
    pos = append_u32_dec(out_buf, pos, max_buf_len, width);
    out_buf[pos++] = ',';
    out_buf[pos++] = 'v';
    out_buf[pos++] = '=';
    pos = append_u32_dec(out_buf, pos, max_buf_len, height);
    const char *mid = ",q=2;";
    for (size_t i = 0U; mid[i] != '\0'; ++i) {
        out_buf[pos++] = mid[i];
    }

    size_t i = 0U;
    while (i + 3U <= raw_bytes) {
        uint32_t triple = ((uint32_t)rgb24_pixels[i] << 16U) |
                          ((uint32_t)rgb24_pixels[i + 1U] << 8U) |
                          (uint32_t)rgb24_pixels[i + 2U];
        out_buf[pos++] = b64_lut[(triple >> 18U) & 0x3FU];
        out_buf[pos++] = b64_lut[(triple >> 12U) & 0x3FU];
        out_buf[pos++] = b64_lut[(triple >> 6U) & 0x3FU];
        out_buf[pos++] = b64_lut[triple & 0x3FU];
        i += 3U;
    }
    size_t rem = raw_bytes - i;
    if (rem == 1U) {
        uint32_t triple = (uint32_t)rgb24_pixels[i] << 16U;
        out_buf[pos++] = b64_lut[(triple >> 18U) & 0x3FU];
        out_buf[pos++] = b64_lut[(triple >> 12U) & 0x3FU];
        out_buf[pos++] = '=';
        out_buf[pos++] = '=';
    } else if (rem == 2U) {
        uint32_t triple = ((uint32_t)rgb24_pixels[i] << 16U) |
                          ((uint32_t)rgb24_pixels[i + 1U] << 8U);
        out_buf[pos++] = b64_lut[(triple >> 18U) & 0x3FU];
        out_buf[pos++] = b64_lut[(triple >> 12U) & 0x3FU];
        out_buf[pos++] = b64_lut[(triple >> 6U) & 0x3FU];
        out_buf[pos++] = '=';
    }
    out_buf[pos++] = '\x1b';
    out_buf[pos++] = '\\';
    out_buf[pos] = '\0';
    return pos;
}

/* ============================================================================
 * 10. 64-Byte Constitutive Physical Matter State & Branchless Optical Operator
 * ============================================================================ */

void flow_matter_state64_init(FlowMatterState64 *out_matter,
                              uint32_t matter_id,
                              int32_t ior_eta_q16,
                              int32_t extinction_kappa_q16,
                              int32_t viscosity_quench_q16,
                              int32_t surface_relief_q16,
                              int32_t micro_roughness_q16,
                              int32_t mie_scatter_q16,
                              const int32_t absorb_rgb_q16[3],
                              int32_t lattice_wire_q16,
                              int32_t thickness_mm_q16,
                              uint32_t thermal_kelvin,
                              uint32_t density_kg_m3) {
    if (!out_matter) {
        return;
    }
    memset(out_matter, 0, sizeof(*out_matter));
    out_matter->matter_id      = matter_id;
    out_matter->density_kg_m3  = (density_kg_m3 > 0U) ? density_kg_m3 : 1000U;
    out_matter->thermal_kelvin = (thermal_kelvin > 0U) ? thermal_kelvin : 295U;

    /* Gladstone-Dale law (\eta = 1 + k_GD * \rho) if ior_eta_q16 <= 0 */
    int64_t eta = (int64_t)ior_eta_q16;
    if (eta <= 0LL) {
        eta = 65536LL + (int64_t)out_matter->density_kg_m3 * 14LL;
    }
    out_matter->ior_eta_q16          = (int32_t)flow_lean_abi_eval_clamp(eta, 65536LL, 262144LL);
    out_matter->extinction_kappa_q16 = (int32_t)flow_lean_abi_eval_clamp((int64_t)extinction_kappa_q16, 0LL, 524288LL);
    out_matter->viscosity_quench_q16 = (int32_t)flow_lean_abi_eval_clamp((int64_t)viscosity_quench_q16, 0LL, 65536LL);
    out_matter->surface_relief_q16   = (int32_t)flow_lean_abi_eval_clamp((int64_t)surface_relief_q16, 0LL, 131072LL);
    out_matter->micro_roughness_q16  = (int32_t)flow_lean_abi_eval_clamp((int64_t)micro_roughness_q16, 0LL, 65536LL);
    out_matter->mie_scatter_q16      = (int32_t)flow_lean_abi_eval_clamp((int64_t)mie_scatter_q16, 0LL, 65536LL);

    for (size_t c = 0U; c < 3U; ++c) {
        int64_t a = absorb_rgb_q16 ? (int64_t)absorb_rgb_q16[c] : 0LL;
        out_matter->absorb_rgb_q16[c] = (int32_t)flow_lean_abi_eval_clamp(a, 0LL, 262144LL);
    }

    out_matter->lattice_wire_q16 = (int32_t)flow_lean_abi_eval_clamp((int64_t)lattice_wire_q16, 0LL, 65536LL);
    out_matter->thickness_mm_q16 = (int32_t)flow_lean_abi_eval_clamp(
        (thickness_mm_q16 > 0) ? (int64_t)thickness_mm_q16 : 65536LL, 1024LL, 1638400LL
    );
    out_matter->matter_mac = flow_capsule64_calc_mac(out_matter, 0x4D41545445523634ULL);
}

static int hex_nibble_val(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

void flow_matter_state64_format_hex128(const FlowMatterState64 *matter,
                                       char out_hex[129]) {
    static const char hex_lut[] = "0123456789abcdef";
    if (!out_hex) {
        return;
    }
    if (!matter) {
        memset(out_hex, '0', 128U);
        out_hex[128] = '\0';
        return;
    }
    const uint8_t *raw = (const uint8_t *)matter;
    for (size_t i = 0U; i < 64U; ++i) {
        out_hex[i * 2U]      = hex_lut[(raw[i] >> 4U) & 0x0FU];
        out_hex[i * 2U + 1U] = hex_lut[raw[i] & 0x0FU];
    }
    out_hex[128] = '\0';
}

bool flow_matter_state64_parse_hex128(const char *hex128,
                                      FlowMatterState64 *out_matter) {
    if (!hex128 || !out_matter) {
        return false;
    }
    FlowMatterState64 candidate;
    uint8_t *raw = (uint8_t *)&candidate;
    for (size_t i = 0U; i < 64U; ++i) {
        int hi = hex_nibble_val(hex128[i * 2U]);
        int lo = hex_nibble_val(hex128[i * 2U + 1U]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        raw[i] = (uint8_t)((hi << 4) | lo);
    }
    uint64_t expected_mac = flow_capsule64_calc_mac(&candidate, 0x4D41545445523634ULL);
    if (candidate.matter_mac != expected_mac || expected_mac == 0ULL) {
        return false;
    }
    *out_matter = candidate;
    return true;
}

int32_t flow_matter_eval_fresnel_q16(const FlowMatterState64 *matter,
                                     int32_t cos_theta_q16) {
    if (!matter) {
        return 0;
    }
    int64_t eta   = flow_lean_abi_eval_clamp((int64_t)matter->ior_eta_q16, 65536LL, 262144LL);
    int64_t kappa = flow_lean_abi_eval_clamp((int64_t)matter->extinction_kappa_q16, 0LL, 524288LL);

    int64_t dm = eta - 65536LL;
    int64_t dp = eta + 65536LL;
    int64_t k2 = (kappa * kappa) / 65536LL;
    int64_t num = (dm * dm) / 65536LL + k2;
    int64_t den = (dp * dp) / 65536LL + k2;
    if (den <= 0LL) {
        den = 65536LL;
    }
    int64_t f0 = flow_lean_abi_eval_clamp((num * 65536LL) / den, 0LL, 65536LL);

    int64_t cos_t = flow_lean_abi_eval_clamp((int64_t)cos_theta_q16, 0LL, 65536LL);
    int64_t m1 = 65536LL - cos_t;
    int64_t m2 = (m1 * m1) / 65536LL;
    int64_t m4 = (m2 * m2) / 65536LL;
    int64_t m5 = (m4 * m1) / 65536LL;

    int64_t f_theta = f0 + ((65536LL - f0) * m5) / 65536LL;
    return (int32_t)flow_lean_abi_eval_clamp(f_theta, 0LL, 65536LL);
}

int32_t flow_matter_eval_vogel_radius_q16(const FlowMatterState64 *matter) {
    if (!matter) {
        return 0;
    }
    int64_t eta     = flow_lean_abi_eval_clamp((int64_t)matter->ior_eta_q16, 65536LL, 262144LL);
    int64_t sigma_r = flow_lean_abi_eval_clamp((int64_t)matter->micro_roughness_q16, 0LL, 65536LL);
    int64_t mu_s    = flow_lean_abi_eval_clamp((int64_t)matter->mie_scatter_q16, 0LL, 65536LL);
    int64_t d_mm    = flow_lean_abi_eval_clamp((int64_t)matter->thickness_mm_q16, 1024LL, 1638400LL);

    /* Snell paraxial factor (1 - 1/\eta) in Q16.16 */
    int64_t snell_q16 = ((eta - 65536LL) * 65536LL) / eta;
    /* Normalized thickness factor d / 8mm in Q16.16 */
    int64_t d_norm = flow_lean_abi_eval_clamp(d_mm / 8LL, 4096LL, 131072LL);

    int64_t surf_cone = (snell_q16 * sigma_r) / 65536LL;
    int64_t total_scatter = surf_cone + (mu_s * 3LL) / 4LL;
    int64_t r_vogel = (d_norm * total_scatter) / 65536LL;
    return (int32_t)flow_lean_abi_eval_clamp(r_vogel, 0LL, 65536LL);
}

int32_t flow_matter_eval_caustic_gain_q16(const FlowMatterState64 *matter,
                                          int32_t laplacian_h_q16) {
    if (!matter) {
        return 65536;
    }
    int64_t eta    = flow_lean_abi_eval_clamp((int64_t)matter->ior_eta_q16, 65536LL, 262144LL);
    int64_t relief = flow_lean_abi_eval_clamp((int64_t)matter->surface_relief_q16, 0LL, 131072LL);
    int64_t snell_q16 = ((eta - 65536LL) * 65536LL) / eta;

    /* Focal convergence term: z_focus * (1 - 1/\eta) * \nabla^2 h */
    int64_t focus_scale = (snell_q16 * relief) / 65536LL;
    int64_t defocus_term = (focus_scale * (int64_t)laplacian_h_q16) / 65536LL;
    int64_t jacobian_det = 65536LL - defocus_term;
    if (jacobian_det < 0LL) {
        jacobian_det = -jacobian_det;
    }
    if (jacobian_det < 16384LL) {
        jacobian_det = 16384LL; /* Cap maximum caustic focus at 4.0x (262144 Q16) */
    }
    int64_t raw_gain = (65536LL * 65536LL) / jacobian_det;

    /* Scattering (\sigma_r + \mu_s) smoothly diffuses sharp caustics toward 1.0x */
    int64_t scatter_damp = flow_lean_abi_eval_clamp(
        ((int64_t)matter->micro_roughness_q16 + (int64_t)matter->mie_scatter_q16) / 2LL,
        0LL, 65536LL
    );
    int64_t gain = ((raw_gain * (65536LL - scatter_damp)) + 65536LL * scatter_damp) / 65536LL;
    return (int32_t)flow_lean_abi_eval_clamp(gain, 16384LL, 262144LL);
}

static int64_t eval_pade_exp_neg_q16(int64_t x_q16) {
    if (x_q16 <= 0LL) {
        return 65536LL;
    }
    int64_t x2_half = (x_q16 * x_q16) / 131072LL;
    int64_t den = 65536LL + x_q16 + x2_half;
    return flow_lean_abi_eval_clamp((65536LL * 65536LL) / den, 0LL, 65536LL);
}

void flow_matter_eval_radiance_rgb_q16(const FlowMatterState64 *matter,
                                       const int32_t grad_h_q16[2],
                                       int32_t laplacian_h_q16,
                                       const int32_t bg_rgb_q16[3],
                                       const int32_t light_delta_q16[2],
                                       int32_t out_rgb_q16[3]) {
    if (!out_rgb_q16) {
        return;
    }
    int64_t bg[3] = {
        bg_rgb_q16 ? (int64_t)bg_rgb_q16[0] : 32768LL,
        bg_rgb_q16 ? (int64_t)bg_rgb_q16[1] : 32768LL,
        bg_rgb_q16 ? (int64_t)bg_rgb_q16[2] : 32768LL
    };
    if (!matter) {
        out_rgb_q16[0] = (int32_t)bg[0];
        out_rgb_q16[1] = (int32_t)bg[1];
        out_rgb_q16[2] = (int32_t)bg[2];
        return;
    }

    int64_t gx = grad_h_q16 ? (int64_t)grad_h_q16[0] : 0LL;
    int64_t gy = grad_h_q16 ? (int64_t)grad_h_q16[1] : 0LL;
    int64_t relief = flow_lean_abi_eval_clamp((int64_t)matter->surface_relief_q16, 0LL, 131072LL);
    gx = (gx * relief) / 65536LL;
    gy = (gy * relief) / 65536LL;

    /* 1. Surface Normal N = normalize(-gx, -gy, 1.0) -> cos_theta = N.z in Q16.16 */
    int64_t g2 = (gx * gx + gy * gy) / 65536LL;
    int64_t cos_theta = (65536LL * 65536LL) / (65536LL + g2 / 2LL);
    int64_t f_theta = (int64_t)flow_matter_eval_fresnel_q16(matter, (int32_t)cos_theta);

    /* 2. Complex refractive index skin-depth attenuation (continuous dielectric -> metal) */
    int64_t kappa = flow_lean_abi_eval_clamp((int64_t)matter->extinction_kappa_q16, 0LL, 524288LL);
    int64_t skin_trans = eval_pade_exp_neg_q16(kappa * 2LL);
    int64_t transmit_weight = ((65536LL - f_theta) * skin_trans) / 65536LL;

    /* 3. Beer-Lambert spectral volume transmittance + Mie subsurface back-scatter */
    int64_t d_norm = flow_lean_abi_eval_clamp((int64_t)matter->thickness_mm_q16 / 8LL, 4096LL, 196608LL);
    int64_t mu_s   = flow_lean_abi_eval_clamp((int64_t)matter->mie_scatter_q16, 0LL, 65536LL);
    int64_t opt_path = (d_norm * (65536LL + mu_s / 2LL)) / 65536LL;

    int64_t beer_rgb[3];
    for (size_t c = 0U; c < 3U; ++c) {
        int64_t mu_a = flow_lean_abi_eval_clamp((int64_t)matter->absorb_rgb_q16[c], 0LL, 262144LL);
        beer_rgb[c] = eval_pade_exp_neg_q16((mu_a * opt_path) / 65536LL);
    }

    /* 4. 2-Jet Analytic Jacobian Caustics & Embedded Lattice Wire Occlusion */
    int64_t caustic_gain = (int64_t)flow_matter_eval_caustic_gain_q16(matter, laplacian_h_q16);
    int64_t wire_occ = flow_lean_abi_eval_clamp(
        65536LL - ((int64_t)matter->lattice_wire_q16 * 45875LL) / 65536LL,
        16384LL, 65536LL
    );

    /* 5. Micro-Pebble Softbox Light Vector Bending: u_bent = light_delta + 0.35 * grad_h */
    int64_t lx = (light_delta_q16 ? (int64_t)light_delta_q16[0] : 0LL) + (gx * 22938LL) / 65536LL;
    int64_t ly = (light_delta_q16 ? (int64_t)light_delta_q16[1] : 0LL) + (gy * 22938LL) / 65536LL;
    int64_t l_dist2 = (lx * lx + ly * ly) / 65536LL;
    int64_t softbox_spec = eval_pade_exp_neg_q16(l_dist2 * 3LL);

    /* 6. Planck Blackbody Thermal Emission for T > 650K (molten silicate / lava) */
    int64_t t_kelvin = (int64_t)matter->thermal_kelvin;
    int64_t emit_r = 0LL, emit_g = 0LL, emit_b = 0LL;
    if (t_kelvin > 650LL) {
        int64_t excess = flow_lean_abi_eval_clamp(t_kelvin - 650LL, 0LL, 2000LL);
        emit_r = (excess * 65536LL) / 700LL;
        emit_g = (excess * excess * 65536LL) / (950LL * 950LL);
        emit_b = (excess > 450LL) ? (((excess - 450LL) * 65536LL) / 1100LL) : 0LL;
    }

    for (size_t c = 0U; c < 3U; ++c) {
        /* Transmitted & caustics-focused background + Mie turbid subsurface scatter */
        int64_t trans_c = (bg[c] * beer_rgb[c]) / 65536LL;
        trans_c = (trans_c * caustic_gain) / 65536LL;
        int64_t sss_c = (beer_rgb[c] * mu_s * 24576LL) / (65536LL * 65536LL);
        int64_t body_c = ((trans_c + sss_c) * wire_occ) / 65536LL;

        /* Dielectric + Metallic complex Fresnel reflection */
        int64_t refl_c = ((49152LL + softbox_spec) * f_theta) / 65536LL;
        int64_t emit_c = (c == 0U) ? emit_r : ((c == 1U) ? emit_g : emit_b);

        int64_t total_c = (body_c * transmit_weight) / 65536LL + refl_c + emit_c;
        out_rgb_q16[c] = (int32_t)flow_lean_abi_eval_clamp(total_c, 0LL, 262144LL);
    }
}

void flow_gpu_bus1k_update_slot64(FlowEngineBus1K *inout_bus,
                                  uint32_t slot_id,
                                  const void *block64) {
    if (!inout_bus || !block64 || slot_id >= FLOW_ENGINE_BUS_SLOTS) {
        return;
    }
    memcpy(&inout_bus->slots[slot_id], block64, sizeof(FlowEngineBus64));
}

void flow_gpu_bus1k_sync_svg_nodes(FlowEngineBus1K *inout_bus,
                                   const FlowSvgUiNode64 *svg_nodes,
                                   size_t node_count) {
    if (!inout_bus || !svg_nodes) {
        return;
    }
    size_t count = (node_count <= 8U) ? node_count : 8U;
    for (size_t i = 0U; i < count; ++i) {
        memcpy(&inout_bus->slots[FLOW_BUS_SLOT_SVG_UI_0 + i], &svg_nodes[i], sizeof(FlowSvgUiNode64));
    }
}

void flow_cloud_room64_init(FlowCloudRoomDesc64 *out_room,
                            uint32_t room_id,
                            uint32_t map_block_count,
                            uint32_t active_views,
                            uint32_t tile_width_px,
                            uint32_t tile_height_px,
                            uint32_t sla_budget_us,
                            uint64_t shared_world_hash) {
    if (!out_room) {
        return;
    }
    memset(out_room, 0, sizeof(*out_room));
    out_room->room_id = room_id;

    /* Round or clamp map_block_count to power-of-2 mask (2^k - 1) */
    uint32_t pow2_blocks = 1U;
    while (pow2_blocks < map_block_count && pow2_blocks < (1U << 30U)) {
        pow2_blocks <<= 1U;
    }
    out_room->map_block_mask = pow2_blocks - 1U;

    uint32_t views = (uint32_t)flow_lean_abi_eval_clamp((int64_t)active_views, 1LL, 16LL);
    out_room->active_view_count = views;

    /* Select minimal power-of-2 Tile-Atlas grid (cols x rows >= views) */
    if (views == 1U) {
        out_room->grid_cols = 1U;
        out_room->grid_rows = 1U;
    } else if (views == 2U) {
        out_room->grid_cols = 2U;
        out_room->grid_rows = 1U;
    } else if (views <= 4U) {
        out_room->grid_cols = 2U;
        out_room->grid_rows = 2U;
    } else if (views <= 8U) {
        out_room->grid_cols = 4U;
        out_room->grid_rows = 2U;
    } else {
        out_room->grid_cols = 4U;
        out_room->grid_rows = 4U;
    }

    out_room->tile_width_px     = (tile_width_px > 0U) ? tile_width_px : 960U;
    out_room->tile_height_px    = (tile_height_px > 0U) ? tile_height_px : 540U;
    out_room->sla_budget_us     = (sla_budget_us > 0U) ? sla_budget_us : 8333U;
    out_room->measured_frame_us = 0U;
    out_room->lod_degrade_level = 0U;
    out_room->active_seat_mask  = (views >= 16U) ? 0xFFFFU : ((1U << views) - 1U);
    out_room->egress_stream_fmt = FLOW_STREAM_FMT_YUV420P_PIPE;
    out_room->shared_world_hash = shared_world_hash;
    out_room->room_mac = flow_capsule64_calc_mac(out_room, 0x434C4F5544524D36ULL);
}

uint32_t flow_cloud_room64_route_pixel_to_seat(const FlowCloudRoomDesc64 *room,
                                               uint32_t atlas_x_px,
                                               uint32_t atlas_y_px,
                                               int32_t *out_local_ndc_x_q16,
                                               int32_t *out_local_ndc_y_q16) {
    if (out_local_ndc_x_q16) *out_local_ndc_x_q16 = 0;
    if (out_local_ndc_y_q16) *out_local_ndc_y_q16 = 0;
    if (!room || room->tile_width_px == 0U || room->tile_height_px == 0U ||
        room->grid_cols == 0U || room->grid_rows == 0U) {
        return 0xFFFFFFFFU;
    }
    uint32_t col_idx = atlas_x_px / room->tile_width_px;
    uint32_t row_idx = atlas_y_px / room->tile_height_px;
    if (col_idx >= room->grid_cols || row_idx >= room->grid_rows) {
        return 0xFFFFFFFFU;
    }
    uint32_t seat_idx = row_idx * room->grid_cols + col_idx;
    if (seat_idx >= 16U || ((room->active_seat_mask >> seat_idx) & 1U) == 0U) {
        return 0xFFFFFFFFU;
    }

    uint32_t local_x = atlas_x_px % room->tile_width_px;
    uint32_t local_y = atlas_y_px % room->tile_height_px;
    int64_t ndc_x = (((int64_t)local_x * 131072LL) / (int64_t)room->tile_width_px) - 65536LL;
    int64_t ndc_y = (((int64_t)local_y * 131072LL) / (int64_t)room->tile_height_px) - 65536LL;
    if (out_local_ndc_x_q16) {
        *out_local_ndc_x_q16 = (int32_t)flow_lean_abi_eval_clamp(ndc_x, -65536LL, 65536LL);
    }
    if (out_local_ndc_y_q16) {
        *out_local_ndc_y_q16 = (int32_t)flow_lean_abi_eval_clamp(ndc_y, -65536LL, 65536LL);
    }
    return seat_idx;
}

uint32_t flow_cloud_room64_step_sla_governor(FlowCloudRoomDesc64 *inout_room,
                                             uint32_t measured_frame_us) {
    if (!inout_room) {
        return 0U;
    }
    inout_room->measured_frame_us = measured_frame_us;
    uint32_t budget = (inout_room->sla_budget_us > 0U) ? inout_room->sla_budget_us : 8333U;
    int64_t lod = (int64_t)inout_room->lod_degrade_level;
    if (measured_frame_us > budget) {
        lod = flow_lean_abi_eval_clamp(lod + 1LL, 0LL, 3LL);
    } else if (measured_frame_us < (budget * 3U) / 4U && lod > 0LL) {
        lod = flow_lean_abi_eval_clamp(lod - 1LL, 0LL, 3LL);
    }
    inout_room->lod_degrade_level = (uint32_t)lod;
    inout_room->room_mac = flow_capsule64_calc_mac(inout_room, 0x434C4F5544524D36ULL);
    return inout_room->lod_degrade_level;
}
