# Settings Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A settings screen reachable from the pause menu and the title, whose seven values apply live and persist in their own EEPROM container.

**Architecture:** Settings live in a second two-block container at EEPROM blocks 2–3, structurally identical to the progress container at 0–1 but independent of it, so progress commits keep their present two-block cost and a pre-settings cart migrates for free. Risky logic goes in libdragon-free units host-tested by `tests/run_tests.sh`; the libdragon shells hold only storage and side effects. Menu rows stay a data table — value rendering is data, not callbacks.

**Tech Stack:** C99, libdragon (preview `07f1977bb`), tiny3d `7f5773f64`, Docker build via `bash build.sh`, host unit tests via `gcc` in `tests/run_tests.sh`.

Design spec: `docs/superpowers/specs/2026-08-01-settings-screen-design.md`

## Global Constraints

- Branch is `feat/settings`, cut from `fix/eeprom-stable-signature`. Do not rebase onto `master` until that branch merges — this plan depends on `src/meta/save_format.{h,c}`, which exists only there.
- Host-testable units must contain **no libdragon and no N64 headers**: `save_format.c`, `menu_nav.c`, `settings_data.c`. `menu.h` and `input.h` are libdragon-free and may be included by them.
- Host tests compile with `gcc -std=c99 -O1 -Wall -Wextra -Werror`. Warnings are errors; there is no opt-out.
- ROM builds only inside Docker: `bash build.sh`, output `crux64.z64`, full log in `build.log`. LSP errors about missing `libdragon.h` / `t3d.h` on the host are expected and are not build failures.
- `OBJS` in `Makefile` must list every new `.o`; object paths mirror source paths (`src/meta/foo.c` → `build/src/meta/foo.o`).
- `^` and `$` are `rdpq_text` escape characters. Bare ones assert at draw time. `#`, `[`, `]` and `>` are safe.
- Never call `data_cache_hit_*` on a pointer from `malloc_uncached()` or `surface_alloc()`. No task here should need a cache op at all.
- `eeprom_write` costs ~6 ms and blocks the CPU; `eeprom_read` ~750 µs. A frame is 16.7 ms. No EEPROM write may occur per-frame.
- Settings defaults, verbatim from the spec: `music_vol` 7, `sfx_vol` 7, `cam_sens` 2 (`1.00x`), `invert_x` off, `invert_y` off, `rumble` **on**, `stamina_bar` **on**.
- `stamina_bar` defaulting on is a **knowing departure** from GDD 4 ("No Stamina Bar By Default"), recorded under "Deliberate departures" in the spec. Do not "fix" it to off — that is a separate decision about shipping presentation.
- `SENS_TABLE` is `{ 0.50f, 0.75f, 1.00f, 1.50f, 2.00f }`.
- Commit messages end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

## File Structure

| File | Responsibility | libdragon |
|---|---|---|
| `src/meta/save_format.{h,c}` | container format: CRC, header build, classification. **Modified** — version and legacy check become parameters | no |
| `src/meta/save_container.{h,c}` | **new** — read/write one two-block container at a base block | yes |
| `src/meta/save.{h,c}` | progress record. **Modified** — slims onto `save_container` | yes |
| `src/meta/settings_data.{h,c}` | **new** — `save_settings_t`, defaults, clamping, row table. Stateless | no |
| `src/meta/settings.{h,c}` | **new** — live record, EEPROM I/O, side effects, screen assembly | yes |
| `src/meta/menu_nav.{h,c}` | cursor and repeat timing. **Modified** — gains a horizontal axis | no |
| `src/meta/menu.{h,c}` | screen shell and drawing. **Modified** — kinds, screens, geometry, one-deep stack | yes |
| `src/audio/synth.{h,c}` | **Modified** — two output gain statics | yes |
| `src/input/rumble.{h,c}` | **Modified** — an enable gate | yes |
| `src/render/render.c` | **Modified** — draw the menu over the title too | yes |
| `src/main.c` | **Modified** — title menu, camera application, stamina gate, duck | yes |
| `tests/settings_test.c` | **new** — settings logic | host |
| `tests/menu_nav_test.c` | **Modified** — horizontal axis cases | host |
| `tests/save_format_test.c` | **Modified** — new signature, version and legacy parameterization | host |

---

### Task 1: Parameterize the container classifier

Two containers with independent version lineages cannot share one compile-time `SAVE_VERSION`, and the eepromfs legacy signature is meaningful only at block 0. Both become parameters. This task changes a pure unit plus its one caller, so it ends with both host tests and the ROM build green.

**Files:**
- Modify: `src/meta/save_format.h`, `src/meta/save_format.c:36-53`
- Modify: `src/meta/save.c:47`, `src/meta/save.c:86`
- Test: `tests/save_format_test.c`

**Interfaces:**
- Produces: `SAVE_PROGRESS_VERSION` (1) and `SAVE_SETTINGS_VERSION` (1) replacing `SAVE_VERSION`; `save_layout_t save_format_detect(const uint8_t block0[SAVE_BLOCK_SIZE], uint8_t expect_version, bool allow_legacy, uint8_t *out_version, uint8_t *out_len)`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/save_format_test.c`, above `main`:

```c
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
```

Register both in `main`, after `test_detects_legacy_regardless_of_trailing_crc();`:

```c
    test_expect_version_drives_classification();
    test_legacy_is_suppressed_when_disallowed();
