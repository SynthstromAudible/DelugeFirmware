/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

#include "dsp/delay/delay_buffer.h"
#include <cstdint>

class ParamManagerForTimeline;
class ParamManager;
class StereoSample;

struct StutterConfig {
	bool useSongStutter = true;
	bool quantized = true;
	bool reversed = false;
	bool pingPong = false;
};

class Stutterer {
public:
	Stutterer() = default;
	static void initParams(ParamManager* paramManager);
	inline bool isStuttering(void* source) { return stutterSource == source; }
	// These calls are slightly awkward with the magniture & timePerTickInverse, but that's the price for not depending
	// on currentSong and playbackhandler...
	[[nodiscard]] Error beginStutter(void* source, ParamManagerForTimeline* paramManager, StutterConfig stutterConfig,
	                                 int32_t magnitude, uint32_t timePerTickInverse);
	void processStutter(StereoSample* buffer, int32_t numSamples, ParamManager* paramManager, int32_t magnitude,
	                    uint32_t timePerTickInverse);
	void endStutter(ParamManagerForTimeline* paramManager = nullptr);

private:
	enum class Status {
		OFF,
		RECORDING,
		PLAYING,
	};
	int32_t getStutterRate(ParamManager* paramManager, int32_t magnitude, uint32_t timePerTickInverse);
	bool currentReverse;
	DelayBuffer buffer;
	Status status = Status::OFF;
	// TODO: This is currently unused! It's set to 7 initially, and never modified. Either we should set it depending
	// on sync, or get rid of it entirely.
	uint8_t sync = 7;
	StutterConfig stutterConfig;
	int32_t sizeLeftUntilRecordFinished = 0;
	int32_t valueBeforeStuttering = 0;
	int32_t lastQuantizedKnobDiff = 0;
	/// This functions as cookie, allowing different users to know who is currently stuttering, so only those who
	/// are will send audio here.
	void* stutterSource = nullptr;
};

// There's only one stutter effect active at a time, so we have a global stutterer to save memory.
extern Stutterer stutterer;
