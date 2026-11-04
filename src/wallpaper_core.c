#include "flow_wallpaper.h"
#include <stdio.h>
#include <string.h>

static inline uint64_t wp_splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

static uint64_t calc_wallpaper_mac64(const FlowWallpaperState64 *wp) {
    uint64_t h = 0x57414C4C50415052ULL ^ wp->master_seed;
    h = wp_splitmix64(h ^ (((uint64_t)(uint32_t)wp->cursor_x_q16 << 32U) | (uint32_t)wp->cursor_y_q16));
    h = wp_splitmix64(h ^ (((uint64_t)(uint32_t)wp->parallax_offset_x_q16 << 32U) | (uint32_t)wp->parallax_offset_y_q16));
    h = wp_splitmix64(h ^ (((uint64_t)(uint32_t)wp->audio_reactive_env_q16 << 32U) | (uint32_t)wp->occlusion_ratio_q16));
    h = wp_splitmix64(h ^ (((uint64_t)wp->battery_percent << 32U) | wp->ac_power_connected));
    h = wp_splitmix64(h ^ (((uint64_t)wp->power_mode << 32U) | wp->target_frame_interval_us));
    h = wp_splitmix64(h ^ (((uint64_t)wp->vcam_preset_id << 32U) | wp->finish_and_flags));
    return (h == 0ULL) ? 1ULL : h;
}

static uint64_t calc_host_config_mac64(const FlowDesktopHostConfig64 *host) {
    uint64_t h = 0x484F535443464736ULL ^ host->native_window_handle;
    h = wp_splitmix64(h ^ (((uint64_t)host->os_platform_id << 32U) | host->desktop_z_level));
    h = wp_splitmix64(h ^ (((uint64_t)host->screen_width_px << 32U) | host->screen_height_px));
    h = wp_splitmix64(h ^ (((uint64_t)host->click_through_enabled << 32U) | host->stationary_all_spaces));
    h = wp_splitmix64(h ^ (((uint64_t)host->audio_muted_by_default << 32U) | host->active_refresh_hz));
    return (h == 0ULL) ? 1ULL : h;
}

void flow_wallpaper_init64(FlowWallpaperState64 *wp,
                           uint64_t master_seed,
                           uint32_t vcam_preset_id,
                           uint32_t finish_class,
                           bool audio_muted) {
    if (!wp)
        return;
    memset(wp, 0, sizeof(*wp));
    wp->master_seed = (master_seed == 0ULL) ? 0x20261105ULL : master_seed;
    wp->cursor_x_q16 = 0;
    wp->cursor_y_q16 = 0;
    wp->parallax_offset_x_q16 = 0;
    wp->parallax_offset_y_q16 = 0;
    wp->audio_reactive_env_q16 = 0;
    wp->occlusion_ratio_q16 = 0;
    wp->battery_percent = 100U;
    wp->ac_power_connected = 1U;
    wp->power_mode = FLOW_WP_POWER_ACTIVE_60HZ;
    wp->target_frame_interval_us = FLOW_WP_INTERVAL_ACTIVE_60HZ_US;
    wp->vcam_preset_id = vcam_preset_id & 3U;
    wp->finish_and_flags = (finish_class & 3U) |
                           FLOW_WP_FLAG_CLICK_THROUGH |
                           FLOW_WP_FLAG_ALL_SPACES |
                           (audio_muted ? FLOW_WP_FLAG_AUDIO_MUTE : 0U);
    wp->wallpaper_mac = calc_wallpaper_mac64(wp);
}

void flow_wallpaper_host_init64(FlowDesktopHostConfig64 *host,
                                uint32_t os_platform_id,
                                uint32_t width_px,
                                uint32_t height_px,
                                bool audio_muted) {
    if (!host)
        return;
    memset(host, 0, sizeof(*host));
    host->native_window_handle = 0ULL;
    host->os_platform_id = os_platform_id;
    host->desktop_z_level = 1U;
    host->screen_width_px = (width_px > 0U) ? width_px : 3840U;
    host->screen_height_px = (height_px > 0U) ? height_px : 2160U;
    host->click_through_enabled = 1U;
    host->stationary_all_spaces = 1U;
    host->audio_muted_by_default = audio_muted ? 1U : 0U;
    host->multi_monitor_index = 0U;
    host->active_refresh_hz = 60U;
    host->eco_pause_transitions = 0U;
    host->frames_rendered_total = 0U;
    host->frames_skipped_sleep = 0U;
    host->host_seal_mac = calc_host_config_mac64(host);
}

