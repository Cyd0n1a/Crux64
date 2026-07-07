#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>
#include <stdlib.h>

#include "scatter_render.h"
#include "../gen/scatter.h"
#include "../gen/tree_gen.h"
#include "../gen/mountain.h"

/* proctree parameters per tree variant. Kept low (even ring counts —
 * proctree overruns its vertex budget on odd mSegments) so a tree is a
 * few hundred verts, not a few thousand. Different seeds give distinct
 * silhouettes; steps=3 on the last makes it a touch bushier. */
static const struct { int seed, seg, lvl, steps; float twig; }
TREE_PARAM[SCAT_TREE_VARIANTS] = {
    { 262, 4, 2, 2, 0.50f },
    {   7, 4, 2, 2, 0.52f },
    {  99, 4, 2, 3, 0.46f },
};

/* Foliage tint per variant (0xRRGGBBAA), deep alpine conifer greens. */
static const uint32_t FOLIAGE[SCAT_TREE_VARIANTS] = {
    0x2C5A28FF, 0x36602EFF, 0x284A24FF,
};
#define BARK_COLOR 0x54381EFF   /* matches the campfire logs */

/* Per-instance draw distances (m). Trees are the heavy mesh, so they
 * cull sooner than their size alone would suggest; rocks are tiny and
 * pop in close; boulders are big landmarks kept visible far out. */
static const float KIND_DIST[SCAT_KIND_COUNT] = {
    [SCAT_TREE] = 150.f, [SCAT_ROCK] = 88.f, [SCAT_BOULDER] = 175.f,
};

typedef struct { rspq_block_t *blk; float height; } tree_mesh_blk_t;

static tree_mesh_blk_t tree_blk[SCAT_TREE_VARIANTS];
static rspq_block_t   *rock_blk[SCAT_ROCK_VARIANTS];
static rspq_block_t   *boulder_blk[SCAT_BOULDER_VARIANTS];

/* One static matrix per scatter instance, and a cached world-space
 * cull sphere (center + radius). */
static T3DMat4FP *inst_mat;
static T3DVec3   *inst_center;
static float     *inst_radius;
static int        inst_count;

/* ------------------------------------------------------------------ */

static void pack_into(T3DVertPacked *buf, int v, const float p[3],
                      const float nrm[3], uint32_t color) {
    T3DVec3 n = {{ nrm[0], nrm[1], nrm[2] }};
    t3d_vec3_norm(&n);
    uint16_t norm = t3d_vert_pack_normal(&n);
    T3DVertPacked *pk = &buf[v / 2];
    if (v & 1) {
        pk->posB[0] = (int16_t)p[0]; pk->posB[1] = (int16_t)p[1];
        pk->posB[2] = (int16_t)p[2]; pk->normB = norm; pk->rgbaB = color;
    } else {
        pk->posA[0] = (int16_t)p[0]; pk->posA[1] = (int16_t)p[1];
        pk->posA[2] = (int16_t)p[2]; pk->normA = norm; pk->rgbaA = color;
    }
}

/* Bake an indexed mesh into a recorded rspq block, splitting it into
 * <=70-vertex windows for the RSP vertex cache. The per-window packed
 * buffers are allocated uncached and kept for the life of the program
 * (the block DMAs from them every frame). Positions are authored in a
 * fixed-point-friendly unit space and scaled by the instance matrix.
 * VSCALE bakes the unit mesh at a larger integer size so the int16
 * vertex positions keep precision; matrices divide it back out. */
#define VSCALE 256.f

static rspq_block_t *bake_mesh(const float *pos, const float *nrm,
                               const uint32_t *col, int nvert,
                               const int *idx, int ntri) {
    int *slot = malloc(sizeof(int) * nvert);
    for (int i = 0; i < nvert; i++) slot[i] = -1;
    int  batch_orig[70];
    uint8_t (*ltri)[3] = malloc(sizeof(uint8_t) * 3 * (ntri > 0 ? ntri : 1));
    int used = 0, ntl = 0;

    rspq_block_begin();
    for (int t = 0; t <= ntri; t++) {
        int v[3] = { 0, 0, 0 }, need = 0;
        bool last = (t == ntri);
        if (!last) {
            v[0] = idx[t * 3]; v[1] = idx[t * 3 + 1]; v[2] = idx[t * 3 + 2];
            for (int k = 0; k < 3; k++)
                if (slot[v[k]] < 0) need++;
        }

        if (last || used + need > 70) {
            if (used > 0) {
                T3DVertPacked *buf =
                    malloc_uncached(sizeof(T3DVertPacked) * ((used + 1) / 2));
                for (int i = 0; i < used; i++) {
                    int o = batch_orig[i];
                    pack_into(buf, i, &pos[o * 3], &nrm[o * 3], col[o]);
                }
                t3d_vert_load(buf, 0, used);
                for (int i = 0; i < ntl; i++)
                    t3d_tri_draw(ltri[i][0], ltri[i][1], ltri[i][2]);
            }
            for (int i = 0; i < used; i++) slot[batch_orig[i]] = -1;
            used = 0; ntl = 0;
            if (last) break;
        }

        for (int k = 0; k < 3; k++)
            if (slot[v[k]] < 0) { slot[v[k]] = used; batch_orig[used++] = v[k]; }
        ltri[ntl][0] = (uint8_t)slot[v[0]];
        ltri[ntl][1] = (uint8_t)slot[v[1]];
        ltri[ntl][2] = (uint8_t)slot[v[2]];
        ntl++;
    }
    t3d_tri_sync();
    rspq_block_t *blk = rspq_block_end();

    free(slot);
    free(ltri);
    return blk;
}

