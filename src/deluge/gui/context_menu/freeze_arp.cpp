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

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): CI partial-parse false positive
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
	using enum l10n::String;
	// The freeze is checked from here, and the menu is only left once it goes ahead (the change is made from the
	// clip's view): when it's refused, the menu stays open behind the popup
	arp_freeze::Result result = arp_freeze::freeze(clip, [] { soundEditor.exitCompletely(); });

	l10n::String message = STRING_FOR_ARP_FROZEN;
	switch (result) {
	case arp_freeze::Result::OK:
		uiNeedsRendering(getRootUI());
		break;
	case arp_freeze::Result::RECORDING:
		message = STRING_FOR_CLIP_IS_RECORDING;
		break;
	case arp_freeze::Result::NO_NOTES:
		message = STRING_FOR_CANT_FREEZE_ARP_NO_NOTES;
		break;
	case arp_freeze::Result::TOO_LONG:
		message = STRING_FOR_CANT_FREEZE_ARP_TOO_LONG;
		break;
	case arp_freeze::Result::TOO_MANY_NOTES:
		message = STRING_FOR_CANT_FREEZE_ARP_TOO_MANY_NOTES;
		break;
	case arp_freeze::Result::MUTED_ROW:
		message = STRING_FOR_CANT_FREEZE_ARP_MUTED_ROW;
		break;
	case arp_freeze::Result::UNFROZEN_ROW:
		message = STRING_FOR_CANT_FREEZE_ARP_UNFROZEN_ROW;
		break;
	case arp_freeze::Result::AUTOMATION:
		message = STRING_FOR_CANT_FREEZE_ARP_AUTOMATION;
		break;
	case arp_freeze::Result::FILL_NOTES:
		message = STRING_FOR_CANT_FREEZE_ARP_FILL_NOTES;
		break;
	case arp_freeze::Result::OUTSIDE_SCALE:
		message = STRING_FOR_CANT_FREEZE_ARP_OUTSIDE_SCALE;
		break;
	case arp_freeze::Result::OUT_OF_RANGE:
		message = STRING_FOR_CANT_FREEZE_ARP_OUT_OF_RANGE;
		break;
	case arp_freeze::Result::ARP_OFF:
		message = STRING_FOR_ARP_IS_OFF;
		break;
	case arp_freeze::Result::NO_MEMORY:
		message = STRING_FOR_ERROR_INSUFFICIENT_RAM;
		break;
	case arp_freeze::Result::NOT_IN_CLIP_VIEW:
		message = STRING_FOR_ERROR_GENERIC; // A bug: leaving the menu gets back to the clip's view
		break;
	}
	// Refused before leaving the menu: back to it from the confirmation
	if (getCurrentUI() == this) {
		display->setNextTransitionDirection(-1);
		close();
	}
	display->displayPopup(l10n::get(message));
	return true;
}

} // namespace deluge::gui::context_menu
