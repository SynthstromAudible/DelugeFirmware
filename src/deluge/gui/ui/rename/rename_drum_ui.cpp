/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/ui/rename/rename_drum_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

RenameDrumUI renameDrumUI{};

RenameDrumUI::RenameDrumUI() {
	title = "Rename item";
}

bool RenameDrumUI::opened() {
	bool success = QwertyUI::opened();
	if (!success) {
		return false;
	}

	enteredText.set(&getDrum()->name);

	displayText();

	drawKeys();

	return true;
}

bool RenameDrumUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0b11;
	return true;
}

SoundDrum* RenameDrumUI::getDrum() {
	return (SoundDrum*)soundEditor.currentSound;
}

ActionResult RenameDrumUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Back button
	if (b == BACK) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitUI();
		}
	}

	// Select encoder button
	else if (b == SELECT_ENC) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			enterKeyPress();
		}
	}

	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

void RenameDrumUI::enterKeyPress() {

	if (enteredText.isEmpty()) {
		return;
	}

	// If actually changing it...
	if (!getDrum()->name.equalsCaseIrrespective(&enteredText)) {
		if (getCurrentKit()->getDrumFromName(enteredText.get())) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DUPLICATE_NAMES));
			return;
		}
	}

	getDrum()->name.set(&enteredText);
	exitUI();
}

void RenameDrumUI::exitUI() {
	display->setNextTransitionDirection(-1);
	close();
}

ActionResult RenameDrumUI::padAction(int32_t x, int32_t y, int32_t on) {

	// Audition pad
	if (x == kDisplayWidth + 1) {
		return instrumentClipView.padAction(x, y, on);
	}

	// Main pad
	else if (x < kDisplayWidth) {
		return QwertyUI::padAction(x, y, on);
	}

	// Otherwise, exit
	else {
		if (on && !currentUIMode) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitUI();
		}
	}

	return ActionResult::DEALT_WITH;
}

ActionResult RenameDrumUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
		return ActionResult::DEALT_WITH;
	}
	return instrumentClipView.verticalEncoderAction(offset, inCardRoutine);
}
