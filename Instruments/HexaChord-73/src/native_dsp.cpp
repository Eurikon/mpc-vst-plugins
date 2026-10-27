#include "native_dsp.hpp"
#include "hexachord_params.h"
#include "native_envelope.hpp"
#include "native_filters.hpp"
#include "native_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace hexachord::native {
namespace {
constexpr int MAX_VOICES = 16;
constexpr int CHORD_NOTES = 4;
constexpr float DEFAULT_TRANSPOSE_SEMITONES = -12.0f;
constexpr int CHORUS_BUF_SIZE = 4096;
constexpr int CHORUS_MASK = CHORUS_BUF_SIZE - 1;
constexpr int DELAY_BUF_SIZE = 65536;
constexpr int DELAY_MASK = DELAY_BUF_SIZE - 1;
constexpr int PROCESS_CAPACITY = 128;
constexpr int FILTER_COEFF_STRIDE = 7;
constexpr std::array<int, 12> engineControlIds{16, 17, 18, 20, 21, 22, 24, 25, 26, 28, 29, 30};
enum ParameterId {
    ChordType,
    Inversion,
    SynthModel,
    SubOsc,
    TapeWow,
    WowSpeed,
    Cutoff,
    Resonance,
    Attack,
    Decay,
    Sustain,
    Release,
    TapeEcho,
    ShimmerVerb,
    ChorusMix,
    OutputGain,
    FeltHammer,
    FeltTine,
    FeltBark,
    FeltDamping,
    AnaDetune,
    AnaDrift,
    AnaFilterEnv,
    AnaWave,
    DcoPulseWidth,
    DcoPwmDepth,
    DcoPwmRate,
    DcoWave,
    PrismColour,
    PrismPartial,
    PrismStrike,
    PrismRing
};

struct VoiceUnit {
    ADSR *ampEnv = nullptr;
    ADSR *filterEnv = nullptr;
    OtaLowpass ota;
    ToneLowpass tone;
    float baseFreq = 261.63f;
    float velocity = 0.0f;
    bool active = false;
    float driftPhase = 0.0f;
    float driftSpeed = 0.5f;
    float driftInc = 0.0f;
    float pulsePhase = 0.0f;
    float sawPhase = 0.0f;
    float phase3 = 0.0f;
    float phase4 = 0.0f;
    int octave = 0;
    float strikeFast = 1.0f;
    float strikeMedium = 1.0f;
    float strikeSlow = 1.0f;
    int filterPhase = 0;
    int model = -1;
    int fade = 0;
    float previous = 0.0f;
    float transition = 0.0f;
    float dcInput = 0.0f;
    float dcOutput = 0.0f;
};
struct TriggerSlot {
    int midiNote = -1;
    std::uint32_t serial = 0;
    std::array<int, 4> voiceIndices{};
};

float limitedIndex(float carrier, float modulator, float index, float rate, float fundamental) {

    if (fundamental <= rate * 0.015f) return index;

    const float budget = max((rate * 0.45f - carrier) / modulator - 1.0f, 0.0f) / (2.0f * PI);
    return min(index, budget);
}

float roundParameter(float value) {
    const float upper = std::ceil(value);
    return upper - 0.5f <= value ? upper : upper - 1.0f;
}

float smoothControl(float current, float target, float smoothing) {
    if (std::fabs(current - target) < 0.0001f) return target;
    return current * smoothing + target * (1.0f - smoothing);
}

float getChordOffset(int chordType, int voiceIndex, int inversion) {
    float semi = 0.0f;
    if (chordType == 0) {
        if (voiceIndex == 0)
            semi = 0.0f;
        else if (voiceIndex == 1)
            semi = 3.0f;
        else if (voiceIndex == 2)
            semi = 10.0f;
        else
            semi = 14.0f;
    } else if (chordType == 1) {
        if (voiceIndex == 0)
            semi = 0.0f;
        else if (voiceIndex == 1)
            semi = 4.0f;
        else if (voiceIndex == 2)
            semi = 11.0f;
        else
            semi = 14.0f;
    } else if (chordType == 2) {
        if (voiceIndex == 0)
            semi = 0.0f;
        else if (voiceIndex == 1)
            semi = 3.0f;
        else if (voiceIndex == 2)
            semi = 7.0f;
        else
            semi = 10.0f;
    } else if (chordType == 3) {
        if (voiceIndex == 0)
            semi = 0.0f;
        else if (voiceIndex == 1)
            semi = 4.0f;
        else if (voiceIndex == 2)
            semi = 7.0f;
        else
            semi = 11.0f;
    } else if (chordType == 4) {
        if (voiceIndex == 0)
            semi = 0.0f;
        else if (voiceIndex == 1)
            semi = 5.0f;
        else if (voiceIndex == 2)
            semi = 7.0f;
        else
            semi = 14.0f;
    } else if (chordType == 5) {
        if (voiceIndex == 0)
            semi = 0.0f;
        else if (voiceIndex == 1)
            semi = 3.0f;
        else if (voiceIndex == 2)
            semi = 10.0f;
        else
            semi = 17.0f;
    } else if (chordType == 6) {
        if (voiceIndex == 0)
            semi = -12.0f;
        else if (voiceIndex == 1)
            semi = 0.0f;
        else if (voiceIndex == 2)
            semi = 7.0f;
        else
            semi = 12.0f;
    } else {
        if (voiceIndex == 0)
            semi = -12.0f;
        else if (voiceIndex == 1)
            semi = 0.0f;
        else if (voiceIndex == 2)
            semi = 12.0f;
        else
            semi = 24.0f;
    }

    if (inversion == 1) {
        if (voiceIndex == 0) semi += 12.0f;
    } else if (inversion == 2) {
        if (voiceIndex == 0 || voiceIndex == 1) semi += 12.0f;
    } else if (inversion == 3) {
        if (voiceIndex == 0) semi -= 12.0f;
        if (voiceIndex == 1) semi += 0.0f;
        if (voiceIndex == 2) semi += 12.0f;
        if (voiceIndex == 3) semi += 12.0f;
    }

    return semi;
}
} // namespace

