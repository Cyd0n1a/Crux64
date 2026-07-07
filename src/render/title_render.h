#pragma once

#include <t3d/t3d.h>

/* Title-screen logo: "CRUX64" spelled in multi-coloured cubes floating
 * in front of the camera, waving on a group sine and cycling a rainbow
 * ripple. Drawn only while the game sits on the title screen. */

void title_render_init(void);
void title_render_draw(const T3DVec3 *eye, const T3DVec3 *target);
