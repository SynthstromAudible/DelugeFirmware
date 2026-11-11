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

#include <cstddef>
#include <cstdint>

namespace deluge::audio {

/// @brief The render's measured cost per frame, smoothed, read as the time one 128-frame block would take.
///
/// The CPU cull and direness decide on this. It is per frame, so it reads the same whatever window sizes the board
/// renders and however many renders one of its ticks holds; its thresholds keep their meaning in samples of real time
/// to render a full 128-frame block (80 of 128 is 62.5% of the CPU).
class RenderLoad {
public:
	/// @brief The block the reading is scaled to, in frames.
	static constexpr std::size_t kBlockFrames = 128;
	/// @brief The output rate, in frames per second.
	static constexpr double kSampleRate = 44100.0;

	/// @brief Notes one render: it took @p seconds for @p frames. Moves the reading halfway to it; a render of no
	/// frames is ignored.
	void add(double seconds, std::size_t frames) {
		if (frames == 0) {
			return;
		}
		const double perFrame = seconds / static_cast<double>(frames);
		secondsPerFrame_ = started_ ? (secondsPerFrame_ + perFrame) / 2 : perFrame;
		started_ = true;
	}

	/// @brief The time a 128-frame block takes at the smoothed cost, in samples of real time, rounded to nearest.
	[[nodiscard]] int32_t block_samples() const {
		return static_cast<int32_t>(secondsPerFrame_ * static_cast<double>(kBlockFrames) * kSampleRate + 0.5);
	}

private:
	double secondsPerFrame_ = 0;
	bool started_ = false;
};

} // namespace deluge::audio
