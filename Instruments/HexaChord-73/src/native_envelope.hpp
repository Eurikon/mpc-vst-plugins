#pragma once

#include "native_math.hpp"

namespace hexachord::native {

struct ADSRShape {
    float attackInc = .001f;
    float decayMult = .999f;
    float sustainLevel = 1.f;
    float releaseMult = .999f;

    void set(float attackMs, float decayMs, float sustain, float releaseMs, float sampleRate) {
        const float attackSamples = max(attackMs * .001f * sampleRate, 1.f);
        attackInc = 1.f / attackSamples;
        const float decaySamples = max(decayMs * .001f * sampleRate, 1.f);
        decayMult = exp(-1.f / decaySamples);
        sustainLevel = clamp(sustain, 0.f, 1.f);
        const float releaseSamples = max(releaseMs * .001f * sampleRate, 1.f);
        releaseMult = exp(-1.f / releaseSamples);
    }
};

class ADSR {
public:
    void configure(const ADSRShape& shape) {
        attackInc_ = shape.attackInc;
        decayMult_ = shape.decayMult;
        sustainLevel_ = shape.sustainLevel;
        releaseMult_ = shape.releaseMult;
    }
    void gate(bool on) {
        if (on) stage_ = 1;
        else if (stage_ != 0) stage_ = 4;
    }
    int render(float* buffer, int frames) {
        int stage = stage_;
        float value = value_;
        const float attackInc = attackInc_, decayMult = decayMult_;
        const float sustainLevel = sustainLevel_, releaseMult = releaseMult_;
        int activeFrames = frames;
        for (int i = 0; i < frames; ++i) {
            if (stage == 1) {
                value += attackInc;
                if (value >= 1.f) {
                    value = 1.f;
                    stage = 2;
                }
            } else if (stage == 2) {
                value = sustainLevel + (value - sustainLevel) * decayMult;
                if (std::fabs(value - sustainLevel) < .0005f) {
                    value = sustainLevel;
                    stage = 3;
                }
            } else if (stage == 3) {
                value = sustainLevel;
            } else if (stage == 4) {
                value *= releaseMult;
                if (value < .0001f) {
                    value = 0.f;
                    stage = 0;
                }
            }
            if (stage == 0 && activeFrames == frames) activeFrames = i;
            buffer[i] = value;
        }
        stage_ = stage;
        value_ = value;
        return activeFrames;
    }
    bool isActive() const { return stage_ != 0; }
    void reset() {
        stage_ = 0;
        value_ = 0.f;
    }

private:
    int stage_ = 0;
    float value_ = 0.f;
    float attackInc_ = .001f;
    float decayMult_ = .999f;
    float sustainLevel_ = 1.f;
    float releaseMult_ = .999f;
};
}
