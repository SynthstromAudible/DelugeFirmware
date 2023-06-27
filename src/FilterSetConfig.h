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

#ifndef FILTERSETCONFIG_H_
#define FILTERSETCONFIG_H_

#include "definitions.h"

class FilterSetConfig {

public:
	FilterSetConfig();
	int32_t init(int32_t lpfFrequency, int32_t lpfResonance, int32_t hpfFrequency, int32_t hpfResonance,
	             uint8_t lpfMode, int32_t filterGain, bool adjustVolumeForHPFResonance = true,
	             int32_t* overallOscAmplitude = NULL);

	int32_t processedResonance;                            // 1 represented as 1073741824
	int32_t divideByTotalMoveabilityAndProcessedResonance; // 1 represented as 1073741824

	//moveability is tan(f)/(1+tan(f))
	int32_t moveability;                  // 1 represented by 2147483648
	int32_t divideBy1PlusTannedFrequency; // 1 represented by 2147483648

	// All feedbacks have 1 represented as 1073741824
	int32_t lpf1Feedback;
	int32_t lpf2Feedback;
	int32_t lpf3Feedback;

	int32_t hpfMoveability; // 1 represented by 2147483648

	// All feedbacks have 1 represented as 1073741824
	int32_t hpfLPF1Feedback;
	int32_t hpfHPF3Feedback;

	int32_t hpfProcessedResonance; // 1 represented as 1073741824
	bool hpfDoAntialiasing;
	int32_t hpfDivideByProcessedResonance;

	int32_t divideByTotalMoveability; // 1 represented as 268435456

	int32_t lpfRawResonance;
	int32_t alteredHpfMomentumMultiplier;
	int32_t thisHpfResonance;
	bool doLPF;
	bool doHPF;

	bool doOversampling;
};

#endif /* FILTERSETCONFIG_H_ */
