#include <libdragon.h>
#include <string.h>

#include "save_container.h"

/**
 * Loads and validates a save container payload.
 *
 * @param base_block EEPROM block containing the save container header.
 * @param expect_version Save format version expected by the caller.
 * @param allow_legacy Whether legacy save formats may be accepted.
 * @param payload Buffer receiving the validated payload.
 * @param len Expected payload length.
 * @return The detected save layout, or `SAVE_LAYOUT_BLANK` when validation fails.
 */
save_layout_t save_container_load(int base_block, uint8_t expect_version,
                                  bool allow_legacy,
                                  uint8_t *payload, uint8_t len) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    uint8_t block[SAVE_BLOCK_SIZE];
    uint8_t hdr_len = 0;

    eeprom_read(base_block, hdr);

    save_layout_t layout = save_format_detect(hdr, expect_version, allow_legacy,
                                              NULL, &hdr_len);
    if (layout != SAVE_LAYOUT_CURRENT) return layout;
    if (hdr_len != len)                return SAVE_LAYOUT_BLANK;

    eeprom_read(base_block + 1, block);
    if (!save_payload_valid(hdr, block, len)) return SAVE_LAYOUT_BLANK;

    memcpy(payload, block, len);
    return SAVE_LAYOUT_CURRENT;
}

/**
 * Stores a versioned payload in EEPROM.
 *
 * @param base_block EEPROM block containing the record header.
 * @param version Save format version to store.
 * @param payload Payload data to store.
 * @param len Number of payload bytes to store.
 * @return `true` if both EEPROM writes succeed, `false` otherwise.
 */
bool save_container_store(int base_block, uint8_t version,
                          const uint8_t *payload, uint8_t len) {
    uint8_t block[SAVE_BLOCK_SIZE];
    uint8_t hdr[SAVE_BLOCK_SIZE];

    /* Pad the payload out to a whole block so the tail is defined and the
     * CRC stays reproducible if the record is ever shorter than a block. */
    memset(block, 0, sizeof block);
    memcpy(block, payload, len);
    save_header_build(hdr, version, block, len);

    /* Payload first: if power is lost between the two writes, the stale
     * header's CRC will not match the new payload, so the next boot resets
     * instead of loading a half-updated record. */
    if (eeprom_write(base_block + 1, block) != 0) return false;
    if (eeprom_write(base_block,     hdr)   != 0) return false;
    return true;
}
