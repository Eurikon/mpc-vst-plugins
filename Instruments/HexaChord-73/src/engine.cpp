#include "native_dsp.hpp"
#include "hexachord_params.h"
extern "C" {
#include "engine.h"
}
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

namespace {
constexpr int parameterCount = 32;
constexpr int legacyParameterCount = 16;
constexpr int feltStateParameterCount = 20;
constexpr int stateCapacity = 8192;
constexpr double pi = 3.14159265358979323846;
constexpr bool chordPadYIncreasesUp = true;
constexpr double chordPadScale = 205. / 160.;
constexpr double voicingRadii[] = {0, .26, .52, .85};
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(sizeof(unsigned) == 4);
static_assert(sizeof(hexachord::parameters) / sizeof(hexachord::parameters[0]) == parameterCount);

float hostMinimum(int i) {
    return hexachord::parameters[i].curve == hexachord::Curve::Linear ? hexachord::parameters[i].minimum : 0.f;
}
float hostMaximum(int i) {
    return hexachord::parameters[i].curve == hexachord::Curve::Linear ? hexachord::parameters[i].maximum : 1.f;
}
float physicalValue(int i, float value) {
    const auto& p = hexachord::parameters[i];
    if (p.curve == hexachord::Curve::Exponential)
        return p.minimum * std::pow(p.maximum / p.minimum, value);
    if (p.curve == hexachord::Curve::Squared)
        return p.minimum + value * value * (p.maximum - p.minimum);
    return value;
}
float initialValue(int i) {
    const auto& p = hexachord::parameters[i];
    if (p.curve == hexachord::Curve::Exponential)
        return std::log(p.initial / p.minimum) / std::log(p.maximum / p.minimum);
    if (p.curve == hexachord::Curve::Squared)
        return std::sqrt((p.initial - p.minimum) / (p.maximum - p.minimum));
    return p.initial;
}
struct Engine {
    hexachord::native::Dsp dsp;
    std::atomic<float> values[parameterCount];
    std::atomic<float> chordPad[2];
    std::atomic<bool> chordPadPending{false};
    std::atomic<float> meters[3]{};
    std::atomic<unsigned> revision{0};
    std::atomic<bool> fault{false};
    std::mutex writer;
    float applied[parameterCount];
    unsigned appliedRevision = 1;
    bool held[16][128]{};
    bool sustained[16][128]{};
    bool pedal[16]{};
#ifdef HEXACHORD_TESTING
    bool trapNextRender = false;
    unsigned parameterSnapshots = 0;
#endif
    Engine() {
        for (auto& position : chordPad) position.store(.5f, std::memory_order_relaxed);
        for (auto& meter : meters) meter.store(0.f, std::memory_order_relaxed);
        for (int i = 0; i < parameterCount; ++i) {
            values[i].store(initialValue(i), std::memory_order_relaxed);
            applied[i] = NAN;
        }
    }
};

template<class Function> bool guarded(Engine& e, Function function) {
    if (e.fault.load(std::memory_order_relaxed)) return false;
    function();
    return !e.fault.load(std::memory_order_relaxed);
}

bool snapshot(Engine& e, float* destination, unsigned* revision = nullptr) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        unsigned before = e.revision.load(std::memory_order_acquire);
        if (before & 1) continue;
        for (int i = 0; i < parameterCount; ++i) destination[i] = e.values[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (before == e.revision.load(std::memory_order_acquire)) {
            if (revision) *revision = before;
            return true;
        }
    }
    return false;
}

void synchronize(Engine& e) {
    const unsigned published = e.revision.load(std::memory_order_acquire);
    if (!(published & 1) && published == e.appliedRevision) return;
    float current[parameterCount];
    unsigned revision;
    if (!snapshot(e, current, &revision)) return;
#ifdef HEXACHORD_TESTING
    ++e.parameterSnapshots;
#endif
    for (int i = 0; i < parameterCount; ++i) {
        if (current[i] == e.applied[i]) continue;
        e.dsp.setParam(i, physicalValue(i, current[i]));
        e.applied[i] = current[i];
    }
    e.appliedRevision = revision;
}

