#pragma once

#include <stdbool.h>

/* Streaming MP3 music (GDD 3.3's sole authored-asset exception, via
 * minimp3). Two tracks live in the ROM filesystem — a title loop and an
 * in-game loop — and are decoded frame-by-frame straight off the cartridge
 * (the in-game track is far larger than the 8MB the Expansion Pak gives us,
 * so it cannot be held in RAM; it streams via DragonFS). Decoded PCM is
 * downmixed live into the synth's 22kHz output buffer (src/audio/synth.c),
 * so the wind, heartbeat and event SFX still sit on top. Each track loops
 * on end-of-file; a short fade-in hides the seam.
 *
 * If the filesystem can't be mounted the whole module goes quiet and the
 * synth's drone bed carries the ambience instead — nothing hard-depends on
 * a track being present. */

typedef enum {
    MUSIC_NONE = 0,
    MUSIC_TITLE,      /* title-screen.mp3 */
    MUSIC_GAME,       /* main-game-loop1.mp3 */
    MUSIC_TRACK_COUNT
} music_track_t;

bool music_init(void);              /* mount DFS; false if unavailable */
void music_play(music_track_t t);   /* (re)start a track, or MUSIC_NONE to stop */
void music_stop(void);
bool music_active(void);            /* true while a track is producing samples */

/* Pull the next stereo output sample (-1..1), decoding more of the stream
 * on demand. Writes 0 when nothing is playing. Called once per output
 * sample from the synth mix loop. */
void music_sample(float *l, float *r);
