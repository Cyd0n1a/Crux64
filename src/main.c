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
 * Phase 3 (GDD 5.3): stick-figure IK + limb snapping. Hold a C button
 * to steer that limb along the rock, release to catch the nearest grip;
 * the torso follows the anchors and a D-pad orbit camera tracks the
 * climb. Balance and stamina land in Phase 4.
 */

#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

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

    long long prev = timer_ticks();

    while (1) {
        long long now = timer_ticks();
        float dt = (float)TIMER_MICROS_LL(now - prev) / 1000000.f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        input_poll();
        const input_state_t *in = input_state();

        climber_update(in, dt);

        cam_yaw   += in->cam_x * dt * 2.2f;
        cam_pitch += in->cam_y * dt * 1.6f;
        if (cam_pitch < -0.45f) cam_pitch = -0.45f;
        if (cam_pitch >  1.05f) cam_pitch =  1.05f;

        /* Haptics: solid catch thumps, a whiffed release buzzes softly;
         * piton/rest/chalk keep their Phase 1 cues. */
        if (cs->snapped)     rumble_kick(0.5f, 0.18f);
        if (cs->snap_failed) rumble_kick(0.25f, 0.30f);
        if (in->piton) rumble_kick(1.0f, 0.35f);
        if (in->rest)  rumble_kick(0.4f, 0.60f);
        if (in->chalk) rumble_kick(0.2f, 0.15f);
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

        render_hud_t hud = {
            .gen_ms      = gen_ms,
            .climber_alt = cs->alt,
            .rumble_ok   = in->rumble_present,
            .limb        = cs->active != LIMB_NONE ? limb_name(cs->active) : NULL,
            .grip_count  = grips_count(),
        };
        render_frame(&eye, &target, &hud);
    }
}
