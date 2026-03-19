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
#include <algorithm>

ConsequenceLanesChange::ConsequenceLanesChange(LanesEngine* engine) : engine_(engine) {
	if (!engine_) {
		return;
	}

	baseNote_ = engine_->baseNote;
	intervalMin_ = engine_->intervalMin;
	intervalMax_ = engine_->intervalMax;
	intervalScaleAware_ = engine_->intervalScaleAware;
	defaultVelocity_ = engine_->defaultVelocity;
	trackLength_ = engine_->trackLength;
	locked_ = engine_->locked;
	lockedSeed_ = engine_->lockedSeed;

	for (int32_t i = 0; i < LanesEngine::kNumLanes; i++) {
		LanesLane* lane = engine_->getLane(i);
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
	if (!engine_) {
		return Error::NONE;
	}

	// Swap engine-level state with snapshot
	std::swap(baseNote_, engine_->baseNote);
	std::swap(intervalMin_, engine_->intervalMin);
	std::swap(intervalMax_, engine_->intervalMax);
	std::swap(intervalScaleAware_, engine_->intervalScaleAware);
	std::swap(defaultVelocity_, engine_->defaultVelocity);
	std::swap(trackLength_, engine_->trackLength);
	std::swap(locked_, engine_->locked);
	std::swap(lockedSeed_, engine_->lockedSeed);

	// Swap per-lane state
	for (int32_t i = 0; i < LanesEngine::kNumLanes; i++) {
		LanesLane* lane = engine_->getLane(i);
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
