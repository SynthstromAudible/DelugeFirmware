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
#include "dsp/waves.hpp"
#include "dsp_ng/components/periodic.hpp"
#include "dsp_ng/core/parallel.hpp"
#include "dsp_ng/core/processor.hpp"
#include <argon.hpp>

namespace deluge::dsp::oscillators {

template <typename WaveFunction>
class ParallelOscillatorFor : public ParallelProcessor<ParallelOscillatorFor<WaveFunction>, float, 4, Argon<float>> {
public:
	using value_type = float;
	using parallel_type = Argon<float>;

	ParallelOscillatorFor(WaveFunction wave_function) : wave_function_{wave_function} {}

	parallel_type render(parallel_type input) final {
		auto output = waves::triangle(periodic_component_.getPhase());
		periodic_component_.advance();
		return output;
	}

	void setFrequency(size_t slot, float frequency) {
		auto phase = periodic_component_.getPhase();
		phase[slot] = frequency * (1.0f / kSampleRate);
		periodic_component_.setPhase(phase);
	}

private:
	Periodic<Argon<float>> periodic_component_{};
	WaveFunction wave_function_;
};
} // namespace deluge::dsp::oscillators
