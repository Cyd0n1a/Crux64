#include "rsp_synth.h"
#include <malloc.h>
#include <string.h>

DEFINE_RSP_UCODE(rsp_synth);
uint32_t rsp_synth_id;

void rsp_synth_init(void) {
    void* state = UncachedAddr(rspq_overlay_get_state(&rsp_synth));
    memset(state, 0, 4); // clear prng state? we can leave it or init it
    ((uint32_t*)state)[0] = 0x63727578; // initial seed
    
    rsp_synth_id = rspq_overlay_register(&rsp_synth);
}

void rsp_synth_white_noise(float *buffer, uint16_t count) {
    // CMD 0 is SynthCmd_WhiteNoise
    rspq_write(rsp_synth_id, 0, count << 16, PhysicalAddr(buffer) & 0xFFFFFF);
}
