#include "Params.h"
#include "Wavetables.h"

// The factory presets bundled with the web version (js/factory-presets.js), transcribed as
// overrides onto the default patch -- which is exactly how they are stored there, as sparse
// parameter objects merged over DEFAULTS.
//
// "4-Key Arp" keeps its name and its sound; its arpeggiator settings are dropped, because the
// arp is not part of this port and the host sequences the plugin instead.

namespace spectra {

static const std::vector<Params>& build()
{
    static const std::vector<Params> presets = []
    {
        std::vector<Params> v;
        auto add = [&v] (const char* name, int table1, int table2, auto&& edit)
        {
            Params p;
            p.name = name;
            p.osc1.table = table1;
            p.osc2.table = table2;
            edit (p);
            v.push_back (p);
        };

        const int basic = 0, sweep = 1, vox = 3, metallic = 4, digital = 5;

        add ("4-Key Arp", sweep, basic, [] (Params& p) {
            p.osc1.on = true;  p.osc1.pos = 0.21416015625f;
            p.osc1.specMode = SpecMode::comb; p.osc1.spec = 0.45f;
            p.osc1.unison = 2; p.osc1.detune = 8.0f; p.osc1.level = 0.8f;
            p.osc2.on = true;  p.osc2.pos = 0.5782421875f;
            p.subLevel = 0.25f;
            p.filter.cut = 700.0f; p.filter.res = 0.35f;
            p.filter.env = 0.65f; p.filter.key = 0.5f;
            p.env1 = { 0.002f, 0.6f, 0.0f, 0.35f };
            p.env2 = { 0.001f, 0.22f, 0.0f, 0.2f };
            p.mods[0] = { 4, 5, 0.3f };          // Velocity -> Cutoff
            p.velSens = 0.7f;
            p.fx.delayTime = 0.32f; p.fx.delayFeedback = 0.42f; p.fx.delayMix = 0.6617578125f;
            p.fx.reverbSize = 0.45f; p.fx.reverbMix = 0.2f;
        });

        add ("Acid Bass", basic, basic, [] (Params& p) {
            p.osc1.on = true; p.osc1.pos = 0.52f;
            p.osc1.warpMode = WarpMode::sync; p.osc1.warp = 0.15f;
            p.osc1.level = 0.85f; p.osc1.rand = 0.0f;
            p.osc2.on = false;
            p.subLevel = 0.35f;
            p.filter.cut = 320.0f; p.filter.res = 0.78f;
            p.filter.drive = 0.45f; p.filter.env = 0.72f;
            p.env1 = { 0.002f, 0.5f, 0.7f, 0.08f };
            p.env2 = { 0.001f, 0.18f, 0.05f, 0.1f };
            p.mods[0] = { 4, 5, 0.35f };          // Velocity -> Cutoff
            p.mods[1] = { 5, 5, 0.5f };           // Mod Wheel -> Cutoff
            p.glide = 0.07f; p.poly = 1; p.velSens = 0.6f;
            p.fx.dist = 0.3f;
            p.fx.delayTime = 0.18f; p.fx.delayFeedback = 0.25f; p.fx.delayMix = 0.12f;
        });

        add ("Init", basic, basic, [] (Params&) {});

        add ("JB1", sweep, digital, [] (Params& p) {
            p.osc1.on = true; p.osc1.pos = 0.7399609375f;
            p.osc1.unison = 5; p.osc1.detune = 14.0f;
            p.osc1.spread = 0.9f; p.osc1.blend = 0.8f; p.osc1.level = 0.65f;
            p.osc2.on = true; p.osc2.pos = 0.3234765625f;
            p.osc2.specMode = SpecMode::smooth; p.osc2.spec = 0.35f;
            p.osc2.unison = 3; p.osc2.detune = 10.0f;
            p.osc2.semi = -12; p.osc2.level = 0.45f;
            p.filter.cut = 2600.0f; p.filter.res = 0.18f;
            p.filter.env = 0.2f; p.filter.key = 0.3f;
            p.env1 = { 0.9f, 1.5f, 0.85f, 1.6f };
            p.env2 = { 1.2f, 2.0f, 0.4f, 1.5f };
            p.lfo1.rate = 0.22f;
            p.lfo2.rate = 0.11f;
            p.mods[0] = { 1, 1, 0.23125f };       // LFO 1 -> Osc1 Pos
            p.mods[1] = { 2, 2, -1.0f };          // LFO 2 -> Osc2 Pos
            p.velSens = 0.3f; p.master = 0.75f;
            p.fx.chorusRate = 0.4f; p.fx.chorusDepth = 0.45f; p.fx.chorusMix = 0.5f;
            p.fx.delayTime = 0.4721f; p.fx.delayFeedback = 0.554328125f; p.fx.delayMix = 0.7519140625f;
            p.fx.reverbSize = 0.7f; p.fx.reverbMix = 0.38f;
        });

        add ("Spectral Pluck", sweep, basic, [] (Params& p) {
            p.osc1.on = true; p.osc1.pos = 0.45f;
            p.osc1.specMode = SpecMode::comb; p.osc1.spec = 0.45f;
            p.osc1.unison = 2; p.osc1.detune = 8.0f; p.osc1.level = 0.8f;
            p.osc2.on = false;
            p.subLevel = 0.25f;
            p.filter.cut = 700.0f; p.filter.res = 0.35f;
            p.filter.env = 0.65f; p.filter.key = 0.5f;
            p.env1 = { 0.002f, 0.6f, 0.0f, 0.35f };
            p.env2 = { 0.001f, 0.22f, 0.0f, 0.2f };
            p.mods[0] = { 4, 5, 0.3f };
            p.velSens = 0.7f;
            p.fx.delayTime = 0.32f; p.fx.delayFeedback = 0.42f; p.fx.delayMix = 0.3f;
            p.fx.reverbSize = 0.45f; p.fx.reverbMix = 0.2f;
        });

        add ("Vox Morph", vox, metallic, [] (Params& p) {
            p.osc1.on = true; p.osc1.pos = 0.2f;
            p.osc1.unison = 3; p.osc1.detune = 9.0f;
            p.osc1.spread = 0.7f; p.osc1.level = 0.8f;
            p.osc2.on = true; p.osc2.pos = 0.5f; p.osc2.semi = 7;
            p.osc2.level = 0.22f;
            p.osc2.specMode = SpecMode::disperse; p.osc2.spec = 0.5f;
            p.filter.cut = 5200.0f; p.filter.res = 0.15f;
            p.env1 = { 0.12f, 0.8f, 0.8f, 0.7f };
            p.lfo1.rate = 0.13f; p.lfo1.shape = LfoShape::triangle;
            p.mods[0] = { 1, 1, 0.55f };          // LFO 1 -> Osc1 Pos
            p.mods[1] = { 5, 2, 0.6f };           // Mod Wheel -> Osc2 Pos
            p.master = 0.75f;
            p.fx.chorusDepth = 0.3f; p.fx.chorusMix = 0.4f;
            p.fx.reverbSize = 0.6f; p.fx.reverbMix = 0.3f;
        });

        add ("Warm Pad", basic, sweep, [] (Params& p) {
            p.osc1.on = true; p.osc1.pos = 0.3f;
            p.osc1.unison = 5; p.osc1.detune = 14.0f;
            p.osc1.spread = 0.9f; p.osc1.blend = 0.8f; p.osc1.level = 0.65f;
            p.osc2.on = true; p.osc2.pos = 0.18f; p.osc2.semi = -12;
            p.osc2.unison = 3; p.osc2.detune = 10.0f; p.osc2.level = 0.45f;
            p.osc2.specMode = SpecMode::smooth; p.osc2.spec = 0.35f;
            p.filter.cut = 2600.0f; p.filter.res = 0.18f;
            p.filter.env = 0.2f; p.filter.key = 0.3f;
            p.env1 = { 0.9f, 1.5f, 0.85f, 1.6f };
            p.env2 = { 1.2f, 2.0f, 0.4f, 1.5f };
            p.lfo1.rate = 0.22f;
            p.lfo2.rate = 0.11f;
            p.mods[0] = { 1, 1, 0.25f };
            p.mods[1] = { 2, 2, 0.15f };
            p.velSens = 0.3f; p.master = 0.75f;
            p.fx.chorusRate = 0.4f; p.fx.chorusDepth = 0.45f; p.fx.chorusMix = 0.5f;
            p.fx.reverbSize = 0.7f; p.fx.reverbMix = 0.38f;
        });

        return v;
    }();

    return presets;
}

const std::vector<Params>& factoryPresets() { return build(); }

} // namespace spectra
