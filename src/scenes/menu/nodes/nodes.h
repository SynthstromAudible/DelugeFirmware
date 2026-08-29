#pragma once

/// @brief What the batch-1 node tables export: one span of top-level nodes per subtree.
///
/// The six subtrees are exactly the ones the retired Python generator produced
/// (`src/deluge/gui/menu_item/generate/main.py`'s `top_level_menus`, whose output was the committed
/// `g_menus.inc`), re-declared as `constexpr Node` literals. Each span holds that file's
/// contribution to `top_level_menus`, in the same order:
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
/// declares the subtrees and nothing above them, which is why these are spans of roots rather than
/// one tree.
///
/// @note **Naming.** `k<Subtree>Children` is the brief's own spelling. They are `extern const`
///       spans over `constexpr` arrays in the corresponding `.cpp`, not `constexpr` themselves --
///       the nodes are file-local so that a table's internals cannot be reached from outside it.

#include "scenes/menu/node.h"

#include <span>

namespace deluge::scenes::menu::nodes {

/// @brief The filters subtree: `soundFiltersMenu` -> {`lpfMenu`, `hpfMenu`, `filterRoutingMenu`}.
extern const std::span<const Node* const> kSoundFiltersChildren;
/// @brief The compressor subtree: `audioCompMenu`.
extern const std::span<const Node* const> kSoundCompressorChildren;
/// @brief The unison subtree: `unisonMenu`.
extern const std::span<const Node* const> kSoundUnisonChildren;
/// @brief The oscillator subtree: recorder, both sources, both modulators, the mixer.
extern const std::span<const Node* const> kSoundOscChildren;
/// @brief The four envelope submenus.
extern const std::span<const Node* const> kSoundEnvChildren;
/// @brief The four LFO submenus.
extern const std::span<const Node* const> kSoundLfoChildren;

/// @brief Both oscillator subtree nodes that must carry `Node::range_dependent`, and every other
///        one in batch 1 -- see `kRangeDependentNodes`. Declared per file so each table owns its
///        own expectation; `batch1.cpp` is what joins them.
extern const std::span<const Node* const> kOscRangeDependentNodes;
/// @brief The oscillator subtree's half-precision bindings: both pulse widths
///        (`gui/menu_item/osc/pulse_width.h:32,34-37`).
extern const std::span<const Node* const> kOscHalfPrecisionNodes;
/// @brief The compressor subtree's half-precision bindings: the threshold
///        (`gui/menu_item/audio_compressor/compressor_params.h:29-33`).
extern const std::span<const Node* const> kCompressorHalfPrecisionNodes;

/// @brief All seventeen top-level nodes of batch 1, in `main.py`'s own order -- the `roots`
///        argument `validate_tree` (and Task 10's dump) wants.
std::span<const Node* const> batch1_roots();

/// @brief Every node in these six subtrees that must carry `Node::range_dependent`.
///
/// The validator's `Flag::RangeDependent` audit compares this against what the tree actually sets,
/// so a table that forgets the flag fails loudly rather than quietly editing whichever `MultiRange`
/// happened to be first. Batch 1 contains four of the five legacy items `node.h` names -- both
/// oscillators' file selector, sample-transpose, audio recorder, and start/end loop points -- which
/// is eight nodes; only `multi_range.h` itself is not here.
std::span<const Node* const> kRangeDependentNodes();

/// @brief Every node in these six subtrees whose binding must use `Scaling::HalfPrecision`.
///
/// `osc::PulseWidth` for both sources (`gui/menu_item/osc/pulse_width.h:32,34-37`) and
/// `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:29-33`) -- the only two
/// legacy classes that use the half-precision value scaling, and the ones `bindings.h` says Tasks
/// 6-8 must name at the call site because nothing can infer them.
std::span<const Node* const> kHalfPrecisionNodes();

} // namespace deluge::scenes::menu::nodes
