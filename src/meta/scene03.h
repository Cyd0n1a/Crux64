#pragma once

#include "dialogue.h"

/* Scene 03 — "The Circling Hour": morning of day two at camp two, adapted
 * from crux64-game-scene-three-dialogue-script.md.
 *
 * NOT WIRED UP YET. The script places this scene after the meditation
 * minigame, which does not exist, so nothing calls this accessor. The lines
 * are adapted and compiled so they stay in step with the dialogue API; hook
 * it in once meditation lands and camp two can advance to day two. */
const dlg_line_t *scene03_scene(int *count);
