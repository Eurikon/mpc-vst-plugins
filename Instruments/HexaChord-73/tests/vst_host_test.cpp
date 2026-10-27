#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <string>
#include <vector>
#include "params.h"
#include "hexachord_params.h"

struct Effect;
using Host = intptr_t (*)(Effect*, int32_t, int32_t, intptr_t, void*, float);
struct Effect {
    int32_t magic;
    intptr_t (*dispatch)(Effect*, int32_t, int32_t, intptr_t, void*, float);
    void (*process)(Effect*, float**, float**, int32_t);
    void (*set)(Effect*, int32_t, float);
    float (*get)(Effect*, int32_t);
    int32_t programs, parameters, inputs, outputs, flags;
    intptr_t reserved1, reserved2;
    int32_t latency, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uid, version;
    void (*replace)(Effect*, float**, float**, int32_t);
    void (*doubleReplace)(Effect*, double**, double**, int32_t);
    char future[56];
};
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
    require(e->dispatch(e, 25, 0, 0, &events, 0) == 1, "VST MIDI dispatch");
}
double render(Effect* e, int frames, int silentPrefix = 0, bool accumulated = false) {
    std::vector<float> left(frames, accumulated ? .25f : 0.f), right(left);
    float* channels[] = {left.data(), right.data()};
    if (accumulated) e->process(e, nullptr, channels, frames);
    else e->replace(e, nullptr, channels, frames);
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
    require(effect->dispatch(effect, opcode, index, 0, buffer, 0) == 1, "VST parameter text dispatch");
    return buffer;
}
std::string chunk(Effect* effect) {
    void* pointer = nullptr;
    const auto bytes = effect->dispatch(effect, 23, 0, 0, &pointer, 0);
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
        effect->set(effect, item.index, 0);
        require(parameterText(effect, 7, item.index) == item.minimum, "VST physical display lower bound");
        effect->set(effect, item.index, 1);
        require(parameterText(effect, 7, item.index) == item.maximum, "VST physical display upper bound");
        effect->set(effect, item.index, item.fraction);
        const auto exact = chunk(effect);
        const auto numeric = effect->get(effect, item.index);
        require(parameterText(effect, 7, item.index) == item.formatted, "VST fractional physical display rounding");
        require(effect->get(effect, item.index) == numeric && std::fabs(numeric - item.fraction) < .000001f,
                "VST display changed normalized parameter");
        require(chunk(effect) == exact, "VST display changed exact saved state");
    }
    require(effect->dispatch(effect, 24, 0, original.size(), const_cast<char*>(original.data()), 0) == 1,
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
        for (int read = 0; read < 4; ++read)
            require(std::fabs(effect->get(effect, i) - expected) < .000001f,
                    "Continuous enum host position changed on read");
    };
    auto send = [&](Effect* effect, int i, float value) {
        effect->set(effect, i, value);
        const float clamped = std::fmax(0.f, std::fmin(1.f, value));
        raw(effect, i, clamped);
        at(effect, i, int(std::lround(double(clamped) * spans[i])));
    };
    auto pick = [&](Effect* effect, int i, int option) {
        send(effect, i, float(option) / spans[i]);
    };
    auto restore = [&](Effect* effect, const std::string& saved) {
        require(effect->dispatch(effect, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
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
                const float baseline = a->get(a, i);
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
                    send(a, i, a->get(a, i) + direction * rate);
                    int current = selected(a, i);
                    require(direction > 0 ? current >= previous : current <= previous,
                            "Readback-relative host stream reversed enum selection");
                    previous = current;
                }
                at(a, i, direction > 0 ? spans[i] : 0);
            }
        }
        pick(a, i, 1);
        float running = a->get(a, i);
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
                if (event % 7 == 0) running = a->get(a, i);
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
        send(a, i, a->get(a, i) - rates[0]);
        at(a, i, 1);

        send(a, i, fractional);
        const auto preserved = chunk(a);
        for (intptr_t size : {intptr_t(0), intptr_t(8193)}) {
            require(a->dispatch(a, 24, 0, size, const_cast<char*>(canonical.data()), 0) == 0,
                    "Malformed-size enum state accepted");
            raw(a, i, fractional);
            require(chunk(a) == preserved, "Rejected enum state mutated canonical values");
        }
        for (float invalid : {NAN, INFINITY, -INFINITY}) {
            a->set(a, i, invalid);
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
            a->set(a, index, selected);
            require(std::fabs(a->get(a, index) - selected) < .000001f, "Engine host control jumped");
            require(chunk(b) == originalB, "Engine host control leaked between instances");
        }
        a->set(a, index, i % 4 == 3 ? .5f : .3f);
    }
    const auto saved = chunk(a);
    require(b->dispatch(b, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
            "New engine VST chunk rejected");
    require(chunk(b) == saved, "Engine values did not survive VST reload");
    for (int i = 18; i < 34; ++i)
        require(std::fabs(a->get(a, i) - b->get(b, i)) < .000001f, "Restored engine control mismatch");
    std::string legacy;
    for (int count : {16, 20}) {
        require(b->dispatch(b, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
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
        require(b->dispatch(b, 24, 0, legacy.size(), legacy.data(), 0) == 1,
                "Legacy VST chunk rejected");
        const auto restoredLegacy = chunk(b);
        require(b->dispatch(b, 2, 0, 0, nullptr, 0) == 1 && chunk(b) == restoredLegacy,
                "Post-load program selection erased a migrated legacy state");
        for (int i = 16; i < count; ++i)
            require(std::fabs(a->get(a, i + 2) - b->get(b, i + 2)) < .000001f,
                    "Legacy VST chunk changed an existing engine control");
        for (int i = count - 16; i < 16; ++i)
            require(parameterText(b, 7, i + 18) == defaults[i], "Legacy VST chunk did not migrate engine defaults");
    }
    const auto migrated = chunk(b);
    for (intptr_t size : {intptr_t(saved.size() - 1), intptr_t(saved.size() - 2)}) {
        require(b->dispatch(b, 24, 0, size, const_cast<char*>(saved.data()), 0) == 0,
                "Unterminated or partial VST state chunk accepted");
        require(chunk(b) == migrated, "Rejected partial VST chunk mutated state");
    }
    auto trailing = legacy;
    trailing += " extra";
    trailing.push_back(0);
    require(b->dispatch(b, 24, 0, trailing.size(), trailing.data(), 0) == 0 && chunk(b) == migrated,
            "Embedded terminator hid trailing VST chunk data");
    require(b->dispatch(b, 24, 0, legacy.size(), nullptr, 0) == 0 && chunk(b) == migrated,
            "Null VST chunk was not rejected");
    require(a->dispatch(a, 24, 0, originalA.size(), const_cast<char*>(originalA.data()), 0) == 1 &&
                b->dispatch(b, 24, 0, originalB.size(), const_cast<char*>(originalB.data()), 0) == 1,
            "Engine fixture restore rejected");
}
void factoryDefaults(Effect* a, Effect* b) {
    const auto originalA = chunk(a), originalB = chunk(b);
    for (int model = 0; model < 4; ++model) {
        for (int index = 0; index < 34; ++index)
            if (index != 16 && index != 17) a->set(a, index, PARAMS[index].def == 1 ? 0.f : 1.f);
        a->set(a, 2, model == 3 ? 0.f : 1.f);
        const char* state = hexachord::factoryStates[model];
        require(a->dispatch(a, 24, 0, std::strlen(state) + 1, const_cast<char*>(state), 0) == 1,
                "Factory VST chunk rejected");
        for (int index = 0; index < 34; ++index) {
            const float expected = index == 2 ? model / 3.f : PARAMS[index].def;
            require(std::fabs(a->get(a, index) - expected) < .000001f,
                    "Factory VST preset did not reset every parameter and Q-Link position");
        }
        require(chunk(b) == originalB, "Factory VST preset altered another instance");
    }
    require(a->dispatch(a, 24, 0, originalA.size(), const_cast<char*>(originalA.data()), 0) == 1,
            "Factory fixture restore rejected");
}
void factoryPrograms(Effect* a, Effect* b) {
    const auto originalA = chunk(a), originalB = chunk(b);
    require(a->programs == 4 && b->programs == 4 && a->dispatch(a, 3, 0, 0, nullptr, 0) == 0,
            "Default-first factory program count or initial index changed");
    auto defaults = [&](Effect* effect, int program) {
        const int model = hexachord::factoryProgramModels[program];
        require(effect->dispatch(effect, 3, 0, 0, nullptr, 0) == program, "Factory bank/model mapping mismatch");
        for (int index = 0; index < 34; ++index)
            require(std::fabs(effect->get(effect, index) - (index == 2 ? model / 3.f : PARAMS[index].def)) < .000001f,
                    "Factory program did not restore every parameter and Q-Link position");
    };
    auto select = [&](Effect* effect, intptr_t program) {
        settingUi = true;
        const auto result = effect->dispatch(effect, 2, 0, program, nullptr, 0);
        settingUi = false;
        require(!reenteredUi, "Factory program setter re-entered the host");
        return result;
    };
    for (int program = 0; program < 4; ++program) {
        struct { char name[24]; char guard[8]; } buffer;
        std::memset(&buffer, 0x6b, sizeof buffer);
        require(a->dispatch(a, 29, program, 0, buffer.name, 0) == 1 &&
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
                    a->set(a, index, PARAMS[index].def == 1 ? 0.f : 1.f);
            if (repeat == 0) a->set(a, 2, hexachord::factoryProgramModels[program] == 3 ? 0.f : 1.f);
            else {
                const float fractional = (hexachord::factoryProgramModels[program] + .125f) / 3.f;
                a->set(a, 2, std::fmin(fractional, .99f));
                require(a->dispatch(a, 3, 0, 0, nullptr, 0) == program,
                        "Tweaked sound lost its current factory bank index");
            }
            a->set(a, 16, .1f);
            a->set(a, 17, .9f);
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
                            std::fabs(notice.value - a->get(a, notice.index)) < .000001f,
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
        require(a->dispatch(a, 29, invalid, 0, name, 0) == 0 && !name[0] && chunk(a) == valid,
                "Invalid indexed factory name accepted or changed state");
    }
    require(a->dispatch(a, 5, 0, 0, nullptr, 0) == 0 && a->dispatch(a, 29, 0, 0, nullptr, 0) == 0,
            "Null factory name buffer accepted");
    for (const char* invalid : {"INVALID", "HX73 3 0", "HX73 3 nan"}) {
        require(a->dispatch(a, 24, 0, std::strlen(invalid) + 1, const_cast<char*>(invalid), 0) == 0,
                "Malformed factory/project state accepted");
        require(chunk(a) == valid, "Malformed state changed factory parameters");
        require(select(a, 3) == 1, "Invalid chunk armed the restore guard");
        defaults(a, 3);
    }
    for (int index = 0; index < 34; ++index)
        if (index != 16 && index != 17) a->set(a, index, .25f);
    a->set(a, 2, 1);
    const auto project = chunk(a);
    require(a->dispatch(a, 24, 0, project.size(), const_cast<char*>(project.data()), 0) == 1,
            "Project state roundtrip rejected");
    require(select(a, 0) == 1 && chunk(a) == project && a->dispatch(a, 3, 0, 0, nullptr, 0) == 3,
            "Post-chunk host program selection erased restored edits or the engine");
    render(a, 0);
    require(select(a, 0) == 1 && chunk(a) == project, "Zero-frame processing cleared the restore guard");
    render(a, 128);
    require(select(a, 3) == 1, "Current program could not be reset after the first positive block");
    defaults(a, 3);
    require(b->dispatch(b, 24, 0, originalB.size(), const_cast<char*>(originalB.data()), 0) == 1,
            "Second-instance project restore rejected");
    render(a, 128);
    require(select(b, 2) == 1 && chunk(b) == originalB, "One instance cleared another instance's restore guard");
    render(b, 128);
    require(select(b, 2) == 1, "Second-instance restore guard did not clear on its own positive block");
    defaults(b, 2);
    require(a->dispatch(a, 24, 0, originalA.size(), const_cast<char*>(originalA.data()), 0) == 1 &&
                b->dispatch(b, 24, 0, originalB.size(), const_cast<char*>(originalB.data()), 0) == 1,
            "Factory program fixture restore rejected");
}
void chordPadControls(Effect* a, Effect* b) {
    constexpr double pi = 3.14159265358979323846;
    const double radii[] = {0, .26, .52, .85};
    const auto originalA = chunk(a), originalB = chunk(b);
    auto restore = [&](Effect* effect, const std::string& saved) {
        require(effect->dispatch(effect, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
                "XY fixture state restore rejected");
    };
    auto point = [&](double x, double y, bool reverse) {
        settingUi = true;
        if (reverse) {
            a->set(a, 17, float(y));
            a->set(a, 16, float(x));
        } else {
            a->set(a, 16, float(x));
            a->set(a, 17, float(y));
        }
        settingUi = false;
        require(!reenteredUi, "XY gesture re-entered its host setter");
    };
    for (int index = 0; index < 34; ++index)
        require(a->dispatch(a, 26, index, 0, nullptr, 0) == 1,
                "XY or DSP parameter lost host automation");
    for (int index : {-1, 34})
        require(a->dispatch(a, 26, index, 0, nullptr, 0) == 0, "Invalid parameter became automatable");
    require(!std::strcmp(PARAMS[16].key, "ChordPadX") && !std::strcmp(PARAMS[17].key, "ChordPadY") &&
                !PARAMS[16].nopts && !PARAMS[17].nopts && PARAMS[16].min == 0 && PARAMS[16].max == 1 &&
                PARAMS[17].min == 0 && PARAMS[17].max == 1,
            "UI aliases were not appended after the original DSP parameters");
    for (int chord = 0; chord < 8; ++chord) {
        for (int inversion = 0; inversion < 4; ++inversion) {
            for (bool reverse : {false, true}) {
                a->set(a, 0, float(chord) / 7);
                a->set(a, 1, float(inversion) / 3);
                const auto expected = chunk(a);
                const double angle = chord * pi / 4.;
                const double x = .5 + 160. / 410. * radii[inversion] * std::sin(angle);
                const double y = .5 + 160. / 410. * radii[inversion] * std::cos(angle);
                require(std::fabs(a->get(a, 16) - x) < .000001 &&
                            std::fabs(a->get(a, 17) - y) < .000001,
                        "VST direct choices did not synchronize XY handles");
                a->set(a, 0, float(inversion ? (chord + 4) % 8 : chord) / 7);
                a->set(a, 1, 0);
                const auto before = chunk(a);
                notifications.clear();
                point(x, y, reverse);
                require(chunk(a) == before && notifications.empty(),
                        "XY coordinate writes escaped before the audio-block boundary");
                render(a, 128, 0, reverse);
                require(std::fabs(a->get(a, 0) - float(chord) / 7) < .000001 &&
                            std::fabs(a->get(a, 1) - float(inversion) / 3) < .000001,
                        "VST polar XY selection is incorrect");
                require(chunk(a) == expected, "VST XY gesture changed other DSP state or chunk bytes");
                require(chunk(b) == originalB, "VST XY gesture leaked to another instance");
                for (const auto& notification : notifications)
                    require(notification.effect == a && notification.index >= 0 && notification.index < 2 &&
                                std::fabs(notification.value - a->get(a, notification.index)) < .000001,
                            "XY gesture reported a UI alias or unrelated parameter");
            }
        }
    }
    restore(a, originalA);
    render(a, 128);
    a->set(a, 0, 1.25f / 7);
    a->set(a, 1, 1.25f / 3);
    a->set(a, 2, 1.25f / 3);
    const float enginePosition = a->get(a, 2);
    notifications.clear();
    displayUpdates = 0;
    point(.5, .5 + 160. / 410. * .52, false);
    render(a, 128);
    require(a->get(a, 0) == 0 && std::fabs(a->get(a, 1) - 2.f / 3) < .000001,
            "XY gesture retained the changed chord or voicing Q-Link shadow");
    require(a->get(a, 2) == enginePosition, "XY gesture reset an unrelated continuous Q-Link position");
    require(notifications.size() == 2 && displayUpdates > 0,
            "XY gesture did not notify only its two changed DSP parameters");
    for (const auto& notification : notifications)
        require(notification.index < 2, "XY notification echoed an unrelated host parameter");
    const auto beforeInvalid = chunk(a);
    const float x = a->get(a, 16), y = a->get(a, 17);
    for (float invalid : {NAN, INFINITY, -INFINITY}) {
        point(invalid, invalid, false);
        require(chunk(a) == beforeInvalid && a->get(a, 16) == x && a->get(a, 17) == y,
                "VST invalid XY input mutated state or handles");
    }
    notifications.clear();
    a->set(a, 0, 3.f / 7);
    a->set(a, 1, 2.f / 3);
    render(a, 128);
    require(notifications.empty(), "Ordinary host enum changes were echoed as custom UI changes");
    point(.1, .1, false);
    restore(a, originalA);
    render(a, 128);
    require(chunk(a) == originalA && a->get(a, 16) == .5f && a->get(a, 17) == .5f,
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
        require(effect->dispatch(effect, 24, 0, saved.size(), const_cast<char*>(saved.data()), 0) == 1,
                "Automation fixture state restore rejected");
    };
    for (bool recordReverse : {false, true}) {
        restore(a, originalA);
        render(a, 128);
        std::vector<Sample> recorded;
        std::vector<std::string> states;
        auto edit = [&](int index, float value) {
            if (a->dispatch(a, 26, index, 0, nullptr, 0)) recorded.push_back({index, value});
            settingUi = true;
            a->set(a, index, value);
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
            require(std::fabs(a->get(a, 0) - step.chord / 7.f) < .000001f &&
                        std::fabs(a->get(a, 1) - step.voicing / 3.f) < .000001f,
                    "Recorded editor gesture mapped to the wrong musical selection");
            if (!step.xy) require(notifications.empty(), "Direct recorded choices were echoed as plugin edits");
            states.push_back(chunk(a));
        }
        require(recorded.size() == states.size() * 2, "Host excluded a diagram coordinate from the recorded track");
        const auto captured = chunk(a);
        for (bool replayReverse : {false, true}) {
            b->set(b, 16, .1f);
            b->set(b, 17, .9f);
            restore(b, originalA);
            notifications.clear();
            render(b, 128);
            require(chunk(b) == originalA && notifications.empty(), "Chunk restore retained a pending coordinate edit");
            for (size_t block = 0; block < states.size(); ++block) {
                const auto before = chunk(b);
                notifications.clear();
                for (int axis = 0; axis < 2; ++axis) {
                    const auto& sample = recorded[block * 2 + (replayReverse ? 1 - axis : axis)];
                    b->set(b, sample.index, sample.value);
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
            b->set(b, 0, 0);
            b->set(b, 1, 0);
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
    require(argc == 2, "Supply plugin library path");
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }
    auto entry = reinterpret_cast<Effect* (*)(Host)>(dlsym(library, "VSTPluginMain"));
    require(entry != nullptr, "VST entry export");
    Effect* a = entry(host);
    Effect* b = entry(host);
    require(a && b && a != b, "Two VST instances");
    require(a->magic == 0x56737450 && a->inputs == 0 && a->outputs == 2 && a->parameters == 34, "VST instrument ABI");
    require(a->uid == 0x48783733 && a->version == 1000, "Stable identity and development version");
    require(a->flags & (1 << 8), "Instrument flag");
    require(a->dispatch(a, 0, 0, 0, nullptr, 0) == 1, "VST open");
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
    a->dispatch(a, 10, 0, 0, nullptr, 44100);
    a->dispatch(a, 11, 0, 257, nullptr, 0);
    a->dispatch(a, 12, 0, 1, nullptr, 0);
    require(render(a, 257) == 0, "New VST instance should be silent");
    require(render(b, 257) == 0, "Second VST instance should be silent");
    a->set(a, 8, 0);
    a->set(a, 9, 0);
    a->set(a, 11, 0);
    a->set(a, 12, 0);
    a->set(a, 13, 0);
    a->set(a, 14, 0);
    a->set(a, 6, .8f);
    require(std::fabs(a->get(a, 6) - .8f) < .00001f, "Parameter roundtrip");
    require(std::fabs(b->get(b, 6) - .8f) > .01f, "VST instance parameter isolation");
    char display[64]{};
    a->dispatch(a, 7, 6, 0, display, 0);
    require(std::fabs(std::atof(display) - 5023.773) < 1, "Curved parameter physical display");
    void* pointer = nullptr;
    intptr_t bytes = a->dispatch(a, 23, 0, 0, &pointer, 0);
    require(pointer && bytes > 1 && bytes <= 8192, "Bounded VST chunk");
    std::string chunk(static_cast<const char*>(pointer), size_t(bytes));
    require(b->dispatch(b, 24, 0, bytes, chunk.data(), 0) == 1, "VST chunk reload");
    for (int i = 0; i < 34; ++i) require(std::fabs(a->get(a, i) - b->get(b, i)) < .00001f, "VST restored parameter mismatch");
    b->dispatch(b, 24, 0, 8, const_cast<char*>("INVALID"), 0);
    require(std::fabs(b->get(b, 6) - .8f) < .00001f, "Invalid VST state mutated parameters");
    require(b->dispatch(b, 24, 0, 8193, chunk.data(), 0) == 0, "Oversized VST chunk accepted");
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
    a->dispatch(a, 12, 0, 0, nullptr, 0);
    a->dispatch(a, 1, 0, 0, nullptr, 0);
    b->dispatch(b, 1, 0, 0, nullptr, 0);
    dlclose(library);
    std::puts("Native VST ABI, sample timing, automation, continuous enum host contracts, state, isolation and host-block tests passed.");
}
