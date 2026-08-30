#pragma once

/// @brief The master page as data: `sound_master` and its five columns.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (task-6 fix round 1, I3). From
/// `gui/ui/menus.cpp`'s `soundMasterMenu` literal (`:1739-1742`) and the globals it names
/// (`:1727`, `:1727-1735`):
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `synth_mode` | `synthModeMenu` (`:1735`) | `SynthModeSelection` (`gui/menu_item/synth_mode.h:28`) | Enum |
/// | `master_volume` | `volumeMenu` (`:1731`) | `patched_param::Integer`, `GLOBAL_VOLUME_POST_FX`, `BAR` | Int (param) |
/// | `master_pan` | `panMenu` (`:1733`) | `patched_param::Pan`, `LOCAL_PAN` (`patched_param/pan.h:21`) | Int (param, `Scaling::Pan`) |
/// | `master_transpose` | `masterTransposeMenu` (`:1725`) | `MasterTranspose` (`master_transpose.h:31`) | Int (hooks + learn param) |
/// | `vibrato` | `vibratoMenu` (`:1725`) | `patch_cable_strength::Fixed` | **Custom, E2** |
/// | `sound_master` | `soundMasterMenu` (`:1739`) | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`vibratoMenu` is an E2 placeholder.** `patch_cable_strength::Fixed` edits a *patch
///       cable*'s strength, not a parameter: its `readCurrentValue`/`writeCurrentValue` go through
///       `PatchCableSet` (`gui/menu_item/patch_cable_strength.cpp`), and spec E §4 lists
///       "patch-cable strength" among the bespoke items E2 owns alongside the patch-cable pages
///       themselves. The node names it and points at `kPending`, so the tree's shape is right and
///       `validate_tree` reports it (`Issue::Kind::PlaceholderCustom`) until E2 lands.
///
/// @note **`masterTransposeMenu` is a `menu_item::Transpose`-shaped item**: an `Integer` with its
///       own read/write over `Sound::transpose` AND a `PatchedParam` reporting
///       `LOCAL_PITCH_ADJUST` (`master_transpose.h:69-70`), exactly the pattern
///       `detail.h::with_learn_param` exists for (task-6 fix round 1, I1). Its `Hint` is `None`:
///       `getRenderingStyle()` returns `NUMBER` (`master_transpose.h:86`).
///
/// @note **Two legacy behaviours have no `Node` spelling and are recorded, not silently dropped.**
///       `SynthModeSelection::writeCurrentValue` ends with `view.setKnobIndicatorLevels()`
///       (`synth_mode.h:57`) -- `gui/views/` is behind the `scenes-includes-outside` wall, and the
///       equivalent is the `Dirty` flag `bindings::write` already raises. And
///       `MasterTranspose::getColumnLabel` shortens the label to "TRANSPOSE"
///       (`master_transpose.h:88`), a per-item column label `Node` does not carry (the same gap
///       `sound_filters.h` records for its own four).

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/patch_cables.h" // `vibrato` is a fixed patch cable
#include "scenes/menu/nodes/sound_osc.h"    // kPending
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The master page's own hooks. Bodies (and their citations) in the `.cpp`.
/// @{
int32_t get_synth_mode(const MenuContext& ctx);
void set_synth_mode(const MenuContext& ctx, int32_t value);
int32_t get_master_transpose(const MenuContext& ctx);
void set_master_transpose(const MenuContext& ctx, int32_t value);
/// @}

/// `SynthModeSelection::getOptions` (`gui/menu_item/synth_mode.h:60-67`).
inline constexpr uint16_t kSynthModeNames[] = {
    sid(deluge::l10n::String::STRING_FOR_SUBTRACTIVE),
    sid(deluge::l10n::String::STRING_FOR_FM),
    sid(deluge::l10n::String::STRING_FOR_RINGMOD),
};

inline constexpr Node synth_mode = enumeration(sid(deluge::l10n::String::STRING_FOR_SYNTH_MODE), kSynthModeNames,
                                               get_synth_mode, set_synth_mode, relevance::ringmod_allowed);

inline constexpr Node master_volume = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_VOLUME_LEVEL),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::GLOBAL_VOLUME_POST_FX}, Hint::Bar,
          nullptr, sid(deluge::l10n::String::STRING_FOR_MASTER_LEVEL)));

inline constexpr Node master_pan =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_PAN),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_PAN},
                           Hint::Pan, nullptr, 0, Scaling::Pan));

// `MasterTranspose::getMinValue`/`getMaxValue` (`master_transpose.h:84-85`).
inline constexpr Node master_transpose = with_patch_chain(
    with_learn_param(with_title(int_item(sid(deluge::l10n::String::STRING_FOR_MASTER_TRANSPOSE), get_master_transpose,
                                         set_master_transpose, IntRange{-96, 96, 1}, Hint::None),
                                sid(deluge::l10n::String::STRING_FOR_MASTER_TRAN_MENU_TITLE)),
                     {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_PITCH_ADJUST}));

// `vibratoMenu` (`gui/ui/menus.cpp:1727-1728`): a `patch_cable_strength::Fixed` on
// LOCAL_PITCH_ADJUST from LFO_GLOBAL_1, i.e. the depth of the one cable that makes an LFO wobble the
// pitch. Its select press chains to the range source picker like every other cable-strength page
// (`patch_cable_strength.cpp:302-306`).
inline constexpr Node vibrato = with_select_chain(custom(sid(deluge::l10n::String::STRING_FOR_VIBRATO), &kVibratoItem),
                                                  &patch_cable_strength_chain, kPatchCableStrengthChain);

inline constexpr const Node* kMasterChildren[] = {&synth_mode, &master_volume, &master_pan, &master_transpose,
                                                  &vibrato};
inline constexpr Node sound_master =
    submenu(sid(deluge::l10n::String::STRING_FOR_MASTER), kMasterChildren, Layout::Horizontal);

/// @brief This file's contribution to the sound-editor root: `soundMasterMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundMasterChildren;

} // namespace deluge::scenes::menu::nodes
