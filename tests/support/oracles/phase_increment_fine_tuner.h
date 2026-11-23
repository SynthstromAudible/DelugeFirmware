/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "dsp/core/fixed_point.h"
#include "dsp/fixed/interpolation/table_interpolation.h"
#include "util/lookuptables/lookuptables.h"

#include <algorithm>
#include <cstdint>

namespace deluge::test::oracle {

/// @brief The app's fine tuner as it stood before `dsp::fixed::FineTune` replaced it (`PhaseIncrementFineTuner`):
/// the oracle the replacement and the unison layout are held to, bit for bit.
class PhaseIncrementFineTuner {
public:
	PhaseIncrementFineTuner() { setNoDetune(); }

	void setup(int32_t detuneScaled) {
		multiplier = deluge::dsp::fixed::kLinearTableInterpolator.interpolate(2147483648u + detuneScaled, 32,
		                                                                      centAdjustTableSmall);
	}
	void setup_cents(int32_t cents) { setup(cents_step(cents)); }
	[[nodiscard]] static int32_t cents_step(int32_t cents) {
		constexpr int32_t kMaxCents = 50;
		return std::clamp(cents, -kMaxCents, kMaxCents) * 42949672;
	}
	[[nodiscard]] int32_t detune(int32_t phaseIncrement) const {
		return mul_as_wrap<deluge::dsp::Q<31>>(deluge::dsp::Q<31>::from_raw(phaseIncrement),
		                                       deluge::dsp::Q<30>::from_raw(multiplier))
		    .raw();
	}
	void setNoDetune() { multiplier = 1073741824; }

	bool operator==(const PhaseIncrementFineTuner&) const = default;

private:
	int32_t multiplier{};
};

} // namespace deluge::test::oracle
