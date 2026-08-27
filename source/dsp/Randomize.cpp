#include "Randomize.h"
#include "Wavetables.h"
#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace spectra {

namespace {

struct Range { float lo, hi; };
struct EnvRanges { Range a, d, s, r; };

/** One character's slice of the roll. Field for field the CHARACTER_TABLE entries in
 *  js/randomize.js. Envelope and cutoff ranges are knob positions (t in 0..1) rather than
 *  seconds or hertz, because those params are exponentially scaled -- drawing a time uniformly
 *  in seconds would put almost every roll in the multi-second range. */
struct CharacterSpec
{
    const char* name;
    EnvRanges env1, env2;
    Range cut, res, fltEnv, key;
    int unisonLo, unisonHi;
    Range detune, sub;
    float noiseChance;
    std::array<int, 5> semiChoices;
    int numSemiChoices;
    float glideChance;
    Range chorusMix, delayMix, reverbMix;
    float driveChance;
};

constexpr std::array<CharacterSpec, kNumCharacters> kCharacters { {
    { "Bass",
      { { 0.0f, 0.05f }, { 0.35f, 0.6f }, { 0.4f, 0.85f }, { 0.15f, 0.35f } },
      { { 0.0f, 0.04f }, { 0.3f, 0.55f }, { 0.0f, 0.25f }, { 0.15f, 0.3f } },
      { 0.25f, 0.5f }, { 0.1f, 0.5f }, { 0.25f, 0.75f }, { 0.0f, 0.4f },
      1, 3, { 4.0f, 30.0f }, { 0.35f, 0.8f }, 0.15f,
      { 0, 0, -12, 0, 0 }, 3, 0.3f,
      { 0.0f, 0.15f }, { 0.0f, 0.12f }, { 0.02f, 0.14f }, 0.5f },

    { "Pluck",
      { { 0.0f, 0.02f }, { 0.5f, 0.72f }, { 0.0f, 0.2f }, { 0.2f, 0.45f } },
      { { 0.0f, 0.02f }, { 0.4f, 0.62f }, { 0.0f, 0.15f }, { 0.15f, 0.4f } },
      { 0.4f, 0.7f }, { 0.15f, 0.55f }, { 0.3f, 0.85f }, { 0.2f, 0.7f },
      1, 4, { 6.0f, 40.0f }, { 0.0f, 0.3f }, 0.25f,
      { 0, 0, 0, 12, 0 }, 4, 0.1f,
      { 0.0f, 0.3f }, { 0.05f, 0.35f }, { 0.08f, 0.3f }, 0.3f },

    { "Pad",
      { { 0.35f, 0.62f }, { 0.5f, 0.75f }, { 0.6f, 0.95f }, { 0.5f, 0.75f } },
      { { 0.2f, 0.55f }, { 0.45f, 0.7f }, { 0.3f, 0.8f }, { 0.4f, 0.7f } },
      { 0.45f, 0.75f }, { 0.0f, 0.35f }, { -0.2f, 0.5f }, { 0.0f, 0.5f },
      3, 8, { 12.0f, 55.0f }, { 0.0f, 0.35f }, 0.3f,
      { 0, 0, 0, 12, -12 }, 5, 0.15f,
      { 0.15f, 0.5f }, { 0.05f, 0.35f }, { 0.2f, 0.5f }, 0.15f },

    { "Lead",
      { { 0.0f, 0.12f }, { 0.35f, 0.6f }, { 0.5f, 0.9f }, { 0.2f, 0.45f } },
      { { 0.0f, 0.1f }, { 0.3f, 0.6f }, { 0.2f, 0.6f }, { 0.2f, 0.4f } },
      { 0.5f, 0.8f }, { 0.1f, 0.5f }, { 0.0f, 0.6f }, { 0.2f, 0.8f },
      2, 6, { 8.0f, 45.0f }, { 0.0f, 0.4f }, 0.2f,
      { 0, 0, 0, 12, 7 }, 5, 0.45f,
      { 0.05f, 0.35f }, { 0.08f, 0.4f }, { 0.08f, 0.35f }, 0.45f },

    { "Texture",
      { { 0.3f, 0.65f }, { 0.5f, 0.8f }, { 0.4f, 0.9f }, { 0.55f, 0.8f } },
      { { 0.15f, 0.6f }, { 0.4f, 0.75f }, { 0.2f, 0.8f }, { 0.4f, 0.75f } },
      { 0.4f, 0.8f }, { 0.0f, 0.45f }, { -0.5f, 0.5f }, { 0.0f, 0.6f },
      2, 8, { 15.0f, 70.0f }, { 0.0f, 0.3f }, 0.5f,
      { 0, 0, 12, -12, 7 }, 5, 0.2f,
      { 0.1f, 0.45f }, { 0.15f, 0.5f }, { 0.25f, 0.6f }, 0.35f },
} };

/** Mod-matrix routings worth hearing. A uniform src/dst draw mostly lands on Off or wires an
 *  envelope to pan; these are pairs that do something musical. `max` caps the amount so a
 *  routing modulates rather than destroys -- pitch especially, where anything past a few
 *  percent is just out of tune. Mirrors MOD_ROUTINGS in js/randomize.js. */
struct Routing { int src, dst; float max; };

constexpr std::array<Routing, 18> kRoutings { {
    { 1, dO1Pos,  0.6f  },   // LFO 1  -> Osc1 Pos
    { 1, dO2Pos,  0.6f  },   // LFO 1  -> Osc2 Pos
    { 1, dCut,    0.5f  },   // LFO 1  -> Cutoff
    { 1, dPan,    0.7f  },   // LFO 1  -> Pan
    { 2, dO1Warp, 0.5f  },   // LFO 2  -> Osc1 Warp
    { 2, dCut,    0.45f },   // LFO 2  -> Cutoff
    { 2, dO1Pos,  0.4f  },   // LFO 2  -> Osc1 Pos
    { 3, dO1Pos,  0.7f  },   // Env 2  -> Osc1 Pos
    { 3, dO1Warp, 0.6f  },   // Env 2  -> Osc1 Warp
    { 3, dCut,    0.6f  },   // Env 2  -> Cutoff
    { 4, dCut,    0.5f  },   // Vel    -> Cutoff
    { 4, dO1Pos,  0.4f  },   // Vel    -> Osc1 Pos
    { 5, dO1Pos,  0.7f  },   // Wheel  -> Osc1 Pos
    { 5, dO1Warp, 0.6f  },   // Wheel  -> Osc1 Warp
    { 5, dCut,    0.5f  },   // Wheel  -> Cutoff
    { 6, dCut,    0.5f  },   // Note   -> Cutoff
    { 7, dO1Pos,  0.25f },   // Random -> Osc1 Pos
    { 7, dPitch,  0.04f },   // Random -> Pitch (analogue drift)
} };

// Warp/spectral modes worth pairing with a real amount. Off stays in the draw so a roll can
// land on a clean wavetable, which some of the best ones do.
constexpr std::array<int, 10> kWarpPicks { 0, 0, 1, 1, 2, 3, 3, 4, 5, 6 };
constexpr std::array<int, 9>  kSpecPicks { 0, 0, 0, 1, 2, 3, 4, 5, 6 };

/** Thin sugar over Rng so the port below reads like the JavaScript it came from. */
struct Roll
{
    Rng& rng;

