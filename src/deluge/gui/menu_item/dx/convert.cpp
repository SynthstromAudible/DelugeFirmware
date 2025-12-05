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

#include "convert.h"
#include "gui/ui/browser/dx_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "util/dx7_converter.h"

namespace deluge::gui::menu_item {

DxConvert dxConvert{l10n::String::STRING_FOR_DX_CONVERT};

void DxConvert::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.shouldGoUpOneLevelOnBegin = true;
	display->setNextTransitionDirection(1);
	dxBrowser.setConversionMode(true);
	bool success = openUI(&dxBrowser);
	if (!success) {
		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	}
}

MenuItem* DxConvert::selectButtonPress() {
	// This should never be called since we push the browser UI
	return nullptr;
}

} // namespace deluge::gui::menu_item
