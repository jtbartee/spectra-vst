// Offline verification harness for the SPECTRA DSP core.
//
// The DSP has no JUCE dependency, so it can be driven directly: build tables, play notes, and
// check the result. The checks that matter most here are the ones covering the wavetable
// pipeline -- a mis-transcribed FFT convention or a broken mip selection produces a plugin
// that builds, validates, and sounds like aliasing mush.
//
//   ./spectra_render            -- run all checks, write a WAV per factory preset
//   ./spectra_render --quick    -- checks only, no WAV output

#include "../source/dsp/SpectraEngine.h"
#include "../source/dsp/Randomize.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace spectra;

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 512;

static void writeWav (const std::string& path, const std::vector<float>& l,
                      const std::vector<float>& r, double sampleRate)
{
    const uint32_t n = uint32_t (l.size());
    const uint32_t dataBytes = n * 2 * 2;
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2);
    u32 (uint32_t (sampleRate)); u32 (uint32_t (sampleRate) * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        auto clip = [] (float v) { return int16_t (std::max (-1.0f, std::min (1.0f, v)) * 32767.0f); };
        int16_t s0 = clip (l[i]), s1 = clip (r[i]);
        std::fwrite (&s0, 2, 1, f); std::fwrite (&s1, 2, 1, f);
    }
    std::fclose (f);
}

static double goertzel (const std::vector<float>& x, size_t from, size_t count, double freq, double sr)
{
    const double w = 2.0 * M_PI * freq / sr;
    const double coeff = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < count && from + i < x.size(); ++i)
    {
        s0 = double (x[from + i]) + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

struct Result { int passed = 0, failed = 0; };

static void check (Result& r, bool ok, const std::string& what, const std::string& detail = "")
{
    if (ok) ++r.passed;
    else { ++r.failed; std::printf ("  FAIL  %s  %s\n", what.c_str(), detail.c_str()); }
}

struct Rendered
{
    std::vector<float> l, r;
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;

    void measure()
    {
        double sumSq = 0.0;
        for (size_t i = 0; i < l.size(); ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) { finite = false; break; }
            peak = std::max (peak, std::fabs (l[i]));
            sumSq += double (l[i]) * double (l[i]);
        }
        rms = std::sqrt (sumSq / double (std::max<size_t> (1, l.size())));
    }
};

/** Builds and publishes both oscillators' tables the way the plugin's message thread does. */
static void applyTables (SpectraEngine& engine, const Params& p)
{
    for (int o = 0; o < 2; ++o)
    {
        const OscParams& op = (o == 0) ? p.osc1 : p.osc2;
        auto frames = buildFactoryHarmFrames (op.table);
        auto warped = spectralWarp (frames, op.specMode, op.spec);
        engine.publishTable (o, buildMips (warped));
    }
}

static Rendered playNote (SpectraEngine& engine, int note, float vel,
                          double holdSeconds, double tailSeconds)
{
    const int hold = int (kSr * holdSeconds), tail = int (kSr * tailSeconds);
    Rendered out;
    out.l.assign (size_t (hold + tail), 0.0f);
    out.r.assign (size_t (hold + tail), 0.0f);

    engine.noteOn (note, vel);
    for (int pos = 0; pos < hold; pos += kBlock)
        engine.render (out.l.data() + pos, out.r.data() + pos, std::min (kBlock, hold - pos));
    engine.noteOff (note);
    for (int pos = 0; pos < tail; pos += kBlock)
        engine.render (out.l.data() + hold + pos, out.r.data() + hold + pos,
                       std::min (kBlock, tail - pos));

    out.measure();
    return out;
}

/** A plain patch for anything measuring pitch or spectrum: one oscillator, no unison, filter
 *  wide open, no modulation, no effects. */
static Params tuningPatch (int table = 6 /* Sine */)
{
    Params p;
    p.name = "Tuning";
    p.osc1.on = true;
    p.osc1.table = table;
    p.osc1.pos = 0.0f;
    p.osc1.unison = 1;
    p.osc1.rand = 0.0f;
    p.osc1.level = 0.9f;
    p.osc2.on = false;
    p.subLevel = 0.0f;
    p.noiseLevel = 0.0f;
    p.filter.on = false;
    p.env1 = { 0.001f, 0.5f, 1.0f, 0.05f };
    p.env2 = { 0.001f, 0.5f, 1.0f, 0.05f };
    p.velSens = 0.0f;
    p.master = 0.8f;
    p.fx = FxParams {};
    p.fx.reverbMix = 0.0f;
    return p;
}