```

Then update the seven existing `save_format_detect` call sites in that file to the new signature. Each gains `SAVE_PROGRESS_VERSION, true,` as arguments 2 and 3, and every remaining bare `SAVE_VERSION` becomes `SAVE_PROGRESS_VERSION`:

```c
    CHECK(save_format_detect(hdr, SAVE_PROGRESS_VERSION, true, &ver, &len) == SAVE_LAYOUT_CURRENT);
    CHECK(save_format_detect(hdr, SAVE_PROGRESS_VERSION, true, &ver, NULL) == SAVE_LAYOUT_OLDER);
    CHECK(save_format_detect(hdr, SAVE_PROGRESS_VERSION, true, &ver, NULL) == SAVE_LAYOUT_NEWER);
    CHECK(save_format_detect(legacy_carts[i], SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_LEGACY);
    CHECK(save_format_detect(zeros, SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_BLANK);
    CHECK(save_format_detect(ones,  SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_BLANK);
    CHECK(save_format_detect(junk,  SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_BLANK);
    CHECK(save_format_detect(hdr, SAVE_PROGRESS_VERSION, true, NULL, NULL) == SAVE_LAYOUT_CURRENT);
```

In `test_detects_older` the header is built with version 0 and in `test_detects_newer` with version 2; leave those build values alone, only the detect calls change.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `bash tests/run_tests.sh`
Expected: FAIL — compile errors, `too many arguments to function 'save_format_detect'` and `'SAVE_PROGRESS_VERSION' undeclared`.

- [ ] **Step 3: Update the header**

In `src/meta/save_format.h`, replace `#define SAVE_VERSION 1` with:

```c
/* One version byte per container, because they version independently: the
 * progress record at blocks 0-1 and the settings record at blocks 2-3 are
 * separate lineages and a single compile-time constant cannot serve both. */
#define SAVE_PROGRESS_VERSION  1
#define SAVE_SETTINGS_VERSION  1
```

Update the enum comments, which currently name `SAVE_VERSION`:

```c
typedef enum {
    SAVE_LAYOUT_CURRENT,  /* CRX header, version == the expected one */
    SAVE_LAYOUT_OLDER,    /* CRX header, version <  the expected one */
    SAVE_LAYOUT_NEWER,    /* CRX header, version >  the expected one */
    SAVE_LAYOUT_LEGACY,   /* written by the old eepromfs container */
    SAVE_LAYOUT_BLANK,    /* unrecognised: fresh or foreign cart */
} save_layout_t;
```

And replace the `save_format_detect` declaration and its comment:

```c
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
```

- [ ] **Step 4: Update the implementation**

Replace `save_format_detect` in `src/meta/save_format.c`:

```c
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
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `bash tests/run_tests.sh`
Expected: PASS — `all save_format checks passed`, 14 test functions.

- [ ] **Step 6: Update save.c's call sites**

`src/meta/save.c:47`, inside `write_now`:

```c
    save_header_build(hdr, SAVE_PROGRESS_VERSION, payload, (uint8_t)sizeof rec);
```

`src/meta/save.c:86`, the switch head:

```c
    switch (save_format_detect(block0, SAVE_PROGRESS_VERSION, true, &version, &len)) {
```

- [ ] **Step 7: Build the ROM**

Run: `bash build.sh`
Expected: `crux64.z64` produced, no errors in `build.log`.

- [ ] **Step 8: Commit**

```bash
git add src/meta/save_format.h src/meta/save_format.c src/meta/save.c tests/save_format_test.c
git commit -m "save: take the expected version and legacy check as parameters

A second container is coming at blocks 2-3 with its own version lineage,
and one compile-time SAVE_VERSION cannot serve both. The eepromfs legacy
signature is meaningful only at block 0; run against the settings
container it could adopt unrelated bytes as a record.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Extract the container I/O

The torn-write ordering — payload block before header block, so an interrupted write leaves a detectable CRC mismatch rather than a half-updated record — is about to be needed twice. Write it once. This task also corrects two doc comments left stale by the eepromfs removal.

**Files:**
- Create: `src/meta/save_container.h`, `src/meta/save_container.c`
- Modify: `src/meta/save.c` (rewrite `write_now` and `save_init`, delete `pack_payload`), `src/meta/save.h:8-14`, `src/main.c:11-16`, `Makefile:30`

**Interfaces:**
- Consumes: `save_format_detect(block0, expect_version, allow_legacy, out_version, out_len)`, `save_header_build`, `save_payload_valid`, `SAVE_PROGRESS_VERSION` (Task 1).
- Produces: `save_layout_t save_container_load(int base_block, uint8_t expect_version, bool allow_legacy, uint8_t *payload, uint8_t len)` and `bool save_container_store(int base_block, uint8_t version, const uint8_t *payload, uint8_t len)`.

- [ ] **Step 1: Write the header**

Create `src/meta/save_container.h`:

```c
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
```

- [ ] **Step 2: Write the implementation**

Create `src/meta/save_container.c`:

```c
#include <libdragon.h>
#include <string.h>

#include "save_container.h"

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
```

- [ ] **Step 3: Move save.c onto it**

In `src/meta/save.c`, replace the includes, block defines, `pack_payload` and `write_now` (lines 14–59) with:

```c
#include <libdragon.h>
#include <string.h>

#include "save.h"
#include "save_format.h"
#include "save_container.h"

#define PROGRESS_BASE   0
#define PROGRESS_PAYLOAD (PROGRESS_BASE + 1)

static save_data_t rec;        /* live record */
static bool  present;          /* an EEPROM is mounted and writable */
static bool  dirty;            /* rec changed since the last flush */
static float time_accum;       /* seconds not yet rolled into a minute */

static void defaults(void) {
    memset(&rec, 0, sizeof rec);
    rec.initials[0] = rec.initials[1] = rec.initials[2] = 'A';
}

static void write_now(void) {
    if (!present) return;
    if (save_container_store(PROGRESS_BASE, SAVE_PROGRESS_VERSION,
                             (const uint8_t *)&rec, (uint8_t)sizeof rec))
        dirty = false;
}
```

Then replace `save_init` (lines 71–127) with:

```c
bool save_init(void) {
    defaults();
    present    = false;
    dirty      = false;
    time_accum = 0.f;

    if (eeprom_present() == EEPROM_NONE) return false;
    present = true;

    uint8_t payload[SAVE_BLOCK_SIZE];

    switch (save_container_load(PROGRESS_BASE, SAVE_PROGRESS_VERSION, true,
                                payload, (uint8_t)sizeof rec)) {
    case SAVE_LAYOUT_CURRENT:
        if (!adopt_payload(payload)) {   /* cleared slot, never written */
            defaults();
            write_now();
        }
        break;

    case SAVE_LAYOUT_LEGACY:
        /* Pre-fix cart written by eepromfs: its record sits at block 1,
         * because eepfs reserved exactly one block for the signature
         * (eepromfs.c:241). save_container_load returns LEGACY without
         * reading it, since only we know that layout. Adopt and restamp. */
        eeprom_read(PROGRESS_PAYLOAD, payload);
        if (!adopt_payload(payload)) defaults();
        dirty = true;
        write_now();
        break;

    case SAVE_LAYOUT_OLDER:
        /* No version-0 CRX cart was ever released, so this is unreachable
         * today. It exists so a future progress-format change can migrate
         * by filling in a branch rather than restructuring save_init. */
        defaults();
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

`adopt_payload` is unchanged and must stay declared above `save_init`.

- [ ] **Step 4: Correct the two stale eepromfs comments**

`src/meta/save.h`, replace the paragraph at lines 8–14 that begins "We keep the live copy in RAM":

```c
 * We keep the live copy in RAM, fold this session's progress into it for
 * free every frame, and only flush to EEPROM at natural rest points
 * (piton checkpoints, the end of a fall). Each block write blocks the CPU
 * for ~6ms, so it must never run per frame. */
```

and the `save_init` comment at lines 25–28:

```c
/* Loads the record into RAM through the block-0/1 container. On a fresh or
 * foreign cart nothing validates, so we fall back to defaults and stamp
 * them down. Returns false when no EEPROM is present — the session still
 * runs, nothing just persists. Safe to read save_get() either way. */
```

`src/main.c`, lines 11–16, replace "in a single 8-byte eepromfs block" with the container:

```c
 * Phase 6 (GDD 5.6): EEPROM save state. The run's max altitude, fall
 * count and play time persist across power-offs in a two-block container
 * at EEPROM blocks 0-1 (src/meta/save.c) — recorded in RAM each frame and
 * flushed only at rest points (piton checkpoints, the end of a fall).
 * The title screen shows the saved best.
```

- [ ] **Step 5: Add the object to the Makefile**

In `Makefile`, after the `save_format.o` line:

```make
    $(BUILD_DIR)/src/meta/save_container.o \
```

- [ ] **Step 6: Build and verify host tests still pass**

Run: `bash build.sh && bash tests/run_tests.sh`
Expected: `crux64.z64` produced; both host suites pass. `save_format.c` did not change in this task, so its tests are unaffected — they are run to confirm nothing regressed.

- [ ] **Step 7: Commit**

```bash
git add src/meta/save_container.h src/meta/save_container.c src/meta/save.c src/meta/save.h src/main.c Makefile
git commit -m "save: extract the two-block container into save_container

The settings record needs the same header/payload pair at blocks 2-3, and
the torn-write ordering is not something to write twice. save.c keeps its
boot flow and legacy rescue; only the I/O moves.

Also corrects two comments that still described eepromfs.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: A horizontal axis for menu navigation

Sliders and toggles are adjusted with left/right, which needs its own repeat timing. It goes beside the vertical logic for the same reason that one is there: the timing is the off-by-one-prone part, and it is testable on the host.

**Files:**
- Modify: `src/meta/menu_nav.h`, `src/meta/menu_nav.c`
- Test: `tests/menu_nav_test.c`

**Interfaces:**
- Produces: `int menu_nav_step_h(menu_nav_t *n, int dir, float dt)` returning `-1`, `0` or `+1` — the amount to move the highlighted row's value this frame. `menu_nav_t` gains `h_repeat_t` and `h_last_dir`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/menu_nav_test.c`, above `main` (match the file's existing `CHECK` idiom):

```c
static void test_h_first_press_moves_immediately(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == +1);
    CHECK(menu_nav_step_h(&n, -1, 0.016f) == -1);
}

static void test_h_held_waits_the_delay_then_repeats(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == +1);   /* the initial step */

    /* Nothing more until MENU_NAV_DELAY has elapsed. */
    float t = 0.f;
    while (t < MENU_NAV_DELAY - 0.02f) {
        CHECK(menu_nav_step_h(&n, +1, 0.016f) == 0);
        t += 0.016f;
    }

    /* Then it fires, and again every MENU_NAV_REPEAT. */
    int fired = 0;
    for (int i = 0; i < 40; i++)
        if (menu_nav_step_h(&n, +1, 0.016f)) fired++;
    CHECK(fired >= 3);
}

static void test_h_release_rearms_the_immediate_step(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == +1);
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == 0);
    CHECK(menu_nav_step_h(&n,  0, 0.016f) == 0);    /* released */
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == +1);   /* immediate again */
}

/* The axes must not share timer state: adjusting a value while stepping
 * rows would otherwise swallow one of the two. */
static void test_axes_are_independent(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step(&n, +1, 0.016f) == true);
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == +1);
    CHECK(n.cursor == 1);

    /* Holding vertical must not re-arm horizontal. */
    CHECK(menu_nav_step(&n, +1, 0.016f) == false);
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == 0);
}

static void test_reset_clears_the_horizontal_axis(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == +1);
    menu_nav_reset(&n, 4);
    CHECK(n.h_last_dir == 0);
    CHECK(menu_nav_step_h(&n, +1, 0.016f) == +1);   /* immediate after reset */
}
```

Register all five in `main`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `bash tests/run_tests.sh`
Expected: FAIL — `implicit declaration of function 'menu_nav_step_h'` and `'menu_nav_t' has no member named 'h_last_dir'`.

- [ ] **Step 3: Extend the header**

In `src/meta/menu_nav.h`, add the two fields to `menu_nav_t` after `last_dir`:

```c
    float h_repeat_t; /* same, for the left/right value axis */
    int   h_last_dir; /* -1 left, +1 right, 0 released */
```

and declare the stepper after `menu_nav_step`:

```c
/* dir: -1 left, +1 right, 0 none. Returns the amount to move the
 * highlighted row's value this frame: -1, 0 or +1. Same cadence as the
 * vertical axis — immediate on a fresh direction, then MENU_NAV_DELAY and
 * MENU_NAV_REPEAT — but with its own timer, so adjusting a value and
 * stepping rows in the same frame cannot swallow either. Values clamp
 * rather than wrap; that is the caller's business, not this function's. */
int menu_nav_step_h(menu_nav_t *n, int dir, float dt);
```

- [ ] **Step 4: Implement it**

In `src/meta/menu_nav.c`, add to `menu_nav_reset`, after `n->last_dir = 0;`:

```c
    n->h_repeat_t = 0.f;
    n->h_last_dir = 0;
```

and append:

```c
int menu_nav_step_h(menu_nav_t *n, int dir, float dt) {
    if (dir == 0) {               /* released: re-arm the immediate move */
        n->h_last_dir = 0;
        n->h_repeat_t = 0.f;
        return 0;
    }

    bool move;
    if (dir != n->h_last_dir) {
        n->h_last_dir = dir;
        n->h_repeat_t = MENU_NAV_DELAY;
        move = true;
    } else {
        n->h_repeat_t -= dt;
        move = (n->h_repeat_t <= 0.f);
        if (move) n->h_repeat_t += MENU_NAV_REPEAT;
    }

    return move ? dir : 0;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `bash tests/run_tests.sh`
Expected: PASS — all three suites.

- [ ] **Step 6: Commit**

```bash
git add src/meta/menu_nav.h src/meta/menu_nav.c tests/menu_nav_test.c
git commit -m "menu_nav: a horizontal axis with its own repeat timer

Sliders and toggles adjust with left/right. Separate timers so adjusting a
value and stepping rows in one frame cannot swallow either.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Generalize the menu shell

The menu grows value rows, a second and third screen, a one-deep stack and computed panel geometry. It still never includes `settings.h`: screens carry their own accessors. At the end of this task the pause menu has a SETTINGS row that does nothing, because no screen is registered yet — that is expected, and Task 6 fills it.

**Files:**
- Modify: `src/meta/menu.h`, `src/meta/menu.c`

**Interfaces:**
- Consumes: `menu_nav_step_h` (Task 3).
- Produces: `menu_kind_t { MENU_ACTION, MENU_TOGGLE, MENU_SLIDER }`; `menu_result_t` gaining `MENU_BEGIN_CLIMB`, `MENU_SETTINGS`, `MENU_BACK`; `menu_item_t` gaining `int vmax` and `const char *const *value_labels`; `menu_screen_t` gaining `bool scrim`, `bool start_confirms`, `int (*get)(int)`, `void (*set)(int,int)`, `void (*on_close)(void)`; `void menu_open_screen(const menu_screen_t *)`, `void menu_register_settings(const menu_screen_t *)`, `const menu_screen_t *menu_title_screen(void)`, `bool menu_settings_open(void)`.

- [ ] **Step 1: Rewrite the header's types**

In `src/meta/menu.h`, replace the two enums and two structs (the block from `typedef enum { MENU_ACTION }` through the `menu_screen_t` definition) with:

```c
typedef enum {
    MENU_ACTION,        /* fires an intent when confirmed */
    MENU_TOGGLE,        /* two-state value, adjusted left/right or by A */
    MENU_SLIDER,        /* 0..vmax value, adjusted left/right */
} menu_kind_t;

typedef enum {
    MENU_NONE = 0,      /* menu still open, nothing decided this frame */
    MENU_RESUME,        /* closed: hand control back to the climber */
    MENU_QUIT_TITLE,    /* closed: reset the run and return to the title */
    MENU_BEGIN_CLIMB,   /* closed: leave the title and start the prologue */

    /* Handled entirely inside menu.c — they push and pop the one-deep
     * screen stack and return MENU_NONE, so main.c never learns that a
     * screen changed. Never returned from menu_update. */
    MENU_SETTINGS,
    MENU_BACK,
} menu_result_t;

typedef struct {
    const char *label;
    menu_kind_t kind;
    int         id;     /* ACTION: the menu_result_t; else a setting id */
    int         vmax;   /* TOGGLE: 1; SLIDER: top of range; ACTION: 0 */

    /* vmax + 1 strings naming each value, or NULL to draw a bar. */
    const char *const *value_labels;
} menu_item_t;

typedef struct {
    const char        *title;   /* NULL draws no title bar */
    const menu_item_t *items;
    int                count;

    bool scrim;           /* dim the world behind the panel */
    bool start_confirms;  /* Start confirms rather than closing (the title,
                             which has nothing to back out to) */

    /* Supplied by whoever owns the values. NULL on screens of pure
     * actions. This indirection is what keeps settings.h out of menu.c. */
    int  (*get)(int id);
    void (*set)(int id, int delta);
    void (*on_close)(void);   /* fires on the pop, before the screen moves */
} menu_screen_t;
```

Replace the function declarations at the bottom of the file with:

```c
struct rdpq_font_s;
void menu_init(struct rdpq_font_s *font);

void menu_open(void);                            /* open the pause screen */
void menu_open_screen(const menu_screen_t *s);   /* open a specific screen */

/* settings.c hands its screen in at boot, so menu.c can push it from a
 * SETTINGS row without including settings.h. Until this is called, a
 * SETTINGS row does nothing. */
void menu_register_settings(const menu_screen_t *s);

const menu_screen_t *menu_title_screen(void);

bool          menu_active(void);
bool          menu_settings_open(void);   /* for the caller's music duck */
menu_result_t menu_update(const input_state_t *in, float dt);
void          menu_draw(void);   /* call inside rdpq_attach, after the 3D pass */
```

Also update the file's top comment, which still says stage 1 has one screen:

```c
/* Pause and title menus.
 *
 * Deliberately knows nothing about gameplay or settings: rows return an
 * INTENT and main.c carries it out, and value rows reach their data
 * through the screen's get/set callbacks. Do not include climber.h,
 * save.h or settings.h here — the menu must stay testable and reusable.
 *
 * Screen depth is exactly one (pause -> settings, title -> settings), so
 * there is a single return_to pointer rather than a stack. */
```

- [ ] **Step 2: Rewrite the screen tables and state**

In `src/meta/menu.c`, replace the `pause_items` / `pause_screen` block and the state declarations (lines 24–36) with:

```c
static const menu_item_t pause_items[] = {
    { "RESUME",        MENU_ACTION, MENU_RESUME,     0, NULL },
    { "SETTINGS",      MENU_ACTION, MENU_SETTINGS,   0, NULL },
    { "QUIT TO TITLE", MENU_ACTION, MENU_QUIT_TITLE, 0, NULL },
};

static const menu_screen_t pause_screen = {
    .title = "PAUSED",
    .items = pause_items,
    .count = (int)(sizeof pause_items / sizeof pause_items[0]),
    .scrim = true,
};

/* The title menu draws over the orbiting vista, so no scrim and no title
 * bar — the logo is already there. Start confirms instead of closing,
 * which keeps Start-on-boot starting the climb as it always has. */
static const menu_item_t title_items[] = {
    { "BEGIN CLIMB", MENU_ACTION, MENU_BEGIN_CLIMB, 0, NULL },
    { "SETTINGS",    MENU_ACTION, MENU_SETTINGS,    0, NULL },
};

static const menu_screen_t title_screen = {
    .title = NULL,
    .items = title_items,
    .count = (int)(sizeof title_items / sizeof title_items[0]),
    .scrim = false,
    .start_confirms = true,
};

const menu_screen_t *menu_title_screen(void) { return &title_screen; }

static bool       g_open;
static bool       g_confirm;    /* the quit confirmation is showing */
static menu_nav_t g_nav;

static const menu_screen_t *g_screen;
static const menu_screen_t *g_return_to;   /* NULL when at the root */
static const menu_screen_t *g_settings;    /* registered by settings.c */

void menu_register_settings(const menu_screen_t *s) { g_settings = s; }

bool menu_settings_open(void) {
    return g_open && g_settings && g_screen == g_settings;
}
```

- [ ] **Step 3: Rewrite open, close and the push/pop**

Replace `menu_open` and `close_with` (lines 43–55) with:

```c
void menu_open_screen(const menu_screen_t *s) {
    g_open      = true;
    g_confirm   = false;
    g_screen    = s;
    g_return_to = NULL;
    menu_nav_reset(&g_nav, s->count);
}

void menu_open(void) { menu_open_screen(&pause_screen); }

bool menu_active(void) { return g_open; }

static menu_result_t close_with(menu_result_t r) {
    g_open      = false;
    g_confirm   = false;
    g_return_to = NULL;
    return r;
}

/* Back out one level: pop to the parent screen if there is one, otherwise
 * close the menu entirely. The pop is where a screen flushes its state. */
static menu_result_t back_out(void) {
    if (!g_return_to) return close_with(MENU_RESUME);

    if (g_screen->on_close) g_screen->on_close();
    g_screen    = g_return_to;
    g_return_to = NULL;
    menu_nav_reset(&g_nav, g_screen->count);
    return MENU_NONE;
}
```

- [ ] **Step 4: Rewrite menu_update**

Replace the body of `menu_update` after the confirm block with:

```c
    /* One direction from both inputs, so d-pad and stick cannot double-step.
     * cam_y is +1 for d-pad up and stick_y is positive up; both mean "move
     * to the previous row". */
    int dir = 0;
    if      (in->cam_y > 0 || in->stick_y >  0.5f) dir = -1;
    else if (in->cam_y < 0 || in->stick_y < -0.5f) dir =  1;
    menu_nav_step(&g_nav, dir, dt);

    const menu_item_t *it = &g_screen->items[g_nav.cursor];

    /* Left/right adjusts the highlighted value. Action rows ignore it. */
    int hdir = 0;
    if      (in->cam_x > 0 || in->stick_x >  0.5f) hdir =  1;
    else if (in->cam_x < 0 || in->stick_x < -0.5f) hdir = -1;
    int step = menu_nav_step_h(&g_nav, hdir, dt);
    if (step != 0 && it->kind != MENU_ACTION && g_screen->set)
        g_screen->set(it->id, step);

    /* On the title there is nothing to back out to, so Start confirms the
     * row instead and B does nothing. Everywhere else both leave. */
    if (!g_screen->start_confirms && (in->start_btn || in->b_btn))
        return back_out();

    const bool confirm = in->a_btn ||
                         (g_screen->start_confirms && in->start_btn);
    if (!confirm) return MENU_NONE;

    /* A on a toggle flips it, wrapping — the one-button alternative to
     * left/right. A slider has too many steps for that to be useful, so A
     * leaves it alone. */
    if (it->kind == MENU_TOGGLE) {
        if (g_screen->get && g_screen->set) {
            const int v = g_screen->get(it->id);
            g_screen->set(it->id, (v >= it->vmax ? 0 : v + 1) - v);
        }
        return MENU_NONE;
    }
    if (it->kind == MENU_SLIDER) return MENU_NONE;

    switch (it->id) {
    case MENU_SETTINGS:
        if (!g_settings) return MENU_NONE;   /* not registered yet */
        g_return_to = g_screen;
        g_screen    = g_settings;
        menu_nav_reset(&g_nav, g_screen->count);
        return MENU_NONE;

    case MENU_BACK:
        return back_out();

    case MENU_QUIT_TITLE:            /* confirm before losing a run */
        g_confirm = true;
        return MENU_NONE;

    default:
        return close_with((menu_result_t)it->id);
    }
}
```

The confirm block above it is unchanged except that its title reference stays valid; leave lines 65–71 exactly as they are.

- [ ] **Step 5: Rewrite menu_draw for computed geometry**

Replace the panel constants (lines 16–22) with:

```c
/* Centred panel, width fixed and height derived from the row count — a
 * screen may carry two rows or eight. Both stay inside the overscan-safe
 * area at 320x240. */
#define PANEL_X0   48
#define PANEL_X1  272
#define PAD        12
#define ROW_H      14
#define TITLE_H    (ROW_H + 6)
#define VALUE_X   (PANEL_X1 - PAD - 62)
```

and replace `menu_draw` entirely:

```c
void menu_draw(void) {
    if (!g_open || !g_screen) return;

    const bool titled = (g_screen->title != NULL);
    const int  rows   = g_confirm ? 2 : g_screen->count;
    const int  h      = PAD * 2 + rows * ROW_H + (titled ? TITLE_H : 0);
    const int  y0     = (SCREEN_H - h) / 2;

    if (g_screen->scrim) {
        /* Dim the frozen world. Same idiom as splash.c's crossfade veil. */
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0, 0, 0, 150));
        rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);
    }

    rdpq_set_mode_fill(RGBA32(12, 14, 22, 255));
    rdpq_fill_rectangle(PANEL_X0, y0, PANEL_X1, y0 + h);
    rdpq_set_mode_standard();

    const int tx = PANEL_X0 + PAD;
    int       ty = y0 + PAD;

    if (titled) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .width = PANEL_X1 - PANEL_X0, .align = ALIGN_CENTER,
            .style_id = STY_DIM,
        }, MENU_FONT, PANEL_X0, ty, "%s", g_screen->title);
        ty += TITLE_H;
    }

    if (g_confirm) {
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STY_SEL },
                         MENU_FONT, tx, ty, "REALLY QUIT?");
        ty += ROW_H;
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STY_DIM },
                         MENU_FONT, tx, ty, "A = YES   B = NO");
        return;
    }

    for (int i = 0; i < g_screen->count; i++) {
        const menu_item_t *it = &g_screen->items[i];
        const bool sel = (i == g_nav.cursor);
        const int  sty = sel ? STY_SEL : STY_ROW;

        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = sty },
                         MENU_FONT, tx, ty, "%s %s",
                         sel ? ">" : " ", it->label);

        if (it->kind != MENU_ACTION && g_screen->get) {
            const int v = g_screen->get(it->id);
            if (it->value_labels) {
                rdpq_text_printf(&(rdpq_textparms_t){ .style_id = sty },
                                 MENU_FONT, VALUE_X, ty, "%s",
                                 it->value_labels[v]);
            } else {
                /* A bar of vmax cells. vmax is bounded by the row table,
                 * but clamp anyway so a bad row cannot overrun this. */
                char bar[17];
                int  n = it->vmax;
                if (n > 16) n = 16;
                for (int c = 0; c < n; c++) bar[c] = (c < v) ? '#' : '-';
                bar[n] = '\0';
                rdpq_text_printf(&(rdpq_textparms_t){ .style_id = sty },
                                 MENU_FONT, VALUE_X, ty, "[%s]", bar);
            }
        }

        ty += ROW_H;
    }
}
```

- [ ] **Step 6: Build and smoke test**

Run: `bash build.sh && bash tests/run_tests.sh`
Expected: `crux64.z64` produced; host tests pass.

Then run the ROM in gopher64 and pause. Expect: three rows, the panel centred and taller than before, RESUME and QUIT TO TITLE behaving exactly as they did, the quit confirmation unchanged, and SETTINGS doing nothing when confirmed. SETTINGS is inert because `menu_register_settings` has no caller yet.

```bash
/home/ahscott/Projects/n64/gopher64-linux-x86_64 crux64.z64
```

- [ ] **Step 7: Commit**

```bash
git add src/meta/menu.h src/meta/menu.c
git commit -m "menu: value rows, a second screen and computed geometry

Rows gain TOGGLE and SLIDER kinds whose values render from data rather
than callbacks; screens carry their own get/set so menu.c still never
includes settings.h. Screen depth is one, so one return_to pointer rather
than a stack. Panel height now follows the row count.

The SETTINGS row is inert until settings.c registers its screen.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Settings record and row table

All the settings logic, in a stateless libdragon-free unit, exactly as `menu_nav` and `save_format` are. The live record and its side effects come later, in Task 6.

**Files:**
- Create: `src/meta/settings_data.h`, `src/meta/settings_data.c`
- Create: `tests/settings_test.c`
- Modify: `tests/run_tests.sh`, `Makefile`

**Interfaces:**
- Consumes: `menu_item_t`, `MENU_ACTION`, `MENU_TOGGLE`, `MENU_SLIDER`, `MENU_BACK` from `menu.h` — **these arrive in Task 4**, which precedes this task.
- Produces: `save_settings_t`; `SET_FLAG_INVERT_X/Y/RUMBLE/STAMINA`; `setting_id_t` with `SET_MUSIC_VOL, SET_SFX_VOL, SET_CAM_SENS, SET_INVERT_X, SET_INVERT_Y, SET_RUMBLE, SET_STAMINA_BAR, SET_ID_COUNT`; `SET_VOL_MAX` (10), `SET_SENS_MAX` (4); `void settings_data_defaults(save_settings_t *)`, `int settings_data_get(const save_settings_t *, int id)`, `void settings_data_adjust(save_settings_t *, int id, int delta)`, `float settings_data_sens(const save_settings_t *)`, `const menu_item_t *settings_data_rows(int *count)`.

- [ ] **Step 1: Write the failing tests**

Create `tests/settings_test.c`:

```c
/* Host unit tests for the settings record. No libdragon and no N64
 * toolchain — run with tests/run_tests.sh. */
#include "../src/meta/settings_data.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do {                                         \
    if (!(cond)) {                                               \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                              \
    }                                                            \
} while (0)

static void test_defaults_match_the_spec(void) {
    save_settings_t s;
    settings_data_defaults(&s);
    CHECK(s.music_vol == 7);
    CHECK(s.sfx_vol   == 7);
    CHECK(s.cam_sens  == 2);
    CHECK(settings_data_get(&s, SET_INVERT_X)    == 0);
    CHECK(settings_data_get(&s, SET_INVERT_Y)    == 0);
    CHECK(settings_data_get(&s, SET_RUMBLE)      == 1);
    CHECK(settings_data_get(&s, SET_STAMINA_BAR) == 1);
}

static void test_defaults_zero_the_reserved_bytes(void) {
    save_settings_t s;
    memset(&s, 0xAB, sizeof s);
    settings_data_defaults(&s);
    for (int i = 0; i < 4; i++) CHECK(s.reserved[i] == 0);
}

/* The forward-compatibility rule: adjusting a setting must never disturb
 * bytes this build does not understand, or an older build reading a newer
 * cart would destroy the newer build's settings on the first change. */
static void test_adjust_never_touches_reserved(void) {
    save_settings_t s;
    settings_data_defaults(&s);
    for (int i = 0; i < 4; i++) s.reserved[i] = (uint8_t)(0x40 + i);
    for (int id = 0; id < SET_ID_COUNT; id++) {
        settings_data_adjust(&s, id, +1);
        settings_data_adjust(&s, id, -1);
    }
    for (int i = 0; i < 4; i++) CHECK(s.reserved[i] == (uint8_t)(0x40 + i));
}

/* Every id's range, walked from both ends. Clamping, never wrapping — a
 * held direction must come to rest at the end, not roll over. */
static void test_adjust_clamps_at_both_ends(void) {
    static const struct { int id; int vmax; } r[] = {
        { SET_MUSIC_VOL,   SET_VOL_MAX  },
        { SET_SFX_VOL,     SET_VOL_MAX  },
        { SET_CAM_SENS,    SET_SENS_MAX },
        { SET_INVERT_X,    1 },
        { SET_INVERT_Y,    1 },
        { SET_RUMBLE,      1 },
        { SET_STAMINA_BAR, 1 },
    };
    for (size_t i = 0; i < sizeof r / sizeof r[0]; i++) {
        save_settings_t s;
        settings_data_defaults(&s);

        for (int n = 0; n < 40; n++) settings_data_adjust(&s, r[i].id, +1);
        CHECK(settings_data_get(&s, r[i].id) == r[i].vmax);

        for (int n = 0; n < 40; n++) settings_data_adjust(&s, r[i].id, -1);
        CHECK(settings_data_get(&s, r[i].id) == 0);
    }
}

static void test_every_value_in_range_round_trips(void) {
    save_settings_t s;
    settings_data_defaults(&s);
    for (int n = 0; n < 40; n++) settings_data_adjust(&s, SET_MUSIC_VOL, -1);
    for (int v = 0; v <= SET_VOL_MAX; v++) {
        CHECK(settings_data_get(&s, SET_MUSIC_VOL) == v);
        settings_data_adjust(&s, SET_MUSIC_VOL, +1);
    }
}

/* Each flag owns exactly one bit: setting one must not disturb the others. */
static void test_flags_are_independent(void) {
    static const int ids[] = { SET_INVERT_X, SET_INVERT_Y,
                               SET_RUMBLE, SET_STAMINA_BAR };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        save_settings_t s;
        settings_data_defaults(&s);

        int before[4];
        for (size_t j = 0; j < 4; j++) before[j] = settings_data_get(&s, ids[j]);

        int flipped = before[i] ? -1 : +1;
        settings_data_adjust(&s, ids[i], flipped);

        for (size_t j = 0; j < 4; j++) {
            if (j == i) CHECK(settings_data_get(&s, ids[j]) != before[j]);
            else        CHECK(settings_data_get(&s, ids[j]) == before[j]);
        }
    }
}

static void test_sensitivity_table(void) {
    save_settings_t s;
    settings_data_defaults(&s);
    CHECK(settings_data_sens(&s) == 1.00f);          /* default index 2 */

    for (int n = 0; n < 40; n++) settings_data_adjust(&s, SET_CAM_SENS, -1);
    CHECK(settings_data_sens(&s) == 0.50f);

    for (int n = 0; n < 40; n++) settings_data_adjust(&s, SET_CAM_SENS, +1);
    CHECK(settings_data_sens(&s) == 2.00f);
}

/* The row table is data the menu trusts blindly: a slider whose vmax
 * disagrees with its field's real range draws a bar the value can never
 * fill, and a labelled row short one string indexes off the end. */
static void test_row_table_is_consistent(void) {
    int count = 0;
    const menu_item_t *rows = settings_data_rows(&count);
    CHECK(rows != NULL);
    CHECK(count == 8);            /* seven settings plus BACK */

    int value_rows = 0;
    for (int i = 0; i < count; i++) {
        const menu_item_t *it = &rows[i];
        CHECK(it->label != NULL);

        if (it->kind == MENU_ACTION) {
            CHECK(it->id == MENU_BACK);
            continue;
        }
        value_rows++;

        /* vmax must be the value the field actually saturates at. */
        save_settings_t s;
        settings_data_defaults(&s);
        for (int n = 0; n < 40; n++) settings_data_adjust(&s, it->id, +1);
        CHECK(settings_data_get(&s, it->id) == it->vmax);

        if (it->value_labels)
            for (int v = 0; v <= it->vmax; v++) CHECK(it->value_labels[v] != NULL);
    }
    CHECK(value_rows == 7);
}

int main(void) {
    test_defaults_match_the_spec();
    test_defaults_zero_the_reserved_bytes();
    test_adjust_never_touches_reserved();
    test_adjust_clamps_at_both_ends();
    test_every_value_in_range_round_trips();
    test_flags_are_independent();
    test_sensitivity_table();
    test_row_table_is_consistent();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all settings checks passed\n");
    return 0;
}
```

Add to `tests/run_tests.sh`, after the `save_format_test` block:

```bash
gcc -std=c99 -O1 -Wall -Wextra -Werror \
    -o build/settings_test settings_test.c ../src/meta/settings_data.c
./build/settings_test
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `bash tests/run_tests.sh`
Expected: FAIL — `settings_data.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/meta/settings_data.h`:

```c
#pragma once

/* Pure settings logic — no libdragon, no N64 headers, so this file is
 * compiled both into the ROM and into tests/run_tests.sh on the host.
 * Stateless: every function takes the record it operates on. The live
 * record, its EEPROM storage and its side effects live in settings.c. */

#include <stdint.h>
#include <stdbool.h>

#include "menu.h"

/* One 8-byte EEPROM block, stored in its own container at blocks 2-3. */
typedef struct __attribute__((packed)) {
    uint8_t music_vol;   /* 0..SET_VOL_MAX */
    uint8_t sfx_vol;     /* 0..SET_VOL_MAX */
    uint8_t cam_sens;    /* 0..SET_SENS_MAX, an index into the sens table */
    uint8_t flags;       /* SET_FLAG_* bits */

    /* Zeroed by settings_data_defaults and NEVER cleared on store: a record
     * loaded from the cart is written back verbatim. That is what lets a
     * later version claim these bytes without a version bump — an older
     * build preserves settings it does not understand instead of destroying
     * them. Only works while zero is the right default for whatever lands
     * here; a setting needing a nonzero default needs a version bump. */
    uint8_t reserved[4];
} save_settings_t;

_Static_assert(sizeof(save_settings_t) == 8,
               "save_settings_t must be one 8-byte EEPROM block");

#define SET_FLAG_INVERT_X  0x01
#define SET_FLAG_INVERT_Y  0x02
#define SET_FLAG_RUMBLE    0x04
#define SET_FLAG_STAMINA   0x08

typedef enum {
    SET_MUSIC_VOL,
    SET_SFX_VOL,
    SET_CAM_SENS,
    SET_INVERT_X,
    SET_INVERT_Y,
    SET_RUMBLE,
    SET_STAMINA_BAR,
    SET_ID_COUNT,
} setting_id_t;

#define SET_VOL_MAX   10
#define SET_SENS_MAX   4

void settings_data_defaults(save_settings_t *s);

/* Current value of one setting, 0..vmax. Flags report 0 or 1. */
int  settings_data_get(const save_settings_t *s, int id);

/* Moves one setting by `delta`, clamped to its range. Never wraps: a held
 * direction comes to rest at the end. Leaves reserved[] alone. */
void settings_data_adjust(save_settings_t *s, int id, int delta);

/* Camera sensitivity as a multiplier, from the cam_sens index. */
float settings_data_sens(const save_settings_t *s);

/* The settings screen's rows, including the trailing BACK action. The
 * table is const; settings.c pairs it with the callbacks that reach the
 * live record. */
const menu_item_t *settings_data_rows(int *count);
```

- [ ] **Step 4: Write the implementation**

Create `src/meta/settings_data.c`:

```c
#include "settings_data.h"

static const float sens_table[SET_SENS_MAX + 1] = {
    0.50f, 0.75f, 1.00f, 1.50f, 2.00f,
};

void settings_data_defaults(save_settings_t *s) {
    s->music_vol = 7;
    s->sfx_vol   = 7;
    s->cam_sens  = 2;                                  /* 1.00x */
    s->flags     = SET_FLAG_RUMBLE | SET_FLAG_STAMINA;
    for (int i = 0; i < 4; i++) s->reserved[i] = 0;
}

/* Zero for the three scalar settings, which live in their own bytes. */
static uint8_t flag_bit(int id) {
    switch (id) {
    case SET_INVERT_X:    return SET_FLAG_INVERT_X;
    case SET_INVERT_Y:    return SET_FLAG_INVERT_Y;
    case SET_RUMBLE:      return SET_FLAG_RUMBLE;
    case SET_STAMINA_BAR: return SET_FLAG_STAMINA;
    default:              return 0;
    }
}

static int id_vmax(int id) {
    switch (id) {
    case SET_MUSIC_VOL:
    case SET_SFX_VOL:  return SET_VOL_MAX;
    case SET_CAM_SENS: return SET_SENS_MAX;
    default:           return 1;
    }
}

int settings_data_get(const save_settings_t *s, int id) {
    switch (id) {
    case SET_MUSIC_VOL: return s->music_vol;
    case SET_SFX_VOL:   return s->sfx_vol;
    case SET_CAM_SENS:  return s->cam_sens;
    default: {
        const uint8_t b = flag_bit(id);
        return (b && (s->flags & b)) ? 1 : 0;
    }
    }
}

void settings_data_adjust(save_settings_t *s, int id, int delta) {
    const int hi = id_vmax(id);
    int v = settings_data_get(s, id) + delta;
    if (v < 0)  v = 0;
    if (v > hi) v = hi;

    switch (id) {
    case SET_MUSIC_VOL: s->music_vol = (uint8_t)v; break;
    case SET_SFX_VOL:   s->sfx_vol   = (uint8_t)v; break;
    case SET_CAM_SENS:  s->cam_sens  = (uint8_t)v; break;
    default: {
        const uint8_t b = flag_bit(id);
        if (!b) break;
        if (v) s->flags |= b;
        else   s->flags  = (uint8_t)(s->flags & (uint8_t)~b);
        break;
    }
    }
}

float settings_data_sens(const save_settings_t *s) {
    uint8_t i = s->cam_sens;
    if (i > SET_SENS_MAX) i = SET_SENS_MAX;
    return sens_table[i];
}

static const char *const lbl_dir[]  = { "NORMAL", "INVERTED" };
static const char *const lbl_on[]   = { "OFF", "ON" };
static const char *const lbl_sens[] = { "0.50x", "0.75x", "1.00x",
                                        "1.50x", "2.00x" };

/* value_labels NULL means "draw a bar", which is what the two volumes
 * want; the sensitivity slider names its steps instead. */
static const menu_item_t rows[] = {
    { "MUSIC",       MENU_SLIDER, SET_MUSIC_VOL,   SET_VOL_MAX,  NULL     },
    { "SOUND",       MENU_SLIDER, SET_SFX_VOL,     SET_VOL_MAX,  NULL     },
    { "CAMERA X",    MENU_TOGGLE, SET_INVERT_X,    1,            lbl_dir  },
    { "CAMERA Y",    MENU_TOGGLE, SET_INVERT_Y,    1,            lbl_dir  },
    { "SENSITIVITY", MENU_SLIDER, SET_CAM_SENS,    SET_SENS_MAX, lbl_sens },
    { "RUMBLE",      MENU_TOGGLE, SET_RUMBLE,      1,            lbl_on   },
    { "STAMINA BAR", MENU_TOGGLE, SET_STAMINA_BAR, 1,            lbl_on   },
    { "BACK",        MENU_ACTION, MENU_BACK,       0,            NULL     },
};

const menu_item_t *settings_data_rows(int *count) {
    if (count) *count = (int)(sizeof rows / sizeof rows[0]);
    return rows;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `bash tests/run_tests.sh`
Expected: PASS — `all settings checks passed`.

- [ ] **Step 6: Add the object to the Makefile**

In `Makefile`, after the `save_container.o` line:

```make
    $(BUILD_DIR)/src/meta/settings_data.o \
```

- [ ] **Step 7: Build and commit**

```bash
bash build.sh
git add src/meta/settings_data.h src/meta/settings_data.c tests/settings_test.c tests/run_tests.sh Makefile
git commit -m "settings: record, clamping and row table as a pure unit

Stateless and libdragon-free, so the clamping and the flag packing are
host-tested the way menu_nav and save_format are. Includes the rule that
makes the reserved bytes usable later: adjust never touches them.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: The settings shell — storage and side effects

The live record, its container I/O, and the two consumers that are pushed to rather than polled.

**Files:**
- Create: `src/meta/settings.h`, `src/meta/settings.c`
- Modify: `src/audio/synth.h`, `src/audio/synth.c:9`, the mix tail near `src/audio/synth.c:340`
- Modify: `src/input/rumble.h`, `src/input/rumble.c`
- Modify: `src/main.c:90`, `Makefile`, `CLAUDE.md`

**Interfaces:**
- Consumes: `save_container_load` / `save_container_store` (Task 2); `menu_register_settings` (Task 4); `settings_data_*` and `save_settings_t` (Task 5).
- Produces: `void settings_init(void)`, `const save_settings_t *settings_get(void)`; `void synth_set_gains(float music, float sfx)`; `void rumble_set_enabled(bool on)`.

- [ ] **Step 1: Add the synth gains**

In `src/audio/synth.h`, after `void synth_poll(void);`:

```c
/* Output gains, 0..1, pushed from settings.c when the player changes them.
 * Stored in statics rather than polled: the mix loop runs once per output
 * sample and must not reach outside itself. `sfx` scales the whole
 * diegetic layer — wind, drone bed, heartbeat and one-shots alike. */
void synth_set_gains(float music, float sfx);
```

In `src/audio/synth.c`, after the `MUSIC_GAIN` define at line 9:

```c
/* Player volumes (settings.c). MUSIC_GAIN is the fixed mix balance; these
 * scale on top of it, and music.c's pause duck is a third multiplier, so
 * none of the three can fight the others. */
static float user_music_gain = 1.f;
static float user_sfx_gain   = 1.f;
```

Add, next to `synth_poll`:

```c
void synth_set_gains(float music, float sfx) {
    user_music_gain = clampf(music, 0.f, 1.f);
    user_sfx_gain   = clampf(sfx,   0.f, 1.f);
}
```

And in the mix tail, replace the two `outL` / `outR` lines:

```c
        float outL = clampf(sample * user_sfx_gain +
                            ml * MUSIC_GAIN * user_music_gain, -1.f, 1.f);
        float outR = clampf(sample * user_sfx_gain +
                            mr * MUSIC_GAIN * user_music_gain, -1.f, 1.f);
```

- [ ] **Step 2: Add the rumble gate**

In `src/input/rumble.h`, after `void rumble_init(void);`:

```c
#include <stdbool.h>

/* Master enable (settings.c). Gating here rather than at the call sites
 * covers all ten rumble_kick calls at once and keeps this driver free of
 * a settings.h include. Disabling also drops any kick already decaying. */
void rumble_set_enabled(bool on);
```

In `src/input/rumble.c`, add the static beside the others:

```c
static bool  enabled = true;
```

set it in `rumble_init`:

```c
void rumble_init(void) {
    level = fade = pwm_acc = 0.f;
    motor_on = false;
    enabled  = true;
}
```

add the setter:

```c
void rumble_set_enabled(bool on) {
    enabled = on;
    if (!enabled) {
        /* Kill anything mid-decay; rumble_update drops the motor next frame. */
        level = 0.f;
        fade  = 0.f;
    }
}
```

and gate `rumble_kick` on its first line:

```c
void rumble_kick(float strength, float duration) {
    if (!enabled) return;
    if (strength <= level) return;
```

- [ ] **Step 3: Write the settings header**

Create `src/meta/settings.h`:

```c
#pragma once

#include "settings_data.h"

/* The live settings record: loaded from its own EEPROM container at
 * blocks 2-3, applied to the audio and rumble subsystems, and exposed to
 * main.c for the camera and the stamina bar.
 *
 * Writes happen once, when the settings screen closes, and only if a value
 * actually changed — a held slider must never write per frame, at ~6ms and
 * a wear cycle each. */

/* Loads the record (or defaults), applies it, and registers the settings
 * screen with the menu. Call after rumble_init and synth_init, since it
 * pushes to both. */
void settings_init(void);

const save_settings_t *settings_get(void);
```

- [ ] **Step 4: Write the settings implementation**

Create `src/meta/settings.c`:

```c
#include <libdragon.h>

#include "settings.h"
#include "settings_data.h"
#include "save_container.h"
#include "menu.h"
#include "../audio/synth.h"
#include "../input/rumble.h"

/* Blocks 0-1 hold the progress record; settings get their own container so
 * a piton commit never rewrites them and a pre-settings cart migrates by
 * simply having nothing here. */
#define SETTINGS_BASE  2

static save_settings_t cur;
static bool            dirty;
static menu_screen_t   screen;

static void apply(void) {
    synth_set_gains((float)cur.music_vol / (float)SET_VOL_MAX,
                    (float)cur.sfx_vol   / (float)SET_VOL_MAX);
    rumble_set_enabled((cur.flags & SET_FLAG_RUMBLE) != 0);
}

static int row_get(int id) { return settings_data_get(&cur, id); }

static void row_set(int id, int delta) {
    const int before = settings_data_get(&cur, id);
    settings_data_adjust(&cur, id, delta);
    if (settings_data_get(&cur, id) == before) return;   /* clamped, no-op */
    dirty = true;
    apply();                 /* live: you hear and feel what you adjust */
}

/* Fires when the settings screen pops. The only write path. */
static void row_close(void) {
    if (!dirty) return;
    if (eeprom_present() == EEPROM_NONE) { dirty = false; return; }

    if (save_container_store(SETTINGS_BASE, SAVE_SETTINGS_VERSION,
                             (const uint8_t *)&cur, (uint8_t)sizeof cur))
        dirty = false;
}

void settings_init(void) {
    int count = 0;
    const menu_item_t *rows = settings_data_rows(&count);

    settings_data_defaults(&cur);
    dirty = false;

    /* Loads over the defaults, and leaves them alone on anything that is
     * not a valid current record — including a cart written before
     * settings existed, which is the whole migration. allow_legacy is
     * false: the eepromfs signature means nothing at block 2. */
    if (eeprom_present() != EEPROM_NONE)
        save_container_load(SETTINGS_BASE, SAVE_SETTINGS_VERSION, false,
                            (uint8_t *)&cur, (uint8_t)sizeof cur);

    screen = (menu_screen_t){
        .title          = "SETTINGS",
        .items          = rows,
        .count          = count,
        .scrim          = true,
        .start_confirms = false,
        .get            = row_get,
        .set            = row_set,
        .on_close       = row_close,
    };
    menu_register_settings(&screen);

    apply();
}

const save_settings_t *settings_get(void) { return &cur; }
```

- [ ] **Step 5: Call it at boot**

In `src/main.c`, after `save_init();` at line 90:

```c
    settings_init();
```

and add the include beside the other `src/meta` includes:

```c
#include "meta/settings.h"
```

- [ ] **Step 6: Add the object to the Makefile**

In `Makefile`, after the `settings_data.o` line:

```make
    $(BUILD_DIR)/src/meta/settings.o \
```

- [ ] **Step 7: Build and verify on cart**

Run: `bash build.sh && bash tests/run_tests.sh`

Then, in gopher64: pause, open SETTINGS, and check each row. MUSIC and SOUND must change the mix as you hold left/right. RUMBLE and STAMINA BAR toggle their labels (the stamina bar itself is not wired until Task 7). Back out with B, quit the emulator, relaunch **the same ROM**, and confirm the values persisted.

```bash
/home/ahscott/Projects/n64/gopher64-linux-x86_64 crux64.z64
```

Note that gopher64 names saves `CRUX64-<uppercase sha256 of the ROM>.eep`, so this check only works if the ROM is unchanged between the two runs. If you rebuild in between you get a blank EEPROM and the check proves nothing. To carry a save across a rebuild:

```bash
cp old.eep ~/.local/share/gopher64/saves/CRUX64-$(sha256sum crux64.z64 | awk '{print toupper($1)}').eep
```

- [ ] **Step 8: Document the container layout**

In `CLAUDE.md`, in the `## EEPROM` section, after the paragraph describing the container in `src/meta/save_format.c`, add:

```markdown
- Two containers, not one: progress at blocks 0–1, settings at blocks 2–3
  (`src/meta/settings.c`). Growing the payload instead would make every
  progress commit a three-block ~18 ms write against a 16.7 ms frame, and
  those fire mid-gameplay at piton placement. Separate containers also make
  the settings migration free — a pre-settings cart has nothing at block 2,
  so `save_format_detect` says BLANK and defaults apply.
- `save_format_detect` takes the expected version and an `allow_legacy`
  flag because the two containers version independently and the eepromfs
  signature is meaningful only at block 0.
- `save_settings_t`'s four reserved bytes are zeroed by `defaults()` and
  never cleared on store, so a later version can claim them without a
  version bump. Do not "tidy" that by memsetting the tail before a write —
  an older build would then destroy a newer build's settings.
```

- [ ] **Step 9: Commit**

```bash
git add src/meta/settings.h src/meta/settings.c src/audio/synth.h src/audio/synth.c \
        src/input/rumble.h src/input/rumble.c src/main.c Makefile CLAUDE.md
git commit -m "settings: live record, EEPROM container and side effects

Settings load from their own container at blocks 2-3, apply live as they
change, and write once when the screen closes. Audio gains and the rumble
enable are pushed to their subsystems rather than polled: the mix loop
runs per output sample and must not reach outside itself.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Wire the title menu, the camera and the stamina bar

The remaining consumers, plus the title screen's menu and the music duck.

**Files:**
- Modify: `src/render/render.c:382-385`
- Modify: `src/main.c:207-220` (title branch), `src/main.c:293-302` (duck), `src/main.c:363-367` (camera), `src/main.c:491` (stamina)

**Interfaces:**
- Consumes: `menu_open_screen`, `menu_title_screen`, `menu_settings_open`, `MENU_BEGIN_CLIMB` (Task 4); `settings_get`, `settings_data_sens`, `SET_FLAG_INVERT_X/Y/STAMINA` (Tasks 5 and 6).

- [ ] **Step 1: Draw the menu over the title**

`menu_draw` currently runs only in render.c's `hud->menu` branch, which is an `else if` after `hud->title` — so the title menu would never appear. In `src/render/render.c`, in the `if (hud->title)` branch, add the call between the two existing ones:

```c
    if (hud->title) {
        draw_title_hud(hud);
        /* The title menu draws over the vista and its record line, but
         * under the splash fade so the boot transition still covers it. */
        menu_draw();
        /* Boot splash fading out over the title scene (no-op once done). */
        splash_draw_overlay();
    } else if (hud->cinematic) {
```

- [ ] **Step 2: Drive the title menu from main.c**

In `src/main.c`, replace the `if (in->start_btn) { ... }` block inside `if (in_title)` (lines 212–220) with:

```c
            /* The title runs the menu shell: BEGIN CLIMB / SETTINGS, with
             * the cursor starting on BEGIN CLIMB and Start confirming, so
             * pressing Start on boot begins the climb as it always has. */
            if (!menu_active()) menu_open_screen(menu_title_screen());

            if (menu_update(in, dt) == MENU_BEGIN_CLIMB) {
                in_title     = false;
                in_cutscene  = true;
                scene_t      = 0.f;
                scene_look_y = 96.f;    /* the peak, seen from base camp */
                scene_alt    = 0.18f;
                int nlines;
                const dlg_line_t *lines = prologue_scene(&nlines);
                dialogue_start(lines, nlines);
                rumble_kick(0.4f, 0.2f);
                synth_chalk();
                music_play(MUSIC_GAME);   /* swap the title loop for the climb */
            }
```

`menu_open_screen` arms the one-frame lock, so the Start press that skipped the splash cannot also confirm a title row.

Then, in the `MENU_QUIT_TITLE` branch at line 304, the menu is closed by `close_with` before main.c sees the result, so the title branch reopens its own screen on the next frame. No change is needed there.

- [ ] **Step 3: Lift the duck on the settings screen**

In `src/main.c`, replace the duck handling at lines 293–302:

```c
        if (!menu_active() && in->start_btn) {
            menu_open();
            music_set_duck(0.25f);
        }

        if (menu_active()) {
            menu_result_t mr = menu_update(in, dt);

            /* A volume slider judged through a duck is not judged at all,
             * so the settings screen hears the track at full level. Setting
             * the same value repeatedly is free — music_set_duck glides. */
            music_set_duck(menu_settings_open() ? 1.f : 0.25f);

            if (mr != MENU_NONE)
                music_set_duck(1.f);    /* one place: every exit restores it */
```

- [ ] **Step 4: Apply the camera settings**

In `src/main.c`, replace lines 363–367:

```c
        /* D-pad always orbits; on foot, holding Z hands the stick to
         * the camera too (the sim ignores it while Z is down). */
        bool cam_stick = cs->mode == CLIMBER_ON_FOOT && in->z_held;
        const save_settings_t *sg = settings_get();
        const float cam_sens = settings_data_sens(sg);
        const float inv_x = (sg->flags & SET_FLAG_INVERT_X) ? -1.f : 1.f;
        const float inv_y = (sg->flags & SET_FLAG_INVERT_Y) ? -1.f : 1.f;

        /* Inversion belongs here and nowhere else. Applying it in input.c
         * would also invert menu navigation, which reads cam_y directly
         * (menu.c). An inverted-Y player still moves menu cursors normally. */
        cam_yaw   += inv_x * (in->cam_x + (cam_stick ? in->stick_x : 0.f))
                   * dt * 2.2f * cam_sens;
        cam_pitch += inv_y * (in->cam_y + (cam_stick ? in->stick_y : 0.f))
                   * dt * 1.6f * cam_sens;
        if (cam_pitch < -0.45f) cam_pitch = -0.45f;
        if (cam_pitch >  1.05f) cam_pitch =  1.05f;
```

- [ ] **Step 5: Gate the stamina bar**

In `src/main.c`, replace the `.stam` line at 491:

```c
            .stam        = (cs->mode != CLIMBER_ON_FOOT &&
                            (settings_get()->flags & SET_FLAG_STAMINA))
                         ? cs->stam : NULL,
```

- [ ] **Step 6: Build and run the full verification**

Run: `bash build.sh && bash tests/run_tests.sh`
Expected: `crux64.z64` produced; all three host suites pass.

Then, in gopher64, work through every check:

1. **Title menu.** On boot the cursor sits on BEGIN CLIMB; pressing Start begins the climb immediately. Reopen, move to SETTINGS, confirm with A — the settings panel draws over the vista with no scrim. Back out with B and confirm BEGIN CLIMB still works.
2. **Every setting.** Change all seven. MUSIC and SOUND alter the mix live; SENSITIVITY and both CAMERA rows change how the D-pad orbits; RUMBLE off silences the pak on a piton strike; STAMINA BAR off hides the four bars.
3. **Persistence.** Quit, relaunch the same ROM, confirm all seven survived.
4. **The migration.** This is the one that matters. Take a cart that has a progress record but no settings — `tests/fixtures/legacy-eepromfs.eep` after one boot of the current ROM will do, or any `.eep` from before this branch. Copy it onto the current ROM's hash and boot: settings must come up at defaults and the progress record must be intact on the title screen's best-altitude line.
   ```bash
   cp <old>.eep ~/.local/share/gopher64/saves/CRUX64-$(sha256sum crux64.z64 | awk '{print toupper($1)}').eep
   ```
5. **Corruption.** With a hex editor, flip a byte in block 3 (file offset 24–31) of a saved `.eep`. Boot: settings reset to defaults, the progress record untouched.
6. **The write stall.** Change a setting and back out while music is playing. Listen for a click or dropout on that frame — the two-block write blocks the CPU for ~12 ms and `synth_poll` does not run during it. Report what you hear rather than assuming it is fine.
7. **Duck.** Entering settings from the pause menu should raise the music to full over ~80 ms without a click, and returning should lower it again.

- [ ] **Step 7: Commit**

```bash
git add src/render/render.c src/main.c
git commit -m "settings: title menu, camera, stamina bar and the duck

The title runs the menu shell with the cursor on BEGIN CLIMB and Start
confirming, so Start-on-boot still starts the climb. Camera inversion is
applied here rather than in input.c, which would also invert menu
navigation. The settings screen lifts the pause duck so a volume slider
can be judged at the level it will actually play at.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Verification summary

Host tests, all via `bash tests/run_tests.sh`:

| Suite | Covers |
|---|---|
| `save_format_test` | 14 cases: CRC, header layout, version parameterization, legacy suppression |
| `menu_nav_test` | existing vertical cases plus 5 horizontal, including axis independence |
| `settings_test` | 8 cases: defaults, clamping, flag independence, reserved-byte preservation, row-table consistency |

What host tests cannot reach, and so must be done on cart (Task 7 Step 6): the migration from a pre-settings cart, corruption recovery, the write stall on screen exit, and every live-application path.

## Known gap

`save_commit()` has never executed — every `write_now()` so far has run from inside `save_init`. This plan adds a second write path on top of it. Do the outstanding progress check first (start a run, plant a piton with Z, quit, relaunch, confirm the record persisted) so that a failure during Task 6 or 7 is unambiguously the settings container and not the untested progress write.
