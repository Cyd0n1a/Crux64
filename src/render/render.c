#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "render.h"
#include "climber_render.h"
#include "campsite_render.h"
#include "scatter_render.h"
#include "title_render.h"
#include "../meta/dialogue.h"
#include "../gen/mountain.h"
#include "../sim/campsite.h"
#include "../version.h"

/* Pre-dawn alpine sky; fog fades terrain into the same tint so the
 * horizon is seamless (GDD 3.1: fog hides chunk pop-in + altitude). */
#define SKY_COLOR   RGBA32(114, 128, 156, 0xFF)
#define FOG_NEAR    160.f
#define FOG_FAR     430.f
/* Near plane hugs the climber close-up; far stays past the fog wall.
 * 16-bit Z at this ratio is tight but the fog hides distant fighting. */
#define CAM_FOV     T3D_DEG_TO_RAD(65.f)
#define CAM_NEAR    1.2f
#define CAM_FAR     500.f
/* Title screen: the orbit camera sits ~360m out, so push the fog wall
 * past the far rim of the massif; the near plane relaxes too (nothing
 * sits closer than the cube logo at 10m). */
#define TITLE_NEAR      3.f
#define TITLE_FAR       950.f
#define TITLE_FOG_NEAR  520.f
#define TITLE_FOG_FAR   900.f

#define NUM_CHUNKS    (MTN_CHUNKS * MTN_CHUNKS)
#define CHUNK_VCOUNT  (MTN_CHUNK_VERTS * MTN_CHUNK_VERTS)   /* 64 <= 70 cache */
#define CHUNK_PACKED  (CHUNK_VCOUNT / 2)

static T3DViewport viewport;
static surface_t   zbuf;
static T3DMat4FP  *model_mat;

/* Static mesh: verts baked once at boot, one recorded block per chunk. */
static T3DVertPacked *chunk_verts[NUM_CHUNKS];
static rspq_block_t  *chunk_dpl[NUM_CHUNKS];
static T3DVec3        chunk_center[NUM_CHUNKS];
static float          chunk_radius[NUM_CHUNKS];
static int            chunks_drawn;

static void set_vert(T3DVertPacked *packed, int v, int gx, int gz) {
    float h = mountain_height_at(gx, gz);
    float n[3];
    mountain_normal_at(gx, gz, n);

    int16_t px = (int16_t)(gx * MTN_CELL_SIZE - MTN_HALF);
    int16_t py = (int16_t)h;
    int16_t pz = (int16_t)(gz * MTN_CELL_SIZE - MTN_HALF);

    T3DVec3 nv = {{ n[0], n[1], n[2] }};
    uint16_t norm  = t3d_vert_pack_normal(&nv);
    uint32_t color = mountain_color_at(gx, gz);

    T3DVertPacked *p = &packed[v / 2];
    if (v & 1) {
        p->posB[0] = px; p->posB[1] = py; p->posB[2] = pz;
        p->normB = norm; p->rgbaB = color;
    } else {
        p->posA[0] = px; p->posA[1] = py; p->posA[2] = pz;
        p->normA = norm; p->rgbaA = color;
    }
}