/** Returns a description of the first out-of-range field, or "" if the patch is clean. This is
 *  the C++ counterpart of running a rolled patch through sanitizeParams() in the browser: the
 *  roll must never produce a value the parameter layout would have to clamp. */
static std::string outOfRange (const Params& p)
{
    std::string bad;
    auto chk = [&bad] (bool ok, const char* what, double v)
    {
        if (! ok && bad.empty()) bad = std::string (what) + "=" + std::to_string (v);
    };
    auto inRange = [] (double v, double lo, double hi) { return std::isfinite (v) && v >= lo && v <= hi; };

    const int numTables = int (factoryTableNames().size());
    for (int o = 0; o < 2; ++o)
    {
        const OscParams& q = (o == 0) ? p.osc1 : p.osc2;
        const char* n = (o == 0) ? "osc1" : "osc2";
        chk (inRange (q.pos, 0, 1), (std::string (n) + ".pos").c_str(), q.pos);
        chk (inRange (q.warp, -1, 1), (std::string (n) + ".warp").c_str(), q.warp);
        chk (inRange (q.spec, 0, 1), (std::string (n) + ".spec").c_str(), q.spec);
        chk (q.unison >= 1 && q.unison <= kMaxUnison, (std::string (n) + ".unison").c_str(), q.unison);
        chk (inRange (q.detune, 0, 100), (std::string (n) + ".detune").c_str(), q.detune);
        chk (inRange (q.spread, 0, 1), (std::string (n) + ".spread").c_str(), q.spread);
        chk (inRange (q.blend, 0, 1), (std::string (n) + ".blend").c_str(), q.blend);
        chk (q.semi >= -24 && q.semi <= 24, (std::string (n) + ".semi").c_str(), q.semi);
        chk (inRange (q.fine, -100, 100), (std::string (n) + ".fine").c_str(), q.fine);
        chk (inRange (q.rand, 0, 1), (std::string (n) + ".rand").c_str(), q.rand);
        chk (inRange (q.level, 0, 1), (std::string (n) + ".level").c_str(), q.level);
        chk (inRange (q.pan, -1, 1), (std::string (n) + ".pan").c_str(), q.pan);
        chk (q.table >= 0 && q.table < numTables, (std::string (n) + ".table").c_str(), q.table);
        chk (int (q.warpMode) >= 0 && int (q.warpMode) < kNumWarpModes,
             (std::string (n) + ".warpMode").c_str(), int (q.warpMode));
        chk (int (q.specMode) >= 0 && int (q.specMode) < kNumSpecModes,
             (std::string (n) + ".specMode").c_str(), int (q.specMode));
    }

    chk (inRange (p.subLevel, 0, 1), "subLevel", p.subLevel);
    chk (p.subOct >= 0 && p.subOct <= 1, "subOct", p.subOct);
    chk (p.subShape >= 0 && p.subShape <= 2, "subShape", p.subShape);
    chk (inRange (p.noiseLevel, 0, 1), "noiseLevel", p.noiseLevel);

    chk (int (p.filter.type) >= 0 && int (p.filter.type) < kNumFilterTypes, "filter.type", int (p.filter.type));
    chk (inRange (p.filter.cut, 20, 20000), "filter.cut", p.filter.cut);
    chk (inRange (p.filter.res, 0, 1), "filter.res", p.filter.res);
    chk (inRange (p.filter.drive, 0, 1), "filter.drive", p.filter.drive);
    chk (inRange (p.filter.env, -1, 1), "filter.env", p.filter.env);
    chk (inRange (p.filter.key, 0, 1), "filter.key", p.filter.key);

    for (int e = 0; e < 2; ++e)
    {
        const EnvParams& q = (e == 0) ? p.env1 : p.env2;
        const std::string n = (e == 0) ? "env1" : "env2";
        chk (inRange (q.a, 0.001, 20), (n + ".a").c_str(), q.a);
        chk (inRange (q.d, 0.001, 20), (n + ".d").c_str(), q.d);
        chk (inRange (q.s, 0, 1), (n + ".s").c_str(), q.s);
        chk (inRange (q.r, 0.001, 20), (n + ".r").c_str(), q.r);
    }

    for (int l = 0; l < 2; ++l)
    {
        const LfoParams& q = (l == 0) ? p.lfo1 : p.lfo2;
        const std::string n = (l == 0) ? "lfo1" : "lfo2";
        chk (inRange (q.rate, 0.01, 30), (n + ".rate").c_str(), q.rate);
        chk (int (q.shape) >= 0 && int (q.shape) < kNumLfoShapes, (n + ".shape").c_str(), int (q.shape));
    }

    for (size_t m = 0; m < p.mods.size(); ++m)
    {
        const std::string n = "mod" + std::to_string (m + 1);
        chk (p.mods[m].src >= 0 && p.mods[m].src < kNumModSources, (n + ".src").c_str(), p.mods[m].src);
        chk (p.mods[m].dst >= 0 && p.mods[m].dst < kNumModDests, (n + ".dst").c_str(), p.mods[m].dst);
        chk (inRange (p.mods[m].amt, -1, 1), (n + ".amt").c_str(), p.mods[m].amt);
    }

    chk (inRange (p.glide, 0, 2), "glide", p.glide);
    chk (p.poly >= 1 && p.poly <= kMaxVoices, "poly", p.poly);
    chk (inRange (p.velSens, 0, 1), "velSens", p.velSens);
    chk (inRange (p.master, 0, 1), "master", p.master);

    chk (inRange (p.fx.dist, 0, 1), "fx.dist", p.fx.dist);
    chk (inRange (p.fx.chorusRate, 0.05, 8), "fx.chorusRate", p.fx.chorusRate);
    chk (inRange (p.fx.chorusDepth, 0, 1), "fx.chorusDepth", p.fx.chorusDepth);
    chk (inRange (p.fx.chorusMix, 0, 1), "fx.chorusMix", p.fx.chorusMix);
    chk (inRange (p.fx.delayTime, 0.02, 1.5), "fx.delayTime", p.fx.delayTime);
    chk (inRange (p.fx.delayFeedback, 0, 0.9), "fx.delayFeedback", p.fx.delayFeedback);
    chk (inRange (p.fx.delayMix, 0, 1), "fx.delayMix", p.fx.delayMix);
    chk (inRange (p.fx.reverbSize, 0, 1), "fx.reverbSize", p.fx.reverbSize);
    chk (inRange (p.fx.reverbMix, 0, 1), "fx.reverbMix", p.fx.reverbMix);
    return bad;
}

