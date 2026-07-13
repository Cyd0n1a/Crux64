#include "splash.h"
#include <math.h>

/* Timeline (seconds). Audio (MUSIC_SPLASH: roar 0-2.8, jingle 3.2-16.4) is
 * tuned to these phase edges; the roar sits under the libdragon logo and the
 * jingle plays out under Cydonis, ending as the key art comes up.
 *
 *   libdragon        Cydonis                 key art
 * |- in -|- hold -|- x -|--- hold (jingle) ---|- x -|- hold -|- fade to title -|
 * 0    2.2      3.2   4.6                    16.4  17.9    20.9              22.4
 */
#define T_LD_IN     2.2f
#define T_LD_HOLD   3.2f
#define T_XFADE     4.6f
#define T_CYD_HOLD  16.4f   /* jingle ends here; key art + title music start */
#define T_KEY_XFADE 17.9f
#define T_KEY_HOLD  20.9f   /* fullscreen ends; the fade-out overlays the title */
#define T_END       22.4f

#define SCREEN_W 320
#define SCREEN_H 240

static sprite_t *ld_spr, *cyd_spr, *key_spr;

/* The Cydonis sine-wave pass rewrites a full surface on the CPU each frame, so
 * it needs the same double-buffer + syncpoint discipline as the terrain verts
 * (3 framebuffers let the CPU run 2 frames ahead of the RDP). Only Cydonis
 * uses this; the key art blits straight through. */
static surface_t        wave_buf[2];
static rspq_syncpoint_t buf_sync[2];
static int              flip;

static float t;
static bool  skipped, music_latched, avail;

void splash_init(void) {
    ld_spr  = sprite_load("rom:/ld_logo.sprite");
    cyd_spr = sprite_load("rom:/cydonis.sprite");
    key_spr = sprite_load("rom:/keyart.sprite");

    /* A NULL sprite means the filesystem isn't there; skip the whole splash
     * rather than crash, and let the title track start immediately. */
    avail = ld_spr && cyd_spr && key_spr;
    if (!avail) { music_latched = true; return; }

    surface_t px = sprite_get_pixels(cyd_spr);
    wave_buf[0] = surface_alloc(FMT_RGBA16, px.width, px.height);
    wave_buf[1] = surface_alloc(FMT_RGBA16, px.width, px.height);
}

void splash_update(float dt) {
    if (splash_finished()) return;
    t += dt;
    if (t >= T_CYD_HOLD) music_latched = true;
}

void splash_skip(void) {
    skipped = true;
    music_latched = true;   /* main.c cues the title track on the way out */
}

bool splash_fullscreen(void)      { return avail && !skipped && t < T_KEY_HOLD; }
bool splash_finished(void)        { return !avail || skipped || t >= T_END; }
bool splash_title_music_due(void) { return music_latched; }

static void logo_modes(float alpha) {
    int a = (int)(alpha * 255.f);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_set_prim_color(RGBA32(255, 255, 255, a));
}

static void draw_ld(float alpha, float theta, float ycenter) {
    logo_modes(alpha);
    rdpq_blitparms_t p = {
        .cx = ld_spr->width / 2,
        .cy = ld_spr->height / 2,
        .theta = theta,
        .filtering = true,
    };
    rdpq_sprite_blit(ld_spr, SCREEN_W / 2.f, ycenter, &p);
}

/* Cydonis logo with each pixel column displaced by a travelling sine. Done
 * CPU-side into wave_buf (one row-major pass over the texels) and blitted once
 * -- far cheaper for the RDP than one-texel-wide column loads. */
