# Relink-Stable EEPROM Save Format Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the eepromfs save container with one whose identity is a literal value, so recompiling the ROM can no longer wipe the player's record.

**Architecture:** A pure, host-testable unit (`save_format`) owns the CRC, the header layout and cart classification. A thin shell (`save.c`) does EEPROM I/O and keeps `save.h`'s public API byte-for-byte unchanged, so no call site moves. Existing eepromfs carts are rescued by matching only the six bytes of the old signature that do not vary between builds.

**Tech Stack:** C99, libdragon (`eeprom_read`/`eeprom_write` low-level API), gcc for host tests, Docker for the N64 build.

**Spec:** `docs/superpowers/specs/2026-08-01-relink-stable-save-design.md`

## Global Constraints

- `save_data_t` must stay byte-identical: `char initials[3]; uint16_t max_altitude; uint16_t time_played; uint8_t falls;`, packed, with the existing `_Static_assert(sizeof(save_data_t) == 8, ...)` intact.
- `save.h`'s public API must not change. `main.c` must not be modified by this plan. Its nine call sites are at `main.c:90,233,305,357,401,402,403,405`.
- `src/meta/save_format.c` and `.h` must not include `libdragon.h` or any N64 header — they compile on the host under `gcc -std=c99 -O1 -Wall -Wextra -Werror`.
- Magic is `'C','R','X'`; `SAVE_VERSION` is `1`; header is 8 bytes at block 0; payload starts at block 1.
- The legacy detector compares **only** bytes 0–5 of block 0 against `65 65 70 01 00 08`. Bytes 6–7 must never be compared.
- Settings persistence is out of scope — that is stage 2 of the pause shell.
- Cart is 4 Kbit (`N64_ROM_SAVETYPE = eeprom4k` in the Makefile), 64 blocks available.

## File Structure

| File | Responsibility |
|---|---|
| `src/meta/save_format.h` | Container constants, `save_layout_t`, four function declarations. No libdragon. |
| `src/meta/save_format.c` | CRC-16, header build, cart classification, payload validation. No libdragon. |
| `src/meta/save.c` | EEPROM I/O and the boot flow. Public API unchanged. |
| `src/meta/save.h` | Unchanged. |
| `tests/save_format_test.c` | Host tests for every `save_format` function. |
| `tests/run_tests.sh` | Gains a second compile-and-run line. |
| `Makefile` | Gains `save_format.o`. |
| `CLAUDE.md` | Save line corrected; the eepromfs trap recorded as a gotcha. |

---

### Task 1: CRC-16 and header construction

**Files:**
- Create: `src/meta/save_format.h`
- Create: `src/meta/save_format.c`
- Create: `tests/save_format_test.c`
- Modify: `tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: `uint16_t save_crc16(const uint8_t *data, size_t len)` and `void save_header_build(uint8_t out[8], uint8_t version, const uint8_t *payload, uint8_t len)`, plus the constants `SAVE_BLOCK_SIZE` (8), `SAVE_VERSION` (1), and the header offset macros `SAVE_HDR_VERSION` (3), `SAVE_HDR_CRC_HI` (4), `SAVE_HDR_CRC_LO` (5), `SAVE_HDR_LEN` (6), `SAVE_HDR_RESERVED` (7).

- [ ] **Step 1: Write the failing test**

Create `tests/save_format_test.c`:

```c
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
```

Add to `tests/run_tests.sh`, after the existing `./build/menu_nav_test` line:

```bash
gcc -std=c99 -O1 -Wall -Wextra -Werror \
    -o build/save_format_test save_format_test.c ../src/meta/save_format.c
./build/save_format_test
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `bash tests/run_tests.sh`
Expected: FAIL — `save_format_test.c:2:10: fatal error: ../src/meta/save_format.h: No such file or directory`. The existing `menu_nav` line must still print `all menu_nav checks passed` first.

- [ ] **Step 3: Write the header**

Create `src/meta/save_format.h`:

