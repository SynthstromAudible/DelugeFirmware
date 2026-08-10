/*
 * Copyright (c) 2024 Sean Ditny
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

#include "gui/context_menu/configure_song_macros.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "model/song/song.h"

extern "C" {
#include "fatfs/ff.h"
}

namespace deluge::gui::context_menu {

PLACE_SDRAM_BSS ConfigureSongMacros configureSongMacros{};

bool ConfigureSongMacros::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0x01; // Only mode (audition) column
	*rows = 0x0;
	return true;
}

char const* ConfigureSongMacros::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_CONFIGURE_SONG_MACROS_SHORT);
}

std::span<char const*> ConfigureSongMacros::getOptions() {
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

bool ConfigureSongMacros::setupAndCheckAvailability() {
	sessionView.enterMacrosConfigMode();
	return true;
}

bool ConfigureSongMacros::acceptCurrentOption() {
	sessionView.exitMacrosConfigMode();
	return false; // return false so you exit out of the context menu
}

ActionResult ConfigureSongMacros::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (b == BACK) {
		sessionView.exitMacrosConfigMode();
	}

	return ContextMenu::buttonAction(b, on, inCardRoutine);
}

ActionResult ConfigureSongMacros::padAction(int32_t x, int32_t y, int32_t on) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	// don't allow user to switch modes
	if (x <= kDisplayWidth) {
		return sessionView.gridHandlePads(x, y, on);
	}
	// exit menu with audition pad column
	else {
		sessionView.exitMacrosConfigMode();
		return ContextMenu::padAction(x, y, on);
	}
}

// renders the selected macro slots kind in the context menu
void ConfigureSongMacros::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	ContextMenu::renderOLED(canvas);

	if (sessionView.selectedMacro != -1) {
		const char* macroKind =
		    sessionView.getMacroKindString(currentSong->sessionMacros[sessionView.selectedMacro].kind);
		int32_t windowHeight = 40;
		int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
		int32_t textPixelY = windowMinY + 20 + kTextSpacingY;
		canvas.drawString(macroKind, 22, textPixelY, kTextSpacingX, kTextSpacingY, 0, OLED_MAIN_WIDTH_PIXELS - 26);
	}
}

ActionResult ConfigureSongMacros::horizontalEncoderAction(int32_t offset) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	return sessionView.gridHandleScroll(offset, 0);
}

ActionResult ConfigureSongMacros::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	return sessionView.gridHandleScroll(0, offset);
}

} // namespace deluge::gui::context_menu
