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
#include "dsp-next/core/fixed_point.h"
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_inputs.h"
#include "synth/sources/input/live/live_pitch_shifter.h"
#include <algorithm> // std::fill_n
#include <tuple>

namespace deluge::synth {

namespace {
/// @brief An input sample times the source amplitude: the high word, ×16 as the input is levelled.
[[gnu::always_inline]] inline dsp::Q<31> levelled(dsp::Q<31> sample, dsp::Q<31> amplitude) {
	// The product at Q31 (×2), then ×8
	return scale_shl_wrap<3>(mul_as_wrap<dsp::Q<31>>(sample, amplitude));
}

/// @brief The input ring's sample at @p word: the codec's words are Q31 samples.
[[gnu::always_inline]] inline dsp::Q<31> input_sample(const int32_t* word) {
	return dsp::Q<31>::from_raw(*word);
}

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

bool InputSource::note_on(const SourceNote& note) {
	channel_ = channel_from_osc_type(note.osc_type);
	return true;
}

// Channel routing, pitch shift and stereo unison, each a branch:
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void InputSource::render_input(dsp::Q<31>* osc_buffer, int32_t num_samples, uint32_t phase_increment,
                               dsp::Q<31> source_amplitude, dsp::Q<31> amplitude_increment, bool stereo_buffer,
                               bool stereo_unison, dsp::Q<30> amplitude_l, dsp::Q<30> amplitude_r,
                               int32_t interpolation_buffer_size, dsp::fixed::Cong& noise, dsp::Q<31>* mono_scratch) {
	// If pitch shifting and we weren't previously...
	if (phase_increment != kMaxSampleValue) {

		if (!live_pitch_shifter_.exists()) {

			// A configured-stereo channel folds to the left channel when the host has no stereo input.
			InputChannel channel_now = channel_;
			if (channel_now == InputChannel::STEREO && !in_->stereo_input_available()) {
				channel_now = InputChannel::LEFT;
			}
			// The oscillator's history of the channel's frame: without one there is nothing to shift
			const bool have_input = live_inputs_ != nullptr
			                        && (channel_now == InputChannel::STEREO ? live_inputs_->stereo != nullptr
			                                                                : live_inputs_->mono != nullptr);

			if (have_input) {
				// Out of memory leaves it absent, to try again next block
				std::ignore = live_pitch_shifter_.create(channel_now, static_cast<int32_t>(phase_increment));
			}
		}
	}

	// If not pitch shifting and we were previously...
	else {
		if (live_pitch_shifter_.exists() && live_pitch_shifter_.may_be_removed_without_click()) {
			live_pitch_shifter_.destroy();
		}
	}

	// Yes pitch shifting
	if (live_pitch_shifter_.exists()) {
		// With unison stereo spread, oscBuffer is an interleaved stereo buffer. A mono input must be rendered
		// to a temporary mono buffer first and then panned across it, otherwise the mono render would write
		// into the interleaved buffer as if it were mono and produce noise. Either way the shifter reports the
		// hops it performed; the port folds them into the host's CPU-load accounting.
		if (stereo_unison && !live_pitch_shifter_.stereo()) {
			dsp::Q<31>* render_buffer = mono_scratch;
			std::fill_n(render_buffer, num_samples, dsp::Q<31>{});

			in_->report_hops(live_pitch_shifter_.render(
			    render_buffer, num_samples, static_cast<int32_t>(phase_increment), source_amplitude,
			    amplitude_increment, interpolation_buffer_size, *in_, *live_inputs_, noise));

			for (int32_t i = 0; i < num_samples; i++) {
				add_panned(&osc_buffer[i << 1], render_buffer[i], amplitude_l, amplitude_r);
			}
		}
		else {
			in_->report_hops(live_pitch_shifter_.render(osc_buffer, num_samples, static_cast<int32_t>(phase_increment),
			                                            source_amplitude, amplitude_increment,
			                                            interpolation_buffer_size, *in_, *live_inputs_, noise));
		}
	}

	// No pitch shifting
	else {

		dsp::Q<31>* __restrict__ osc_buffer_pos = osc_buffer;
		int32_t const* __restrict__ input_read_pos = in_->ring_read_pos();
		int32_t const* const input_ring_end = in_->ring_end();
		dsp::Q<31> source_amplitude_this_unison = source_amplitude;

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

			dsp::Q<31> source_amplitude_now = source_amplitude_this_unison;

			// With unison stereo spread, oscBuffer is an interleaved stereo buffer and each unison part is
			// panned across it.
			if (stereo_unison) {
				dsp::Q<31> const* const osc_buffer_end = osc_buffer + num_samples * 2;
				do {
					source_amplitude_now = add_wrap(source_amplitude_now, amplitude_increment);

					add_panned(osc_buffer_pos,
					           levelled(input_sample(&input_read_pos[channel_offset]), source_amplitude_now),
					           amplitude_l, amplitude_r);
					osc_buffer_pos += 2;

					input_read_pos += NUM_MONO_INPUT_CHANNELS;
					if (input_read_pos >= input_ring_end) {
						input_read_pos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
					}
				} while (osc_buffer_pos != osc_buffer_end);
			}
			else {
				dsp::Q<31> const* const osc_buffer_end = osc_buffer + num_samples;
				do {
					source_amplitude_now = add_wrap(source_amplitude_now, amplitude_increment);

					// Mono / left channel (or stereo condensed to mono)
					add_into(osc_buffer_pos++,
					         levelled(input_sample(&input_read_pos[channel_offset]), source_amplitude_now));

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

			dsp::Q<31> const* const osc_buffer_end = osc_buffer + num_samples * num_channels_after_condensing;

			dsp::Q<31> source_amplitude_now = source_amplitude_this_unison;
			do {
				source_amplitude_now = add_wrap(source_amplitude_now, amplitude_increment);

				dsp::Q<31> sample_l = input_sample(&input_read_pos[0]);
				const dsp::Q<31> sample_r = input_sample(&input_read_pos[1]);

				// If condensing to mono, do that now
				if (!stereo_buffer) {
					sample_l = add_wrap(halve_floor(sample_l), halve_floor(sample_r));
				}

				// Mono / left channel (or stereo condensed to mono)
				add_into(osc_buffer_pos++, levelled(sample_l, source_amplitude_now));

				// Right channel
				if (stereo_buffer) {
					add_into(osc_buffer_pos++, levelled(sample_r, source_amplitude_now));
				}

				input_read_pos += NUM_MONO_INPUT_CHANNELS;
				if (input_read_pos >= input_ring_end) {
					input_read_pos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
				}
			} while (osc_buffer_pos != osc_buffer_end);
		}
	}
}

SourceRenderResult InputSource::render(const SourceBlock& block) {
	// Live input never ends by itself; channel routing follows the channel latched at note-on.
	// The pitch shifter steps the increment's word
	render_input(block.osc_buffer, block.num_samples, block.phase_increment.raw(), block.source_amplitude,
	             block.amplitude_increment, block.stereo_buffer, block.stereo_unison, block.amplitude_l,
	             block.amplitude_r, block.input.interpolation_buffer_size, *block.noise, block.stereo_scratch);
	return deluge::synth::SourceRenderResult::ACTIVE;
}

void InputSource::unassign(bool /*deleting_song*/) {
	live_pitch_shifter_.destroy();
}

} // namespace deluge::synth
