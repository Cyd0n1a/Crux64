# Settings Screen — Design

Stage 2 of the pause shell. Stage 1 (`2026-07-31-pause-shell-design.md`) built
a pause menu with two action rows and deferred every setting to this document.

## Problem

The game has no way to change anything. Volume, camera feel, rumble and the
stamina readout are all compile-time constants. GDD 4 requires at minimum an
options toggle for the stamina bar, and stage 1's spec committed the title
screen's entry point to this stage: "it arrives in stage 2 alongside the
settings it opens."

Nothing persists either. `save_data_t` is exactly eight bytes with every byte
spoken for, so settings cannot live inside the existing record.

## Outcome

A settings screen reachable from both the pause menu and the title, whose
values apply the moment they change and survive a power cycle.

## Scope

In scope: music volume, sound volume, camera invert X, camera invert Y, camera
sensitivity, rumble enable, stamina-bar enable. A title-screen menu to reach
them from. Persistence in a second EEPROM container.

Out of scope: initials editing and save management (stage 3); any change to
`save_data_t`, to the progress container's on-cart layout, or to the dev HUD
overlay.

## Storage

Settings get their **own container** at blocks 2–3, structurally identical to
the progress container at blocks 0–1 but independent of it:

```
block 0   'C','R','X', ver, crc_hi, crc_lo, len, rsvd    progress header
block 1   save_data_t                                    progress payload
block 2   'C','R','X', ver, crc_hi, crc_lo, len, rsvd    settings header
block 3   save_settings_t                                settings payload
```

This was chosen over growing the payload to version 2. Growing it would make
every progress commit a three-block write — roughly 18 ms against a 16.7 ms
frame budget — and those commits fire mid-gameplay at piton placement
(`main.c:404`). It would also rewrite the settings bytes on every piton for no
reason. With two containers each commit stays at today's two blocks, and the
two records wear independently.

The choice also removes the migration entirely. A cart written before this
change has nothing at block 2, `save_format_detect` returns `SAVE_LAYOUT_BLANK`,
and settings fall back to defaults with the progress record untouched. There is
no `migrate_from` to write, and none to get wrong.

### The record

```c
typedef struct __attribute__((packed)) {
    uint8_t music_vol;   /* 0..10 */
    uint8_t sfx_vol;     /* 0..10 */
    uint8_t cam_sens;    /* 0..4, index into SENS_TABLE */
    uint8_t flags;       /* bit0 invert_x, bit1 invert_y,
                            bit2 rumble, bit3 stamina_bar */
    uint8_t reserved[4]; /* zeroed by defaults(); never cleared on store */
} save_settings_t;

_Static_assert(sizeof(save_settings_t) == 8,
               "save_settings_t must be one 8-byte EEPROM block");
```

The four reserved bytes are deliberate: stage 3 can claim them **without a
version bump**, provided zero is the correct default for whatever lands there.
Two rules make that work, and both must hold:

- Reserved bytes are zeroed **only** by `settings_data_defaults()`. A record
  loaded from the cart is stored back verbatim — the pack path copies the whole
  struct and never re-zeroes the tail. Without this, an older build that read a
  newer cart would silently destroy the newer build's settings the first time
  the player changed anything.
- The CRC covers all eight payload bytes regardless of which are meaningful, so
  an older build validates a newer cart's payload without understanding it.

A future setting whose sensible default is nonzero is not covered by this and
requires a version bump.

Defaults, applied on a blank or corrupt container:

| field | default | note |
|---|---|---|
| `music_vol` | 7 | of 10 |
| `sfx_vol` | 7 | of 10 |
| `cam_sens` | 2 | `1.00x` |
| `invert_x` | off | |
| `invert_y` | off | |
| `rumble` | **on** | GDD 1.3 makes the Rumble Pak mandatory hardware |
| `stamina_bar` | **on** | see "Deliberate departures" |

`SENS_TABLE` is `{ 0.50f, 0.75f, 1.00f, 1.50f, 2.00f }`.

## The `save_format.c` signature change

