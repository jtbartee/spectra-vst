#include "Parameters.h"

namespace spectra {

using APVTS = juce::AudioProcessorValueTreeState;

namespace ids {
juce::String osc (int index, const char* suffix)
{
    return juce::String (index == 0 ? "o1" : "o2") + suffix;
}

juce::String mod (int slot, const char* suffix)
{
    return "mod" + juce::String (slot + 1) + suffix;
}
}

juce::StringArray warpModeNames()   { return { "Off", "Bend", "Sync", "Formant", "Quantize", "Mirror", "Squeeze" }; }
juce::StringArray specModeNames()   { return { "Off", "Smooth", "Tilt", "Stretch", "Shift", "Comb", "Disperse" }; }
juce::StringArray lfoShapeNames()   { return { "Sine", "Triangle", "Saw", "Square", "S&H" }; }
juce::StringArray filterTypeNames() { return { "Lowpass", "Bandpass", "Highpass", "Notch" }; }

juce::StringArray modSourceNames()
{
    return { "Off", "LFO 1", "LFO 2", "Env 2", "Velocity", "Mod Wheel", "Note", "Random" };
}

juce::StringArray modDestNames()
{
    return { "Off", "Osc1 Pos", "Osc2 Pos", "Osc1 Warp", "Osc2 Warp", "Cutoff", "Resonance",
             "Pitch", "Amp", "Pan", "Osc1 Level", "Osc2 Level" };
}

juce::StringArray tableNames()
{
    juce::StringArray names;
    for (const auto& n : factoryTableNames()) names.add (juce::String (n));
    return names;
}

/** An exponential range matching params.js `tToValue` with scale 'exp', rather than
 *  approximating it with a JUCE skew factor -- a knob position means the same thing in the
 *  plugin as it does in the browser. */
static juce::NormalisableRange<float> expRange (float lo, float hi)
{
    return juce::NormalisableRange<float> (
        lo, hi,
        [lo, hi] (float, float, float t) { return lo * std::pow (hi / lo, t); },
        [lo, hi] (float, float, float v) { return std::log (std::max (lo, v) / lo) / std::log (hi / lo); },
        [] (float mn, float mx, float v) { return juce::jlimit (mn, mx, v); });
}

using Formatter = std::function<juce::String (float, int)>;

static juce::String hzString (float v)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, 1) + " kHz";
    return juce::String (v, v < 10.0f ? 2 : 1) + " Hz";
}

static juce::String secString (float v)
{
    if (v >= 1.0f) return juce::String (v, 2) + " s";
    return juce::String (juce::roundToInt (v * 1000.0f)) + " ms";
}

static std::unique_ptr<juce::AudioParameterFloat>
floatParam (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range,
            float def, Formatter fmt)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (fmt)));
}

