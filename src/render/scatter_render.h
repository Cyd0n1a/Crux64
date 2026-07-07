#pragma once

#include <t3d/t3d.h>

/* Terrain-scatter visuals: proctree-generated stunted conifers plus
 * procedural mossy rocks and boulders, instanced across the placements
 * from src/gen/scatter.c. Meshes are built once and baked into recorded
 * rspq blocks; every instance's matrix is static (the dressing never
 * moves) so draw time is just cull + matrix push + block run.
 *
 * scatter_render_init() must run after scatter_generate(). Draw between
 * the climber and the campfire so it inherits the sun + fire lighting
 * (the campfire pass leaves the light state cleared for its flames). */

void scatter_render_init(void);
void scatter_render_draw(const T3DVec3 *eye, const T3DVec3 *target);