struct Dsp::State {
    std::array<float, parameterCount> paramValues{};
    std::array<float, parameterCount> smoothedValues{};
    std::array<ADSR, 4> ampEnvelopes{};
    std::array<ADSR, 4> filterEnvelopes{};
    std::array<VoiceUnit, MAX_VOICES> voiceUnits{};
    std::array<TriggerSlot, 4> triggers{};
    float subPhase = 0.0f;
    float subFreq = 130.8f;
    std::array<float, CHORUS_BUF_SIZE> chorBufL{}, chorBufR{};
    int chorWrite = 0;
    float chorLfo1 = 0.0f;
    float chorLfo2 = 0.5f;
    float chorToneL = 0.0f;
    float chorToneR = 0.0f;
    float dcInputL = 0.0f;
    float dcInputR = 0.0f;
    float dcOutputL = 0.0f;
    float dcOutputR = 0.0f;
    std::array<float, DELAY_BUF_SIZE> delayBufL{}, delayBufR{}, shimmerBufL{}, shimmerBufR{};
    Biquad delayToneL, delayToneR, shimmerToneL, shimmerToneR;
    int delayWrite = 0;
    int shimmerWrite = 0;
    std::array<float, 4> telemetry{};
    std::array<float, capacity> outputL{}, outputR{};
    std::array<float, PROCESS_CAPACITY> blockEngineA{}, blockEngineB{}, blockEngineC{};
    std::array<float, 12> engineControlValues{};
    unsigned engineControlMoving = 0;
    bool cutoffSettled = false;
    std::array<float, PROCESS_CAPACITY> blockWow{}, blockCutoff{}, blockSumL{}, blockSumR{};
    std::array<float, PROCESS_CAPACITY> blockFilterEnv{}, blockAmpEnv{};
    std::array<int, PROCESS_CAPACITY> blockActive{};
    std::array<float, PROCESS_CAPACITY> voiceAmpEnv{}, voiceFilterEnv{};
    std::array<std::array<float, PROCESS_CAPACITY>, CHORD_NOTES> groupWave{};
    std::array<float, (PROCESS_CAPACITY / 16 + 1) * FILTER_COEFF_STRIDE> filterCoefficients{};
    float peakL = 0.0f;
    float peakR = 0.0f;
    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    float sampleRate = 44100.0f;
    float epFastDecay = 0.999f;
    float epMediumDecay = 0.999f;
    float epSlowDecay = 0.999f;
    int lastFeltDamping = -1;
    float bellFastDecay = 0.999f;
    float bellMediumDecay = 0.999f;
    float bellSlowDecay = 0.999f;
    float lastPrismStrike = -1.0f;
    int lastPrismRing = -1;
    float analogDcCoeff = 0.999f;
    float dcoDcCoeff = 0.999f;
    float chorusToneCoeff = 0.55f;
    float lastOutputDb = 1000.0f;
    float cachedOutputGain = 1.0f;
    float coeff = 0.995f;
    std::uint32_t triggerSerial = 0;
    int lastChordType = -1;
    int lastInversion = -1;
    float lastAttack = -1.0f;
    float lastDecay = -1.0f;
    float lastSustain = -1.0f;
    float lastRelease = -1.0f;
    ADSRShape ampShape, filterShape;

    State() {
        for (int i = 0; i < parameterCount; ++i) {
            paramValues[i] = parameters[i].initial;
            smoothedValues[i] = parameters[i].initial;
        }
        for (int i = 0; i < MAX_VOICES; ++i) {
            auto &voice = voiceUnits[i];
            voice.ampEnv = &ampEnvelopes[i / CHORD_NOTES];
            voice.filterEnv = &filterEnvelopes[i / CHORD_NOTES];
            voice.driftPhase = static_cast<float>(i) * 1.37f;
            voice.driftSpeed = 0.2f + static_cast<float>(i % 5) * 0.15f;
        }
        for (int t = 0; t < 4; ++t)
            for (int c = 0; c < CHORD_NOTES; ++c)
                triggers[t].voiceIndices[c] = t * CHORD_NOTES + c;
        init(44100.0f);
    }

    float param(int id) const { return paramValues[id]; }
    int paramInt(int id) const { return static_cast<int>(roundParameter(paramValues[id])); }
    void prepareCutoff(int frames) {
        float current = smoothedValues[Cutoff];
        int i = 0;
        if (!cutoffSettled) {
            const float target = paramValues[Cutoff];
            for (; i < frames; ++i) {
                const float next = current * coeff + target * (1.0f - coeff);
                blockCutoff[i] = next;
                if (next == current) {
                    cutoffSettled = true;
                    ++i;
                    break;
                }
                current = next;
            }
            smoothedValues[Cutoff] = current;
        }
        std::fill(blockCutoff.begin() + i, blockCutoff.begin() + frames, current);
    }
    void resetSmoothing() {
        smoothedValues = paramValues;
        cutoffSettled = false;
    }
    void setParam(int id, float value) {
        if (id < 0 || id >= parameterCount || !std::isfinite(value)) return;
        const auto &parameter = parameters[id];
        float bounded = clamp(value, parameter.minimum, parameter.maximum);
        if (parameter.stepped) bounded = roundParameter(bounded);
        if (bounded == paramValues[id] && std::signbit(bounded) == std::signbit(paramValues[id])) return;
        paramValues[id] = bounded;
        if (id == Cutoff) cutoffSettled = false;
        if (id >= FeltHammer && id <= PrismStrike && (id - FeltHammer) % 4 != 3) {
            const int index = ((id - FeltHammer) / 4) * 3 + (id - FeltHammer) % 4;
            engineControlMoving |= 1u << index;
        }
    }
    float getParam(int id) const { return id >= 0 && id < parameterCount ? paramValues[id] : 0.0f; }

    void updateFeltDamping() {
        const int damping = paramInt(FeltDamping);
        if (damping == lastFeltDamping) return;
        const float scale = damping == 0 ? 0.5f : (damping == 2 ? 2.0f : 1.0f);
        epFastDecay = exp(-1.0f / (0.18f * sampleRate * scale));
        epMediumDecay = exp(-1.0f / (0.95f * sampleRate * scale));
        epSlowDecay = exp(-1.0f / (3.8f * sampleRate * scale));
        lastFeltDamping = damping;
    }

