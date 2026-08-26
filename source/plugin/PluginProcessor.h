#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "../dsp/SpectraEngine.h"

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

    int currentProgram = 0;
    float modWheel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraAudioProcessor)
};
