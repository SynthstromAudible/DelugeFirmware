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
#include "dsp/reverb/freeverb/delay_lines.hpp"
#include "dsp/reverb/freeverb/tuning.h"
#include <cstdint>
#include <span>

namespace freeverb {
// All the allpass filters: numallpasses in series for the left channel, then numallpasses for the right
class Allpasses {
public:
	Allpasses() = default;

	// For allpass i; the last maxblockframes words of the buffer are spare
	void setBuffer(size_t i, std::span<int32_t> buffer) { lines_.setBuffer(i, buffer); }

	void mute() { lines_.mute(); }

	// One frame, in place
	[[gnu::always_inline]] void process(int32_t& l, int32_t& r) {
		for (size_t i = 0; i < numallpasses; i++) {
			l = process(i, l);
			r = process(numallpasses + i, r);
		}
		lines_.advance();
	}

	// Exactly the same as process() for each frame of a block (a multiple of four frames long, at most
	// maxblockframes), in place
	void processBlock(int32_t* samples_l, int32_t* samples_r, size_t numFrames);

private:
	[[gnu::always_inline]] int32_t process(size_t i, int32_t input) {
		int32_t bufout = lines_[i];
		int32_t output = -input + bufout;

		lines_[i] = input + (bufout >> 1); // Shortcut - because feedback was always one half by default anyway
		// buffer[bufidx] = input + (multiply_32x32_rshift32_rounded(bufout, feedback) << 1);

		return output;
	}

	DelayLines<2 * numallpasses> lines_;
};
} // namespace freeverb
