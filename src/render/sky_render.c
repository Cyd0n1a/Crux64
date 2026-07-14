#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "sky_render.h"
#include "../gen/noise.h"
#include "../gen/mountain.h"

/* The sun direction the light rig points from (render.c normalises its own
 * copy for lighting; we re-normalise here for the on-screen sprite). */
T3DVec3 SKY_SUN_DIR = {{ 0.48f, 0.62f, 0.34f }};

#define SKY_PI       3.14159265f

/* --- cloud dome ------------------------------------------------------- */
#define CLOUD_TEX    32          /* IA16 coverage map (tileable) */
#define DOME_R       300.f       /* metres; < the tightest far plane (500) */
/* One t3d_vert_load must stay <= T3D_VERTEX_CACHE_SIZE (70). 17*4 = 68 verts
 * keeps a rounder horizon ring within that budget in a single load. */
#define DOME_SEG     16          /* longitude segments */
#define DOME_RING    3           /* latitude rings, horizon -> zenith */
#define DOME_VERTS   ((DOME_RING + 1) * (DOME_SEG + 1))
#define DOME_PACKED  ((DOME_VERTS + 1) / 2)
#define TILES_AROUND 4           /* cloud-tex repeats around the dome */
#define TILES_VERT   2           /* ... and horizon -> zenith */
#define CLOUD_SCROLL 1.6f        /* texels/sec drift */
#define MAT_BUFS     3           /* CPU runs up to 2 frames ahead */

/* --- sun / flare ------------------------------------------------------ */
#define GLOW_TEX     32          /* IA16 radial falloff, reused per blob */
#define STARS_TEX    64
#define MOON_TEX     32

static surface_t cloud_surf;
static surface_t glow_surf;
static surface_t stars_surf;
static surface_t moon_surf;

static float g_time_of_day = 12.0f;

static T3DVertPacked   *dome_verts;
static rspq_block_t    *dome_blk;
static T3DMat4FP       *dome_mat[MAT_BUFS];
static rspq_syncpoint_t dome_sync[MAT_BUFS];
static bool             dome_sync_valid[MAT_BUFS];
static int              dome_buf;

/* Sun screen position carried a frame so the depth probe reads a settled
 * z-buffer; alpha is lerped so the one-frame lag never shows. */
static float g_sun_sx, g_sun_sy;
static bool  g_sun_have;
static bool  g_sun_vis_z;      /* depth probe: sky (not behind the massif) */
static float g_flare_alpha;    /* eased 0..1 overall flare intensity */

static inline float sky_sstep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return t * t * (3.f - 2.f * t);
}

/* Seamless fbm on the unit tile: bilinearly cross-fade the sample with its
 * three wrapped neighbours so the 32x32 texture repeats without a seam. */
static float tile_fbm(float u, float v) {
    const float S = 3.0f;               /* noise units across one tile */
    float x = u * S, y = v * S;
    float a = noise_fbm2(x,     y,     4, 2.0f, 0.5f);
    float b = noise_fbm2(x - S, y,     4, 2.0f, 0.5f);
    float c = noise_fbm2(x,     y - S, 4, 2.0f, 0.5f);
    float d = noise_fbm2(x - S, y - S, 4, 2.0f, 0.5f);
    return a * (1.f - u) * (1.f - v) + b * u * (1.f - v)
         + c * (1.f - u) * v         + d * u * v;
}

