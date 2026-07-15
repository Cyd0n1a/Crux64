/*
 *   ____ ____  _   ___  __ ____ _  _
 *  / ___|  _ \| | | \ \/ // ___| || |
 * | |   | |_) | | | |\  /| '_ \| || |_
 * | |___|  _ <| |_| |/  \| (_) |__   _|
 *  \____|_| \_\\___//_/\_\\___/   |_|
 *
 * Crux64 — physics-based procedural mountain climbing for the N64.
 * (c) 2026 Amanda Hariette-Scott and Cydonis Heavy Industries.
 *
 * Phase 6 (GDD 5.6): EEPROM save state. The run's max altitude, fall
 * count and play time persist across power-offs in a single 8-byte
 * eepromfs block (src/meta/save.c) — recorded in RAM each frame and
 * flushed only at rest points (piton checkpoints, the end of a fall).
 * The title screen shows the saved best.
 *
 * Music (GDD 3.3): the drone bed's planned replacement is here — two
 * composed MP3s (a title loop and an in-game loop) stream off the
 * cartridge and are decoded by minimp3 (src/audio/music.c), mixed live
 * into the synth buffer. The in-game track is bigger than RAM, so it
 * streams from the ROM filesystem rather than loading; each loops on
 * end-of-file, and the drone bed steps aside while music plays.
 *
 * Phase 5 (GDD 5.5): procedural audio. Every sound is synthesized live
 * (src/audio/synth.c) — no sampled assets. Wind howls fiercer with
 * altitude, a heartbeat quickens as the weakest grip fails (locked to
 * the rumble),
 * and one-shots fire on events: brown-noise placements, a metallic
 * piton strike, body-thud landings, and pitch-bent exertion grunts that
 * rasp harder the closer the climber is to peeling off.
 *
 * Phase 4 (GDD 5.4): posture, balance, stamina. Anchored limbs tire —
 * arms faster than legs, worse when overextended or off balance — and
 * peel when spent; lose the wall and you fall until the rope catches
 * on your last piton or you fetch up on walkable ground. Hold R with a
 * limb selected to shake it out, A chalks up, Z drives a piton (full
 * restore + fall checkpoint). Fatigue reads as limb shake and rising
 * rumble; the camera leans in on delicate holds.
 */

#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>
#include <stdio.h>

#include "version.h"
#include "input/input.h"
#include "input/rumble.h"
#include "audio/synth.h"
#include "audio/music.h"
#include "gen/mountain.h"
#include "gen/grips.h"
#include "gen/scatter.h"
#include "sim/climber.h"
#include "meta/save.h"
#include "meta/dialogue.h"
#include "meta/prologue.h"
#include "render/render.h"
#include "render/splash.h"
#include "render/sky_render.h"
#include "sim/weather.h"

