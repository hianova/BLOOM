#ifndef BLOOM_CAMERA_H
#define BLOOM_CAMERA_H

#include "bloom_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLOOM_CAM_ORBIT  = 0,
    BLOOM_CAM_FLY    = 1,
    BLOOM_CAM_FOLLOW = 2
} BloomCamMode;

typedef struct {
    BloomCamMode mode;
    float        target[3];
    float        pos[3];
    float        follow_offset[3];
    float        yaw;
    float        pitch;
    float        distance;
    float        target_yaw;
    float        target_pitch;
    float        target_distance;
    float        damping;
    float        move_speed;
    float        trauma;
    float        trauma_decay;
    float        shake_time;
    float        focal;
    float        near_z;
    float        far_z;
} BloomCamController;

void bloom_cam_init_orbit(BloomCamController *c, const float target[3],
                          float yaw, float pitch, float dist);
void bloom_cam_init_fly(BloomCamController *c, const float pos[3],
                        float yaw, float pitch);
void bloom_cam_init_follow(BloomCamController *c, const float offset[3], float damping);

void bloom_cam_orbit_drag(BloomCamController *c, float dx, float dy);
void bloom_cam_orbit_pan(BloomCamController *c, float dx, float dy);
void bloom_cam_orbit_zoom(BloomCamController *c, float scroll_dy);
void bloom_cam_fly_move(BloomCamController *c, float fwd, float right, float up, float dt_sec);
void bloom_cam_follow_set_target(BloomCamController *c, const float target[3]);
void bloom_cam_add_trauma(BloomCamController *c, float amount);

BloomCamera bloom_cam_step(BloomCamController *c, float dt_sec);

void bloom_cam_screen_ray(const BloomCamera *cam, float vp_w, float vp_h,
                          float px, float py, float out_ro[3], float out_rd[3]);

bool bloom_cam_ray_sphere(const float ro[3], const float rd[3],
                          const float center[3], float radius, float *out_t);

bool bloom_cam_ray_aabb(const float ro[3], const float rd[3],
                        const float bmin[3], const float bmax[3], float *out_t);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_CAMERA_H */
