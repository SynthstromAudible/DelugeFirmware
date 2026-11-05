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

#include "synth/sources/input/live/live_pitch_shifter.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h"
#include "memory/memory_allocator_interface.h" // allocMaxSpeed / delugeDealloc (the thin allocator seam)
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_input.h"

#include <memory>
#include <new>

namespace deluge::synth {

namespace {
/// @brief The pooled live input of @p channel, if the host has it.
template <typename Frame>
[[nodiscard]] LiveInput<Frame>* input_of(AudioInputPort& port, InputChannel channel) {
	if constexpr (std::is_same_v<Frame, dsp::StereoSample>) {
		return port.stereo_input(false);
	}
	else {
		return port.mono_input(channel, false);
	}
}
} // namespace

template <typename Frame>
LivePitchShifter<Frame>::LivePitchShifter(InputChannel channel, int32_t phase_increment)
    : channel_(channel), scheduler_(phase_increment) {
}

template <typename Frame>
int32_t LivePitchShifter<Frame>::render(dsp::Q<31>* output_buffer, int32_t num_samples, int32_t phase_increment,
                                        dsp::Q<31> amplitude, dsp::Q<31> amplitude_increment,
                                        int32_t interpolation_buffer_size, AudioInputPort& port,
                                        dsp::fixed::Cong& noise) {
	LiveInput<Frame>* input = input_of<Frame>(port, channel_);
	if (input == nullptr) {
		return 0; // The host let the input go: nothing to read
	}
	input->ingest(num_samples, port);
	const History& history = input->history();
	const int32_t which_kernel = dsp::fixed::WindowedSincKernel::variant_for(phase_increment);
	configure(heads_.newer, history, phase_increment, which_kernel, interpolation_buffer_size);
	configure(heads_.older, history, phase_increment, which_kernel, interpolation_buffer_size);
	if (!placed_) {
		// The first block: the newer head starts direct at the block's own first frame, so it plays the input as it
		// arrives until the first hop, with no onset entry noted (quirk Q9)
		heads_.newer.template on<&Kernel::begin_hop>(typename Kernel::Hop{
		    .cursor = history.now() - num_samples, .kind = dsp::fixed::pitch_shift::HeadKind::DIRECT});
		placed_ = true;
	}

	scheduler_.begin_block({.length = num_samples,
	                        .phase_increment = phase_increment,
	                        .amplitude = amplitude,
	                        .amplitude_step = amplitude_increment},
	                       history);
	int32_t done = 0;
	while (const auto segment = scheduler_.next(history, heads_, noise)) {
		const std::span<Frame> frames = frames_of(output_buffer + (done * Kernel::kChannels), segment->length);
		// A hop may have changed a head's kind since the block began
		configure(heads_.newer, history, phase_increment, which_kernel, interpolation_buffer_size);
		heads_.newer.template on<&Kernel::start_segment>(segment->newer, segment->newer_step);
		heads_.newer.render_block(frames, frames);
		if (segment->older_sounding) {
			configure(heads_.older, history, phase_increment, which_kernel, interpolation_buffer_size);
			heads_.older.template on<&Kernel::start_segment>(segment->older, segment->older_step);
			heads_.older.render_block(frames, frames);
		}
		done += segment->length;
	}
	return scheduler_.hops();
}

template <typename Frame>
bool LivePitchShifter<Frame>::may_be_removed_without_click() const {
	return !scheduler_.crossfading() && heads_.newer.state().kind == dsp::fixed::pitch_shift::HeadKind::DIRECT;
}

template <typename Frame>
std::span<Frame> LivePitchShifter<Frame>::frames_of(dsp::Q<31>* output_buffer, int32_t num_samples) {
	if constexpr (std::is_same_v<Frame, dsp::StereoSample>) {
		return dsp::as_frames(std::span<dsp::Q<31>>{output_buffer, static_cast<std::size_t>(num_samples) * 2});
	}
	else {
		return {output_buffer, static_cast<std::size_t>(num_samples)};
	}
}

template <typename Frame>
void LivePitchShifter<Frame>::configure(dsp::Mono<Kernel>& head, const History& history, int32_t phase_increment,
                                        int32_t which_kernel, int32_t interpolation_buffer_size) {
	head.configure({.history = &history,
	                .phase_increment = static_cast<uint32_t>(phase_increment),
	                .kernel_variant = which_kernel,
	                .kind = head.state().kind,
	                .linear = interpolation_buffer_size <= 2});
}

template class LivePitchShifter<dsp::Q<31>>;
template class LivePitchShifter<dsp::StereoSample>;

namespace {
using MonoShifter = LivePitchShifter<dsp::Q<31>>;
using StereoShifter = LivePitchShifter<dsp::StereoSample>;
} // namespace

bool PitchShifter::create(InputChannel channel, int32_t phase_increment) {
	destroy();
	stereo_ = channel == InputChannel::STEREO;
	void* memory = allocMaxSpeed(stereo_ ? sizeof(StereoShifter) : sizeof(MonoShifter));
	if (memory == nullptr) {
		return false;
	}
	// Placed in the allocator's block, which destroy frees
	if (stereo_) {
		new (memory) StereoShifter(channel, phase_increment); // NOLINT(cppcoreguidelines-owning-memory)
	}
	else {
		new (memory) MonoShifter(channel, phase_increment); // NOLINT(cppcoreguidelines-owning-memory)
	}
	block_ = memory;
	return true;
}

void PitchShifter::destroy() {
	if (block_ == nullptr) {
		return;
	}
	if (stereo_) {
		std::destroy_at(static_cast<StereoShifter*>(block_));
	}
	else {
		std::destroy_at(static_cast<MonoShifter*>(block_));
	}
	delugeDealloc(block_);
	block_ = nullptr;
}

int32_t PitchShifter::render(dsp::Q<31>* output_buffer, int32_t num_samples, int32_t phase_increment,
                             dsp::Q<31> amplitude, dsp::Q<31> amplitude_increment, int32_t interpolation_buffer_size,
                             AudioInputPort& port, dsp::fixed::Cong& noise) {
	if (stereo_) {
		return static_cast<StereoShifter*>(block_)->render(output_buffer, num_samples, phase_increment, amplitude,
		                                                   amplitude_increment, interpolation_buffer_size, port, noise);
	}
	return static_cast<MonoShifter*>(block_)->render(output_buffer, num_samples, phase_increment, amplitude,
	                                                 amplitude_increment, interpolation_buffer_size, port, noise);
}

bool PitchShifter::may_be_removed_without_click() const {
	if (stereo_) {
		return static_cast<const StereoShifter*>(block_)->may_be_removed_without_click();
	}
	return static_cast<const MonoShifter*>(block_)->may_be_removed_without_click();
}

} // namespace deluge::synth
