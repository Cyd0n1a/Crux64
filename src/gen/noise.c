#include "noise.h"
#include <math.h>

static uint8_t perm[512];

void noise_seed(uint32_t seed) {
    uint8_t p[256];
    for (int i = 0; i < 256; i++) p[i] = i;

    /* xorshift32-driven Fisher-Yates: same seed, same mountain. */
    uint32_t s = seed ? seed : 1u;
    for (int i = 255; i > 0; i--) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        int j = s % (uint32_t)(i + 1);
        uint8_t t = p[i]; p[i] = p[j]; p[j] = t;
    }
    for (int i = 0; i < 512; i++) perm[i] = p[i & 255];
}

static float grad2(int hash, float x, float y) {
    switch (hash & 7) {
    case 0: return  x + y;
    case 1: return  x - y;
    case 2: return -x + y;
    case 3: return -x - y;
    case 4: return  x;
    case 5: return -x;
    case 6: return  y;
    default: return -y;
    }
}

/* Classic 2D simplex noise (Gustavson's public-domain formulation). */
float noise_simplex2(float x, float y) {
    const float F2 = 0.3660254038f;   /* (sqrt(3)-1)/2 */
    const float G2 = 0.2113248654f;   /* (3-sqrt(3))/6 */

    float s = (x + y) * F2;
    int i = (int)floorf(x + s);
    int j = (int)floorf(y + s);

    float t  = (i + j) * G2;
    float x0 = x - (i - t);
    float y0 = y - (j - t);

    int i1 = (x0 > y0) ? 1 : 0;
    int j1 = 1 - i1;

    float x1 = x0 - i1 + G2;
    float y1 = y0 - j1 + G2;
    float x2 = x0 - 1.f + 2.f * G2;
    float y2 = y0 - 1.f + 2.f * G2;

    int ii = i & 255, jj = j & 255;
    float n = 0.f;

    float t0 = 0.5f - x0 * x0 - y0 * y0;
    if (t0 > 0.f) {
        t0 *= t0;
        n += t0 * t0 * grad2(perm[ii + perm[jj]], x0, y0);
    }
    float t1 = 0.5f - x1 * x1 - y1 * y1;
    if (t1 > 0.f) {
        t1 *= t1;
        n += t1 * t1 * grad2(perm[ii + i1 + perm[jj + j1]], x1, y1);
    }
    float t2 = 0.5f - x2 * x2 - y2 * y2;
    if (t2 > 0.f) {
        t2 *= t2;
        n += t2 * t2 * grad2(perm[ii + 1 + perm[jj + 1]], x2, y2);
    }
    return 70.f * n;
}

float noise_fbm2(float x, float y, int octaves, float lacunarity, float gain) {
    float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int o = 0; o < octaves; o++) {
        sum  += amp * noise_simplex2(x * freq, y * freq);
        norm += amp;
        amp  *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}
