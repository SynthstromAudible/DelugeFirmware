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

#include "definitions_cxx.hpp"

class SamplePercCacheZone {
public:
	SamplePercCacheZone(int32_t newStartPos);
	void resetEndPos(int32_t newEndPos);

	int32_t startPos;
	int32_t endPos; // This can and will go to -1 when playing reversed and the "end" is reached, because endPos gets
	                // set to the sample-number *after* the last one.

	int32_t samplesAtStartWhichShouldBeReplaced; // Number of samples at the start of this zone which can be regarded as
	                                             // "suss", and we'll wanna overwrite all of those if another zone is
	                                             // gonna connect from behind

	int32_t lastAngle;
	int32_t lastSampleRead;
	int32_t angleLPFMem[kDifferenceLPFPoles];
};