    void updatePrismDecay() {
        const float strike = param(PrismStrike);
        const int ring = paramInt(PrismRing);
        if (strike == lastPrismStrike && ring == lastPrismRing) return;
        const float ringScale = ring == 0 ? 0.5f : (ring == 2 ? 2.0f : 1.0f);
        const float strikeScale = strike * 0.01f;
        bellFastDecay = exp(-1.0f / (0.65f * sampleRate * strikeScale * ringScale));
        bellMediumDecay = exp(-1.0f / (2.4f * sampleRate * ringScale));
        bellSlowDecay = exp(-1.0f / (5.2f * sampleRate * ringScale));
        lastPrismStrike = strike;
        lastPrismRing = ring;
    }

    void init(float sr) {
        sampleRate = std::isfinite(sr) && sr >= 8000.0f && sr <= 96000.0f ? sr : 44100.0f;
        sr = sampleRate;
        coeff = smoothCoeff(5.0f, sr);
        lastFeltDamping = -1;
        updateFeltDamping();
        for (int i = 0; i < 12; i++)
            engineControlValues[i] = param(engineControlIds[i]);
        engineControlMoving = 0;
        lastPrismStrike = -1.0f;
        lastPrismRing = -1;
        updatePrismDecay();
        analogDcCoeff = exp(-2.0f * PI * 8.0f / sr);
        dcoDcCoeff = exp(-2.0f * PI * 20.0f / sr);
        chorusToneCoeff = 1.0f - exp(-2.0f * PI * min(6000.0f, sr * 0.4f) / sr);
        lastOutputDb = param(OutputGain);
        cachedOutputGain = dbToLinear(lastOutputDb);
        resetSmoothing();

        for (int i = 0; i < MAX_VOICES; i++) {
            auto &v = voiceUnits[i];
            if (i % CHORD_NOTES == 0) {
                v.ampEnv->reset();
                v.filterEnv->reset();
            }
            v.active = false;
            v.velocity = 0.0f;
            v.pulsePhase = 0.0f;
            v.sawPhase = 0.0f;
            v.phase3 = 0.0f;
            v.phase4 = 0.0f;
            v.octave = 0;
            v.strikeFast = 1.0f;
            v.strikeMedium = 1.0f;
            v.strikeSlow = 1.0f;
            v.filterPhase = 0;
            v.model = -1;
            v.fade = 0;
            v.previous = 0.0f;
            v.transition = 0.0f;
            v.dcInput = 0.0f;
            v.dcOutput = 0.0f;
            v.ota.reset();
            v.tone.reset();
            v.driftPhase = static_cast<float>(i) * 1.37f;
            v.driftInc = (v.driftSpeed * 2.0f * PI) / sr;
        }
        for (int t = 0; t < 4; t++) {
            triggers[t].midiNote = -1;
            triggers[t].serial = 0;
        }

        delayToneL.reset();
        delayToneR.reset();
        delayToneL.setLowpass(2800.0f, 0.6f, sr);
        delayToneR.setLowpass(2800.0f, 0.6f, sr);

        shimmerToneL.reset();
        shimmerToneR.reset();
        shimmerToneL.setHighpass(600.0f, 0.7f, sr);
        shimmerToneR.setHighpass(600.0f, 0.7f, sr);

        chorBufL.fill(0.0f);
        chorBufR.fill(0.0f);
        delayBufL.fill(0.0f);
        delayBufR.fill(0.0f);
        shimmerBufL.fill(0.0f);
        shimmerBufR.fill(0.0f);

        chorWrite = 0;
        delayWrite = 0;
        shimmerWrite = 0;
        subPhase = 0.0f;

        subFreq = 130.8f;
        wowPhase = 0.0f;
        flutterPhase = 0.0f;
        chorLfo1 = 0.0f;
        chorLfo2 = 0.5f;
        chorToneL = 0.0f;
        chorToneR = 0.0f;
        dcInputL = 0.0f;
        dcInputR = 0.0f;
        dcOutputL = 0.0f;
        dcOutputR = 0.0f;
        peakL = 0.0f;
        peakR = 0.0f;
        triggerSerial = 0;
        lastChordType = -1;
        lastInversion = -1;
        lastAttack = -1.0f;
        lastDecay = -1.0f;
        lastSustain = -1.0f;
        lastRelease = -1.0f;
        telemetry.fill(0.0f);
        filterCoefficients.fill(0.0f);
        outputL.fill(0.0f);
        outputR.fill(0.0f);
    }

    void noteOn(int note, int velocity) {
        if (note < 0 || note > 127) return;
        if (velocity <= 0) {
            noteOff(note);
            return;
        }
        velocity = velocity > 127 ? 127 : velocity;
        int slotIdx = -1;
        for (int t = 0; t < 4; t++) {
            if (triggers[t].midiNote == note) {
                slotIdx = t;
                break;
            }
        }
        if (slotIdx < 0) {
            for (int t = 0; t < 4; t++) {
                const auto &candidate = triggers[t];
                if (candidate.midiNote < 0) {
                    bool silent = true;
                    for (int c = 0; c < CHORD_NOTES; c++) {
                        if (voiceUnits[candidate.voiceIndices[c]].active) silent = false;
                    }
                    if (silent) {
                        slotIdx = t;
                        break;
                    }
                }
            }
        }
        if (slotIdx < 0) {
            std::uint32_t oldestAge = 0;
            bool foundReleased = false;
            for (int t = 0; t < 4; t++) {
                const auto &candidate = triggers[t];
                const bool released = candidate.midiNote < 0;
                const std::uint32_t age = triggerSerial - candidate.serial;
                if (slotIdx < 0 || (released && !foundReleased) || (released == foundReleased && age >= oldestAge)) {
                    slotIdx = t;
                    oldestAge = age;
                    foundReleased = released;
                }
            }
        }

        auto &slot = triggers[slotIdx];
        slot.midiNote = note;
        slot.serial = ++triggerSerial;

        const auto chordType = paramInt(ChordType);
        const auto inversion = paramInt(Inversion);
        const float vel = static_cast<float>(velocity) / 127.0f;

        subFreq = noteToHz(static_cast<float>(note - 12) + DEFAULT_TRANSPOSE_SEMITONES);
        auto &firstVoice = voiceUnits[slot.voiceIndices[0]];
        firstVoice.ampEnv->gate(true);
        firstVoice.filterEnv->gate(true);

        for (int c = 0; c < CHORD_NOTES; c++) {
            const auto vIdx = slot.voiceIndices[c];
            auto &v = voiceUnits[vIdx];
            const float offset = getChordOffset(chordType, c, inversion);
            const float finalNote = static_cast<float>(note) + offset;
            v.baseFreq = noteToHz(finalNote + DEFAULT_TRANSPOSE_SEMITONES);
            v.velocity = vel;
            v.active = true;
            v.strikeFast = 1.0f;
            v.strikeMedium = 1.0f;
            v.strikeSlow = 1.0f;
            if (paramInt(SynthModel) == 0 || paramInt(SynthModel) == 3) {
                v.pulsePhase = 0.0f;
                v.sawPhase = 0.0f;
                v.phase3 = 0.0f;
                v.phase4 = 0.0f;
            }
        }
    }

