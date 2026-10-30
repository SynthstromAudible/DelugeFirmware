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
/// @brief Test-local reference for the effects the bitcrush, EQ, send-and-volume, duck, click and monitoring kernels
/// replace, copied line for line. The kernels are held bit-exact to it.
///
/// The copies are, in order: `effects::Bitcrush` (`engines/effects/bitcrush.{h,cpp}`), `effects::Equalizer`
/// (`engines/effects/equalizer.{h,cpp}`), `effects::SendAndVolume` (`engines/effects/send_and_volume.{h,cpp}`), the
/// clip chain's duck level (`ClipChain::begin_block`), the reverb return's duck level (`AudioEngine::renderReverb`),
/// the metronome's render (`processing/metronome/metronome.cpp`), the output stage's monitoring fold (the output loop
/// in `AudioEngine`) and an audio track's monitoring loop (`AudioOutput::render`).
///
/// They differ from the code only as follows:
///  - the metronome's render takes its volumes as arguments where the app reads the song's param and the menu's
///    volume, and the class keeps only the fields the render reads;
///  - the output stage's fold adds into two ints where the app adds into its loop's locals, and leaves the ring advance
///    to the caller;
///  - the track loop takes its ring end and length as arguments where the app reads the engine's buffers, its input
///    channel enum is declared here, and its `output_sample += …` are written per channel, the same sums, so they fall
///    inside the function's own body where UBSan's overflow check is off (StereoSample's operator+= is checked);
///  - every function holding a plain signed sum or shift that can overflow at full scale is marked
///    @ref DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/curves/exp_curve.h"
#include "dsp-next/fixed/vca/pan.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#ifndef DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC
/// @brief Marks a reference function whose legacy signed sums or shifts may overflow at full scale: UBSan's checks are
/// off in its body (and only there), wherever it is inlined.
#define DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC __attribute__((no_sanitize("signed-integer-overflow", "shift")))
#endif