int main (int argc, char** argv)
{
    const bool quick = (argc > 1 && std::strcmp (argv[1], "--quick") == 0);
    Result r;
    if (! quick) std::filesystem::create_directories ("render_out");

    std::printf ("SPECTRA DSP verification  (%.0f Hz, %d-sample blocks)\n", kSr, kBlock);

    // --- the wavetable pipeline itself ---
    {
        // A single-harmonic table must synthesise to a clean sine at 0.95 peak, which pins
        // down the FFT scaling convention in both directions.
        auto frames = buildFactoryHarmFrames (factoryTableIndexByName ("Sine"));
        auto table = buildMips (frames);
        check (r, table->numFrames == 1, "wavetable: sine table has one frame");

        float peak = 0.0f;
        double sumSq = 0.0;
        for (int i = 0; i < kFrame; ++i)
        {
            const float v = table->mips[0][size_t (i)];
            peak = std::max (peak, std::fabs (v));
            sumSq += double (v) * double (v);
        }
        const double rms = std::sqrt (sumSq / double (kFrame));
        check (r, std::fabs (peak - 0.95f) < 0.01f, "wavetable: normalised to 0.95",
               "peak=" + std::to_string (peak));
        // A sine's RMS is its peak / sqrt(2).
        check (r, std::fabs (rms - 0.95 / std::sqrt (2.0)) < 0.01, "wavetable: synthesises a sine",
               "rms=" + std::to_string (rms));

        // Guard samples must mirror the first sample of their frame, or interpolation wraps
        // into garbage at the end of every cycle.
        bool guardsOk = true;
        for (int k = 0; k < kMips; ++k)
            if (table->mips[k][size_t (kFrame)] != table->mips[k][0]) guardsOk = false;
        check (r, guardsOk, "wavetable: guard samples mirror the frame start");

        // Analysis must invert synthesis.
        std::vector<float> wave (size_t (kFrame), 0.0f);
        for (int i = 0; i < kFrame; ++i)
            wave[size_t (i)] = std::sin (2.0f * float (M_PI) * float (i) / float (kFrame));
        HarmFrame hf = analyzeFrame (wave);
        const float mag1 = std::hypot (hf.re[1], hf.im[1]);
        float restMax = 0.0f;
        for (int h = 2; h <= 64; ++h) restMax = std::max (restMax, std::hypot (hf.re[size_t (h)], hf.im[size_t (h)]));
        check (r, std::fabs (mag1 - 1.0f) < 0.01f, "wavetable: analysis finds the fundamental at unity",
               "mag=" + std::to_string (mag1));
        check (r, restMax < 0.01f, "wavetable: analysis finds no spurious harmonics",
               "max=" + std::to_string (restMax));
    }

    // --- table build cost, which is why this happens off the audio thread ---
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto frames = buildFactoryHarmFrames (0);
        auto warped = spectralWarp (frames, SpecMode::disperse, 0.5f);
        auto table = buildMips (warped);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
        std::printf ("  table build: %.1f ms for %d frames x %d mips\n", ms, table->numFrames, kMips);
        check (r, table->numFrames == 17, "wavetable: Basic Shapes has 17 morph frames",
               "frames=" + std::to_string (table->numFrames));
    }

    // --- every factory preset renders, sounds, and releases ---
    std::printf ("%-18s %8s %8s %10s\n", "preset", "peak", "rms", "state");
    std::printf ("---------------------------------------------------\n");
    for (const Params& p : factoryPresets())
    {
        SpectraEngine engine;
        engine.prepare (kSr, kBlock);
        engine.setParams (p);
        applyTables (engine, p);

        Rendered out = playNote (engine, 57, 0.85f, 2.0, 3.0);

        check (r, out.finite, p.name + ": finite output");
        check (r, out.peak > 0.005f, p.name + ": audible", "peak=" + std::to_string (out.peak));
        check (r, out.peak <= 2.0f, p.name + ": not blowing up", "peak=" + std::to_string (out.peak));

        const size_t win = size_t (kSr * 0.2);
        double loudest = 0.0, tail = 0.0;
        for (size_t s = 0; s + win < out.l.size(); s += win)
        {
            double sq = 0.0;
            for (size_t k = 0; k < win; ++k) sq += double (out.l[s + k]) * double (out.l[s + k]);
            loudest = std::max (loudest, sq);
        }
        for (size_t k = out.l.size() - win; k < out.l.size(); ++k) tail += double (out.l[k]) * double (out.l[k]);
        check (r, tail < loudest, p.name + ": releases",
               "loudest=" + std::to_string (loudest) + " tail=" + std::to_string (tail));

        std::printf ("%-18s %8.4f %8.5f %10s\n", p.name.c_str(), out.peak, out.rms,
                     (out.finite && out.peak > 0.005f && tail < loudest) ? "ok" : "OFF");

        if (! quick)
        {
            std::string file = p.name;
            for (auto& c : file) c = (c == ' ') ? '-' : char (std::tolower (c));
            writeWav ("render_out/" + file + ".wav", out.l, out.r, kSr);
        }
    }
    std::printf ("---------------------------------------------------\n");

    // --- pitch ---
    for (int note : { 45, 57, 69, 81 })
    {
        SpectraEngine engine;
        engine.prepare (kSr, kBlock);
        Params p = tuningPatch();
        engine.setParams (p);
        applyTables (engine, p);

        Rendered out = playNote (engine, note, 0.9f, 1.0, 0.05);
        const double predicted = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const size_t from = size_t (kSr * 0.2), count = size_t (kSr * 0.5);
        const double atPredicted = goertzel (out.l, from, count, predicted, kSr);
        double rival = 0.0;
        for (int st : { -2, -1, 1, 2 })
            rival = std::max (rival, goertzel (out.l, from, count, predicted * std::pow (2.0, st / 12.0), kSr));
        check (r, atPredicted > rival, "tuning: MIDI note " + std::to_string (note),
               "pred=" + std::to_string (predicted) + " rival=" + std::to_string (rival));
    }

    // --- semitone / fine offsets ---
    {
        SpectraEngine engine;
        engine.prepare (kSr, kBlock);
        Params p = tuningPatch();
        p.osc1.semi = -12;
        p.osc1.fine = 50.0f;
        engine.setParams (p);
        applyTables (engine, p);

        Rendered out = playNote (engine, 69, 0.9f, 1.0, 0.05);
        const double predicted = 440.0 * std::pow (2.0, -1.0) * std::pow (2.0, 50.0 / 1200.0);
        const size_t from = size_t (kSr * 0.2), count = size_t (kSr * 0.6);
        const double atPredicted = goertzel (out.l, from, count, predicted, kSr);
        const double rival = std::max (goertzel (out.l, from, count, predicted * 1.0595, kSr),
                                       goertzel (out.l, from, count, predicted / 1.0595, kSr));
        check (r, atPredicted > rival, "tuning: semitone and fine offsets",
               "pred=" + std::to_string (predicted));
    }

    // --- mip selection really band-limits high notes ---
    // A saw played near the top of the keyboard has almost no harmonics left below Nyquist. If
    // the mip pick were wrong, the ones above it would fold down and show up as strong energy
    // well below the fundamental -- which is exactly what this measures.
    {
        SpectraEngine engine;
        engine.prepare (kSr, kBlock);
        Params p = tuningPatch (0);          // Basic Shapes
        p.osc1.pos = 0.5f;                   // the saw frame
        engine.setParams (p);
        applyTables (engine, p);

        Rendered out = playNote (engine, 108, 0.9f, 1.0, 0.05);   // ~4186 Hz
        const size_t from = size_t (kSr * 0.2), count = size_t (kSr * 0.5);
        const double fundamental = goertzel (out.l, from, count, 4186.0, kSr);
        double foldedDown = 0.0;
        for (double f = 100.0; f < 3000.0; f += 137.0)
            foldedDown = std::max (foldedDown, goertzel (out.l, from, count, f, kSr));
        check (r, out.finite, "band-limiting: finite");
        check (r, foldedDown < fundamental * 0.25,
               "band-limiting: no aliased energy below the fundamental",
               "fund=" + std::to_string (fundamental) + " folded=" + std::to_string (foldedDown));
    }

    // --- every warp mode is bounded and audible ---
    for (int mode = 0; mode < kNumWarpModes; ++mode)
    {
        SpectraEngine engine;
        engine.prepare (kSr, kBlock);
        Params p = tuningPatch (0);
        p.osc1.pos = 0.5f;
        p.osc1.warpMode = WarpMode (mode);
        p.osc1.warp = 0.75f;
        engine.setParams (p);
        applyTables (engine, p);

        Rendered out = playNote (engine, 57, 0.9f, 0.8, 0.1);
        check (r, out.finite, "warp mode " + std::to_string (mode) + ": finite");
        check (r, out.peak > 0.005f, "warp mode " + std::to_string (mode) + ": audible",
               "peak=" + std::to_string (out.peak));
        check (r, out.peak <= 2.0f, "warp mode " + std::to_string (mode) + ": bounded",
               "peak=" + std::to_string (out.peak));
    }

    // --- every spectral mode changes the table and stays sane ---
    {
        auto base = buildFactoryHarmFrames (0);
        for (int mode = 1; mode < kNumSpecModes; ++mode)
        {
            auto warped = spectralWarp (base, SpecMode (mode), 0.6f);
            auto table = buildMips (warped);

            bool finite = true;
            float peak = 0.0f;
            double diff = 0.0;
            auto plain = buildMips (base);
            for (size_t i = 0; i < table->mips[0].size(); ++i)
            {
                const float v = table->mips[0][i];
                if (! std::isfinite (v)) { finite = false; break; }
                peak = std::max (peak, std::fabs (v));
                diff += std::fabs (double (v) - double (plain->mips[0][i]));
            }
            check (r, finite, "spectral mode " + std::to_string (mode) + ": finite table");
            check (r, peak > 0.1f, "spectral mode " + std::to_string (mode) + ": table is not empty",
                   "peak=" + std::to_string (peak));
            check (r, diff > 1.0, "spectral mode " + std::to_string (mode) + ": changes the table",
                   "diff=" + std::to_string (diff));
        }
    }

    // --- filter types differ from one another in the right direction ---
    {
        auto renderWith = [&] (FilterType type)
        {
            SpectraEngine engine;
            engine.prepare (kSr, kBlock);
            Params p = tuningPatch (0);
            p.osc1.pos = 0.5f;
            p.filter.on = true;
            p.filter.type = type;
            p.filter.cut = 800.0f;
            p.filter.res = 0.2f;
            engine.setParams (p);
            applyTables (engine, p);
            return playNote (engine, 33, 0.9f, 1.0, 0.05);   // ~55 Hz, plenty of harmonics
        };

        Rendered lp = renderWith (FilterType::lowpass);
        Rendered hp = renderWith (FilterType::highpass);
        const size_t from = size_t (kSr * 0.2), count = size_t (kSr * 0.5);

        const double lpLow  = goertzel (lp.l, from, count, 110.0, kSr);
        const double lpHigh = goertzel (lp.l, from, count, 4400.0, kSr);
        const double hpLow  = goertzel (hp.l, from, count, 110.0, kSr);
        const double hpHigh = goertzel (hp.l, from, count, 4400.0, kSr);

        check (r, lp.finite && hp.finite, "filter: finite");
        check (r, lpLow / std::max (1.0e-9, lpHigh) > hpLow / std::max (1.0e-9, hpHigh),
               "filter: lowpass keeps lows, highpass keeps highs",
               "lp=" + std::to_string (lpLow / std::max (1.0e-9, lpHigh))
                   + " hp=" + std::to_string (hpLow / std::max (1.0e-9, hpHigh)));
    }

    // --- unison detunes and widens ---
    {
        auto renderWith = [&] (int voices)
        {
            SpectraEngine engine;
            engine.prepare (kSr, kBlock);
            Params p = tuningPatch (0);
            p.osc1.pos = 0.5f;
            p.osc1.unison = voices;
            p.osc1.detune = 30.0f;
            p.osc1.spread = 1.0f;
            engine.setParams (p);
            applyTables (engine, p);
            return playNote (engine, 57, 0.9f, 1.0, 0.05);
        };
        Rendered one = renderWith (1);
        Rendered many = renderWith (7);
        check (r, many.finite, "unison: finite");

        // With one voice the two channels are identical; with seven spread across the field
        // they are not.
        double monoDiff = 0.0, wideDiff = 0.0;
        for (size_t i = 0; i < one.l.size(); ++i)
        {
            monoDiff += std::fabs (double (one.l[i]) - double (one.r[i]));
            wideDiff += std::fabs (double (many.l[i]) - double (many.r[i]));
        }
        check (r, wideDiff > monoDiff * 10.0 + 1.0, "unison: spreads across the stereo field",
               "mono=" + std::to_string (monoDiff) + " wide=" + std::to_string (wideDiff));
    }

    // --- the mod matrix does something ---
    {
        auto renderWith = [&] (float amount)
        {
            SpectraEngine engine;
            engine.prepare (kSr, kBlock);
            Params p = tuningPatch (0);
            p.osc1.pos = 0.5f;
            p.filter.on = true;
            p.filter.cut = 1200.0f;
            p.lfo1.rate = 4.0f;
            p.mods[0] = { 1, dCut, amount };   // LFO 1 -> Cutoff
            engine.setParams (p);
            applyTables (engine, p);
            return playNote (engine, 45, 0.9f, 1.5, 0.05);
        };
        Rendered still = renderWith (0.0f);
        Rendered swept = renderWith (0.9f);
        double diff = 0.0;
        for (size_t i = 0; i < still.l.size(); ++i)
            diff += std::fabs (double (still.l[i]) - double (swept.l[i]));
        check (r, swept.finite, "mod matrix: finite");
        check (r, diff > 100.0, "mod matrix: LFO to cutoff sweeps the filter",
               "diff=" + std::to_string (diff));
    }

    // --- polyphony limit and stealing ---
    {
        SpectraEngine engine;
        engine.prepare (kSr, kBlock);
        Params p = tuningPatch (0);
        p.poly = 4;
        engine.setParams (p);
        applyTables (engine, p);

        std::vector<float> l (size_t (kBlock), 0.0f), rr (size_t (kBlock), 0.0f);
        for (int i = 0; i < 8; ++i)
        {
            engine.noteOn (48 + i * 2, 0.9f);
            engine.render (l.data(), rr.data(), kBlock);
        }
        check (r, engine.getActiveVoiceCount() <= 5, "polyphony: the voice limit is enforced",
               "active=" + std::to_string (engine.getActiveVoiceCount()));

        bool finite = true;
        for (int i = 0; i < 200; ++i)
        {
            engine.noteOn (36 + (i * 7) % 60, 0.95f);
            engine.render (l.data(), rr.data(), kBlock);
            for (int k = 0; k < kBlock; ++k) if (! std::isfinite (l[size_t (k)])) finite = false;
        }
        check (r, finite, "polyphony: finite output under abuse");

        engine.panic();
        for (int i = 0; i < 40; ++i) engine.render (l.data(), rr.data(), kBlock);
        check (r, engine.getActiveVoiceCount() == 0, "panic: all voices retired",
               "active=" + std::to_string (engine.getActiveVoiceCount()));
    }

    // --- glide slides into the note ---
    {
        SpectraEngine engine;
        engine.prepare (kSr, kBlock);
        Params p = tuningPatch();
        p.glide = 0.6f;
        p.poly = 1;
        engine.setParams (p);
        applyTables (engine, p);

        std::vector<float> l (size_t (kBlock), 0.0f), rr (size_t (kBlock), 0.0f);
        engine.noteOn (48, 0.9f);
        for (int i = 0; i < 100; ++i) engine.render (l.data(), rr.data(), kBlock);
        engine.noteOff (48);
        engine.noteOn (72, 0.9f);

        Rendered out;
        out.l.assign (size_t (kSr * 0.5), 0.0f);
        out.r.assign (size_t (kSr * 0.5), 0.0f);
        for (int pos = 0; pos < int (out.l.size()); pos += kBlock)
            engine.render (out.l.data() + pos, out.r.data() + pos,
                           std::min (kBlock, int (out.l.size()) - pos));
        out.measure();

        const size_t count = size_t (kSr * 0.04);
        const double atOld = goertzel (out.l, 0, count, 440.0 * std::pow (2.0, (48 - 69) / 12.0), kSr);
        const double atNew = goertzel (out.l, 0, count, 440.0 * std::pow (2.0, (72 - 69) / 12.0), kSr);
        check (r, out.finite, "glide: finite");
        check (r, atOld > atNew, "glide: starts from the previous pitch",
               "old=" + std::to_string (atOld) + " new=" + std::to_string (atNew));
    }

    // --- table hot-swap while sounding ---
    // The audio thread must be able to pick up a table published mid-note without glitching.
    {
        SpectraEngine engine;
        engine.prepare (kSr, kBlock);
        Params p = tuningPatch (0);
        engine.setParams (p);
        applyTables (engine, p);

        Rendered out;
        out.l.assign (size_t (kSr * 2.0), 0.0f);
        out.r.assign (size_t (kSr * 2.0), 0.0f);
        engine.noteOn (57, 0.9f);

        int published = 0;
        for (int pos = 0; pos < int (out.l.size()); pos += kBlock)
        {
            if ((pos / kBlock) % 12 == 0 && published < 6)
            {
                auto frames = buildFactoryHarmFrames (published % 6);
                engine.publishTable (0, buildMips (frames));
                ++published;
            }
            engine.render (out.l.data() + pos, out.r.data() + pos,
                           std::min (kBlock, int (out.l.size()) - pos));
        }
        out.measure();
        check (r, out.finite, "table swap: finite while a note is sounding");
        check (r, out.peak > 0.005f, "table swap: still audible", "peak=" + std::to_string (out.peak));
    }

    // --- the dice ---
    // Every roll gets rendered and measured. This is the point of keeping randomizePatch() in
    // the JUCE-free DSP layer: a roll that produces a legal-looking patch which happens to be
    // silent, or which blows up the filter, is caught here rather than in a host.
    {
        constexpr int kRolls = 40;
        std::string rangeErr, excludeErr, modErr;
        int silent = 0, nonFinite = 0, loudest = 0;
        float loudestPeak = 0.0f;
        bool charactersSeen[kNumCharacters] = { false, false, false, false, false };

        Params base;                    // Init, i.e. what the plugin starts on
        base.master = 0.8f;
        base.poly = 8;
        base.velSens = 0.5f;

        for (int i = 0; i < kRolls; ++i)
        {
            Rng rng { uint32_t (i * 2654435761u + 12345u) };
            Character character {};
            const Params p = randomizePatch (base, rng, &character);
            charactersSeen[size_t (character)] = true;

            if (rangeErr.empty()) rangeErr = outOfRange (p);

            // Performance settings must ride through every roll untouched.
            if (excludeErr.empty()
                && (p.master != base.master || p.poly != base.poly || p.velSens != base.velSens))
                excludeErr = "roll " + std::to_string (i) + " moved a performance param";

            // One to three live routings, no destination wired twice.
            int live = 0;
            bool usedDst[kNumModDests] = {};
            for (const ModSlot& m : p.mods)
            {
                if (m.src == 0 || m.dst == 0)
                {
                    if (m.amt != 0.0f && modErr.empty()) modErr = "off slot has a non-zero amount";
                    continue;
                }
                if (usedDst[m.dst] && modErr.empty()) modErr = "destination wired twice";
                usedDst[m.dst] = true;
                ++live;
            }
            if ((live < 1 || live > 3) && modErr.empty())
                modErr = std::to_string (live) + " live routings";

            SpectraEngine engine;
            engine.prepare (kSr, kBlock);
            engine.setParams (p);
            applyTables (engine, p);

            // 2 seconds of hold covers the longest attack a roll can produce (~0.63 s).
            const Rendered out = playNote (engine, 48, 0.9f, 2.0, 1.0);
            if (! out.finite) ++nonFinite;
            else if (out.peak < 0.01f || out.rms < 1e-4) ++silent;

            if (out.finite && out.peak > loudestPeak) { loudestPeak = out.peak; loudest = i; }
        }

        check (r, rangeErr.empty(), "randomize: rolls stay in range", rangeErr);
        check (r, excludeErr.empty(), "randomize: performance params excluded", excludeErr);
        check (r, modErr.empty(), "randomize: mod matrix routings are sane", modErr);
        check (r, nonFinite == 0, "randomize: every roll renders finite output",
               std::to_string (nonFinite) + " of " + std::to_string (kRolls) + " blew up");
        check (r, silent == 0, "randomize: every roll is audible",
               std::to_string (silent) + " of " + std::to_string (kRolls) + " were silent");

        bool allCharacters = true;
        for (bool seen : charactersSeen) allCharacters = allCharacters && seen;
        check (r, allCharacters, "randomize: all five characters reachable");

        // The same seed must produce the same patch, or a roll you liked can never be recovered.
        Rng a { 777u }, b { 777u };
        Character ca {}, cb {};
        const Params pa = randomizePatch (base, a, &ca);
        const Params pb = randomizePatch (base, b, &cb);
        check (r, ca == cb && pa.filter.cut == pb.filter.cut && pa.osc1.pos == pb.osc1.pos
                  && pa.osc1.table == pb.osc1.table && pa.env1.a == pb.env1.a,
               "randomize: a seeded roll is deterministic");

        Rng c { 778u };
        Character cc {};
        const Params pc = randomizePatch (base, c, &cc);
        check (r, pc.osc1.pos != pa.osc1.pos || pc.filter.cut != pa.filter.cut,
               "randomize: different seeds give different patches");

        if (! quick)
        {
            // Write the loudest roll out so the dice can actually be listened to.
            Rng rng { uint32_t (loudest * 2654435761u + 12345u) };
            Character character {};
            const Params p = randomizePatch (base, rng, &character);
            SpectraEngine engine;
            engine.prepare (kSr, kBlock);
            engine.setParams (p);
            applyTables (engine, p);
            const Rendered out = playNote (engine, 48, 0.9f, 2.0, 1.5);
            writeWav (std::string ("render_out/random-") + characterName (character) + ".wav",
                      out.l, out.r, kSr);
            std::printf ("  note  loudest roll was #%d (%s), peak %.3f\n",
                         loudest, characterName (character), double (loudestPeak));
        }
    }

    std::printf ("---------------------------------------------------\n");
    std::printf ("%d passed, %d failed\n", r.passed, r.failed);
    if (! quick) std::printf ("WAVs written to render_out/\n");
    return r.failed == 0 ? 0 : 1;
}
