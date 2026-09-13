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

#pragma once

#include "model/iterance/iterance.h"
#include <cstdint>

// Decodes an iterance value from the old C1.2 / pre-1.3 probability-byte encoding.
//
// Old format (noteHexLength==22 and the pre-lift else path): iterance was stored in
// the probability field as (kNumProbabilityValues + legacyPresetIndex) where
// legacyPresetIndex ran from 1 ("1 of 2") to 35 ("8 of 8").
//
// Commit 3ce3d41d prepended FIRST and LAST to iterancePresets[], shifting every
// subsequent preset's array index by +2.  This function applies that correction.
Iterance iteranceFromLegacyProbabilityByte(int32_t probability);

// Decodes an iterance value from the old early-1.3-nightly preset-index encoding.
//
// Old format (noteHexLength==26): the preset index was stored directly.  In that
// era the index space was: 0=OFF, 1=1of2 … 35=8of8, 36=custom.  After FIRST and
// LAST were prepended the mapping is: 0=OFF, 1=FIRST, 2=LAST, 3=1of2 … 37=8of8,
// 38=custom.  This function applies the +2 correction to non-zero indices.
Iterance iteranceFromLegacyPresetIndex(int32_t presetIndex);
