#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
#if defined(__SSE2__) && !defined(__aarch64__) && !defined(__arm__)
#include <xmmintrin.h>
#endif

static inline int32_t hexachord_round_sample(float value) {
#if defined(__arm__) && defined(__VFP_FP__)
    float bits;
    // VCVTR preserves lrintf's rounding mode without a per-sample library call.
    __asm__("vcvtr.s32.f32 %0, %1" : "=t"(bits) : "t"(value));
    int32_t rounded;
    memcpy(&rounded, &bits, sizeof rounded);
    return rounded;
#else
    return (int32_t)lrintf(value);
#endif
}

static inline uint64_t hexachord_fp_read(void) {
#if defined(__aarch64__)
    uint64_t value; __asm__ volatile("mrs %0, fpcr" : "=r"(value)); return value;
#elif defined(__arm__) && defined(__VFP_FP__)
    uint32_t value; __asm__ volatile("vmrs %0, fpscr" : "=r"(value)); return value;
#elif defined(__SSE2__)
    return _mm_getcsr();
#else
    return 0;
#endif
}
static inline void hexachord_fp_restore(uint64_t value) {
#if defined(__aarch64__)
    __asm__ volatile("msr fpcr, %0" :: "r"(value) : "memory");
#elif defined(__arm__) && defined(__VFP_FP__)
    __asm__ volatile("vmsr fpscr, %0" :: "r"((uint32_t)value) : "memory");
#elif defined(__SSE2__)
    _mm_setcsr((unsigned)value);
#else
    (void)value;
#endif
}
static inline uint64_t hexachord_fp_enter(void) {
    const uint64_t previous = hexachord_fp_read();
#if defined(__aarch64__) || (defined(__arm__) && defined(__VFP_FP__))
    hexachord_fp_restore((previous & ~(3ull << 22)) | (1ull << 24));
#elif defined(__SSE2__)
    hexachord_fp_restore(previous | (1u << 15) | (1u << 6));
#endif
    return previous;
}
