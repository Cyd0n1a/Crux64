#pragma once

#include <stdbool.h>

/* Base camp: a tent and campfire placed procedurally on the apron in
 * front of the route's first hold. The player spawns beside the fire
 * and walks in to the wall. Libdragon-free (host-testable). */

typedef struct {
    bool  valid;
    float fire[3];      /* fire pit center, on the ground */
    float tent[3];      /* tent center, on the ground */
    float tent_yaw;     /* door faces the fire */
    float spawn[3];     /* player spawn, beside the fire */
    float spawn_yaw;    /* facing the wall */
} campsite_t;

/* Place camp on flat ground out from wall_pos along out_xz (unit,
 * horizontal). The approach path back to the wall stays walkable.
 * Returns false if no site fits (steep or cramped apron). */
bool campsite_place(const float wall_pos[3], const float out_xz[3]);
const campsite_t *campsite_get(void);
