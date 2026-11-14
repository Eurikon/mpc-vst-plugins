#include "native_dsp.hpp"
#include "native_fp.h"
#include "hexachord_params.h"
extern "C" {
#include "engine.h"
}
#include <atomic>
#include <array>
#include <cerrno>
#include <charconv>
#include <cfenv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
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
float finiteClamp(float value, float minimum, float maximum) {
    // Preserve the library's signed-zero choice at zero bounds.
    if (value == 0.f && (minimum == 0.f || maximum == 0.f)) return std::fmax(minimum, std::fmin(maximum, value));
    return value < minimum ? minimum : value > maximum ? maximum : value;
}
struct Engine {
    hexachord::native::Dsp dsp;
    std::atomic<float> values[parameterCount];
    std::atomic<float> chordPad[2];
    float chordPositions[8][4][2]{};
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
        for (int chord = 0; chord < 8; ++chord) for (int voicing = 0; voicing < 4; ++voicing) {
            const double angle = float(chord) * pi / 4;
            const double radius = voicingRadii[voicing] / chordPadScale;
            chordPositions[chord][voicing][0] = float(.5 + .5 * radius * std::sin(angle));
            const double vertical = .5 * radius * std::cos(angle);
            chordPositions[chord][voicing][1] = float(.5 + (chordPadYIncreasesUp ? vertical : -vertical));
        }
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
constexpr unsigned keyHash(const char* key) {
    unsigned result = 2166136261u;
    while (*key) result = (result ^ static_cast<unsigned char>(*key++)) * 16777619u;
    return result;
}
constexpr auto parameterIndex() {
    std::array<int, 64> result{};
    for (auto& index : result) index = -1;
    for (int index = 0; index < parameterCount; ++index) {
        unsigned slot = keyHash(hexachord::parameters[index].key) & 63u;
        while (result[slot] >= 0) slot = (slot + 1) & 63u;
        result[slot] = index;
    }
    return result;
}
int indexOf(const char* key) {
    static constexpr auto indices = parameterIndex();
    unsigned slot = keyHash(key) & 63u;
    while (indices[slot] >= 0) {
        const int index = indices[slot];
        if (!std::strcmp(key, hexachord::parameters[index].key)) return index;
        slot = (slot + 1) & 63u;
    }
    return -1;
}
bool number(const char* text, float& value) {
    errno = 0;
    const char* cursor = text;
    const bool negative = *cursor == '-';
    if (negative || *cursor == '+') ++cursor;
    unsigned integer = 0, digits = 0;
    while (*cursor >= '0' && *cursor <= '9' && digits < 7) {
        integer = integer * 10 + unsigned(*cursor++ - '0');
        ++digits;
    }
    if (digits && !*cursor) {
        value = negative ? -float(integer) : float(integer);
        return true;
    }
    if (*cursor == '.') {
        ++cursor;
        unsigned places = 0;
        while (*cursor >= '0' && *cursor <= '9' && digits < 7) {
            integer = integer * 10 + unsigned(*cursor++ - '0');
            ++digits; ++places;
        }
        if (digits && !*cursor) {
            static constexpr double divisor[] = {1., 10., 100., 1000., 10000., 100000., 1000000., 10000000.};
            value = float((negative ? -double(integer) : double(integer)) / divisor[places]);
            return true;
        }
    }
    char* end = nullptr;
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
    const int chord = int(e.values[0].load(std::memory_order_relaxed));
    const int voicing = int(e.values[1].load(std::memory_order_relaxed));
    e.chordPad[0].store(e.chordPositions[chord][voicing][0], std::memory_order_relaxed);
    e.chordPad[1].store(e.chordPositions[chord][voicing][1], std::memory_order_relaxed);
    e.chordPadPending.store(false, std::memory_order_release);
}
void setChordPad(Engine& e, int axis, float value) {
    std::lock_guard<std::mutex> lock(e.writer);
    // XY handles arrive separately; commit their latest pair at an audio-block boundary.
    e.chordPad[axis].store(finiteClamp(value, 0.f, 1.f), std::memory_order_relaxed);
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
    value = finiteClamp(value, hostMinimum(i), hostMaximum(i));
    if (hexachord::parameters[i].stepped) value = std::round(value);
    std::lock_guard<std::mutex> lock(e.writer);
    const float previous = e.values[i].load(std::memory_order_relaxed);
    // Chord selectors must still realign the touch point when their value is unchanged.
    if (i >= 2 && value == previous && std::signbit(value) == std::signbit(previous)) return;
    e.revision.fetch_add(1, std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_release);
    e.values[i].store(value, std::memory_order_relaxed);
    if (i < 2) alignChordPad(e);
    e.revision.fetch_add(1, std::memory_order_release);
}
int formatValue(char* buffer, int length, float value, const char* format = "%.9g") {
    if (value >= -99999999.f && value <= 99999999.f && value == float(int(value)) &&
        !(value == 0.f && std::signbit(value)) && !std::strcmp(format, "%.9g")) {
        const auto result = std::to_chars(buffer, buffer + length - 1, int(value));
        if (result.ec != std::errc{}) { buffer[0] = 0; return 0; }
        *result.ptr = 0;
        return int(result.ptr - buffer);
    }
    if (!std::strcmp(format, "%.9g")) {
        const auto result = std::to_chars(buffer, buffer + length - 1, value, std::chars_format::general, 9);
        if (result.ec != std::errc{}) { buffer[0] = 0; return 0; }
        *result.ptr = 0;
        return int(result.ptr - buffer);
    }
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
        const size_t length = std::strlen(key);
        if (length > 8 && !std::strcmp(key + length - 8, "_display")) {
            for (int n = 0; n < parameterCount; ++n) {
                const char* name = hexachord::parameters[n].key;
                if (std::strlen(name) == length - 8 && !std::strncmp(key, name, length - 8)) {
                    i = n; display = true; break;
                }
            }
        }
    }
    if (i < 0) return 0;
    float value = e.values[i].load(std::memory_order_relaxed);
    if (display) {
        value = physicalValue(i, value);
        if (hexachord::parameters[i].curve != hexachord::Curve::Linear) {
            if (!std::isfinite(value)) return 0;
            value = finiteClamp(value, hexachord::parameters[i].minimum, hexachord::parameters[i].maximum);
            return formatValue(buffer, length, value, "%.0f");
        }
    }
    return formatValue(buffer, length, value);
}
int16_t pcm(float value) {
    if (!std::isfinite(value)) return 0;
    if (value >= 32767.f / 32768.f) return 32767;
    if (value <= -1.f) return -32768;
    return static_cast<int16_t>(hexachord_round_sample(value * 32768.f));
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
const mpc_engine_t api = {create, destroy, midi, setParam, getParam, render, nullptr, nullptr};
}

