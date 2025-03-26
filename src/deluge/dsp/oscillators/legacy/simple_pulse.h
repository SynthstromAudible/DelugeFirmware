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

#include "oscillator.h"
#include <arm_neon.h>
#include <limits>

namespace deluge::dsp::oscillator {
struct SimplePulseOscillator : PWMOscillator, LegacyOscillator {
	SimplePulseOscillator() = default;

	Argon<q31_t> render() override {
		Argon<q31_t> output =
		    argon::ternary(getPhase() < getPulseWidth(), Argon<int32_t>{std::numeric_limits<int32_t>::max()},
		                   Argon<int32_t>{std::numeric_limits<int32_t>::min()});
		this->advance();
		return output;
	}
};
} // namespace deluge::dsp::oscillator
