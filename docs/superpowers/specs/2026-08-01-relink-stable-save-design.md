# Relink-Stable EEPROM Save Format — Design

**Date:** 2026-08-01
**Status:** Approved, ready for implementation planning
**Branch:** `fix/eeprom-stable-signature`

## Problem

The save record is silently destroyed by unrelated code changes.

`save.c:43` mounts the record through libdragon's eepromfs. eepromfs stamps a
signature block on the cart and refuses to read a filesystem whose signature
does not match the one the running ROM computes. That signature is built in
`libdragon/src/joybus/eepromfs.c:113-143`:

```
sig = 'e','e','p', count, total_bytes_hi, total_bytes_lo, crc16_hi, crc16_lo
```

where the checksum comes from `eepromfs.c:280`:

```c
eepfs_files_checksum = calculate_crc16((void *)entries, sizeof(eepfs_entry_t) * count);
```

`eepfs_entry_t` (`libdragon/include/eepromfs.h:43-76`) is:

```c
typedef struct eepfs_entry_t {
    const char * path;
    size_t       size;
} eepfs_entry_t;
```

The CRC therefore runs over the raw struct bytes — which include the **pointer**,
not the string it addresses. The signature depends on where the linker happens to
place the literal `"/crux64.sav"`.

When that address moves, `eepfs_verify_signature()` returns false and `save.c:46-51`
takes the `eepfs_wipe()` branch. Best altitude, initials, falls and playtime are
erased with no error path and no user-visible signal.

### Evidence

Two independent confirmations.

**Controlled experiment.** Adding a `volatile const char[64]` to `src/input/input.c`
— a translation unit linked before `save.o` — moved the stored path pointer:

```
baseline   eeprom_files[0] = { path=0x800799f0, size=8 }
with pad   eeprom_files[0] = { path=0x80079a10, size=8 }
```

A 32-byte shift, from a change with no relationship to saving. (An earlier attempt
using an *unreferenced* probe showed no movement — `--gc-sections` had stripped it
before it could perturb `.rodata`. That run was a broken experiment, not a negative
result.)

**Real save data.** Four consecutive gopher64 EEPROM images from 2026-07-31:

```
65 65 70 01 00 08 ed e3
65 65 70 01 00 08 7f 7f
65 65 70 01 00 08 55 1b
65 65 70 01 00 08 10 bb
```

Identical magic, file count and total size; four different checksums. Each build
would have rejected — and wiped — the previous build's cart.

### Why it has gone unnoticed

gopher64 keys save files by ROM hash (`CRUX64-<sha256>.eep`), so every rebuild
starts from a blank EEPROM anyway. The fault is invisible under the emulator and
only manifests on hardware, where one physical cart is reflashed repeatedly.

## Scope

This change replaces the **container**. It does not change what is stored.

**In scope**

- A save container whose identity is a literal value, immune to relinking.
- Rescue of the existing eepromfs-written record.
- A version byte, so future payload changes migrate instead of resetting.
- Host tests for the format logic.

**Explicitly out of scope**

- `save_data_t` — stays byte-identical. Same fields, same order, same 8 bytes.
- `save.h`'s public API — unchanged, so `main.c` is not touched. All nine call
  sites (`main.c:90,233,305,357,401,402,403,405`) keep working as written.
- Settings persistence. That is stage 2 of the pause shell, and lands as payload
  version 2 on top of this work.
- Initials editing and save management UI. Stage 3.

### Relationship to the GDD

GDD 3.4 specifies "libdragon's eepromfs high-level/**low-level** API". The
low-level `eeprom_read`/`eeprom_write` path is already sanctioned by that wording,
so this is a change of layer within the stated design, not a departure from it.
`CLAUDE.md`'s one-line summary ("EEPROM 4k via eepromfs") is narrower than the GDD
and is updated as part of this branch.

## On-cart format

Two blocks of the 64 available on a 4 Kbit cart (`N64_ROM_SAVETYPE = eeprom4k`).

