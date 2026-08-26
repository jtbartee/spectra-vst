#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace spectra;

SpectraAudioProcessor::SpectraAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SPECTRA", spectra::createParameterLayout())
{
    // Build both tables up front and synchronously: it costs ~11 ms once, and it means the
    // plugin is playable the instant it is loaded rather than starting on a placeholder sine.
    for (int osc = 0; osc < 2; ++osc)
    {
        const TableRequest req = currentRequest (osc);
        buildAndPublish (osc, req);
        pendingRequest[osc] = builtRequest[osc] = req;
    }

    startTimerHz (30);
}

SpectraAudioProcessor::~SpectraAudioProcessor()
{
    stopTimer();
    // Jobs capture `this`, so none may outlive the processor.
    buildPool.removeAllJobs (true, 5000);
}

void SpectraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.setParams (spectra::readParams (apvts));
    engine.setWheel (modWheel);

    // Rebuild for the new sample rate's engine instance, which starts on a placeholder table.
    for (int osc = 0; osc < 2; ++osc)
    {
        const TableRequest req = currentRequest (osc);
        buildAndPublish (osc, req);
        pendingRequest[osc] = builtRequest[osc] = req;
    }

    // The master compressor delays the signal against its own detector, as Web Audio's
    // DynamicsCompressorNode does. That is real latency, so the host is told about it.
    setLatencySamples (juce::roundToInt (double (spectra::Compressor::kLookaheadSeconds) * sampleRate));
}

bool SpectraAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo out only: unison spread, per-oscillator pan and the chorus are all stereo.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// -- wavetable rebuilds ------------------------------------------------------

SpectraAudioProcessor::TableRequest SpectraAudioProcessor::currentRequest (int osc) const
{
    auto value = [this] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        return p != nullptr ? p->load() : 0.0f;
    };

    TableRequest req;
    req.table    = int (value (spectra::ids::osc (osc, spectra::ids::oscTable)));
    req.specMode = int (value (spectra::ids::osc (osc, spectra::ids::oscSpecMode)));
    req.spec     = value (spectra::ids::osc (osc, spectra::ids::oscSpec));
    return req;
}

void SpectraAudioProcessor::buildAndPublish (int osc, TableRequest req)
{
    auto frames = spectra::buildFactoryHarmFrames (req.table);
    auto warped = spectra::spectralWarp (frames, spectra::SpecMode (req.specMode), req.spec);
    auto table = spectra::buildMips (warped);

    {
        const juce::ScopedLock sl (displayLock);
        displayFrames[osc] = table->displayFrames;
    }
    engine.publishTable (osc, std::move (table));
}

void SpectraAudioProcessor::timerCallback()
{
    const uint32_t now = juce::Time::getMillisecondCounter();

    for (int osc = 0; osc < 2; ++osc)
    {
        const TableRequest req = currentRequest (osc);
        if (req != pendingRequest[osc])
        {
            pendingRequest[osc] = req;
            lastChangeMs[osc] = now;
        }

        // Debounced by 90 ms, the same settle the web version uses, so dragging the Spectral
        // knob does not queue a rebuild per frame.
        if (pendingRequest[osc] != builtRequest[osc]
            && ! building[osc].load()
            && now - lastChangeMs[osc] >= 90)
        {
            builtRequest[osc] = pendingRequest[osc];
            building[osc] = true;
            const TableRequest job = builtRequest[osc];
            buildPool.addJob ([this, osc, job]
            {
                buildAndPublish (osc, job);
                building[osc] = false;
            });
        }
    }
}

void SpectraAudioProcessor::getDisplayFrame (int osc, float pos, std::vector<float>& out) const
{
    out.clear();
    if (osc < 0 || osc > 1) return;

    const juce::ScopedLock sl (displayLock);
    const auto& frames = displayFrames[osc];
    if (frames.empty()) return;

    const float fpos = juce::jlimit (0.0f, 1.0f, pos) * float (frames.size() - 1);
    out = frames[size_t (juce::roundToInt (fpos))];
}

// -- programs ----------------------------------------------------------------

int SpectraAudioProcessor::getNumPrograms()
{
    return int (spectra::factoryPresets().size());
}

const juce::String SpectraAudioProcessor::getProgramName (int index)
{
    const auto& all = spectra::factoryPresets();
    if (index < 0 || index >= int (all.size())) return {};
    return juce::String (all[size_t (index)].name);
}

void SpectraAudioProcessor::setCurrentProgram (int index)
{
    const auto& all = spectra::factoryPresets();
    if (index < 0 || index >= int (all.size())) return;

    // A no-op when the index has not moved: hosts call this with 0 while restoring a session,
    // and applying the preset there would wipe the state about to be loaded.
    if (index == currentProgram) return;

    currentProgram = index;
    spectra::writeParams (apvts, all[size_t (index)]);
    // The timer picks up the table change and rebuilds off the audio thread.
}

// -- MIDI and rendering ------------------------------------------------------

void SpectraAudioProcessor::handleMidiMessage (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
    {
        // The web version has no pitch-bend path; the plugin routes the wheel to the mod
        // matrix's Mod Wheel source instead, which is where the engine already reads it.
    }
    else if (m.isController() && m.getControllerNumber() == 1)
    {
        modWheel = float (m.getControllerValue()) / 127.0f;
        engine.setWheel (modWheel);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.panic();
}

void SpectraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    engine.setParams (spectra::readParams (apvts));

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    // Split the block at each MIDI event so note timing is sample-accurate.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int eventTime = juce::jlimit (0, numSamples, meta.samplePosition);
        if (eventTime > pos)
        {
            engine.render (left + pos, right + pos, eventTime - pos);
            pos = eventTime;
        }
        handleMidiMessage (meta.getMessage());
    }
    if (pos < numSamples)
        engine.render (left + pos, right + pos, numSamples - pos);
}

juce::AudioProcessorEditor* SpectraAudioProcessor::createEditor()
{
    return new SpectraAudioProcessorEditor (*this);
}

void SpectraAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectraAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    // The timer notices the restored table selection and rebuilds.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectraAudioProcessor();
}
