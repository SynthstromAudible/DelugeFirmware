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
/// @brief See `shell_automation_glue.h`.

#include "gui/ui/shell_automation_glue.h"

#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_pad_forward.h" // menuShowsAutomationViewParam
#include "gui/ui/ui.h" // getRootUI, rootUIIsClipMinderScreen, swapOutRootUILowLevel, uiNeedsRendering, currentUIMode
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "synth/modulation/cable.h"

#include "scenes/menu/menu_scene.h"
#include "scenes/menu/transition.h"
#include "scenes/registry.h"

namespace deluge::gui::ui {
namespace {

namespace params = deluge::modulation::params;
using deluge::scenes::menu::AutomationParam;
using deluge::scenes::menu::MenuTransition;
using deluge::scenes::menu::Node;
using deluge::scenes::menu::TransitionCause;

/// The automation view's identity of the param @p node edits, if it edits one.
std::optional<AutomationParam> paramOf(const Node* node) {
	if (node == nullptr) {
		return std::nullopt;
	}
	const auto& scene = deluge::scenes::menu_scene();
	return deluge::scenes::menu::automation_param(*node, scene.menu_context(), scene.state());
}

/// Whether the ported menu is what takes the guest's input (not a scene it opened over itself).
bool menuTakesInput() {
	return &shellHostUI.shell().stack().input_top() == &deluge::scenes::menu_scene();
}

/// Whether `Automation::buttonAction` acts at all: over a clip minder, or the arranger. @p clipMinder is set to which.
bool automationButtonsAct(bool& clipMinder) {
	clipMinder = rootUIIsClipMinderScreen();
	return clipMinder || currentSong->persistentView.lastClipInstanceEnteredStartPos != -1;
}

/// `Automation::buttonAction`'s "menu view" exit: the root goes back to the UI CLIP left.
void leaveMenuView() {
	automationView.onMenuView = false;
	automationView.resetInterpolationShortcutBlinking();
	automationView.resetPadSelectionShortcutBlinking();
	swapOutRootUILowLevel(automationView.previousUI);
	uiNeedsRendering(automationView.previousUI);
	view.setKnobIndicatorLevels();
}

/// `Automation::selectAutomationViewParameter` for @p param: the view's selection, read on the clip (or, off a clip
/// minder, the song) the menu edits.
void selectParam(const AutomationParam& param, bool clipMinder) {
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = nullptr;
	if (clipMinder) {
		modelStack =
		    automationView.getModelStackWithParamForClip(currentSong->setupModelStackWithCurrentClip(modelStackMemory),
		                                                 getCurrentClip(), false, param.id, param.kind);
	}
	else {
		modelStack = currentSong->getModelStackWithParam(
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory), param.id);
	}
	if (modelStack != nullptr) {
		// A cable's source, which the view names it by, is the one furthest from the param (`PatchCableStrength`'s
		// `getS`).
		const PatchSource source = param.kind == params::Kind::PATCH_CABLE
		                               ? deluge::synth::CableTarget{static_cast<uint32_t>(param.id)}.bottom_source()
		                               : PatchSource::NONE;
		automationView.selectParam(modelStack, clipMinder, source);
	}
}

/// `Automation::buttonAction`'s SELECT/BACK arm on the menu's param: leave "menu view", or select @p param in the view.
void selectInView(const AutomationParam& param, bool clipMinder) {
	if (automationView.onMenuView) {
		leaveMenuView();
	}
	else {
		selectParam(param, clipMinder);
		uiNeedsRendering(&automationView);
	}
	view.setModLedStates();
	PadLEDs::reassessGreyout();
}

/// `SoundEditor::handlePotentialParamMenuChange` and the shortcut pads' SELECT, for @p t.
void runTransition(const MenuTransition& t) {
	if (getRootUI() != &automationView) {
		return;
	}
	bool clipMinder = false;
	if (!automationButtonsAct(clipMinder)) {
		return;
	}
	const std::optional<AutomationParam> to = paramOf(t.to);
	switch (t.cause) {
	case TransitionCause::Select:
	case TransitionCause::Back:
	case TransitionCause::ColumnButton:
		// No change of item does nothing. A menu that closes files no record, so BACK out of the menu leaves the view
		// on its param, and in "menu view".
		if (t.from == t.to) {
			return;
		}
		if (to) {
			selectInView(*to, clipMinder);
		}
		else if (paramOf(t.from)) {
			// Off a param onto something else: out of "menu view", or back to the overview.
			if (automationView.onMenuView) {
				leaveMenuView();
			}
			else {
				automationView.initParameterSelection();
				uiNeedsRendering(&automationView);
				PadLEDs::reassessGreyout();
			}
		}
		break;
	case TransitionCause::Shortcut:
		// A shortcut sends SELECT to the item it lands on whatever it left, the same item included.
		if (to) {
			selectInView(*to, clipMinder);
		}
		break;
	}
}

/// `Automation::buttonAction`'s CLIP arm, on the press: from another root into the automation view on @p param in
/// "menu view", or out of it again.
void clipOnParam(const AutomationParam& param, bool clipMinder) {
	RootUI* root = getRootUI();
	if (root != &automationView) {
		automationView.onMenuView = true;
		automationView.previousUI = root;
		selectParam(param, clipMinder);
		swapOutRootUILowLevel(&automationView);
		automationView.initializeView();
		automationView.openedInBackground();
	}
	else if (automationView.onMenuView) {
		leaveMenuView();
	}
	view.setModLedStates();
	PadLEDs::reassessGreyout();
}

/// `SoundEditor::buttonAction`'s KEYBOARD arm over the automation view: out of "menu view", onto the keyboard.
void keyboardFromAutomationView(Clip& clip) {
	if (automationView.onMenuView) {
		clip.onAutomationClipView = false;
		automationView.onMenuView = false;
		indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, true);
	}
	automationView.resetInterpolationShortcutBlinking();
	automationView.resetPadSelectionShortcutBlinking();
	instrumentClipView.resetSelectedNoteRowBlinking();
	swapOutRootUILowLevel(&keyboardScreen);
	keyboardScreen.openedInBackground();
	PadLEDs::reassessGreyout();
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, getRootUI() == &keyboardScreen);
}

} // namespace

