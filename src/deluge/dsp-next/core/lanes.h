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

#include "dsp-next/core/frame.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"

#include <concepts>
#include <cstddef>
#include <utility>

/// @file
/// @brief Lanes<K, N>: one kernel run on N independent streams, interleaved as frames.
namespace deluge::dsp {

namespace detail {

/// @brief How a block's frame type @p F holds its lanes: the lane type, the lane count, and the conversions to and
/// from a @ref Frame that the loop steps.
template <class F>
struct frame_lanes;

/// @brief A @ref Frame is its own lanes.
template <class T, std::size_t N>
struct frame_lanes<Frame<T, N>> {
	using lane_type = T;                    ///< One lane's sample.
	static constexpr std::size_t lanes = N; ///< The lane count.

	/// @brief @p f's lanes.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Frame<T, N> to_lanes(Frame<T, N> f) { return f; }
	/// @brief The frame holding @p f's lanes.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Frame<T, N> from_lanes(Frame<T, N> f) { return f; }
};

/// @brief A @ref StereoSample is two q31 lanes, left in lane 0, converted by bit cast (@ref to_frame).
template <>
struct frame_lanes<StereoSample> {
	using lane_type = q31;                  ///< One channel's sample, Q31.
	static constexpr std::size_t lanes = 2; ///< Left and right.

	/// @brief @p s as two lanes: left in lane 0, right in lane 1.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Frame<q31, 2> to_lanes(StereoSample s) { return to_frame(s); }
	/// @brief Two lanes as a stereo frame: lane 0 to left, lane 1 to right.
	[[nodiscard]] [[gnu::always_inline]] static constexpr StereoSample from_lanes(Frame<q31, 2> f) {
		return to_stereo(f);
	}
};

} // namespace detail

/// @brief A scalar kernel run on N independent streams: one Coefficients shared by every lane, one State per lane,
/// over blocks of interleaved frames, lane 0 stepped first in every frame.
/// @tparam K The kernel: a generator, processor or mixer kernel whose step is one sample (`step_type` is
/// `value_type`). A staged kernel has no step to run per lane and a control kernel no per-sample loop, so both are
/// rejected at the class.
/// @tparam N The number of lanes. At least 1.
/// @tparam F The frame type: N samples of K's `value_type`, lane 0 first. `Frame<value_type, N>` by default; a
/// @ref StereoSample holds two q31 lanes, left in lane 0. @ref detail::frame_lanes maps it to the lanes the loop
/// steps.
/// @details
/// **Shared coefficients.** `configure` calls `K::coefficients` once; every lane steps with the result.
///
/// **Per block.** `renderBlock` calls K's `prepare(coefficients, state, n)` once per lane, when K declares one, since a
/// Block may depend on the State: lane 0's first, then lane 1's, and on, so a `prepare` drawing from a stream every
/// lane shares (noise) draws in lane order on every block. Each lane steps with its own Block. Then, for a moded
/// kernel, it dispatches once on `coefficients().mode` through with_mode, and inside that one dispatch a single loop
/// runs every frame in the block's mode: a mode change set by `configure` takes effect at the next block, and every
/// State carries across it untouched.
///
/// **Order.** Each frame steps lane 0, then 1, … N − 1, into a local frame, then stores it. A kernel drawing from a
/// stream every lane shares (noise) therefore draws in lane order on every frame; at N = 2 that is left before right.
///
/// **The loop.** One input pointer per input block and one output pointer step over whole frames. Each frame is
/// loaded whole, every input pointer is stepped, the lanes are stepped, and then the frame is stored: the step for the
/// next loads comes before the store, since on the Cortex-A9 a load whose address is computed after a pending store
/// waits for that store's data. Every input frame is read whole before any lane of its output is written, so an input
/// block may be the same buffer as the output.
///
/// **Single samples.** `render(…)` is a one-frame block: it builds one-frame spans and runs a whole block's `prepare`,
/// dispatch and loop, which under the shipping `-fno-inline-functions` costs several out-of-line `std::span` calls per
/// call. It is a convenience for tests and for control-rate values, not for per-frame use in shipping code.
///
/// **Inlining.** As in @ref Mono: the app builds with `-fno-inline-functions`, so `render`, `renderBlock`, the loop and
/// the per-frame lane step are `[[gnu::always_inline]]`. The loop runs on local copies of the coefficients and every
/// State, taken after `prepare` and written back after the block, so that they stay in registers even though the
/// output may alias the component. The States and Blocks are built-in arrays in a struct, whose copy constructs nothing
/// and whose indexing is no call.
template <kernel K, std::size_t N, class F = Frame<typename K::value_type, N>>
requires(N >= 1 && !staged_kernel<K> && !control_kernel<K>)
class Lanes {
	using Coefficients = typename K::Coefficients; ///< K's coefficients, computed on configure, shared by every lane.
	using State = typename K::State;               ///< K's state, one per lane, carried from block to block.
	using Block = kernel_block_t<K>;               ///< K's per-block values, one per lane, or EmptyBlock.
	using Access = detail::frame_lanes<F>;         ///< How F holds its lanes.
	using LaneFrame = Frame<typename Access::lane_type, N>; ///< The lanes the loop steps.