static void bake_clouds(void) {
    /* Bake off the mountain's seed so the sky is deterministic, then restore
     * the shared permutation table for downstream noise consumers. */
    noise_seed(MTN_SEED ^ 0xC10D5u);
    cloud_surf = surface_alloc(FMT_IA16, CLOUD_TEX, CLOUD_TEX);
    for (int y = 0; y < CLOUD_TEX; y++) {
        uint16_t *row = (uint16_t *)((uint8_t *)cloud_surf.buffer + y * cloud_surf.stride);
        for (int x = 0; x < CLOUD_TEX; x++) {
            float u = (x + 0.5f) / CLOUD_TEX;
            float v = (y + 0.5f) / CLOUD_TEX;
            float n = tile_fbm(u, v);                 /* ~[-1,1] */
            float cov = sky_sstep(0.46f, 0.74f, (n + 1.f) * 0.5f);
            uint8_t a = (uint8_t)(cov * 235.f);       /* leave a touch of sky */
            row[x] = (uint16_t)((0xFFu << 8) | a);    /* I = 0xFF, A = cover */
        }
    }
    noise_seed(MTN_SEED);
    data_cache_hit_writeback_invalidate(cloud_surf.buffer,
                                        cloud_surf.stride * CLOUD_TEX);
}

static void bake_glow(void) {
    glow_surf = surface_alloc(FMT_IA16, GLOW_TEX, GLOW_TEX);
    const float c = (GLOW_TEX - 1) * 0.5f;
    for (int y = 0; y < GLOW_TEX; y++) {
        uint16_t *row = (uint16_t *)((uint8_t *)glow_surf.buffer + y * glow_surf.stride);
        for (int x = 0; x < GLOW_TEX; x++) {
            float dx = (x - c) / (GLOW_TEX * 0.5f);
            float dy = (y - c) / (GLOW_TEX * 0.5f);
            float r = sqrtf(dx * dx + dy * dy);       /* 0 centre, 1 at edge */
            float a = 1.f - r;
            if (a < 0.f) a = 0.f;
            a = a * a;                                /* soft, bright core */
            row[x] = (uint16_t)((0xFFu << 8) | (uint8_t)(a * 255.f));
        }
    }
    data_cache_hit_writeback_invalidate(glow_surf.buffer,
                                        glow_surf.stride * GLOW_TEX);
}

static void bake_stars(void) {
    stars_surf = surface_alloc(FMT_RGBA16, STARS_TEX, STARS_TEX);
    for (int y = 0; y < STARS_TEX; y++) {
        uint16_t *row = (uint16_t *)((uint8_t *)stars_surf.buffer + y * stars_surf.stride);
        for (int x = 0; x < STARS_TEX; x++) {
            float n = noise_simplex2(x * 12.0f, y * 12.0f);
            if (n > 0.85f) {
                float intensity = (n - 0.85f) / 0.15f;
                uint8_t c = (uint8_t)(intensity * 255.0f);
                row[x] = (uint16_t)(((c >> 3) << 11) | ((c >> 3) << 6) | ((c >> 3) << 1) | (c ? 1 : 0));
            } else {
                row[x] = 0;
            }
        }
    }
    data_cache_hit_writeback_invalidate(stars_surf.buffer, stars_surf.stride * STARS_TEX);
}

static void bake_moon(void) {
    moon_surf = surface_alloc(FMT_RGBA16, MOON_TEX, MOON_TEX);
    for (int y = 0; y < MOON_TEX; y++) {
        uint16_t *row = (uint16_t *)((uint8_t *)moon_surf.buffer + y * moon_surf.stride);
        for (int x = 0; x < MOON_TEX; x++) {
            float dx = (x - 15.5f) / 16.0f;
            float dy = (y - 15.5f) / 16.0f;
            if (dx*dx + dy*dy <= 1.0f) {
                float n = noise_simplex2(x * 0.2f, y * 0.2f);
                uint8_t c = (uint8_t)(200.0f + 55.0f * n);
                row[x] = (uint16_t)(((c >> 3) << 11) | ((c >> 3) << 6) | ((c >> 3) << 1) | 1);
            } else {
                row[x] = 0;
            }
        }
    }
    data_cache_hit_writeback_invalidate(moon_surf.buffer, moon_surf.stride * MOON_TEX);
}

