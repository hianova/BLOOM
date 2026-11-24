#ifndef BLOOM_GESTURE_H
#define BLOOM_GESTURE_H

#include "bloom_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLOOM_GESTURE_NONE         = 0,
    BLOOM_GESTURE_CLICK        = 1,
    BLOOM_GESTURE_DOUBLE_CLICK = 2,
    BLOOM_GESTURE_LONG_PRESS   = 3,
    BLOOM_GESTURE_DRAG_START   = 4,
    BLOOM_GESTURE_DRAG_MOVE    = 5,
    BLOOM_GESTURE_DRAG_END     = 6,
    BLOOM_GESTURE_PINCH        = 7,
    BLOOM_GESTURE_WHEEL        = 8,
    BLOOM_GESTURE_ACTION       = 9
} BloomGestureKind;

typedef struct {
    BloomGestureKind kind;
    float            x;
    float            y;
    float            dx;
    float            dy;
    float            scale_delta;
    uint32_t         action_id;
    uint32_t         modifiers;
} BloomGestureEvent;

typedef struct {
    uint32_t key_code;
    uint32_t modifiers;
    uint32_t action_id;
} BloomKeyBinding;

typedef struct {
    bool            down;
    bool            dragging;
    bool            long_fired;
    float           down_x, down_y;
    float           last_x, last_y;
    double          down_time;
    double          last_click_time;
    float           last_click_x, last_click_y;
    BloomKeyBinding bindings[64];
    uint32_t        binding_cnt;
} BloomGestureState;

void bloom_gesture_init(BloomGestureState *gs);

bool bloom_gesture_bind_key(BloomGestureState *gs, uint32_t key_code,
                            uint32_t modifiers, uint32_t action_id);

bool bloom_gesture_feed(BloomGestureState *gs, const BloomInputEvent *ev,
                        double now_sec, bool text_input_focused,
                        BloomGestureEvent *out_gev);

bool bloom_gesture_poll_long_press(BloomGestureState *gs, double now_sec,
                                   BloomGestureEvent *out_gev);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_GESTURE_H */
