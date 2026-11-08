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
#include <cstddef>
#include <cstdint>

/// @file
/// @brief Poly<K, N>: N copies of a float kernel (processor or generator), one per NEON lane, each with its own
/// settings and its own signal.
namespace deluge::dsp {

/// @brief N copies of @p K stepped as one: each frame, lane l carries copy l's sample, and one step over
/// Pack<f32, N> steps them all.
/// @details **Settings per lane.** `configure(lane, settings)` runs K's scalar `coefficients` and writes the result
/// into that lane, so each lane's arithmetic is the scalar kernel's, bit for bit (the step pins its order).
/// **One mode.** Every lane runs one mode, dispatched once per block. **Per block.** A K that declares `prepare` has
/// it called once, on the packed coefficients and state, before the block's frames, as Mono does for one copy.
/// **Parking.** A fresh Poly has every lane parked; `configure` un-parks a lane. A parked lane is fed zero, its output
/// is not written, and its state is reset to State{} after every block; lanes share nothing, so it perturbs no
/// other. **Blocks.** Every unparked lane's input and output spans have the block's length (a parked lane's may be
/// empty); frames go in and out N at a time through a transpose, the last n mod N frames lane by lane. **Mode.**
/// Configure and set_mode set the mode; configure asserts it matches once any lane has been configured, and that
/// never clears, so the mode stays the last one set, even once every lane is parked. **Empty blocks.** A block with
/// every lane parked, or of length 0, steps nothing but still runs `prepare` with n = 0, as Mono does on an empty
/// block. **Generators.** A generator has no input: its `render_block` takes the outputs alone, and a parked lane's
/// state is stepped with the rest and reset after the block.
template <poly_kernel K, std::size_t N>
requires(N == 2 || N == 4)
class Poly : public detail::Copies<K, N> {
	using Base = detail::Copies<K, N>;
	using V = Pack<f32, N>;
	using Mode = K::Mode;
	using typename Base::PackedCoefficients;
	using typename Base::PackedState;

	/// One bit per lane, lane l at bit l.
	using LaneMask = uint32_t;
	static constexpr LaneMask kEveryLane = (LaneMask{1} << N) - 1;

public:
	using Base::reset;
	using Base::set_mode;
	using Base::state;

	/// @brief Sets lane @p lane from @p settings, and un-parks it. Its mode must be the lanes' mode once any lane is
	/// configured.
	void configure(std::size_t lane, const K::Settings& settings) {
		Base::configure(lane, settings);
		parked_ &= ~bit(lane);
	}
	/// @brief Lane @p lane at rest, fed zero, its output unwritten, until it is configured again.
	void park(std::size_t lane) {
		reset(lane);
		parked_ |= bit(lane);
	}
	/// @brief Whether lane @p lane is parked.
	[[nodiscard]] bool parked(std::size_t lane) const { return (parked_ & bit(lane)) != 0; }

	/// @brief One block: lane l reads @p in[l] and writes @p out[l]. Every unparked lane's spans have the same length;
	/// a parked lane's may be empty.
	void render_block(std::array<fp::Signal, N> in, std::array<fp::Buffer, N> out)
	requires processor_kernel<K>
	{
		const std::size_t n = block_length(in);
		assert(block_length(out) == n);
		with_mode<typename K::Modes>(this->coefficients().mode, [&]<Mode M> { run<M>(in, out, n); });
	}

	/// @brief One block of a generator: lane l writes @p out[l]. Every unparked lane's span has the same length; a
	/// parked lane's may be empty.
	void render_block(std::array<fp::Buffer, N> out)
	requires generator_kernel<K>
	{
		const std::size_t n = block_length(out);
		const std::array<fp::Signal, N> no_input{};
		with_mode<typename K::Modes>(this->coefficients().mode, [&]<Mode M> { run<M>(no_input, out, n); });
	}

private:
	/// The length of the unparked lanes' spans in @p spans, which must agree; 0 when every lane is parked.
	template <class Span>
	[[nodiscard]] std::size_t block_length(const std::array<Span, N>& spans) const {
		std::size_t n = 0;
		bool first = true;
		for (std::size_t l = 0; l < N; ++l) {
			if (!parked(l)) {
				if (first) {
					n = spans[l].size();
					first = false;
				}
				assert(spans[l].size() == n);
			}
		}
		return n;
	}