namespace reference {

using deluge::dsp::StereoSample;

// ---- effects::Bitcrush ----

struct BitcrushParams {
	int32_t bitcrush{std::numeric_limits<int32_t>::min()};
	int32_t sample_rate_reduction{std::numeric_limits<int32_t>::min()};
};

namespace bitcrush_detail {
constexpr uint32_t kOne = 4194304;
constexpr uint32_t kBelowOne = kOne - 1;
[[nodiscard]] inline int32_t masked(int32_t value, uint32_t mask) {
	return static_cast<int32_t>(static_cast<uint32_t>(value) & mask);
}
} // namespace bitcrush_detail

class Bitcrush {
public:
	DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC void process(std::span<StereoSample> block, const BitcrushParams& params,
	                                                        int32_t& post_fx_volume) {
		using bitcrush_detail::kBelowOne;
		using bitcrush_detail::kOne;
		using bitcrush_detail::masked;
		uint32_t mask_for_srr = 0xFFFFFFFFu;
		const bool srr_enabled = params.sample_rate_reduction != std::numeric_limits<int32_t>::min();

		if (params.bitcrush >= -2113929216) {
			const auto preset = static_cast<uint32_t>((static_cast<int64_t>(params.bitcrush) + 2147483648LL) >> 29);
			if (preset > 4) {
				post_fx_volume >>= (preset - 4);
			}
			if (srr_enabled) {
				mask_for_srr = 0xFFFFFFFFu << (18 + preset);
			}
			else {
				const uint32_t mask = 0xFFFFFFFFu << (19 + preset);
				for (StereoSample& sample : block) {
					sample.l = masked(sample.l, mask);
					sample.r = masked(sample.r, mask);
				}
			}
		}

		if (!srr_enabled) {
			srr_on_last_time_ = false;
			return;
		}

		if (!srr_on_last_time_) {
			srr_on_last_time_ = true;
			last_sample_.l = last_sample_.r = 0;
			grabbed_.l = grabbed_.r = 0;
			low_rate_pos_ = 0;
		}

		const uint32_t preset = static_cast<uint32_t>(params.sample_rate_reduction) + 2147483648u;
		const int32_t low_rate_increment =
		    deluge::dsp::fixed::exp_curve(static_cast<int32_t>(kOne), static_cast<int32_t>(preset >> 3));
		const auto high_rate_increment =
		    static_cast<int32_t>((0xFFFFFFFFu / static_cast<uint32_t>(low_rate_increment >> 6)) << 6);

		for (StereoSample& sample : block) {
			if (low_rate_pos_ < kOne) {
				const auto toward_this = static_cast<int32_t>(low_rate_pos_);
				const int32_t toward_last = static_cast<int32_t>(kBelowOne) - toward_this;

				last_grabbed_ = grabbed_;
				grabbed_.l = multiply_32x32_rshift32_rounded(last_sample_.l, toward_last << 9)
				             + multiply_32x32_rshift32_rounded(sample.l, toward_this << 9);
				grabbed_.r = multiply_32x32_rshift32_rounded(last_sample_.r, toward_last << 9)
				             + multiply_32x32_rshift32_rounded(sample.r, toward_this << 9);
				grabbed_.l = masked(grabbed_.l, mask_for_srr);
				grabbed_.r = masked(grabbed_.r, mask_for_srr);

				low_rate_pos_ += static_cast<uint32_t>(low_rate_increment);

				high_rate_pos_ =
				    static_cast<uint32_t>(multiply_32x32_rshift32_rounded(
				                              static_cast<int32_t>(low_rate_pos_ & kBelowOne), high_rate_increment << 8)
				                          << 2);
			}
			low_rate_pos_ -= kOne;
			last_sample_ = sample;

			const auto toward_grabbed = static_cast<int32_t>(std::min(high_rate_pos_, kBelowOne));
			const int32_t toward_last_grabbed = static_cast<int32_t>(kBelowOne) - toward_grabbed;
			sample.l = (multiply_32x32_rshift32_rounded(last_grabbed_.l, toward_last_grabbed << 9)
			            + multiply_32x32_rshift32_rounded(grabbed_.l, toward_grabbed << 9))
			           << 2;
			sample.r = (multiply_32x32_rshift32_rounded(last_grabbed_.r, toward_last_grabbed << 9)
			            + multiply_32x32_rshift32_rounded(grabbed_.r, toward_grabbed << 9))
			           << 2;

			high_rate_pos_ += static_cast<uint32_t>(high_rate_increment);
		}
	}

private:
	bool srr_on_last_time_{false};
	uint32_t low_rate_pos_{0};
	uint32_t high_rate_pos_{0};
	StereoSample last_sample_{};
	StereoSample grabbed_{};
	StereoSample last_grabbed_{};
};

// ---- effects::Equalizer ----

struct EqualizerParams {
	int32_t bass{0};
	int32_t treble{0};
	int32_t bass_frequency{0};
	int32_t treble_frequency{0};
};

class Equalizer {
public:
	DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC void process(std::span<StereoSample> block,
	                                                        const EqualizerParams& params) {
		const bool do_bass = params.bass != 0;
		const bool do_treble = params.treble != 0;
		if (!do_bass && !do_treble) {
			return;
		}

		int32_t positive = (params.bass >> 1) + 1073741824;
		const int32_t bass_gain = (multiply_32x32_rshift32_rounded(positive, positive) << 1) - 536870912;
		positive = (params.treble >> 1) + 1073741824;
		const int32_t treble_gain = multiply_32x32_rshift32_rounded(positive, positive) << 1;

		if (do_bass) {
			bass_coefficient_ = deluge::dsp::fixed::exp_curve(120000000, (params.bass_frequency >> 5) * 6);
		}
		if (do_treble) {
			treble_coefficient_ = deluge::dsp::fixed::exp_curve(700000000, (params.treble_frequency >> 5) * 6);
		}

		for (StereoSample& sample : block) {
			int32_t treble_l = 0;
			int32_t treble_r = 0;
			if (do_treble) {
				without_treble_.l += multiply_32x32_rshift32(sample.l - without_treble_.l, treble_coefficient_) << 1;
				without_treble_.r += multiply_32x32_rshift32(sample.r - without_treble_.r, treble_coefficient_) << 1;
				treble_l = sample.l - without_treble_.l;
				treble_r = sample.r - without_treble_.r;
				sample.l = without_treble_.l;
				sample.r = without_treble_.r;
			}
			if (do_bass) {
				bass_only_.l += multiply_32x32_rshift32(sample.l - bass_only_.l, bass_coefficient_);
				bass_only_.r += multiply_32x32_rshift32(sample.r - bass_only_.r, bass_coefficient_);
			}
			if (do_treble) {
				sample.l += multiply_32x32_rshift32(treble_l, treble_gain) << 3;
				sample.r += multiply_32x32_rshift32(treble_r, treble_gain) << 3;
			}
			if (do_bass) {
				sample.l += multiply_32x32_rshift32(bass_only_.l, bass_gain) << 3;
				sample.r += multiply_32x32_rshift32(bass_only_.r, bass_gain) << 3;
			}
		}
	}

