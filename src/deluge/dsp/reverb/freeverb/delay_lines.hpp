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

#include "dsp/reverb/freeverb/tuning.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace freeverb {

// N delay lines (circular buffers) of different lengths, which always step along together, either one frame at a time
// or a block at a time. A block is done one line at a time, straight through, so a line that wraps round during the
// block runs on into the overhang after the end of its buffer: that first gets a copy of the start of the buffer, and
// is copied back there afterwards. The lines are all longer than a block, so the start is up to date when it's copied.
template <size_t N>
struct DelayLines {
	// The last maxblockframes words of the buffer are the overhang
	void setBuffer(size_t i, std::span<int32_t> buffer) {
		begin[i] = buffer.data();
		end[i] = buffer.data() + buffer.size() - maxblockframes;
		pos[i] = begin[i];
		updateFramesToWrap();
	}

	void mute() {
		for (size_t i = 0; i < N; i++) {
			std::fill(begin[i], end[i], 0);
		}
	}

	// Each line's sample from this frame, to be read and then replaced
	[[gnu::always_inline]] int32_t& operator[](size_t i) { return *pos[i]; }

	[[gnu::always_inline]] void advance() {
		for (size_t i = 0; i < N; i++) {
			pos[i]++;
		}
		if (--framesToWrap == 0) {
			wrap();
		}
	}

	// Before processing a block of numFrames (at most maxblockframes) straight through from pos
	void startBlock(size_t numFrames) {
		if (numFrames >= framesToWrap) {
			for (size_t i = 0; i < N; i++) {
				if (pos[i] + numFrames > end[i]) {
					std::copy(begin[i], begin[i] + (pos[i] + numFrames - end[i]), end[i]);
				}
			}
		}
	}

	// After processing the block, with pos moved on by numFrames
	void endBlock(size_t numFrames) {
		if (numFrames >= framesToWrap) {
			wrap();
		}
		else {
			framesToWrap -= numFrames;
		}
	}

	void wrap() {
		for (size_t i = 0; i < N; i++) {
			if (pos[i] >= end[i]) {
				std::copy(end[i], pos[i], begin[i]);
				pos[i] = begin[i] + (pos[i] - end[i]);
			}
		}
		updateFramesToWrap();
	}

	void updateFramesToWrap() {
		framesToWrap = end[0] - pos[0];
		for (size_t i = 1; i < N; i++) {
			framesToWrap = std::min<size_t>(framesToWrap, end[i] - pos[i]);
		}
	}

	std::array<int32_t*, N> pos;
	std::array<int32_t*, N> begin;
	std::array<int32_t*, N> end;
	size_t framesToWrap; // until the first of the lines gets to its end, so always at least 1
};

} // namespace freeverb