static void pack_dome_vert(int vi, float px, float py, float pz,
                           int16_t s, int16_t t, uint16_t norm) {
    T3DVertPacked *p = &dome_verts[vi / 2];
    if (vi & 1) {
        p->posB[0] = (int16_t)px; p->posB[1] = (int16_t)py; p->posB[2] = (int16_t)pz;
        p->normB = norm; p->rgbaB = 0xFFFFFFFFu;
        p->stB[0] = s; p->stB[1] = t;
    } else {
        p->posA[0] = (int16_t)px; p->posA[1] = (int16_t)py; p->posA[2] = (int16_t)pz;
        p->normA = norm; p->rgbaA = 0xFFFFFFFFu;
        p->stA[0] = s; p->stA[1] = t;
    }
}

static void build_dome(void) {
    dome_verts = malloc_uncached(sizeof(T3DVertPacked) * DOME_PACKED);
    T3DVec3 up = {{ 0, 1, 0 }};
    uint16_t norm = t3d_vert_pack_normal(&up);   /* unused by the combiner */

    /* Dip a touch below the horizon so the bottom rim never gaps against the
     * terrain silhouette; stop shy of the pole to avoid a UV pinch overhead. */
    const float lat0 = -8.f  * (SKY_PI / 180.f);
    const float lat1 =  86.f * (SKY_PI / 180.f);

    for (int i = 0; i <= DOME_RING; i++) {
        float fi = (float)i / DOME_RING;
        float lat = lat0 + (lat1 - lat0) * fi;
        float sy = sinf(lat), cy = cosf(lat);
        for (int j = 0; j <= DOME_SEG; j++) {
            float fj = (float)j / DOME_SEG;
            float lon = fj * 2.f * SKY_PI;
            float px = DOME_R * cy * cosf(lon);
            float py = DOME_R * sy;
            float pz = DOME_R * cy * sinf(lon);
            int16_t s = (int16_t)(fj * CLOUD_TEX * TILES_AROUND * 32.f);
            int16_t t = (int16_t)(fi * CLOUD_TEX * TILES_VERT   * 32.f);
            pack_dome_vert(i * (DOME_SEG + 1) + j, px, py, pz, s, t, norm);
        }
    }

    rspq_block_begin();
    t3d_vert_load(dome_verts, 0, DOME_VERTS);
    for (int i = 0; i < DOME_RING; i++) {
        for (int j = 0; j < DOME_SEG; j++) {
            int a = i * (DOME_SEG + 1) + j;
            int b = a + 1;
            int c = a + (DOME_SEG + 1);
            int d = c + 1;
            /* Cull is off for the dome, so winding is free. */
            t3d_tri_draw(a, c, d);
            t3d_tri_draw(a, d, b);
        }
    }
    t3d_tri_sync();
    dome_blk = rspq_block_end();
}

void sky_render_init(void) {
    bake_clouds();
    bake_glow();
    bake_stars();
    bake_moon();
    build_dome();
    for (int i = 0; i < MAT_BUFS; i++) {
        dome_mat[i] = malloc_uncached(sizeof(T3DMat4FP));
        dome_sync_valid[i] = false;
    }
}

void sky_update_time(float time_of_day) {
    g_time_of_day = time_of_day;
    float angle = (time_of_day / 24.0f) * 2.0f * SKY_PI - (SKY_PI / 2.0f);
    SKY_SUN_DIR.v[0] = 0.48f;
    SKY_SUN_DIR.v[1] = sinf(angle);
    SKY_SUN_DIR.v[2] = cosf(angle);
}

