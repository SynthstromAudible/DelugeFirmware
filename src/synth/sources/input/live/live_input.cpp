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
#include "synth/sources/input/live/live_input.h"
#include "board_config.h" // NUM_MONO_INPUT_CHANNELS, SSI_RX_BUFFER_NUM_SAMPLES

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace deluge::synth {

namespace {
/// @brief The codec's sample at @p word: its words are Q31 samples.
[[gnu::always_inline]] inline dsp::Q<31> codec_sample(const int32_t* word) {
	return dsp::Q<31>::from_raw(*word); // A codec word
}

/// @brief The frame of @p channel in the codec's interleaved frame at @p frame.
template <typename Frame>
[[gnu::always_inline]] inline Frame frame_at(const int32_t* frame, InputChannel channel) {
	if constexpr (std::is_same_v<Frame, dsp::StereoSample>) {
		return {.l = codec_sample(frame), .r = codec_sample(frame + 1)};
	}
	else {
		return codec_sample(channel == InputChannel::RIGHT ? frame + 1 : frame);
	}
}

/// Frames ingested at once, through a block on the stack: the render's largest block.
constexpr std::size_t kIngestChunk = 128;
} // namespace

template <typename Frame>
void LiveInput<Frame>::ingest(int32_t n, AudioInputPort& port) {
	const uint32_t now = port.sample_time();
	const uint32_t end = now + static_cast<uint32_t>(n);
	if (ingested_to_ == end) {
		return; // Another voice ingested this block
	}
	if (ingested_to_ != now) {
		history_.restart(); // Frames were missed
	}

	const int32_t* read_pos = port.ring_read_pos();
	const int32_t* const ring_end = port.ring_end();
	std::array<Frame, kIngestChunk> block{};
	auto remaining = static_cast<std::size_t>(n);
	while (remaining != 0) {
		const std::size_t count = std::min(remaining, kIngestChunk);
		for (std::size_t i = 0; i < count; ++i) {
			block[i] = frame_at<Frame>(read_pos, channel_);
			read_pos += NUM_MONO_INPUT_CHANNELS;
			if (read_pos >= ring_end) {
				read_pos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
		}
		history_.write(std::span<const Frame>{block.data(), count});
		remaining -= count;
	}
	ingested_to_ = end;
}

template class LiveInput<dsp::Q<31>>;
template class LiveInput<dsp::StereoSample>;

} // namespace deluge::synth
