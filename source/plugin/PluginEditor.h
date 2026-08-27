#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/** The dark-slate-and-cyan look from the web version's style.css. */
class SpectraLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SpectraLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    static const juce::Colour bg, panel, panelEdge, text, dim, accent, accent2, warn;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
          const juce::String& displayName, juce::Colour ring);
    void resized() override;

    static constexpr int prefW = 66, prefH = 76;

private:
    juce::Slider slider;
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A toggle drawn as a small glowing pill, matching the web UI's switches. */
class Toggle : public juce::Button
{
public:
    Toggle (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
            const juce::String& displayName, juce::Colour colour);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    static constexpr int prefW = 116, prefH = 22;

private:
    juce::Colour colour;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

class Selector : public juce::Component
{
public:
    Selector (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
              const juce::String& displayName, const juce::StringArray& choices);
    void resized() override;

    static constexpr int prefW = 116, prefH = 38;

private:
    juce::ComboBox box;
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Draws the current morph frame of one oscillator's wavetable. This is the control the whole
 *  instrument is organised around, so it gets real estate rather than a readout. */
class WavetableDisplay : public juce::Component
{
public:
    WavetableDisplay (SpectraAudioProcessor&, int oscIndex, juce::Colour trace);
    void refresh (float pos);
    void paint (juce::Graphics&) override;

private:
    SpectraAudioProcessor& processor;
    int osc;
    juce::Colour trace;
    std::vector<float> frame;
};

/** One row of the mod matrix: source, destination, amount. */
class ModRow : public juce::Component
{
public:
    ModRow (juce::AudioProcessorValueTreeState&, int slot);
    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int prefW = 320, prefH = 46;

private:
    int slot;
    juce::ComboBox srcBox, dstBox;
    juce::Slider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcAttach, dstAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amtAttach;
};

/** A titled panel section: knobs flow into rows, wide controls stack underneath. */
class Section : public juce::Component
{
public:
    Section (const juce::String& title, juce::Colour accent, int knobsPerRow);

    void addKnob (juce::Component*);    // not owned
    void addWide (juce::Component*);    // not owned
    void setHeader (juce::Component*);  // not owned; sits directly under the title

    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredWidth() const;
    int preferredHeight() const;

    static constexpr int titleH = 20, pad = 9, gap = 5;

private:
    juce::String title;
    juce::Colour accent;
    int knobsPerRow;
    juce::Array<juce::Component*> knobs, wide;
    juce::Component* header = nullptr;
    int headerHeight = 0;
};

class VoiceLeds : public juce::Component
{
public:
    void setMask (uint32_t m);
    void paint (juce::Graphics&) override;

private:
    uint32_t mask = 0;
};

class SpectraAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit SpectraAudioProcessorEditor (SpectraAudioProcessor&);
    ~SpectraAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void presetChosen();
    /** Keeps the undo button's enablement and the roll caption in step with the processor. */
    void syncRollUi();

    /** Lays the section flow out inside `area`, returning the height it needs. With
     *  `apply == false` it measures without moving anything. */
    int flowSections (juce::Rectangle<int> area, bool apply);

public:
    /** The height this editor needs at a given width, header and margins included. Public so
     *  the UI check can sweep it across the resizable width range. */
    int requiredHeightFor (int width);

    static constexpr int kHeaderHeight = 58;

private:

    SpectraAudioProcessor& processor;
    SpectraLookAndFeel lookAndFeel;

    juce::ComboBox presetBox;
    juce::Label presetLabel, voicesLabel;
    VoiceLeds voiceLeds;

    // The web version's RANDOM / MUTATE pair, plus an undo the browser build does not have.
    juce::TextButton randomButton, mutateButton, undoButton;
    juce::Label diceLabel, rollLabel;
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<WavetableDisplay> displays[2];

    // The panels live in a scrolling viewport, the way the web version's panel grid scrolls in
    // the browser. The section flow is a step function of width -- it needs 1081px of height at
    // 1240 wide but only 831 at 1600 -- so a fixed minimum window height either clips the bottom
    // row or forces an absurdly tall floor. Scrolling sidesteps both.
    juce::Viewport panelView;
    juce::Component panelHolder;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Toggle> toggles;
    juce::OwnedArray<Selector> selectors;
    juce::OwnedArray<ModRow> modRows;
    juce::OwnedArray<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraAudioProcessorEditor)
};
