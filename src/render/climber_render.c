#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "climber_render.h"
#include "../sim/climber.h"
#include "../sim/vec3.h"
#include "../gen/grips.h"
#include "../gen/mountain.h"

/* Bones are one shared unit box (authored at +/-64 so the int16 verts
 * carry precision; the matrix bakes the 1/64 back in), instanced with a
 * per-bone basis matrix — GDD 3.1's "matrix stack updated per-frame".
 * Matrices are rewritten every frame, so they're triple-buffered and
 * syncpoint-guarded: with 3 framebuffers the CPU runs up to 2 frames
 * ahead of the RSP. */

#define VSCALE      64.f
#define MAX_MATS    28
#define MAT_BUFS    3

#define GRIP_DRAW_R    3.2f
#define GRIP_DRAW_MAX  14

#define COL_SUIT   0xE0482CFF
#define COL_SKIN   0xF0C8A0FF
#define COL_GRIP   0xE8E0D0FF
#define COL_READY  0x58E060FF

static T3DVertPacked *box_suit;    /* 8 verts: y 0..64, x/z +/-64 */
static T3DVertPacked *box_skin;
static T3DVertPacked *octa_grip;   /* 6 verts at +/-64 on each axis */
static T3DVertPacked *octa_ready;

static rspq_block_t *blk_suit, *blk_skin, *blk_grip, *blk_ready;

static T3DMat4FP       *mats[MAT_BUFS];
static rspq_syncpoint_t mat_sync[MAT_BUFS];
static bool             mat_sync_valid[MAT_BUFS];
static int              mat_buf, mat_used;

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
    /* Bottom ring y=0, top ring y=64; rounded corner normals. */
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
    /* CCW from outside. */
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
    pack_vert(buf, 0,  0,  VSCALE, 0,   0,  1, 0, color);   /* top */
    pack_vert(buf, 1,  0, -VSCALE, 0,   0, -1, 0, color);   /* bottom */
    pack_vert(buf, 2,  VSCALE, 0, 0,    1,  0, 0, color);   /* +x */
    pack_vert(buf, 3, -VSCALE, 0, 0,   -1,  0, 0, color);   /* -x */
    pack_vert(buf, 4,  0, 0,  VSCALE,   0,  0, 1, color);   /* +z */
    pack_vert(buf, 5,  0, 0, -VSCALE,   0,  0,-1, color);   /* -z */
    return buf;
}

static rspq_block_t *record_octa(T3DVertPacked *buf) {
    rspq_block_begin();
    t3d_vert_load(buf, 0, 6);
    t3d_tri_draw(0, 4, 2); t3d_tri_draw(0, 2, 5);   /* top half */
    t3d_tri_draw(0, 5, 3); t3d_tri_draw(0, 3, 4);
    t3d_tri_draw(1, 2, 4); t3d_tri_draw(1, 5, 2);   /* bottom half */
    t3d_tri_draw(1, 3, 5); t3d_tri_draw(1, 4, 3);
    t3d_tri_sync();
    return rspq_block_end();
}

void climber_render_init(void) {
    box_suit   = make_box(COL_SUIT);
    box_skin   = make_box(COL_SKIN);
    octa_grip  = make_octa(COL_GRIP);
    octa_ready = make_octa(COL_READY);
    blk_suit  = record_box(box_suit);
    blk_skin  = record_box(box_skin);
    blk_grip  = record_octa(octa_grip);
    blk_ready = record_octa(octa_ready);

    for (int i = 0; i < MAT_BUFS; i++) {
        mats[i] = malloc_uncached(sizeof(T3DMat4FP) * MAX_MATS);
        mat_sync_valid[i] = false;
    }
}

/* Next matrix slot in this frame's buffer. */
static T3DMat4FP *next_mat(void) {
    if (mat_used >= MAX_MATS)
        return NULL;
    return &mats[mat_buf][mat_used++];
}

/* Basis matrix for a segment p1->p2: Y along the bone scaled to its
 * length, X/Z scaled to width, columns kept right-handed. */
static void draw_bone(const float p1[3], const float p2[3], float width,
                      const float ref[3], rspq_block_t *blk) {
    T3DMat4FP *fp = next_mat();
    if (!fp) return;

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

    t3d_matrix_push(fp);
    rspq_block_run(blk);
    t3d_matrix_pop(1);
}

static void draw_marker(const float pos[3], const float n[3], float size,
                        rspq_block_t *blk) {
    T3DMat4FP *fp = next_mat();
    if (!fp) return;

    float s = size / VSCALE;
    T3DMat4 m;
    t3d_mat4_identity(&m);
    m.m[0][0] = s; m.m[1][1] = s; m.m[2][2] = s;
    for (int r = 0; r < 3; r++)
        m.m[3][r] = pos[r] + n[r] * 0.04f;
    t3d_mat4_to_fixed_3x4(fp, &m);

    t3d_matrix_push(fp);
    rspq_block_run(blk);
    t3d_matrix_pop(1);
}

void climber_render_draw(void) {
    const climber_t *c = climber_state();

    if (mat_sync_valid[mat_buf])
        rspq_syncpoint_wait(mat_sync[mat_buf]);
    mat_used = 0;

    /* Grip markers around the climber; the snap candidate pulses green. */
    int cand = -1;
    if (c->active != LIMB_NONE)
        cand = grips_nearest(c->limbs[c->active].tip, 0.42f, -1);

    int near[GRIP_DRAW_MAX];
    int n = grips_collect(c->hip, GRIP_DRAW_R, near, GRIP_DRAW_MAX);
    for (int i = 0; i < n; i++) {
        const grip_t *g = grip_get(near[i]);
        if (near[i] == cand) {
            float pulse = 0.085f + 0.025f * sinf((float)get_ticks_us() * 8e-6f);
            draw_marker(g->pos, g->n, pulse, blk_ready);
        } else {
            draw_marker(g->pos, g->n, 0.055f, blk_grip);
        }
    }
    if (cand >= 0) {
        bool drawn = false;
        for (int i = 0; i < n; i++)
            if (near[i] == cand) drawn = true;
        if (!drawn) {
            const grip_t *g = grip_get(cand);
            draw_marker(g->pos, g->n, 0.085f, blk_ready);
        }
    }

    /* Skeleton: torso + head boxes, two segments per limb. */
    draw_bone(c->hip, c->neck, 0.085f, c->wall_n, blk_suit);

    float head_base[3];
    v3_copy(head_base, c->head);
    v3_mad(head_base, c->up, -CL_HEAD_R);
    float head_top[3];
    v3_copy(head_top, c->head);
    v3_mad(head_top, c->up, CL_HEAD_R);
    draw_bone(head_base, head_top, CL_HEAD_R * 0.85f, c->wall_n, blk_skin);

    for (int l = 0; l < LIMB_COUNT; l++) {
        const climber_limb_t *lb = &c->limbs[l];
        draw_bone(lb->root, lb->mid, 0.048f, c->wall_n, blk_suit);
        draw_bone(lb->mid,  lb->tip, 0.040f, c->wall_n, blk_suit);
    }

    mat_sync[mat_buf] = rspq_syncpoint_new();
    mat_sync_valid[mat_buf] = true;
    mat_buf = (mat_buf + 1) % MAT_BUFS;
}
