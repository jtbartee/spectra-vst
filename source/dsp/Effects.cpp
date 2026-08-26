#include "Effects.h"

namespace spectra {

void DelayLine::prepare (double sampleRate, double maxDelaySeconds)
{
    sr = float (sampleRate);
    const int n = std::max (4, int (maxDelaySeconds * sampleRate) + 4);
    buffer.assign (size_t (n), 0.0f);
    writePos = 0;
    maxDelay = float (maxDelaySeconds);
}

void DelayLine::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// ---------------------------------------------------------------------------
// Distortion
// ---------------------------------------------------------------------------

void Distortion::prepare (double sampleRate)
{
    wet.setSampleRate (sampleRate);
    dry.setSampleRate (sampleRate);
    wet.snapTo (0.0f);
    dry.snapTo (1.0f);
    prevL = prevR = 0.0f;
    k = 1.0f;
    norm = std::tanh (1.0f);
}

void Distortion::setParams (const FxParams& fx)
{
    const float v = clampf (fx.dist, 0.0f, 1.0f);
    wet.setTarget (v, 0.02f);
    dry.setTarget (1.0f - v * 0.8f, 0.02f);
    k = 1.0f + v * 30.0f;
    norm = std::tanh (k);
}

inline float Distortion::shape (float x) const
{
    return std::tanh (k * clampf (x, -1.0f, 1.0f)) / norm;
}

void Distortion::process (float& l, float& r)
{
    const float inL = l, inR = r;

    // 2x oversampling: shape the midpoint and the sample, then average. Averaging is the
    // decimation filter, which is crude but is what keeps the added harmonics from folding
    // straight back down -- the WaveShaperNode's own '2x' mode is doing the same job.
    const float midL = (prevL + inL) * 0.5f;
    const float midR = (prevR + inR) * 0.5f;
    const float wetL = (shape (midL) + shape (inL)) * 0.5f;
    const float wetR = (shape (midR) + shape (inR)) * 0.5f;
    prevL = inL;
    prevR = inR;

    const float w = wet.next(), d = dry.next();
    l = inL * d + wetL * w;
    r = inR * d + wetR * w;
}

// ---------------------------------------------------------------------------
// Chorus
// ---------------------------------------------------------------------------

void Chorus::prepare (double sampleRate)
{
    sr = float (sampleRate);
    for (auto* dl : { &aL, &aR, &bL, &bR }) dl->prepare (sampleRate, 0.1);
    for (auto* s : { &rate, &depthA, &depthB, &wet, &dry }) s->setSampleRate (sampleRate);
    rate.snapTo (0.6f);
    depthA.snapTo (0.35f * 0.008f);
    depthB.snapTo (0.35f * 0.0065f);
    wet.snapTo (0.0f);
    dry.snapTo (1.0f);
    phaseA = phaseB = 0.0f;
}

void Chorus::setParams (const FxParams& fx)
{
    rate.setTarget (fx.chorusRate, 0.05f);
    depthA.setTarget (fx.chorusDepth * 0.008f, 0.05f);
    depthB.setTarget (fx.chorusDepth * 0.0065f, 0.05f);
    wet.setTarget (fx.chorusMix * 0.75f, 0.02f);
    dry.setTarget (1.0f - fx.chorusMix * 0.4f, 0.02f);
}

void Chorus::process (float& l, float& r)
{
    const float inL = l, inR = r;
    const float f = rate.next();

    phaseA += f / sr;
    if (phaseA >= 1.0f) phaseA -= std::floor (phaseA);
    // The second tap runs at 1.13x the first, so the two never lock into a single beat.
    phaseB += (f * 1.13f) / sr;
    if (phaseB >= 1.0f) phaseB -= std::floor (phaseB);

    const float lfoA = std::sin (2.0f * float (M_PI) * phaseA);
    const float lfoB = std::sin (2.0f * float (M_PI) * phaseB);
    const float tA = 0.025f + lfoA * depthA.next();
    const float tB = 0.031f + lfoB * depthB.next();

    aL.write (inL); aR.write (inR);
    bL.write (inL); bR.write (inR);

    const float wetL = aL.read (tA) + bL.read (tB);
    const float wetR = aR.read (tA) + bR.read (tB);

    const float w = wet.next(), d = dry.next();
    l = inL * d + wetL * w;
    r = inR * d + wetR * w;
}

// ---------------------------------------------------------------------------
// Delay
// ---------------------------------------------------------------------------

void Delay::prepare (double sampleRate)
{
    lineL.prepare (sampleRate, 2.0);
    lineR.prepare (sampleRate, 2.0);
    for (auto* s : { &time, &feedback, &wet }) s->setSampleRate (sampleRate);
    time.snapTo (0.35f);
    feedback.snapTo (0.35f);
    wet.snapTo (0.0f);
}

void Delay::setParams (const FxParams& fx)
{
    time.setTarget (fx.delayTime, 0.08f);
    feedback.setTarget (clampf (fx.delayFeedback, 0.0f, 0.9f), 0.02f);
    wet.setTarget (fx.delayMix * 0.9f, 0.02f);
}

void Delay::process (float& l, float& r)
{
    const float inL = l, inR = r;
    const float t = time.next(), fb = feedback.next();

    const float tapL = lineL.read (t);
    const float tapR = lineR.read (t);
    lineL.write (inL + tapL * fb);
    lineR.write (inR + tapR * fb);

    const float w = wet.next();
    l = inL + tapL * w;     // the dry path is unity here, matching the web graph
    r = inR + tapR * w;
}

// ---------------------------------------------------------------------------
// Reverb
// ---------------------------------------------------------------------------

void Reverb::prepare (double sampleRate)
{
    sr = float (sampleRate);
    for (auto& line : lines) line.prepare (sampleRate, 0.15);
    wet.setSampleRate (sampleRate);
    wet.snapTo (0.15f);
    for (auto& d : damp) d = 0.0f;
    dirty = true;
    rebuild();
}

void Reverb::setParams (const FxParams& fx)
{
    if (fx.reverbSize != sizeValue)
    {
        sizeValue = fx.reverbSize;
        dirty = true;
    }
    wet.setTarget (fx.reverbMix, 0.02f);
    if (dirty) rebuild();
}

void Reverb::rebuild()
{
    dirty = false;

    // The web version's impulse is `len = sr * (0.4 + size * 4.5)` with an envelope of
    // exp(-decay * t) where decay = 4 / (len/sr). That reaches -60 dB (e^-6.9) at
    // 6.9/decay seconds, which is the RT60 reproduced here.
    const float lengthSeconds = 0.4f + clampf (sizeValue, 0.0f, 1.0f) * 4.5f;
    const float rt60 = std::max (0.1f, 1.725f * lengthSeconds);

    static constexpr float baseMs[kLines] = { 23.3f, 29.7f, 37.1f, 43.9f, 51.7f, 59.3f, 67.9f, 73.1f };
    const float scale = 0.55f + clampf (sizeValue, 0.0f, 1.0f) * 0.85f;

    for (int i = 0; i < kLines; ++i)
    {
        delaySeconds[size_t (i)] = baseMs[i] * 0.001f * scale;
        feedbackGain[size_t (i)] = std::pow (10.0f, -3.0f * delaySeconds[size_t (i)] / rt60);
    }
}

void Reverb::process (float& l, float& r)
{
    const float inL = l, inR = r;

    float taps[kLines];
    for (int i = 0; i < kLines; ++i) taps[i] = lines[size_t (i)].read (delaySeconds[size_t (i)]);

    // Householder feedback matrix: lossless, cheap, and it diffuses every line into every
    // other one, which is what builds density.
    float sum = 0.0f;
    for (int i = 0; i < kLines; ++i) sum += taps[i];
    const float correction = 2.0f * sum / float (kLines);

    const float excitation = (inL + inR) * 0.5f;
    for (int i = 0; i < kLines; ++i)
    {
        float v = (taps[i] - correction) * feedbackGain[size_t (i)] + excitation * 0.35f;
        // The same gentle high damping the generated impulse has baked into it.
        damp[size_t (i)] += 0.35f * (v - damp[size_t (i)]);
        v = damp[size_t (i)];
        if (! std::isfinite (v)) v = 0.0f;
        lines[size_t (i)].write (v);
    }

    float wl = 0.0f, wr = 0.0f;
    for (int i = 0; i < kLines; ++i) ((i % 2) == 0 ? wl : wr) += taps[i];
    wl *= 2.0f / float (kLines);
    wr *= 2.0f / float (kLines);

    const float w = wet.next();
    l = inL + wl * w;
    r = inR + wr * w;
}

// ---------------------------------------------------------------------------
// Compressor
// ---------------------------------------------------------------------------

void Compressor::prepare (double sampleRate, float thresholdDb, float kneeDb, float ratioValue,
                          float attackSeconds, float releaseSeconds)
{
    threshold = thresholdDb;
    knee = std::max (0.0001f, kneeDb);
    ratio = std::max (1.0f, ratioValue);
    attackCoeff  = float (std::exp (-1.0 / (double (attackSeconds) * sampleRate)));
    releaseCoeff = float (std::exp (-1.0 / (double (releaseSeconds) * sampleRate)));
    envelopeDb = 0.0f;

    const float atUnityDb = curveDb (0.0f);
    const float linear = std::pow (10.0f, atUnityDb / 20.0f);
    makeupGain = std::pow (1.0f / std::max (1.0e-6f, linear), 0.6f);

    lookaheadL.prepare (sampleRate, double (kLookaheadSeconds) * 2.0);
    lookaheadR.prepare (sampleRate, double (kLookaheadSeconds) * 2.0);
}

float Compressor::curveDb (float inDb) const
{
    const float over = inDb - threshold;
    if (2.0f * over < -knee) return inDb;
    if (2.0f * std::fabs (over) <= knee)
    {
        const float x = over + knee * 0.5f;
        return inDb + (1.0f / ratio - 1.0f) * x * x / (2.0f * knee);
    }
    return threshold + over / ratio;
}

void Compressor::process (float& l, float& r)
{
    const float peak = std::max (std::fabs (l), std::fabs (r));
    const float inDb = 20.0f * std::log10 (std::max (1.0e-9f, peak));
    const float targetReductionDb = std::min (0.0f, curveDb (inDb) - inDb);

    const float coeff = targetReductionDb < envelopeDb ? attackCoeff : releaseCoeff;
    envelopeDb = targetReductionDb + (envelopeDb - targetReductionDb) * coeff;

    const float g = std::pow (10.0f, envelopeDb / 20.0f) * makeupGain;

    lookaheadL.write (l);
    lookaheadR.write (r);
    l = lookaheadL.read (kLookaheadSeconds) * g;
    r = lookaheadR.read (kLookaheadSeconds) * g;
}

} // namespace spectra
