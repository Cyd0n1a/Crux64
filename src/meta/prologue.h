#pragma once

#include "dialogue.h"

/* Scene 01 — "The Morning After": the base-camp prologue, adapted from
 * crux64-game-scene-one-dialogue-script.md into a linear dialogue reel.
 * The scripted actions (leave the tent, pour the tea) are folded in as
 * prompt beats the player clicks through; the tent-crawl / tea-pour
 * choreography is a separate, larger gameplay task. */
const dlg_line_t *prologue_scene(int *count);
