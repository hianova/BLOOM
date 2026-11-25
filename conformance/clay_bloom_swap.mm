#include "bloom.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define CLAY_IMPLEMENTATION
#include "../build/conformance_deps/clay.h"
#pragma clang diagnostic pop

struct AppState {
    BloomAtlas *atlas = nullptr;
    int32_t selected_item = 1;
    uint16_t badge_uv[4]{};
    float vp_w = 1280.0f;
    float vp_h = 720.0f;
    uint32_t last_cmd_count = 0u;
    int hovered_item = -1;
};

static uint32_t clay_rgba8(Clay_Color c) {
    auto ch = [](float v) -> uint32_t {
        long q = lroundf(std::max(0.0f, std::min(255.0f, v)));
        return (uint32_t)q;
    };
    return BLOOM_RGBA(ch(c.r), ch(c.g), ch(c.b), ch(c.a));
}

static void clay_err_handler(Clay_ErrorData err) {
    fprintf(stderr, "[Clay Error %d] %.*s\n", (int)err.errorType, err.errorText.length, err.errorText.chars);
}

static Clay_Dimensions clay_bloom_measure_text(Clay_StringSlice text, Clay_TextElementConfig *cfg, void *ud) {
    auto *app = static_cast<AppState *>(ud);
    float fpx = (cfg && cfg->fontSize > 0u) ? (float)cfg->fontSize : 15.0f;
    float mw = 0.0f, mh = 0.0f;
    if (app && app->atlas && text.length > 0 && text.chars) {
        std::string s(text.chars, (size_t)text.length);
        bloom_atlas_measure_text(app->atlas, s.c_str(), fpx, 0.0f, &mw, &mh);
    }
    if (mw < 0.5f && text.length > 0) mw = fpx * 0.45f * (float)text.length;
    if (mh < fpx) mh = fpx * 1.25f;
    return Clay_Dimensions{mw, mh};
}

struct ClipParent {
    BloomHandle handle;
    float x;
    float y;
};

