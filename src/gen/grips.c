#include "grips.h"
#include "mountain.h"
#include "noise.h"
#include <math.h>

/* Grips live on faces steep enough to need hands (ny below
 * GRIP_SLOPE_MAX), where the high-frequency mask is positive — the
 * "small noise artifacts" of GDD 3.2. The ~50% acceptance keeps the
 * reach-radius neighbor graph above its percolation threshold, so
 * routes connect instead of dead-ending on grip islands; mask lows
 * still thin the field into the crux moves. */
#define GRIP_SLOPE_MAX  0.70f   /* 45°+ only: gentler ground is a walk/
                                   scramble where the wall-climb posture
                                   degenerates (hands can't reach rock) */
/* Grip sampling spacing is pinned in world metres (not a fraction of the
 * cell) so density stays constant if the world is rescaled — after the 2x
 * footprint bump a cell-relative step would have doubled to 1.2m and halved
 * the grip density. GRIP_SUBS is the sub-samples/axis that tile one cell. */
#define GRIP_STEP       0.7f
#define GRIP_SUBS       ((int)(MTN_CELL_SIZE / GRIP_STEP + 0.5f))   /* 17 @ 12m */
#define GRIP_MASK_FREQ  1.05f
#define GRIP_MASK_MIN   (-0.15f)  /* ~60% acceptance: the coarser 0.7m grid
                                     puts diagonal neighbours past arm reach,
                                     so accept a few more holds to keep the
                                     reach-radius graph above percolation */

static grip_t grips[GRIP_MAX];
static int    n_grips;

/* Bucket = terrain cell (56x56); counting sort into a flat index list. */
#define BUCKETS (MTN_CELLS * MTN_CELLS)
static int32_t bucket_of[GRIP_MAX];
static int     bucket_start[BUCKETS + 1];
static int32_t bucket_items[GRIP_MAX];

static int bucket_index(float x, float z) {
    int bx = (int)((x + MTN_HALF) / MTN_CELL_SIZE);
    int bz = (int)((z + MTN_HALF) / MTN_CELL_SIZE);
    if (bx < 0) bx = 0; else if (bx >= MTN_CELLS) bx = MTN_CELLS - 1;
    if (bz < 0) bz = 0; else if (bz >= MTN_CELLS) bz = MTN_CELLS - 1;
    return bz * MTN_CELLS + bx;
}

static void try_grip(float x, float z) {
    if (n_grips >= GRIP_MAX)
        return;
    if (x < -MTN_HALF || x > MTN_HALF || z < -MTN_HALF || z > MTN_HALF)
        return;

    float mask = noise_simplex2(x * GRIP_MASK_FREQ * (1.f / MTN_CELL_SIZE) + 400.f,
                                z * GRIP_MASK_FREQ * (1.f / MTN_CELL_SIZE) - 220.f);
    if (mask < GRIP_MASK_MIN)
        return;

    float n[3];
    float h = mountain_surface(x, z, n);
    if (n[1] > GRIP_SLOPE_MAX)   /* too flat to need a hold */
        return;

    grip_t *g = &grips[n_grips];
    g->pos[0] = x; g->pos[1] = h; g->pos[2] = z;
    g->n[0] = n[0]; g->n[1] = n[1]; g->n[2] = n[2];
    bucket_of[n_grips] = bucket_index(x, z);
    n_grips++;
}

void grips_generate(void) {
    n_grips = 0;

    for (int cz = 0; cz < MTN_CELLS; cz++) {
        for (int cx = 0; cx < MTN_CELLS; cx++) {
            /* Cheap cell reject: corner spread below the slope needed
             * for holds means no sub-sample inside can qualify. */
            float h00 = mountain_height_at(cx,     cz);
            float h10 = mountain_height_at(cx + 1, cz);
            float h01 = mountain_height_at(cx,     cz + 1);
            float h11 = mountain_height_at(cx + 1, cz + 1);
            float hmin = fminf(fminf(h00, h10), fminf(h01, h11));
            float hmax = fmaxf(fmaxf(h00, h10), fmaxf(h01, h11));
            if (hmax - hmin < MTN_CELL_SIZE * 0.8f)
                continue;

            float x0 = cx * MTN_CELL_SIZE - MTN_HALF;
            float z0 = cz * MTN_CELL_SIZE - MTN_HALF;
            for (int sz = 0; sz < GRIP_SUBS; sz++) {
                for (int sx = 0; sx < GRIP_SUBS; sx++) {
                    float x = x0 + (sx + 0.5f) * GRIP_STEP;
                    float z = z0 + (sz + 0.5f) * GRIP_STEP;
                    /* Jitter breaks the sample grid so holds read organic. */
                    x += 0.22f * noise_simplex2(x * 3.1f, z * 3.1f + 77.f);
                    z += 0.22f * noise_simplex2(x * 3.1f - 51.f, z * 3.1f);
                    try_grip(x, z);
                }
            }
        }
    }

    /* Counting sort into per-bucket runs. */
    for (int b = 0; b <= BUCKETS; b++)
        bucket_start[b] = 0;
    for (int i = 0; i < n_grips; i++)
        bucket_start[bucket_of[i] + 1]++;
    for (int b = 0; b < BUCKETS; b++)
        bucket_start[b + 1] += bucket_start[b];
    static int cursor[BUCKETS];
    for (int b = 0; b < BUCKETS; b++)
        cursor[b] = bucket_start[b];
    for (int i = 0; i < n_grips; i++)
        bucket_items[cursor[bucket_of[i]]++] = i;
}

int grips_count(void) { return n_grips; }

const grip_t *grip_get(int i) { return &grips[i]; }

/* Visit buckets overlapping the query sphere's XZ bounding box. */
#define FOR_BUCKET_RANGE(p, radius)                                        \
    int bx0 = (int)((p[0] - radius + MTN_HALF) / MTN_CELL_SIZE);           \
    int bx1 = (int)((p[0] + radius + MTN_HALF) / MTN_CELL_SIZE);           \
    int bz0 = (int)((p[2] - radius + MTN_HALF) / MTN_CELL_SIZE);           \
    int bz1 = (int)((p[2] + radius + MTN_HALF) / MTN_CELL_SIZE);           \
    if (bx0 < 0) bx0 = 0;                                                  \
    if (bz0 < 0) bz0 = 0;                                                  \
    if (bx1 >= MTN_CELLS) bx1 = MTN_CELLS - 1;                             \
    if (bz1 >= MTN_CELLS) bz1 = MTN_CELLS - 1;

static float dist2(const float a[3], const float b[3]) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

int grips_nearest(const float p[3], float radius, int exclude) {
    FOR_BUCKET_RANGE(p, radius);
    float best = radius * radius;
    int best_i = -1;
    for (int bz = bz0; bz <= bz1; bz++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            int b = bz * MTN_CELLS + bx;
            for (int k = bucket_start[b]; k < bucket_start[b + 1]; k++) {
                int i = bucket_items[k];
                if (i == exclude)
                    continue;
                float d2 = dist2(p, grips[i].pos);
                if (d2 < best) {
                    best = d2;
                    best_i = i;
                }
            }
        }
    }
    return best_i;
}