This is forced by the two-container decision, not incidental to it. The
function as shipped hardcodes both a version and a legacy check:

```c
if (v == SAVE_VERSION) return SAVE_LAYOUT_CURRENT;          /* line 45 */
...
if (memcmp(block0, legacy_sig, sizeof legacy_sig) == 0)     /* line 49 */
```

Two containers with independent version lineages cannot share one compile-time
`SAVE_VERSION`: the moment settings move to version 2 while progress stays at
1, that comparison is wrong for one of them. And `legacy_sig` — the pre-fix
eepromfs signature `65 65 70 01 00 08` — is meaningful only at block 0. Run
against the settings container it is a misclassification path that could adopt
arbitrary bytes as a legacy record.

Both become parameters:

```c
save_layout_t save_format_detect(const uint8_t block0[SAVE_BLOCK_SIZE],
                                 uint8_t expect_version,
                                 bool allow_legacy,
                                 uint8_t *out_version, uint8_t *out_len);
```

`SAVE_VERSION` splits into `SAVE_PROGRESS_VERSION 1` and
`SAVE_SETTINGS_VERSION 1`. Progress calls with `allow_legacy = true`, settings
with `false`. Every call site is caught by the compiler, and the twelve
existing host tests are updated in place.

## Modules

| Module | Responsibility | libdragon |
|---|---|---|
| `src/meta/save_format.{h,c}` | container format: CRC, header build, classification | no |
| `src/meta/save_container.{h,c}` | **new** — read/write a two-block container at a base block | yes |
| `src/meta/save.{h,c}` | progress record | yes |
| `src/meta/settings_data.{h,c}` | **new** — settings logic and row table | no |
| `src/meta/settings.{h,c}` | **new** — settings storage and side effects | yes |
| `src/meta/menu_nav.{h,c}` | cursor and repeat timing, now two axes | no |
| `src/meta/menu.{h,c}` | screen shell and drawing | yes |

`save_container` exists so the torn-write ordering — payload block before
header block, so an interrupted write leaves a detectable CRC mismatch rather
than a half-updated record — is written once rather than duplicated in both
`save.c` and `settings.c`:

```c
save_layout_t save_container_load(int base_block, uint8_t expect_version,
                                  bool allow_legacy,
                                  uint8_t *payload, uint8_t len);
bool save_container_store(int base_block, uint8_t version,
                          const uint8_t *payload, uint8_t len);
```

`save_container_load` writes `payload` **only** when it returns
`SAVE_LAYOUT_CURRENT` with a header length matching `len` and a validated CRC.
On every other outcome — blank, corrupt, newer, or a length disagreement — it
leaves the buffer untouched. Callers therefore fill their defaults first and
call load over the top, so a failed load needs no recovery branch. `save.c`'s
legacy path is the one exception and stays in `save.c`, since only the progress
container has a legacy layout to adopt.

`save.c` slims onto these; its boot flow, legacy rescue and public API are
otherwise unchanged.

The settings split follows the `menu_nav` / `save_format` precedent that has
worked twice now: everything with logic in it goes in the libdragon-free unit,
and the shell holds only storage and side effects.

- `settings_data.c` is **stateless**. It exposes the defaults, the row table,
  and pure functions over a caller-owned record: `settings_data_defaults()`,
  `settings_data_get(const save_settings_t *, int id)`,
  `settings_data_adjust(save_settings_t *, int id, int delta)` (clamped, never
  wrapping), and `settings_data_rows(int *count)`. All host-tested.
- `settings.c` owns `static save_settings_t cur;`, loads and stores it through
  `save_container`, wraps the pure functions as the menu's callbacks, and calls
  `apply()`.

## Menu shell

Rows stay fully data-driven — the row table gains value rendering as *data*
rather than as display callbacks:

```c
typedef enum { MENU_ACTION, MENU_TOGGLE, MENU_SLIDER } menu_kind_t;

typedef struct {
    const char *label;
    menu_kind_t kind;
    int         id;      /* ACTION: a menu_result_t; TOGGLE/SLIDER: a setting id */
    int         vmax;    /* TOGGLE: 1; SLIDER: top of range */
    const char *const *value_labels;  /* vmax+1 strings, or NULL to draw a bar */
} menu_item_t;
```

