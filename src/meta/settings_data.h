#pragma once

/* Pure settings logic — no libdragon, no N64 headers, so this file is
 * compiled both into the ROM and into tests/run_tests.sh on the host.
 * Stateless: every function takes the record it operates on. The live
 * record, its EEPROM storage and its side effects live in settings.c. */

#include <stdint.h>
#include <stdbool.h>

#include "menu.h"

/* One 8-byte EEPROM block, stored in its own container at blocks 2-3. */
typedef struct __attribute__((packed)) {
    uint8_t music_vol;   /* 0..SET_VOL_MAX */
    uint8_t sfx_vol;     /* 0..SET_VOL_MAX */
    uint8_t cam_sens;    /* 0..SET_SENS_MAX, an index into the sens table */
    uint8_t flags;       /* SET_FLAG_* bits */

    /* Zeroed by settings_data_defaults and NEVER cleared on store: a record
     * loaded from the cart is written back verbatim. That is what lets a
     * later version claim these bytes without a version bump — an older
     * build preserves settings it does not understand instead of destroying
     * them. Only works while zero is the right default for whatever lands
     * here; a setting needing a nonzero default needs a version bump. */
    uint8_t reserved[4];
} save_settings_t;

_Static_assert(sizeof(save_settings_t) == 8,
               "save_settings_t must be one 8-byte EEPROM block");

#define SET_FLAG_INVERT_X  0x01
#define SET_FLAG_INVERT_Y  0x02
#define SET_FLAG_RUMBLE    0x04
#define SET_FLAG_STAMINA   0x08

typedef enum {
    SET_MUSIC_VOL,
    SET_SFX_VOL,
    SET_CAM_SENS,
    SET_INVERT_X,
    SET_INVERT_Y,
    SET_RUMBLE,
    SET_STAMINA_BAR,
    SET_ID_COUNT,
} setting_id_t;

#define SET_VOL_MAX   10
#define SET_SENS_MAX   4

void settings_data_defaults(save_settings_t *s);

/* Current value of one setting, 0..vmax. Flags report 0 or 1. */
int  settings_data_get(const save_settings_t *s, int id);

/* Moves one setting by `delta`, clamped to its range. Never wraps: a held
 * direction comes to rest at the end. Leaves reserved[] alone. */
void settings_data_adjust(save_settings_t *s, int id, int delta);

/* Camera sensitivity as a multiplier, from the cam_sens index. */
float settings_data_sens(const save_settings_t *s);

/* The settings screen's rows, including the trailing BACK action. The
 * table is const; settings.c pairs it with the callbacks that reach the
 * live record. */
const menu_item_t *settings_data_rows(int *count);
