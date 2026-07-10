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
    [SCAT_TREE] = 72.f, [SCAT_ROCK] = 55.f, [SCAT_BOULDER] = 130.f,
};

/* Horizontal frustum half-angle tangent (65° vertical FOV widened by the
 * 4:3 aspect ≈ 80° across, plus margin so nothing pops at the edges). */
#define FRUSTUM_TAN 1.20f

/* A tree is two draws: bark branches (untextured, culled) and leaf cards
 * (textured, alpha-cutout, no cull). They stay separate blocks so each can
 * carry its own RDP state. */
typedef struct { rspq_block_t *branch, *twig; float height; } tree_mesh_blk_t;

static tree_mesh_blk_t tree_blk[SCAT_TREE_VARIANTS];
static rspq_block_t   *rock_blk[SCAT_ROCK_VARIANTS];
static rspq_block_t   *boulder_blk[SCAT_BOULDER_VARIANTS];

/* Shared foliage texture (assets/tree-leaves.png -> ROM sprite). One 32x32
 * RGBA16 tile fits TMEM whole; every leaf card samples it. NULL if the load
 * fails, in which case trees fall back to bare branches (no crash). */
static sprite_t *leaf_spr;
#define LEAF_TEX 32   /* sprite is 32x32; twig UV 0..1 spans the whole tile */

/* One static matrix per scatter instance, and a cached world-space
 * cull sphere (center + radius). */
static T3DMat4FP *inst_mat;
static T3DVec3   *inst_center;
static float     *inst_radius;   /* far-cull bounding radius   */
static int        inst_count;

/* Near-skip radius: once the eye is this close, the mesh straddles the
 * camera near plane. Computed inline (no per-instance array) so the boot
 * allocation footprint stays identical to the pre-cull build. */
static inline float inst_near_radius(const scatter_t *s) {
    return s->kind == SCAT_TREE ? s->scale * 0.5f + 2.5f
                                : s->scale + 1.6f;
}

/* ------------------------------------------------------------------ */

static void pack_into(T3DVertPacked *buf, int v, const float p[3],
                      const float nrm[3], uint32_t color, const float uv[2]) {
    T3DVec3 n = {{ nrm[0], nrm[1], nrm[2] }};
    t3d_vec3_norm(&n);
    uint16_t norm = t3d_vert_pack_normal(&n);
    /* ST is 10.5 fixed point in pixel coords; 0..1 UV spans the whole tile. */
    int16_t s = 0, t = 0;
    if (uv) {
        s = (int16_t)(uv[0] * (LEAF_TEX * 32.f));
        t = (int16_t)(uv[1] * (LEAF_TEX * 32.f));
    }
    T3DVertPacked *pk = &buf[v / 2];
    if (v & 1) {
        pk->posB[0] = (int16_t)p[0]; pk->posB[1] = (int16_t)p[1];
        pk->posB[2] = (int16_t)p[2]; pk->normB = norm; pk->rgbaB = color;
        pk->stB[0] = s; pk->stB[1] = t;
    } else {
        pk->posA[0] = (int16_t)p[0]; pk->posA[1] = (int16_t)p[1];
        pk->posA[2] = (int16_t)p[2]; pk->normA = norm; pk->rgbaA = color;
        pk->stA[0] = s; pk->stA[1] = t;
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
                               const uint32_t *col, const float *uv, int nvert,
                               const int *idx, int ntri, bool textured) {
    int *slot = malloc(sizeof(int) * nvert);
    for (int i = 0; i < nvert; i++) slot[i] = -1;
    int  batch_orig[70];
    uint8_t (*ltri)[3] = malloc(sizeof(uint8_t) * 3 * (ntri > 0 ? ntri : 1));
    int used = 0, ntl = 0;

    rspq_block_begin();
    if (textured) {
        /* Leaf cards: modulate the leaf texture by the shaded vertex tint,
         * take alpha straight from the texture, and hard-cutout the silhouette.
         * Fog is off (its blender wants the alpha channel we're using for the
         * cutout — and trees sit well inside the fog-near plane anyway), and
         * culling is off so a single winding shows the card from both sides. */
        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, SHADE, 0), (0, 0, 0, TEX0)));
        rdpq_mode_fog(0);
        rdpq_mode_alphacompare(128);
        t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);
    }
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
                    pack_into(buf, i, &pos[o * 3], &nrm[o * 3], col[o],
                              uv ? &uv[o * 2] : NULL);
                }
                /* t3d_vert_load DMAs vertices in even pairs, so an odd
                 * count pulls one extra, uninitialised half-pack into the
                 * RSP and transforms it (never drawn — no tri indexes it).
                 * Fill it with a copy of vert 0 so nothing garbage is
                 * ever DMA'd. */
                if (used & 1) {
                    int o = batch_orig[0];
                    pack_into(buf, used, &pos[o * 3], &nrm[o * 3], col[o],
                              uv ? &uv[o * 2] : NULL);
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
    if (textured) {
        /* Hand the shared terrain/scatter state back untouched so the bark,
         * rocks and campfire that draw around us are unaffected. */
        rdpq_mode_alphacompare(0);
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_CULL_BACK);
    }
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

