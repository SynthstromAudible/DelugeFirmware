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

#include "gui/context_menu/freeze_arp.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "model/clip/arp_freeze.h"
#include <array>

namespace deluge::gui::context_menu {

FreezeArp& getFreezeArp() {
	static FreezeArp freeze_arp;
	return freeze_arp;
}

char const* FreezeArp::getTitle() {
	return l10n::get(l10n::String::STRING_FOR_FREEZE_ARP_QMARK);
}

std::span<char const*> FreezeArp::getOptions() {
	using enum l10n::String;
	if (display->haveOLED()) {
		static std::array<char const*, 1> options{l10n::get(STRING_FOR_OK)};
		return options;
	}
	static std::array<char const*, 1> options{l10n::get(STRING_FOR_SURE)};
	return options;
}

bool FreezeArp::acceptCurrentOption() {
	soundEditor.exitCompletely();

	using enum l10n::String;
	l10n::String message = STRING_FOR_ARP_FROZEN;
	switch (arp_freeze::freeze(clip)) {
	case arp_freeze::Result::OK:
		uiNeedsRendering(getRootUI());
		break;
	case arp_freeze::Result::RECORDING:
		message = STRING_FOR_CLIP_IS_RECORDING;
		break;
	case arp_freeze::Result::TOO_LONG:
		message = STRING_FOR_CANT_FREEZE_ARP_TOO_LONG;
		break;
	case arp_freeze::Result::TOO_MANY_NOTES:
		message = STRING_FOR_CANT_FREEZE_ARP_TOO_MANY_NOTES;
		break;
	case arp_freeze::Result::NO_MEMORY:
		message = STRING_FOR_ERROR_INSUFFICIENT_RAM;
		break;
	}
	display->displayPopup(l10n::get(message));
	return true;
}

} // namespace deluge::gui::context_menu
