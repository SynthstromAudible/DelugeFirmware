/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "browse.h"
#include "gui/ui/browser/dx_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"

namespace deluge::gui::menu_item {

DxBrowseMenu dxBrowseMenu{l10n::String::STRING_FOR_DX_BROWSER};

void DxBrowseMenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.shouldGoUpOneLevelOnBegin = true;
	// if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) {
	// 	keyboardScreen.exitAuditionMode();
	// }
	bool success = openUI(&dxBrowser);
	if (!success) {
		// if (getCurrentUI() == &soundEditor) soundEditor.goUpOneLevel();
		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	}
}

} // namespace deluge::gui::menu_item
