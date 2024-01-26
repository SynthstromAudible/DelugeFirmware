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

#include <cstdint>

class Song;
class StereoSample;
class InstrumentClip;
class ArrangementRow;
class Output;
class Clip;
class ModelStackWithTimelineCounter;

class PlaybackMode {
public:
	PlaybackMode();
	virtual ~PlaybackMode();
	bool hasPlaybackActive();

	virtual void setupPlayback() = 0; // Call this *before* resetPlayPos
	virtual bool endPlayback() = 0;   // Returns whether to do an instant song swap
	virtual void doTickForward(int32_t posIncrement) = 0;
	virtual void resetPlayPos(int32_t newPos, bool doingComplete = true, int32_t buttonPressLatency = 0) = 0;
	virtual void resyncToSongTicks(Song* song) = 0;
	virtual void reversionDone() = 0; // This is only to be called if playbackHandler.isEitherClockActive()
	virtual bool isOutputAvailable(Output* output) = 0;
	virtual bool considerLaunchEvent(int32_t numTicksBeingIncremented) {
		return false;
	} // Returns whether Song was swapped
	virtual void stopOutputRecordingAtLoopEnd() = 0;
	virtual int32_t
	getPosAtWhichClipWillCut(ModelStackWithTimelineCounter const*
	                             modelStack) = 0; // That's *cut* - as in, cut out abruptly. If it's looping, and the
	                                              // user isn't stopping it, that's not a cut.
	virtual bool
	willClipContinuePlayingAtEnd(ModelStackWithTimelineCounter const*
	                                 modelStack) = 0; // We say "continue playing" now, because we want to include a
	                                                  // pingpong, which arguably doesn't fall under "loop".
	virtual bool willClipLoopAtSomePoint(
	    ModelStackWithTimelineCounter const*
	        modelStack) = 0; // This includes it "looping" in arranger before the Clip's full length due to that
	                         // ClipInstance ending, and there being another instance of the same Clip right after.
	virtual bool wantsToDoTempolessRecord(int32_t newPos) { return false; }
	virtual void
	reSyncClip(ModelStackWithTimelineCounter* modelStack, bool mustSetPosToSomething = false,
	           bool mayResumeClip = true) = 0; // Check playbackHandler.isEitherClockActive() before calling this.
};

extern PlaybackMode* currentPlaybackMode;
