#include "bloom_camera.h"
#include <math.h>
#include <string.h>

static float clampf_cam(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void cam_basis_from_yp(float yaw, float pitch, float r[3], float u[3], float f[3]) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    f[0] = sy * cp; f[1] = -sp; f[2] = cy * cp;
    r[0] = cy; r[1] = 0.0f; r[2] = -sy;
    u[0] = f[1] * r[2] - f[2] * r[1];
    u[1] = f[2] * r[0] - f[0] * r[2];
    u[2] = f[0] * r[1] - f[1] * r[0];
}

static void cam_init_common(BloomCamController *c) {
    memset(c, 0, sizeof(*c));
    c->damping = 12.0f;
    c->move_speed = 6.0f;
    c->trauma_decay = 1.8f;
    c->focal = 1.8f;
    c->near_z = 0.5f;
    c->far_z = 80.0f;
}

void bloom_cam_init_orbit(BloomCamController *c, const float target[3],
                          float yaw, float pitch, float dist) {
    if (!c) return;
    cam_init_common(c);
    c->mode = BLOOM_CAM_ORBIT;
    if (target) memcpy(c->target, target, 12u);
    c->yaw = c->target_yaw = yaw;
    c->pitch = c->target_pitch = clampf_cam(pitch, -1.45f, 1.45f);
    c->distance = c->target_distance = clampf_cam(dist, 0.5f, 100.0f);
    float r[3], u[3], f[3];
    cam_basis_from_yp(c->yaw, c->pitch, r, u, f);
    for (int k = 0; k < 3; ++k) c->pos[k] = c->target[k] - f[k] * c->distance;
}

void bloom_cam_init_fly(BloomCamController *c, const float pos[3],
                        float yaw, float pitch) {
    if (!c) return;
    cam_init_common(c);
    c->mode = BLOOM_CAM_FLY;
    if (pos) memcpy(c->pos, pos, 12u);
    c->yaw = c->target_yaw = yaw;
    c->pitch = c->target_pitch = clampf_cam(pitch, -1.45f, 1.45f);
}

void bloom_cam_init_follow(BloomCamController *c, const float offset[3], float damping) {
    if (!c) return;
    cam_init_common(c);
    c->mode = BLOOM_CAM_FOLLOW;
    if (offset) memcpy(c->follow_offset, offset, 12u);
    else { c->follow_offset[1] = 1.2f; c->follow_offset[2] = -5.0f; }
    c->damping = damping > 0.1f ? damping : 8.0f;
    for (int k = 0; k < 3; ++k) c->pos[k] = c->target[k] + c->follow_offset[k];
}

void bloom_cam_orbit_drag(BloomCamController *c, float dx, float dy) {
    if (!c) return;
    c->target_yaw += dx * 0.005f;
    c->target_pitch = clampf_cam(c->target_pitch + dy * 0.005f, -1.45f, 1.45f);
}

void bloom_cam_orbit_pan(BloomCamController *c, float dx, float dy) {
    if (!c) return;
    float r[3], u[3], f[3];
    cam_basis_from_yp(c->yaw, c->pitch, r, u, f);
    float scale = 0.002f * clampf_cam(c->distance, 1.0f, 50.0f);
    for (int k = 0; k < 3; ++k) {
        c->target[k] += (-r[k] * dx + u[k] * dy) * scale;
    }
}

void bloom_cam_orbit_zoom(BloomCamController *c, float scroll_dy) {
    if (!c) return;
    c->target_distance = clampf_cam(c->target_distance - scroll_dy * 0.45f, 0.6f, 60.0f);
}

void bloom_cam_fly_move(BloomCamController *c, float fwd, float right, float up, float dt_sec) {
    if (!c) return;
    float r[3], u[3], f[3];
    cam_basis_from_yp(c->yaw, c->pitch, r, u, f);
    float step = c->move_speed * (dt_sec > 0.0f ? dt_sec : 0.016f);
    for (int k = 0; k < 3; ++k) {
        c->pos[k] += (f[k] * fwd + r[k] * right + u[k] * up) * step;
    }
}

void bloom_cam_follow_set_target(BloomCamController *c, const float target[3]) {
    if (!c || !target) return;
    memcpy(c->target, target, 12u);
}

void bloom_cam_add_trauma(BloomCamController *c, float amount) {
    if (!c) return;
    c->trauma = clampf_cam(c->trauma + amount, 0.0f, 1.0f);
}

