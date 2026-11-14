#pragma once

#include "native_math.hpp"
#include <algorithm>

namespace hexachord::native {

struct ADSRShape {
    float attackInc = .001f;
    float decayMult = .999f;
    float sustainLevel = 1.f;
    float releaseMult = .999f;

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
        if (frames <= 0) return 0;
        if (stage_ == 0) { std::fill_n(buffer, frames, value_); return 0; }
        if (stage_ == 3) {
            value_ = sustainLevel_;
            std::fill_n(buffer, frames, value_);
            return frames;
        }
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
                    std::fill(buffer + i, buffer + frames, value);
                    break;
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
