#pragma once
#include "Mapping.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace spectra {

/** One morph frame as complex harmonic amplitudes c[h] for h = 1..kHarmonics (index 0 is DC
 *  and stays unused). Ported from js/wavetables.js. */
struct HarmFrame
{
    std::vector<float> re, im;
    HarmFrame() : re (size_t (kHarmonics) + 1, 0.0f), im (size_t (kHarmonics) + 1, 0.0f) {}
};

/** A built table: band-limited time-domain copies, one per octave, so high notes never alias.
 *  `mips[k]` holds numFrames * kStride samples. */
struct WavetableSet
{
    int numFrames = 1;
    std::vector<float> mips[kMips];
    std::vector<std::vector<float>> displayFrames;   // 128-point previews for the editor
};

/** Analyses one time-domain frame (any length; it is resampled to kFrame first). */
HarmFrame analyzeFrame (const std::vector<float>& samples);

/** The spectral-morph row: operates on harmonic frames before the mips are built, exactly as
 *  the web version bakes it in on the main thread. */
std::vector<HarmFrame> spectralWarp (const std::vector<HarmFrame>& frames, SpecMode mode, float amt);

/** Synthesises every mip level from a set of harmonic frames. Costs single-digit milliseconds,
 *  which is why it belongs off the audio thread. */
std::unique_ptr<WavetableSet> buildMips (const std::vector<HarmFrame>& frames);

/** The seven factory tables from js/wavetables.js. */
const std::vector<std::string>& factoryTableNames();
std::vector<HarmFrame> buildFactoryHarmFrames (int index);
int factoryTableIndexByName (const std::string& name);

/** A single sine, used as the safe default before any table has been built. */
std::unique_ptr<WavetableSet> defaultSineTable();

/**
 *  Hands a freshly built table from the message thread to the audio thread without the audio
 *  thread ever allocating, freeing or blocking.
 *
 *  The audio thread swaps in a pending table and parks the old one in `retired`; the message
 *  thread reclaims it on the next publish. If `retired` is still occupied the swap is simply
 *  deferred to a later block, which is safe because the currently-active table stays valid.
 */
class TableSlot
{
public:
    TableSlot() : active (defaultSineTable().release()) {}

    ~TableSlot()
    {
        delete active;
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    TableSlot (const TableSlot&) = delete;
    TableSlot& operator= (const TableSlot&) = delete;

    /** Message thread. Takes ownership; also reclaims whatever the audio thread handed back. */
    void publish (std::unique_ptr<WavetableSet> next)
    {
        delete retired.exchange (nullptr, std::memory_order_acq_rel);
        delete pending.exchange (next.release(), std::memory_order_acq_rel);
    }

    /** Audio thread. Never allocates, never frees, never blocks. */
    const WavetableSet& get() noexcept
    {
        if (retired.load (std::memory_order_acquire) == nullptr)
        {
            if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                WavetableSet* old = active;
                active = next;
                retired.store (old, std::memory_order_release);
            }
        }
        return *active;
    }

private:
    WavetableSet* active = nullptr;
    std::atomic<WavetableSet*> pending { nullptr };
    std::atomic<WavetableSet*> retired { nullptr };
};

} // namespace spectra
