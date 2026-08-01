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

#define HDR_BLOCK      0
#define PAYLOAD_BLOCK  1

static save_data_t rec;        /* live record */
static bool  present;          /* an EEPROM is mounted and writable */
static bool  dirty;            /* rec changed since the last flush */
static float time_accum;       /* seconds not yet rolled into a minute */

static void defaults(void) {
    memset(&rec, 0, sizeof rec);
    rec.initials[0] = rec.initials[1] = rec.initials[2] = 'A';
}

/* Copies the record into a whole-block buffer. The payload is exactly one
 * block today; the memset keeps the tail defined if it ever is not, so the
 * CRC stays reproducible. */
static void pack_payload(uint8_t out[SAVE_BLOCK_SIZE]) {
    memset(out, 0, SAVE_BLOCK_SIZE);
    memcpy(out, &rec, sizeof rec);
}

static void write_now(void) {
    if (!present) return;

    uint8_t payload[SAVE_BLOCK_SIZE];
    uint8_t hdr[SAVE_BLOCK_SIZE];
    pack_payload(payload);
    save_header_build(hdr, SAVE_PROGRESS_VERSION, payload, (uint8_t)sizeof rec);

    /* Payload first: if power is lost between the two writes, the stale
     * header's CRC will not match the new payload, so the next boot resets
     * instead of loading a half-updated record.
     *
     * Note libdragon asserts on a bad status (eeprom.c:80) before we ever
     * see it, so in practice a failure halts rather than returning here.
     * The check costs nothing and is correct if that ever changes. */
    if (eeprom_write(PAYLOAD_BLOCK, payload) == 0 &&
        eeprom_write(HDR_BLOCK, hdr) == 0)
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

bool save_init(void) {
    defaults();
    present    = false;
    dirty      = false;
    time_accum = 0.f;

    if (eeprom_present() == EEPROM_NONE) return false;
    present = true;

    uint8_t block0[SAVE_BLOCK_SIZE];
    uint8_t payload[SAVE_BLOCK_SIZE];
    uint8_t version = 0, len = 0;

    eeprom_read(HDR_BLOCK, block0);

    switch (save_format_detect(block0, SAVE_PROGRESS_VERSION, true, &version, &len)) {
    case SAVE_LAYOUT_CURRENT:
        if (len != sizeof rec) {       /* header disagrees about the payload */
            write_now();
            break;
        }
        eeprom_read(PAYLOAD_BLOCK, payload);
        if (!save_payload_valid(block0, payload, len) || !adopt_payload(payload)) {
            defaults();
            write_now();
        }
        break;

    case SAVE_LAYOUT_OLDER:
        /* No version-0 CRX cart was ever released, so this is unreachable
         * today. It exists so stage 2 can add a migration by filling in a
         * branch rather than restructuring save_init, and is covered by a
         * host test that builds a synthetic version-0 header. */
        defaults();
        write_now();
        break;

    case SAVE_LAYOUT_LEGACY:
        /* Pre-fix cart written by eepromfs: its record sits at block 1,
         * because eepfs reserved exactly one block for the signature
         * (eepromfs.c:241). Adopt it and restamp in the new format. */
        eeprom_read(PAYLOAD_BLOCK, payload);
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