BloomCamera bloom_cam_step(BloomCamController *c, float dt_sec) {
    BloomCamera out = {{0.0f, 0.0f, -4.0f}, 0.0f, 0.0f, 1.8f, 0.5f, 80.0f};
    if (!c) return out;
    float dt = clampf_cam(dt_sec, 0.0f, 0.1f);
    float alpha = 1.0f - expf(-c->damping * dt);

    c->yaw += (c->target_yaw - c->yaw) * alpha;
    c->pitch += (c->target_pitch - c->pitch) * alpha;
    c->distance += (c->target_distance - c->distance) * alpha;

    if (c->mode == BLOOM_CAM_ORBIT) {
        float r[3], u[3], f[3];
        cam_basis_from_yp(c->yaw, c->pitch, r, u, f);
        for (int k = 0; k < 3; ++k) c->pos[k] = c->target[k] - f[k] * c->distance;
    } else if (c->mode == BLOOM_CAM_FOLLOW) {
        for (int k = 0; k < 3; ++k) {
            float desired = c->target[k] + c->follow_offset[k];
            c->pos[k] += (desired - c->pos[k]) * alpha;
        }
        float dx = c->target[0] - c->pos[0];
        float dy = c->target[1] - c->pos[1];
        float dz = c->target[2] - c->pos[2];
        float hdist = sqrtf(dx * dx + dz * dz);
        if (hdist > 1e-4f || fabsf(dy) > 1e-4f) {
            c->yaw = atan2f(dx, dz);
            c->pitch = atan2f(-dy, hdist);
        }
    }

    float shake = c->trauma * c->trauma;
    c->shake_time += dt * 42.0f;
    float sy = shake * 0.08f * sinf(c->shake_time * 1.13f);
    float sp = shake * 0.06f * cosf(c->shake_time * 1.47f);
    if (c->trauma > 0.0f) {
        c->trauma = clampf_cam(c->trauma - c->trauma_decay * dt, 0.0f, 1.0f);
    }

    memcpy(out.pos, c->pos, 12u);
    out.yaw = c->yaw + sy;
    out.pitch = clampf_cam(c->pitch + sp, -1.49f, 1.49f);
    out.focal = c->focal;
    out.near_z = c->near_z;
    out.far_z = c->far_z;
    return out;
}

void bloom_cam_screen_ray(const BloomCamera *cam, float vp_w, float vp_h,
                          float px, float py, float out_ro[3], float out_rd[3]) {
    if (!cam || vp_w <= 0.0f || vp_h <= 0.0f) return;
    float r[3], u[3], f[3];
    cam_basis_from_yp(cam->yaw, cam->pitch, r, u, f);
    float uvx = ((px + 0.5f) / vp_w) * 2.0f - 1.0f;
    float uvy = 1.0f - ((py + 0.5f) / vp_h) * 2.0f;
    float aspect = vp_w / vp_h;
    float focal = cam->focal > 0.05f ? cam->focal : 1.0f;
    float sx = uvx * aspect / focal;
    float sy = uvy / focal;
    float dx = r[0] * sx + u[0] * sy + f[0];
    float dy = r[1] * sx + u[1] * sy + f[1];
    float dz = r[2] * sx + u[2] * sy + f[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) { dx /= len; dy /= len; dz /= len; }
    if (out_ro) memcpy(out_ro, cam->pos, 12u);
    if (out_rd) { out_rd[0] = dx; out_rd[1] = dy; out_rd[2] = dz; }
}

bool bloom_cam_ray_sphere(const float ro[3], const float rd[3],
                          const float center[3], float radius, float *out_t) {
    float oc[3] = {ro[0] - center[0], ro[1] - center[1], ro[2] - center[2]};
    float b = oc[0] * rd[0] + oc[1] * rd[1] + oc[2] * rd[2];
    float c = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - radius * radius;
    float h = b * b - c;
    if (h < 0.0f) return false;
    float s = sqrtf(h);
    float t = -b - s;
    if (t < 0.0f) t = -b + s;
    if (t < 0.0f) return false;
    if (out_t) *out_t = t;
    return true;
}

bool bloom_cam_ray_aabb(const float ro[3], const float rd[3],
                        const float bmin[3], const float bmax[3], float *out_t) {
    float tmin = 0.0f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(rd[i]) < 1e-8f) {
            if (ro[i] < bmin[i] || ro[i] > bmax[i]) return false;
        } else {
            float inv = 1.0f / rd[i];
            float t0 = (bmin[i] - ro[i]) * inv;
            float t1 = (bmax[i] - ro[i]) * inv;
            if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
            if (t0 > tmin) tmin = t0;
            if (t1 < tmax) tmax = t1;
            if (tmin > tmax) return false;
        }
    }
    if (out_t) *out_t = tmin;
    return true;
}
