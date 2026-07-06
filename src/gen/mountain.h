#pragma once

#include <stdint.h>

/* Fixed-seed procedural mountain (GDD 3.2). The heightmap lives in RAM
 * (Expansion Pak) and is evaluated once at boot; the renderer bakes it
 * into static Tiny3D chunks sized to the 70-vert RSP cache. */

#define MTN_SEED        0x63727578u   /* "crux" — GDD 3.2 */

#define MTN_CHUNKS      8             /* 8x8 chunk grid */
#define MTN_CHUNK_CELLS 7             /* 7x7 cells => 8x8 = 64 verts/chunk */
#define MTN_CHUNK_VERTS (MTN_CHUNK_CELLS + 1)
#define MTN_CELLS       (MTN_CHUNKS * MTN_CHUNK_CELLS)   /* 56 */
#define MTN_VERTS       (MTN_CELLS + 1)                  /* 57 */

#define MTN_CELL_SIZE   6.f           /* world units (~meters) per cell */
#define MTN_SIZE        (MTN_CELLS * MTN_CELL_SIZE)      /* 336 */
#define MTN_HALF        (MTN_SIZE * 0.5f)
#define MTN_PEAK_H      240.f

void  mountain_generate(void);

/* Grid accessors (ix/iz in 0..MTN_VERTS-1, clamped). */
float mountain_height_at(int ix, int iz);
void  mountain_normal_at(int ix, int iz, float out[3]);
uint32_t mountain_color_at(int ix, int iz);              /* 0xRRGGBBAA */

/* Bilinear world-space sample; 0 outside the footprint. */
float mountain_height(float x, float z);

/* Mesh-exact world-space sample: evaluates the triangle the renderer
 * actually draws (cells split i00-i01-i11 / i00-i11-i10), so grips and
 * limb tips sit flush on the visible surface. out_n may be NULL. */
float mountain_surface(float x, float z, float out_n[3]);
