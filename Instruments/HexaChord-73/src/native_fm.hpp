#pragma once
#include "native_math.hpp"

#if defined(__ARM_NEON) && !defined(HEXACHORD_SCALAR_FM)
#include <arm_neon.h>

namespace hexachord::native::fm {
using V = float32x4_t;
inline V splat(float x) { return vdupq_n_f32(x); }
inline V add(V a, V b) { return vaddq_f32(a, b); }
inline V sub(V a, V b) { return vsubq_f32(a, b); }
inline V mul(V a, V b) { return vmulq_f32(a, b); }
inline V scale(V a, float b) { return vmulq_n_f32(a, b); }
inline V madd(V accumulator, V a, V b) {
#if !defined(__aarch64__)
    // ARMv7 VMLA retains two roundings; AArch64 FMLA would change them.
    return vmlaq_f32(accumulator, a, b);
#else
    return add(accumulator, mul(a, b));
#endif
}
inline V scaleAdd(V accumulator, V value, float coefficient) {
#if !defined(__aarch64__)
    return vmlaq_n_f32(accumulator, value, coefficient);
#else
    return add(accumulator, scale(value, coefficient));
#endif
}
inline V scaleSub(V accumulator, V value, float coefficient) {
#if !defined(__aarch64__)
    return vmlsq_n_f32(accumulator, value, coefficient);
#else
    return sub(accumulator, scale(value, coefficient));
#endif
}
inline V four(float a, float b, float c, float d) {
    V result = vdupq_n_f32(a);
    result = vsetq_lane_f32(b, result, 1); result = vsetq_lane_f32(c, result, 2);
    return vsetq_lane_f32(d, result, 3);
}
inline V bound(V x, float low, float high) { return vminq_f32(vmaxq_f32(x, splat(low)), splat(high)); }
inline bool all(uint32x4_t mask) {
    const auto pair = vmin_u32(vget_low_u32(mask), vget_high_u32(mask));
    return vget_lane_u32(vpmin_u32(pair, pair), 0) == UINT32_MAX;
}
inline V divide(V numerator, V denominator) {
#if defined(__aarch64__)
    return vdivq_f32(numerator, denominator);
#else
    // Keep exact VFP division in the aliased scalar registers, avoiding lane transfers through core registers.
    register V result asm("q0") = numerator;
    register V divisor asm("q1") = denominator;
    asm("vdiv.f32 s0, s0, s4\n\t"
        "vdiv.f32 s1, s1, s5\n\t"
        "vdiv.f32 s2, s2, s6\n\t"
        "vdiv.f32 s3, s3, s7"
        : "+w"(result) : "w"(divisor));
    return result;
#endif
}
inline V divideMasked(V numerator, V denominator, uint32x4_t mask) {
#if defined(__aarch64__)
    (void)mask;
    return divide(numerator, denominator);
#else
    return four(vgetq_lane_u32(mask, 0) ? vgetq_lane_f32(numerator, 0) / vgetq_lane_f32(denominator, 0) : 0.f,
                vgetq_lane_u32(mask, 1) ? vgetq_lane_f32(numerator, 1) / vgetq_lane_f32(denominator, 1) : 0.f,
                vgetq_lane_u32(mask, 2) ? vgetq_lane_f32(numerator, 2) / vgetq_lane_f32(denominator, 2) : 0.f,
                vgetq_lane_u32(mask, 3) ? vgetq_lane_f32(numerator, 3) / vgetq_lane_f32(denominator, 3) : 0.f);
#endif
}
inline V divideConstant(V numerator, float denominator, double reciprocal) {
#if defined(__aarch64__)
    (void)reciprocal;
    return vdivq_f32(numerator, splat(denominator));
#else
    // Binary64 retains enough guard bits to round a binary32 constant quotient exactly.
    return four(float(double(vgetq_lane_f32(numerator, 0)) * reciprocal),
                float(double(vgetq_lane_f32(numerator, 1)) * reciprocal),
                float(double(vgetq_lane_f32(numerator, 2)) * reciprocal),
                float(double(vgetq_lane_f32(numerator, 3)) * reciprocal));
#endif
}
inline V divideAudioConstant(V numerator, float denominator, double reciprocal) {
#if !defined(__aarch64__)
    if (denominator == 2.f * PI || denominator == 8000.f || denominator == 22050.f || denominator == 44100.f ||
        denominator == 48000.f || denominator == 96000.f) {
        // Oscillator numerators stay normal; each divisor's complete mantissa domain is validated.
        const V inverse = splat(1.f / denominator), divisor = splat(denominator);
        const V quotient = mul(numerator, inverse);
        const V corrected = vfmaq_f32(quotient, vfmsq_f32(numerator, quotient, divisor), inverse);
        return vbslq_f32(vceqq_f32(numerator, splat(0.f)), numerator, corrected);
    }
#endif
    return divideConstant(numerator, denominator, reciprocal);
}
inline V sine(V phase) {
    const V position = scale(phase, 2048.f);
    const auto integer = vcvtq_s32_f32(position);
    const V fraction = sub(position, vcvtq_f32_s32(integer));
    const auto offsets = vandq_s32(integer, vdupq_n_s32(2047));
    const int a = vgetq_lane_s32(offsets, 0), b = vgetq_lane_s32(offsets, 1);
    const int c = vgetq_lane_s32(offsets, 2), d = vgetq_lane_s32(offsets, 3);
    const auto low = vzip_f32(vld1_f32(detail::sineTable.data() + a), vld1_f32(detail::sineTable.data() + b));
    const auto high = vzip_f32(vld1_f32(detail::sineTable.data() + c), vld1_f32(detail::sineTable.data() + d));
    const V first = vcombine_f32(low.val[0], high.val[0]), second = vcombine_f32(low.val[1], high.val[1]);
    return madd(first, sub(second, first), fraction);
}
inline V sinePm(V phase) {
    return sine(vbslq_f32(vcgeq_f32(phase, splat(1.f)), sub(phase, splat(1.f)),
                         vbslq_f32(vcltq_f32(phase, splat(0.f)), add(phase, splat(1.f)), phase)));
}
inline V clipQuotient(V numerator, V denominator) {
#if defined(__aarch64__)
    return divide(numerator, denominator);
#else
    // The bounded clip quotient uses residual refinement instead of four scalar divides on Gen1.
    V inverse = vrecpeq_f32(denominator);
    inverse = mul(inverse, vrecpsq_f32(denominator, inverse));
    const V quotient = mul(numerator, inverse);
    const V corrected = vfmaq_f32(quotient, vfmsq_f32(numerator, quotient, denominator), inverse);
    return vbslq_f32(vceqq_f32(numerator, splat(0.f)), numerator, corrected);
#endif
}
inline V clip(V value) {
    const V x = bound(value, -3.f, 3.f), square = mul(x, x);
    return bound(clipQuotient(mul(x, add(splat(27.f), square)), scaleAdd(splat(27.f), square, 9.f)), -1.f, 1.f);
}
inline V weight(V frequency, float start, float width) {
    const V remaining = sub(splat(start + width), frequency);
    if (all(vcgeq_f32(remaining, splat(width)))) return splat(1.f);
    return bound(divide(remaining, splat(width)), 0.f, 1.f);
}
inline V limit(V frequency, float carrierRatio, float modulatorRatio, V index, float rate) {
    const auto low = vcleq_f32(frequency, splat(rate * .015f));
    if (all(low)) return index;
    // Retain the exact division near the limiter boundary; this margin only skips an inactive limiter.
    const auto occupied = add(scale(frequency, carrierRatio),
                              mul(scale(frequency, modulatorRatio), add(splat(1.f), scale(index, 2.f * PI))));
    const auto unchanged = vorrq_u32(low, vcleq_f32(occupied, splat(rate * .449f)));
    if (all(unchanged)) return index;
    const V budget = divideAudioConstant(vmaxq_f32(sub(divideMasked(sub(splat(rate * .45f), scale(frequency, carrierRatio)),
                                                     scale(frequency, modulatorRatio), vmvnq_u32(unchanged)), splat(1.f)), splat(0.f)),
                            2.f * PI, 1.0 / double(2.f * PI));
    return vbslq_f32(unchanged, index, vminq_f32(index, budget));
}
__attribute__((always_inline)) inline V blep(V phase, V step) {
#if !defined(__aarch64__)
    return four(hexachord::native::blep(vgetq_lane_f32(phase, 0), vgetq_lane_f32(step, 0)),
                hexachord::native::blep(vgetq_lane_f32(phase, 1), vgetq_lane_f32(step, 1)),
                hexachord::native::blep(vgetq_lane_f32(phase, 2), vgetq_lane_f32(step, 2)),
                hexachord::native::blep(vgetq_lane_f32(phase, 3), vgetq_lane_f32(step, 3)));
#else
    const auto early = vcltq_f32(phase, step), late = vcgtq_f32(phase, sub(splat(1.f), step));
    if (all(vmvnq_u32(vorrq_u32(early, late)))) return splat(0.f);
    const V position = divideMasked(vbslq_f32(early, phase, sub(phase, splat(1.f))), step, vorrq_u32(early, late));
    const V square = mul(position, position);
    const V rising = sub(sub(add(position, position), square), splat(1.f));
    const V falling = add(add(add(square, position), position), splat(1.f));
    return vbslq_f32(early, rising, vbslq_f32(late, falling, splat(0.f)));
#endif
}
__attribute__((always_inline)) inline V pulseWithEdge(V phase, V step, V width, V leading) {
    V edge = sub(phase, width);
    edge = vbslq_f32(vcltq_f32(edge, splat(0.f)), add(edge, splat(1.f)), edge);
    const V square = vbslq_f32(vcltq_f32(phase, width), splat(1.f), splat(-1.f));
    return sub(add(square, leading), blep(edge, step));
}
__attribute__((always_inline)) inline V pulse(V phase, V step, V width) {
    return pulseWithEdge(phase, step, width, blep(phase, step));
}
inline V sawWithEdge(V phase, V leading) { return sub(sub(scale(phase, 2.f), splat(1.f)), leading); }
inline V saw(V phase, V step) { return sub(sub(scale(phase, 2.f), splat(1.f)), blep(phase, step)); }
struct Batch {
    V frequency, pulse, saw, third, fourth;
    float fast, medium, slow;
};
template<int model, bool unrestricted = false>
__attribute__((always_inline)) inline V render(const Batch& b, float velocity, float rate, float a, float level, float controlC) {
    const V frequency = b.frequency;
    const float fast = b.fast, medium = b.medium, slow = b.slow;
    const float velocity2 = velocity * velocity;
    float firstAmount, secondAmount;
    V firstIndex, secondIndex, upperWeight;
    if constexpr (model == 0) {
        firstAmount = (.06f + velocity2 * .52f) * (.15f + fast * .85f) * a;
        secondAmount = (.08f + velocity2 * .33f) * fast * a;
        firstIndex = unrestricted ? splat(firstAmount) : limit(frequency, 1.f, 2.f, splat(firstAmount), rate);
        secondIndex = unrestricted ? splat(secondAmount) : limit(frequency, 2.01f, 7.03f, splat(secondAmount), rate);
        upperWeight = unrestricted ? splat(1.f) : weight(scale(frequency, 2.01f), rate * .32f, rate * .14f);
    } else {
        firstAmount = (.12f + velocity2 * .70f) * (.10f + fast * .90f) * a;
        secondAmount = (.04f + velocity * .30f) * medium * a;
        firstIndex = unrestricted ? splat(firstAmount) : limit(frequency, 1.f, 2.713f, splat(firstAmount), rate);
        secondIndex = unrestricted ? splat(secondAmount) : limit(frequency, 1.497f, 4.197f, splat(secondAmount), rate);
        upperWeight = unrestricted ? splat(1.f) : weight(scale(frequency, 1.497f), rate * .32f, rate * .14f);
    }
    const bool firstZero = unrestricted ? firstAmount == 0.f : all(vceqq_f32(firstIndex, splat(0.f)));
    const bool secondZero = unrestricted ? secondAmount == 0.f : all(vceqq_f32(secondIndex, splat(0.f)));
    const V first = firstZero ? sine(b.pulse) :
        sinePm(madd(b.pulse, sine(b.saw), firstIndex));
    const V second = secondZero ? sine(b.third) :
        sinePm(madd(b.third, sine(b.fourth), secondIndex));
    if constexpr (model == 0) {
        const V upper = scale(mul(scale(scale(second, .08f + velocity * .32f), medium), upperWeight), level);
        const V body = scale(first, .26f + slow * .74f);
        return scale(clip(scale(add(body, upper), 1.f + velocity2 * controlC)), .72f);
    } else {
        const V upper = scale(mul(scale(scale(second, medium), .36f), upperWeight), level);
        return add(scale(first, .18f + slow * .46f), upper);
    }
}
}
#endif
