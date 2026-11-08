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
/// @brief The float_filters list's cases: the float kernels (one-pole, smoother, SVF) in Mono, Poly, Linked and Bank.
/// @details Every case reads this file's f32 input block, filled from the seeded noise scaled to ±1, and writes its
/// f32 output block. Each case sets flush-to-zero at setup (FloatBenchFlush) and keeps it for the run, as the float
/// instrument will.

#include "bench/cases.h"

#include "dsp-next/fixed/noise/noise.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::bench {

namespace {

/// @brief The seed the f32 input is filled from.
constexpr uint32_t kFloatSeed = 0x5EED'F10A;

/// @brief Floats of padding between the f32 input and output: 256 bytes.
constexpr std::size_t kFloatGap = 64;

/// @brief The f32 blocks: kMaxBlock samples for each of kMaxLanes streams, one stream after another.
/// @details One object, so their distance is fixed by its layout. The output starts 256 bytes past a multiple of 1 KB
/// from the input, and every stream is 512 bytes long, so no input stream sits a multiple of 1 KB from an output
/// stream (the shared blocks' rule, in cases.cpp).
struct FloatBlocks {
	alignas(16) std::array<dsp::f32, kMaxBlock * kMaxLanes> input;  ///< The streams read.
	std::array<dsp::f32, kFloatGap> after_input;                    ///< Padding, never read or written.
	alignas(16) std::array<dsp::f32, kMaxBlock * kMaxLanes> output; ///< The streams written.
};

static_assert((offsetof(FloatBlocks, output) - offsetof(FloatBlocks, input)) % 1024 == 256,
              "the output starts 256 bytes past a multiple of 1 KB from the input");
static_assert((kMaxBlock * sizeof(dsp::f32)) % 1024 == 512, "streams are 512 bytes apart");

FloatBlocks float_blocks{};

/// @brief Fills the f32 input from a fresh stream: the noise's words scaled to ±1.
void fill_float_input() {
	dsp::fixed::SeededNoise noise{kFloatSeed};
	for (dsp::f32& sample : float_blocks.input) {
		sample = static_cast<dsp::f32>(noise.render()) * 0x1p-31f;
	}
}

/// @return The input stream @p stream's first @p n samples.
[[nodiscard]] dsp::fp::Signal input_stream(std::size_t stream, std::size_t n) {
	return dsp::fp::Signal{float_blocks.input.data() + stream * kMaxBlock, n};
}

/// @return The output stream @p stream's first @p n samples.
[[nodiscard]] dsp::fp::Buffer output_stream(std::size_t stream, std::size_t n) {
	return dsp::fp::Buffer{float_blocks.output.data() + stream * kMaxBlock, n};
}

} // namespace

template <class S>
void FloatMonoCase<S>::setup(std::size_t n) {
	flush_.enable();
	fill_float_input();
	in_ = input_stream(0, n);
	out_ = output_stream(0, n);
	block_ = 0;
	mono_.configure(S::settings(0, 0));
	mono_.reset();
}

template <class S>
void FloatMonoCase<S>::block(std::size_t /*n*/) {
	if constexpr (S::kRetargets) {
		mono_.configure(S::settings(0, block_++));
	}
	mono_.render_block(in_, out_);
}

template <class S, std::size_t N>
void FloatPolyCase<S, N>::setup(std::size_t n) {
	flush_.enable();
	fill_float_input();
	block_ = 0;
	for (std::size_t lane = 0; lane < N; ++lane) {
		in_[lane] = input_stream(lane, n);
		out_[lane] = output_stream(lane, n);
		poly_.configure(lane, S::settings(lane, 0));
		poly_.reset(lane);
	}
}

template <class S, std::size_t N>
void FloatPolyCase<S, N>::block(std::size_t /*n*/) {
	if constexpr (S::kRetargets) {
		for (std::size_t lane = 0; lane < N; ++lane) {
			poly_.configure(lane, S::settings(lane, block_));
		}
		++block_;
	}
	poly_.render_block(in_, out_);
}

template <class S, std::size_t N>
void FloatLinkedCase<S, N>::setup(std::size_t n) {
	using F = dsp::Frame<dsp::f32, N>;
	static_assert(sizeof(F) == N * sizeof(dsp::f32) && N <= kMaxLanes);
	flush_.enable();
	fill_float_input();
	in_ = dsp::Signal<F>{reinterpret_cast<const F*>(float_blocks.input.data()), n};
	out_ = dsp::Buffer<F>{reinterpret_cast<F*>(float_blocks.output.data()), n};
	linked_.configure(S::settings(0, 0));
	linked_.reset();
}

template <class S, std::size_t N>
void FloatLinkedCase<S, N>::block(std::size_t /*n*/) {
	linked_.render_block(in_, out_);
}

void FloatBankCase::setup(std::size_t n) {
	flush_.enable();
	fill_float_input();
	in_ = input_stream(0, n);
	out_ = output_stream(0, n);
	for (std::size_t lane = 0; lane < 4; ++lane) {
		bank_.configure(lane, FloatSvfBandSettings::settings(lane, 0));
		bank_.reset(lane);
	}
}

void FloatBankCase::block(std::size_t /*n*/) {
	bank_.render_block(in_, out_);
}

template class FloatMonoCase<FloatOnePoleSettings>;
template class FloatMonoCase<FloatSmootherSettings>;
template class FloatMonoCase<FloatSvfSettings>;
template class FloatPolyCase<FloatOnePoleSettings, 2>;
template class FloatPolyCase<FloatOnePoleSettings, 4>;
template class FloatPolyCase<FloatSmootherSettings, 4>;
template class FloatPolyCase<FloatSvfSettings, 2>;
template class FloatPolyCase<FloatSvfSettings, 4>;
template class FloatLinkedCase<FloatOnePoleSettings, 2>;
template class FloatLinkedCase<FloatSvfSettings, 2>;

} // namespace deluge::bench
