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
    save_header_build(hdr, SAVE_VERSION, sample, (uint8_t)sizeof sample);
    CHECK(hdr[0] == 'C');
    CHECK(hdr[1] == 'R');
    CHECK(hdr[2] == 'X');
    CHECK(hdr[SAVE_HDR_VERSION]  == SAVE_VERSION);
    CHECK(hdr[SAVE_HDR_LEN]      == sizeof sample);
    CHECK(hdr[SAVE_HDR_RESERVED] == 0);
}

static void test_header_stores_crc_big_endian(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    save_header_build(hdr, SAVE_VERSION, sample, (uint8_t)sizeof sample);
    uint16_t want = save_crc16(sample, sizeof sample);
    uint16_t got  = (uint16_t)((hdr[SAVE_HDR_CRC_HI] << 8) | hdr[SAVE_HDR_CRC_LO]);
    CHECK(got == want);
}

int main(void) {
    test_crc_is_deterministic();
    test_crc_detects_every_single_byte_flip();
    test_crc_of_empty_is_the_seed();
    test_header_carries_magic_version_len();
    test_header_stores_crc_big_endian();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all save_format checks passed\n");
    return 0;
}
