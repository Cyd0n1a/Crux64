#pragma once

#include <stdint.h>

/* Fixed-seed 2D Simplex noise (GDD 3.2). Kept libdragon-free so the
 * generator can be compiled and validated on the host. */

void  noise_seed(uint32_t seed);
float noise_simplex2(float x, float y);                 /* ~[-1, 1] */
float noise_fbm2(float x, float y, int octaves,
                 float lacunarity, float gain);         /* ~[-1, 1] */
