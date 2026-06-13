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
#include "dsp-next/fixed/resetter.h" // resetter_reciprocal
#include "storage/wave_table/wave_table.h"
#include <cstdint>

namespace deluge::dsp {

/// @brief A dsp-next block generator that adapts the (sample-backed) WaveTable engine.
///
/// WaveTable loads its band data from the SD card, so — unlike the analytic / band-limited
/// table oscillators — it depends on `storage/` and cannot live in the freestanding dsp-next
/// library. It lives app-side instead and implements the dsp-next @ref BlockGenerator interface,
/// which is exactly the block-level virtual boundary intended for sample-backed / foreign
/// engines: the oscillator registry can hold it behind a `BlockGenerator<q31>*` alongside the
/// in-library oscillators. (Later it moves behind the planned SampleSource port.)
///
/// renderBlock is a thin pass-through to WaveTable::render — same arguments, so it is bit-exact
/// with the former direct call. The hard-sync reset reciprocal is derived from the resetter
/// (matching the dsp-next sync components) rather than passed in. Phase is threaded through the
/// owned Phasor; phase() reads it back for the voice.
class WavetableOscillator : public BlockGenerator<q31> {
	WaveTable* table_;
	Phasor phasor_;
	Phasor resetter_;
	bool sync_;
	uint32_t retrigger_phase_;
	int32_t wave_index_;
	int32_t wave_index_increment_;

public:
	WavetableOscillator(WaveTable* table, Phasor phasor, bool sync, Phasor resetter, uint32_t retrigger_phase,
	                    int32_t wave_index, int32_t wave_index_increment)
	    : table_{table}, phasor_{phasor}, resetter_{resetter}, sync_{sync}, retrigger_phase_{retrigger_phase},
	      wave_index_{wave_index}, wave_index_increment_{wave_index_increment} {}

	/// The phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(fixed::Buffer output) override {
		phasor_.seek(table_->render(output.data(), static_cast<int32_t>(output.size()), phasor_.increment(),
		                            phasor_.phase(), sync_, resetter_.phase(), resetter_.increment(),
		                            fixed::resetter_reciprocal(resetter_.increment()), retrigger_phase_, wave_index_,
		                            wave_index_increment_));
	}
};

} // namespace deluge::dsp
