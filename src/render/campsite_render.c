#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "campsite_render.h"
#include "../sim/campsite.h"
#include "../sim/vec3.h"
#include "../gen/mountain.h"

/* Same instancing scheme as the climber: unit meshes authored at
 * +/-VSCALE, world scale baked into the matrices. Everything except
 * the flames is static — matrices written once at init, the whole
 * camp recorded into a single rspq block. The flames rewrite their
 * matrices every frame, so those are triple-buffered and syncpoint-
 * guarded like the climber's bones. */

#define VSCALE      64.f
#define FLAME_COUNT 3
#define MAT_BUFS    3

#define LOG_COUNT   4
#define STONE_COUNT 6
#define STATIC_MATS (1 + LOG_COUNT + STONE_COUNT + 1)   /* tent, logs, stones, scorch */

#define COL_TENT    0xE8B428FF   /* mustard expedition nylon */
#define COL_DOOR    0x2A1E14FF
#define COL_LOG     0x54381EFF
#define COL_STONE   0x8A8A90FF
#define COL_SCORCH  0x201A16FF
#define COL_FLAME_O 0xE85818FF   /* outer tongue */
#define COL_FLAME_M 0xF09018FF   /* mid */
#define COL_FLAME_C 0xF8E058FF   /* core */

static T3DVertPacked *box_log, *box_stone;
static T3DVertPacked *tent_verts;
static T3DVertPacked *octa_scorch;
static T3DVertPacked *octa_flame[FLAME_COUNT];

static rspq_block_t *blk_log, *blk_stone, *blk_scorch;
static rspq_block_t *blk_flame[FLAME_COUNT];
static rspq_block_t *blk_static;

static T3DMat4FP *smats;                       /* written once at init */
static T3DMat4FP *fmats[MAT_BUFS];             /* flames, per frame */
static rspq_syncpoint_t fsync[MAT_BUFS];
static bool             fsync_valid[MAT_BUFS];
static int              fbuf;

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

static T3DVertPacked *make_box(uint32_t color) {
    T3DVertPacked *buf = malloc_uncached(sizeof(T3DVertPacked) * 4);
    static const float cx[4] = { -1, 1, 1, -1 };
    static const float cz[4] = { -1, -1, 1, 1 };
    for (int i = 0; i < 4; i++) {
        pack_vert(buf, i,     cx[i] * VSCALE, 0.f,    cz[i] * VSCALE,
                  cx[i], -1.f, cz[i], color);
        pack_vert(buf, i + 4, cx[i] * VSCALE, VSCALE, cz[i] * VSCALE,
                  cx[i],  1.f, cz[i], color);
    }
    return buf;
}

static rspq_block_t *record_box(T3DVertPacked *buf) {
    rspq_block_begin();
    t3d_vert_load(buf, 0, 8);
    t3d_tri_draw(3, 2, 6); t3d_tri_draw(3, 6, 7);   /* +z */
    t3d_tri_draw(1, 0, 4); t3d_tri_draw(1, 4, 5);   /* -z */
    t3d_tri_draw(2, 1, 5); t3d_tri_draw(2, 5, 6);   /* +x */
    t3d_tri_draw(0, 3, 7); t3d_tri_draw(0, 7, 4);   /* -x */
    t3d_tri_draw(4, 7, 6); t3d_tri_draw(4, 6, 5);   /* top */
    t3d_tri_draw(0, 1, 2); t3d_tri_draw(0, 2, 3);   /* bottom */
    t3d_tri_sync();
    return rspq_block_end();
}

static T3DVertPacked *make_octa(uint32_t color) {
    T3DVertPacked *buf = malloc_uncached(sizeof(T3DVertPacked) * 3);
    pack_vert(buf, 0,  0,  VSCALE, 0,   0,  1, 0, color);
    pack_vert(buf, 1,  0, -VSCALE, 0,   0, -1, 0, color);
    pack_vert(buf, 2,  VSCALE, 0, 0,    1,  0, 0, color);
    pack_vert(buf, 3, -VSCALE, 0, 0,   -1,  0, 0, color);
    pack_vert(buf, 4,  0, 0,  VSCALE,   0,  0, 1, color);
    pack_vert(buf, 5,  0, 0, -VSCALE,   0,  0,-1, color);
    return buf;
}