	[[nodiscard]] static constexpr LaneMask bit(std::size_t lane) {
		assert(lane < N);
		return LaneMask{1} << lane;
	}

	/// Every lane's input and output, as pointers held in the loop's registers.
	struct LanePointers {
		std::array<const f32*, N> in;
		std::array<f32*, N> out;
	};

	template <Mode M>
	void run(const std::array<fp::Signal, N>& in, const std::array<fp::Buffer, N>& out, std::size_t n) {
		const PackedCoefficients c = this->coefficients();
		PackedState s = this->packed_state();
		const LaneMask parked = parked_;
		LanePointers p{};
		for (std::size_t l = 0; l < N; ++l) {
			p.in[l] = in[l].data();
			p.out[l] = out[l].data();
		}
		kernel_block_t<K> block{};
		if constexpr (detail::declares_prepare<K>) {
			block = K::prepare(c, s, n);
		}
		std::size_t i = 0;
		for (; i + N <= n; i += N) {
			step_frames<M>(c, block, s, parked, p, i);
		}
		for (; i < n; ++i) {
			step_frame<M>(c, block, s, parked, p, i);
		}
		for (std::size_t l = 0; l < N; ++l) {
			if ((parked & bit(l)) != 0) {
				set_lane(s, l, typename K::State{});
			}
		}
		this->set_packed_state(s);
	}

	/// N frames, i … i + N − 1, through a transpose in and out.
	template <Mode M>
	[[gnu::always_inline]] static void step_frames(const PackedCoefficients& c, const kernel_block_t<K>& block,
	                                               PackedState& s, LaneMask parked, const LanePointers& p,
	                                               std::size_t i) {
		std::array<V, N> frames{};
		if constexpr (processor_kernel<K>) {
			for (std::size_t l = 0; l < N; ++l) {
				frames[l] = (parked & bit(l)) != 0 ? V{0.f} : V::load(p.in[l] + i);
			}
			transpose(frames); // frames[t] lane l = copy l's sample i + t
		}
		for (std::size_t t = 0; t < N; ++t) {
			if constexpr (processor_kernel<K>) {
				frames[t] = K::template step<M>(c, block, s, frames[t]);
			}
			else {
				frames[t] = K::template step<M>(c, block, s);
			}
		}
		transpose(frames); // frames[l] = copy l's samples i … i + N − 1
		for (std::size_t l = 0; l < N; ++l) {
			if ((parked & bit(l)) == 0) {
				frames[l].store(p.out[l] + i);
			}
		}
	}

	/// One frame, i, lane by lane.
	template <Mode M>
	[[gnu::always_inline]] static void step_frame(const PackedCoefficients& c, const kernel_block_t<K>& block,
	                                              PackedState& s, LaneMask parked, const LanePointers& p,
	                                              std::size_t i) {
		V y{};
		if constexpr (processor_kernel<K>) {
			V x{0.f};
			for (std::size_t l = 0; l < N; ++l) {
				if ((parked & bit(l)) == 0) {
					x.set(l, p.in[l][i]);
				}
			}
			y = K::template step<M>(c, block, s, x);
		}
		else {
			y = K::template step<M>(c, block, s);
		}
		for (std::size_t l = 0; l < N; ++l) {
			if ((parked & bit(l)) == 0) {
				p.out[l][i] = y.get(l);
			}
		}
	}

	LaneMask parked_ = kEveryLane; ///< The lanes held at rest, lane l at bit l: every lane until it is configured.
};

} // namespace deluge::dsp