static uint32_t clay_bloom_submit(BloomScene *scene, Clay_RenderCommandArray cmds, float vp_w, float vp_h) {
    bloom_scene_clear_ui(scene);
    BloomUiNodeDesc root{};
    root.id = "clay_root";
    root.kind = BLOOM_UI_PANEL;
    root.x = 0.0f;
    root.y = 0.0f;
    root.w = vp_w;
    root.h = vp_h;
    BloomHandle root_h = bloom_scene_add_ui(scene, &root);

    std::vector<ClipParent> stack;
    stack.push_back(ClipParent{root_h, 0.0f, 0.0f});

    uint32_t submitted = 0u;
    for (int32_t i = 0; i < cmds.length; ++i) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (!cmd) continue;
        const Clay_BoundingBox &bb = cmd->boundingBox;
        const ClipParent &cur = stack.back();

        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "clay_%d", i);

        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                BloomUiNodeDesc d{};
                d.id = id_buf;
                d.parent = cur.handle;
                d.kind = BLOOM_UI_PANEL;
                d.pos_absolute = true;
                d.x = bb.x - cur.x;
                d.y = bb.y - cur.y;
                d.w = std::max(1.0f, bb.width);
                d.h = std::max(1.0f, bb.height);
                BloomHandle h = bloom_scene_add_ui(scene, &d);
                stack.push_back(ClipParent{h, bb.x, bb.y});
                ++submitted;
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                if (stack.size() > 1u) stack.pop_back();
                ++submitted;
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                BloomUiNodeDesc d{};
                d.id = id_buf;
                d.parent = cur.handle;
                d.kind = BLOOM_UI_PANEL;
                d.pos_absolute = true;
                d.x = bb.x - cur.x;
                d.y = bb.y - cur.y;
                d.w = bb.width;
                d.h = bb.height;
                d.fill_rgba8 = clay_rgba8(cmd->renderData.rectangle.backgroundColor);
                d.radius_px = cmd->renderData.rectangle.cornerRadius.topLeft;
                bloom_scene_add_ui(scene, &d);
                ++submitted;
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                const Clay_BorderRenderData &br = cmd->renderData.border;
                uint16_t bw = std::max({br.width.left, br.width.right, br.width.top, br.width.bottom});
                BloomUiNodeDesc d{};
                d.id = id_buf;
                d.parent = cur.handle;
                d.kind = BLOOM_UI_PANEL;
                d.pos_absolute = true;
                d.x = bb.x - cur.x;
                d.y = bb.y - cur.y;
                d.w = bb.width;
                d.h = bb.height;
                d.border_rgba8 = clay_rgba8(br.color);
                d.border_px = (float)bw;
                d.radius_px = br.cornerRadius.topLeft;
                bloom_scene_add_ui(scene, &d);
                ++submitted;
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                const Clay_TextRenderData &tr = cmd->renderData.text;
                if (tr.stringContents.length <= 0 || !tr.stringContents.chars) break;
                std::string txt(tr.stringContents.chars, (size_t)tr.stringContents.length);
                BloomUiNodeDesc d{};
                d.id = id_buf;
                d.parent = cur.handle;
                d.kind = BLOOM_UI_TEXT;
                d.pos_absolute = true;
                d.x = bb.x - cur.x;
                d.y = bb.y - cur.y;
                d.w = bb.width + 6.0f;
                d.h = bb.height + 4.0f;
                d.pad = 0.0f;
                d.font_px = (float)(tr.fontSize > 0u ? tr.fontSize : 15u);
                d.text_rgba8 = clay_rgba8(tr.textColor);
                d.label = txt.c_str();
                bloom_scene_add_ui(scene, &d);
                ++submitted;
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                const auto *uv = static_cast<const uint16_t *>(cmd->renderData.image.imageData);
                if (!uv) break;
                BloomUiNodeDesc d{};
                d.id = id_buf;
                d.parent = cur.handle;
                d.kind = BLOOM_UI_SPRITE;
                d.pos_absolute = true;
                d.x = bb.x - cur.x;
                d.y = bb.y - cur.y;
                d.w = bb.width;
                d.h = bb.height;
                memcpy(d.sprite_uv_unorm16, uv, sizeof(d.sprite_uv_unorm16));
                d.sprite_tint_rgba8 = clay_rgba8(cmd->renderData.image.backgroundColor);
                d.radius_px = cmd->renderData.image.cornerRadius.topLeft;
                bloom_scene_add_ui(scene, &d);
                ++submitted;
                break;
            }
            default:
                break;
        }
    }
    return submitted;
}

