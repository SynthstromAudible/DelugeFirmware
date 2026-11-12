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
/// @brief See `util/sound_menu_open.h`.

#include "util/sound_menu_open.h"

#include "gui/ui/shell_choice_dispatch.h" // shellMenuModControllable, shellMenuParamManager
#include "gui/ui/shell_guest_services.h"  // the shell* arms
#include "util/shell_menu_entry.h"

namespace deluge::gui::ui {

bool soundMenuOpen() {
	return shellSoundMenuOpen();
}

bool soundMenuKitGlobalFx() {
	return shellKitGlobalFx();
}

bool soundMenuCurrent() {
	return shellSoundMenuOpen();
}

ModControllableAudio* soundMenuModControllable() {
	return shellMenuModControllable();
}

ParamManagerForTimeline* soundMenuParamManager() {
	return shellMenuParamManager();
}

bool menuCurrent() {
	return shellMenuFrontmost();
}

void readSoundMenuValueAgain() {
	if (shellMenuFrontmost()) {
		shellMenuReadValueAgain();
	}
}

void mpeZonesPotentiallyUpdated() {
	shellMpeZonesPotentiallyUpdated();
}

std::optional<int32_t> dxColumnFlashRow() {
	if (shellSoundMenuOpen()) {
		return shellDxColumnFlashRow();
	}
	return std::nullopt;
}

bool drumSelectionFrozen() {
	return shellFreezesDrumSelection();
}

bool rangePickerOpen() {
	return shellRangePickerOpen();
}

bool rangeDependentMenuOpen() {
	return shellRangeDependentOpen();
}

bool noteWithinCurrentRange(int32_t untransposed) {
	if (shellRangeDependentOpen()) {
		return shellNoteWithinCurrentRange(untransposed);
	}
	return true;
}

void changeRangeForNote(int32_t note) {
	if (shellRangePickerOpen()) {
		shellChangeRangeForNote(note);
	}
}

ActionResult menuShortcutPadAction(int32_t x, int32_t y, bool on) {
	return shellShortcutPadAction(x, y, on);
}

} // namespace deluge::gui::ui
