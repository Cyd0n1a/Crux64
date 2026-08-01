#pragma once

#include <stdbool.h>
#include <stddef.h>   /* NULL: row tables are written by host-compiled units */
#include "../input/input.h"

/* Pause and title menus.
 *
 * Deliberately knows nothing about gameplay or settings: rows return an
 * INTENT and main.c carries it out, and value rows reach their data
 * through the screen's get/set callbacks. Do not include climber.h,
 * save.h or settings.h here — the menu must stay testable and reusable.
 *
 * Screen depth is exactly one (pause -> settings, title -> settings), so
 * there is a single return_to pointer rather than a stack. */

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

    /* ACTION: the menu_result_t; else a setting id. The two id spaces
     * overlap numerically, so `kind` must gate every use of this field —
     * as menu_update and menu_draw do. An ungated get(it->id) would read
     * a BACK row as a setting. */
    int         id;
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

/* Adds the menu's styles to the already-loaded HUD font. The builtin font
 * is a shared static buffer and must never be loaded twice, so the caller
 * owns the load and passes the handle (same contract as dialogue_init). */
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
