#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Params.h"
#include "../dsp/Wavetables.h"

namespace spectra::ids {
// Parameter IDs mirror the ids in js/params.js so a browser patch and a plugin preset name the
// same things. They are a stable on-disk format: renaming one breaks every saved session.

// Per-oscillator ids are built from a prefix ("o1" / "o2") at runtime; these are the suffixes.
inline constexpr const char* oscOn        = "_on";
inline constexpr const char* oscPos       = "_pos";
inline constexpr const char* oscWarpMode  = "_warpmode";
inline constexpr const char* oscWarp      = "_warp";
inline constexpr const char* oscSpecMode  = "_specmode";
inline constexpr const char* oscSpec      = "_spec";
inline constexpr const char* oscUnison    = "_unison";
inline constexpr const char* oscDetune    = "_detune";
inline constexpr const char* oscSpread    = "_spread";
inline constexpr const char* oscBlend     = "_blend";
inline constexpr const char* oscSemi      = "_semi";
inline constexpr const char* oscFine      = "_fine";
inline constexpr const char* oscRand      = "_rand";
inline constexpr const char* oscLevel     = "_level";
inline constexpr const char* oscPan       = "_pan";
inline constexpr const char* oscTable     = "_table";

inline constexpr const char* subLevel   = "sub_level";
inline constexpr const char* subOct     = "sub_oct";
inline constexpr const char* subShape   = "sub_shape";
inline constexpr const char* noiseLevel = "noise_level";

inline constexpr const char* fltOn    = "flt_on";
inline constexpr const char* fltType  = "flt_type";
inline constexpr const char* fltCut   = "flt_cut";
inline constexpr const char* fltRes   = "flt_res";
inline constexpr const char* fltDrive = "flt_drive";
inline constexpr const char* fltEnv   = "flt_env";
inline constexpr const char* fltKey   = "flt_key";

inline constexpr const char* env1A = "env1_a";
inline constexpr const char* env1D = "env1_d";
inline constexpr const char* env1S = "env1_s";
inline constexpr const char* env1R = "env1_r";
inline constexpr const char* env2A = "env2_a";
inline constexpr const char* env2D = "env2_d";
inline constexpr const char* env2S = "env2_s";
inline constexpr const char* env2R = "env2_r";

inline constexpr const char* lfo1Shape  = "lfo1_shape";
inline constexpr const char* lfo1Rate   = "lfo1_rate";
inline constexpr const char* lfo1Retrig = "lfo1_retrig";
inline constexpr const char* lfo2Shape  = "lfo2_shape";
inline constexpr const char* lfo2Rate   = "lfo2_rate";
inline constexpr const char* lfo2Retrig = "lfo2_retrig";

inline constexpr const char* glide   = "glide";
inline constexpr const char* poly    = "poly";
inline constexpr const char* velSens = "velsens";
inline constexpr const char* master  = "master";

inline constexpr const char* fxDist       = "fx_dist";
inline constexpr const char* fxChorRate   = "fx_chor_rate";
inline constexpr const char* fxChorDepth  = "fx_chor_depth";
inline constexpr const char* fxChorMix    = "fx_chor_mix";
inline constexpr const char* fxDlyTime    = "fx_dly_time";
inline constexpr const char* fxDlyFb      = "fx_dly_fb";
inline constexpr const char* fxDlyMix     = "fx_dly_mix";
inline constexpr const char* fxRevSize    = "fx_rev_size";
inline constexpr const char* fxRevMix     = "fx_rev_mix";

/** "o1"/"o2" + suffix, e.g. osc(0, oscPos) == "o1_pos". */
juce::String osc (int index, const char* suffix);
/** "mod1_src" .. "mod4_amt". */
juce::String mod (int slot, const char* suffix);

inline constexpr const char* modSrc = "_src";
inline constexpr const char* modDst = "_dst";
inline constexpr const char* modAmt = "_amt";
}

namespace spectra {

juce::StringArray warpModeNames();
juce::StringArray specModeNames();
juce::StringArray lfoShapeNames();
juce::StringArray filterTypeNames();
juce::StringArray modSourceNames();
juce::StringArray modDestNames();
juce::StringArray tableNames();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

Params readParams (juce::AudioProcessorValueTreeState& apvts);
void writeParams (juce::AudioProcessorValueTreeState& apvts, const Params& p);

} // namespace spectra
