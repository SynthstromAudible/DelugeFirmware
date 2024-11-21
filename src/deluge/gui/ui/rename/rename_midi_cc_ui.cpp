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

#include "gui/ui/rename/rename_midi_cc_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"

RenameMidiCCUI renameMidiCCUI{};

RenameMidiCCUI::RenameMidiCCUI() {
	title = "CC Name";
}

bool RenameMidiCCUI::opened() {
	bool success = QwertyUI::opened();
	if (!success) {
		return false;
	}

	Clip* clip = getCurrentClip();

	int32_t cc = clip->lastSelectedParamID;

	// if we're not dealing with a real cc number
	// then don't allow user to edit the name
	if (cc < 0 || cc == CC_EXTERNAL_MOD_WHEEL || cc >= kNumRealCCNumbers) {
		return false;
	}

	MIDIInstrument* midiInstrument = (MIDIInstrument*)clip->output;

	String* name = midiInstrument->getNameFromCC(cc);
	if (name) {
		enteredText.set(name);
	}
	else {
		enteredText.clear();
	}

	displayText();

	drawKeys();

	return true;
}

bool RenameMidiCCUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0b11;
	return true;
}

ActionResult RenameMidiCCUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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

void RenameMidiCCUI::enterKeyPress() {

	Clip* clip = getCurrentClip();
	MIDIInstrument* midiInstrument = (MIDIInstrument*)clip->output;
	int32_t cc = clip->lastSelectedParamID;

	midiInstrument->setNameForCC(cc, &enteredText);
	midiInstrument->editedByUser = true; // need to set this to true so that the name gets saved with the song / preset

	exitUI();
}

bool RenameMidiCCUI::exitUI() {
	display->setNextTransitionDirection(-1);
	close();
	return true;
}

ActionResult RenameMidiCCUI::padAction(int32_t x, int32_t y, int32_t on) {

	// Main pad
	if (x < kDisplayWidth) {
		return QwertyUI::padAction(x, y, on);
	}

	// Otherwise, exit
	if (on && !currentUIMode) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		exitUI();
	}

	return ActionResult::DEALT_WITH;
}

ActionResult RenameMidiCCUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}
