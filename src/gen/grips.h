#pragma once

/* Procedural grip points (GDD 3.2): small high-frequency noise artifacts
 * on steep faces act as handholds/footholds. Generated once at boot after
 * the heightmap, bucketed per terrain cell for fast radius queries.
 * Pure C + math.h — host-testable like the rest of src/gen/. */

/* Expansion Pak RAM (~5MB incl. bucket indices, 32 bytes/grip). Sized for
 * the 2x-scaled world at 0.7m grip spacing: ~153k grips (measured, fixed
 * seed) with headroom. Above the GDD's original ~1.6MB budget because the
 * footprint grew 4x in area — density is preserved, so the count follows. */
#define GRIP_MAX 163840

typedef struct {
    float pos[3];   /* on the render mesh surface */
    float n[3];     /* face normal at the grip */
} grip_t;

void grips_generate(void);
int  grips_count(void);
const grip_t *grip_get(int i);

/* Nearest grip to p within radius (-1 if none); exclude skips one index. */
int grips_nearest(const float p[3], float radius, int exclude);

/* Gather up to max grip indices within radius of p; returns count. */
int grips_collect(const float p[3], float radius, int *out, int max);

/* Count grips in a vertical column: horizontal distance <= radius_xz of
 * p, altitude within [p.y, p.y + height]. Route-length heuristic. */
int grips_column(const float p[3], float radius_xz, float height);
