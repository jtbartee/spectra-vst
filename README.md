# SPECTRA — VST3 / AU

A native C++/JUCE port of the [spectra-synth](../spectra-synth) web synth: a polyphonic
wavetable instrument with two warped, unison-capable wavetable oscillators, sub and noise, a
ZDF state-variable filter, two envelopes, two LFOs, a 4-slot mod matrix, and a distortion /
chorus / delay / reverb chain.

Builds VST3, AU and a Standalone app from one target.

## Building

JUCE 8.0.6 is expected at `../JUCE`, shared with the other synth ports. Point elsewhere with
`-DSPECTRA_JUCE_PATH=...`.

```sh
cmake -B build -DSPECTRA_UNIVERSAL=OFF -DCMAKE_BUILD_TYPE=Release   # OFF = fast native build
cmake --build build --parallel 8
cd build && ctest --output-on-failure
```

`COPY_PLUGIN_AFTER_BUILD` installs into `~/Library/Audio/Plug-Ins/`. Drop
`-DSPECTRA_UNIVERSAL=OFF` for a universal arm64 + x86_64 binary.

## Layout

```
source/dsp/      the synthesis engine, wavetable pipeline and effects, with zero JUCE dependency
source/plugin/   the thin JUCE wrapper: parameters, processor, editor
tools/           offline verification harnesses
```

## Verifying

- **`spectra_render`** drives the DSP directly. Beyond the usual per-preset checks, it covers
  the wavetable pipeline specifically: that a single-harmonic table synthesises to a clean
  0.95-peak sine, that analysis inverts synthesis, that every frame's guard sample mirrors its
  first sample, and — the one that catches a broken mip selection — that a note near the top of
  the keyboard shows no aliased energy folded down below its own fundamental. It also exercises
  all seven warp modes, all seven spectral modes, the filter types, unison spread, the mod
  matrix, the polyphony limit, glide, and a table hot-swap while a note is sounding.
- **`spectra_hosttest`** loads the built VST3 the way a DAW does and checks the wrapper: buses,
  MIDI, reported latency, the preset list, and state round-tripping.
- **`spectra_uicheck`** instantiates the processor in-process, drives the dice through the
  processor API, and writes offscreen PNGs of the editor at its default and minimum sizes.
  Snapshotting through the VST3 host wrapper comes back blank — there the editor is an embedded
  native view — so this links the plugin's shared code directly.

All three are wired as ctest targets: `ctest --test-dir build --output-on-failure`.

## The dice

RANDOM / MUTATE / UNDO in the header, ported from the web build's `js/randomize.js`.

The roll lives in `source/dsp/Randomize.cpp` rather than in the JUCE wrapper, which is what lets
`spectra_render` roll 40 patches, render every one, and assert they are finite and audible — a
roll that produces a legal-looking patch which happens to be silent is caught offline instead of
in a host. It picks a character (Bass, Pluck, Pad, Lead, Texture) and narrows the ranges that
decide whether a patch is playable, leaving the wavetable, warp/spectral modes, morph position
and mod matrix free.

**Mutate is the exception**: it runs over the APVTS rather than the `Params` struct. The ranges
are already declared once in `createParameterLayout()`, and duplicating 82 of them in the DSP
layer would only create a second place for them to drift out of step. This matches `jp8-vst`.

Master volume, voice count and velocity sensitivity are excluded from every roll — the C++ side
of the web build's `RANDOMIZE_EXCLUDE`. **UNDO is a deviation in the other direction**: it is one
level deep and the web version has it too, but the browser build got it as part of this same
change rather than the plugin adding something the web lacks.

## Porting notes

- **Tables are built off the audio thread.** Building the eight band-limited mip levels for a
  17-frame table costs about 11 ms, so it happens on a worker thread and is handed to the audio
  thread through `TableSlot` — which swaps in a pending table and parks the old one for the
  message thread to reclaim, so the audio thread never allocates, frees or blocks. This mirrors
  the web version exactly, where tables are built on the main thread and posted to the worklet.
  Rebuilds are debounced by 90 ms, the same settle the browser uses.
- **The spectral row is baked into the table, not applied live.** Changing Spectral or Spec Amt
  triggers a rebuild; changing Warp does not, because time-domain warping happens per sample.
- **`fastTanh` is transcribed, not replaced.** The rational approximation in `worklet.js` is
  what the master soft-clip and the filter drive actually sound like; `std::tanh` would change
  the saturation character.
- **The FFT, the harmonic conventions and the normalisation are transcribed verbatim**,
  including the shared peak normalisation across all mips (so loudness does not jump between
  octaves) and the 0.95 ceiling that leaves room for Gibbs ripple.
- **Pitch bend is routed to the mod matrix.** The web version has no pitch-bend path at all;
  rather than invent one, the plugin leaves the wheel where the engine already reads it, as the
  matrix's Mod Wheel source.

### Deliberate deviations

1. **The reverb is a feedback delay network, not a convolver.** The web version convolves
   against a generated impulse — exponentially decaying, gently damped white noise whose length
   and decay both follow the single Size control. Reproducing that exactly means partitioned
   FFT convolution over an impulse up to five seconds long, with the latency and CPU cost that
   implies, for what is musically the most generic reverb tail there is. The FDN's RT60 is
   derived from the same Size mapping, so the control behaves the same way.
2. **The master compressor uses a soft-knee curve** rather than Blink's numerically-solved
   saturation curve, with the same controls, the same 6 ms detector lookahead and the same
   `^0.6` makeup-gain rule. That lookahead is real latency and is reported to the host.
3. **The distortion's 2x oversampling is simple.** `WaveShaperNode`'s `'2x'` mode uses a proper
   polyphase filter; this shapes the sample and its midpoint and averages the two. It does the
   same job of keeping the added harmonics from folding straight back down, less precisely.

Not ported: the custom-wavetable editor (draw / harmonic / formula modes), the arpeggiator, and
the separate 808 drum machine that lives on its own page in the web version. The seven factory
wavetables are all exposed; every factory preset uses only those, so no preset is affected.
