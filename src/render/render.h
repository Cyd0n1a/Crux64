#pragma once

#include <t3d/t3d.h>

typedef struct {
    float gen_ms;        /* boot-time mountain generation cost */
    float cam_alt;
    bool  rumble_ok;
} render_hud_t;

void render_init(void);
void render_frame(const T3DVec3 *eye, const T3DVec3 *target,
                  const render_hud_t *hud);