	/// @brief One State per lane, lane 0 first.
	/// @details A struct around a built-in array: a copy is one assignment that constructs nothing, so the loop's
	/// local copy never passes its address to an out-of-line constructor and stays in registers; and indexing is no
	/// call, as std::array's is under -fno-inline-functions.
	struct States {
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		State lane[N]; ///< The States, lane 0 first.
	};

	/// @brief One Block per lane, lane 0 first, held as @ref States is.
	struct Blocks {
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		Block lane[N]; ///< The Blocks, lane 0 first.
	};

	static_assert(std::same_as<typename K::step_type, typename K::value_type>,
	              "Lanes steps one sample per lane: K's step type is its value type");
	static_assert(std::same_as<typename Access::lane_type, typename K::value_type> && Access::lanes == N,
	              "the frame type holds N samples of K's value type");

public:
	using value_type = F; ///< A block's frame: one sample per lane.

	/// @brief Value-initialised Coefficients and State{} in every lane.
	[[gnu::always_inline]] Lanes() = default;

	/// @brief Configured from @p settings, with State{} in every lane.
	[[gnu::always_inline]] explicit Lanes(const typename K::Settings& settings)
	    : coefficients_{K::coefficients(settings)} {}

	/// @brief Store the coefficients for @p settings, computed once for every lane; they apply from the next block,
	/// every State untouched.
	[[gnu::always_inline]] void configure(const typename K::Settings& settings) {
		coefficients_ = K::coefficients(settings);
	}

	/// @brief The coefficients from the last configure, including any owner-facing values the kernel exposes.
	[[nodiscard]] [[gnu::always_inline]] const Coefficients& coefficients() const { return coefficients_; }

	/// @brief Return every lane to the reset State: State{}, or whatever K's `static void reset(State&)` quirk sets
	/// when K declares one.
	[[gnu::always_inline]] void reset() {
		for (State& s : states_.lane) {
			if constexpr (detail::declares_reset<K>) {
				K::reset(s);
			}
			else {
				s = State{};
			}
		}
	}

	/// @brief Lane @p lane's state between blocks. @p lane is less than N.
	[[nodiscard]] [[gnu::always_inline]] const State& state(std::size_t lane) const { return states_.lane[lane]; }

