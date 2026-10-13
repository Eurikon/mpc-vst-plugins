extern "C" {
#include "engine.h"
}
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <cstring>

int main() {
    const auto* api = mpc_engine();
    void* a = api->create(nullptr);
    void* b = api->create(nullptr);
    assert(a && b && a != b);
    int16_t audio[256];
    char state[2048], value[64];
    api->set_param(a, "cutoff", "0.73");
    api->get_param(b, "cutoff", value, sizeof value);
    assert(std::fabs(std::atof(value) - 0.5) < 1e-6);
    assert(api->get_param(a, "state", state, sizeof state) > 0);
    api->set_param(b, "state", state);
    api->get_param(b, "cutoff", value, sizeof value);
    assert(std::fabs(std::atof(value) - 0.73) < 1e-6);
    api->set_param(a, "theme", "4");
    assert(api->get_param(a, "state", state, sizeof state) > 0);
    api->set_param(b, "state", state);
    api->get_param(b, "theme", value, sizeof value);assert(std::atoi(value) == 4);
    std::string legacy = state;
    legacy.erase(legacy.find_last_of(' '));legacy.erase(legacy.find_last_of(' '));
    api->set_param(b, "state", legacy.c_str());
    api->get_param(b, "theme", value, sizeof value);assert(std::atoi(value) == 0);
    api->get_param(b, "cutoff", value, sizeof value);assert(std::fabs(std::atof(value) - 0.73) < 1e-6);
    void* plain = api->create("/tmp");void* themed = api->create("/tmp");assert(plain && themed);
    api->set_param(themed, "theme", "5");
    uint8_t themeNote[] = {0x90, 48, 110};api->midi(plain, themeNote, 3);api->midi(themed, themeNote, 3);
    int16_t plainAudio[256], themedAudio[256];
    for (int block = 0; block < 100; ++block) {
        api->render(plain, plainAudio, 128);api->render(themed, themedAudio, 128);
        assert(!std::memcmp(plainAudio, themedAudio, sizeof plainAudio));
    }
    api->destroy(plain);api->destroy(themed);
    api->set_param(b, "state", "MS1 0.1 nan");
    api->get_param(b, "cutoff", value, sizeof value);
    assert(std::fabs(std::atof(value) - 0.73) < 1e-6);
    api->set_param(b, "cutoff", "nan");
    api->get_param(b, "cutoff", value, sizeof value);
    assert(std::fabs(std::atof(value) - 0.73) < 1e-6);
    api->set_param(b, "cutoff", "4");
    api->get_param(b, "cutoff", value, sizeof value);
    assert(std::atof(value) == 1.0);
    assert(api->get_param(a, "state", value, 4) == 0);
    uint8_t on[] = {0x90, 48, 110}, off[] = {0x80, 48, 0};
    api->midi(a, on, 3);
    long long energy = 0;
    for (int block = 0; block < 100; ++block) {
        api->render(a, audio, 128);
        for (int i = 0; i < 128; ++i) {
            assert(audio[2*i] == audio[2*i+1]);
            energy += static_cast<long long>(audio[2*i]) * audio[2*i];
        }
    }
    assert(energy > 100000);
    api->midi(a, off, 3);
    for (int block = 0; block < 1000; ++block) api->render(a, audio, 128);
    for (int i = 0; i < 256; ++i) assert(std::abs(int(audio[i])) <= 1);
    api->midi(a, on, 3);
    api->render(a, audio, 128);
    uint8_t panic[] = {0xb0, 120, 0};
    api->midi(a, panic, 3);
    api->render(a, audio, 128);
    for (int i = 0; i < 256; ++i) assert(audio[i] == 0);
    api->set_param(a, "level", "0");
    api->midi(a, on, 3);
    api->render(a, audio, 17);
    for (int i = 0; i < 34; ++i) assert(audio[i] == 0);
    for (int frames = 1; frames <= 128; ++frames) {
        for (auto& sample : audio) sample = 12345;
        api->render(a, audio, frames);
        for (int i = 0; i < 2 * frames; ++i) assert(audio[i] == 0);
        for (int i = 2 * frames; i < 256; ++i) assert(audio[i] == 12345);
    }
    api->set_param(a, "level", "0.75");
    uint8_t second[] = {0x91, 55, 127};
    api->midi(a, second, 3);
    api->midi(a, off, 3);
    energy = 0;
    for (int block = 0; block < 100; ++block) {
        api->render(a, audio, 128);
        for (int i = 0; i < 256; ++i) energy += static_cast<long long>(audio[i]) * audio[i];
    }
    assert(energy > 100000);
    uint8_t allOff[] = {0xb1, 123, 0};
    api->midi(a, allOff, 3);
    for (int block = 0; block < 1000; ++block) api->render(a, audio, 128);
    for (int i = 0; i < 256; ++i) assert(std::abs(int(audio[i])) <= 1);
    api->midi(a, on, 1);
    api->midi(a, nullptr, 3);
    api->destroy(a); api->destroy(b);
    std::puts("Engine audio, release, state, validation and instance tests passed.");
}
