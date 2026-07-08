#include "dialogue.h"
#include "../audio/synth.h"

#include <libdragon.h>
#include <string.h>
#include <math.h>

/* Dialogue shares the debug-HUD mono font (id 1); it only adds extra
 * styles, which the HUD never uses (it prints with the default style 0).
 * The builtin font is a shared static buffer and can't be loaded twice. */
#define DLG_FONT      FONT_BUILTIN_DEBUG_MONO
#define STY_TEXT      0   /* body / speech (font default, white) */
#define STY_SPEAKER   1   /* warm amber name      */
#define STY_DIM       2   /* narration + prompts  */

/* Lower-third box (320x240 screen). */
#define BOX_X0   16
#define BOX_Y0   150
#define BOX_X1   304
#define BOX_Y1   228
#define PAD      10

/* Typewriter speeds (glyphs/sec) and the caption dwell. */
#define CPS_SPEECH     26.f
#define CPS_NARR       30.f
#define CPS_CAPTION    34.f
#define CAPTION_HOLD   2.2f

static const dlg_line_t *g_lines;
static int   g_count;
static int   g_idx;

static float g_reveal;     /* glyphs revealed so far (fractional) */
static int   g_shown;      /* floor(g_reveal) */
static int   g_blip_idx;   /* glyphs already ticked */
static float g_hold;       /* caption auto-advance timer */
static bool  g_lock;       /* swallow the button that launched the scene */
static bool  g_done;

/* Static-shimmer LCG (re-seeded per frame so the fleck pattern crawls). */
static uint32_t g_stat = 0x1234567u;
static inline uint32_t srnd(void) {
    g_stat = g_stat * 1664525u + 1013904223u;
    return g_stat;
}

void dialogue_init(rdpq_font_t *font) {
    /* Style 0 is the font default (white) the HUD already uses — leave it.
     * Add the two dialogue-only styles on top. */
    rdpq_font_style(font, STY_SPEAKER, &(rdpq_fontstyle_t){ .color = RGBA32(240, 196, 108, 255) });
    rdpq_font_style(font, STY_DIM,     &(rdpq_fontstyle_t){ .color = RGBA32(150, 158, 176, 255) });
}

void dialogue_start(const dlg_line_t *lines, int count) {
    g_lines = lines;
    g_count = count;
    g_idx   = 0;
    g_reveal = g_shown = g_blip_idx = 0;
    g_hold  = 0.f;
    g_lock  = true;
    g_done  = (count <= 0);
}

bool dialogue_active(void) { return !g_done; }

static float line_cps(dlg_style_t s) {
    return s == DLG_CAPTION ? CPS_CAPTION
         : s == DLG_SPEECH  ? CPS_SPEECH
                            : CPS_NARR;
}

static void next_line(void) {
    if (++g_idx >= g_count) { g_done = true; return; }
    g_reveal = 0.f;
    g_shown  = 0;
    g_blip_idx = 0;
    g_hold   = 0.f;
}

void dialogue_update(const input_state_t *in, float dt) {
    if (g_done) return;

    const dlg_line_t *L = &g_lines[g_idx];
    int len = (int)strlen(L->text);

    /* Grow the reveal, ticking a muffled blip for each freshly shown glyph
     * (skip whitespace so the cadence tracks the syllables, not the gaps). */
    g_reveal += dt * line_cps(L->style);
    if (g_reveal > (float)len) g_reveal = (float)len;
    g_shown = (int)g_reveal;
    while (g_blip_idx < g_shown) {
        char c = L->text[g_blip_idx++];
        if (c != ' ' && c != '\n' && c != '\t')
            synth_blip();
    }

    bool fully = g_shown >= len;

    /* Captions need no input: they type, dwell, then move on themselves. */
    if (L->style == DLG_CAPTION && fully) {
        g_hold += dt;
        if (g_hold >= CAPTION_HOLD) { next_line(); return; }
    }

    if (g_lock) { g_lock = false; return; }   /* eat the launching keypress */

    if (in->start_btn) { g_done = true; return; }   /* skip the whole scene */

    if (in->a_btn || in->b_btn) {
        if (!fully) {
            /* Snap the rest of the line up; don't machine-gun the blips. */
            g_reveal = (float)len;
            g_shown  = len;
            g_blip_idx = len;
        } else {
            next_line();
        }
    }
}

static void draw_static_strip(int x0, int y0, int x1) {
    /* A 4px header band of flickering flecks — an old TV losing signal. */
    rdpq_set_mode_fill(RGBA32(18, 20, 28, 255));
    rdpq_fill_rectangle(x0, y0, x1, y0 + 4);
    int w = x1 - x0;
    for (int i = 0; i < 34; i++) {
        int px = x0 + (int)(srnd() % (uint32_t)w);
        int py = y0 + (int)(srnd() % 4u);
        uint8_t g = 90 + (uint8_t)(srnd() % 130u);
        rdpq_set_fill_color(RGBA32(g, g, g, 255));
        rdpq_fill_rectangle(px, py, px + 1, py + 1);
    }
}

void dialogue_draw(void) {
    if (g_done) return;
    const dlg_line_t *L = &g_lines[g_idx];

    if (L->style == DLG_CAPTION) {
        /* Centred title card over a slim dark band, no box chrome. */
        rdpq_set_mode_fill(RGBA32(8, 9, 14, 255));
        rdpq_fill_rectangle(0, 158, 320, 200);
        rdpq_set_mode_standard();
        rdpq_text_printf(&(rdpq_textparms_t){
            .width = 320, .align = ALIGN_CENTER, .line_spacing = 4,
            .style_id = STY_TEXT, .max_chars = (int16_t)g_shown,
        }, DLG_FONT, 0, 168, "%s", L->text);
        return;
    }

    /* Box body + static header. */
    rdpq_set_mode_fill(RGBA32(12, 14, 22, 255));
    rdpq_fill_rectangle(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1);
    draw_static_strip(BOX_X0, BOX_Y0, BOX_X1);
    rdpq_set_mode_standard();

    int tx = BOX_X0 + PAD;
    int ty = BOX_Y0 + 10;

    if (L->style == DLG_SPEECH && L->speaker) {
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STY_SPEAKER },
                         DLG_FONT, tx, ty, "%s", L->speaker);
        ty += 12;
    }

    int16_t style = (L->style == DLG_SPEECH) ? STY_TEXT : STY_DIM;
    rdpq_text_printf(&(rdpq_textparms_t){
        .width = BOX_X1 - BOX_X0 - PAD * 2, .wrap = WRAP_WORD,
        .line_spacing = 2, .style_id = style, .max_chars = (int16_t)g_shown,
    }, DLG_FONT, tx, ty, "%s", L->text);

    /* Blinking advance chevron once the line has finished typing. */
    if (g_shown >= (int)strlen(L->text)) {
        float t = (float)((double)get_ticks_us() * 1e-6);
        if (fmodf(t, 0.9f) < 0.55f)
            rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STY_DIM },
                             DLG_FONT, BOX_X1 - 16, BOX_Y1 - 12, "> ");
    }
}