APVTS::ParameterLayout createParameterLayout()
{
    APVTS::ParameterLayout layout;
    const Params d {};

    const Formatter pct   = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
    const Formatter plain = [] (float v, int) { return juce::String (v, 2); };
    const Formatter hz    = [] (float v, int) { return hzString (v); };
    const Formatter sec   = [] (float v, int) { return secString (v); };
    const Formatter cents = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ct"; };

    const juce::NormalisableRange<float> unit { 0.0f, 1.0f };
    const juce::NormalisableRange<float> bipolar { -1.0f, 1.0f };

    auto addOsc = [&] (int index, const OscParams& def_)
    {
        const juce::String label = index == 0 ? "Osc1 " : "Osc2 ";
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ids::osc (index, ids::oscOn), 1 }, label + "On", def_.on));
        layout.add (floatParam (ids::osc (index, ids::oscPos), label + "Position", unit, def_.pos,
                                [] (float v, int) { return juce::String (v, 3); }));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::osc (index, ids::oscWarpMode), 1 }, label + "Warp Mode",
            warpModeNames(), int (def_.warpMode)));
        layout.add (floatParam (ids::osc (index, ids::oscWarp), label + "Warp Amt", bipolar, def_.warp, plain));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::osc (index, ids::oscSpecMode), 1 }, label + "Spectral",
            specModeNames(), int (def_.specMode)));
        layout.add (floatParam (ids::osc (index, ids::oscSpec), label + "Spec Amt", unit, def_.spec, plain));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { ids::osc (index, ids::oscUnison), 1 }, label + "Unison", 1, 8, def_.unison));
        layout.add (floatParam (ids::osc (index, ids::oscDetune), label + "Detune",
                                { 0.0f, 100.0f }, def_.detune, cents));
        layout.add (floatParam (ids::osc (index, ids::oscSpread), label + "Spread", unit, def_.spread, plain));
        layout.add (floatParam (ids::osc (index, ids::oscBlend), label + "Blend", unit, def_.blend, plain));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { ids::osc (index, ids::oscSemi), 1 }, label + "Semi", -24, 24, def_.semi));
        layout.add (floatParam (ids::osc (index, ids::oscFine), label + "Fine",
                                { -100.0f, 100.0f }, def_.fine, cents));
        layout.add (floatParam (ids::osc (index, ids::oscRand), label + "Phase Rnd", unit, def_.rand, plain));
        layout.add (floatParam (ids::osc (index, ids::oscLevel), label + "Level", unit, def_.level, plain));
        layout.add (floatParam (ids::osc (index, ids::oscPan), label + "Pan", bipolar, def_.pan, plain));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::osc (index, ids::oscTable), 1 }, label + "Wavetable",
            tableNames(), def_.table));
    };

    addOsc (0, d.osc1);
    addOsc (1, d.osc2);

    layout.add (floatParam (ids::subLevel, "Sub", unit, d.subLevel, plain));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::subOct, 1 }, "Sub Oct", juce::StringArray { "-1 Oct", "-2 Oct" }, d.subOct));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::subShape, 1 }, "Sub Shape",
        juce::StringArray { "Sine", "Triangle", "Square" }, d.subShape));
    layout.add (floatParam (ids::noiseLevel, "Noise", unit, d.noiseLevel, plain));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::fltOn, 1 }, "Filter", d.filter.on));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::fltType, 1 }, "Filter Type", filterTypeNames(), int (d.filter.type)));
    layout.add (floatParam (ids::fltCut, "Cutoff", expRange (20.0f, 20000.0f), d.filter.cut, hz));
    layout.add (floatParam (ids::fltRes, "Reso", unit, d.filter.res, plain));
    layout.add (floatParam (ids::fltDrive, "Drive", unit, d.filter.drive, plain));
    layout.add (floatParam (ids::fltEnv, "Env 2 to Cutoff", bipolar, d.filter.env, plain));
    layout.add (floatParam (ids::fltKey, "Keytrack", unit, d.filter.key, plain));

    const auto envTime = expRange (0.001f, 20.0f);
    layout.add (floatParam (ids::env1A, "Env1 Attack", envTime, d.env1.a, sec));
    layout.add (floatParam (ids::env1D, "Env1 Decay", envTime, d.env1.d, sec));
    layout.add (floatParam (ids::env1S, "Env1 Sustain", unit, d.env1.s, plain));
    layout.add (floatParam (ids::env1R, "Env1 Release", envTime, d.env1.r, sec));
    layout.add (floatParam (ids::env2A, "Env2 Attack", envTime, d.env2.a, sec));
    layout.add (floatParam (ids::env2D, "Env2 Decay", envTime, d.env2.d, sec));
    layout.add (floatParam (ids::env2S, "Env2 Sustain", unit, d.env2.s, plain));
    layout.add (floatParam (ids::env2R, "Env2 Release", envTime, d.env2.r, sec));

    const auto lfoRate = expRange (0.01f, 30.0f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::lfo1Shape, 1 }, "LFO1 Shape", lfoShapeNames(), int (d.lfo1.shape)));
    layout.add (floatParam (ids::lfo1Rate, "LFO1 Rate", lfoRate, d.lfo1.rate, hz));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::lfo1Retrig, 1 }, "LFO1 Retrig", d.lfo1.retrig));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::lfo2Shape, 1 }, "LFO2 Shape", lfoShapeNames(), int (d.lfo2.shape)));
    layout.add (floatParam (ids::lfo2Rate, "LFO2 Rate", lfoRate, d.lfo2.rate, hz));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::lfo2Retrig, 1 }, "LFO2 Retrig", d.lfo2.retrig));

    for (int m = 0; m < 4; ++m)
    {
        const juce::String label = "Mod " + juce::String (m + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::mod (m, ids::modSrc), 1 }, label + "Source",
            modSourceNames(), d.mods[size_t (m)].src));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::mod (m, ids::modDst), 1 }, label + "Dest",
            modDestNames(), d.mods[size_t (m)].dst));
        layout.add (floatParam (ids::mod (m, ids::modAmt), label + "Amount", bipolar,
                                d.mods[size_t (m)].amt, plain));
    }

    layout.add (floatParam (ids::glide, "Glide", { 0.0f, 2.0f, 0.0f, 0.4f }, d.glide, sec));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ids::poly, 1 }, "Voices", 1, 16, d.poly));
    layout.add (floatParam (ids::velSens, "Vel Sens", unit, d.velSens, plain));
    layout.add (floatParam (ids::master, "Master", unit, d.master, plain));

    layout.add (floatParam (ids::fxDist, "Distort", unit, d.fx.dist, plain));
    layout.add (floatParam (ids::fxChorRate, "Ch Rate", expRange (0.05f, 8.0f), d.fx.chorusRate, hz));
    layout.add (floatParam (ids::fxChorDepth, "Ch Depth", unit, d.fx.chorusDepth, plain));
    layout.add (floatParam (ids::fxChorMix, "Ch Mix", unit, d.fx.chorusMix, plain));
    layout.add (floatParam (ids::fxDlyTime, "Dly Time", { 0.02f, 1.5f }, d.fx.delayTime, sec));
    layout.add (floatParam (ids::fxDlyFb, "Dly FB", { 0.0f, 0.9f }, d.fx.delayFeedback, plain));
    layout.add (floatParam (ids::fxDlyMix, "Dly Mix", unit, d.fx.delayMix, plain));
    layout.add (floatParam (ids::fxRevSize, "Rev Size", unit, d.fx.reverbSize, plain));
    layout.add (floatParam (ids::fxRevMix, "Rev Mix", unit, d.fx.reverbMix, plain));

    return layout;
}

