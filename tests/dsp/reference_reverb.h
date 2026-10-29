/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
 * Freeverb written by Jezar at Dreampoint, June 2000 (public domain).
 * Mutable and Digital heavily based on Mutable Instruments' code, copyright 2014 Emilie Gillet (MIT License).
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
/// @brief Test-local reference for the app's three reverb models (Freeverb, Mutable and Digital) and the reverb that
/// switches between them: `dsp/reverb/`, copied line for line. The kernels are held bit-exact to it.
///
/// The copies are, in order: the Freeverb tuning constants (`freeverb/tuning.h`), `Comb` (`freeverb/comb.hpp`),
/// `Allpass` (`freeverb/allpass.hpp`), @ref deluge::dsp::test::reverb_ref::ReferenceFreeverb (`freeverb/freeverb.hpp`
/// and `freeverb/freeverb.cpp`), `toQ31Saturating` (`dsp/util.hpp`), `OnePole`, `Interpolate` and `FxEngine`
/// (`fx_engine.hpp`), `DualCosineOscillator` (`cosine_oscillator.hpp`), `calcFilterCutoff` (`base.hpp`),
/// @ref deluge::dsp::test::reverb_ref::ReferenceMutable (`mutable.hpp`),
/// @ref deluge::dsp::test::reverb_ref::ReferenceDigital (`digital.hpp`) and
/// @ref deluge::dsp::test::reverb_ref::ReferenceReverb (`reverb.hpp`).
///
/// They differ from the code only as follows:
///  - there is no virtual `Base`: each model holds `amplitude_left_` and `amplitude_right_` with `setPanLevels`,
///    `getPanLeft` and `getPanRight`, and keeps the base's getters and setters as plain members; the reverb reaches the
///    model it holds through `std::visit` where the app goes through its `Base*`;
///  - Freeverb's no-op knob setters (`setHPF`, `setLPF`) are plain no-ops, and their getters return the base's 0;
///  - every function holding a plain signed sum that can overflow at full scale is marked
///    @ref DELUGE_REFERENCE_LEGACY_ARITHMETIC: `Comb::process`, `Allpass::process`, `ProcessOne`, Freeverb's `process`
///    (the DC loop) and both plates' `process` (the q31 mix);
///  - `util/functions.h` is not included: the copies use only `util/fixedpoint.h`'s multiplies, so the header builds
///    without the libdeluge include path;
///  - names the app qualifies with `dsp::` (`OnePole`, `Interpolate`, `toQ31Saturating`) are qualified with
///    `reverb_ref::`, and `util::map` with `::util::`, so each resolves to the same function from this namespace;
///    `FxEngine` drops its `DELUGE_DSP_BOUNDS_CHECK` blocks;
///  - each model has a `view()` returning its whole state for the digest, and read-only accessors (the coefficients,
///    the lines and their indices, the LFO's vectors, Digital's line topology);
///  - path counters (@ref deluge::dsp::test::counts) are incremented at each path's site, beside the copied lines and
///    never in them.

#pragma once

#include "argon.hpp"
#include "definitions_cxx.hpp"
#include "reverb_views.h"
#include "reverb_workloads.h"
#include "util/fixedpoint.h"
#include "util/misc.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <type_traits>
#include <variant>

#ifndef DELUGE_REFERENCE_LEGACY_ARITHMETIC
/// @brief Marks a reference function whose legacy signed sums may overflow at full scale: UBSan's overflow check is
/// off in its body (and only there), wherever it is inlined.
#define DELUGE_REFERENCE_LEGACY_ARITHMETIC __attribute__((no_sanitize("signed-integer-overflow")))
#endif

namespace deluge::dsp::test {

/// @brief How often the reference took each path the workloads must reach. A spec zeroes it, runs the workloads, and
/// reads it.
struct ReverbCounts {
	std::size_t freeverb_blocks = 0;
	/// A Freeverb block whose cross-mix runs (wet2 != 0).
	std::size_t freeverb_wet2_blocks = 0;
	/// A frame whose comb outputs, summed in int64, leave int32: the int32 sum wraps.
	std::size_t freeverb_comb_sum_wrapped = 0;
	/// An output channel whose `+=` wraps.
	std::size_t freeverb_output_wrapped = 0;
	/// A Freeverb block ending with the DC blocker's memory away from 0.
	std::size_t freeverb_dc_nonzero = 0;
	std::size_t mutable_blocks = 0;
	std::size_t digital_blocks = 0;
	/// The LFO's steps (one every 32 plate frames).
	std::size_t lfo_steps = 0;
	/// Completed laps of a plate's ring: a wrap of the write index at least 32768 frames after the previous one.
	std::size_t ring_laps = 0;
	/// Plate frames since the write index last wrapped.
	std::size_t frames_since_wrap = 0;
	/// toQ31Saturating clamps.
	std::size_t saturated_high = 0;
	std::size_t saturated_low = 0;
	/// A plate block with its output high-pass or low-pass away from open (hpf != 0 or lpf != 1).
	std::size_t plate_filtered_blocks = 0;
	std::size_t model_changes = 0;
};

/// @brief The reference's path counters.
inline ReverbCounts counts;

} // namespace deluge::dsp::test

namespace deluge::dsp::test::reverb_ref {

using deluge::dsp::StereoSample;

// ---- freeverb/tuning.h ----

constexpr int32_t numcombs = 8;
constexpr int32_t numallpasses = 4;
constexpr float muted = 0;
constexpr float fixedgain = 0.015f;
constexpr float scalewet = 3;
constexpr float scaledry = 2;
constexpr float scaledamp = 0.4f;
constexpr float scaleroom = 0.28f;
constexpr float offsetroom = 0.7f;
constexpr float initialroom = 0.5f;
constexpr float initialdamp = 0.5f;
constexpr float initialwet = 1 / scalewet;
constexpr float initialdry = 0;
constexpr float initialwidth = 1;
constexpr int32_t stereospread = 23;

// These values assume 44.1KHz sample rate
// they will probably be OK for 48KHz sample rate
// but would need scaling for 96KHz (or other) sample rates.
// The values were obtained by listening tests.
constexpr int32_t combtuningL1 = 1116;
constexpr int32_t combtuningR1 = 1116 + stereospread;
constexpr int32_t combtuningL2 = 1188;
constexpr int32_t combtuningR2 = 1188 + stereospread;
constexpr int32_t combtuningL3 = 1277;
constexpr int32_t combtuningR3 = 1277 + stereospread;
constexpr int32_t combtuningL4 = 1356;
constexpr int32_t combtuningR4 = 1356 + stereospread;
constexpr int32_t combtuningL5 = 1422;
constexpr int32_t combtuningR5 = 1422 + stereospread;
constexpr int32_t combtuningL6 = 1491;
constexpr int32_t combtuningR6 = 1491 + stereospread;
constexpr int32_t combtuningL7 = 1557;
constexpr int32_t combtuningR7 = 1557 + stereospread;
constexpr int32_t combtuningL8 = 1617;
constexpr int32_t combtuningR8 = 1617 + stereospread;
constexpr int32_t allpasstuningL1 = 556;
constexpr int32_t allpasstuningR1 = 556 + stereospread;
constexpr int32_t allpasstuningL2 = 441;
constexpr int32_t allpasstuningR2 = 441 + stereospread;
constexpr int32_t allpasstuningL3 = 341;
constexpr int32_t allpasstuningR3 = 341 + stereospread;
constexpr int32_t allpasstuningL4 = 225;
constexpr int32_t allpasstuningR4 = 225 + stereospread;

// ---- freeverb/comb.hpp ----

class Comb {
public:
	Comb() = default;
	constexpr void setBuffer(std::span<int32_t> buffer) { buffer_ = buffer; }