static void build_chunk(int cx, int cz) {
    int ci = cz * MTN_CHUNKS + cx;
    chunk_verts[ci] = malloc_uncached(sizeof(T3DVertPacked) * CHUNK_PACKED);

    int gx0 = cx * MTN_CHUNK_CELLS;
    int gz0 = cz * MTN_CHUNK_CELLS;

    float hmin = 1e9f, hmax = -1e9f;
    for (int vz = 0; vz < MTN_CHUNK_VERTS; vz++) {
        for (int vx = 0; vx < MTN_CHUNK_VERTS; vx++) {
            set_vert(chunk_verts[ci], vz * MTN_CHUNK_VERTS + vx,
                     gx0 + vx, gz0 + vz);
            float h = mountain_height_at(gx0 + vx, gz0 + vz);
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }

    /* Bounding sphere for CPU-side culling. */
    float half_xz = MTN_CHUNK_CELLS * MTN_CELL_SIZE * 0.5f;
    chunk_center[ci] = (T3DVec3){{
        (gx0 + MTN_CHUNK_CELLS * 0.5f) * MTN_CELL_SIZE - MTN_HALF,
        (hmin + hmax) * 0.5f,
        (gz0 + MTN_CHUNK_CELLS * 0.5f) * MTN_CELL_SIZE - MTN_HALF,
    }};
    float half_y = (hmax - hmin) * 0.5f;
    chunk_radius[ci] = sqrtf(2.f * half_xz * half_xz + half_y * half_y);

    rspq_block_begin();
    t3d_vert_load(chunk_verts[ci], 0, CHUNK_VCOUNT);
    for (int vz = 0; vz < MTN_CHUNK_CELLS; vz++) {
        for (int vx = 0; vx < MTN_CHUNK_CELLS; vx++) {
            int i00 = vz * MTN_CHUNK_VERTS + vx;
            int i10 = i00 + 1;
            int i01 = i00 + MTN_CHUNK_VERTS;
            int i11 = i01 + 1;
            /* CCW seen from above (+Y) => front faces point up/out. */
            t3d_tri_draw(i00, i01, i11);
            t3d_tri_draw(i00, i11, i10);
        }
    }
    t3d_tri_sync();
    chunk_dpl[ci] = rspq_block_end();
}

void render_init(void) {
    viewport = t3d_viewport_create();
    zbuf     = surface_alloc(FMT_RGBA16, 320, 240);

    model_mat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(model_mat);

    for (int cz = 0; cz < MTN_CHUNKS; cz++)
        for (int cx = 0; cx < MTN_CHUNKS; cx++)
            build_chunk(cx, cz);

    climber_render_init();
    campsite_render_init();
    scatter_render_init();
    title_render_init();
    dialogue_init();

    rdpq_font_t *font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, font);
}

static void draw_hud(const render_hud_t *hud) {
    /* Per-limb stamina bars (dev meter; GDD 4 hides this behind an
     * options toggle once menus exist). Order matches the C buttons:
     * right arm, left arm, right leg, left leg. */
    if (hud->stam) {
        rdpq_set_mode_fill(RGBA32(28, 30, 40, 0xFF));
        for (int i = 0; i < 4; i++)
            rdpq_fill_rectangle(252 + i * 13, 182, 260 + i * 13, 202);
        for (int i = 0; i < 4; i++) {
            float s = hud->stam[i];
            rdpq_set_fill_color(s > 0.5f  ? RGBA32(70, 200, 90, 0xFF)
                              : s > 0.25f ? RGBA32(230, 190, 60, 0xFF)
                                          : RGBA32(235, 60, 50, 0xFF));
            int h = (int)(s * 18.f + 0.5f);
            if (h > 0)
                rdpq_fill_rectangle(253 + i * 13, 201 - h,
                                    259 + i * 13, 201);
        }
        rdpq_set_mode_standard();
    }
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 252, 216,
                     "P%d C%d", hud->pitons, hud->chalk);

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 22,
                     "CRUX64 %s  seed 0x%08lX", CRUX64_VERSION,
                     (unsigned long)MTN_SEED);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 34,
                     "gen %.0fms  grips %d  chunks %d/%d  fps %.1f",
                     hud->gen_ms, hud->grip_count, chunks_drawn, NUM_CHUNKS,
                     display_get_fps());
    if (hud->status)
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 204,
                         "%s", hud->status);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 216,
                     "ALT %dm%s", (int)hud->climber_alt,
                     hud->rumble_ok ? "" : "   INSERT RUMBLE PAK");
}

static void draw_title_hud(const render_hud_t *hud) {
    float t = (float)((double)get_ticks_us() * 1e-6);
    if (fmodf(t, 1.1f) < 0.75f)
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 116, 176,
                         "PRESS START");
    /* GDD 3.4: the persistent record, so a returning climber sees their
     * best straight off the title screen. */
    if (hud->initials)
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 192,
                         "%.3s  BEST %dm  FALLS %d",
                         hud->initials, hud->best_alt, hud->lifetime_falls);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 204,
                     "CRUX64 %s%s", CRUX64_VERSION,
                     hud->rumble_ok ? "" : "   INSERT RUMBLE PAK");
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 216,
                     "(c) 2026 Cydonis Heavy Industries");
}

