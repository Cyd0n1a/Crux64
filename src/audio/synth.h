#pragma once

#include <stdbool.h>

/* Procedural audio subsystem (GDD 3.3). Every sound is synthesized into
 * the audio buffer in real time — no sampled assets. Three continuous
 * layers (wind, an ambient drone bed, and a stress heartbeat) are
 * modulated by gameplay state; one-shot SFX fire on climbing events.
 *
 * The GDD earmarks minimp3 for a composed background loop as its sole
 * authored-asset exception; until such a track exists the drone bed
 * stands in for it, and real music can later be mixed into the same
 * output buffer. Runs at 22kHz mono (GDD: downsample to spare CPU),
 * duplicated to both channels. A sine LUT keeps the per-sample cost off
 * the FPU so the heavy continuous synthesis fits inside a 30fps frame. */

void synth_init(void);
void synth_poll(void);      /* fill pending audio buffers; call once/frame */

/* Continuous state, pushed from the climber sim each frame. */
void synth_set_altitude(float alt01);   /* 0..1: higher = fiercer wind gusts */
void synth_set_stress(float stress01);  /* 0..1: heartbeat rate + volume */
void synth_set_falling(bool falling);   /* wind rushes up during a fall */

/* One-shot SFX, keyed off climbing events. */
void synth_grunt(float effort);   /* exertion voice; effort 0..1 bends pitch/rasp */
void synth_place(float vel);      /* hand/foot on rock: brown-noise thud */
void synth_piton(void);           /* metallic strike + low sine thud */
void synth_impact(float hard);    /* body thud on landing / rope catch */
void synth_chalk(void);           /* soft chalk exhale */
