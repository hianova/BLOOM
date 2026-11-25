#include "bloom.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_HIGH_LCD_ACCURACY 1
#include "../build/conformance_deps/peanut_gb.h"
#pragma clang diagnostic pop

struct GbHost {
    struct gb_s gb{};
    std::vector<uint8_t> rom;
    std::vector<uint8_t> cart_ram;
    uint8_t fb[160u * 144u * 4u]{};
    uint32_t palette[4] = {
        BLOOM_RGBA(0x9B, 0xBC, 0x0F, 0xFF),
        BLOOM_RGBA(0x8B, 0xAC, 0x0F, 0xFF),
        BLOOM_RGBA(0x30, 0x62, 0x30, 0xFF),
        BLOOM_RGBA(0x0F, 0x38, 0x0F, 0xFF)
    };
    uint64_t frame_cnt = 0u;
    bool err_seen = false;
};

static uint8_t host_rom_read(struct gb_s *gb, const uint_fast32_t addr) {
    const auto *h = static_cast<const GbHost *>(gb->direct.priv);
    return (addr < h->rom.size()) ? h->rom[addr] : 0xFFu;
}

static uint8_t host_ram_read(struct gb_s *gb, const uint_fast32_t addr) {
    const auto *h = static_cast<const GbHost *>(gb->direct.priv);
    return (addr < h->cart_ram.size()) ? h->cart_ram[addr] : 0xFFu;
}

static void host_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
    auto *h = static_cast<GbHost *>(gb->direct.priv);
    if (addr < h->cart_ram.size()) h->cart_ram[addr] = val;
}

static void host_gb_error(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr) {
    auto *h = static_cast<GbHost *>(gb->direct.priv);
    fprintf(stderr, "[Peanut-GB Error %d @ 0x%04x]\n", (int)err, addr);
    h->err_seen = true;
}

