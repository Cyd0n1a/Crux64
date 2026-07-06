#include "climber.h"
#include "vec3.h"
#include "../gen/mountain.h"
#include "../gen/grips.h"

#include <math.h>
#include <stddef.h>

static climber_t cl;
static int prev_grip[LIMB_COUNT];   /* fallback if a release finds no hold */

#define TIP_SPEED    1.5f    /* m/s of steered limb travel */
#define SNAP_RADIUS  0.42f
#define WALL_GAP     0.34f   /* hip clearance off the face */
#define TORSO_EASE   7.f

#define WALK_SPEED     2.1f
#define WALK_SLOPE_MIN 0.72f  /* ground ny below this is too steep to walk */
#define MOUNT_RADIUS   1.35f  /* a hold this close to the chest is grabbable */
#define DISMOUNT_H     1.15f  /* max hip height over ground to step off */
#define STAND_H        ((CL_LEG_UPPER + CL_LEG_LOWER) * 0.93f)

static float gait;         /* walk cycle phase */
static float walk_sm;      /* smoothed walk speed for the gait pose */

static const float WORLD_UP[3] = { 0.f, 1.f, 0.f };

static bool limb_is_arm(limb_id_t l) {
    return l == LIMB_ARM_R || l == LIMB_ARM_L;
}

/* Climber-frame side: +1 for the right-side limbs. */
static float limb_side(limb_id_t l) {
    return (l == LIMB_ARM_R || l == LIMB_LEG_R) ? 1.f : -1.f;
}

static float limb_upper(limb_id_t l) { return limb_is_arm(l) ? CL_ARM_UPPER : CL_LEG_UPPER; }
static float limb_lower(limb_id_t l) { return limb_is_arm(l) ? CL_ARM_LOWER : CL_LEG_LOWER; }
static float limb_reach(limb_id_t l) { return (limb_upper(l) + limb_lower(l)) * 0.99f; }

/* The torso relax keeps anchors inside this fraction of full reach —
 * the headroom is what lets the next limb actually travel. Anchors at
 * 100% reach deadlock the climber spread-eagled. */
#define REACH_SOFT 0.84f

/* Stick a point to the render mesh. Only y is touched: shifting x/z
 * (e.g. floating along the normal) would accumulate lateral drift when
 * called every frame. Heightmap => (x,z) parametrizes the whole face. */
static void surface_stick(float p[3]) {
    p[1] = mountain_surface(p[0], p[2], NULL);
}

static void attach_tip(climber_limb_t *lb, int grip) {
    lb->grip = grip;
    v3_copy(lb->tip, grip_get(grip)->pos);
}

/* Analytic two-bone IK: place mid so |root-mid|=l1, |mid-tip|=l2, bending
 * toward the pole direction (GDD 3.1's custom IK solver). */
static void solve_limb(climber_limb_t *lb, float l1, float l2, const float pole[3]) {
    float d[3];
    v3_sub(d, lb->tip, lb->root);
    float len = v3_norm(d);
    if (len < 1e-5f) {
        v3_set(d, 0.f, -1.f, 0.f);
        len = 0.01f;
    }
    float lmin = fabsf(l1 - l2) + 0.01f;
    float lmax = l1 + l2 - 0.005f;
    float lc = len < lmin ? lmin : (len > lmax ? lmax : len);

    float a = (l1 * l1 + lc * lc - l2 * l2) / (2.f * lc);
    float h2 = l1 * l1 - a * a;
    float h = h2 > 0.f ? sqrtf(h2) : 0.f;

    float perp[3];
    v3_copy(perp, pole);
    v3_mad(perp, d, -v3_dot(perp, d));
    if (v3_norm(perp) < 1e-5f) {
        /* Pole parallel to the limb: any perpendicular will do. */
        float ref[3] = { d[1], d[2], d[0] };
        v3_cross(perp, d, ref);
        v3_norm(perp);
    }

    v3_copy(lb->mid, lb->root);
    v3_mad(lb->mid, d, a);
    v3_mad(lb->mid, perp, h);
}

