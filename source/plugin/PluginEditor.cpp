#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

const juce::Colour SpectraLookAndFeel::bg        { 0xff0d0e12 };
const juce::Colour SpectraLookAndFeel::panel     { 0xff16181f };
const juce::Colour SpectraLookAndFeel::panelEdge { 0xff23262f };
const juce::Colour SpectraLookAndFeel::text      { 0xffc9cdd8 };
const juce::Colour SpectraLookAndFeel::dim       { 0xff7a8095 };
const juce::Colour SpectraLookAndFeel::accent    { 0xff5edeff };
const juce::Colour SpectraLookAndFeel::accent2   { 0xff7c5cff };
const juce::Colour SpectraLookAndFeel::warn      { 0xffff5e7a };

SpectraLookAndFeel::SpectraLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId,            dim);
    setColour (juce::Slider::textBoxTextColourId,    text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::trackColourId,          accent);
    setColour (juce::Slider::backgroundColourId,     panelEdge);
    setColour (juce::ComboBox::backgroundColourId,   bg);
    setColour (juce::ComboBox::textColourId,         text);
    setColour (juce::ComboBox::outlineColourId,      panelEdge);
    setColour (juce::ComboBox::arrowColourId,        accent);
    setColour (juce::PopupMenu::backgroundColourId,  panel);
    setColour (juce::PopupMenu::textColourId,        text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, panelEdge);
    setColour (juce::PopupMenu::highlightedTextColourId, accent);
}

void SpectraLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                           float sliderPos, float startAngle, float endAngle,
                                           juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float track = radius * 0.16f;
    const auto ring = s.findColour (juce::Slider::trackColourId);

    juce::Path bg_;
    bg_.addCentredArc (cx, cy, radius - track * 0.5f, radius - track * 0.5f, 0.0f,
                       startAngle, endAngle, true);
    g.setColour (panelEdge);
    g.strokePath (bg_, juce::PathStrokeType (track, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Bipolar controls fill outward from the centre; unipolar ones from the start.
    const bool bipolar = s.getMinimum() < -0.0001 && s.getMaximum() > 0.0001
                      && std::abs (s.getMinimum() + s.getMaximum()) < 0.0001;
    const float from = bipolar ? startAngle + (endAngle - startAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path fill;
        fill.addCentredArc (cx, cy, radius - track * 0.5f, radius - track * 0.5f, 0.0f,
                            juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (ring);
        g.strokePath (fill, juce::PathStrokeType (track, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    g.setColour (panel.brighter (0.06f));
    g.fillEllipse (cx - radius * 0.66f, cy - radius * 0.66f, radius * 1.32f, radius * 1.32f);

    juce::Path pointer;
    const float len = radius * 0.58f, thickness = 2.0f;
    pointer.addRoundedRectangle (-thickness * 0.5f, -len, thickness, len * 0.9f, thickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (cx, cy));
    g.setColour (text);
    g.fillPath (pointer);
}

// ---------------------------------------------------------------------------

Knob::Knob (APVTS& apvts, const juce::String& paramID, const juce::String& displayName,
            juce::Colour ring)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 13);
    slider.setColour (juce::Slider::trackColourId, ring);
    addAndMakeVisible (slider);

    caption.setText (displayName, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (juce::FontOptions (9.5f));
    caption.setColour (juce::Label::textColourId, SpectraLookAndFeel::dim);
    addAndMakeVisible (caption);

    attachment = std::make_unique<APVTS::SliderAttachment> (apvts, paramID, slider);

    if (auto* param = apvts.getParameter (paramID))
        slider.setDoubleClickReturnValue (true, double (param->convertFrom0to1 (param->getDefaultValue())));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromBottom (11));
    slider.setBounds (r);
}

// ---------------------------------------------------------------------------

Toggle::Toggle (APVTS& apvts, const juce::String& paramID, const juce::String& displayName,
                juce::Colour c)
    : juce::Button (displayName), colour (c)
{
    setClickingTogglesState (true);
    setButtonText (displayName);
    attachment = std::make_unique<APVTS::ButtonAttachment> (apvts, paramID, *this);
}

void Toggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();

    g.setColour (on ? colour.withAlpha (0.16f)
                    : SpectraLookAndFeel::bg.brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? colour : SpectraLookAndFeel::panelEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    g.setColour (on ? colour : SpectraLookAndFeel::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (getButtonText(), r, juce::Justification::centred, true);
}

// ---------------------------------------------------------------------------

Selector::Selector (APVTS& apvts, const juce::String& paramID, const juce::String& displayName,
                    const juce::StringArray& choices)
{
    box.addItemList (choices, 1);
    addAndMakeVisible (box);

    caption.setText (displayName, juce::dontSendNotification);
    caption.setFont (juce::FontOptions (9.0f));
    caption.setColour (juce::Label::textColourId, SpectraLookAndFeel::dim.darker (0.2f));
    addAndMakeVisible (caption);

    attachment = std::make_unique<APVTS::ComboBoxAttachment> (apvts, paramID, box);
}

void Selector::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (13));
    box.setBounds (r.reduced (0, 1));
}

// ---------------------------------------------------------------------------

WavetableDisplay::WavetableDisplay (SpectraAudioProcessor& p, int oscIndex, juce::Colour t)
    : processor (p), osc (oscIndex), trace (t) {}

void WavetableDisplay::refresh (float pos)
{
    std::vector<float> next;
    processor.getDisplayFrame (osc, pos, next);
    if (next == frame) return;
    frame = std::move (next);
    repaint();
}

void WavetableDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (SpectraLookAndFeel::bg);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (SpectraLookAndFeel::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

    g.setColour (SpectraLookAndFeel::panelEdge);
    g.drawLine (r.getX() + 4.0f, r.getCentreY(), r.getRight() - 4.0f, r.getCentreY(), 1.0f);

    if (frame.size() < 2) return;

    juce::Path path;
    const float w = r.getWidth() - 8.0f, h = r.getHeight() * 0.42f;
    for (size_t i = 0; i < frame.size(); ++i)
    {
        const float x = r.getX() + 4.0f + w * float (i) / float (frame.size() - 1);
        const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, frame[i]) * h;
        if (i == 0) path.startNewSubPath (x, y);
        else path.lineTo (x, y);
    }

    g.setColour (trace.withAlpha (0.25f));
    g.strokePath (path, juce::PathStrokeType (3.0f));
    g.setColour (trace);
    g.strokePath (path, juce::PathStrokeType (1.4f));
}

// ---------------------------------------------------------------------------

ModRow::ModRow (APVTS& apvts, int slotIndex) : slot (slotIndex)
{
    srcBox.addItemList (spectra::modSourceNames(), 1);
    dstBox.addItemList (spectra::modDestNames(), 1);
    addAndMakeVisible (srcBox);
    addAndMakeVisible (dstBox);

    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16);
    addAndMakeVisible (amount);

    srcAttach = std::make_unique<APVTS::ComboBoxAttachment> (
        apvts, spectra::ids::mod (slot, spectra::ids::modSrc), srcBox);
    dstAttach = std::make_unique<APVTS::ComboBoxAttachment> (
        apvts, spectra::ids::mod (slot, spectra::ids::modDst), dstBox);
    amtAttach = std::make_unique<APVTS::SliderAttachment> (
        apvts, spectra::ids::mod (slot, spectra::ids::modAmt), amount);
}

