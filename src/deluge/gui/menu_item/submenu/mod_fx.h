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

#include "deluge/gui/l10n/l10n.h"
#include "deluge/gui/menu_item/submenu.h"
#include <string_view>

namespace deluge::gui::menu_item::submenu {
class ModFxHorizontalMenu final : public HorizontalMenu {
public:
	using HorizontalMenu::HorizontalMenu;

	[[nodiscard]] std::string_view getTitle() const override {
		if (renderingStyle() == HorizontalMenu::RenderingStyle::VERTICAL) {
			return Submenu::getTitle();
		}

		if (paging.visiblePageNumber == 0) {
			// On the first page we show the mod fx type selector, so we display regular MOD-FX title
			return deluge::l10n::getView(title);
		}

		// On other pages user can tweak params related to the selected mod fx type, so we show the type name
		// Sorry, a bit of hacky casts here
		const auto* modFxTypeMenuItem = static_cast<mod_fx::Type*>(paging.pages[0].items[0]);
		const auto modFxTypeMenuItemValue = const_cast<mod_fx::Type*>(modFxTypeMenuItem)->getValue();
		return modfx::getModNames()[modFxTypeMenuItemValue];
	}
};
} // namespace deluge::gui::menu_item::submenu
