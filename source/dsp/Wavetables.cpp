#include "Wavetables.h"
#include "Fft.h"
#include <cstring>

namespace spectra {

// ---------------------------------------------------------------------------
// Analysis / synthesis
// ---------------------------------------------------------------------------

HarmFrame analyzeFrame (const std::vector<float>& samples)
{
    std::vector<float> re (size_t (kFrame), 0.0f), im (size_t (kFrame), 0.0f);
    const size_t n = samples.size();
    if (n == 0) return {};

    // Linear resample of the input to kFrame points.
    for (int i = 0; i < kFrame; ++i)
    {
        const double x = (double (i) / double (kFrame)) * double (n);
        const size_t i0 = size_t (std::floor (x)) % n;
        const size_t i1 = (i0 + 1) % n;
        const float fx = float (x - std::floor (x));
        re[size_t (i)] = samples[i0] + (samples[i1] - samples[i0]) * fx;
    }

    fft (re.data(), im.data(), size_t (kFrame), false);

    HarmFrame out;
    for (int h = 1; h <= kHarmonics; ++h)
    {
        out.re[size_t (h)] = (2.0f * re[size_t (h)]) / float (kFrame);
        out.im[size_t (h)] = (2.0f * im[size_t (h)]) / float (kFrame);
    }
    return out;
}

/** Synthesises a harmonic frame to the time domain, keeping harmonics <= maxH. */
static void synthFrame (const HarmFrame& hf, int maxH, std::vector<float>& outRe, std::vector<float>& outIm)
{
    std::fill (outRe.begin(), outRe.end(), 0.0f);
    std::fill (outIm.begin(), outIm.end(), 0.0f);

    const int top = std::min (maxH, kHarmonics);
    for (int h = 1; h <= top; ++h)
    {
        const float cr = (hf.re[size_t (h)] * float (kFrame)) / 2.0f;
        const float ci = (hf.im[size_t (h)] * float (kFrame)) / 2.0f;
        outRe[size_t (h)] = cr;               outIm[size_t (h)] = ci;
        outRe[size_t (kFrame - h)] = cr;      outIm[size_t (kFrame - h)] = -ci;
    }
    fft (outRe.data(), outIm.data(), size_t (kFrame), true);
}

std::unique_ptr<WavetableSet> buildMips (const std::vector<HarmFrame>& frames)
{
    auto set = std::make_unique<WavetableSet>();
    const int numFrames = std::max (1, int (frames.size()));
    set->numFrames = numFrames;

    std::vector<float> scratchRe (size_t (kFrame), 0.0f), scratchIm (size_t (kFrame), 0.0f);

    for (int k = 0; k < kMips; ++k)
    {
        const int maxH = kHarmonics >> k;
        auto& buf = set->mips[k];
        buf.assign (size_t (numFrames) * size_t (kStride), 0.0f);

        for (int f = 0; f < numFrames; ++f)
        {
            synthFrame (frames[size_t (f)], maxH, scratchRe, scratchIm);
            const size_t base = size_t (f) * size_t (kStride);
            std::memcpy (buf.data() + base, scratchRe.data(), size_t (kFrame) * sizeof (float));
            buf[base + size_t (kFrame)] = buf[base];   // guard sample for wrap-free interpolation
        }
    }

    // One shared normalisation factor across every mip, so loudness does not jump between
    // octaves and no mip exceeds +/-0.95 -- band-limited truncation can overshoot the
    // full-band peak through Gibbs ripple.
    float peak = 0.0f;
    for (auto& m : set->mips)
        for (float v : m) peak = std::max (peak, std::fabs (v));

    if (peak > 1.0e-9f)
    {
        const float g = 0.95f / peak;
        for (auto& m : set->mips)
            for (float& v : m) v *= g;
    }

    for (int f = 0; f < numFrames; ++f)
    {
        std::vector<float> d (128, 0.0f);
        const size_t base = size_t (f) * size_t (kStride);
        for (int i = 0; i < 128; ++i) d[size_t (i)] = set->mips[0][base + size_t ((i * kFrame) >> 7)];
        set->displayFrames.push_back (std::move (d));
    }

    return set;
}

std::unique_ptr<WavetableSet> defaultSineTable()
{
    auto set = std::make_unique<WavetableSet>();
    set->numFrames = 1;
    std::vector<float> buf (size_t (kStride), 0.0f);
    for (int i = 0; i < kFrame; ++i)
        buf[size_t (i)] = std::sin ((2.0f * float (M_PI) * float (i)) / float (kFrame)) * 0.95f;
    buf[size_t (kFrame)] = buf[0];
    for (auto& m : set->mips) m = buf;
    return set;
}

// ---------------------------------------------------------------------------
// Spectral warping
// ---------------------------------------------------------------------------

std::vector<HarmFrame> spectralWarp (const std::vector<HarmFrame>& frames, SpecMode mode, float amt)
{
    if (mode == SpecMode::off || amt <= 0.0f) return frames;

    std::vector<HarmFrame> out;
    out.reserve (frames.size());

    // Deterministic LCG for Disperse, so the same patch always produces the same table.
    uint32_t seed = 1234567u;
    auto rand = [&seed]
    {
        seed = uint32_t (seed * 1664525u + 1013904223u);
        return float (seed) / 4294967296.0f;
    };

    for (const HarmFrame& hf : frames)
    {
        HarmFrame o;
        switch (mode)
        {
            case SpecMode::smooth:   // progressive low-pass on the harmonic series
            {
                const float k = amt * amt * 0.4f;
                for (int h = 1; h <= kHarmonics; ++h)
                {
                    const float g = std::exp (-float (h - 1) * k);
                    o.re[size_t (h)] = hf.re[size_t (h)] * g;
                    o.im[size_t (h)] = hf.im[size_t (h)] * g;
                }
                break;
            }
            case SpecMode::tilt:     // brighten by boosting highs relative to lows
            {
                for (int h = 1; h <= kHarmonics; ++h)
                {
                    const float g = std::pow (float (h), amt * 1.2f);
                    o.re[size_t (h)] = hf.re[size_t (h)] * g;
                    o.im[size_t (h)] = hf.im[size_t (h)] * g;
                }
                break;
            }
            case SpecMode::stretch:  // remap harmonic h to h * (1 + 3*amt)
            {
                const float m = 1.0f + 3.0f * amt;
                for (int h = 1; h <= kHarmonics; ++h)
                {
                    const int t = int (std::lround (float (h) * m));
                    if (t > kHarmonics) break;
                    o.re[size_t (t)] += hf.re[size_t (h)];
                    o.im[size_t (t)] += hf.im[size_t (h)];
                }
                break;
            }
            case SpecMode::shift:    // slide the whole spectrum up by up to 64 bins
            {
                const int s = int (std::lround (amt * 64.0f));
                for (int h = 1; h <= kHarmonics; ++h)
                {
                    const int t = h + s;
                    if (t > kHarmonics) break;
                    o.re[size_t (t)] = hf.re[size_t (h)];
                    o.im[size_t (t)] = hf.im[size_t (h)];
                }
                // Keep the fundamental present so the pitch still reads correctly.
                o.re[1] += hf.re[1] * 0.3f;
                o.im[1] += hf.im[1] * 0.3f;
                break;
            }
            case SpecMode::comb:     // attenuate harmonics that are not multiples of n
            {
                const int n = 2 + int (std::lround (amt * 6.0f));
                for (int h = 1; h <= kHarmonics; ++h)
                {
                    const float keep = (h == 1 || h % n == 0) ? 1.0f : 1.0f - amt;
                    o.re[size_t (h)] = hf.re[size_t (h)] * keep;
                    o.im[size_t (h)] = hf.im[size_t (h)] * keep;
                }
                break;
            }
            case SpecMode::disperse: // scramble harmonic phases (lush / glassy)
            {
                for (int h = 1; h <= kHarmonics; ++h)
                {
                    const float mag = std::hypot (hf.re[size_t (h)], hf.im[size_t (h)]);
                    if (mag < 1.0e-12f) continue;
                    const float ph0 = std::atan2 (hf.im[size_t (h)], hf.re[size_t (h)]);
                    const float ph = ph0 + (rand() * 2.0f - 1.0f) * float (M_PI) * amt * (h > 1 ? 1.0f : 0.0f);
                    o.re[size_t (h)] = mag * std::cos (ph);
                    o.im[size_t (h)] = mag * std::sin (ph);
                }
                break;
            }
            case SpecMode::off:
            default:
                return frames;
        }
        out.push_back (std::move (o));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Factory tables
// ---------------------------------------------------------------------------

namespace {

struct ShapeHarm { std::vector<float> re, im; };

/** A sine-phase harmonic: c[h] = -i*mag, so the time signal is mag*sin(2*pi*h*x). */
ShapeHarm shapeHarmonics (const char* shape)
{
    ShapeHarm s { std::vector<float> (size_t (kHarmonics) + 1, 0.0f),
                  std::vector<float> (size_t (kHarmonics) + 1, 0.0f) };
    for (int h = 1; h <= kHarmonics; ++h)
    {
        float m = 0.0f, sign = 1.0f;
        if (std::strcmp (shape, "sine") == 0)
        {
            m = (h == 1) ? 1.0f : 0.0f;
        }
        else if (std::strcmp (shape, "triangle") == 0)
        {
            if (h % 2 == 1)
            {
                m = 1.0f / float (h * h);
                sign = (((h - 1) / 2) % 2 == 0) ? 1.0f : -1.0f;
            }
        }
        else if (std::strcmp (shape, "saw") == 0)
        {
            m = 1.0f / float (h);
        }
        else if (std::strcmp (shape, "square") == 0)
        {
            if (h % 2 == 1) m = 1.0f / float (h);
        }
        s.im[size_t (h)] = -(m * sign);
    }
    return s;
}

ShapeHarm pulseHarmonics (float width)
{
    ShapeHarm s { std::vector<float> (size_t (kHarmonics) + 1, 0.0f),
                  std::vector<float> (size_t (kHarmonics) + 1, 0.0f) };
    for (int h = 1; h <= kHarmonics; ++h)
    {
        const float m = (2.0f / (float (M_PI) * float (h))) * std::sin (float (M_PI) * float (h) * width);
        s.im[size_t (h)] = -m;
    }
    return s;
}

HarmFrame blendShapes (const ShapeHarm& a, const ShapeHarm& b, float t)
{
    HarmFrame o;
    for (int h = 1; h <= kHarmonics; ++h)
    {
        o.re[size_t (h)] = a.re[size_t (h)] + (b.re[size_t (h)] - a.re[size_t (h)]) * t;
        o.im[size_t (h)] = a.im[size_t (h)] + (b.im[size_t (h)] - a.im[size_t (h)]) * t;
    }
    return o;
}

std::vector<HarmFrame> basicShapes()
{
    std::vector<ShapeHarm> shapes {
        shapeHarmonics ("sine"), shapeHarmonics ("triangle"),
        shapeHarmonics ("saw"),  shapeHarmonics ("square"), pulseHarmonics (0.12f)
    };
    std::vector<HarmFrame> frames;
    constexpr int N = 17;
    for (int f = 0; f < N; ++f)
    {
        const float t = (float (f) / float (N - 1)) * float (shapes.size() - 1);
        const int i = std::min (int (shapes.size()) - 2, int (std::floor (t)));
        frames.push_back (blendShapes (shapes[size_t (i)], shapes[size_t (i + 1)], t - float (i)));
    }
    return frames;
}

std::vector<HarmFrame> harmonicSweep()
{
    std::vector<HarmFrame> frames;
    constexpr int N = 17;
    for (int f = 0; f < N; ++f)
    {
        HarmFrame o;
        const float t = float (f) / float (N - 1);
        const float cutoff = 1.0f + t * t * 80.0f;   // a sweeping low-pass on a saw
        const float peak = 1.0f + t * 60.0f;         // with a resonant bump at the edge
        for (int h = 1; h <= kHarmonics; ++h)
        {
            const float lp = 1.0f / (1.0f + std::pow (float (h) / cutoff, 6.0f));
            const float res = 2.5f * std::exp (-std::pow ((float (h) - peak) / 3.0f, 2.0f));
            o.im[size_t (h)] = -((1.0f / float (h)) * (lp + res * lp));
        }
        frames.push_back (std::move (o));
    }
    return frames;
}

std::vector<HarmFrame> pwmTable()
{
    std::vector<HarmFrame> frames;
    constexpr int N = 17;
    for (int f = 0; f < N; ++f)
    {
        const float w = 0.5f - (float (f) / float (N - 1)) * 0.46f;
        const ShapeHarm p = pulseHarmonics (w);
        HarmFrame o;
        o.re.assign (p.re.begin(), p.re.end());
        o.im.assign (p.im.begin(), p.im.end());
        frames.push_back (std::move (o));
    }
    return frames;
}

std::vector<HarmFrame> voxTable()
{
    // A saw pushed through morphing formant peaks: a rough vowel sweep A -> O -> E -> I.
    static const float vowels[4][3] = {
        { 800.0f, 1150.0f, 2900.0f }, { 450.0f, 800.0f, 2830.0f },
        { 400.0f, 1700.0f, 2600.0f }, { 250.0f, 1750.0f, 3050.0f }
    };
    constexpr float baseHz = 110.0f;   // formants expressed for a low voice; h = freq / base
    std::vector<HarmFrame> frames;
    constexpr int N = 17;
    for (int f = 0; f < N; ++f)
    {
        const float t = (float (f) / float (N - 1)) * 3.0f;
        const int i = std::min (2, int (std::floor (t)));
        const float ft = t - float (i);
        float fm[3];
        for (int k = 0; k < 3; ++k) fm[k] = vowels[i][k] + (vowels[i + 1][k] - vowels[i][k]) * ft;

        HarmFrame o;
        for (int h = 1; h <= 256; ++h)
        {
            float g = 0.02f;
            for (int k = 0; k < 3; ++k)
            {
                const float center = fm[k] / baseHz;
                const float bw = 2.0f + float (k) * 2.0f;
                g += std::exp (-std::pow ((float (h) - center) / bw, 2.0f)) * (1.0f - float (k) * 0.25f);
            }
            o.im[size_t (h)] = -(1.0f / float (h)) * g * 3.0f;
        }
        frames.push_back (std::move (o));
    }
    return frames;
}

std::vector<HarmFrame> metallicTable()
{
    // Sparse quasi-inharmonic partials, growing denser across the table.
    std::vector<HarmFrame> frames;
    constexpr int N = 17;
    uint32_t seed = 99u;
    auto rand = [&seed] { seed = uint32_t (seed * 1664525u + 1013904223u); return float (seed) / 4294967296.0f; };

    std::vector<int> partials;
    double p = 1.0;
    while (p <= 700.0) { partials.push_back (int (std::lround (p))); p = p * 1.83 + 0.7; }
    std::vector<float> phases;
    for (size_t i = 0; i < partials.size(); ++i) phases.push_back (rand() * float (M_PI) * 2.0f);

    for (int f = 0; f < N; ++f)
    {
        const float t = float (f) / float (N - 1);
        HarmFrame o;
        o.im[1] = -0.6f;   // anchor the fundamental
        for (size_t k = 0; k < partials.size(); ++k)
        {
            const int h = partials[k];
            if (h > kHarmonics) continue;
            const float m = (0.9f / float (k + 1)) * (0.25f + 0.75f * t);
            const float ph = phases[k] + t * float (k) * 2.0f;
            o.re[size_t (h)] += m * std::cos (ph);
            o.im[size_t (h)] += m * std::sin (ph);
        }
        frames.push_back (std::move (o));
    }
    return frames;
}

std::vector<HarmFrame> digitalTable()
{
    // A sine quantised to fewer and fewer steps: crunchy bit-reduced shapes.
    std::vector<HarmFrame> frames;
    constexpr int N = 17;
    for (int f = 0; f < N; ++f)
    {
        const float steps = std::max (2.0f, std::round (64.0f / std::pow (2.0f, (float (f) / float (N - 1)) * 5.0f)));
        std::vector<float> wave (size_t (kFrame), 0.0f);
        for (int i = 0; i < kFrame; ++i)
        {
            const float s = std::sin ((2.0f * float (M_PI) * float (i)) / float (kFrame));
            wave[size_t (i)] = std::round (s * steps) / steps;
        }
        frames.push_back (analyzeFrame (wave));
    }
    return frames;
}

std::vector<HarmFrame> sineTable()
{
    HarmFrame o;
    o.im[1] = -1.0f;
    return { o };
}

} // namespace

const std::vector<std::string>& factoryTableNames()
{
    static const std::vector<std::string> names {
        "Basic Shapes", "Harmonic Sweep", "PWM", "Vox", "Metallic", "Digital", "Sine"
    };
    return names;
}

std::vector<HarmFrame> buildFactoryHarmFrames (int index)
{
    switch (index)
    {
        case 0: return basicShapes();
        case 1: return harmonicSweep();
        case 2: return pwmTable();
        case 3: return voxTable();
        case 4: return metallicTable();
        case 5: return digitalTable();
        case 6: return sineTable();
        default: break;
    }
    return basicShapes();
}

int factoryTableIndexByName (const std::string& name)
{
    const auto& names = factoryTableNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return int (i);
    return 0;
}

} // namespace spectra
