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
#pragma once

#include "dsp-next/core/copies.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/poly_kernel.h"
#include "dsp-next/core/sample.h"

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>

/// @file
/// @brief Bank<K, N, Mix>: one signal into N copies of a float kernel, each with its own settings, mixed to the
/// output: a filter, comb or allpass bank.
namespace deluge::dsp {

namespace mix {
/// @brief The lanes summed in lane order.
struct Sum {};
/// @brief Each lane times its gain, summed in lane order.
struct Weighted {};
/// @brief The even lanes summed to left, the odd lanes to right, each in lane order.
struct StereoPair {};
} // namespace mix

/// @brief N copies of @p K reading one input: each sample broadcasts to every lane, one step over Pack<f32, N> steps
/// every copy, and @p Mix reduces the lanes to the output.
/// @details Settings per lane and one mode, as @ref Poly; a K that declares `prepare` has it called once per block
/// on the packed coefficients and state, as Poly does. Each copy is bit for bit Mono<K> with its settings; the mix
/// adds the lanes in lane order, unfused (a lane's gain multiplies, then the sum adds), so the output is the Mono
/// outputs' sum in that order. N = 4 StereoPair: left = lane 0 + lane 2, right = lane 1 + lane 3; N = 2: left =
/// lane 0, right = lane 1. The mix takes N samples at a time: their N packs transpose into one pack per lane, which
/// add as packs in lane order and store as one vector; the last n mod N samples mix one at a time.
template <poly_kernel K, std::size_t N, class Mix>
requires(processor_kernel<K> && (N == 2 || N == 4))
class Bank : public detail::Copies<K, N> {
	using Base = detail::Copies<K, N>;
	using V = Pack<f32, N>;
	using Mode = K::Mode;
	using typename Base::PackedCoefficients;
	using typename Base::PackedState;

public:
	using Base::configure;
	using Base::reset;
	using Base::set_mode;
	using Base::state;

	/// @brief Lane @p lane's gain in the mix; a lane never set has gain 1.
	void set_gain(std::size_t lane, f32 gain)
	requires std::same_as<Mix, mix::Weighted>
	{
		assert(lane < N);
		gains_.set(lane, gain);
	}

	/// @brief One block into one output; the mode is dispatched once per block.
	void render_block(fp::Signal in, fp::Buffer out)
	requires(!std::same_as<Mix, mix::StereoPair>)
	{
		assert(out.size() == in.size());
		with_mode<typename K::Modes>(this->coefficients().mode, [&]<Mode M> { run<M>(in, out.data(), nullptr); });
	}

	/// @brief One block into left and right; the mode is dispatched once per block.
	void render_block(fp::Signal in, fp::Buffer left, fp::Buffer right)
	requires std::same_as<Mix, mix::StereoPair>
	{
		assert(left.size() == in.size() && right.size() == in.size());
		with_mode<typename K::Modes>(this->coefficients().mode, [&]<Mode M> { run<M>(in, left.data(), right.data()); });
	}

private:
	/// N samples' outputs: element t is every lane's output for the t-th of them.
	using Frames = std::array<V, N>;

	/// One block into @p first (the output, or left) and @p second (right, for StereoPair): N samples at a time, the
	/// last n mod N one at a time.
	template <Mode M>
	void run(fp::Signal in, f32* first, f32* second) {
		const PackedCoefficients c = this->coefficients();
		PackedState s = this->packed_state();
		const V gains = gains_;
		kernel_block_t<K> block{};
		if constexpr (detail::declares_prepare<K>) {
			block = K::prepare(c, s, in.size());
		}
		std::size_t i = 0;
		for (; i + N <= in.size(); i += N) {
			Frames y;
			for (std::size_t t = 0; t < N; ++t) {
				y[t] = K::template step<M>(c, block, s, V{in[i + t]});
			}
			mix_frames(y, gains, first, second, i);
		}
		for (; i < in.size(); ++i) {
			mix_one(K::template step<M>(c, block, s, V{in[i]}), gains, first, second, i);
		}
		this->set_packed_state(s);
	}

	/// Mixes the outputs @p y of samples @p i … i + N − 1 into @p first (and @p second): the N packs transpose into
	/// one per lane, which add as packs in lane order.
	[[gnu::always_inline]] static void mix_frames(Frames y, V gains, f32* first, f32* second, std::size_t i) {
		if constexpr (std::same_as<Mix, mix::Weighted>) {
			for (V& frame : y) {
				frame = frame * gains;
			}
		}
		transpose(y); // y[l] = lane l's outputs for the N samples
		if constexpr (std::same_as<Mix, mix::StereoPair>) {
			if constexpr (N == 4) {
				(y[0] + y[2]).store(first + i);
				(y[1] + y[3]).store(second + i);
			}
			else {
				y[0].store(first + i);
				y[1].store(second + i);
			}
		}
		else {
			V total = y[0];
			for (std::size_t l = 1; l < N; ++l) {
				total = total + y[l];
			}
			total.store(first + i);
		}
	}

	/// Mixes the outputs @p y of sample @p i into @p first (and @p second), as @ref mix_frames does each sample.
	[[gnu::always_inline]] static void mix_one(V y, V gains, f32* first, f32* second, std::size_t i) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		if constexpr (std::same_as<Mix, mix::Weighted>) {
			y = y * gains;
		}
		if constexpr (std::same_as<Mix, mix::StereoPair>) {
			if constexpr (N == 4) {
				first[i] = y.get(0) + y.get(2);
				second[i] = y.get(1) + y.get(3);
			}
			else {
				first[i] = y.get(0);
				second[i] = y.get(1);
			}
		}
		else {
			first[i] = sum(y);
		}
	}

	V gains_{1.f}; ///< Weighted: each lane's gain; 1 until set_gain.
};

} // namespace deluge::dsp