/* Joint roots from the current torso frame. */
static void place_roots(void) {
    v3_copy(cl.neck, cl.hip);
    v3_mad(cl.neck, cl.up, CL_TORSO_LEN);
    v3_copy(cl.head, cl.neck);
    v3_mad(cl.head, cl.up, CL_HEAD_R + 0.05f);
    v3_mad(cl.head, cl.fwd, 0.02f);

    for (int l = 0; l < LIMB_COUNT; l++) {
        climber_limb_t *lb = &cl.limbs[l];
        if (limb_is_arm((limb_id_t)l)) {
            v3_copy(lb->root, cl.neck);
            v3_mad(lb->root, cl.up, -0.06f);
            v3_mad(lb->root, cl.right, limb_side((limb_id_t)l) * CL_SHOULDER_X);
        } else {
            v3_copy(lb->root, cl.hip);
            v3_mad(lb->root, cl.right, limb_side((limb_id_t)l) * CL_HIP_X);
        }
    }
}

/* Torso follows the anchors: frame from the wall normal + hand/foot
 * spread, position eased toward a point hung between hands and feet,
 * relaxed until every anchored tip is back inside its limb's reach. */
static void solve_torso(float dt) {
    climber_limb_t *ra = &cl.limbs[LIMB_ARM_R], *la = &cl.limbs[LIMB_ARM_L];
    climber_limb_t *rl = &cl.limbs[LIMB_LEG_R], *ll = &cl.limbs[LIMB_LEG_L];

    float hands[3], feet[3];
    v3_lerp(hands, ra->tip, la->tip, 0.5f);
    v3_lerp(feet,  rl->tip, ll->tip, 0.5f);

    /* Wall normal: average of the faces the four tips rest on. */
    float wn[3] = { 0.f, 0.f, 0.f }, n[3];
    for (int l = 0; l < LIMB_COUNT; l++) {
        mountain_surface(cl.limbs[l].tip[0], cl.limbs[l].tip[2], n);
        v3_add(wn, wn, n);
    }
    if (v3_norm(wn) < 1e-5f)
        v3_copy(wn, WORLD_UP);
    v3_copy(cl.wall_n, wn);
    v3_scale(cl.fwd, wn, -1.f);

    /* Spine hugs the wall (tangent-up), leaning with the hand/foot
     * spread. Blending toward world-up instead makes the torso "stand"
     * on sub-vertical faces and pushes the rock out of arm's reach. */
    float upt[3];
    v3_copy(upt, WORLD_UP);
    v3_mad(upt, wn, -v3_dot(upt, wn));
    if (v3_norm(upt) < 1e-5f)
        v3_copy(upt, WORLD_UP);
    float spine[3];
    v3_sub(spine, hands, feet);
    if (v3_norm(spine) < 1e-5f)
        v3_copy(spine, upt);
    float up0[3];
    v3_lerp(up0, upt, spine, 0.35f);
    v3_norm(up0);

    v3_cross(cl.right, up0, cl.fwd);
    if (v3_norm(cl.right) < 1e-5f)
        v3_set(cl.right, 1.f, 0.f, 0.f);
    v3_cross(cl.up, cl.fwd, cl.right);
    v3_norm(cl.up);

    /* Target: hang the pelvis between feet and hands, off the wall. */
    float target[3];
    v3_lerp(target, feet, hands, 0.48f);
    v3_mad(target, cl.up, -CL_TORSO_LEN * 0.42f);
    v3_mad(target, cl.wall_n, WALL_GAP);

    /* Keep the pelvis out of the rock. */
    float sn[3];
    float sh = mountain_surface(target[0], target[2], sn);
    float clear = (target[1] - sh) * sn[1];
    if (clear < WALL_GAP)
        v3_mad(target, sn, WALL_GAP - clear);

    /* Pull toward any anchor drifting past its limb's soft reach, until
     * every anchored tip has movement headroom again. */
    for (int pass = 0; pass < 6; pass++) {
        float save[3];
        v3_copy(save, cl.hip);
        v3_copy(cl.hip, target);
        place_roots();
        v3_copy(cl.hip, save);

        for (int l = 0; l < LIMB_COUNT; l++) {
            climber_limb_t *lb = &cl.limbs[l];
            if (lb->grip < 0 && cl.active == (limb_id_t)l)
                continue;   /* steered limb chases the torso, not vice versa */
            float d[3];
            v3_sub(d, lb->tip, lb->root);
            float len = v3_len(d);
            float soft = limb_reach((limb_id_t)l) * REACH_SOFT;
            if (len > soft)
                v3_mad(target, d, (len - soft) / len * 0.9f);
        }
    }

    float t = TORSO_EASE * dt;
    if (t > 1.f) t = 1.f;
    v3_lerp(cl.hip, cl.hip, target, t);
    place_roots();
    cl.alt = cl.hip[1];
}

