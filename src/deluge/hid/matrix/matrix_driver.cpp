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

#include "hid/matrix/matrix_driver.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include <cstring>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

MatrixDriver matrixDriver{};

MatrixDriver::MatrixDriver() {
	PadLEDs::init();
}

void MatrixDriver::noPressesHappening(bool inCardRoutine) {

	// Correct any misunderstandings

	for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (padStates[x][y]) {
				padAction(x, y, false);
			}
		}
	}
}

ActionResult MatrixDriver::padAction(int32_t x, int32_t y, int32_t velocity) {
	// do not interpret pad actions when stem export is underway
	if (stemExport.processStarted) {
		return ActionResult::DEALT_WITH;
	}

	padStates[x][y] = velocity;
#if ENABLE_MATRIX_DEBUG
	D_PRINT("UI=%s,PAD_X=%d,PAD_Y=%d,VEL=%d", getCurrentUI()->getUIName(), x, y, velocity);
#endif
	auto ui = getCurrentUI();
	if (ui == nullptr) {
		return ActionResult::DEALT_WITH; // only happens when booting
	}
	ActionResult result = getCurrentUI()->padAction(x, y, velocity);
	if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	return ActionResult::DEALT_WITH;
}

bool MatrixDriver::isPadPressed(int32_t x, int32_t y) {
	return padStates[x][y];
}
