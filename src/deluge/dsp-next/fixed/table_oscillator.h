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
#pragma once

#include "dsp-next/core/generator.h"
#include "dsp-next/fixed/table.h"
#include "util/misc.h" // util::constexpr_for
#include <argon.hpp>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief A band-limited wavetable oscillator: a stateful generator that advances its own
/// q32 phase and reads a Table with 4-lane linear interpolation. Backs the band-limited
/// saw / square / triangle / analog waves (only the Table differs).
///
/// render() is the exact legacy `waveRenderingFunctionGeneral` math — the same per-lane
/// {value, value+1} read and `value + (next-value)·fraction` interpolation — so it is
/// bit-exact, raw (no amplitude/mix; that is the Amplitude mixer's job). The block fill
/// comes from the Generator mixin (render() per 4-lane chunk). The owned phase is the
/// fill phase; the caller still owns the persistent per-voice phase.
class [[gnu::hot]] TableOscillator : public Generator<TableOscillator, q31> {
	const int16_t* samples_;
	int32_t magnitude_;
	uint32_t phase_;
	uint32_t increment_;

public:
	TableOscillator(Table table, uint32_t phase, uint32_t increment)
	    : samples_{table.samples.data()}, magnitude_{table.magnitude}, phase_{phase}, increment_{increment} {}

	[[gnu::always_inline]] Argon<q31> render() {
		Argon<uint32_t> readValue = 0U;
		ArgonHalf<uint16_t> strength2 = 0U;

		util::constexpr_for<0, 4, 1>([&]<int i>() {
			phase_ += increment_;
			uint32_t rshifted = phase_ >> (32 - 16 - magnitude_);
			strength2[i] = rshifted;
			uint32_t whichValue = phase_ >> (32 - magnitude_);
			// Read the {value, next-value} interpolation pair as one word (the table is flat
			// int16; whichValue << 1 is the byte offset of entry whichValue).
			readValue[i].Load(reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(samples_) + (whichValue << 1)));
		});

		strength2 = strength2 >> 1;
		ArgonHalf<int16_t> value1 = readValue.Narrow().As<int16_t>();
		ArgonHalf<int16_t> value2 = readValue.ShiftRightNarrow<16>().As<int16_t>();
		Argon<int32_t> value1Big = value1.ShiftLeftLong<16>();
		ArgonHalf<int16_t> difference = value2 - value1;
		return value1Big.MultiplyDoubleAddSaturateLong(difference, strength2.As<int16_t>());
	}
};

} // namespace deluge::dsp::fixed
