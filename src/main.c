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
 * Phase 1 (GDD 5.1): core libdragon setup, Tiny3D bring-up, joypad
 * subsystem with the GDD 2.4 limb mapping, and Rumble Pak feedback.
 * Boots to a basecamp diagnostics screen that exercises all of it.
 */

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "version.h"
#include "input/input.h"
#include "input/rumble.h"

static T3DViewport viewport;
static surface_t   zbuf;

static void render_init(void) {
    viewport = t3d_viewport_create();
    zbuf     = surface_alloc(FMT_RGBA16, 320, 240);

    rdpq_font_t *font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, font);
}

/* Basecamp diagnostics: prove out every Phase 1 subsystem on-screen.
 * C buttons light up their limb, the stick reads back deadzoned values,
 * Z/R/A fire the rumble cues they'll drive for real in later phases. */
static void draw_diagnostics(float total) {
    const input_state_t *in = input_state();

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 116, 32,
                     "C R U X 6 4");
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 104, 44,
                     "basecamp %s", CRUX64_VERSION);

    int y = 76, lh = 12;
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 48, y,
                     "EXPANSION PAK  8MB OK");
    y += lh;
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 48, y,
                     "CONTROLLER     %s",
                     in->pad_present ? "PORT 1 OK" : "NOT FOUND");
    y += lh;
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 48, y,
                     "RUMBLE PAK     %s",
                     in->rumble_present ? "OK" : "INSERT PAK");
    y += lh * 2;

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 48, y,
                     "LIMB (C)       %s", limb_name(in->limb));
    y += lh;
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 48, y,
                     "STICK          %+.2f %+.2f", in->stick_x, in->stick_y);
    y += lh;
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 48, y,
                     "CAMERA (D-PAD) %+d %+d", in->cam_x, in->cam_y);
    y += lh * 2;

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 48, y,
                     "Z PITON   R SHAKE-OUT   A CHALK");

    /* Blinking cursor keeps the screen visibly alive at a glance. */
    if ((int)(total * 2.f) & 1)
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 48, 216, "_");
}

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
    render_init();

    long long prev  = timer_ticks();
    float     total = 0.f;

    while (1) {
        long long now = timer_ticks();
        float dt = (float)TIMER_MICROS_LL(now - prev) / 1000000.f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;
        total += dt;

        input_poll();
        const input_state_t *in = input_state();

        /* Rumble cues per GDD: piton strike is a sharp heavy hit,
         * shake-out a light pulse, chalk a faint dusting. */
        if (in->piton) rumble_kick(1.0f, 0.35f);
        if (in->rest)  rumble_kick(0.4f, 0.60f);
        if (in->chalk) rumble_kick(0.2f, 0.15f);
        rumble_update(dt);

        surface_t *disp = display_get();

        t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(70.f), 2.f, 200.f);
        t3d_viewport_look_at(&viewport,
                             &(T3DVec3){{ 0.f, 0.f, 40.f }},
                             &(T3DVec3){{ 0.f, 0.f,  0.f }},
                             &(T3DVec3){{ 0.f, 1.f,  0.f }});

        rdpq_attach(disp, &zbuf);
        t3d_frame_start();
        t3d_viewport_attach(&viewport);

        /* Pre-dawn alpine sky; the fog tint of later phases. */
        t3d_screen_clear_color(RGBA32(38, 48, 76, 255));
        t3d_screen_clear_depth();

        draw_diagnostics(total);

        rdpq_detach_show();
    }
}
