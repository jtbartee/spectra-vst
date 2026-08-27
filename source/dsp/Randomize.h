#pragma once
#include "Params.h"

namespace spectra {

/** The five flavours a roll can land on. Named in the editor's status line so a patch you like
 *  can be rolled for again. Mirrors CHARACTERS in js/randomize.js. */
enum class Character { bass = 0, pluck, pad, lead, texture };
inline constexpr int kNumCharacters = 5;

const char* characterName (Character c);

/** Ports randomizePatch() from js/randomize.js: a curated roll, not uniform noise.
 *
 *  A uniform draw across 82 parameters reliably produces a silent or broken patch, so the roll
 *  first picks a character and lets it narrow every range that decides whether a patch is
 *  playable -- envelope times, cutoff, level balance, unison width, effect depth. What stays
 *  free is the part that makes SPECTRA itself: the wavetable, the warp and spectral mode pair,
 *  the morph position, and the mod matrix.
 *
 *  Master volume, voice count and velocity sensitivity ride through untouched. That is the C++
 *  side of the web build's RANDOMIZE_EXCLUDE list -- they are performance settings rather than
 *  "the sound", and moving them would surprise you.
 *
 *  Unlike the DSP itself this is not sample-exact against the browser: the two use different
 *  PRNGs, so a roll matches the web version in distribution and structure, not sequence.
 *
 *  @param current  patch to build on -- anything the roll does not name keeps its value
 *  @param rng      randomness source; seed it to reproduce a roll
 *  @param outCharacter  if non-null, receives the character that was rolled
 */
Params randomizePatch (const Params& current, Rng& rng, Character* outCharacter = nullptr);

} // namespace spectra
