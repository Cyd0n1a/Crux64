#include "scatter.h"
#include "mountain.h"
#include "noise.h"
#include "../sim/campsite.h"

#include <math.h>

/* One grid pass over the footprint: at each jittered sample the slope
 * and altitude decide whether a tree, a rock or (rarely) a boulder
 * drops there. A low-frequency "grove" field clusters the conifers into
 * stands instead of a uniform lawn, and thins them out toward a rough
 * treeline. Everything below is gated on the mesh-exact surface so the
 * trunks and rocks sit flush on the triangles the renderer draws. */

#define SCAT_STEP        4.0f    /* candidate spacing (m) */
#define EDGE_MARGIN      6.0f

/* Trees: gentle ground below the treeline. */
#define TREE_NY_MIN      0.80f
#define TREE_H_LO        2.0f
#define TREELINE         58.0f
#define TREE_FADE_LO     30.0f   /* full density below here, 0 at TREELINE */
#define GROVE_FREQ       0.028f
#define GROVE_MIN        0.14f   /* grove field must clear this to seed trees */
#define TREE_DENSITY     0.38f   /* stands stay open, not a solid canopy */

/* Mossy rocks: broad spread on anything not near-vertical. */
#define ROCK_NY_MIN      0.62f
#define ROCK_H_HI        118.0f
#define ROCK_PROB        0.038f

/* Boulders: rare, and they tolerate steeper ground than the rocks. */
#define BOULDER_NY_MIN   0.50f
#define BOULDER_H_HI     138.0f
#define BOULDER_PROB     0.006f

/* Keep the camp and its immediate approach clear of dressing. */
#define CAMP_CLEAR       7.0f
#define SPAWN_CLEAR      4.0f

#define MAX_TREES        130
#define MAX_ROCKS        230
#define MAX_BOULDERS     40
#define SCAT_MAX         (MAX_TREES + MAX_ROCKS + MAX_BOULDERS)

static scatter_t items[SCAT_MAX];
static int       n_items;
static int       n_trees, n_rocks, n_boulders;

/* Deterministic per-sample hash → LCG, so placement never depends on
 * iteration order and reproduces exactly from the fixed seed. */
static uint32_t hash_seed(int ix, int iz) {
    uint32_t h = MTN_SEED ^ 0x9E3779B9u;
    h ^= (uint32_t)ix * 0x85EBCA6Bu;
    h  = (h << 13) | (h >> 19);
    h ^= (uint32_t)iz * 0xC2B2AE35u;
    h *= 0x27D4EB2Fu;
    h ^= h >> 15;
    return h ? h : 1u;
}
static float rng01(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return (float)(*s >> 8) * (1.f / 16777216.f);   /* [0,1) */
}

static float smooth_fade(float h) {   /* 1 below FADE_LO → 0 at TREELINE */
    if (h <= TREE_FADE_LO) return 1.f;
    if (h >= TREELINE)     return 0.f;
    float t = (h - TREE_FADE_LO) / (TREELINE - TREE_FADE_LO);
    return 1.f - t * t * (3.f - 2.f * t);
}

static bool near_camp(float x, float z) {
    const campsite_t *c = campsite_get();
    if (!c->valid)
        return false;
    float dx = x - c->fire[0], dz = z - c->fire[2];
    if (dx * dx + dz * dz < CAMP_CLEAR * CAMP_CLEAR)
        return true;
    dx = x - c->spawn[0]; dz = z - c->spawn[2];
    if (dx * dx + dz * dz < SPAWN_CLEAR * SPAWN_CLEAR)
        return true;
    dx = x - c->tent[0]; dz = z - c->tent[2];
    return dx * dx + dz * dz < SPAWN_CLEAR * SPAWN_CLEAR;
}

static void push(scatter_kind_t kind, int variant, float x, float z,
                 float h, float yaw, float scale) {
    if (n_items >= SCAT_MAX)
        return;
    scatter_t *s = &items[n_items++];
    s->pos[0] = x; s->pos[1] = h; s->pos[2] = z;
    s->yaw = yaw; s->scale = scale;
    s->kind = (uint8_t)kind; s->variant = (uint8_t)variant;
}

void scatter_generate(void) {
    n_items = n_trees = n_rocks = n_boulders = 0;
    noise_seed(MTN_SEED);   /* grove field shares the mountain's noise */

    float lim = MTN_HALF - EDGE_MARGIN;
    for (float gz = -lim; gz <= lim; gz += SCAT_STEP) {
        for (float gx = -lim; gx <= lim; gx += SCAT_STEP) {
            int ix = (int)floorf(gx / SCAT_STEP);
            int iz = (int)floorf(gz / SCAT_STEP);
            uint32_t s = hash_seed(ix, iz);

            /* Jitter the sample inside its cell so rows don't line up. */
            float x = gx + (rng01(&s) - 0.5f) * SCAT_STEP;
            float z = gz + (rng01(&s) - 0.5f) * SCAT_STEP;

            float n[3];
            float h = mountain_surface(x, z, n);
            float ny = n[1];

            if (near_camp(x, z))
                continue;

            /* Trees first: a stand of conifers wins the spot outright. */
            if (ny >= TREE_NY_MIN && h >= TREE_H_LO && h < TREELINE
                && n_trees < MAX_TREES) {
                float grove = noise_fbm2(x * GROVE_FREQ, z * GROVE_FREQ,
                                         3, 2.0f, 0.5f);
                if (grove > GROVE_MIN) {
                    float density = (grove - GROVE_MIN) * TREE_DENSITY * smooth_fade(h);
                    if (rng01(&s) < density) {
                        int   var   = (int)(rng01(&s) * SCAT_TREE_VARIANTS);
                        if (var >= SCAT_TREE_VARIANTS) var = SCAT_TREE_VARIANTS - 1;
                        float yaw   = rng01(&s) * (float)(2.0 * M_PI);
                        /* Stunted at altitude, taller down in the valley. */
                        float tall  = 3.2f + 3.0f * smooth_fade(h) + rng01(&s) * 1.4f;
                        push(SCAT_TREE, var, x, z, h, yaw, tall);
                        n_trees++;
                        continue;
                    }
                }
            }

            /* Boulders are rarer than rocks, so roll them first. */
            if (ny >= BOULDER_NY_MIN && h < BOULDER_H_HI
                && n_boulders < MAX_BOULDERS && rng01(&s) < BOULDER_PROB) {
                int   var   = (int)(rng01(&s) * SCAT_BOULDER_VARIANTS);
                if (var >= SCAT_BOULDER_VARIANTS) var = SCAT_BOULDER_VARIANTS - 1;
                float yaw   = rng01(&s) * (float)(2.0 * M_PI);
                float rad   = 1.3f + rng01(&s) * 1.7f;
                push(SCAT_BOULDER, var, x, z, h, yaw, rad);
                n_boulders++;
                continue;
            }

            if (ny >= ROCK_NY_MIN && h < ROCK_H_HI
                && n_rocks < MAX_ROCKS && rng01(&s) < ROCK_PROB) {
                int   var   = (int)(rng01(&s) * SCAT_ROCK_VARIANTS);
                if (var >= SCAT_ROCK_VARIANTS) var = SCAT_ROCK_VARIANTS - 1;
                float yaw   = rng01(&s) * (float)(2.0 * M_PI);
                float rad   = 0.28f + rng01(&s) * 0.55f;
                push(SCAT_ROCK, var, x, z, h, yaw, rad);
                n_rocks++;
            }
        }
    }
}

int scatter_count(void) { return n_items; }

const scatter_t *scatter_get(int i) { return &items[i]; }
