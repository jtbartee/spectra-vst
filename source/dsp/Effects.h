#pragma once
#include "Params.h"
#include <array>
#include <vector>

namespace spectra {

/** A fractional delay line with linear interpolation, matching a Web Audio DelayNode with a
 *  modulated delayTime. */
class DelayLine
{
public:
    void prepare (double sampleRate, double maxDelaySeconds);
    void reset();

    inline void write (float x)
    {
        buffer[size_t (writePos)] = x;
        if (++writePos >= int (buffer.size())) writePos = 0;
    }

    inline float read (float delaySeconds) const
    {
        const float d = clampf (delaySeconds, 0.0f, maxDelay) * sr;
        const int n = int (buffer.size());
        float pos = float (writePos) - 1.0f - d;
        while (pos < 0.0f) pos += float (n);
        const int i0 = int (pos);
        const float frac = pos - float (i0);
        const int i1 = (i0 + 1) % n;
        return buffer[size_t (i0)] + (buffer[size_t (i1)] - buffer[size_t (i0)]) * frac;
    }

private:
    std::vector<float> buffer;
    float sr = 44100.0f, maxDelay = 1.0f;
    int writePos = 0;
};

/** Web Audio's setTargetAtTime as a per-sample recursion. */
class ParamSmoother
{
public:
    void setSampleRate (double s) { sampleRate = s; recalc(); }
    void snapTo (float v) { value = v; target = v; }
    void setTarget (float t, float tau)
    {
        target = t;
        if (tau != timeConstant) { timeConstant = std::max (1.0e-5f, tau); recalc(); }
    }
    inline float next() { value = target + (value - target) * coeff; return value; }
    float current() const { return value; }

private:
    void recalc() { coeff = float (std::exp (-1.0 / (double (timeConstant) * sampleRate))); }
    double sampleRate = 44100.0;
    float value = 0.0f, target = 0.0f, timeConstant = 0.02f, coeff = 0.0f;
};

/** Parallel dry/wet tanh waveshaper. The curve is `tanh(k*x)/tanh(k)` with `k = 1 + amount*30`,
 *  run at 2x oversampling, exactly as the WaveShaperNode is configured in js/synth.js. */
class Distortion
{
public:
    void prepare (double sampleRate);
    void setParams (const FxParams& fx);
    void process (float& l, float& r);

private:
    inline float shape (float x) const;
    ParamSmoother wet, dry;
    float k = 1.0f, norm = 1.0f;
    float prevL = 0.0f, prevR = 0.0f;
};

/** Two LFO-modulated delay taps summed into a wet bus. */
class Chorus
{
public:
    void prepare (double sampleRate);
    void setParams (const FxParams& fx);
    void process (float& l, float& r);

private:
    float sr = 44100.0f, phaseA = 0.0f, phaseB = 0.0f;
    DelayLine aL, aR, bL, bR;
    ParamSmoother rate, depthA, depthB, wet, dry;
};

/** A feedback delay. Dry passes at unity; the wet tap is scaled by `mix * 0.9`. */
class Delay
{
public:
    void prepare (double sampleRate);
    void setParams (const FxParams& fx);
    void process (float& l, float& r);

private:
    DelayLine lineL, lineR;
    ParamSmoother time, feedback, wet;
};

/** Reverb.
 *
 *  The web version convolves against a generated impulse -- exponentially decaying, gently
 *  damped white noise whose length and decay both follow the single Size control. Reproducing
 *  that exactly would mean partitioned FFT convolution over an impulse up to five seconds
 *  long, with the latency and CPU cost that implies, for what is musically the most generic
 *  reverb tail there is. This is a feedback delay network whose RT60 is derived from the same
 *  Size mapping, so the control behaves the same way.
 */
class Reverb
{
public:
    void prepare (double sampleRate);
    void setParams (const FxParams& fx);
    void process (float& l, float& r);

private:
    static constexpr int kLines = 8;
    void rebuild();

    float sr = 44100.0f;
    std::array<DelayLine, kLines> lines;
    std::array<float, kLines> delaySeconds {}, feedbackGain {}, damp {};
    ParamSmoother wet;
    float sizeValue = 0.5f;
    bool dirty = true;
};

/** The master safety compressor, standing in for Web Audio's DynamicsCompressorNode. Soft-knee
 *  curve rather than Blink's numerically-solved saturation, but the same controls, the same
 *  6 ms detector lookahead and the same `^0.6` makeup-gain rule. */
class Compressor
{
public:
    void prepare (double sampleRate, float thresholdDb, float kneeDb, float ratio,
                  float attackSeconds, float releaseSeconds);
    void process (float& l, float& r);

    static constexpr float kLookaheadSeconds = 0.006f;

private:
    float curveDb (float inDb) const;

    float threshold = -6.0f, knee = 6.0f, ratio = 6.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, envelopeDb = 0.0f, makeupGain = 1.0f;
    DelayLine lookaheadL, lookaheadR;
};

} // namespace spectra
