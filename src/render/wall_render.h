#pragma once

/* World-boundary cliff wall (GDD-adjacent: a hard edge to the playfield). A
 * tall solid cylinder ringing the terrain footprint at WORLD_WALL_R, its base
 * following the ground and its top a fixed height above. Textured with the
 * authored tiling cliff sprite, lit and fogged like the terrain so it fades
 * into the horizon rather than reading as a sharp box. Built once at boot from
 * the mountain surface. */

void wall_render_init(void);

/* Draw the wall ring. Uses the world render state (shaded + fog + back-cull)
 * already bound by the terrain pass, and the identity model matrix. */
void wall_render_draw(void);