```c
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
#define SAVE_VERSION     1

/* Byte offsets within the block-0 header. Bytes 0-2 are the magic. */
#define SAVE_HDR_VERSION   3
#define SAVE_HDR_CRC_HI    4
#define SAVE_HDR_CRC_LO    5
#define SAVE_HDR_LEN       6
#define SAVE_HDR_RESERVED  7

typedef enum {
    SAVE_LAYOUT_CURRENT,  /* CRX header, version == SAVE_VERSION */
    SAVE_LAYOUT_OLDER,    /* CRX header, version <  SAVE_VERSION */
    SAVE_LAYOUT_NEWER,    /* CRX header, version >  SAVE_VERSION */
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

/* Classifies a cart from its first block. Reports the header's version and
 * length bytes through the out-params when they exist (zero otherwise);
 * both may be NULL. Deliberately does NOT judge whether the length is
 * acceptable — the expected size differs per version, so save.c owns that
 * decision. */
save_layout_t save_format_detect(const uint8_t block0[SAVE_BLOCK_SIZE],
                                 uint8_t *out_version, uint8_t *out_len);

/* True when `payload` matches the CRC recorded in `block0`. */
bool save_payload_valid(const uint8_t block0[SAVE_BLOCK_SIZE],
                        const uint8_t *payload, uint8_t len);
```

- [ ] **Step 4: Write the implementation**

Create `src/meta/save_format.c`:

```c
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
```

`save_format_detect` and `save_payload_valid` arrive in Task 2. Declaring them
in the header without defining them is fine here: nothing calls them yet, and
the test binary only references what Task 1 defines.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `bash tests/run_tests.sh`
Expected: PASS — `all menu_nav checks passed` then `all save_format checks passed`.

- [ ] **Step 6: Commit**

```bash
git add src/meta/save_format.h src/meta/save_format.c tests/save_format_test.c tests/run_tests.sh
git commit -m "save: CRC-16 and relink-stable header construction"
```

---

### Task 2: Cart classification

**Files:**
- Modify: `src/meta/save_format.c`
- Modify: `tests/save_format_test.c`

**Interfaces:**
- Consumes: `save_crc16`, `save_header_build`, `SAVE_BLOCK_SIZE`, `SAVE_VERSION`, the `SAVE_HDR_*` offsets and `save_layout_t` from Task 1.
- Produces: `save_layout_t save_format_detect(const uint8_t block0[8], uint8_t *out_version, uint8_t *out_len)` and `bool save_payload_valid(const uint8_t block0[8], const uint8_t *payload, uint8_t len)`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/save_format_test.c`, above `main`:

```c
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
    save_header_build(hdr, SAVE_VERSION, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, &ver, &len) == SAVE_LAYOUT_CURRENT);
    CHECK(ver == SAVE_VERSION);
    CHECK(len == sizeof sample);
}

static void test_detects_older(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    uint8_t ver = 99;
    save_header_build(hdr, 0, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, &ver, NULL) == SAVE_LAYOUT_OLDER);
    CHECK(ver == 0);
}

static void test_detects_newer(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    uint8_t ver = 0;
    save_header_build(hdr, 99, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, &ver, NULL) == SAVE_LAYOUT_NEWER);
    CHECK(ver == 99);
}

static void test_detects_legacy_regardless_of_trailing_crc(void) {
    for (size_t i = 0; i < 6; i++)
        CHECK(save_format_detect(legacy_carts[i], NULL, NULL) == SAVE_LAYOUT_LEGACY);
}

static void test_detects_blank(void) {
    const uint8_t zeros[SAVE_BLOCK_SIZE] = { 0 };
    const uint8_t ones[SAVE_BLOCK_SIZE]  = { 0xff, 0xff, 0xff, 0xff,
                                             0xff, 0xff, 0xff, 0xff };
    const uint8_t junk[SAVE_BLOCK_SIZE]  = { 'C', 'R', 'Z', 1, 0, 0, 8, 0 };
    CHECK(save_format_detect(zeros, NULL, NULL) == SAVE_LAYOUT_BLANK);
    CHECK(save_format_detect(ones,  NULL, NULL) == SAVE_LAYOUT_BLANK);
    CHECK(save_format_detect(junk,  NULL, NULL) == SAVE_LAYOUT_BLANK);
}

