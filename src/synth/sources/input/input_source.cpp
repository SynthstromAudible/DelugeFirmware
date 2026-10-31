/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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
#include "synth/sources/input/input_source.h"
#include "board_config.h" // NUM_MONO_INPUT_CHANNELS, SSI_RX_BUFFER_NUM_SAMPLES
#include "dsp-next/core/q31_ops.h"
#include "memory/memory_allocator_interface.h" // allocMaxSpeed / delugeDealloc (the thin allocator seam)
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_input_buffer.h"
#include "synth/sources/input/live/live_pitch_shifter.h"
#include <cstring>
#include <new>

namespace deluge::synth {
namespace {
/// The input channel a live-input `OscType` names: the note-on boundary is the only place the synth reads it.
InputChannel channel_from_osc_type(OscType input_type) {
	switch (input_type) {
	case OscType::INPUT_R:
		return InputChannel::RIGHT;
	case OscType::INPUT_STEREO:
		return InputChannel::STEREO;
	default:
		return InputChannel::LEFT;
	}
}
} // namespace

void InputSource::prepare_for_note_on(OscType input_type) {
	channel_ = channel_from_osc_type(input_type);
}

// Channel routing, pitch shift and stereo unison, each a branch:
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void InputSource::render(int32_t* osc_buffer, int32_t num_samples, uint32_t phase_increment, int32_t source_amplitude,
                         int32_t amplitude_increment, bool stereo_buffer, bool stereo_unison, int32_t amplitude_l,
                         int32_t amplitude_r, int32_t interpolation_buffer_size, dsp::fixed::Cong& noise,
                         int32_t* mono_scratch) {
	// If pitch shifting and we weren't previously...
	if (phase_increment != kMaxSampleValue) {

		if (live_pitch_shifter_ == nullptr) {

			// A configured-stereo channel folds to the left channel when the host has no stereo input.
			InputChannel channel_now = channel_;
			if (channel_now == InputChannel::STEREO && !in_->stereo_input_available()) {
				channel_now = InputChannel::LEFT;
			}
			LiveInputBuffer* live_input_buffer = in_->buffer_for_channel(channel_now, true);

			if (live_input_buffer != nullptr) {

				void* memory = allocMaxSpeed(sizeof(LivePitchShifter));

				if (memory != nullptr) {
					// Placed in the allocator's block, which unassign frees
					// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
					live_pitch_shifter_ =
					    new (memory) LivePitchShifter(channel_now, static_cast<int32_t>(phase_increment));
				}
			}
		}
	}

	// If not pitch shifting and we were previously...
	else {
		if ((live_pitch_shifter_ != nullptr) && live_pitch_shifter_->may_be_removed_without_click()) {
			live_pitch_shifter_->~LivePitchShifter();
			delugeDealloc(live_pitch_shifter_);
			live_pitch_shifter_ = nullptr;
		}
	}

	// Yes pitch shifting
	if (live_pitch_shifter_ != nullptr) {
		// With unison stereo spread, oscBuffer is an interleaved stereo buffer. A mono input must be rendered
		// to a temporary mono buffer first and then panned across it, otherwise the mono render would write
		// into the interleaved buffer as if it were mono and produce noise. Either way the shifter reports the
		// hops it performed; the port folds them into the host's CPU-load accounting.
		if (stereo_unison && live_pitch_shifter_->num_channels == 1) {
			int32_t* render_buffer = mono_scratch;
			memset(render_buffer, 0, num_samples * sizeof(int32_t));

			in_->report_hops(live_pitch_shifter_->render(render_buffer, num_samples,
			                                             static_cast<int32_t>(phase_increment), source_amplitude,
			                                             amplitude_increment, interpolation_buffer_size, *in_, noise));

			for (int32_t i = 0; i < num_samples; i++) {
				osc_buffer[(i << 1)] += multiply_32x32_rshift32(render_buffer[i], amplitude_l) << 2;
				osc_buffer[(i << 1) + 1] += multiply_32x32_rshift32(render_buffer[i], amplitude_r) << 2;
			}
		}
		else {
			in_->report_hops(live_pitch_shifter_->render(osc_buffer, num_samples, static_cast<int32_t>(phase_increment),
			                                             source_amplitude, amplitude_increment,
			                                             interpolation_buffer_size, *in_, noise));
		}
	}

	// No pitch shifting
	else {

		int32_t* __restrict__ osc_buffer_pos = osc_buffer;
		int32_t const* __restrict__ input_read_pos = in_->ring_read_pos();
		int32_t const* const input_ring_end = in_->ring_end();
		int32_t source_amplitude_this_unison = source_amplitude;

		// Just left, or just right, or if (stereo but there's only the internal, mono mic)
		if (channel_ != InputChannel::STEREO || !in_->stereo_input_available()) {

			int32_t channel_offset = 0;
			// If right, but not internal mic
			if (channel_ == InputChannel::RIGHT && in_->stereo_input_available()) {
				channel_offset = 1;

				// Or if left or using internal mic
			}
			else {
				channel_offset = 0;
			}

			int32_t source_amplitude_now = source_amplitude_this_unison;

			// With unison stereo spread, oscBuffer is an interleaved stereo buffer and each unison part is
			// panned across it.
			if (stereo_unison) {
				int32_t const* const osc_buffer_end = osc_buffer + num_samples * 2;
				do {
					source_amplitude_now += amplitude_increment;

					int32_t sample = multiply_32x32_rshift32(input_read_pos[channel_offset], source_amplitude_now) << 4;
					*(osc_buffer_pos++) += multiply_32x32_rshift32(sample, amplitude_l) << 2;
					*(osc_buffer_pos++) += multiply_32x32_rshift32(sample, amplitude_r) << 2;

					input_read_pos += NUM_MONO_INPUT_CHANNELS;
					if (input_read_pos >= input_ring_end) {
						input_read_pos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
					}
				} while (osc_buffer_pos != osc_buffer_end);
			}
			else {
				int32_t const* const osc_buffer_end = osc_buffer + num_samples;
				do {
					source_amplitude_now += amplitude_increment;

					// Mono / left channel (or stereo condensed to mono)
					*(osc_buffer_pos++) += multiply_32x32_rshift32(input_read_pos[channel_offset], source_amplitude_now)
					                       << 4;

					input_read_pos += NUM_MONO_INPUT_CHANNELS;
					if (input_read_pos >= input_ring_end) {
						input_read_pos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
					}
				} while (osc_buffer_pos != osc_buffer_end);
			}
		}

		// Stereo
		else {

			int32_t num_channels_after_condensing = stereo_buffer ? 2 : 1;

			int32_t const* const osc_buffer_end = osc_buffer + num_samples * num_channels_after_condensing;

			int32_t source_amplitude_now = source_amplitude_this_unison;
			do {
				source_amplitude_now += amplitude_increment;

				int32_t sample_l = input_read_pos[0];
				int32_t sample_r = input_read_pos[1];

				// If condensing to mono, do that now
				if (!stereo_buffer) {
					sample_l = ((sample_l >> 1) + (sample_r >> 1));
				}

				// Mono / left channel (or stereo condensed to mono)
				*(osc_buffer_pos++) += multiply_32x32_rshift32(sample_l, source_amplitude_now) << 4;

				// Right channel
				if (stereo_buffer) {
					*(osc_buffer_pos++) += multiply_32x32_rshift32(sample_r, source_amplitude_now) << 4;
				}

				input_read_pos += NUM_MONO_INPUT_CHANNELS;
				if (input_read_pos >= input_ring_end) {
					input_read_pos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
				}
			} while (osc_buffer_pos != osc_buffer_end);
		}
	}
}

SourceRenderResult InputSource::render_source(const SourceRenderContext& ctx) {
	// Live input never self-unassigns; channel routing follows the channel latched at note-on (see render()).
	render(ctx.osc_buffer, ctx.num_samples, ctx.phase_increment, ctx.source_amplitude, ctx.amplitude_increment,
	       ctx.stereo_buffer, ctx.stereo_unison, ctx.amplitude_l, ctx.amplitude_r, ctx.interpolation_buffer_size,
	       *ctx.noise, ctx.stereo_scratch);
	return deluge::synth::SourceRenderResult::ACTIVE;
}

void InputSource::unassign() {
	if (live_pitch_shifter_ != nullptr) {
		live_pitch_shifter_->~LivePitchShifter();
		delugeDealloc(live_pitch_shifter_);
		live_pitch_shifter_ = nullptr;
	}
}

} // namespace deluge::synth
