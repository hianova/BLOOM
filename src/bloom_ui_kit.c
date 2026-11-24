#include "bloom_ui_kit.h"
#include <string.h>

BloomTheme bloom_theme_dark(void) {
    BloomTheme t;
    t.bg_canvas     = BLOOM_RGBA(0x0B, 0x0F, 0x17, 0xFF);
    t.bg_surface    = BLOOM_RGBA(0x13, 0x1A, 0x26, 0xF0);
    t.bg_elevated   = BLOOM_RGBA(0x1D, 0x28, 0x3A, 0xF8);
    t.bg_input      = BLOOM_RGBA(0x0F, 0x15, 0x20, 0xFF);
    t.border_subtle = BLOOM_RGBA(0x2D, 0x41, 0x5B, 0xBB);
    t.border_focus  = BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF);
    t.accent        = BLOOM_RGBA(0x2B, 0x7A, 0xCE, 0xFF);
    t.accent_hover  = BLOOM_RGBA(0x3A, 0x90, 0xEB, 0xFF);
    t.danger        = BLOOM_RGBA(0xC9, 0x3B, 0x52, 0xFF);
    t.success       = BLOOM_RGBA(0x2E, 0xC8, 0x7A, 0xFF);
    t.text_primary  = BLOOM_RGBA(0xEC, 0xF3, 0xFD, 0xFF);
    t.text_muted    = BLOOM_RGBA(0x88, 0x99, 0xB0, 0xFF);
    t.radius_sm     = 5.0f;
    t.radius_md     = 10.0f;
    t.radius_lg     = 14.0f;
    t.pad_md        = 14.0f;
    t.gap_md        = 10.0f;
    t.font_sm       = 13.0f;
    t.font_md       = 15.0f;
    t.font_lg       = 18.0f;
    t.shadow_rgba8  = BLOOM_RGBA(0x00, 0x00, 0x00, 0x94);
    return t;
}

BloomTheme bloom_theme_light(void) {
    BloomTheme t;
    t.bg_canvas     = BLOOM_RGBA(0xF2, 0xF5, 0xFA, 0xFF);
    t.bg_surface    = BLOOM_RGBA(0xFF, 0xFF, 0xFF, 0xF5);
    t.bg_elevated   = BLOOM_RGBA(0xFA, 0xFC, 0xFF, 0xFF);
    t.bg_input      = BLOOM_RGBA(0xEE, 0xF2, 0xF8, 0xFF);
    t.border_subtle = BLOOM_RGBA(0xC6, 0xD1, 0xE0, 0xFF);
    t.border_focus  = BLOOM_RGBA(0x16, 0x77, 0xFF, 0xFF);
    t.accent        = BLOOM_RGBA(0x16, 0x77, 0xFF, 0xFF);
    t.accent_hover  = BLOOM_RGBA(0x3B, 0x8F, 0xFF, 0xFF);
    t.danger        = BLOOM_RGBA(0xDC, 0x35, 0x45, 0xFF);
    t.success       = BLOOM_RGBA(0x19, 0x9A, 0x58, 0xFF);
    t.text_primary  = BLOOM_RGBA(0x14, 0x1D, 0x2B, 0xFF);
    t.text_muted    = BLOOM_RGBA(0x5E, 0x6E, 0x84, 0xFF);
    t.radius_sm     = 5.0f;
    t.radius_md     = 10.0f;
    t.radius_lg     = 14.0f;
    t.pad_md        = 14.0f;
    t.gap_md        = 10.0f;
    t.font_sm       = 13.0f;
    t.font_md       = 15.0f;
    t.font_lg       = 18.0f;
    t.shadow_rgba8  = BLOOM_RGBA(0x14, 0x20, 0x34, 0x38);
    return t;
}

static BloomUiNodeDesc base_desc(const BloomTheme *t, BloomHandle parent, const char *id,
                                 BloomUiKind kind, float w, float h) {
    BloomUiNodeDesc d;
    memset(&d, 0, sizeof(d));
    d.id = id;
    d.parent = parent;
    d.kind = kind;
    d.w = w;
    d.h = h;
    d.pad = t->pad_md;
    d.gap = t->gap_md;
    d.font_px = t->font_md;
    d.text_rgba8 = t->text_primary;
    d.radius_px = t->radius_md;
    d.border_px = 1.0f;
    d.border_rgba8 = t->border_subtle;
    return d;
}