static void test_detect_tolerates_null_outparams(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    save_header_build(hdr, SAVE_VERSION, sample, (uint8_t)sizeof sample);
    CHECK(save_format_detect(hdr, NULL, NULL) == SAVE_LAYOUT_CURRENT);
}

static void test_payload_validates(void) {
    uint8_t hdr[SAVE_BLOCK_SIZE];
    uint8_t bad[8];
    save_header_build(hdr, SAVE_VERSION, sample, (uint8_t)sizeof sample);
    CHECK(save_payload_valid(hdr, sample, (uint8_t)sizeof sample) == true);
    memcpy(bad, sample, sizeof bad);
    bad[4] ^= 0x01;
    CHECK(save_payload_valid(hdr, bad, (uint8_t)sizeof bad) == false);
}
```

Add these seven calls to `main`, before the failure check:

```c
    test_detects_current();
    test_detects_older();
    test_detects_newer();
    test_detects_legacy_regardless_of_trailing_crc();
    test_detects_blank();
    test_detect_tolerates_null_outparams();
    test_payload_validates();
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `bash tests/run_tests.sh`
Expected: FAIL at link — `undefined reference to 'save_format_detect'` and `undefined reference to 'save_payload_valid'`.

- [ ] **Step 3: Write the implementation**

Append to `src/meta/save_format.c`:

```c
/* The pre-fix cart's eepromfs signature: magic "eep", one file, 8 bytes
 * total. Only these six bytes are ever compared. Bytes 6-7 hold the
 * pointer-derived checksum that differs from build to build, so ignoring
 * them is exactly what lets an old cart be rescued no matter which build
 * happened to write it. */
static const uint8_t legacy_sig[6] = { 0x65, 0x65, 0x70, 0x01, 0x00, 0x08 };

save_layout_t save_format_detect(const uint8_t block0[SAVE_BLOCK_SIZE],
                                 uint8_t *out_version, uint8_t *out_len) {
    if (out_version) *out_version = 0;
    if (out_len)     *out_len     = 0;

    if (block0[0] == 'C' && block0[1] == 'R' && block0[2] == 'X') {
        const uint8_t v = block0[SAVE_HDR_VERSION];
        if (out_version) *out_version = v;
        if (out_len)     *out_len     = block0[SAVE_HDR_LEN];
        if (v == SAVE_VERSION) return SAVE_LAYOUT_CURRENT;
        return (v < SAVE_VERSION) ? SAVE_LAYOUT_OLDER : SAVE_LAYOUT_NEWER;
    }

    if (memcmp(block0, legacy_sig, sizeof legacy_sig) == 0)
        return SAVE_LAYOUT_LEGACY;

    return SAVE_LAYOUT_BLANK;
}

bool save_payload_valid(const uint8_t block0[SAVE_BLOCK_SIZE],
                        const uint8_t *payload, uint8_t len) {
    const uint16_t want = (uint16_t)(((uint16_t)block0[SAVE_HDR_CRC_HI] << 8) |
                                      (uint16_t)block0[SAVE_HDR_CRC_LO]);
    return save_crc16(payload, len) == want;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `bash tests/run_tests.sh`
Expected: PASS — both `all menu_nav checks passed` and `all save_format checks passed`.

- [ ] **Step 5: Commit**

```bash
git add src/meta/save_format.c tests/save_format_test.c
git commit -m "save: classify current, older, newer, legacy and blank carts"
```

---

### Task 3: Move save.c onto raw EEPROM

**Files:**
- Modify: `src/meta/save.c` (whole-file rewrite of the eepromfs parts; `save_note_altitude`, `save_note_fall`, `save_add_time`, `save_commit` and `save_get` keep their current bodies)
- Modify: `Makefile:29-35` (add `save_format.o` to `OBJS`)

**Interfaces:**
- Consumes: `save_crc16`, `save_header_build`, `save_format_detect`, `save_payload_valid`, `save_layout_t`, `SAVE_BLOCK_SIZE`, `SAVE_VERSION` from Tasks 1–2.
- Produces: no new public symbols. `save.h` is untouched.

- [ ] **Step 1: Add the object to the Makefile**

In `Makefile`, immediately after the `$(BUILD_DIR)/src/meta/save.o \` line, add:

```make
    $(BUILD_DIR)/src/meta/save_format.o \
