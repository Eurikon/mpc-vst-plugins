extern "C" {
#include "engine.h"
void hexachord_test_force_trap(void*);
uint64_t hexachord_test_memory_bytes(void*);
void* hexachord_test_memory_address(void*);
unsigned hexachord_test_parameter_snapshots(void*);
float hexachord_test_applied_param(void*, int);
}
#include "hexachord_params.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <thread>

namespace {
const mpc_engine_t* api = nullptr;
unsigned assertions = 0;
void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
float value(void* engine, const char* key) {
    char text[64];
    require(api->get_param(engine, key, text, sizeof text) > 0, key);
    return std::strtof(text, nullptr);
}
std::string text(void* engine, const char* key) {
    char buffer[64]{};
    require(api->get_param(engine, key, buffer, sizeof buffer) > 0, key);
    return buffer;
}
void message(void* engine, int status, int key, int velocity = 0) {
    const uint8_t bytes[] = {uint8_t(status), uint8_t(key), uint8_t(velocity)};
    api->midi(engine, bytes, sizeof bytes);
}
uint64_t energy(void* engine, int blocks = 20) {
    int16_t samples[256];
    uint64_t result = 0;
    for (int n = 0; n < blocks; ++n) {
        api->render(engine, samples, 128);
        for (int16_t sample : samples) result += int64_t(sample) * sample;
    }
    return result;
}
void silent(void* engine, int blocks = 1) { require(energy(engine, blocks) == 0, "Expected silence"); }
void fast(void* engine) {
    api->set_param(engine, "Attack", "0");
    api->set_param(engine, "Decay", "0");
    api->set_param(engine, "Release", "0");
    api->set_param(engine, "TapeEcho", "0");
    api->set_param(engine, "ShimmerVerb", "0");
    api->set_param(engine, "ChorusMix", "0");
    api->set_param(engine, "TapeWow", "0");
    api->set_param(engine, "OutputGain", "-9");
    api->set_param(engine, "Cutoff", "0.65");
}
std::string state(void* engine) {
    char text[8192];
    require(api->get_param(engine, "state", text, sizeof text) > 0, "State serialization");
    return text;
}
float physical(int index, float input) {
    const auto& parameter = hexachord::parameters[index];
    if (parameter.curve == hexachord::Curve::Exponential)
        return parameter.minimum * std::pow(parameter.maximum / parameter.minimum, input);
    if (parameter.curve == hexachord::Curve::Squared)
        return parameter.minimum + input * input * (parameter.maximum - parameter.minimum);
    return input;
}
void appliedParameters(void* engine) {
    for (int i = 0; i < 32; ++i)
        require(hexachord_test_applied_param(engine, i) == physical(i, value(engine, hexachord::parameters[i].key)),
                "Published parameter did not reach native DSP");
}
void parameterRevision(void* engine, void* other) {
    const auto original = state(engine);
    const unsigned initial = hexachord_test_parameter_snapshots(engine);
    require(initial == 1, "Initial parameters were not applied once");
    energy(engine, 40);
    message(engine, 0x80, 48);
    require(hexachord_test_parameter_snapshots(engine) == initial, "Unchanged parameters repeated full snapshots");
    api->set_param(engine, "Cutoff", ".37");
    energy(engine, 1);
    require(hexachord_test_parameter_snapshots(engine) == initial + 1, "Scalar revision was not applied once");
    appliedParameters(engine);
    energy(engine, 20);
    require(hexachord_test_parameter_snapshots(engine) == initial + 1, "Applied scalar repeated snapshots");
    api->set_param(engine, "Cutoff", "nan");
    energy(engine, 1);
    require(hexachord_test_parameter_snapshots(engine) == initial + 1, "Invalid scalar published a revision");
    api->set_param(engine, "Attack", ".5");
    message(engine, 0x80, 48);
    require(hexachord_test_parameter_snapshots(engine) == initial + 2, "MIDI did not apply pending parameters");
    appliedParameters(engine);
    api->set_param(engine, "state", original.c_str());
    energy(engine, 1);
    require(hexachord_test_parameter_snapshots(engine) == initial + 3, "State restoration missed its revision");
    appliedParameters(engine);
    require(hexachord_test_parameter_snapshots(other) == 1, "Parameter cache leaked between instances");
}
void displays(void* engine) {
    const auto original = state(engine);
    struct Expected { const char* key; const char* initial; const char* minimum; const char* maximum;
                      const char* fraction; const char* formatted; };
    const Expected expected[] = {
        {"Cutoff", "1400", "20", "20000", "0.8", "5024"},
        {"Attack", "350", "10", "4000", "0.5", "1008"},
        {"Decay", "1200", "50", "5000", "0.25", "359"},
        {"Release", "2200", "50", "6000", "0.37", "865"},
    };
    for (const auto& item : expected) {
        const auto key = std::string(item.key) + "_display";
        require(text(engine, key.c_str()) == item.initial, "Default physical display contains float noise");
        api->set_param(engine, item.key, "0");
        require(text(engine, key.c_str()) == item.minimum, "Physical display lower bound");
        api->set_param(engine, item.key, "1");
        require(text(engine, key.c_str()) == item.maximum, "Physical display upper bound");
        api->set_param(engine, item.key, item.fraction);
        const auto exact = state(engine);
        require(text(engine, key.c_str()) == item.formatted, "Fractional physical display rounding");
        require(value(engine, item.key) == std::strtof(item.fraction, nullptr), "Display changed raw parameter precision");
        require(state(engine) == exact, "Display changed exact saved state");
        char shortBuffer[2] = {'x', 'x'};
        require(api->get_param(engine, key.c_str(), shortBuffer, sizeof shortBuffer) == 0 && !shortBuffer[0],
                "Truncated physical display accepted");
        char invalidBuffer[2] = {'x', 'x'};
        require(api->get_param(engine, key.c_str(), invalidBuffer, 0) == 0 && invalidBuffer[0] == 'x',
                "Zero-length display wrote a buffer");
    }
    api->set_param(engine, "state", original.c_str());
    require(state(engine) == original, "Display checks altered restored parameters");
}
void engineState(void* a, void* b) {
    const auto originalA = state(a), originalB = state(b);
    const char* keys[] = {"FeltHammer", "FeltTine", "FeltBark", "FeltDamping",
                         "AnaDetune", "AnaDrift", "AnaFilterEnv", "AnaWave",
                         "DcoPulseWidth", "DcoPwmDepth", "DcoPwmRate", "DcoWave",
                         "PrismColour", "PrismPartial", "PrismStrike", "PrismRing"};
    const float defaults[] = {100, 100, 140, 1, 100, 100, 100, 2, 34, 16, 100, 2, 100, 100, 100, 1};
    const float minima[] = {0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 25, 0};
    const float maxima[] = {150, 150, 200, 2, 200, 200, 200, 2, 90, 40, 300, 2, 150, 150, 200, 2};
    for (int i = 0; i < 16; ++i) {
        require(value(a, keys[i]) == defaults[i], "Engine default changed the existing sound");
        api->set_param(a, keys[i], "999");
        require(value(a, keys[i]) == maxima[i], "Engine upper bound was not clamped");
        api->set_param(a, keys[i], "-10");
        require(value(a, keys[i]) == minima[i], "Engine lower bound was not clamped");
        for (const char* invalid : {"nan", "inf", "-inf", "1tail"}) {
            const auto saved = state(a);
            api->set_param(a, keys[i], invalid);
            require(state(a) == saved, "Invalid engine scalar mutated saved state");
        }
    }
    for (const char* key : {"FeltDamping", "AnaWave", "DcoWave", "PrismRing"}) {
        api->set_param(a, key, "1.6");
        require(value(a, key) == 2, "Engine switch was not stepped");
    }
    for (int i = 0; i < 32; ++i) {
        const auto& p = hexachord::parameters[i];
        float selected = p.curve == hexachord::Curve::Linear ?
            p.minimum + (p.maximum - p.minimum) * .37f : .37f;
        if (p.stepped) selected = std::round(selected);
        api->set_param(a, p.key, std::to_string(selected).c_str());
    }
    const auto saved = state(a);
    require(saved.rfind("HX73 3 ", 0) == 0, "New state did not use version three");
    api->set_param(b, "state", saved.c_str());
    require(state(b) == saved, "Thirty-two-field state did not reload exactly");
    for (const auto& p : hexachord::parameters)
        require(value(a, p.key) == value(b, p.key), "Thirty-two-field roundtrip lost a parameter");
    std::istringstream input(saved.substr(7));
    std::string fields[32], extra;
    for (auto& field : fields) require(bool(input >> field), "Missing saved parameter field");
    require(!(input >> extra), "Unexpected saved parameter field");
    auto chunk = [&](int version, int count) {
        std::string result = "HX73 " + std::to_string(version);
        for (int i = 0; i < count; ++i) result += " " + fields[i];
        return result;
    };
    for (int count : {16, 20}) {
        api->set_param(b, "state", saved.c_str());
        const auto legacy = chunk(count == 16 ? 1 : 2, count);
        api->set_param(b, "state", legacy.c_str());
        for (int i = 0; i < count; ++i)
            require(value(a, hexachord::parameters[i].key) == value(b, hexachord::parameters[i].key),
                    "Legacy migration changed an original parameter");
        for (int i = count; i < 32; ++i)
            require(value(b, keys[i - 16]) == defaults[i - 16], "Legacy migration did not restore appended defaults");
    }
    const auto migrated = state(b);
    for (const auto& invalid : {chunk(1, 15), chunk(1, 17), chunk(2, 16), chunk(2, 19), chunk(2, 21),
                               saved + " 0", chunk(3, 20), chunk(3, 31), chunk(4, 32),
                               chunk(3, 31) + " nan", chunk(3, 31) + " 1.4",
                               chunk(3, 31) + " 1e", chunk(3, 31) + " 3"}) {
        api->set_param(b, "state", invalid.c_str());
        require(state(b) == migrated, "Malformed state partially changed migrated values");
    }
    api->set_param(a, "state", originalA.c_str());
    api->set_param(b, "state", originalB.c_str());
}
void factoryDefaults(void* a, void* b) {
    const auto originalA = state(a), originalB = state(b);
    for (int model = 0; model < 4; ++model) {
        for (const auto& parameter : hexachord::parameters) {
            const float changed = parameter.curve != hexachord::Curve::Linear ? 1.f :
                parameter.initial == parameter.maximum ? parameter.minimum : parameter.maximum;
            api->set_param(a, parameter.key, std::to_string(changed).c_str());
        }
        api->set_param(a, "SynthModel", model == 3 ? "0" : "3");
        api->set_param(a, "state", hexachord::factoryStates[model]);
        std::istringstream fields(std::string(hexachord::factoryStates[model]).substr(7));
        for (int index = 0; index < 32; ++index) {
            float expected;
            require(bool(fields >> expected), "Factory chunk lost a parameter");
            require(value(a, hexachord::parameters[index].key) == expected,
                    "Factory preset did not restore every native parameter");
        }
        require(value(a, "SynthModel") == model, "Factory preset did not select its engine");
        require(state(b) == originalB, "Factory preset reset another instance");
    }
    api->set_param(a, "state", originalA.c_str());
}
void padPoint(void* engine, double x, double y, bool reverse = false) {
    char horizontal[32], vertical[32];
    std::snprintf(horizontal, sizeof horizontal, "%.9g", x);
    std::snprintf(vertical, sizeof vertical, "%.9g", y);
    if (reverse) {
        api->set_param(engine, "ChordPadY", vertical);
        api->set_param(engine, "ChordPadX", horizontal);
    } else {
        api->set_param(engine, "ChordPadX", horizontal);
        api->set_param(engine, "ChordPadY", vertical);
    }
}
void padPolar(void* engine, double angle, double radius, bool reverse = false) {
    constexpr double pi = 3.14159265358979323846;
    padPoint(engine, .5 + 160. / 410. * radius * std::sin(angle * pi / 180.),
             .5 + 160. / 410. * radius * std::cos(angle * pi / 180.), reverse);
}
void chordPad(void* a, void* b) {
    constexpr double pi = 3.14159265358979323846;
    const double radii[] = {0, .26, .52, .85};
    const auto original = state(a), other = state(b);
    for (int chord = 0; chord < 8; ++chord) {
        for (int inversion = 0; inversion < 4; ++inversion) {
            for (bool reverse : {false, true}) {
                api->set_param(a, "ChordType", std::to_string(chord).c_str());
                api->set_param(a, "Inversion", std::to_string(inversion).c_str());
                const auto expected = state(a);
                const double angle = chord * pi / 4.;
                const double x = .5 + 160. / 410. * radii[inversion] * std::sin(angle);
                const double y = .5 + 160. / 410. * radii[inversion] * std::cos(angle);
                require(std::fabs(value(a, "ChordPadX") - x) < .000001 &&
                            std::fabs(value(a, "ChordPadY") - y) < .000001,
                        "Direct choices did not synchronize XY coordinates");
                api->set_param(a, "ChordType", std::to_string(inversion ? (chord + 4) % 8 : chord).c_str());
                api->set_param(a, "Inversion", "0");
                const auto before = state(a);
                padPoint(a, x, y, reverse);
                require(state(a) == before, "One-axis writes escaped before pair commit");
                require(value(a, "ChordPadPending") == 1, "XY pair was not staged");
                api->set_param(a, "ChordPadApply", "1");
                require(value(a, "ChordType") == chord && value(a, "Inversion") == inversion,
                        "Polar XY chord or voicing selection is incorrect");
                require(state(a) == expected, "XY gesture altered non-chord DSP state or chunk format");
                require(value(a, "ChordPadPending") == 0 &&
                            std::fabs(value(a, "ChordPadX") - x) < .000001 &&
                            std::fabs(value(a, "ChordPadY") - y) < .000001,
                        "XY pair did not retain its raw touch position");
                require(state(b) == other, "XY gesture leaked to another engine");
            }
        }
    }
    struct Boundary { double radius; int inversion; };
    for (const auto& boundary : {Boundary{.1299, 0}, {.1301, 1}, {.3899, 1},
                                 {.3901, 2}, {.6799, 2}, {.6801, 3}}) {
        api->set_param(a, "ChordType", "0");
        api->set_param(a, "Inversion", "0");
        padPolar(a, 0, boundary.radius);
        api->set_param(a, "ChordPadApply", "1");
        require(value(a, "Inversion") == boundary.inversion, "XY radial band boundary changed");
    }
    api->set_param(a, "ChordType", "5");
    api->set_param(a, "Inversion", "2");
    padPoint(a, .51, .49);
    api->set_param(a, "ChordPadApply", "1");
    require(value(a, "ChordType") == 5 && value(a, "Inversion") == 0,
            "Center touch changed the retained chord");
    api->set_param(a, "ChordType", "0");
    api->set_param(a, "Inversion", "0");
    for (const auto& position : {Boundary{25, 0}, {30, 1}, {20, 1}, {16, 0}}) {
        padPolar(a, position.radius, .52);
        api->set_param(a, "ChordPadApply", "1");
        require(value(a, "ChordType") == position.inversion, "XY angular hysteresis changed");
    }
    const auto unchanged = state(a);
    const float oldX = value(a, "ChordPadX"), oldY = value(a, "ChordPadY");
    for (const char* invalid : {"nan", "inf", "-inf", "", "0.1garbage"}) {
        api->set_param(a, "ChordPadX", invalid);
        api->set_param(a, "ChordPadY", invalid);
        require(state(a) == unchanged && value(a, "ChordPadPending") == 0 &&
                    value(a, "ChordPadX") == oldX && value(a, "ChordPadY") == oldY,
                "Invalid XY input changed state or pending coordinates");
    }
    padPoint(a, -10, 10);
    require(value(a, "ChordPadX") == 0 && value(a, "ChordPadY") == 1, "XY coordinates were not clamped");
    api->set_param(a, "state", original.c_str());
    require(state(a) == original && value(a, "ChordPadPending") == 0 &&
                value(a, "ChordPadX") == .5f && value(a, "ChordPadY") == .5f,
            "Chunk restore retained stale XY coordinates or a pending gesture");
}
}

