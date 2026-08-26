#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Constants and small helpers shared across the SPECTRA engine, ported from worklet.js,
// js/params.js and js/wavetables.js.

namespace spectra {

inline constexpr int kFrame     = 2048;          // samples per wavetable frame
inline constexpr int kHarmonics = 1024;          // kFrame / 2
inline constexpr int kMips      = 8;             // mip k allows harmonics up to kHarmonics >> k
inline constexpr int kStride    = kFrame + 1;    // each frame carries a guard sample
inline constexpr int kCtrl      = 32;            // control-rate interval, in samples
inline constexpr int kMaxUnison = 8;
inline constexpr int kMaxVoices = 16;

/** Mod-matrix destination indices. These must stay in step with MOD_DESTS in params.js --
 *  presets store the integer, not the name. */
enum ModDest
{
    dOff = 0, dO1Pos, dO2Pos, dO1Warp, dO2Warp, dCut, dRes, dPitch, dAmp, dPan, dO1Lvl, dO2Lvl
};
inline constexpr int kNumModDests = 12;
inline constexpr int kNumModSources = 8;

enum class WarpMode { off = 0, bend, sync, formant, quantize, mirror, squeeze };
inline constexpr int kNumWarpModes = 7;

enum class SpecMode { off = 0, smooth, tilt, stretch, shift, comb, disperse };
inline constexpr int kNumSpecModes = 7;

enum class LfoShape { sine = 0, triangle, saw, square, sampleHold };
inline constexpr int kNumLfoShapes = 5;

enum class FilterType { lowpass = 0, bandpass, highpass, notch };
inline constexpr int kNumFilterTypes = 4;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** The rational tanh approximation from worklet.js. It is not just an optimisation: it is what
 *  the master soft-clip and the filter drive actually sound like, so std::tanh would change
 *  the saturation character. */
inline float fastTanh (float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return (x * (27.0f + x2)) / (27.0f + 9.0f * x2);
}

/** params.js tToValue / valueToT. */
inline float tToValueLin (float t, float lo, float hi) { return lo + (hi - lo) * clampf (t, 0.0f, 1.0f); }
inline float tToValueExp (float t, float lo, float hi) { return lo * std::pow (hi / lo, clampf (t, 0.0f, 1.0f)); }

/** xorshift32, matching the per-voice noise generator in worklet.js. */
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}

    inline uint32_t nextBits()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** The worklet's `s / 2147483648 - 1`, i.e. a float in [-1, 1). */
    inline float nextNoise() { return float (nextBits()) / 2147483648.0f - 1.0f; }

    inline float next01() { return float (nextBits() & 0x00FFFFFFu) / float (0x01000000u); }
    inline float bipolar() { return next01() * 2.0f - 1.0f; }

private:
    uint32_t state;
};

} // namespace spectra
