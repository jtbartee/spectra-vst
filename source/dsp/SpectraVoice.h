#pragma once
#include "Params.h"
#include "Wavetables.h"

namespace spectra {

/** ADSR from worklet.js. The coefficients are computed once per block by the engine, which is
 *  why they arrive as a separate struct rather than living in the envelope. */
struct EnvCoefs { float aInc = 0.001f, dK = 0.999f, s = 1.0f, rK = 0.999f; };

class Env
{
public:
    enum Stage { idle = 0, attack, decay, release };

    void trigger() { stage = attack; }
    void doRelease() { if (stage != idle) stage = release; }
    void kill() { stage = idle; v = 0.0f; }
    Stage getStage() const { return stage; }
    float value() const { return v; }

    inline float tick (const EnvCoefs& c)
    {
        switch (stage)
        {
            case attack:
                v += c.aInc;
                if (v >= 1.0f) { v = 1.0f; stage = decay; }
                break;
            case decay:
                v = c.s + (v - c.s) * c.dK;
                break;
            case release:
                v *= c.rK;
                if (v < 1.0e-4f) { v = 0.0f; stage = idle; }
                break;
            case idle:
            default:
                break;
        }
        return v;
    }

private:
    Stage stage = idle;
    float v = 0.0f;
};

/** Per-oscillator running state, recomputed at control rate and read per sample. */
struct OscState
{
    float phases[kMaxUnison] {};
    float incs[kMaxUnison] {};
    float gainL[kMaxUnison] {};
    float gainR[kMaxUnison] {};
    int   unison = 1;
    bool  on = false;
    const float* tbl = nullptr;   // the mip buffer chosen for this pitch
    int   base0 = 0, base1 = 0;   // frame-pair offsets
    float ff = 0.0f;              // frame interpolation fraction
    WarpMode warpMode = WarpMode::off;
    float wA = 0.0f;              // warp constant for this block
};

/** Shared, engine-level state a voice needs at control rate. */
struct EngineState
{
    const WavetableSet* tables[2] { nullptr, nullptr };
    float lfoGlobal[2] { 0.0f, 0.0f };
    float wheel = 0.0f;
};

class SpectraVoice
{
public:
    void prepare (double sampleRate, uint32_t seed);

    void noteOn (int note, float vel, const Params& p, int lastNote, uint64_t ageCounter);
    void noteOff();
    void kill() { active = false; env1.kill(); env2.kill(); }

    bool isActive() const { return active; }
    bool isGated() const { return gate; }
    int  getNote() const { return note; }
    uint64_t getAge() const { return age; }

    /** Control-rate update: glide, the mod matrix, oscillator and filter coefficients. */
    void control (const Params& p, const EngineState& eng);

    /** Renders `n` samples starting at `off`, adding into the buffers. */
    void run (float* outL, float* outR, int off, int n, const EnvCoefs& e1c, const EnvCoefs& e2c);

private:
    float lfoValue (int i, LfoShape shape, float globalPhase, bool retrig);

    float sr = 44100.0f;
    bool active = false, gate = false;
    int note = -1;
    uint64_t age = 0;
    float vel = 1.0f;
    float curNote = 60.0f, targetNote = 60.0f;

    Env env1, env2;
    OscState osc[2];

    float subPhase = 0.0f, subInc = 0.0f;
    int   subShape = 0;
    float subLevel = 0.0f, noiseLevel = 0.0f;

    float lfoPh[2] {}, lfoHeld[2] {}, lfoPrevPh[2] {};
    float randMod = 0.0f;
    float gain = 0.0f;

    // Stereo ZDF state-variable filter state.
    float ic1L = 0.0f, ic2L = 0.0f, ic1R = 0.0f, ic2R = 0.0f;
    float fG = 0.5f, fK = 1.0f, fA1 = 0.5f, fDrive = 0.0f;
    FilterType fType = FilterType::lowpass;
    bool fOn = false;

    Rng noiseRng { 0x1234567u };
    Rng randRng { 0x89abcdefu };

    float modScratch[kNumModDests] {};
};

} // namespace spectra
