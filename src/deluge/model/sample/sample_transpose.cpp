/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "model/sample/sample_transpose.h"

#include <cmath>

namespace deluge::sample {

SampleTranspose transposeForNote(float midiNote, bool minimizeOctaves, bool rangeCoversJustOneNote,
                                 int32_t thatOneNote) {
	float semitones = (float)60 - midiNote;
	int32_t semitonesInt = roundf(semitones);
	int32_t cents = roundf((semitones - semitonesInt) * 100);

	// If it's the only range, minimize the transpose
	if (minimizeOctaves) {
		while (semitonesInt <= -6) {
			semitonesInt += 12;
		}
		while (semitonesInt > 6) {
			semitonesInt -= 12;
		}
	}

	else if (rangeCoversJustOneNote) {
		if (semitonesInt == 60 - thatOneNote) {
			if (cents >= -4 && cents <= 4) {
				cents = 0;
			}
		}
	}

	return {.semitones = semitonesInt, .cents = cents};
}

} // namespace deluge::sample