static rspq_block_t *record_octa(T3DVertPacked *buf) {
    rspq_block_begin();
    t3d_vert_load(buf, 0, 6);
    t3d_tri_draw(0, 4, 2); t3d_tri_draw(0, 2, 5);
    t3d_tri_draw(0, 5, 3); t3d_tri_draw(0, 3, 4);
    t3d_tri_draw(1, 2, 4); t3d_tri_draw(1, 5, 2);
    t3d_tri_draw(1, 3, 5); t3d_tri_draw(1, 4, 3);
    t3d_tri_sync();
    return rspq_block_end();
}

/* A-frame tent in local meters * VSCALE: ridge along z, door at +z.
 * 9 verts (6 shell + 3 dark door triangle inset in the front cap). */
#define TENT_W2 (0.95f * VSCALE)
#define TENT_L2 (1.10f * VSCALE)
#define TENT_H  (1.40f * VSCALE)

static T3DVertPacked *make_tent(void) {
    T3DVertPacked *buf = malloc_uncached(sizeof(T3DVertPacked) * 5);
    pack_vert(buf, 0,  0,       TENT_H,  TENT_L2,  0,  1,  0.3f, COL_TENT);
    pack_vert(buf, 1,  0,       TENT_H, -TENT_L2,  0,  1, -0.3f, COL_TENT);
    pack_vert(buf, 2, -TENT_W2, 0,       TENT_L2, -1, 0.3f,  0.3f, COL_TENT);
    pack_vert(buf, 3,  TENT_W2, 0,       TENT_L2,  1, 0.3f,  0.3f, COL_TENT);
    pack_vert(buf, 4, -TENT_W2, 0,      -TENT_L2, -1, 0.3f, -0.3f, COL_TENT);
    pack_vert(buf, 5,  TENT_W2, 0,      -TENT_L2,  1, 0.3f, -0.3f, COL_TENT);
    /* Door: floats 2cm in front of the +z cap. */
    pack_vert(buf, 6,  0,             TENT_H * 0.68f, TENT_L2 + 2, 0, 0.2f, 1, COL_DOOR);
    pack_vert(buf, 7, -TENT_W2 * 0.4f, 0,             TENT_L2 + 2, 0, 0.2f, 1, COL_DOOR);
    pack_vert(buf, 8,  TENT_W2 * 0.4f, 0,             TENT_L2 + 2, 0, 0.2f, 1, COL_DOOR);
    pack_vert(buf, 9,  0, 0, 0,  0, 1, 0, COL_TENT);   /* pad slot */
    return buf;
}

static void record_tent(T3DVertPacked *buf) {
    t3d_vert_load(buf, 0, 10);
    t3d_tri_draw(0, 2, 4); t3d_tri_draw(0, 4, 1);   /* -x slope */
    t3d_tri_draw(0, 1, 5); t3d_tri_draw(0, 5, 3);   /* +x slope */
    t3d_tri_draw(1, 4, 5);                          /* back cap */
    t3d_tri_draw(0, 3, 2);                          /* front cap */
    t3d_tri_draw(6, 8, 7);                          /* door */
    t3d_tri_sync();
}

/* Basis matrix for a segment p1->p2, like the climber's draw_bone but
 * written into a caller-owned (static) matrix slot. */
