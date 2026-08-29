/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief The unison subtree: `unisonMenu` and its three columns.
///
/// From `gui/menu_item/generate/dmenus/unison.py` and `generate/g_menus.inc:46-54`:
///
/// | node | legacy global | legacy class |
/// |---|---|---|
/// | `unison_count` | `numUnisonMenu` (`:46`) | `unison::CountToStereoSpread` (`unison/count.h:69`) |
/// | `unison_detune` | `unisonDetuneMenu` (`:47`) | `unison::Detune` (`unison/detune.h:26`) |
/// | `unison_spread` | `unison::stereoSpreadMenu` (`:48`) | `unison::StereoSpread` (`unison/stereoSpread.h:26`) |
/// | `unison` | `unisonMenu` (`:54`) | `HorizontalMenu`, `Layout::FIXED` |
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

#include "scenes/menu/nodes/nodes.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp"
#include "processing/sound/sound.h"

#include <array>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

int32_t get_unison_count(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->numUnison : 1; // `unison/count.h:35`
}
void set_unison_count(const MenuContext& ctx, int32_t value) {
	// `Count::writeCurrentValue` (`unison/count.h:37-63`): the affect-entire kit loop, then
	// `setNumUnison` with the sound's own model stack -- which is why `apply_to_targets` hands one
	// over (`bindings.h`'s `SoundTarget::stack`).
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->setNumUnison(v, t.stack);
		                           }
	                           });
}

int32_t get_unison_detune(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->unisonDetune : 0; // `unison/detune.h:29`
}
void set_unison_detune(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->setUnisonDetune(v, t.stack); // `unison/detune.h:31-57`
		                           }
	                           });
}

int32_t get_unison_spread(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->unisonStereoSpread : 0; // `unison/stereoSpread.h:28`
}
void set_unison_spread(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.sound != nullptr) {
			    t.sound->setUnisonStereoSpread(v); // `unison/stereoSpread.h:30-51` -- no stack needed
		    }
	    });
}

// ---------------------------------------------------------------------------------------------
// The tree. Ranges: `Count::getMinValue`/`getMaxValue` (`unison/count.h:64-65`),
// `Detune::getMaxValue` (`unison/detune.h:61`), `StereoSpread::getMaxValue` (`stereoSpread.h:53`),
// each over `Number`'s default minimum of 0 where no minimum is overridden.

constexpr Node unison_count = with_title(int_item(sid(String::STRING_FOR_UNISON_NUMBER), get_unison_count,
                                                  set_unison_count, IntRange{1, kMaxNumVoicesUnison, 1}, Hint::None),
                                         sid(String::STRING_FOR_UNISON_NUMBER_MENU_TITLE));
constexpr Node unison_detune = with_title(int_item(sid(String::STRING_FOR_UNISON_DETUNE), get_unison_detune,
                                                   set_unison_detune, IntRange{0, kMaxUnisonDetune, 1}, Hint::None),
                                          sid(String::STRING_FOR_UNISON_DETUNE_MENU_TITLE));
constexpr Node unison_spread =
    with_title(int_item(sid(String::STRING_FOR_UNISON_STEREO_SPREAD), get_unison_spread, set_unison_spread,
                        IntRange{0, kMaxUnisonStereoSpread, 1}, Hint::Knob),
               sid(String::STRING_FOR_UNISON_STEREO_SPREAD_MENU_TITLE));

constexpr const Node* kUnisonChildren[] = {&unison_count, &unison_detune, &unison_spread};
constexpr Node unison = submenu(sid(String::STRING_FOR_UNISON), kUnisonChildren, Layout::Horizontal);

constexpr const Node* kRoots[] = {&unison};

} // namespace

const std::span<const Node* const> kSoundUnisonChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