static float raw (APVTS& a, const juce::String& id)
{
    if (auto* p = a.getRawParameterValue (id)) return p->load();
    jassertfalse;
    return 0.0f;
}

static bool rawBool (APVTS& a, const juce::String& id) { return raw (a, id) > 0.5f; }

static void readOsc (APVTS& a, OscParams& o, int index)
{
    o.on       = rawBool (a, ids::osc (index, ids::oscOn));
    o.pos      = raw (a, ids::osc (index, ids::oscPos));
    o.warpMode = WarpMode (juce::jlimit (0, kNumWarpModes - 1, int (raw (a, ids::osc (index, ids::oscWarpMode)))));
    o.warp     = raw (a, ids::osc (index, ids::oscWarp));
    o.specMode = SpecMode (juce::jlimit (0, kNumSpecModes - 1, int (raw (a, ids::osc (index, ids::oscSpecMode)))));
    o.spec     = raw (a, ids::osc (index, ids::oscSpec));
    o.unison   = int (raw (a, ids::osc (index, ids::oscUnison)));
    o.detune   = raw (a, ids::osc (index, ids::oscDetune));
    o.spread   = raw (a, ids::osc (index, ids::oscSpread));
    o.blend    = raw (a, ids::osc (index, ids::oscBlend));
    o.semi     = int (raw (a, ids::osc (index, ids::oscSemi)));
    o.fine     = raw (a, ids::osc (index, ids::oscFine));
    o.rand     = raw (a, ids::osc (index, ids::oscRand));
    o.level    = raw (a, ids::osc (index, ids::oscLevel));
    o.pan      = raw (a, ids::osc (index, ids::oscPan));
    o.table    = juce::jlimit (0, int (factoryTableNames().size()) - 1,
                               int (raw (a, ids::osc (index, ids::oscTable))));
}