void ModRow::paint (juce::Graphics& g)
{
    g.setColour (SpectraLookAndFeel::dim.darker (0.3f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (juce::String (slot + 1), getLocalBounds().removeFromLeft (14),
                juce::Justification::centredLeft);
}

void ModRow::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (14);
    auto top = r.removeFromTop (22);
    srcBox.setBounds (top.removeFromLeft (140).reduced (1));
    top.removeFromLeft (4);
    dstBox.setBounds (top.reduced (1));
    amount.setBounds (r.reduced (1, 2));
}

// ---------------------------------------------------------------------------

Section::Section (const juce::String& t, juce::Colour a, int perRow)
    : title (t), accent (a), knobsPerRow (juce::jmax (1, perRow)) {}

void Section::addKnob (juce::Component* c) { knobs.add (c); addAndMakeVisible (c); }
void Section::addWide (juce::Component* c) { wide.add (c);  addAndMakeVisible (c); }

void Section::setHeader (juce::Component* c)
{
    header = c;
    headerHeight = 56;
    addAndMakeVisible (c);
}

int Section::preferredWidth() const
{
    int w = knobs.isEmpty() ? 0 : pad * 2 + juce::jmin (knobsPerRow, knobs.size()) * Knob::prefW;
    for (auto* c : wide)
        w = juce::jmax (w, pad * 2 + (dynamic_cast<ModRow*> (c) != nullptr ? ModRow::prefW : Toggle::prefW));
    return juce::jmax (w, 132);
}

int Section::preferredHeight() const
{
    int h = titleH + pad;
    if (header != nullptr) h += headerHeight + gap;
    if (! knobs.isEmpty())
        h += ((knobs.size() + knobsPerRow - 1) / knobsPerRow) * Knob::prefH;
    for (auto* c : wide)
    {
        if (dynamic_cast<ModRow*> (c) != nullptr) h += ModRow::prefH + gap;
        else if (dynamic_cast<Selector*> (c) != nullptr) h += Selector::prefH + gap;
        else h += Toggle::prefH + gap;
    }
    return h + pad;
}

void Section::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (SpectraLookAndFeel::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (SpectraLookAndFeel::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto titleArea = getLocalBounds().removeFromTop (titleH);
    g.setColour (accent);
    g.fillRoundedRectangle (float (pad), 7.0f, 3.0f, 10.0f, 1.5f);
    g.setColour (SpectraLookAndFeel::text);
    g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    g.drawText (title.toUpperCase(), titleArea.reduced (pad + 9, 0), juce::Justification::centredLeft);
}

void Section::resized()
{
    auto area = getLocalBounds().reduced (pad, 0);
    area.removeFromTop (titleH);

    if (header != nullptr)
    {
        header->setBounds (area.removeFromTop (headerHeight));
        area.removeFromTop (gap);
    }

    int index = 0;
    while (index < knobs.size())
    {
        auto row = area.removeFromTop (Knob::prefH);
        for (int i = 0; i < knobsPerRow && index < knobs.size(); ++i, ++index)
            knobs[index]->setBounds (row.removeFromLeft (Knob::prefW));
    }

    for (auto* c : wide)
    {
        const int hgt = dynamic_cast<ModRow*> (c) != nullptr ? ModRow::prefH
                      : dynamic_cast<Selector*> (c) != nullptr ? Selector::prefH
                                                               : Toggle::prefH;
        auto row = area.removeFromTop (hgt);
        if (dynamic_cast<ModRow*> (c) != nullptr) c->setBounds (row);
        else c->setBounds (row.withWidth (juce::jmin (row.getWidth(), Toggle::prefW)));
        area.removeFromTop (gap);
    }
}

// ---------------------------------------------------------------------------

void VoiceLeds::setMask (uint32_t m)
{
    if (m == mask) return;
    mask = m;
    repaint();
}

void VoiceLeds::paint (juce::Graphics& g)
{
    const float d = 6.0f, spacing = 9.0f;
    const float y = float (getHeight()) * 0.5f - d * 0.5f;
    for (int i = 0; i < spectra::kMaxVoices; ++i)
    {
        const bool on = (mask & (1u << uint32_t (i))) != 0;
        const float x = float (i) * spacing;
        if (on)
        {
            g.setColour (SpectraLookAndFeel::accent.withAlpha (0.3f));
            g.fillEllipse (x - 2.0f, y - 2.0f, d + 4.0f, d + 4.0f);
        }
        g.setColour (on ? SpectraLookAndFeel::accent : SpectraLookAndFeel::panelEdge);
        g.fillEllipse (x, y, d, d);
    }
}

// ---------------------------------------------------------------------------

SpectraAudioProcessorEditor::SpectraAudioProcessorEditor (SpectraAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = processor.getValueTree();
    using LF = SpectraLookAndFeel;

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setSelectedItemIndex (processor.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this] { presetChosen(); };
    addAndMakeVisible (presetBox);

    auto styleTag = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::FontOptions (9.0f).withStyle ("Bold"));
        l.setColour (juce::Label::textColourId, SpectraLookAndFeel::accent);
        addAndMakeVisible (l);
    };
    styleTag (presetLabel, "PRESET");
    styleTag (voicesLabel, "VOICES");
    addAndMakeVisible (voiceLeds);

    auto makeSection = [this] (const juce::String& title, juce::Colour a, int perRow) -> Section*
    {
        auto* s = new Section (title, a, perRow);
        sections.add (s);
        addAndMakeVisible (s);
        return s;
    };
    auto addKnob = [this, &apvts] (Section* s, const juce::String& id, const juce::String& name,
                                   juce::Colour c)
    {
        auto* k = new Knob (apvts, id, name, c);
        knobs.add (k);
        s->addKnob (k);
    };
    auto addToggle = [this, &apvts] (Section* s, const juce::String& id, const juce::String& name,
                                     juce::Colour c)
    {
        auto* t = new Toggle (apvts, id, name, c);
        toggles.add (t);
        s->addWide (t);
    };
    auto addSelector = [this, &apvts] (Section* s, const juce::String& id, const juce::String& name,
                                       const juce::StringArray& choices)
    {
        auto* sel = new Selector (apvts, id, name, choices);
        selectors.add (sel);
        s->addWide (sel);
    };

    // -- oscillators, each fronted by its wavetable display --
    for (int o = 0; o < 2; ++o)
    {
        const juce::Colour c = (o == 0) ? LF::accent : LF::accent2;
        auto* sec = makeSection (o == 0 ? "Oscillator 1" : "Oscillator 2", c, 4);

        displays[o] = std::make_unique<WavetableDisplay> (processor, o, c);
        sec->setHeader (displays[o].get());

        addSelector (sec, spectra::ids::osc (o, spectra::ids::oscTable), "WAVETABLE", spectra::tableNames());
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscPos), "Position", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscWarp), "Warp", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscSpec), "Spec", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscLevel), "Level", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscUnison), "Unison", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscDetune), "Detune", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscSpread), "Spread", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscBlend), "Blend", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscSemi), "Semi", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscFine), "Fine", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscRand), "Ph Rnd", c);
        addKnob (sec, spectra::ids::osc (o, spectra::ids::oscPan), "Pan", c);
        addSelector (sec, spectra::ids::osc (o, spectra::ids::oscWarpMode), "WARP", spectra::warpModeNames());
        addSelector (sec, spectra::ids::osc (o, spectra::ids::oscSpecMode), "SPECTRAL", spectra::specModeNames());
        addToggle (sec, spectra::ids::osc (o, spectra::ids::oscOn), o == 0 ? "Osc 1" : "Osc 2", c);
    }

    auto* subSec = makeSection ("Sub & Noise", LF::accent2, 2);
    addKnob (subSec, spectra::ids::subLevel, "Sub", LF::accent2);
    addKnob (subSec, spectra::ids::noiseLevel, "Noise", LF::accent2);
    addSelector (subSec, spectra::ids::subOct, "SUB OCT", juce::StringArray { "-1 Oct", "-2 Oct" });
    addSelector (subSec, spectra::ids::subShape, "SUB SHAPE", juce::StringArray { "Sine", "Triangle", "Square" });

    auto* filter = makeSection ("Filter", LF::accent, 3);
    addKnob (filter, spectra::ids::fltCut, "Cutoff", LF::accent);
    addKnob (filter, spectra::ids::fltRes, "Reso", LF::accent);
    addKnob (filter, spectra::ids::fltDrive, "Drive", LF::accent);
    addKnob (filter, spectra::ids::fltEnv, "Env 2", LF::accent);
    addKnob (filter, spectra::ids::fltKey, "Keytrack", LF::accent);
    addSelector (filter, spectra::ids::fltType, "TYPE", spectra::filterTypeNames());
    addToggle (filter, spectra::ids::fltOn, "Filter", LF::accent);

    auto* env1 = makeSection ("Env 1 / Amp", LF::accent, 4);
    addKnob (env1, spectra::ids::env1A, "A", LF::accent);
    addKnob (env1, spectra::ids::env1D, "D", LF::accent);
    addKnob (env1, spectra::ids::env1S, "S", LF::accent);
    addKnob (env1, spectra::ids::env1R, "R", LF::accent);

    auto* env2 = makeSection ("Env 2 / Mod", LF::accent2, 4);
    addKnob (env2, spectra::ids::env2A, "A", LF::accent2);
    addKnob (env2, spectra::ids::env2D, "D", LF::accent2);
    addKnob (env2, spectra::ids::env2S, "S", LF::accent2);
    addKnob (env2, spectra::ids::env2R, "R", LF::accent2);

    auto* lfo1 = makeSection ("LFO 1", LF::accent2, 1);
    addKnob (lfo1, spectra::ids::lfo1Rate, "Rate", LF::accent2);
    addSelector (lfo1, spectra::ids::lfo1Shape, "SHAPE", spectra::lfoShapeNames());
    addToggle (lfo1, spectra::ids::lfo1Retrig, "Retrig", LF::accent2);

    auto* lfo2 = makeSection ("LFO 2", LF::accent2, 1);
    addKnob (lfo2, spectra::ids::lfo2Rate, "Rate", LF::accent2);
    addSelector (lfo2, spectra::ids::lfo2Shape, "SHAPE", spectra::lfoShapeNames());
    addToggle (lfo2, spectra::ids::lfo2Retrig, "Retrig", LF::accent2);

    auto* matrix = makeSection ("Mod Matrix", LF::warn, 1);
    for (int m = 0; m < 4; ++m)
    {
        auto* row = new ModRow (apvts, m);
        modRows.add (row);
        matrix->addWide (row);
    }

    auto* global = makeSection ("Global", LF::text, 4);
    addKnob (global, spectra::ids::glide, "Glide", LF::text);
    addKnob (global, spectra::ids::poly, "Voices", LF::text);
    addKnob (global, spectra::ids::velSens, "Vel Sens", LF::text);
    addKnob (global, spectra::ids::master, "Master", LF::text);

    auto* fx = makeSection ("FX", LF::accent, 4);
    addKnob (fx, spectra::ids::fxDist, "Distort", LF::accent);
    addKnob (fx, spectra::ids::fxChorRate, "Ch Rate", LF::accent);
    addKnob (fx, spectra::ids::fxChorDepth, "Ch Depth", LF::accent);
    addKnob (fx, spectra::ids::fxChorMix, "Ch Mix", LF::accent);
    addKnob (fx, spectra::ids::fxDlyTime, "Dly Time", LF::accent);
    addKnob (fx, spectra::ids::fxDlyFb, "Dly FB", LF::accent);
    addKnob (fx, spectra::ids::fxDlyMix, "Dly Mix", LF::accent);
    addKnob (fx, spectra::ids::fxRevSize, "Rev Size", LF::accent);
    addKnob (fx, spectra::ids::fxRevMix, "Rev Mix", LF::accent);

    startTimerHz (20);

    setResizable (true, true);
    setResizeLimits (940, 640, 2000, 1500);
    setSize (1240, 860);
}

