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

#ifndef PARAMCOLLECTIONSUMMARY_H_
#define PARAMCOLLECTIONSUMMARY_H_

#include "definitions.h"

class ParamCollection;

class ParamCollectionSummary {
public:
	inline bool containsAutomation() {
		return (whichParamsAreAutomated[0] | whichParamsAreAutomated[1]
#if MAX_NUM_UINTS_TO_REP_ALL_PARAMS > 2
		        | whichParamsAreAutomated[2]
#endif
		);
	}

	inline void resetInterpolationRecord(int topUintToRepParams) {
		for (int i = topUintToRepParams; i >= 0; i--) {
			whichParamsAreInterpolating[i] = 0;
		}
	}

	inline void resetAutomationRecord(int topUintToRepParams) {
		for (int i = topUintToRepParams; i >= 0; i--) {
			whichParamsAreAutomated[i] = 0;
		}
	}

	void cloneFlagsFrom(ParamCollectionSummary* other) {
		for (int i = 0; i < MAX_NUM_UINTS_TO_REP_ALL_PARAMS; i++) {
			whichParamsAreAutomated[i] = other->whichParamsAreAutomated[i];
		}

		for (int i = 0; i < MAX_NUM_UINTS_TO_REP_ALL_PARAMS; i++) {
			whichParamsAreInterpolating[i] = other->whichParamsAreInterpolating[i];
		}
	}

	ParamCollection* paramCollection;
	uint32_t whichParamsAreAutomated[MAX_NUM_UINTS_TO_REP_ALL_PARAMS];
	uint32_t whichParamsAreInterpolating[MAX_NUM_UINTS_TO_REP_ALL_PARAMS];

	// The list of these ParamCollectionSummarys, in ParamManager, must be terminated by one whose values are *all* zero. This helps because if we know this, we can check for stuff faster.
};

#endif /* PARAMCOLLECTIONSUMMARY_H_ */