static void seg_mat(T3DMat4FP *fp, const float p1[3], const float p2[3],
                    float width, const float ref[3]) {
    float y[3];
    v3_sub(y, p2, p1);
    float len = v3_norm(y);
    if (len < 1e-5f) {
        v3_set(y, 0.f, 1.f, 0.f);
        len = 0.01f;
    }
    float x[3];
    v3_cross(x, ref, y);
    if (v3_norm(x) < 1e-5f) {
        float alt[3] = { y[1], y[2], y[0] };
        v3_cross(x, alt, y);
        v3_norm(x);
    }
    float z[3];
    v3_cross(z, x, y);

    float sw = width / VSCALE, sl = len / VSCALE;
    T3DMat4 m;
    t3d_mat4_identity(&m);
    for (int r = 0; r < 3; r++) {
        m.m[0][r] = x[r] * sw;
        m.m[1][r] = y[r] * sl;
        m.m[2][r] = z[r] * sw;
        m.m[3][r] = p1[r];
    }
    t3d_mat4_to_fixed_3x4(fp, &m);
}

void campsite_render_init(void) {
    box_log     = make_box(COL_LOG);
    box_stone   = make_box(COL_STONE);
    tent_verts  = make_tent();
    octa_scorch = make_octa(COL_SCORCH);
    blk_log     = record_box(box_log);
    blk_stone   = record_box(box_stone);
    blk_scorch  = record_octa(octa_scorch);

    static const uint32_t flame_col[FLAME_COUNT] =
        { COL_FLAME_O, COL_FLAME_M, COL_FLAME_C };
    for (int i = 0; i < FLAME_COUNT; i++) {
        octa_flame[i] = make_octa(flame_col[i]);
        blk_flame[i]  = record_octa(octa_flame[i]);
    }
    for (int i = 0; i < MAT_BUFS; i++) {
        fmats[i] = malloc_uncached(sizeof(T3DMat4FP) * FLAME_COUNT);
        fsync_valid[i] = false;
    }

    const campsite_t *camp = campsite_get();
    if (!camp->valid)
        return;

    smats = malloc_uncached(sizeof(T3DMat4FP) * STATIC_MATS);
    int mi = 0;

    /* Tent: yaw the local frame so the door (+z) faces the fire. */
    {
        float cy = cosf(camp->tent_yaw), sy = sinf(camp->tent_yaw);
        float s = 1.f / VSCALE;
        T3DMat4 m;
        t3d_mat4_identity(&m);
        m.m[0][0] =  cy * s; m.m[0][2] = -sy * s;
        m.m[2][0] =  sy * s; m.m[2][2] =  cy * s;
        m.m[1][1] = s;
        for (int r = 0; r < 3; r++)
            m.m[3][r] = camp->tent[r];
        t3d_mat4_to_fixed_3x4(&smats[mi], &m);
    }
    int tent_mat = mi++;

    /* Log teepee over the pit; each butt end sits on the real ground. */
    static const float REF[3] = { 0.31f, 0.05f, 0.95f };
    float apex[3] = { camp->fire[0], camp->fire[1] + 0.62f, camp->fire[2] };
    int log_mat = mi;
    for (int i = 0; i < LOG_COUNT; i++) {
        float a = 0.4f + (float)i * (2.f * (float)M_PI / LOG_COUNT);
        float p1[3] = { camp->fire[0] + cosf(a) * 0.58f, 0.f,
                        camp->fire[2] + sinf(a) * 0.58f };
        p1[1] = mountain_surface(p1[0], p1[2], NULL);
        float p2[3];
        v3_lerp(p2, p1, apex, 1.12f);   /* tips cross past the apex */
        seg_mat(&smats[mi++], p1, p2, 0.055f, REF);
    }

    /* Fire ring stones. */
    int stone_mat = mi;
    for (int i = 0; i < STONE_COUNT; i++) {
        float a = 0.9f + (float)i * (2.f * (float)M_PI / STONE_COUNT);
        float w = 0.10f + 0.03f * (float)((i * 7) % 3);
        float p1[3] = { camp->fire[0] + cosf(a) * 0.62f, 0.f,
                        camp->fire[2] + sinf(a) * 0.62f };
        p1[1] = mountain_surface(p1[0], p1[2], NULL) - 0.04f;
        float p2[3] = { p1[0], p1[1] + 0.16f, p1[2] };
        seg_mat(&smats[mi++], p1, p2, w, REF);
    }

    /* Scorched patch under the pit. */
    {
        T3DMat4 m;
        t3d_mat4_identity(&m);
        m.m[0][0] = 0.5f / VSCALE;   /* 0.5m half-width, squashed flat */
        m.m[1][1] = 0.025f / VSCALE;
        m.m[2][2] = 0.5f / VSCALE;
        m.m[3][0] = camp->fire[0];
        m.m[3][1] = camp->fire[1] + 0.03f;
        m.m[3][2] = camp->fire[2];
        t3d_mat4_to_fixed_3x4(&smats[mi], &m);
    }
    int scorch_mat = mi++;

    rspq_block_begin();
    t3d_matrix_push(&smats[tent_mat]);
    record_tent(tent_verts);
    t3d_matrix_pop(1);
    t3d_matrix_push(&smats[scorch_mat]);
    rspq_block_run(blk_scorch);
    t3d_matrix_pop(1);
    for (int i = 0; i < LOG_COUNT; i++) {
        t3d_matrix_push(&smats[log_mat + i]);
        rspq_block_run(blk_log);
        t3d_matrix_pop(1);
    }
    for (int i = 0; i < STONE_COUNT; i++) {
        t3d_matrix_push(&smats[stone_mat + i]);
        rspq_block_run(blk_stone);
        t3d_matrix_pop(1);
    }
    blk_static = rspq_block_end();
}