SpectraAudioProcessorEditor::~SpectraAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SpectraAudioProcessorEditor::presetChosen()
{
    const int index = presetBox.getSelectedItemIndex();
    if (index < 0 || index == processor.getCurrentProgram()) return;
    processor.setCurrentProgram (index);
}

void SpectraAudioProcessorEditor::timerCallback()
{
    voiceLeds.setMask (processor.getVoiceActiveMask());

    auto& apvts = processor.getValueTree();
    for (int o = 0; o < 2; ++o)
        if (displays[o] != nullptr)
            if (auto* pos = apvts.getRawParameterValue (spectra::ids::osc (o, spectra::ids::oscPos)))
                displays[o]->refresh (pos->load());

    const int program = processor.getCurrentProgram();
    if (presetBox.getSelectedItemIndex() != program)
        presetBox.setSelectedItemIndex (program, juce::dontSendNotification);
}

void SpectraAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (SpectraLookAndFeel::bg);

    auto header = getLocalBounds().removeFromTop (58).toFloat();
    g.setColour (SpectraLookAndFeel::panel);
    g.fillRect (header);
    g.setColour (SpectraLookAndFeel::accent.withAlpha (0.5f));
    g.drawLine (0.0f, header.getBottom(), header.getWidth(), header.getBottom(), 1.0f);

    g.setColour (SpectraLookAndFeel::text);
    g.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
    g.drawText ("SPECTRA", 18, 10, 180, 26, juce::Justification::centredLeft);

    g.setColour (SpectraLookAndFeel::dim);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("WAVETABLE SYNTHESIZER", 20, 34, 260, 14, juce::Justification::centredLeft);
}

void SpectraAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (58);

    auto right = header.removeFromRight (460).reduced (8, 12);
    auto voiceCol = right.removeFromRight (150);
    voicesLabel.setBounds (voiceCol.removeFromTop (12));
    voiceLeds.setBounds (voiceCol.removeFromTop (16));

    right.removeFromRight (14);
    auto presetCol = right.removeFromRight (260);
    presetLabel.setBounds (presetCol.removeFromTop (12));
    presetBox.setBounds (presetCol.removeFromTop (24));

    area.reduce (10, 8);

    // Flow the sections left to right, wrapping when the next one will not fit.
    int x = area.getX(), y = area.getY(), rowHeight = 0;
    for (auto* s : sections)
    {
        const int w = s->preferredWidth(), h = s->preferredHeight();
        if (x + w > area.getRight() && x > area.getX())
        {
            x = area.getX();
            y += rowHeight + 8;
            rowHeight = 0;
        }
        s->setBounds (x, y, w, h);
        x += w + 8;
        rowHeight = juce::jmax (rowHeight, h);
    }
}
