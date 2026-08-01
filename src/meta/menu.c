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

/* Centred panel, width fixed and height derived from the row count — a
 * screen may carry two rows or eight. Both stay inside the overscan-safe
 * area at 320x240. */
#define PANEL_X0   48
#define PANEL_X1  272
#define PAD        12
#define ROW_H      14
#define TITLE_H    (ROW_H + 6)
#define VALUE_X   (PANEL_X1 - PAD - 62)

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
    .panel_alpha = 255,
};

/* The title menu draws over the orbiting vista, so no scrim, no title bar
 * and a half-transparent panel — the logo and the vista read through it.
 * Start confirms instead of closing, which keeps Start-on-boot starting
 * the climb as it always has. */
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
    .panel_alpha = 128,
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

void menu_init(rdpq_font_t *font) {
    rdpq_font_style(font, STY_SEL, &(rdpq_fontstyle_t){ .color = RGBA32(240, 196, 108, 255) });
    rdpq_font_style(font, STY_DIM, &(rdpq_fontstyle_t){ .color = RGBA32(150, 158, 176, 255) });
}

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

void menu_draw(void) {
    if (!g_open || !g_screen) return;

    const bool titled = (g_screen->title != NULL);
    const int  rows   = g_confirm ? 2 : g_screen->count;
    const int  h      = PAD * 2 + rows * ROW_H + (titled ? TITLE_H : 0);
    const int  y0     = (SCREEN_H - h) / 2;

    /* Both rectangles blend, so the mode is set once. Same idiom as
     * splash.c's crossfade veil. Fill mode would be marginally cheaper for
     * an opaque panel but discards alpha outright, and the title panel
     * needs to be seen through. */
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    if (g_screen->scrim) {          /* dim the frozen world */
        rdpq_set_prim_color(RGBA32(0, 0, 0, 150));
        rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);
    }

    rdpq_set_prim_color(RGBA32(12, 14, 22, g_screen->panel_alpha));
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
