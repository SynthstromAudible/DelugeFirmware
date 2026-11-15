/*
 * Copyright © 2018-2026 Synthstrom Audible Limited
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
/// @brief The live pitch shifter's oracle: the shifter, its play heads and its input buffer as they were before their
/// rewrite as dsp-next components, verbatim but for owning their storage. The shifter is handed its buffer and reads
/// the input ring through a plain @ref ReferencePort; the compile-time-dead repitched-buffer and hop-timing paths are
/// resolved away. Never edit its arithmetic: the components are proven against it (pitch_shift_workloads.h).

#pragma once

#include "board_config.h"
#include "definitions_cxx.hpp"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/interpolation/sinc_interpolation.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "dsp-next/fixed/timestretch/crossfade.h"
#include "synth/sources/input/input_channel.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

// The legacy arithmetic, verbatim.
// NOLINTBEGIN

namespace deluge::dsp::fixed::reference_interpolator {

namespace detail {
/// Shift a 16-tap window held as two 8-lane halves toward older indices by @p N (1 ... 15): the value at i ends up at
/// i + N and the oldest fall off. `vext` takes its offset as an immediate, hence the template. The @p N vacated head
/// slots come out holding unspecified values, which the caller overwrites.
template <int N>
[[gnu::always_inline]] inline void shift_window_by(Argon<int16_t>& low, Argon<int16_t>& high) {
	static_assert(N > 0 && N < 16);
	if constexpr (N < 8) {
		// The top half takes the upper end of the bottom half, the bottom half moves up within itself
		high = low.Extract<8 - N>(high);
		low = low.Extract<8 - N>(low);
	}
	else if constexpr (N == 8) {
		high = low;
	}
	else {
		// Only the top half keeps anything, taken from the bottom half
		high = low.Extract<16 - N>(low);
	}
}
} // namespace detail

/// @brief A stereo windowed-sinc / linear sample interpolator over a sliding tap window (q31 backend).
///
/// Holds the most recent @ref kInterpolationMaxNumSamples input samples per channel — the *tap window*, newest at
/// index 0, oldest at the end. The owner feeds the window (@ref push_l / @ref push_r advance it by one sample,
/// @ref jump_forward by several) and reads a sub-sample-accurate output frame with @ref interpolate (windowed-sinc,
/// using the @p which_kernel-selected oversampling variant of @ref kWindowedSincKernel) or, on the cheap path,
/// @ref interpolate_linear (2-tap linear).
///
/// The tap windows are intentionally public: the sample-playback reader fills and shifts them directly for loop
/// crossfades and jump-forward zero-fills — sample-specific window management that isn't generic interpolation, and
/// which a later refactor may lift behind a richer API. The feed/read methods are the common path.
struct ReferenceInterpolator {
	using Window = std::array<int16_t, kInterpolationMaxNumSamples>;

	/// @brief Sub-sample-accurate windowed-sinc interpolation of the current window.
	/// @param channels 1 = mono (left window only), 2 = stereo (both).
	/// @param whichKernel the oversampling-kernel variant (from `WindowedSincKernel::variant_for`, by playback rate).
	/// @param oscPos the sub-sample phase within the window.
	/// @return the interpolated frame (right channel left untouched when @p channels == 1).
	[[nodiscard]] StereoSample interpolate(std::size_t channels, int32_t which_kernel, uint32_t osc_pos);

	/// @brief 2-tap linear interpolation between the two newest window samples — the cheap alternative.
	/// @details An integer lerp: the Q15 samples times Q15 weights that sum to 32767, so the frame is Q30 of the
	/// samples, as @ref interpolate's is.
	/// @param channels 1 = mono, 2 = stereo. @param phase the sub-sample phase.
	[[nodiscard]] StereoSample interpolate_linear(std::size_t channels, uint32_t phase);

	/// Advance the left window by one sample, inserting @p value at the head.
	[[gnu::always_inline]] constexpr void push_l(int16_t value) {
		shift_window(buffer_l, 1);
		buffer_l[0] = value;
	}
	/// Advance the right window by one sample, inserting @p value at the head.
	[[gnu::always_inline]] constexpr void push_r(int16_t value) {
		shift_window(buffer_r, 1);
		buffer_r[0] = value;
	}
	/// Advance both windows by @p num_samples, vacating that many head slots for the caller to fill.
	[[gnu::always_inline]] constexpr void jump_forward(std::size_t num_samples) {
		shift_window(buffer_l, num_samples);
		shift_window(buffer_r, num_samples);
	}

	/// The per-channel tap windows (newest sample at index 0). Quadword-aligned for the NEON dot product, and
	/// value-initialised so an unfed tap reads as a defined 0 rather than allocation residue.
	alignas(sizeof(int32_t) * 4) Window buffer_l{};
	alignas(sizeof(int32_t) * 4) Window buffer_r{};

private:
	/// Shift @p window toward older indices by @p n samples, vacating the @p n head slots (the discarded tail falls
	/// off the end; the vacated slots hold unspecified values until the caller fills them). Shared by @ref push_l /
	/// @ref push_r / @ref jump_forward. Two NEON `vext`s per window instead of up to 15 halfword copies, which neither
	/// GCC nor Clang vectorises for n < 8 (the loop reads slots it has just written).
	[[gnu::always_inline]] static constexpr void shift_window(Window& window, std::size_t n) {
		if consteval {
			for (int32_t i = kInterpolationMaxNumSamples - 1; i >= static_cast<int32_t>(n); i--) {
				window[i] = window[i - static_cast<int32_t>(n)];
			}
		}
		else {
			static_assert(kInterpolationMaxNumSamples == 2 * Argon<int16_t>::lanes);
			if (n == 0 || n >= kInterpolationMaxNumSamples) {
				return; // 0: nothing moves. 16 and more: nothing stays, the caller refills every slot
			}
			auto low = Argon<int16_t>::Load(window.data());
			auto high = Argon<int16_t>::Load(window.data() + Argon<int16_t>::lanes);
			switch (n) {
			case 1:
				detail::shift_window_by<1>(low, high);
				break;
			case 2:
				detail::shift_window_by<2>(low, high);
				break;
			case 3:
				detail::shift_window_by<3>(low, high);
				break;
			case 4:
				detail::shift_window_by<4>(low, high);
				break;
			case 5:
				detail::shift_window_by<5>(low, high);
				break;
			case 6:
				detail::shift_window_by<6>(low, high);
				break;
			case 7:
				detail::shift_window_by<7>(low, high);
				break;
			case 8:
				detail::shift_window_by<8>(low, high);
				break;
			case 9:
				detail::shift_window_by<9>(low, high);
				break;
			case 10:
				detail::shift_window_by<10>(low, high);
				break;
			case 11:
				detail::shift_window_by<11>(low, high);
				break;
			case 12:
				detail::shift_window_by<12>(low, high);
				break;
			case 13:
				detail::shift_window_by<13>(low, high);
				break;
			case 14:
				detail::shift_window_by<14>(low, high);
				break;
			case 15:
				detail::shift_window_by<15>(low, high);
				break;
			}
			low.StoreTo(window.data());
			high.StoreTo(window.data() + Argon<int16_t>::lanes);
		}
	}
};

/// Load one tap window (a @ref Interpolator channel buffer) into the int16 argon vectors @ref windowed_sinc_dot wants.
[[gnu::always_inline]] inline SincWindow load_window(const int16_t* buffer) {
	SincWindow window;
	for (size_t i = 0; i < kSincWindowVectors; ++i) {
		window[i] = Q16Vec<15>::from_raw(Argon<int16_t>::Load(&buffer[i * Argon<int16_t>::lanes]));
	}
	return window;
}

inline StereoSample ReferenceInterpolator::interpolate(std::size_t channels, int32_t which_kernel, uint32_t osc_pos) {
	constexpr size_t kNumBitsInTableSize = 8;
	constexpr size_t kRshiftAmount = ((24 + kInterpolationMaxNumSamplesMagnitude) - 16 - kNumBitsInTableSize + 1);

	uint32_t rshifted;
	if constexpr (kRshiftAmount >= 0) {
		rshifted = osc_pos >> kRshiftAmount;
	}
	else {
		rshifted = osc_pos << (-kRshiftAmount);
	}

	int16_t strength2 = rshifted & std::numeric_limits<int16_t>::max();
	const auto progress_small =
	    static_cast<int32_t>(osc_pos >> (24 + kInterpolationMaxNumSamplesMagnitude - kNumBitsInTableSize));

	// Position the (shared) kernel to sub-sample accuracy once, then weight each channel's window with it.
	const int16_t* kernel_row = kWindowedSincKernel.variant(which_kernel) + progress_small * kSincKernelTaps;
	SincWindow kernel = blend_sinc_kernel(kernel_row, strength2);

	// Each window's Q30 dot product, its word read as the Q31 sample.
	StereoSample output;
	output.l = reinterpret_bits<Q<31>>(windowed_sinc_dot(kernel, load_window(buffer_l.data())));
	if (channels == 2) {
		output.r = reinterpret_bits<Q<31>>(windowed_sinc_dot(kernel, load_window(buffer_r.data())));
	}
	return output;
}

inline StereoSample ReferenceInterpolator::interpolate_linear(std::size_t channels, uint32_t phase) {
	StereoSample output;
	int16_t strength2 = phase >> 9;
	int16_t strength1 = std::numeric_limits<int16_t>::max() - strength2; // inverse

	// The 16-bit samples times their Q15 strengths, summed: integer products, read as the Q31 sample.
	output.l = Q<31>::from_raw((buffer_l[1] * strength1) + (buffer_l[0] * strength2));
	if (channels == 2) {
		output.r = Q<31>::from_raw((buffer_r[1] * strength1) + (buffer_r[0] * strength2));
	}
	return output;
}

} // namespace deluge::dsp::fixed::reference_interpolator

namespace deluge::dsp::test::reference_pitch_shift {

using deluge::dsp::fixed::reference_interpolator::ReferenceInterpolator;
using deluge::synth::InputChannel;
using std::memset;

/// The input ring as the shifter reads it: the frame clock and the block's first interleaved frame.
struct ReferencePort {
	uint32_t time = 0;                 ///< The audio frame clock at the block's start.
	const int32_t* read_pos = nullptr; ///< The block's first frame in the ring.
	const int32_t* end = nullptr;      ///< One past the ring's last word.
};

/// How often the oracle took each path, summed over every shifter since @ref reset_counts: the workloads' coverage.
struct Counts {
	std::size_t hops_up = 0;            ///< hop_end while pitching up
	std::size_t hops_down = 0;          ///< hop_end while pitching down
	std::size_t cuts = 0;               ///< a hop ended early on a percussive onset
	std::size_t shortenings = 0;        ///< a hop shortened as the newer head would run out
	std::size_t sign_flips = 0;         ///< the aligner's total change changed sign
	std::size_t early_flips = 0;        ///< the aligner turned round on its first step
	std::size_t sub_sample_offsets = 0; ///< the aligner placed the head between samples
	std::size_t direct_blocks = 0;      ///< a head rendered direct
	std::size_t sinc_blocks = 0;        ///< a head rendered repitched, windowed sinc
	std::size_t linear_blocks = 0;      ///< a head rendered repitched, linear
};
inline Counts counts{};
inline void reset_counts() {
	counts = {};
}

class ReferenceLivePitchShifter;
class ReferenceInputBuffer;

class ReferenceInputBuffer {
public:
	ReferenceInputBuffer();
	~ReferenceInputBuffer() = default;
	/// Ingest @p numSamples of fresh input (via @p port — the ring + frame clock) and run the angle/perc analysis.
	void give_input(int32_t num_samples, const ReferencePort& port, InputChannel channel);
	bool get_averages_for_crossfade(int32_t* totals, int32_t start_pos, int32_t length_to_average_each,
	                                int32_t num_channels);

	uint32_t up_to_time;

	uint32_t num_raw_samples_processed;

	int32_t last_sample_read{};
	int32_t last_angle{};
	int32_t angle_lpf_mem[kDifferenceLPFPoles]{};

	// Zero-initialised: the hop-cut decision reads percBuffer positions ahead of what giveInput has written during
	// warm-up, so an unwritten slot must be a defined 0 rather than recycled allocator content.
	uint8_t perc_buffer[kInputPercBufferSize]{};

	// Must be last!!! Cos we're gonna allocate and access it double-length for stereo
	int32_t raw_buffer[kInputRawBufferSize]{};
};

class ReferenceLivePitchShifter;
class ReferenceInputBuffer;

enum class PlayHeadMode {
	REPITCHED_BUFFER,
	RAW_REPITCHING,
	RAW_DIRECT,
};

struct ReferencePlayHead {
	void render(dsp::Q<31>* output_buffer, int32_t num_samples, int32_t num_channels, int32_t phase_increment,
	            int32_t amplitude, int32_t amplitude_increment, int32_t* repitched_buffer, int32_t* raw_buffer,
	            int32_t which_kernel, int32_t interpolation_buffer_size);
	int32_t get_estimated_playtime_remaining(uint32_t repitched_buffer_write_pos,
	                                         ReferenceInputBuffer* live_input_buffer, int32_t phase_increment);

	int32_t get_num_raw_samples_behind_input(ReferenceInputBuffer* live_input_buffer,
	                                         ReferenceLivePitchShifter* live_pitch_shifter, int32_t phase_increment);

	void fill_interpolation_buffer(ReferenceInputBuffer* live_input_buffer, int32_t num_channels);

	// Default member initializers: this class has no constructor and the OLDER play
	// head is never explicitly initialized by ReferenceLivePitchShifter, so on hardware its
	// fields would be stale heap bytes (the sim/golden builds zero them, masking it —
	// render() reads `mode` / play positions, so garbage here corrupts the pitch
	// shift). Zero matches the sim behaviour (`mode == REPITCHED_BUFFER`).
	PlayHeadMode mode = PlayHeadMode::REPITCHED_BUFFER;
	int32_t raw_buffer_read_pos = 0;
	uint32_t osc_pos = 0;

	ReferenceInterpolator interpolator_;

	uint32_t perc_pos = 0;
};

class ReferenceInputBuffer;

class ReferenceLivePitchShifter {
public:
	ReferenceLivePitchShifter(InputChannel channel, int32_t phase_increment);
	~ReferenceLivePitchShifter();

	/// Render @p numSamplesThisFunctionCall of pitch-shifted output, summed into @p outputBuffer.
	/// @return the number of time-stretch hops performed this call. The caller folds this into the host's
	/// per-routine hop accounting (CPU-load spreading) — the pitch-shifter itself stays free of that global.
	[[nodiscard]] int32_t render(dsp::Q<31>* output_buffer, int32_t num_samples_this_function_call,
	                             int32_t phase_increment, int32_t amplitude, int32_t amplitude_increment,
	                             int32_t interpolation_buffer_size, const ReferencePort& port,
	                             ReferenceInputBuffer* buffer, dsp::fixed::Cong& noise);

	bool may_be_removed_without_click();

	int8_t num_channels;
	InputChannel channel;

	uint32_t crossfade_progress; // Out of kMaxSampleValue
	// crossfadeIncrement + percThresholdForCut are only set at the first hop (before the crossfade/perc logic that
	// reads them becomes reachable), so default-initialise them — otherwise a freshly-allocated shifter carries
	// read-before-write garbage in these fields.
	uint32_t crossfade_increment{0};
	int32_t next_crossfade_length;
	int32_t samples_til_hop_end;
	int32_t samples_into_hop;

	int32_t perc_threshold_for_cut{0};

	ReferencePlayHead play_heads[2];

private:
	void hop_end(int32_t phase_increment, ReferenceInputBuffer* live_input_buffer, uint64_t num_raw_samples_processed,
	             uint64_t num_raw_samples_processed_latest, dsp::fixed::Cong& noise);
	void consider_repitched_buffer(int32_t phase_increment);
	bool older_play_head_is_currently_sounding();
};

inline ReferenceInputBuffer::ReferenceInputBuffer() {
	up_to_time = 0;
	num_raw_samples_processed = 0;
	last_sample_read = 0;
	last_angle = 0;
	memset(angle_lpf_mem, 0, sizeof(angle_lpf_mem));
}

inline void ReferenceInputBuffer::give_input(int32_t num_samples, const ReferencePort& port, InputChannel channel) {

	uint32_t current_time = port.time;

	if (up_to_time == (uint32_t)(current_time + num_samples)) {
		return; // It's already been done
	}

	// Or if we need to reset everything cos we missed some
	if (up_to_time != current_time) {
		num_raw_samples_processed = 0;
		last_sample_read = 0;
		last_angle = 0;
		memset(angle_lpf_mem, 0, sizeof(angle_lpf_mem));
	}

	int32_t const* __restrict__ input_read_pos = port.read_pos;
	int32_t const* const input_ring_end = port.end;

	uint32_t end_num_raw_samples_processed = num_raw_samples_processed + num_samples;

	do {

		int32_t this_sample_read;

		if (channel == InputChannel::LEFT) {
			this_sample_read = input_read_pos[0] >> 2;
			raw_buffer[num_raw_samples_processed & (kInputRawBufferSize - 1)] = input_read_pos[0];
		}
		else if (channel == InputChannel::RIGHT) {
			this_sample_read = input_read_pos[1] >> 2;
			raw_buffer[num_raw_samples_processed & (kInputRawBufferSize - 1)] = input_read_pos[1];
		}
		else { // STEREO
			this_sample_read = (input_read_pos[0] >> 2) + (input_read_pos[1] >> 2);
			raw_buffer[(num_raw_samples_processed & (kInputRawBufferSize - 1)) * 2] = input_read_pos[0];
			raw_buffer[(num_raw_samples_processed & (kInputRawBufferSize - 1)) * 2 + 1] = input_read_pos[1];
		}

		int32_t angle = this_sample_read - last_sample_read;
		last_sample_read = this_sample_read;
		if (angle < 0) {
			angle = -angle;
		}

		for (int32_t p = 0; p < kDifferenceLPFPoles; p++) {
			int32_t distance_to_go = angle - angle_lpf_mem[p];
			// A 1/512 step toward the angle, rounded: the rounded high word of the distance times 2^23
			angle_lpf_mem[p] += mul_r(deluge::dsp::Q<31>::from_raw(distance_to_go), deluge::dsp::q<31>(0x1p-8)).raw();

			angle = angle_lpf_mem[p];
		}

		if ((num_raw_samples_processed & (kPercBufferReductionSize - 1)) == 0) {

			int32_t difference = angle - last_angle;
			if (difference < 0) {
				difference = -difference;
			}

			// guard against dividing by 0
			int32_t percussiveness = angle ? (((uint64_t)difference * 262144 / angle) >> 1) : 0;

			percussiveness = dsp::fixed::tanh<23>(dsp::Q<31>::from_raw(percussiveness)).raw();

			perc_buffer[(num_raw_samples_processed >> kPercBufferReductionMagnitude) & (kInputPercBufferSize - 1)] =
			    percussiveness;
		}
		last_angle = angle;

		input_read_pos += NUM_MONO_INPUT_CHANNELS;
		if (input_read_pos >= input_ring_end) {
			input_read_pos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
		}

		num_raw_samples_processed++;
	} while (num_raw_samples_processed != end_num_raw_samples_processed);

	up_to_time = current_time + num_samples;
}

inline bool ReferenceInputBuffer::get_averages_for_crossfade(int32_t* totals, int32_t start_pos,
                                                             int32_t length_to_average_each, int32_t num_channels) {

	int32_t current_pos = start_pos;
	for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {

		totals[i] = 0;
		int32_t end_pos = (current_pos + length_to_average_each) & (kInputRawBufferSize - 1);

		// Splitting this in two by numChannels makes it a bit faster for mono, but slower for stereo
		do {
			totals[i] += raw_buffer[current_pos * num_channels] >> 16;
			if (num_channels == 2) {
				totals[i] += raw_buffer[current_pos * 2 + 1] >> 16;
			}

			current_pos = (current_pos + 1) & (kInputRawBufferSize - 1);
		} while (current_pos != end_pos);
	}

	return true;
}

/// @brief Adds @p sample times the amplitude into the buffer sample at @p out, wrapping: the rounded product moved to
/// Q31, then ×2^(N − 1) for the play head mode's level, ×16 repitched or interpolated and ×8 direct.
template <int N>
[[gnu::always_inline]] inline void add_levelled(dsp::Q<31>* out, int32_t sample, int32_t amplitude) {
	const auto product = mul_r_as_wrap<dsp::Q<31>>(dsp::Q<31>::from_raw(sample), dsp::Q<31>::from_raw(amplitude));
	*out = add_wrap(*out, scale_shl_wrap<N - 1>(product));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): split by the pitch-shifter rewrite (TODO.md)
inline void ReferencePlayHead::render(dsp::Q<31>* __restrict__ output_buffer, int32_t num_samples, int32_t num_channels,
                                      int32_t phase_increment, int32_t amplitude, int32_t amplitude_increment,
                                      [[maybe_unused]] int32_t* repitched_buffer, int32_t* raw_buffer,
                                      int32_t which_kernel, int32_t interpolation_buffer_size) {

	dsp::Q<31>* output_buffer_end = output_buffer + num_samples * num_channels;

	if (mode == PlayHeadMode::RAW_REPITCHING) {
		++(interpolation_buffer_size > 2 ? counts.sinc_blocks : counts.linear_blocks);
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

				interpolator_.jump_forward(num_samples_to_jump_forward);

				while (num_samples_to_jump_forward--) {
					interpolator_.buffer_l[num_samples_to_jump_forward] =
					    static_cast<int16_t>(raw_buffer[raw_buffer_read_pos * num_channels] >> 16);
					if (num_channels == 2) {
						interpolator_.buffer_r[num_samples_to_jump_forward] =
						    static_cast<int16_t>(raw_buffer[raw_buffer_read_pos * 2 + 1] >> 16);
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
		++counts.direct_blocks;
		do {
			amplitude += amplitude_increment;

			add_levelled<4>(output_buffer, raw_buffer[raw_buffer_read_pos * num_channels], amplitude);
			output_buffer++;

			if (num_channels == 2) {
				add_levelled<4>(output_buffer, raw_buffer[raw_buffer_read_pos * 2 + 1], amplitude);
				output_buffer++;
			}
			raw_buffer_read_pos = (raw_buffer_read_pos + 1) & (kInputRawBufferSize - 1);
		} while (output_buffer != output_buffer_end);
	}
}

// Returns how much longer (in raw samples) this play-head could play for before it reaches "now" time (which is itself
// moving forward) and runs out of audio Only valid if phaseIncrement > kMaxSampleValue
// NOLINTNEXTLINE(readability-make-member-function-const): the header changes with the pitch-shifter rewrite (TODO.md)
inline int32_t ReferencePlayHead::get_estimated_playtime_remaining([[maybe_unused]] uint32_t repitched_buffer_write_pos,
                                                                   ReferenceInputBuffer* live_input_buffer,
                                                                   int32_t phase_increment) {
	uint32_t how_far_back = 0;
	if (mode == PlayHeadMode::RAW_REPITCHING) {
		uint32_t how_far_back_raw =
		    (live_input_buffer->num_raw_samples_processed - raw_buffer_read_pos) & (kInputRawBufferSize - 1);
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
inline int32_t
ReferencePlayHead::get_num_raw_samples_behind_input(ReferenceInputBuffer* live_input_buffer,
                                                    [[maybe_unused]] ReferenceLivePitchShifter* live_pitch_shifter,
                                                    [[maybe_unused]] int32_t phase_increment) {
	if (mode == PlayHeadMode::RAW_REPITCHING) {
		return static_cast<int32_t>((live_input_buffer->num_raw_samples_processed - raw_buffer_read_pos)
		                            & (kInputRawBufferSize - 1));
	}

	else { // DIRECT
		return 0;
	}
}

/// @param numChannels a value between 0 and 1
inline void ReferencePlayHead::fill_interpolation_buffer(ReferenceInputBuffer* live_input_buffer,
                                                         int32_t num_channels) {
	for (int32_t i = 1; i <= kInterpolationMaxNumSamples; i++) {
		int32_t pos = (uint32_t)(raw_buffer_read_pos - i) & (kInputRawBufferSize - 1);

		interpolator_.buffer_l[i - 1] =
		    static_cast<int16_t>(std::cmp_less(pos, live_input_buffer->num_raw_samples_processed)
		                             ? live_input_buffer->raw_buffer[pos * num_channels] >> 16
		                             : 0);
	}

	if (num_channels > 1) {
		for (int32_t i = 1; i <= kInterpolationMaxNumSamples; i++) {
			int32_t pos = (uint32_t)(raw_buffer_read_pos - i) & (kInputRawBufferSize - 1);

			interpolator_.buffer_r[i - 1] =
			    static_cast<int16_t>(std::cmp_less(pos, live_input_buffer->num_raw_samples_processed)
			                             ? live_input_buffer->raw_buffer[pos * num_channels + 1] >> 16
			                             : 0);
		}
	}
}

inline ReferenceLivePitchShifter::ReferenceLivePitchShifter(InputChannel new_channel, int32_t phase_increment) {
	channel = new_channel;
	num_channels = (new_channel == InputChannel::STEREO) ? 2 : 1;

	if (phase_increment < kMaxSampleValue) {
		next_crossfade_length = samples_til_hop_end = kInterpolationMaxNumSamples * 2;
	}

	else if (phase_increment < 17774841) { // If up by less than 1 semitone...
		samples_til_hop_end = 2048;
		next_crossfade_length = 256;
	}

	else {
		next_crossfade_length = samples_til_hop_end = 256;
	}

	crossfade_progress = kMaxSampleValue;
	samples_into_hop = 0;

	consider_repitched_buffer(phase_increment);

	play_heads[PLAY_HEAD_NEWER].mode = PlayHeadMode::RAW_DIRECT;
	play_heads[PLAY_HEAD_NEWER].osc_pos = 0;

	play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos = 0;
	play_heads[PLAY_HEAD_NEWER].perc_pos = 0xFFFFFFFF;
}

inline ReferenceLivePitchShifter::~ReferenceLivePitchShifter() {
}

inline int32_t ReferenceLivePitchShifter::render(dsp::Q<31>* __restrict__ output_buffer,
                                                 int32_t num_samples_this_function_call, int32_t phase_increment,
                                                 int32_t amplitude, int32_t amplitude_increment,
                                                 int32_t interpolation_buffer_size, const ReferencePort& port,
                                                 ReferenceInputBuffer* buffer, dsp::fixed::Cong& noise) {

	ReferenceInputBuffer* live_input_buffer = buffer;
	if (!live_input_buffer) {
		return 0; // Render nothing rather than dereference null
	}

	live_input_buffer->give_input(num_samples_this_function_call, port, channel);

	int32_t which_kernel = dsp::fixed::WindowedSincKernel::variant_for(phase_increment);

	uint64_t num_raw_samples_processed_at_start =
	    live_input_buffer->num_raw_samples_processed - num_samples_this_function_call;

	bool just_did_hop = false;
	int32_t hops_ended = 0;

startRenderAgain:

	if (!just_did_hop && phase_increment > kMaxSampleValue) {
		int32_t max_playable_samples_newer =
		    play_heads[PLAY_HEAD_NEWER].get_estimated_playtime_remaining(0, live_input_buffer, phase_increment);

		if (samples_til_hop_end + next_crossfade_length > max_playable_samples_newer) {

			int32_t max_total_playable = max_playable_samples_newer + samples_into_hop;
			next_crossfade_length = std::min(next_crossfade_length, max_total_playable >> 1);

			samples_til_hop_end = max_playable_samples_newer - next_crossfade_length;
			++counts.shortenings;
			// D_PRINTLN("shortening hop");

			if (samples_til_hop_end < 0) {
				samples_til_hop_end = 0;
				next_crossfade_length = std::max(max_playable_samples_newer, 0_i32);
				// D_PRINTLN("nex");
				crossfade_progress = kMaxSampleValue;
			}

			else if (samples_til_hop_end == 0) {
				// D_PRINTLN("to zero");
			}

			else if (samples_til_hop_end > 0 && older_play_head_is_currently_sounding()) {
				uint32_t min_crossfade_increment =
				    (uint32_t)(kMaxSampleValue - crossfade_progress) / samples_til_hop_end + 1;
				if (min_crossfade_increment > crossfade_increment) {
					crossfade_increment = min_crossfade_increment;
					// D_PRINTLN("d");
				}
			}
		}

		if (samples_til_hop_end && older_play_head_is_currently_sounding()) {
			uint32_t max_playable_samples_older =
			    play_heads[PLAY_HEAD_OLDER].get_estimated_playtime_remaining(0, live_input_buffer, phase_increment);
			// D_PRINTLN(maxPlayableSamplesOlder);
			if (!max_playable_samples_older) {
				crossfade_increment = kMaxSampleValue;
			}
			else {
				uint32_t min_crossfade_increment =
				    (uint32_t)(kMaxSampleValue - crossfade_progress) / max_playable_samples_older + 1;
				// crossfadeIncrement = std::max(crossfadeIncrement, minCrossfadeIncrement);
				if (min_crossfade_increment > crossfade_increment) {
					crossfade_increment = min_crossfade_increment;
					// D_PRINTLN("c");
				}
			}
		}
	}

	// If the percussiveness is higher at "now" time than at the newer play head, end the hop.
	// This was designed with pitching-down in mind, but sounds good on pitching-up too
	if (!just_did_hop && !older_play_head_is_currently_sounding() && samples_til_hop_end
	    && play_heads[PLAY_HEAD_NEWER].mode != PlayHeadMode::RAW_DIRECT) {

		int32_t how_far_back =
		    play_heads[PLAY_HEAD_NEWER].get_num_raw_samples_behind_input(live_input_buffer, this, phase_increment);

		uint32_t newer_play_head_perc_pos =
		    (live_input_buffer->num_raw_samples_processed - how_far_back - 1) >> kPercBufferReductionMagnitude;

		uint32_t latest_perc_pos_before = (num_raw_samples_processed_at_start - 1) >> kPercBufferReductionMagnitude;
		uint32_t latest_perc_pos_now =
		    (live_input_buffer->num_raw_samples_processed - 1) >> kPercBufferReductionMagnitude;

		if (latest_perc_pos_now != newer_play_head_perc_pos
		    && (newer_play_head_perc_pos != play_heads[PLAY_HEAD_NEWER].perc_pos
		        || latest_perc_pos_now != latest_perc_pos_before)) {

			int32_t perc_latest = live_input_buffer->perc_buffer[latest_perc_pos_now & (kInputPercBufferSize - 1)];
			int32_t perc_newer_play_head =
			    live_input_buffer->perc_buffer[newer_play_head_perc_pos & (kInputPercBufferSize - 1)];

			if (perc_latest >= perc_newer_play_head + perc_threshold_for_cut) {
				/*
				D_PRINT(percLatest);
				D_PRINTLN(" vs  %d", percNewerPlayHead);
				*/
				samples_til_hop_end = 0;
				++counts.cuts;
			}
		}

		play_heads[PLAY_HEAD_NEWER].perc_pos = newer_play_head_perc_pos;
	}

	if (!samples_til_hop_end) {
		hop_end(phase_increment, live_input_buffer, num_raw_samples_processed_at_start,
		        live_input_buffer->num_raw_samples_processed, noise);
		++hops_ended;
		just_did_hop = true;
		goto startRenderAgain;
	}

	int32_t num_samples_this_timestretched_read =
	    std::min((int32_t)num_samples_this_function_call, samples_til_hop_end);

	bool older_play_head_audible_here = older_play_head_is_currently_sounding();

	int32_t newer_source_amplitude_now;
	int32_t newer_amplitude_increment_now;
	int32_t older_source_amplitude_now;
	int32_t older_amplitude_increment_now;

	// If the older play-head is still fading out, which means we need to work out the crossfade envelope too...
	if (older_play_head_audible_here) {

		// Use linear crossfades. These sound less jarring when doing short hops. Square root ones sound better for
		// longer hops, with more difference in material between hops (i.e. time more stretched)
		using deluge::dsp::Q;
		// The crossfade's Q24 progress as the newer hop's Q31 amplitude, the older's the rest of full scale
		int32_t newer_hop_amplitude_now = as_wrap<Q<31>>(Q<24>::from_raw(crossfade_progress)).raw();
		int32_t older_hop_amplitude_now = 2147483647 - newer_hop_amplitude_now;

		crossfade_progress += crossfade_increment * num_samples_this_timestretched_read;

		// Where the progress will be, held to [−1, 1) before its move to Q31
		int32_t newer_hop_amplitude_after = as_wrap<Q<31>>(clamp_bits<25>(Q<24>::from_raw(crossfade_progress))).raw();

		int32_t newer_hop_amplitude_increment = (int32_t)(newer_hop_amplitude_after - newer_hop_amplitude_now)
		                                        / (int32_t)num_samples_this_timestretched_read;

		// The source amplitude times each Q31 hop amplitude (and its step), moved back to Q31
		const auto source = Q<31>::from_raw(amplitude);
		int32_t hop_amplitude_change = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(newer_hop_amplitude_increment)).raw();

		newer_amplitude_increment_now = amplitude_increment + hop_amplitude_change;
		newer_source_amplitude_now = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(newer_hop_amplitude_now)).raw();

		older_amplitude_increment_now = amplitude_increment - hop_amplitude_change;
		older_source_amplitude_now = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(older_hop_amplitude_now)).raw();
	}

	// Or, if we're just hearing the newer play-head
	else {
		newer_source_amplitude_now = amplitude;
		newer_amplitude_increment_now = amplitude_increment;
		// No need to set the "older" ones, cos that's not going to be rendered
	}

	// Read newer play-head
	play_heads[PLAY_HEAD_NEWER].render(output_buffer, num_samples_this_timestretched_read, num_channels,
	                                   phase_increment, newer_source_amplitude_now, newer_amplitude_increment_now,
	                                   nullptr, live_input_buffer->raw_buffer, which_kernel, interpolation_buffer_size);

	// Read older play-head
	if (older_play_head_audible_here) {
		play_heads[PLAY_HEAD_OLDER].render(output_buffer, num_samples_this_timestretched_read, num_channels,
		                                   phase_increment, older_source_amplitude_now, older_amplitude_increment_now,
		                                   nullptr, live_input_buffer->raw_buffer, which_kernel,
		                                   interpolation_buffer_size);
	}

	samples_til_hop_end -= num_samples_this_timestretched_read;
	samples_into_hop += num_samples_this_timestretched_read;
	num_samples_this_function_call -= num_samples_this_timestretched_read;

	if (num_samples_this_function_call) {
		output_buffer += num_samples_this_timestretched_read * num_channels;

		amplitude += amplitude_increment * num_samples_this_timestretched_read;

		num_raw_samples_processed_at_start += num_samples_this_timestretched_read;

		goto startRenderAgain;
	}

	return hops_ended;
}

