#include "synth.h"
#include <libdragon.h>
#include <math.h>
#include <string.h>

/* GDD 3.3: 22kHz mono keeps a big margin of CPU for the sim while the
 * continuous layers run every sample. Output is duplicated L=R. */
#define SAMPLE_RATE 22050
#define MAX_VOICES  8

#define LUT_SIZE 1024               /* power of two: index masks cheaply */
#define LUT_MASK (LUT_SIZE - 1)
static float sine_lut[LUT_SIZE];

/* phase in turns (0..1); wraps by masking, no fmodf in the hot path. */
static inline float lut_sin(float phase) {
    int idx = (int)(phase * (float)LUT_SIZE);
    return sine_lut[idx & LUT_MASK];
}

/* --- noise + rng ------------------------------------------------------ */
static uint32_t noise_lfsr = 0xC0DEu;
static uint32_t rng_state  = 0x63727578u;   /* the fixed seed, for parity */

static inline float white(void) {
    /* 16-bit Galois LFSR, mapped to -1..1. */
    noise_lfsr = (noise_lfsr >> 1) ^ (-(noise_lfsr & 1u) & 0xB400u);
    return (float)(int)(noise_lfsr & 0xFFFF) * (1.f / 32768.f) - 1.f;
}
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static float frand(float a, float b) {
    return a + ((float)(rng() >> 8) / 16777216.f) * (b - a);
}
static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

/* --- one-shot voices -------------------------------------------------- */
enum { W_SINE, W_SAW, W_SQUARE, W_NOISE, W_BROWN };

typedef struct {
    int   active;
    int   pos, len;
    float phase;
    float f0, f1;          /* linear pitch slide across the voice's life */
    int   wave;
    float volume;
    float lp_y, lp_a, lp_decay;   /* one-pole low-pass + coefficient sweep */
    float brown_y;                /* brown-noise integrator state */
    float clip;                   /* rasp: pre-gain overdrive, 0 = clean */
    int   prio;
} voice_t;

static voice_t voices[MAX_VOICES];

static void voice_start(int prio, const voice_t *cfg) {
    int   steal = -1;
    float best  = -1.f;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) { steal = i; break; }
        if (voices[i].prio > prio) continue;
        float score = (float)(prio - voices[i].prio) * 10.f
                    + (float)voices[i].pos / (float)voices[i].len;
        if (score > best) { best = score; steal = i; }
    }
    if (steal < 0) return;   /* everything live outranks this sound */

    voices[steal]         = *cfg;
    voices[steal].active  = 1;
    voices[steal].pos     = 0;
    voices[steal].phase   = 0.f;
    voices[steal].lp_y    = 0.f;
    voices[steal].brown_y = 0.f;
    voices[steal].prio    = prio;
}

/* --- SFX definitions (GDD 3.3) ---------------------------------------- */

void synth_grunt(float effort) {
    /* Exertion voice: a low saw through a lowpass, pitch bending down and
     * rasping harder as stamina fails (GDD 3.3 "increasingly stressed").
     * A breath of filtered noise is layered under it. */
    effort = clampf(effort, 0.f, 1.f);
    float base  = frand(150.f, 185.f) - 45.f * effort;
    voice_t g = {
        .len = (int)(SAMPLE_RATE * (0.26f + 0.12f * effort)),
        .f0 = base * 1.08f, .f1 = base * 0.72f,
        .wave = W_SAW, .volume = 0.20f + 0.06f * effort,
        .lp_a = 0.32f + 0.30f * effort, .lp_decay = 0.99994f,
        .clip = 0.25f + 0.65f * effort,
    };
    voice_t breath = {
        .len = (int)(SAMPLE_RATE * 0.18f),
        .f0 = 1.f, .f1 = 1.f,
        .wave = W_BROWN, .volume = 0.06f + 0.05f * effort,
        .lp_a = 0.20f, .lp_decay = 1.f,
    };
    voice_start(3, &g);
    voice_start(2, &breath);
}

void synth_place(float vel) {
    /* Hand/foot onto rock: a short brown-noise thud; harder placements
     * are louder and brighter (GDD 3.3). */
    vel = clampf(vel, 0.f, 1.f);
    voice_t v = {
        .len = (int)(SAMPLE_RATE * (0.05f + 0.05f * vel)),
        .f0 = 1.f, .f1 = 1.f,
        .wave = W_BROWN, .volume = 0.14f + 0.14f * vel,
        .lp_a = 0.30f + 0.35f * vel, .lp_decay = 0.9994f,
    };
    voice_start(2, &v);
}

