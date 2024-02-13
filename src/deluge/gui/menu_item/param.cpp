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
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
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

ActionResult Param::buttonAction(deluge::hid::Button b, bool on) {
	using namespace deluge::hid::button;

	// Clip or Song button
	// Used to enter automation view from sound editor

	bool clipMinder = rootUIIsClipMinderScreen();

	if ((b == CLIP_VIEW && clipMinder)
	    || (b == SESSION_VIEW && !clipMinder && currentSong->lastClipInstanceEnteredStartPos != -1)) {
		if (on) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);

			int32_t p = getP();
			modulation::params::Kind kind = modelStack->paramCollection->getParamKind();

			if (clipMinder) {
				Clip* clip = getCurrentClip();
				clip->lastSelectedParamID = p;
				clip->lastSelectedParamKind = kind;
			}
			else {
				currentSong->lastSelectedParamID = p;
				currentSong->lastSelectedParamKind = kind;
				automationView.onArrangerView = true;
			}

			swapOutRootUILowLevel(&automationView);
			automationView.openedInBackground();
			soundEditor.exitCompletely();
		}
		return ActionResult::DEALT_WITH;
	}
	return ActionResult::NOT_DEALT_WITH;
}

} // namespace deluge::gui::menu_item
