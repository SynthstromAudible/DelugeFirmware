#pragma once

/// @brief The sidechain page as data: `sidechain` and its six columns, plus the three `SideChain`
///        conversions the reverb's own sidechain page reuses.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From `origin/main:src/deluge/gui/ui/menus.cpp`'s
/// `sidechainMenu` literal and the globals it names:
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `sidechain_volume` | `sidechainVolumeShortcutMenu` | `sidechain::VolumeShortcut` (`origin/main:src/deluge/gui/menu_item/sidechain/volume.h`) | **Custom** |
/// | `sidechain_sync` | `sidechainSyncMenu` | `sidechain::Sync` (`origin/main:src/deluge/gui/menu_item/sidechain/sync.h`) | SyncLevel, 10 options |
/// | `sidechain_shape` | `sidechainShapeMenu` | `sidechain::Shape` (`origin/main:src/deluge/gui/menu_item/sidechain/shape.h`) | Int (param; its Edit raises the reverb's recompute) |
/// | `sidechain_send` | `sidechainSendMenu` | `sidechain::Send` (`origin/main:src/deluge/gui/menu_item/sidechain/send.h`) | Int |
/// | `sidechain_attack` | `sidechainAttackMenu` | `sidechain::Attack` (`origin/main:src/deluge/gui/menu_item/sidechain/attack.h`) | Int |
/// | `sidechain_release` | `sidechainReleaseMenu` | `sidechain::Release` (`origin/main:src/deluge/gui/menu_item/sidechain/release.h`) | Int |
/// | `sidechain` | `sidechainMenu` | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`sidechainVolumeShortcutMenu` is a custom node.** It derives from
///       `patch_cable_strength::Fixed` -- the ducking amount is the strength of the
///       `SIDECHAIN -> GLOBAL_VOLUME_POST_REVERB_SEND` patch cable, not a parameter -- so it is a
///       `custom` node over `kSidechainVolumeItem`. Same treatment as `sound_master.h`'s `vibrato`.
///
/// @note **`sidechain::Sync` overrides `size()` to 10** (`origin/main:src/deluge/gui/menu_item/sidechain/sync.h`), not
///       `NUM_SYNC_VALUES` (`origin/main:src/deluge/model/sync.h`), here `plugin::kNumCombinedSyncValues`, which is why
///       `sync_level_item` takes its range as an argument.
///
/// @note **Attack and release are lookup-table indices, not values.** Legacy stores a rate and shows the closest index
///       into `attackRateTable` / `releaseRateTable` (`Attack::readCurrentValue` in
///       `origin/main:src/deluge/gui/menu_item/sidechain/attack.h`, `Release::readCurrentValue` in
///       `origin/main:src/deluge/gui/menu_item/sidechain/release.h`) -- `attackRateTable` / `releaseRateTable` in
///       `util/lookuptables/lookuptables.h`, outside `src/ui/scenes`' include allow-list, so the `.cpp` declares the
///       two arrays `extern` and transcribes `getLookupIndexFromValue` (`util/functions.cpp`) locally. The same
///       treatment `sound_compressor.cpp` gives `lshiftAndSaturate<24>`.

#include "project/setting_group.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/param_shows.h"
#include "ui/scenes/menu/nodes/patch_cables.h" // the volume-ducking item is a fixed patch cable
#include "ui/scenes/menu/nodes/sound_osc.h"
#include "ui/scenes/menu/relevance.h"

#include "plugin/types/sync.h"

#include <cstdint>
#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @name The sidechain conversions, shared with the reverb's sidechain page (`sound_fx.h`).
///
/// Between a stored attack, release or sync and the menu's value, as the legacy items convert them
/// (`gui/menu_item/sidechain/`).
/// @{
int32_t sidechain_attack_to_menu(int32_t attack);
int32_t sidechain_attack_from_menu(int32_t menu_value);
int32_t sidechain_release_to_menu(int32_t release);
int32_t sidechain_release_from_menu(int32_t menu_value);
/// @}

/// @brief `SyncLevel::size()` for both sidechains -- `sidechain::Sync::size()`
/// (`origin/main:src/deluge/gui/menu_item/sidechain/sync.h`).
inline constexpr IntRange kSidechainSyncRange{0, 9, 1};

