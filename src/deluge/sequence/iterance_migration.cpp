/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "sequence/iterance_migration.h"
#include "project/iterance.h"
#include "project/note_probability.h"
#include <cstdint>

using deluge::project::Iterance;

Iterance iteranceFromLegacyProbabilityByte(int32_t probability) {
	// The old preset index (1-based) is encoded as probability - deluge::project::kProbabilityValueCount.
	// deluge::project::kIterancePresets[] opens with FIRST and LAST, so the old index N sits at array position N+1
	// (+2 for FIRST and LAST, -1 for 0-based indexing).
	int32_t idx = probability - deluge::project::kProbabilityValueCount + 1;
	// Defensive guard: idx is 2..36 for any valid C1.2 file, whose presets numbered 35.
	if (idx < 2 || idx >= deluge::project::kIterancePresetCount) {
		return deluge::project::kIteranceOff;
	}
	return deluge::project::kIterancePresets[idx];
}

Iterance iteranceFromLegacyPresetIndex(int32_t presetIndex) {
	// Shift all non-zero indices by +2 to skip the newly prepended FIRST and LAST.
	if (presetIndex == 0) {
		return deluge::project::kIteranceOff;
	}
	int32_t newIndex = presetIndex + 2;
	if (newIndex <= deluge::project::kIterancePresetCount) {
		return deluge::project::kIterancePresets[newIndex - 1];
	}
	if (newIndex == deluge::project::kCustomIterancePreset) {
		return deluge::project::kIteranceCustom;
	}
	return deluge::project::kIteranceOff;
}
