# Pause Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a pause menu to Crux64 — Start freezes gameplay behind a dimmed scrim and offers Resume or Quit to Title.

**Architecture:** Two new modules under `src/meta/`. `menu_nav.c` holds the pure cursor and repeat-delay state machine with no libdragon dependency, so it unit-tests on the host with `gcc`. `menu.c` holds presentation and returns *intent* (`MENU_RESUME` / `MENU_QUIT_TITLE`) which `main.c` executes — the menu never includes `climber.h` or `save.h`. Rows come from a data-driven table so stages 2 and 3 can add TOGGLE and SLIDER kinds without touching navigation.

**Tech Stack:** C99, libdragon (preview `07f1977bb`), tiny3d (`7f5773f64`), rdpq for 2D drawing, Docker build via `bash build.sh`.

**Spec:** `docs/superpowers/specs/2026-07-31-pause-shell-design.md`

## Global Constraints

- Build **only** via `bash build.sh` (Docker wrapper). Host LSP errors about missing `libdragon.h` / `t3d.h` are expected and are not build failures.
- Do **not** bump the `libdragon` or `tiny3d` submodules. They are pinned; `patches/` is re-applied over them by `build.sh`.
- `^` and `$` are rdpq_text escape characters — a bare one asserts at draw time. Write `^^` / `$$` for literals. All menu labels must avoid both.
- NEVER call `data_cache_hit_*` on an uncached pointer (`malloc_uncached`, `surface_alloc`). None of this plan needs cache maintenance.
- The builtin font is a **shared static buffer** — `rdpq_font_load_builtin` must not be called a second time. `menu_init()` receives the handle already loaded at `render.c:163`.
- Font style ids 0–2 are taken (0 = HUD/body, 1 = dialogue speaker, 2 = dialogue dim). The menu uses **3 = selected** and **4 = dim**.
- `menu.c` and `menu_nav.c` must not include `climber.h`, `save.h`, or any `sim/` header.
- Repeat timing is fixed by the spec: **0.30 s** initial delay, **0.12 s** repeat interval.
- Commit after every task. Do not bundle tasks into one commit.

## File Structure

| File | Responsibility |
|------|----------------|
| `src/meta/menu_nav.h` (create) | Cursor + repeat-delay types and constants. No libdragon. |
| `src/meta/menu_nav.c` (create) | Cursor movement, repeat timing, open-frame lock. Pure C. |
| `tests/menu_nav_test.c` (create) | Host unit tests for the above. |
| `tests/run_tests.sh` (create) | Compiles and runs the host tests with `gcc`. |
| `src/meta/menu.h` (create) | Menu item/screen types, `menu_result_t`, public API. |
| `src/meta/menu.c` (create) | Pause screen table, input handling, panel drawing. |
| `src/audio/music.h` (modify) | Declare `music_set_duck`. |
| `src/audio/music.c` (modify) | Duck gain, glided, multiplied into the existing `fade`. |
| `src/render/render.h` (modify) | `bool menu` on `render_hud_t`. |
| `src/render/render.c` (modify) | `menu_init(font)`; draw scrim + `menu_draw()` in the HUD tail. |
| `src/main.c` (modify) | Pause state, sim freeze, quit sequence. |
| `Makefile` (modify) | Add `menu_nav.o` and `menu.o` to `OBJS`. |

---

### Task 1: menu_nav — cursor and repeat-delay state

**Files:**
- Create: `src/meta/menu_nav.h`, `src/meta/menu_nav.c`
- Create: `tests/menu_nav_test.c`, `tests/run_tests.sh`
- Modify: `Makefile` (add `$(BUILD_DIR)/src/meta/menu_nav.o` to `OBJS`)

