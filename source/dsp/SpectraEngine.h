#pragma once
#include "Effects.h"
#include "SpectraVoice.h"
#include "Wavetables.h"
#include <array>

namespace spectra {

/** The whole instrument: 16 voices, the shared LFO phases, the wavetable slots, and the FX
 *  chain. Ports worklet.js's SpectraEngine plus the main-thread FX graph in js/synth.js.
 *
 *  Wavetables are *not* built here. Building the mips costs single-digit milliseconds, so it
 *  happens off the audio thread and arrives through TableSlot, exactly as the web version
 *  builds tables on the main thread and posts them to the worklet.
 */
class SpectraEngine
{
public:
    void prepare (double sampleRate, int maximumBlockSize);

    void setParams (const Params& p);
    const Params& getParams() const { return params; }

    /** Message thread: hand a freshly built table to an oscillator. */
    void publishTable (int osc, std::unique_ptr<WavetableSet> table);

    void noteOn (int midiNote, float velocity01);
    void noteOff (int midiNote);
    void setWheel (float value0to1) { wheel = clampf (value0to1, 0.0f, 1.0f); }
    void panic();

    void render (float* left, float* right, int numSamples);

    int getActiveVoiceCount() const;
    uint32_t getVoiceActiveMask() const;

private:
    void updateEnvCoefs();

    double sr = 44100.0;
    Params params;

    std::array<SpectraVoice, kMaxVoices> voices;
    TableSlot tables[2];

    uint64_t ageCounter = 0;
    int lastNote = -1;
    float wheel = 0.0f;
    float lfoGlobal[2] { 0.0f, 0.0f };

    EnvCoefs e1c, e2c;

    Distortion distortion;
    Chorus chorus;
    Delay delay;
    Reverb reverb;
    Compressor comp;

    std::vector<float> scratchL, scratchR;
};

} // namespace spectra
