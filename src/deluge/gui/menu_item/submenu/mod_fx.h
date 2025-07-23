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
		if (renderingStyle() == VERTICAL) {
			return Submenu::getTitle();
		}

		if (paging.visiblePageNumber == 0) {
			// On the first page we show the mod fx type selector, so we display a regular MOD-FX title
			return deluge::l10n::getView(title);
		}

		// On other pages user can tweak params related to the selected mod fx type, so we show the type name
		const ModFXType modFxType = soundEditor.currentModControllable->modFXType_;
		return modfx::getModNames()[static_cast<uint8_t>(modFxType)];
	}
};
} // namespace deluge::gui::menu_item::submenu
