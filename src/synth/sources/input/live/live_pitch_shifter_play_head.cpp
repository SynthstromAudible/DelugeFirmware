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

#include "synth/sources/input/live/live_pitch_shifter_play_head.h"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/interpolation/interpolator.h"
#include "synth/sources/input/live/legacy_input_view.h"
#include "synth/sources/input/live/live_pitch_shifter.h"
#include <argon.hpp>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#ifdef __ARM_NEON
#pragma GCC target("fpu=neon") // no-op (and invalid) off-target; SIMDe supplies NEON on the host
#endif
#endif

#include "arm_neon_shim.h"

// The live pitch shifter keeps its legacy arithmetic as it is until its rewrite (TODO.md).
// NOLINTBEGIN(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,modernize-avoid-c-style-cast,readability-implicit-bool-conversion,readability-make-member-function-const)

namespace deluge::synth {

namespace {
/// @brief Adds @p sample times the amplitude into the buffer sample at @p out, wrapping: the rounded product moved to
/// Q31, then ×2^(N − 1) for the play head mode's level, ×16 repitched or interpolated and ×8 direct.
template <int N>
[[gnu::always_inline]] inline void add_levelled(dsp::Q<31>* out, int32_t sample, int32_t amplitude) {
	const auto product = mul_r_as_wrap<dsp::Q<31>>(dsp::Q<31>::from_raw(sample), dsp::Q<31>::from_raw(amplitude));
	*out = add_wrap(*out, scale_shl_wrap<N - 1>(product));
}
} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): split by the pitch-shifter rewrite (TODO.md)
void LivePitchShifterPlayHead::render(dsp::Q<31>* __restrict__ output_buffer, int32_t num_samples, int32_t num_channels,
                                      int32_t phase_increment, int32_t amplitude, int32_t amplitude_increment,
                                      [[maybe_unused]] int32_t* repitched_buffer, const LegacyInputView& input,
                                      int32_t which_kernel, int32_t interpolation_buffer_size) {

	dsp::Q<31>* output_buffer_end = output_buffer + num_samples * num_channels;

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		do {
			amplitude += amplitude_increment;

			add_levelled<5>(output_buffer, repitched_buffer[repitchedBufferReadPos * num_channels], amplitude);
			output_buffer++;

			if (num_channels == 2) {
				add_levelled<5>(outputBufferR, repitched_buffer[repitchedBufferReadPos * 2 + 1], amplitude);
				outputBufferR++;
			}

			repitchedBufferReadPos = (repitchedBufferReadPos + 1) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
		} while (output_buffer != output_buffer_end);
	}

	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		do {

			osc_pos += phase_increment;
			int32_t num_samples_to_jump_forward = osc_pos >> 24;
			if (num_samples_to_jump_forward) {
				osc_pos &= 16777215;

				// If jumping forward by more than kInterpolationMaxNumSamples, we first need to jump to the one before
				// we're jumping forward to, to grab its value
				if (num_samples_to_jump_forward > kInterpolationMaxNumSamples) {
					raw_buffer_read_pos =
					    (raw_buffer_read_pos + (num_samples_to_jump_forward - kInterpolationMaxNumSamples))
					    & (kInputRawBufferSize - 1);

					// Shouldn't be necesssary, but for some reason this seems to do
					// some optimization and speed things up. Re-test?
					num_samples_to_jump_forward = kInterpolationMaxNumSamples;
				}

				auto feed = interpolator_.advance(num_samples_to_jump_forward);
				while (feed.remaining() != 0) {
					const int16_t l = input.high_l(raw_buffer_read_pos);
					if (num_channels == 2) {
						feed.put(l, input.high_r(raw_buffer_read_pos));
					}
					else {
						feed.put(l);
					}

					raw_buffer_read_pos = (raw_buffer_read_pos + 1) & (kInputRawBufferSize - 1);
				}
			}

			amplitude += amplitude_increment;

			deluge::dsp::StereoSample sample_read = (interpolation_buffer_size > 2)
			                                            ? interpolator_.interpolate(num_channels, which_kernel, osc_pos)
			                                            : interpolator_.interpolate_linear(num_channels, osc_pos);

			add_levelled<5>(output_buffer, sample_read.l.raw(), amplitude);
			output_buffer++;

			if (num_channels == 2) {
				add_levelled<5>(output_buffer, sample_read.r.raw(), amplitude);
				output_buffer++;
			}
		} while (output_buffer != output_buffer_end);
	}

	else { // DIRECT
		do {
			amplitude += amplitude_increment;

			add_levelled<4>(output_buffer, input.word_l(raw_buffer_read_pos), amplitude);
			output_buffer++;

			if (num_channels == 2) {
				add_levelled<4>(output_buffer, input.word_r(raw_buffer_read_pos), amplitude);
				output_buffer++;
			}
			raw_buffer_read_pos = (raw_buffer_read_pos + 1) & (kInputRawBufferSize - 1);
		} while (output_buffer != output_buffer_end);
	}
}

