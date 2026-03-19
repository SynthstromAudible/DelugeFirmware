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

#include "model/consequence/consequence.h"
#include "model/sequencing/lanes_engine.h"
#include <array>

class ConsequenceLanesChange final : public Consequence {
public:
	/// Construct from a LanesEngine pointer (captures current state for undo).
	ConsequenceLanesChange(LanesEngine* engine);
	Error revert(TimeType time, ModelStack* modelStack) override;

private:
	LanesEngine* engine_;

	struct LaneSnapshot {
		std::array<int16_t, kMaxLanesSteps> values;
		std::array<uint8_t, kMaxLanesSteps> stepProbability;
		uint8_t length;
		LaneDirection direction;
		uint8_t euclideanPulses;
		uint8_t clockDivision;
		bool muted;
	};

	LaneSnapshot lanes_[LanesEngine::kNumLanes];
	int16_t baseNote_;
	int32_t intervalMin_;
	int32_t intervalMax_;
	bool intervalScaleAware_;
	uint8_t defaultVelocity_;
	uint16_t trackLength_;
	bool locked_;
	uint32_t lockedSeed_;
};
