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

#include "dsp/core/phasor.h"
#include <cstdint>

struct ClassicOscillator : SIMDGenerator<int32_t> {
	SIMDPhasor<uint32_t> phasor_; ///< The phasor used to generate the waveform phase

	constexpr ClassicOscillator() = default; ///< Default constructor
	constexpr ClassicOscillator(uint32_t phase, uint32_t step) { setPhase(phase, step); }

	/// @brief Set the phase of the oscillator.
	/// @param phase The new phase value to set.
	constexpr void setPhase(uint32_t phase, uint32_t step) {
		phasor_ = SIMDPhasor<uint32_t>{
		    Argon{phase}.MultiplyAdd(Argon<uint32_t>{1U, 2U, 3U, 4U}, step),
		    step * 4,
		};
	};
};

class SimpleOscillatorFor : public ClassicOscillator {
	using FunctionType = Argon<q31_t> (*)(Argon<uint32_t>);
	FunctionType func_;

public:
	SimpleOscillatorFor(FunctionType func, uint32_t phase, uint32_t step)
	    : ClassicOscillator(phase, step), func_(func) {}
	Argon<q31_t> render() override {
		Argon<q31_t> output = func_(phasor_.phase);
		phasor_.phase = phasor_.render();
		return output;
	}
};
