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

#include <ArrangerView.h>
#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <InstrumentClipMinder.h>
#include <InstrumentClipView.h>
#include <samplebrowser.h>
#include "matrixdriver.h"
#include "functions.h"
#include "numericdriver.h"
#include "uart.h"
#include "AudioRecorder.h"
#include <string.h>
#include <SessionView.h>
#include "lookuptables.h"
#include "Session.h"
#include "KeyboardScreen.h"
#include "SampleMarkerEditor.h"
#include "uitimermanager.h"
#include "song.h"
#include "AudioClipView.h"
#include "AudioClip.h"
#include "WaveformRenderer.h"
#include "MenuItemColour.h"
#include "PadLEDs.h"
#include "extern.h"

extern "C" {
#include "sio_char.h"
}

MatrixDriver matrixDriver;

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
	if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

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
