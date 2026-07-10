#pragma once

#include <stdint.h>

/* C bridge over the vendored proctree generator (src/gen/proctree.cpp,
 * Paul Brunt / Jari Komppa, 3-clause BSD — C++). proctree itself is a
 * C++ TU; this shim exposes a plain-C surface so the renderer
 * (scatter_render.c) never has to include the C++ header.
 *
 * tree_gen() runs proctree once and hands back malloc'd, densely-packed
 * position/normal/index arrays for the branch mesh and the twig (leaf
 * card) mesh. Caller owns the buffers and frees them with tree_free().
 *
 * NOTE: proctree miscounts its vertex budget for ODD mSegments and
 * overruns the branch buffer (calcVertSizes uses floor(seg/2)-1 where
 * createForks writes ceil(seg/2)-1). tree_gen() forces mSegments even. */

typedef struct {
    int    nvert, nface;        /* branch mesh */
    int    ntwigvert, ntwigface;
    float *vpos, *vnrm;         /* nvert * 3 floats */
    int   *vidx;                /* nface * 3 ints  */
    float *tpos, *tnrm;         /* ntwigvert * 3 floats */
    float *tuv;                 /* ntwigvert * 2 floats (0..1 per leaf card) */
    int   *tidx;                /* ntwigface * 3 ints  */
} tree_mesh_t;

#ifdef __cplusplus
extern "C" {
#endif

void tree_gen(tree_mesh_t *out, int seed, int segments, int levels,
              int steps, float twig_scale, float trunk_len);
void tree_free(tree_mesh_t *m);

#ifdef __cplusplus
}
#endif