BloomHandle bloom_kit_panel(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                            const char *id, const char *title,
                            float x, float y, float w, float h, BloomFlexDir dir) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_PANEL, w, h);
    d.flex_dir = dir;
    d.x = x; d.y = y;
    d.fill_rgba8 = t->bg_surface;
    d.radius_px = t->radius_lg;
    d.shadow_blur_px = 14.0f;
    d.shadow_rgba8 = t->shadow_rgba8;
    d.label = title;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_button(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                             const char *id, const char *label, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_BUTTON, w, h);
    d.fill_rgba8 = t->accent;
    d.border_rgba8 = t->accent_hover;
    d.radius_px = t->radius_sm;
    d.pad = 10.0f;
    d.label = label;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_text_input(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                                 const char *id, const char *placeholder, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_TEXT_INPUT, w, h);
    d.fill_rgba8 = t->bg_input;
    d.radius_px = t->radius_sm;
    d.pad = 10.0f;
    d.placeholder = placeholder;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_checkbox(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                               const char *id, const char *label, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_CHECKBOX, w, h);
    d.border_px = 0.0f;
    d.border_rgba8 = 0u;
    d.fill_rgba8 = 0u;
    d.label = label;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_switch(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                             const char *id, const char *label, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_SWITCH, w, h);
    d.border_px = 0.0f;
    d.border_rgba8 = 0u;
    d.fill_rgba8 = 0u;
    d.label = label;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_select(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                             const char *id, const char *options_pipe, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_SELECT, w, h);
    d.fill_rgba8 = t->bg_elevated;
    d.radius_px = t->radius_sm;
    d.options_pipe = options_pipe;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_tabs(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                           const char *id, const char *options_pipe, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_TABS, w, h);
    d.fill_rgba8 = t->bg_input;
    d.radius_px = t->radius_sm;
    d.options_pipe = options_pipe;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_scrollview(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                                 const char *id, float w, float h, BloomFlexDir dir) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_SCROLLVIEW, w, h);
    d.flex_dir = dir;
    d.fill_rgba8 = t->bg_input;
    d.pad = 8.0f;
    d.gap = 8.0f;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_progress(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                               const char *id, float progress_t, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_PROGRESS, w, h);
    d.fill_rgba8 = t->bg_input;
    d.radius_px = h * 0.5f;
    d.border_px = 0.0f;
    d.border_rgba8 = 0u;
    d.progress_t = progress_t;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_toast(BloomScene *sc, const BloomTheme *t, const char *id,
                            const char *message, float x, float y, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, BLOOM_HANDLE_NULL, id, BLOOM_UI_TOAST, w, h);
    d.x = x; d.y = y;
    d.fill_rgba8 = t->bg_elevated;
    d.border_rgba8 = t->success;
    d.shadow_blur_px = 14.0f;
    d.shadow_rgba8 = t->shadow_rgba8;
    d.label = message;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_modal(BloomScene *sc, const BloomTheme *t, const char *id,
                            const char *title, float x, float y, float w, float h) {
    BloomUiNodeDesc d = base_desc(t, BLOOM_HANDLE_NULL, id, BLOOM_UI_MODAL, w, h);
    d.flex_dir = BLOOM_FLEX_COL;
    d.x = x; d.y = y;
    d.fill_rgba8 = t->bg_elevated;
    d.border_rgba8 = t->border_focus;
    d.radius_px = t->radius_lg;
    d.shadow_blur_px = 20.0f;
    d.shadow_rgba8 = t->shadow_rgba8;
    d.modal_trap = true;
    d.label = title;
    return bloom_scene_add_ui(sc, &d);
}

BloomHandle bloom_kit_sprite(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                             const char *id, const uint16_t uv_unorm16[4], float w, float h) {
    BloomUiNodeDesc d = base_desc(t, parent, id, BLOOM_UI_SPRITE, w, h);
    d.border_px = 0.0f;
    d.border_rgba8 = 0u;
    d.fill_rgba8 = 0u;
    d.radius_px = t->radius_sm;
    if (uv_unorm16) memcpy(d.sprite_uv_unorm16, uv_unorm16, 8u);
    d.sprite_tint_rgba8 = 0xFFFFFFFFu;
    return bloom_scene_add_ui(sc, &d);
}