inline constexpr int16_t kMinSearchFine[] = {
    8, 8, 8, 8, 8, 8,  8,  8,  // -12, ....
    8, 8, 8, 8, 8, 12, 14, 16, // +0, ....
    17                         // +12
};

inline constexpr int16_t kMinSearchCoarse[] = {10, 10, 10, 17, 20};

inline constexpr int16_t kMaxSearchFine[] = {
    8, 8, 8,  8,  8,  8,  8,  8,  // -12, ....
    8, 8, 10, 11, 12, 14, 16, 18, // +0, ....
    21                            // +12
};

inline constexpr int16_t kMaxSearchCoarse[] = {15, 15, 15, 21, 20};

inline constexpr int16_t kPercThresholdFine[] = {
    18,  18, 18, 18, 24, 30, 25, 35, // -12, ....
    130, 40, 40, 45, 50, 47, 45, 42, // +0, ....
    40                               // +12
};

inline constexpr int16_t kPercThresholdCoarse[] = {15, 18, 130, 40, 20};

inline constexpr int16_t kCrossfadeFine[] = {
    30, 31, 32, 34, 35, 30, 25, 10, // -12, ....
    10, 15, 15, 22, 30, 16, 15, 13, // +0, ....
    40                              // +12
};

inline constexpr int16_t kCrossfadeCoarse[] = {30, 30, 10, 40, 20};

