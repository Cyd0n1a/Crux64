#pragma once

/* Base camp visuals: A-frame tent, stone fire ring with a log teepee,
 * and an animated campfire (flickering unlit flame octas; the matching
 * point light is set by render.c so it also lights the terrain). */

void campsite_render_init(void);
void campsite_render_draw(void);