void synth_piton(void) {
    /* GDD 3.3: sharp metallic white-noise burst with a fast exponential
     * decay, layered with a low sine "thud". */
    voice_t metal = {
        .len = (int)(SAMPLE_RATE * 0.10f),
        .f0 = 1.f, .f1 = 1.f,
        .wave = W_NOISE, .volume = 0.30f,
        .lp_a = 0.92f, .lp_decay = 0.9990f,
    };
    voice_t thud = {
        .len = (int)(SAMPLE_RATE * 0.14f),
        .f0 = 128.f, .f1 = 70.f,
        .wave = W_SINE, .volume = 0.30f, .lp_a = 1.f, .lp_decay = 1.f,
    };
    voice_start(4, &metal);
    voice_start(4, &thud);
}

void synth_impact(float hard) {
    /* Body meeting rock — landing a fall or the rope snapping taut. A
     * descending sine thud plus a lowpassed noise slap. */
    hard = clampf(hard, 0.f, 1.f);
    voice_t thud = {
        .len = (int)(SAMPLE_RATE * (0.16f + 0.14f * hard)),
        .f0 = 90.f + 40.f * hard, .f1 = 34.f,
        .wave = W_SINE, .volume = 0.28f + 0.14f * hard,
        .lp_a = 1.f, .lp_decay = 1.f,
    };
    voice_t slap = {
        .len = (int)(SAMPLE_RATE * 0.12f),
        .f0 = 1.f, .f1 = 1.f,
        .wave = W_NOISE, .volume = 0.18f + 0.14f * hard,
        .lp_a = 0.45f, .lp_decay = 0.9988f,
    };
    voice_start(5, &thud);
    voice_start(5, &slap);
}

void synth_chalk(void) {
    /* Soft, dry exhale of chalk dust: gently filtered noise, low level. */
    voice_t v = {
        .len = (int)(SAMPLE_RATE * 0.16f),
        .f0 = 1.f, .f1 = 1.f,
        .wave = W_NOISE, .volume = 0.10f,
        .lp_a = 0.10f, .lp_decay = 0.9997f,
    };
    voice_start(1, &v);
}

/* --- continuous layers ------------------------------------------------ */
/* Setters latch a target; the mix loop slews toward it so parameter
 * jumps between frames never click. */
static float alt_tgt,  alt_cur;
static float str_tgt,  str_cur;
static float fall_tgt, fall_cur;

void synth_set_altitude(float a) { alt_tgt  = clampf(a, 0.f, 1.f); }
void synth_set_stress(float s)   { str_tgt  = clampf(s, 0.f, 1.f); }
void synth_set_falling(bool f)   { fall_tgt = f ? 1.f : 0.f; }

/* Wind: white noise through a lowpass whose cutoff a slow LFO sweeps,
 * minus a slower lowpass to thin it toward a howl. */
static float wind_lp, wind_lp2;
static float wind_lfo_ph, wind_gust_ph;

/* Ambient drone bed: a few detuned low partials of a minor chord with a
 * slow breathing tremolo — the placeholder for the minimp3 track. */
static float pad_ph[4];
static float pad_trem_ph;
static const float pad_freq[4] = { 55.00f, 65.41f, 82.41f, 110.00f };  /* A minor-ish */

/* Heartbeat: a 46Hz thud whose rate + volume climb with stress. */
static float hb_clock, hb_env, hb_ph;

