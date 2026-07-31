#pragma once

#include "dialogue.h"

/* Scene 02 — "Crepuscular Twilight Stars": the camp-two checkpoint cutscene,
 * adapted from crux64-game-scene-two-dialogue-script.md into a linear
 * dialogue reel. Plays once per run, the first time the climber reaches the
 * first rest point on foot.
 *
 * Maya reads the tracker altitude back to the player, so the scene has to be
 * built rather than baked: pass the climber's current altitude in metres and
 * the returned array is valid until the next call. */
const dlg_line_t *scene02_scene(float alt_m, int *count);