static void draw_cyd(float alpha, float amp, float wt) {
    surface_t px = sprite_get_pixels(cyd_spr);
    int W = px.width, H = px.height;
    if (W > SCREEN_W) W = SCREEN_W;
    float x0 = (SCREEN_W - W) / 2.f;
    float y0 = (SCREEN_H - H) / 2.f;

    logo_modes(alpha);

    if (amp < 0.25f) {   /* flat during the hold: blit the sprite as-is */
        rdpq_sprite_blit(cyd_spr, x0, y0, NULL);
        return;
    }

    surface_t      *dst = &wave_buf[flip];
    const uint16_t *s   = px.buffer;
    uint16_t       *d   = dst->buffer;
    int sp = px.stride / 2, dp = dst->stride / 2;

    static int16_t off[SCREEN_W];
    for (int x = 0; x < W; x++)
        off[x] = (int16_t)(amp * sinf(x * 0.049f + wt * 4.2f));

    for (int y = 0; y < H; y++) {
        uint16_t *row = d + y * dp;
        for (int x = 0; x < W; x++) {
            int sy = y - off[x];
            /* vacated texels go transparent (RGBA16 zero) */
            row[x] = ((unsigned)sy < (unsigned)H) ? s[sy * sp + x] : 0;
        }
    }
    data_cache_hit_writeback(d, dst->stride * H);
    rdpq_tex_blit(dst, x0, y0, NULL);
}

/* Key art, scaled into the Title Safe area (0.90) to prevent CRT overscan
 * cropping, with a gentle continuous zoom-out over its lifetime. */
static float key_scale(void) {
    float u = (t - T_CYD_HOLD) / (T_END - T_CYD_HOLD);
    if (u < 0.f) u = 0.f; else if (u > 1.f) u = 1.f;
    return 0.96f - 0.06f * u;
}

static void draw_key(float alpha, float scale) {
    logo_modes(alpha);
    rdpq_blitparms_t p = {
        .cx = key_spr->width / 2,
        .cy = key_spr->height / 2,
        .scale_x = scale,
        .scale_y = scale,
        .filtering = true,
    };
    rdpq_sprite_blit(key_spr, SCREEN_W / 2.f, SCREEN_H / 2.f, &p);
}

static float ease(float u) { return u * u * (3.f - 2.f * u); }

void splash_render(surface_t *disp) {
    flip ^= 1;
    if (buf_sync[flip])
        rspq_syncpoint_wait(buf_sync[flip]);

    rdpq_attach(disp, NULL);
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);

    if (t < T_LD_IN) {
        /* Crossfade in while dropping from above the screen, unwinding one
         * full turn as it settles. */
        float e = ease(t / T_LD_IN);
        draw_ld(t / T_LD_IN, 6.2831853f * (1.f - e), -140.f + e * 260.f);
    } else if (t < T_LD_HOLD) {
        draw_ld(1.f, 0.f, 120.f);
    } else if (t < T_XFADE) {
        float u = (t - T_LD_HOLD) / (T_XFADE - T_LD_HOLD);
        draw_ld(1.f - u, 0.f, 120.f);
        draw_cyd(u, 2.f + 6.f * (1.f - u), t);
    } else if (t < T_CYD_HOLD) {
        /* Hold: gentle residual ripple while the jingle plays. */
        draw_cyd(1.f, 2.f, t);
    } else if (t < T_KEY_XFADE) {
        float u = (t - T_CYD_HOLD) / (T_KEY_XFADE - T_CYD_HOLD);
        draw_cyd(1.f - u, 2.f, t);
        draw_key(u, key_scale());
    } else {
        draw_key(1.f, key_scale());
    }

    rdpq_detach_show();
    buf_sync[flip] = rspq_syncpoint_new();
}

void splash_draw_overlay(void) {
    if (skipped || t < T_KEY_HOLD || t >= T_END) return;

    float u = (t - T_KEY_HOLD) / (T_END - T_KEY_HOLD);

    /* Black veil fading away so the whole screen -- not just the key art --
     * crossfades from the splash into the live title scene, with the key art
     * fading out on top of it. */
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, (int)((1.f - u) * 255.f)));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);

    draw_key(1.f - u, key_scale());
}

void splash_free(void) {
    if (!avail) return;
    rspq_wait();                 /* the RDP may still be reading these */
    sprite_free(ld_spr);   ld_spr  = NULL;
    sprite_free(cyd_spr);  cyd_spr = NULL;
    sprite_free(key_spr);  key_spr = NULL;
    surface_free(&wave_buf[0]);
    surface_free(&wave_buf[1]);
    avail = false;               /* draws/overlay become no-ops from here */
}
