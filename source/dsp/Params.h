#pragma once
#include "Mapping.h"
#include <array>
#include <string>
#include <vector>

namespace spectra {

/** One oscillator's slice of the patch, from oscParams() in js/params.js. */
struct OscParams
{
    bool  on = true;
    float pos = 0.0f;
    WarpMode warpMode = WarpMode::off;
    float warp = 0.0f;             // -1..1
    SpecMode specMode = SpecMode::off;
    float spec = 0.0f;
    int   unison = 1;              // 1..8
    float detune = 18.0f;          // cents
    float spread = 0.8f;
    float blend = 0.7f;
    int   semi = 0;                // -24..24
    float fine = 0.0f;             // -100..100 cents
    float rand = 1.0f;             // phase randomisation probability
    float level = 0.75f;
    float pan = 0.0f;              // -1..1

    /** Which factory wavetable feeds this oscillator. */
    int table = 0;
};

struct EnvParams { float a = 0.005f, d = 0.6f, s = 1.0f, r = 0.3f; };

struct LfoParams
{
    LfoShape shape = LfoShape::sine;
    float rate = 2.0f;
    bool  retrig = true;
};

struct ModSlot { int src = 0, dst = 0; float amt = 0.0f; };

struct FilterParams
{
    bool on = true;
    FilterType type = FilterType::lowpass;
    float cut = 12000.0f;
    float res = 0.3f;
    float drive = 0.0f;
    float env = 0.0f;      // -1..1, Env2 -> cutoff
    float key = 0.0f;
};

struct FxParams
{
    float dist = 0.0f;
    float chorusRate = 0.6f, chorusDepth = 0.35f, chorusMix = 0.0f;
    float delayTime = 0.35f, delayFeedback = 0.35f, delayMix = 0.0f;
    float reverbSize = 0.5f, reverbMix = 0.15f;
};

/** The whole patch. Field-for-field the PARAMS table in js/params.js, plus the two table
 *  selections, which the web version stores alongside the params rather than inside them. */
struct Params
{
    std::string name = "Init";

    OscParams osc1, osc2;

    float subLevel = 0.0f;
    int   subOct = 0;        // 0 = -1 octave, 1 = -2 octaves
    int   subShape = 0;      // 0 sine, 1 triangle, 2 square
    float noiseLevel = 0.0f;

    FilterParams filter;
    EnvParams env1 { 0.005f, 0.6f, 1.0f, 0.3f };
    EnvParams env2 { 0.005f, 0.5f, 0.0f, 0.3f };
    LfoParams lfo1, lfo2;
    std::array<ModSlot, 4> mods {};

    float glide = 0.0f;
    int   poly = 8;          // 1..16
    float velSens = 0.5f;
    float master = 0.8f;

    FxParams fx;

    Params()
    {
        osc2.on = false;
        lfo1.rate = 2.0f;
        lfo2.rate = 0.3f;
    }
};

/** The seven factory presets bundled with the web version. */
const std::vector<Params>& factoryPresets();

} // namespace spectra
