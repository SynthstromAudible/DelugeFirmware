/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams
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
#include "storage/wave_table/wave_table.h"
#include <cstdint>
#include <span>
namespace deluge::dsp {

template <typename T>
struct PhasorPair {
	T phase;
	T phase_increment;
};

class Oscillator {
public:
	static uint32_t renderOsc(OscType type, bool do_osc_sync, std::span<int32_t> buffer, PhasorPair<uint32_t> osc,
	                          uint32_t pulse_width, bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
	                          PhasorPair<uint32_t> resetter, uint32_t retrigger_phase, int32_t wave_index_increment,
	                          int source_wave_index_last_time, WaveTable* wave_table);

	// Non-synced rendering
	static void renderSine(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                       PhasorPair<FixedPoint<30>> amplitude);
	static void renderTriangle(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                           PhasorPair<FixedPoint<30>> amplitude);
	static void renderSquare(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                         PhasorPair<FixedPoint<30>> amplitude);
	static void renderPWM(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
	                      bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude);
	static void renderSaw(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                      PhasorPair<FixedPoint<30>> amplitude);
	static void renderAnalogSaw2(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                             PhasorPair<FixedPoint<30>> amplitude);
	static void renderAnalogSquare(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                               PhasorPair<FixedPoint<30>> amplitude);
	static void renderAnalogPWM(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
	                            bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude, uint32_t retrigger_phase);
	// Synced rendering
	static uint32_t renderSineSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                               PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
	                               uint32_t retrigger_phase);
	static uint32_t renderTriangleSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                                   PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
	                                   uint32_t retrigger_phase);
	static uint32_t renderSquareSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                                 PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
	                                 uint32_t retrigger_phase);
	static uint32_t renderPWMSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
	                              bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
	                              PhasorPair<uint32_t> resetter, uint32_t retrigger_phase);
	static uint32_t renderSawSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                              PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
	                              uint32_t retrigger_phase);
	static uint32_t renderAnalogSaw2Sync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                                     PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
	                                     uint32_t retrigger_phase);
	static uint32_t renderAnalogSquareSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                                       PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
	                                       uint32_t retrigger_phase);

	static uint32_t renderAnalogPWMSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
	                                    bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
	                                    PhasorPair<uint32_t> resetter, uint32_t retrigger_phase);

	static uint32_t renderWavetable(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
	                                PhasorPair<FixedPoint<30>> amplitude, bool do_osc_sync,
	                                PhasorPair<uint32_t> resetter, uint32_t retrigger_phase,
	                                int32_t wave_index_increment, int source_wave_index_last_time,
	                                WaveTable* wave_table);
};

} // namespace deluge::dsp
