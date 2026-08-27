#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "../dsp/SpectraEngine.h"
#include "../dsp/Randomize.h"

class SpectraAudioProcessor : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    SpectraAudioProcessor();
    ~SpectraAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }   // 20s release plus a long reverb

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getValueTree() { return apvts; }
    int getActiveVoiceCount() const { return engine.getActiveVoiceCount(); }
    uint32_t getVoiceActiveMask() const { return engine.getVoiceActiveMask(); }

    /** Copies the preview waveform for `osc` at morph position `pos` into `out`, for the
     *  editor's wavetable display. Safe to call from the message thread at any time. */
    void getDisplayFrame (int osc, float pos, std::vector<float>& out) const;

    // ----- the dice ---------------------------------------------------------
    // The web build's RANDOM / MUTATE pair, plus an undo the browser does not have.

    /** Roll a whole new patch. */
    void randomizePatch();
    /** Nudge the current patch instead of replacing it. */
    void mutatePatch (float amount = 0.15f);

    bool canUndoRandomize() const { return ! undoSnapshot.isEmpty(); }
    void undoRandomize();

    /** What the last roll produced, for the editor's status line ("Rolled: Texture"). */
    juce::String getLastRollDescription() const { return lastRoll; }

private:
    void timerCallback() override;
    void handleMidiMessage (const juce::MidiMessage& m);

    /** What a built table depends on. Anything else can be changed without rebuilding. */
    struct TableRequest
    {
        int table = 0;
        int specMode = 0;
        float spec = 0.0f;
        bool operator!= (const TableRequest& o) const
        {
            return table != o.table || specMode != o.specMode || spec != o.spec;
        }
    };

    TableRequest currentRequest (int osc) const;
    void buildAndPublish (int osc, TableRequest req);

    juce::AudioProcessorValueTreeState apvts;
    spectra::SpectraEngine engine;

    // Building a table costs ~11 ms, so it never happens on the audio thread. The web version
    // has the same split -- tables are built on the main thread and posted to the worklet.
    juce::ThreadPool buildPool { 1 };
    TableRequest pendingRequest[2], builtRequest[2];
    std::atomic<bool> building[2] { { false }, { false } };
    uint32_t lastChangeMs[2] { 0, 0 };

    juce::CriticalSection displayLock;
    std::vector<std::vector<float>> displayFrames[2];

    /** Every parameter's normalised value as it stood immediately before the last roll. Empty
     *  means there is nothing to undo. One level deep, matching the web build's single-step
     *  undo. */
    juce::Array<float> undoSnapshot;
    void takeUndoSnapshot();
    /** True for the performance settings that no roll may touch -- the C++ side of
     *  RANDOMIZE_EXCLUDE in js/randomize.js. */
    static bool isExcludedFromRolls (const juce::String& paramID);

    spectra::Rng rollRng { uint32_t (juce::Random::getSystemRandom().nextInt()) };
    juce::String lastRoll;

    int currentProgram = 0;
    float modWheel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraAudioProcessor)
};