/* Steer the active limb's tip along the rock in the local tangent frame:
 * stick up follows the surface uphill, stick x tracks the climber's
 * right. Clamped to the limb's reach sphere around its root. */
static void steer_tip(climber_limb_t *lb, limb_id_t l, const input_state_t *in, float dt) {
    float n[3];
    mountain_surface(lb->tip[0], lb->tip[2], n);

    float upt[3];
    v3_copy(upt, WORLD_UP);
    v3_mad(upt, n, -v3_dot(upt, n));
    if (v3_norm(upt) < 0.2f) {
        /* Near-flat ground: "up" means away from the wall the torso faces. */
        v3_copy(upt, cl.fwd);
        v3_mad(upt, n, -v3_dot(upt, n));
        if (v3_norm(upt) < 1e-5f)
            v3_set(upt, 0.f, 0.f, -1.f);
    }
    float rt[3];
    v3_cross(rt, n, upt);
    v3_norm(rt);

    v3_mad(lb->tip, rt,  in->stick_x * TIP_SPEED * dt);
    v3_mad(lb->tip, upt, in->stick_y * TIP_SPEED * dt);
    surface_stick(lb->tip);

    /* Reach limit applied in the wall plane, around the root's footprint
     * on the rock. A plain radial clamp toward the root fights the
     * surface re-stick (root floats off the wall), and their equilibrium
     * pins the tip well short of the sphere top — the climber deadlocks. */
    float sn[3];
    float sh = mountain_surface(lb->root[0], lb->root[2], sn);
    float d_perp = (lb->root[1] - sh) * sn[1];   /* distance along sn */
    if (d_perp < 0.f) d_perp = 0.f;
    float rootw[3];
    v3_copy(rootw, lb->root);
    v3_mad(rootw, sn, -d_perp);
    float reach = limb_reach(l);
    float rw2 = reach * reach - d_perp * d_perp;
    float rw = rw2 > 0.01f ? sqrtf(rw2) : 0.1f;

    float dw[3];
    v3_sub(dw, lb->tip, rootw);
    float lenw = v3_len(dw);
    if (lenw > rw) {
        v3_copy(lb->tip, rootw);
        v3_mad(lb->tip, dw, rw / lenw);
        surface_stick(lb->tip);
    }
}

static void snap_limb(limb_id_t l) {
    climber_limb_t *lb = &cl.limbs[l];
    int g = grips_nearest(lb->tip, SNAP_RADIUS, -1);
    if (g >= 0) {
        attach_tip(lb, g);
        cl.snapped = true;
    } else {
        attach_tip(lb, prev_grip[l]);
        cl.snap_failed = true;
    }
}

static void solve_all_limbs(void) {
    for (int l = 0; l < LIMB_COUNT; l++) {
        climber_limb_t *lb = &cl.limbs[l];
        float pole[3];
        if (limb_is_arm((limb_id_t)l)) {
            /* Elbows drift out from the wall and down. */
            v3_copy(pole, cl.wall_n);
            v3_mad(pole, cl.up, -0.7f);
            v3_mad(pole, cl.right, limb_side((limb_id_t)l) * 0.15f);
        } else {
            /* Knees frog outward. */
            v3_copy(pole, cl.wall_n);
            v3_mad(pole, cl.right, limb_side((limb_id_t)l) * 0.8f);
            v3_mad(pole, cl.up, 0.1f);
        }
        solve_limb(lb, limb_upper((limb_id_t)l), limb_lower((limb_id_t)l), pole);
    }
}

/* Latch onto the wall: frame off the nearest hold's face, then seed
 * each limb on a close, unshared grip near its resting spot — far or
 * doubled-up seeds start the climber overextended. */
