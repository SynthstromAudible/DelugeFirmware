/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "gui/views/arranger_view.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "model/clip/instrument_clip_minder.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/ui/browser/sample_browser.h"
#include "hid/matrix/matrix_driver.h"
#include "util/functions.h"
#include "hid/display/numeric_driver.h"
#include "io/uart/uart.h"
#include "gui/ui/audio_recorder.h"
#include <cstring>
#include "gui/views/session_view.h"
#include "util/lookuptables/lookuptables.h"
#include "playback/mode/session.h"
#include "gui/ui/keyboard_screen.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui_timer_manager.h"
#include "model/song/song.h"
#include "gui/views/audio_clip_view.h"
#include "model/clip/audio_clip.h"
#include "gui/waveform/waveform_renderer.h"
#include "gui/menu_item/colour.h"
#include "hid/led/pad_leds.h"
#include "extern.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
}

MatrixDriver matrixDriver{};

MatrixDriver::MatrixDriver() {
	PadLEDs::init();
}

void MatrixDriver::noPressesHappening(bool inCardRoutine) {

	// Correct any misunderstandings

	for (int x = 0; x < displayWidth + sideBarWidth; x++) {
		for (int y = 0; y < displayHeight; y++) {
			if (padStates[x][y]) {
				padAction(x, y, false);
			}
		}
	}
}

int MatrixDriver::padAction(int x, int y, int velocity) {
	padStates[x][y] = velocity;
	int result = getCurrentUI()->padAction(x, y, velocity);
	if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) {
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	return ACTION_RESULT_DEALT_WITH;
}

bool MatrixDriver::isPadPressed(int x, int y) {
	return padStates[x][y];
}

bool MatrixDriver::isUserDoingBootloaderOverwriteAction() {
	for (int x = 0; x < displayWidth + sideBarWidth; x++) {
		for (int y = 0; y < displayHeight; y++) {
			bool shouldBePressed = (x == 0 && y == 7) || (x == 1 && y == 6) || (x == 2 && y == 5);
			if (padStates[x][y] != shouldBePressed) {
				return false;
			}
		}
	}
	return true;
}
