/* Phase 6 (GDD 3.4): EEPROM-backed save state.
 *
 * The cart is addressed through libdragon's low-level eeprom_read /
 * eeprom_write rather than eepromfs. eepromfs identifies a filesystem by
 * CRCing its raw eepfs_entry_t array, which holds `path` as a pointer, so
 * its signature moves whenever the linker relocates the path literal and
 * eepfs_verify_signature() then wipes the cart. Four real carts showed four
 * different signatures for the same file table. Here identity is a literal
 * magic value, which recompilation cannot disturb.
 *
 * Block 0 is the header (magic, version, payload CRC, payload length),
 * block 1 the payload. See src/meta/save_format.h. */

#include <libdragon.h>
#include <string.h>

#include "save.h"
#include "save_format.h"
#include "save_container.h"

#define PROGRESS_BASE   0
#define PROGRESS_PAYLOAD (PROGRESS_BASE + 1)

static save_data_t rec;        /* live record */
static bool  present;          /* an EEPROM is mounted and writable */
static bool  dirty;            /* rec changed since the last flush */
static float time_accum;       /**
 * Resets the live save record to its default values.
 */

static void defaults(void) {
    memset(&rec, 0, sizeof rec);
    rec.initials[0] = rec.initials[1] = rec.initials[2] = 'A';
}

/**
 * Persists the current progress record when EEPROM storage is available.
 */
static void write_now(void) {
    if (!present) return;
    if (save_container_store(PROGRESS_BASE, SAVE_PROGRESS_VERSION,
                             (const uint8_t *)&rec, (uint8_t)sizeof rec))
        dirty = false;
}

/* Adopts a payload block into `rec`, rejecting a slot that was cleared but
 * never written — the same guard the eepromfs version used. */
static bool adopt_payload(const uint8_t payload[SAVE_BLOCK_SIZE]) {
    save_data_t in;
    memcpy(&in, payload, sizeof in);
    if (in.initials[0] == '\0') return false;
    rec = in;
    return true;
}

/**
 * Initializes progress state from EEPROM and creates or updates the stored record when needed.
 *
 * @returns `true` if EEPROM is present, `false` otherwise.
 */
bool save_init(void) {
    defaults();
    present    = false;
    dirty      = false;
    time_accum = 0.f;

    if (eeprom_present() == EEPROM_NONE) return false;
    present = true;

    uint8_t payload[SAVE_BLOCK_SIZE];

    switch (save_container_load(PROGRESS_BASE, SAVE_PROGRESS_VERSION, true,
                                payload, (uint8_t)sizeof rec)) {
    case SAVE_LAYOUT_CURRENT:
        if (!adopt_payload(payload)) {   /* cleared slot, never written */
            defaults();
            write_now();
        }
        break;

    case SAVE_LAYOUT_OLDER:
        /* No version-0 CRX cart was ever released, so this is unreachable
         * today. It exists so a future progress-format change can migrate
         * by filling in a branch rather than restructuring save_init. */
        defaults();
        write_now();
        break;

    case SAVE_LAYOUT_LEGACY:
        /* Pre-fix cart written by eepromfs: its record sits at block 1,
         * because eepfs reserved exactly one block for the signature
         * (eepromfs.c:241). save_container_load returns LEGACY without
         * reading it, since only we know that layout. Adopt and restamp. */
        eeprom_read(PROGRESS_PAYLOAD, payload);
        if (!adopt_payload(payload)) defaults();
        dirty = true;
        write_now();
        break;

    case SAVE_LAYOUT_NEWER:
    case SAVE_LAYOUT_BLANK:
    default:
        defaults();
        write_now();
        break;
    }

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