void sky_dome_draw(T3DViewport *vp, const T3DVec3 *eye) {
    (void)vp;
    if (dome_sync_valid[dome_buf])
        rspq_syncpoint_wait(dome_sync[dome_buf]);

    /* Centre the dome on the eye: it rides with the camera, reading as an
     * infinitely distant sky. Translation only — clouds drift via UV scroll. */
    T3DMat4 m;
    t3d_mat4_identity(&m);
    m.m[3][0] = eye->v[0];
    m.m[3][1] = eye->v[1];
    m.m[3][2] = eye->v[2];
    t3d_mat4_to_fixed_3x4(dome_mat[dome_buf], &m);

    float time = (float)((double)get_ticks_us() * 1e-6);
    float scroll = fmodf(time * CLOUD_SCROLL, (float)CLOUD_TEX);

    float night_blend = 1.0f;
    if (g_time_of_day > 5.0f && g_time_of_day < 19.0f) night_blend = 0.0f;
    else if (g_time_of_day >= 5.0f && g_time_of_day < 7.0f) night_blend = 1.0f - ((g_time_of_day - 5.0f) / 2.0f);
    else if (g_time_of_day >= 17.0f && g_time_of_day < 19.0f) night_blend = (g_time_of_day - 17.0f) / 2.0f;

    if (night_blend > 0.01f) {
        rdpq_mode_combiner(RDPQ_COMBINER1((0, 0, 0, PRIM), (TEX0, 0, PRIM, 0)));
        rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
        rdpq_mode_zbuf(false, false);
        rdpq_set_prim_color(RGBA32(255, 255, 255, (uint8_t)(night_blend * 255.f)));
        rdpq_tex_upload(TILE0, &stars_surf, &(rdpq_texparms_t){
            .s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE,
            .s.translate = time * 0.2f, .t.translate = time * 0.2f,
        });
        
        t3d_state_set_drawflags(T3D_FLAG_TEXTURED);
        t3d_matrix_push(dome_mat[dome_buf]);
        rspq_block_run(dome_blk);
        t3d_matrix_pop(1);
    }

    rdpq_mode_combiner(RDPQ_COMBINER1((0, 0, 0, PRIM), (TEX0, 0, PRIM, 0)));
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);   /* alpha-over the sky clear */
    rdpq_mode_zbuf(false, false);               /* backdrop: no depth */
    rdpq_set_prim_color(RGBA32(236, 242, 255, 255));   /* cool cloud tint */
    rdpq_tex_upload(TILE0, &cloud_surf, &(rdpq_texparms_t){
        .s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE,
        .s.translate = scroll,        .t.translate = 0,
    });

    t3d_state_set_drawflags(T3D_FLAG_TEXTURED);
    t3d_matrix_push(dome_mat[dome_buf]);
    rspq_block_run(dome_blk);
    t3d_matrix_pop(1);

    rdpq_mode_zbuf(true, true);   /* restore the frame-global z mode */

    dome_sync[dome_buf] = rspq_syncpoint_new();
    dome_sync_valid[dome_buf] = true;
    dome_buf = (dome_buf + 1) % MAT_BUFS;
}

void sky_sun_presample(const surface_t *zbuf) {
    if (!g_sun_have) { g_sun_vis_z = false; return; }
    int x = (int)g_sun_sx, y = (int)g_sun_sy;
    if (x < 0 || x >= zbuf->width || y < 0 || y >= zbuf->height) {
        g_sun_vis_z = false;
        return;
    }
    const uint16_t *zb = (const uint16_t *)zbuf->buffer;
    uint16_t d = zb[y * (zbuf->stride / 2) + x];
    g_sun_vis_z = (d > 0xFFF0u);   /* nothing near drawn here => open sky */
}

static void blit_glow(float cx, float cy, float rad,
                      uint8_t r, uint8_t g, uint8_t b, float a) {
    if (a <= 0.f) return;
    if (a > 1.f) a = 1.f;
    rdpq_set_prim_color(RGBA32(r, g, b, (uint8_t)(a * 255.f)));
    rdpq_texture_rectangle_scaled(TILE0,
        (int)(cx - rad), (int)(cy - rad), (int)(cx + rad), (int)(cy + rad),
        0, 0, GLOW_TEX, GLOW_TEX);
}