void dispatchMenuTransitions() {
	const std::optional<MenuTransition> t = deluge::scenes::menu_scene().take_transition();
	if (!t) {
		return;
	}
	runTransition(*t);
	shellHostUI.refreshRootFacts();
}

std::optional<ActionResult> forwardAutomationButton(deluge::hid::Button b, bool on) {
	using namespace deluge::hid::button;
	if (b == KEYBOARD) {
		// Only the automation view's arm; the keyboard's and the clip view's stay with the menu.
		Clip* clip = rootUIIsClipMinderScreen() ? getCurrentClip() : nullptr;
		if (on && currentUIMode == UI_MODE_NONE && clip != nullptr && clip->type == ClipType::INSTRUMENT
		    && getRootUI() == &automationView) {
			keyboardFromAutomationView(*clip);
			shellHostUI.refreshRootFacts();
			return ActionResult::DEALT_WITH;
		}
		return std::nullopt;
	}
	if (b != CLIP_VIEW && b != X_ENC) {
		return std::nullopt;
	}
	// CLIP and X_ENC are the param item's own (`Automation::buttonAction`); on a horizontal page, its focused column's.
	bool clipMinder = false;
	const std::optional<AutomationParam> param = deluge::scenes::menu_scene().focused_automation_param();
	if (!param || !automationButtonsAct(clipMinder)) {
		return std::nullopt;
	}
	if (b == CLIP_VIEW) {
		if (on) {
			clipOnParam(*param, clipMinder);
			shellHostUI.refreshRootFacts();
		}
		return ActionResult::DEALT_WITH;
	}
	// X_ENC: the view's own, held for zoom and for LEARN's copy and paste.
	if (getRootUI() != &automationView) {
		return std::nullopt;
	}
	(void)automationView.buttonAction(b, on);
	return ActionResult::DEALT_WITH;
}

bool automationEditorTakesKnobs() {
	return getRootUI() == &automationView && automationView.inAutomationEditor();
}

void forwardAutomationGoldTurn(int32_t whichModEncoder, int32_t offset) {
	automationView.modEncoderAction(whichModEncoder, offset);
	deluge::scenes::menu_scene().read_value_again();
}

void forwardAutomationGoldPush(uint8_t whichModEncoder, bool on) {
	automationView.modEncoderButtonAction(whichModEncoder, on);
	deluge::scenes::menu_scene().read_value_again();
}

bool forwardAutomationSelectTurn(int8_t offset) {
	if (getRootUI() != &automationView || !isUIModeActive(UI_MODE_NOTES_PRESSED) || !menuShowsAutomationViewParam()
	    || automationView.multiPadPressSelected) {
		return false;
	}
	const int32_t scaled = Buttons::isButtonPressed(deluge::hid::button::SHIFT) ? offset * 5 : offset;
	automationView.modEncoderAction(0, scaled);
	deluge::scenes::menu_scene().read_value_again();
	return true;
}

std::optional<ActionResult> forwardMenuHorizontal(int32_t offset, uint32_t now_ms) {
	if (!menuTakesInput()) {
		return std::nullopt;
	}
	using HorizontalTurn = deluge::scenes::menu::MenuScene::HorizontalTurn;
	const HorizontalTurn route = deluge::scenes::menu_scene().route_horizontal_turn(
	    offset, Buttons::isButtonPressed(deluge::hid::button::X_ENC), now_ms);
	if (route == HorizontalTurn::Menu) {
		return std::nullopt;
	}
	if (route == HorizontalTurn::Root) {
		RootUI* root = getRootUI();
		if (root == &automationView) {
			(void)automationView.horizontalEncoderAction(offset);
		}
		else if (root == &keyboardScreen) {
			(void)keyboardScreen.horizontalEncoderAction(offset);
		}
	}
	return ActionResult::DEALT_WITH;
}

std::optional<ActionResult> forwardMenuVertical(int32_t offset) {
	if (!menuTakesInput()) {
		return std::nullopt;
	}
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
		return ActionResult::DEALT_WITH;
	}
	return getRootUI()->verticalEncoderAction(offset);
}

} // namespace deluge::gui::ui