void* create(const char*) {
    Engine* e = nullptr;
    try {
        e = new Engine;
        e->dsp.init(44100.f);
        synchronize(*e);
    } catch (...) {
        delete e;
        return nullptr;
    }
    return e;
}
void destroy(void* instance) { delete static_cast<Engine*>(instance); }
bool pitchHeld(const Engine& e, int note) {
    for (int c = 0; c < 16; ++c) if (e.held[c][note] || e.sustained[c][note]) return true;
    return false;
}
void release(Engine& e, int channel, int note) {
    e.held[channel][note] = false;
    e.sustained[channel][note] = e.pedal[channel];
    if (!pitchHeld(e, note)) e.dsp.noteOff(note);
}
void midi(void* instance, const uint8_t* message, int length) {
    if (!instance || !message || length < 3 || message[1] > 127 || message[2] > 127) return;
    auto& e = *static_cast<Engine*>(instance);
    const int type = message[0] & 0xf0, channel = message[0] & 15, note = message[1];
    guarded(e, [&] {
        synchronize(e);
        if (type == 0x90 && message[2]) {
            e.held[channel][note] = true;
            e.sustained[channel][note] = false;
            e.dsp.noteOn(note, message[2]);
        } else if (type == 0x80 || type == 0x90) {
            if (e.held[channel][note]) release(e, channel, note);
        } else if (type == 0xb0 && note == 64) {
            e.pedal[channel] = message[2] >= 64;
            if (!e.pedal[channel]) {
                for (int n = 0; n < 128; ++n) {
                    bool wasSustained = e.sustained[channel][n];
                    e.sustained[channel][n] = false;
                    if (wasSustained && !pitchHeld(e, n)) e.dsp.noteOff(n);
                }
            }
        } else if (type == 0xb0 && note == 123) {
            for (int n = 0; n < 128; ++n) if (e.held[channel][n]) release(e, channel, n);
        } else if (type == 0xb0 && note == 120) {
            // The shared effects retain mixed tails, so panic clears the whole instrument.
            std::memset(e.held, 0, sizeof e.held);
            std::memset(e.sustained, 0, sizeof e.sustained);
            std::memset(e.pedal, 0, sizeof e.pedal);
            e.dsp.allSoundOff();
        } else if (type == 0xb0 && note == 121) {
            e.pedal[channel] = false;
            for (int n = 0; n < 128; ++n) {
                bool wasSustained = e.sustained[channel][n];
                e.sustained[channel][n] = false;
                if (wasSustained && !pitchHeld(e, n)) e.dsp.noteOff(n);
            }
        }
    });
}
int indexOf(const char* key) {
    for (int i = 0; i < parameterCount; ++i) if (!std::strcmp(key, hexachord::parameters[i].key)) return i;
    return -1;
}
bool number(const char* text, float& value) {
    char* end = nullptr;
    errno = 0;
    value = std::strtof(text, &end);
    return end != text && *end == 0 && errno != ERANGE && std::isfinite(value);
}
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool parseState(const char* text, float* values) {
    if (strnlen(text, stateCapacity) == stateCapacity) return false;
    int savedCount = 0;
    if (!std::strncmp(text, "HX73 1 ", 7)) savedCount = legacyParameterCount;
    else if (!std::strncmp(text, "HX73 2 ", 7)) savedCount = feltStateParameterCount;
    else if (!std::strncmp(text, "HX73 3 ", 7)) savedCount = parameterCount;
    else return false;
    for (int i = 0; i < parameterCount; ++i) values[i] = initialValue(i);
    const char* cursor = text + 7;
    for (int i = 0; i < savedCount; ++i) {
        while (whitespace(*cursor)) ++cursor;
        char* end = nullptr;
        errno = 0;
        values[i] = std::strtof(cursor, &end);
        if (end == cursor || errno == ERANGE || !std::isfinite(values[i]) ||
            values[i] < hostMinimum(i) || values[i] > hostMaximum(i) ||
            (hexachord::parameters[i].stepped && values[i] != std::round(values[i])) ||
            (*end && !whitespace(*end))) return false;
        cursor = end;
    }
    while (whitespace(*cursor)) ++cursor;
    return *cursor == 0;
}
void alignChordPad(Engine& e) {
    const double angle = e.values[0].load(std::memory_order_relaxed) * pi / 4;
    const double radius = voicingRadii[int(e.values[1].load(std::memory_order_relaxed))] / chordPadScale;
    e.chordPad[0].store(float(.5 + .5 * radius * std::sin(angle)), std::memory_order_relaxed);
    const double vertical = .5 * radius * std::cos(angle);
    e.chordPad[1].store(float(.5 + (chordPadYIncreasesUp ? vertical : -vertical)), std::memory_order_relaxed);
    e.chordPadPending.store(false, std::memory_order_release);
}
void setChordPad(Engine& e, int axis, float value) {
    std::lock_guard<std::mutex> lock(e.writer);
    // XY handles arrive separately; commit their latest pair at an audio-block boundary.
    e.chordPad[axis].store(std::fmax(0.f, std::fmin(1.f, value)), std::memory_order_relaxed);
    e.chordPadPending.store(true, std::memory_order_release);
}
void applyChordPad(Engine& e) {
    std::unique_lock<std::mutex> lock(e.writer, std::try_to_lock);
    if (!lock.owns_lock()) return;
    if (!e.chordPadPending.exchange(false, std::memory_order_acq_rel)) return;
    e.revision.fetch_add(1, std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_release);
    const double x = chordPadScale * (2. * e.chordPad[0].load(std::memory_order_relaxed) - 1.);
    const double y = (chordPadYIncreasesUp ? 1. : -1.) *
                     chordPadScale * (2. * e.chordPad[1].load(std::memory_order_relaxed) - 1.);
    const double radius = std::hypot(x, y);
    const int inversion = radius > .68 ? 3 : radius > .39 ? 2 : radius > .13 ? 1 : 0;
    e.values[1].store(float(inversion), std::memory_order_relaxed);
    if (radius > .13) {
        double angle = std::atan2(x, y) * 180. / pi;
        if (angle < 0) angle += 360.;
        const int current = int(e.values[0].load(std::memory_order_relaxed));
        const double difference = std::remainder(angle - current * 45., 360.);
        if (std::fabs(difference) > 28.5)
            e.values[0].store(float(int(std::lround(angle / 45.)) % 8), std::memory_order_relaxed);
    }
    e.revision.fetch_add(1, std::memory_order_release);
}
void setParam(void* instance, const char* key, const char* text) {
    if (!instance || !key || !text) return;
    auto& e = *static_cast<Engine*>(instance);
    float staged[parameterCount];
    if (!std::strcmp(key, "state")) {
        if (!parseState(text, staged)) return;
        std::lock_guard<std::mutex> lock(e.writer);
        e.revision.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_release);
        for (int i = 0; i < parameterCount; ++i) e.values[i].store(staged[i], std::memory_order_relaxed);
        alignChordPad(e);
        e.revision.fetch_add(1, std::memory_order_release);
        return;
    }
    if (!std::strcmp(key, "ChordPadX") || !std::strcmp(key, "ChordPadY")) {
        float value;
        if (number(text, value)) setChordPad(e, !std::strcmp(key, "ChordPadY"), value);
        return;
    }
    if (!std::strcmp(key, "ChordPadApply")) {
        applyChordPad(e);
        return;
    }
    int i = indexOf(key);
    float value;
    if (i < 0 || !number(text, value)) return;
    value = std::fmax(hostMinimum(i), std::fmin(hostMaximum(i), value));
    if (hexachord::parameters[i].stepped) value = std::round(value);
    std::lock_guard<std::mutex> lock(e.writer);
    e.revision.fetch_add(1, std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_release);
    e.values[i].store(value, std::memory_order_relaxed);
    if (i < 2) alignChordPad(e);
    e.revision.fetch_add(1, std::memory_order_release);
}
int formatValue(char* buffer, int length, float value, const char* format = "%.9g") {
    int written = std::snprintf(buffer, length, format, value);
    if (written < 0 || written >= length) { buffer[0] = 0; return 0; }
    return written;
}
int getParam(void* instance, const char* key, char* buffer, int length) {
    if (!instance || !key || !buffer || length <= 0) return 0;
    buffer[0] = 0;
    auto& e = *static_cast<Engine*>(instance);
    if (!std::strcmp(key, "ChordPadX") || !std::strcmp(key, "ChordPadY"))
        return formatValue(buffer, length, e.chordPad[!std::strcmp(key, "ChordPadY")].load(std::memory_order_relaxed));
    if (!std::strcmp(key, "ChordPadPending")) {
        if (length < 2) return 0;
        buffer[0] = e.chordPadPending.load(std::memory_order_acquire) ? '1' : '0';
        buffer[1] = 0;
        return 1;
    }
    if (!std::strcmp(key, "state")) {
        float values[parameterCount];
        if (!snapshot(e, values)) return 0;
        int used = std::snprintf(buffer, length, "HX73 3");
        if (used < 0 || used >= length) { buffer[0] = 0; return 0; }
        for (float value : values) {
            int written = std::snprintf(buffer + used, length - used, " %.9g", value);
            if (written < 0 || written >= length - used) { buffer[0] = 0; return 0; }
            used += written;
        }
        return used;
    }
    if (!std::strcmp(key, "fault")) return formatValue(buffer, length, e.fault.load() ? 1.f : 0.f);
    const char* meterNames[] = {"meter_l", "meter_r", "voices"};
    for (int i = 0; i < 3; ++i) if (!std::strcmp(key, meterNames[i])) return formatValue(buffer, length, e.meters[i].load());
    int i = indexOf(key);
    bool display = false;
    if (i < 0) {
        for (int n = 0; n < parameterCount; ++n) {
            char candidate[64];
            std::snprintf(candidate, sizeof candidate, "%s_display", hexachord::parameters[n].key);
            if (!std::strcmp(key, candidate)) { i = n; display = true; break; }
        }
    }
    if (i < 0) return 0;
    float value = e.values[i].load(std::memory_order_relaxed);
    if (display) {
        value = physicalValue(i, value);
        if (hexachord::parameters[i].curve != hexachord::Curve::Linear) {
            if (!std::isfinite(value)) return 0;
            value = std::fmax(hexachord::parameters[i].minimum, std::fmin(hexachord::parameters[i].maximum, value));
            return formatValue(buffer, length, value, "%.0f");
        }
    }
    return formatValue(buffer, length, value);
}
int16_t pcm(float value) {
    if (!std::isfinite(value)) return 0;
    if (value >= 32767.f / 32768.f) return 32767;
    if (value <= -1.f) return -32768;
    return static_cast<int16_t>(std::lrintf(value * 32768.f));
}
void render(void* instance, int16_t* output, int frames) {
    if (!output || frames <= 0) return;
    if (frames > 128) return;
    std::memset(output, 0, size_t(frames) * 2 * sizeof(int16_t));
    if (!instance) return;
    auto& e = *static_cast<Engine*>(instance);
    bool valid = guarded(e, [&] {
#ifdef HEXACHORD_TESTING
        if (e.trapNextRender) { e.trapNextRender = false; e.fault.store(true, std::memory_order_relaxed); return; }
#endif
        synchronize(e);
        if (e.dsp.render(uint32_t(frames)) != uint32_t(frames))
            e.fault.store(true, std::memory_order_relaxed);
    });
    if (!valid) {
        for (auto& meter : e.meters) meter.store(0.f, std::memory_order_relaxed);
        return;
    }
    const float* left = e.dsp.output(0);
    const float* right = e.dsp.output(1);
    for (int n = 0; n < frames; ++n) {
        output[2 * n] = pcm(left[n]);
        output[2 * n + 1] = pcm(right[n]);
    }
    for (int i = 0; i < 3; ++i) {
        float value = e.dsp.telemetry()[i];
        e.meters[i].store(std::isfinite(value) ? value : 0.f, std::memory_order_relaxed);
    }
}
const mpc_engine_t api = {create, destroy, midi, setParam, getParam, render, nullptr};
}

extern "C" const mpc_engine_t* mpc_engine() { return &api; }
#ifdef HEXACHORD_TESTING
extern "C" unsigned hexachord_test_parameter_snapshots(void* instance) {
    return instance ? static_cast<Engine*>(instance)->parameterSnapshots : 0;
}
extern "C" float hexachord_test_applied_param(void* instance, int index) {
    auto* e = static_cast<Engine*>(instance);
    return e && index >= 0 && index < parameterCount ? e->dsp.getParam(index) : NAN;
}
extern "C" void hexachord_test_force_trap(void* instance) {
    if (instance) static_cast<Engine*>(instance)->trapNextRender = true;
}
extern "C" uint64_t hexachord_test_memory_bytes(void* instance) {
    auto* e = static_cast<Engine*>(instance);
    return e ? e->dsp.storageBytes() : 0;
}
extern "C" void* hexachord_test_memory_address(void* instance) {
    auto* e = static_cast<Engine*>(instance);
    return e ? const_cast<void*>(e->dsp.storageAddress()) : nullptr;
}
#endif