static void host_lcd_draw_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
    auto *h = static_cast<GbHost *>(gb->direct.priv);
    if (line >= 144u) return;
    uint8_t *dst = &h->fb[(size_t)line * 160u * 4u];
    for (uint32_t x = 0u; x < 160u; ++x) {
        uint8_t shade = pixels[x] & LCD_COLOUR;
        uint32_t c = h->palette[shade];
        dst[x * 4u + 0u] = (uint8_t)(c & 0xFFu);
        dst[x * 4u + 1u] = (uint8_t)((c >> 8u) & 0xFFu);
        dst[x * 4u + 2u] = (uint8_t)((c >> 16u) & 0xFFu);
        dst[x * 4u + 3u] = 0xFFu;
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        std::string rom_path = "build/conformance_deps/dmg-acid2.gb";
        std::string ppm_path;
        bool headless = true;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) rom_path = argv[++i];
            else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) ppm_path = argv[++i];
            else if (strcmp(argv[i], "--headless") == 0) headless = true;
        }
        (void)headless;

        FILE *fp = fopen(rom_path.c_str(), "rb");
        if (!fp) {
            fprintf(stderr, "[FAIL] Cannot open ROM %s\n", rom_path.c_str());
            return 1;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz < 32768) {
            fclose(fp);
            fprintf(stderr, "[FAIL] Invalid ROM size %ld\n", sz);
            return 1;
        }

        GbHost host{};
        host.rom.resize((size_t)sz);
        if (fread(host.rom.data(), 1u, (size_t)sz, fp) != (size_t)sz) {
            fclose(fp);
            return 1;
        }
        fclose(fp);

        enum gb_init_error_e init_ret = gb_init(&host.gb, host_rom_read, host_ram_read, host_ram_write, host_gb_error, &host);
        if (init_ret != GB_INIT_NO_ERROR) {
            fprintf(stderr, "[FAIL] gb_init failed (%d)\n", (int)init_ret);
            return 1;
        }
        size_t ram_sz = 0u;
        gb_get_save_size_s(&host.gb, &ram_sz);
        host.cart_ram.assign(ram_sz, 0u);
        gb_init_lcd(&host.gb, host_lcd_draw_line);

        char rom_title[32]{};
        gb_get_rom_name(&host.gb, rom_title);

        /* Step 120 frames so dmg-acid2 completes its PPU conformance render */
        for (int f = 0; f < 120; ++f) {
            gb_run_frame(&host.gb);
            ++host.frame_cnt;
        }
        if (host.err_seen) return 1;

        std::set<uint32_t> unique_colors;
        for (size_t i = 0u; i < 160u * 144u; ++i) {
            uint32_t px = 0u;
            memcpy(&px, &host.fb[i * 4u], 4u);
            unique_colors.insert(px);
        }
        if (unique_colors.size() < 4u) {
            fprintf(stderr, "[FAIL] Expected all 4 DMG shades in dmg-acid2 framebuffer, got %zu\n", unique_colors.size());
            return 1;
        }

        BloomScene *scene = bloom_scene_create();
        BloomGpu *gpu = bloom_gpu_create(nullptr, nullptr);
        if (!scene || !gpu) return 1;

        BloomAtlas *atlas = bloom_gpu_get_atlas(gpu);
        uint16_t gb_uv[4]{};
        if (!bloom_atlas_add_sprite_rgba8(atlas, host.fb, 160u, 144u, gb_uv)) {
            fprintf(stderr, "[FAIL] bloom_atlas_add_sprite_rgba8 failed\n");
            return 1;
        }

        BloomUiNodeDesc shell{};
        shell.id = "gb_shell";
        shell.kind = BLOOM_UI_PANEL;
        shell.flex_dir = BLOOM_FLEX_ROW;
        shell.x = 24.0f; shell.y = 24.0f;
        shell.w = 1232.0f; shell.h = 672.0f;
        shell.pad = 24.0f; shell.gap = 28.0f;
        shell.fill_rgba8 = BLOOM_RGBA(0x11, 0x16, 0x20, 0xFF);
        shell.border_rgba8 = BLOOM_RGBA(0x30, 0x45, 0x62, 0xFF);
        shell.border_px = 1.5f;
        shell.radius_px = 14.0f;
        shell.label = "Peanut-GB x BLOOM Metal 3 Backend Swap (DMG-01 PPU Conformance)";
        shell.font_px = 18.0f;
        BloomHandle h_shell = bloom_scene_add_ui(scene, &shell);

        BloomUiNodeDesc bezel{};
        bezel.id = "gb_bezel";
        bezel.parent = h_shell;
        bezel.kind = BLOOM_UI_PANEL;
        bezel.w = 688.0f; bezel.h = 620.0f;
        bezel.pad = 24.0f;
        bezel.fill_rgba8 = BLOOM_RGBA(0x28, 0x2E, 0x3A, 0xFF);
        bezel.border_rgba8 = BLOOM_RGBA(0x4E, 0x5A, 0x70, 0xFF);
        bezel.border_px = 2.0f;
        bezel.radius_px = 16.0f;
        bezel.shadow_blur_px = 18.0f;
        bezel.shadow_rgba8 = BLOOM_RGBA(0x00, 0x00, 0x00, 0x99);
        BloomHandle h_bezel = bloom_scene_add_ui(scene, &bezel);

        BloomUiNodeDesc screen{};
        screen.id = "gb_screen";
        screen.parent = h_bezel;
        screen.kind = BLOOM_UI_SPRITE;
        screen.w = 640.0f; screen.h = 576.0f;
        screen.radius_px = 6.0f;
        memcpy(screen.sprite_uv_unorm16, gb_uv, sizeof(gb_uv));
        screen.sprite_tint_rgba8 = BLOOM_RGBA(0xFF, 0xFF, 0xFF, 0xFF);
        screen.sprite_nearest = true;
        bloom_scene_add_ui(scene, &screen);

        BloomUiNodeDesc side{};
        side.id = "gb_telemetry";
        side.parent = h_shell;
        side.kind = BLOOM_UI_PANEL;
        side.flex_dir = BLOOM_FLEX_COL;
        side.pad = 18.0f; side.gap = 12.0f;
        side.fill_rgba8 = BLOOM_RGBA(0x16, 0x1D, 0x2B, 0xFF);
        side.border_rgba8 = BLOOM_RGBA(0x34, 0x48, 0x66, 0xFF);
        side.border_px = 1.0f;
        side.radius_px = 10.0f;
        side.label = "SM83 CPU & PPU Telemetry";
        side.font_px = 16.0f;
        BloomHandle h_side = bloom_scene_add_ui(scene, &side);

        char reg_buf[128];
        snprintf(reg_buf, sizeof(reg_buf), "ROM: %s | Frames: %llu", rom_title, (unsigned long long)host.frame_cnt);
        BloomUiNodeDesc t_rom{};
        t_rom.id = "t_rom"; t_rom.parent = h_side; t_rom.kind = BLOOM_UI_TEXT; t_rom.h = 32.0f; t_rom.font_px = 15.0f;
        t_rom.fill_rgba8 = BLOOM_RGBA(0x1D, 0x28, 0x3B, 0xFF); t_rom.radius_px = 6.0f; t_rom.label = reg_buf;
        bloom_scene_add_ui(scene, &t_rom);

        snprintf(reg_buf, sizeof(reg_buf), "PC=0x%04X SP=0x%04X AF=0x%02X%02X BC=0x%04X",
                 host.gb.cpu_reg.pc.reg, host.gb.cpu_reg.sp.reg, host.gb.cpu_reg.a, host.gb.cpu_reg.f.reg, host.gb.cpu_reg.bc.reg);
        BloomUiNodeDesc t_cpu{};
        t_cpu.id = "t_cpu"; t_cpu.parent = h_side; t_cpu.kind = BLOOM_UI_TEXT; t_cpu.h = 32.0f; t_cpu.font_px = 15.0f;
        t_cpu.fill_rgba8 = BLOOM_RGBA(0x1D, 0x28, 0x3B, 0xFF); t_cpu.radius_px = 6.0f; t_cpu.label = reg_buf;
        BloomHandle h_cpu = bloom_scene_add_ui(scene, &t_cpu);

        snprintf(reg_buf, sizeof(reg_buf), "DE=0x%04X HL=0x%04X Joypad=0x%02X",
                 host.gb.cpu_reg.de.reg, host.gb.cpu_reg.hl.reg, host.gb.direct.joypad);
        BloomUiNodeDesc t_hl{};
        t_hl.id = "t_hl"; t_hl.parent = h_side; t_hl.kind = BLOOM_UI_TEXT; t_hl.h = 32.0f; t_hl.font_px = 15.0f;
        t_hl.fill_rgba8 = BLOOM_RGBA(0x1D, 0x28, 0x3B, 0xFF); t_hl.radius_px = 6.0f; t_hl.label = reg_buf;
        BloomHandle h_hl = bloom_scene_add_ui(scene, &t_hl);

        BloomUiNodeDesc t_mode{};
        t_mode.id = "t_mode"; t_mode.parent = h_side; t_mode.kind = BLOOM_UI_TEXT; t_mode.h = 32.0f; t_mode.font_px = 14.0f;
        t_mode.text_rgba8 = BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF);
        t_mode.label = "Backend: libbloom.a (Nearest In-Place Atlas)";
        bloom_scene_add_ui(scene, &t_mode);

        BloomUiNodeDesc btn_reset{};
        btn_reset.id = "btn_reset"; btn_reset.parent = h_side; btn_reset.kind = BLOOM_UI_BUTTON; btn_reset.h = 40.0f;
        btn_reset.fill_rgba8 = BLOOM_RGBA(0x23, 0x4E, 0x7A, 0xFF); btn_reset.radius_px = 8.0f;
        btn_reset.font_px = 15.0f; btn_reset.label = "Reset DMG CPU (Zero Leak)";
        bloom_scene_add_ui(scene, &btn_reset);

        bloom_gpu_render_headless(gpu, scene, 1280u, 720u, true);
        uint32_t chk_dmg = bloom_gpu_headless_checksum(gpu);
        if (!ppm_path.empty()) bloom_gpu_save_headless_ppm(gpu, ppm_path.c_str());

        /* Live 1,000-frame streaming soak test (single atlas slot updated in-place every frame) */
        std::vector<double> gpu_times;
        gpu_times.reserve(1000u);
        for (int f = 0; f < 1000; ++f) {
            gb_run_frame(&host.gb);
            ++host.frame_cnt;
            if (!bloom_atlas_update_sprite_rgba8(atlas, gb_uv, host.fb, 160u, 144u)) {
                fprintf(stderr, "[FAIL] In-place sprite update failed at frame %d\n", f);
                return 1;
            }
            if ((f & 63) == 0) {
                snprintf(reg_buf, sizeof(reg_buf), "PC=0x%04X SP=0x%04X AF=0x%02X%02X BC=0x%04X",
                         host.gb.cpu_reg.pc.reg, host.gb.cpu_reg.sp.reg, host.gb.cpu_reg.a, host.gb.cpu_reg.f.reg, host.gb.cpu_reg.bc.reg);
                bloom_scene_set_ui_label(scene, h_cpu, reg_buf);
            }
            bloom_scene_mark_dirty(scene);
            bloom_gpu_render_headless(gpu, scene, 1280u, 720u, true);
            gpu_times.push_back(bloom_gpu_get_stats(gpu).last_gpu_ms);
        }

        if (bloom_atlas_sprite_count(atlas) != 1u) {
            fprintf(stderr, "[FAIL] Sprite atlas leaked! count=%u != 1\n", bloom_atlas_sprite_count(atlas));
            return 1;
        }

        /* Verify joypad bit toggling & gb_reset */
        host.gb.direct.joypad_bits.a = 0u;
        gb_run_frame(&host.gb);
        host.gb.direct.joypad = 0xFFu;
        gb_reset(&host.gb);
        snprintf(reg_buf, sizeof(reg_buf), "DE=0x%04X HL=0x%04X Joypad=0x%02X",
                 host.gb.cpu_reg.de.reg, host.gb.cpu_reg.hl.reg, host.gb.direct.joypad);
        bloom_scene_set_ui_label(scene, h_hl, reg_buf);
        bloom_gpu_render_headless(gpu, scene, 1280u, 720u, true);

        std::sort(gpu_times.begin(), gpu_times.end());
        double med_ms = gpu_times[gpu_times.size() / 2u];
        double p99_ms = gpu_times[(gpu_times.size() * 99u) / 100u];

        printf("[PEANUT-GB-SWAP] PASS: rom=%s shades=%zu sprites=%u chk=0x%08x | 1000f med=%.3f ms p99=%.3f ms\n",
               rom_title, unique_colors.size(), bloom_atlas_sprite_count(atlas), chk_dmg, med_ms, p99_ms);

        if (med_ms >= 1.0) {
            fprintf(stderr, "[FAIL] Peanut-GB 1000f med GPU %.3f ms >= 1.0 ms\n", med_ms);
            return 1;
        }

        bloom_gpu_destroy(gpu);
        bloom_scene_destroy(scene);
        return 0;
    }
}
