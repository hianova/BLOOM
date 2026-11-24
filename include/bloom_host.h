#ifndef BLOOM_HOST_H
#define BLOOM_HOST_H

#include "bloom_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_init)(void *userdata, void *ca_metal_layer, uint32_t vp_w, uint32_t vp_h);
    void (*on_frame)(void *userdata, double dt_sec, uint32_t vp_w, uint32_t vp_h);
    void (*on_input)(void *userdata, const BloomInputEvent *ev);
    void (*on_shutdown)(void *userdata);
    void *userdata;
} BloomHostCallbacks;

/** Run native Cocoa NSWindow + CAMetalLayer + DisplayLink loop until closed. */
int bloom_host_run_cocoa(const char *title, uint32_t width_pt, uint32_t height_pt,
                         const BloomHostCallbacks *cbs);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_HOST_H */