int main(void) {
    /* GDD 1.3: Expansion Pak is a hard requirement (heightmap arrays,
     * display lists, audio buffers). libdragon halts with an error
     * screen if the full 8MB isn't present. */
    assert_memory_expanded();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    joypad_init();
    timer_init();

    debug_init_usblog();
    debug_init_emulog();
    if (debug_init_sdfs("sd:/", -1)) {
        debug_init_sdlog("sd:/crux64_crash.log", "a");
    }

    input_init();
    rumble_init();
    synth_init();
    save_init();
    /* Mount the ROM filesystem. If the FS can't be found, music stays silent
     * and the synth's drone bed covers ambience. The boot splash cues its own
     * track (MUSIC_SPLASH); the title loop starts as the splash hands over. */
    music_init();
    t3d_init((T3DInitParams){});

    long long gen_start = timer_ticks();
    mountain_generate();
    grips_generate();
    climber_init();     /* also pitches base camp, which scatter avoids */
    scatter_generate(); /* trees, rocks and boulders across the lower flanks */
    weather_init();
    float gen_ms = (float)TIMER_MICROS_LL(timer_ticks() - gen_start) / 1000.f;

    render_init();

    /* Boot splash: libdragon logo -> Cydonis logo -> game key art -> title.
     * Kick the roar+jingle track now (after gen, so it starts in sync with the
     * first splash frame rather than during the generation freeze). */
    splash_init();
    music_play(MUSIC_SPLASH);

    /* Follow camera: orbits the climber on the D-pad, seeded looking
     * at their back. GDD 3.1: zooms in as strain rises on delicate
     * holds, pulls back to showcase the peak when resting or falling. */
    const climber_t *cs = climber_state();
    float cam_yaw   = atan2f(cs->wall_n[0], cs->wall_n[2]);
    float cam_pitch = 0.25f;
    float cam_dist  = 5.6f;
    float pulse_t   = 0.f;   /* fatigue rumble pulse timer */
    float grunt_t   = 1.f;   /* spacing between sustained-effort grunts */

    /* Title screen: a slow right-to-left orbit around the whole massif
     * (camera strafes right, so the mountain drifts leftward on screen)
     * with the cube-letter logo floating in front. Start drops the
     * player into the run at base camp. */
    bool  in_title  = true;
    float title_ang = 0.f;

    /* Boot splash owns the frame until it hands over to the title. title_cued
     * latches the one-time swap from the splash track to the title loop. */
    bool  in_splash   = true;
    bool  title_cued  = false;
    bool  splash_done = false;   /* one-time free of the boot-splash assets */

    /* Prologue (Scene 01, "The Morning After"): a base-camp cutscene that
     * plays once on New Game — the camera holds the player's dawn view of
     * Mt. Xerxes while Maya's headset call types out in the dialogue box.
     * Skippable with Start; hands control to the climber when it ends. */
    bool  in_prologue = false;
    float scene_t     = 0.f;

    long long prev = timer_ticks();

    while (1) {
        long long now = timer_ticks();
        float dt = (float)TIMER_MICROS_LL(now - prev) / 1000000.f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        input_poll();
        const input_state_t *in = input_state();

        weather_update(dt);
        sky_update_time(weather_current()->time_of_day);

        /* Advance the boot splash every frame (no-op once finished) and swap
         * the splash track for the title loop the moment the key art comes up,
         * so music is already playing under the title screen. */
        splash_update(dt);
        if (splash_title_music_due() && !title_cued) {
            music_play(MUSIC_TITLE);
            title_cued = true;
        }
        if (splash_finished() && !splash_done) {
            splash_free();       /* boot-only sprites reclaimed for the run */
            splash_done = true;
        }

        if (in_splash) {
            bool skip_now = in->start_btn || in->a_btn || in->b_btn;
            if (skip_now) splash_skip();

            if (splash_fullscreen()) {
                /* Calm bed under the splash; the drone auto-silences while a
                 * track streams, leaving just faint wind. */
                synth_set_altitude(0.30f);
                synth_set_stress(0.f);
                synth_set_falling(false);
                synth_poll();
                rumble_update(dt);
                splash_render(display_get());
                continue;
            }

            /* Fullscreen phase over: hand control to the title. A skip press
             * is consumed here so it doesn't also trip the title's Start; a
             * natural finish falls through so the title frame renders with the
             * splash's fade-out overlay on top. */
            in_splash = false;
            if (skip_now) continue;
        }

        if (in_title) {
            title_ang += dt * 0.10f;
            T3DVec3 target = {{ 0.f, 105.f, 0.f }};
            T3DVec3 eye = {{ sinf(title_ang) * 360.f, 170.f,
                             cosf(title_ang) * 360.f }};
            if (in->start_btn) {
                in_title    = false;
                in_prologue = true;
                scene_t     = 0.f;
                int nlines;
                const dlg_line_t *lines = prologue_scene(&nlines);
                dialogue_start(lines, nlines);
                rumble_kick(0.4f, 0.2f);
                synth_chalk();
                music_play(MUSIC_GAME);   /* swap the title loop for the climb */
            }
            rumble_update(dt);

            /* High, exposed vista: gentle wind and the drone bed, no strain. */
            synth_set_altitude(0.45f);
            synth_set_stress(0.f);
            synth_set_falling(false);
            synth_poll();

            const save_data_t *sv = save_get();
            render_hud_t hud = {
                .title          = true,
                .rumble_ok      = in->rumble_present,
                .status         = NULL,
                .best_alt       = sv->max_altitude,
                .lifetime_falls = sv->falls,
                .initials       = sv->initials,
            };
            render_frame(&eye, &target, &hud);
            continue;
        }

        if (in_prologue) {
            scene_t += dt;

            /* Hold the player's first-person dawn view: stand at the neck,
             * look up the wall at the peak, with a slow cinematic push-in.
             * The mountain sits in front of the climber, opposite the wall
             * normal (which points outward, from rock to climber). */
            float fdx = -cs->wall_n[0], fdz = -cs->wall_n[2];
            float fl = sqrtf(fdx * fdx + fdz * fdz);
            if (fl > 1e-4f) { fdx /= fl; fdz /= fl; }

            float push = 4.0f - 1.2f * (1.f - expf(-scene_t * 0.15f));
            T3DVec3 eye = {{
                cs->neck[0] - fdx * push,
                cs->neck[1] + 1.9f + 0.4f * (1.f - expf(-scene_t * 0.1f)),
                cs->neck[2] - fdz * push,
            }};
            T3DVec3 target = {{
                cs->neck[0] + fdx * 42.f, 96.f, cs->neck[2] + fdz * 42.f,
            }};

            dialogue_update(in, dt);

            /* Calm base-camp ambience — gentle wind, the campfire flicker
             * lights the scene (render_frame drives the fire light). */
            rumble_update(dt);
            synth_set_altitude(0.18f);
            synth_set_stress(0.f);
            synth_set_falling(false);
            synth_poll();

            render_hud_t hud = {
                .cinematic = true,
                .rumble_ok = in->rumble_present,
            };
            render_frame(&eye, &target, &hud);

            if (!dialogue_active())
                in_prologue = false;   /* next frame: hand over to gameplay */
            continue;
        }

        climber_update(in, cam_yaw, dt);

        /* D-pad always orbits; on foot, holding Z hands the stick to
         * the camera too (the sim ignores it while Z is down). */
        bool cam_stick = cs->mode == CLIMBER_ON_FOOT && in->z_held;
        cam_yaw   += (in->cam_x + (cam_stick ? in->stick_x : 0.f)) * dt * 2.2f;
        cam_pitch += (in->cam_y + (cam_stick ? in->stick_y : 0.f)) * dt * 1.6f;
        if (cam_pitch < -0.45f) cam_pitch = -0.45f;
        if (cam_pitch >  1.05f) cam_pitch =  1.05f;

        /* The weakest grip still holding — drives rumble + warnings. */
        float worst = 1.f;
        for (int l = 0; l < LIMB_COUNT; l++)
            if (cs->limbs[l].grip >= 0 && cs->stam[l] < worst)
                worst = cs->stam[l];

        /* Haptics: one kick per climbing event... */
        if (cs->snapped)     rumble_kick(0.5f, 0.18f);
        if (cs->snap_failed) rumble_kick(0.25f, 0.30f);
        if (cs->mounted)     rumble_kick(0.45f, 0.20f);
        if (cs->dismounted)  rumble_kick(0.3f, 0.15f);
        if (cs->piton_set)   rumble_kick(1.0f, 0.35f);
        if (cs->chalked)     rumble_kick(0.2f, 0.15f);
        if (cs->peeled)      rumble_kick(0.7f, 0.35f);
        if (cs->fell)        rumble_kick(1.0f, 1.0f);
        if (cs->caught)      rumble_kick(1.0f, 0.5f);
        if (cs->landed)      rumble_kick(0.85f, 0.45f);

        /* ...and the matching procedural SFX (GDD 3.3). */
        if (cs->snapped)     synth_place(0.5f + 0.5f * cs->strain);
        if (cs->mounted)     synth_place(0.5f);
        if (cs->piton_set)   synth_piton();
        if (cs->chalked)     synth_chalk();
        if (cs->peeled)      synth_grunt(1.f);
        if (cs->fell)        synth_grunt(0.8f);
        if (cs->caught)      synth_impact(0.6f);
        if (cs->landed)      synth_impact(0.9f);

        /* Phase 6 (GDD 3.4): fold this run into the persistent record —
         * RAM-only every frame, flushed to EEPROM only at rest points
         * (a piton checkpoint, the end of a fall) so the slow write never
         * lands mid-move. */
        save_add_time(dt);
        save_note_altitude(cs->alt);
        if (cs->fell) save_note_fall();
        if (cs->piton_set || cs->fell || cs->landed || cs->caught)
            save_commit();

        /* ...plus GDD 2.2's progressive fatigue: light pulses quicken
         * as the worst grip tires, and once failure is a second or two
         * out the motor stays on. */
        if (cs->mode == CLIMBER_CLIMBING) {
            if (worst < 0.15f) {
                rumble_kick(0.8f, 0.3f);
            } else if (worst < 0.55f) {
                pulse_t -= dt;
                if (pulse_t <= 0.f) {
                    float x = (0.55f - worst) / 0.40f;
                    rumble_kick(0.15f + 0.30f * x, 0.12f);
                    pulse_t = 1.2f - 0.9f * x;
                }
            } else {
                pulse_t = 0.3f;
            }
        }
        rumble_update(dt);

        /* GDD 3.1: zoom in on delicate holds, pull back at rest and to
         * frame the whole tumble on a fall. */
        float dist_want = cs->mode == CLIMBER_FALLING
                        ? 7.6f : 5.6f - 2.4f * cs->strain;
        cam_dist += (dist_want - cam_dist) * fminf(1.f, 2.5f * dt);

        T3DVec3 target = {{ cs->neck[0], cs->neck[1], cs->neck[2] }};
        float cp = cosf(cam_pitch);
        T3DVec3 eye = {{
            target.v[0] + sinf(cam_yaw) * cp * cam_dist,
            target.v[1] + sinf(cam_pitch) * cam_dist,
            target.v[2] + cosf(cam_yaw) * cp * cam_dist,
        }};

        /* Keep the camera out of the rock. */
        float ground = mountain_height(eye.v[0], eye.v[2]) + 0.6f;
        if (eye.v[1] < ground) eye.v[1] = ground;

        /* Continuous audio (GDD 3.3): wind fiercer with altitude, the
         * heartbeat rising as the weakest grip fails, wind rushing on a
         * fall. Sustained hard holds draw involuntary grunts, spaced
         * tighter the closer the climber is to the edge. */
        float alt01 = cs->alt / 120.f;
        if (alt01 < 0.f) alt01 = 0.f;
        if (alt01 > 1.f) alt01 = 1.f;
        float stress = (0.5f - worst) * 2.f;
        if (stress < 0.f) stress = 0.f;

        if (cs->mode == CLIMBER_CLIMBING && cs->strain > 0.5f && !cs->shakeout) {
            grunt_t -= dt;
            if (grunt_t <= 0.f) {
                synth_grunt(0.3f + 0.6f * cs->strain);
                grunt_t = 1.8f - 0.9f * cs->strain;
            }
        } else {
            grunt_t = 0.6f;
        }

        synth_set_altitude(alt01);
        synth_set_stress(stress);
        synth_set_falling(cs->mode == CLIMBER_FALLING);
        synth_set_weather_wind(weather_current()->wind_strength);
        synth_set_weather_rain(weather_current()->rain_intensity);
        synth_poll();

        char status[64];
        if (cs->mode == CLIMBER_ON_FOOT)
            snprintf(status, sizeof status, "ON FOOT   (Z) CAMERA  (C) GRAB WALL");
        else if (cs->mode == CLIMBER_FALLING)
            snprintf(status, sizeof status, "FALLING!");
        else if (cs->shakeout)
            snprintf(status, sizeof status, "SHAKING OUT: %s", limb_name(cs->active));
        else if (worst < 0.15f)
            snprintf(status, sizeof status, "!! GRIP FAILING - SHAKE OUT (R) !!");
        else if (cs->active != LIMB_NONE)
            snprintf(status, sizeof status, "MOVING: %s", limb_name(cs->active));
        else
            snprintf(status, sizeof status, "ON WALL   (R) STEP OFF  (Z) PITON");

        render_hud_t hud = {
            .gen_ms      = gen_ms,
            .climber_alt = cs->alt,
            .rumble_ok   = in->rumble_present,
            .status      = status,
            .grip_count  = grips_count(),
            .stam        = cs->mode == CLIMBER_ON_FOOT ? NULL : cs->stam,
            .pitons      = cs->pitons,
            .chalk       = cs->chalk_uses,
        };
        render_frame(&eye, &target, &hud);
    }
}
