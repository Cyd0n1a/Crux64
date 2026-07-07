#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Phase 6 (GDD 3.4 / 5.6): EEPROM save state.
 *
 * The whole persistent record is a single 8-byte EEPROM block — packed
 * exactly to the GDD's layout so it lands in one block on a 4Kbit cart.
 * We keep the live copy in RAM, fold this session's progress into it for
 * free every frame, and only flush to EEPROM at natural rest points
 * (piton checkpoints, the end of a fall). An eepromfs write blocks the
 * CPU for milliseconds per block, so it must never run per frame. */
typedef struct __attribute__((packed)) {
    char     initials[3];   /* player initials (default "AAA") */
    uint16_t max_altitude;  /* highest hip altitude reached, meters */
    uint16_t time_played;   /* cumulative play time, minutes */
    uint8_t  falls;         /* lifetime falls */
} save_data_t;

_Static_assert(sizeof(save_data_t) == 8, "save_data_t must be one 8-byte EEPROM block");

/* Mounts eepromfs and loads the record into RAM. On a fresh or foreign
 * cart the signature won't match, so we wipe to a clean slot with our
 * defaults. Returns false when no EEPROM is present — the session still
 * runs, nothing just persists. Safe to read save_get() either way. */
bool save_init(void);

/* The live in-RAM record (always valid after save_init: real data, or
 * zeroed defaults when there's no cart). */
const save_data_t *save_get(void);

/* Fold session progress in. All RAM-only and cheap — call freely.
 * save_note_altitude returns true the frame it sets a new record. */
bool save_note_altitude(float meters);
void save_note_fall(void);
void save_add_time(float seconds);

/* Flush to EEPROM only if the record changed since the last write.
 * Blocks for a few ms — call at rest points, never every frame. */
void save_commit(void);
