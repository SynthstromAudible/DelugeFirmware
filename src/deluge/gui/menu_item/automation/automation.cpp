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

#include "automation.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"

namespace deluge::gui::menu_item {

MenuItem* Automation::selectButtonPress() {
	// If shift held down, delete automation
	if (Buttons::isShiftButtonPressed()) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStack = getModelStackWithParam(modelStackMemory);
		if (modelStack && modelStack->autoParam) {
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);

			modelStack->autoParam->deleteAutomation(action, modelStack);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			// if automation view is open in background and automation is deleted
			// then refresh automation view UI
			if (getRootUI() == &automationView) {
				uiNeedsRendering(&automationView);
			}
		}

		return (MenuItem*)0xFFFFFFFF; // No navigation
	}
	return nullptr; // Navigate back
}

ActionResult Automation::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	bool clipMinder = rootUIIsClipMinderScreen();
	bool arrangerView = !clipMinder && (currentSong->lastClipInstanceEnteredStartPos != -1);
	RootUI* rootUI = getRootUI();

	// Clip or Song button
	// Used to enter automation view from sound editor
	if ((b == CLIP_VIEW && clipMinder) || (b == SESSION_VIEW && arrangerView)) {
		if (on) {
			// if we're not in automation view yet
			// save current UI so you can switch back to it once we exit out of current menu
			// flag automation view as onMenuView so we know that we're dealing with the background
			// automation view used exclusively with the menu
			if (rootUI != &automationView) {
				automationView.onMenuView = true;
				automationView.previousUI = rootUI;
				selectAutomationViewParameter(clipMinder);
				swapOutRootUILowLevel(&automationView);
				automationView.initializeView();
				automationView.openedInBackground();
			}
			// if we're in automation view and it's the menu view
			// swap out background UI from automation view to the previous UI
			else if (automationView.onMenuView) {
				automationView.onMenuView = false;
				automationView.resetInterpolationShortcutBlinking();
				automationView.resetPadSelectionShortcutBlinking();
				swapOutRootUILowLevel(automationView.previousUI);
				uiNeedsRendering(automationView.previousUI);
				view.setKnobIndicatorLevels();
			}
			view.setModLedStates();
			PadLEDs::reassessGreyout();
		}
		return ActionResult::DEALT_WITH;
	}
	// Select encoder button, used to change current parameter selection in automation view
	// Back button, used to back out of current automatable parameter menu
	else if ((b == SELECT_ENC || b == BACK) && (clipMinder || arrangerView)) {
		if (on) {
			if (rootUI == &automationView) {
				// if we got here, and we're in the automation menu view
				// then we want to reset the background root UI to the previous UI
				// because you just entered a new menu or backed out of the current param menu
				if (automationView.onMenuView) {
					automationView.onMenuView = false;
					automationView.resetInterpolationShortcutBlinking();
					automationView.resetPadSelectionShortcutBlinking();
					swapOutRootUILowLevel(automationView.previousUI);
					uiNeedsRendering(automationView.previousUI);
					view.setKnobIndicatorLevels();
				}
				// if you are already in automation view and entered an automatable parameter menu
				else {
					selectAutomationViewParameter(clipMinder);
					uiNeedsRendering(rootUI);
				}
				view.setModLedStates();
				PadLEDs::reassessGreyout();
			}
		}
		return ActionResult::DEALT_WITH;
	}
	else if ((b == X_ENC) && (clipMinder || arrangerView)) {
		// Horizontal encoder button to zoom in/out of underlying automation view
		if (rootUI == &automationView) {
			automationView.buttonAction(b, on, inCardRoutine);
			return ActionResult::DEALT_WITH;
		}
	}
	return ActionResult::NOT_DEALT_WITH;
}

void Automation::selectAutomationViewParameter(bool clipMinder) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = getModelStackWithParam(modelStackMemory);
	if (modelStack) {
		int32_t knobPos = automationView.getAutomationParameterKnobPos(modelStack, view.modPos) + kKnobPosOffset;
		automationView.setAutomationKnobIndicatorLevels(modelStack, knobPos, knobPos);

		int32_t p = modelStack->paramId;
		modulation::params::Kind kind = modelStack->paramCollection->getParamKind();

		Clip* clip = getCurrentClip();

		if (clipMinder) {
			clip->lastSelectedParamID = p;
			clip->lastSelectedParamKind = kind;
			clip->lastSelectedOutputType = clip->output->type;
			clip->lastSelectedPatchSource = getPatchSource();
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
			clip->lastSelectedParamArrayPosition = 0;
		}
		else {
			currentSong->lastSelectedParamID = p;
			currentSong->lastSelectedParamKind = kind;
			currentSong->lastSelectedParamShortcutX = kNoSelection;
			currentSong->lastSelectedParamShortcutY = kNoSelection;
			currentSong->lastSelectedParamArrayPosition = 0;
			automationView.onArrangerView = true;
		}
		// not blinking any shortcuts for patch cables
		// no scroll selection for patch cables
		if (kind != deluge::modulation::params::Kind::PATCH_CABLE) {
			automationView.getLastSelectedParamShortcut(clip);
			automationView.getLastSelectedParamArrayPosition(clip);
		}
	}
}

void Automation::horizontalEncoderAction(int32_t offset) {
	if (getRootUI() == &automationView) {
		automationView.horizontalEncoderAction(offset);
	}
}

} // namespace deluge::gui::menu_item
