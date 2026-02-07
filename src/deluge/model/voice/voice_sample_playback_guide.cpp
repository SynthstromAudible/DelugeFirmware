/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "model/voice/voice_sample_playback_guide.h"
#include "definitions_cxx.hpp"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"

VoiceSamplePlaybackGuide::VoiceSamplePlaybackGuide() {
}

void VoiceSamplePlaybackGuide::setupPlaybackBounds(bool reversed) {

	SamplePlaybackGuide::setupPlaybackBounds(reversed);

	int32_t loopStartPlaybackAtSample = 0;
	int32_t loopEndPlaybackAtSample = 0;

	// Loop points are only obeyed if not in STRETCH mode
	if (!sequenceSyncLengthTicks) {

		loopStartPlaybackAtSample = reversed ? ((SampleHolderForVoice*)audioFileHolder)->loopEndPos
		                                     : ((SampleHolderForVoice*)audioFileHolder)->loopStartPos;
		loopEndPlaybackAtSample = reversed ? ((SampleHolderForVoice*)audioFileHolder)->loopStartPos
		                                   : ((SampleHolderForVoice*)audioFileHolder)->loopEndPos;

		if (reversed) { // Don't mix this with the above - we want to keep 0s as 0
			if (loopStartPlaybackAtSample) {
				loopStartPlaybackAtSample--;
			}
			if (loopEndPlaybackAtSample) {
				loopEndPlaybackAtSample--;
			}
		}
	}

	loopStartPlaybackAtByte = 0;
	loopEndPlaybackAtByte = 0;

	Sample* sample = (Sample*)audioFileHolder->audioFile;
	int32_t bytesPerSample = sample->numChannels * sample->byteDepth;

	if (loopStartPlaybackAtSample) {
		loopStartPlaybackAtByte = sample->audioDataStartPosBytes + loopStartPlaybackAtSample * bytesPerSample;
	}
	else {
		loopStartPlaybackAtByte = startPlaybackAtByte;
	}

	if (loopEndPlaybackAtSample) {
		loopEndPlaybackAtByte = sample->audioDataStartPosBytes + loopEndPlaybackAtSample * bytesPerSample;
	}
}

// This is, whether to obey the loop-end point as opposed to the actual end-of-sample point (which sometimes might cause
// looping too)
bool VoiceSamplePlaybackGuide::shouldObeyLoopEndPointNow() {
	return (loopEndPlaybackAtByte && !noteOffReceived);
}

int32_t VoiceSamplePlaybackGuide::getBytePosToStartPlayback(bool justLooped) {
	if (!justLooped) {
		return SamplePlaybackGuide::getBytePosToStartPlayback(justLooped);
	}
	else {
		return loopStartPlaybackAtByte;
	}
}

// This is actually an important function whose output is the basis for a lot of stuff
int32_t VoiceSamplePlaybackGuide::getBytePosToEndOrLoopPlayback() {
	if (shouldObeyLoopEndPointNow()) {
		return loopEndPlaybackAtByte;
	}
	else {
		return SamplePlaybackGuide::getBytePosToEndOrLoopPlayback();
	}
}

LoopType VoiceSamplePlaybackGuide::getLoopingType(const Source& source) const {
	if (loopEndPlaybackAtByte) {
		return noteOffReceived ? LoopType::NONE : LoopType::LOW_LEVEL;
	}
	return (source.repeatMode == SampleRepeatMode::LOOP || source.repeatMode == SampleRepeatMode::PHASE_LOCKED)
	           ? LoopType::LOW_LEVEL
	           : LoopType::NONE;
}