static bool mount_wall(const float probe[3]) {
    int start = grips_nearest(probe, MOUNT_RADIUS, -1);
    if (start < 0)
        return false;
    const grip_t *s = grip_get(start);

    v3_copy(cl.wall_n, s->n);
    v3_scale(cl.fwd, cl.wall_n, -1.f);
    float upt[3];
    v3_copy(upt, WORLD_UP);
    v3_mad(upt, s->n, -v3_dot(upt, s->n));
    if (v3_norm(upt) < 1e-5f)
        v3_copy(upt, WORLD_UP);
    v3_copy(cl.up, upt);
    v3_cross(cl.right, cl.up, cl.fwd);
    if (v3_norm(cl.right) < 1e-5f)
        v3_set(cl.right, 1.f, 0.f, 0.f);

    v3_copy(cl.hip, s->pos);
    v3_mad(cl.hip, upt, 0.7f);
    v3_mad(cl.hip, cl.wall_n, WALL_GAP);
    place_roots();

    for (int l = 0; l < LIMB_COUNT; l++) {
        climber_limb_t *lb = &cl.limbs[l];
        float want[3];
        v3_copy(want, lb->root);
        v3_mad(want, cl.up, limb_is_arm((limb_id_t)l) ? 0.42f : -0.60f);
        v3_mad(want, cl.right, limb_side((limb_id_t)l) * 0.12f);
        v3_mad(want, cl.fwd, WALL_GAP);

        int g = -1;
        int near[12];
        int n = grips_collect(want, 1.0f, near, 12);
        float best = 1e9f;
        for (int k = 0; k < n; k++) {
            bool used = false;
            for (int m = 0; m < l; m++)
                if (cl.limbs[m].grip == near[k])
                    used = true;
            if (used)
                continue;
            float d[3];
            v3_sub(d, grip_get(near[k])->pos, want);
            float d2 = v3_dot(d, d);
            if (d2 < best) {
                best = d2;
                g = near[k];
            }
        }
        if (g < 0)
            g = grips_nearest(want, 2.f, -1);
        if (g < 0)
            g = grips_nearest(s->pos, 3.f, -1);
        if (g < 0)
            g = start;
        attach_tip(lb, g);
        prev_grip[l] = g;
    }

    /* Settle the torso onto the seeded anchors. */
    for (int i = 0; i < 12; i++)
        solve_torso(0.25f);
    solve_all_limbs();

    cl.mode = CLIMBER_CLIMBING;
    cl.mounted = true;
    return true;
}

static void enter_on_foot(void) {
    cl.mode = CLIMBER_ON_FOOT;
    cl.active = LIMB_NONE;
    /* Keep facing where the wall was; walking recomputes the frame. */
    cl.yaw = atan2f(cl.fwd[0], cl.fwd[2]);
    for (int l = 0; l < LIMB_COUNT; l++)
        cl.limbs[l].grip = -1;
    gait = 0.f;
    walk_sm = 0.f;
    cl.dismounted = true;
}

/* Standing pose: torso upright over the ground, feet stepping with the
 * gait phase, arms hanging with a slight counter-swing. speed 0..1. */
static void walk_pose(float speed) {
    float gn[3];
    float gh = mountain_surface(cl.hip[0], cl.hip[2], gn);

    v3_set(cl.fwd, sinf(cl.yaw), 0.f, cosf(cl.yaw));
    v3_scale(cl.wall_n, cl.fwd, -1.f);
    float up0[3];
    v3_copy(up0, WORLD_UP);
    v3_mad(up0, gn, 0.3f);   /* lean slightly with the ground */
    v3_norm(up0);
    v3_cross(cl.right, up0, cl.fwd);
    if (v3_norm(cl.right) < 1e-5f)
        v3_set(cl.right, 1.f, 0.f, 0.f);
    v3_cross(cl.up, cl.fwd, cl.right);
    v3_norm(cl.up);

    cl.hip[1] = gh + STAND_H + 0.025f * sinf(gait * 2.f) * speed;
    place_roots();

    for (int l = 0; l < LIMB_COUNT; l++) {
        climber_limb_t *lb = &cl.limbs[l];
        float pole[3];
        if (limb_is_arm((limb_id_t)l)) {
            /* Arms hang, counter-swinging the same-side leg. */
            float ph = gait + ((limb_id_t)l == LIMB_ARM_R ? 0.f : (float)M_PI);
            v3_copy(lb->tip, lb->root);
            v3_mad(lb->tip, cl.up, -(CL_ARM_UPPER + CL_ARM_LOWER) * 0.78f);
            v3_mad(lb->tip, cl.right, limb_side((limb_id_t)l) * 0.05f);
            v3_mad(lb->tip, cl.fwd, -sinf(ph) * 0.14f * speed);
            v3_scale(pole, cl.fwd, -1.f);           /* elbows point back */
            v3_mad(pole, cl.up, -0.4f);
        } else {
            float ph = gait + ((limb_id_t)l == LIMB_LEG_R ? (float)M_PI : 0.f);
            float step = sinf(ph) * 0.30f * speed;
            float lift = fmaxf(0.f, cosf(ph)) * 0.13f * speed;
            v3_copy(lb->tip, lb->root);
            v3_mad(lb->tip, cl.fwd, step);
            surface_stick(lb->tip);
            lb->tip[1] += lift;
            v3_copy(pole, cl.fwd);                  /* knees point forward */
            v3_mad(pole, cl.up, 0.2f);
        }
        solve_limb(lb, limb_upper((limb_id_t)l), limb_lower((limb_id_t)l), pole);
    }
    cl.alt = cl.hip[1];
}