So `CAMERA X` carries `{"NORMAL","INVERTED"}`, `SENSITIVITY` carries
`{"0.50x","0.75x","1.00x","1.50x","2.00x"}`, and the two volumes pass `NULL`
and render as `[#######---]`.

`menu_screen_t` gains what a screen needs to be self-describing:

```c
typedef struct {
    const char        *title;    /* NULL draws no title bar */
    const menu_item_t *items;
    int                count;
    bool               scrim;    /* dim the world behind (pause yes, title no) */
    int              (*get)(int id);
    void             (*set)(int id, int delta);
    void             (*on_close)(void);   /* NULL for screens with nothing to flush */
} menu_screen_t;
```

`settings.c` supplies `get`, `set` and `on_close`. This is what preserves
`menu.h`'s stage-1 rule: the menu still never includes `settings.h`, and stays
host-testable and reusable.

Screen depth is exactly one (pause → settings, title → settings), so `menu.c`
holds a single `return_to` pointer, not a general stack. Building a stack for a
depth of one would be the same speculative work stage 1 declined.

**Horizontal navigation** goes into `menu_nav_t` as a second axis with its own
`last_dir` and `repeat_t`, adjusted through `menu_nav_step_h()` and host-tested
beside the vertical axis — the same reasoning that put the vertical logic there
in stage 1. Left and right adjust the highlighted row; on an `MENU_ACTION` row
they do nothing.

**Panel geometry** becomes computed rather than fixed, since a screen may now
carry three rows or eight: width fixed at x 48–272, height derived from the row
count and vertically centred. Eight rows plus a title yields a 150 px panel at
y 45–195, inside the overscan-safe area.

## Screens

**Pause** — `RESUME` / `SETTINGS` / `QUIT TO TITLE`. The quit confirmation is
unchanged.

**Title** — `BEGIN CLIMB` / `SETTINGS`, no scrim, no title bar (the logo is
already drawn behind it), cursor defaulting to `BEGIN CLIMB`. Confirmed by
**A or Start**, so pressing Start on boot still begins the climb exactly as it
does today. `menu_result_t` gains `MENU_BEGIN_CLIMB`; `main.c`'s title branch
drives `menu_update`/`menu_draw` and acts on that result in place of its
current bare `if (in->start_btn)`.

**Settings** — the seven rows plus `BACK`. B or Start returns to whichever
screen opened it, as does confirming `BACK`.

`menu_result_t` distinguishes results that leave the menu from navigation that
stays inside it. `MENU_RESUME`, `MENU_QUIT_TITLE` and `MENU_BEGIN_CLIMB` are
returned to `main.c`. `MENU_SETTINGS` and `MENU_BACK` are action ids handled
entirely within `menu.c` — they push and pop `return_to` and return
`MENU_NONE`, so `main.c` never learns that a screen changed. `on_close` fires
on the pop, before the screen pointer moves, which is where `settings.c`
flushes to EEPROM.

## Applying the settings

Values apply **live**, on the frame they change, so the player hears and feels
what they are adjusting. Everything is pushed to its consumer rather than
polled, matching the rule stage 1's spec set for the audio path: `music_sample`
runs once per output sample and must never poll anything.

| Setting | Applied at |
|---|---|
| music volume | `synth_set_gains()` → static in `synth.c`, multiplied into the music term of the final mix |
| sound volume | `synth_set_gains()` → static, multiplied into the diegetic `sample` term |
| camera invert X/Y, sensitivity | `main.c:364-365`, sign and scale on the two accumulate lines |
| rumble | `rumble_set_enabled()` → early-out inside `rumble_kick`, covering all ten call sites at once |
| stamina bar | `main.c:491`, `.stam` gated to NULL when off |

Gating rumble inside the driver rather than at the call sites is what keeps
`rumble.c` free of a `settings.h` include and stops the ten `rumble_kick` calls
in `main.c` from each needing a condition.

Two traps this arrangement avoids, called out so they are not reintroduced:

