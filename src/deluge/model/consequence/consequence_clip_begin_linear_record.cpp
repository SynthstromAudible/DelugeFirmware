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

#include "model/consequence/consequence_clip_begin_linear_record.h"

#include "gui/ui/ui.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"

ConsequenceClipBeginLinearRecord::ConsequenceClipBeginLinearRecord(Clip* newClip) {
	clip = newClip;
	type = Consequence::CLIP_BEGIN_LINEAR_RECORD;
}

int32_t ConsequenceClipBeginLinearRecord::revert(TimeType time, ModelStack* modelStack) {

	// Going backwards...
	if (time == BEFORE) {
		clip->abortRecording();

		// If Clip active, deactivate it
		if (modelStack->song->isClipActive(clip)) {

			// But first, some exceptions

			// Don't deactivate Clip if playback stopped
			if (!playbackHandler.isEitherClockActive() || currentPlaybackMode != &session) {
				return NO_ERROR;
			}

			// Or if Clip is soloing, deactivating that would be a can of worms, whereas this whole auto-deactivation
			// feature is really just intended as an unessential thing the user will find helpful in most cases
			if (clip->soloingInSessionMode) {
				return NO_ERROR;
			}

			// Or if we're viewing the Clip, don't deactivate it, cos it's a massive hassle, and confusing, for user to
			// go out and reactivate it
			if (modelStack->song->getCurrentClip() == clip && getCurrentUI()->toClipMinder()) {
				return NO_ERROR;
			}

doToggle:
			int32_t clipIndex = modelStack->song->sessionClips.getIndexForClip(clip);
			session.toggleClipStatus(clip, &clipIndex, true, 0);
		}
	}

	// Going forwards...
	else { // AFTER

		// If Clip inactive, activate it
		if (!modelStack->song->isClipActive(clip)) {

			// But first, an exception:
			// Check that the reason it's showing as "inactive" isn't just that there's another Clip soloing - in which
			// case we don't want to toggle its status
			if (currentSong->getAnyClipsSoloing()) {
				return NO_ERROR;
			}

			goto doToggle;
		}
	}

	return NO_ERROR;
}
