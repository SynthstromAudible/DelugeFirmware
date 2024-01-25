/*
 * Copyright © 2022-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"

class ParamCollection;

class ParamCollectionSummary {
public:
	inline bool containsAutomation() {
		if constexpr (kMaxNumUnsignedIntegerstoRepAllParams > 2) {
			return (whichParamsAreAutomated[0] | whichParamsAreAutomated[1] | whichParamsAreAutomated[2]);
		}
		else {
			return (whichParamsAreAutomated[0] | whichParamsAreAutomated[1]);
		}
	}

	inline void resetInterpolationRecord(int32_t topUintToRepParams) {
		for (int32_t i = topUintToRepParams; i >= 0; i--) {
			whichParamsAreInterpolating[i] = 0;
		}
	}

	inline void resetAutomationRecord(int32_t topUintToRepParams) {
		for (int32_t i = topUintToRepParams; i >= 0; i--) {
			whichParamsAreAutomated[i] = 0;
		}
	}

	void cloneFlagsFrom(ParamCollectionSummary* other) {
		for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) {
			whichParamsAreAutomated[i] = other->whichParamsAreAutomated[i];
		}

		for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) {
			whichParamsAreInterpolating[i] = other->whichParamsAreInterpolating[i];
		}
	}

	ParamCollection* paramCollection;
	uint32_t whichParamsAreAutomated[kMaxNumUnsignedIntegerstoRepAllParams];
	uint32_t whichParamsAreInterpolating[kMaxNumUnsignedIntegerstoRepAllParams];

	// The list of these ParamCollectionSummarys, in ParamManager, must be terminated by one whose values are *all*
	// zero. This helps because if we know this, we can check for stuff faster.
};
