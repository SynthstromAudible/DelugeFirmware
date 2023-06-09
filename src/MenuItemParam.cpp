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

#include <AudioEngine.h>
#include "MenuItemParam.h"
#include "definitions.h"
#include "ParamSet.h"
#include "soundeditor.h"
#include "numericdriver.h"
#include "matrixdriver.h"
#include "Action.h"
#include "ActionLogger.h"
#include "Buttons.h"
#include "ModelStack.h"

int MenuItemParam::getMaxValue() {
	return 50;
}

int MenuItemParam::getMinValue() {
	return 0;
}

uint8_t MenuItemParam::getP() {
	return p;
}

MenuItem* MenuItemParam::selectButtonPress() {

	// If shift button pressed, delete automation
	if (Buttons::isShiftButtonPressed()) {

		Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);

		modelStack->autoParam->deleteAutomation(action, modelStack);

		numericDriver.displayPopup(HAVE_OLED ? "Automation deleted" : "DELETED");
		return (MenuItem*)0xFFFFFFFF; // Don't navigate away
	}
	else return NULL; // Navigate backwards
}
