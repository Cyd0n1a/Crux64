#pragma once

#include <t3d/t3d.h>
#include <surface.h>

/* Procedural sky (post-roadmap polish). Everything is generated at boot from
 * the same simplex noise the mountain uses (GDD 3.2) — no ROM assets:
 *
 *   - a cloud dome: a hemisphere centred on the camera, textured with a
 *     tileable fbm coverage map and UV-scrolled so the clouds drift gently.
 *     It draws as a pure backdrop (no depth), and the terrain overdraws it.
 *   - a screen-space sun disc + lens flare, projected from the sun direction
 *     and faded out when the sun leaves the screen or the mountain occludes
 *     it (sampled one frame late from the depth buffer). */

/* The world-space sun direction, shared by the light rig (render.c) and the
 * visible sun sprite so the flare sits where the light comes from. */
extern const T3DVec3 SKY_SUN_DIR;

void sky_render_init(void);

/* Backdrop cloud dome. Call right after the screen clears and before the
 * terrain: it disables depth for its own draw and restores the frame's
 * z-buffer mode on exit, leaving the matrix stack balanced. */
void sky_dome_draw(T3DViewport *vp, const T3DVec3 *eye);

/* Sun disc + lens flare, screen-space. Call in the overlay stage, after the
 * 3D pass and before the HUD text. */
void sky_sun_draw(T3DViewport *vp, const T3DVec3 *eye);

/* Sample the depth buffer at last frame's sun position for occlusion. Call
 * once at the top of the frame, before the depth clear (it reads the depth
 * left over from the previous frame). */
void sky_sun_presample(const surface_t *zbuf);
