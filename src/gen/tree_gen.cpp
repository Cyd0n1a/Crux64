/* C++ shim over the vendored proctree generator. Runs proctree with a
 * low-poly parameter set suited to the N64 (a few hundred verts per
 * tree, not a few thousand) and copies its output into plain-C malloc'd
 * arrays so scatter_render.c can bake them into Tiny3D vertex buffers
 * without ever touching the C++ Proctree namespace. */

#include <stdlib.h>
#include <string.h>

#include "proctree.h"
#include "tree_gen.h"

static float *copy_vec3(const Proctree::fvec3 *src, int n) {
    float *dst = (float *)malloc(sizeof(float) * 3 * n);
    for (int i = 0; i < n; i++) {
        dst[i * 3 + 0] = src[i].x;
        dst[i * 3 + 1] = src[i].y;
        dst[i * 3 + 2] = src[i].z;
    }
    return dst;
}

static float *copy_vec2(const Proctree::fvec2 *src, int n) {
    float *dst = (float *)malloc(sizeof(float) * 2 * n);
    for (int i = 0; i < n; i++) {
        dst[i * 2 + 0] = src[i].u;
        dst[i * 2 + 1] = src[i].v;
    }
    return dst;
}

static int *copy_ivec3(const Proctree::ivec3 *src, int n) {
    int *dst = (int *)malloc(sizeof(int) * 3 * n);
    for (int i = 0; i < n; i++) {
        dst[i * 3 + 0] = src[i].x;
        dst[i * 3 + 1] = src[i].y;
        dst[i * 3 + 2] = src[i].z;
    }
    return dst;
}

extern "C" void tree_gen(tree_mesh_t *out, int seed, int segments,
                         int levels, int steps, float twig_scale,
                         float trunk_len) {
    /* Force an even ring count: proctree's vertex-budget precalc is only
     * correct for even mSegments (see tree_gen.h). */
    if (segments < 4) segments = 4;
    if (segments & 1) segments++;

    Proctree::Tree tree;
    tree.mProperties.mSeed        = seed;
    tree.mProperties.mSegments    = segments;
    tree.mProperties.mLevels      = levels;
    tree.mProperties.mTreeSteps   = steps;
    tree.mProperties.mTwigScale   = twig_scale;
    tree.mProperties.mTrunkLength = trunk_len;
    tree.generate();

    out->nvert     = tree.mVertCount;
    out->nface     = tree.mFaceCount;
    out->ntwigvert = tree.mTwigVertCount;
    out->ntwigface = tree.mTwigFaceCount;
    out->vpos = copy_vec3(tree.mVert,       tree.mVertCount);
    out->vnrm = copy_vec3(tree.mNormal,     tree.mVertCount);
    out->vidx = copy_ivec3(tree.mFace,      tree.mFaceCount);
    out->tpos = copy_vec3(tree.mTwigVert,   tree.mTwigVertCount);
    out->tnrm = copy_vec3(tree.mTwigNormal, tree.mTwigVertCount);
    out->tuv  = copy_vec2(tree.mTwigUV,     tree.mTwigVertCount);
    out->tidx = copy_ivec3(tree.mTwigFace,  tree.mTwigFaceCount);
    /* tree's own new[] buffers are released by its destructor here. */
}

extern "C" void tree_free(tree_mesh_t *m) {
    free(m->vpos); free(m->vnrm); free(m->vidx);
    free(m->tpos); free(m->tnrm); free(m->tuv); free(m->tidx);
    memset(m, 0, sizeof(*m));
}
