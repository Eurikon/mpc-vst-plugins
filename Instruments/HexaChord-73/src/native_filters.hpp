#pragma once

#include "native_math.hpp"
#include "native_fm.hpp"

namespace hexachord::native {

class OtaLowpass {
public:
    void set(float cutoff, float resonance, float drive, float sampleRate) {
        const float phase = clamp(cutoff, 20.f, sampleRate * .34f) / (sampleRate * 2.f);
        const float tangent = sineCycle(phase) / sineCycle(phase + .25f);
        g_ = tangent / (1.f + tangent);
        const float feedback = resonance * 3.85f;
        gain_ = (1.f + resonance * .65f) / drive;
        const float g2 = g_ * g_;
        const float inverse = 1.f / (1.f + feedback * g2 * g2);
        inputGain_ = drive * inverse;
        f4_ = feedback * (1.f - g_) * inverse;
        f3_ = f4_ * g_;
        f2_ = f3_ * g_;
        f1_ = f2_ * g_;
    }
    void writeCoefficients(float* output) const {
        output[0] = g_;
        output[1] = inputGain_;
        output[2] = gain_;
        output[3] = f1_;
        output[4] = f2_;
        output[5] = f3_;
        output[6] = f4_;
    }
    void readCoefficients(const float* input) {
        g_ = input[0]; inputGain_ = input[1]; gain_ = input[2];
        f1_ = input[3]; f2_ = input[4]; f3_ = input[5]; f4_ = input[6];
    }
    void render(float* buffer, int offset, int frames, const float* coefficients = nullptr) {
        const float g = coefficients ? coefficients[0] : g_;
        const float inputGain = coefficients ? coefficients[1] : inputGain_;
        const float gain = coefficients ? coefficients[2] : gain_;
        const float f1 = coefficients ? coefficients[3] : f1_;
        const float f2 = coefficients ? coefficients[4] : f2_;
        const float f3 = coefficients ? coefficients[5] : f3_;
        const float f4 = coefficients ? coefficients[6] : f4_;
        float z1 = z1_, z2 = z2_, z3 = z3_, z4 = z4_;
        for (int i = offset; i < offset + frames; ++i) {
            const float input = softClip(buffer[i] * inputGain - f1 * z1 - f2 * z2 - f3 * z3 - f4 * z4);
            const float y1 = z1 + g * (input - z1);
            const float y2 = z2 + g * (y1 - z2);
            const float y3 = z3 + g * (y2 - z3);
            const float y4 = z4 + g * (y3 - z4);
            z1 = 2.f * y1 - z1;
            z2 = 2.f * y2 - z2;
            z3 = 2.f * y3 - z3;
            z4 = 2.f * y4 - z4;
            buffer[i] = y4 * gain;
        }
        z1_ = z1; z2_ = z2; z3_ = z3; z4_ = z4;
    }
    static void renderFour(OtaLowpass* const filters[4], float (*buffer)[4],
                           int offset, int frames, const float* coefficients) {
#if defined(__ARM_NEON) && !defined(HEXACHORD_SCALAR_FM)
        {
            const auto load = [&](float OtaLowpass::*member) {
                return fm::four(filters[0]->*member, filters[1]->*member, filters[2]->*member, filters[3]->*member);
            };
            const auto store = [&](fm::V value, float OtaLowpass::*member) {
                filters[0]->*member = vgetq_lane_f32(value, 0); filters[1]->*member = vgetq_lane_f32(value, 1);
                filters[2]->*member = vgetq_lane_f32(value, 2); filters[3]->*member = vgetq_lane_f32(value, 3);
            };
            auto z1 = load(&OtaLowpass::z1_);
            auto z2 = load(&OtaLowpass::z2_);
            auto z3 = load(&OtaLowpass::z3_);
            auto z4 = load(&OtaLowpass::z4_);
            const float g = coefficients[0], inputGain = coefficients[1], gain = coefficients[2];
            const float f1 = coefficients[3], f2 = coefficients[4], f3 = coefficients[5], f4 = coefficients[6];
            const int end = offset + frames;
            for (; offset < end; ++offset) {
                const auto input = vld1q_f32(buffer[offset]);
                const auto driven = fm::scaleSub(fm::scaleSub(fm::scaleSub(fm::scaleSub(fm::scale(input, inputGain),
                    z1, f1), z2, f2), z3, f3), z4, f4);
                const auto clipped = fm::clip(driven);
                const auto y1 = fm::scaleAdd(z1, fm::sub(clipped, z1), g);
                const auto y2 = fm::scaleAdd(z2, fm::sub(y1, z2), g);
                const auto y3 = fm::scaleAdd(z3, fm::sub(y2, z3), g);
                const auto y4 = fm::scaleAdd(z4, fm::sub(y3, z4), g);
                z1 = fm::sub(fm::scale(y1, 2.f), z1); z2 = fm::sub(fm::scale(y2, 2.f), z2);
                z3 = fm::sub(fm::scale(y3, 2.f), z3); z4 = fm::sub(fm::scale(y4, 2.f), z4);
                const auto output = fm::scale(y4, gain);
                vst1q_f32(buffer[offset], output);
            }
            store(z1, &OtaLowpass::z1_);
            store(z2, &OtaLowpass::z2_);
            store(z3, &OtaLowpass::z3_);
            store(z4, &OtaLowpass::z4_);
            return;
        }
#else
        const float g = coefficients[0], inputGain = coefficients[1], gain = coefficients[2];
        const float f1 = coefficients[3], f2 = coefficients[4], f3 = coefficients[5], f4 = coefficients[6];
        float z1[4], z2[4], z3[4], z4[4];
        for (int lane = 0; lane < 4; ++lane) {
            z1[lane] = filters[lane]->z1_; z2[lane] = filters[lane]->z2_;
            z3[lane] = filters[lane]->z3_; z4[lane] = filters[lane]->z4_;
        }
        for (int frame = offset; frame < offset + frames; ++frame) {
            float current[4];
            for (int lane = 0; lane < 4; ++lane)
                current[lane] = softClip(buffer[frame][lane] * inputGain - f1 * z1[lane] - f2 * z2[lane] -
                                         f3 * z3[lane] - f4 * z4[lane]);
            for (int lane = 0; lane < 4; ++lane) {
                const float y = z1[lane] + g * (current[lane] - z1[lane]);
                z1[lane] = 2.f * y - z1[lane];
                current[lane] = y;
            }
            for (int lane = 0; lane < 4; ++lane) {
                const float y = z2[lane] + g * (current[lane] - z2[lane]);
                z2[lane] = 2.f * y - z2[lane];
                current[lane] = y;
            }
            for (int lane = 0; lane < 4; ++lane) {
                const float y = z3[lane] + g * (current[lane] - z3[lane]);
                z3[lane] = 2.f * y - z3[lane];
                current[lane] = y;
            }
            for (int lane = 0; lane < 4; ++lane) {
                const float y = z4[lane] + g * (current[lane] - z4[lane]);
                z4[lane] = 2.f * y - z4[lane];
                current[lane] = y;
            }
            for (int lane = 0; lane < 4; ++lane) buffer[frame][lane] = current[lane] * gain;
        }
        for (int lane = 0; lane < 4; ++lane) {
            filters[lane]->z1_ = z1[lane]; filters[lane]->z2_ = z2[lane];
            filters[lane]->z3_ = z3[lane]; filters[lane]->z4_ = z4[lane];
        }
#endif
    }
    void reset() { z1_ = z2_ = z3_ = z4_ = 0.f; }

private:
    float z1_ = 0.f, z2_ = 0.f, z3_ = 0.f, z4_ = 0.f;
    float g_ = .1f, gain_ = 1.f, inputGain_ = 1.f;
    float f1_ = 0.f, f2_ = 0.f, f3_ = 0.f, f4_ = 0.f;
};

class ToneLowpass {
public:
    void set(float cutoff, float resonance, float sampleRate) {
        const float phase = clamp(cutoff, 20.f, sampleRate * .42f) / (sampleRate * 2.f);
        const float g = sineCycle(phase) / sineCycle(phase + .25f);
        k_ = 1.6f - resonance * 1.1f;
        a1_ = 1.f / (1.f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }
    void writeCoefficients(float* output) const {
        output[0] = a1_; output[1] = a2_; output[2] = a3_;
    }
    void readCoefficients(const float* input) {
        a1_ = input[0]; a2_ = input[1]; a3_ = input[2];
    }
    void render(float* buffer, int offset, int frames, const float* coefficients = nullptr) {
        const float a1 = coefficients ? coefficients[0] : a1_;
        const float a2 = coefficients ? coefficients[1] : a2_;
        const float a3 = coefficients ? coefficients[2] : a3_;
        float z1 = z1_, z2 = z2_;
        for (int i = offset; i < offset + frames; ++i) {
            const float v3 = buffer[i] - z2;
            const float v1 = a1 * z1 + a2 * v3;
            const float v2 = z2 + a2 * z1 + a3 * v3;
            z1 = 2.f * v1 - z1;
            z2 = 2.f * v2 - z2;
            buffer[i] = v2;
        }
        z1_ = z1; z2_ = z2;
    }
    static void renderFour(ToneLowpass* const filters[4], float (*buffer)[4],
                           int offset, int frames, const float* coefficients) {
#if defined(__ARM_NEON) && !defined(HEXACHORD_SCALAR_FM)
        {
            const auto load = [&](float ToneLowpass::*member) {
                return fm::four(filters[0]->*member, filters[1]->*member, filters[2]->*member, filters[3]->*member);
            };
            const auto store = [&](fm::V value, float ToneLowpass::*member) {
                filters[0]->*member = vgetq_lane_f32(value, 0); filters[1]->*member = vgetq_lane_f32(value, 1);
                filters[2]->*member = vgetq_lane_f32(value, 2); filters[3]->*member = vgetq_lane_f32(value, 3);
            };
            auto z1 = load(&ToneLowpass::z1_);
            auto z2 = load(&ToneLowpass::z2_);
            const float a1 = coefficients[0], a2 = coefficients[1], a3 = coefficients[2];
            const int end = offset + frames;
            for (; offset < end; ++offset) {
                const auto input = vld1q_f32(buffer[offset]);
                const auto v3 = fm::sub(input, z2);
                const auto v1 = fm::scaleAdd(fm::scale(z1, a1), v3, a2);
                const auto v2 = fm::scaleAdd(fm::scaleAdd(z2, z1, a2), v3, a3);
                z1 = fm::sub(fm::scale(v1, 2.f), z1); z2 = fm::sub(fm::scale(v2, 2.f), z2);
                vst1q_f32(buffer[offset], v2);
            }
            store(z1, &ToneLowpass::z1_);
            store(z2, &ToneLowpass::z2_);
            return;
        }
#else
        const float a1 = coefficients[0], a2 = coefficients[1], a3 = coefficients[2];
        float z1[4], z2[4];
        for (int lane = 0; lane < 4; ++lane) {
            z1[lane] = filters[lane]->z1_; z2[lane] = filters[lane]->z2_;
        }
        for (int frame = offset; frame < offset + frames; ++frame) {
            float v3[4], v1[4], v2[4];
            for (int lane = 0; lane < 4; ++lane) v3[lane] = buffer[frame][lane] - z2[lane];
            for (int lane = 0; lane < 4; ++lane) v1[lane] = a1 * z1[lane] + a2 * v3[lane];
            for (int lane = 0; lane < 4; ++lane) v2[lane] = z2[lane] + a2 * z1[lane] + a3 * v3[lane];
            for (int lane = 0; lane < 4; ++lane) z1[lane] = 2.f * v1[lane] - z1[lane];
            for (int lane = 0; lane < 4; ++lane) z2[lane] = 2.f * v2[lane] - z2[lane];
            for (int lane = 0; lane < 4; ++lane) buffer[frame][lane] = v2[lane];
        }
        for (int lane = 0; lane < 4; ++lane) {
            filters[lane]->z1_ = z1[lane]; filters[lane]->z2_ = z2[lane];
        }
#endif
    }
    void reset() { z1_ = z2_ = 0.f; }

private:
    float z1_ = 0.f, z2_ = 0.f;
    float a1_ = 1.f, a2_ = 0.f, a3_ = 0.f, k_ = 1.4f;
};

class Biquad {
public:
    void setLowpass(float cutoff, float q, float sampleRate) {
        const float clampedCutoff = clamp(cutoff, 20.f, sampleRate * .48f);
        const float clampedQ = max(q, .1f);
        const float w0 = (2.f * PI * clampedCutoff) / sampleRate;
        const float cosine = ::cosf(w0);
        const float alpha = ::sinf(w0) / (2.f * clampedQ);
        const float a0 = 1.f + alpha;
        b0_ = ((1.f - cosine) * .5f) / a0;
        b1_ = (1.f - cosine) / a0;
        b2_ = ((1.f - cosine) * .5f) / a0;
        a1_ = (-2.f * cosine) / a0;
        a2_ = (1.f - alpha) / a0;
    }
    void setHighpass(float cutoff, float q, float sampleRate) {
        const float clampedCutoff = clamp(cutoff, 20.f, sampleRate * .48f);
        const float clampedQ = max(q, .1f);
        const float w0 = (2.f * PI * clampedCutoff) / sampleRate;
        const float cosine = ::cosf(w0);
        const float alpha = ::sinf(w0) / (2.f * clampedQ);
        const float a0 = 1.f + alpha;
        b0_ = ((1.f + cosine) * .5f) / a0;
        b1_ = (-(1.f + cosine)) / a0;
        b2_ = ((1.f + cosine) * .5f) / a0;
        a1_ = (-2.f * cosine) / a0;
        a2_ = (1.f - alpha) / a0;
    }
    void render(float* buffer, int offset, int frames) {
        renderSamples([&](int i) -> float& { return buffer[i]; }, offset, frames);
    }
private:
    template<class Access>
    void renderSamples(Access sample, int offset, int frames) {
        const float b0 = b0_, b1 = b1_, b2 = b2_, a1 = a1_, a2 = a2_;
        float x1 = x1_, x2 = x2_, y1 = y1_, y2 = y2_;
        for (int i = offset; i < offset + frames; ++i) {
            const float x = sample(i);
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            sample(i) = y;
        }
        x1_ = x1; x2_ = x2; y1_ = y1; y2_ = y2;
    }
public:
    static void renderFour(Biquad* const filters[4], float (*buffer)[4], int offset, int frames) {
#if defined(__ARM_NEON) && !defined(HEXACHORD_SCALAR_FM)
        auto load = [&](float Biquad::*member) {
            return fm::four(filters[0]->*member, filters[1]->*member, filters[2]->*member, filters[3]->*member);
        };
        auto store = [&](fm::V value, float Biquad::*member) {
            filters[0]->*member = vgetq_lane_f32(value, 0); filters[1]->*member = vgetq_lane_f32(value, 1);
            filters[2]->*member = vgetq_lane_f32(value, 2); filters[3]->*member = vgetq_lane_f32(value, 3);
        };
        const auto b0 = load(&Biquad::b0_), b1 = load(&Biquad::b1_), b2 = load(&Biquad::b2_);
        const auto a1 = load(&Biquad::a1_), a2 = load(&Biquad::a2_);
        auto x1 = load(&Biquad::x1_), x2 = load(&Biquad::x2_), y1 = load(&Biquad::y1_), y2 = load(&Biquad::y2_);
        for (int i = offset; i < offset + frames; ++i) {
            const auto x = vld1q_f32(buffer[i]);
            const auto y = fm::sub(fm::sub(fm::madd(fm::madd(fm::mul(b0, x), b1, x1), b2, x2),
                                                fm::mul(a1, y1)), fm::mul(a2, y2));
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            vst1q_f32(buffer[i], y);
        }
        store(x1, &Biquad::x1_); store(x2, &Biquad::x2_); store(y1, &Biquad::y1_); store(y2, &Biquad::y2_);
#else
        for (int lane = 0; lane < 4; ++lane) filters[lane]->renderSamples([&](int i) -> float& { return buffer[i][lane]; }, offset, frames);
#endif
    }
    void reset() { x1_ = x2_ = y1_ = y2_ = 0.f; }

private:
    float b0_ = 1.f, b1_ = 0.f, b2_ = 0.f, a1_ = 0.f, a2_ = 0.f;
    float x1_ = 0.f, x2_ = 0.f, y1_ = 0.f, y2_ = 0.f;
};
}
