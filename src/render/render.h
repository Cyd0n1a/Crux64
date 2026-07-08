#pragma once

#include <t3d/t3d.h>

typedef struct {
    float gen_ms;        /* boot-time mountain generation cost */
    float climber_alt;
    bool  rumble_ok;
    const char *status;  /* mode/limb line, NULL to hide */
    int   grip_count;
    bool  title;         /* title screen: far fog, cube logo, no HUD */
    bool  cinematic;     /* prologue: draw the scene + dialogue box, no dev HUD */

    /* Phase 4 (GDD 4): per-limb stamina meter + inventory corner. */
    const float *stam;   /* LIMB_COUNT staminas, NULL hides the bars */
    int   pitons;
    int   chalk;

    /* Phase 6 (GDD 3.4): persistent record, shown on the title screen. */
    int   best_alt;      /* saved max altitude, meters */
    int   lifetime_falls;
    const char *initials;  /* 3 chars, NULL hides the record line */
} render_hud_t;

void render_init(void);
void render_frame(const T3DVec3 *eye, const T3DVec3 *target,
                  const render_hud_t *hud);
