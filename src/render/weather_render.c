#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "weather_render.h"
#include "../sim/weather.h"
#include "../gen/noise.h"

#define MAX_PARTICLES 128

typedef struct {
    float x, y, z;
    float speed;
    float phase;
} particle_t;

static particle_t particles[MAX_PARTICLES];
static T3DVertPacked *part_verts;
static T3DMat4FP *part_mat;

void weather_render_init(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particles[i].x = (noise_simplex2(i * 1.1f, 0.0f)) * 40.0f;
        particles[i].y = (noise_simplex2(0.0f, i * 1.1f)) * 40.0f;
        particles[i].z = (noise_simplex2(i * 1.3f, i * 1.3f)) * 40.0f;
        particles[i].speed = 10.0f + noise_simplex2(i, i) * 2.0f;
        particles[i].phase = noise_simplex2(i * 2.0f, 0.0f) * 3.14f;
    }
    part_verts = malloc_uncached(sizeof(T3DVertPacked) * (MAX_PARTICLES * 3 / 2 + 1));
    part_mat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(part_mat);
}

void weather_render_draw(const T3DVec3 *eye, const T3DVec3 *target) {
    const weather_state_t *w = weather_current();
    if (w->rain_intensity <= 0.0f && w->snow_intensity <= 0.0f) {
        return;
    }

    bool is_snow = w->snow_intensity > 0.0f;
    float intensity = is_snow ? w->snow_intensity : w->rain_intensity;
    int num_parts = (int)(intensity * MAX_PARTICLES);
    if (num_parts > MAX_PARTICLES) num_parts = MAX_PARTICLES;
    
    float dt = 1.0f / 30.0f; // Approx
    float time = (float)((double)get_ticks_us() * 1e-6);

    for (int i = 0; i < num_parts; i++) {
        particle_t *p = &particles[i];
        if (is_snow) {
            p->y -= (p->speed * 0.3f) * dt;
            p->x += sinf(p->phase + time * 2.0f) * 2.0f * dt;
        } else {
            p->y -= (p->speed * 3.0f) * dt;
        }

        // Wrap around camera (local volume of 40x40x40)
        float dx = p->x - eye->v[0];
        float dy = p->y - eye->v[1];
        float dz = p->z - eye->v[2];

        if (dx > 20.0f) p->x -= 40.0f;
        if (dx < -20.0f) p->x += 40.0f;
        if (dy > 20.0f) p->y -= 40.0f;
        if (dy < -20.0f) p->y += 40.0f;
        if (dz > 20.0f) p->z -= 40.0f;
        if (dz < -20.0f) p->z += 40.0f;
    }

    // Pack vertices (drawing small triangles/lines to save bandwidth)
    for (int i = 0; i < num_parts; i++) {
        particle_t *p = &particles[i];
        uint32_t color = is_snow ? 0xFFFFFFB4 : 0xB4C8FF78;
        
        int v_idx = i * 3;
        T3DVertPacked *vp1 = &part_verts[v_idx / 2];
        T3DVertPacked *vp2 = &part_verts[(v_idx + 1) / 2];
        T3DVertPacked *vp3 = &part_verts[(v_idx + 2) / 2];

        float stretch = is_snow ? 0.2f : 1.0f;
        
        // Very basic packing (A/B slots)
        if ((v_idx) & 1) {
            vp1->posB[0] = (int16_t)p->x; vp1->posB[1] = (int16_t)(p->y + stretch); vp1->posB[2] = (int16_t)p->z;
            vp1->rgbaB = color;
        } else {
            vp1->posA[0] = (int16_t)p->x; vp1->posA[1] = (int16_t)(p->y + stretch); vp1->posA[2] = (int16_t)p->z;
            vp1->rgbaA = color;
        }

        if ((v_idx + 1) & 1) {
            vp2->posB[0] = (int16_t)(p->x - 0.1f); vp2->posB[1] = (int16_t)p->y; vp2->posB[2] = (int16_t)p->z;
            vp2->rgbaB = color;
        } else {
            vp2->posA[0] = (int16_t)(p->x - 0.1f); vp2->posA[1] = (int16_t)p->y; vp2->posA[2] = (int16_t)p->z;
            vp2->rgbaA = color;
        }

        if ((v_idx + 2) & 1) {
            vp3->posB[0] = (int16_t)(p->x + 0.1f); vp3->posB[1] = (int16_t)p->y; vp3->posB[2] = (int16_t)p->z;
            vp3->rgbaB = color;
        } else {
            vp3->posA[0] = (int16_t)(p->x + 0.1f); vp3->posA[1] = (int16_t)p->y; vp3->posA[2] = (int16_t)p->z;
            vp3->rgbaA = color;
        }
    }
    
    data_cache_hit_writeback(part_verts, sizeof(T3DVertPacked) * (MAX_PARTICLES * 3 / 2 + 1));

    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
    rdpq_mode_zbuf(true, false); // depth test, no write

    t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH);
    t3d_matrix_push(part_mat);

    // Draw in batches of T3D_VERTEX_CACHE_SIZE (70 / 3 = 23 tris)
    for (int i = 0; i < num_parts; i += 23) {
        int count = num_parts - i;
        if (count > 23) count = 23;
        t3d_vert_load(part_verts, i * 3, count * 3);
        for (int j = 0; j < count; j++) {
            int v = i * 3 + j * 3;
            t3d_tri_draw(v, v + 1, v + 2);
        }
        t3d_tri_sync();
    }

    t3d_matrix_pop(1);
    rdpq_mode_zbuf(true, true);
}
