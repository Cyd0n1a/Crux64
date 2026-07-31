# Pause Shell — Design

**Date:** 2026-07-31
**Status:** Approved, ready for implementation planning
**Stage:** 1 of 3

## Context

Crux64 has no way to pause. `main.c`'s frame loop runs splash → title →
cutscene → gameplay, and once gameplay starts the only exit is powering off.
`start_btn` is currently read in exactly two places — the splash skip
(`main.c:183`) and the title's "begin" (`main.c:211`) — so Start is free
during play.

This spec covers the **pause shell only**. The full options feature was
split into three stages so the risky EEPROM format change lands separately
from the fiddly input work, and so there is a working pause after stage one:

| Stage | Scope | Status |
|-------|-------|--------|
| **1** | Menu module, navigation, Resume / Quit to Title | **this spec** |
| 2 | Settings + persistence: audio gains, camera, rumble, 2nd EEPROM block | later |
| 3 | Cheats, save management, About | later |

Decisions already made that constrain stages 2–3, recorded here so they are
not re-litigated:

- Pause menu with Options **nested inside it**; the title screen opens that
  same Options screen directly. One module, two entry points.
- Settings persist in a new EEPROM block; **cheats are RAM-only** and reset
  every boot, so a forgotten cheat cannot become permanent.
- Enabling any cheat **taints the run**: altitude and falls stop being
  recorded and `save_commit` stops writing for the rest of that run. Clearing
  the taint requires a Quit to Title.
- Options is organised **two-level**: a category list, each opening its own
  panel.

## Goals

- Start pauses gameplay from any climber mode — on foot, climbing, mid-fall.
- The frozen world stays visible behind a dimmed scrim; the pause reads as a
  held breath, not a context switch.
- Resume returns to play exactly where it left off.
- Quit to Title resets the run cleanly enough that a second playthrough
  behaves like the first.

## Non-goals

- Any setting whatsoever. Stage 1's menu has two rows.
- A title-screen entry point. With no settings yet it would open an empty
  screen; it arrives in stage 2 alongside the settings it opens.
- Submenus and a screen stack. Stage 1 has one screen, so building the
  navigation depth now would be speculative.
- Pausing during cutscenes. That branch already spends `start_btn` on
  skipping the scene.

## Architecture

Two new modules under `src/meta/`, beside `dialogue.c`.

### `menu_nav.c` / `menu_nav.h` — pure navigation state

No libdragon dependency, so it compiles and unit-tests on the host with
`gcc`. Holds the cursor index and the repeat-delay timer; this is the
off-by-one-prone part of the feature and the part worth testing directly.

```c
typedef struct {
    int   cursor;
    int   count;
    float repeat_t;   /* time until the held direction repeats */
    int   last_dir;   /* -1, 0, +1 — the direction currently held */
    bool  lock;       /* swallow the frame that opened the menu */
} menu_nav_t;

void menu_nav_reset(menu_nav_t *n, int count);
/* dir: -1 up, +1 down, 0 none. Returns true when the cursor moved. */
bool menu_nav_step(menu_nav_t *n, int dir, float dt);
```

Timing: a fresh direction moves immediately, then waits **0.30 s** before
repeating every **0.12 s**. The cursor wraps at both ends.

### `menu.c` / `menu.h` — presentation and wiring

```c
typedef enum { MENU_ACTION } menu_kind_t;   /* TOGGLE/SLIDER arrive in stage 2 */

typedef struct { const char *label; menu_kind_t kind; int id; } menu_item_t;
typedef struct { const char *title; const menu_item_t *items; int count; } menu_screen_t;

typedef enum { MENU_NONE = 0, MENU_RESUME, MENU_QUIT_TITLE } menu_result_t;

void          menu_init(rdpq_font_t *font);
void          menu_open(void);
bool          menu_active(void);
menu_result_t menu_update(const input_state_t *in, float dt);
void          menu_draw(void);
```

**The boundary that matters:** `menu.c` includes only `input.h`, `menu_nav.h`
and libdragon — never `climber.h` or `save.h`. It returns *intent*
(`MENU_QUIT_TITLE`); `main.c` executes it. The menu never knows what a
climber is.

The item table is data-driven so stages 2 and 3 add `TOGGLE` and `SLIDER`
kinds without revisiting navigation, cursor or repeat logic.

## Input

| Input | Effect |
|-------|--------|
| Start (gameplay) | Open the menu |
| Start / B (row list) | Close and resume |
| D-pad up/down, stick up/down | Move cursor |
| A (row list) | Confirm the highlighted row |
| A / B (quit confirm) | Yes / cancel back to the row list — Start ignored |

`in->cam_y` comes from **held** buttons (`input.c:54`) and `stick_y` is a
continuous float, so neither is edge-triggered — hence `menu_nav`'s repeat
timer. Both feed the same timer, with `|stick_y| > 0.5` counting as a press,
so stick and d-pad cannot double-step each other.

