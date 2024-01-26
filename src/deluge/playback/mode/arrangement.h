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

#include "playback/mode/playback_mode.h"

class ClipInstance;
class Output;
class Song;

class Arrangement final : public PlaybackMode {
public:
	Arrangement();

	// PlaybackMode implementation
	void setupPlayback();
	bool endPlayback(); // Returns whether to do an instant song swap
	void doTickForward(int32_t posIncrement);
	void resetPlayPos(int32_t newPos, bool doingComplete = true, int32_t buttonPressLatency = 0);
	void resyncToSongTicks(Song* song);
	void reversionDone();
	bool isOutputAvailable(Output* output);
	void stopOutputRecordingAtLoopEnd();
	int32_t getPosAtWhichClipWillCut(ModelStackWithTimelineCounter const* modelStack);
	bool willClipContinuePlayingAtEnd(ModelStackWithTimelineCounter const* modelStack);
	bool willClipLoopAtSomePoint(ModelStackWithTimelineCounter const* modelStack);
	void reSyncClip(ModelStackWithTimelineCounter* modelStack, bool mustSetPosToSomething, bool mayResumeClip);

	// Clips remain "active" even after playback has stopped, or after they've finished playing but the next Clip for
	// the Instrument / row hasn't started yet. It'll also become active if the user starts editing one
	void resumeClipInstancePlayback(ClipInstance* clipInstance, bool doingComplete = true,
	                                bool mayActuallyResumeClip = true);
	void rowEdited(Output* output, int32_t startPos, int32_t endPos, Clip* clipRemoved,
	               ClipInstance* clipInstanceAdded);
	int32_t doUniqueCloneOnClipInstance(ClipInstance* clipInstance, int32_t newLength = -1,
	                                    bool shouldCloneRepeats = false);
	int32_t getLivePos(uint32_t* timeRemainder = nullptr);
	void endAnyLinearRecording();

	int32_t lastProcessedPos;
	int32_t playbackStartedAtPos;
};

extern Arrangement arrangement;
