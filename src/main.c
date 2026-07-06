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
 * Phase 2 (GDD 5.2): fixed-seed Simplex noise evaluated into a static
 * Tiny3D mountain mesh — chunked, vertex-lit, fogged. An orbit camera
 * scouts the peak; Phase 1 input + rumble mapping stays live.
 */

#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "version.h"
#include "input/input.h"
#include "input/rumble.h"
#include "gen/mountain.h"
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
    float gen_ms = (float)TIMER_MICROS_LL(timer_ticks() - gen_start) / 1000.f;

    render_init();

    /* Scout camera: orbit the peak with the stick, D-pad up/down zooms.
     * Later phases replace this with the collision-aware orbital cam. */
    float cam_yaw  = 0.8f;
    float cam_alt  = 150.f;
    float cam_dist = 280.f;

    long long prev = timer_ticks();

    while (1) {
        long long now = timer_ticks();
        float dt = (float)TIMER_MICROS_LL(now - prev) / 1000000.f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        input_poll();
        const input_state_t *in = input_state();

        cam_yaw  += in->stick_x * dt * 1.3f;
        cam_alt  += in->stick_y * dt * 90.f;
        cam_dist -= in->cam_y * dt * 140.f;
        if (cam_alt  <  15.f) cam_alt  =  15.f;
        if (cam_alt  > 320.f) cam_alt  = 320.f;
        if (cam_dist <  70.f) cam_dist =  70.f;
        if (cam_dist > 420.f) cam_dist = 420.f;

        /* Rumble cues per GDD: piton strike heavy, shake-out light,
         * chalk a faint dusting. */
        if (in->piton) rumble_kick(1.0f, 0.35f);
        if (in->rest)  rumble_kick(0.4f, 0.60f);
        if (in->chalk) rumble_kick(0.2f, 0.15f);
        rumble_update(dt);

        T3DVec3 eye = {{ sinf(cam_yaw) * cam_dist,
                         cam_alt,
                         cosf(cam_yaw) * cam_dist }};

        /* Keep the camera out of the rock. */
        float ground = mountain_height(eye.v[0], eye.v[2]) + 10.f;
        if (eye.v[1] < ground) eye.v[1] = ground;

        float taim = cam_alt * 0.6f + 30.f;
        if (taim > MTN_PEAK_H * 0.85f) taim = MTN_PEAK_H * 0.85f;
        T3DVec3 target = {{ 0.f, taim, 0.f }};

        render_hud_t hud = {
            .gen_ms    = gen_ms,
            .cam_alt   = eye.v[1],
            .rumble_ok = in->rumble_present,
        };
        render_frame(&eye, &target, &hud);
    }
}