Params readParams (APVTS& a)
{
    Params p;

    readOsc (a, p.osc1, 0);
    readOsc (a, p.osc2, 1);

    p.subLevel   = raw (a, ids::subLevel);
    p.subOct     = int (raw (a, ids::subOct));
    p.subShape   = int (raw (a, ids::subShape));
    p.noiseLevel = raw (a, ids::noiseLevel);

    p.filter.on    = rawBool (a, ids::fltOn);
    p.filter.type  = FilterType (juce::jlimit (0, kNumFilterTypes - 1, int (raw (a, ids::fltType))));
    p.filter.cut   = raw (a, ids::fltCut);
    p.filter.res   = raw (a, ids::fltRes);
    p.filter.drive = raw (a, ids::fltDrive);
    p.filter.env   = raw (a, ids::fltEnv);
    p.filter.key   = raw (a, ids::fltKey);

    p.env1 = { raw (a, ids::env1A), raw (a, ids::env1D), raw (a, ids::env1S), raw (a, ids::env1R) };
    p.env2 = { raw (a, ids::env2A), raw (a, ids::env2D), raw (a, ids::env2S), raw (a, ids::env2R) };

    p.lfo1.shape  = LfoShape (juce::jlimit (0, kNumLfoShapes - 1, int (raw (a, ids::lfo1Shape))));
    p.lfo1.rate   = raw (a, ids::lfo1Rate);
    p.lfo1.retrig = rawBool (a, ids::lfo1Retrig);
    p.lfo2.shape  = LfoShape (juce::jlimit (0, kNumLfoShapes - 1, int (raw (a, ids::lfo2Shape))));
    p.lfo2.rate   = raw (a, ids::lfo2Rate);
    p.lfo2.retrig = rawBool (a, ids::lfo2Retrig);

    for (int m = 0; m < 4; ++m)
    {
        p.mods[size_t (m)].src = juce::jlimit (0, kNumModSources - 1, int (raw (a, ids::mod (m, ids::modSrc))));
        p.mods[size_t (m)].dst = juce::jlimit (0, kNumModDests - 1, int (raw (a, ids::mod (m, ids::modDst))));
        p.mods[size_t (m)].amt = raw (a, ids::mod (m, ids::modAmt));
    }

    p.glide   = raw (a, ids::glide);
    p.poly    = int (raw (a, ids::poly));
    p.velSens = raw (a, ids::velSens);
    p.master  = raw (a, ids::master);

    p.fx.dist          = raw (a, ids::fxDist);
    p.fx.chorusRate    = raw (a, ids::fxChorRate);
    p.fx.chorusDepth   = raw (a, ids::fxChorDepth);
    p.fx.chorusMix     = raw (a, ids::fxChorMix);
    p.fx.delayTime     = raw (a, ids::fxDlyTime);
    p.fx.delayFeedback = raw (a, ids::fxDlyFb);
    p.fx.delayMix      = raw (a, ids::fxDlyMix);
    p.fx.reverbSize    = raw (a, ids::fxRevSize);
    p.fx.reverbMix     = raw (a, ids::fxRevMix);

    return p;
}