bool flow_wallpaper_step_governor64(FlowWallpaperState64 *wp,
                                    FlowDesktopHostConfig64 *host,
                                    int32_t occlusion_ratio_q16,
                                    uint32_t battery_percent,
                                    bool ac_power_connected) {
    if (!wp)
        return false;

    if (occlusion_ratio_q16 < 0)
        occlusion_ratio_q16 = 0;
    if (occlusion_ratio_q16 > 65536)
        occlusion_ratio_q16 = 65536;
    if (battery_percent > 100U)
        battery_percent = 100U;

    wp->occlusion_ratio_q16 = occlusion_ratio_q16;
    wp->battery_percent = battery_percent;
    wp->ac_power_connected = ac_power_connected ? 1U : 0U;

    bool prev_paused = (wp->power_mode == FLOW_WP_POWER_PAUSED_0HZ);
    bool prev_eco    = (wp->power_mode == FLOW_WP_POWER_BATTERY_30HZ);

    /* 1. Dual-Threshold Occlusion Pause Hysteresis ([75%, 90%] = [49152, 58982] Q16) */
    bool next_paused = prev_paused;
    if (occlusion_ratio_q16 >= FLOW_WP_OCCLUSION_PAUSE_ENTER_Q16) {
        next_paused = true;
    } else if (occlusion_ratio_q16 <= FLOW_WP_OCCLUSION_PAUSE_EXIT_Q16) {
        next_paused = false;
    }

    /* 2. Dual-Threshold Battery Eco Hysteresis ([40%, 50%]) */
    bool next_eco = prev_eco;
    if (ac_power_connected) {
        next_eco = false;
    } else if (battery_percent <= FLOW_WP_BATTERY_ECO_ENTER_PCT) {
        next_eco = true;
    } else if (battery_percent >= FLOW_WP_BATTERY_ECO_EXIT_PCT) {
        next_eco = false;
    }

    if (next_paused) {
        wp->power_mode = FLOW_WP_POWER_PAUSED_0HZ;
        wp->target_frame_interval_us = FLOW_WP_INTERVAL_PAUSED_0HZ_US;
    } else if (next_eco) {
        wp->power_mode = FLOW_WP_POWER_BATTERY_30HZ;
        wp->target_frame_interval_us = FLOW_WP_INTERVAL_ECO_30HZ_US;
    } else {
        wp->power_mode = FLOW_WP_POWER_ACTIVE_60HZ;
        wp->target_frame_interval_us = FLOW_WP_INTERVAL_ACTIVE_60HZ_US;
    }

    if (host) {
        if (prev_paused != next_paused) {
            host->eco_pause_transitions++;
        }
        if (next_paused) {
            host->active_refresh_hz = 0U;
            host->frames_skipped_sleep++;
        } else if (next_eco) {
            host->active_refresh_hz = 30U;
            host->frames_rendered_total++;
        } else {
            host->active_refresh_hz = 60U;
            host->frames_rendered_total++;
        }
        host->host_seal_mac = calc_host_config_mac64(host);
    }

    wp->wallpaper_mac = calc_wallpaper_mac64(wp);
    return !next_paused;
}

