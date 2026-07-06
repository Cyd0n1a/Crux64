#pragma once

#include <stdbool.h>
#include "../input/input.h"

/* Phase 3 (GDD 5.3): stick-figure climber. Two modes:
 *  - ON_FOOT: spawn at the mountain's base and walk (stick, camera-
 *    relative) over any ground gentler than the walk slope limit; hold
 *    Z to steer the camera with the stick instead. Hold a C button
 *    near a gripped wall to mount it.
 *  - CLIMBING: four two-bone limbs anchored to grips; hold a C button
 *    to steer that limb along the rock, release to snap to the nearest
 *    hold. The torso follows the anchors; analytic IK places elbows
 *    and knees. Press R with no limb held to step off onto walkable
 *    ground. Balance/stamina physics arrive in Phase 4.
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

typedef enum {
    CLIMBER_ON_FOOT,
    CLIMBER_CLIMBING,
} climber_mode_t;

typedef struct {
    float root[3];   /* shoulder / hip joint */
    float mid[3];    /* elbow / knee (IK result) */
    float tip[3];    /* hand / foot */
    int   grip;      /* grip index, -1 while player-steered / on foot */
} climber_limb_t;

typedef struct {
    climber_mode_t mode;
    float hip[3], neck[3], head[3];
    float right[3], up[3], fwd[3];   /* torso frame; fwd = facing */
    float wall_n[3];                 /* out of the rock face (-fwd) */
    float yaw;                       /* on-foot facing angle */

    climber_limb_t limbs[LIMB_COUNT];
    limb_id_t active;                /* limb currently player-steered */

    bool snapped;                    /* set for one frame on grip catch */
    bool snap_failed;                /* release found no grip in range */
    bool mounted;                    /* one frame: grabbed onto a wall */
    bool dismounted;                 /* one frame: stepped off to foot */
    float alt;                       /* hip altitude, meters */
} climber_t;

void climber_init(void);
/* cam_yaw: camera azimuth, so on-foot stick movement is camera-relative. */
void climber_update(const input_state_t *in, float cam_yaw, float dt);
const climber_t *climber_state(void);
