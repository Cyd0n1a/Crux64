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