void flow_wallpaper_step_parallax_q16(FlowWallpaperState64 *wp,
                                      int32_t desktop_cursor_x_q16,
                                      int32_t desktop_cursor_y_q16,
                                      int32_t audio_sample_abs_q16) {
    if (!wp)
        return;

    if (desktop_cursor_x_q16 < -65536) desktop_cursor_x_q16 = -65536;
    if (desktop_cursor_x_q16 >  65536) desktop_cursor_x_q16 =  65536;
    if (desktop_cursor_y_q16 < -65536) desktop_cursor_y_q16 = -65536;
    if (desktop_cursor_y_q16 >  65536) desktop_cursor_y_q16 =  65536;

    wp->cursor_x_q16 = desktop_cursor_x_q16;
    wp->cursor_y_q16 = desktop_cursor_y_q16;

    /* Target parallax is 1/4 of normalized cursor offset in [-16384, +16384] */
    int32_t target_px = desktop_cursor_x_q16 >> 2;
    int32_t target_py = desktop_cursor_y_q16 >> 2;

    /* Critically-damped 2-Jet EMA step: offset += (target - offset) >> 2 */
    int32_t next_px = wp->parallax_offset_x_q16 + ((target_px - wp->parallax_offset_x_q16) >> 2);
    int32_t next_py = wp->parallax_offset_y_q16 + ((target_py - wp->parallax_offset_y_q16) >> 2);

    if (next_px < -FLOW_WP_MAX_PARALLAX_Q16) next_px = -FLOW_WP_MAX_PARALLAX_Q16;
    if (next_px >  FLOW_WP_MAX_PARALLAX_Q16) next_px =  FLOW_WP_MAX_PARALLAX_Q16;
    if (next_py < -FLOW_WP_MAX_PARALLAX_Q16) next_py = -FLOW_WP_MAX_PARALLAX_Q16;
    if (next_py >  FLOW_WP_MAX_PARALLAX_Q16) next_py =  FLOW_WP_MAX_PARALLAX_Q16;

    wp->parallax_offset_x_q16 = next_px;
    wp->parallax_offset_y_q16 = next_py;

    /* Fast attack, smooth decay audio envelope in [0, 65536] */
    if (audio_sample_abs_q16 < 0) audio_sample_abs_q16 = -audio_sample_abs_q16;
    if (audio_sample_abs_q16 > 65536) audio_sample_abs_q16 = 65536;
    if (audio_sample_abs_q16 > wp->audio_reactive_env_q16) {
        wp->audio_reactive_env_q16 = audio_sample_abs_q16;
    } else {
        wp->audio_reactive_env_q16 = (int32_t)(((int64_t)wp->audio_reactive_env_q16 * 57344LL) >> 16U);
    }

    wp->wallpaper_mac = calc_wallpaper_mac64(wp);
}

void flow_wallpaper_encode_hex128(const FlowWallpaperState64 *wp, char out_hex[129]) {
    static const char kHexDigits[16] = {
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
    };
    if (!wp || !out_hex)
        return;
    const uint8_t *bytes = (const uint8_t *)wp;
    for (size_t i = 0U; i < 64U; ++i) {
        out_hex[i * 2U + 0U] = kHexDigits[(bytes[i] >> 4U) & 0x0FU];
        out_hex[i * 2U + 1U] = kHexDigits[bytes[i] & 0x0FU];
    }
    out_hex[128] = '\0';
}

static int hex_nibble_val(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    return -1;
}

