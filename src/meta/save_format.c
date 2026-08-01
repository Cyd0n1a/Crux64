#include "save_format.h"

#include <string.h>

uint16_t save_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

void save_header_build(uint8_t out[SAVE_BLOCK_SIZE], uint8_t version,
                       const uint8_t *payload, uint8_t len) {
    const uint16_t crc = save_crc16(payload, len);
    out[0] = 'C';
    out[1] = 'R';
    out[2] = 'X';
    out[SAVE_HDR_VERSION]  = version;
    out[SAVE_HDR_CRC_HI]   = (uint8_t)(crc >> 8);
    out[SAVE_HDR_CRC_LO]   = (uint8_t)(crc & 0xFFu);
    out[SAVE_HDR_LEN]      = len;
    out[SAVE_HDR_RESERVED] = 0;
}

/* The pre-fix cart's eepromfs signature: magic "eep", one file, 8 bytes
 * total. Only these six bytes are ever compared. Bytes 6-7 hold the
 * pointer-derived checksum that differs from build to build, so ignoring
 * them is exactly what lets an old cart be rescued no matter which build
 * happened to write it. */
static const uint8_t legacy_sig[6] = { 0x65, 0x65, 0x70, 0x01, 0x00, 0x08 };

save_layout_t save_format_detect(const uint8_t block0[SAVE_BLOCK_SIZE],
                                 uint8_t expect_version, bool allow_legacy,
                                 uint8_t *out_version, uint8_t *out_len) {
    if (out_version) *out_version = 0;
    if (out_len)     *out_len     = 0;

    if (block0[0] == 'C' && block0[1] == 'R' && block0[2] == 'X') {
        const uint8_t v = block0[SAVE_HDR_VERSION];
        if (out_version) *out_version = v;
        if (out_len)     *out_len     = block0[SAVE_HDR_LEN];
        if (v == expect_version) return SAVE_LAYOUT_CURRENT;
        return (v < expect_version) ? SAVE_LAYOUT_OLDER : SAVE_LAYOUT_NEWER;
    }

    if (allow_legacy && memcmp(block0, legacy_sig, sizeof legacy_sig) == 0)
        return SAVE_LAYOUT_LEGACY;

    return SAVE_LAYOUT_BLANK;
}

bool save_payload_valid(const uint8_t block0[SAVE_BLOCK_SIZE],
                        const uint8_t *payload, uint8_t len) {
    const uint16_t want = (uint16_t)(((uint16_t)block0[SAVE_HDR_CRC_HI] << 8) |
                                      (uint16_t)block0[SAVE_HDR_CRC_LO]);
    return save_crc16(payload, len) == want;
}
