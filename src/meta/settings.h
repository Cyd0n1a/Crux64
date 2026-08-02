#pragma once

#include "settings_data.h"

/* The live settings record: loaded from its own EEPROM container at
 * blocks 2-3, applied to the audio and rumble subsystems, and exposed to
 * main.c for the camera and the stamina bar.
 *
 * Writes happen once, when the settings screen closes, and only if a value
 * actually changed — a held slider must never write per frame, at ~6ms and
 * a wear cycle each. */

/* Loads the record (or defaults), applies it, and registers the settings
 * screen with the menu. Call after rumble_init and synth_init, since it
 * pushes to both. */
void settings_init(void);

const save_settings_t *settings_get(void);