bool flow_wallpaper_decode_hex128(const char *hex_str, FlowWallpaperState64 *out_wp) {
    if (!hex_str || !out_wp)
        return false;
    for (size_t i = 0U; i < 128U; ++i) {
        if (hex_str[i] == '\0')
            return false;
    }
    FlowWallpaperState64 tmp;
    uint8_t *bytes = (uint8_t *)&tmp;
    for (size_t i = 0U; i < 64U; ++i) {
        int hi = hex_nibble_val(hex_str[i * 2U + 0U]);
        int lo = hex_nibble_val(hex_str[i * 2U + 1U]);
        if (hi < 0 || lo < 0)
            return false;
        bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    if (!flow_wallpaper_verify_mac64(&tmp))
        return false;
    *out_wp = tmp;
    return true;
}

bool flow_wallpaper_verify_mac64(const FlowWallpaperState64 *wp) {
    if (!wp || wp->wallpaper_mac == 0ULL)
        return false;
    return wp->wallpaper_mac == calc_wallpaper_mac64(wp);
}

int flow_wallpaper_export_web_runner(const FlowWallpaperState64 *wp, const char *html_path) {
    if (!wp || !html_path)
        return -1;
    char hex128[129];
    flow_wallpaper_encode_hex128(wp, hex128);

    FILE *f = fopen(html_path, "w");
    if (!f)
        return -1;

    fprintf(f,
        "<!DOCTYPE html>\n"
        "<html lang=\"zh-Hant\">\n"
        "<head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">\n"
        "<title>FLOW Wallpaper Engine — 64B Zero-Token Cross-Platform Live Desktop</title>\n"
        "<style>\n"
        "*{box-sizing:border-box;margin:0;padding:0}\n"
        "body{background:#02040a;color:#e2e8f0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,monospace;overflow:hidden;user-select:none}\n"
        "canvas{position:fixed;inset:0;width:100vw;height:100vh;display:block;z-index:1}\n"
        "#hud{position:fixed;bottom:20px;right:20px;z-index:10;background:rgba(6,11,22,0.78);backdrop-filter:blur(12px);"
        "border:1px solid rgba(56,189,248,0.32);border-radius:10px;padding:12px 16px;width:420px;font-size:12px;box-shadow:0 12px 32px rgba(0,0,0,0.65)}\n"
        "#hud h1{font-size:13px;color:#38bdf8;letter-spacing:0.06em;margin-bottom:6px;display:flex;justify-content:space-between}\n"
        ".row{display:flex;gap:6px;margin-top:6px;align-items:center}\n"
        "button{background:#0f172a;color:#93c5fd;border:1px solid #1e3a8a;border-radius:5px;padding:4px 8px;cursor:pointer;font-size:11px}\n"
        "button:hover{background:#1e293b;border-color:#38bdf8}\n"
        "input{background:#020617;color:#f8fafc;border:1px solid #334155;border-radius:4px;padding:4px 6px;font-family:monospace;font-size:10px;width:100%%}\n"
        ".badge{color:#fbbf24;font-weight:600}\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<canvas id=\"wp\"></canvas>\n"
        "<div id=\"hud\">\n"
        "  <h1><span>FLOW WALLPAPER ENGINE (64B SSOT)</span><span id=\"fps\" class=\"badge\">60 Hz ACTIVE</span></h1>\n"
        "  <div>Seed: <span id=\"seedLbl\">0x%016llX</span> | VCam: <span id=\"vcamLbl\">%u</span> | Finish: <span id=\"finLbl\">%u</span></div>\n"
        "  <div class=\"row\">\n"
        "    <button onclick=\"cycleVCam()\">[C] 切換鏡頭</button>\n"
        "    <button onclick=\"cycleFinish()\">[F] 高定塗裝</button>\n"
        "    <button onclick=\"nextSeed()\">[R] 隨機母種子</button>\n"
        "    <button onclick=\"toggleEco()\">[E] 模擬省電/休眠</button>\n"
        "  </div>\n"
        "  <div class=\"row\"><input id=\"hexBox\" readonly value=\"%s\" onclick=\"this.select()\"></div>\n"
        "</div>\n",
        (unsigned long long)wp->master_seed,
        wp->vcam_preset_id,
        wp->finish_and_flags & 3U,
        hex128);

    fputs(
        "<script>\n"
        "const cv=document.getElementById('wp'),gl=cv.getContext('webgl2')||cv.getContext('webgl');\n"
        "let seed=", f);
    fprintf(f, "%u,vcam=%u,fin=%u,ecoMode=0,mx=0,my=0,px=0,py=0,t0=performance.now();\n",
            (uint32_t)(wp->master_seed & 0xFFFFFFFFULL),
            wp->vcam_preset_id & 3U,
            wp->finish_and_flags & 3U);
    fputs(
        "window.addEventListener('mousemove',e=>{mx=(e.clientX/window.innerWidth)*2-1;my=(e.clientY/window.innerHeight)*2-1;});\n"
        "function resize(){cv.width=window.innerWidth*devicePixelRatio;cv.height=window.innerHeight*devicePixelRatio;if(gl)gl.viewport(0,0,cv.width,cv.height);}\n"
        "window.addEventListener('resize',resize);resize();\n"
        "function cycleVCam(){vcam=(vcam+1)&3;document.getElementById('vcamLbl').textContent=vcam;}\n"
        "function cycleFinish(){fin=(fin+1)&3;document.getElementById('finLbl').textContent=fin;}\n"
        "function nextSeed(){seed=(seed+0x9E3779B9)>>>0;document.getElementById('seedLbl').textContent='0x'+seed.toString(16).toUpperCase();}\n"
        "function toggleEco(){ecoMode=(ecoMode+1)%3;document.getElementById('fps').textContent=ecoMode===0?'60 Hz ACTIVE':(ecoMode===1?'30 Hz ECO':'0 Hz PAUSED');}\n"
        "const vs=`attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}`;\n"
        "const fs=`precision highp float;uniform vec2 res,par;uniform float tm,vc,fn;\n"
        "void main(){\n"
        "  vec2 uv=(gl_FragCoord.xy-0.5*res)/res.y - par*0.18;\n"
        "  if(abs(uv.y)>0.42){gl_FragColor=vec4(0.01,0.02,0.04,1.0);return;}\n"
        "  float zoom=mix(1.35,0.82,vc*0.33);\n"
        "  vec3 ro=vec3(par.x*0.6,par.y*0.4,-3.2),rd=normalize(vec3(uv/zoom,1.1));\n"
        "  vec3 col=vec3(0.004,0.008,0.018);\n"
        "  float b=dot(ro,rd),c=dot(ro,ro)-1.15,d=b*b-c;\n"
        "  if(d>0.0){\n"
        "    float t=-b-sqrt(d);vec3 pos=ro+t*rd,nor=normalize(pos);\n"
        "    vec3 sun=normalize(vec3(0.6,0.4,-0.7));\n"
        "    float diff=max(dot(nor,sun),0.0),rim=pow(1.0-max(dot(-rd,nor),0.0),3.0);\n"
        "    float band=0.5+0.5*sin(nor.y*28.0+sin(nor.x*10.0+tm*0.6));\n"
        "    vec3 pal=fn<0.5?vec3(0.18,0.52,0.98):(fn<1.5?vec3(0.92,0.68,0.22):(fn<2.5?vec3(1.0,0.78,0.24):vec3(0.14,0.82,0.56)));\n"
        "    col=mix(vec3(0.08,0.12,0.20),pal,band)*(diff*1.3+0.12)+pal*rim*0.85;\n"
        "  }\n"
        "  gl_FragColor=vec4(pow(clamp(col,0.0,1.0),vec3(0.4545)),1.0);\n"
        "}`;\n"
        "if(gl){\n"
        "  const sh=(t,s)=>{const o=gl.createShader(t);gl.shaderSource(o,s);gl.compileShader(o);return o;};\n"
        "  const pr=gl.createProgram();gl.attachShader(pr,sh(gl.VERTEX_SHADER,vs));gl.attachShader(pr,sh(gl.FRAGMENT_SHADER,fs));gl.linkProgram(pr);gl.useProgram(pr);\n"
        "  const bf=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,bf);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),gl.STATIC_DRAW);\n"
        "  const loc=gl.getAttribLocation(pr,'p');gl.enableVertexAttribArray(loc);gl.vertexAttribPointer(loc,2,gl.FLOAT,false,0,0);\n"
        "  function tick(now){\n"
        "    requestAnimationFrame(tick);\n"
        "    if(document.hidden||ecoMode===2)return;\n"
        "    if(ecoMode===1&&(now-t0)<32)return;\n"
        "    t0=now;px+=(mx*0.25-px)*0.12;py+=(-my*0.25-py)*0.12;\n"
        "    gl.uniform2f(gl.getUniformLocation(pr,'res'),cv.width,cv.height);\n"
        "    gl.uniform2f(gl.getUniformLocation(pr,'par'),px,py);\n"
        "    gl.uniform1f(gl.getUniformLocation(pr,'tm'),now*0.001);\n"
        "    gl.uniform1f(gl.getUniformLocation(pr,'vc'),vcam);\n"
        "    gl.uniform1f(gl.getUniformLocation(pr,'fn'),fin);\n"
        "    gl.drawArrays(gl.TRIANGLE_STRIP,0,4);\n"
        "  }\n"
        "  requestAnimationFrame(tick);\n"
        "}\n"
        "</script>\n"
        "</body>\n"
        "</html>\n", f);

    fclose(f);
    return 0;
}
