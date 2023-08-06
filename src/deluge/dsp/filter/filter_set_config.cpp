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

#include "dsp/filter/filter_set_config.h"
#include "io/debug/print.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

HPLadderConfig::HPLadderConfig() = default;
q31_t HPLadderConfig::init(q31_t hpfFrequency, q31_t hpfResonance, bool adjustVolumeForHPFResonance, q31_t filterGain) {
	int32_t extraFeedback = 1200000000;

	int32_t tannedFrequency =
	    instantTan(lshiftAndSaturate<5>(hpfFrequency)); // Between 0 and 8, by my making. 1 represented by 268435456

	int32_t hpfDivideBy1PlusTannedFrequency =
	    (int64_t)2147483648u * 134217728
	    / (134217728 + (tannedFrequency >> 1)); // Between ~0.1 and 1. 1 represented by 2147483648

	int32_t resonanceUpperLimit = 536870911;
	int32_t resonance = ONE_Q31 - (std::min(hpfResonance, resonanceUpperLimit) << 2); // Limits it

	resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
	hpfProcessedResonance =
	    ONE_Q31 - resonance; //ONE_Q31 - rawResonance2; // Always between 0 and 2. 1 represented as 1073741824

	hpfProcessedResonance = std::max(hpfProcessedResonance, (int32_t)134217728); // Set minimum resonance amount

	int32_t hpfProcessedResonanceUnaltered = hpfProcessedResonance;

	// Extra feedback
	hpfProcessedResonance = multiply_32x32_rshift32(hpfProcessedResonance, extraFeedback) << 1;

	hpfDivideByProcessedResonance = 2147483648u / (hpfProcessedResonance >> (23));

	hpfMoveability = multiply_32x32_rshift32_rounded(tannedFrequency, hpfDivideBy1PlusTannedFrequency) << 4;

	int32_t moveabilityTimesProcessedResonance =
	    multiply_32x32_rshift32(hpfProcessedResonanceUnaltered, hpfMoveability); // 1 = 536870912
	int32_t moveabilitySquaredTimesProcessedResonance =
	    multiply_32x32_rshift32(moveabilityTimesProcessedResonance, hpfMoveability); // 1 = 268435456

	hpfHPF3Feedback = -multiply_32x32_rshift32_rounded(hpfMoveability, hpfDivideBy1PlusTannedFrequency);
	hpfLPF1Feedback = hpfDivideBy1PlusTannedFrequency >> 1;

	uint32_t toDivideBy =
	    ((int32_t)268435456 - (moveabilityTimesProcessedResonance >> 1) + moveabilitySquaredTimesProcessedResonance);
	divideByTotalMoveability = (int32_t)((uint64_t)hpfProcessedResonance * 67108864 / toDivideBy);

	hpfDoAntialiasing = (hpfProcessedResonance > 900000000);

	if (adjustVolumeForHPFResonance) {
		// Adjust volume for HPF resonance
		q31_t rawResonance = std::min(hpfResonance, (q31_t)ONE_Q31 >> 2) << 2;
		q31_t squared = multiply_32x32_rshift32(rawResonance, rawResonance) << 1;
		squared = (multiply_32x32_rshift32(squared, squared) >> 4)
		          * 19; // Make bigger to have more of a volume cut happen at high resonance
		filterGain = multiply_32x32_rshift32(filterGain, ONE_Q31 - squared) << 1;
	}

	return filterGain;
}
