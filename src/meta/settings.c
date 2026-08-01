#include <libdragon.h>

#include "settings.h"
#include "settings_data.h"
#include "save_container.h"
#include "menu.h"
#include "../audio/synth.h"
#include "../input/rumble.h"

/* Blocks 0-1 hold the progress record; settings get their own container so
 * a piton commit never rewrites them and a pre-settings cart migrates by
 * simply having nothing here. */
#define SETTINGS_BASE  2

static save_settings_t cur;
static bool            dirty;
static menu_screen_t   screen;

static void apply(void) {
    synth_set_gains((float)cur.music_vol / (float)SET_VOL_MAX,
                    (float)cur.sfx_vol   / (float)SET_VOL_MAX);
    rumble_set_enabled((cur.flags & SET_FLAG_RUMBLE) != 0);
}

static int row_get(int id) { return settings_data_get(&cur, id); }

static void row_set(int id, int delta) {
    const int before = settings_data_get(&cur, id);
    settings_data_adjust(&cur, id, delta);
    if (settings_data_get(&cur, id) == before) return;   /* clamped, no-op */
    dirty = true;
    apply();                 /* live: you hear and feel what you adjust */
}

/* Fires when the settings screen pops. The only write path. */
static void row_close(void) {
    if (!dirty) return;
    if (eeprom_present() == EEPROM_NONE) { dirty = false; return; }

    if (save_container_store(SETTINGS_BASE, SAVE_SETTINGS_VERSION,
                             (const uint8_t *)&cur, (uint8_t)sizeof cur))
        dirty = false;
}

void settings_init(void) {
    int count = 0;
    const menu_item_t *rows = settings_data_rows(&count);

    settings_data_defaults(&cur);
    dirty = false;

    /* Loads over the defaults, and leaves them alone on anything that is
     * not a valid current record — including a cart written before
     * settings existed, which is the whole migration. allow_legacy is
     * false: the eepromfs signature means nothing at block 2. */
    if (eeprom_present() != EEPROM_NONE)
        save_container_load(SETTINGS_BASE, SAVE_SETTINGS_VERSION, false,
                            (uint8_t *)&cur, (uint8_t)sizeof cur);

    screen = (menu_screen_t){
        .title          = "SETTINGS",
        .items          = rows,
        .count          = count,
        .scrim          = true,
        .start_confirms = false,
        .panel_alpha    = 255,
        .get            = row_get,
        .set            = row_set,
        .on_close       = row_close,
    };
    menu_register_settings(&screen);

    apply();
}

const save_settings_t *settings_get(void) { return &cur; }