inline constexpr int16_t kMaxHopLengthFine[] = {
    20,  27,  35,  42,  50,  60,  70,  90,  // -12, ....
    140, 140, 140, 140, 140, 140, 140, 140, // +0, ....
    140                                     // +12
};

inline constexpr int16_t kMaxHopLengthCoarse[] = {10, 20, 140, 140, 140};

inline constexpr int16_t kRandomFine[] = {
    0, 0,  0,  0,  0,  0,  0,  0,  // -12, ....
    0, 15, 25, 76, 50, 57, 65, 72, // +0, ....
    80                             // +12
};

inline constexpr int16_t kRandomCoarse[] = {0, 0, 0, 80, 80};

inline void ReferenceLivePitchShifter::hop_end(int32_t phase_increment, ReferenceInputBuffer* live_input_buffer,
                                               uint64_t num_raw_samples_processed_at_now_time,
                                               uint64_t num_raw_samples_processed_latest, dsp::fixed::Cong& noise) {

	// int32_t numChannelsNow = numChannels;

	// D_PRINTLN("");
	if (crossfade_progress < kMaxSampleValue) {
		// if (ALPHA_OR_BETA_VERSION) FREEZE_WITH_ERROR("FADE");
	}
	// D_PRINTLN(phaseIncrement);

	// D_PRINTLN((uint32_t)(liveInputBuffer->numRawSamplesProcessed - playHeads[PLAY_HEAD_NEWER].rawBufferReadPos) &
	// (kInputRawBufferSize - 1)); // How far behind raw buffer is being read

	// What was new is now old
	play_heads[PLAY_HEAD_OLDER] = play_heads[PLAY_HEAD_NEWER];
	uint32_t this_crossfade_length = next_crossfade_length;

	int32_t pitch_log = dsp::fixed::quick_log(phase_increment);

	int32_t min_search;
	int32_t max_search;
	int32_t max_hop_length;
	int32_t random_element;

	// Neutral is (832 << 20). Each octave is a (32 << 20)

	// If within +/- 1 octave...
	if (pitch_log >= (800 << 20) && pitch_log < (864 << 20)) {
		int32_t position = pitch_log - (800 << 20);

		min_search = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kMinSearchFine, 4) >> 9;
		max_search = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kMaxSearchFine, 4) >> 9;
		perc_threshold_for_cut =
		    dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kPercThresholdFine, 4) >> 16;
		next_crossfade_length =
		    dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kCrossfadeFine, 4) >> 12;
		max_hop_length =
		    (dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kMaxHopLengthFine, 4) >> 16) * 100;
		random_element = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kRandomFine, 4);
	}

	// Or if outside of that...
	else {
		if (pitch_log > (896 << 20)) {
			pitch_log = (896 << 20);
		}
		else if (pitch_log < (768 << 20)) {
			pitch_log = (768 << 20);
		}

		int32_t position = pitch_log - (768 << 20);

		min_search = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kMinSearchCoarse, 2) >> 9;
		max_search = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kMaxSearchCoarse, 2) >> 9;
		perc_threshold_for_cut =
		    dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kPercThresholdCoarse, 2) >> 16;
		next_crossfade_length =
		    dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kCrossfadeCoarse, 2) >> 12;
		max_hop_length =
		    (dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kMaxHopLengthCoarse, 2) >> 16) * 100;
		random_element = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kRandomCoarse, 2);
	}

	/*
	minSearch = StorageManager::devVarA << 7;
	maxSearch = StorageManager::devVarB << 7;
	nextCrossfadeLength = StorageManager::devVarC << 4;
	percThresholdForCut = StorageManager::devVarD;
	maxHopLength = StorageManager::devVarE * 100;
	//int32_t searchSizeFactor = StorageManager::devVarF;
	randomElement = StorageManager::devVarG << 16;
	*/

	// Collect info on those moving average for the now-older play-head, which we're going to fade out -
	// so that we can then fine-tune the position of the new play-head to match it

	// First, work out the length we'd *like* to use for the moving averages
	int32_t length_per_moving_average =
	    ((uint64_t)phase_increment * TimeStretch::Crossfade::kMovingAverageLength) >> 24;
	length_per_moving_average = std::clamp<int32_t>(
	    length_per_moving_average, 1, TimeStretch::Crossfade::kMovingAverageLength * 2); // Keep things sensible

	// Ok, and this crossfade we're about to do, how long will it be in samples of (unpitched) source material?
	int32_t crossfade_length_samples_source = ((uint64_t)this_crossfade_length * phase_increment) >> 24;

	// What's the maximum amount further forward than that older play-head that data actually exists yet for us to
	// examine?
	int32_t max_offset_from_head =
	    (uint32_t)(num_raw_samples_processed_latest - play_heads[PLAY_HEAD_OLDER].raw_buffer_read_pos)
	    & (kInputRawBufferSize - 1);

	// Ok, work out the end-pos of our moving-averages region
	int32_t averages_end_offset_from_head =
	    (crossfade_length_samples_source >> 1)
	    + ((length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages) >> 1);
	averages_end_offset_from_head =
	    std::min(averages_end_offset_from_head,
	             max_offset_from_head); // And make sure it's not beyond the end of the existent data

	// We now know the length of the *total* moving-averages region, so divide down to get the length of *each*
	// moving-average region If commenting out this next line, must make sure we still don't search back before we
	// started writing to buffer
	length_per_moving_average =
	    std::min(length_per_moving_average,
	             averages_end_offset_from_head >> 1); // / TimeStretch::Crossfade::kNumMovingAverages

	int32_t averages_start_offset_from_head =
	    averages_end_offset_from_head - (length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages);

	int32_t old_head_totals[TimeStretch::Crossfade::kNumMovingAverages];

	// Occasionally (if we've only just switched pitch shifting on), that'll be zero, so we can'd do the moving
	// averages. But if it's ok, then...
	if (length_per_moving_average) {
		int32_t averages_start_pos_old_head =
		    (play_heads[PLAY_HEAD_OLDER].raw_buffer_read_pos + averages_start_offset_from_head)
		    & (kInputRawBufferSize - 1);
		live_input_buffer->get_averages_for_crossfade(old_head_totals, averages_start_pos_old_head,
		                                              length_per_moving_average, num_channels);
	}

	int32_t averages_start_pos_new_head;
	int32_t search_size;
	int32_t search_direction;
	int32_t num_full_directions_searched;

	int32_t how_far_back;

	// Now we're going to pick a position for the new play-head based on searching the percussiveness data. This is
	// totally different for pitching up vs down. And while we're at it, also decide our search parameters for our
	// fine-tuning of the new play head pos - if we're going to do that (we might not - if lengthPerMovingAverage == 0)

	// If pitch shifting up...
	if (phase_increment > kMaxSampleValue) {
		++counts.hops_up;

		if (live_input_buffer) {

			// Search backwards, looking for the area (ending at now-time) with the highest average percussiveness.
			// This particularly helps to keep the hops from all happening at the same length, which leads to a clearly
			// audible sort of "tone". But, some sort of random element could actually work just as well - I haven't
			// tried yet.

			// A random fraction of the search length: the Q31 noise times the random element moved up 8, Q30, times the
			// length's word read as Q31 (an integer count, so the product is its fraction at Q29), ×4
			const auto fraction = mul(deluge::dsp::Q<31>::from_raw(static_cast<int32_t>(noise.next())),
			                          deluge::dsp::Q<31>::from_raw(random_element << 8));
			min_search += scale_shl_wrap<2>(mul(deluge::dsp::Q<31>::from_raw(min_search), fraction)).raw();

			int32_t back_edge = min_search >> kPercBufferReductionMagnitude; // Pixellated
			int32_t how_far_back_searched = 0;
			int32_t perc_pos =
			    (num_raw_samples_processed_at_now_time + kPercBufferReductionSize - 1) >> kPercBufferReductionMagnitude;

			uint32_t total_perc = 0;
			float best_average = 0;
			int32_t best_how_far_back = min_search >> kPercBufferReductionMagnitude; // Pixellated

			while (back_edge < (max_search >> kPercBufferReductionMagnitude)) {

				while (how_far_back_searched < back_edge) {
					how_far_back_searched++;
					if (how_far_back_searched > perc_pos) {
						goto stopPercSearch;
					}
					int32_t perc_here =
					    live_input_buffer
					        ->perc_buffer[(uint32_t)(perc_pos - how_far_back_searched) & (kInputPercBufferSize - 1)];
					total_perc += perc_here;
				}

				float average_perc = (float)total_perc / how_far_back_searched;
				if (average_perc > best_average) {
					best_average = average_perc;
					best_how_far_back = how_far_back_searched;
				}

				// else if (averagePerc < bestAverage * 0.5) break; // Didn't help

				back_edge++;
			}

stopPercSearch:

			how_far_back = best_how_far_back << kPercBufferReductionMagnitude;
		}

		else {
			how_far_back = 1500;
		}

		samples_til_hop_end =
		    ((uint64_t)how_far_back << 24) / (uint32_t)(phase_increment - kMaxSampleValue) - next_crossfade_length;
		if (samples_til_hop_end < 100) {
			samples_til_hop_end = 100; // Must be 100, not 200. Otherwise, shifting up 2 octaves gets messed up. (Though
			                           // maybe not anymore?)
		}
		else if (samples_til_hop_end > max_hop_length) {
			samples_til_hop_end = max_hop_length;
		}

		int32_t min_distance_back = num_raw_samples_processed_at_now_time - num_raw_samples_processed_latest
		                            + averages_start_offset_from_head
		                            + (length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages);
		how_far_back = std::max(how_far_back, min_distance_back);

		// NOLINTNEXTLINE(modernize-use-integer-sign-comparison): a negative howFarBack wraps, as it always has
		if (static_cast<uint64_t>(how_far_back) > num_raw_samples_processed_at_now_time) {
			how_far_back = num_raw_samples_processed_at_now_time;
		}

		if (length_per_moving_average) {
			averages_start_pos_new_head =
			    (num_raw_samples_processed_at_now_time - how_far_back + averages_start_offset_from_head)
			    & (kInputRawBufferSize - 1);
			search_size = 490; // Allow tracking down to about 45Hz
			search_size = std::min(search_size, samples_til_hop_end);
			num_full_directions_searched = 0;
			search_direction = 1;
		}

		play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos =
		    (uint32_t)(num_raw_samples_processed_at_now_time - how_far_back) & (kInputRawBufferSize - 1);
	}

	// Or if pitch shifting down...
	else {
		++counts.hops_down;
		samples_til_hop_end = max_hop_length;

		if (length_per_moving_average) {
			averages_start_pos_new_head = (num_raw_samples_processed_latest
			                               - length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)
			                              & (kInputRawBufferSize - 1);
			search_size = 980; // Allow tracking down to about 45Hz
			search_size = std::min(search_size, samples_into_hop);
			num_full_directions_searched = 1;
			search_direction = -1;
		}

		play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos =
		    (uint32_t)(num_raw_samples_processed_latest
		               - (length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)
		               - averages_start_offset_from_head)
		    & (kInputRawBufferSize - 1);
	}

	int32_t best_offset = 0;

	int32_t additional_osc_pos = 0;

	// If we're all good to do the moving averages...
	if (length_per_moving_average && play_heads[PLAY_HEAD_OLDER].mode != PlayHeadMode::RAW_DIRECT) {

		// Ok, and now we're going to get that moving-average info for the new play-head in the position where we're
		// currently proposing we put it - but we're then going to experiment with shifting that, below

		if (((uint32_t)(averages_start_pos_new_head - averages_start_offset_from_head) & (kInputRawBufferSize - 1))
		    >= num_raw_samples_processed_latest) {
			goto stopSearch;
		}

		if (((uint32_t)(num_raw_samples_processed_latest - averages_start_pos_new_head) & (kInputRawBufferSize - 1))
		    < static_cast<uint32_t>(length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)) {
			goto stopSearch;
		}

		int32_t new_head_totals[TimeStretch::Crossfade::kNumMovingAverages];
		live_input_buffer->get_averages_for_crossfade(new_head_totals, averages_start_pos_new_head,
		                                              length_per_moving_average, num_channels);

		int32_t best_difference_abs = deluge::dsp::fixed::totalDifferenceAbs(old_head_totals, new_head_totals);
		int32_t times_sign_flipped = 0;

		int32_t initial_total_change = deluge::dsp::fixed::totalChange(old_head_totals, new_head_totals);

startSearch:
		int32_t last_total_change = initial_total_change;

		int32_t read_pos[TimeStretch::Crossfade::kNumMovingAverages + 1];

		read_pos[0] = averages_start_pos_new_head;
		if (search_direction == -1) {
			read_pos[0] = (uint32_t)(read_pos[0] - 1) & (kInputRawBufferSize - 1);
		}

		int32_t new_head_running_totals[TimeStretch::Crossfade::kNumMovingAverages];
		for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {
			new_head_running_totals[i] = new_head_totals[i];
			read_pos[i + 1] = (uint32_t)(read_pos[i] + length_per_moving_average) & (kInputRawBufferSize - 1);
		}

		int32_t offset_now = 0;
		int32_t end_offset;

		int32_t search_size_boundary = search_size;
		if (search_direction == -1) {
			if (num_raw_samples_processed_latest < kInputRawBufferSize) { // Is this right?
				search_size_boundary =
				    averages_start_pos_new_head - averages_start_offset_from_head
				    - 1; // The -1 is becacuse of the 1 we subtract from readPos[0] above when searching left
				if (search_size_boundary <= 0) {
					goto searchNextDirection;
				}
			}
		}
		else {
			// readPos is sized [kNumMovingAverages + 1] and filled [0..kNumMovingAverages]
			// (above); indexing [kNumMovingAverages + 1] read one past the end — adjacent
			// stack garbage → wrong search boundary. The furthest filled read position is
			// readPos[kNumMovingAverages].
			search_size_boundary =
			    (uint32_t)(num_raw_samples_processed_latest - read_pos[TimeStretch::Crossfade::kNumMovingAverages])
			    & (kInputRawBufferSize - 1);
		}

		{
			int32_t search_size_here = std::min(search_size, search_size_boundary);
			// guard against potential infinite do/while loop below
			if (search_size_here <= 0) {
				goto searchNextDirection;
			}
			end_offset = search_size_here * search_direction;
		}

		do {

			for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages + 1; i++) {

				int32_t read_value = live_input_buffer->raw_buffer[read_pos[i] * num_channels] >> 16;
				if (num_channels == 2) {
					read_value += live_input_buffer->raw_buffer[read_pos[i] * 2 + 1] >> 16;
				}

				read_pos[i] = (uint32_t)(read_pos[i] + search_direction) & (kInputRawBufferSize - 1);

				if (i < TimeStretch::Crossfade::kNumMovingAverages) {
					new_head_running_totals[i] -= read_value * search_direction;
				}

				if (i > 0) {
					new_head_running_totals[i - 1] += read_value * search_direction;
				}
			}

			int32_t difference_abs = deluge::dsp::fixed::totalDifferenceAbs(old_head_totals, new_head_running_totals);

			// If our very first read is worse, let's switch search direction right now - that'll improve our odds
			if (offset_now == 0 && search_direction == 1 && !num_full_directions_searched
			    && difference_abs > best_difference_abs) {
				++counts.early_flips;
				search_direction = -search_direction;
				goto startSearch;
			}

			int32_t new_offset_now = offset_now + search_direction;

			// Keep track of best match
			bool this_offset_is_best_match = (difference_abs < best_difference_abs);
			if (this_offset_is_best_match) {
				best_difference_abs = difference_abs;
				best_offset = new_offset_now;
			}

			int32_t this_total_change = deluge::dsp::fixed::totalChange(old_head_totals, new_head_running_totals);

			// If sign just flipped...
			if ((this_total_change >= 0) != (last_total_change >= 0)) {

				// Try going in between the samples for the most accurate positioning, lining-up-wise.
				// The benefit of this is visible on a spectrum analysis if you're pitching a high-pitched sine wave
				// right down, while also time stretching it
				if (phase_increment != kMaxSampleValue
				    && (this_offset_is_best_match || offset_now == best_offset)) { // If best was this one or last one
					uint32_t this_total_difference_abs = std::abs(this_total_change);
					uint32_t last_total_difference_abs = std::abs(last_total_change);
					++counts.sub_sample_offsets;
					additional_osc_pos = ((uint64_t)last_total_difference_abs << 24)
					                     / (uint32_t)(last_total_difference_abs + this_total_difference_abs);
					if (search_direction == -1) {
						additional_osc_pos = kMaxSampleValue - additional_osc_pos;
					}
					if (this_offset_is_best_match != (search_direction == -1)) {
						best_offset--;
					}
				}

				// After sign has flipped twice (in total, including both search directions), we can be fairly sure
				// we've found a good fit
				times_sign_flipped++;
				++counts.sign_flips;
				if (times_sign_flipped >= 2) {
					goto stopSearch;
				}
			}

			offset_now = new_offset_now;
			last_total_change = this_total_change;
		} while (offset_now != end_offset);