**Interfaces:**
- Consumes: nothing.
- Produces: `menu_nav_t`, `menu_nav_reset(menu_nav_t*, int count)`, `menu_nav_step(menu_nav_t*, int dir, float dt) -> bool`, `menu_nav_take_lock(menu_nav_t*) -> bool`, constants `MENU_NAV_DELAY` (0.30f) and `MENU_NAV_REPEAT` (0.12f). Task 3 consumes all of these.

- [ ] **Step 1: Create the header**

`src/meta/menu_nav.h`:

```c
#pragma once

#include <stdbool.h>

/* Pure cursor + repeat-delay state for the pause menu.
 *
 * Deliberately free of libdragon so it compiles on the host: the repeat
 * timing is the off-by-one-prone part of the menu, and tests/menu_nav_test.c
 * exercises it directly with gcc. Presentation lives in menu.c. */

#define MENU_NAV_DELAY   0.30f   /* seconds a held direction waits before repeating */
#define MENU_NAV_REPEAT  0.12f   /* seconds between repeats thereafter */

typedef struct {
    int   cursor;     /* highlighted row, always in [0, count) */
    int   count;      /* rows on the current screen */
    float repeat_t;   /* time left before the held direction repeats */
    int   last_dir;   /* -1 up, +1 down, 0 released */
    bool  lock;       /* true until the opening frame's button is swallowed */
} menu_nav_t;

/* Cursor to the top, timers cleared, lock armed. */
void menu_nav_reset(menu_nav_t *n, int count);

/* dir: -1 up, +1 down, 0 none. Returns true on the frames the cursor moved.
 * A fresh direction moves at once; holding waits MENU_NAV_DELAY then repeats
 * every MENU_NAV_REPEAT. The cursor wraps at both ends. */
bool menu_nav_step(menu_nav_t *n, int dir, float dt);

/* True exactly once after a reset — the frame that opened the menu, whose
 * button press must not also be read as a menu action. */
bool menu_nav_take_lock(menu_nav_t *n);
```

- [ ] **Step 2: Write the failing tests**

`tests/menu_nav_test.c`:

```c
/* Host unit tests for the pause menu's cursor and repeat-delay logic.
 * No libdragon and no N64 toolchain — run with tests/run_tests.sh. */
#include "../src/meta/menu_nav.h"

#include <stdio.h>

static int failures;

#define CHECK(cond) do {                                         \
    if (!(cond)) {                                               \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                              \
    }                                                            \
} while (0)

#define FRAME (1.f / 60.f)

static void test_reset_starts_at_top(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 2);
    CHECK(n.cursor == 0);
    CHECK(n.count == 2);
}

static void test_lock_fires_exactly_once(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 2);
    CHECK(menu_nav_take_lock(&n) == true);
    CHECK(menu_nav_take_lock(&n) == false);
    CHECK(menu_nav_take_lock(&n) == false);
}

static void test_fresh_press_moves_immediately(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 3);
    CHECK(menu_nav_step(&n, 1, FRAME) == true);
    CHECK(n.cursor == 1);
}

static void test_hold_does_not_repeat_before_delay(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 3);
    menu_nav_step(&n, 1, FRAME);          /* the immediate move */
    CHECK(n.cursor == 1);

    float held = 0.f;
    while (held < MENU_NAV_DELAY - 0.03f) {
        menu_nav_step(&n, 1, FRAME);
        held += FRAME;
    }
    CHECK(n.cursor == 1);                 /* still parked */
}

static void test_hold_repeats_without_machine_gunning(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 64);               /* large, so nothing wraps */
    int moves = 0;
    for (int i = 0; i < 60; i++)          /* one second at 60 fps */
        if (menu_nav_step(&n, 1, FRAME)) moves++;

    /* One immediate move, then ~(1.0 - 0.30) / 0.12 repeats. Asserted as a
     * range, not an exact count: the frame grid never lands squarely on the
     * timer. Catches both "never repeats" (1) and "moves every frame" (60). */
    CHECK(moves >= 5);
    CHECK(moves <= 8);
    CHECK(n.cursor == moves);
}

static void test_release_then_press_moves_immediately(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    menu_nav_step(&n, 1, FRAME);          /* cursor 1 */
    menu_nav_step(&n, 0, FRAME);          /* released */
    CHECK(menu_nav_step(&n, 1, FRAME) == true);
    CHECK(n.cursor == 2);
}

static void test_wraps_at_both_ends(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 2);
    menu_nav_step(&n, -1, FRAME);         /* up from the top row */
    CHECK(n.cursor == 1);
    menu_nav_step(&n, 0, FRAME);
    menu_nav_step(&n, 1, FRAME);          /* down from the bottom row */
    CHECK(n.cursor == 0);
}

static void test_empty_screen_is_safe(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 0);
    CHECK(menu_nav_step(&n, 1, FRAME) == false);
    CHECK(n.cursor == 0);
}

int main(void) {
    test_reset_starts_at_top();
    test_lock_fires_exactly_once();
    test_fresh_press_moves_immediately();
    test_hold_does_not_repeat_before_delay();
    test_hold_repeats_without_machine_gunning();
    test_release_then_press_moves_immediately();
    test_wraps_at_both_ends();
    test_empty_screen_is_safe();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all menu_nav checks passed\n");
    return 0;
}
```

