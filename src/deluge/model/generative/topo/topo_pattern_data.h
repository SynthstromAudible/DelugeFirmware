/*
 * Pattern data for the "Topo Drums" generative drum engine.
 *
 * Derived from the original Mutable Instruments Grids firmware:
 * Copyright 2011/2012 Emilie Gillet (emilie.o.gillet@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>

namespace deluge::model::topo {

constexpr uint8_t kNumBanks = 3;                                          // kick, snare, hi-hat
constexpr uint8_t kPulsesPerStep = 3;                                     // 24 PPQN; 8 steps per quarter note
constexpr uint16_t kStepsPerPattern = 32;                                 // one bar
constexpr uint16_t kPulsesPerPattern = kStepsPerPattern * kPulsesPerStep; // 96

constexpr uint8_t kNumMapNodes = 25;
constexpr uint16_t kNodeSize = kNumBanks * kStepsPerPattern; // 96 bytes

// 25 reference patterns, each 3 banks x 32 steps, values 0-255 (level / velocity).
extern const uint8_t kPatternNodes[kNumMapNodes][kNodeSize];

// Which node sits at each point of the 5x5 topographic map.
extern const uint8_t kDrumMap[5][5];

// Euclidean (Bjorklund) patterns, indexed by (length - 1) * 32 + density.
extern const uint32_t kEuclideanPatterns[1024];

} // namespace deluge::model::topo