	/// @brief Generate one frame: a one-frame block, so one `prepare(…, 1)` per lane and one mode dispatch per call.
	/// @details A convenience for tests and control rate, not for per-frame use in shipping code (see **Single
	/// samples** in the class documentation).
	[[nodiscard]] [[gnu::always_inline]] value_type render()
	requires generator_kernel<K>
	{
		value_type out{};
		renderBlock(Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Fill @p out with the next `out.size()` frames, lane 0 first in every frame.
	[[gnu::always_inline]] void renderBlock(Buffer<value_type> out)
	requires generator_kernel<K>
	{
		runBlock(out);
	}

	/// @brief Process one frame: a one-frame block, so one `prepare(…, 1)` per lane and one mode dispatch per call.
	/// @details A convenience for tests and control rate, not for per-frame use in shipping code (see **Single
	/// samples** in the class documentation).
	[[nodiscard]] [[gnu::always_inline]] value_type render(value_type x)
	requires processor_kernel<K>
	{
		value_type out{};
		renderBlock(Signal<value_type>{&x, 1}, Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Process `out.size()` frames of @p in into @p out, lane 0 first in every frame.
	/// @param in At least `out.size()` frames; may be the same buffer as @p out.
	/// @param out The block to fill, every frame of it.
	[[gnu::always_inline]] void renderBlock(Signal<value_type> in, Buffer<value_type> out)
	requires processor_kernel<K>
	{
		runBlock(out, in);
	}

	/// @brief Mix one pair of frames: a one-frame block, so one `prepare(…, 1)` per lane and one mode dispatch per
	/// call.
	/// @details A convenience for tests and control rate, not for per-frame use in shipping code (see **Single
	/// samples** in the class documentation).
	[[nodiscard]] [[gnu::always_inline]] value_type render(value_type a, value_type b)
	requires mixer_kernel<K>
	{
		value_type out{};
		renderBlock(Signal<value_type>{&a, 1}, Signal<value_type>{&b, 1}, Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Mix `out.size()` frames of @p a and @p b into @p out, lane 0 first in every frame.
	/// @param a,b At least `out.size()` frames each; either may be the same buffer as @p out.
	/// @param out The block to fill, every frame of it.
	[[gnu::always_inline]] void renderBlock(Signal<value_type> a, Signal<value_type> b, Buffer<value_type> out)
	requires mixer_kernel<K>
	{
		runBlock(out, a, b);
	}

private:
	/// @brief Render one block: `prepare` once per lane in lane order, one mode dispatch, one loop.
	/// @param out The block to fill.
	/// @param in The input blocks: none, one or two.
	/// @details `prepare` runs on the member States, before the local copies are taken, so the copies' addresses never
	/// escape to a `prepare` that is not inlined.
	template <typename... In>
	[[gnu::always_inline]] void runBlock(Buffer<value_type> out, In... in) {
		const std::size_t n = out.size();
		const Blocks blocks = prepareLanes(n, std::make_index_sequence<N>{});
		const Coefficients c = coefficients_;
		States s = states_;
		value_type* const dst = out.data();
		kernel_ops::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () {
			loop<M>(c, blocks, s, dst, static_cast<std::ptrdiff_t>(n), in.data()...);
		});
		states_ = s;
	}

	/// @brief Every lane's Block for a block of @p n frames: K's `prepare` on each member State, lane 0 first.
	template <std::size_t... Lane>
	[[nodiscard]] [[gnu::always_inline]] Blocks prepareLanes(std::size_t n, std::index_sequence<Lane...> /*lanes*/) {
		// A braced list evaluates its elements left to right: lane 0 prepares first.
		return Blocks{{kernel_ops::prepare_block<K>(coefficients_, states_.lane[Lane], n)...}};
	}

	/// @brief One frame at mode @p M: lane 0's `step`, then lane 1's, … N − 1's, each with its own Block and State.
	/// @param x The frame's inputs, already loaded whole: none, one or two lane frames.
	/// @return The frame's output lanes.
	template <auto M, typename... X>
	[[nodiscard]] [[gnu::always_inline]] static LaneFrame stepFrame(const Coefficients& c, const Blocks& blocks,
	                                                                States& s, const X&... x) {
		const auto step_lane = [&]<std::size_t Lane> [[gnu::always_inline]] () {
			return kernel_ops::step_at<K, M>(c, blocks.lane[Lane], s.lane[Lane], x.lane[Lane]...);
		};
		LaneFrame y;
		// A comma fold runs its operands left to right: lane 0 first.
		[&]<std::size_t... Lane> [[gnu::always_inline]] (std::index_sequence<Lane...>) {
			((y.lane[Lane] = step_lane.template operator()<Lane>()), ...);
		}(std::make_index_sequence<N>{});
		return y;
	}

	/// @brief @p n frames of the input blocks @p src (none, one or two) into @p dst, one @ref stepFrame per frame.
	/// @details Each frame's inputs are loaded whole and the input pointers stepped before the frame is stepped, and
	/// the frame is stored after: the pointers for the next loads never wait behind the store, and an input may be the
	/// output.
	template <auto M, typename... Src>
	[[gnu::always_inline]] static void loop(const Coefficients& c, const Blocks& blocks, States& s, value_type* dst,
	                                        std::ptrdiff_t n, const Src*... src) {
		value_type* const end = dst + n;
		while (dst != end) {
			const LaneFrame y = stepFrame<M>(c, blocks, s, Access::to_lanes(*src++)...);
			*dst++ = Access::from_lanes(y);
		}
	}

	Coefficients coefficients_{}; ///< K's coefficients for the configured settings, shared by every lane.
	States states_{};             ///< Each lane's state, lane 0 first, carried from block to block.
};

} // namespace deluge::dsp
