#include "bloom_gesture.h"
#include <math.h>
#include <string.h>

void bloom_gesture_init(BloomGestureState *gs) {
    if (!gs) return;
    memset(gs, 0, sizeof(*gs));
    gs->last_click_time = -10.0;
}

bool bloom_gesture_bind_key(BloomGestureState *gs, uint32_t key_code,
                            uint32_t modifiers, uint32_t action_id) {
    if (!gs || gs->binding_cnt >= 64u) return false;
    gs->bindings[gs->binding_cnt++] = (BloomKeyBinding){key_code, modifiers, action_id};
    return true;
}

static float dist_2d(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    return sqrtf(dx * dx + dy * dy);
}

bool bloom_gesture_feed(BloomGestureState *gs, const BloomInputEvent *ev,
                        double now_sec, bool text_input_focused,
                        BloomGestureEvent *out_gev) {
    if (!gs || !ev || !out_gev) return false;
    memset(out_gev, 0, sizeof(*out_gev));
    out_gev->modifiers = ev->modifiers;

    if (ev->type == BLOOM_EVENT_POINTER_DOWN) {
        gs->down = true;
        gs->dragging = false;
        gs->long_fired = false;
        gs->down_x = ev->x;
        gs->down_y = ev->y;
        gs->last_x = ev->x;
        gs->last_y = ev->y;
        gs->down_time = now_sec;
        return false;
    }

    if (ev->type == BLOOM_EVENT_POINTER_MOVE) {
        if (!gs->down) {
            gs->last_x = ev->x;
            gs->last_y = ev->y;
            return false;
        }
        float dx = ev->x - gs->last_x;
        float dy = ev->y - gs->last_y;
        gs->last_x = ev->x;
        gs->last_y = ev->y;
        if (!gs->dragging) {
            if (dist_2d(gs->down_x, gs->down_y, ev->x, ev->y) < 6.0f) return false;
            gs->dragging = true;
            out_gev->kind = BLOOM_GESTURE_DRAG_START;
            out_gev->x = gs->down_x;
            out_gev->y = gs->down_y;
            out_gev->dx = ev->x - gs->down_x;
            out_gev->dy = ev->y - gs->down_y;
            return true;
        }
        out_gev->kind = BLOOM_GESTURE_DRAG_MOVE;
        out_gev->x = ev->x;
        out_gev->y = ev->y;
        out_gev->dx = dx;
        out_gev->dy = dy;
        return true;
    }

    if (ev->type == BLOOM_EVENT_POINTER_UP) {
        if (!gs->down) return false;
        bool was_drag = gs->dragging;
        bool was_long = gs->long_fired;
        gs->down = false;
        gs->dragging = false;
        out_gev->x = ev->x;
        out_gev->y = ev->y;
        if (was_drag) {
            out_gev->kind = BLOOM_GESTURE_DRAG_END;
            return true;
        }
        if (was_long) return false;
        if ((now_sec - gs->last_click_time) <= 0.35 &&
            dist_2d(gs->last_click_x, gs->last_click_y, ev->x, ev->y) <= 12.0f) {
            out_gev->kind = BLOOM_GESTURE_DOUBLE_CLICK;
            gs->last_click_time = -10.0;
        } else {
            out_gev->kind = BLOOM_GESTURE_CLICK;
            gs->last_click_time = now_sec;
            gs->last_click_x = ev->x;
            gs->last_click_y = ev->y;
        }
        return true;
    }

    if (ev->type == BLOOM_EVENT_PINCH) {
        out_gev->kind = BLOOM_GESTURE_PINCH;
        out_gev->x = ev->x;
        out_gev->y = ev->y;
        out_gev->scale_delta = ev->scale_delta;
        return true;
    }

    if (ev->type == BLOOM_EVENT_SCROLL) {
        out_gev->kind = BLOOM_GESTURE_WHEEL;
        out_gev->x = ev->x;
        out_gev->y = ev->y;
        out_gev->dx = ev->dx;
        out_gev->dy = ev->dy;
        return true;
    }

    if (ev->type == BLOOM_EVENT_KEY_DOWN) {
        bool has_mod = (ev->modifiers & (BLOOM_MOD_CTRL | BLOOM_MOD_SUPER | BLOOM_MOD_ALT)) != 0u;
        bool is_nav = (ev->key_code == 53u || ev->key_code == 48u);
        if (text_input_focused && !has_mod && !is_nav) return false;
        for (uint32_t i = 0u; i < gs->binding_cnt; ++i) {
            const BloomKeyBinding *b = &gs->bindings[i];
            if (b->key_code == ev->key_code && b->modifiers == ev->modifiers) {
                out_gev->kind = BLOOM_GESTURE_ACTION;
                out_gev->action_id = b->action_id;
                return true;
            }
        }
    }
    return false;
}

bool bloom_gesture_poll_long_press(BloomGestureState *gs, double now_sec,
                                   BloomGestureEvent *out_gev) {
    if (!gs || !out_gev || !gs->down || gs->dragging || gs->long_fired) return false;
    if ((now_sec - gs->down_time) < 0.45) return false;
    gs->long_fired = true;
    memset(out_gev, 0, sizeof(*out_gev));
    out_gev->kind = BLOOM_GESTURE_LONG_PRESS;
    out_gev->x = gs->down_x;
    out_gev->y = gs->down_y;
    return true;
}
