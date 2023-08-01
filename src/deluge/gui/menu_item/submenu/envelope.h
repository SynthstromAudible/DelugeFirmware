/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#pragma once
#include "gui/menu_item/submenu_referring_to_one_thing.h"

extern void setEnvelopeNumberForTitles(int32_t);

namespace deluge::gui::menu_item::submenu {
template <size_t n>
class Envelope final : public SubmenuReferringToOneThing<n> {
public:
	using SubmenuReferringToOneThing<n>::SubmenuReferringToOneThing;
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) {
		SubmenuReferringToOneThing<n>::beginSession(navigatedBackwardFrom);
		setEnvelopeNumberForTitles(this->thingIndex);
	}
#endif
};

// Template deduction guide, will not be required with P2582@C++23
template <size_t n>
Envelope(const string&, MenuItem* const (&)[n], int32_t) -> Envelope<n>;

} // namespace deluge::gui::menu_item::submenu
