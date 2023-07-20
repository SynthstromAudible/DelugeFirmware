/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "definitions.h"
#include "util/fixedpoint.h"

class FilterSetConfig {

public:
	FilterSetConfig();
	q31_t init(q31_t lpfFrequency, q31_t lpfResonance, q31_t hpfFrequency, q31_t hpfResonance, uint8_t lpfMode,
	           q31_t filterGain, bool adjustVolumeForHPFResonance = true, q31_t* overallOscAmplitude = NULL);

	q31_t processedResonance;                            // 1 represented as 1073741824
	q31_t divideByTotalMoveabilityAndProcessedResonance; // 1 represented as 1073741824

	//moveability is tan(f)/(1+tan(f))
	q31_t moveability;                  // 1 represented by 2147483648
	q31_t divideBy1PlusTannedFrequency; // 1 represented by 2147483648

	// All feedbacks have 1 represented as 1073741824
	q31_t lpf1Feedback;
	q31_t lpf2Feedback;
	q31_t lpf3Feedback;

	q31_t hpfMoveability; // 1 represented by 2147483648

	// All feedbacks have 1 represented as 1073741824
	q31_t hpfLPF1Feedback;
	q31_t hpfHPF3Feedback;

	q31_t hpfProcessedResonance; // 1 represented as 1073741824
	bool hpfDoAntialiasing;
	q31_t hpfDivideByProcessedResonance;

	q31_t divideByTotalMoveability; // 1 represented as 268435456

	q31_t lpfRawResonance;
	q31_t SVFInputScale;
	q31_t alteredHpfMomentumMultiplier;
	q31_t thisHpfResonance;
	bool doLPF;
	bool doHPF;

	bool doOversampling;
};