- **Camera inversion must not reach menu navigation.** `menu.c:77-78` reads
  `in->cam_y` to move the cursor. Inversion is applied only on `main.c`'s two
  camera-accumulate lines, never in `input.c`, so an inverted-Y player still
  navigates menus normally.
- **Sound volume covers the whole diegetic layer**, not just event SFX — the
  `sample` term carries wind, the drone bed and the heartbeat too. A player who
  sets it to 0 loses the heartbeat and grip-failure cues that GDD 2.2 treats as
  primary stamina feedback. That is the player's choice to make, the same as
  turning rumble off, and the row is labelled `SOUND` rather than `SFX` so it
  does not read as affecting only one-shots.

## Persistence cadence

EEPROM is written **once, when the settings screen closes**, and only if a
value actually changed. A slider held left must never write per frame: each
`eeprom_write` costs about 6 ms and consumes endurance.

Two consequences to accept deliberately:

- The two-block write blocks the CPU for roughly 12 ms on the closing frame,
  which stalls `synth_poll` on a frame where `main.c:290-292` requires audio to
  keep running. A click or brief dropout on that transition is plausible. The
  write happens before that frame renders so the stall lands in one place, and
  the transition is on the manual-verification list.
- `eeprom_write` asserts on a nonzero status (`eeprom.c:80`) rather than
  returning it, so a write failure halts the ROM. The return value is still
  checked, for the same reason `save.c` checks it: the check is correct and
  free, not reachable today.

**Music ducking.** The pause menu ducks music to 0.25. The settings screen
lifts the duck to 1.0 while it is open, because a volume slider judged through
a duck is not judged at all, and restores 0.25 on return to the pause menu.
`main.c` sets this each frame from `menu_settings_open()`; `music_set_duck`
glides over ~80 ms, so repeating the same value is free and the transition does
not click.

## Testing

Host tests, run by `tests/run_tests.sh` with `gcc -std=c99 -O1 -Wall -Wextra
-Werror`:

- `tests/settings_test.c` — defaults are what the table says; `adjust` clamps
  at both ends and never wraps; every flag bit round-trips independently
  through pack and unpack; the row table's `vmax` matches each field's real
  range, and every row with `value_labels` supplies exactly `vmax + 1` of them.
- `tests/menu_nav_test.c` — the horizontal axis, mirroring the vertical cases:
  immediate first step, `MENU_NAV_DELAY` before repeat, `MENU_NAV_REPEAT`
  thereafter, and the two axes stepping independently without cross-talk.
- `tests/save_format_test.c` — updated for the new signature, plus
  `expect_version` driving CURRENT/OLDER/NEWER against a version the caller
  chooses, and a legacy block-0 image classified `BLANK` when
  `allow_legacy = false`.

On cart, four checks that host tests cannot reach:

1. Change every setting, quit, power cycle, confirm all seven survive.
2. **The migration:** boot a pre-settings cart — one with a valid progress
   container and nothing at block 2. Settings come up at defaults, the progress
   record is intact. `tests/fixtures/` gains a fixture image for this.
3. Corrupt block 3 by hand; confirm settings reset to defaults and the progress
   record is untouched.
4. Listen to the settings-screen exit for the write stall.

## Deliberate departures

**The stamina bar ships on.** GDD 4 says "No Stamina Bar By Default ... A
toggle in the options menu enables it." This design ships it enabled and lets
the toggle turn it off. That is a departure, made knowingly: the bars are the
readout used while tuning stamina, and flipping the default changes what every
playtest looks like. Revisiting it is a one-line default change plus a fixture
update, and it belongs in the pass that decides the game's shipping presentation
rather than in the pass that builds the menu.

## Sequencing

`save_commit()` has never executed on hardware or in emulation — every
`write_now()` so far has run from inside `save_init`. This stage adds a second
write path on top of it, so the outstanding manual check (start a run, plant a
piton with Z, quit, relaunch, confirm the record persisted) should be done
before stage-2 code lands. Otherwise a failure here is ambiguous between the
new settings container and the untested progress write.