int main() {
    api = mpc_engine();
    require(api && api->create && api->render && !api->process, "Instrument engine API");
    void* a = api->create(nullptr);
    void* b = api->create(nullptr);
    require(a && b && a != b, "Independent engine creation");
    uint64_t initialMemory = hexachord_test_memory_bytes(a);
    const auto* storageA = hexachord_test_memory_address(a);
    const auto* storageB = hexachord_test_memory_address(b);
    require(initialMemory > 0 && initialMemory <= 1310720, "Native storage exceeds the prior fixed memory budget");
    require(initialMemory == hexachord_test_memory_bytes(b), "Native storage size varies between instances");
    require(storageA && storageB && storageA != storageB, "Native storage is not independently owned");
    silent(a);
    silent(b);
    parameterRevision(a, b);
    displays(a);
    engineState(a, b);
    factoryDefaults(a, b);
    chordPad(a, b);
    require(std::fabs(value(a, "Cutoff_display") - 1400.f) < 0.01f, "Cutoff default curve");
    require(std::fabs(value(a, "Attack_display") - 350.f) < 0.01f, "Attack default curve");
    api->set_param(a, "Cutoff", "0.8");
    require(value(a, "Cutoff_display") == 5024, "Rounded exponential cutoff display");
    require(std::fabs(value(b, "Cutoff_display") - 1400.f) < 0.01f, "Parameter isolation");
    api->set_param(a, "Attack", "0.5");
    require(value(a, "Attack_display") == 1008, "Rounded squared time display");
    api->set_param(a, "Inversion", "2.4");
    require(value(a, "Inversion") == 2, "Stepped parameter rounding");
    api->set_param(a, "SubOsc", "999");
    require(value(a, "SubOsc") == 100, "Parameter upper clamp");
    api->set_param(a, "SubOsc", "-100");
    require(value(a, "SubOsc") == 0, "Parameter lower clamp");
    std::string saved = state(a);
    api->set_param(b, "state", saved.c_str());
    require(state(b) == saved, "State reload exact values");
    for (const char* bad : {"", "HX73 2 0", "HX73 1 nan", "HX73 1 0.5", "HX73 1 99"}) {
        api->set_param(b, "state", bad);
        require(state(b) == saved, "Invalid state changed values");
    }
    api->set_param(b, "state", (saved + " extra").c_str());
    require(state(b) == saved, "Trailing state token accepted");
    api->set_param(b, "state", std::string(9000, 'x').c_str());
    require(state(b) == saved, "Oversized state accepted");
    api->set_param(b, "Cutoff", "nan");
    api->set_param(b, "Cutoff", "inf");
    api->set_param(b, "Cutoff", "0.1garbage");
    require(state(b) == saved, "Invalid scalar changed values");
    char shortBuffer[4];
    require(api->get_param(a, "state", shortBuffer, sizeof shortBuffer) == 0 && !shortBuffer[0], "Truncated state accepted");
    require(api->get_param(a, "unknown", shortBuffer, sizeof shortBuffer) == 0, "Unknown parameter accepted");
    fast(a);
    fast(b);
    message(a, 0x90, 48, 100);
    require(energy(a) > 1000000, "MIDI produces audio");
    silent(b);
    require(value(a, "voices") >= 4, "Actual voice telemetry");
    message(a, 0x90, 48, 100);
    energy(a, 1);
    require(value(a, "voices") == 4, "Repeated pitch should retrigger one group");
    message(a, 0x90, 48, 0);
    energy(a, 200);
    silent(a);
    require(value(a, "voices") == 0, "Zero-velocity note releases voices");
    message(a, 0xb0, 64, 127);
    message(a, 0x90, 52, 110);
    energy(a, 10);
    message(a, 0x80, 52);
    require(energy(a, 100) > 1000000, "Sustain pedal holds sound");
    message(a, 0xb0, 64, 0);
    energy(a, 200);
    silent(a);
    message(a, 0x90, 48, 100);
    message(a, 0x91, 48, 100);
    message(a, 0x80, 48);
    require(energy(a) > 1000000, "Other MIDI channel retains same pitch");
    message(a, 0x81, 48);
    energy(a, 200);
    silent(a);
    message(a, 0x90, 60, 120);
    energy(a);
    message(a, 0xb0, 123);
    energy(a, 200);
    silent(a);
    for (int model = 0; model < 4; ++model) {
        api->set_param(a, "SynthModel", std::to_string(model).c_str());
        for (int chord = 0; chord < 8; ++chord) {
            api->set_param(a, "ChordType", std::to_string(chord).c_str());
            message(a, 0x90, 48, 110);
            require(energy(a, 10) > 0, "Silent model or chord");
            message(a, 0xb0, 120);
            silent(a);
        }
    }
    int16_t samples[256];
    for (int frames = 1; frames <= 128; ++frames) {
        for (auto& sample : samples) sample = 12345;
        api->render(a, samples, frames);
        for (int i = 0; i < 2 * frames; ++i) require(samples[i] == 0, "Partial render not silent after panic");
        for (int i = 2 * frames; i < 256; ++i) require(samples[i] == 12345, "Partial render overwrote buffer tail");
    }
    api->midi(a, nullptr, 3);
    const uint8_t invalid[] = {0x90, 255, 127};
    api->midi(a, invalid, 3);
    api->midi(a, invalid, 1);
    silent(a);
    std::string firstState = state(a), secondState = state(b);
    api->set_param(a, "state", firstState.c_str());
    std::atomic<bool> failed{false};
    std::thread writer([&] {
        for (int i = 0; i < 3000; ++i) {
            api->set_param(a, "state", i & 1 ? firstState.c_str() : secondState.c_str());
        }
    });
    for (int i = 0; i < 1000; ++i) {
        api->render(a, samples, 128);
        if (value(a, "fault") != 0) failed = true;
        char serialized[8192];
        if (api->get_param(a, "state", serialized, sizeof serialized) > 0 &&
            firstState != serialized && secondState != serialized) failed = true;
    }
    writer.join();
    require(!failed, "Concurrent state publication was torn or caused fault");
    api->render(a, samples, 128);
    appliedParameters(a);
    require(hexachord_test_memory_bytes(a) == initialMemory && hexachord_test_memory_address(a) == storageA &&
                hexachord_test_memory_address(b) == storageB,
            "Native storage moved or grew during MIDI, reset or rendering");
    message(a, 0x90, 48, 120);
    energy(a, 1);
    hexachord_test_force_trap(a);
    silent(a);
    require(value(a, "fault") == 1, "Injected native failure should latch fault");
    silent(a, 10);
    require(value(b, "fault") == 0, "Native failure contaminated another instance");
    api->destroy(a);
    api->destroy(b);
    api->destroy(nullptr);
    std::printf("Native engine audio, MIDI, curves, state, concurrency, memory and fault tests passed (%u assertions).\n", assertions);
}
