// Allpass filter declaration
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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
#include "util/functions.h"
#include <cstdint>
#include <limits>
#include <span>

namespace freeverb {
class Allpass {
public:
	Allpass() = default;

	constexpr void setBuffer(std::span<int32_t> buffer) { buffer_ = buffer; }

	constexpr void mute() { std::fill(buffer_.begin(), buffer_.end(), 0); }

	constexpr void setFeedback(float val) { feedback_ = val * std::numeric_limits<int32_t>::max(); }

	[[nodiscard]] constexpr float getFeedback() const { return (float)feedback_ / std::numeric_limits<int32_t>::max(); }

	[[gnu::always_inline]] constexpr int32_t process(int32_t input) {
		int32_t bufout = buffer_[bufidx_];
		int32_t output = -input + bufout;

		buffer_[bufidx_] = input + (bufout >> 1); // Shortcut - because feedback was always one half by default anyway
		// buffer[bufidx] = input + (multiply_32x32_rshift32_rounded(bufout, feedback) << 1);

		if (++bufidx_ >= buffer_.size()) {
			bufidx_ = 0;
		}

		return output;
	}

private:
	int32_t feedback_;
	std::span<int32_t> buffer_;
	int32_t bufidx_{0};
};
} // namespace freeverb
