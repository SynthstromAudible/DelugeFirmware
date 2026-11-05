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

#include "dsp-next/fixed/pitch_shift/input_history.h"
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/input_channel.h"

#include <cstdint>

namespace deluge::synth {

/// @brief One input channel's live history, shared by every voice reading it: written once per audio block.
/// @details The host pools one per channel (@ref AudioInputPort::mono_input, @ref AudioInputPort::stereo_input) and
/// lets it go once a block passes without it being read. The first voice to read it in a block ingests the block's
/// frames from the codec ring; the others find them written. A block that does not follow the last one ingested
/// starts the history over.
/// @tparam Frame dsp::Q<31> for the left or right channel, dsp::StereoSample for both.
template <typename Frame>
class LiveInput {
public:
	using History = dsp::fixed::pitch_shift::InputHistory<Frame>;

	/// @brief The input of @p channel: LEFT or RIGHT for a mono input, STEREO for a stereo one.
	explicit LiveInput(InputChannel channel) : channel_(channel) {}

	/// @brief Writes this block's @p n frames from @p port's ring into the history, once per block.
	/// @param n The block's frames, at least 1.
	void ingest(int32_t n, AudioInputPort& port);

	[[nodiscard]] const History& history() const { return history_; }
	[[nodiscard]] InputChannel channel() const { return channel_; }
	/// @brief The frame clock the last block ingested ended at: the pool's test for an input no voice reads.
	[[nodiscard]] uint32_t ingested_to() const { return ingested_to_; }

private:
	History history_{};
	uint32_t ingested_to_ = 0;
	InputChannel channel_;
};

extern template class LiveInput<dsp::Q<31>>;
extern template class LiveInput<dsp::StereoSample>;

} // namespace deluge::synth