void campsite_render_draw(void) {
    const campsite_t *camp = campsite_get();
    if (!camp->valid)
        return;

    rspq_block_run(blk_static);

    /* Flames: half-extent scales + a y-column shear wobbling on mixed
     * sine frequencies — cheap fire that never repeats visibly. Drawn
     * unlit (full ambient, no lights) so they read as emissive; the
     * next frame resets the light state before the terrain anyway. */
    if (fsync_valid[fbuf])
        rspq_syncpoint_wait(fsync[fbuf]);

    float t = (float)((double)get_ticks_us() * 1e-6);
    static const float w0[FLAME_COUNT] = { 0.30f, 0.20f, 0.11f };
    static const float h0[FLAME_COUNT] = { 0.42f, 0.55f, 0.34f };

    t3d_light_set_count(0);
    t3d_light_set_ambient((uint8_t[]){ 0xFF, 0xFF, 0xFF, 0xFF });

    for (int i = 0; i < FLAME_COUNT; i++) {
        float ph = (float)i * 2.1f;
        float hh = h0[i] * (1.f + 0.20f * sinf(t * 9.7f + ph)
                                + 0.11f * sinf(t * 23.3f + ph * 1.7f));
        float hw = w0[i] * (1.f + 0.13f * sinf(t * 13.1f + ph + 0.9f));
        float rot = t * (1.6f + 0.5f * (float)i) + ph;
        float cr = cosf(rot), sr = sinf(rot);
        float lean_x = hh * 0.20f * sinf(t * 6.1f + ph);
        float lean_z = hh * 0.20f * sinf(t * 5.3f + ph + 1.1f);

        T3DMat4 m;
        t3d_mat4_identity(&m);
        m.m[0][0] =  cr * hw / VSCALE; m.m[0][2] = -sr * hw / VSCALE;
        m.m[2][0] =  sr * hw / VSCALE; m.m[2][2] =  cr * hw / VSCALE;
        m.m[1][0] = lean_x / VSCALE;
        m.m[1][1] = hh / VSCALE;
        m.m[1][2] = lean_z / VSCALE;
        m.m[3][0] = camp->fire[0];
        m.m[3][1] = camp->fire[1] + 0.10f + hh;
        m.m[3][2] = camp->fire[2];
        t3d_mat4_to_fixed_3x4(&fmats[fbuf][i], &m);

        t3d_matrix_push(&fmats[fbuf][i]);
        rspq_block_run(blk_flame[i]);
        t3d_matrix_pop(1);
    }

    fsync[fbuf] = rspq_syncpoint_new();
    fsync_valid[fbuf] = true;
    fbuf = (fbuf + 1) % MAT_BUFS;
}
