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
#include <cstdint>
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

	[[gnu::always_inline]] void ProcessOne(int32_t input, StereoSample& output_sample) {
		int32_t out_l = 0;
		int32_t out_r = 0;

		// Accumulate comb filters in parallel
		combs_.process(input, out_l, out_r);

		// Feed through allpasses in series
		allpasses_.process(out_l, out_r);

		// Calculate output
		out_l = (out_l + multiply_32x32_rshift32_rounded(out_r, wet2)) << 1;
		out_r = (out_r + multiply_32x32_rshift32_rounded(out_l, wet2)) << 1;

		// Mix output
		output_sample.l += multiply_32x32_rshift32_rounded(out_l, this->getPanLeft());
		output_sample.r += multiply_32x32_rshift32_rounded(out_r, this->getPanRight());
	}

	void process(std::span<int32_t> input, std::span<StereoSample> output) override;

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
	freeverb::Combs combs_;

	// Allpass filters
	freeverb::Allpasses allpasses_;

	// Buffers for the combs
	std::array<int32_t, combtuningL1 + maxblockframes> bufcombL1;
	std::array<int32_t, combtuningR1 + maxblockframes> bufcombR1;
	std::array<int32_t, combtuningL2 + maxblockframes> bufcombL2;
	std::array<int32_t, combtuningR2 + maxblockframes> bufcombR2;
	std::array<int32_t, combtuningL3 + maxblockframes> bufcombL3;
	std::array<int32_t, combtuningR3 + maxblockframes> bufcombR3;
	std::array<int32_t, combtuningL4 + maxblockframes> bufcombL4;
	std::array<int32_t, combtuningR4 + maxblockframes> bufcombR4;
	std::array<int32_t, combtuningL5 + maxblockframes> bufcombL5;
	std::array<int32_t, combtuningR5 + maxblockframes> bufcombR5;
	std::array<int32_t, combtuningL6 + maxblockframes> bufcombL6;
	std::array<int32_t, combtuningR6 + maxblockframes> bufcombR6;
	std::array<int32_t, combtuningL7 + maxblockframes> bufcombL7;
	std::array<int32_t, combtuningR7 + maxblockframes> bufcombR7;
	std::array<int32_t, combtuningL8 + maxblockframes> bufcombL8;
	std::array<int32_t, combtuningR8 + maxblockframes> bufcombR8;

	// Buffers for the allpasses
	std::array<int32_t, allpasstuningL1 + maxblockframes> bufallpassL1;
	std::array<int32_t, allpasstuningR1 + maxblockframes> bufallpassR1;
	std::array<int32_t, allpasstuningL2 + maxblockframes> bufallpassL2;
	std::array<int32_t, allpasstuningR2 + maxblockframes> bufallpassR2;
	std::array<int32_t, allpasstuningL3 + maxblockframes> bufallpassL3;
	std::array<int32_t, allpasstuningR3 + maxblockframes> bufallpassR3;
	std::array<int32_t, allpasstuningL4 + maxblockframes> bufallpassL4;
	std::array<int32_t, allpasstuningR4 + maxblockframes> bufallpassR4;

	int32_t reverb_send_post_lpf_ = 0;
};
} // namespace deluge::dsp::reverb
