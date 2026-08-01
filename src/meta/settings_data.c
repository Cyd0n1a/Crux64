#include "settings_data.h"

static const float sens_table[SET_SENS_MAX + 1] = {
    0.50f, 0.75f, 1.00f, 1.50f, 2.00f,
};

void settings_data_defaults(save_settings_t *s) {
    s->music_vol = 7;
    s->sfx_vol   = 7;
    s->cam_sens  = 2;                                  /* 1.00x */
    s->flags     = SET_FLAG_RUMBLE | SET_FLAG_STAMINA;
    for (int i = 0; i < 4; i++) s->reserved[i] = 0;
}

/* Zero for the three scalar settings, which live in their own bytes. */
static uint8_t flag_bit(int id) {
    switch (id) {
    case SET_INVERT_X:    return SET_FLAG_INVERT_X;
    case SET_INVERT_Y:    return SET_FLAG_INVERT_Y;
    case SET_RUMBLE:      return SET_FLAG_RUMBLE;
    case SET_STAMINA_BAR: return SET_FLAG_STAMINA;
    default:              return 0;
    }
}

static int id_vmax(int id) {
    switch (id) {
    case SET_MUSIC_VOL:
    case SET_SFX_VOL:  return SET_VOL_MAX;
    case SET_CAM_SENS: return SET_SENS_MAX;
    default:           return 1;
    }
}

int settings_data_get(const save_settings_t *s, int id) {
    switch (id) {
    case SET_MUSIC_VOL: return s->music_vol;
    case SET_SFX_VOL:   return s->sfx_vol;
    case SET_CAM_SENS:  return s->cam_sens;
    default: {
        const uint8_t b = flag_bit(id);
        return (b && (s->flags & b)) ? 1 : 0;
    }
    }
}

void settings_data_adjust(save_settings_t *s, int id, int delta) {
    const int hi = id_vmax(id);
    int v = settings_data_get(s, id) + delta;
    if (v < 0)  v = 0;
    if (v > hi) v = hi;

    switch (id) {
    case SET_MUSIC_VOL: s->music_vol = (uint8_t)v; break;
    case SET_SFX_VOL:   s->sfx_vol   = (uint8_t)v; break;
    case SET_CAM_SENS:  s->cam_sens  = (uint8_t)v; break;
    default: {
        const uint8_t b = flag_bit(id);
        if (!b) break;
        if (v) s->flags |= b;
        else   s->flags  = (uint8_t)(s->flags & (uint8_t)~b);
        break;
    }
    }
}

float settings_data_sens(const save_settings_t *s) {
    uint8_t i = s->cam_sens;
    if (i > SET_SENS_MAX) i = SET_SENS_MAX;
    return sens_table[i];
}

static const char *const lbl_dir[]  = { "NORMAL", "INVERTED" };
static const char *const lbl_on[]   = { "OFF", "ON" };
static const char *const lbl_sens[] = { "0.50x", "0.75x", "1.00x",
                                        "1.50x", "2.00x" };

/* value_labels NULL means "draw a bar", which is what the two volumes
 * want; the sensitivity slider names its steps instead. */
static const menu_item_t rows[] = {
    { "MUSIC",       MENU_SLIDER, SET_MUSIC_VOL,   SET_VOL_MAX,  NULL     },
    { "SOUND",       MENU_SLIDER, SET_SFX_VOL,     SET_VOL_MAX,  NULL     },
    { "CAMERA X",    MENU_TOGGLE, SET_INVERT_X,    1,            lbl_dir  },
    { "CAMERA Y",    MENU_TOGGLE, SET_INVERT_Y,    1,            lbl_dir  },
    { "SENSITIVITY", MENU_SLIDER, SET_CAM_SENS,    SET_SENS_MAX, lbl_sens },
    { "RUMBLE",      MENU_TOGGLE, SET_RUMBLE,      1,            lbl_on   },
    { "STAMINA BAR", MENU_TOGGLE, SET_STAMINA_BAR, 1,            lbl_on   },
    { "BACK",        MENU_ACTION, MENU_BACK,       0,            NULL     },
};

const menu_item_t *settings_data_rows(int *count) {
    if (count) *count = (int)(sizeof rows / sizeof rows[0]);
    return rows;
}