static void walk_update(const input_state_t *in, float cam_yaw, float dt) {
    float mx = 0.f, mz = 0.f;
    if (!in->z_held) {   /* Z held: the stick is steering the camera */
        float fx = -sinf(cam_yaw), fz = -cosf(cam_yaw);
        float rx =  cosf(cam_yaw), rz = -sinf(cam_yaw);
        mx = rx * in->stick_x + fx * in->stick_y;
        mz = rz * in->stick_x + fz * in->stick_y;
    }
    float m = sqrtf(mx * mx + mz * mz);
    if (m > 1.f) {
        mx /= m; mz /= m;
        m = 1.f;
    }

    if (m > 0.08f) {
        float nx = cl.hip[0] + mx * WALK_SPEED * dt;
        float nz = cl.hip[2] + mz * WALK_SPEED * dt;
        if (nx < -MTN_HALF + 1.f) nx = -MTN_HALF + 1.f;
        if (nx >  MTN_HALF - 1.f) nx =  MTN_HALF - 1.f;
        if (nz < -MTN_HALF + 1.f) nz = -MTN_HALF + 1.f;
        if (nz >  MTN_HALF - 1.f) nz =  MTN_HALF - 1.f;

        float gn[3];
        mountain_surface(nx, nz, gn);
        if (gn[1] >= WALK_SLOPE_MIN) {   /* too steep to walk = a wall */
            cl.hip[0] = nx;
            cl.hip[2] = nz;
        }

        /* Turn toward the movement direction (shortest way around). */
        float want = atan2f(mx, mz);
        float diff = fmodf(want - cl.yaw + 3.f * (float)M_PI, 2.f * (float)M_PI)
                   - (float)M_PI;
        float turn = 10.f * dt;
        if (turn > 1.f) turn = 1.f;
        cl.yaw += diff * turn;

        gait += m * 7.f * dt;
    }

    float t = 8.f * dt;
    if (t > 1.f) t = 1.f;
    walk_sm += (m - walk_sm) * t;
    walk_pose(walk_sm);

    /* Hold a C button facing a gripped wall to grab on. */
    if (in->limb != LIMB_NONE) {
        float probe[3];
        v3_copy(probe, cl.neck);
        v3_mad(probe, cl.fwd, 0.55f);
        mount_wall(probe);
    }
}

void climber_update(const input_state_t *in, float cam_yaw, float dt) {
    cl.snapped = cl.snap_failed = cl.mounted = cl.dismounted = false;

    if (cl.mode == CLIMBER_ON_FOOT) {
        cl.active = LIMB_NONE;
        walk_update(in, cam_yaw, dt);
        return;
    }

    /* A change of held C button snaps the old limb first — input.c only
     * raises limb_released when every C button is up. */
    if (cl.active != LIMB_NONE && in->limb != cl.active)
        snap_limb(cl.active);
    cl.active = in->limb;

    if (cl.active != LIMB_NONE) {
        climber_limb_t *lb = &cl.limbs[cl.active];
        if (lb->grip >= 0) {
            prev_grip[cl.active] = lb->grip;
            lb->grip = -1;
        }
        steer_tip(lb, cl.active, in, dt);
    }

    solve_torso(dt);
    solve_all_limbs();

    /* R with no limb held: step off, if there's walkable ground under
     * the hips within standing distance (base of the wall, ledges,
     * bench tops between cliff bands). */
    if (cl.active == LIMB_NONE && in->rest) {
        float gn[3];
        float gh = mountain_surface(cl.hip[0], cl.hip[2], gn);
        if (gn[1] >= WALK_SLOPE_MIN && cl.hip[1] - gh < DISMOUNT_H)
            enter_on_foot();
    }
}

