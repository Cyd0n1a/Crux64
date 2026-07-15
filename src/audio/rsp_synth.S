#include <rsp_queue.inc>

    .set noreorder
    .set at

    .data
RSPQ_BeginOverlayHeader
    RSPQ_DefineCommand SynthCmd_WhiteNoise, 8
RSPQ_EndOverlayHeader

RSPQ_BeginSavedState
    .align 4
PRNG_STATE: .word 0x63727578
RSPQ_EndSavedState

    .bss
    .align 4
DMA_BUFFER: .space 1024

    .text

    # a0: [upper 16: len in 32-bit words] [lower 16: cmd]
    # a1: RDRAM pointer
SynthCmd_WhiteNoise:
    srl s6, a0, 16        # s6 = count in words
    move s1, a1           # s1 = rdram ptr
    
    la t0, PRNG_STATE
    lw s3, 0(t0)          # s3 = rng state
    lui s5, 0x3F80        # float 1.0 mask
    li s7, 0x007FFFFF     # mantissa mask

OuterLoop:
    blez s6, Done
    li s2, 256            # 256 words = 1024 bytes
    bgt s6, s2, 1f
    nop
    move s2, s6           # s2 = chunk size in words
1:
    sub s6, s2            # remaining count -= chunk
    
    la t2, DMA_BUFFER
    move t4, s2           # loop counter
    
InnerLoop:
    # rng ^= rng << 13
    sll t5, s3, 13
    xor s3, s3, t5
    # rng ^= rng >> 17
    srl t5, s3, 17
    xor s3, s3, t5
    # rng ^= rng << 5
    sll t5, s3, 5
    xor s3, s3, t5
    
    # map to float [1.0, 2.0) -> mantissa is lower 23 bits
    and t5, s3, s7
    or t5, t5, s5
    
    sw t5, 0(t2)
    addiu t2, 4
    
    addiu t4, -1
    bgtz t4, InnerLoop
    nop
    
    # DMA chunk to RDRAM
    move s0, s1           # rdram
    la s4, DMA_BUFFER     # dmem
    sll t5, s2, 2         # size in bytes (words * 4)
    addi t0, t5, -1       # t0 = bytes - 1
    
    # Save volatile state before jal
    addiu sp, -4
    sw ra, 0(sp)
    
    jal DMAOut            # Wait, DMAOut destroys at, t2
    nop
    
    # restore volatile state
    lw ra, 0(sp)
    addiu sp, 4
    
    sll t5, s2, 2
    add s1, t5            # rdram += bytes
    
    j OuterLoop
    nop

Done:
    la t0, PRNG_STATE
    sw s3, 0(t0)          # save rng state
    jr ra
    nop