static Clay_String clay_dyn_str(const char *s, std::vector<std::string> &pool) {
    pool.emplace_back(s);
    const std::string &ref = pool.back();
    Clay_String cs{};
    cs.isStaticallyAllocated = false;
    cs.length = (int32_t)ref.size();
    cs.chars = ref.c_str();
    return cs;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
static Clay_RenderCommandArray build_clay_ui(AppState *app, std::vector<std::string> &str_pool) {
    str_pool.clear();
    str_pool.reserve(128);
    app->hovered_item = -1;

    const Clay_Color c_bg = {14, 18, 26, 255};
    const Clay_Color c_hdr = {22, 29, 42, 255};
    const Clay_Color c_panel = {18, 24, 34, 255};
    const Clay_Color c_card = {26, 35, 50, 255};
    const Clay_Color c_sel = {38, 78, 128, 255};
    const Clay_Color c_hov = {32, 46, 68, 255};
    const Clay_Color c_accent = {56, 208, 255, 255};
    const Clay_Color c_border = {56, 78, 108, 255};
    const Clay_Color c_txt = {234, 242, 252, 255};
    const Clay_Color c_muted = {152, 172, 198, 255};

    Clay_BeginLayout();

    CLAY(CLAY_ID("AppRoot"), {
        .layout = {
            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = c_bg
    }) {
        /* Top Header Bar */
        CLAY(CLAY_ID("HeaderBar"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48)},
                .padding = {16, 16, 10, 10},
                .childGap = 16,
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}
            },
            .backgroundColor = c_hdr,
            .border = {.color = c_border, .width = {0, 0, 0, 1, 0}}
        }) {
            CLAY(CLAY_ID("BadgeSprite"), {
                .layout = {.sizing = {CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24)}},
                .cornerRadius = CLAY_CORNER_RADIUS(4),
                .image = {.imageData = app->badge_uv}
            }) {}
            CLAY_TEXT(CLAY_STRING("BLOOM x Clay UI Engine Swap (OSS Drop-In)"),
                      CLAY_TEXT_CONFIG({.textColor = c_txt, .fontSize = 16}));
            CLAY(CLAY_ID("HdrSpacer"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}}}) {}
            CLAY(CLAY_ID("MetalPill"), {
                .layout = {.padding = {10, 10, 4, 4}},
                .backgroundColor = {20, 62, 82, 255},
                .cornerRadius = CLAY_CORNER_RADIUS(6),
                .border = {.color = c_accent, .width = CLAY_BORDER_OUTSIDE(1)}
            }) {
                CLAY_TEXT(CLAY_STRING("Metal 3 Direct / Zero SDL"),
                          CLAY_TEXT_CONFIG({.textColor = c_accent, .fontSize = 13}));
            }
        }

        /* Main 3-Pane IDE Body */
        CLAY(CLAY_ID("BodyRow"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(12),
                .childGap = 12,
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            }
        }) {
            /* Left Sidebar: 16 selectable scene items */
            CLAY(CLAY_ID("Sidebar"), {
                .layout = {
                    .sizing = {CLAY_SIZING_FIXED(230), CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(10),
                    .childGap = 4,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = c_panel,
                .cornerRadius = CLAY_CORNER_RADIUS(8),
                .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()},
                .border = {.color = c_border, .width = CLAY_BORDER_OUTSIDE(1)}
            }) {
                CLAY_TEXT(CLAY_STRING("場景節點清單 (16 Nodes)"),
                          CLAY_TEXT_CONFIG({.textColor = c_muted, .fontSize = 13}));
                for (int i = 0; i < 16; ++i) {
                    bool is_sel = (i == app->selected_item);
                    CLAY(CLAY_IDI("SideRow", (uint32_t)i), {
                        .layout = {
                            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(32)},
                            .padding = {10, 10, 6, 6},
                            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}
                        },
                        .backgroundColor = is_sel ? c_sel : (Clay_Hovered() ? c_hov : Clay_Color{24, 31, 44, 255}),
                        .cornerRadius = CLAY_CORNER_RADIUS(5),
                        .border = is_sel ? Clay_BorderElementConfig{.color = c_accent, .width = CLAY_BORDER_OUTSIDE(1)}
                                         : Clay_BorderElementConfig{}
                    }) {
                        if (Clay_Hovered()) app->hovered_item = i;
                        char lbl[48];
                        snprintf(lbl, sizeof(lbl), "#%02d Node_Asset_%02d", i, i);
                        CLAY_TEXT(clay_dyn_str(lbl, str_pool),
                                  CLAY_TEXT_CONFIG({.textColor = is_sel ? c_txt : c_muted, .fontSize = 14}));
                    }
                }
            }

            /* Center Scrollable Document Viewport */
            CLAY(CLAY_ID("DocScroll"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(14),
                    .childGap = 10,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = c_panel,
                .cornerRadius = CLAY_CORNER_RADIUS(8),
                .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()},
                .border = {.color = c_border, .width = CLAY_BORDER_OUTSIDE(1)}
            }) {
                CLAY_TEXT(CLAY_STRING("滾動檢查視窗 — Clay Scissor Clip + BLOOM CoreText"),
                          CLAY_TEXT_CONFIG({.textColor = c_txt, .fontSize = 16}));
                for (int c = 0; c < 12; ++c) {
                    bool active_card = (c == (app->selected_item % 12));
                    CLAY(CLAY_IDI("DocCard", (uint32_t)c), {
                        .layout = {
                            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                            .padding = CLAY_PADDING_ALL(12),
                            .childGap = 6,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        },
                        .backgroundColor = active_card ? Clay_Color{30, 52, 82, 255} : c_card,
                        .cornerRadius = CLAY_CORNER_RADIUS(8),
                        .border = {.color = active_card ? c_accent : c_border, .width = CLAY_BORDER_OUTSIDE(1)}
                    }) {
                        char title[64];
                        snprintf(title, sizeof(title), "資產節點卡片 #%02d — PBR Meshlet Cluster", c);
                        CLAY_TEXT(clay_dyn_str(title, str_pool),
                                  CLAY_TEXT_CONFIG({.textColor = c_txt, .fontSize = 15}));
                        char body[96];
                        snprintf(body, sizeof(body),
                                 "Clay 立即模式排版 -> BLOOM Single-CB GPU Quad/Text/Clip (Idx %d)", c);
                        CLAY_TEXT(clay_dyn_str(body, str_pool),
                                  CLAY_TEXT_CONFIG({.textColor = c_muted, .fontSize = 13}));
                    }
                }
            }

            /* Right Property Inspector */
            CLAY(CLAY_ID("Inspector"), {
                .layout = {
                    .sizing = {CLAY_SIZING_FIXED(260), CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(12),
                    .childGap = 10,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = c_panel,
                .cornerRadius = CLAY_CORNER_RADIUS(8),
                .border = {.color = c_border, .width = CLAY_BORDER_OUTSIDE(1)}
            }) {
                CLAY_TEXT(CLAY_STRING("屬性檢視器 (Inspector)"),
                          CLAY_TEXT_CONFIG({.textColor = c_txt, .fontSize = 15}));
                char sel_str[64];
                snprintf(sel_str, sizeof(sel_str), "Selected: Node_Asset_%02d", app->selected_item);
                CLAY_TEXT(clay_dyn_str(sel_str, str_pool),
                          CLAY_TEXT_CONFIG({.textColor = c_accent, .fontSize = 14}));
                for (int p = 0; p < 5; ++p) {
                    const char *labels[5] = {
                        "Roughness: 0.42",
                        "Metallic: 0.85",
                        "Meshlets: 64v / 96t",
                        "Zero Heap Churn: OK",
                        "Idle Skip: Active"
                    };
                    CLAY(CLAY_IDI("PropBox", (uint32_t)p), {
                        .layout = {
                            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34)},
                            .padding = {10, 10, 8, 8},
                            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}
                        },
                        .backgroundColor = c_card,
                        .cornerRadius = CLAY_CORNER_RADIUS(6)
                    }) {
                        CLAY_TEXT(clay_dyn_str(labels[p], str_pool),
                                  CLAY_TEXT_CONFIG({.textColor = c_txt, .fontSize = 13}));
                    }
                }
            }
        }

        /* Footer Status Bar */
        CLAY(CLAY_ID("FooterBar"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30)},
                .padding = {16, 16, 6, 6},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}
            },
            .backgroundColor = c_hdr,
            .border = {.color = c_border, .width = {0, 0, 1, 0, 0}}
        }) {
            char st[96];
            snprintf(st, sizeof(st), "Viewport %.0fx%.0f | Active Node #%02d | Commands >= 100",
                     app->vp_w, app->vp_h, app->selected_item);
            CLAY_TEXT(clay_dyn_str(st, str_pool),
                      CLAY_TEXT_CONFIG({.textColor = c_muted, .fontSize = 12}));
        }
    }

    return Clay_EndLayout(0.016f);
}
#pragma clang diagnostic pop