static void setPlain (APVTS& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

static void writeOsc (APVTS& a, const OscParams& o, int index)
{
    setPlain (a, ids::osc (index, ids::oscOn), o.on ? 1.0f : 0.0f);
    setPlain (a, ids::osc (index, ids::oscPos), o.pos);
    setPlain (a, ids::osc (index, ids::oscWarpMode), float (int (o.warpMode)));
    setPlain (a, ids::osc (index, ids::oscWarp), o.warp);
    setPlain (a, ids::osc (index, ids::oscSpecMode), float (int (o.specMode)));
    setPlain (a, ids::osc (index, ids::oscSpec), o.spec);
    setPlain (a, ids::osc (index, ids::oscUnison), float (o.unison));
    setPlain (a, ids::osc (index, ids::oscDetune), o.detune);
    setPlain (a, ids::osc (index, ids::oscSpread), o.spread);
    setPlain (a, ids::osc (index, ids::oscBlend), o.blend);
    setPlain (a, ids::osc (index, ids::oscSemi), float (o.semi));
    setPlain (a, ids::osc (index, ids::oscFine), o.fine);
    setPlain (a, ids::osc (index, ids::oscRand), o.rand);
    setPlain (a, ids::osc (index, ids::oscLevel), o.level);
    setPlain (a, ids::osc (index, ids::oscPan), o.pan);
    setPlain (a, ids::osc (index, ids::oscTable), float (o.table));
}

void writeParams (APVTS& a, const Params& p)
{
    writeOsc (a, p.osc1, 0);
    writeOsc (a, p.osc2, 1);

    setPlain (a, ids::subLevel, p.subLevel);
    setPlain (a, ids::subOct, float (p.subOct));
    setPlain (a, ids::subShape, float (p.subShape));
    setPlain (a, ids::noiseLevel, p.noiseLevel);

    setPlain (a, ids::fltOn, p.filter.on ? 1.0f : 0.0f);
    setPlain (a, ids::fltType, float (int (p.filter.type)));
    setPlain (a, ids::fltCut, p.filter.cut);
    setPlain (a, ids::fltRes, p.filter.res);
    setPlain (a, ids::fltDrive, p.filter.drive);
    setPlain (a, ids::fltEnv, p.filter.env);
    setPlain (a, ids::fltKey, p.filter.key);

    setPlain (a, ids::env1A, p.env1.a); setPlain (a, ids::env1D, p.env1.d);
    setPlain (a, ids::env1S, p.env1.s); setPlain (a, ids::env1R, p.env1.r);
    setPlain (a, ids::env2A, p.env2.a); setPlain (a, ids::env2D, p.env2.d);
    setPlain (a, ids::env2S, p.env2.s); setPlain (a, ids::env2R, p.env2.r);

    setPlain (a, ids::lfo1Shape, float (int (p.lfo1.shape)));
    setPlain (a, ids::lfo1Rate, p.lfo1.rate);
    setPlain (a, ids::lfo1Retrig, p.lfo1.retrig ? 1.0f : 0.0f);
    setPlain (a, ids::lfo2Shape, float (int (p.lfo2.shape)));
    setPlain (a, ids::lfo2Rate, p.lfo2.rate);
    setPlain (a, ids::lfo2Retrig, p.lfo2.retrig ? 1.0f : 0.0f);

    for (int m = 0; m < 4; ++m)
    {
        setPlain (a, ids::mod (m, ids::modSrc), float (p.mods[size_t (m)].src));
        setPlain (a, ids::mod (m, ids::modDst), float (p.mods[size_t (m)].dst));
        setPlain (a, ids::mod (m, ids::modAmt), p.mods[size_t (m)].amt);
    }

    setPlain (a, ids::glide, p.glide);
    setPlain (a, ids::poly, float (p.poly));
    setPlain (a, ids::velSens, p.velSens);
    setPlain (a, ids::master, p.master);

    setPlain (a, ids::fxDist, p.fx.dist);
    setPlain (a, ids::fxChorRate, p.fx.chorusRate);
    setPlain (a, ids::fxChorDepth, p.fx.chorusDepth);
    setPlain (a, ids::fxChorMix, p.fx.chorusMix);
    setPlain (a, ids::fxDlyTime, p.fx.delayTime);
    setPlain (a, ids::fxDlyFb, p.fx.delayFeedback);
    setPlain (a, ids::fxDlyMix, p.fx.delayMix);
    setPlain (a, ids::fxRevSize, p.fx.reverbSize);
    setPlain (a, ids::fxRevMix, p.fx.reverbMix);
}

} // namespace spectra
