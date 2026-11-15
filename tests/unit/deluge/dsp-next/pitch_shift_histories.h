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

/// @file
/// @brief Input histories the pitch shifter's hop specs search: a workload signal written frame by frame.

#pragma once

#include "pitch_shift_workloads.h"

#include "dsp-next/fixed/pitch_shift/input_history.h"

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace pitch_shift_histories {

/// @brief A history of @p frames of @p signal, on the heap (a stereo one is 64 KB).
template <typename Frame>
std::unique_ptr<deluge::dsp::fixed::pitch_shift::InputHistory<Frame>> history_of(pitch_shift_workloads::Signal signal,
                                                                                 uint32_t frames) {
	auto h = std::make_unique<deluge::dsp::fixed::pitch_shift::InputHistory<Frame>>();
	std::vector<Frame> block;
	block.reserve(128);
	for (uint32_t n = 0; n < frames; ++n) {
		const auto f = pitch_shift_workloads::input_frame(signal, n);
		if constexpr (std::is_same_v<Frame, deluge::dsp::StereoSample>) {
			block.push_back({.l = deluge::dsp::Q<31>::from_raw(f[0]), .r = deluge::dsp::Q<31>::from_raw(f[1])});
		}
		else {
			block.push_back(deluge::dsp::Q<31>::from_raw(f[0]));
		}
		if (block.size() == 128 || n + 1 == frames) {
			h->write(block);
			block.clear();
		}
	}
	return h;
}

/// @brief The signals and lengths the hop specs search: a ring over several laps of each signal, and one under a lap.
struct HistoryCase {
	pitch_shift_workloads::Signal signal;
	uint32_t frames;
};
inline const std::vector<HistoryCase>& history_cases() {
	using pitch_shift_workloads::Signal;
	static const std::vector<HistoryCase> cases{
	    {Signal::PERCUSSIVE, 40000},      {Signal::SINE, 30000}, {Signal::SAW, 20000}, {Signal::PERCUSSIVE, 5000},
	    {Signal::SILENCE_THEN_SAW, 9600},
	};
	return cases;
}

} // namespace pitch_shift_histories
