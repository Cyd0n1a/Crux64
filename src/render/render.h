#pragma once

#include <t3d/t3d.h>

typedef struct {
    float gen_ms;        /* boot-time mountain generation cost */
    float climber_alt;
    bool  rumble_ok;
    const char *status;  /* mode/limb line, NULL to hide */
    int   grip_count;
    bool  title;         /* title screen: far fog, cube logo, no HUD */

    /* Phase 4 (GDD 4): per-limb stamina meter + inventory corner. */
    const float *stam;   /* LIMB_COUNT staminas, NULL hides the bars */
    int   pitons;
    int   chalk;
} render_hud_t;

void render_init(void);
void render_frame(const T3DVec3 *eye, const T3DVec3 *target,
                  const render_hud_t *hud);
