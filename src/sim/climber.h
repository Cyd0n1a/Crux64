#pragma once

#include <stdbool.h>
#include "../input/input.h"

/* Phase 3 (GDD 5.3): stick-figure climber. Four two-bone limbs anchored
 * to grip points; hold a C button to detach that limb and steer its tip
 * along the rock with the stick, release to snap to the nearest grip.
 * The torso follows the anchors; an analytic IK solver places elbows
 * and knees. Balance/stamina physics arrive in Phase 4.
 * Libdragon-free (host-testable): plain floats, world units = meters. */

/* Segment lengths — a lanky ~1.8m alpinist so grips spaced ~0.6-0.9m
 * stay within reach of successive moves. */
#define CL_TORSO_LEN  0.55f
#define CL_ARM_UPPER  0.46f
#define CL_ARM_LOWER  0.44f
#define CL_LEG_UPPER  0.50f
#define CL_LEG_LOWER  0.48f
#define CL_SHOULDER_X 0.24f
#define CL_HIP_X      0.14f
#define CL_HEAD_R     0.11f

typedef struct {
    float root[3];   /* shoulder / hip joint */
    float mid[3];    /* elbow / knee (IK result) */
    float tip[3];    /* hand / foot */
    int   grip;      /* grip index, -1 while player-steered */
} climber_limb_t;

typedef struct {
    float hip[3], neck[3], head[3];
    float right[3], up[3], fwd[3];   /* torso frame; fwd points into rock */
    float wall_n[3];                 /* out of the rock face */

    climber_limb_t limbs[LIMB_COUNT];
    limb_id_t active;                /* limb currently player-steered */

    bool snapped;                    /* set for one frame on grip catch */
    bool snap_failed;                /* release found no grip in range */
    float alt;                       /* hip altitude, meters */
} climber_t;

void climber_init(void);
void climber_update(const input_state_t *in, float dt);
const climber_t *climber_state(void);
