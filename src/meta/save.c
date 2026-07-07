/* Phase 6 (GDD 3.4): EEPROM-backed save state.
 *
 * libdragon's eepromfs (eepfs_*) gives us a signature block that tells a
 * blank (or another game's) cart apart from ours, so first boot lays down
 * a clean slot instead of reading uninitialised garbage as a "record".
 * One 8-byte file at "/crux64.sav" — read/written whole via eepfs_read /
 * eepfs_write (no stdio needed). */

#include <libdragon.h>
#include <string.h>

#include "save.h"

#define SAVE_PATH "/crux64.sav"

static save_data_t rec;        /* live record */
static bool  present;          /* an EEPROM is mounted and writable */
static bool  dirty;            /* rec changed since the last flush */
static float time_accum;       /* seconds not yet rolled into a minute */

static const eepfs_entry_t eeprom_files[] = {
    { SAVE_PATH, sizeof(save_data_t) },
};

static void defaults(void) {
    memset(&rec, 0, sizeof rec);
    rec.initials[0] = rec.initials[1] = rec.initials[2] = 'A';
}

static void write_now(void) {
    if (!present) return;
    if (eepfs_write(SAVE_PATH, &rec, sizeof rec) == EEPFS_ESUCCESS)
        dirty = false;
}

bool save_init(void) {
    defaults();
    present    = false;
    dirty      = false;
    time_accum = 0.f;

    if (eeprom_present() == EEPROM_NONE) return false;
    if (eepfs_init(eeprom_files, 1) != EEPFS_ESUCCESS) return false;
    present = true;

    if (!eepfs_verify_signature()) {
        /* Fresh or foreign cart: lay down our signature + a clean slot. */
        eepfs_wipe();
        write_now();            /* persist the defaults */
        return true;
    }

    if (eepfs_read(SAVE_PATH, &rec, sizeof rec) != EEPFS_ESUCCESS)
        defaults();
    /* A wiped-but-never-written slot reads all zeros. */
    if (rec.initials[0] == '\0') defaults();
    return true;
}

const save_data_t *save_get(void) { return &rec; }

bool save_note_altitude(float meters) {
    if (meters < 0.f) meters = 0.f;
    uint16_t m = (meters > 65535.f) ? 65535u : (uint16_t)meters;
    if (m > rec.max_altitude) {
        rec.max_altitude = m;
        dirty = true;
        return true;
    }
    return false;
}

void save_note_fall(void) {
    if (rec.falls < 255) rec.falls++;
    dirty = true;
}

void save_add_time(float seconds) {
    if (seconds <= 0.f) return;
    time_accum += seconds;
    while (time_accum >= 60.f) {
        time_accum -= 60.f;
        if (rec.time_played < 65535) rec.time_played++;
        dirty = true;
    }
}

void save_commit(void) {
    if (dirty) write_now();
}
