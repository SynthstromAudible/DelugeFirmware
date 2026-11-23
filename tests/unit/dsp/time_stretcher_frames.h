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

// A frame_source over the time stretcher's records (time_stretcher_records.h): a recorded frame window's words read as
// runs of frames, each word the unmasked read the legacy made for that sample.
#pragma once

#include "dsp/fixed/sample_read/frame_source.h"

#include "time_stretcher_records.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace time_stretcher_frames {

/// @brief The frames of a recorded window, as runs that stop at the window's edge, at every multiple of a split (to
/// stand in for chunk boundaries), and before the first frame that was not resident.
class RecordFrames {
public:
	explicit RecordFrames(const time_stretcher_records::FrameWindow& window, int32_t split_every = INT32_MAX)
	    : window_(window), split_every_(split_every) {}

	[[nodiscard]] std::optional<deluge::dsp::fixed::sample_read::FrameRun>
	run(int32_t frame, deluge::dsp::fixed::sample_read::Direction direction) const {
		using deluge::dsp::fixed::sample_read::Direction;
		const bool forward = direction == Direction::FORWARD;
		if (frame < window_.first_frame || frame > window_.last_frame || !resident(frame, forward)) {
			return std::nullopt;
		}
		int32_t frames = 1;
		for (int32_t f = frame + (forward ? 1 : -1);; f += forward ? 1 : -1) {
			const int32_t crossed = forward ? f : f + 1; // the boundary between f and the frame before it in play order
			if (f < window_.first_frame || f > window_.last_frame || !resident(f, forward)
			    || crossed % split_every_ == 0) {
				break;
			}
			frames++;
		}
		const auto ch = static_cast<int32_t>(window_.channels);
		const std::size_t at = window_.words.offset + static_cast<std::size_t>(frame - window_.first_frame) * ch;
		return deluge::dsp::fixed::sample_read::FrameRun{
		    .first = reinterpret_cast<const std::byte*>(&time_stretcher_records::kWords[at]),
		    .frames = frames,
		    .stride = (forward ? 4 : -4) * ch,
		    .channels = window_.channels,
		    .byte_depth = 4,
		};
	}
	[[nodiscard]] int32_t channels() const { return window_.channels; }
	[[nodiscard]] int32_t length_frames() const { return window_.length_frames; }

private:
	[[nodiscard]] bool resident(int32_t frame, bool forward) const {
		if (window_.resident_until == INT32_MAX) {
			return true;
		}
		return forward ? frame < window_.resident_until : frame > window_.resident_until;
	}

	time_stretcher_records::FrameWindow window_;
	int32_t split_every_;
};

static_assert(deluge::dsp::fixed::sample_read::frame_source<RecordFrames>);

} // namespace time_stretcher_frames
