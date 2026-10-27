#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace hexachord::native {

class Dsp {
  public:
    static constexpr std::uint32_t capacity = 4096;
    static constexpr int parameterCount = 32;

    Dsp();
    ~Dsp();
    Dsp(const Dsp &) = delete;
    Dsp &operator=(const Dsp &) = delete;

    void init(float sampleRate);
    void setParam(int id, float value);
    float getParam(int id) const;
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void allNotesOff();
    void allSoundOff();
    std::uint32_t render(std::uint32_t frames);
    const float *output(unsigned channel) const;
    const float *telemetry() const;
    const void *storageAddress() const;
    std::size_t storageBytes() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace hexachord::native
