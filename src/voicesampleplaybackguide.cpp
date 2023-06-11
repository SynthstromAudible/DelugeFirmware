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

#include <AudioFileManager.h>
#include <voicesampleplaybackguide.h>
#include "source.h"
#include "Sample.h"
#include "MultisampleRange.h"
#include "voice.h"
#include "SampleHolderForVoice.h"

VoiceSamplePlaybackGuide::VoiceSamplePlaybackGuide() {
}

void VoiceSamplePlaybackGuide::setupPlaybackBounds(bool reversed) {

	SamplePlaybackGuide::setupPlaybackBounds(reversed);

	int loopStartPlaybackAtSample = 0;
	int loopEndPlaybackAtSample = 0;

	// Loop points are only obeyed if not in STRETCH mode
	if (!sequenceSyncLengthTicks) {

		loopStartPlaybackAtSample = reversed ? ((SampleHolderForVoice*)audioFileHolder)->loopEndPos
		                                     : ((SampleHolderForVoice*)audioFileHolder)->loopStartPos;
		loopEndPlaybackAtSample = reversed ? ((SampleHolderForVoice*)audioFileHolder)->loopStartPos
		                                   : ((SampleHolderForVoice*)audioFileHolder)->loopEndPos;

		if (reversed) { // Don't mix this with the above - we want to keep 0s as 0
			if (loopStartPlaybackAtSample) loopStartPlaybackAtSample--;
			if (loopEndPlaybackAtSample) loopEndPlaybackAtSample--;
		}
	}

	loopStartPlaybackAtByte = 0;
	loopEndPlaybackAtByte = 0;

	Sample* sample = (Sample*)audioFileHolder->audioFile;
	int bytesPerSample = sample->numChannels * sample->byteDepth;

	if (loopStartPlaybackAtSample) {
		loopStartPlaybackAtByte = sample->audioDataStartPosBytes + loopStartPlaybackAtSample * bytesPerSample;
	}
	else {
		loopStartPlaybackAtByte = startPlaybackAtByte;
	}

	if (loopEndPlaybackAtSample)
		loopEndPlaybackAtByte = sample->audioDataStartPosBytes + loopEndPlaybackAtSample * bytesPerSample;
}

// This is, whether to obey the loop-end point as opposed to the actual end-of-sample point (which sometimes might cause looping too)
bool VoiceSamplePlaybackGuide::shouldObeyLoopEndPointNow() {
	return (loopEndPlaybackAtByte && !noteOffReceived);
}

int32_t VoiceSamplePlaybackGuide::getBytePosToStartPlayback(bool justLooped) {
	if (!justLooped) return SamplePlaybackGuide::getBytePosToStartPlayback(justLooped);
	else return loopStartPlaybackAtByte;
}

// This is actually an important function whose output is the basis for a lot of stuff
int32_t VoiceSamplePlaybackGuide::getBytePosToEndOrLoopPlayback() {
	if (shouldObeyLoopEndPointNow()) return loopEndPlaybackAtByte;
	else return SamplePlaybackGuide::getBytePosToEndOrLoopPlayback();
}

int VoiceSamplePlaybackGuide::getLoopingType(Source* source) {
	if (loopEndPlaybackAtByte) return noteOffReceived ? 0 : LOOP_LOW_LEVEL;
	else return (source->repeatMode == SAMPLE_REPEAT_LOOP) ? LOOP_LOW_LEVEL : 0;
}
