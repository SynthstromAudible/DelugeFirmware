/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/clear_macro_target.h"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "hid/display/display.h"

namespace deluge::gui::context_menu {

ClearMacroTarget clearMacroTarget{};

char const* ClearMacroTarget::getTitle() {
	return l10n::get(l10n::String::STRING_FOR_DELETE_QMARK);
}

std::span<char const*> ClearMacroTarget::getOptions() {
	using enum l10n::String;
	if (display->haveOLED()) {
		static char const* options[] = {l10n::get(STRING_FOR_OK)};
		return {options, 1};
	}
	static char const* options[] = {l10n::get(STRING_FOR_SURE)};
	return {options, 1};
}

bool ClearMacroTarget::acceptCurrentOption() {
	automationView.acceptPendingTargetClear();
	return false; // exit the menu either way
}

} // namespace deluge::gui::context_menu