void render_frame(const T3DVec3 *eye, const T3DVec3 *target,
                  const render_hud_t *hud) {
    float cam_near = hud->title ? TITLE_NEAR     : CAM_NEAR;
    float cam_far  = hud->title ? TITLE_FAR      : CAM_FAR;
    float fog_near = hud->title ? TITLE_FOG_NEAR : FOG_NEAR;
    float fog_far  = hud->title ? TITLE_FOG_FAR  : FOG_FAR;

    t3d_viewport_set_projection(&viewport, CAM_FOV, cam_near, cam_far);
    t3d_viewport_look_at(&viewport, eye, target, &(T3DVec3){{ 0, 1, 0 }});

    rdpq_attach(display_get(), &zbuf);
    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(SKY_COLOR);

    t3d_screen_clear_color(SKY_COLOR);
    t3d_screen_clear_depth();

    t3d_fog_set_range(fog_near, fog_far);
    t3d_fog_set_enabled(true);

    /* Low warm sun + cool skylight ambient (GDD 3.1). */
    t3d_light_set_ambient((uint8_t[]){ 72, 78, 100, 0xFF });
    T3DVec3 sun_dir = {{ 0.48f, 0.62f, 0.34f }};
    t3d_vec3_norm(&sun_dir);
    t3d_light_set_directional(0, (uint8_t[]){ 255, 235, 200, 0xFF }, &sun_dir);

    /* The campfire casts flickering warm light onto the terrain, tent
     * and climber (point light positions are world-space in tiny3d). */
    const campsite_t *camp = campsite_get();
    int nlights = 1;
    if (camp->valid) {
        float tf = (float)((double)get_ticks_us() * 1e-6);
        float fl = 0.70f + 0.20f * sinf(tf * 11.3f)
                         + 0.10f * sinf(tf * 26.7f + 1.7f);
        uint8_t fc[4] = { (uint8_t)(250.f * fl), (uint8_t)(148.f * fl),
                          (uint8_t)(52.f * fl), 0xFF };
        T3DVec3 fpos = {{ camp->fire[0], camp->fire[1] + 0.55f, camp->fire[2] }};
        t3d_light_set_point(1, fc, &fpos, 8.f + 2.f * fl, false);
        nlights = 2;
    }
    t3d_light_set_count(nlights);

    t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_CULL_BACK);

    /* CPU chunk culling: behind-camera and beyond-fog chunks skipped. */
    T3DVec3 fwd = {{ target->v[0] - eye->v[0],
                     target->v[1] - eye->v[1],
                     target->v[2] - eye->v[2] }};
    t3d_vec3_norm(&fwd);
    float max_dist = fog_far;

    chunks_drawn = 0;
    t3d_matrix_push(model_mat);
    for (int ci = 0; ci < NUM_CHUNKS; ci++) {
        T3DVec3 to = {{ chunk_center[ci].v[0] - eye->v[0],
                        chunk_center[ci].v[1] - eye->v[1],
                        chunk_center[ci].v[2] - eye->v[2] }};
        float along = t3d_vec3_dot(&to, &fwd);
        if (along < -chunk_radius[ci])
            continue;
        float d2 = t3d_vec3_dot(&to, &to);
        float reach = max_dist + chunk_radius[ci];
        if (d2 > reach * reach)
            continue;
        rspq_block_run(chunk_dpl[ci]);
        chunks_drawn++;
    }
    t3d_matrix_pop(1);

    climber_render_draw();
    /* Trees, rocks and boulders before the campfire: the fire pass
     * clears the light state for its unlit flames, so the scatter must
     * draw while the sun + fire lights are still bound. Title mode is a
     * far orbit of the bare massif, so skip the dressing there. The
     * prologue also skips it: that camera parks down among the trees at
     * base camp, and the near-camera tree overdraw still trips the
     * tiny3d RSP-queue corruption (the deferred scatter perf pass owns
     * the real fix) — so keep the cutscene on the safe subset. */
    if (!hud->title && !hud->cinematic)
        scatter_render_draw(eye, target);
    campsite_render_draw();

    if (hud->title) {
        title_render_draw(eye, target);
        draw_title_hud(hud);
    } else if (hud->cinematic) {
        /* Prologue: the base-camp scene plays behind the dialogue box; the
         * dev HUD stays hidden so the frame reads cinematic. */
        dialogue_draw();
    } else {
        draw_hud(hud);
    }

    rdpq_detach_show();
}