	constexpr void mute() { std::fill(buffer_.begin(), buffer_.end(), 0); }

	constexpr void setDamp(float val) {
		damp1_ = val * std::numeric_limits<int32_t>::max();
		damp2_ = std::numeric_limits<int32_t>::max() - damp1_;
	}

	[[nodiscard]] constexpr float getDamp() const { return damp1_ / std::numeric_limits<int32_t>::max(); }

	constexpr void setFeedback(int32_t val) { feedback_ = val; }

	[[nodiscard]] constexpr int32_t getFeedback() const { return feedback_; }

	// Big to inline - but crucial for speed
	DELUGE_REFERENCE_LEGACY_ARITHMETIC inline int32_t process(int32_t input) {
		int32_t output = buffer_[bufidx_];

		filterstore_ = (multiply_32x32_rshift32_rounded(output, damp2_) + //<
		                multiply_32x32_rshift32_rounded(filterstore_, damp1_))
		               << 1;

		buffer_[bufidx_] = input + (multiply_32x32_rshift32_rounded(filterstore_, feedback_) << 1);

		if (++bufidx_ >= buffer_.size()) {
			bufidx_ = 0;
		}

		return output;
	}

	[[nodiscard]] int32_t damp1() const { return damp1_; }
	[[nodiscard]] int32_t damp2() const { return damp2_; }
	/// @return What the next process returns: the word at the index.
	[[nodiscard]] int32_t next_output() const { return buffer_[bufidx_]; }
	[[nodiscard]] std::span<const int32_t> buffer() const { return buffer_; }
	[[nodiscard]] int32_t index() const { return bufidx_; }
	[[nodiscard]] int32_t store() const { return filterstore_; }

private:
	int32_t feedback_{};
	int32_t filterstore_{0};
	int32_t damp1_{};
	int32_t damp2_{};
	std::span<int32_t> buffer_{};
	int32_t bufidx_{0};
};

// ---- freeverb/allpass.hpp ----

class Allpass {
public:
	Allpass() = default;

	constexpr void setBuffer(std::span<int32_t> buffer) { buffer_ = buffer; }

	constexpr void mute() { std::fill(buffer_.begin(), buffer_.end(), 0); }

	constexpr void setFeedback(float val) { feedback_ = val * std::numeric_limits<int32_t>::max(); }

	[[nodiscard]] constexpr float getFeedback() const { return (float)feedback_ / std::numeric_limits<int32_t>::max(); }