	void copy_coefficients_from(const Equalizer& other) {
		bass_coefficient_ = other.bass_coefficient_;
		treble_coefficient_ = other.treble_coefficient_;
	}

private:
	int32_t bass_coefficient_{0};
	int32_t treble_coefficient_{0};
	StereoSample without_treble_{};
	StereoSample bass_only_{};
};

// ---- effects::SendAndVolume ----

struct SendAndVolumeParams {
	int32_t post_fx_volume{0};
	int32_t post_reverb_volume{0};
	int32_t reverb_send{0};
	int32_t pan{0};
	bool ramp{false};
	bool stereo{true};
};

class SendAndVolume {
public:
	constexpr explicit SendAndVolume(int32_t last_post_reverb_volume = 0)
	    : last_post_reverb_volume_{last_post_reverb_volume} {}

	DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC [[nodiscard]] bool
	process(std::span<StereoSample> block, std::span<int32_t> reverb, const SendAndVolumeParams& params) {
		const int32_t send_gain = multiply_32x32_rshift32(params.post_fx_volume, params.reverb_send) << 5;

		int32_t gain_l = multiply_32x32_rshift32(params.post_reverb_volume, params.post_fx_volume) << 5;
		int32_t gain_r = gain_l;

		int32_t step_l = 0;
		int32_t step_r = 0;
		if (params.ramp) {
			const auto volume_step =
			    static_cast<int32_t>(static_cast<double>(params.post_reverb_volume - last_post_reverb_volume_)
			                         / static_cast<double>(block.size()));
			step_l = step_r = multiply_32x32_rshift32(params.post_fx_volume, volume_step) << 5;
		}

		if (params.pan != 0 && params.stereo) {
			const deluge::dsp::fixed::PanAmplitudes pan = deluge::dsp::fixed::pan_amplitudes(params.pan);
			gain_l = multiply_32x32_rshift32(gain_l, pan.left) << 2;
			gain_r = multiply_32x32_rshift32(gain_r, pan.right) << 2;
			step_l = multiply_32x32_rshift32(step_l, pan.left) << 2;
			step_r = multiply_32x32_rshift32(step_r, pan.right) << 2;
		}

		auto reverb_out = reverb.begin();
		for (StereoSample& sample : block) {
			if (params.reverb_send != 0) {
				*(reverb_out++) += multiply_32x32_rshift32(sample.l + sample.r, send_gain) << 1;
			}
			if (params.ramp) {
				gain_l += step_l;
				gain_r += step_r;
			}
			sample.l = multiply_32x32_rshift32(sample.l, gain_l) << 5;
			sample.r = multiply_32x32_rshift32(sample.r, gain_r) << 5;
		}

		last_post_reverb_volume_ = params.post_reverb_volume;
		return params.reverb_send != 0;
	}

	void prime(int32_t post_reverb_volume) {
		if (last_post_reverb_volume_ == -1) {
			last_post_reverb_volume_ = post_reverb_volume;
		}
	}

	void forget() { last_post_reverb_volume_ = -1; }

private:
	int32_t last_post_reverb_volume_;
};

// ---- the two duck levels ----

/// ClipChain::begin_block's level: the square of the ducked level, scaled to 134217728 at no duck.
DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC inline int32_t clip_duck_level(int32_t duck, int32_t depth) {
	const int32_t positive = multiply_32x32_rshift32(duck, depth) + 536870912;
	return (positive >> 15) * (positive >> 16);
}

/// AudioEngine::renderReverb's return volume.
DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC inline int32_t reverb_duck_level(int32_t sidechainOutput,
                                                                            int32_t reverbSidechainVolumeInEffect) {
	int32_t positivePatchedValue = multiply_32x32_rshift32(sidechainOutput, reverbSidechainVolumeInEffect) + 0x20000000;
	int32_t reverbOutputVolume = (positivePatchedValue >> 15) * (positivePatchedValue >> 14);
	return reverbOutputVolume;
}

// ---- the metronome ----

class Click {
public:
	void trigger(uint32_t newPhaseIncrement) {
		sounding = true;
		phase = 0;
		phaseIncrement = newPhaseIncrement;
		timeSinceTrigger = 0;
	}

	DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC void render(std::span<StereoSample> buffer, uint32_t metronomeVolume,
	                                                       int32_t volumePostFX) {
		if (!sounding) {
			return;
		}

		q31_t high = multiply_32x32_rshift32(metronomeVolume, volumePostFX);

		for (StereoSample& sample : buffer) {
			q31_t value = (phase <= ONE_Q31) ? high : -high;
			phase += phaseIncrement;

			sample.l += value;
			sample.r += value;
		}

		timeSinceTrigger += buffer.size();
		if (timeSinceTrigger > 1000) {
			sounding = false;
		}
	}

	/// The metronome's setVolume.
	static uint32_t volume(int32_t linearParam) { return (exp(float(linearParam) / 200.0f) - 1.0) * float(1 << 27); }

	uint32_t phase{};
	uint32_t phaseIncrement{};
	uint32_t timeSinceTrigger{};
	bool sounding{false};
};

// ---- monitoring ----

enum class MonitoringAction { NONE, SUBTRACT_RIGHT_CHANNEL, REMOVE_RIGHT_CHANNEL };

/// The output loop's monitoring fold of one ring frame, AUDIO_OUTPUT_GAIN_DOUBLINGS as @p shift.
DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC inline void output_monitor(MonitoringAction monitoringAction,
                                                                      const int32_t* inputReadPos, int32_t& lAdjusted,
                                                                      int32_t& rAdjusted, int shift) {
	if (monitoringAction == MonitoringAction::SUBTRACT_RIGHT_CHANNEL) {
		int32_t value = (inputReadPos[0] >> (shift + 1)) - (inputReadPos[1] >> (shift));
		lAdjusted += value;
		rAdjusted += value;
	}

	else {
		lAdjusted += inputReadPos[0] >> (shift);

		if (monitoringAction == MonitoringAction::NONE) {
			rAdjusted += inputReadPos[1] >> (shift);
		}
		else { // Remove right channel
			rAdjusted += inputReadPos[0] >> (shift);
		}
	}
}

enum class AudioInputChannel { NONE, LEFT, RIGHT, STEREO, BALANCED };

/// AudioOutput::render's monitoring loop over a ring of @p ring_frames stereo frames ending at @p input_end, starting
/// at @p input_ptr.
DELUGE_REFERENCE_EFFECTS_LEGACY_ARITHMETIC inline void
track_monitor(AudioInputChannel input_channel, std::span<StereoSample> output, const int32_t* input_ptr,
              const int32_t* input_end, int32_t ring_frames, int32_t amplitude_start, int32_t amplitude_end) {
	auto amplitude_increment = static_cast<int32_t>((static_cast<double>(amplitude_end - amplitude_start) //
	                                                 / static_cast<double>(output.size())));
	int32_t amplitude = amplitude_start;

	for (StereoSample& output_sample : output) {
		amplitude += amplitude_increment;

		StereoSample input = {
		    .l = multiply_32x32_rshift32(input_ptr[0], amplitude) << 2,
		    .r = multiply_32x32_rshift32(input_ptr[1], amplitude) << 2,
		};

		switch (input_channel) {
		case AudioInputChannel::LEFT: {
			const q31_t mono = input.l;
			output_sample.l += mono;
			output_sample.r += mono;
		} break;

		case AudioInputChannel::RIGHT: {
			const q31_t mono = input.r;
			output_sample.l += mono;
			output_sample.r += mono;
		} break;

		case AudioInputChannel::BALANCED: {
			const q31_t mono = (input.l / 2) - (input.r / 2);
			output_sample.l += mono;
			output_sample.r += mono;
		} break;

		case AudioInputChannel::NONE: // Means combine channels
		{
			const q31_t mono = (input.l / 2) + (input.r / 2);
			output_sample.l += mono;
			output_sample.r += mono;
		} break;

		default: // STEREO
			output_sample.l += input.l;
			output_sample.r += input.r;
			break;
		}

		input_ptr += 2;
		if (input_ptr >= input_end) {
			input_ptr -= ring_frames * 2;
		}
	}
}

} // namespace reference