    void noteOff(int note) {
        if (note < 0 || note > 127) return;
        for (int t = 0; t < 4; t++) {
            auto &slot = triggers[t];
            if (slot.midiNote == note) {
                slot.midiNote = -1;
                auto &firstVoice = voiceUnits[slot.voiceIndices[0]];
                firstVoice.ampEnv->gate(false);
                firstVoice.filterEnv->gate(false);
            }
        }
        int latestNote = -1;
        std::uint32_t newestAge = UINT32_MAX;
        for (int t = 0; t < 4; t++) {
            auto &slot = triggers[t];
            const std::uint32_t age = triggerSerial - slot.serial;
            if (slot.midiNote >= 0 && age < newestAge) {
                latestNote = slot.midiNote;
                newestAge = age;
            }
        }
        if (latestNote >= 0) subFreq = noteToHz(static_cast<float>(latestNote - 12) + DEFAULT_TRANSPOSE_SEMITONES);
    }

    void allNotesOff() {
        for (int t = 0; t < 4; t++) {
            const int note = triggers[t].midiNote;
            if (note >= 0) noteOff(note);
        }
    }

    void processBlock(float *outL, float *outR, int frames) {
        const float rate = sampleRate;

        const float smoothingCoeff = coeff;
        const float preDcCoeff = analogDcCoeff;
        const float chorusCoeff = chorusToneCoeff;

        const auto chordType = paramInt(ChordType);
        const auto inversion = paramInt(Inversion);
        const auto synthModel = paramInt(SynthModel);
        const int controlOffset = synthModel * 3;
        const float targetA = param(engineControlIds[controlOffset]);
        const float targetB = param(engineControlIds[controlOffset + 1]);
        const float targetC = param(engineControlIds[controlOffset + 2]);
        float controlA = engineControlValues[controlOffset];
        float controlB = engineControlValues[controlOffset + 1];
        float controlC = engineControlValues[controlOffset + 2];
        const int wave = synthModel == 1 ? paramInt(AnaWave) : (synthModel == 2 ? paramInt(DcoWave) : 2);
        if (synthModel == 0)
            updateFeltDamping();
        else if (synthModel == 3)
            updatePrismDecay();

        const unsigned selectedControls = 7u << controlOffset;
        unsigned moving = (engineControlMoving >> controlOffset) & 7u;
        if (synthModel == 3) {
            controlC = targetC;
            moving &= ~4u;
        }
        int controlFrame = 0;
        for (; moving && controlFrame < frames; ++controlFrame) {
            if (moving & 1u) {
                controlA = smoothControl(controlA, targetA, smoothingCoeff);
                if (controlA == targetA) moving &= ~1u;
            }
            if (moving & 2u) {
                controlB = smoothControl(controlB, targetB, smoothingCoeff);
                if (controlB == targetB) moving &= ~2u;
            }
            if (moving & 4u) {
                controlC = smoothControl(controlC, targetC, smoothingCoeff);
                if (controlC == targetC) moving &= ~4u;
            }
            blockEngineA[controlFrame] = synthModel == 2 && controlA == 34.0f ? 0.34f : controlA * 0.01f;
            blockEngineB[controlFrame] = controlB * 0.01f;
            blockEngineC[controlFrame] = synthModel == 3 ? bellFastDecay : controlC * 0.01f;
        }
        std::fill(blockEngineA.begin() + controlFrame, blockEngineA.begin() + frames,
                  synthModel == 2 && controlA == 34.0f ? 0.34f : controlA * 0.01f);
        std::fill(blockEngineB.begin() + controlFrame, blockEngineB.begin() + frames, controlB * 0.01f);
        std::fill(blockEngineC.begin() + controlFrame, blockEngineC.begin() + frames,
                  synthModel == 3 ? bellFastDecay : controlC * 0.01f);
        engineControlValues[controlOffset] = controlA;
        engineControlValues[controlOffset + 1] = controlB;
        engineControlValues[controlOffset + 2] = controlC;
        unsigned inactiveMoving = engineControlMoving & ~selectedControls;
        while (inactiveMoving) {
            const int index = __builtin_ctz(inactiveMoving);
            engineControlValues[index] = param(engineControlIds[index]);
            inactiveMoving &= inactiveMoving - 1u;
        }
        engineControlMoving = moving << controlOffset;

        if (chordType != lastChordType || inversion != lastInversion) {
            for (int t = 0; t < 4; t++) {
                auto &slot = triggers[t];
                if (slot.midiNote >= 0) {
                    for (int c = 0; c < CHORD_NOTES; c++) {
                        const auto vIdx = slot.voiceIndices[c];
                        auto &v = voiceUnits[vIdx];
                        if (v.active) {
                            const float offset = getChordOffset(chordType, c, inversion);
                            const float finalNote = static_cast<float>(slot.midiNote) + offset;
                            v.baseFreq = noteToHz(finalNote + DEFAULT_TRANSPOSE_SEMITONES);
                        }
                    }
                }
            }
            lastChordType = chordType;
            lastInversion = inversion;
        }

        const float subPercent = param(SubOsc) * 0.01f;
        const float dcoSubGain = subPercent * 0.34f;
        const float wowDepth = param(TapeWow) * 0.01f;
        const float wowSpeedHz = param(WowSpeed);
        const float resonance = param(Resonance) * 0.01f;

        const float attackMs = param(Attack);
        const float decayMs = param(Decay);
        const float sustain = param(Sustain) * 0.01f;
        const float releaseMs = param(Release);

        const float chorusMix = param(ChorusMix) * 0.01f;
        const float echoMix = param(TapeEcho) * 0.01f;
        const float shimmerMix = param(ShimmerVerb) * 0.01f;
        const float outputDb = param(OutputGain);
        if (outputDb != lastOutputDb) {
            cachedOutputGain = dbToLinear(outputDb);
            lastOutputDb = outputDb;
        }
        const float outputGain = cachedOutputGain;
        const float modelGain = synthModel == 0 ? 0.20f : (synthModel == 1 ? 0.48f : (synthModel == 2 ? 0.28f : 0.25f));

        if (attackMs != lastAttack || decayMs != lastDecay || sustain != lastSustain || releaseMs != lastRelease) {
            ampShape.set(attackMs, decayMs, sustain, releaseMs, rate);
            filterShape.set(attackMs * 0.7f, decayMs * 1.1f, sustain * 0.6f, releaseMs, rate);
            for (int v = 0; v < MAX_VOICES; v += CHORD_NOTES) {
                auto &unit = voiceUnits[v];
                unit.ampEnv->configure(ampShape);
                unit.filterEnv->configure(filterShape);
            }
            lastAttack = attackMs;
            lastDecay = decayMs;
            lastSustain = sustain;
            lastRelease = releaseMs;
        }

        const float wowInc = static_cast<float>((wowSpeedHz * 2.0f * PI) / rate);
        const float flutterInc = static_cast<float>((18.5f * 2.0f * PI) / rate);

        const int delaySampsL = static_cast<int>(rate * 0.375f);
        const int delaySampsR = static_cast<int>(rate * 0.500f);

        const int shimmerSampsL = static_cast<int>(rate * 0.185f);
        const int shimmerSampsR = static_cast<int>(rate * 0.245f);
        std::fill_n(blockSumL.data(), frames, 0.0f);
        std::fill_n(blockSumR.data(), frames, 0.0f);
        std::fill_n(blockAmpEnv.data(), frames, 0.0f);
        std::fill_n(blockActive.data(), frames, 0);
        prepareCutoff(frames);
        for (int i = 0; i < frames; i++) {
            wowPhase += wowInc;
            if (wowPhase > 2.0f * PI) wowPhase -= 2.0f * PI;

            flutterPhase += flutterInc;
            if (flutterPhase > 2.0f * PI) flutterPhase -= 2.0f * PI;

            blockWow[i] = sineUnit(wowPhase / (2.0f * PI)) * 0.018f * wowDepth +
                          sineUnit(flutterPhase / (2.0f * PI)) * 0.005f * wowDepth;
        }

        const float fastDecay = synthModel == 0 ? epFastDecay : bellFastDecay;
        const float mediumDecay = synthModel == 0 ? epMediumDecay : bellMediumDecay;
        const float slowDecay = synthModel == 0 ? epSlowDecay : bellSlowDecay;
        const float nyquistStart = rate * 0.32f;
        const float nyquistWidth = rate * 0.14f;
        const float dcCoeff = synthModel == 2 ? dcoDcCoeff : analogDcCoeff;

        for (int group = 0; group < MAX_VOICES; group += CHORD_NOTES) {
            auto &leader = voiceUnits[group];
            if (!leader.active) continue;
            const int activeFrames = leader.ampEnv->render(voiceAmpEnv.data(), frames);
            leader.filterEnv->render(voiceFilterEnv.data(), activeFrames < frames ? activeFrames + 1 : frames);
            for (int lane = 0; lane < CHORD_NOTES; ++lane) {
                auto &voice = voiceUnits[group + lane];
                float driftPhase = voice.driftPhase;
                float pulsePhase = voice.pulsePhase;
                float sawPhase = voice.sawPhase;
                float phase3 = voice.phase3;
                float phase4 = voice.phase4;
                int octave = voice.octave;
                float strikeFast = voice.strikeFast;
                float strikeMedium = voice.strikeMedium;
                float strikeSlow = voice.strikeSlow;
                float dcInput = voice.dcInput;
                float dcOutput = voice.dcOutput;
                int fade = voice.fade;
                float previous = voice.previous;
                float transition = voice.transition;
                const float driftInc = voice.driftInc;
                const float baseFreq = voice.baseFreq;
                const float velocity = voice.velocity;
                const float velocity2 = velocity * velocity;
                if (voice.model != synthModel) {
                    transition = previous;
                    fade = voice.model < 0 ? 0 : 64;
                    voice.model = synthModel;
                    voice.filterPhase = 0;
                }
                for (int i = 0; i < activeFrames; i++) {

                    const float wowMod = blockWow[i];

                    driftPhase += synthModel == 2 ? driftInc * blockEngineC[i] : driftInc;
                    if (driftPhase > 2.0f * PI) driftPhase -= 2.0f * PI;
                    const float drift = synthModel == 3 ? 0.0f
                                                        : (driftPhase <= 2.0f * PI ? sineUnit(driftPhase / (2.0f * PI))
                                                                                   : sineCycle(driftPhase / (2.0f * PI)));
                    const float pitchDrift =
                        synthModel == 1 ? drift * (0.0025f * blockEngineB[i]) : (synthModel == 0 ? drift * 0.00045f : 0.0f);
                    const float curFreq = clamp(baseFreq * (1.0f + wowMod + pitchDrift), 1.0f, rate * 0.48f);
                    const float dt = curFreq / rate;
                    float oscSample = 0.0f;
                    if (synthModel == 0) {
                        const float hammerScale = blockEngineA[i];
                        const float tineScale = blockEngineB[i];
                        const float barkDrive = blockEngineC[i];
                        const float tineWeight =
                            clamp((nyquistStart + nyquistWidth - curFreq * 2.01f) / nyquistWidth, 0.0f, 1.0f);
                        float bodyMod = (0.06f + velocity2 * 0.52f) * (0.15f + strikeFast * 0.85f);
                        float tineMod = (0.08f + velocity2 * 0.33f) * strikeFast;
                        if (hammerScale != 1.0f) {
                            bodyMod *= hammerScale;
                            tineMod *= hammerScale;
                        }
                        const float bodyIndex = limitedIndex(curFreq, curFreq * 2.0f, bodyMod, rate, curFreq);
                        const float tineIndex = limitedIndex(curFreq * 2.01f, curFreq * 7.03f, tineMod, rate, curFreq);
                        const float body =
                            bodyIndex == 0.0f ? sineUnit(pulsePhase) : sinePm(pulsePhase + sineUnit(sawPhase) * bodyIndex);
                        const float tine =
                            tineIndex == 0.0f ? sineUnit(phase3) : sinePm(phase3 + sineUnit(phase4) * tineIndex);
                        float upper = tine * (0.08f + velocity * 0.32f) * strikeMedium * tineWeight;
                        if (tineScale != 1.0f) upper *= tineScale;
                        oscSample =
                            softClip((body * (0.26f + strikeSlow * 0.74f) + upper) * (1.0f + velocity2 * barkDrive)) *
                            0.72f;
                        pulsePhase += dt;
                        sawPhase += dt * 2.0f;
                        phase3 += dt * 2.01f;
                        phase4 += dt * 7.03f;
                    } else if (synthModel == 1) {
                        const float detune = blockEngineA[i];
                        const float detuneRatio = detune == 1.0f ? 1.0045f : 1.0f + 0.0045f * detune;
                        const float dt2 = clamp(dt * (detuneRatio + drift * (0.0011f * blockEngineB[i])), 0.000001f, 0.48f);
                        const float pulse = polyBlepPulse(pulsePhase, dt, 0.48f + drift * 0.07f);
                        if (wave == 1)
                            oscSample = pulse * 1.09f;
                        else {
                            oscSample = polyBlepSaw(pulsePhase, dt) * 0.58f + polyBlepSaw(sawPhase, dt2) * 0.38f;
                            if (wave == 2) oscSample += pulse * 0.13f;
                        }
                        pulsePhase += dt;
                        sawPhase += dt2;
                    } else if (synthModel == 2) {
                        const float width = clamp(blockEngineA[i] + drift * blockEngineB[i], 0.05f, 0.95f);
                        const float subCycle = (pulsePhase + static_cast<float>(octave)) * 0.5f;
                        if (wave == 0)
                            oscSample = polyBlepSaw(pulsePhase, dt) * 0.88f;
                        else if (wave == 1)
                            oscSample = polyBlepPulse(pulsePhase, dt, width) * 0.88f;
                        else
                            oscSample = polyBlepSaw(pulsePhase, dt) * 0.30f + polyBlepPulse(pulsePhase, dt, width) * 0.58f;
                        oscSample += polyBlepPulse(subCycle, dt * 0.5f, 0.5f) * dcoSubGain;
                        pulsePhase += dt;
                        if (pulsePhase >= 1.0f) octave = 1 - octave;
                    } else {
                        const float colour = blockEngineA[i];
                        const float partialLevel = blockEngineB[i];
                        const float partialWeight =
                            clamp((nyquistStart + nyquistWidth - curFreq * 1.497f) / nyquistWidth, 0.0f, 1.0f);
                        float firstMod = (0.12f + velocity2 * 0.70f) * (0.10f + strikeFast * 0.90f);
                        float secondMod = (0.04f + velocity * 0.30f) * strikeMedium;
                        if (colour != 1.0f) {
                            firstMod *= colour;
                            secondMod *= colour;
                        }
                        const float firstIndex = limitedIndex(curFreq, curFreq * 2.713f, firstMod, rate, curFreq);
                        const float secondIndex =
                            limitedIndex(curFreq * 1.497f, curFreq * 4.197f, secondMod, rate, curFreq);
                        const float first = firstIndex == 0.0f ? sineUnit(pulsePhase)
                                                               : sinePm(pulsePhase + sineUnit(sawPhase) * firstIndex);
                        const float second =
                            secondIndex == 0.0f ? sineUnit(phase3) : sinePm(phase3 + sineUnit(phase4) * secondIndex);
                        float upper = second * strikeMedium * 0.36f * partialWeight;
                        if (partialLevel != 1.0f) upper *= partialLevel;
                        oscSample = first * (0.18f + strikeSlow * 0.46f) + upper;
                        pulsePhase += dt;
                        sawPhase += dt * 2.713f;
                        phase3 += dt * 1.497f;
                        phase4 += dt * 4.197f;
                    }
                    if (pulsePhase >= 1.0f) pulsePhase -= 1.0f;
                    if (sawPhase >= 1.0f) sawPhase -= sawPhase >= 2.0f ? 2.0f : 1.0f;
                    if (phase3 >= 1.0f) phase3 -= 1.0f;
                    if (phase4 >= 1.0f)
                        phase4 -= phase4 >= 4.0f ? 4.0f : (phase4 >= 3.0f ? 3.0f : (phase4 >= 2.0f ? 2.0f : 1.0f));
                    if (synthModel == 0 || synthModel == 3) {
                        strikeFast *= synthModel == 3 ? blockEngineC[i] : fastDecay;
                        strikeMedium *= mediumDecay;
                        strikeSlow *= slowDecay;
                    }

                    if (synthModel == 1 || synthModel == 2) {

                        const float coupled = oscSample - dcInput + preDcCoeff * dcOutput;
                        dcInput = oscSample;
                        dcOutput = coupled;
                        oscSample = coupled;
                    }

                    groupWave[lane][i] = oscSample;
                }
                voice.dcInput = dcInput;
                voice.dcOutput = dcOutput;
                voice.driftPhase = driftPhase;
                voice.pulsePhase = pulsePhase;
                voice.sawPhase = sawPhase;
                voice.phase3 = phase3;
                voice.phase4 = phase4;
                voice.octave = octave;
                voice.strikeFast = strikeFast;
                voice.strikeMedium = strikeMedium;
                voice.strikeSlow = strikeSlow;
                voice.fade = fade;
                voice.transition = transition;
            }
            int filterPhase = leader.filterPhase;
            const float velocity = leader.velocity;
            const float velocity2 = velocity * velocity;
            OtaLowpass *const otaFilters[4] = {&voiceUnits[group].ota, &voiceUnits[group + 1].ota,
                                             &voiceUnits[group + 2].ota, &voiceUnits[group + 3].ota};
            ToneLowpass *const toneFilters[4] = {&voiceUnits[group].tone, &voiceUnits[group + 1].tone,
                                               &voiceUnits[group + 2].tone, &voiceUnits[group + 3].tone};
            float *const buffers[4] = {groupWave[0].data(), groupWave[1].data(),
                                      groupWave[2].data(), groupWave[3].data()};
            int filterOffset = 0;
            float *coefficientPointer = filterCoefficients.data();
            if (filterPhase != 0 && activeFrames > 0) {
                if (synthModel == 1 || synthModel == 2)
                    leader.ota.writeCoefficients(coefficientPointer);
                else
                    leader.tone.writeCoefficients(coefficientPointer);
            }
            while (filterOffset < activeFrames) {
                if (filterPhase == 0) {

                    const float baseCutoff = blockCutoff[filterOffset];
                    const float fltEnv = voiceFilterEnv[filterOffset];
                    if (synthModel == 1)
                        leader.ota.set(baseCutoff * (0.62f + fltEnv * (2.8f * blockEngineC[filterOffset])) + 80.0f,
                                      resonance, 1.25f + velocity * 0.65f, rate);
                    else if (synthModel == 2)
                        leader.ota.set(baseCutoff * (0.52f + fltEnv * 1.35f) + 100.0f, resonance, 1.15f, rate);
                    else if (synthModel == 0)
                        leader.tone.set(baseCutoff * (1.55f + velocity2 * 1.45f + fltEnv) + 160.0f, resonance * 0.65f,
                                       rate);
                    else
                        leader.tone.set(baseCutoff * (2.1f + fltEnv * 3.0f) + 250.0f, resonance * 0.45f, rate);
                    if (synthModel == 1 || synthModel == 2)
                        leader.ota.writeCoefficients(coefficientPointer);
                    else
                        leader.tone.writeCoefficients(coefficientPointer);
                }
                const int count = std::min(16 - filterPhase, activeFrames - filterOffset);
                if (synthModel == 1 || synthModel == 2)
                    OtaLowpass::renderFour(otaFilters, buffers, filterOffset, count, coefficientPointer);
                else
                    ToneLowpass::renderFour(toneFilters, buffers, filterOffset, count, coefficientPointer);
                filterOffset += count;
                filterPhase = (filterPhase + count) & 15;
                coefficientPointer += FILTER_COEFF_STRIDE;
            }

            for (int lane = 0; lane < CHORD_NOTES; ++lane) {
                auto &voice = voiceUnits[group + lane];
                const float velocity = voice.velocity;
                const float panPos = (static_cast<float>(lane) - 1.5f) * 0.35f;
                const float pL = 0.707f * (1.0f - panPos);
                const float pR = 0.707f * (1.0f + panPos);
                int fade = voice.fade;
                float previous = voice.previous;
                const float transition = voice.transition;
                for (int i = 0; i < activeFrames; i++) {

                    const float amp = voiceAmpEnv[i];
                    const float shaped = groupWave[lane][i];
                    blockActive[i] = blockActive[i] + 1;
                    blockAmpEnv[i] = blockAmpEnv[i] + amp * velocity;
                    float voiceSig = shaped * amp * velocity * modelGain;
                    if (fade > 0) {
                        const float blend = static_cast<float>(fade) / 64.0f;
                        voiceSig = transition * blend + voiceSig * (1.0f - blend);
                        fade--;
                    }
                    previous = voiceSig;
                    blockSumL[i] = blockSumL[i] + voiceSig * pL;
                    blockSumR[i] = blockSumR[i] + voiceSig * pR;
                }
                voice.fade = fade;
                voice.previous = previous;
                voice.filterPhase = filterPhase;
                voice.active = voice.ampEnv->isActive();
            }
        }

        for (int i = 0; i < frames; i++) {

            const int active = blockActive[i];
            if (active > 0 && subPercent > 0.001f) {
                const float wowMod = blockWow[i];
                subPhase += (subFreq * (1.0f + wowMod * 0.5f)) / rate;
                if (subPhase > 1.0f) subPhase -= 1.0f;
                const float subAmp = blockAmpEnv[i] / static_cast<float>(active);
                const float subSig = sineUnit(subPhase) * subPercent * subAmp * 0.32f;
                blockSumL[i] = blockSumL[i] + subSig;
                blockSumR[i] = blockSumR[i] + subSig;
            }
        }

        float lastWowMod = 0.0f;
        int offset = 0;
        while (offset < frames) {
            const int count = std::min(16, frames - offset);
            const int end = offset + count;
            for (int i = offset; i < end; i++) {
                float filteredL = blockSumL[i];
                float filteredR = blockSumR[i];

                filteredL = softClip(filteredL * 0.95f);
                filteredR = softClip(filteredR * 0.95f);
                const float coupledL = filteredL - dcInputL + dcCoeff * dcOutputL;
                const float coupledR = filteredR - dcInputR + dcCoeff * dcOutputR;
                dcInputL = filteredL;
                dcInputR = filteredR;
                dcOutputL = coupledL;
                dcOutputR = coupledR;
                filteredL = coupledL;
                filteredR = coupledR;

                chorLfo1 += 0.85f / rate;
                if (chorLfo1 > 1.0f) chorLfo1 -= 1.0f;
                chorLfo2 += 0.65f / rate;
                if (chorLfo2 > 1.0f) chorLfo2 -= 1.0f;

                const float lfo = sineUnit(chorLfo1);
                const float lfoR = synthModel == 2 ? -lfo : sineCycle(chorLfo2 + 0.25f);
                const float chorusTime = synthModel == 2 ? 0.007f : 0.012f;
                const float chorusDepth = synthModel == 2 ? 0.0019f : 0.0012f;
                const float modOffsetL = rate * (chorusTime + lfo * chorusDepth);
                const float modOffsetR = rate * (chorusTime + lfoR * chorusDepth);
                chorToneL += (filteredL - chorToneL) * chorusCoeff;
                chorToneR += (filteredR - chorToneR) * chorusCoeff;
                const int cWrite = chorWrite & CHORUS_MASK;
                chorBufL[cWrite] = chorToneL;
                chorBufR[cWrite] = chorToneR;
                const int wholeL = static_cast<int>(modOffsetL);
                const int wholeR = static_cast<int>(modOffsetR);
                const float fracL = modOffsetL - static_cast<float>(wholeL);
                const float fracR = modOffsetR - static_cast<float>(wholeR);
                const int readIdxL = (cWrite - wholeL) & CHORUS_MASK;
                const int readIdxR = (cWrite - wholeR) & CHORUS_MASK;
                const float firstL = chorBufL[readIdxL];
                const float firstR = chorBufR[readIdxR];
                const float chorWetL = firstL + (chorBufL[(readIdxL - 1) & CHORUS_MASK] - firstL) * fracL;
                const float chorWetR = firstR + (chorBufR[(readIdxR - 1) & CHORUS_MASK] - firstR) * fracR;
                chorWrite = (chorWrite + 1) & CHORUS_MASK;

                blockSumL[i] = filteredL * (1.0f - chorusMix * 0.5f) + chorWetL * (chorusMix * 0.7f);
                blockSumR[i] = filteredR * (1.0f - chorusMix * 0.5f) + chorWetR * (chorusMix * 0.7f);

                const int dWrite = (delayWrite + i - offset) & DELAY_MASK;
                const int sWrite = (shimmerWrite + i - offset) & DELAY_MASK;
                voiceAmpEnv[i] = delayBufL[(dWrite - delaySampsL) & DELAY_MASK];
                voiceFilterEnv[i] = delayBufR[(dWrite - delaySampsR) & DELAY_MASK];
                blockFilterEnv[i] = shimmerBufL[(sWrite - shimmerSampsL) & DELAY_MASK];
                blockAmpEnv[i] = shimmerBufR[(sWrite - shimmerSampsR) & DELAY_MASK];
            }

            delayToneL.render(voiceAmpEnv.data(), offset, count);
            delayToneR.render(voiceFilterEnv.data(), offset, count);
            shimmerToneL.render(blockFilterEnv.data(), offset, count);
            shimmerToneR.render(blockAmpEnv.data(), offset, count);
            for (int i = offset; i < end; i++) {
                const float chorusedL = blockSumL[i];
                const float chorusedR = blockSumR[i];
                const float delayedL = voiceAmpEnv[i];
                const float delayedR = voiceFilterEnv[i];
                const float shimmerL = blockFilterEnv[i];
                const float shimmerR = blockAmpEnv[i];

                const auto dWrite = delayWrite & DELAY_MASK;
                const float echoFb = 0.42f;
                delayBufL[dWrite] = chorusedL + delayedR * echoFb;
                delayBufR[dWrite] = chorusedR + delayedL * echoFb;
                delayWrite = (delayWrite + 1) & DELAY_MASK;

                const auto sWrite = shimmerWrite & DELAY_MASK;
                const float shimmerFb = 0.55f;
                shimmerBufL[sWrite] = chorusedL + shimmerR * shimmerFb;
                shimmerBufR[sWrite] = chorusedR + shimmerL * shimmerFb;
                shimmerWrite = (shimmerWrite + 1) & DELAY_MASK;

                const float finalL = (chorusedL * (1.0f - (echoMix + shimmerMix) * 0.45f) + delayedL * echoMix * 0.65f +
                                      shimmerL * shimmerMix * 0.55f) *
                                     outputGain;
                const float finalR = (chorusedR * (1.0f - (echoMix + shimmerMix) * 0.45f) + delayedR * echoMix * 0.65f +
                                      shimmerR * shimmerMix * 0.55f) *
                                     outputGain;

                const float safeOutL = clamp(finalL, -1.0f, 1.0f);
                const float safeOutR = clamp(finalR, -1.0f, 1.0f);

                outL[i] = safeOutL;
                outR[i] = safeOutR;

                peakL = max(peakL, std::fabs(safeOutL));
                peakR = max(peakR, std::fabs(safeOutR));
                lastWowMod = blockWow[i];
            }
            offset = end;
        }

        telemetry[0] = peakL;
        telemetry[3] = lastWowMod;
        telemetry[1] = peakR;
        int activeCount = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            if (voiceUnits[v].active) activeCount++;
        }
        telemetry[2] = static_cast<float>(activeCount);
    }
};

