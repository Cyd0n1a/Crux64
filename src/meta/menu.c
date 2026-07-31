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
