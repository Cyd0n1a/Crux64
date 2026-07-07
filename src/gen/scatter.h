#pragma once

#include <stdint.h>

/* Terrain scatter (GDD 1.2 "procedural yet permanent", 3.2): stunted
 * conifers along the lower slopes, mossy rocks and the occasional
 * boulder strewn over the flanks. Placed once at boot from the same
 * fixed seed as the mountain, so the dressing is identical for every
 * player. Pure C + math.h, host-testable like the rest of src/gen/;
 * the meshes themselves are built in src/render/scatter_render.c.
 *
 * Must run AFTER mountain_generate() (needs the surface) and after the
 * campsite is placed (its footprint is kept clear). */

typedef enum {
    SCAT_TREE = 0,
    SCAT_ROCK,
    SCAT_BOULDER,
    SCAT_KIND_COUNT
} scatter_kind_t;

typedef struct {
    float   pos[3];     /* on the mesh surface */
    float   yaw;        /* random spin about Y */
    float   scale;      /* world height (trees) / radius (rocks), metres */
    uint8_t kind;       /* scatter_kind_t */
    uint8_t variant;    /* which mesh of that kind */
} scatter_t;

void scatter_generate(void);
int  scatter_count(void);
const scatter_t *scatter_get(int i);

/* How many mesh variants the renderer should build per kind. */
#define SCAT_TREE_VARIANTS    3
#define SCAT_ROCK_VARIANTS    3
#define SCAT_BOULDER_VARIANTS 2