Dsp::Dsp() : state_(std::make_unique<State>()) {}
Dsp::~Dsp() = default;
void Dsp::init(float sampleRate) { state_->init(sampleRate); }
void Dsp::setParam(int id, float value) { state_->setParam(id, value); }
float Dsp::getParam(int id) const { return state_->getParam(id); }
void Dsp::noteOn(int note, int velocity) { state_->noteOn(note, velocity); }
void Dsp::noteOff(int note) { state_->noteOff(note); }
void Dsp::allNotesOff() { state_->allNotesOff(); }
void Dsp::allSoundOff() { state_->init(state_->sampleRate); }
std::uint32_t Dsp::render(std::uint32_t frames) {
    if (!frames || frames > capacity) return 0;
    for (std::uint32_t offset = 0; offset < frames; offset += PROCESS_CAPACITY)
        state_->processBlock(state_->outputL.data() + offset, state_->outputR.data() + offset,
                             static_cast<int>(std::min<std::uint32_t>(PROCESS_CAPACITY, frames - offset)));
    state_->peakL = 0.0f;
    state_->peakR = 0.0f;
    return frames;
}
const float *Dsp::output(unsigned channel) const {
    return channel == 0 ? state_->outputL.data() : (channel == 1 ? state_->outputR.data() : nullptr);
}
const float *Dsp::telemetry() const { return state_->telemetry.data(); }
const void *Dsp::storageAddress() const { return state_.get(); }
std::size_t Dsp::storageBytes() const { return sizeof(State); }

} // namespace hexachord::native
