#pragma once

/// @brief The unison subtree as data: `unison` and its three columns.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (fix round 1, I3). From
/// `gui/menu_item/generate/dmenus/unison.py` and `generate/g_menus.inc:46-54` (at `ce4a79de0`):
///
// clang-format off
/// | node | legacy global | legacy class |
/// |---|---|---|
/// | `unison_count` | `numUnisonMenu` (`:303`) | `unison::CountToStereoSpread` (`unison/count.h:76`) |
/// | `unison_detune` | `unisonDetuneMenu` (`:304`) | `unison::Detune` (`unison/detune.h:27`) |
/// | `unison_spread` | `unison::stereoSpreadMenu` (`:48`) | `unison::StereoSpread` (`unison/stereoSpread.h:27`) |
/// | `unison` | `unisonMenu` (`:311`) | `HorizontalMenu`, `Layout::FIXED` |
// clang-format on
///
/// @note **`unisonMenu` is built with `HorizontalMenu::Layout::FIXED`** (`g_menus.inc:54`), a paging
///       mode `Node::Layout` (Vertical / Horizontal) does not carry. That one still belongs to a
///       later interpreter task. (`CountToStereoSpread::selectButtonPress()` used to be recorded
///       here too; it is now ported -- see `unison_count`'s `select_chain` below.)
///
/// @note **Hints.** `Count` and `Detune` both override `renderInHorizontalMenu` with bespoke art -- a
///       big centred integer (`unison/count.h:57-62`) and three splayed lines (`unison/detune.h:63-104`)
///       -- neither of which is a `ui::oled::Hint`, so they take `Hint::None`. `StereoSpread` does not
///       override it and so keeps `Number`'s default knob (`gui/menu_item/number.h:48`).

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp" // kMaxNumVoicesUnison, kMaxUnisonDetune, kMaxUnisonStereoSpread

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The three `Sound` unison hooks. Bodies (and their citations) in the `.cpp`.
/// @{
int32_t get_unison_count(const MenuContext& ctx);
void set_unison_count(const MenuContext& ctx, int32_t value);
int32_t get_unison_detune(const MenuContext& ctx);
void set_unison_detune(const MenuContext& ctx, int32_t value);
int32_t get_unison_spread(const MenuContext& ctx);
void set_unison_spread(const MenuContext& ctx, int32_t value);
/// @}

// Ranges: `Count::getMinValue`/`getMaxValue` (`unison/count.h:64-65`), `Detune::getMaxValue`
// (`unison/detune.h:61`), `StereoSpread::getMaxValue` (`stereoSpread.h:53`), each over `Number`'s
// default minimum of 0 where no minimum is overridden.
inline constexpr Node unison_detune =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_UNISON_DETUNE), get_unison_detune, set_unison_detune,
                        IntRange{0, kMaxUnisonDetune, 1}, Hint::None),
               sid(deluge::l10n::String::STRING_FOR_UNISON_DETUNE_MENU_TITLE));
// Declared before `unison_count`, which chains to it: `with_select_chain` needs its address.
//
// `kUnisonChildren` below still fixes the **on-screen** order (count, detune, spread) -- declaration
// order in this file never mattered for that. It no longer fixes the **node-id** order, though:
// `assign_node_ids` visits a node's `chain_targets` while visiting the node itself, before the
// parent moves on to its next child, so `unison_spread` is numbered as `unison_count`'s chain target
// and lands *ahead* of `unison_detune` (221 vs 222 in the shipped dump). That is a consequence of
// the walk, not of this reordering, and it is why E2 task 3's id shift is not uniform.
inline constexpr Node unison_spread =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_UNISON_STEREO_SPREAD), get_unison_spread,
                        set_unison_spread, IntRange{0, kMaxUnisonStereoSpread, 1}, Hint::Knob),
               sid(deluge::l10n::String::STRING_FOR_UNISON_STEREO_SPREAD_MENU_TITLE));

/// @brief `CountToStereoSpread::selectButtonPress()` (`unison/count.h:76-79`): a select press on the
///        count opens the stereo spread, unconditionally.
///
/// The one ported chain whose target is already a static child of the same page, so nothing new is
/// declared for it -- `unison_spread` is `unisonMenu`'s third column either way (`menus.cpp:305-311`).
inline constexpr const Node* kUnisonCountChain[] = {&unison_spread};
inline const Node* unison_count_chain(const MenuContext&, const MenuState&) {
	return &unison_spread;
}

inline constexpr Node unison_count =
    with_select_chain(with_title(int_item(sid(deluge::l10n::String::STRING_FOR_UNISON_NUMBER), get_unison_count,
                                          set_unison_count, IntRange{1, kMaxNumVoicesUnison, 1}, Hint::None),
                                 sid(deluge::l10n::String::STRING_FOR_UNISON_NUMBER_MENU_TITLE)),
                      unison_count_chain, kUnisonCountChain);

inline constexpr const Node* kUnisonChildren[] = {&unison_count, &unison_detune, &unison_spread};
inline constexpr Node unison =
    submenu(sid(deluge::l10n::String::STRING_FOR_UNISON), kUnisonChildren, Layout::Horizontal);

/// @brief This file's contribution to `kBatch1Roots`: `unisonMenu` (`g_menus.inc:54`).
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundUnisonChildren;

} // namespace deluge::scenes::menu::nodes
