#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "title_render.h"
#include "../sim/vec3.h"

/* One shared unit cube (authored at +/-64 like the climber bones) is
 * instanced per letter-cell. The cube's verts are greyscale with a
 * baked top-to-bottom gradient; drawing with zero lights and a coloured
 * ambient tints it, so a single mesh serves every rainbow hue. */

#define VSCALE      64.f
#define MAT_BUFS    3

#define CELL        0.40f   /* letter-grid pitch (world units) */
#define CUBE_HALF   0.17f   /* cube half-extent; < CELL/2 leaves gaps */
#define LOGO_DIST   10.f    /* metres in front of the eye */
#define LOGO_RAISE  2.9f    /* lifted so the peak stays visible below */

#define WAVE_AMP    0.13f   /* group sine bob, per-column phase */
#define WAVE_SPEED  2.1f
#define WAVE_PHASE  0.38f
#define SWAY_AMP    0.15f   /* whole-word yaw sway (radians) */
#define SWAY_SPEED  0.45f
#define HUE_SPAN    0.034f  /* ~full rainbow across the word */
#define HUE_SPEED   0.24f   /* ripple travel (hue cycles per second) */

/* 5-row cube-font glyphs; '#' places a cube. */
static const char *const GLYPHS[][5] = {
    { ".###", "#...", "#...", "#...", ".###" },   /* C */
    { "###.", "#..#", "###.", "#.#.", "#..#" },   /* R */
    { "#..#", "#..#", "#..#", "#..#", ".##." },   /* U */
    { "#...#", ".#.#.", "..#..", ".#.#.", "#...#" }, /* X */
    { ".###", "#...", "###.", "#..#", ".##." },   /* 6 */
    { "#..#", "#..#", "####", "...#", "...#" },   /* 4 */
};
#define GLYPH_COUNT  6
#define GLYPH_ROWS   5
#define MAX_CUBES    72

static float cube_cx[MAX_CUBES];   /* cell coords, centred on the word */
static float cube_cy[MAX_CUBES];
static int   cube_count;

static T3DVertPacked *cube_verts;
static rspq_block_t  *blk_cube;

static T3DMat4FP       *mats[MAT_BUFS];
static rspq_syncpoint_t mat_sync[MAT_BUFS];
static bool             mat_sync_valid[MAT_BUFS];
static int              mat_buf;

static void pack_vert(T3DVertPacked *buf, int v, float x, float y, float z,
                      float nx, float ny, float nz, uint32_t color) {
    T3DVec3 n = {{ nx, ny, nz }};
    t3d_vec3_norm(&n);
    uint16_t norm = t3d_vert_pack_normal(&n);
    T3DVertPacked *p = &buf[v / 2];
    if (v & 1) {
        p->posB[0] = (int16_t)x; p->posB[1] = (int16_t)y; p->posB[2] = (int16_t)z;
        p->normB = norm; p->rgbaB = color;
    } else {
        p->posA[0] = (int16_t)x; p->posA[1] = (int16_t)y; p->posA[2] = (int16_t)z;
        p->normA = norm; p->rgbaA = color;
    }
}

static uint32_t grey(int g) {
    if (g < 0)   g = 0;
    if (g > 255) g = 255;
    return ((uint32_t)g << 24) | ((uint32_t)g << 16) | ((uint32_t)g << 8) | 0xFF;
}

static void build_cube(void) {
    cube_verts = malloc_uncached(sizeof(T3DVertPacked) * 4);
    /* Centred +/-64 box, bottom ring 0-3 / top ring 4-7. The greys fake
     * a light: bright top, dark base, corners nudged apart so adjacent
     * faces separate even under a flat coloured ambient. */
    static const float cx[4] = { -1, 1, 1, -1 };
    static const float cz[4] = { -1, -1, 1, 1 };
    static const int   tweak[4] = { 12, -10, 4, -16 };
    for (int i = 0; i < 4; i++) {
        pack_vert(cube_verts, i,     cx[i] * VSCALE, -VSCALE, cz[i] * VSCALE,
                  cx[i], -1.f, cz[i], grey(150 + tweak[i]));
        pack_vert(cube_verts, i + 4, cx[i] * VSCALE,  VSCALE, cz[i] * VSCALE,
                  cx[i],  1.f, cz[i], grey(238 + tweak[i]));
    }
    rspq_block_begin();
    t3d_vert_load(cube_verts, 0, 8);
    /* CCW from outside (same layout as the climber's bone box). */
    t3d_tri_draw(3, 2, 6); t3d_tri_draw(3, 6, 7);   /* +z */
    t3d_tri_draw(1, 0, 4); t3d_tri_draw(1, 4, 5);   /* -z */
    t3d_tri_draw(2, 1, 5); t3d_tri_draw(2, 5, 6);   /* +x */
    t3d_tri_draw(0, 3, 7); t3d_tri_draw(0, 7, 4);   /* -x */
    t3d_tri_draw(4, 7, 6); t3d_tri_draw(4, 6, 5);   /* top */
    t3d_tri_draw(0, 1, 2); t3d_tri_draw(0, 2, 3);   /* bottom */
    t3d_tri_sync();
    blk_cube = rspq_block_end();
}

