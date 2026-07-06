#include "mountain.h"
#include "noise.h"
#include <math.h>

static float heights[MTN_VERTS * MTN_VERTS];

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Peak profile x layered simplex: a radial cone gives the massif its
 * silhouette, a coarse fBm carves ridges and gullies into the flanks,
 * and a fine octave adds the small bumps that later become holds. */
static float sample_height(int ix, int iz) {
    float nx = ((float)ix / MTN_CELLS) * 2.f - 1.f;   /* -1..1 */
    float nz = ((float)iz / MTN_CELLS) * 2.f - 1.f;
    float r  = clampf(sqrtf(nx * nx + nz * nz), 0.f, 1.f);

    float profile = powf(1.f - r, 1.6f);

    float ridges = noise_fbm2(nx * 1.9f, nz * 1.9f, 5, 2.05f, 0.5f);
    float detail = noise_fbm2(nx * 9.f + 37.f, nz * 9.f - 11.f, 2, 2.1f, 0.5f);

    float h = MTN_PEAK_H * profile * (0.82f + 0.30f * ridges)
            + detail * 7.f * (0.2f + profile);
    return h < 0.f ? 0.f : h;
}

void mountain_generate(void) {
    noise_seed(MTN_SEED);
    for (int iz = 0; iz < MTN_VERTS; iz++)
        for (int ix = 0; ix < MTN_VERTS; ix++)
            heights[iz * MTN_VERTS + ix] = sample_height(ix, iz);
}

float mountain_height_at(int ix, int iz) {
    if (ix < 0) ix = 0; else if (ix >= MTN_VERTS) ix = MTN_VERTS - 1;
    if (iz < 0) iz = 0; else if (iz >= MTN_VERTS) iz = MTN_VERTS - 1;
    return heights[iz * MTN_VERTS + ix];
}

void mountain_normal_at(int ix, int iz, float out[3]) {
    float dx = (mountain_height_at(ix + 1, iz) - mountain_height_at(ix - 1, iz))
             / (2.f * MTN_CELL_SIZE);
    float dz = (mountain_height_at(ix, iz + 1) - mountain_height_at(ix, iz - 1))
             / (2.f * MTN_CELL_SIZE);
    float len = sqrtf(dx * dx + 1.f + dz * dz);
    out[0] = -dx / len;
    out[1] =  1.f / len;
    out[2] = -dz / len;
}

/* Albedo from slope + altitude (GDD 3.1: Gouraud via vertex colors).
 * Steep = bare rock, gentle high ground = snow, gentle low = scree.
 * A slow noise jitters the bands so nothing reads as a hard contour. */
uint32_t mountain_color_at(int ix, int iz) {
    float h = mountain_height_at(ix, iz);
    float n[3];
    mountain_normal_at(ix, iz, n);
    float ny = n[1];

    float jit = noise_simplex2(ix * 0.31f + 91.f, iz * 0.31f - 53.f);

    /* Base rock: warm grey, mottled. */
    float rr = 104.f + 16.f * jit;
    float gg = 100.f + 14.f * jit;
    float bb =  96.f + 12.f * jit;

    /* Snow settles where it's high and not too steep. */
    float snowline = 128.f + 22.f * jit;
    float snow = clampf((h - snowline) / 24.f, 0.f, 1.f)
               * clampf((ny - 0.55f) / 0.18f, 0.f, 1.f);
    rr += (232.f - rr) * snow;
    gg += (238.f - gg) * snow;
    bb += (246.f - bb) * snow;

    /* Mossy scree on the gentle low apron. */
    float scree = clampf((45.f - h) / 30.f, 0.f, 1.f)
                * clampf((ny - 0.72f) / 0.15f, 0.f, 1.f);
    rr += (92.f  - rr) * scree;
    gg += (106.f - gg) * scree;
    bb += (78.f  - bb) * scree;

    uint32_t r = (uint32_t)clampf(rr, 0.f, 255.f);
    uint32_t g = (uint32_t)clampf(gg, 0.f, 255.f);
    uint32_t b = (uint32_t)clampf(bb, 0.f, 255.f);
    return (r << 24) | (g << 16) | (b << 8) | 0xFF;
}

float mountain_surface(float x, float z, float out_n[3]) {
    float gx = (x + MTN_HALF) / MTN_CELL_SIZE;
    float gz = (z + MTN_HALF) / MTN_CELL_SIZE;
    gx = clampf(gx, 0.f, (float)MTN_CELLS - 0.001f);
    gz = clampf(gz, 0.f, (float)MTN_CELLS - 0.001f);

    int ix = (int)gx, iz = (int)gz;
    float fx = gx - ix, fz = gz - iz;

    float h00 = mountain_height_at(ix,     iz);
    float h10 = mountain_height_at(ix + 1, iz);
    float h01 = mountain_height_at(ix,     iz + 1);
    float h11 = mountain_height_at(ix + 1, iz + 1);

    /* Renderer splits each cell into (i00,i01,i11) and (i00,i11,i10);
     * the first covers fz >= fx. Plane gradients per triangle: */
    float dhx, dhz, h;
    if (fz >= fx) {
        dhx = h11 - h01;
        dhz = h01 - h00;
        h   = h00 + dhz * fz + dhx * fx;
    } else {
        dhx = h10 - h00;
        dhz = h11 - h10;
        h   = h00 + dhx * fx + dhz * fz;
    }

    if (out_n) {
        float sx = dhx / MTN_CELL_SIZE;
        float sz = dhz / MTN_CELL_SIZE;
        float len = sqrtf(sx * sx + 1.f + sz * sz);
        out_n[0] = -sx / len;
        out_n[1] =  1.f / len;
        out_n[2] = -sz / len;
    }
    return h;
}

float mountain_height(float x, float z) {
    float gx = (x + MTN_HALF) / MTN_CELL_SIZE;
    float gz = (z + MTN_HALF) / MTN_CELL_SIZE;
    if (gx < 0.f || gz < 0.f || gx > MTN_CELLS || gz > MTN_CELLS)
        return 0.f;

    int ix = (int)gx, iz = (int)gz;
    float fx = gx - ix, fz = gz - iz;

    float h00 = mountain_height_at(ix,     iz);
    float h10 = mountain_height_at(ix + 1, iz);
    float h01 = mountain_height_at(ix,     iz + 1);
    float h11 = mountain_height_at(ix + 1, iz + 1);

    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return h0 + (h1 - h0) * fz;
}