searchNextDirection:
		// Search the other direction if we haven't already
		num_full_directions_searched++;
		if (num_full_directions_searched < 2) {
			search_direction = -search_direction;
			goto startSearch;
		}
	}

stopSearch:

	additional_osc_pos += play_heads[PLAY_HEAD_OLDER].osc_pos;
	if (additional_osc_pos >= kMaxSampleValue) {
		additional_osc_pos -= kMaxSampleValue;
		best_offset++;
	}

	play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos =
	    (uint32_t)(play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos + best_offset) & (kInputRawBufferSize - 1);

	// If still here, use raw direct buffer - usually because we're pitching down

	if (phase_increment == kMaxSampleValue) {
		play_heads[PLAY_HEAD_NEWER].mode = PlayHeadMode::RAW_DIRECT;
		play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos =
		    num_raw_samples_processed_at_now_time & (kInputRawBufferSize - 1);
	}

	else {
		play_heads[PLAY_HEAD_NEWER].mode = PlayHeadMode::RAW_REPITCHING;

		// memset(playHeads[PLAY_HEAD_NEWER].interpolationBuffer, 0,
		// sizeof(playHeads[PLAY_HEAD_NEWER].interpolationBuffer));
		play_heads[PLAY_HEAD_NEWER].fill_interpolation_buffer(live_input_buffer, num_channels);
		play_heads[PLAY_HEAD_NEWER].osc_pos = additional_osc_pos;
	}

	play_heads[PLAY_HEAD_NEWER].perc_pos = 0xFFFFFFFF;

	if (this_crossfade_length) {
		crossfade_progress = 0;
		crossfade_increment = (kMaxSampleValue - 1) / this_crossfade_length + 1;
	}
	else {
		crossfade_progress = kMaxSampleValue;
	}

	/*
	if (phaseIncrement > kMaxSampleValue) {
	    uint64_t totalPlayableSamples = ((uint64_t)howFarBack << 24) / (uint32_t)(phaseIncrement - kMaxSampleValue);
	    totalPlayableSamples = std::max(totalPlayableSamples, (uint64_t)2);

	    totalPlayableSamples = std::min(totalPlayableSamples, (uint64_t)16384);

	    if (nextCrossfadeLength > totalPlayableSamples - 1) nextCrossfadeLength = totalPlayableSamples - 1;

	    samplesTilHopEnd = totalPlayableSamples - nextCrossfadeLength;
	}
	*/

	consider_repitched_buffer(phase_increment);

	samples_into_hop = 0;
}

inline void ReferenceLivePitchShifter::consider_repitched_buffer(int32_t /*phaseIncrement*/) {
}

inline bool ReferenceLivePitchShifter::older_play_head_is_currently_sounding() {
	return (crossfade_progress < kMaxSampleValue);
}

inline bool ReferenceLivePitchShifter::may_be_removed_without_click() {
	return (!older_play_head_is_currently_sounding() && play_heads[PLAY_HEAD_NEWER].mode == PlayHeadMode::RAW_DIRECT);
}

} // namespace deluge::dsp::test::reference_pitch_shift
// NOLINTEND
