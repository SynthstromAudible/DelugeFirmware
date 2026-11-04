#pragma once

/// @brief The sound-editor root menu as data: `sound_root`, whose children are the sound subtrees the
///        `sound_*.h` headers declare.
///
/// The transcription of `gui/ui/menus.cpp`'s `soundEditorRootMenu` literal -- **27** children, in that exact order:
///
// clang-format off
/// |  # | legacy child | node | declared by |
/// |---|---|---|---|
/// |  1 | `soundEditorRootActionsMenu` | `sound_actions` | `sound_actions.h` |
/// |  2 | `soundMasterMenu`            | `sound_master`  | `sound_master.h` |
/// |  3 | `arpMenu`                    | `arp`           | `sound_arp.h` |
/// |  4 | `randomizerMenu`             | `randomizer`    | `sound_randomizer.h` |
/// |  5 | `audioCompMenu`              | `audio_comp`    | `sound_compressor.h` |
/// |  6 | `soundFiltersMenu`           | `sound_filters` | `sound_filters.h` |
/// |  7 | `soundFXMenu`                | `sound_fx`      | `sound_fx.h` |
/// |  8 | `sidechainMenu`              | `sidechain`     | `sound_sidechain.h` |
/// |  9 | `source0Menu`                | `source0`       | `sound_osc.h` |
/// | 10 | `source1Menu`                | `source1`       | `sound_osc.h` |
/// | 11 | `modulator0Menu`             | `modulator0`    | `sound_osc.h` |
/// | 12 | `modulator1Menu`             | `modulator1`    | `sound_osc.h` |
/// | 13 | `oscMixerMenu`               | `osc_mixer`     | `sound_osc.h` |
/// | 14 | `env1Menu` .. `env4Menu`     | `env1` .. `env4`| `sound_env.h` |
/// | 18 | `lfo1Menu` .. `lfo4Menu`     | `lfo1` .. `lfo4`| `sound_lfo.h` |
/// | 22 | `voiceMenu`                  | `voice`         | `sound_voice.h` |
/// | 23 | `bendMenu`                   | `bend`          | `sound_bend.h` |
/// | 24 | `drumBendRangeMenu`          | `drum_bend_range` | `sound_bend.h` |
/// | 25 | `patchCablesMenu`            | `patch_cables`  | **Custom** (below) |
/// | 26 | `sequenceDirectionMenu`      | `sequence_direction` | `sound_actions.h` |
/// | 27 | `outputMidiSubmenu`          | `output_midi`   | `sound_actions.h` |
// clang-format on
///
/// @note **The count is 27.** `tables_2_spec` asserts 27 against the live `soundEditorRootMenu`
///       object rather than against a written list, so the tree is checked against legacy and not
///       against prose.
///
/// @note **`patchCablesMenu` is a custom node.** `menu_item::PatchCables`
///       (`gui/menu_item/patch_cables.h`) is the mod-matrix browser -- a whole editor with its own
///       scroll state and shortcut blinking -- so it is a `custom` node over `kPatchCablesItem`
///       rather than a data-only page.
///
/// @note **`bendMenu` and `drumBendRangeMenu` share a name.** See `sound_bend.h` and
///       `validate.cpp`'s `co_visible`: legacy separates them by relevance, never by name.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/patch_cables.h"
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
#include "scenes/menu/nodes/sound_sidechain.h"
#include "scenes/menu/nodes/sound_voice.h"

#include <span>

namespace deluge::scenes::menu::nodes {

// `patchCablesMenu` (`gui/ui/menus.cpp:1737`): the MOD MATRIX list of every existing cable on this
// sound. A select press opens the selected cable's own strength page -- the range one when that
// cable's destination is itself a cable (`patch_cables.cpp:168-190`).
inline constexpr Node patch_cables =
    with_select_chain(custom(sid(deluge::l10n::String::STRING_FOR_MOD_MATRIX), &kPatchCablesItem), &patch_cables_chain,
                      kPatchCablesChain);

inline constexpr const Node* kSoundRootChildren[] = {
    &sound_actions, &sound_master,
    &arp,           &randomizer,
    &audio_comp,    &sound_filters,
    &sound_fx,      &sidechain,
    &source0,       &source1,
    &modulator0,    &modulator1,
    &osc_mixer,     &env1,
    &env2,          &env3,
    &env4,          &lfo1,
    &lfo2,          &lfo3,
    &lfo4,          &voice,
    &bend,          &drum_bend_range,
    &patch_cables,  &sequence_direction,
    &output_midi,
};
inline constexpr Node sound_root = submenu(sid(deluge::l10n::String::STRING_FOR_SOUND), kSoundRootChildren);

/// @brief The whole child list as a span, for a caller that wants the children rather than the root.
///
/// Defined in the matching `.cpp` (a `const` namespace-scope object needs the `extern` visible at
/// its definition to get external linkage), like every other subtree's exported span.
extern const std::span<const Node* const> kSoundRootChildrenSpan;

/// @brief The one root of the batch-2 tree, as the `roots` span `validate_tree` and
///        `assign_node_ids` want.
///
/// A one-element list rather than a bare pointer so it composes with the same interfaces
/// `kBatch1Roots` does.
inline constexpr const Node* kSoundRootRootArray[] = {&sound_root};
inline constexpr std::span<const Node* const> kSoundRootRoots{kSoundRootRootArray};

} // namespace deluge::scenes::menu::nodes