/* Raise a colour toward white by frac (0..1); keeps the hue but lifts the
 * value so a modulated texture reads through the tint rather than doubling
 * up on it (a green tint over green leaves goes muddy otherwise). */
static uint32_t lighten(uint32_t c, float frac) {
    int r = (int)((c >> 24) & 0xFF), g = (int)((c >> 16) & 0xFF),
        b = (int)((c >> 8) & 0xFF);
    r += (int)((255 - r) * frac);
    g += (int)((255 - g) * frac);
    b += (int)((255 - b) * frac);
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
}

static void build_tree(int variant) {
    tree_mesh_t m;
    tree_gen(&m, TREE_PARAM[variant].seed, TREE_PARAM[variant].seg,
             TREE_PARAM[variant].lvl, TREE_PARAM[variant].steps,
             TREE_PARAM[variant].twig, 2.4f);

    float height = 0.f;

    /* --- Bark branches: closed tubes, back-face culled, untextured. --- */
    float    *bpos = malloc(sizeof(float) * 3 * m.nvert);
    float    *bnrm = malloc(sizeof(float) * 3 * m.nvert);
    uint32_t *bcol = malloc(sizeof(uint32_t) * m.nvert);
    for (int i = 0; i < m.nvert; i++) {
        for (int k = 0; k < 3; k++) {
            bpos[i * 3 + k] = m.vpos[i * 3 + k] * VSCALE;
            bnrm[i * 3 + k] = m.vnrm[i * 3 + k];
        }
        if (m.vpos[i * 3 + 1] > height) height = m.vpos[i * 3 + 1];
        bcol[i] = jitter_color(BARK_COLOR, (uint32_t)(i * 2654435761u), 6);
    }
    int *bidx = malloc(sizeof(int) * 3 * m.nface);
    for (int i = 0; i < m.nface * 3; i++) bidx[i] = m.vidx[i];
    tree_blk[variant].branch = bake_mesh(bpos, bnrm, bcol, NULL,
                                         m.nvert, bidx, m.nface, false);
    free(bpos); free(bnrm); free(bcol); free(bidx);

    /* --- Leaf cards: textured, alpha-cutout, no cull (a single winding
     * shows both faces). Each card samples the whole leaf tile; a per-card
     * mirror breaks the obvious repeat. The tint is a lightened variant
     * green so the leaf photo's own colour carries through the modulate. --- */
    uint32_t tint = lighten(FOLIAGE[variant], 0.6f);
    float    *tpos = malloc(sizeof(float) * 3 * m.ntwigvert);
    float    *tnrm = malloc(sizeof(float) * 3 * m.ntwigvert);
    float    *tuv  = malloc(sizeof(float) * 2 * m.ntwigvert);
    uint32_t *tcol = malloc(sizeof(uint32_t) * m.ntwigvert);
    for (int i = 0; i < m.ntwigvert; i++) {
        for (int k = 0; k < 3; k++) {
            tpos[i * 3 + k] = m.tpos[i * 3 + k] * VSCALE;
            tnrm[i * 3 + k] = m.tnrm[i * 3 + k];
        }
        if (m.tpos[i * 3 + 1] > height) height = m.tpos[i * 3 + 1];
        /* proctree emits twigs as 8-vertex cards (two crossed quads); flip
         * the UV per card so neighbours don't look stamped from one photo. */
        uint32_t h = (uint32_t)(i / 8) * 2654435761u;
        float u = m.tuv[i * 2 + 0], v = m.tuv[i * 2 + 1];
        if (h & 0x10000u) u = 1.f - u;
        if (h & 0x20000u) v = 1.f - v;
        tuv[i * 2 + 0] = u; tuv[i * 2 + 1] = v;
        tcol[i] = jitter_color(tint, (uint32_t)(i * 40503u + 7u), 6);
    }
    int *tidx = malloc(sizeof(int) * 3 * m.ntwigface);
    for (int i = 0; i < m.ntwigface * 3; i++) tidx[i] = m.tidx[i];
    tree_blk[variant].twig = bake_mesh(tpos, tnrm, tcol, tuv,
                                       m.ntwigvert, tidx, m.ntwigface, true);
    free(tpos); free(tnrm); free(tuv); free(tcol);

    tree_blk[variant].height = height;
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
    return bake_mesh(pos, nrm, col, NULL, 6, idx, 8, false);
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
    /* DFS is already up (music_init runs first). A NULL here just means the
     * trees draw bare-branched — never a crash. */
    leaf_spr = sprite_load("rom:/tree-leaves.sprite");

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
        case SCAT_ROCK:    return rock_blk[s->variant];
        case SCAT_BOULDER: return boulder_blk[s->variant];
        default:           return NULL;
    }
}