`tests/run_tests.sh`:

```bash
#!/usr/bin/env bash
# Host-side unit tests. These do NOT need Docker or the N64 toolchain —
# every file under test is deliberately free of libdragon.
set -e
cd "$(dirname "$0")"
mkdir -p build
gcc -std=c99 -O1 -Wall -Wextra -Werror \
    -o build/menu_nav_test menu_nav_test.c ../src/meta/menu_nav.c -lm
./build/menu_nav_test
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `bash tests/run_tests.sh`

Expected: FAIL at link time — `undefined reference to 'menu_nav_reset'` (and `menu_nav_step`, `menu_nav_take_lock`). `menu_nav.c` does not exist yet.

- [ ] **Step 4: Write the implementation**

`src/meta/menu_nav.c`:

```c
#include "menu_nav.h"

void menu_nav_reset(menu_nav_t *n, int count) {
    n->cursor   = 0;
    n->count    = count;
    n->repeat_t = 0.f;
    n->last_dir = 0;
    n->lock     = true;
}

bool menu_nav_take_lock(menu_nav_t *n) {
    if (!n->lock) return false;
    n->lock = false;
    return true;
}

bool menu_nav_step(menu_nav_t *n, int dir, float dt) {
    if (n->count <= 0) {          /* nothing to point at */
        n->cursor = 0;
        return false;
    }
    if (n->cursor < 0)         n->cursor = 0;
    if (n->cursor >= n->count) n->cursor = n->count - 1;

    if (dir == 0) {               /* released: re-arm the immediate move */
        n->last_dir = 0;
        n->repeat_t = 0.f;
        return false;
    }

    bool move;
    if (dir != n->last_dir) {
        n->last_dir = dir;
        n->repeat_t = MENU_NAV_DELAY;
        move = true;
    } else {
        n->repeat_t -= dt;
        move = (n->repeat_t <= 0.f);
        if (move) n->repeat_t += MENU_NAV_REPEAT;
    }

    if (move) {
        n->cursor += dir;
        if (n->cursor < 0)         n->cursor = n->count - 1;
        if (n->cursor >= n->count) n->cursor = 0;
    }
    return move;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `bash tests/run_tests.sh`

Expected: `all menu_nav checks passed`, exit 0.

- [ ] **Step 6: Add to the ROM build**

In `Makefile`, add to `OBJS` immediately after the `prologue.o` line:

```make
    $(BUILD_DIR)/src/meta/menu_nav.o \
```

- [ ] **Step 7: Verify the ROM still builds**

Run: `bash build.sh`

Expected: `--- OK: libdragon install + tiny3d build + make ---` and a written `crux64.z64`. `menu_nav.c` appears in the `[CC]` list.

- [ ] **Step 8: Commit**

```bash
git add src/meta/menu_nav.h src/meta/menu_nav.c tests/menu_nav_test.c tests/run_tests.sh Makefile
git commit -m "menu: cursor and repeat-delay state machine with host tests"
```

---

### Task 2: Music ducking

**Files:**
- Modify: `src/audio/music.h` (append declaration)
- Modify: `src/audio/music.c:142-167` (`music_sample`) and the statics block at `:25-40`

**Interfaces:**
- Consumes: nothing.
- Produces: `void music_set_duck(float d)` — `d` in 0..1, clamped; 1.0 is full volume. Task 4 calls it.

- [ ] **Step 1: Declare the function**

In `src/audio/music.h`, add below `music_active()`:

```c
/* Pause ducking: scales the music down without touching the track state.
 * Glided over ~80ms inside music_sample so it never clicks. Kept separate
 * from the user volume that stage 2 adds — the two multiply, so a duck can
 * never fight the slider. d is clamped to 0..1; 1.0 is full volume. */
void music_set_duck(float d);
```

- [ ] **Step 2: Add the statics**

In `src/audio/music.c`, after the `fade` static at `:40`:

```c
static float duck = 1.f;        /* current ducking gain, glided toward... */
static float duck_target = 1.f; /* ...this, set by music_set_duck */
```

- [ ] **Step 3: Implement the setter**

Add near the other public functions in `src/audio/music.c`:

```c
void music_set_duck(float d) {
    if (d < 0.f) d = 0.f;
    if (d > 1.f) d = 1.f;
    duck_target = d;
}
```

- [ ] **Step 4: Apply it in the mix**

In `music_sample`, replace these two lines:

```c
    float g = fade * (1.f / 32768.f);
```

with:

```c
    /* Glide toward the duck target so pausing fades the music instead of
     * cutting it. Same shape as the fade-in above. */
    if (duck != duck_target) {
        float step = 1.f / (OUT_RATE * 0.08f);   /* ~80ms */
        if (duck < duck_target) {
            duck += step;
            if (duck > duck_target) duck = duck_target;
        } else {
            duck -= step;
            if (duck < duck_target) duck = duck_target;
        }
    }

    float g = fade * duck * (1.f / 32768.f);
```

- [ ] **Step 5: Verify the build**

Run: `bash build.sh`

Expected: OK, `music.c` recompiles, no warnings. (`OUT_RATE` is already defined in this file — it is used by the fade at `:161`.)

- [ ] **Step 6: Commit**

```bash
git add src/audio/music.h src/audio/music.c
git commit -m "audio: add glided music ducking gain"
```

---

### Task 3: The menu module

**Files:**
- Create: `src/meta/menu.h`, `src/meta/menu.c`
- Modify: `Makefile` (add `$(BUILD_DIR)/src/meta/menu.o` to `OBJS`)

**Interfaces:**
- Consumes: `menu_nav_t`, `menu_nav_reset`, `menu_nav_step`, `menu_nav_take_lock` from Task 1. `input_state_t` from `src/input/input.h`.
- Produces: `menu_result_t` (`MENU_NONE`, `MENU_RESUME`, `MENU_QUIT_TITLE`), `menu_init(struct rdpq_font_s *)`, `menu_open(void)`, `menu_active(void) -> bool`, `menu_update(const input_state_t *, float) -> menu_result_t`, `menu_draw(void)`. Task 4 calls all of them.

At the end of this task the module compiles into the ROM but nothing calls it. That is intentional — Task 4 wires it up.

- [ ] **Step 1: Create the header**

`src/meta/menu.h`:

```c
#pragma once

#include <stdbool.h>
#include "../input/input.h"

/* Pause menu (stage 1 of the options feature).
 *
 * Deliberately knows nothing about gameplay: it returns an INTENT and
 * main.c carries it out. Do not include climber.h or save.h here — the
 * menu must stay testable and reusable from the title screen in stage 2.
 *
 * Rows are a data-driven table so stage 2 can add TOGGLE and SLIDER kinds
 * without revisiting cursor or repeat handling. */

typedef enum {
    MENU_ACTION,        /* a row that fires an intent when A is pressed */
} menu_kind_t;

typedef enum {
    MENU_NONE = 0,      /* menu still open, nothing decided this frame */
    MENU_RESUME,        /* closed: hand control back to the climber */
    MENU_QUIT_TITLE,    /* closed: reset the run and return to the title */
} menu_result_t;

typedef struct {
    const char *label;
    menu_kind_t kind;
    int         id;     /* for MENU_ACTION: the menu_result_t to return */
} menu_item_t;

typedef struct {
    const char        *title;
    const menu_item_t *items;
    int                count;
} menu_screen_t;

/* Adds the menu's styles to the already-loaded HUD font. The builtin font
 * is a shared static buffer and must never be loaded twice, so the caller
 * owns the load and passes the handle (same contract as dialogue_init). */
struct rdpq_font_s;
void menu_init(struct rdpq_font_s *font);

void          menu_open(void);
bool          menu_active(void);
menu_result_t menu_update(const input_state_t *in, float dt);
void          menu_draw(void);   /* call inside rdpq_attach, after the 3D pass */
```

- [ ] **Step 2: Create the implementation**

`src/meta/menu.c`:

```c
#include "menu.h"
#include "menu_nav.h"

#include <libdragon.h>

/* Shares the HUD's mono font (id 1). Styles 0-2 are taken: 0 is the font
 * default the HUD prints with, 1 and 2 belong to the dialogue box. */
#define MENU_FONT   FONT_BUILTIN_DEBUG_MONO
#define STY_ROW     0   /* unselected row (font default, white) */
#define STY_SEL     3   /* highlighted row: warm amber */
#define STY_DIM     4   /* title bar and hints */

#define SCREEN_W  320
#define SCREEN_H  240

/* Centred panel. */
#define PANEL_X0   80
#define PANEL_Y0   76
#define PANEL_X1  240
#define PANEL_Y1  156
#define PAD        12
#define ROW_H      14

static const menu_item_t pause_items[] = {
    { "RESUME",        MENU_ACTION, MENU_RESUME     },
    { "QUIT TO TITLE", MENU_ACTION, MENU_QUIT_TITLE },
};

static const menu_screen_t pause_screen = {
    "PAUSED", pause_items,
    (int)(sizeof pause_items / sizeof pause_items[0]),
};

static bool       g_open;
static bool       g_confirm;   /* the quit confirmation is showing */
static menu_nav_t g_nav;

void menu_init(rdpq_font_t *font) {
    rdpq_font_style(font, STY_SEL, &(rdpq_fontstyle_t){ .color = RGBA32(240, 196, 108, 255) });
    rdpq_font_style(font, STY_DIM, &(rdpq_fontstyle_t){ .color = RGBA32(150, 158, 176, 255) });
}

void menu_open(void) {
    g_open    = true;
    g_confirm = false;
    menu_nav_reset(&g_nav, pause_screen.count);
}

bool menu_active(void) { return g_open; }

static menu_result_t close_with(menu_result_t r) {
    g_open    = false;
    g_confirm = false;
    return r;
}

menu_result_t menu_update(const input_state_t *in, float dt) {
    if (!g_open) return MENU_NONE;

    /* Swallow the frame that opened us: start_btn is edge-triggered and is
     * still set on this frame, so without this the menu opens and closes in
     * a single press. */
    if (menu_nav_take_lock(&g_nav)) return MENU_NONE;

    if (g_confirm) {
        /* A distinct state: B cancels rather than closing, and Start is
         * ignored so a reflexive unpause cannot skip a destructive choice. */
        if (in->a_btn) return close_with(MENU_QUIT_TITLE);
        if (in->b_btn) g_confirm = false;
        return MENU_NONE;
    }

    /* One direction from both inputs, so d-pad and stick cannot double-step.
     * cam_y is +1 for d-pad up and stick_y is positive up; both mean "move
     * to the previous row". */
    int dir = 0;
    if      (in->cam_y > 0 || in->stick_y >  0.5f) dir = -1;
    else if (in->cam_y < 0 || in->stick_y < -0.5f) dir =  1;
    menu_nav_step(&g_nav, dir, dt);

    if (in->start_btn || in->b_btn) return close_with(MENU_RESUME);

    if (in->a_btn) {
        const menu_item_t *it = &pause_screen.items[g_nav.cursor];
        if (it->id == MENU_QUIT_TITLE) {   /* confirm before losing a run */
            g_confirm = true;
            return MENU_NONE;
        }
        return close_with((menu_result_t)it->id);
    }

    return MENU_NONE;
}

void menu_draw(void) {
    if (!g_open) return;

    /* Dim the frozen world. Same idiom as splash.c's crossfade veil. */
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, 150));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);

    /* Panel. */
    rdpq_set_mode_fill(RGBA32(12, 14, 22, 255));
    rdpq_fill_rectangle(PANEL_X0, PANEL_Y0, PANEL_X1, PANEL_Y1);
    rdpq_set_mode_standard();

    int tx = PANEL_X0 + PAD;
    int ty = PANEL_Y0 + 14;

    rdpq_text_printf(&(rdpq_textparms_t){
        .width = PANEL_X1 - PANEL_X0, .align = ALIGN_CENTER, .style_id = STY_DIM,
    }, MENU_FONT, PANEL_X0, ty, "%s", pause_screen.title);
    ty += ROW_H + 6;

    if (g_confirm) {
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STY_SEL },
                         MENU_FONT, tx, ty, "REALLY QUIT?");
        ty += ROW_H;
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STY_DIM },
                         MENU_FONT, tx, ty, "A = YES   B = NO");
        return;
    }

    for (int i = 0; i < pause_screen.count; i++) {
        bool sel = (i == g_nav.cursor);
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = sel ? STY_SEL : STY_ROW },
                         MENU_FONT, tx, ty, "%s %s",
                         sel ? ">" : " ", pause_screen.items[i].label);
        ty += ROW_H;
    }
}
```

- [ ] **Step 3: Add to the ROM build**

In `Makefile`, add to `OBJS` immediately after the `menu_nav.o` line from Task 1:

```make
    $(BUILD_DIR)/src/meta/menu.o \
```

- [ ] **Step 4: Verify the build**

Run: `bash build.sh`

Expected: OK. `menu.c` appears in the `[CC]` list and links with no warnings. Nothing calls it yet, so the ROM behaves exactly as before.

- [ ] **Step 5: Re-run the host tests**

Run: `bash tests/run_tests.sh`

Expected: still `all menu_nav checks passed`. This task did not touch `menu_nav.c`, so a failure here means an accidental edit.

- [ ] **Step 6: Commit**

```bash
git add src/meta/menu.h src/meta/menu.c Makefile
git commit -m "menu: pause screen module with data-driven rows"
```

---

### Task 4: Wire the pause into the frame loop

**Files:**
- Modify: `src/render/render.h` (add `bool menu` to `render_hud_t`)
- Modify: `src/render/render.c:163-165` (init) and the HUD tail near `:380-390`
- Modify: `src/main.c` (include, pause branch, quit sequence)

**Interfaces:**
- Consumes: everything from Tasks 1–3, plus `music_set_duck` from Task 2.
- Produces: the working feature. Nothing later depends on it.

- [ ] **Step 1: Add the HUD flag**

In `src/render/render.h`, add below the `cinematic` field:

```c
    bool  menu;          /* pause menu: dim the world, hide the dev HUD */
```

- [ ] **Step 2: Initialise the menu's font styles**

In `src/render/render.c`, add `#include "../meta/menu.h"` beside the existing `#include "../meta/dialogue.h"` at line 13. Then, in `render_init`, immediately after `dialogue_init(font);` at `:165`:

```c
    menu_init(font);
```

- [ ] **Step 3: Draw the menu in the HUD tail**

In `src/render/render.c`, change the HUD tail branch (near `:380`) from:

```c
    } else if (hud->cinematic) {
        /* Prologue: the base-camp scene plays behind the dialogue box; the
         * dev HUD stays hidden so the frame reads cinematic. */
        dialogue_draw();
    } else {
        draw_hud(hud);
    }
```

to:

```c
    } else if (hud->cinematic) {
        /* Prologue: the base-camp scene plays behind the dialogue box; the
         * dev HUD stays hidden so the frame reads cinematic. */
        dialogue_draw();
    } else if (hud->menu) {
        /* Paused: the frozen world dims behind the panel and the dev HUD
         * steps aside so the stamina bars don't compete with it. */
        menu_draw();
    } else {
        draw_hud(hud);
    }
```

- [ ] **Step 4: Include the menu in main.c**

In `src/main.c`, add beside the other meta includes (after `#include "meta/scene02.h"`):

```c
#include "meta/menu.h"
```

- [ ] **Step 5: Open the menu on Start, and freeze the sim**

In `src/main.c`, immediately **before** the `climber_update(in, cam_yaw, dt);` call, insert:

```c
        /* Pause. Start is free during gameplay (the title and splash consume
         * it in branches that have already continued by here). While the menu
         * is open the sim is frozen outright rather than ticked with dt = 0,
         * but the frame keeps rendering and synth_poll keeps running — the RDP
         * needs work every frame and the audio buffers underrun without it. */
        if (!menu_active() && in->start_btn) {
            menu_open();
            music_set_duck(0.25f);
        }

        if (menu_active()) {
            menu_result_t mr = menu_update(in, dt);

            if (mr != MENU_NONE)
                music_set_duck(1.f);    /* one place: every exit restores it */

            if (mr == MENU_QUIT_TITLE) {
                save_commit();          /* dirty-checked; banks this run */
                climber_init();         /* re-entrant: stamina, gear, camp */
                weather_init();         /* back to dawn */
                camp2_seen  = false;    /* scene 02 can fire again */
                in_cutscene = false;
                in_title    = true;
                music_play(MUSIC_TITLE);
                continue;
            }

            if (mr == MENU_NONE) {
                /* Still paused. Hold the camera where it was and keep the
                 * frame alive: the wind and heartbeat drop out, but
                 * synth_poll must still run or the audio buffers underrun. */
                synth_set_altitude(0.f);
                synth_set_stress(0.f);
                synth_set_falling(false);
                synth_poll();
                rumble_update(dt);

                T3DVec3 target = {{ cs->neck[0], cs->neck[1], cs->neck[2] }};
                float cp = cosf(cam_pitch);
                T3DVec3 eye = {{
                    target.v[0] + sinf(cam_yaw) * cp * cam_dist,
                    target.v[1] + sinf(cam_pitch) * cam_dist,
                    target.v[2] + cosf(cam_yaw) * cp * cam_dist,
                }};
                float ground = mountain_height(eye.v[0], eye.v[2]) + 0.6f;
                if (eye.v[1] < ground) eye.v[1] = ground;

                render_hud_t hud = { .menu = true, .rumble_ok = in->rumble_present };
                render_frame(&eye, &target, &hud);
                continue;
            }

            /* MENU_RESUME falls through into gameplay on this same frame.
             * Continuing here instead would render one frame with the menu
             * already closed and the dev HUD suppressed — a visible blink. */
        }

```

- [ ] **Step 6: Build**

Run: `bash build.sh`

Expected: OK, a written `crux64.z64`. If `camp2_seen` is reported undeclared, the insertion landed above its declaration — it is declared near `in_cutscene` at the top of `main`.

- [ ] **Step 7: Boot smoke test**

Run:

```bash
cd /home/ahscott/Projects/n64 && timeout 50 ./gopher64-linux-x86_64 \
    /home/ahscott/Projects/n64/Crux64/crux64.z64 > /tmp/pause_boot.log 2>&1; echo "exit: $?"
grep -icE "assert|crash|panic" /tmp/pause_boot.log
```

Expected: `exit: 124` (the timeout killed a still-running ROM — an earlier exit means it died) and `0` matches. Note this only proves the ROM still boots; with no input it sits on the title screen and never reaches the pause path.

- [ ] **Step 8: Manual verification**

There is no automated way to check pause behaviour. Run the ROM and confirm each, reporting honestly which were actually checked:

1. Start while on foot opens the panel; the world dims and freezes behind it.
2. Start while climbing opens it, and Resume returns to the same hold with stamina unchanged.
3. Start mid-fall opens it, and Resume continues the fall rather than snapping.
4. Up/down moves the cursor once per press, and holding repeats at a readable speed — not one row per frame.
5. Start and B both resume from the row list.
6. `QUIT TO TITLE` → A shows `REALLY QUIT?`; B cancels back to the rows; Start does nothing while it is showing.
7. Confirming returns to the title with music back at full volume; starting a new run has full stamina, pitons and chalk, and scene 02 can fire again.

- [ ] **Step 9: Commit**

```bash
git add src/render/render.h src/render/render.c src/main.c
git commit -m "feat: pause menu with resume and quit to title"
```

---

## Self-Review

**Spec coverage:**

| Spec requirement | Task |
|---|---|
| `menu_nav` pure module, 0.30 s / 0.12 s timing, wrap | 1 |
| Open-frame lock | 1 (state), 3 (consumed) |
| `menu.c` returns intent, no gameplay includes | 3 |
| Data-driven row table | 3 |
| Two rows: Resume, Quit to Title | 3 |
| Inline quit confirm; B cancels, Start ignored | 3 |
| Font handle reused, styles 3 and 4 | 3 (impl), 4 (init call) |
| Dimmed scrim over the frozen world | 3 (draw), 4 (branch) |
| `music_set_duck`, separate multiplied gain | 2 |
| Duck restored on every exit | 4 (single site) |
| Wind/heartbeat quiet via existing setters | 4 |
| Sim frozen, frame and `synth_poll` still running | 4 |
| Paused time not counted (`save_add_time` skipped) | 4 (the `continue` skips it) |
| Quit sequence incl. `save_commit` first, `camp2_seen` reset | 4 |
| No dt spike on resume | 4 (inherited: `main.c` clamps dt at 0.1 s) |
| Host unit tests | 1 |
| Build, boot smoke, manual passes | 1, 2, 3, 4 |

No gaps.

**Placeholder scan:** No TBD/TODO, no "add error handling", no "similar to Task N". Every code step carries the actual code.

**Type consistency:** `menu_nav_t` / `menu_nav_reset` / `menu_nav_step` / `menu_nav_take_lock` and the `MENU_NAV_DELAY` / `MENU_NAV_REPEAT` constants are spelled identically in Tasks 1 and 3. `menu_result_t` values `MENU_NONE` / `MENU_RESUME` / `MENU_QUIT_TITLE` match across Tasks 3 and 4. `music_set_duck` matches between Tasks 2 and 4. `render_hud_t.menu` matches between Steps 1 and 3 of Task 4.

**Known limitation:** Task 4's step 8 is manual. The pause *feel* — repeat cadence, dim level, resume fidelity — cannot be asserted automatically on this stack, and should be reported as checked or not checked, never implied.