```
block 0 — header
  [0..2]  'C','R','X'    magic; a literal value, never an address
  [3]     version        payload format version, currently 1
  [4..5]  crc16(payload) big-endian
  [6]     payload length in bytes
  [7]     reserved, written as 0

block 1 — payload
  save_data_t (8 bytes)
```

Sizes are values, not derived from link-time state, so nothing in this header can
change as a result of recompilation.

## Module split

Follows the `menu_nav` precedent from the pause shell: the risky logic goes in a
pure unit that runs on the host, and the libdragon dependency stays in a thin shell.

### `src/meta/save_format.h` / `save_format.c` — pure, no libdragon

Owns CRC-16, header pack/parse, and layout detection. Includes only `<stdint.h>`,
`<stdbool.h>` and `<string.h>`, so it compiles and tests on the host.

```c
typedef enum {
    SAVE_LAYOUT_CURRENT,   /* CRX header, version == SAVE_VERSION */
    SAVE_LAYOUT_OLDER,     /* CRX header, version <  SAVE_VERSION */
    SAVE_LAYOUT_NEWER,     /* CRX header, version >  SAVE_VERSION */
    SAVE_LAYOUT_LEGACY,    /* eepromfs-written, pre-fix cart      */
    SAVE_LAYOUT_BLANK,     /* unrecognised: fresh or foreign cart */
} save_layout_t;
```

Interface:

- `uint16_t save_crc16(const uint8_t *data, size_t len)`
- `void save_header_build(uint8_t out[8], uint8_t version, const uint8_t *payload, uint8_t len)`
- `save_layout_t save_format_detect(const uint8_t block0[8], uint8_t *out_version, uint8_t *out_len)`
- `bool save_payload_valid(const uint8_t block0[8], const uint8_t *payload, uint8_t len)`

`save_format_detect` reports the header's version and length bytes through its
out-parameters but does **not** judge whether that length is acceptable — it
cannot, because the expected size differs per version. `save.c` makes that call:
for `SAVE_LAYOUT_CURRENT` it requires `len == sizeof(save_data_t)`, and for
`SAVE_LAYOUT_OLDER` the corresponding `migrate_from` owns the check. A length
that fails its version's expectation is handled as `SAVE_LAYOUT_BLANK`.

### `src/meta/save.c` — EEPROM I/O only

Keeps the existing statics (`rec`, `present`, `dirty`, `time_accum`) and the
existing public functions unchanged in behaviour. `save_init()` gains the boot
flow below; `write_now()` writes the header block and the payload block.

## Boot flow

```
eeprom_present() == EEPROM_NONE
    -> defaults, present = false, return false            (unchanged behaviour)

read block 0, classify with save_format_detect()

SAVE_LAYOUT_CURRENT   -> read payload; crc ok  -> load
                                       crc bad -> defaults, rewrite
SAVE_LAYOUT_OLDER     -> migrate_from(version), rewrite at SAVE_VERSION
SAVE_LAYOUT_NEWER     -> defaults, rewrite
SAVE_LAYOUT_LEGACY    -> adopt block 1 as save_data_t, rewrite in new format
SAVE_LAYOUT_BLANK     -> defaults, write fresh
```

At version 1 there is no `SAVE_LAYOUT_OLDER` case reachable in practice — no
version 0 CRX cart was ever written. The branch exists so stage 2 can add a
migration without restructuring `save_init`, and is covered by a test that
constructs a synthetic version-0 header.

### Legacy detection

A pre-fix cart is recognised by comparing **only bytes 0–5** of block 0 against:

```
65 65 70 01 00 08     'e','e','p', count=1, total_bytes=0x0008
```

Bytes 6–7 are deliberately ignored. Those two bytes are exactly the
pointer-derived checksum that caused the bug, and they differ from build to
build — skipping them is what lets the rescue succeed regardless of which build
wrote the cart. The four real images above all share these six bytes.

The adopted record then passes the same sanity guard the current code uses at
`save.c:56`: if `initials[0] == '\0'` the slot was wiped but never written, so
defaults are used instead.

Because the magic differs (`'C','R','X'` vs `'e','e','p'`), a legacy cart can
never be mistaken for a current one, and the rescue runs exactly once — the
rewrite replaces the eepromfs signature.