`a_btn`, `b_btn` and `start_btn` come from `joypad_get_buttons_pressed` and
are already edge-triggered.

**The open-frame lock is mandatory.** `start_btn` is still set on the frame
that calls `menu_open()`; without swallowing it the menu opens and closes in
one press. This is the `g_lock` pattern from `dialogue.c:105`.

## Rows

Stage 1's single screen:

```
  ///  PAUSED  ///

  > RESUME
    QUIT TO TITLE
```

`QUIT TO TITLE` confirms **inline** rather than opening a second screen —
the row becomes `REALLY QUIT?  A = YES   B = NO`, defaulting to no. A
mis-press should not cost a climb in progress.

While that confirm is showing, the menu is in a distinct state and the
buttons mean different things: **B cancels the confirm** and returns to the
normal row list rather than closing the menu, and **Start is ignored
entirely** so a reflexive unpause cannot skip past a destructive choice. Only
A commits. The confirm state is cleared whenever the menu closes, so
reopening never lands mid-confirm.

## Integration

Three touch points.

**`main.c`** — open on Start in the gameplay path. While `menu_active()`,
skip `climber_update`, `weather_update` and `save_add_time`, but keep calling
`synth_poll`, `rumble_update` and `render_frame`. Freezing the sim is not
freezing the frame: the RDP still needs work each frame, and `synth_poll`
must keep running or the audio buffers underrun.

Quit sequence, executed by `main.c` on `MENU_QUIT_TITLE`:

```c
save_commit();          /* dirty-checked; banks this run's altitude */
climber_init();         /* re-entrant: stamina, pitons, chalk, camp */
weather_init();         /* back to dawn */
camp2_seen  = false;    /* scene 02 can fire again next run */
in_cutscene = false;
in_title    = true;
music_set_duck(1.f);
music_play(MUSIC_TITLE);
```

Both resets were verified, not assumed: `climber_init` re-assigns the whole
struct (`climber.c:885`) and re-places the camp deterministically;
`weather_init` is pure assignment back to dawn (`weather.c:8`). The mountain,
grips and scatter all derive from the fixed seed and never need regenerating.
`save_commit()` must come first — it only writes when dirty, but skipping it
would silently discard a record set during that run. Stage 3's cheat-taint
flag resets in this same block.

**`render.c`** — `render_hud_t` gains `bool menu`. The HUD tail draws the
world, then a dark scrim, then `menu_draw()`, skipping `draw_hud()` so the
stamina bars do not compete with the panel.

**`music.c`** — one new function, `music_set_duck(float)`, applied as a
separate multiplier inside `music_sample()`. Stage 2's user volume becomes a
*second* gain multiplied against it, so the slider and the duck never fight.
Gains are stored in statics and updated on change — `music_sample()` runs
once per output sample and must not poll anything.

The wind and heartbeat need no new API: `synth_set_altitude(0)` and
`synth_set_stress(0)` already do exactly this.

## Error handling and edge cases

- **Font.** The builtin font is a shared static buffer that cannot be loaded
  twice (`dialogue.c:10`). `menu_init()` takes the already-loaded HUD font
  handle, as `dialogue_init()` does, and registers **new** style ids (3 =
  selected, 4 = disabled) rather than reusing dialogue's 0–2.
- **Music duck restored on every exit.** Reset it in one place when the menu
  closes, whatever the reason, or the track stays quiet for the session.
- **No EEPROM.** `save_commit()` already no-ops when `present` is false
  (`save.c:31`), so the quit path is safe on a cart without one.
- **No dt spike on resume.** `main.c` recomputes `dt` per frame and clamps at
  0.1 s, so a long pause cannot dump accumulated time into the sim.
- **Paused time is not play time** — skipping `save_add_time` is deliberate.
- **Label hygiene.** `^` and `$` are rdpq_text escape characters and assert at
  draw time (CLAUDE.md). Menu labels are authored by us; this is a review
  check.
- **Bounds.** Guard `count == 0` and clamp the cursor so an empty screen
  cannot index out of bounds.

## Verification

1. **Host unit test** for `menu_nav` (`gcc`, no libdragon): cursor wrap,
   initial-delay vs repeat cadence, the open-frame lock, and stick/d-pad not
   double-stepping.
2. **Clean `bash build.sh`.**
3. **50 s boot smoke test** — expect `timeout` exit 124 and zero asserts.
4. **Manual emulator passes:** pause on foot, while climbing, and mid-fall;
   resume from each; Quit to Title and confirm stamina, pitons, chalk and
   camp reset, scene 02 can fire again, and music returns to full level.

Step 4 is the honest gap: there is no automated way to verify pause *feel*.
It will be reported as manually checked or not checked — never implied.

## Note for the open crash investigation

Paused, the render path runs every frame while the sim does not. That
separation is a bisection axis crash #4 has never been tested against. This
is not a reason to reopen that investigation now, and the design should not
be changed to chase it — but the property should not be designed away either.
