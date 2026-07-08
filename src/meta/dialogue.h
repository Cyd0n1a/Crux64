#pragma once

#include <stdbool.h>
#include "../input/input.h"

/* Data-driven dialogue system (post-roadmap polish; the GDD's roadmap is
 * silent on story, so this sits alongside the scatter/music polish work).
 *
 * A scene is a flat array of lines the player clicks through. Each line
 * types out one glyph at a time behind a lower-third box styled like a
 * burst of TV static, with a muffled per-letter tick (synth_blip) standing
 * in for speech. A press of A/B reveals the rest of a still-typing line, or
 * advances to the next; Start skips the whole scene. Captions dwell and
 * auto-advance so the scene breathes without input.
 *
 * The engine is UI-only and holds no gameplay state, so the caller decides
 * when a scene runs (e.g. main.c plays the prologue once, at base camp,
 * before handing control to the climber). */

typedef enum {
    DLG_CAPTION,    /* centred lower-third title card; dwells then auto-advances */
    DLG_NARRATION,  /* boxed stage direction, no speaker, dim italic-feel */
    DLG_SPEECH,     /* boxed line with a speaker name */
    DLG_PROMPT,     /* boxed UI prompt, e.g. "[ Push forward to exit tent ]" */
} dlg_style_t;

typedef struct {
    const char *speaker;   /* NULL except for DLG_SPEECH */
    const char *text;
    dlg_style_t style;
} dlg_line_t;

void dialogue_init(void);   /* load + style the dialogue font; call once at boot */

/* Begin a scene. The lines array must outlive the scene (use static data). */
void dialogue_start(const dlg_line_t *lines, int count);

bool dialogue_active(void);                              /* false once the scene ends */
void dialogue_update(const input_state_t *in, float dt); /* once per frame while active */
void dialogue_draw(void);   /* call inside rdpq_attach, after the 3D pass */
