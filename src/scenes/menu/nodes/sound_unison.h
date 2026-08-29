#pragma once

/// @brief The unison subtree as data: `unison` and its three columns.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (fix round 1, I3). From
/// `gui/menu_item/generate/dmenus/unison.py` and `generate/g_menus.inc:46-54` (at `ce4a79de0`):
///
// clang-format off
/// | node | legacy global | legacy class |
/// |---|---|---|
/// | `unison_count` | `numUnisonMenu` (`:303`) | `unison::CountToStereoSpread` (`unison/count.h:69`) |
/// | `unison_detune` | `unisonDetuneMenu` (`:304`) | `unison::Detune` (`unison/detune.h:26`) |
/// | `unison_spread` | `unison::stereoSpreadMenu` (`:48`) | `unison::StereoSpread` (`unison/stereoSpread.h:26`) |
/// | `unison` | `unisonMenu` (`:311`) | `HorizontalMenu`, `Layout::FIXED` |
// clang-format on
///
/// @note **Two legacy behaviours have no `Node` spelling and are recorded, not silently dropped.**
///       `CountToStereoSpread::selectButtonPress()` returns `&stereoSpreadMenu` (`unison/count.h:72`)
///       -- pressing select on the count jumps to the spread rather than editing in place; and
///       `unisonMenu` is built with `HorizontalMenu::Layout::FIXED` (`g_menus.inc:54`), a paging mode
///       `Node::Layout` (Vertical / Horizontal) does not carry. Both belong to E2's interpreter work.
///
/// @note **Hints.** `Count` and `Detune` both override `renderInHorizontalMenu` with bespoke art -- a
///       big centred integer (`unison/count.h:57-62`) and three splayed lines (`unison/detune.h:64-108`)
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
inline constexpr Node unison_count =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_UNISON_NUMBER), get_unison_count, set_unison_count,
                        IntRange{1, kMaxNumVoicesUnison, 1}, Hint::None),
               sid(deluge::l10n::String::STRING_FOR_UNISON_NUMBER_MENU_TITLE));
inline constexpr Node unison_detune =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_UNISON_DETUNE), get_unison_detune, set_unison_detune,
                        IntRange{0, kMaxUnisonDetune, 1}, Hint::None),
               sid(deluge::l10n::String::STRING_FOR_UNISON_DETUNE_MENU_TITLE));
inline constexpr Node unison_spread =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_UNISON_STEREO_SPREAD), get_unison_spread,
                        set_unison_spread, IntRange{0, kMaxUnisonStereoSpread, 1}, Hint::Knob),
               sid(deluge::l10n::String::STRING_FOR_UNISON_STEREO_SPREAD_MENU_TITLE));

inline constexpr const Node* kUnisonChildren[] = {&unison_count, &unison_detune, &unison_spread};
inline constexpr Node unison =
    submenu(sid(deluge::l10n::String::STRING_FOR_UNISON), kUnisonChildren, Layout::Horizontal);

/// @brief This file's contribution to `kBatch1Roots`: `unisonMenu` (`g_menus.inc:54`).
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundUnisonChildren;

} // namespace deluge::scenes::menu::nodes
