// Reverb model implementation
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

#include "dsp/reverb/freeverb/freeverb.hpp"
#include <algorithm>
#include <argon.hpp>
#include <limits>

namespace freeverb {

// 4x4 transpose: from four vectors of four frames each to four vectors of one frame each, and back
[[gnu::always_inline]] static inline void transposeFour(int32x4_t& a, int32x4_t& b, int32x4_t& c, int32x4_t& d) {
	int32x4x2_t ab = vtrnq_s32(a, b);
	int32x4x2_t cd = vtrnq_s32(c, d);
	a = vcombine_s32(vget_low_s32(ab.val[0]), vget_low_s32(cd.val[0]));
	b = vcombine_s32(vget_low_s32(ab.val[1]), vget_low_s32(cd.val[1]));
	c = vcombine_s32(vget_high_s32(ab.val[0]), vget_high_s32(cd.val[0]));
	d = vcombine_s32(vget_high_s32(ab.val[1]), vget_high_s32(cd.val[1]));
}

// multiply_32x32_rshift32_rounded() in each lane
[[gnu::always_inline]] static inline int32x4_t multiplyRounded(int32x4_t a, int32x4_t b) {
	return vcombine_s32(vrshrn_n_s64(vmull_s32(vget_low_s32(a), vget_low_s32(b)), 32),
	                    vrshrn_n_s64(vmull_s32(vget_high_s32(a), vget_high_s32(b)), 32));
}

// Four combs over a block, one per lane, four frames at a time. Their outputs are summed, and either stored in output
// or (if accumulate) added to it. Each comb's filterstore is always even (it's something << 1), so we keep half of it,
// which is less than 2^30 in magnitude: then vqrdmulh, (2 * a * b + 2^31) >> 32, which never saturates for it, is
// exactly multiply_32x32_rshift32_rounded() of the filterstore.
template <bool accumulate>
[[gnu::always_inline]] static inline void
processFourCombs(int32_t** pos, int32_t* filterstore, int32x4_t damp1, int32x4_t damp2, int32x4_t feedback,
                 const int32_t* __restrict__ input, int32_t* __restrict__ output, size_t numChunks) {
	int32x4_t halfFilterstore = vshrq_n_s32(vld1q_s32(filterstore), 1);
	int32_t* __restrict__ pos0 = pos[0];
	int32_t* __restrict__ pos1 = pos[1];
	int32_t* __restrict__ pos2 = pos[2];
	int32_t* __restrict__ pos3 = pos[3];

	do {
		// One comb per vector: what each outputs for the four frames
		int32x4_t a = vld1q_s32(pos0);
		int32x4_t b = vld1q_s32(pos1);
		int32x4_t c = vld1q_s32(pos2);
		int32x4_t d = vld1q_s32(pos3);

		int32x4_t sum = vaddq_s32(vaddq_s32(a, b), vaddq_s32(c, d));
		if constexpr (accumulate) {
			sum = vaddq_s32(sum, vld1q_s32(output));
		}
		vst1q_s32(output, sum);
		output += 4;

		// Then one frame per vector, to step the filters along frame by frame, leaving what's to go into the buffers
		// (before the input is added) in a, b, c, d
		transposeFour(a, b, c, d);
		auto step = [&](int32x4_t& frame) __attribute__((always_inline)) {
			int32x4_t filterstore =
			    vaddq_s32(multiplyRounded(frame, damp2), vqrdmulhq_s32(halfFilterstore, damp1)); // before << 1
			halfFilterstore = vshrq_n_s32(vshlq_n_s32(filterstore, 1), 1);
			frame = vqrdmulhq_s32(halfFilterstore, feedback);
		};
		step(a);
		step(b);
		step(c);
		step(d);

		// And back to one comb per vector
		transposeFour(a, b, c, d);
		int32x4_t in = vld1q_s32(input);
		input += 4;
		vst1q_s32(pos0, vaddq_s32(in, vshlq_n_s32(a, 1)));
		vst1q_s32(pos1, vaddq_s32(in, vshlq_n_s32(b, 1)));
		vst1q_s32(pos2, vaddq_s32(in, vshlq_n_s32(c, 1)));
		vst1q_s32(pos3, vaddq_s32(in, vshlq_n_s32(d, 1)));
		pos0 += 4;
		pos1 += 4;
		pos2 += 4;
		pos3 += 4;
	} while (--numChunks != 0);

	vst1q_s32(filterstore, vshlq_n_s32(halfFilterstore, 1));
	pos[0] = pos0;
	pos[1] = pos1;
	pos[2] = pos2;
	pos[3] = pos3;
}

void Combs::processBlock(const int32_t* input, int32_t* output_l, int32_t* output_r, size_t numFrames) {
	static_assert(kNumCombs == 16);
	int32x4_t damp1 = vdupq_n_s32(damp1_);
	int32x4_t damp2 = vdupq_n_s32(damp2_);
	int32x4_t feedback = vdupq_n_s32(feedback_);
	size_t numChunks = numFrames / 4;

	lines_.startBlock(numFrames);
	processFourCombs<false>(&lines_.pos[0], &filterstore_[0], damp1, damp2, feedback, input, output_l, numChunks);
	processFourCombs<true>(&lines_.pos[4], &filterstore_[4], damp1, damp2, feedback, input, output_l, numChunks);
	processFourCombs<false>(&lines_.pos[8], &filterstore_[8], damp1, damp2, feedback, input, output_r, numChunks);
	processFourCombs<true>(&lines_.pos[12], &filterstore_[12], damp1, damp2, feedback, input, output_r, numChunks);
	lines_.endBlock(numFrames);
}

// Four allpasses in series over a block, in place. Each one's output only depends on its input and its buffer, which
// isn't read back for at least its length (much more than four frames), so each can do four frames at once.
[[gnu::always_inline]] static inline void processFourAllpasses(int32_t** pos, int32_t* __restrict__ samples,
                                                               size_t numChunks) {
	int32_t* __restrict__ pos0 = pos[0];
	int32_t* __restrict__ pos1 = pos[1];
	int32_t* __restrict__ pos2 = pos[2];
	int32_t* __restrict__ pos3 = pos[3];

	do {
		int32x4_t input = vld1q_s32(samples);
		auto step = [&](int32_t* __restrict__& pos) __attribute__((always_inline)) {
			int32x4_t bufout = vld1q_s32(pos);
			int32x4_t output = vsubq_s32(bufout, input);
			vst1q_s32(pos, vsraq_n_s32(input, bufout, 1)); // input + (bufout >> 1)
			pos += 4;
			input = output;
		};
		step(pos0);
		step(pos1);
		step(pos2);
		step(pos3);
		vst1q_s32(samples, input);
		samples += 4;
	} while (--numChunks != 0);

	pos[0] = pos0;
	pos[1] = pos1;
	pos[2] = pos2;
	pos[3] = pos3;
}

void Allpasses::processBlock(int32_t* samples_l, int32_t* samples_r, size_t numFrames) {
	static_assert(numallpasses == 4);
	lines_.startBlock(numFrames);
	processFourAllpasses(&lines_.pos[0], samples_l, numFrames / 4);
	processFourAllpasses(&lines_.pos[4], samples_r, numFrames / 4);
	lines_.endBlock(numFrames);
}

} // namespace freeverb

