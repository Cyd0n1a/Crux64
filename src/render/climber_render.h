#pragma once

/* Stick-figure + grip-marker drawing. Must be called inside an active
 * t3d frame (render_frame does), after the terrain. */
void climber_render_init(void);
void climber_render_draw(void);
