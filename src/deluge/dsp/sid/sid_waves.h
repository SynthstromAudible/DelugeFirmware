/*
 * Copyright © 2024 The Deluge Community
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

#include "dsp/sid/arm_intrinsics.h"
#include <array>
#include <tuple>

namespace deluge {
namespace dsp {
namespace sid {

// SID models
enum class SidModel { MOS6581, MOS8580 };

// These tables will be initialized in .cpp file
extern std::array<int16_t, 4096> sidTriangleTable;
extern std::array<int16_t, 4096> sidSawTable;
extern std::array<int16_t, 4096> sidPulseTable;
extern std::array<int16_t, 4096> sidNoiseTable;

// Initialize SID wave tables - should be called during startup
void initSidWaveTables();

// Get Triangle waveform values (vectorized version)
std::tuple<int32x4_t, uint32_t> generateSidTriangleVector(uint32_t phase, uint32_t phaseIncrement,
                                                          uint32_t phaseOffset = 0);

// Get Sawtooth waveform values (vectorized version)
std::tuple<int32x4_t, uint32_t> generateSidSawVector(uint32_t phase, uint32_t phaseIncrement, uint32_t phaseOffset = 0);

// Get Pulse waveform values (vectorized version)
std::tuple<int32x4_t, uint32_t> generateSidPulseVector(uint32_t phase, uint32_t phaseIncrement, uint32_t pulseWidth);

// Get Noise waveform values (vectorized version)
std::tuple<int32x4_t, uint32_t> generateSidNoiseVector(uint32_t phase, uint32_t phaseIncrement);

// Main render function for SID triangle waveform
void renderSidTriangle(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, uint32_t phaseIncrement,
                       uint32_t& phase, bool applyAmplitude, uint32_t phaseOffset = 0, int32_t amplitudeIncrement = 0);

// Main render function for SID saw waveform
void renderSidSaw(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, uint32_t phaseIncrement, uint32_t& phase,
                  bool applyAmplitude, uint32_t phaseOffset = 0, int32_t amplitudeIncrement = 0);

// Main render function for SID pulse waveform with pulse width control
void renderSidPulse(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, uint32_t phaseIncrement,
                    uint32_t& phase, uint32_t pulseWidth, bool applyAmplitude, int32_t amplitudeIncrement = 0);

// Main render function for SID noise waveform
void renderSidNoise(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, uint32_t phaseIncrement,
                    uint32_t& phase, bool applyAmplitude, int32_t amplitudeIncrement = 0);

} // namespace sid
} // namespace dsp
} // namespace deluge