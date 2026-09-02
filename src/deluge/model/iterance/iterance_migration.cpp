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

#include "model/iterance/iterance_migration.h"
#include "definitions_cxx.hpp"
#include <array>
#include <cstdint>

// iterancePresets is defined in lookuptables.cpp.
extern const std::array<Iterance, kNumIterancePresets> iterancePresets;

Iterance iteranceFromLegacyProbabilityByte(int32_t probability) {
    // The old preset index (1-based) is encoded as probability - kNumProbabilityValues.
    // FIRST and LAST were prepended to iterancePresets[], so the old index N now sits at
    // array position N+1 (i.e. +2 for the prepend, -1 for 0-based indexing = net +1).
    int32_t idx = probability - kNumProbabilityValues + 1;
    // Defensive guard: idx should be 2..36 for any valid C1.2 file (old kNumIterancePresets=35).
    if (idx < 2 || idx >= kNumIterancePresets) {
        return kDefaultIteranceValue;
    }
    return iterancePresets[idx];
}

Iterance iteranceFromLegacyPresetIndex(int32_t presetIndex) {
    // Shift all non-zero indices by +2 to skip the newly prepended FIRST and LAST.
    if (presetIndex == 0) {
        return kDefaultIteranceValue;
    }
    int32_t newIndex = presetIndex + 2;
    if (newIndex <= kNumIterancePresets) {
        return iterancePresets[newIndex - 1];
    }
    if (newIndex == kCustomIterancePreset) {
        return kCustomIteranceValue;
    }
    return kDefaultIteranceValue;
}