int main(int argc, char **argv) {
    @autoreleasepool {
        std::string ppm_path;
        bool headless = true;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) ppm_path = argv[++i];
            else if (strcmp(argv[i], "--headless") == 0) headless = true;
        }
        (void)headless;

        BloomScene *scene = bloom_scene_create();
        BloomGpu *gpu = bloom_gpu_create(nullptr, nullptr);
        if (!scene || !gpu) {
            fprintf(stderr, "[FAIL] BLOOM init failed\n");
            return 1;
        }

        AppState app{};
        app.atlas = bloom_gpu_get_atlas(gpu);
        uint8_t badge_px[16u * 16u * 4u];
        for (uint32_t y = 0u; y < 16u; ++y) {
            for (uint32_t x = 0u; x < 16u; ++x) {
                uint8_t *p = &badge_px[(y * 16u + x) * 4u];
                bool ring = (x == 0 || y == 0 || x == 15 || y == 15 || (x > 4 && x < 11 && y > 4 && y < 11));
                p[0] = ring ? 56u : 22u;
                p[1] = ring ? 208u : 42u;
                p[2] = ring ? 255u : 68u;
                p[3] = 255u;
            }
        }
        bloom_atlas_add_sprite_rgba8(app.atlas, badge_px, 16u, 16u, app.badge_uv);

        uint32_t mem_sz = Clay_MinMemorySize();
        void *arena_mem = malloc(mem_sz);
        Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(mem_sz, arena_mem);
        Clay_Initialize(arena, Clay_Dimensions{app.vp_w, app.vp_h}, Clay_ErrorHandler{clay_err_handler, nullptr});
        Clay_SetMeasureTextFunction(clay_bloom_measure_text, &app);

        std::vector<std::string> str_pool;

        /* 1. Initial 1280x720 frame */
        Clay_SetPointerState(Clay_Vector2{-100.0f, -100.0f}, false);
        Clay_RenderCommandArray cmds0 = build_clay_ui(&app, str_pool);
        uint32_t sub0 = clay_bloom_submit(scene, cmds0, app.vp_w, app.vp_h);
        bloom_gpu_render_headless(gpu, scene, 1280u, 720u, true);
        uint32_t chk0 = bloom_gpu_headless_checksum(gpu);
        if (sub0 < 100u || cmds0.length < 100) {
            fprintf(stderr, "[FAIL] Expected >= 100 Clay cmds, got %d / submitted %u\n", cmds0.length, sub0);
            return 1;
        }
        if (!ppm_path.empty()) bloom_gpu_save_headless_ppm(gpu, ppm_path.c_str());

        /* 2. Hover + Click on sidebar row #4 */
        Clay_ElementData row4 = Clay_GetElementData(CLAY_IDI("SideRow", 4u));
        if (!row4.found) {
            fprintf(stderr, "[FAIL] SideRow[4] bounds missing\n");
            return 1;
        }
        Clay_SetPointerState(Clay_Vector2{row4.boundingBox.x + 20.0f, row4.boundingBox.y + 12.0f}, true);
        Clay_RenderCommandArray cmds_hov = build_clay_ui(&app, str_pool);
        if (app.hovered_item != 4) {
            fprintf(stderr, "[FAIL] Clay hover hit-test expected 4, got %d\n", app.hovered_item);
            return 1;
        }
        app.selected_item = 4;
        Clay_RenderCommandArray cmds_sel = build_clay_ui(&app, str_pool);
        clay_bloom_submit(scene, cmds_sel, app.vp_w, app.vp_h);
        bloom_gpu_render_headless(gpu, scene, 1280u, 720u, true);
        uint32_t chk_sel = bloom_gpu_headless_checksum(gpu);
        if (chk_sel == chk0) {
            fprintf(stderr, "[FAIL] Selection state change did not affect frame checksum\n");
            return 1;
        }
        (void)cmds_hov;

        /* 3. Scissor scroll test inside DocScroll + verify header top pixels untouched */
        std::vector<uint8_t> before_px(1280u * 720u * 4u);
        uint32_t rw = 0u, rh = 0u;
        bloom_gpu_readback_headless_rgba8(gpu, before_px.data(), (uint32_t)before_px.size(), &rw, &rh);

        Clay_ElementData doc_data = Clay_GetElementData(CLAY_ID("DocScroll"));
        Clay_SetPointerState(Clay_Vector2{doc_data.boundingBox.x + 120.0f, doc_data.boundingBox.y + 120.0f}, false);
        Clay_UpdateScrollContainers(false, Clay_Vector2{0.0f, -180.0f}, 1.0f / 60.0f);
        Clay_RenderCommandArray cmds_scr = build_clay_ui(&app, str_pool);
        clay_bloom_submit(scene, cmds_scr, app.vp_w, app.vp_h);
        bloom_gpu_render_headless(gpu, scene, 1280u, 720u, true);
        uint32_t chk_scr = bloom_gpu_headless_checksum(gpu);

        std::vector<uint8_t> after_px(1280u * 720u * 4u);
        bloom_gpu_readback_headless_rgba8(gpu, after_px.data(), (uint32_t)after_px.size(), &rw, &rh);
        /* Header strip y=4..40 should be byte-identical after scrolling DocScroll (scissor containment) */
        for (uint32_t y = 4u; y < 40u; ++y) {
            if (memcmp(&before_px[(size_t)y * rw * 4u], &after_px[(size_t)y * rw * 4u], (size_t)rw * 4u) != 0) {
                fprintf(stderr, "[FAIL] Scissor bleed detected into header row %u!\n", y);
                return 1;
            }
        }
        if (chk_scr == chk_sel) {
            fprintf(stderr, "[FAIL] Scroll did not move document content\n");
            return 1;
        }

        /* 4. Dynamic resize torture (1920x1080 -> 800x600 -> 1280x720) */
        const uint32_t sizes[3][2] = {{1920u, 1080u}, {800u, 600u}, {1280u, 720u}};
        for (auto &sz : sizes) {
            app.vp_w = (float)sz[0];
            app.vp_h = (float)sz[1];
            Clay_SetLayoutDimensions(Clay_Dimensions{app.vp_w, app.vp_h});
            Clay_RenderCommandArray rc = build_clay_ui(&app, str_pool);
            clay_bloom_submit(scene, rc, app.vp_w, app.vp_h);
            if (!bloom_gpu_render_headless(gpu, scene, sz[0], sz[1], true)) return 1;
        }

        /* 5. 300-frame dynamic rebuild soak benchmark */
        std::vector<double> gpu_times;
        gpu_times.reserve(300u);
        for (int f = 0; f < 300; ++f) {
            Clay_RenderCommandArray rc = build_clay_ui(&app, str_pool);
            clay_bloom_submit(scene, rc, app.vp_w, app.vp_h);
            bloom_gpu_render_headless(gpu, scene, 1280u, 720u, true);
            gpu_times.push_back(bloom_gpu_get_stats(gpu).last_gpu_ms);
        }
        std::sort(gpu_times.begin(), gpu_times.end());
        double med_ms = gpu_times[gpu_times.size() / 2u];
        double p99_ms = gpu_times[(gpu_times.size() * 99u) / 100u];

        printf("[CLAY-BLOOM-SWAP] PASS: cmds=%d submitted=%u hover=%d chk0=0x%08x->sel=0x%08x->scr=0x%08x | 300f med=%.3f ms p99=%.3f ms\n",
               cmds0.length, sub0, app.hovered_item, chk0, chk_sel, chk_scr, med_ms, p99_ms);

        if (med_ms >= 1.5) {
            fprintf(stderr, "[FAIL] Clay soak med GPU %.3f ms >= 1.5 ms\n", med_ms);
            return 1;
        }

        free(arena_mem);
        bloom_gpu_destroy(gpu);
        bloom_scene_destroy(scene);
        return 0;
    }
}
