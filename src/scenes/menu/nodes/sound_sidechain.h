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
/// | `sidechain_volume` | `sidechainVolumeShortcutMenu` (`:806`) | `sidechain::VolumeShortcut` (`sidechain/volume.h:21`) | **Custom, E2** |
/// | `sidechain_sync` | `sidechainSyncMenu` (`:808`) | `sidechain::Sync` (`sidechain/sync.h:20`) | SyncLevel, 10 options |
/// | `sidechain_shape` | `sidechainShapeMenu` (`:811`) | `sidechain::Shape` (`sidechain/shape.h:6`) | Int (param, `updates_reverb_params`) |
/// | `sidechain_send` | `sidechainSendMenu` (`:805`) | `sidechain::Send` (`sidechain/send.h:20`) | Int |
/// | `sidechain_attack` | `sidechainAttackMenu` (`:809`) | `sidechain::Attack` (`sidechain/attack.h:20`) | Int |
/// | `sidechain_release` | `sidechainReleaseMenu` (`:810`) | `sidechain::Release` (`sidechain/release.h:20`) | Int |
/// | `sidechain` | `sidechainMenu` (`:814`) | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`sidechainVolumeShortcutMenu` is an E2 placeholder.** It derives from
///       `patch_cable_strength::Fixed` -- the ducking amount is the strength of the
///       `SIDECHAIN -> GLOBAL_VOLUME_POST_REVERB_SEND` patch cable, not a parameter -- and spec E
///       §4 lists patch-cable strength among the bespoke items E2 owns. Same treatment as
///       `sound_master.h`'s `vibrato`.
///
/// @note **`sidechain::Sync` overrides `size()` to 10** (`sidechain/sync.h:24`), not
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
#include "scenes/menu/nodes/sound_osc.h" // kPending
#include "scenes/menu/relevance.h"

#include <span>

class SideChain;

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The `SideChain` conversions, shared with the reverb's sidechain page (`sound_fx.h`).
///
/// A null @p sc reads as 0 and ignores the write -- the same "this does not apply here" answer the
/// relevance predicates give, and safer than legacy, which would dereference it.
/// @{
int32_t sidechain_attack_to_menu(const SideChain* sc);
void sidechain_attack_from_menu(SideChain* sc, int32_t menu_value);
int32_t sidechain_release_to_menu(const SideChain* sc);
void sidechain_release_from_menu(SideChain* sc, int32_t menu_value);
int32_t sidechain_sync_to_menu(const SideChain* sc);
void sidechain_sync_from_menu(SideChain* sc, int32_t menu_value);
/// @}

/// @name This page's own hooks (the non-reverb spelling). Bodies in the `.cpp`.
/// @{
int32_t get_sidechain_sync(const MenuContext& ctx);
void set_sidechain_sync(const MenuContext& ctx, int32_t value);
int32_t get_sidechain_send(const MenuContext& ctx);
void set_sidechain_send(const MenuContext& ctx, int32_t value);
int32_t get_sidechain_attack(const MenuContext& ctx);
void set_sidechain_attack(const MenuContext& ctx, int32_t value);
int32_t get_sidechain_release(const MenuContext& ctx);
void set_sidechain_release(const MenuContext& ctx, int32_t value);
/// @}

/// @brief `SyncLevel::size()` for both sidechains -- `sidechain::Sync::size()` (`sidechain/sync.h:24`).
inline constexpr IntRange kSidechainSyncRange{0, 9, 1};

/// @brief `Attack::getMaxValue` / `Release::getMaxValue` (`sidechain/attack.h:50`, `release.h:50`):
///        an index into a 51-entry rate table.
inline constexpr IntRange kSidechainRateRange{0, 50, 1};

inline constexpr Node sidechain_volume =
    with_title(custom(sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING), &kPending), // E2
               sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING));
inline constexpr Node sidechain_sync =
    sync_level_item(sid(deluge::l10n::String::STRING_FOR_SYNC), sid(deluge::l10n::String::STRING_FOR_SIDECHAIN_SYNC),
                    get_sidechain_sync, set_sidechain_sync, kSidechainSyncRange);
// `sidechain::Shape` is an `unpatched_param::UpdatingReverbParams`, not a plain `UnpatchedParam`
// (`sidechain/shape.h:6`): the param write is followed by
// `AudioEngine::mustUpdateReverbParamsBeforeNextRender = true` (`updating_reverb_params.h:26-29`).
// `with_reverb_update` is what carries that; see `Binding::updates_reverb_params` for why it is a
// flag rather than a `set` hook (review round 1, I2).
inline constexpr Node sidechain_shape = with_reverb_update(
    param(sid(deluge::l10n::String::STRING_FOR_SHAPE),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_SIDECHAIN_SHAPE},
          Hint::Knob, nullptr, sid(deluge::l10n::String::STRING_FOR_SIDECH_SHAPE_MENU_TITLE)));
inline constexpr Node sidechain_send =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_SEND_TO_SIDECHAIN), get_sidechain_send, set_sidechain_send,
                        IntRange{0, kMaxMenuValue, 1}, Hint::Bar, relevance::is_kit),
               sid(deluge::l10n::String::STRING_FOR_SEND_TO_SIDECH_MENU_TITLE));
inline constexpr Node sidechain_attack =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_ATTACK), get_sidechain_attack, set_sidechain_attack,
                        kSidechainRateRange, Hint::Attack),
               sid(deluge::l10n::String::STRING_FOR_SIDECH_ATTACK_MENU_TITLE));
inline constexpr Node sidechain_release =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_RELEASE), get_sidechain_release, set_sidechain_release,
                        kSidechainRateRange, Hint::Release),
               sid(deluge::l10n::String::STRING_FOR_SIDECH_RELEASE_MENU_TITLE));

inline constexpr const Node* kSidechainChildren[] = {&sidechain_volume, &sidechain_sync,   &sidechain_shape,
                                                     &sidechain_send,   &sidechain_attack, &sidechain_release};
inline constexpr Node sidechain =
    submenu(sid(deluge::l10n::String::STRING_FOR_SIDECHAIN), kSidechainChildren, Layout::Horizontal);

/// @brief This file's contribution to the sound-editor root: `sidechainMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundSidechainChildren;

} // namespace deluge::scenes::menu::nodes
