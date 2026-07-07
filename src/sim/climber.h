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
 *    ground.
 *  - FALLING: fewer than two limbs kept their grip. The climber drops
 *    and slides down the face until the rope catches on the last piton
 *    (remounting there) or they fetch up on walkable ground.
 * Phase 4 (GDD 2.1/2.2): per-limb grip stamina drains while anchored —
 * faster for arms, when overextended, off balance, or short of the
 * 3-point rule — and empties into a peel. Hold R with a limb selected
 * to shake it out; chalk slows drain, a piton restores everything and
 * checkpoints the fall rope.
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
    CLIMBER_FALLING,
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

    /* Phase 4: posture, balance, stamina (GDD 2.1/2.2). */
    float stam[LIMB_COUNT];          /* per-limb grip stamina, 0..1 */
    float core;                      /* whole-body reserve, 0..1 */
    float shake[LIMB_COUNT];         /* visual shake amplitude, 0..1 */
    float imbalance;                 /* CoG outside the support, 0..1 */
    float strain;                    /* smoothed effort; drives camera */
    int   contacts;                  /* limbs currently anchored */
    bool  shakeout;                  /* R held: resting the free limb */

    int   pitons;                    /* inventory (GDD 2.3 / 4) */
    int   chalk_uses;
    int   chalk_holds;               /* holds left on current chalking */
    float piton_pos[3], piton_n[3];  /* last piton = fall checkpoint */
    bool  piton_valid;

    bool  peeled;                    /* one frame: a limb lost its grip */
    limb_id_t peeled_limb;
    bool  fell;                      /* one frame: fall started */
    bool  caught;                    /* one frame: rope caught a piton */
    bool  landed;                    /* one frame: fall ended on ground */
    bool  piton_set;                 /* one frame: piton driven in */
    bool  chalked;                   /* one frame: chalk applied */
} climber_t;

void climber_init(void);
/* cam_yaw: camera azimuth, so on-foot stick movement is camera-relative. */
void climber_update(const input_state_t *in, float cam_yaw, float dt);
const climber_t *climber_state(void);
