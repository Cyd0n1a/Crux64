#pragma once

/* Pure save-container logic — no libdragon, no N64 headers, so this file
 * is compiled both into the ROM and into tests/run_tests.sh on the host.
 *
 * Why this exists: libdragon's eepromfs derives its filesystem signature
 * from a CRC over the raw eepfs_entry_t array (eepromfs.c:280), and that
 * struct stores `path` as a pointer. The signature therefore changes every
 * time the linker moves the path literal, and eepfs_verify_signature()
 * responds by wiping the cart. Observed on four real carts: identical file
 * tables, four different signature checksums. This container identifies
 * itself with a literal magic value instead, which recompilation cannot
 * touch. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SAVE_BLOCK_SIZE  8

/* One version byte per container, because they version independently: the
 * progress record at blocks 0-1 and the settings record at blocks 2-3 are
 * separate lineages and a single compile-time constant cannot serve both. */
#define SAVE_PROGRESS_VERSION  1
#define SAVE_SETTINGS_VERSION  1

/* Byte offsets within the block-0 header. Bytes 0-2 are the magic. */
#define SAVE_HDR_VERSION   3
#define SAVE_HDR_CRC_HI    4
#define SAVE_HDR_CRC_LO    5
#define SAVE_HDR_LEN       6
#define SAVE_HDR_RESERVED  7

typedef enum {
    SAVE_LAYOUT_CURRENT,  /* CRX header, version == the expected one */
    SAVE_LAYOUT_OLDER,    /* CRX header, version <  the expected one */
    SAVE_LAYOUT_NEWER,    /* CRX header, version >  the expected one */
    SAVE_LAYOUT_LEGACY,   /* written by the old eepromfs container */
    SAVE_LAYOUT_BLANK,    /* unrecognised: fresh or foreign cart */
} save_layout_t;

/* CRC-16/CCITT-FALSE: poly 0x1021, seed 0xFFFF, MSB first, no reflection
 * and no final xor. Ours, not libdragon's — theirs is static in
 * eepromfs.c and we need one that links on the host too. */
uint16_t save_crc16(const uint8_t *data, size_t len);

/* Fills a block-0 header describing `payload`. `len` is a byte count, and
 * must be <= 255 since it is stored in one byte. */
void save_header_build(uint8_t out[SAVE_BLOCK_SIZE], uint8_t version,
                       const uint8_t *payload, uint8_t len);

/* Classifies a container from its header block. `expect_version` is the
 * version this caller understands; `allow_legacy` enables the pre-fix
 * eepromfs signature check, which is only meaningful at block 0 — the
 * settings container must pass false or it can adopt unrelated bytes as a
 * legacy record. Reports the header's version and length bytes through the
 * out-params when they exist (zero otherwise); both may be NULL.
 * Deliberately does NOT judge whether the length is acceptable — the
 * expected size differs per version, so the caller owns that decision. */
save_layout_t save_format_detect(const uint8_t block0[SAVE_BLOCK_SIZE],
                                 uint8_t expect_version, bool allow_legacy,
                                 uint8_t *out_version, uint8_t *out_len);

/* True when `payload` matches the CRC recorded in `block0`. */
bool save_payload_valid(const uint8_t block0[SAVE_BLOCK_SIZE],
                        const uint8_t *payload, uint8_t len);
