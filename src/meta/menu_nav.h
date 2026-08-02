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
    float h_repeat_t; /* same, for the left/right value axis */
    int   h_last_dir; /* -1 left, +1 right, 0 released */
    bool  lock;       /* true until the opening frame's button is swallowed */
} menu_nav_t;

/* Cursor to the top, timers cleared, lock armed. */
void menu_nav_reset(menu_nav_t *n, int count);

/* dir: -1 up, +1 down, 0 none. Returns true on the frames the cursor moved.
 * A fresh direction moves at once; holding waits MENU_NAV_DELAY then repeats
 * every MENU_NAV_REPEAT. The cursor wraps at both ends. */
bool menu_nav_step(menu_nav_t *n, int dir, float dt);

/* dir: -1 left, +1 right, 0 none. Returns the amount to move the
 * highlighted row's value this frame: -1, 0 or +1. Same cadence as the
 * vertical axis — immediate on a fresh direction, then MENU_NAV_DELAY and
 * MENU_NAV_REPEAT — but with its own timer, so adjusting a value and
 * stepping rows in the same frame cannot swallow either. Values clamp
 * rather than wrap; that is the caller's business, not this function's. */
int menu_nav_step_h(menu_nav_t *n, int dir, float dt);

/* True exactly once after a reset — the frame that opened the menu, whose
 * button press must not also be read as a menu action. */
bool menu_nav_take_lock(menu_nav_t *n);
