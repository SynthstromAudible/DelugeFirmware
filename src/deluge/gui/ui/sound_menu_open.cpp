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

#include "gui/menu_item/menu_item.h"
#include "gui/ui/shell_guest_services.h" // shellSoundMenuOpen, shellMenuReadValueAgain
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"

namespace deluge::gui::ui {

bool soundMenuOpen() {
	return (getCurrentUI() == &soundEditor && !soundEditor.inSettingsMenu()) || shellSoundMenuOpen();
}

bool soundMenuCurrent() {
	return getCurrentUI() == &soundEditor || shellSoundMenuOpen();
}

void readSoundMenuValueAgain() {
	if (getCurrentUI() == &soundEditor) {
		soundEditor.getCurrentMenuItem()->readValueAgain();
	}
	else if (shellSoundMenuOpen()) {
		shellMenuReadValueAgain();
	}
}

} // namespace deluge::gui::ui