const climber_t *climber_state(void) {
    return &cl;
}

/* Boot placement: a low grip with company to stand on and — the part
 * that matters — a tall column of holds overhead. The lowest crag is
 * often a 3m boulder that tops out onto a walkable bench, stranding
 * the climber; score candidates by route length instead. */
static bool start_candidate(int i, float y_cap) {
    const grip_t *g = grip_get(i);
    if (g->pos[1] >= y_cap)
        return false;
    int near[24];
    int n = grips_collect(g->pos, 1.8f, near, 24);
    int above = 0;
    for (int k = 0; k < n; k++)
        if (grip_get(near[k])->pos[1] > g->pos[1] + 0.5f)
            above++;
    return n >= 5 && above >= 2;
}

static int find_start_grip(void) {
    float y_cap = 60.f;
    for (int tries = 0; tries < 3; tries++, y_cap *= 2.f) {
        /* Pass 1: the best route length any low candidate offers. */
        int best_score = -1;
        for (int i = 0; i < grips_count(); i += 3) {
            if (!start_candidate(i, y_cap))
                continue;
            int score = grips_column(grip_get(i)->pos, 5.f, 45.f);
            if (score > best_score)
                best_score = score;
        }
        if (best_score < 0)
            continue;
        /* Pass 2: the LOWEST candidate still offering most of that —
         * "the base of the mountain", not the best bench mid-face. */
        int thresh = best_score * 3 / 5;
        int best = -1;
        float best_y = 1e9f;
        for (int i = 0; i < grips_count(); i += 3) {
            const grip_t *g = grip_get(i);
            if (g->pos[1] >= best_y || !start_candidate(i, y_cap))
                continue;
            if (grips_column(g->pos, 5.f, 45.f) >= thresh) {
                best = i;
                best_y = g->pos[1];
            }
        }
        if (best >= 0)
            return best;
    }
    return 0;
}

void climber_init(void) {
    cl = (climber_t){ .active = LIMB_NONE };
    gait = 0.f;
    walk_sm = 0.f;
    for (int l = 0; l < LIMB_COUNT; l++) {
        cl.limbs[l].grip = -1;
        prev_grip[l] = 0;
    }

    const grip_t *s = grip_get(find_start_grip());

    /* Spawn on foot: step outward from the route's base hold along its
     * face normal until the ground flattens to walkable, and face the
     * wall. The player walks up and grabs on with a C button. */
    float out[3] = { s->n[0], 0.f, s->n[2] };
    if (v3_norm(out) < 1e-5f)
        v3_set(out, 0.f, 0.f, 1.f);

    bool placed = false;
    for (float t = 0.8f; t <= 9.f; t += 0.5f) {
        float p[3];
        v3_copy(p, s->pos);
        v3_mad(p, out, t);
        float gn[3];
        mountain_surface(p[0], p[2], gn);
        if (gn[1] >= 0.80f) {
            /* A touch further out so the approach reads on camera. */
            float p2[3];
            v3_copy(p2, p);
            v3_mad(p2, out, 1.2f);
            float gn2[3];
            mountain_surface(p2[0], p2[2], gn2);
            if (gn2[1] >= 0.80f)
                v3_copy(p, p2);

            cl.mode = CLIMBER_ON_FOOT;
            cl.hip[0] = p[0];
            cl.hip[2] = p[2];
            cl.yaw = atan2f(-out[0], -out[2]);   /* face the wall */
            walk_pose(0.f);
            placed = true;
            break;
        }
    }

    if (!placed) {
        /* No walkable apron by this face — start latched on instead. */
        mount_wall(s->pos);
        cl.mounted = false;
    }
    cl.dismounted = false;
}