	[[gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC constexpr int32_t process(int32_t input) {
		int32_t bufout = buffer_[bufidx_];
		int32_t output = -input + bufout;

		buffer_[bufidx_] = input + (bufout >> 1); // Shortcut - because feedback was always one half by default anyway
		// buffer[bufidx] = input + (multiply_32x32_rshift32_rounded(bufout, feedback) << 1);

		if (++bufidx_ >= buffer_.size()) {
			bufidx_ = 0;
		}

		return output;
	}

	[[nodiscard]] std::span<const int32_t> buffer() const { return buffer_; }
	[[nodiscard]] int32_t index() const { return bufidx_; }

private:
	int32_t feedback_{};
	std::span<int32_t> buffer_{};
	int32_t bufidx_{0};
};

// ---- freeverb/freeverb.hpp, freeverb/freeverb.cpp ----

class ReferenceFreeverb {
public:
	ReferenceFreeverb() {
		// Tie the components to their buffers
		combL[0].setBuffer(bufcombL1);
		combR[0].setBuffer(bufcombR1);
		combL[1].setBuffer(bufcombL2);
		combR[1].setBuffer(bufcombR2);
		combL[2].setBuffer(bufcombL3);
		combR[2].setBuffer(bufcombR3);
		combL[3].setBuffer(bufcombL4);
		combR[3].setBuffer(bufcombR4);
		combL[4].setBuffer(bufcombL5);
		combR[4].setBuffer(bufcombR5);
		combL[5].setBuffer(bufcombL6);
		combR[5].setBuffer(bufcombR6);
		combL[6].setBuffer(bufcombL7);
		combR[6].setBuffer(bufcombR7);
		combL[7].setBuffer(bufcombL8);
		combR[7].setBuffer(bufcombR8);
		allpassL[0].setBuffer(bufallpassL1);
		allpassR[0].setBuffer(bufallpassR1);
		allpassL[1].setBuffer(bufallpassL2);
		allpassR[1].setBuffer(bufallpassR2);
		allpassL[2].setBuffer(bufallpassL3);
		allpassR[2].setBuffer(bufallpassR3);
		allpassL[3].setBuffer(bufallpassL4);
		allpassR[3].setBuffer(bufallpassR4);

		// Set default values
		allpassL[0].setFeedback(0.5f);
		allpassR[0].setFeedback(0.5f);
		allpassL[1].setFeedback(0.5f);
		allpassR[1].setFeedback(0.5f);
		allpassL[2].setFeedback(0.5f);
		allpassR[2].setFeedback(0.5f);
		allpassL[3].setFeedback(0.5f);
		allpassR[3].setFeedback(0.5f);
		setWet(initialwet);
		setRoomSize(initialroom);
		setDry(initialdry);
		setDamping(initialdamp);
		setWidth(initialwidth);

		// Buffer will be full of rubbish - so we MUST mute them
		mute();
	}

	void mute() {
		for (int32_t i = 0; i < numcombs; i++) {
			combL[i].mute();
			combR[i].mute();
		}
		for (int32_t i = 0; i < numallpasses; i++) {
			allpassL[i].mute();
			allpassR[i].mute();
		}
	}

	void setRoomSize(float value) {
		roomsize = (value * scaleroom) + offsetroom;
		update();
	}

	[[nodiscard]] constexpr float getRoomSize() const { return (roomsize - offsetroom) / scaleroom; }

	void setDamping(float value) {
		damp = value * scaledamp;
		update();
	}

	[[nodiscard]] constexpr float getDamping() const { return damp / scaledamp; }

	void setWet(float value) {
		wet = value * scalewet;
		update();
	}

	[[nodiscard]] constexpr float getWet() const { return wet / scalewet; }

	void setDry(float value) { dry = value * scaledry; }

	[[nodiscard]] constexpr float getDry() const { return dry / scaledry; }

	void setWidth(float value) {
		width = value;
		update();
	}

	[[nodiscard]] constexpr float getWidth() const { return width; }

	// The base's no-op knobs.
	void setHPF(float /*f*/) {}
	[[nodiscard]] float getHPF() const { return 0; }
	void setLPF(float /*f*/) {}
	[[nodiscard]] float getLPF() const { return 0; }

	// The base's pan levels.
	constexpr void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		amplitude_right_ = amplitude_right;
		amplitude_left_ = amplitude_left;
	}
	[[nodiscard]] constexpr int32_t getPanLeft() const { return amplitude_left_; }
	[[nodiscard]] constexpr int32_t getPanRight() const { return amplitude_right_; }

	[[gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC void ProcessOne(int32_t input,
	                                                                          StereoSample& output_sample) {
		int64_t comb_sum_l = 0;
		int64_t comb_sum_r = 0;
		for (int32_t i = 0; i < numcombs; i++) {
			comb_sum_l += combL[i].next_output();
			comb_sum_r += combR[i].next_output();
		}
		if (comb_sum_l != static_cast<int32_t>(comb_sum_l) || comb_sum_r != static_cast<int32_t>(comb_sum_r)) {
			++counts.freeverb_comb_sum_wrapped;
		}

		int32_t out_l = 0;
		int32_t out_r = 0;

		// Accumulate comb filters in parallel
		for (int32_t i = 0; i < numcombs; i++) {
			out_l += combL[i].process(input);
			out_r += combR[i].process(input);
		}

		// Feed through allpasses in series
		for (int32_t i = 0; i < numallpasses; i++) {
			out_l = allpassL[i].process(out_l);
			out_r = allpassR[i].process(out_r);
		}

		// Calculate output
		out_l = (out_l + multiply_32x32_rshift32_rounded(out_r, wet2)) << 1;
		out_r = (out_r + multiply_32x32_rshift32_rounded(out_l, wet2)) << 1;

		const int64_t mixed_l = int64_t{output_sample.l} + multiply_32x32_rshift32_rounded(out_l, this->getPanLeft());
		const int64_t mixed_r = int64_t{output_sample.r} + multiply_32x32_rshift32_rounded(out_r, this->getPanRight());
		counts.freeverb_output_wrapped += static_cast<std::size_t>(mixed_l != static_cast<int32_t>(mixed_l))
		                                  + static_cast<std::size_t>(mixed_r != static_cast<int32_t>(mixed_r));

		// Mix output
		output_sample.l += multiply_32x32_rshift32_rounded(out_l, this->getPanLeft());
		output_sample.r += multiply_32x32_rshift32_rounded(out_r, this->getPanRight());
	}

	[[gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC void process(std::span<int32_t> input,
	                                                                       std::span<StereoSample> output) {
		++counts.freeverb_blocks;
		if (wet2 != 0) {
			++counts.freeverb_wet2_blocks;
		}

		// HPF on reverb input, cos if it has DC offset, the reverb magnifies that, and the sound farts out
		for (int32_t& reverb_sample : input) {
			int32_t distance_to_go_l = reverb_sample - reverb_send_post_lpf_;
			reverb_send_post_lpf_ += distance_to_go_l >> 11;
			reverb_sample -= reverb_send_post_lpf_;
		}

		for (size_t frame = 0; frame < input.size(); frame++) {
			ProcessOne(input[frame], output[frame]);
		}

		if (reverb_send_post_lpf_ != 0) {
			++counts.freeverb_dc_nonzero;
		}
	}

	[[nodiscard]] int32_t wet2_coefficient() const { return wet2; }
	[[nodiscard]] int32_t comb_feedback() const { return combL[0].getFeedback(); }
	[[nodiscard]] int32_t comb_damp1() const { return combL[0].damp1(); }
	[[nodiscard]] int32_t comb_damp2() const { return combL[0].damp2(); }

	/// @return The whole state, in FreeverbView's canonical order.
	[[nodiscard]] FreeverbView view() const {
		FreeverbView v{};
		for (std::size_t i = 0; i < numcombs; ++i) {
			v.lines[i] = combL[i].buffer();
			v.lines[numcombs + i] = combR[i].buffer();
			v.index[i] = combL[i].index();
			v.index[numcombs + i] = combR[i].index();
			v.store[i] = combL[i].store();
			v.store[numcombs + i] = combR[i].store();
		}
		for (std::size_t i = 0; i < numallpasses; ++i) {
			v.lines[(2 * numcombs) + i] = allpassL[i].buffer();
			v.lines[(2 * numcombs) + numallpasses + i] = allpassR[i].buffer();
			v.index[(2 * numcombs) + i] = allpassL[i].index();
			v.index[(2 * numcombs) + numallpasses + i] = allpassR[i].index();
		}
		v.dc = reverb_send_post_lpf_;
		return v;
	}

private:
	void update() {
		// Recalculate internal values after parameter change

		wet1 = wet * (width / 2 + 0.5f);
		wet2 = (((float)1 - width) / 2) / (width / 2 + 0.5f) * std::numeric_limits<int32_t>::max();

		gain = fixedgain * std::numeric_limits<int32_t>::max();

		for (size_t i = 0; i < numcombs; i++) {
			combL[i].setFeedback(roomsize * std::numeric_limits<int32_t>::max());
			combR[i].setFeedback(roomsize * std::numeric_limits<int32_t>::max());
		}

		for (size_t i = 0; i < numcombs; i++) {
			combL[i].setDamp(damp);
			combR[i].setDamp(damp);
		}
	}

	int32_t gain;
	float roomsize;
	float damp;
	float wet;
	float wet1;
	int32_t wet2;
	float dry;
	float width;

	// The following are all declared inline
	// to remove the need for dynamic allocation
	// with its subsequent error-checking messiness

	// Comb filters
	std::array<Comb, numcombs> combL;
	std::array<Comb, numcombs> combR;

	// Allpass filters
	std::array<Allpass, numallpasses> allpassL;
	std::array<Allpass, numallpasses> allpassR;

	// Buffers for the combs
	std::array<int32_t, combtuningL1> bufcombL1;
	std::array<int32_t, combtuningR1> bufcombR1;
	std::array<int32_t, combtuningL2> bufcombL2;
	std::array<int32_t, combtuningR2> bufcombR2;
	std::array<int32_t, combtuningL3> bufcombL3;
	std::array<int32_t, combtuningR3> bufcombR3;
	std::array<int32_t, combtuningL4> bufcombL4;
	std::array<int32_t, combtuningR4> bufcombR4;
	std::array<int32_t, combtuningL5> bufcombL5;
	std::array<int32_t, combtuningR5> bufcombR5;
	std::array<int32_t, combtuningL6> bufcombL6;
	std::array<int32_t, combtuningR6> bufcombR6;
	std::array<int32_t, combtuningL7> bufcombL7;
	std::array<int32_t, combtuningR7> bufcombR7;
	std::array<int32_t, combtuningL8> bufcombL8;
	std::array<int32_t, combtuningR8> bufcombR8;

	// Buffers for the allpasses
	std::array<int32_t, allpasstuningL1> bufallpassL1;
	std::array<int32_t, allpasstuningR1> bufallpassR1;
	std::array<int32_t, allpasstuningL2> bufallpassL2;
	std::array<int32_t, allpasstuningR2> bufallpassR2;
	std::array<int32_t, allpasstuningL3> bufallpassL3;
	std::array<int32_t, allpasstuningR3> bufallpassR3;
	std::array<int32_t, allpasstuningL4> bufallpassL4;
	std::array<int32_t, allpasstuningR4> bufallpassR4;

	int32_t reverb_send_post_lpf_ = 0;

	int32_t amplitude_right_ = 0;
	int32_t amplitude_left_ = 0;
};

// ---- dsp/util.hpp ----

/**
 * Converts a float already scaled into q31 range, clamping rather than overflowing.
 *
 * Letting an out-of-range float reach a narrowing cast is undefined behaviour, and the two
 * targets disagree about it in the worst possible way: ARM's VCVT saturates, so an overdriven
 * tail clips to full-scale positive, while x86 yields INT32_MIN, flipping the same sample to
 * full-scale negative. Anything specced on the host would misreport what the hardware does.
 */
inline q31_t toQ31Saturating(float scaled) {
	// (float)INT32_MAX rounds up to 2^31, which is itself out of range, so the ceiling has to
	// be the largest float below it.
	constexpr float kMax = 2147483520.f; // 0x7FFFFF80
	constexpr float kMin = -2147483648.f;
	if (scaled > kMax) {
		++counts.saturated_high;
	}
	if (scaled < kMin) {
		++counts.saturated_low;
	}
	return static_cast<q31_t>(std::clamp(scaled, kMin, kMax));
}

// ---- fx_engine.hpp ----

constexpr float OnePole(float& out, float in, float coefficient) {
	out += (coefficient) * ((in)-out);
	return out;
}

template <typename T = float>
constexpr T Interpolate(const T x0, const T x1, float fractional) {
	return static_cast<T>(x0 + (x1 - x0) * fractional);
}

// ---- cosine_oscillator.hpp ----

class DualCosineOscillator {
public:
	enum class Mode { APPROX, EXACT };

	template <Mode mode = Mode::APPROX>
	constexpr DualCosineOscillator(std::array<float, 2> frequencies)
	    : frequencies_(ArgonHalf<float>::Load(frequencies.data())) {
		Init<mode>();
	}
	~DualCosineOscillator() = default;

	template <Mode mode = Mode::APPROX>
	inline void SetFrequency(int index, float frequency) {
		frequencies_[index] = frequency;
		Init<mode>();
	}

	inline void InitApproximate() {
		ArgonHalf<float> sign = 16.0f;
		ArgonHalf<float> frequencies = frequencies_;
		frequencies = frequencies.each_lane_with_index([&](float& frequency, int i) {
			frequency -= 0.25f;
			if (frequency < 0.0f) {
				frequency = -frequency;
			}
			else if (frequency > 0.5f) {
				frequency -= 0.5f;
			}
			else {
				sign[i] = -16.0f;
			}
		});
		iir_coefficient_ = sign * frequencies * (1.0f - (2.0f * frequencies));
		initial_amplitude_ = iir_coefficient_ * 0.25f;
	}

	constexpr void Start() {
		y_0 = initial_amplitude_;
		y_1 = 0.5f;
	}

	[[nodiscard]] inline ArgonHalf<float> values() const { return y_0 + 0.5f; }

	inline ArgonHalf<float> Next() {
		ArgonHalf<float> temp = y_1;
		y_1 = iir_coefficient_ * y_1 - y_0;
		y_0 = temp;
		return temp + 0.5f;
	}

	/// @return The two vectors as lanes.
	[[nodiscard]] std::array<float, 2> y_0_lanes() const {
		std::array<float, 2> lanes{};
		y_0.StoreTo(lanes.data());
		return lanes;
	}
	[[nodiscard]] std::array<float, 2> y_1_lanes() const {
		std::array<float, 2> lanes{};
		y_1.StoreTo(lanes.data());
		return lanes;
	}

private:
	template <Mode mode = Mode::APPROX>
	inline void Init() {
		if constexpr (mode == Mode::APPROX) {
			InitApproximate();
		}
		else {
			for (size_t i = 0; i < frequencies_.size(); ++i) {
				iir_coefficient_[i] = 2.0f * std::cos(2.0f * std::numbers::pi_v<float> * frequencies_[i]);
				initial_amplitude_[i] = iir_coefficient_[i] * 0.25f;
			}
		}
		Start();
	}

	ArgonHalf<float> frequencies_;
	ArgonHalf<float> y_0;
	ArgonHalf<float> y_1;
	ArgonHalf<float> iir_coefficient_;
	ArgonHalf<float> initial_amplitude_;
};

// ---- fx_engine.hpp ----

constexpr static int32_t TAIL = -1;

enum LFOIndex { LFO_1, LFO_2 };

class FxEngine {
public:
	/// The LFOs only step once every 32 frames, so a configured frequency has to be scaled up
	/// to compensate. Skip this and the normalised frequency stays small enough that the
	/// approximate cosine's `2 - 32f^2` coefficient rounds to exactly 2.0f -- at which point
	/// both lanes collapse onto one much slower oscillation and the modulation stops being
	/// two independent voices.
	constexpr static float kLFOStride = 32.0f;

	FxEngine(std::span<float> signal, std::array<float, 2> lfo_freqs)
	    : buffer_(signal), lfo_{{lfo_freqs[0] * kLFOStride, lfo_freqs[1] * kLFOStride}}, mask(buffer_.size() - 1) {};
	~FxEngine() = default;

	void Clear() {
		std::fill(buffer_.begin(), buffer_.end(), 0);
		write_ptr_ = 0;
	}

	//[gnu::always_inline]
	void SetLFOFrequency(LFOIndex index, float frequency) { lfo_.SetFrequency(index, frequency * kLFOStride); }

	//[gnu::always_inline]
	void Advance() {
		++counts.frames_since_wrap;
		--write_ptr_;
		if (write_ptr_ < 0) {
			write_ptr_ += buffer_.size();
			if (counts.frames_since_wrap >= 32768) {
				++counts.ring_laps;
			}
			counts.frames_since_wrap = 0;
		}
		StepLFO();
	}

	//[gnu::always_inline]
	float& at(size_t index) { return buffer_[(write_ptr_ + index) & mask]; }

	/// Reads the current value; stepping is Advance()'s job, so that a topology with several
	/// modulated delay lines still only advances the oscillators once per frame.
	//[gnu::always_inline]
	float LFO(LFOIndex lfo) {
		switch (lfo) {
		case LFO_1:
			return lfo_.values()[0];
		case LFO_2:
			return lfo_.values()[1];
		}
		__builtin_unreachable();
	}

	[[nodiscard]] int32_t write_ptr() const { return write_ptr_; }
	[[nodiscard]] std::span<const float> buffer() const { return buffer_; }
	[[nodiscard]] const DualCosineOscillator& lfo() const { return lfo_; }

private:
	//[gnu::always_inline]
	void StepLFO() {
		if ((write_ptr_ & 31) == 0) {
			++counts.lfo_steps;
			lfo_.Next();
		}
	}

	int32_t write_ptr_ = 0;
	std::span<float> buffer_{};
	DualCosineOscillator lfo_; // NOLINT(cppcoreguidelines-pro-type-member-init): own ctor inits it; {} doesn't compile

	size_t mask{};

public: /******************** INNER CLASSES ****************/
	class Context {
	public:
		[[nodiscard]] constexpr float Get() const { return accumulator_; }
		constexpr void Set(float value) { accumulator_ = value; }
		constexpr void Add(float value) { accumulator_ += value; }
		constexpr void Multiply(float value) { accumulator_ *= value; }
		constexpr void Reset() { Set(0); }

		constexpr void Lp(float& state, float coefficient) { accumulator_ = OnePole(state, accumulator_, coefficient); }

		constexpr void Hp(float& state, float coefficient) {
			accumulator_ -= OnePole(state, accumulator_, coefficient);
		}

	private:
		float accumulator_ = 0.f;
	};

	struct DelayLine {
		DelayLine(size_t length) : length(length) {};

		// Store and Fetch
		//[gnu::always_inline]
		void Process(Context& c, size_t offset = 0) {
			this->at(offset) = c.Get();
			c.Set(this->at(length - offset));
		}

		//[gnu::always_inline]
		float& at(int32_t index) {
			if (index == TAIL) {
				index = length - 1;
			}
			return engine_->at(this->base + index);
		}

		//[gnu::always_inline]
		float Read(int32_t offset) {
			// STATIC_ASSERT(d.base + d.length <= size, delay_memory_full);
			return this->at(offset);
		}

		//[gnu::always_inline]
		float Interpolate(float offset) {
			auto offset_integral = static_cast<int32_t>(offset);
			float offset_fractional = offset - static_cast<float>(offset_integral);
			const float a = this->at(offset_integral);
			const float b = this->at(offset_integral + 1);
			return reverb_ref::Interpolate(a, b, offset_fractional);
		}

		/// writes to the delayline
		//[gnu::always_inline]
		void Write(int32_t offset, float value) { this->at(offset) = value; }

	public:
		const size_t length = 0;
		size_t base = 0;
		FxEngine* engine_ = nullptr;
	};

	struct AllPass : public DelayLine {
		AllPass(size_t length) : DelayLine(length) {};

		//[gnu::always_inline]
		float Read(Context& c, int32_t offset, float scale) {
			const float r = DelayLine::Read(offset);
			c.Add(r * scale);
			return r;
		}

		// inline float Read(Context& c, float scale) { return Read(c, scale); }

		//[gnu::always_inline]
		void Write(Context& c, int32_t offset, float scale) {
			DelayLine::Write(offset, c.Get());
			c.Multiply(scale);
		}

		//[gnu::always_inline]
		void Write(Context& c, float scale) { Write(c, (int32_t)0, scale); }

		//[gnu::always_inline]
		void Write(Context& c, int32_t offset, float scale, float input) {
			Write(c, offset, scale);
			c.Add(input);
		}

		//[gnu::always_inline]
		void Write(Context& c, float scale, float input) { Write(c, 0, scale, input); }

		/** @brief Can be used in place of any AllPass::Read calls */
		//[gnu::always_inline]
		float Interpolate(Context& c, float offset, float scale) {
			// STATIC_ASSERT(d.base + d.length <= size, delay_memory_full);
			const float r = DelayLine::Interpolate(offset);
			c.Add(r * scale);
			return r;
		}

		//[gnu::always_inline]
		float Interpolate(Context& c, float offset, LFOIndex index, float amplitude, float scale) {
			offset += amplitude * this->engine_->LFO(index);
			return Interpolate(c, offset, scale);
		}

		//[gnu::always_inline]
		void ProcessInterpolate(Context& c, float offset, LFOIndex index, float amplitude, float scale) {
			const float read = this->Interpolate(c, offset, index, amplitude, scale);
			this->Write(c, 0, -scale, read);
		}

		// Simple Schroeder allpass section
		//
		//        ------[*-scale]-----,
		//       |    -----------     ∨
		//  ---+-'-->| Delayline |--,-+--->
		//     ∧      -----------   |
		//     '------[*scale]------
		//
		//[gnu::always_inline]
		void Process(Context& c, float scale) {
			const float head = c.Get();
			const float tail = this->at(TAIL);

			const float feedback = head + (tail * scale);
			this->at(0) = feedback; // feedback into delayline

			const float feedforward = (feedback * -scale) + tail;
			c.Set(feedforward); // output into pipeline

			// c.Add(tail * scale);
			// this->at(0) = c.Get();

			// c.Multiply(-scale);
			// c.Add(tail);
		}
	};

	static void ConstructTopology(FxEngine& e, std::initializer_list<DelayLine*> delays) {
		size_t base = 0;
		for (DelayLine* d : delays) {
			d->engine_ = &e;
			d->base = base;
			base += d->length + 1;
		}
	}
};

// ---- base.hpp ----

enum class FilterType { LowPass, HighPass };
template <FilterType filtertype>
static constexpr float calcFilterCutoff(float f) {
	float minFreq;
	float maxFreq;
	// this exp will be between 1 and 4.48, half the knob range is about 2
	// for the HPF the result will then be from 0 to 500Hz with half the knob range at 300hz
	// then shift to 20-520Hz as there is a low end buildup in the reverb that should always be filtered out
	// for the LPF the result will be from 0 to 20000 with half the knob range at 5678.537hz
	if constexpr (filtertype == FilterType::LowPass) {
		minFreq = 0.0f;
		maxFreq = 5083.74f;
	}
	else if constexpr (filtertype == FilterType::HighPass) {
		minFreq = 20.0f;
		maxFreq = 150.0f;
	}
	float fc_hz = minFreq + (std::exp(1.5f * f) - 1) * maxFreq;
	float fc = fc_hz / float(kSampleRate);
	float wc = fc / (1 + fc);
	return wc;
}

// ---- mutable.hpp ----

class ReferenceMutable {
	constexpr static size_t kBufferSize = 32768;

public:
	ReferenceMutable() = default;

	~ReferenceMutable() = default;

	DELUGE_REFERENCE_LEGACY_ARITHMETIC void process(std::span<int32_t> in, std::span<StereoSample> output) {
		++counts.mutable_blocks;
		if (hp_cutoff_val_ != 0 || lp_cutoff_val_ != 1) {
			++counts.plate_filtered_blocks;
		}

		// This is the Griesinger topology described in the Dattorro paper
		// (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
		// Modulation is applied to the two long delays for a slow shimmer/chorus effect.
		// Upstream also smears the first diffuser AP; that is deliberately not ported.
		typename FxEngine::AllPass ap1(150);
		typename FxEngine::AllPass ap2(214);
		typename FxEngine::AllPass ap3(319);
		typename FxEngine::AllPass ap4(527);

		typename FxEngine::AllPass dap1a(2182);
		typename FxEngine::AllPass dap1b(2690);
		typename FxEngine::AllPass del1(4501);

		typename FxEngine::AllPass dap2a(2525);
		typename FxEngine::AllPass dap2b(2197);
		typename FxEngine::AllPass del2(6312);

		typename FxEngine::Context c;
		FxEngine::ConstructTopology(engine_, //<
		                            {
		                                &ap1, &ap2, &ap3, &ap4, //<
		                                &dap1a, &dap1b, &del1,  //<
		                                &dap2a, &dap2b, &del2,  //<
		                            });

		const float kap = diffusion_;
		const float klp = lp_;
		const float krt = reverb_time_;

		float lp_1 = lp_decay_1_;
		float lp_2 = lp_decay_2_;

		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			float wet = 0;
			float apout = 0.0f;
			engine_.Advance();

			// Smear AP1 inside the loop.
			// c.Interpolate(ap1, 10.0f, LFO_1, 80.0f, 1.0f);
			// c.Write(ap1, 100, 0.0f);

			const float input_sample = in[frame] / static_cast<float>(std::numeric_limits<int32_t>::max());

			c.Set(input_sample);

			// Diffuse through 4 allpasses.
			ap1.Process(c, kap);
			ap2.Process(c, kap);
			ap3.Process(c, kap);
			ap4.Process(c, kap);
			apout = c.Get();

			// Main reverb loop.
			c.Set(apout);
			del2.Interpolate(c, 6261.0f, LFO_2, 50.0f, krt);
			c.Lp(lp_1, klp);
			dap1a.Process(c, -kap);
			dap1b.Process(c, kap);
			del1.Write(c, 2.0f);
			wet = c.Get();
			wet = wet - reverb_ref::OnePole(hp_r_, wet, hp_cutoff_);
			wet = reverb_ref::OnePole(lp_r_, wet, lp_cutoff_);

			auto output_right = reverb_ref::toQ31Saturating(wet * kOutputGain);

			c.Set(apout);
			del1.Interpolate(c, 4460.0f, LFO_1, 40.0f, krt);
			c.Lp(lp_2, klp);
			dap2a.Process(c, -kap);
			dap2b.Process(c, kap);
			del2.Write(c, 2.0f);
			wet = c.Get();
			wet = wet - reverb_ref::OnePole(hp_l_, wet, hp_cutoff_);
			wet = reverb_ref::OnePole(lp_l_, wet, lp_cutoff_);

			auto output_left = reverb_ref::toQ31Saturating(wet * kOutputGain);

			// Mix
			s.l += multiply_32x32_rshift32_rounded(output_left, getPanLeft());
			s.r += multiply_32x32_rshift32_rounded(output_right, getPanRight());
		}

		lp_decay_1_ = lp_1;
		lp_decay_2_ = lp_2;
	}

	inline void Clear() { engine_.Clear(); }

	/// Lifts the normalised loop signal back into q31. Long spelled UINT32_MAX * 0xF; kept at
	/// exactly that value, since it is what the models were balanced around.
	static constexpr float kOutputGain = 15.f * 4294967296.f;

	static constexpr float kReverbTimeMin = 0.01f;
	static constexpr float kReverbTimeMax = 0.98f;
	static constexpr float kWidthMin = 0.1f;
	static constexpr float kWidthMax = 0.9f;
	/// Range of the one-pole coefficient driving the damping filter inside the reverb loop. kLpMax is fully
	/// transparent, kLpMin is a cutoff of roughly 70Hz, past which there is nothing left of the tail to damp.
	static constexpr float kLpMin = 0.01f;
	static constexpr float kLpMax = 1.f;

	// Reverb Base Overrides
	void setRoomSize(float value) { reverb_time_ = ::util::map(value, 0.f, 1.f, kReverbTimeMin, kReverbTimeMax); }
	[[nodiscard]] float getRoomSize() const {
		return ::util::map(reverb_time_, kReverbTimeMin, kReverbTimeMax, 0.f, 1.f);
	};

	/// 0 is no damping at all, 1 is as dark as the loop filter goes. The curve is logarithmic so the audible
	/// change is spread evenly over the parameter's range.
	void setDamping(float value) {
		lp_val_ = value;
		const float damping = std::clamp(std::log2((value * 50.f) + 1.f) / 5.7f, 0.f, 1.f);
		lp_ = ::util::map(1.f - damping, 0.f, 1.f, kLpMin, kLpMax);
	}
	[[nodiscard]] float getDamping() const { return lp_val_; }

	void setWidth(float value) { diffusion_ = ::util::map(value, 0.f, 1.f, kWidthMin, kWidthMax); }
	[[nodiscard]] float getWidth() const { return ::util::map(diffusion_, kWidthMin, kWidthMax, 0.f, 1.f); };

	void setHPF(float f) {
		hp_cutoff_val_ = f;
		hp_cutoff_ = calcFilterCutoff<FilterType::HighPass>(f);
	}

	[[nodiscard]] float getHPF() const { return hp_cutoff_val_; }

	void setLPF(float f) {
		lp_cutoff_val_ = f;
		lp_cutoff_ = calcFilterCutoff<FilterType::LowPass>(f);
	}

	[[nodiscard]] float getLPF() const { return lp_cutoff_val_; }

	// The base's pan levels.
	constexpr void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		amplitude_right_ = amplitude_right;
		amplitude_left_ = amplitude_left;
	}
	[[nodiscard]] constexpr int32_t getPanLeft() const { return amplitude_left_; }
	[[nodiscard]] constexpr int32_t getPanRight() const { return amplitude_right_; }

	[[nodiscard]] float reverb_time() const { return reverb_time_; }
	[[nodiscard]] float diffusion() const { return diffusion_; }
	[[nodiscard]] float lp() const { return lp_; }
	[[nodiscard]] float hp_cutoff() const { return hp_cutoff_; }
	[[nodiscard]] float lp_cutoff() const { return lp_cutoff_; }

	/// @return The whole state, in PlateView's canonical order (no input band filter: lp_band is 0).
	[[nodiscard]] PlateView view() const {
		return {
		    .ring = engine_.buffer(),
		    .write = engine_.write_ptr(),
		    .lfo_y_0 = engine_.lfo().y_0_lanes(),
		    .lfo_y_1 = engine_.lfo().y_1_lanes(),
		    .lp_decay_1 = lp_decay_1_,
		    .lp_decay_2 = lp_decay_2_,
		    .lp_band = 0.f,
		    .hp_l = hp_l_,
		    .hp_r = hp_r_,
		    .lp_l = lp_l_,
		    .lp_r = lp_r_,
		};
	}

protected:
	static constexpr float sample_rate = kSampleRate;

	std::array<float, kBufferSize> buffer_{};
	FxEngine engine_{buffer_, {0.5f / sample_rate, 0.3f / sample_rate}};

	// size
	float reverb_time_ = 0.665f;

	// width
	float diffusion_ = 0.625f;

	// damping
	float lp_{0.7f};
	float lp_val_{0.7f};

	// These are the state variables for the low-pass filters
	float lp_decay_1_{0};
	float lp_decay_2_{0};

	// High-pass
	float hp_cutoff_val_{0.f};
	// corresponds to 20Hz
	float hp_cutoff_{calcFilterCutoff<FilterType::HighPass>(0)};
	float hp_l_{0.0}; // HP state variable
	float hp_r_{0.0}; // HP state variable

	// Low-pass
	float lp_cutoff_val_{0.f};
	// corresponds to 0Hz
	float lp_cutoff_{calcFilterCutoff<FilterType::LowPass>(0)};
	float lp_l_{0.0}; // LP state variable
	float lp_r_{0.0}; // LP state variable

	int32_t amplitude_right_ = 0;
	int32_t amplitude_left_ = 0;
};

// ---- digital.hpp ----

/// @brief The Griesinger topology model from Part 1 of Effect Design by John Dattorro,
/// classically based on the famous Lexicon 224 Digital Reverb
class ReferenceDigital : public ReferenceMutable {
	constexpr static float kRatio = 29761.f / kSampleRate; // Lexicon sample rate to Deluge sample rate

	/// Dattorro's +/-16 samples of tank modulation, rescaled to our sample rate.
	constexpr static size_t max_excursion = 16.f * kRatio;

	/// An interpolated read touches both `offset` and `offset + 1`, so a modulated line has to
	/// reserve one slot beyond the furthest its LFO can push it. Without this the read spills
	/// into the next line's memory and drags a sample from a whole buffer ago into the tank.
	constexpr static size_t kInterpolationGuard = 1;

	/// Dattorro's decay range. The tank's allpasses are unity gain, so the round trip loses
	/// only decay^4 -- pushing this to the old shared [0.01, 0.98] leaves the top of the knob
	/// ringing for the better part of half a minute.
	constexpr static float kDecayMin = 0.1f;
	constexpr static float kDecayMax = 0.9f;

public:
	DELUGE_REFERENCE_LEGACY_ARITHMETIC void process(std::span<q31_t> in, std::span<StereoSample> output) {
		++counts.digital_blocks;
		if (hp_cutoff_val_ != 0 || lp_cutoff_val_ != 1) {
			++counts.plate_filtered_blocks;
		}

		typename FxEngine::Context c;

		typename FxEngine::AllPass ap1(142 * kRatio);
		typename FxEngine::AllPass ap2(107 * kRatio);
		typename FxEngine::AllPass ap3(379 * kRatio);
		typename FxEngine::AllPass ap4(277 * kRatio);

		typename FxEngine::AllPass dap1a((672 * kRatio) + max_excursion + kInterpolationGuard);
		typename FxEngine::DelayLine del1a(4453 * kRatio);
		typename FxEngine::AllPass dap1b(1800 * kRatio);
		typename FxEngine::DelayLine del1b(3720 * kRatio);

		typename FxEngine::AllPass dap2a((908 * kRatio) + max_excursion + kInterpolationGuard);
		typename FxEngine::DelayLine del2a(4217 * kRatio);
		typename FxEngine::AllPass dap2b(2656 * kRatio);
		typename FxEngine::DelayLine del2b(3163 * kRatio);

		FxEngine::ConstructTopology(engine_, {&ap1, &ap2, &ap3, &ap4,         //<
		                                      &dap1a, &del1a, &dap1b, &del1b, //<
		                                      &dap2a, &del2a, &dap2b, &del2b});

		const float kdecay = reverb_time_;                          // 0.5f
		const float kid1 = 0.750f;                                  // input diffusion 1
		const float kid2 = 0.625f;                                  // input diffusion 2
		const float kdd1 = 0.70f;                                   // decay diffusion 1
		const float kdd2 = std::clamp(kdecay + 0.15f, 0.25f, 0.5f); // decay diffusion 2

		const float kdamp = lp_; // 1.f - 0.0005f;            // damping
		const float kbandwidth = 0.9995f;

		float lp_1 = lp_decay_1_;
		float lp_2 = lp_decay_2_;
		float lp_band = lp_band_;

		for (size_t frame = 0; frame < in.size(); ++frame) {
			engine_.Advance();

			const float input_sample = in[frame] / static_cast<float>(std::numeric_limits<int32_t>::max());
			c.Set(input_sample);

			c.Lp(lp_band, kbandwidth);

			// Diffuse through 4 allpasses.
			ap1.Process(c, kid1);
			ap2.Process(c, kid1);
			ap3.Process(c, kid2);
			ap4.Process(c, kid2);
			float apout = c.Get();

			// Main reverb loop: a figure of eight, where each branch's tail feeds the other's
			// head. del2b's output is already a delayed read, so branch one can close the loop
			// on it here and branch two simply carries on from branch one's accumulator.
			c.Set(apout);
			c.Add(kdecay * del2b.Read(del2b.length));
			dap1a.ProcessInterpolate(c, 672.0f * kRatio, LFO_2, max_excursion, -kdd1);
			del1a.Process(c);
			c.Lp(lp_1, kdamp); // damping
			c.Multiply(kdecay);
			dap1b.Process(c, kdd2);
			del1b.Process(c);

			c.Multiply(kdecay);
			c.Add(apout);
			dap2a.ProcessInterpolate(c, 908.0f * kRatio, LFO_1, max_excursion, -kdd1);
			del2a.Process(c);
			c.Lp(lp_2, kdamp); // damping
			c.Multiply(kdecay);
			dap2b.Process(c, kdd2);
			del2b.Process(c);

			float left_sum = 0;
			left_sum += 0.6f * del2a.at(266 * kRatio);
			left_sum += 0.6f * del2a.at(2974 * kRatio);
			left_sum -= 0.6f * dap2b.at(1913 * kRatio);
			left_sum += 0.6f * del2b.at(1996 * kRatio);
			left_sum -= 0.6f * del1a.at(1990 * kRatio);
			left_sum -= 0.6f * dap1b.at(187 * kRatio);
			left_sum -= 0.6f * del1b.at(1066 * kRatio);
			left_sum = left_sum - reverb_ref::OnePole(hp_l_, left_sum, hp_cutoff_);
			left_sum = reverb_ref::OnePole(lp_l_, left_sum, lp_cutoff_);

			float right_sum = 0;
			right_sum += 0.6f * del1a.at(353 * kRatio);
			right_sum += 0.6f * del1a.at(3627 * kRatio);
			right_sum -= 0.6f * dap1b.at(1228 * kRatio);
			right_sum += 0.6f * del1b.at(2673 * kRatio);
			right_sum -= 0.6f * del2a.at(2111 * kRatio);
			right_sum -= 0.6f * dap2b.at(335 * kRatio);
			right_sum -= 0.6f * del2b.at(121 * kRatio);
			right_sum = right_sum - reverb_ref::OnePole(hp_r_, right_sum, hp_cutoff_);
			right_sum = reverb_ref::OnePole(lp_r_, right_sum, lp_cutoff_);

			q31_t output_left = reverb_ref::toQ31Saturating(left_sum * kOutputGain);
			q31_t output_right = reverb_ref::toQ31Saturating(right_sum * kOutputGain);

			// Mix
			output[frame].l += multiply_32x32_rshift32_rounded(output_left, getPanLeft());
			output[frame].r += multiply_32x32_rshift32_rounded(output_right, getPanRight());
		}

		lp_decay_1_ = lp_1;
		lp_decay_2_ = lp_2;
		lp_band_ = lp_band;
	}

	// Reverb Base Overrides
	void setRoomSize(float value) { reverb_time_ = ::util::map(value, 0.f, 1.f, kDecayMin, kDecayMax); }
	[[nodiscard]] float getRoomSize() const { return ::util::map(reverb_time_, kDecayMin, kDecayMax, 0.f, 1.f); };

	/// @brief One line of the topology: where its memory begins in the ring, and its length.
	struct ReferenceLine {
		uint32_t base;
		uint32_t length;
	};

	/// @return The twelve lines process builds, in its ConstructTopology order: ap1, ap2, ap3, ap4, dap1a, del1a,
	/// dap1b, del1b, dap2a, del2a, dap2b, del2b. Non-const because ConstructTopology takes the engine by reference; it
	/// writes nothing the next process does not write again.
	[[nodiscard]] std::array<ReferenceLine, 12> digital_topology() {
		typename FxEngine::AllPass ap1(142 * kRatio);
		typename FxEngine::AllPass ap2(107 * kRatio);
		typename FxEngine::AllPass ap3(379 * kRatio);
		typename FxEngine::AllPass ap4(277 * kRatio);

		typename FxEngine::AllPass dap1a((672 * kRatio) + max_excursion + kInterpolationGuard);
		typename FxEngine::DelayLine del1a(4453 * kRatio);
		typename FxEngine::AllPass dap1b(1800 * kRatio);
		typename FxEngine::DelayLine del1b(3720 * kRatio);

		typename FxEngine::AllPass dap2a((908 * kRatio) + max_excursion + kInterpolationGuard);
		typename FxEngine::DelayLine del2a(4217 * kRatio);
		typename FxEngine::AllPass dap2b(2656 * kRatio);
		typename FxEngine::DelayLine del2b(3163 * kRatio);

		FxEngine::ConstructTopology(engine_, {&ap1, &ap2, &ap3, &ap4,         //<
		                                      &dap1a, &del1a, &dap1b, &del1b, //<
		                                      &dap2a, &del2a, &dap2b, &del2b});

		const std::array<const FxEngine::DelayLine*, 12> lines{&ap1,   &ap2,   &ap3,   &ap4,   &dap1a, &del1a,
		                                                       &dap1b, &del1b, &dap2a, &del2a, &dap2b, &del2b};
		std::array<ReferenceLine, 12> out{};
		for (std::size_t i = 0; i < lines.size(); ++i) {
			out[i] = {.base = static_cast<uint32_t>(lines[i]->base), .length = static_cast<uint32_t>(lines[i]->length)};
		}
		return out;
	}

	/// @return The whole state, in PlateView's canonical order, the input band filter's memory included.
	[[nodiscard]] PlateView view() const {
		PlateView v = ReferenceMutable::view();
		v.lp_band = lp_band_;
		return v;
	}

private:
	float lp_band_{0.f};
};

// ---- reverb.hpp ----

class ReferenceReverb {
public:
	enum class Model {
		FREEVERB = 0, // Freeverb is the original
		MUTABLE,
		DIGITAL,
	};

	ReferenceReverb() {
		room_size_ = getRoomSize();
		damping_ = getDamping();
		width_ = getWidth();
		hpf_ = getHPF();
		lpf_ = getLPF();
	}
	~ReferenceReverb() = default;

	/// @brief Replaces the active reverb with a freshly built one of model @p m.
	///
	/// Callable only from outside the audio tier. `emplace` destroys the live reverb and constructs a
	/// ~128 KB replacement in its storage, which is far too long to mask interrupts for, and the audio
	/// tier renders from an interrupt executor that can preempt this at any point. So the rebuild
	/// publishes `rebuilding_` instead: while it is set the audio tier contributes no reverb, which
	/// costs a few blocks of reverb tail on a model change and never observes a half-built object.
	///
	/// @param m Reverb model to switch to.
	void setModel(Model m) {
		rebuilding_.store(true, std::memory_order_release);
		switch (m) {
		case Model::FREEVERB:
			reverb_.emplace<ReferenceFreeverb>();
			break;
		case Model::DIGITAL:
			reverb_.emplace<ReferenceDigital>();
			break;
		case Model::MUTABLE:
			reverb_.emplace<ReferenceMutable>();
			break;
		}
		std::visit([&](auto& r) { r.setRoomSize(room_size_); }, reverb_);
		std::visit([&](auto& r) { r.setDamping(damping_); }, reverb_);
		std::visit([&](auto& r) { r.setWidth(width_); }, reverb_);
		std::visit([&](auto& r) { r.setHPF(hpf_); }, reverb_);
		std::visit([&](auto& r) { r.setLPF(lpf_); }, reverb_);
		model_ = m;
		rebuilding_.store(false, std::memory_order_release);
	}

	Model getModel() { return model_; }

	/// @brief Mixes the reverb into @p output. Audio-tier entry point.
	///
	/// @param input  Dry signal to feed the reverb.
	/// @param output Buffer the reverb is mixed into.
	void process(std::span<int32_t> input, std::span<StereoSample> output) {
		// Mid-rebuild there is no reverb to render. `process` mixes into `output`, so contributing
		// nothing leaves the dry signal intact.
		if (rebuilding_.load(std::memory_order_acquire)) {
			return;
		}
		std::visit([&](auto& r) { r.process(input, output); }, reverb_);
	}

	/// @brief Applies stereo pan levels to the active reverb. Audio-tier entry point, called each
	///        block just before `process`.
	///
	/// @param amplitude_left  Left-channel amplitude.
	/// @param amplitude_right Right-channel amplitude.
	void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		if (rebuilding_.load(std::memory_order_acquire)) {
			return;
		}
		std::visit([&](auto& r) { r.setPanLevels(amplitude_left, amplitude_right); }, reverb_);
	}

	void setRoomSize(float value) {
		room_size_ = value;
		std::visit([&](auto& r) { r.setRoomSize(value); }, reverb_);
	}

	[[nodiscard]] float getRoomSize() const {
		return std::visit([](const auto& r) { return r.getRoomSize(); }, reverb_);
	};

	void setDamping(float value) {
		damping_ = value;
		std::visit([&](auto& r) { r.setDamping(value); }, reverb_);
	}

	[[nodiscard]] float getDamping() const {
		return std::visit([](const auto& r) { return r.getDamping(); }, reverb_);
	}

	void setWidth(float value) {
		width_ = value;
		std::visit([&](auto& r) { r.setWidth(value); }, reverb_);
	}

	[[nodiscard]] float getWidth() const {
		return std::visit([](const auto& r) { return r.getWidth(); }, reverb_);
	};

	void setHPF(float f) {
		hpf_ = f;
		std::visit([&](auto& r) { r.setHPF(f); }, reverb_);
	}
	[[nodiscard]] float getHPF() const {
		return std::visit([](const auto& r) { return r.getHPF(); }, reverb_);
	}

	void setLPF(float f) {
		lpf_ = f;
		std::visit([&](auto& r) { r.setLPF(f); }, reverb_);
	}
	[[nodiscard]] float getLPF() const {
		return std::visit([](const auto& r) { return r.getLPF(); }, reverb_);
	}

	template <typename T>
	constexpr T& reverb_as() {
		return std::get<T>(reverb_);
	}

	/// @brief Folds the held model's whole state into @p d.
	void view_digest(reverb_workloads::Digest& d) const {
		std::visit(
		    [&](const auto& r) {
			    using M = std::remove_cvref_t<decltype(r)>;
			    if constexpr (std::is_same_v<M, ReferenceFreeverb>) {
				    reverb_workloads::digest_freeverb(d, r.view());
			    }
			    else {
				    reverb_workloads::digest_plate(d, r.view());
			    }
		    },
		    reverb_);
	}

private:
	std::variant<ReferenceFreeverb, ReferenceMutable, ReferenceDigital> reverb_{};

	Model model_ = Model::FREEVERB;

	/// Set while `setModel` tears down and rebuilds the active reverb, so the audio tier knows to
	/// leave it alone. Written only outside the audio tier, read only inside it.
	std::atomic<bool> rebuilding_ = false;

	float room_size_ = 0.f;
	float damping_ = 0.f;
	float width_ = 0.f;
	float hpf_ = 0.f;
	float lpf_ = 0.f;
};

} // namespace deluge::dsp::test::reverb_ref

namespace deluge::dsp::test {

/// @brief The reference reverb behind the runner shape, in the engine's call order (getReverbParamsFromSong,
/// renderReverb). It lives on the heap: the reverb is over 128 KB.
struct ReferenceRunner {
	std::unique_ptr<reverb_ref::ReferenceReverb> reverb = std::make_unique<reverb_ref::ReferenceReverb>();

	void set_model(reverb_workloads::Model m) {
		reverb->setModel(static_cast<reverb_ref::ReferenceReverb::Model>(m));
		++counts.model_changes;
	}
	void set_knobs(const reverb_workloads::Knobs& k) {
		reverb->setRoomSize(k.room_size);
		reverb->setHPF(k.hpf);
		reverb->setLPF(k.lpf);
		reverb->setDamping(k.damping);
		reverb->setWidth(k.width);
	}
	void render(std::span<int32_t> send, std::span<StereoSample> out, int32_t l, int32_t r) {
		reverb->setPanLevels(l, r);
		reverb->process(send, out);
	}
	void digest_state(reverb_workloads::Digest& d) const { reverb->view_digest(d); }
};
static_assert(reverb_workloads::ReverbRunner<ReferenceRunner>);

} // namespace deluge::dsp::test