namespace deluge::dsp::reverb {

Freeverb::Freeverb() {
	// Tie the components to their buffers
	combs_.setBuffer(0, bufcombL1);
	combs_.setBuffer(1, bufcombL2);
	combs_.setBuffer(2, bufcombL3);
	combs_.setBuffer(3, bufcombL4);
	combs_.setBuffer(4, bufcombL5);
	combs_.setBuffer(5, bufcombL6);
	combs_.setBuffer(6, bufcombL7);
	combs_.setBuffer(7, bufcombL8);
	combs_.setBuffer(numcombs + 0, bufcombR1);
	combs_.setBuffer(numcombs + 1, bufcombR2);
	combs_.setBuffer(numcombs + 2, bufcombR3);
	combs_.setBuffer(numcombs + 3, bufcombR4);
	combs_.setBuffer(numcombs + 4, bufcombR5);
	combs_.setBuffer(numcombs + 5, bufcombR6);
	combs_.setBuffer(numcombs + 6, bufcombR7);
	combs_.setBuffer(numcombs + 7, bufcombR8);
	allpasses_.setBuffer(0, bufallpassL1);
	allpasses_.setBuffer(1, bufallpassL2);
	allpasses_.setBuffer(2, bufallpassL3);
	allpasses_.setBuffer(3, bufallpassL4);
	allpasses_.setBuffer(numallpasses + 0, bufallpassR1);
	allpasses_.setBuffer(numallpasses + 1, bufallpassR2);
	allpasses_.setBuffer(numallpasses + 2, bufallpassR3);
	allpasses_.setBuffer(numallpasses + 3, bufallpassR4);

	// Set default values
	setWet(initialwet);
	setRoomSize(initialroom);
	setDry(initialdry);
	setDamping(initialdamp);
	setWidth(initialwidth);

	// Buffer will be full of rubbish - so we MUST mute them
	mute();
}

void Freeverb::mute() {
	combs_.mute();
	allpasses_.mute();
}

void Freeverb::update() {
	// Recalculate internal values after parameter change

	wet1 = wet * (width / 2 + 0.5f);
	wet2 = (((float)1 - width) / 2) / (width / 2 + 0.5f) * std::numeric_limits<int32_t>::max();

	gain = fixedgain * std::numeric_limits<int32_t>::max();

	combs_.setFeedback(roomsize * std::numeric_limits<int32_t>::max());
	combs_.setDamp(damp);
}

void Freeverb::process(std::span<int32_t> input, std::span<StereoSample> output) {
	// HPF on reverb input, cos if it has DC offset, the reverb magnifies that, and the sound farts out
	int32_t reverb_send_post_lpf = reverb_send_post_lpf_;
	for (int32_t& reverb_sample : input) {
		int32_t distance_to_go_l = reverb_sample - reverb_send_post_lpf;
		reverb_send_post_lpf += distance_to_go_l >> 11;
		reverb_sample -= reverb_send_post_lpf;
	}
	reverb_send_post_lpf_ = reverb_send_post_lpf;

	// The filters don't depend on each other within a frame, so rather than doing a frame at a time (ProcessOne()), do
	// each group of four filters over a whole block at once, four frames at a time
	size_t numBlockFrames = input.size() & ~(size_t)3;
	for (size_t start = 0; start < numBlockFrames; start += maxblockframes) {
		size_t numFrames = std::min<size_t>(maxblockframes, numBlockFrames - start);
		alignas(16) std::array<int32_t, maxblockframes> out_l;
		alignas(16) std::array<int32_t, maxblockframes> out_r;

		// Accumulate comb filters in parallel
		combs_.processBlock(&input[start], out_l.data(), out_r.data(), numFrames);

		// Feed through allpasses in series
		allpasses_.processBlock(out_l.data(), out_r.data(), numFrames);

		// Calculate and mix output, as at the end of ProcessOne(). out_l and out_r end up even, so vqrdmulh of half of
		// them is exactly multiply_32x32_rshift32_rounded() of them (see processFourCombs())
		int32x4_t wet = vdupq_n_s32(wet2);
		int32x4_t pan_l = vdupq_n_s32(getPanLeft());
		int32x4_t pan_r = vdupq_n_s32(getPanRight());
		for (size_t frame = 0; frame < numFrames; frame += 4) {
			int32x4_t l = vld1q_s32(&out_l[frame]);
			int32x4_t r = vld1q_s32(&out_r[frame]);
			int32x4_t half_l = vshrq_n_s32(vshlq_n_s32(vaddq_s32(l, freeverb::multiplyRounded(r, wet)), 1), 1);
			int32x4_t half_r = vshrq_n_s32(vshlq_n_s32(vaddq_s32(r, vqrdmulhq_s32(half_l, wet)), 1), 1);

			int32x4x2_t mix = vld2q_s32(&output[start + frame].l);
			mix.val[0] = vaddq_s32(mix.val[0], vqrdmulhq_s32(half_l, pan_l));
			mix.val[1] = vaddq_s32(mix.val[1], vqrdmulhq_s32(half_r, pan_r));
			vst2q_s32(&output[start + frame].l, mix);
		}
	}

	// Any frames left over
	for (size_t frame = numBlockFrames; frame < input.size(); frame++) {
		ProcessOne(input[frame], output[frame]);
	}
}

} // namespace deluge::dsp::reverb