```

- [ ] **Step 2: Rewrite the container half of save.c**

Replace everything in `src/meta/save.c` from the file's opening comment down to
and including `save_init`, leaving `save_get`, `save_note_altitude`,
`save_note_fall`, `save_add_time` and `save_commit` exactly as they are:

```c
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
    save_header_build(hdr, SAVE_VERSION, payload, (uint8_t)sizeof rec);

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

    switch (save_format_detect(block0, &version, &len)) {
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
```

- [ ] **Step 3: Build the ROM**

Run: `bash build.sh`
Expected: exit 0, and `build.log` ends with `=== build.sh finished OK`. `src/meta/save.c` and `src/meta/save_format.c` both appear in the `[CC]` list.

- [ ] **Step 4: Confirm main.c really was untouched**

Run: `git status --short src/main.c`
Expected: empty output. If `main.c` shows as modified, the public API changed and the task is wrong.

- [ ] **Step 5: Re-run the host tests**

Run: `bash tests/run_tests.sh`
Expected: PASS, both suites. This catches a header edit that broke the pure unit.

- [ ] **Step 6: Boot smoke test**

Run the ROM under gopher64 for ~10 seconds and capture stdout:

```bash
timeout 15 gopher64 crux64.z64 > /tmp/boot.log 2>&1; grep -ci "assert\|crash\|exception" /tmp/boot.log
```

Expected: `0`. Then kill any survivor with
`ps -eo pid,comm | grep -iE "^ *[0-9]+ gopher64" | awk '{print $1}' | xargs -r kill -9`
(do not use `pgrep -f`, which matches this command itself).

- [ ] **Step 7: Confirm the new format reached the cart**

```bash
python3 -c "
import glob, os
f = max(glob.glob(os.path.expanduser('~/.local/share/gopher64/saves/CRUX64-*.eep')), key=os.path.getmtime)
d = open(f, 'rb').read()
print(os.path.basename(f))
print('block0:', d[0:8].hex(' '))
print('block1:', d[8:16].hex(' '))
assert d[0:3] == b'CRX', 'magic not written'
assert d[3] == 1, 'wrong version'
print('OK: CRX header present, version 1')
"
```

Expected: `OK: CRX header present, version 1`.

- [ ] **Step 8: Commit**

```bash
git add src/meta/save.c Makefile
git commit -m "save: replace eepromfs container with relink-stable header"
```

---

### Task 4: Prove the bug is dead, and record the trap

**Files:**
- Modify: `CLAUDE.md` (the "Save:" line under Project conventions, plus a new cache-maintenance-style gotcha)
- Temporarily modify then revert: `src/input/input.c`

**Interfaces:**
- Consumes: the finished save container from Task 3.
- Produces: no code. This task's deliverable is evidence plus documentation.

Host tests cannot prove this fix, because the failure is a linker-placement
effect that only exists in a linked ROM. Steps 1–6 are the real acceptance test.

- [ ] **Step 1: Establish a record on a cart**

This step needs a human at the controller. Build and run the ROM, start a run
past the title screen, then trigger a commit: `save_commit` only fires when
`piton_set || fell || landed || caught` (`main.c:404`), so planting a piton with
Z is the quickest reliable trigger. Altitude is banked every frame from spawn
(`main.c:402`), so the recorded value will be nonzero as soon as a commit lands —
the real carts captured during diagnosis hold 19 m and 11 m, which is roughly
spawn height.

Then capture the cart:

```bash
cp "$(ls -t ~/.local/share/gopher64/saves/CRUX64-*.eep | head -1)" /tmp/before.eep
python3 -c "
d=open('/tmp/before.eep','rb').read()
print('block0:', d[0:8].hex(' '))
print('block1:', d[8:16].hex(' '))
alt=(d[11]<<8)|d[12]
print('altitude recorded:', alt)
assert d[0:3]==b'CRX' and alt>0, 'need a nonzero record before continuing'
"
```

Expected: a `CRX` header and a nonzero altitude. Do not continue without both —
a zero record makes the rest of the test vacuous.

- [ ] **Step 2: Shift .rodata so the old container would have broken**

Add to `src/input/input.c`, immediately above `const char *limb_name(`:

```c
volatile const char eepfs_probe_pad[64] = "PROBEPADPROBEPADPROBEPADPROBEPAD";
```

and add this as the first line inside `limb_name`'s body:

```c
    if (eepfs_probe_pad[0] == 0) return (const char *)eepfs_probe_pad;
```

Both parts are required. `volatile` plus a real read is what stops
`--gc-sections` from deleting the pad before it can move anything — an
unreferenced probe gets stripped and the test then passes for the wrong reason.

- [ ] **Step 3: Rebuild and confirm the shift actually happened**

```bash
bash build.sh > /dev/null 2>&1
python3 -c "
d=open('build/crux64.elf','rb').read()
assert b'PROBEPADPROBEPAD' in d, 'pad was stripped — the test would be vacuous'
print('OK: pad survived linking, .rodata moved')
"
```

Expected: `OK: pad survived linking, .rodata moved`.

- [ ] **Step 4: Carry the cart across to the new ROM**

gopher64 names saves by ROM hash, so a rebuilt ROM otherwise starts from a blank
EEPROM and the test proves nothing. Copying the image onto the new hash is what
simulates one physical cart being reflashed:

```bash
timeout 15 gopher64 crux64.z64 > /dev/null 2>&1
NEW=$(ls -t ~/.local/share/gopher64/saves/CRUX64-*.eep | head -1)
ps -eo pid,comm | grep -iE "^ *[0-9]+ gopher64" | awk '{print $1}' | xargs -r kill -9
cp /tmp/before.eep "$NEW"
echo "carried onto $(basename "$NEW")"
```

- [ ] **Step 5: Run the new ROM and confirm the record survived**

```bash
timeout 20 gopher64 crux64.z64 > /tmp/after.log 2>&1
ps -eo pid,comm | grep -iE "^ *[0-9]+ gopher64" | awk '{print $1}' | xargs -r kill -9
python3 -c "
import glob, os
f = max(glob.glob(os.path.expanduser('~/.local/share/gopher64/saves/CRUX64-*.eep')), key=os.path.getmtime)
now = open(f,'rb').read()
was = open('/tmp/before.eep','rb').read()
print('before block1:', was[8:16].hex(' '))
print('after  block1:', now[8:16].hex(' '))
assert now[8:16] == was[8:16], 'RECORD LOST — the fix does not work'
print('PASS: record survived a relink that moved .rodata')
"
```

Expected: `PASS: record survived a relink that moved .rodata`. Under the old
eepromfs code this step is what would have failed.

- [ ] **Step 6: Revert the probe and rebuild**

```bash
git checkout src/input/input.c
bash build.sh > /dev/null 2>&1; echo "exit=$?"
git status --short src/
```

Expected: `exit=0` and empty `git status` output.

- [ ] **Step 7: Verify the legacy rescue against a real pre-fix cart**

The fixture is committed at `tests/fixtures/legacy-eepromfs.eep` — a real pre-fix
cart whose block 1 reads `41 41 41 00 13 00 01 00`, an altitude of 19 m.

Find the target cart by hashing the ROM rather than by `ls -t`: gopher64 names
saves `CRUX64-<uppercase sha256 of the ROM>.eep`, and an `ls -t` lookup will
happily hand you a *different* build's cart.

```bash
CART=~/.local/share/gopher64/saves/CRUX64-$(sha256sum crux64.z64 | awk '{print toupper($1)}').eep
cp tests/fixtures/legacy-eepromfs.eep "$CART"
timeout 20 gopher64 crux64.z64 > /dev/null 2>&1
ps -eo pid,comm | grep -iE "^ *[0-9]+ gopher64" | awk '{print $1}' | xargs -r kill -9
python3 -c "
d=open('$CART','rb').read()
print('block0:', d[0:8].hex(' '))
print('block1:', d[8:16].hex(' '))
assert d[0:3]==b'CRX', 'header was not restamped'
alt=(d[11]<<8)|d[12]
assert alt==19, 'legacy altitude not carried over, got %d' % alt
print('PASS: 19 m record rescued from an eepromfs cart and restamped as CRX')
"
```

Expected: `PASS: 19 m record rescued from an eepromfs cart and restamped as CRX`.

- [ ] **Step 8: Update CLAUDE.md**

Replace the existing line under Project conventions:

```
- Save: EEPROM 4k via eepromfs (GDD 3.4 `save_data_t`).
```

with:

```
- Save: EEPROM 4k via libdragon's low-level `eeprom_read`/`eeprom_write`
  (GDD 3.4 `save_data_t`, unchanged). NOT eepromfs — see below.
```

And add this to the gotchas, after the Cache maintenance section:

```markdown
## EEPROM

- Do NOT use eepromfs. `eepfs_init` identifies a filesystem by CRCing the raw
  `eepfs_entry_t` array, and that struct stores `path` as a *pointer*
  (`eepromfs.c:280`, `eepromfs.h:59`). The signature therefore depends on where
  the linker puts the path literal, so any change to a translation unit linked
  before `save.o` moves it, `eepfs_verify_signature()` fails, and `eepfs_wipe()`
  destroys the player's record with no error path. Confirmed twice: a `volatile`
  pad in `input.c` moved the pointer `0x800799f0` → `0x80079a10`, and four real
  carts carried four different signatures for one unchanged file table.
- This is invisible under gopher64, which keys saves by ROM hash and so hands
  every rebuild a blank EEPROM. It only bites on hardware, where one cart is
  reflashed repeatedly. Test save changes by copying the `.eep` onto the new
  ROM hash's filename under `~/.local/share/gopher64/saves/`.
- `eeprom_write` asserts on a nonzero status byte (`eeprom.c:80`) rather than
  returning it, so a write failure halts the ROM. Budget writes accordingly:
  each block costs ~6 ms and blocks the CPU.
```

- [ ] **Step 9: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: record the eepromfs pointer-signature trap"
```

---

## Self-Review

**Spec coverage.** Problem statement and evidence → Task 4 Step 8 (CLAUDE.md).
On-cart format → Task 1. Module split → Tasks 1–3. Boot flow, all six branches →
Task 3 Step 2. Legacy detection → Task 2 Step 3. Error handling: no EEPROM, CRC
mismatch, length disagreement, failed write, torn write → Task 3 Step 2. Write
cadence → unchanged, and Task 3 preserves `save_commit`'s dirty check. Host tests
1–9 → Tasks 1–2. Acceptance test → Task 4 Steps 1–6. Legacy rescue test → Task 4
Step 7. Stage-2 migration path → the `SAVE_LAYOUT_OLDER` branch in Task 3 Step 2.

**Type consistency.** `save_format_detect` and `save_payload_valid` carry the
three-argument and three-argument forms respectively in the header (Task 1), the
tests (Task 2 Step 1), the implementation (Task 2 Step 3) and the caller (Task 3
Step 2). `SAVE_BLOCK_SIZE`, `SAVE_VERSION` and the `SAVE_HDR_*` offsets are
defined once in Task 1 and used unchanged thereafter. `save_layout_t`'s five
enumerators are spelled identically in all three places.

**Note on Task 1 Step 4.** The header declares four functions but only two are
defined at that commit. This is intentional: the Task 1 test binary references
only the two, so it links. Task 2 completes the set. A reviewer stopping at
Task 1 will see a header promising more than the `.c` delivers, which is the
cost of splitting the unit across two reviewable commits.
