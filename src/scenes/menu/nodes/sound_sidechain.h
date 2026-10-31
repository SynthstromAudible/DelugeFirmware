#pragma once

/// @brief The sidechain page as data: `sidechain` and its six columns, plus the three `SideChain`
///        conversions the reverb's own sidechain page reuses.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (task-6 fix round 1, I3). From
/// `gui/ui/menus.cpp`'s `sidechainMenu` literal (`:814-823`) and the globals it names (`:806-812`):
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `sidechain_volume` | `sidechainVolumeShortcutMenu` (`:806`) | `sidechain::VolumeShortcut` (`sidechain/volume.h:23`) | **Custom, E2** |
/// | `sidechain_sync` | `sidechainSyncMenu` (`:808`) | `sidechain::Sync` (`sidechain/sync.h:28`) | SyncLevel, 10 options |
/// | `sidechain_shape` | `sidechainShapeMenu` (`:811`) | `sidechain::Shape` (`sidechain/shape.h:6`) | Int (param; its Edit raises the reverb's recompute) |
/// | `sidechain_send` | `sidechainSendMenu` (`:805`) | `sidechain::Send` (`sidechain/send.h:28`) | Int |
/// | `sidechain_attack` | `sidechainAttackMenu` (`:809`) | `sidechain::Attack` (`sidechain/attack.h:28`) | Int |
/// | `sidechain_release` | `sidechainReleaseMenu` (`:810`) | `sidechain::Release` (`sidechain/release.h:28`) | Int |
/// | `sidechain` | `sidechainMenu` (`:814`) | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`sidechainVolumeShortcutMenu` is an E2 placeholder.** It derives from
///       `patch_cable_strength::Fixed` -- the ducking amount is the strength of the
///       `SIDECHAIN -> GLOBAL_VOLUME_POST_REVERB_SEND` patch cable, not a parameter -- and spec E
///       §4 lists patch-cable strength among the bespoke items E2 owns. Same treatment as
///       `sound_master.h`'s `vibrato`.
///
/// @note **`sidechain::Sync` overrides `size()` to 10** (`sidechain/sync.h:33`), not
///       `NUM_SYNC_VALUES`, which is why `sync_level_item` takes its range as an argument.
///
/// @note **Attack and release are lookup-table indices, not values.** Legacy stores a rate and
///       shows the closest index into `attackRateTable` / `releaseRateTable`
///       (`sidechain/attack.h:34-37`, `release.h:34-37`) -- `util/lookuptables/lookuptables.h:122-123`,
///       outside `src/scenes`' include allow-list, so the `.cpp` declares the two arrays `extern`
///       and transcribes `getLookupIndexFromValue` (`util/functions.cpp:1428-1443`) locally. The
///       same treatment `sound_compressor.cpp` gives `lshiftAndSaturate<24>`.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/param_shows.h"
#include "scenes/menu/nodes/patch_cables.h" // the volume-ducking item is a fixed patch cable
#include "scenes/menu/nodes/sound_osc.h"    // kPending
#include "scenes/menu/relevance.h"

#include "engine-api/types/sync.h"

#include <cstdint>
#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The sidechain conversions, shared with the reverb's sidechain page (`sound_fx.h`).
///
/// Between a stored attack, release or sync and the menu's value, as the legacy items convert them
/// (`gui/menu_item/sidechain/`).
/// @{
int32_t sidechain_attack_to_menu(int32_t attack);
int32_t sidechain_attack_from_menu(int32_t menu_value);
int32_t sidechain_release_to_menu(int32_t release);
int32_t sidechain_release_from_menu(int32_t menu_value);
int32_t sidechain_sync_to_menu(deluge::engine::SyncType type, deluge::engine::SyncLevel level);
/// @}

/// @brief `SyncLevel::size()` for both sidechains -- `sidechain::Sync::size()` (`sidechain/sync.h:33`).
inline constexpr IntRange kSidechainSyncRange{0, 9, 1};

/// @brief `Attack::getMaxValue` / `Release::getMaxValue` (`sidechain/attack.h:63`, `release.h:63`):
///        an index into a 51-entry rate table.
inline constexpr IntRange kSidechainRateRange{0, 50, 1};

// `sidechainVolumeShortcutMenu` (`gui/ui/menus.cpp:806-807`): a `sidechain::VolumeShortcut`, which
// is a `patch_cable_strength::Fixed` on GLOBAL_VOLUME_POST_REVERB_SEND from the SIDECHAIN source.
// The reverb's recompute its `writeCurrentValue` raises (`sidechain/volume.h:26-29`) is the cable
// Edit's: SetPatchCable raises it for this cable.
inline constexpr Node sidechain_volume =
    with_value_text(with_column_name(with_select_chain(custom(sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING),
                                                              &kSidechainVolumeItem),
                                                       &patch_cable_strength_chain, kPatchCableStrengthChain),
                                     sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING_SHORT)),
                    cable_strength_text);
inline constexpr Node sidechain_sync =
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_SYNC), Kind::SyncLevel,
                            {.kind = deluge::scenes::bind::Target::Kind::EFFECT, .path = "sidechain/syncLevel"}),
               sid(deluge::l10n::String::STRING_FOR_SIDECHAIN_SYNC));
// `sidechain::Shape` is an `unpatched_param::UpdatingReverbParams`, not a plain `UnpatchedParam`
// (`sidechain/shape.h:6`): the reverb recomputes after a write to it (`updating_reverb_params.h:26-29`), which
// SetParamValue raises for the param.
inline constexpr Node sidechain_shape = with_column_name(
    param(sid(deluge::l10n::String::STRING_FOR_SHAPE),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_SIDECHAIN_SHAPE},
          Hint::Knob, nullptr, sid(deluge::l10n::String::STRING_FOR_SIDECH_SHAPE_MENU_TITLE)),
    sid(deluge::l10n::String::STRING_FOR_SHAPE_SHORT));
inline constexpr Node sidechain_send =
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_SEND_TO_SIDECHAIN), Kind::Int,
                            {.kind = deluge::scenes::bind::Target::Kind::SOUND, .path = "sideChainSend"}, Hint::Bar),
               sid(deluge::l10n::String::STRING_FOR_SEND_TO_SIDECH_MENU_TITLE));
inline constexpr Node sidechain_attack = with_column_name(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_ATTACK), Kind::Int,
                            {.kind = deluge::scenes::bind::Target::Kind::EFFECT, .path = "sidechain/attack"},
                            Hint::Attack),
               sid(deluge::l10n::String::STRING_FOR_SIDECH_ATTACK_MENU_TITLE)),
    sid(deluge::l10n::String::STRING_FOR_ATTACK_SHORT));
inline constexpr Node sidechain_release = with_column_name(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_RELEASE), Kind::Int,
                            {.kind = deluge::scenes::bind::Target::Kind::EFFECT, .path = "sidechain/release"},
                            Hint::Release),
               sid(deluge::l10n::String::STRING_FOR_SIDECH_RELEASE_MENU_TITLE)),
    sid(deluge::l10n::String::STRING_FOR_RELEASE_SHORT));

inline constexpr const Node* kSidechainChildren[] = {&sidechain_volume, &sidechain_sync,   &sidechain_shape,
                                                     &sidechain_send,   &sidechain_attack, &sidechain_release};
inline constexpr Node sidechain =
    submenu(sid(deluge::l10n::String::STRING_FOR_SIDECHAIN), kSidechainChildren, Layout::Horizontal);

/// @brief This file's contribution to the sound-editor root: `sidechainMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundSidechainChildren;

} // namespace deluge::scenes::menu::nodes
