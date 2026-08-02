/* Host unit tests for the settings record. No libdragon and no N64
 * toolchain — run with tests/run_tests.sh. */
#include "../src/meta/settings_data.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do {                                         \
    if (!(cond)) {                                               \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                              \
    }                                                            \
} while (0)

static void test_defaults_match_the_spec(void) {
    save_settings_t s;
    settings_data_defaults(&s);
    CHECK(s.music_vol == 7);
    CHECK(s.sfx_vol   == 7);
    CHECK(s.cam_sens  == 2);
    CHECK(settings_data_get(&s, SET_INVERT_X)    == 0);
    CHECK(settings_data_get(&s, SET_INVERT_Y)    == 0);
    CHECK(settings_data_get(&s, SET_RUMBLE)      == 1);
    CHECK(settings_data_get(&s, SET_STAMINA_BAR) == 1);
}

static void test_defaults_zero_the_reserved_bytes(void) {
    save_settings_t s;
    memset(&s, 0xAB, sizeof s);
    settings_data_defaults(&s);
    for (int i = 0; i < 4; i++) CHECK(s.reserved[i] == 0);
}

/* The forward-compatibility rule: adjusting a setting must never disturb
 * bytes this build does not understand, or an older build reading a newer
 * cart would destroy the newer build's settings on the first change. */
static void test_adjust_never_touches_reserved(void) {
    save_settings_t s;
    settings_data_defaults(&s);
    for (int i = 0; i < 4; i++) s.reserved[i] = (uint8_t)(0x40 + i);
    for (int id = 0; id < SET_ID_COUNT; id++) {
        settings_data_adjust(&s, id, +1);
        settings_data_adjust(&s, id, -1);
    }
    for (int i = 0; i < 4; i++) CHECK(s.reserved[i] == (uint8_t)(0x40 + i));
}

/* Every id's range, walked from both ends. Clamping, never wrapping — a
 * held direction must come to rest at the end, not roll over. */
static void test_adjust_clamps_at_both_ends(void) {
    static const struct { int id; int vmax; } r[] = {
        { SET_MUSIC_VOL,   SET_VOL_MAX  },
        { SET_SFX_VOL,     SET_VOL_MAX  },
        { SET_CAM_SENS,    SET_SENS_MAX },
        { SET_INVERT_X,    1 },
        { SET_INVERT_Y,    1 },
        { SET_RUMBLE,      1 },
        { SET_STAMINA_BAR, 1 },
    };
    for (size_t i = 0; i < sizeof r / sizeof r[0]; i++) {
        save_settings_t s;
        settings_data_defaults(&s);

        for (int n = 0; n < 40; n++) settings_data_adjust(&s, r[i].id, +1);
        CHECK(settings_data_get(&s, r[i].id) == r[i].vmax);

        for (int n = 0; n < 40; n++) settings_data_adjust(&s, r[i].id, -1);
        CHECK(settings_data_get(&s, r[i].id) == 0);
    }
}

static void test_every_value_in_range_round_trips(void) {
    save_settings_t s;
    settings_data_defaults(&s);
    for (int n = 0; n < 40; n++) settings_data_adjust(&s, SET_MUSIC_VOL, -1);
    for (int v = 0; v <= SET_VOL_MAX; v++) {
        CHECK(settings_data_get(&s, SET_MUSIC_VOL) == v);
        settings_data_adjust(&s, SET_MUSIC_VOL, +1);
    }
}

/* Each flag owns exactly one bit: setting one must not disturb the others. */
static void test_flags_are_independent(void) {
    static const int ids[] = { SET_INVERT_X, SET_INVERT_Y,
                               SET_RUMBLE, SET_STAMINA_BAR };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        save_settings_t s;
        settings_data_defaults(&s);

        int before[4];
        for (size_t j = 0; j < 4; j++) before[j] = settings_data_get(&s, ids[j]);

        int flipped = before[i] ? -1 : +1;
        settings_data_adjust(&s, ids[i], flipped);

        for (size_t j = 0; j < 4; j++) {
            if (j == i) CHECK(settings_data_get(&s, ids[j]) != before[j]);
            else        CHECK(settings_data_get(&s, ids[j]) == before[j]);
        }
    }
}

static void test_sensitivity_table(void) {
    save_settings_t s;
    settings_data_defaults(&s);
    CHECK(settings_data_sens(&s) == 1.00f);          /* default index 2 */

    for (int n = 0; n < 40; n++) settings_data_adjust(&s, SET_CAM_SENS, -1);
    CHECK(settings_data_sens(&s) == 0.50f);

    for (int n = 0; n < 40; n++) settings_data_adjust(&s, SET_CAM_SENS, +1);
    CHECK(settings_data_sens(&s) == 2.00f);
}

/* The row table is data the menu trusts blindly: a slider whose vmax
 * disagrees with its field's real range draws a bar the value can never
 * fill, and a labelled row short one string indexes off the end. */
static void test_row_table_is_consistent(void) {
    int count = 0;
    const menu_item_t *rows = settings_data_rows(&count);
    CHECK(rows != NULL);
    CHECK(count == 8);            /* seven settings plus BACK */

    int value_rows = 0;
    for (int i = 0; i < count; i++) {
        const menu_item_t *it = &rows[i];
        CHECK(it->label != NULL);

        if (it->kind == MENU_ACTION) {
            CHECK(it->id == MENU_BACK);
            continue;
        }
        value_rows++;

        /* vmax must be the value the field actually saturates at. */
        save_settings_t s;
        settings_data_defaults(&s);
        for (int n = 0; n < 40; n++) settings_data_adjust(&s, it->id, +1);
        CHECK(settings_data_get(&s, it->id) == it->vmax);

        if (it->value_labels)
            for (int v = 0; v <= it->vmax; v++) CHECK(it->value_labels[v] != NULL);
    }
    CHECK(value_rows == 7);
}

int main(void) {
    test_defaults_match_the_spec();
    test_defaults_zero_the_reserved_bytes();
    test_adjust_never_touches_reserved();
    test_adjust_clamps_at_both_ends();
    test_every_value_in_range_round_trips();
    test_flags_are_independent();
    test_sensitivity_table();
    test_row_table_is_consistent();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all settings checks passed\n");
    return 0;
}