/// @brief `Attack::getMaxValue` / `Release::getMaxValue` (`origin/main:src/deluge/gui/menu_item/sidechain/attack.h`,
///        `origin/main:src/deluge/gui/menu_item/sidechain/release.h`): an index into a 51-entry rate table.
inline constexpr IntRange kSidechainRateRange{0, 50, 1};

// `sidechainVolumeShortcutMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`): a `sidechain::VolumeShortcut`, which is a
// `patch_cable_strength::Fixed` on GLOBAL_VOLUME_POST_REVERB_SEND from the SIDECHAIN source. The reverb's recompute its
// `writeCurrentValue` raises (`VolumeShortcut::writeCurrentValue` in
// `origin/main:src/deluge/gui/menu_item/sidechain/volume.h`) is the cable Edit's: SetPatchCable raises it for this
// cable. Its column is the ducking meter (`SIDECHAIN_DUCKING`).
inline constexpr Node sidechain_volume = fixed_cable_column(
    with_hint(
        with_value_text(with_column_name(with_select_chain(custom(sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING),
                                                                  &kSidechainVolumeItem),
                                                           &patch_cable_strength_chain, kPatchCableStrengthChain),
                                         sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING_SHORT)),
                        ducking_strength_text),
        Hint::Ducking),
    ducking_strength);
inline constexpr Node sidechain_sync = with_affect_entire(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_SYNC), Kind::SyncLevel,
                            {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "sidechain/syncLevel"}),
               sid(deluge::l10n::String::STRING_FOR_SIDECHAIN_SYNC)));
// `sidechain::Shape` (`origin/main:src/deluge/gui/menu_item/sidechain/shape.h`) is an
// `unpatched_param::UpdatingReverbParams`, not a plain unpatched param: the reverb recomputes after a write to it
// (`UpdatingReverbParams::writeCurrentValue` in
// `origin/main:src/deluge/gui/menu_item/unpatched_param/updating_reverb_params.h`), which SetParamValue raises for the
// param.
inline constexpr Node sidechain_shape =
    with_column_name(param(sid(deluge::l10n::String::STRING_FOR_SHAPE),
                           sound_param(deluge::instruments::classic::param::kCompressorShape), Hint::Knob, nullptr,
                           sid(deluge::l10n::String::STRING_FOR_SIDECH_SHAPE_MENU_TITLE)),
                     sid(deluge::l10n::String::STRING_FOR_SHAPE_SHORT));
inline constexpr Node sidechain_send =
    with_affect_entire(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_SEND_TO_SIDECHAIN), Kind::Int,
                                               {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                .path = "sideChainSend",
                                                .group = deluge::project::SettingGroupId::SOUND_ROUTING},
                                               Hint::Bar),
                                  sid(deluge::l10n::String::STRING_FOR_SEND_TO_SIDECH_MENU_TITLE)));
inline constexpr Node sidechain_attack = with_affect_entire(with_column_name(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_ATTACK), Kind::Int,
                            {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "sidechain/attack"},
                            Hint::Attack),
               sid(deluge::l10n::String::STRING_FOR_SIDECH_ATTACK_MENU_TITLE)),
    sid(deluge::l10n::String::STRING_FOR_ATTACK_SHORT)));
inline constexpr Node sidechain_release = with_affect_entire(with_column_name(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_RELEASE), Kind::Int,
                            {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "sidechain/release"},
                            Hint::Release),
               sid(deluge::l10n::String::STRING_FOR_SIDECH_RELEASE_MENU_TITLE)),
    sid(deluge::l10n::String::STRING_FOR_RELEASE_SHORT)));

inline constexpr const Node* kSidechainChildren[] = {&sidechain_volume, &sidechain_sync,   &sidechain_shape,
                                                     &sidechain_send,   &sidechain_attack, &sidechain_release};
inline constexpr Node sidechain =
    submenu(sid(deluge::l10n::String::STRING_FOR_SIDECHAIN), kSidechainChildren, Layout::Horizontal);

/// @brief This file's contribution to the sound-editor root: `sidechainMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundSidechainChildren;

} // namespace deluge::ui::scenes::menu::nodes
