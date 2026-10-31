#pragma once
/// @file
/// @brief A legacy submenu's children in the order the menu shows them.
///
/// `eq::EqMenu::ensureCorrectItemsOrderAndGetValues` (`gui/menu_item/eq/eq_menu.h:120-160`) reorders its four items on
/// its first render, to bass, bass frequency, treble frequency, treble -- the graph's handles left to right -- and the
/// port's `nodes::eq` lists them in that order. A spec walking legacy's tree against the port's reads the reordered
/// list; every other menu's is its constructor's.

// `eq_menu.h` names `UnpatchedParam` without including it.
#include "gui/menu_item/unpatched_param.h"

#include "gui/menu_item/eq/eq_menu.h"
#include "gui/menu_item/submenu.h"
#include "modulation/params/param.h"

#include <algorithm>
#include <array>

namespace deluge::scenes::spec {

/// @brief @p items, the children of @p menu, in the order `menu` shows them.
inline deluge::vector<::MenuItem*> in_menu_order(::MenuItem& menu, const deluge::vector<::MenuItem*>& items) {
	deluge::vector<::MenuItem*> out = items;
	if (dynamic_cast<deluge::gui::menu_item::eq::EqMenu*>(&menu) == nullptr) {
		return out;
	}
	namespace params = deluge::modulation::params;
	constexpr std::array kOrder{params::UNPATCHED_BASS, params::UNPATCHED_BASS_FREQ, params::UNPATCHED_TREBLE_FREQ,
	                            params::UNPATCHED_TREBLE};
	const auto rank = [&](::MenuItem* item) {
		const auto p = static_cast<deluge::gui::menu_item::UnpatchedParam*>(item)->getP();
		return std::distance(kOrder.begin(), std::ranges::find(kOrder, p));
	};
	std::ranges::stable_sort(out, {}, rank);
	return out;
}

} // namespace deluge::scenes::spec