// Returns how much longer (in raw samples) this play-head could play for before it reaches "now" time (which is itself
// moving forward) and runs out of audio Only valid if phaseIncrement > kMaxSampleValue
// NOLINTNEXTLINE(readability-make-member-function-const): the header changes with the pitch-shifter rewrite (TODO.md)
int32_t LivePitchShifterPlayHead::get_estimated_playtime_remaining([[maybe_unused]] uint32_t repitched_buffer_write_pos,
                                                                   const LegacyInputView& live_input,
                                                                   int32_t phase_increment) {
	uint32_t how_far_back = 0;
#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		how_far_back =
		    (uint32_t)(repitched_buffer_write_pos - repitchedBufferReadPos) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
	}
	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		uint32_t how_far_back_raw = (live_input.frames_written() - raw_buffer_read_pos) & (kInputRawBufferSize - 1);
		how_far_back = (static_cast<uint64_t>(how_far_back_raw) << 24) / static_cast<uint32_t>(phase_increment);
	}
	else {                 // DIRECT
		return 2147483647; // It'd never run out
	}

	uint64_t estimate =
	    (static_cast<uint64_t>(how_far_back) << 24) / static_cast<uint32_t>(phase_increment - kMaxSampleValue);
	if (estimate >= 2147483647) {
		return 2147483647;
	}
	else {
		return estimate;
	}
}

// NOLINTNEXTLINE(readability-make-member-function-const): the header changes with the pitch-shifter rewrite (TODO.md)
int32_t
LivePitchShifterPlayHead::get_num_raw_samples_behind_input(const LegacyInputView& live_input,
                                                           [[maybe_unused]] LivePitchShifter* live_pitch_shifter,
                                                           [[maybe_unused]] int32_t phase_increment) {
#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		uint32_t howFarBackRepitched = (uint32_t)(live_pitch_shifter->repitchedBufferWritePos - repitchedBufferReadPos)
		                               & (INPUT_REPITCHED_BUFFER_SIZE - 1);
		return ((uint64_t)howFarBackRepitched * phase_increment) >> 24;
	}
	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		return static_cast<int32_t>((live_input.frames_written() - raw_buffer_read_pos) & (kInputRawBufferSize - 1));
	}

	else { // DIRECT
		return 0;
	}
}

/// @brief Refills the window from the raw ring behind the read position, newest first; a position not yet written reads
/// as silence.
/// @param num_channels 1 or 2.
void LivePitchShifterPlayHead::fill_interpolation_buffer(const LegacyInputView& live_input, int32_t num_channels) {
	auto feed = interpolator_.fill_tail(0);
	for (int32_t i = 1; i <= kInterpolationMaxNumSamples; i++) {
		int32_t pos = (uint32_t)(raw_buffer_read_pos - i) & (kInputRawBufferSize - 1);
		const bool written = live_input.written(pos);
		const int16_t l = written ? live_input.high_l(pos) : int16_t{0};
		if (num_channels > 1) {
			feed.put(l, written ? live_input.high_r(pos) : int16_t{0});
		}
		else {
			feed.put(l);
		}
	}
}

} // namespace deluge::synth
// NOLINTEND(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,modernize-avoid-c-style-cast,readability-implicit-bool-conversion,readability-make-member-function-const)
