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

#include "model/consequence/consequence_audio_clip_set_sample.h"
#include "io/debug/log.h"
#include "model/clip/audio_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"

ConsequenceAudioClipSetSample::ConsequenceAudioClipSetSample(AudioClip* newClip) {

	clip = newClip;
	filePathToRevertTo.set(&newClip->sampleHolder.filePath);
	endPosToRevertTo = newClip->sampleHolder.endPos;
}

int32_t ConsequenceAudioClipSetSample::revert(TimeType time, ModelStack* modelStack) {

	String filePathBeforeRevert;
	filePathBeforeRevert.set(&clip->sampleHolder.filePath);
	uint64_t endPosBeforeRevert = clip->sampleHolder.endPos;

	clip->unassignVoiceSample();

	clip->sampleHolder.filePath.set(&filePathToRevertTo);
	clip->sampleHolder.endPos = endPosToRevertTo;

	if (filePathToRevertTo.isEmpty()) {

		clip->sampleHolder.setAudioFile(NULL);

		// Deactivate Clip if it'd otherwise suddenly start recording again
		if (playbackHandler.playbackState && playbackHandler.recording == RecordingMode::NORMAL) {
			clip->activeIfNoSolo = false;
		}
	}
	else {
		int32_t error = clip->sampleHolder.loadFile(false, false, true);
		if (error) {
			display->displayError(error); // Rare, shouldn't cause later problems.
		}

		if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(clip)) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			clip->resumePlayback(modelStackWithTimelineCounter);
		}
	}

	clip->renderData.xScroll = -1; // Force re-render

	filePathToRevertTo.set(&filePathBeforeRevert);
	endPosToRevertTo = endPosBeforeRevert;

	return NO_ERROR;
}