extern "C" const mpc_engine_t* mpc_engine() { return &api; }
extern "C" int hexachord_chord_pending(void* instance) {
    return instance && static_cast<Engine*>(instance)->chordPadPending.load(std::memory_order_acquire);
}
extern "C" int hexachord_snprintf(char* buffer, size_t capacity, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char encoded[64];
    std::to_chars_result converted{encoded, std::errc::invalid_argument};
    if (!std::strcmp(format, "%d"))
        converted = std::to_chars(encoded, encoded + sizeof encoded, va_arg(arguments, int));
    else if (!std::strcmp(format, "%ld"))
        converted = std::to_chars(encoded, encoded + sizeof encoded, va_arg(arguments, long));
#if defined(__linux__)
    else if (!std::strcmp(format, "%g") && std::fegetround() == FE_TONEAREST) {
        const auto* locale = std::localeconv();
        if (locale->decimal_point[0] == '.' && locale->decimal_point[1] == 0) {
            va_list numeric;
            va_copy(numeric, arguments);
            const double value = va_arg(numeric, double);
            va_end(numeric);
            if (std::isfinite(value))
                converted = std::to_chars(encoded, encoded + sizeof encoded, value, std::chars_format::general, 6);
        }
    }
#endif
    if (converted.ec == std::errc{}) {
        const auto length = size_t(converted.ptr - encoded);
        if (capacity) {
            const auto copied = length < capacity ? length : capacity - 1;
            std::memcpy(buffer, encoded, copied);
            buffer[copied] = 0;
        }
        va_end(arguments);
        return int(length);
    }
    const int result = std::vsnprintf(buffer, capacity, format, arguments);
    va_end(arguments);
    return result;
}
#ifdef HEXACHORD_TESTING
extern "C" int hexachord_test_number(const char* text, float* value) { return number(text, *value); }
extern "C" int16_t hexachord_test_pcm(float value) { return pcm(value); }
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