void title_render_init(void) {
    build_cube();

    /* Lay the word out once: cell coords centred on the whole string. */
    int widths[GLYPH_COUNT], total_w = 0;
    for (int g = 0; g < GLYPH_COUNT; g++) {
        widths[g] = 0;
        while (GLYPHS[g][0][widths[g]] != '\0')
            widths[g]++;
        total_w += widths[g] + (g > 0 ? 1 : 0);
    }
    float x0 = -0.5f * (float)total_w + 0.5f;

    cube_count = 0;
    float xoff = 0.f;
    for (int g = 0; g < GLYPH_COUNT; g++) {
        for (int r = 0; r < GLYPH_ROWS; r++) {
            for (int c = 0; c < widths[g]; c++) {
                if (GLYPHS[g][r][c] != '#' || cube_count >= MAX_CUBES)
                    continue;
                cube_cx[cube_count] = x0 + xoff + (float)c;
                cube_cy[cube_count] = (float)(GLYPH_ROWS - 1 - r) - 2.f;
                cube_count++;
            }
        }
        xoff += (float)widths[g] + 1.f;
    }

    for (int i = 0; i < MAT_BUFS; i++) {
        mats[i] = malloc_uncached(sizeof(T3DMat4FP) * MAX_CUBES);
        mat_sync_valid[i] = false;
    }
}

static float sat01(float v) {
    if (v < 0.f) return 0.f;
    if (v > 1.f) return 1.f;
    return v;
}

/* Saturated rainbow: hue in [0,1) -> RGB, s=0.8 v=1. */
static void hue_to_rgb(float h, uint8_t out[4]) {
    h -= floorf(h);
    float r = sat01(fabsf(h * 6.f - 3.f) - 1.f);
    float g = sat01(2.f - fabsf(h * 6.f - 2.f));
    float b = sat01(2.f - fabsf(h * 6.f - 4.f));
    const float s = 0.8f;
    out[0] = (uint8_t)(255.f * (1.f - s + s * r));
    out[1] = (uint8_t)(255.f * (1.f - s + s * g));
    out[2] = (uint8_t)(255.f * (1.f - s + s * b));
    out[3] = 0xFF;
}

void title_render_draw(const T3DVec3 *eye, const T3DVec3 *target) {
    if (mat_sync_valid[mat_buf])
        rspq_syncpoint_wait(mat_sync[mat_buf]);

    float t = (float)((double)get_ticks_us() * 1e-6);

    /* Camera basis: the logo hangs a fixed distance ahead of the eye,
     * so it stays put on screen while the world orbits behind it. */
    float fwd[3] = { target->v[0] - eye->v[0],
                     target->v[1] - eye->v[1],
                     target->v[2] - eye->v[2] };
    v3_norm(fwd);
    float up_w[3] = { 0.f, 1.f, 0.f };
    float right[3];
    v3_cross(right, fwd, up_w);
    if (v3_norm(right) < 1e-5f)
        v3_set(right, 1.f, 0.f, 0.f);
    float up[3];
    v3_cross(up, right, fwd);

    /* Whole-word yaw sway about the camera-up axis. */
    float ang = SWAY_AMP * sinf(t * SWAY_SPEED);
    float ca = cosf(ang), sa = sinf(ang);
    float r2[3], zc[3];
    for (int i = 0; i < 3; i++)
        r2[i] = right[i] * ca + fwd[i] * sa;
    v3_cross(zc, r2, up);   /* right-handed third column (toward camera) */

    float base[3] = { eye->v[0], eye->v[1], eye->v[2] };
    v3_mad(base, fwd, LOGO_DIST);
    v3_mad(base, up, LOGO_RAISE);

    const float s = CUBE_HALF / VSCALE;

    t3d_light_set_count(0);   /* ambient-only: cube grey x hue = colour */

    for (int i = 0; i < cube_count; i++) {
        float wave = WAVE_AMP * sinf(t * WAVE_SPEED + cube_cx[i] * WAVE_PHASE);
        float pos[3];
        v3_copy(pos, base);
        v3_mad(pos, r2, cube_cx[i] * CELL);
        v3_mad(pos, up, cube_cy[i] * CELL + wave);

        T3DMat4FP *fp = &mats[mat_buf][i];
        T3DMat4 m;
        t3d_mat4_identity(&m);
        for (int r = 0; r < 3; r++) {
            m.m[0][r] = r2[r] * s;
            m.m[1][r] = up[r] * s;
            m.m[2][r] = zc[r] * s;
            m.m[3][r] = pos[r];
        }
        t3d_mat4_to_fixed_3x4(fp, &m);

        uint8_t col[4];
        hue_to_rgb(cube_cx[i] * HUE_SPAN + cube_cy[i] * 0.02f - t * HUE_SPEED,
                   col);
        t3d_light_set_ambient(col);

        t3d_matrix_push(fp);
        rspq_block_run(blk_cube);
        t3d_matrix_pop(1);
    }

    mat_sync[mat_buf] = rspq_syncpoint_new();
    mat_sync_valid[mat_buf] = true;
    mat_buf = (mat_buf + 1) % MAT_BUFS;
}