static void draw_one(int i) {
    const scatter_t *s = scatter_get(i);
    t3d_matrix_push(&inst_mat[i]);
    if (s->kind == SCAT_TREE) {
        rspq_block_run(tree_blk[s->variant].branch);
        /* Leaf cards need the tile resident; scatter_render_draw uploads it
         * once before the tree pass. Skip if the texture failed to load. */
        if (leaf_spr)
            rspq_block_run(tree_blk[s->variant].twig);
    } else {
        rspq_block_run(block_for(s));
    }
    t3d_matrix_pop(1);
}

/* A tree is ~250 tris; a grove of them in frame would swamp the RSP (2fps
 * on cycle-accurate emulation), so trees get a hard per-frame budget and
 * only the nearest few actually draw. Rocks and boulders are ~8 tris, so
 * they draw freely once culled. */
#define TREE_BUDGET 6

typedef struct { int idx; float d2; } cand_t;
static cand_t tree_cand[512];   /* >= max placed instances */

void scatter_render_draw(const T3DVec3 *eye, const T3DVec3 *target) {
    if (inst_count <= 0)
        return;

    T3DVec3 fwd = {{ target->v[0] - eye->v[0],
                     target->v[1] - eye->v[1],
                     target->v[2] - eye->v[2] }};
    t3d_vec3_norm(&fwd);

    int ntree = 0;
    for (int i = 0; i < inst_count; i++) {
        const scatter_t *s = scatter_get(i);
        T3DVec3 to = {{ inst_center[i].v[0] - eye->v[0],
                        inst_center[i].v[1] - eye->v[1],
                        inst_center[i].v[2] - eye->v[2] }};
        float d2 = t3d_vec3_dot(&to, &to);

        /* Crash guard: never draw an instance the eye is inside (its
         * geometry would straddle the near plane). */
        float near = inst_near_radius(s);
        if (d2 < near * near)
            continue;

        float reach = KIND_DIST[s->kind] + inst_radius[i];
        if (d2 > reach * reach)
            continue;

        float along = t3d_vec3_dot(&to, &fwd);
        if (along < -inst_radius[i])
            continue;   /* behind the camera */

        /* Horizontal frustum reject: how far off the view axis the
         * instance sits vs. what the FOV allows at that depth. */
        float perp2 = d2 - along * along;
        if (perp2 < 0.f) perp2 = 0.f;
        float allowed = along * FRUSTUM_TAN + inst_radius[i];
        if (allowed > 0.f && perp2 > allowed * allowed)
            continue;

        if (s->kind == SCAT_TREE) {
            /* Insertion-sort into the nearest-first candidate list,
             * bounded to the budget so the list and the draw stay cheap. */
            int pos = ntree < TREE_BUDGET ? ntree : TREE_BUDGET - 1;
            if (ntree >= TREE_BUDGET && d2 >= tree_cand[pos].d2)
                continue;   /* farther than the current cutoff */
            if (ntree < TREE_BUDGET) ntree++;
            while (pos > 0 && tree_cand[pos - 1].d2 > d2) {
                tree_cand[pos] = tree_cand[pos - 1];
                pos--;
            }
            tree_cand[pos].idx = i;
            tree_cand[pos].d2  = d2;
        } else {
            draw_one(i);
        }
    }

    /* Upload the shared leaf tile once, just before the tree pass (rocks
     * above drew untextured, so nothing needed it yet). */
    if (ntree > 0 && leaf_spr)
        rdpq_sprite_upload(TILE0, leaf_spr, NULL);
    for (int t = 0; t < ntree; t++)
        draw_one(tree_cand[t].idx);
}
