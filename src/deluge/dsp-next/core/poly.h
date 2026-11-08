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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/poly_kernel.h"
#include "dsp-next/core/sample.h"

#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>

/// @file
/// @brief Poly<K, N>: N copies of a float kernel, one per NEON lane, each with its own settings and its own signal.
namespace deluge::dsp {

/// @brief N copies of @p K stepped as one: each frame, lane l carries copy l's sample, and one step over
/// Pack<f32, N> steps them all.
/// @details **Settings per lane.** `configure(lane, settings)` runs K's scalar `coefficients` and writes the result
/// into that lane, so each lane's arithmetic is the scalar kernel's, bit for bit (the step pins its order).
/// **One mode.** Every lane runs one mode, dispatched once per block. **Per block.** A K that declares `prepare` has
/// it called once, on the packed coefficients and state, before the block's frames, as Mono does for one copy.
/// **Parking.** A parked lane is fed zero, its output is not written, and its state is held at rest after every
/// block; lanes share nothing, so it perturbs no other. **Blocks.** Every input and output span has the block's
/// length (a parked lane's spans may be empty); frames go in and out N at a time through a transpose, the last n mod N
/// frames lane by lane. **Mode.** The first configure fixes the mode and `configured_` is never cleared, so it stays
/// the last one configured until set_mode changes it, even once every lane is parked. **Empty blocks.** A block with
/// every lane parked, or of length 0, steps nothing but still runs `prepare` with n = 0, as Mono does on an empty
/// block.
template <poly_kernel K, std::size_t N>
requires(N == 2 || N == 4)
class Poly {
	using V = Pack<f32, N>;
	using PackedCoefficients = K::template CoefficientsT<V>;
	using PackedState = K::template StateT<V>;
	using Mode = K::Mode;

public:
	/// @brief Sets lane @p lane from @p settings, and un-parks it. Its mode must be the bank's mode once any lane is
	/// configured.
	void configure(std::size_t lane, const K::Settings& settings) {
		assert(lane < N);
		const typename K::Coefficients c = K::coefficients(settings);
		assert(!configured_.any() || c.mode == coefficients_.mode);
		coefficients_.mode = c.mode;
		set_lane(coefficients_, lane, c);
		configured_.set(lane);
		parked_.reset(lane);
	}
	/// @brief Every lane's mode, from the next block.
	void set_mode(Mode mode) { coefficients_.mode = mode; }
	/// @brief Lane @p lane's state to K's reset state.
	void reset(std::size_t lane) {
		assert(lane < N);
		set_lane(state_, lane, typename K::State{});
	}
	/// @brief Lane @p lane at rest, fed zero, its output unwritten, until it is configured again.
	void park(std::size_t lane) {
		reset(lane);
		parked_.set(lane);
	}
	/// @brief Whether lane @p lane is parked.
	[[nodiscard]] bool parked(std::size_t lane) const { return parked_.test(lane); }
	/// @brief Lane @p lane's state.
	[[nodiscard]] K::State state(std::size_t lane) const {
		assert(lane < N);
		return get_lane<typename K::State>(state_, lane);
	}

	/// @brief One block: lane l reads @p in[l] and writes @p out[l]. Every unparked lane's spans have the same length;
	/// a parked lane's may be empty.
	void render_block(std::array<fp::Signal, N> in, std::array<fp::Buffer, N> out) {
		std::size_t n = 0;
		bool first = true;
		for (std::size_t l = 0; l < N; ++l) {
			if (!parked_.test(l)) {
				if (first) {
					n = in[l].size();
					first = false;
				}
				assert(in[l].size() == n && out[l].size() == n);
			}
		}
		with_mode<typename K::Modes>(coefficients_.mode, [&]<Mode M> { run<M>(in, out, n); });
	}

private:
	template <Mode M>
	void run(const std::array<fp::Signal, N>& in, const std::array<fp::Buffer, N>& out, std::size_t n) {
		const PackedCoefficients c = coefficients_;
		PackedState s = state_;
		kernel_block_t<K> block{};
		if constexpr (detail::declares_prepare<K>) {
			block = K::prepare(c, s, n);
		}
		std::size_t i = 0;
		for (; i + N <= n; i += N) {
			step_frames<M>(c, block, s, in, out, i);
		}
		for (; i < n; ++i) {
			step_frame<M>(c, block, s, in, out, i);
		}
		for (std::size_t l = 0; l < N; ++l) {
			if (parked_.test(l)) {
				set_lane(s, l, typename K::State{});
			}
		}
		state_ = s;
	}

	/// N frames, i … i + N − 1, through a transpose in and out.
	template <Mode M>
	void step_frames(const PackedCoefficients& c, const kernel_block_t<K>& block, PackedState& s,
	                 const std::array<fp::Signal, N>& in, const std::array<fp::Buffer, N>& out, std::size_t i) const {
		std::array<V, N> frames{};
		for (std::size_t l = 0; l < N; ++l) {
			frames[l] = parked_.test(l) ? V{0.f} : V::load(&in[l][i]);
		}
		transpose(frames); // frames[t] lane l = copy l's sample i + t
		for (std::size_t t = 0; t < N; ++t) {
			frames[t] = K::template step<M>(c, block, s, frames[t]);
		}
		transpose(frames); // frames[l] = copy l's samples i … i + N − 1
		for (std::size_t l = 0; l < N; ++l) {
			if (!parked_.test(l)) {
				frames[l].store(&out[l][i]);
			}
		}
	}

	/// One frame, i, lane by lane.
	template <Mode M>
	void step_frame(const PackedCoefficients& c, const kernel_block_t<K>& block, PackedState& s,
	                const std::array<fp::Signal, N>& in, const std::array<fp::Buffer, N>& out, std::size_t i) const {
		V x{0.f};
		for (std::size_t l = 0; l < N; ++l) {
			if (!parked_.test(l)) {
				x.set(l, in[l][i]);
			}
		}
		const V y = K::template step<M>(c, block, s, x);
		for (std::size_t l = 0; l < N; ++l) {
			if (!parked_.test(l)) {
				out[l][i] = y.get(l);
			}
		}
	}

	PackedCoefficients coefficients_{}; ///< Every lane's coefficients; the mode every lane runs.
	PackedState state_{};               ///< Every lane's state.
	std::bitset<N> configured_{};       ///< The lanes configured since construction.
	std::bitset<N> parked_{};           ///< The lanes held at rest.
};

} // namespace deluge::dsp