## Error handling

Every failure path lands on a defined state rather than propagating.

- **No EEPROM.** `present = false`; the session runs normally and nothing persists.
  `save_get()` returns zeroed defaults. This is existing behaviour and is preserved.
- **CRC mismatch.** Treated as corruption: defaults, then rewrite so the next boot
  is clean. Preferred over loading a damaged record, which could poison the
  best-altitude value permanently.
- **Failed write.** `eeprom_write` returns a status byte. `write_now()` only clears
  `dirty` when every block write succeeds, so a failed commit is retried at the
  next rest point instead of being silently dropped.
- **Payload length disagreement.** If the header's length byte does not match the
  size expected for the detected version, the cart is treated as
  `SAVE_LAYOUT_BLANK` — defaults, then rewrite.

## Write cadence

Unchanged from today: `save_commit()` is dirty-checked and called only at rest
points, never per frame. A commit now writes two blocks (header and payload)
rather than one, because the payload CRC lives in the header.

This doubles block-0 wear. EEPROM endurance is on the order of 10⁵ writes against
a handful of commits per session, so splitting the CRC out to avoid it is
optimisation the project does not need. Recorded as a deliberate decision, not an
oversight.

## Testing

### Host tests — `tests/save_format_test.c`

Added to `tests/run_tests.sh`, which compiles with
`gcc -std=c99 -O1 -Wall -Wextra -Werror`.

1. **CRC determinism** — same input yields the same value across calls.
2. **CRC sensitivity** — flipping any single byte changes the result.
3. **Header round-trip** — `save_header_build` then parse recovers version,
   length and CRC.
4. **Detect current** — a freshly built header classifies as `SAVE_LAYOUT_CURRENT`.
5. **Detect older** — a synthetic version-0 CRX header classifies as
   `SAVE_LAYOUT_OLDER` and reports version 0.
6. **Detect newer** — version 99 classifies as `SAVE_LAYOUT_NEWER`.
7. **Detect legacy** — six block-0 values sharing the prefix `65 65 70 01 00 08`
   but each carrying a *different* trailing CRC, all classify as
   `SAVE_LAYOUT_LEGACY`. Four are the checksums captured from real carts
   (`ede3`, `7f7f`, `551b`, `10bb`); two are invented. Proves the detector is
   independent of bytes 6–7.
8. **Detect blank** — all-zeroes, all-`0xFF`, and random bytes classify as
   `SAVE_LAYOUT_BLANK`.
9. **Payload validation** — a correct payload validates; a corrupted one does not.

### Acceptance test — the bug is actually dead

Host tests cannot prove the fix, because the failure is a linker-placement
effect. The end-to-end check:

1. Build the ROM, run it under gopher64, reach a nonzero altitude so a record
   is written. Note the `.eep` file under `~/.local/share/gopher64/saves/`.
2. Apply the `volatile const char[64]` pad to `src/input/input.c` and rebuild.
   Confirm the `eeprom_files` path pointer moved, by the same ELF inspection
   used to diagnose this.
3. Copy the old `.eep` to the new ROM hash's filename. gopher64 keys saves by
   ROM hash, so this step is what simulates one physical cart being reflashed —
   without it the emulator would present a blank EEPROM and the test would pass
   vacuously.
4. Run the new ROM. **The record must still be there.** Under the current code
   it would be wiped.
5. Revert the pad and rebuild.

Step 3 is the load-bearing step; a version of this test that skips it proves
nothing.

### Legacy rescue test

Copy one of the four captured pre-fix `.eep` images to the new build's hash
filename, boot, and confirm the 19 m record and "AAA" initials survive and that
block 0 is rewritten with the `CRX` magic.

## Migration path for stage 2

Stage 2 adds settings. With this container in place that becomes:

- Bump `SAVE_VERSION` to 2 and extend the payload with `settings_t`.
- Add a `migrate_from(1)` that copies the version-1 `save_data_t` forward and
  fills settings with defaults.
- Existing records survive. No wipe.

This is the concrete reason the fix comes first rather than being folded in.
