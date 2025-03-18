
/**
 * Copyright (c) 2025 Katherine Whitlock
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

#include "dsp/core/generator.h"
#include "util/fixedpoint.h"
#include <argon.hpp>
#include <cstdint>
#include <limits>
#include <span>

/// @brief A base class for oscillators that use a lookup table for waveform generation.
/// @details This class provides a mechanism to generate waveforms using a pre-defined table of values.
/// It does linear interpolation between table values for smoother output.
class TableOscillator : public SIMDGenerator<q31_t> {
protected:
	const int16_t* table_;
	int32_t table_size_magnitude_;

	Argon<uint32_t> phase_ = 0;
	Argon<uint32_t> phase_increment_ = 0;

public:
	virtual ~TableOscillator() = default;
	TableOscillator(const int16_t* table, int32_t table_size_magnitude)
	    : table_(table), table_size_magnitude_(table_size_magnitude) {}

	void setPhase(uint32_t phase, uint32_t phase_increment) {
		phase_ = Argon{phase}.MultiplyAdd(Argon<uint32_t>{1U, 2U, 3U, 4U}, phase_increment);
		phase_increment_ = phase_increment * 4;
	}

	Argon<q31_t> process() override {
		Argon<uint32_t> indices = phase_ >> (32 - table_size_magnitude_);

		ArgonHalf<int16_t> fractional = (indices.ShiftRightNarrow<16>() >> 1).As<int16_t>();
		auto [value1, value2] = ArgonHalf<int16_t>::LoadGatherInterleaved<2>(table_, indices);

		// this is a standard linear interpolation of a + (b - a) * fractional
		Argon<q31_t> output = value1.ShiftLeftLong<16>().MultiplyDoubleAddSaturateLong(value2 - value1, fractional);

		phase_ = phase_ + phase_increment_; // advance the phase vector by 4 samples

		return output;
	}
};

class PWMTableOscillator final : public TableOscillator {
	uint32_t phase_to_add_ = 0; // used for pulse width modulation
public:
	PWMTableOscillator(const int16_t* table, int32_t table_size_magnitude)
	    : TableOscillator(table, table_size_magnitude) {}

	void setPhaseToAdd(uint32_t phase_to_add) { phase_to_add_ = phase_to_add; }

	Argon<q31_t> process() override {
		Argon<uint32_t> phase_later = phase_ + phase_to_add_;
		Argon<uint32_t> indices_a = phase_ >> (32 - table_size_magnitude_);
		ArgonHalf<int16_t> rshifted_a =
		    indices_a.ShiftRightNarrow<16>().BitwiseAnd(std::numeric_limits<int16_t>::max()).As<int16_t>();
		auto [value_a1, value_a2] = ArgonHalf<int16_t>::LoadGatherInterleaved<2>(table_, indices_a);

		Argon<uint32_t> indices_b = phase_later >> (32 - table_size_magnitude_);
		ArgonHalf<int16_t> rshifted_b =
		    indices_b.ShiftRightNarrow<16>().BitwiseAnd(std::numeric_limits<int16_t>::max()).As<int16_t>();
		auto [value_b1, value_b2] = ArgonHalf<int16_t>::LoadGatherInterleaved<2>(table_, indices_b);

		// Sneakily do this backwards to flip the polarity of the output, which we need to do anyway
		ArgonHalf<int16_t> strength_a1 = rshifted_a | std::numeric_limits<int16_t>::min();
		ArgonHalf<int16_t> strength_a2 = std::numeric_limits<int16_t>::min() - strength_a1;

		Argon<int32_t> output_a = strength_a2                               //<
		                              .MultiplyDoubleSaturateLong(value_a2) //<
		                              .MultiplyDoubleAddSaturateLong(strength_a1, value_a1);

		ArgonHalf<int16_t> strength_b2 = rshifted_b & std::numeric_limits<int16_t>::max();
		ArgonHalf<int16_t> strength_b1 = std::numeric_limits<int16_t>::max() - strength_b2;

		Argon<int32_t> output_b = strength_b2                               //<
		                              .MultiplyDoubleSaturateLong(value_b2) //<
		                              .MultiplyDoubleAddSaturateLong(strength_b1, value_b1);

		Argon<q31_t> output = output_a.MultiplyRoundFixedPoint(output_b) << 1; // (a *. b) << 1 (average?)
		phase_ = phase_ + (phase_increment_ * 4);                              // advance the phase vector by 4 samples
		return output;
	}
};
