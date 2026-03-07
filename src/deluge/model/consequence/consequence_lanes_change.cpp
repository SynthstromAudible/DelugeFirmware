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

#include "model/consequence/consequence_lanes_change.h"
#include "model/clip/instrument_clip.h"
#include <algorithm>

ConsequenceLanesChange::ConsequenceLanesChange(InstrumentClip* clip) : clip_(clip) {
	LanesEngine* engine = clip->lanesEngine.get();
	if (!engine) {
		return;
	}

	baseNote_ = engine->baseNote;
	intervalMin_ = engine->intervalMin;
	intervalMax_ = engine->intervalMax;
	intervalScaleAware_ = engine->intervalScaleAware;
	defaultVelocity_ = engine->defaultVelocity;
	trackLength_ = engine->trackLength;
	locked_ = engine->locked;
	lockedSeed_ = engine->lockedSeed;

	for (int32_t i = 0; i < LanesEngine::kNumLanes; i++) {
		LanesLane* lane = engine->getLane(i);
		if (!lane) {
			continue;
		}
		lanes_[i].values = lane->values;
		lanes_[i].stepProbability = lane->stepProbability;
		lanes_[i].length = lane->length;
		lanes_[i].direction = lane->direction;
		lanes_[i].euclideanPulses = lane->euclideanPulses;
		lanes_[i].clockDivision = lane->clockDivision;
		lanes_[i].muted = lane->muted;
	}
}

Error ConsequenceLanesChange::revert(TimeType time, ModelStack* modelStack) {
	LanesEngine* engine = clip_->lanesEngine.get();
	if (!engine) {
		return Error::NONE;
	}

	// Swap engine-level state with snapshot
	std::swap(baseNote_, engine->baseNote);
	std::swap(intervalMin_, engine->intervalMin);
	std::swap(intervalMax_, engine->intervalMax);
	std::swap(intervalScaleAware_, engine->intervalScaleAware);
	std::swap(defaultVelocity_, engine->defaultVelocity);
	std::swap(trackLength_, engine->trackLength);
	std::swap(locked_, engine->locked);
	std::swap(lockedSeed_, engine->lockedSeed);

	// Swap per-lane state
	for (int32_t i = 0; i < LanesEngine::kNumLanes; i++) {
		LanesLane* lane = engine->getLane(i);
		if (!lane) {
			continue;
		}
		std::swap(lanes_[i].values, lane->values);
		std::swap(lanes_[i].stepProbability, lane->stepProbability);
		std::swap(lanes_[i].length, lane->length);
		std::swap(lanes_[i].direction, lane->direction);
		std::swap(lanes_[i].euclideanPulses, lane->euclideanPulses);
		std::swap(lanes_[i].clockDivision, lane->clockDivision);
		std::swap(lanes_[i].muted, lane->muted);
	}

	return Error::NONE;
}
