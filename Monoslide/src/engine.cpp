#include "dsp/SynthDSP.hpp"
#include "params.h"
extern "C" {
#include "engine.h"
}
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <new>

static_assert(std::atomic<float>::is_always_lock_free, "Audio parameters require lock-free atomics");

namespace {
enum { Cutoff, EnvMod, Decay, Drive, Accent, Resonance, Fine, Level, Waveform,
       SlideTime, VcfRange, DecayRange, Sustain, Release, Blend, BassComp, Legato, AccentThreshold };
struct Engine {
    SynthVoice voice;
    std::atomic<float> values[NPARAMS];
    uint64_t held[16][128] = {};
    uint8_t velocity[16][128] = {};
    uint64_t order = 0;
    int note = 60, channel = 0;
    bool gate = false, slide = false;
    Engine() {
        for (int i = 0; i < NPARAMS; ++i) {
            const auto& p = PARAMS[i];
            values[i].store(p.nopts ? p.def * (p.nopts - 1) : p.min + p.def * (p.max - p.min));
        }
        voice.setSampleRate(44100.0);
    }
};
float maximum(int i) { return PARAMS[i].nopts ? float(PARAMS[i].nopts - 1) : PARAMS[i].max; }
float minimum(int i) { return PARAMS[i].nopts ? 0.f : PARAMS[i].min; }
int indexOf(const char* key) {
    for (int i = 0; i < NPARAMS; ++i) if (!std::strcmp(key, PARAMS[i].key)) return i;
    return -1;
}
bool number(const char* text, float& value) {
    char* end;
    errno = 0;
    value = std::strtof(text, &end);
    return end != text && *end == 0 && errno != ERANGE && std::isfinite(value);
}
void* create(const char*) {
    try { return new Engine; } catch (...) { return nullptr; }
}
void destroy(void* ptr) { delete static_cast<Engine*>(ptr); }
void selectNote(Engine& e) {
    uint64_t newest = 0;
    for (int c = 0; c < 16; ++c)
        for (int n = 0; n < 128; ++n)
            if (e.held[c][n] > newest) { newest = e.held[c][n]; e.note = n; e.channel = c; }
    e.gate = newest != 0;
    if (!e.gate) e.slide = false;
}
void midi(void* ptr, const uint8_t* msg, int len) {
    if (!ptr || !msg || len < 3 || msg[1] > 127 || msg[2] > 127) return;
    auto& e = *static_cast<Engine*>(ptr);
    int type = msg[0] & 0xf0, c = msg[0] & 15, n = msg[1];
    if (type == 0x90 && msg[2]) {
        e.slide = e.gate && e.values[Legato].load() > 0.5f;
        if (!e.slide) e.voice.prevGate = false;
        e.held[c][n] = ++e.order;
        e.velocity[c][n] = msg[2];
        e.note = n; e.channel = c; e.gate = true;
    } else if (type == 0x80 || (type == 0x90 && !msg[2])) {
        e.held[c][n] = 0;
        selectNote(e);
    } else if (type == 0xb0 && (n == 120 || n == 123)) {
        std::memset(e.held[c], 0, sizeof e.held[c]);
        selectNote(e);
        if (n == 120 && !e.gate) e.voice.reset();
    }
}
void setParam(void* ptr, const char* key, const char* text) {
    if (!ptr || !key || !text) return;
    auto& e = *static_cast<Engine*>(ptr);
    if (!std::strcmp(key, "state")) {
        if (std::strncmp(text, "MS1 ", 4)) return;
        float staged[NPARAMS];
        const char* cursor = text + 4;
        for (int i = 0; i < NPARAMS; ++i) {
            while (*cursor == ' ' || *cursor == '\n' || *cursor == '\t') ++cursor;
            if (!*cursor && i == AccentThreshold + 1) {
                for (int j = i; j < NPARAMS; ++j) {
                    const auto& p = PARAMS[j];
                    staged[j] = p.nopts ? p.def * (p.nopts - 1) : p.min + p.def * (p.max - p.min);
                }
                break;
            }
            char* end;
            errno = 0;
            staged[i] = std::strtof(cursor, &end);
            if (end == cursor || errno == ERANGE || !std::isfinite(staged[i]) ||
                staged[i] < minimum(i) || staged[i] > maximum(i)) return;
            if (*end && *end != ' ' && *end != '\n' && *end != '\t') return;
            if ((PARAMS[i].nopts || PARAMS[i].int_display) && staged[i] != std::round(staged[i])) return;
            cursor = end;
        }
        while (*cursor == ' ' || *cursor == '\n' || *cursor == '\t') ++cursor;
        if (*cursor) return;
        for (int i = 0; i < NPARAMS; ++i) e.values[i].store(staged[i]);
        return;
    }
    int i = indexOf(key);
    float value;
    if (i < 0 || !number(text, value)) return;
    value = clamp(value, minimum(i), maximum(i));
    if (PARAMS[i].nopts || PARAMS[i].int_display) value = std::round(value);
    e.values[i].store(value);
    if (i == Waveform) e.values[Blend].store(value);
}
int getParam(void* ptr, const char* key, char* buf, int length) {
    if (!ptr || !key || !buf || length <= 0) return 0;
    auto& e = *static_cast<Engine*>(ptr);
    if (!std::strcmp(key, "state")) {
        int used = std::snprintf(buf, length, "MS1");
        if (used < 0 || used >= length) return 0;
        for (int i = 0; i < NPARAMS; ++i) {
            int written = std::snprintf(buf + used, length - used, " %.9g", e.values[i].load());
            if (written < 0 || written >= length - used) { buf[0] = 0; return 0; }
            used += written;
        }
        return used;
    }
    int i = indexOf(key);
    if (i < 0) return 0;
    int written = std::snprintf(buf, length, "%.9g", e.values[i].load());
    return written >= 0 && written < length ? written : 0;
}
void render(void* ptr, int16_t* out, int frames) {
    if (!ptr || !out || frames <= 0) return;
    auto& e = *static_cast<Engine*>(ptr);
    float p[NPARAMS];
    for (int i = 0; i < NPARAMS; ++i) p[i] = e.values[i].load();
    bool accented = e.velocity[e.channel][e.note] >= p[AccentThreshold];
    for (int i = 0; i < frames; ++i) {
        float sample = e.voice.process((e.note - 60) / 12.f, e.gate, accented, e.slide,
            p[Cutoff], 0.f, p[Resonance], p[EnvMod], p[Decay], p[Accent], p[Drive], p[Fine],
            p[Blend], p[SlideTime], p[VcfRange], p[DecayRange], p[Sustain], p[Release], p[BassComp]);
        float output = sample * std::min(p[Level], 0.75f) / 0.75f;
        if (p[Level] > 0.75f) {
            float drive = 1.f + (p[Level] - 0.75f) * 12.f;
            output = 5.f * std::tanh(output / 5.f * drive) / std::tanh(drive);
        }
        // Rack audio voltages become normalized host audio at the engine boundary.
        float normalized = std::isfinite(output) ? clamp(output / 5.f, -1.f, 1.f) : 0.f;
        out[2*i] = out[2*i+1] = static_cast<int16_t>(std::lrint(normalized * 32767.f));
    }
}
const mpc_engine_t api = {create, destroy, midi, setParam, getParam, render, nullptr};
}
extern "C" const mpc_engine_t* mpc_engine() { return &api; }
