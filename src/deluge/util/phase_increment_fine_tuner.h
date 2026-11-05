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

#include <algorithm>
#include <cstdint>
class PhaseIncrementFineTuner {
public:
	PhaseIncrementFineTuner();

	void setup(int32_t detuneScaled);
	/// @brief Sets the detune from @p cents, held to ±50 (see cents_step).
	void setup_cents(int32_t cents) { setup(cents_step(cents)); }
	/// @brief @p cents as a detune step: held to ±50, a quarter tone either way, as cents × 42949672 overflows past it.
	[[nodiscard]] static int32_t cents_step(int32_t cents) {
		constexpr int32_t kMaxCents = 50;
		return std::clamp(cents, -kMaxCents, kMaxCents) * 42949672;
	}
	int32_t detune(int32_t phaseIncrement) const;
	void setNoDetune();

	bool operator==(const PhaseIncrementFineTuner&) const = default;

private:
	int32_t multiplier{};
};
