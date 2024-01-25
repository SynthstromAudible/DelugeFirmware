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
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/matrix/matrix_driver.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item {

MenuItem* Param::selectButtonPress() {
	if (!Buttons::isShiftButtonPressed()) { // Shift button not pressed,
		return nullptr;                     // So navigate backwards
	}

	// If shift button pressed, delete automation
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);

	modelStack->autoParam->deleteAutomation(action, modelStack);

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));
	return (MenuItem*)0xFFFFFFFF; // Don't navigate away
}
} // namespace deluge::gui::menu_item
