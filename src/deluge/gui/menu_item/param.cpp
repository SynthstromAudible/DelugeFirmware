/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "param.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"

namespace deluge::gui::menu_item {

MenuItem* Param::selectButtonPress() {
	return Automation::selectButtonPress();
}

ActionResult Param::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	return Automation::buttonAction(b, on, inCardRoutine);
}

void Param::horizontalEncoderAction(int32_t offset) {
	RootUI* rootUI = getRootUI();
	if (rootUI == &automationView) {
		automationView.horizontalEncoderAction(offset);
	}
	else if (rootUI == &keyboardScreen) {
		keyboardScreen.horizontalEncoderAction(offset);
	}
}

ModelStackWithAutoParam* Param::getModelStackWithParam(void* memory) {
	return getModelStack(memory);
}

} // namespace deluge::gui::menu_item
