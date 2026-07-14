#pragma once

#include <stdbool.h>

/* Procedural audio subsystem (GDD 3.3). Every sound is synthesized into
 * the audio buffer in real time — no sampled assets. Three continuous
 * layers (wind, an ambient drone bed, and a stress heartbeat) are
 * modulated by gameplay state; one-shot SFX fire on climbing events.
 *
 * The GDD earmarks minimp3 for a composed background loop as its sole
 * authored-asset exception; that track now exists (src/audio/music.c),
 * and its decoded PCM is mixed straight into this output buffer, so the
 * drone bed drops out whenever real music is playing and only covers for
 * a missing filesystem. Runs at 22kHz (GDD: downsample to spare CPU); the
 * diegetic layers are mono, the music stereo. A sine LUT keeps the
 * per-sample cost off the FPU so the synthesis fits inside a 30fps frame. */

void synth_init(void);
void synth_poll(void);      /* fill pending audio buffers; call once/frame */

/* Continuous state, pushed from the climber sim each frame. */
void synth_set_altitude(float alt01);   /* 0..1: higher = fiercer wind gusts */
void synth_set_stress(float stress01);  /* 0..1: heartbeat rate + volume */
void synth_set_falling(bool falling);   /* wind rushes up during a fall */
void synth_set_weather_wind(float w);   /* 0..1: environmental wind from weather */
void synth_set_weather_rain(float r);   /* 0..1: environmental rain noise */

/* One-shot SFX, keyed off climbing events. */
void synth_grunt(float effort);   /* exertion voice; effort 0..1 bends pitch/rasp */
void synth_place(float vel);      /* hand/foot on rock: brown-noise thud */
void synth_piton(void);           /* metallic strike + low sine thud */
void synth_impact(float hard);    /* body thud on landing / rope catch */
void synth_chalk(void);           /* soft chalk exhale */
void synth_blip(void);            /* dialogue typewriter tick: muffled speech-percussion */
