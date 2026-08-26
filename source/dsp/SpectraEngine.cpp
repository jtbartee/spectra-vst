#include "SpectraEngine.h"

namespace spectra {

void SpectraEngine::prepare (double sampleRate, int maximumBlockSize)
{
    sr = sampleRate;

    for (int i = 0; i < kMaxVoices; ++i)
        voices[size_t (i)].prepare (sampleRate, 0xC0FFEE01u + uint32_t (i) * 0x9E3779B9u);

    ageCounter = 0;
    lastNote = -1;
    lfoGlobal[0] = lfoGlobal[1] = 0.0f;

    distortion.prepare (sampleRate);
    chorus.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    comp.prepare (sampleRate, -6.0f, 6.0f, 6.0f, 0.002f, 0.15f);

    scratchL.assign (size_t (std::max (maximumBlockSize, 1024)), 0.0f);
    scratchR.assign (size_t (std::max (maximumBlockSize, 1024)), 0.0f);

    updateEnvCoefs();
}

void SpectraEngine::setParams (const Params& p)
{
    params = p;
    updateEnvCoefs();
    distortion.setParams (p.fx);
    chorus.setParams (p.fx);
    delay.setParams (p.fx);
    reverb.setParams (p.fx);
}

void SpectraEngine::publishTable (int osc, std::unique_ptr<WavetableSet> table)
{
    if (osc < 0 || osc > 1 || table == nullptr) return;
    tables[osc].publish (std::move (table));
}

void SpectraEngine::updateEnvCoefs()
{
    auto set = [this] (EnvCoefs& c, const EnvParams& e)
    {
        c.aInc = 1.0f / (std::max (0.0005f, e.a) * float (sr));
        c.dK = std::exp (-4.0f / (std::max (0.001f, e.d) * float (sr)));
        c.s = clampf (e.s, 0.0f, 1.0f);
        c.rK = std::exp (-5.0f / (std::max (0.001f, e.r) * float (sr)));
    };
    set (e1c, params.env1);
    set (e2c, params.env2);
}

// -- note handling (port of SpectraEngine.noteOn / noteOff) -------------------

void SpectraEngine::noteOn (int midiNote, float velocity01)
{
    const int poly = std::max (1, std::min (kMaxVoices, params.poly));
    const float vel = clampf (velocity01, 0.0f, 1.0f);

    // Retrigger the same note if it is still held or releasing, then take a free voice, then
    // steal the oldest.
    SpectraVoice* v = nullptr;
    for (auto& x : voices)
        if (x.isActive() && x.getNote() == midiNote) { v = &x; break; }
    if (v == nullptr)
        for (auto& x : voices)
            if (! x.isActive()) { v = &x; break; }
    if (v == nullptr)
    {
        SpectraVoice* oldest = nullptr;
        for (auto& x : voices)
            if (oldest == nullptr || x.getAge() < oldest->getAge()) oldest = &x;
        v = oldest;
    }
    if (v == nullptr) return;

    // Enforce the polyphony limit: if the rest of the pool is already at the cap, retire its
    // oldest member.
    int activeOthers = 0;
    SpectraVoice* oldestOther = nullptr;
    for (auto& x : voices)
    {
        if (! x.isActive() || &x == v) continue;
        ++activeOthers;
        if (oldestOther == nullptr || x.getAge() < oldestOther->getAge()) oldestOther = &x;
    }
    if (activeOthers >= poly && oldestOther != nullptr) oldestOther->kill();

    v->noteOn (midiNote, vel, params, lastNote, ageCounter++);
    lastNote = midiNote;
}

void SpectraEngine::noteOff (int midiNote)
{
    for (auto& v : voices)
        if (v.isActive() && v.getNote() == midiNote && v.isGated()) v.noteOff();
}

void SpectraEngine::panic()
{
    for (auto& v : voices) v.kill();
}

int SpectraEngine::getActiveVoiceCount() const
{
    int n = 0;
    for (const auto& v : voices) if (v.isActive()) ++n;
    return n;
}

uint32_t SpectraEngine::getVoiceActiveMask() const
{
    uint32_t mask = 0;
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[size_t (i)].isActive()) mask |= (1u << uint32_t (i));
    return mask;
}

// -- rendering ---------------------------------------------------------------

void SpectraEngine::render (float* left, float* right, int numSamples)
{
    if (numSamples <= 0) return;
    if (int (scratchL.size()) < numSamples)
    {
        // Only reachable if the host raises its block size without calling prepareToPlay,
        // which it should not; growing here is safer than writing out of bounds.
        scratchL.assign (size_t (numSamples), 0.0f);
        scratchR.assign (size_t (numSamples), 0.0f);
    }

    std::fill (scratchL.begin(), scratchL.begin() + numSamples, 0.0f);
    std::fill (scratchR.begin(), scratchR.begin() + numSamples, 0.0f);

    EngineState eng;
    eng.tables[0] = &tables[0].get();
    eng.tables[1] = &tables[1].get();
    eng.wheel = wheel;

    for (int off = 0; off < numSamples; off += kCtrl)
    {
        const int len = std::min (kCtrl, numSamples - off);

        // Free-running global LFO phases, shared by every voice that is not set to retrigger.
        for (int i = 0; i < 2; ++i)
        {
            const float rate = (i == 0 ? params.lfo1 : params.lfo2).rate;
            lfoGlobal[i] += (rate * float (len)) / float (sr);
            lfoGlobal[i] -= std::floor (lfoGlobal[i]);
            eng.lfoGlobal[i] = lfoGlobal[i];
        }

        for (auto& v : voices)
        {
            if (! v.isActive()) continue;
            v.control (params, eng);
            v.run (scratchL.data(), scratchR.data(), off, len, e1c, e2c);
        }
    }

    // Master gain and soft clip happen inside the engine, before the FX chain -- the same
    // place the web version applies them, at the end of the worklet's process().
    const float m = params.master;
    for (int i = 0; i < numSamples; ++i)
    {
        float l = fastTanh (scratchL[size_t (i)] * m);
        float r = fastTanh (scratchR[size_t (i)] * m);

        distortion.process (l, r);
        chorus.process (l, r);
        delay.process (l, r);
        reverb.process (l, r);
        comp.process (l, r);

        left[i] = l;
        right[i] = r;
    }
}

} // namespace spectra
