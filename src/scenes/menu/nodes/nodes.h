#pragma once

/// @brief The batch-1 menu tables: the six per-subtree headers, and everything joined across them.
///
/// The six subtrees are exactly the ones the retired Python generator produced
/// (`src/deluge/gui/menu_item/generate/main.py`'s `top_level_menus` at `ce4a79de0`, whose output was
/// the committed `g_menus.inc`), re-declared as `constexpr Node` literals. Each `sound_<x>.h`
/// declares its own nodes as `inline constexpr` -- one address per node across every translation
/// unit -- so Task 9's shortcut tables can be `constexpr` arrays naming `&lpf_freq` and friends
/// directly, the way legacy's `paramShortcutsForSounds` names `&lpfFreqMenu` (fix round 1, I3).
/// Each `sound_<x>.cpp` keeps the `get`/`set` hook bodies and exports its own roots as a span:
///
///   * `kSoundFiltersChildren` -- `soundFiltersMenu`
///   * `kSoundCompressorChildren` -- `audioCompMenu`
///   * `kSoundUnisonChildren` -- `unisonMenu`
///   * `kSoundOscChildren` -- `recorderMenu`, `source0Menu`, `source1Menu`, `modulator0Menu`,
///     `modulator1Menu`, `oscMixerMenu`
///   * `kSoundEnvChildren` -- `env1Menu` .. `env4Menu`
///   * `kSoundLfoChildren` -- `lfo1Menu` .. `lfo4Menu`
///
/// Seventeen roots in all. Where they hang off the sound-editor root menu is Task 7's business
/// (`soundEditorRootMenu`), and which pads reach them is Task 9's (`shortcuts.cpp`); this task
/// declares the subtrees and nothing above them, which is why `kBatch1Roots` is a list of roots
/// rather than one tree.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/sound_compressor.h"
#include "scenes/menu/nodes/sound_env.h"
#include "scenes/menu/nodes/sound_filters.h"
#include "scenes/menu/nodes/sound_lfo.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/menu/nodes/sound_unison.h"

#include <span>

namespace deluge::scenes::menu::nodes {

/// @brief All seventeen top-level nodes of batch 1, in `main.py`'s own order -- the `roots`
///        argument `validate_tree` (and Task 10's dump) wants.
///
/// `constexpr` rather than a joined-at-startup array: with every node header-visible there is
/// nothing to join at runtime, so the walk order every node id derives from is fixed at compile
/// time and adding a root cannot silently overflow a hand-maintained count.
inline constexpr const Node* kBatch1RootArray[] = {
    // filters
    &sound_filters,
    // compressor
    &audio_comp,
    // unison
    &unison,
    // oscillators
    &recorder,
    &source0,
    &source1,
    &modulator0,
    &modulator1,
    &osc_mixer,
    // envelopes
    &env1,
    &env2,
    &env3,
    &env4,
    // LFOs
    &lfo1,
    &lfo2,
    &lfo3,
    &lfo4,
};
inline constexpr std::span<const Node* const> kBatch1Roots{kBatch1RootArray};

/// @brief Every node in these six subtrees that must carry `Node::range_dependent`.
///
/// The validator's `Flag::RangeDependent` audit compares this against what the tree actually sets,
/// so a table that forgets the flag fails loudly rather than quietly editing whichever `MultiRange`
/// happened to be first. Batch 1 contains four of the five legacy classes `node.h` names -- both
/// oscillators' file selector, sample-transpose, audio recorder and start/end loop points -- which
/// is **ten** nodes; only `multi_range.h` itself is not here.
inline constexpr const Node* kRangeDependentArray[] = {
    &sample0_recorder,  &sample1_recorder, &file0_selector, &file1_selector, &source0_transpose,
    &source1_transpose, &sample0_start,    &sample0_end,    &sample1_start,  &sample1_end,
};
inline constexpr std::span<const Node* const> kRangeDependentNodes{kRangeDependentArray};

/// @brief Every node in these six subtrees whose binding must use `Scaling::HalfPrecision`.
///
/// `osc::PulseWidth` for both sources (`gui/menu_item/osc/pulse_width.h:32,34-37`) and
/// `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:29-33`) -- the only two
/// legacy classes that use the half-precision value scaling, and the ones `bindings.h` says Tasks
/// 6-8 must name at the call site because nothing can infer them.
inline constexpr const Node* kHalfPrecisionArray[] = {&osc0_pulse_width, &osc1_pulse_width, &comp_threshold};
inline constexpr std::span<const Node* const> kHalfPrecisionNodes{kHalfPrecisionArray};

} // namespace deluge::scenes::menu::nodes