    float uni()                     { return rng.next01(); }
    bool  chance (float p)          { return uni() < p; }
    float rand (float lo, float hi) { return lo + uni() * (hi - lo); }
    float randRange (Range r)       { return rand (r.lo, r.hi); }

    int randInt (int lo, int hi)                 // inclusive at both ends
    {
        const int span = hi - lo + 1;
        const int i = int (uni() * float (span));
        return lo + (i < span ? i : span - 1);
    }

    template <typename Container>
    auto choice (const Container& c) -> decltype (c[0])
    {
        const int n = int (c.size());
        const int i = int (uni() * float (n));
        return c[i < n ? i : n - 1];
    }

    /** params.js tToValue on an exponential knob. */
    float expAt (float t, float lo, float hi) { return tToValueExp (t, lo, hi); }
};

} // namespace

const char* characterName (Character c)
{
    const int i = int (c);
    return (i >= 0 && i < kNumCharacters) ? kCharacters[size_t (i)].name : "Random";
}

Params randomizePatch (const Params& current, Rng& rng, Character* outCharacter)
{
    Roll r { rng };
    Params p = current;   // anything the roll does not name keeps its current value

    const int ci = r.randInt (0, kNumCharacters - 1);
    const CharacterSpec& c = kCharacters[size_t (ci)];
    if (outCharacter != nullptr) *outCharacter = Character (ci);
    p.name = c.name;

    // --- oscillators --------------------------------------------------------
    // Osc 1 always sounds; Osc 2 joins most of the time, and when it does it sits under Osc 1
    // rather than fighting it.
    const bool twoOsc = r.chance (0.7f);
    p.osc1.on = true;
    p.osc2.on = twoOsc;

    for (OscParams* o : { &p.osc1, &p.osc2 })
    {
        o->pos = r.rand (0.0f, 1.0f);
        o->warpMode = WarpMode (r.choice (kWarpPicks));
        // Bend and Squeeze are the two that read as bipolar; the rest sound best pushed in one
        // direction, so the sign is drawn rather than the whole range.
        o->warp = o->warpMode == WarpMode::off
                    ? 0.0f
                    : (r.chance (0.5f) ? r.rand (0.15f, 1.0f) : r.rand (-1.0f, -0.15f));
        o->specMode = SpecMode (r.choice (kSpecPicks));
        o->spec = o->specMode == SpecMode::off ? 0.0f : r.rand (0.15f, 0.9f);
        o->unison = r.randInt (c.unisonLo, c.unisonHi);
        o->detune = o->unison > 1 ? r.randRange (c.detune) : r.rand (0.0f, 12.0f);
        o->spread = r.rand (0.4f, 1.0f);
        o->blend  = r.rand (0.45f, 1.0f);
        o->rand   = r.chance (0.75f) ? 1.0f : r.rand (0.0f, 0.5f);
        o->pan    = r.chance (0.6f) ? 0.0f : r.rand (-0.6f, 0.6f);
    }

    p.osc1.semi  = c.semiChoices[size_t (r.randInt (0, c.numSemiChoices - 1))];
    p.osc1.fine  = r.chance (0.6f) ? 0.0f : r.rand (-8.0f, 8.0f);
    p.osc1.level = r.rand (0.6f, 0.9f);

    // Osc 2 lands on a real interval against Osc 1, not an arbitrary semitone.
    constexpr std::array<int, 7> kIntervals { 0, 0, 0, 7, 12, -12, 5 };
    p.osc2.semi  = std::max (-24, std::min (24, p.osc1.semi + r.choice (kIntervals)));
    p.osc2.fine  = r.chance (0.4f) ? 0.0f : r.rand (-14.0f, 14.0f);
    p.osc2.level = twoOsc ? r.rand (0.25f, 0.7f) : 0.0f;

    // Two wavetables that differ give the morph somewhere to go.
    const int numTables = int (factoryTableNames().size());
    p.osc1.table = r.randInt (0, numTables - 1);
    p.osc2.table = r.chance (0.55f) ? p.osc1.table : r.randInt (0, numTables - 1);

    // --- sub + noise --------------------------------------------------------
    p.subLevel = r.chance (0.45f) ? 0.0f : r.randRange (c.sub);
    constexpr std::array<int, 3> kSubOcts { 0, 0, 1 };
    p.subOct   = r.choice (kSubOcts);
    p.subShape = r.randInt (0, 2);
    p.noiseLevel = r.chance (1.0f - c.noiseChance) ? 0.0f : r.rand (0.03f, 0.2f);

    // --- filter -------------------------------------------------------------
    constexpr std::array<int, 7> kFilterPicks { 0, 0, 0, 0, 1, 2, 3 };   // lowpass most of the time
    p.filter.on    = true;
    p.filter.type  = FilterType (r.choice (kFilterPicks));
    p.filter.cut   = r.expAt (r.randRange (c.cut), 20.0f, 20000.0f);
    p.filter.res   = r.randRange (c.res);
    p.filter.drive = r.chance (1.0f - c.driveChance) ? 0.0f : r.rand (0.1f, 0.6f);
    p.filter.env   = r.randRange (c.fltEnv);
    p.filter.key   = r.randRange (c.key);

    // --- envelopes ----------------------------------------------------------
    const std::array<std::pair<EnvParams*, const EnvRanges*>, 2> envs {
        std::make_pair (&p.env1, &c.env1), std::make_pair (&p.env2, &c.env2) };
    for (const auto& e : envs)
    {
        e.first->a = r.expAt (r.randRange (e.second->a), 0.001f, 20.0f);
        e.first->d = r.expAt (r.randRange (e.second->d), 0.001f, 20.0f);
        e.first->s = r.randRange (e.second->s);
        e.first->r = r.expAt (r.randRange (e.second->r), 0.001f, 20.0f);
    }

    // A short decay landing on a near-zero sustain is a click rather than a note. The character
    // ranges are chosen to avoid it, but this is the guarantee.
    if (p.env1.s < 0.1f && p.env1.d < 0.12f) p.env1.d = r.rand (0.12f, 0.4f);

    // --- LFOs ---------------------------------------------------------------
    // LFO 1 is the audible-motion one; LFO 2 runs slow, for drift.
    constexpr std::array<int, 7> kLfo1Shapes { 0, 0, 1, 1, 2, 3, 4 };
    constexpr std::array<int, 4> kLfo2Shapes { 0, 0, 1, 4 };
    p.lfo1.shape  = LfoShape (r.choice (kLfo1Shapes));
    p.lfo1.rate   = r.expAt (r.rand (0.3f, 0.75f), 0.01f, 30.0f);
    p.lfo1.retrig = r.chance (0.6f);
    p.lfo2.shape  = LfoShape (r.choice (kLfo2Shapes));
    p.lfo2.rate   = r.expAt (r.rand (0.05f, 0.4f), 0.01f, 30.0f);
    p.lfo2.retrig = r.chance (0.3f);

    // --- mod matrix ---------------------------------------------------------
    // One to three live routings, never the same destination twice.
    const int live = r.randInt (1, 3);
    std::vector<int> usedDst;
    for (int slot = 0; slot < 4; ++slot)
    {
        ModSlot& m = p.mods[size_t (slot)];

        // Draw only from routings whose destination is still free, so two slots can never fight
        // over the same target.
        std::vector<Routing> avail;
        if (slot < live)
            for (const Routing& routing : kRoutings)
                if (std::find (usedDst.begin(), usedDst.end(), routing.dst) == usedDst.end())
                    avail.push_back (routing);

        if (slot >= live || avail.empty())
        {
            m.src = 0;
            m.dst = 0;
            m.amt = 0.0f;
            continue;
        }

        const Routing pick = avail[size_t (r.randInt (0, int (avail.size()) - 1))];
        usedDst.push_back (pick.dst);
        const float amt = r.rand (0.15f, 1.0f) * pick.max;
        m.src = pick.src;
        m.dst = pick.dst;
        m.amt = r.chance (0.5f) ? amt : -amt;
    }

    // --- global + effects ---------------------------------------------------
    p.glide = r.chance (1.0f - c.glideChance) ? 0.0f : r.rand (0.02f, 0.25f);

    p.fx.dist          = r.chance (1.0f - c.driveChance * 0.6f) ? 0.0f : r.rand (0.05f, 0.4f);
    p.fx.chorusRate    = r.expAt (r.rand (0.15f, 0.6f), 0.05f, 8.0f);
    p.fx.chorusDepth   = r.rand (0.2f, 0.7f);
    p.fx.chorusMix     = r.randRange (c.chorusMix);
    p.fx.delayTime     = r.rand (0.12f, 0.6f);
    p.fx.delayFeedback = r.rand (0.15f, 0.6f);
    p.fx.delayMix      = r.randRange (c.delayMix);
    p.fx.reverbSize    = r.rand (0.25f, 0.9f);
    p.fx.reverbMix     = r.randRange (c.reverbMix);

    // master, poly and velSens are never written: they came in on `current` and stay there.
    return p;
}

} // namespace spectra
