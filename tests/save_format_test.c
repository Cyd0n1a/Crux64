/* Host unit tests for the save container format. No libdragon and no N64
 * toolchain — run with tests/run_tests.sh. */
#include "../src/meta/save_format.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do {                                         \
    if (!(cond)) {                                               \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                              \
    }                                                            \
} while (0)

/* A stand-in for save_data_t. This file must not include save.h, which
 * pulls in nothing N64-specific today but is not guaranteed to stay that
 * way; the format layer only ever sees opaque payload bytes anyway. */
static const uint8_t sample[8] = { 'A', 'A', 'A', 0x00, 0x13, 0x00, 0x01, 0x00 };

static void test_crc_is_deterministic(void) {
    CHECK(save_crc16(sample, sizeof sample) == save_crc16(sample, sizeof sample));
}

static void test_crc_detects_every_single_byte_flip(void) {
    uint16_t base = save_crc16(sample, sizeof sample);
    for (size_t i = 0; i < sizeof sample; i++) {
        uint8_t buf[8];
        memcpy(buf, sample, sizeof buf);
        buf[i] ^= 0x01;
        CHECK(save_crc16(buf, sizeof buf) != base);
    }
}

static void test_crc_of_empty_is_the_seed(void) {
    CHECK(save_crc16(sample, 0) == 0xFFFF);
}

static void test_header_carries_magic_version_len(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    save_header_build(hdr, SAVE_PROGRESS_VERSION, sample, (uint8_t)sizeof sample);
    CHECK(hdr[0] == 'C');
    CHECK(hdr[1] == 'R');
    CHECK(hdr[2] == 'X');
    CHECK(hdr[SAVE_HDR_VERSION]  == SAVE_PROGRESS_VERSION);
    CHECK(hdr[SAVE_HDR_LEN]      == sizeof sample);
    CHECK(hdr[SAVE_HDR_RESERVED] == 0);
}

static void test_header_stores_crc_big_endian(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    save_header_build(hdr, SAVE_PROGRESS_VERSION, sample, (uint8_t)sizeof sample);
    uint16_t want = save_crc16(sample, sizeof sample);
    uint16_t got  = (uint16_t)((hdr[SAVE_HDR_CRC_HI] << 8) | hdr[SAVE_HDR_CRC_LO]);
    CHECK(got == want);
}

/* Block-0 images from four real pre-fix carts. Identical in bytes 0-5,
 * different in bytes 6-7 — those two are the pointer-derived checksum that
 * this whole change exists to stop depending on. */
static const uint8_t legacy_carts[6][SAVE_BLOCK_SIZE] = {
    { 0x65, 0x65, 0x70, 0x01, 0x00, 0x08, 0xed, 0xe3 },  /* real */
    { 0x65, 0x65, 0x70, 0x01, 0x00, 0x08, 0x7f, 0x7f },  /* real */
    { 0x65, 0x65, 0x70, 0x01, 0x00, 0x08, 0x55, 0x1b },  /* real */
    { 0x65, 0x65, 0x70, 0x01, 0x00, 0x08, 0x10, 0xbb },  /* real */
    { 0x65, 0x65, 0x70, 0x01, 0x00, 0x08, 0x00, 0x00 },  /* invented */
    { 0x65, 0x65, 0x70, 0x01, 0x00, 0x08, 0xff, 0xff },  /* invented */
};

static void test_detects_current(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    uint8_t ver = 99, len = 99;
    save_header_build(hdr, SAVE_PROGRESS_VERSION, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, SAVE_PROGRESS_VERSION, true, &ver, &len) == SAVE_LAYOUT_CURRENT);
    CHECK(ver == SAVE_PROGRESS_VERSION);
    CHECK(len == sizeof sample);
}

static void test_detects_older(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    uint8_t ver = 99;
    save_header_build(hdr, 0, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, SAVE_PROGRESS_VERSION, true, &ver, NULL) == SAVE_LAYOUT_OLDER);
    CHECK(ver == 0);
}

static void test_detects_newer(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    uint8_t ver = 0;
    save_header_build(hdr, 99, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, SAVE_PROGRESS_VERSION, true, &ver, NULL) == SAVE_LAYOUT_NEWER);
    CHECK(ver == 99);
}

static void test_detects_legacy_regardless_of_trailing_crc(void) {
    for (size_t i = 0; i < 6; i++)
        CHECK(save_format_detect(legacy_carts[i], SAVE_PROGRESS_VERSION, true,
                                 NULL, NULL) == SAVE_LAYOUT_LEGACY);
}

/* The expected version is the caller's, not a compile-time constant: the
 * progress and settings containers version independently. */
static void test_expect_version_drives_classification(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    save_header_build(hdr, 3, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, 3, true, NULL, NULL) == SAVE_LAYOUT_CURRENT);
    CHECK(save_format_detect(hdr, 4, true, NULL, NULL) == SAVE_LAYOUT_OLDER);
    CHECK(save_format_detect(hdr, 2, true, NULL, NULL) == SAVE_LAYOUT_NEWER);
}

/* The settings container sits at block 2, where a pre-fix cart's leftover
 * bytes have no business being read as an eepromfs signature. */
static void test_legacy_is_suppressed_when_disallowed(void) {
    for (size_t i = 0; i < sizeof legacy_carts / sizeof legacy_carts[0]; i++) {
        CHECK(save_format_detect(legacy_carts[i], SAVE_SETTINGS_VERSION,
                                 false, NULL, NULL) == SAVE_LAYOUT_BLANK);
        CHECK(save_format_detect(legacy_carts[i], SAVE_PROGRESS_VERSION,
                                 true,  NULL, NULL) == SAVE_LAYOUT_LEGACY);
    }
}

static void test_detects_blank(void) {
    const uint8_t zeros[SAVE_BLOCK_SIZE] = { 0 };
    const uint8_t ones[SAVE_BLOCK_SIZE]  = { 0xff, 0xff, 0xff, 0xff,
                                             0xff, 0xff, 0xff, 0xff };
    const uint8_t junk[SAVE_BLOCK_SIZE]  = { 'C', 'R', 'Z', 1, 0, 0, 8, 0 };
    CHECK(save_format_detect(zeros, SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_BLANK);
    CHECK(save_format_detect(ones,  SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_BLANK);
    CHECK(save_format_detect(junk,  SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_BLANK);
}

static void test_detect_tolerates_null_outparams(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    save_header_build(hdr, SAVE_PROGRESS_VERSION, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_CURRENT);
}

static void test_payload_validates(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    uint8_t bad[8];
    save_header_build(hdr, SAVE_PROGRESS_VERSION, sample, (uint8_t)sizeof sample);
    CHECK(save_payload_valid(hdr, sample, (uint8_t)sizeof sample) == true);
    memcpy(bad, sample, sizeof bad);
    bad[4] ^= 0x01;
    CHECK(save_payload_valid(hdr, bad, (uint8_t)sizeof bad) == false);
}

int main(void) {
    test_crc_is_deterministic();
    test_crc_detects_every_single_byte_flip();
    test_crc_of_empty_is_the_seed();
    test_header_carries_magic_version_len();
    test_header_stores_crc_big_endian();
    test_detects_current();
    test_detects_older();
    test_detects_newer();
    test_detects_legacy_regardless_of_trailing_crc();
    test_expect_version_drives_classification();
    test_legacy_is_suppressed_when_disallowed();
    test_detects_blank();
    test_detect_tolerates_null_outparams();
    test_payload_validates();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all save_format checks passed\n");
    return 0;
}
