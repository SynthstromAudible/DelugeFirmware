/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "model/clip/clip_minder.h"
#include "definitions_cxx.hpp"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"

ActionResult ClipMinder::buttonAction(deluge::hid::Button b, bool on) {
	return ActionResult::NOT_DEALT_WITH;
}

/// Called by button action of active clip view when b == SESSION_VIEW
void ClipMinder::transitionToArrangerOrSession() {
	// should we transition to Arranger View?
	if (currentSong->lastClipInstanceEnteredStartPos != -1 || getCurrentClip()->isArrangementOnlyClip()) {
		// try to transition to Arranger View
		if (arrangerView.transitionToArrangementEditor()) {
			// successfully transitioned
			return;
		}
	}
	// if we didn't transition to arranger, then transition to Session View
	sessionView.transitionToSessionView();
}
