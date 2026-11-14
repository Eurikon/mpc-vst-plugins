#include <cmath>
#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <string>
#include <vector>
#include "params.h"
#include "hexachord_params.h"
#include "native_fm.hpp"
#include "native_fp.h"
#include "native_filters.hpp"
#include "native_envelope.hpp"

struct AEffect;
using Host = intptr_t (*)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
    void (*process)(AEffect*, float**, float**, int32_t);
    void (*setParameter)(AEffect*, int32_t, float);
    float (*getParameter)(AEffect*, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect*, float**, float**, int32_t);
    void (*processDoubleReplacing)(AEffect*, double**, double**, int32_t);
    char future[56];
};
using Effect = AEffect;
struct MidiEvent {
    int32_t type = 1, byteSize = sizeof(MidiEvent), frame = 0, flags = 0, length = 0, offset = 0;
    uint8_t data[4]{};
    char detune = 0, releaseVelocity = 0, reserved1 = 0, reserved2 = 0;
};
struct Events { int32_t count; intptr_t reserved; MidiEvent* events[2]; };

namespace {
struct Notification { Effect* effect; int index; float value; };
std::vector<Notification> notifications;
bool settingUi = false, reenteredUi = false;
int displayUpdates = 0;
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
void vectorMath() {
    const int rounding = std::fegetround();
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        require(std::fesetround(mode) == 0, "Cannot select sample rounding mode");
        for (int integer = -32768; integer < 32767; ++integer) {
            const float tie = float(integer) + .5f;
            for (float input : {tie, std::nextafter(tie, -INFINITY), std::nextafter(tie, INFINITY)})
                require(hexachord_round_sample(input) == std::lrintf(input), "Native sample rounding changed output");
        }
    }
    require(std::fesetround(rounding) == 0, "Cannot restore sample rounding mode");
#if defined(__ARM_NEON) && !defined(HEXACHORD_SCALAR_FM)
    using namespace hexachord::native;
    uint32_t seed = 0x731a;
    auto random = [&] { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.f; };
    for (int iteration = 0; iteration < 131072; ++iteration) {
        float numerator[4], actual[4], expected[4];
        const float denominator = iteration % 2 ? 2.f * PI : 8000.f + random() * 88000.f;
        for (int lane = 0; lane < 4; ++lane) {
            numerator[lane] = (random() - .5f) * 0x1p+24f;
            if (iteration % 4 == 0) numerator[lane] = std::nextafter(denominator * numerator[lane], 0.f);
            expected[lane] = numerator[lane] / denominator;
        }
        vst1q_f32(actual, fm::divide(vld1q_f32(numerator), fm::splat(denominator)));
        require(!std::memcmp(actual, expected, sizeof actual), "Vector lane division changed rounded output");
        vst1q_f32(actual, fm::divideConstant(vld1q_f32(numerator), denominator, 1.0 / double(denominator)));
        require(!std::memcmp(actual, expected, sizeof actual), "Constant division changed rounded output");
        for (int lane = 0; lane < 4; ++lane)
            require(cachedAudioQuotient(numerator[lane], denominator, 1.0 / double(denominator)) == expected[lane],
                    "Scalar cached division changed rounded output");
        float divisors[4];
        for (int lane = 0; lane < 4; ++lane) {
            divisors[lane] = (random() - .5f) * 65536.f;
            if (divisors[lane] == 0.f) divisors[lane] = -0.f;
            expected[lane] = numerator[lane] / divisors[lane];
        }
        vst1q_f32(actual, fm::divide(vld1q_f32(numerator), vld1q_f32(divisors)));
        require(!std::memcmp(actual, expected, sizeof actual), "Independent vector divisors crossed lanes");
        float multiplier[4], accumulator[4];
        for (int lane = 0; lane < 4; ++lane) {
            multiplier[lane] = random() * 8.f - 4.f;
            accumulator[lane] = random() * 8.f - 4.f;
            const float product = numerator[lane] * multiplier[lane];
            expected[lane] = accumulator[lane] + product;
        }
        vst1q_f32(actual, fm::madd(vld1q_f32(accumulator), vld1q_f32(numerator), vld1q_f32(multiplier)));
        require(!std::memcmp(actual, expected, sizeof actual), "Vector multiply-add changed rounding");
    }
    for (float divisor : {2.f * PI, 44100.f}) {
        const float zeros[] = {0.f, -0.f, 0.f, -0.f}; float actual[4];
        vst1q_f32(actual, fm::divideAudioConstant(vld1q_f32(zeros), divisor, 1.0 / double(divisor)));
        require(!std::memcmp(actual, zeros, sizeof actual), "Audio quotient changed signed zero");
    }
    for (int iteration = 0; iteration < 32768; ++iteration) {
        float input[4], actual[4];
        for (int lane = 0; lane < 4; ++lane) input[lane] = (random() - .5f) * 24.f;
        vst1q_f32(actual, fm::clip(vld1q_f32(input)));
        for (int lane = 0; lane < 4; ++lane)
            require(std::isfinite(actual[lane]) && std::fabs(actual[lane] - softClip(input[lane])) <= 0x1p-21f,
                    "Refined clipping exceeded its arithmetic error bound");
    }
    {
        const float zero[] = {0.f, -0.f, 0.f, -0.f}; float actual[4];
        vst1q_f32(actual, fm::clip(vld1q_f32(zero)));
        require(!std::memcmp(actual, zero, sizeof actual), "Refined clipping changed signed zero");
    }
    const float ratios[][2] = {{1.f, 2.f}, {2.01f, 7.03f}, {1.f, 2.713f}, {1.497f, 4.197f}};
    for (float rate : {2.f * PI, 8000.f, 22050.f, 44100.f, 48000.f, 96000.f}) {
        for (uint32_t bits = 0x3f800000; bits < 0x40000000; bits += 4) {
            const uint32_t words[] = {bits, bits + 1, bits + 2, bits + 3};
            float input[4], actual[4], expected[4];
            std::memcpy(input, words, sizeof input);
            for (int lane = 0; lane < 4; ++lane) expected[lane] = input[lane] / rate;
            vst1q_f32(actual, fm::divideAudioConstant(vld1q_f32(input), rate, 1.0 / double(rate)));
            require(!std::memcmp(actual, expected, sizeof actual), "Audio constant quotient changed rounding");
            for (int lane = 0; lane < 4; ++lane)
                require(cachedAudioQuotient(input[lane], rate, 1.0 / double(rate)) == expected[lane],
                        "Scalar audio quotient changed rounding");
        }
    }
    for (float rate : {8000.f, 22050.f, 44100.f, 48000.f, 96000.f}) for (const auto& ratio : ratios) {
        for (int iteration = 0; iteration < 4096; ++iteration) {
            float frequency[4], index[4], expected[4], actual[4];
            for (int lane = 0; lane < 4; ++lane) {
                const float f = frequency[lane] = 1.f + random() * (rate * .48f - 1.f);
                const float budget = max((rate * .45f - f * ratio[0]) / (f * ratio[1]) - 1.f, 0.f) / (2.f * PI);
                index[lane] = iteration % 4 == 0 ? budget : random() * 2.f;
                if (iteration % 4 == 1 && budget < 2.f) index[lane] = std::nextafter(budget, 0.f);
                if (iteration % 4 == 2 && budget < 2.f) index[lane] = std::nextafter(budget, 2.f);
                expected[lane] = f <= rate * .015f ? index[lane] : min(index[lane], budget);
            }
            vst1q_f32(actual, fm::limit(vld1q_f32(frequency), ratio[0], ratio[1], vld1q_f32(index), rate));
            for (int lane = 0; lane < 4; ++lane) require(actual[lane] == expected[lane], "Vector limiter changed exact boundary result");
            for (int lane = 0; lane < 4; ++lane) frequency[lane] = random();
            vst1q_f32(actual, fm::sine(vld1q_f32(frequency)));
            for (int lane = 0; lane < 4; ++lane) require(actual[lane] == sineUnit(frequency[lane]), "Vector sine changed interpolation");
        }
    }
#endif
}
void envelopes() {
    using namespace hexachord::native;
    ADSR candidate;
    ADSRShape shape;
    int stage = 0;
    float value = 0.f;
    const int sizes[] = {0, 1, 7, 16, 31, 128, 257};
    for (int block = 0; block < 4096; ++block) {
        shape.attackInc = .001f + float(block % 41) * .001f;
        shape.decayMult = .98f + float(block % 19) * .001f;
        shape.sustainLevel = float(block % 17) / 16.f;
        shape.releaseMult = .90f + float(block % 97) * .001f;
        candidate.configure(shape);
        if (block % 67 == 0) { stage = 1; candidate.gate(true); }
        if (block % 67 == 29) { if (stage) stage = 4; candidate.gate(false); }
        const int frames = sizes[block % 7];
        std::array<float, 257> expected, actual;
        expected.fill(123.f); actual.fill(123.f);
        int activeFrames = frames;
        for (int i = 0; i < frames; ++i) {
            if (stage == 1) {
                value += shape.attackInc;
                if (value >= 1.f) { value = 1.f; stage = 2; }
            } else if (stage == 2) {
                value = shape.sustainLevel + (value - shape.sustainLevel) * shape.decayMult;
                if (std::fabs(value - shape.sustainLevel) < .0005f) { value = shape.sustainLevel; stage = 3; }
            } else if (stage == 3) value = shape.sustainLevel;
            else if (stage == 4) {
                value *= shape.releaseMult;
                if (value < .0001f) { value = 0.f; stage = 0; }
            }
            if (stage == 0 && activeFrames == frames) activeFrames = i;
            expected[i] = value;
        }
        require(candidate.render(actual.data(), frames) == activeFrames, "Envelope active length changed");
        require(!std::memcmp(actual.data(), expected.data(), sizeof actual), "Envelope samples or buffer boundary changed");
        require(candidate.isActive() == (stage != 0), "Envelope stage changed");
    }
}
void effectFilters() {
    const auto previous = hexachord_fp_enter();
    using hexachord::native::Biquad;
    Biquad reference[4], candidate[4];
    Biquad* filters[] = {&candidate[0], &candidate[1], &candidate[2], &candidate[3]};
    float expected[4][257], actual[257][4];
    for (float rate : {8000.f, 44100.f, 48000.f, 96000.f}) {
        for (int lane = 0; lane < 4; ++lane) {
            reference[lane].reset(); candidate[lane].reset();
            if (lane < 2) {
                reference[lane].setLowpass(2800.f, .6f, rate); candidate[lane].setLowpass(2800.f, .6f, rate);
            } else {
                reference[lane].setHighpass(600.f, .7f, rate); candidate[lane].setHighpass(600.f, .7f, rate);
            }
        }
        for (int block = 0; block < 64; ++block) {
            for (int lane = 0; lane < 4; ++lane) for (int i = 0; i < 257; ++i)
                actual[i][lane] = expected[lane][i] = block < 32 ? std::sin(float(i + block * 257 + lane) * .13f) : 0.f;
            for (int lane = 0; lane < 4; ++lane) reference[lane].render(expected[lane], 0, 257);
            for (int start = 0; start < 257; start += 13)
                Biquad::renderFour(filters, actual, start, std::min(13, 257 - start));
            for (int lane = 0; lane < 4; ++lane) for (int i = 0; i < 257; ++i)
                require(actual[i][lane] == expected[lane][i], "Grouped effect filters changed samples or state");
        }
    }
    hexachord_fp_restore(previous);
}
void floatingPoint(Effect* effect) {
    const auto original = hexachord_fp_read();
    const auto saved = hexachord_fp_enter();
#if defined(__aarch64__) || (defined(__arm__) && defined(__VFP_FP__))
    const uint64_t mask = 1ull << 24;
#elif defined(__SSE2__)
    const uint64_t mask = (1u << 15) | (1u << 6);
#else
    const uint64_t mask = 0;
#endif
    require(saved == original && (hexachord_fp_read() & mask) == mask, "Audio denormal protection was not enabled");
    hexachord_fp_restore(saved);
    require(hexachord_fp_read() == original, "FP helper changed caller state");
    for (uint64_t mode : std::array<uint64_t, 5>{0, mask, mask | (1ull << 22), mask | (2ull << 22), mask | (3ull << 22)}) {
#if defined(__SSE2__) && !defined(__aarch64__) && !defined(__arm__)
        mode = mode & ~(3ull << 22);
#endif
        hexachord_fp_restore((original & ~mask) | mode);
        const auto before = hexachord_fp_read();
        float left[128]{}, right[128]{}; float* channels[] = {left, right};
        effect->processReplacing(effect, nullptr, channels, 128);
        require(hexachord_fp_read() == before, "Replacing process leaked FP state");
        effect->process(effect, nullptr, channels, 128);
        require(hexachord_fp_read() == before, "Accumulating process leaked FP state");
    }
    hexachord_fp_restore(original);
}
intptr_t host(Effect* effect, int32_t opcode, int32_t index, intptr_t, void*, float value) {
    if (opcode == 0) {
        notifications.push_back({effect, index, value});
        reenteredUi |= settingUi;
    }
    if (opcode == 42) {
        ++displayUpdates;
        reenteredUi |= settingUi;
    }
    return opcode == 1 ? 2400 : 0;
}
void midi(Effect* e, int status, int note, int velocity, int frame) {
    MidiEvent event;
    event.frame = frame;
    event.data[0] = uint8_t(status);
    event.data[1] = uint8_t(note);
    event.data[2] = uint8_t(velocity);
    Events events{1, 0, {&event, nullptr}};
    require(e->dispatcher(e, 25, 0, 0, &events, 0) == 1, "VST MIDI dispatch");
}
double render(Effect* e, int frames, int silentPrefix = 0, bool accumulated = false) {
    std::vector<float> left(frames, accumulated ? .25f : 0.f), right(left);
    float* channels[] = {left.data(), right.data()};
    if (accumulated) e->process(e, nullptr, channels, frames);
    else e->processReplacing(e, nullptr, channels, frames);
    double energy = 0;
    for (int i = 0; i < frames; ++i) {
        require(std::isfinite(left[i]) && std::isfinite(right[i]), "Non-finite VST audio");
        if (i < silentPrefix) require(left[i] == 0 && right[i] == 0, "MIDI event played before deltaFrames");
        energy += double(left[i]) * left[i] + double(right[i]) * right[i];
    }
    return energy;
}
std::string parameterText(Effect* effect, int opcode, int index) {
    char buffer[64]{};
    require(effect->dispatcher(effect, opcode, index, 0, buffer, 0) == 1, "VST parameter text dispatch");
    return buffer;
}
std::string chunk(Effect* effect) {
    void* pointer = nullptr;
    const auto bytes = effect->dispatcher(effect, 23, 0, 0, &pointer, 0);
    require(pointer && bytes > 1 && bytes <= 8192, "Bounded display-test VST chunk");
    return {static_cast<const char*>(pointer), size_t(bytes)};
}
void displays(Effect* effect) {
    const auto original = chunk(effect);
    const char* defaults[] = {"Minor 9th", "Root", "Ana 78", "35", "25", "0.6", "1400", "20",
                              "350", "1200", "75", "2200", "30", "35", "35", "-3"};
    const char* units[] = {"", "", "", "%", "%", "hz", "Hz", "%", "ms", "ms", "%", "ms", "%", "%", "%", "dB"};
    for (int i = 0; i < 16; ++i) {
        require(parameterText(effect, 7, i) == defaults[i], "VST default parameter display");
        require(parameterText(effect, 6, i) == units[i], "VST physical units changed or duplicated");
    }
    struct Expected { int index; const char* minimum; const char* maximum; float fraction; const char* formatted; };
    const Expected expected[] = {{6, "20", "20000", .8f, "5024"}, {8, "10", "4000", .5f, "1008"},
                                  {9, "50", "5000", .25f, "359"}, {11, "50", "6000", .37f, "865"}};
    for (const auto& item : expected) {
        effect->setParameter(effect, item.index, 0);
        require(parameterText(effect, 7, item.index) == item.minimum, "VST physical display lower bound");
        effect->setParameter(effect, item.index, 1);
        require(parameterText(effect, 7, item.index) == item.maximum, "VST physical display upper bound");
        effect->setParameter(effect, item.index, item.fraction);
        const auto exact = chunk(effect);
        const auto numeric = effect->getParameter(effect, item.index);
        require(parameterText(effect, 7, item.index) == item.formatted, "VST fractional physical display rounding");
        require(effect->getParameter(effect, item.index) == numeric && std::fabs(numeric - item.fraction) < .000001f,
                "VST display changed normalized parameter");
        require(chunk(effect) == exact, "VST display changed exact saved state");
    }
    require(effect->dispatcher(effect, 24, 0, original.size(), const_cast<char*>(original.data()), 0) == 1,
            "Restore parameters after display checks");
    require(chunk(effect) == original, "Display checks altered restored VST state");
}
void enumControls(Effect* a, Effect* b) {
    const auto originalA = chunk(a), originalB = chunk(b);
    const int spans[] = {7, 3, 3};
    const float rates[] = {1.f / 128.f, .01f};
    auto selected = [&](Effect* effect, int i) {
        const auto label = parameterText(effect, 7, i);
        for (int option = 0; option <= spans[i]; ++option)
            if (label == PARAMS[i].opts[option]) return option;
        require(false, "Invalid enum display");
        return -1;
    };
    auto at = [&](Effect* effect, int i, int option) {
        require(selected(effect, i) == option, "Unexpected canonical enum selection");
        float values[3];
        const auto saved = chunk(effect);
        require(std::sscanf(saved.c_str(), "HX73 3 %f %f %f", &values[0], &values[1], &values[2]) == 3,
                "Canonical enum state could not be read");
        require(values[i] == option, "Saved enum disagrees with displayed choice");
    };
    auto raw = [&](Effect* effect, int i, float expected) {
        for (int read = 0; read < 4; ++read) {
            const float received = effect->getParameter(effect, i);
            if (std::fabs(received - expected) >= .000001f)
                std::fprintf(stderr, "Enum %d read %d: received %.9g, expected %.9g\n", i, read, received, expected);
            require(std::fabs(received - expected) < .000001f,
                    "Continuous enum host position changed on read");
        }
    };
    auto send = [&](Effect* effect, int i, float value) {
        effect->setParameter(effect, i, value);
        const float clamped = std::fmax(0.f, std::fmin(1.f, value));
        raw(effect, i, clamped);
        at(effect, i, int(std::lround(double(clamped) * spans[i])));
    };
    auto pick = [&](Effect* effect, int i, int option) {
        send(effect, i, float(option) / spans[i]);
    };
    auto restore = [&](Effect* effect, const std::string& saved) {
        require(effect->dispatcher(effect, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
                "Enum control state restore rejected");
        require(chunk(effect) == saved, "Enum control state restore changed canonical values");
    };
    for (int i = 0; i < 3; ++i) {
        require(PARAMS[i].nopts == spans[i] + 1 && PARAMS[i].qlink_continuous == 1 && PARAMS[i].qlink_ticks == 0,
                "Enum option range or continuous control opt-in changed");
        for (int option = 0; option <= spans[i]; ++option) pick(a, i, option);
        for (int option = 0; option < spans[i]; ++option) {
            const float midpoint = float((option + .5) / spans[i]);
            for (float value : {std::nextafter(midpoint, 0.f), midpoint, std::nextafter(midpoint, 1.f)})
                send(a, i, value);
        }
        const uint32_t midpointBits[] = {0x3f249249, 0x3f492492, 0x3f6db6db, 0x3f555555};
        for (int boundary = i == 0 ? 0 : 3; boundary < (i == 0 ? 3 : 4); ++boundary) {
            for (int neighbour : {-1, 0, 1}) {
                const uint32_t bits = midpointBits[boundary] + neighbour;
                float value;
                std::memcpy(&value, &bits, sizeof value);
                send(a, i, value);
                if (!neighbour) at(a, i, i == 0 ? boundary + 4 : 2);
            }
        }
        for (float rate : rates) {
            for (int direction : {-1, 1}) {
                pick(a, i, 1);
                const float baseline = a->getParameter(a, i);
                int previous = 1;
                for (int event = 1; event <= 160; ++event) {
                    send(a, i, baseline + direction * event * rate);
                    int current = selected(a, i);
                    require(direction > 0 ? current >= previous : current <= previous,
                            "Cached monotonic host stream reversed enum selection");
                    previous = current;
                }
                at(a, i, direction > 0 ? spans[i] : 0);

                pick(a, i, 1);
                previous = 1;
                for (int event = 0; event < 160; ++event) {
                    send(a, i, a->getParameter(a, i) + direction * rate);
                    int current = selected(a, i);
                    require(direction > 0 ? current >= previous : current <= previous,
                            "Readback-relative host stream reversed enum selection");
                    previous = current;
                }
                at(a, i, direction > 0 ? spans[i] : 0);
            }
        }
        pick(a, i, 1);
        float running = a->getParameter(a, i);
        const int bursts[] = {1, 2, 3, 10, 1, 4};
        for (int direction : {1, -1}) {
            int previous = selected(a, i);
            for (int event = 0; event < 80; ++event) {
                running = std::fmax(0.f, std::fmin(1.f, running + direction * bursts[event % 6] / 128.f));
                send(a, i, running);
                const int current = selected(a, i);
                require(direction > 0 ? current >= previous : current <= previous,
                        "Accelerated cached host stream reversed enum selection");
                previous = current;
                if (event % 7 == 0) running = a->getParameter(a, i);
            }
        }
        for (int option = 1; option < spans[i]; ++option) {
            send(a, i, (option + .49f) / spans[i]);
            at(a, i, option);
            send(a, i, (option + .51f) / spans[i]);
            at(a, i, option + 1);
            send(a, i, (option + .49f) / spans[i]);
            at(a, i, option);
            pick(a, i, option);
            send(a, i, (option - .1f) / spans[i]);
            at(a, i, option);
            pick(a, i, option - 1);
        }
        pick(a, i, 1);
        const auto canonical = chunk(a);
        const float fractional = 1.25f / spans[i];
        for (int repeat = 0; repeat < 12; ++repeat) send(a, i, fractional);
        require(chunk(a) == canonical, "Fractional host position leaked into saved enum state");
        pick(a, i, 1);
        require(chunk(a) == canonical, "Same-value touch changed canonical state");
        send(a, i, fractional);
        restore(a, canonical);
        raw(a, i, 1.f / spans[i]);
        send(a, i, a->getParameter(a, i) - rates[0]);
        at(a, i, 1);

        send(a, i, fractional);
        const auto preserved = chunk(a);
        for (intptr_t size : {intptr_t(0), intptr_t(8193)}) {
            require(a->dispatcher(a, 24, 0, size, const_cast<char*>(canonical.data()), 0) == 0,
                    "Malformed-size enum state accepted");
            raw(a, i, fractional);
            require(chunk(a) == preserved, "Rejected enum state mutated canonical values");
        }
        for (float invalid : {NAN, INFINITY, -INFINITY}) {
            a->setParameter(a, i, invalid);
            raw(a, i, fractional);
            require(chunk(a) == preserved, "Non-finite enum input mutated state");
        }
        send(a, i, -100.f);
        for (int repeat = 0; repeat < 16; ++repeat) send(a, i, -rates[0]);
        send(a, i, rates[0]);
        at(a, i, 0);
        send(a, i, 100.f);
        for (int repeat = 0; repeat < 16; ++repeat) send(a, i, 1.f + rates[0]);
        send(a, i, 1.f - rates[0]);
        at(a, i, spans[i]);
    }
    for (int i = 0; i < 3; ++i) {
        pick(a, i, 1);
        pick(b, i, 2);
    }
    const auto beforeB = chunk(b);
    for (int selectedParam = 0; selectedParam < 3; ++selectedParam) {
        send(a, selectedParam, 1.25f / spans[selectedParam]);
        for (int i = 0; i < 3; ++i) {
            raw(a, i, (i <= selectedParam ? 1.25f : 1.f) / spans[i]);
            raw(b, i, 2.f / spans[i]);
        }
        require(chunk(b) == beforeB, "Continuous enum gesture leaked between instances");
    }
    restore(a, beforeB);
    for (int i = 0; i < 3; ++i) raw(a, i, 2.f / spans[i]);
    restore(a, originalA);
    restore(b, originalB);
    for (int i = 0; i < 3; ++i) {
        raw(a, i, PARAMS[i].def);
        raw(b, i, PARAMS[i].def);
    }
}
void engineControls(Effect* a, Effect* b) {
    const auto originalA = chunk(a), originalB = chunk(b);
    const char* keys[] = {"FeltHammer", "FeltTine", "FeltBark", "FeltDamping",
                         "AnaDetune", "AnaDrift", "AnaFilterEnv", "AnaWave",
                         "DcoPulseWidth", "DcoPwmDepth", "DcoPwmRate", "DcoWave",
                         "PrismColour", "PrismPartial", "PrismStrike", "PrismRing"};
    const char* defaults[] = {"100", "100", "140", "Normal", "100", "100", "100", "Mix",
                             "34", "16", "100", "Both", "100", "100", "100", "Normal"};
    for (int i = 0; i < 16; ++i) {
        const int index = i + 18;
        require(!std::strcmp(PARAMS[index].key, keys[i]), "Engine host index changed or replaced XY");
        require(parameterText(a, 7, index) == defaults[i], "Engine VST default display changed");
        require(parameterText(a, 6, index) == (i % 4 == 3 ? "" : "%"), "Engine VST unit changed");
        for (float selected : {0.f, .25f, .75f, 1.f}) {
            a->setParameter(a, index, selected);
            require(std::fabs(a->getParameter(a, index) - selected) < .000001f, "Engine host control jumped");
            require(chunk(b) == originalB, "Engine host control leaked between instances");
        }
        a->setParameter(a, index, i % 4 == 3 ? .5f : .3f);
    }
    const auto saved = chunk(a);
    require(b->dispatcher(b, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
            "New engine VST chunk rejected");
    require(chunk(b) == saved, "Engine values did not survive VST reload");
    for (int i = 18; i < 34; ++i)
        require(std::fabs(a->getParameter(a, i) - b->getParameter(b, i)) < .000001f, "Restored engine control mismatch");
    std::string legacy;
    for (int count : {16, 20}) {
        require(b->dispatcher(b, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
                "Migration baseline restore rejected");
        legacy = count == 16 ? "HX73 1" : "HX73 2";
        size_t position = 7;
        for (int i = 0; i < count; ++i) {
            const auto end = saved.find(' ', position);
            require(end != std::string::npos, "Missing original state field");
            legacy += " " + saved.substr(position, end - position);
            position = end + 1;
        }
        legacy.push_back(0);
        require(b->dispatcher(b, 24, 0, legacy.size(), legacy.data(), 0) == 1,
                "Legacy VST chunk rejected");
        const auto restoredLegacy = chunk(b);
        require(b->dispatcher(b, 2, 0, 0, nullptr, 0) == 1 && chunk(b) == restoredLegacy,
                "Post-load program selection erased a migrated legacy state");
        for (int i = 16; i < count; ++i)
            require(std::fabs(a->getParameter(a, i + 2) - b->getParameter(b, i + 2)) < .000001f,
                    "Legacy VST chunk changed an existing engine control");
        for (int i = count - 16; i < 16; ++i)
            require(parameterText(b, 7, i + 18) == defaults[i], "Legacy VST chunk did not migrate engine defaults");
    }
    const auto migrated = chunk(b);
    for (intptr_t size : {intptr_t(saved.size() - 1), intptr_t(saved.size() - 2)}) {
        require(b->dispatcher(b, 24, 0, size, const_cast<char*>(saved.data()), 0) == 0,
                "Unterminated or partial VST state chunk accepted");
        require(chunk(b) == migrated, "Rejected partial VST chunk mutated state");
    }
    auto trailing = legacy;
    trailing += " extra";
    trailing.push_back(0);
    require(b->dispatcher(b, 24, 0, trailing.size(), trailing.data(), 0) == 0 && chunk(b) == migrated,
            "Embedded terminator hid trailing VST chunk data");
    require(b->dispatcher(b, 24, 0, legacy.size(), nullptr, 0) == 0 && chunk(b) == migrated,
            "Null VST chunk was not rejected");
    require(a->dispatcher(a, 24, 0, originalA.size(), const_cast<char*>(originalA.data()), 0) == 1 &&
                b->dispatcher(b, 24, 0, originalB.size(), const_cast<char*>(originalB.data()), 0) == 1,
            "Engine fixture restore rejected");
}
void factoryDefaults(Effect* a, Effect* b) {
    const auto originalA = chunk(a), originalB = chunk(b);
    for (int model = 0; model < 4; ++model) {
        for (int index = 0; index < 34; ++index)
            if (index != 16 && index != 17) a->setParameter(a, index, PARAMS[index].def == 1 ? 0.f : 1.f);
        a->setParameter(a, 2, model == 3 ? 0.f : 1.f);
        const char* state = hexachord::factoryStates[model];
        require(a->dispatcher(a, 24, 0, std::strlen(state) + 1, const_cast<char*>(state), 0) == 1,
                "Factory VST chunk rejected");
        for (int index = 0; index < 34; ++index) {
            const float expected = index == 2 ? model / 3.f : PARAMS[index].def;
            require(std::fabs(a->getParameter(a, index) - expected) < .000001f,
                    "Factory VST preset did not reset every parameter and Q-Link position");
        }
        require(chunk(b) == originalB, "Factory VST preset altered another instance");
    }
    require(a->dispatcher(a, 24, 0, originalA.size(), const_cast<char*>(originalA.data()), 0) == 1,
            "Factory fixture restore rejected");
}
void factoryPrograms(Effect* a, Effect* b) {
    const auto originalA = chunk(a), originalB = chunk(b);
    require(a->numPrograms == 4 && b->numPrograms == 4 && a->dispatcher(a, 3, 0, 0, nullptr, 0) == 0,
            "Default-first factory program count or initial index changed");
    auto defaults = [&](Effect* effect, int program) {
        const int model = hexachord::factoryProgramModels[program];
        require(effect->dispatcher(effect, 3, 0, 0, nullptr, 0) == program, "Factory bank/model mapping mismatch");
        for (int index = 0; index < 34; ++index)
            require(std::fabs(effect->getParameter(effect, index) - (index == 2 ? model / 3.f : PARAMS[index].def)) < .000001f,
                    "Factory program did not restore every parameter and Q-Link position");
    };
    auto select = [&](Effect* effect, intptr_t program) {
        settingUi = true;
        const auto result = effect->dispatcher(effect, 2, 0, program, nullptr, 0);
        settingUi = false;
        require(!reenteredUi, "Factory program setter re-entered the host");
        return result;
    };
    for (int program = 0; program < 4; ++program) {
        struct { char name[24]; char guard[8]; } buffer;
        std::memset(&buffer, 0x6b, sizeof buffer);
        require(a->dispatcher(a, 29, program, 0, buffer.name, 0) == 1 &&
                    !std::strcmp(buffer.name, hexachord::factoryNames[hexachord::factoryProgramModels[program]]),
                "Indexed factory name is incorrect");
        for (char value : buffer.guard) require(value == 0x6b, "Factory name exceeded the VST buffer");
    }
    notifications.clear();
    displayUpdates = 0;
    require(select(a, 3) == 1, "Initial nonzero program selection rejected before processing");
    defaults(a, 3);
    require(select(a, 0) == 1, "Initial default program selection rejected");
    defaults(a, 0);
    require(chunk(b) == originalB, "Initialization changed another instance");
    require(notifications.empty() && displayUpdates == 0, "Factory selection notified the host inside its setter");
    render(a, 128);
    for (int program = 0; program < 4; ++program) {
        for (int repeat = 0; repeat < 2; ++repeat) {
            for (int index = 0; index < 34; ++index)
                if (index != 16 && index != 17 && (repeat == 0 || index != 2))
                    a->setParameter(a, index, PARAMS[index].def == 1 ? 0.f : 1.f);
            if (repeat == 0) a->setParameter(a, 2, hexachord::factoryProgramModels[program] == 3 ? 0.f : 1.f);
            else {
                const float fractional = (hexachord::factoryProgramModels[program] + .125f) / 3.f;
                a->setParameter(a, 2, std::fmin(fractional, .99f));
                require(a->dispatcher(a, 3, 0, 0, nullptr, 0) == program,
                        "Tweaked sound lost its current factory bank index");
            }
            a->setParameter(a, 16, .1f);
            a->setParameter(a, 17, .9f);
            notifications.clear();
            displayUpdates = 0;
            require(select(a, program) == 1, "Factory program selection rejected");
            defaults(a, program);
            require(parameterText(a, 5, 0) == hexachord::factoryNames[hexachord::factoryProgramModels[program]],
                    "Current factory name disagrees with the engine");
            require(notifications.empty() && displayUpdates == 0 && chunk(b) == originalB,
                    "Factory reset notified synchronously or altered another instance");
            render(a, 128);
            defaults(a, program);
            bool refreshed[34]{};
            require(notifications.size() == 32 && displayUpdates == 1,
                    "Factory program did not defer exactly its real-parameter and display refresh");
            for (const auto& notice : notifications) {
                require(notice.effect == a && notice.index >= 0 && notice.index < 34 &&
                            notice.index != 16 && notice.index != 17 && !refreshed[notice.index] &&
                            std::fabs(notice.value - a->getParameter(a, notice.index)) < .000001f,
                        "Factory refresh reported an XY alias, duplicate, stale value or wrong instance");
                refreshed[notice.index] = true;
            }
        }
    }
    const auto valid = chunk(a);
    for (intptr_t invalid : {intptr_t(-1), intptr_t(4), INTPTR_MAX})
        require(select(a, invalid) == 0 && chunk(a) == valid, "Invalid factory index altered state");
    for (int invalid : {-1, 4}) {
        char name[24] = "unchanged";
        require(a->dispatcher(a, 29, invalid, 0, name, 0) == 0 && !name[0] && chunk(a) == valid,
                "Invalid indexed factory name accepted or changed state");
    }
    require(a->dispatcher(a, 5, 0, 0, nullptr, 0) == 0 && a->dispatcher(a, 29, 0, 0, nullptr, 0) == 0,
            "Null factory name buffer accepted");
    for (const char* invalid : {"INVALID", "HX73 3 0", "HX73 3 nan"}) {
        require(a->dispatcher(a, 24, 0, std::strlen(invalid) + 1, const_cast<char*>(invalid), 0) == 0,
                "Malformed factory/project state accepted");
        require(chunk(a) == valid, "Malformed state changed factory parameters");
        require(select(a, 3) == 1, "Invalid chunk armed the restore guard");
        defaults(a, 3);
    }
    for (int index = 0; index < 34; ++index)
        if (index != 16 && index != 17) a->setParameter(a, index, .25f);
    a->setParameter(a, 2, 1);
    const auto project = chunk(a);
    require(a->dispatcher(a, 24, 0, project.size(), const_cast<char*>(project.data()), 0) == 1,
            "Project state roundtrip rejected");
    require(select(a, 0) == 1 && chunk(a) == project && a->dispatcher(a, 3, 0, 0, nullptr, 0) == 3,
            "Post-chunk host program selection erased restored edits or the engine");
    render(a, 0);
    require(select(a, 0) == 1 && chunk(a) == project, "Zero-frame processing cleared the restore guard");
    render(a, 128);
    require(select(a, 3) == 1, "Current program could not be reset after the first positive block");
    defaults(a, 3);
    require(b->dispatcher(b, 24, 0, originalB.size(), const_cast<char*>(originalB.data()), 0) == 1,
            "Second-instance project restore rejected");
    render(a, 128);
    require(select(b, 2) == 1 && chunk(b) == originalB, "One instance cleared another instance's restore guard");
    render(b, 128);
    require(select(b, 2) == 1, "Second-instance restore guard did not clear on its own positive block");
    defaults(b, 2);
    require(a->dispatcher(a, 24, 0, originalA.size(), const_cast<char*>(originalA.data()), 0) == 1 &&
                b->dispatcher(b, 24, 0, originalB.size(), const_cast<char*>(originalB.data()), 0) == 1,
            "Factory program fixture restore rejected");
}
void chordPadControls(Effect* a, Effect* b) {
    constexpr double pi = 3.14159265358979323846;
    const double radii[] = {0, .26, .52, .85};
    const auto originalA = chunk(a), originalB = chunk(b);
    auto restore = [&](Effect* effect, const std::string& saved) {
        require(effect->dispatcher(effect, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
                "XY fixture state restore rejected");
    };
    auto point = [&](double x, double y, bool reverse) {
        settingUi = true;
        if (reverse) {
            a->setParameter(a, 17, float(y));
            a->setParameter(a, 16, float(x));
        } else {
            a->setParameter(a, 16, float(x));
            a->setParameter(a, 17, float(y));
        }
        settingUi = false;
        require(!reenteredUi, "XY gesture re-entered its host setter");
    };
    for (int index = 0; index < 34; ++index)
        require(a->dispatcher(a, 26, index, 0, nullptr, 0) == 1,
                "XY or DSP parameter lost host automation");
    for (int index : {-1, 34})
        require(a->dispatcher(a, 26, index, 0, nullptr, 0) == 0, "Invalid parameter became automatable");
    require(!std::strcmp(PARAMS[16].key, "ChordPadX") && !std::strcmp(PARAMS[17].key, "ChordPadY") &&
                !PARAMS[16].nopts && !PARAMS[17].nopts && PARAMS[16].min == 0 && PARAMS[16].max == 1 &&
                PARAMS[17].min == 0 && PARAMS[17].max == 1,
            "UI aliases were not appended after the original DSP parameters");
    for (int chord = 0; chord < 8; ++chord) {
        for (int inversion = 0; inversion < 4; ++inversion) {
            for (bool reverse : {false, true}) {
                a->setParameter(a, 0, float(chord) / 7);
                a->setParameter(a, 1, float(inversion) / 3);
                const auto expected = chunk(a);
                const double angle = chord * pi / 4.;
                const double x = .5 + 160. / 410. * radii[inversion] * std::sin(angle);
                const double y = .5 + 160. / 410. * radii[inversion] * std::cos(angle);
                require(std::fabs(a->getParameter(a, 16) - x) < .000001 &&
                            std::fabs(a->getParameter(a, 17) - y) < .000001,
                        "VST direct choices did not synchronize XY handles");
                a->setParameter(a, 0, float(inversion ? (chord + 4) % 8 : chord) / 7);
                a->setParameter(a, 1, 0);
                const auto before = chunk(a);
                notifications.clear();
                point(x, y, reverse);
                require(chunk(a) == before && notifications.empty(),
                        "XY coordinate writes escaped before the audio-block boundary");
                render(a, 128, 0, reverse);
                require(std::fabs(a->getParameter(a, 0) - float(chord) / 7) < .000001 &&
                            std::fabs(a->getParameter(a, 1) - float(inversion) / 3) < .000001,
                        "VST polar XY selection is incorrect");
                require(chunk(a) == expected, "VST XY gesture changed other DSP state or chunk bytes");
                require(chunk(b) == originalB, "VST XY gesture leaked to another instance");
                for (const auto& notification : notifications)
                    require(notification.effect == a && notification.index >= 0 && notification.index < 2 &&
                                std::fabs(notification.value - a->getParameter(a, notification.index)) < .000001,
                            "XY gesture reported a UI alias or unrelated parameter");
            }
        }
    }
    restore(a, originalA);
    render(a, 128);
    a->setParameter(a, 0, 1.25f / 7);
    a->setParameter(a, 1, 1.25f / 3);
    a->setParameter(a, 2, 1.25f / 3);
    const float enginePosition = a->getParameter(a, 2);
    notifications.clear();
    displayUpdates = 0;
    point(.5, .5 + 160. / 410. * .52, false);
    render(a, 128);
    require(a->getParameter(a, 0) == 0 && std::fabs(a->getParameter(a, 1) - 2.f / 3) < .000001,
            "XY gesture retained the changed chord or voicing Q-Link shadow");
    require(a->getParameter(a, 2) == enginePosition, "XY gesture reset an unrelated continuous Q-Link position");
    require(notifications.size() == 2,
            "XY gesture did not notify only its two changed DSP parameters");
    for (int frames = 0; frames < 2205 && displayUpdates == 0; frames += 128)
        render(a, 128);
    require(displayUpdates == 1, "XY display refresh exceeded the wrapper's 50 ms redraw interval");
    for (const auto& notification : notifications)
        require(notification.index < 2, "XY notification echoed an unrelated host parameter");
    const auto beforeInvalid = chunk(a);
    const float x = a->getParameter(a, 16), y = a->getParameter(a, 17);
    for (float invalid : {NAN, INFINITY, -INFINITY}) {
        point(invalid, invalid, false);
        require(chunk(a) == beforeInvalid && a->getParameter(a, 16) == x && a->getParameter(a, 17) == y,
                "VST invalid XY input mutated state or handles");
    }
    notifications.clear();
    a->setParameter(a, 0, 3.f / 7);
    a->setParameter(a, 1, 2.f / 3);
    render(a, 128);
    require(notifications.empty(), "Ordinary host enum changes were echoed as custom UI changes");
    point(.1, .1, false);
    restore(a, originalA);
    render(a, 128);
    require(chunk(a) == originalA && a->getParameter(a, 16) == .5f && a->getParameter(a, 17) == .5f,
            "VST chunk restore retained a pending XY gesture or stale handles");
    restore(b, originalB);
}
void chordPadAutomation(Effect* a, Effect* b) {
    constexpr double pi = 3.14159265358979323846;
    const double radii[] = {0, .26, .52, .85};
    struct Step { int chord; int voicing; bool xy; };
    const Step steps[] = {{7, 0, false}, {0, 1, true}, {2, 2, true}, {4, 3, true},
                          {6, 1, false}, {7, 2, true}, {1, 3, true}, {1, 0, true}, {5, 2, true}};
    struct Sample { int index; float value; };
    const auto originalA = chunk(a), originalB = chunk(b);
    render(b, 128);
    auto restore = [&](Effect* effect, const std::string& saved) {
        require(effect->dispatcher(effect, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
                "Automation fixture state restore rejected");
    };
    for (bool recordReverse : {false, true}) {
        restore(a, originalA);
        render(a, 128);
        std::vector<Sample> recorded;
        std::vector<std::string> states;
        auto edit = [&](int index, float value) {
            if (a->dispatcher(a, 26, index, 0, nullptr, 0)) recorded.push_back({index, value});
            settingUi = true;
            a->setParameter(a, index, value);
            settingUi = false;
            require(!reenteredUi, "Recording an editor change re-entered its host setter");
        };
        for (const auto& step : steps) {
            const double angle = step.chord * pi / 4.;
            const Sample pair[] = {{step.xy ? 16 : 0, step.xy ? float(.5 + 160. / 410. * radii[step.voicing] * std::sin(angle)) : step.chord / 7.f},
                                   {step.xy ? 17 : 1, step.xy ? float(.5 + 160. / 410. * radii[step.voicing] * std::cos(angle)) : step.voicing / 3.f}};
            const auto before = chunk(a);
            notifications.clear();
            for (int axis = 0; axis < 2; ++axis) {
                const auto& sample = pair[recordReverse ? 1 - axis : axis];
                edit(sample.index, sample.value);
            }
            require(notifications.empty(), "Editor recording notified synchronously");
            if (step.xy) require(chunk(a) == before, "Recorded XY writes bypassed the audio-block boundary");
            render(a, 128);
            require(std::fabs(a->getParameter(a, 0) - step.chord / 7.f) < .000001f &&
                        std::fabs(a->getParameter(a, 1) - step.voicing / 3.f) < .000001f,
                    "Recorded editor gesture mapped to the wrong musical selection");
            if (!step.xy) require(notifications.empty(), "Direct recorded choices were echoed as plugin edits");
            states.push_back(chunk(a));
        }
        require(recorded.size() == states.size() * 2, "Host excluded a diagram coordinate from the recorded track");
        const auto captured = chunk(a);
        for (bool replayReverse : {false, true}) {
            b->setParameter(b, 16, .1f);
            b->setParameter(b, 17, .9f);
            restore(b, originalA);
            notifications.clear();
            render(b, 128);
            require(chunk(b) == originalA && notifications.empty(), "Chunk restore retained a pending coordinate edit");
            for (size_t block = 0; block < states.size(); ++block) {
                const auto before = chunk(b);
                notifications.clear();
                for (int axis = 0; axis < 2; ++axis) {
                    const auto& sample = recorded[block * 2 + (replayReverse ? 1 - axis : axis)];
                    b->setParameter(b, sample.index, sample.value);
                }
                require(notifications.empty(), "Automation replay notified inside a host setter");
                if (steps[block].xy) require(chunk(b) == before, "Replayed XY writes bypassed the audio-block boundary");
                render(b, 128);
                require(chunk(b) == states[block], "Recorded automation did not replay the exact musical state");
                require(chunk(a) == captured, "Automation replay altered the recording instance");
                if (!steps[block].xy) require(notifications.empty(), "Direct automation replay echoed a plugin edit");
                for (const auto& notice : notifications)
                    require(notice.effect == b && notice.index >= 0 && notice.index < 2,
                            "Coordinate automation replay notified an alias or another instance");
            }
            const auto saved = chunk(b);
            b->setParameter(b, 0, 0);
            b->setParameter(b, 1, 0);
            restore(b, saved);
            notifications.clear();
            render(b, 128);
            require(chunk(b) == saved && notifications.empty(), "Saved replay state changed on chunk reload");
        }
    }
    restore(a, originalA);
    restore(b, originalB);
    render(a, 128);
    render(b, 128);
}
}

int main(int argc, char** argv) {
    vectorMath();
    envelopes();
    effectFilters();
    require(argc == 2, "Supply plugin library path");
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }
    auto entry = reinterpret_cast<Effect* (*)(Host)>(dlsym(library, "VSTPluginMain"));
    require(entry != nullptr, "VST entry export");
    Effect* a = entry(host);
    Effect* b = entry(host);
    require(a && b && a != b, "Two VST instances");
    require(a->magic == 0x56737450 && a->numInputs == 0 && a->numOutputs == 2 && a->numParams == 34, "VST instrument ABI");
    require(a->uniqueID == 0x48783733 && a->version == PLUG_VERSION, "Stable identity and declared plugin version");
    require(a->flags & (1 << 8), "Instrument flag");
    require(a->dispatcher(a, 0, 0, 0, nullptr, 0) == 1, "VST open");
    floatingPoint(a);
    require(parameterText(a, 45, 0) == "HexChord 73", "VST effect name");
    require(parameterText(a, 48, 0) == "HexChord 73", "VST product name");
    require(parameterText(a, 47, 0) == "Twisted Cable", "VST vendor name");
    factoryPrograms(a, b);
    displays(a);
    engineControls(a, b);
    factoryDefaults(a, b);
    enumControls(a, b);
    chordPadControls(a, b);
    chordPadAutomation(a, b);
    const auto unmappedState = chunk(a);
    for (int controller = 20; controller <= 35; ++controller) midi(a, 0xb0, controller, 127, 0);
    for (int parameter : {0, 6, 18}) {
        midi(a, 0xb0, 99, 0, 0);
        midi(a, 0xb0, 98, parameter, 0);
        midi(a, 0xb0, 6, 127, 0);
        midi(a, 0xb0, 38, 127, 0);
    }
    render(a, 128);
    require(chunk(a) == unmappedState, "Unmapped MIDI CC/NRPN changed plugin parameters");
    a->dispatcher(a, 10, 0, 0, nullptr, 44100);
    a->dispatcher(a, 11, 0, 257, nullptr, 0);
    a->dispatcher(a, 12, 0, 1, nullptr, 0);
    require(render(a, 257) == 0, "New VST instance should be silent");
    require(render(b, 257) == 0, "Second VST instance should be silent");
    a->setParameter(a, 8, 0);
    a->setParameter(a, 9, 0);
    a->setParameter(a, 11, 0);
    a->setParameter(a, 12, 0);
    a->setParameter(a, 13, 0);
    a->setParameter(a, 14, 0);
    a->setParameter(a, 6, .8f);
    require(std::fabs(a->getParameter(a, 6) - .8f) < .00001f, "Parameter roundtrip");
    require(std::fabs(b->getParameter(b, 6) - .8f) > .01f, "VST instance parameter isolation");
    char display[64]{};
    a->dispatcher(a, 7, 6, 0, display, 0);
    require(std::fabs(std::atof(display) - 5023.773) < 1, "Curved parameter physical display");
    void* pointer = nullptr;
    intptr_t bytes = a->dispatcher(a, 23, 0, 0, &pointer, 0);
    require(pointer && bytes > 1 && bytes <= 8192, "Bounded VST chunk");
    std::string chunk(static_cast<const char*>(pointer), size_t(bytes));
    require(b->dispatcher(b, 24, 0, bytes, chunk.data(), 0) == 1, "VST chunk reload");
    for (int i = 0; i < 34; ++i) require(std::fabs(a->getParameter(a, i) - b->getParameter(b, i)) < .00001f, "VST restored parameter mismatch");
    b->dispatcher(b, 24, 0, 8, const_cast<char*>("INVALID"), 0);
    require(std::fabs(b->getParameter(b, 6) - .8f) < .00001f, "Invalid VST state mutated parameters");
    require(b->dispatcher(b, 24, 0, 8193, chunk.data(), 0) == 0, "Oversized VST chunk accepted");
    midi(a, 0x90, 48, 110, 65);
    require(render(a, 257, 65) > 0, "Sample-accurate VST note produced no audio");
    require(render(b, 257) == 0, "VST instance audio isolation");
    for (int frames : {1, 17, 64, 127, 128, 129, 513, 4096}) require(render(a, frames) > 0, "Variable host block lost audio");
    midi(a, 0x90, 48, 0, 0);
    for (int i = 0; i < 200; ++i) render(a, 128);
    require(render(a, 128) == 0, "VST note-off did not release");
    require(std::fabs(render(a, 128, 0, true) - 16) < .00001, "Legacy process did not accumulate");
    midi(a, 0x90, 55, 120, 128);
    require(render(a, 128) == 0, "End-of-block event played early");
    require(render(a, 128) > 0, "End-of-block event lost");
    midi(a, 0xb0, 120, 0, 0);
    require(render(a, 128) == 0, "Panic left VST audio");
    a->dispatcher(a, 12, 0, 0, nullptr, 0);
    a->dispatcher(a, 1, 0, 0, nullptr, 0);
    b->dispatcher(b, 1, 0, 0, nullptr, 0);
    dlclose(library);
    std::puts("Native VST ABI, sample timing, automation, continuous enum host contracts, state, isolation and host-block tests passed.");
}
