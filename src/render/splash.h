#pragma once
#include <libdragon.h>
#include <stdbool.h>

/* Boot splash sequence (ported from VoidStrider64, adapted for Crux64):
 * libdragon logo (drop/spin/fade in) -> crossfade to the Cydonis logo
 * (per-pixel column sine wave) -> crossfade to the game key art (fade +
 * gentle scale) -> fade into the live title scene.
 *
 * Audio is NOT owned here: Crux64 has no mixer, so the roar+jingle are a
 * single MP3 (MUSIC_SPLASH) streamed through src/audio/music.c like the rest
 * of the music. main.c starts MUSIC_SPLASH at boot and swaps in MUSIC_TITLE
 * once splash_title_music_due() fires (as the key art comes up).
 *
 * The authored sprites are a sanctioned exception to the "nothing stored"
 * pillar, contained to this module (like the music). */
void splash_init(void);

/* Advance the timeline; call once per frame (no-op once finished). */
void splash_update(float dt);

/* Abort the whole sequence (any-button skip). */
void splash_skip(void);

/* True while the splash owns the whole frame (render via splash_render).
 * The final fade renders as an overlay on the title scene instead. */
bool splash_fullscreen(void);
bool splash_finished(void);

/* True once the key art comes up: main.c starts the title-loop MP3 here so
 * it is already playing when the title screen appears. Latch it (stays true
 * for the rest of the sequence). */
bool splash_title_music_due(void);

/* Full-frame splash render: attaches, draws, presents. */
void splash_render(surface_t *disp);

/* Fade-to-title overlay; render.c calls this at the end of the title frame.
 * No-op outside the final fade-out window. */
void splash_draw_overlay(void);

/* Release the sprites + wave buffers once the sequence is done (they are
 * boot-only, and the 2x world leaves little RAM headroom). Idempotent; drains
 * the RDP first since it may still be reading them. Call after
 * splash_finished() goes true. */
void splash_free(void);
