// Reverb model declaration
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "dsp/reverb/base.hpp"
#include "dsp/reverb/freeverb/allpass.hpp"
#include "dsp/reverb/freeverb/comb.hpp"
#include "dsp/reverb/freeverb/tuning.h"
#include <span>

namespace deluge::dsp::reverb {
class Freeverb : public Base {
public:
	Freeverb();
	~Freeverb() override = default;

	void mute();

	void setRoomSize(float value) override {
		roomsize = (value * scaleroom) + offsetroom;
		update();
	}

	[[nodiscard]] constexpr float getRoomSize() const override { return (roomsize - offsetroom) / scaleroom; }

	void setDamping(float value) override {
		damp = value * scaledamp;
		update();
	}

	[[nodiscard]] constexpr float getDamping() const override { return damp / scaledamp; }

	void setWet(float value) {
		wet = value * scalewet;
		update();
	}

	[[nodiscard]] constexpr float getWet() const { return wet / scalewet; }

	void setDry(float value) { dry = value * scaledry; }

	[[nodiscard]] constexpr float getDry() const { return dry / scaledry; }

	void setWidth(float value) override {
		width = value;
		update();
	}

	[[nodiscard]] constexpr float getWidth() const override { return width; }

	[[gnu::always_inline]] void ProcessOne(int32_t input, int32_t* outputL, int32_t* outputR) {
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
		*outputL = (out_l + multiply_32x32_rshift32_rounded(out_r, wet2)) << 1;
		*outputR = (out_r + multiply_32x32_rshift32_rounded(out_l, wet2)) << 1;
	}

	[[gnu::always_inline]] void process(std::span<int32_t> input, std::span<StereoSample> output) override {
		int32_t output_left = 0;
		int32_t output_right = 0;

		for (size_t frame = 0; frame < input.size(); frame++) {
			const int32_t input_sample = input[frame];
			StereoSample& output_sample = output[frame];

			ProcessOne(input_sample, &output_left, &output_right);
			output_sample.l += multiply_32x32_rshift32_rounded(output_left, getPanLeft());
			output_sample.r += multiply_32x32_rshift32_rounded(output_right, getPanRight());
		}
	}

private:
	void update();

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
	std::array<freeverb::Comb, numcombs> combL;
	std::array<freeverb::Comb, numcombs> combR;

	// Allpass filters
	std::array<freeverb::Allpass, numallpasses> allpassL;
	std::array<freeverb::Allpass, numallpasses> allpassR;

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
};
} // namespace deluge::dsp::reverb
