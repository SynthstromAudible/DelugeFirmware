#pragma once

/// @brief The master page as data: `sound_master` and its five columns.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From
/// `origin/main:src/deluge/gui/ui/menus.cpp`'s `soundMasterMenu` literal and the globals it names:
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `synth_mode` | `synthModeMenu` | `SynthModeSelection` (`origin/main:src/deluge/gui/menu_item/synth_mode.h`) | Enum |
/// | `master_volume` | `volumeMenu` | `patched_param::Integer`, `GLOBAL_VOLUME_POST_FX`, `BAR` | Int (param) |
/// | `master_pan` | `panMenu` | `patched_param::Pan`, `LOCAL_PAN` (`panMenu` in `origin/main:src/deluge/gui/ui/menus.cpp`; the class is `origin/main:src/deluge/gui/menu_item/patched_param/pan.h`) | Int (param, `Scaling::Pan`) |
/// | `master_transpose` | `masterTransposeMenu` | `MasterTranspose` (`origin/main:src/deluge/gui/menu_item/master_transpose.h`) | Int (hooks + learn param) |
/// | `vibrato` | `vibratoMenu` | `patch_cable_strength::Fixed` | **Custom** |
/// | `sound_master` | `soundMasterMenu` | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`vibratoMenu` is a custom node.** `patch_cable_strength::Fixed` edits a *patch cable*'s strength, not a
///       parameter: its `readCurrentValue`/`writeCurrentValue` go through `PatchCableSet`
///       (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`), so it is a `custom` node over
///       `kVibratoItem` rather than a data-only page.
///
/// @note **`masterTransposeMenu` is a `menu_item::Transpose`-shaped item**: an `Integer` with its own read/write over
///       `Sound::transpose` AND a `PatchedParam` reporting `LOCAL_PITCH_ADJUST` (`MasterTranspose::MasterTranspose` in
///       `origin/main:src/deluge/gui/menu_item/master_transpose.h`), exactly the pattern `detail.h::with_learn_param`
///       exists for. Its `Hint` is `None`: `Decimal::getRenderingStyle()` returns `NUMBER`.
///
/// @note **`SynthModeSelection::writeCurrentValue` ends with `view.setKnobIndicatorLevels()`**
///       (`origin/main:src/deluge/gui/menu_item/synth_mode.h`) -- `gui/views/` is behind the `scenes-includes-outside`
///       wall, and the equivalent is the `Dirty` flag `bindings::write` already raises.
///       `MasterTranspose::getColumnLabel` (`origin/main:src/deluge/gui/menu_item/master_transpose.h`) is
///       `master_transpose`'s `with_column_name`.

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/fan_out_rules.h"
#include "ui/scenes/menu/nodes/patch_cables.h" // `vibrato` is a fixed patch cable
#include "ui/scenes/menu/nodes/sound_osc.h"
#include "ui/scenes/menu/relevance.h"

#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @name The master page's own hooks. Bodies (and their citations) in the `.cpp`.
/// @{
/// @}

inline constexpr Node synth_mode = with_affect_entire(setting_item(
    sid(deluge::l10n::String::STRING_FOR_SYNTH_MODE), Kind::Enum,
    {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND, .path = "mode", .fan_out_when = fan_out::kRingable}));

inline constexpr Node master_volume = with_patch_chain(param(
    sid(deluge::l10n::String::STRING_FOR_VOLUME_LEVEL), sound_param(deluge::instruments::classic::param::kVolumePostFX),
    Hint::Bar, nullptr, sid(deluge::l10n::String::STRING_FOR_MASTER_LEVEL)));

inline constexpr Node master_pan = with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_PAN),
                                                          sound_param(deluge::instruments::classic::param::kPan),
                                                          Hint::Pan, nullptr, 0, Scaling::Pan));

// `Transpose::getMinValue`/`getMaxValue`/`getNumDecimalPlaces` (`origin/main:src/deluge/gui/menu_item/transpose.h`):
// semitones as a two-place decimal in cents.
/// `MasterTranspose::getColumnLabel` is TRANSPOSE (`origin/main:src/deluge/gui/menu_item/master_transpose.h`).
inline constexpr Node master_transpose = with_column_name(
    with_patch_chain(with_learn_param(
        with_title(with_decimal_places(
                       setting_item(sid(deluge::l10n::String::STRING_FOR_MASTER_TRANSPOSE), Kind::Decimal,
                                    {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND, .path = "transpose"}),
                       2),
                   sid(deluge::l10n::String::STRING_FOR_MASTER_TRAN_MENU_TITLE)),
        sound_param(deluge::instruments::classic::param::kPitch))),
    sid(deluge::l10n::String::STRING_FOR_TRANSPOSE));

// `vibratoMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`): a `patch_cable_strength::Fixed` on LOCAL_PITCH_ADJUST from
// LFO_GLOBAL_1, i.e. the depth of the one cable that makes an LFO wobble the pitch. On the MASTER page its column
// spells the strength and a select press opens the strength page; that page's select chains to the range source
// picker like every other cable-strength page (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`).
inline constexpr Node vibrato = fixed_cable_column(
    with_value_text(with_select_chain(custom(sid(deluge::l10n::String::STRING_FOR_VIBRATO), &kVibratoItem),
                                      &patch_cable_strength_chain, kPatchCableStrengthChain),
                    cable_strength_text),
    vibrato_strength);

inline constexpr const Node* kMasterChildren[] = {&synth_mode, &master_volume, &master_pan, &master_transpose,
                                                  &vibrato};
inline constexpr Node sound_master =
    submenu(sid(deluge::l10n::String::STRING_FOR_MASTER), kMasterChildren, Layout::Horizontal);

/// @brief This file's contribution to the sound-editor root: `soundMasterMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundMasterChildren;

} // namespace deluge::ui::scenes::menu::nodes
