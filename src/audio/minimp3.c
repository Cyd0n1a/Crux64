/* minimp3 implementation translation unit.
 *
 * The decoder is vendored as a single public-domain header (minimp3.h);
 * this is the one place its implementation is emitted. NO_SIMD because the
 * N64's VR4300 has none of the x86/ARM vector paths minimp3 optimises for.
 * Third-party code, so its object is built with -Wno-error (see Makefile). */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"
