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
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/bandlimited/table.h"
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
	Phasor phasor_;

public:
	TableOscillator(Table table, uint32_t phase, uint32_t increment)
	    : samples_{table.samples.data()}, magnitude_{table.magnitude}, phasor_{phase, increment} {}

	/// Jump the phase (e.g. a hard-sync reset between windows). See @ref deluge::dsp::Phasor::seek.
	void seek(uint32_t phase) { phasor_.seek(phase); }

	[[gnu::always_inline]] Argon<q31> render() {
		// Per lane: the high bits of the phase pick a table entry, the next bits are the
		// interpolation fraction. `value_pair` packs {entry, next entry} into one 32-bit word.
		Argon<uint32_t> value_pair = 0U;
		ArgonHalf<uint16_t> fraction = 0U;

		util::constexpr_for<0, 4, 1>([&]<int i>() {
			uint32_t phase = phasor_.advance();
			fraction[i] = phase >> (32 - 16 - magnitude_);
			uint32_t index = phase >> (32 - magnitude_);
			// Read the {entry, next-entry} interpolation pair as one word (the table is flat
			// int16, so `index << 1` is the byte offset of entry `index`).
			value_pair[i].Load(reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(samples_) + (index << 1)));
		});

		// Linear interpolation, q31 high half: entry + (next - entry) * fraction.
		fraction = fraction >> 1;
		ArgonHalf<int16_t> entry = value_pair.Narrow().As<int16_t>();
		ArgonHalf<int16_t> next_entry = value_pair.ShiftRightNarrow<16>().As<int16_t>();
		Argon<int32_t> entry_wide = entry.ShiftLeftLong<16>();
		ArgonHalf<int16_t> slope = next_entry - entry;
		return entry_wide.MultiplyDoubleAddSaturateLong(slope, fraction.As<int16_t>());
	}
};

} // namespace deluge::dsp::fixed