/* ------------------------------------------------------------------ */

static uint32_t jitter_color(uint32_t base, uint32_t h, int amp) {
    int dr = (int)((h >> 3) & 15) - 8;
    int dg = (int)((h >> 9) & 15) - 8;
    int db = (int)((h >> 15) & 15) - 8;
    int r = (int)((base >> 24) & 0xFF) + dr * amp / 8;
    int g = (int)((base >> 16) & 0xFF) + dg * amp / 8;
    int b = (int)((base >>  8) & 0xFF) + db * amp / 8;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
}

static void build_tree(int variant) {
    tree_mesh_t m;
    tree_gen(&m, TREE_PARAM[variant].seed, TREE_PARAM[variant].seg,
             TREE_PARAM[variant].lvl, TREE_PARAM[variant].steps,
             TREE_PARAM[variant].twig, 2.4f);

    int nv = m.nvert + m.ntwigvert;
    /* Branch faces once (closed tubes → back-face cull is fine); twig
     * leaf-cards are single quads, so emit each twig triangle both
     * windings to show foliage from both sides under one draw state. */
    int nt = m.nface + m.ntwigface * 2;

    float *pos = malloc(sizeof(float) * 3 * nv);
    float *nrm = malloc(sizeof(float) * 3 * nv);
    uint32_t *col = malloc(sizeof(uint32_t) * nv);
    int *idx = malloc(sizeof(int) * 3 * nt);

    float height = 0.f;
    for (int i = 0; i < m.nvert; i++) {
        for (int k = 0; k < 3; k++) {
            pos[i * 3 + k] = m.vpos[i * 3 + k] * VSCALE;
            nrm[i * 3 + k] = m.vnrm[i * 3 + k];
        }
        if (m.vpos[i * 3 + 1] > height) height = m.vpos[i * 3 + 1];
        col[i] = jitter_color(BARK_COLOR, (uint32_t)(i * 2654435761u), 6);
    }
    for (int i = 0; i < m.ntwigvert; i++) {
        int d = m.nvert + i;
        for (int k = 0; k < 3; k++) {
            pos[d * 3 + k] = m.tpos[i * 3 + k] * VSCALE;
            nrm[d * 3 + k] = m.tnrm[i * 3 + k];
        }
        if (m.tpos[i * 3 + 1] > height) height = m.tpos[i * 3 + 1];
        col[d] = jitter_color(FOLIAGE[variant], (uint32_t)(i * 40503u + 7u), 10);
    }

    int w = 0;
    for (int i = 0; i < m.nface; i++) {
        idx[w++] = m.vidx[i * 3];
        idx[w++] = m.vidx[i * 3 + 1];
        idx[w++] = m.vidx[i * 3 + 2];
    }
    for (int i = 0; i < m.ntwigface; i++) {
        int a = m.tidx[i * 3] + m.nvert;
        int b = m.tidx[i * 3 + 1] + m.nvert;
        int c = m.tidx[i * 3 + 2] + m.nvert;
        idx[w++] = a; idx[w++] = b; idx[w++] = c;
        idx[w++] = a; idx[w++] = c; idx[w++] = b;   /* back side */
    }

    tree_blk[variant].blk    = bake_mesh(pos, nrm, col, nv, idx, nt);
    tree_blk[variant].height = height;

    free(pos); free(nrm); free(col); free(idx);
    tree_free(&m);
}

/* A lumpy 6-vertex blob (octahedron with hashed per-vertex radii,
 * squashed on Y so it reads as a boulder, not a top). Moss greens the
 * upward faces; bare grey underneath. */
