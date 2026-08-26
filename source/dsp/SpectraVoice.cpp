#include "SpectraVoice.h"

namespace spectra {

void SpectraVoice::prepare (double sampleRate, uint32_t seed)
{
    sr = float (sampleRate);
    noiseRng = Rng { seed };
    randRng = Rng { seed ^ 0x5a5a5a5au };
    active = false;
    gate = false;
    note = -1;
    env1.kill();
    env2.kill();
    ic1L = ic2L = ic1R = ic2R = 0.0f;
    for (auto& o : osc)
        for (int u = 0; u < kMaxUnison; ++u) o.phases[u] = 0.0f;
}

void SpectraVoice::noteOn (int noteNumber, float velocity, const Params& p, int lastNote,
                           uint64_t ageCounter)
{
    active = true;
    gate = true;
    note = noteNumber;
    vel = velocity;
    age = ageCounter;
    targetNote = float (noteNumber);

    // Glide starts from the previous note; without glide the voice simply starts in tune.
    curNote = (p.glide > 0.001f && lastNote >= 0) ? float (lastNote) : float (noteNumber);

    env1.trigger();
    env2.trigger();

    randMod = randRng.bipolar();
    lfoPh[0] = lfoPh[1] = 0.0f;
    lfoPrevPh[0] = lfoPrevPh[1] = 0.0f;
    lfoHeld[0] = randRng.bipolar();
    lfoHeld[1] = randRng.bipolar();

    for (int o = 0; o < 2; ++o)
    {
        const float rnd = (o == 0 ? p.osc1 : p.osc2).rand;
        for (int u = 0; u < kMaxUnison; ++u)
            osc[o].phases[u] = randRng.next01() < rnd ? randRng.next01() : 0.0f;
    }

    subPhase = 0.0f;
    ic1L = ic2L = ic1R = ic2R = 0.0f;
}

void SpectraVoice::noteOff()
{
    gate = false;
    env1.doRelease();
    env2.doRelease();
}

float SpectraVoice::lfoValue (int i, LfoShape shape, float globalPhase, bool retrig)
{
    const float p = retrig ? lfoPh[i] : globalPhase;
    switch (shape)
    {
        case LfoShape::sine:     return std::sin (2.0f * float (M_PI) * p);
        case LfoShape::triangle: return 4.0f * std::fabs (p - 0.5f) - 1.0f;
        case LfoShape::saw:      return 2.0f * p - 1.0f;
        case LfoShape::square:   return p < 0.5f ? 1.0f : -1.0f;
        case LfoShape::sampleHold:
            // Resample on each phase wrap.
            if (p < lfoPrevPh[i]) lfoHeld[i] = randRng.bipolar();
            lfoPrevPh[i] = p;
            return lfoHeld[i];
    }
    return 0.0f;
}

void SpectraVoice::control (const Params& P, const EngineState& eng)
{
    // Glide.
    if (std::fabs (targetNote - curNote) > 1.0e-4f && P.glide > 0.001f)
    {
        const float gk = 1.0f - std::exp (-(float (kCtrl) / sr) * (6.0f / P.glide));
        curNote += (targetNote - curNote) * gk;
    }
    else
    {
        curNote = targetNote;
    }

    // Per-voice LFO phases.
    for (int i = 0; i < 2; ++i)
    {
        const float rate = (i == 0 ? P.lfo1 : P.lfo2).rate;
        lfoPh[i] += (rate * float (kCtrl)) / sr;
        lfoPh[i] -= std::floor (lfoPh[i]);
    }

    // Mod matrix.
    float* mods = modScratch;
    std::fill (mods, mods + kNumModDests, 0.0f);
    for (int m = 0; m < 4; ++m)
    {
        const ModSlot& slot = P.mods[size_t (m)];
        if (slot.src == 0 || slot.dst == 0 || slot.amt == 0.0f) continue;
        float v = 0.0f;
        switch (slot.src)
        {
            case 1: v = lfoValue (0, P.lfo1.shape, eng.lfoGlobal[0], P.lfo1.retrig); break;
            case 2: v = lfoValue (1, P.lfo2.shape, eng.lfoGlobal[1], P.lfo2.retrig); break;
            case 3: v = env2.value(); break;
            case 4: v = vel; break;
            case 5: v = eng.wheel; break;
            case 6: v = clampf (float (note - 60) / 24.0f, -1.0f, 1.0f); break;
            case 7: v = randMod; break;
            default: break;
        }
        if (slot.dst > 0 && slot.dst < kNumModDests) mods[slot.dst] += v * slot.amt;
    }

    const float pitchMod = mods[dPitch] * 24.0f;
    gain = (1.0f - P.velSens * (1.0f - vel)) * std::max (0.0f, 1.0f + mods[dAmp]);

    // Oscillators.
    for (int o = 0; o < 2; ++o)
    {
        const OscParams& op = (o == 0) ? P.osc1 : P.osc2;
        OscState& os = osc[o];
        os.on = op.on;
        if (! os.on) continue;

        const WavetableSet* table = eng.tables[o];
        if (table == nullptr) { os.on = false; continue; }

        const float level = clampf (op.level + mods[o == 0 ? dO1Lvl : dO2Lvl], 0.0f, 1.5f);
        const float freq = 440.0f * std::pow (2.0f,
            (curNote - 69.0f + float (op.semi) + op.fine / 100.0f + pitchMod) / 12.0f);
        const float baseInc = clampf (freq / sr, 0.0f, 0.45f);

        // Mip pick: the highest-resolution level whose harmonics stay below Nyquist.
        const int maxH = std::max (1, int ((0.45f * sr) / std::max (1.0f, freq)));
        int mip = 0;
        while (mip < kMips - 1 && (kHarmonics >> mip) > maxH) ++mip;
        os.tbl = table->mips[mip].data();

        // Wavetable frame pair.
        const float pos = clampf (op.pos + mods[o == 0 ? dO1Pos : dO2Pos], 0.0f, 1.0f);
        const float fpos = pos * float (table->numFrames - 1);
        const int f0 = int (std::floor (fpos));
        os.base0 = f0 * kStride;
        os.base1 = std::min (f0 + 1, table->numFrames - 1) * kStride;
        os.ff = fpos - float (f0);

        // Time-domain warp constants.
        os.warpMode = op.warpMode;
        const float wa = clampf (op.warp + mods[o == 0 ? dO1Warp : dO2Warp], -1.0f, 1.0f);
        switch (os.warpMode)
        {
            case WarpMode::bend:     os.wA = std::pow (2.0f, 3.0f * wa); break;
            case WarpMode::sync:     os.wA = 1.0f + 15.0f * std::fabs (wa); break;
            case WarpMode::formant:  os.wA = 1.0f + 7.0f * std::fabs (wa); break;
            case WarpMode::quantize: os.wA = 2.0f + std::floor (std::fabs (wa) * 30.0f); break;
            case WarpMode::mirror:   os.wA = std::fabs (wa); break;
            case WarpMode::squeeze:  os.wA = clampf (0.5f + wa * 0.45f, 0.03f, 0.97f); break;
            case WarpMode::off:
            default: break;
        }

        // Unison: detune ratios and stereo placement.
        const int U = std::max (1, std::min (kMaxUnison, int (std::lround (float (op.unison)))));
        os.unison = U;
        const float detune = op.detune, spread = op.spread, blendP = op.blend;
        const float basePan = clampf (op.pan + mods[dPan], -1.0f, 1.0f);

        float wSum = 0.0f;
        for (int u = 0; u < U; ++u)
        {
            const float d = (U == 1) ? 0.0f : (float (u) / float (U - 1)) * 2.0f - 1.0f;
            os.incs[u] = baseInc * std::pow (2.0f, (detune * d) / 1200.0f);
            const float w = (U == 1) ? 1.0f
                                     : (1.0f - blendP) * (1.0f - std::fabs (d) * 0.85f) + blendP;
            const float pan = clampf (basePan + spread * d, -1.0f, 1.0f);
            const float a = ((pan + 1.0f) * float (M_PI)) / 4.0f;
            os.gainL[u] = w * std::cos (a);
            os.gainR[u] = w * std::sin (a);
            wSum += w * w;
        }
        const float norm = level / std::sqrt (std::max (1.0e-9f, wSum));
        for (int u = 0; u < U; ++u) { os.gainL[u] *= norm; os.gainR[u] *= norm; }
    }

    // Sub and noise.
    subLevel = P.subLevel;
    noiseLevel = P.noiseLevel;
    if (subLevel > 0.0f)
    {
        const int oct = P.subOct + 1;
        const float f = 440.0f * std::pow (2.0f, (curNote - 69.0f + pitchMod) / 12.0f)
                      / std::pow (2.0f, float (oct));
        subInc = clampf (f / sr, 0.0f, 0.45f);
        subShape = P.subShape;
    }

    // Filter coefficients.
    fOn = P.filter.on;
    if (fOn)
    {
        const float keyOct = (float (note - 60) / 12.0f) * P.filter.key;
        const float envOct = env2.value() * P.filter.env * 6.0f;
        const float modOct = mods[dCut] * 5.0f;
        const float fc = clampf (P.filter.cut * std::pow (2.0f, keyOct + envOct + modOct),
                                 20.0f, sr * 0.45f);
        const float res = clampf (P.filter.res + mods[dRes], 0.0f, 1.0f);
        fG = std::tan ((float (M_PI) * fc) / sr);
        fK = 2.0f - 1.9f * res;
        fA1 = 1.0f / (1.0f + fG * (fG + fK));
        fType = P.filter.type;
        fDrive = P.filter.drive;
    }
}

void SpectraVoice::run (float* outL, float* outR, int off, int n,
                        const EnvCoefs& e1c, const EnvCoefs& e2c)
{
    const float g = fG, k = fK, a1 = fA1;
    const float driveGain = 1.0f + 3.0f * fDrive;
    const float driveComp = 1.0f / std::sqrt (driveGain);

    for (int s = 0; s < n; ++s)
    {
        const float e1 = env1.tick (e1c);
        env2.tick (e2c);
        if (env1.getStage() == Env::idle) { active = false; return; }

        float l = 0.0f, r = 0.0f;

        for (int oi = 0; oi < 2; ++oi)
        {
            OscState& os = osc[oi];
            if (! os.on || os.tbl == nullptr) continue;

            const float* tbl = os.tbl;
            const int b0 = os.base0, b1 = os.base1;
            const float ff = os.ff;
            const WarpMode mode = os.warpMode;
            const float wA = os.wA;

            for (int u = 0; u < os.unison; ++u)
            {
                float p = os.phases[u] + os.incs[u];
                if (p >= 1.0f) p -= 1.0f;
                os.phases[u] = p;

                float wp = p;
                switch (mode)
                {
                    case WarpMode::bend:     wp = std::pow (p, wA); break;
                    case WarpMode::sync:     wp = p * wA; wp -= std::floor (wp); break;
                    case WarpMode::formant:  wp = p * wA; if (wp > 1.0f) wp = 0.99999f; break;
                    case WarpMode::quantize: wp = std::floor (p * wA) / wA; break;
                    case WarpMode::mirror:
                    {
                        const float m = 1.0f - std::fabs (1.0f - 2.0f * p);
                        wp = p + (m - p) * wA;
                        break;
                    }
                    case WarpMode::squeeze:
                        wp = p < wA ? (0.5f * p) / wA : 0.5f + (0.5f * (p - wA)) / (1.0f - wA);
                        break;
                    case WarpMode::off:
                    default: break;
                }

                const float x = wp * float (kFrame);
                const int i0 = int (x);
                const float fx = x - float (i0);
                const float sa = tbl[b0 + i0], sb = tbl[b0 + i0 + 1];
                const float sc = tbl[b1 + i0], sd = tbl[b1 + i0 + 1];
                const float v0 = sa + (sb - sa) * fx;
                const float v1 = sc + (sd - sc) * fx;
                const float v = v0 + (v1 - v0) * ff;
                l += v * os.gainL[u];
                r += v * os.gainR[u];
            }
        }

        if (subLevel > 0.0f)
        {
            float sp = subPhase + subInc;
            if (sp >= 1.0f) sp -= 1.0f;
            subPhase = sp;
            float sv;
            switch (subShape)
            {
                case 1:  sv = 4.0f * std::fabs (sp - 0.5f) - 1.0f; break;
                case 2:  sv = sp < 0.5f ? 1.0f : -1.0f; break;
                default: sv = std::sin (2.0f * float (M_PI) * sp); break;
            }
            const float svl = sv * subLevel * 0.8f;
            l += svl; r += svl;
        }

        if (noiseLevel > 0.0f)
        {
            const float nv = noiseRng.nextNoise() * noiseLevel * 0.5f;
            l += nv; r += nv;
        }

        if (fOn)
        {
            if (fDrive > 0.0f)
            {
                l = fastTanh (l * driveGain) * driveComp;
                r = fastTanh (r * driveGain) * driveComp;
            }
            // ZDF state-variable filter (Zavalishin TPT), per channel.
            float v1 = a1 * (ic1L + g * (l - ic2L));
            float v2 = ic2L + g * v1;
            ic1L = 2.0f * v1 - ic1L;
            ic2L = 2.0f * v2 - ic2L;
            l = fType == FilterType::lowpass  ? v2
              : fType == FilterType::bandpass ? v1
              : fType == FilterType::highpass ? l - k * v1 - v2
                                              : l - k * v1;

            v1 = a1 * (ic1R + g * (r - ic2R));
            v2 = ic2R + g * v1;
            ic1R = 2.0f * v1 - ic1R;
            ic2R = 2.0f * v2 - ic2R;
            r = fType == FilterType::lowpass  ? v2
              : fType == FilterType::bandpass ? v1
              : fType == FilterType::highpass ? r - k * v1 - v2
                                              : r - k * v1;
        }

        const float amp = e1 * gain;
        outL[off + s] += l * amp;
        outR[off + s] += r * amp;
    }
}

} // namespace spectra