static void render(short *buf, int nframes) {
    for (int i = 0; i < nframes; i++) {
        /* Slew continuous controls (~per-sample, tuned for a soft glide). */
        alt_cur  += (alt_tgt  - alt_cur)  * 0.0008f;
        str_cur  += (str_tgt  - str_cur)  * 0.0008f;
        fall_cur += (fall_tgt - fall_cur) * 0.0020f;

        float sample = 0.f;

        /* --- wind --- */
        wind_lfo_ph  += 0.09f  / SAMPLE_RATE;  if (wind_lfo_ph  >= 1.f) wind_lfo_ph  -= 1.f;
        wind_gust_ph += 0.023f / SAMPLE_RATE;  if (wind_gust_ph >= 1.f) wind_gust_ph -= 1.f;
        float cutoff = 0.06f + 0.05f * (lut_sin(wind_lfo_ph) * 0.5f + 0.5f);
        wind_lp  += cutoff * (white() - wind_lp);
        wind_lp2 += 0.004f * (wind_lp - wind_lp2);
        float howl = wind_lp - wind_lp2;                 /* crude band-pass */
        float gust = 0.55f + 0.45f * (lut_sin(wind_gust_ph) * 0.5f + 0.5f);
        float wind_amp = (0.10f + 0.32f * alt_cur) * gust + 0.55f * fall_cur;
        sample += howl * wind_amp;

        /* --- ambient drone bed --- */
        pad_trem_ph += 0.05f / SAMPLE_RATE;  if (pad_trem_ph >= 1.f) pad_trem_ph -= 1.f;
        float trem = 0.7f + 0.3f * (lut_sin(pad_trem_ph) * 0.5f + 0.5f);
        float pad = 0.f;
        for (int p = 0; p < 4; p++) {
            pad_ph[p] += pad_freq[p] / SAMPLE_RATE;
            if (pad_ph[p] >= 1.f) pad_ph[p] -= 1.f;
            pad += lut_sin(pad_ph[p]);
        }
        /* Bed ducks under a fall so the wind rush dominates. */
        sample += pad * 0.018f * trem * (1.f - 0.7f * fall_cur);

        /* --- heartbeat --- */
        if (str_cur > 0.33f) {
            float x   = (str_cur - 0.33f) / 0.67f;       /* 0..1 above onset */
            float bpm = 52.f + 108.f * x;
            hb_clock += (bpm / 60.f) / SAMPLE_RATE;
            if (hb_clock >= 1.f) { hb_clock -= 1.f; hb_env = 1.f; hb_ph = 0.f; }
            hb_ph  += 46.f / SAMPLE_RATE;  if (hb_ph >= 1.f) hb_ph -= 1.f;
            hb_env *= 0.99965f;                            /* ~0.13s thump */
            sample += lut_sin(hb_ph) * hb_env * (0.10f + 0.22f * x);
        } else {
            hb_env *= 0.99965f;
            hb_clock = 0.9f;                               /* primed to beat */
        }

        /* --- one-shot voices --- */
        for (int vi = 0; vi < MAX_VOICES; vi++) {
            voice_t *v = &voices[vi];
            if (!v->active) continue;

            float t    = (float)v->pos / (float)v->len;
            float freq = v->f0 + (v->f1 - v->f0) * t;
            v->phase += freq / SAMPLE_RATE;
            if (v->phase >= 1.f) v->phase -= 1.f;

            float osc;
            switch (v->wave) {
                case W_SINE:   osc = lut_sin(v->phase);                    break;
                case W_SAW:    osc = v->phase * 2.f - 1.f;                 break;
                case W_SQUARE: osc = v->phase < 0.5f ? 1.f : -1.f;         break;
                case W_BROWN:
                    v->brown_y = v->brown_y * 0.96f + white() * 0.04f;
                    osc = v->brown_y * 8.f;
                    break;
                default:       osc = white();                             break;  /* W_NOISE */
            }

            if (v->clip > 0.f) osc = clampf(osc * (1.f + v->clip * 4.f), -1.f, 1.f);

            v->lp_a *= v->lp_decay;
            v->lp_y += v->lp_a * (osc - v->lp_y);

            /* 4ms attack ramp kills the click, then a quadratic decay. */
            float atk = (float)v->pos / (SAMPLE_RATE * 0.004f);
            float env = (atk < 1.f ? atk : 1.f) * (1.f - t * t);
            sample += v->lp_y * env * v->volume;

            v->pos++;
            if (v->pos >= v->len) v->active = 0;
        }

        sample = clampf(sample, -1.f, 1.f);
        short s = (short)(sample * 26000.f);
        buf[i * 2]     = s;
        buf[i * 2 + 1] = s;
    }
}

void synth_init(void) {
    for (int i = 0; i < LUT_SIZE; i++)
        sine_lut[i] = sinf((float)i * (6.2831853f / (float)LUT_SIZE));
    memset(voices, 0, sizeof voices);
    hb_clock = 0.9f;
    audio_init(SAMPLE_RATE, 4);
}

void synth_poll(void) {
    while (audio_can_write()) {
        short *buf = audio_write_begin();
        render(buf, audio_get_buffer_length());
        audio_write_end();
    }
}
