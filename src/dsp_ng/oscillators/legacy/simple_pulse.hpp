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

#include "dsp_ng/core/types.hpp"
#include "oscillator.hpp"
#include <arm_neon.h>
#include <limits>

namespace deluge::dsp::oscillator {
struct SimplePulseOscillator final : PWMOscillator, LegacyOscillator {
	SimplePulseOscillator() = default;

	Argon<q31_t> render() final {
		auto output = argon::ternary(getPhase() < getPulseWidth(),         // If phase is less than pulse width
		                             std::numeric_limits<int32_t>::max(),  // Output max value
		                             std::numeric_limits<int32_t>::min()); // Output min value
		this->advance();
		return output;
	}
};
} // namespace deluge::dsp::oscillator
