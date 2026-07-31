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