static rspq_block_t *build_rock(uint32_t seed, bool boulder) {
    static const float dir[6][3] = {
        {  0,  1,  0 }, {  0, -1,  0 }, {  1,  0,  0 },
        { -1,  0,  0 }, {  0,  0,  1 }, {  0,  0, -1 },
    };
    /* Same face winding as the campfire octa (CCW from outside). */
    static const int face[8][3] = {
        { 0, 4, 2 }, { 0, 2, 5 }, { 0, 5, 3 }, { 0, 3, 4 },
        { 1, 2, 4 }, { 1, 5, 2 }, { 1, 3, 5 }, { 1, 4, 3 },
    };
    uint32_t moss = boulder ? 0x5A6848FF : 0x4C6234FF;   /* upward, mossy */
    uint32_t bare = boulder ? 0x78787CFF : 0x6E6A60FF;   /* rock grey     */

    float pos[18], nrm[18];
    uint32_t col[6];
    uint32_t h = seed | 1u;
    for (int i = 0; i < 6; i++) {
        h = h * 1664525u + 1013904223u;
        float r = 0.72f + (float)((h >> 8) & 255) * (1.f / 255.f) * 0.5f;
        float sy = boulder ? 0.82f : 0.66f;   /* squash flatter for rocks */
        float p[3] = { dir[i][0] * r, dir[i][1] * r * sy, dir[i][2] * r };
        for (int k = 0; k < 3; k++) {
            pos[i * 3 + k] = p[k] * VSCALE;
            nrm[i * 3 + k] = dir[i][k];   /* convex blob: normal ~ direction */
        }
        float up = dir[i][1] * 0.5f + 0.5f;           /* 1 top, 0 bottom */
        uint32_t base = up > 0.55f ? moss : bare;
        col[i] = jitter_color(base, h, 8);
    }
    int idx[24];
    for (int f = 0; f < 8; f++)
        for (int k = 0; k < 3; k++)
            idx[f * 3 + k] = face[f][k];
    return bake_mesh(pos, nrm, col, 6, idx, 8);
}

/* ------------------------------------------------------------------ */

static void build_instance_matrix(int out, const scatter_t *s, float mesh_h) {
    float cy = cosf(s->yaw), sy = sinf(s->yaw);
    float sc;      /* unit-mesh -> world, then / VSCALE for the baked size */
    float sink;    /* how far to drop the origin below the surface */

    if (s->kind == SCAT_TREE) {
        sc   = (s->scale / mesh_h) / VSCALE;   /* scale to target height */
        sink = 0.15f;
    } else {
        sc   = s->scale / VSCALE;              /* scale = radius */
        sink = s->scale * (s->kind == SCAT_BOULDER ? 0.30f : 0.24f);
    }

    T3DMat4 m;
    t3d_mat4_identity(&m);
    m.m[0][0] =  cy * sc; m.m[0][2] = -sy * sc;
    m.m[2][0] =  sy * sc; m.m[2][2] =  cy * sc;
    m.m[1][1] = sc;
    m.m[3][0] = s->pos[0];
    m.m[3][1] = s->pos[1] - sink;
    m.m[3][2] = s->pos[2];
    t3d_mat4_to_fixed_3x4(&inst_mat[out], &m);

    inst_center[out] = (T3DVec3){{ s->pos[0],
                                   s->pos[1] + (s->kind == SCAT_TREE
                                                ? s->scale * 0.5f : 0.f),
                                   s->pos[2] }};
    inst_radius[out] = s->kind == SCAT_TREE ? s->scale * 0.7f
                                            : s->scale * 1.3f;
}

void scatter_render_init(void) {
    for (int v = 0; v < SCAT_TREE_VARIANTS; v++)
        build_tree(v);
    for (int v = 0; v < SCAT_ROCK_VARIANTS; v++)
        rock_blk[v] = build_rock(0xA53F00u + v * 0x9E37u, false);
    for (int v = 0; v < SCAT_BOULDER_VARIANTS; v++)
        boulder_blk[v] = build_rock(0x1B0C55u + v * 0x85EBu, true);

    inst_count  = scatter_count();
    inst_mat    = malloc_uncached(sizeof(T3DMat4FP) * inst_count);
    inst_center = malloc(sizeof(T3DVec3) * inst_count);
    inst_radius = malloc(sizeof(float) * inst_count);

    for (int i = 0; i < inst_count; i++) {
        const scatter_t *s = scatter_get(i);
        float mh = s->kind == SCAT_TREE ? tree_blk[s->variant].height : 1.f;
        build_instance_matrix(i, s, mh);
    }
}

static rspq_block_t *block_for(const scatter_t *s) {
    switch (s->kind) {
        case SCAT_TREE:    return tree_blk[s->variant].blk;
        case SCAT_ROCK:    return rock_blk[s->variant];
        case SCAT_BOULDER: return boulder_blk[s->variant];
        default:           return NULL;
    }
}

void scatter_render_draw(const T3DVec3 *eye, const T3DVec3 *target) {
    if (inst_count <= 0)
        return;

    T3DVec3 fwd = {{ target->v[0] - eye->v[0],
                     target->v[1] - eye->v[1],
                     target->v[2] - eye->v[2] }};
    t3d_vec3_norm(&fwd);

    for (int i = 0; i < inst_count; i++) {
        const scatter_t *s = scatter_get(i);
        T3DVec3 to = {{ inst_center[i].v[0] - eye->v[0],
                        inst_center[i].v[1] - eye->v[1],
                        inst_center[i].v[2] - eye->v[2] }};
        float along = t3d_vec3_dot(&to, &fwd);
        if (along < -inst_radius[i])
            continue;
        float d2 = t3d_vec3_dot(&to, &to);
        float reach = KIND_DIST[s->kind] + inst_radius[i];
        if (d2 > reach * reach)
            continue;

        t3d_matrix_push(&inst_mat[i]);
        rspq_block_run(block_for(s));
        t3d_matrix_pop(1);
    }
}
