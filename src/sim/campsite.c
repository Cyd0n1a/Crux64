#include "campsite.h"
#include "vec3.h"
#include "../gen/mountain.h"

#include <math.h>
#include <stddef.h>

static campsite_t camp;

/* The camp wants to sit well back from the wall so the approach reads
 * on camera, on ground flat enough for a tent, with the straight walk
 * back to the first hold unbroken by anything too steep to cross. */
#define CAMP_NEAR       12.f    /* preferred minimum distance from the wall */
#define CAMP_FAR        30.f
#define CAMP_MIN        6.f     /* accept closer only if nothing else fits */
#define PATH_WALK       0.74f   /* ground ny the approach must keep */
#define PATH_BREAK      0.70f   /* below this the ray hits a gully/crag */
#define PAD_FLAT        0.90f   /* tent-grade ground */
#define PAD_FLAT_RELAX  0.84f
#define TENT_OUT        1.9f    /* tent sits behind the fire, off to a side */
#define TENT_SIDE       1.3f
#define SPAWN_SIDE      1.15f   /* player wakes on the fire's other side */
#define EDGE_MARGIN     3.f

static bool in_bounds(float x, float z) {
    return fabsf(x) < MTN_HALF - EDGE_MARGIN && fabsf(z) < MTN_HALF - EDGE_MARGIN;
}

static float ground_ny(float x, float z) {
    float n[3];
    mountain_surface(x, z, n);
    return n[1];
}

/* Fire, tent and spawn must all sit on pad-grade ground. */
static bool pad_ok(const float fire[3], const float out[3],
                   const float side[3], float flat) {
    float tent[3], spawn[3];
    v3_copy(tent, fire);
    v3_mad(tent, out, TENT_OUT);
    v3_mad(tent, side, TENT_SIDE);
    v3_copy(spawn, fire);
    v3_mad(spawn, side, -SPAWN_SIDE);

    if (!in_bounds(fire[0], fire[2]) || !in_bounds(tent[0], tent[2]) ||
        !in_bounds(spawn[0], spawn[2]))
        return false;
    return ground_ny(fire[0], fire[2])   >= flat &&
           ground_ny(tent[0], tent[2])   >= flat - 0.02f &&
           ground_ny(spawn[0], spawn[2]) >= flat - 0.02f;
}

static void commit(const float fire[3], const float out[3], const float side[3]) {
    v3_copy(camp.fire, fire);
    camp.fire[1] = mountain_surface(fire[0], fire[2], NULL);

    v3_copy(camp.tent, camp.fire);
    v3_mad(camp.tent, out, TENT_OUT);
    v3_mad(camp.tent, side, TENT_SIDE);
    camp.tent[1] = mountain_surface(camp.tent[0], camp.tent[2], NULL);
    float d[3];
    v3_sub(d, camp.fire, camp.tent);
    camp.tent_yaw = atan2f(d[0], d[2]);   /* door toward the fire */

    v3_copy(camp.spawn, camp.fire);
    v3_mad(camp.spawn, side, -SPAWN_SIDE);
    camp.spawn[1] = mountain_surface(camp.spawn[0], camp.spawn[2], NULL);
    camp.spawn_yaw = atan2f(-out[0], -out[2]);   /* face the wall */

    camp.valid = true;
}

/* March one ray out from the wall. Returns the preferred site in
 * best_pref (t >= CAMP_NEAR, tent-grade) or the farthest relaxed site
 * in best_fall; either may stay unset. */
static void march_ray(const float wall[3], const float out[3],
                      float best_pref[3], bool *has_pref,
                      float best_fall[3], float *fall_t) {
    float side[3] = { out[2], 0.f, -out[0] };
    bool walking = false;

    for (float t = 1.f; t <= CAMP_FAR; t += 0.5f) {
        float p[3];
        v3_copy(p, wall);
        v3_mad(p, out, t);
        if (!in_bounds(p[0], p[2]))
            return;

        float ny = ground_ny(p[0], p[2]);
        if (!walking) {
            if (ny >= PATH_WALK)
                walking = true;
            continue;
        }
        if (ny < PATH_BREAK)
            return;   /* apron broken: no walkable line past here */

        if (t >= CAMP_NEAR && pad_ok(p, out, side, PAD_FLAT)) {
            v3_copy(best_pref, p);
            *has_pref = true;
            return;
        }
        if (t >= CAMP_MIN && t > *fall_t && pad_ok(p, out, side, PAD_FLAT_RELAX)) {
            v3_copy(best_fall, p);
            *fall_t = t;
        }
    }
}

bool campsite_place(const float wall_pos[3], const float out_xz[3]) {
    camp.valid = false;

    /* Straight out first, then fan left/right if the direct line is
     * blocked or never flattens. */
    static const float angles[5] = { 0.f, 0.35f, -0.35f, 0.7f, -0.7f };
    float best_fall[3] = { 0.f, 0.f, 0.f };
    float fall_side[3] = { 0.f, 0.f, 0.f };
    float fall_out[3] = { 0.f, 0.f, 0.f };
    float fall_t = 0.f;

    for (int a = 0; a < 5; a++) {
        float ca = cosf(angles[a]), sa = sinf(angles[a]);
        float out[3] = {
            out_xz[0] * ca + out_xz[2] * sa,
            0.f,
            -out_xz[0] * sa + out_xz[2] * ca,
        };
        float side[3] = { out[2], 0.f, -out[0] };

        float pref[3] = { 0.f, 0.f, 0.f };
        bool has_pref = false;
        float ray_fall[3] = { 0.f, 0.f, 0.f };
        float ray_fall_t = 0.f;
        march_ray(wall_pos, out, pref, &has_pref, ray_fall, &ray_fall_t);
        if (has_pref) {
            commit(pref, out, side);
            return true;
        }
        if (ray_fall_t > fall_t) {
            fall_t = ray_fall_t;
            v3_copy(best_fall, ray_fall);
            v3_copy(fall_out, out);
            v3_copy(fall_side, side);
        }
    }

    if (fall_t > 0.f) {
        commit(best_fall, fall_out, fall_side);
        return true;
    }
    return false;
}

const campsite_t *campsite_get(void) {
    return &camp;
}
