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

#include "model/consequence/consequence_begin_playback.h"
#include "playback/playback_handler.h"

ConsequenceBeginPlayback::ConsequenceBeginPlayback() {
	// TODO Auto-generated constructor stub
}

ErrorType ConsequenceBeginPlayback::revert(TimeType time, ModelStack* modelStack) {
	if (time == BEFORE) {
		if (playbackHandler.playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) {
			playbackHandler.endPlayback();
		}
	}

	else {
		if (!playbackHandler.playbackState) {
			playbackHandler.setupPlaybackUsingInternalClock(0);
		}
	}

	return NO_ERROR;
}
