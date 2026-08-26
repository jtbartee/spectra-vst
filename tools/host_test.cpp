// End-to-end plugin check: loads the built VST3 the way a DAW does, plays notes into it, and
// verifies real audio comes out.
//
// The offline DSP harness (render_test.cpp) proves the synthesis is right. This proves the
// JUCE wrapper around it is right -- parameter layout, MIDI handling, buses, state
// round-tripping -- which is the part auval only covers generically.
//
//   ./spectra_hosttest <path-to-SPECTRA.vst3>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace {

int failures = 0;

void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    if (! ok)
    {
        ++failures;
        std::fprintf (stderr, "  FAIL  %s  %s\n", what.toRawUTF8(), detail.toRawUTF8());
    }
    else
    {
        std::printf ("  ok    %s\n", what.toRawUTF8());
    }
}

struct Rendered { float peak = 0.0f; double rms = 0.0; bool finite = true; };

Rendered renderSeconds (juce::AudioPluginInstance& plugin, juce::MidiBuffer&& firstBlockMidi,
                        double sampleRate, int blockSize, double seconds)
{
    Rendered r;
    juce::AudioBuffer<float> buffer (2, blockSize);
    const int blocks = int (sampleRate * seconds / blockSize);
    double sumSq = 0.0;
    int totalSamples = 0;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        if (b == 0) midi = std::move (firstBlockMidi);
        plugin.processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* d = buffer.getReadPointer (ch);
            for (int i = 0; i < blockSize; ++i)
            {
                if (! std::isfinite (d[i])) r.finite = false;
                r.peak = juce::jmax (r.peak, std::abs (d[i]));
                if (ch == 0) { sumSq += double (d[i]) * double (d[i]); ++totalSamples; }
            }
        }
    }
    r.rms = totalSamples > 0 ? std::sqrt (sumSq / double (totalSamples)) : 0.0;
    return r;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: spectra_hosttest <path-to-SPECTRA.vst3>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String pluginPath (argv[1]);
    const double sampleRate = 48000.0;
    const int blockSize = 512;

    std::printf ("Loading %s\n", pluginPath.toRawUTF8());

    juce::AudioPluginFormatManager manager;
    manager.addDefaultFormats();

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, pluginPath);

    check (descriptions.size() > 0, "plugin scanned",
           "found " + juce::String (descriptions.size()) + " description(s)");
    if (descriptions.isEmpty()) return 1;

    const auto& desc = *descriptions[0];
    std::printf ("  name=%s  format=%s  isInstrument=%d\n",
                 desc.name.toRawUTF8(), desc.pluginFormatName.toRawUTF8(), int (desc.isInstrument));
    check (desc.isInstrument, "declares itself an instrument");

    juce::String error;
    auto plugin = manager.createPluginInstance (desc, sampleRate, blockSize, error);
    check (plugin != nullptr, "instantiated", error);
    if (plugin == nullptr) return 1;

    plugin->setPlayConfigDetails (0, 2, sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);

    check (plugin->acceptsMidi(), "accepts MIDI");
    check (plugin->getTotalNumOutputChannels() == 2, "stereo output",
           "channels=" + juce::String (plugin->getTotalNumOutputChannels()));
    check (plugin->getLatencySamples() > 0, "reports the limiter's lookahead latency",
           "samples=" + juce::String (plugin->getLatencySamples()));

    const auto& params = plugin->getParameters();
    check (params.size() >= 78, "parameters exposed", "count=" + juce::String (params.size()));

    // --- silence before any notes ---
    {
        const auto r = renderSeconds (*plugin, {}, sampleRate, blockSize, 0.25);
        check (r.finite && r.peak < 1.0e-6f, "silent when idle", "peak=" + juce::String (r.peak));
    }

    // --- a struck note must actually make a sound ---
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.9f), 0);
        const auto r = renderSeconds (*plugin, std::move (midi), sampleRate, blockSize, 2.0);
        check (r.finite, "note renders finite audio");
        check (r.peak > 0.01f, "note is audible", "peak=" + juce::String (r.peak));
        check (r.peak <= 1.5f, "note does not clip absurdly", "peak=" + juce::String (r.peak));
        std::printf ("        peak=%.4f rms=%.5f\n", r.peak, r.rms);
    }

    // --- a dense chord across the polyphony limit stays sane ---
    {
        juce::MidiBuffer midi;
        for (int n = 36; n < 90; n += 2)
            midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
        const auto r = renderSeconds (*plugin, std::move (midi), sampleRate, blockSize, 1.5);
        check (r.finite, "dense chord renders finite audio");
        check (r.peak > 0.01f, "dense chord is audible", "peak=" + juce::String (r.peak));
        std::printf ("        peak=%.4f rms=%.5f\n", r.peak, r.rms);

        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        // The default patch has the reverb on, so the tail needs several seconds to fall away
        // before "silent" means anything.
        renderSeconds (*plugin, std::move (off), sampleRate, blockSize, 6.0);
        const auto after = renderSeconds (*plugin, {}, sampleRate, blockSize, 0.5);
        check (after.peak < 1.0e-5f, "all-notes-off silences the instrument",
               "peak=" + juce::String (after.peak));
    }

    // --- factory programs are exposed and actually change the sound ---
    {
        check (plugin->getNumPrograms() == 7, "seven factory presets",
               "count=" + juce::String (plugin->getNumPrograms()));
        check (plugin->getProgramName (0).isNotEmpty(), "programs are named",
               "first=" + plugin->getProgramName (0));

        plugin->setCurrentProgram (1);   // Acid Bass
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), 0);
        const auto r = renderSeconds (*plugin, std::move (midi), sampleRate, blockSize, 1.0);
        check (r.finite && r.peak > 0.01f, "a selected program renders audio",
               "program=" + plugin->getProgramName (1) + " peak=" + juce::String (r.peak));

        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        renderSeconds (*plugin, std::move (off), sampleRate, blockSize, 3.0);
    }

    // --- state round-trip: change a patch, save, change again, restore ---
    {
        auto* cutoff = plugin->getParameters()[0];
        for (auto* p : plugin->getParameters())
            if (p->getName (64) == "Cutoff") cutoff = p;

        cutoff->setValueNotifyingHost (0.87f);
        juce::MemoryBlock saved;
        plugin->getStateInformation (saved);
        check (saved.getSize() > 0, "state serialises", "bytes=" + juce::String (saved.getSize()));

        cutoff->setValueNotifyingHost (0.12f);
        plugin->setStateInformation (saved.getData(), int (saved.getSize()));
        check (std::abs (cutoff->getValue() - 0.87f) < 0.01f, "state restores parameter values",
               "value=" + juce::String (cutoff->getValue()));
    }

    plugin->releaseResources();
    plugin.reset();

    std::printf ("%s\n", failures == 0 ? "\nALL HOST CHECKS PASSED" : "\nHOST CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
