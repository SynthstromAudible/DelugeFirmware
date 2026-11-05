/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/pitch_shift/hop_scheduler.h"
#include "dsp-next/fixed/pitch_shift/play_head.h"
#include "synth/sources/input/input_channel.h"

#include <cstdint>
#include <span>
#include <type_traits>

namespace deluge::synth {

class AudioInputPort;

/// @brief One voice's live pitch shifter over a mono (@p Frame dsp::Q<31>) or stereo (dsp::StereoSample) input.
/// @details Two play heads over the channel's live history, cross-faded at each hop: the @ref
/// dsp::fixed::pitch_shift::HopScheduler cuts each block into segments at its hops and gives each head's amplitude
/// across a segment, and each head renders the segment in place as a @ref dsp::fixed::pitch_shift::PlayHead kernel.
template <typename Frame>
class LivePitchShifter {
public:
	using Kernel = dsp::fixed::pitch_shift::PlayHead<Frame>;
	using History = dsp::fixed::pitch_shift::InputHistory<Frame>;

	/// @brief A shifter for @p channel's input, starting at @p phase_increment.
	LivePitchShifter(InputChannel channel, int32_t phase_increment);

	/// @brief Adds @p num_samples of the pitch-shifted input into @p output_buffer (interleaved when stereo).
	/// @details The channel's live input is ingested first, once per block whichever voice reads it. If the host has
	/// let the input go, nothing is rendered.
	/// @return The hops made: the host folds them into its per-routine CPU-load accounting.
	[[nodiscard]] int32_t render(dsp::Q<31>* output_buffer, int32_t num_samples, int32_t phase_increment,
	                             dsp::Q<31> amplitude, dsp::Q<31> amplitude_increment,
	                             int32_t interpolation_buffer_size, AudioInputPort& port, dsp::fixed::Cong& noise);

	/// @brief Whether the shifter can go without a click: no crossfade running and the newer head reading direct.
	[[nodiscard]] bool may_be_removed_without_click() const;

private:
	using Scheduler = dsp::fixed::pitch_shift::HopScheduler<Frame>;

	[[nodiscard]] static std::span<Frame> frames_of(dsp::Q<31>* output_buffer, int32_t num_samples);
	void configure(dsp::Mono<Kernel>& head, const History& history, int32_t phase_increment, int32_t which_kernel,
	               int32_t interpolation_buffer_size);

	InputChannel channel_;
	Scheduler scheduler_;
	dsp::fixed::pitch_shift::PlayHeads<Frame> heads_{};
	bool placed_ = false; ///< The newer head has been placed at the input's first block.
};

extern template class LivePitchShifter<dsp::Q<31>>;
extern template class LivePitchShifter<dsp::StereoSample>;

/// @brief A voice's live pitch shifter, mono or stereo as its input channel: one allocation, made when the voice's
/// pitch leaves the input's own.
/// @details A handle: ownership is explicit, as the pooled sources it lives in are reused rather than destroyed, so the
/// owner calls @ref destroy and nothing frees the shifter behind it.
class PitchShifter {
public:
	/// @brief Makes the shifter for @p channel at @p phase_increment. @return false if the allocator had no room.
	[[nodiscard]] bool create(InputChannel channel, int32_t phase_increment);
	/// @brief Frees the shifter, if there is one.
	void destroy();
	[[nodiscard]] bool exists() const { return block_ != nullptr; }
	/// @brief Whether the shifter's input is stereo, so its output is interleaved frames.
	[[nodiscard]] bool stereo() const { return stereo_; }

	/// @copydoc LivePitchShifter::render
	[[nodiscard]] int32_t render(dsp::Q<31>* output_buffer, int32_t num_samples, int32_t phase_increment,
	                             dsp::Q<31> amplitude, dsp::Q<31> amplitude_increment,
	                             int32_t interpolation_buffer_size, AudioInputPort& port, dsp::fixed::Cong& noise);
	[[nodiscard]] bool may_be_removed_without_click() const;

private:
	void* block_ = nullptr; ///< The allocator's block holding one instantiation.
	bool stereo_ = false;
};

} // namespace deluge::synth
