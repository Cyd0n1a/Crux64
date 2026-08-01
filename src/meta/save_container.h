#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "save_format.h"

/* One two-block EEPROM container: header at `base_block`, payload at
 * base_block + 1. The progress record lives at base 0 and the settings
 * record at base 2, and both come through here so the torn-write ordering
 * is written once rather than duplicated.
 *
 * The caller must have checked eeprom_present() first; neither function
 * probes for a cart. */

/* Loads a container over `payload`, which must be `len` bytes.
 *
 * Writes `payload` ONLY when it returns SAVE_LAYOUT_CURRENT — a validated
 * header of the expected version, agreeing on the length, whose CRC matches
 * the payload block. Every other outcome leaves the buffer untouched, so
 * the idiom is: fill your defaults, then call this over the top and ignore
 * the result unless you care why. A CURRENT header that disagrees about the
 * length, or whose CRC fails, is reported BLANK: there is no usable record
 * either way. LEGACY is returned without reading the payload, because only
 * the caller knows how to interpret an old layout. */
save_layout_t save_container_load(int base_block, uint8_t expect_version,
                                  bool allow_legacy,
                                  uint8_t *payload, uint8_t len);

/* Writes payload then header. Costs two eeprom_writes, ~12 ms, and blocks
 * the CPU throughout — never call this per frame. `len` must be <= 8.
 * Returns false if a write reported failure, though note libdragon asserts
 * on a bad status (eeprom.c:80) before we ever see it. */
bool save_container_store(int base_block, uint8_t version,
                          const uint8_t *payload, uint8_t len);