int grips_column(const float p[3], float radius_xz, float height) {
    FOR_BUCKET_RANGE(p, radius_xz);
    float r2 = radius_xz * radius_xz;
    int n = 0;
    for (int bz = bz0; bz <= bz1; bz++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            int b = bz * MTN_CELLS + bx;
            for (int k = bucket_start[b]; k < bucket_start[b + 1]; k++) {
                const grip_t *g = &grips[bucket_items[k]];
                float dx = g->pos[0] - p[0], dz = g->pos[2] - p[2];
                float dy = g->pos[1] - p[1];
                if (dx * dx + dz * dz <= r2 && dy >= 0.f && dy <= height)
                    n++;
            }
        }
    }
    return n;
}

int grips_collect(const float p[3], float radius, int *out, int max) {
    FOR_BUCKET_RANGE(p, radius);
    float r2 = radius * radius;
    int n = 0;
    for (int bz = bz0; bz <= bz1 && n < max; bz++) {
        for (int bx = bx0; bx <= bx1 && n < max; bx++) {
            int b = bz * MTN_CELLS + bx;
            for (int k = bucket_start[b]; k < bucket_start[b + 1] && n < max; k++) {
                int i = bucket_items[k];
                if (dist2(p, grips[i].pos) <= r2)
                    out[n++] = i;
            }
        }
    }
    return n;
}
