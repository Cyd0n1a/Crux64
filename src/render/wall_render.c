#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>
#include <stdlib.h>

#include "wall_render.h"
#include "../gen/mountain.h"

/* A tall cylindrical cliff wall ringing the world at WORLD_WALL_R. Each of
 * WALL_SEG segments is one textured quad: its base sits on the terrain (buried
 * a little so undulations between segments never open a gap under it) and its
 * top rides WORLD_WALL_H above the ground. Quads carry their own vertices (no
 * sharing across the ring) so each gets a clean local UV — that keeps the 10.5
 * fixed-point ST inside int16 no matter how many times the tile repeats. */

#define WALL_SEG    96          /* segments around the ring */
#define WALL_SKIRT  12.0f       /* base buried this far below the surface, so
                                   terrain dips between samples never open a
                                   gap under the straight bottom edge (scaled
                                   with the world: wider ring-sample spacing +
                                   deeper relief need a deeper skirt) */
#define WALL_TILE   32          /* sprite is 32x32                          */
#define UV_UNIT     (WALL_TILE * 32.f)   /* ST units for one full tile (uv 1) */
#define WALL_TEX_W  6.f         /* metres of wall per horizontal tile repeat */
#define WALL_TEX_H  4.f         /* metres of wall per vertical tile repeat   */

/* One t3d_vert_load stays <= 70 verts: 17 quads * 4 = 68 per window. */
#define QUADS_PER_WIN 17

static sprite_t     *wall_spr;
static rspq_block_t *wall_blk;

static void pack_vert(T3DVertPacked *buf, int v, const float p[3],
                      const float nrm[3], float u, float vt) {
    T3DVec3 n = {{ nrm[0], nrm[1], nrm[2] }};
    t3d_vec3_norm(&n);
    uint16_t norm = t3d_vert_pack_normal(&n);
    int16_t s = (int16_t)(u  * UV_UNIT);
    int16_t t = (int16_t)(vt * UV_UNIT);
    T3DVertPacked *pk = &buf[v / 2];
    if (v & 1) {
        pk->posB[0] = (int16_t)p[0]; pk->posB[1] = (int16_t)p[1];
        pk->posB[2] = (int16_t)p[2]; pk->normB = norm; pk->rgbaB = 0xFFFFFFFFu;
        pk->stB[0] = s; pk->stB[1] = t;
    } else {
        pk->posA[0] = (int16_t)p[0]; pk->posA[1] = (int16_t)p[1];
        pk->posA[2] = (int16_t)p[2]; pk->normA = norm; pk->rgbaA = 0xFFFFFFFFu;
        pk->stA[0] = s; pk->stA[1] = t;
    }
}

void wall_render_init(void) {
    /* DFS is up (music_init ran first). A NULL sprite just means no wall
     * texture is available; we skip the whole draw rather than crash. */
    wall_spr = sprite_load("rom:/cliff-wall.sprite");
    if (!wall_spr)
        return;

    const float R    = WORLD_WALL_R;
    const float arc  = 2.f * (float)M_PI * R / WALL_SEG;   /* per-segment arc */
    const float uR   = arc / WALL_TEX_W;                   /* tile repeats/seg */
    const float vtop = (WORLD_WALL_H + WALL_SKIRT) / WALL_TEX_H;

    /* Column cache: position + terrain height at each ring longitude. */
    float cx[WALL_SEG + 1], cz[WALL_SEG + 1], cg[WALL_SEG + 1];
    for (int j = 0; j <= WALL_SEG; j++) {
        float a = 2.f * (float)M_PI * (float)j / WALL_SEG;
        cx[j] = R * cosf(a);
        cz[j] = R * sinf(a);
        cg[j] = mountain_surface(cx[j], cz[j], NULL);
    }

    rspq_block_begin();
    /* Textured, lit, fogged, opaque. RGB = texel * shade; alpha passes the
     * shade channel so tiny3d's fog blender fades the wall into the horizon.
     * The tile itself is uploaded per-frame in wall_render_draw(). */
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, SHADE, 0), (0, 0, 0, SHADE)));
    /* No back-face cull: one winding shows the inner face from anywhere inside
     * the ring without depending on which way the curve wraps. */
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    for (int base = 0; base < WALL_SEG; base += QUADS_PER_WIN) {
        int nq = WALL_SEG - base;
        if (nq > QUADS_PER_WIN) nq = QUADS_PER_WIN;
        int nv = nq * 4;

        T3DVertPacked *buf = malloc_uncached(sizeof(T3DVertPacked) * ((nv + 1) / 2));
        for (int q = 0; q < nq; q++) {
            int j = base + q;
            float nl[3] = { -cx[j]   / R, 0.f, -cz[j]   / R };   /* inward */
            float nr[3] = { -cx[j+1] / R, 0.f, -cz[j+1] / R };
            float lb[3] = { cx[j],   cg[j]   - WALL_SKIRT,  cz[j]   };
            float lt[3] = { cx[j],   cg[j]   + WORLD_WALL_H, cz[j]   };
            float rt[3] = { cx[j+1], cg[j+1] + WORLD_WALL_H, cz[j+1] };
            float rb[3] = { cx[j+1], cg[j+1] - WALL_SKIRT,  cz[j+1] };
            int v = q * 4;
            pack_vert(buf, v + 0, lb, nl, 0.f, 0.f);
            pack_vert(buf, v + 1, lt, nl, 0.f, vtop);
            pack_vert(buf, v + 2, rt, nr, uR,  vtop);
            pack_vert(buf, v + 3, rb, nr, uR,  0.f);
        }

        t3d_vert_load(buf, 0, nv);
        for (int q = 0; q < nq; q++) {
            int v = q * 4;
            t3d_tri_draw(v + 0, v + 1, v + 2);
            t3d_tri_draw(v + 0, v + 2, v + 3);
        }
    }
    t3d_tri_sync();

    /* Hand the shared terrain state back exactly as we found it. */
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_CULL_BACK);
    wall_blk = rspq_block_end();
}

void wall_render_draw(void) {
    if (!wall_blk)
        return;
    /* Resident tile for the block's textured draws; repeats both ways. */
    rdpq_sprite_upload(TILE0, wall_spr, &(rdpq_texparms_t){
        .s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE,
    });
    rspq_block_run(wall_blk);
}
