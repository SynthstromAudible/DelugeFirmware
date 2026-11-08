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
/// @brief The float_voice list's cases: the float oscillators, the envelope and a whole voice in Mono, Poly and Linked.
/// @details Every case is a generator: it reads nothing and writes its f32 output block, under flush-to-zero
/// (FloatBenchFlush, as in float_filter_cases.cpp: set at setup and kept for the run on the device, held for one block
/// at a time on the host). A kernel with a gate gets its gate events at the start of each block, inside the timed
/// region (float_gate_at).

#include "bench/cases.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::bench {

namespace {

/// @brief The f32 output: kMaxBlock samples for each of kMaxLanes streams, one stream after another.
/// @details Every stream is 512 bytes long, so no two streams sit a multiple of 1 KB apart (the shared blocks' rule,
/// in cases.cpp). The generators read no input block; the table oscillator's reads are of the saw bands.
struct VoiceBlocks {
	alignas(16) std::array<dsp::f32, kMaxBlock * kMaxLanes> output; ///< The streams written.
};

static_assert((kMaxBlock * sizeof(dsp::f32)) % 1024 == 512, "streams are 512 bytes apart");

VoiceBlocks voice_blocks{};

/// @return The output stream @p stream's first @p n samples.
[[nodiscard]] dsp::fp::Buffer output_stream(std::size_t stream, std::size_t n) {
	return dsp::fp::Buffer{voice_blocks.output.data() + stream * kMaxBlock, n};
}

/// @brief Runs the gate event @p gate on @p component, on @p lane when given (Poly), else on every lane.
template <class S, class C, class... Lane>
void run_gate(C& component, FloatGate gate, Lane... lane) {
	switch (gate) {
	case FloatGate::ON:
		component.template on<S::kGateOn>(lane...);
		break;
	case FloatGate::OFF:
		component.template on<S::kGateOff>(lane...);
		break;
	case FloatGate::NONE:
		break;
	}
}

/// @brief Samples rendered at setup to bring a held kernel into sustain: every stage before it ends well within.
constexpr std::size_t kSustainSamples = 8192;

/// @brief Renders a held kernel's blocks until it sits in sustain.
/// @param n The block size.
/// @param render Renders one block.
template <class Render>
void settle(std::size_t n, Render&& render) {
	for (std::size_t done = 0; done < kSustainSamples; done += n) {
		render();
	}
}

} // namespace

template <class S>
void FloatSourceMonoCase<S>::setup(std::size_t n) {
	flush_.enable();
	if constexpr (S::kSawBands) {
		dsp::fp::build_saw_bands();
	}
	out_ = output_stream(0, n);
	block_ = 0;
	mono_.configure(S::settings(0));
	mono_.reset();
	if constexpr (S::kHeld) {
		run_gate<S>(mono_, FloatGate::ON);
		settle(n, [&] { mono_.render_block(out_); });
	}
}

template <class S>
void FloatSourceMonoCase<S>::block(std::size_t /*n*/) {
	[[maybe_unused]] const auto flush = flush_.scope();
	if constexpr (S::kGated) {
		run_gate<S>(mono_, float_gate_at(0, block_));
	}
	++block_;
	mono_.render_block(out_);
}

template <class S, std::size_t N>
void FloatSourcePolyCase<S, N>::setup(std::size_t n) {
	flush_.enable();
	if constexpr (S::kSawBands) {
		dsp::fp::build_saw_bands();
	}
	block_ = 0;
	for (std::size_t lane = 0; lane < N; ++lane) {
		out_[lane] = output_stream(lane, n);
		poly_.configure(lane, S::settings(lane));
		poly_.reset(lane);
	}
	if constexpr (S::kHeld) {
		for (std::size_t lane = 0; lane < N; ++lane) {
			run_gate<S>(poly_, FloatGate::ON, lane);
		}
		settle(n, [&] { poly_.render_block(out_); });
	}
}

template <class S, std::size_t N>
void FloatSourcePolyCase<S, N>::block(std::size_t /*n*/) {
	[[maybe_unused]] const auto flush = flush_.scope();
	if constexpr (S::kGated) {
		for (std::size_t lane = 0; lane < N; ++lane) {
			run_gate<S>(poly_, float_gate_at(lane, block_), lane);
		}
	}
	++block_;
	poly_.render_block(out_);
}

template <class S, std::size_t N>
void FloatSourceLinkedCase<S, N>::setup(std::size_t n) {
	using F = dsp::Frame<dsp::f32, N>;
	static_assert(sizeof(F) == N * sizeof(dsp::f32) && N <= kMaxLanes);
	flush_.enable();
	if constexpr (S::kSawBands) {
		dsp::fp::build_saw_bands();
	}
	out_ = dsp::Buffer<F>{reinterpret_cast<F*>(voice_blocks.output.data()), n};
	block_ = 0;
	linked_.configure(S::settings(0));
	linked_.reset();
	if constexpr (S::kHeld) {
		run_gate<S>(linked_, FloatGate::ON);
		settle(n, [&] { linked_.render_block(out_); });
	}
}

template <class S, std::size_t N>
void FloatSourceLinkedCase<S, N>::block(std::size_t /*n*/) {
	[[maybe_unused]] const auto flush = flush_.scope();
	if constexpr (S::kGated) {
		run_gate<S>(linked_, float_gate_at(0, block_));
	}
	++block_;
	linked_.render_block(out_);
}

template class FloatSourceMonoCase<FloatBlepSettings>;
template class FloatSourceMonoCase<FloatTableSettings>;
template class FloatSourceMonoCase<FloatEnvelopeSettings>;
template class FloatSourceMonoCase<FloatBlepVoiceSettings>;
template class FloatSourceMonoCase<FloatBlepVoiceSustainSettings>;
template class FloatSourcePolyCase<FloatBlepSettings, 2>;
template class FloatSourcePolyCase<FloatBlepSettings, 4>;
template class FloatSourcePolyCase<FloatTableSettings, 2>;
template class FloatSourcePolyCase<FloatTableSettings, 4>;
template class FloatSourcePolyCase<FloatEnvelopeSettings, 4>;
template class FloatSourcePolyCase<FloatEnvelopeSustainSettings, 4>;
template class FloatSourcePolyCase<FloatBlepVoiceSettings, 2>;
template class FloatSourcePolyCase<FloatBlepVoiceSettings, 4>;
template class FloatSourcePolyCase<FloatBlepVoiceSustainSettings, 4>;
template class FloatSourcePolyCase<FloatTableVoiceSettings, 4>;
template class FloatSourceLinkedCase<FloatBlepVoiceSettings, 2>;

} // namespace deluge::bench
