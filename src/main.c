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
 * Phase 3 (GDD 5.3): stick-figure IK + limb snapping. Spawn on foot at
 * the mountain's base: walk with the stick (hold Z to steer the camera
 * instead), hold a C button at a wall to grab on. While climbing, hold
 * a C button to steer that limb along the rock, release to catch the
 * nearest grip; R with no limb held steps off onto walkable ground.
 * Balance and stamina land in Phase 4.
 */

#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>
#include <stdio.h>

#include "version.h"
#include "input/input.h"
#include "input/rumble.h"
#include "gen/mountain.h"
#include "gen/grips.h"
#include "sim/climber.h"
#include "render/render.h"

int main(void) {
    /* GDD 1.3: Expansion Pak is a hard requirement (heightmap arrays,
     * display lists, audio buffers). libdragon halts with an error
     * screen if the full 8MB isn't present. */
    assert_memory_expanded();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    joypad_init();
    timer_init();

    input_init();
    rumble_init();
    t3d_init((T3DInitParams){});

    long long gen_start = timer_ticks();
    mountain_generate();
    grips_generate();
    climber_init();
    float gen_ms = (float)TIMER_MICROS_LL(timer_ticks() - gen_start) / 1000.f;

    render_init();

    /* Follow camera: orbits the climber on the D-pad, seeded looking at
     * their back. GDD 3.1's collision-aware zoom comes with Phase 4. */
    const climber_t *cs = climber_state();
    float cam_yaw   = atan2f(cs->wall_n[0], cs->wall_n[2]);
    float cam_pitch = 0.25f;
    const float cam_dist = 5.6f;

    /* Title screen: a slow right-to-left orbit around the whole massif
     * (camera strafes right, so the mountain drifts leftward on screen)
     * with the cube-letter logo floating in front. Start drops the
     * player into the run at base camp. */
    bool  in_title  = true;
    float title_ang = 0.f;

    long long prev = timer_ticks();

    while (1) {
        long long now = timer_ticks();
        float dt = (float)TIMER_MICROS_LL(now - prev) / 1000000.f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        input_poll();
        const input_state_t *in = input_state();

        if (in_title) {
            title_ang += dt * 0.10f;
            T3DVec3 target = {{ 0.f, 105.f, 0.f }};
            T3DVec3 eye = {{ sinf(title_ang) * 360.f, 170.f,
                             cosf(title_ang) * 360.f }};
            if (in->start_btn) {
                in_title = false;
                rumble_kick(0.4f, 0.2f);
            }
            rumble_update(dt);

            render_hud_t hud = {
                .title     = true,
                .rumble_ok = in->rumble_present,
                .status    = NULL,
            };
            render_frame(&eye, &target, &hud);
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

        /* Haptics: solid catch thumps, a whiffed release buzzes softly;
         * the climbing-verb cues (piton/rest/chalk) only fire on the
         * wall — on foot Z is the camera modifier, not the piton. */
        if (cs->snapped)     rumble_kick(0.5f, 0.18f);
        if (cs->snap_failed) rumble_kick(0.25f, 0.30f);
        if (cs->mounted)     rumble_kick(0.45f, 0.20f);
        if (cs->dismounted)  rumble_kick(0.3f, 0.15f);
        if (cs->mode == CLIMBER_CLIMBING) {
            if (in->piton) rumble_kick(1.0f, 0.35f);
            if (in->rest)  rumble_kick(0.4f, 0.60f);
            if (in->chalk) rumble_kick(0.2f, 0.15f);
        }
        rumble_update(dt);

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

        char status[48];
        if (cs->mode == CLIMBER_ON_FOOT)
            snprintf(status, sizeof status, "ON FOOT   (Z) CAMERA  (C) GRAB WALL");
        else if (cs->active != LIMB_NONE)
            snprintf(status, sizeof status, "MOVING: %s", limb_name(cs->active));
        else
            snprintf(status, sizeof status, "ON WALL   (R) STEP OFF");

        render_hud_t hud = {
            .gen_ms      = gen_ms,
            .climber_alt = cs->alt,
            .rumble_ok   = in->rumble_present,
            .status      = status,
            .grip_count  = grips_count(),
        };
        render_frame(&eye, &target, &hud);
    }
}
