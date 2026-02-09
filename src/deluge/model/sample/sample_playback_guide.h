/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#pragma once

#include <cstddef>
#include <cstdint>

class SampleHolder;
class Sample;
class VoiceSample;
class AudioFileHolder;

class SamplePlaybackGuide {
public:
	SamplePlaybackGuide();
	int32_t getFinalClusterIndex(Sample* sample, bool obeyMarkers, int32_t* getEndPlaybackAtByte = nullptr);
	virtual int32_t getBytePosToStartPlayback(bool justLooped) { return startPlaybackAtByte; }
	virtual int32_t getBytePosToEndOrLoopPlayback() {
		return endPlaybackAtByte;
	} // This is actually an important function whose output is the basis for a lot of stuff
	virtual void setupPlaybackBounds(bool reversed);
	[[nodiscard]] virtual uint32_t getLoopStartPlaybackAtByte() const { return startPlaybackAtByte; }
	[[nodiscard]] virtual uint32_t getLoopEndPlaybackAtByte() const { return endPlaybackAtByte; }
	uint64_t getSyncedNumSamplesIn();
	int32_t getNumSamplesLaggingBehindSync(VoiceSample* voiceSample);
	int32_t adjustPitchToCorrectDriftFromSync(VoiceSample* voiceSample, int32_t phaseIncrement);

	int8_t playDirection;
	AudioFileHolder* audioFileHolder; // If this is NULL, it means Voice that contains "me" (this Guide) is not
	                                  // currently playing this Source/Sample, e.g. becacuse its volume was set to 0.

	// And, this might look a bit hackish, but we'll use this to point to the WaveTable if that's what we're using -
	// even though that's not a Sample, and doesn't otherwise need any of this "playback guide" stuff.

	// These byte numbers are all relative to the audio file start, which includes all the headers at the top.
	// If playing reversed, then end will be left of start.
	uint32_t startPlaybackAtByte;
	uint32_t endPlaybackAtByte;

	int32_t sequenceSyncStartedAtTick;
	uint32_t sequenceSyncLengthTicks; // When 0, means no syncing happening
	bool wrapSyncPosition{false};     // When true, modular-wrap position in getSyncedNumSamplesIn
};
