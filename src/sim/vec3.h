#pragma once

#include <math.h>

/* Plain float[3] helpers for the simulation layer. Kept libdragon-free
 * so src/sim/ compiles with host gcc for validation, same as src/gen/. */

static inline void v3_set(float r[3], float x, float y, float z) {
    r[0] = x; r[1] = y; r[2] = z;
}

static inline void v3_copy(float r[3], const float a[3]) {
    r[0] = a[0]; r[1] = a[1]; r[2] = a[2];
}

static inline void v3_add(float r[3], const float a[3], const float b[3]) {
    r[0] = a[0] + b[0]; r[1] = a[1] + b[1]; r[2] = a[2] + b[2];
}

static inline void v3_sub(float r[3], const float a[3], const float b[3]) {
    r[0] = a[0] - b[0]; r[1] = a[1] - b[1]; r[2] = a[2] - b[2];
}

static inline void v3_scale(float r[3], const float a[3], float s) {
    r[0] = a[0] * s; r[1] = a[1] * s; r[2] = a[2] * s;
}

/* r += a * s */
static inline void v3_mad(float r[3], const float a[3], float s) {
    r[0] += a[0] * s; r[1] += a[1] * s; r[2] += a[2] * s;
}

static inline float v3_dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void v3_cross(float r[3], const float a[3], const float b[3]) {
    float x = a[1] * b[2] - a[2] * b[1];
    float y = a[2] * b[0] - a[0] * b[2];
    float z = a[0] * b[1] - a[1] * b[0];
    r[0] = x; r[1] = y; r[2] = z;
}

static inline float v3_len(const float a[3]) {
    return sqrtf(v3_dot(a, a));
}

static inline float v3_dist(const float a[3], const float b[3]) {
    float d[3];
    v3_sub(d, a, b);
    return v3_len(d);
}

/* Returns the length before normalizing; leaves r untouched if ~zero. */
static inline float v3_norm(float r[3]) {
    float len = v3_len(r);
    if (len > 1e-6f)
        v3_scale(r, r, 1.f / len);
    return len;
}

static inline void v3_lerp(float r[3], const float a[3], const float b[3], float t) {
    r[0] = a[0] + (b[0] - a[0]) * t;
    r[1] = a[1] + (b[1] - a[1]) * t;
    r[2] = a[2] + (b[2] - a[2]) * t;
}
