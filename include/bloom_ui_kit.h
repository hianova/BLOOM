#ifndef BLOOM_UI_KIT_H
#define BLOOM_UI_KIT_H

#include "bloom_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Theme design tokens mapped strictly onto Layer 1/2 quads, shadows & glyphs (zero extra shaders). */
typedef struct {
    uint32_t bg_canvas;
    uint32_t bg_surface;
    uint32_t bg_elevated;
    uint32_t bg_input;
    uint32_t border_subtle;
    uint32_t border_focus;
    uint32_t accent;
    uint32_t accent_hover;
    uint32_t danger;
    uint32_t success;
    uint32_t text_primary;
    uint32_t text_muted;
    float    radius_sm;
    float    radius_md;
    float    radius_lg;
    float    pad_md;
    float    gap_md;
    float    font_sm;
    float    font_md;
    float    font_lg;
    uint32_t shadow_rgba8;
} BloomTheme;

BloomTheme bloom_theme_dark(void);
BloomTheme bloom_theme_light(void);

BloomHandle bloom_kit_panel(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                            const char *id, const char *title,
                            float x, float y, float w, float h, BloomFlexDir dir);

BloomHandle bloom_kit_button(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                             const char *id, const char *label, float w, float h);

BloomHandle bloom_kit_text_input(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                                 const char *id, const char *placeholder, float w, float h);

BloomHandle bloom_kit_checkbox(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                               const char *id, const char *label, float w, float h);

BloomHandle bloom_kit_switch(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                             const char *id, const char *label, float w, float h);

BloomHandle bloom_kit_select(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                             const char *id, const char *options_pipe, float w, float h);

BloomHandle bloom_kit_tabs(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                           const char *id, const char *options_pipe, float w, float h);

BloomHandle bloom_kit_scrollview(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                                 const char *id, float w, float h, BloomFlexDir dir);

BloomHandle bloom_kit_progress(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                               const char *id, float progress_t, float w, float h);

BloomHandle bloom_kit_toast(BloomScene *sc, const BloomTheme *t, const char *id,
                            const char *message, float x, float y, float w, float h);

BloomHandle bloom_kit_modal(BloomScene *sc, const BloomTheme *t, const char *id,
                            const char *title, float x, float y, float w, float h);

BloomHandle bloom_kit_sprite(BloomScene *sc, const BloomTheme *t, BloomHandle parent,
                             const char *id, const uint16_t uv_unorm16[4], float w, float h);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_UI_KIT_H */
