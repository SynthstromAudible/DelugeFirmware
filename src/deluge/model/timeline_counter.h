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

#pragma once

#include "modulation/params/param_manager.h"
#include <cstdint>

class Song;
class Sound;
class ModControllable;
class ModelStackWithTimelineCounter;

class TimelineCounter {
public:
	TimelineCounter();
	virtual ~TimelineCounter();

	/// Get the tick at which this timeline counter last did anything
	virtual int32_t getLastProcessedPos() = 0;
	/// Get the current tick of this timeline counter relative to the playback handler
	virtual uint32_t getLivePos() = 0;
	virtual int32_t getLoopLength() = 0;
	virtual bool isPlayingAutomationNow() = 0;
	virtual bool backtrackingCouldLoopBackToEnd() = 0;
	virtual int32_t getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack) = 0;
	virtual bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* modelStack) {
		return false;
	} // Returns whether any change.

	virtual void getActiveModControllable(ModelStackWithTimelineCounter* modelStack) = 0;
	virtual void expectEvent() = 0;
	virtual TimelineCounter* getTimelineCounterToRecordTo() = 0;
	virtual void instrumentBeenEdited() {}

	ParamManagerForTimeline paramManager;
};