void sky_sun_draw(T3DViewport *vp, const T3DVec3 *eye) {
    /* Project the sun (a far point along its direction) to the screen. */
    T3DVec3 dir = SKY_SUN_DIR;
    t3d_vec3_norm(&dir);
    T3DVec3 world = {{ eye->v[0] + dir.v[0] * 1e5f,
                       eye->v[1] + dir.v[1] * 1e5f,
                       eye->v[2] + dir.v[2] * 1e5f }};
    T3DVec3 screen;
    t3d_viewport_calc_viewspace_pos(vp, &screen, &world);
    T3DVec4 clip;
    t3d_mat4_mul_vec3(&clip, &vp->matCamProj, &world);

    float sx = screen.v[0], sy = screen.v[1];

    T3DVec3 moon_dir = {{ -dir.v[0], -dir.v[1], -dir.v[2] }};
    T3DVec3 world_m = {{ eye->v[0] + moon_dir.v[0] * 1e5f,
                         eye->v[1] + moon_dir.v[1] * 1e5f,
                         eye->v[2] + moon_dir.v[2] * 1e5f }};
    T3DVec3 screen_m;
    t3d_viewport_calc_viewspace_pos(vp, &screen_m, &world_m);
    T3DVec4 clip_m;
    t3d_mat4_mul_vec3(&clip_m, &vp->matCamProj, &world_m);
    float sx_m = screen_m.v[0], sy_m = screen_m.v[1];

    if (dir.v[1] > moon_dir.v[1]) {
        g_sun_sx = sx; g_sun_sy = sy;
    } else {
        g_sun_sx = sx_m; g_sun_sy = sy_m;
    }
    g_sun_have = true;

    bool infront  = clip.v[3] > 0.f;
    bool onscreen = infront && sx > -48.f && sx < 368.f
                            && sy > -48.f && sy < 288.f;
    float target = (onscreen && g_sun_vis_z) ? 1.f : 0.f;
    g_flare_alpha += (target - g_flare_alpha) * 0.25f;
    if (g_flare_alpha < 0.02f) return;
    float fa = g_flare_alpha;

    /* Clean 2D baseline after the 3D pass, then our tinted-glow combiner. */
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER1((0, 0, 0, PRIM), (TEX0, 0, PRIM, 0)));
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_zbuf(false, false);
    rdpq_tex_upload(TILE0, &glow_surf, NULL);

    /* Sun: a soft halo under a hot core. */
    if (dir.v[1] > -0.2f && onscreen) {
        blit_glow(sx, sy, 62.f, 255, 236, 190, 0.28f * fa);
        blit_glow(sx, sy, 26.f, 255, 246, 216, 0.92f * fa);

        /* Ghosts strung along the sun -> screen-centre axis (and past it). */
        float dx = 160.f - sx, dy = 120.f - sy;
        static const struct { float k, rad; uint8_t r, g, b; float a; } ghost[] = {
            { 0.30f, 10.f, 180, 210, 255, 0.24f },
            { 0.55f, 20.f, 255, 220, 180, 0.22f },
            { 0.78f,  7.f, 200, 255, 220, 0.28f },
            { 1.00f, 26.f, 255, 210, 170, 0.18f },
            { 1.30f, 13.f, 180, 190, 255, 0.20f },
        };
        for (int i = 0; i < (int)(sizeof ghost / sizeof ghost[0]); i++)
            blit_glow(sx + dx * ghost[i].k, sy + dy * ghost[i].k, ghost[i].rad,
                      ghost[i].r, ghost[i].g, ghost[i].b, ghost[i].a * fa);
    }

    /* Moon: textured orb */
    bool infront_m  = clip_m.v[3] > 0.f;
    bool onscreen_m = infront_m && sx_m > -48.f && sx_m < 368.f
                                && sy_m > -48.f && sy_m < 288.f;
    if (moon_dir.v[1] > -0.2f && onscreen_m && g_sun_vis_z) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER1((0, 0, 0, PRIM), (TEX0, 0, PRIM, 0)));
        rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_zbuf(false, false);
        rdpq_set_prim_color(RGBA32(255, 255, 255, 220));
        rdpq_tex_upload(TILE0, &moon_surf, NULL);
        rdpq_texture_rectangle_scaled(TILE0,
            (int)(sx_m - 24.f), (int)(sy_m - 24.f), (int)(sx_m + 24.f), (int)(sy_m + 24.f),
            0, 0, MOON_TEX, MOON_TEX);
    }
}
