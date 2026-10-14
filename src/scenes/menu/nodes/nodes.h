#pragma once

/// @brief The menu tables: the per-subtree headers, and everything joined across them.
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
/// Seventeen roots in all. **Task 7 (batch 2) adds the root they hang off** -- `sound_root`
/// (`sound_root.h`) -- plus the eight subtrees the rest of that root needs:
///
///   * `kSoundActionsChildren` -- `soundEditorRootActionsMenu`, `sequenceDirectionMenu`,
///     `outputMidiSubmenu`
///   * `kSoundMasterChildren` -- `soundMasterMenu`
///   * `kSoundArpChildren` -- `arpMenu`
///   * `kSoundRandomizerChildren` -- `randomizerMenu`
///   * `kSoundFxChildren` -- `soundFXMenu`
///   * `kSoundSidechainChildren` -- `sidechainMenu`
///   * `kSoundVoiceChildren` -- `voiceMenu`
///   * `kSoundBendChildren` -- `bendMenu`, `drumBendRangeMenu`
///
/// `kBatch1Roots` stays a list of roots rather than one tree because that is what task 6 declared
/// and what task 9's shortcut sweep still wants; `kSoundRootRoots` (`sound_root.h`) is the batch-2
/// spelling, one root with everything under it. Every batch-1 subtree is reachable from
/// `sound_root` too -- they are the same node objects, not copies -- which is why the two flag
/// lists below serve both.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/multi_range.h"
#include "scenes/menu/nodes/note.h"
#include "scenes/menu/nodes/patch_cables.h"
#include "scenes/menu/nodes/roots.h"
#include "scenes/menu/nodes/settings.h"
#include "scenes/menu/nodes/sound_actions.h"
#include "scenes/menu/nodes/sound_arp.h"
#include "scenes/menu/nodes/sound_bend.h"
#include "scenes/menu/nodes/sound_compressor.h"
#include "scenes/menu/nodes/sound_env.h"
#include "scenes/menu/nodes/sound_filters.h"
#include "scenes/menu/nodes/sound_fx.h"
#include "scenes/menu/nodes/sound_lfo.h"
#include "scenes/menu/nodes/sound_master.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/menu/nodes/sound_randomizer.h"
#include "scenes/menu/nodes/sound_root.h"
#include "scenes/menu/nodes/sound_sidechain.h"
#include "scenes/menu/nodes/sound_unison.h"
#include "scenes/menu/nodes/sound_voice.h"

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

/// @brief Every node in the whole sound tree that must carry `Node::range_dependent`.
///
/// The validator's `Flag::RangeDependent` audit compares this against what the tree actually sets,
/// so a table that forgets the flag fails loudly rather than quietly editing whichever `MultiRange`
/// happened to be first. Batch 1 contains four of the five legacy classes `node.h` names -- both
/// oscillators' file selector, sample-transpose, audio recorder and start/end loop points -- which
/// is **ten** nodes.
///
/// **Batch 2 adds none.** The fifth legacy class, `multi_range.h:32`, is the range picker itself,
/// which is an E2 custom; nothing else in the sound-editor root overrides `isRangeDependent()`.
inline constexpr const Node* kRangeDependentArray[] = {
    &sample0_recorder,  &sample1_recorder, &file0_selector, &file1_selector, &source0_transpose,
    &source1_transpose, &sample0_start,    &sample0_end,    &sample1_start,  &sample1_end,
};
inline constexpr std::span<const Node* const> kRangeDependentNodes{kRangeDependentArray};

/// @brief Every node in the whole sound tree whose binding must use `Scaling::HalfPrecision`.
///
/// `osc::PulseWidth` for both sources (`gui/menu_item/osc/pulse_width.h:32,34-37`) and
/// `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:29-33`) -- the only two
/// legacy classes that use the half-precision value scaling, and the ones `bindings.h` says Tasks
/// 6-8 must name at the call site because nothing can infer them.
///
/// **Batch 2 adds none.** A tree-wide grep for `computeCurrentValueForHalfPrecisionMenuItem` /
/// `computeFinalValueForHalfPrecisionMenuItem` over `gui/menu_item/` finds exactly those two
/// classes; every other batch-2 param class keeps `patched_param::Integer`'s or
/// `UnpatchedParam`'s standard scaling, and the two pan items (`panMenu`, `reverbPanMenu`) use
/// `Scaling::Pan`, which is a different flag with its own inference backstop (`bindings.cpp`'s
/// `scaling_for`).
inline constexpr const Node* kHalfPrecisionArray[] = {&osc0_pulse_width, &osc1_pulse_width, &comp_threshold};
inline constexpr std::span<const Node* const> kHalfPrecisionNodes{kHalfPrecisionArray};

/// @brief Every root of the whole application menu, in `MenuContext::Root` order, plus the two
///        that no `Root` enumerator names.
///
/// Task 8's list: the ten roots `MenuScene::roots()` is indexed by (`sound_root` is `Root::Sound`),
/// then `root_cv` -- the MIDI/CV root's CV-titled twin (`roots.h`'s note) -- so that it too gets a
/// node id and is validated. This is the span `tables_3_spec` hands `validate_tree`, and what Task
/// 10's dump walks.
///
/// **E2 task 9 adds a twelfth**, `multi_range`, for exactly the reason `root_cv` is here: the range
/// picker is reached only by substitution (`MenuScene::enter_child`'s `MustSelectRange` arm), no
/// submenu names it, and nothing else in the tree chains *to* it -- so without a place in this list
/// it would be an unnumbered, unvalidated, undumped page that `SceneId::arg` could never name.
/// `MenuScene::roots()` is unaffected: it is indexed by `MenuContext::Root` and still has ten
/// entries.
///
/// **Spec E-note-family task 5 appends a thirteenth and fourteenth**, `note_editor_root` and
/// `note_row_editor_root` (`nodes/note.h`): legacy opens both from a `currentUIMode` this layer may
/// not read (`context.h`'s own note on `MenuContext::Root`), so -- like `multi_range` -- neither
/// has a `Root` enumerator and neither is named by any submenu literal. Appended after
/// `multi_range` rather than inserted anywhere earlier so every pre-existing node keeps the id
/// `assign_node_ids`'s pre-order walk already gave it. The two custom-iterance roots underneath
/// them are reached only through `chain_targets` (their own `IterancePreset::selectButtonPress()`
/// port) and so do not appear here themselves, exactly as `arpOctaveModeToNoteModeMenu` and its
/// three relatives do not.
inline constexpr const Node* kAllRootArray[] = {
    &sound_root,      &root_drum,        &root_midi_or_cv,  &root_midi_drum,       &root_gate_drum,
    &root_audio_clip, &root_performance, &root_song,        &root_kit_global_fx,   &root_settings,
    &root_cv,         &multi_range,      &note_editor_root, &note_row_editor_root,
};
inline constexpr std::span<const Node* const> kAllRoots{kAllRootArray};

} // namespace deluge::scenes::menu::nodes
