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

#include "gui/context_menu/midi_learn_mode.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "model/song/song.h"

extern "C" {
#include "fatfs/ff.h"
}

namespace deluge::gui::context_menu {

MidiLearnMode midiLearnMode{};

bool MidiLearnMode::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0x01; // Only mode (audition) column
	*rows = 0x0;
	return true;
}

char const* MidiLearnMode::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_MIDI_LEARN);
}

Sized<char const**> MidiLearnMode::getOptions() {
	using enum l10n::String;

	if (display->haveOLED()) {
		static char const* options[] = {l10n::get(STRING_FOR_CONFIGURE_SONG_MACROS_EXIT)};
		return {options, 1};
	}
	else {
		static char const* options[] = {l10n::get(STRING_FOR_CONFIGURE_SONG_MACROS_EXIT)};
		return {options, 1};
	}
}

bool MidiLearnMode::setupAndCheckAvailability() {
	sessionView.enterMidiLearnMode();
	return (currentUIMode == UI_MODE_MIDI_LEARN);
}

bool MidiLearnMode::acceptCurrentOption() {
	sessionView.exitMidiLearnMode();
	return false; // return false so you exit out of the context menu
}

ActionResult MidiLearnMode::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (b == BACK) {
		sessionView.exitMidiLearnMode();
	}

	return ContextMenu::buttonAction(b, on, inCardRoutine);
}

ActionResult MidiLearnMode::padAction(int32_t x, int32_t y, int32_t on) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	// don't allow user to switch modes
	if (x <= kDisplayWidth) {
		return sessionView.padAction(x, y, on);
	}
	// exit menu with audition pad column
	else {
		sessionView.exitMidiLearnMode();
		return ContextMenu::padAction(x, y, on);
	}
}

// renders the selected macro slots kind in the context menu
void MidiLearnMode::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	ContextMenu::renderOLED(canvas);
}

ActionResult MidiLearnMode::horizontalEncoderAction(int32_t offset) {
	return sessionView.horizontalEncoderAction(offset);
}

ActionResult MidiLearnMode::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return sessionView.verticalEncoderAction(offset, inCardRoutine);
}

} // namespace deluge::gui::context_menu
