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
#include "dsp-next/core/stereo_vector.h"
#include "dsp-next/core/types.h"
#include <argon.hpp>
#include <argon/vectorize/for_each.hpp>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

/// @file
/// @brief Mono<K>: a kernel as a mono component, and the only loop a kernel runs in.
namespace deluge::dsp {

namespace detail {

/// @brief The mode a kernel without modes is stepped at: a placeholder the loop is templated on and never reads.
enum class NoMode : uint8_t { NONE };

/// @brief Whether the Argon loop step @p S is the partial last step of its block. Full steps are a distinct type.
template <typename S>
inline constexpr bool is_partial_step = false;

/// @brief A partial step of `argon::vectorize::for_each`.
template <typename T>
inline constexpr bool is_partial_step<argon::vectorize::Step<T, false>> = true;

/// @brief A partial step of `argon::vectorize::for_each_interleaved`.
template <typename T, std::size_t Stride>
inline constexpr bool is_partial_step<argon::vectorize::InterleavedStep<T, Stride, false>> = true;

/// @brief K's per-block values for a block of @p n samples: its `prepare` when it declares one, else an
/// EmptyBlock.
template <typename K>
[[gnu::always_inline]] inline kernel_block_t<K> prepare_block([[maybe_unused]] const typename K::Coefficients& c,
                                                              [[maybe_unused]] typename K::State& s,
                                                              [[maybe_unused]] std::size_t n) {
	if constexpr (declares_prepare<K>) {
		return K::prepare(c, s, n);
	}
	else {
		return kernel_block_t<K>{};
	}
}

/// @brief Call @p body.template operator()<M>() once: at the mode of @p c for a moded kernel, through with_mode,
/// else at NoMode::NONE.
template <typename K, typename Body>
[[gnu::always_inline]] inline void dispatch_mode([[maybe_unused]] const typename K::Coefficients& c, Body&& body) {
	if constexpr (moded_kernel<K>) {
		with_mode<typename K::Modes>(c.mode, body);
	}
	else {
		body.template operator()<NoMode::NONE>();
	}
}

/// @brief One `step` of K at mode @p M (ignored for a kernel without modes).
/// @param x The step's inputs: none, one or two step values, then an ActiveLanes for the counted partial step.
template <typename K, auto M, typename... X>
[[gnu::always_inline]] inline typename K::step_type
step_at(const typename K::Coefficients& c, const kernel_block_t<K>& block, typename K::State& s, X... x) {
	if constexpr (moded_kernel<K>) {
		return K::template step<M>(c, block, s, x...);
	}
	else {
		return K::step(c, block, s, x...);
	}
}

} // namespace detail

/// @brief A kernel as a mono component: one Coefficients, one State; the only loop.
/// @tparam K The kernel. Mono<K> is a @ref generator, @ref processor or @ref mixer to match K's step arity; a staged
/// kernel makes a @ref block_processor only.
/// @details
/// **Per block.** `renderBlock` calls K's `prepare(coefficients, state, n)` once, when K declares one (otherwise the
/// block is an EmptyBlock), then, for a moded kernel, dispatches once on `coefficients().mode` through with_mode.
/// Inside that one dispatch a single loop runs every sample in the block's mode, so a mode change set by
/// `configure` takes effect at the next block, and State carries across it untouched.
///
/// **The loop** is chosen from K's step type:
/// - `step_type == value_type` (q31 or StereoSample): a raw-pointer index loop, one `step` per sample.
/// - `Argon<q31>` over q31 blocks: `argon::vectorize::for_each<q31>`, one `step` per vector.
/// - StereoVector over StereoSample blocks: `argon::vectorize::for_each_interleaved<q31, 2>` over the interleaved
///   frames, de-interleaved on load and re-interleaved on store, one `step` per vector of frames.
///
/// **The partial last vector.** On the vector loops a block that is not a lane multiple ends with one partial step,
/// whose lanes past the end load as zero and are never stored; Argon gives that step its own type, which is how it is
/// told apart. A kernel that declares kPartialStepAdvancesByCount gets its counted `step(…, ActiveLanes{count})` on
/// that step, with the step's active-lane count, so its state advances by exactly the samples rendered; every other
/// step, and every step of a kernel that does not declare it, calls the plain `step`, which advances by a whole
/// vector. Every step loads before it stores, so the input and output blocks may be the same buffer. The distinction
/// by type holds on NEON (and SIMDe); on Helium (MVE) every step is predicated, so whole steps would also arrive
/// counted, with `n` equal to the lane count.
///
/// **Staged kernels** have no loop here: `renderBlock` calls `process(coefficients, state, in, out)` once, or
/// `process<M>` through with_mode when the kernel is moded. A staged kernel does its per-block work in `process`, so
/// one that declares `prepare` is rejected.
///
/// **Inlining.** The app builds with `-fno-inline-functions`, which marks every definition noinline unless it says
/// otherwise, so `render`, `renderBlock`, the loop and every lambda handed to Argon are `[[gnu::always_inline]]`:
/// nothing sits between the block loop and the kernel's own always-inline `step`.
template <kernel K>
class Mono {
	using Coefficients = typename K::Coefficients; ///< K's coefficients, computed on configure.
	using State = typename K::State;               ///< K's state, carried from block to block.
	using Block = kernel_block_t<K>;               ///< K's per-block values, or EmptyBlock.
	using step_type = typename K::step_type;       ///< What one `step` takes and returns.

public:
	using value_type = typename K::value_type; ///< A block's sample: q31 or StereoSample.

	/// @brief Value-initialised Coefficients and State{}.
	Mono() = default;

	/// @brief Configured from @p settings, with State{}.
	explicit Mono(const typename K::Settings& settings) : coefficients_{K::coefficients(settings)} {}

	/// @brief Store the coefficients for @p settings; they apply from the next block, State untouched.
	void configure(const typename K::Settings& settings) { coefficients_ = K::coefficients(settings); }

	/// @brief The coefficients from the last configure, including any owner-facing values the kernel exposes.
	[[nodiscard]] const Coefficients& coefficients() const { return coefficients_; }

	/// @brief Return to the reset State: State{}, or whatever K's `static void reset(State&)` quirk sets when K
	/// declares one.
	void reset() {
		if constexpr (detail::declares_reset<K>) {
			K::reset(state_);
		}
		else {
			state_ = State{};
		}
	}

	/// @brief The kernel's state between blocks.
	[[nodiscard]] const State& state() const { return state_; }

	/// @brief Generate one sample: a one-sample block, so one `prepare(…, 1)` and one mode dispatch per call.
	/// @details A convenience for single samples; a vector kernel renders it as the partial step of a block of one,
	/// so a stateful vector kernel that does not declare kPartialStepAdvancesByCount advances by a whole vector on
	/// every call.
	[[gnu::always_inline]] value_type render()
	requires generator_kernel<K>
	{
		value_type out{};
		renderBlock(Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Fill @p out with the next `out.size()` samples.
	[[gnu::always_inline]] void renderBlock(Buffer<value_type> out)
	requires generator_kernel<K>
	{
		runBlock(out);
	}

	/// @brief Process one sample: a one-sample block, so one `prepare(…, 1)` and one mode dispatch per call.
	/// @details A convenience for single samples; a vector kernel renders it as the partial step of a block of one,
	/// so a stateful vector kernel that does not declare kPartialStepAdvancesByCount advances by a whole vector on
	/// every call.
	[[gnu::always_inline]] value_type render(value_type x)
	requires processor_kernel<K>
	{
		value_type out{};
		renderBlock(Signal<value_type>{&x, 1}, Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Process `out.size()` samples of @p in into @p out.
	/// @param in At least `out.size()` samples; may be the same buffer as @p out.
	/// @param out The block to fill, every sample of it.
	[[gnu::always_inline]] void renderBlock(Signal<value_type> in, Buffer<value_type> out)
	requires processor_kernel<K> || staged_kernel<K>
	{
		if constexpr (staged_kernel<K>) {
			detail::dispatch_mode<K>(coefficients_, [&]<auto M> [[gnu::always_inline]] () {
				if constexpr (moded_kernel<K>) {
					K::template process<M>(coefficients_, state_, in.first(out.size()), out);
				}
				else {
					K::process(coefficients_, state_, in.first(out.size()), out);
				}
			});
		}
		else {
			runBlock(out, in);
		}
	}

	/// @brief Mix one pair of samples: a one-sample block, so one `prepare(…, 1)` and one mode dispatch per call.
	/// @details A convenience for single samples; a vector kernel renders it as the partial step of a block of one.
	/// A mixer kernel cannot declare kPartialStepAdvancesByCount, so a stateful vector mixer advances a whole vector
	/// on every call.
	[[gnu::always_inline]] value_type render(value_type a, value_type b)
	requires mixer_kernel<K>
	{
		value_type out{};
		renderBlock(Signal<value_type>{&a, 1}, Signal<value_type>{&b, 1}, Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Mix `out.size()` samples of @p a and @p b into @p out.
	/// @param a,b At least `out.size()` samples each; either may be the same buffer as @p out.
	/// @param out The block to fill, every sample of it.
	[[gnu::always_inline]] void renderBlock(Signal<value_type> a, Signal<value_type> b, Buffer<value_type> out)
	requires mixer_kernel<K>
	{
		runBlock(out, a, b);
	}

private:
	/// @brief Render one block of a stepping kernel: `prepare` once, one mode dispatch, one loop.
	/// @param out The block to fill.
	/// @param in The input blocks: none, one or two.
	/// @details The loop runs on local copies of the coefficients and state, taken after `prepare` and written back
	/// after the block, so they stay in registers: the output buffer may alias the component, which would otherwise
	/// force a reload and a store of the state on every step. `prepare` sees the members, so the copies' addresses
	/// never escape to a `prepare` that is not inlined.
	template <typename... In>
	[[gnu::always_inline]] void runBlock(Buffer<value_type> out, In... in) {
		const Block block = detail::prepare_block<K>(coefficients_, state_, out.size());
		const Coefficients c = coefficients_;
		State s = state_;
		detail::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () { loop<M>(c, block, s, out, in...); });
		state_ = s;
	}

	/// @brief One vector step at mode @p M, counted on the partial last step when K declares
	/// kPartialStepAdvancesByCount.
	/// @param step The Argon loop step, full or partial.
	/// @param x The step's loaded inputs.
	template <auto M, typename Step, typename... X>
	[[gnu::always_inline]] static step_type vectorStepAt(const Coefficients& c, const Block& block, State& s,
	                                                     const Step& step, X... x) {
		if constexpr (partial_step_advances_by_count<K> && detail::is_partial_step<Step>) {
			return detail::step_at<K, M>(c, block, s, x..., ActiveLanes{.n = static_cast<std::size_t>(step.count())});
		}
		else {
			return detail::step_at<K, M>(c, block, s, x...);
		}
	}

	/// @brief The frames of an interleaved load as a StereoVector.
	/// @details A bit cast rather than element access: std::array's accessors are calls under
	/// `-fno-inline-functions`, and this runs once per step.
	[[gnu::always_inline]] static StereoVector toStereo(const std::array<Argon<q31>, 2>& channels) {
		static_assert(sizeof(StereoVector) == sizeof(channels));
		return __builtin_bit_cast(StereoVector, channels);
	}

	/// @brief Fill @p out from the input blocks @p in (none, one or two), one `step` at mode @p M per sample or
	/// vector; the loop is picked from K's step type (see the class documentation).
	/// @details The blocks' pointers and length are read here, once, so that the loop itself touches only raw
	/// pointers: a span accessor is a call under `-fno-inline-functions`.
	template <auto M, typename... In>
	[[gnu::always_inline]] static void loop(const Coefficients& c, const Block& block, State& s, Buffer<value_type> out,
	                                        In... in) {
		const auto n = static_cast<std::ptrdiff_t>(out.size());
		if constexpr (detail::scalar_step_kernel<K>) {
			scalarLoop<M>(c, block, s, out.data(), n, in.data()...);
		}
		else if constexpr (detail::vector_step_kernel<K>) {
			vectorLoop<M>(c, block, s, out.data(), n, in.data()...);
		}
		else {
			interleavedLoop<M>(c, block, s, detail::interleaved(out), n, detail::interleaved(in)...);
		}
	}

	/// @brief One `step` per sample: @p n samples of @p src into @p dst.
	template <auto M, typename... Src>
	[[gnu::always_inline]] static void scalarLoop(const Coefficients& c, const Block& block, State& s, value_type* dst,
	                                              std::ptrdiff_t n, const Src*... src) {
		for (std::ptrdiff_t i = 0; i < n; ++i) {
			dst[i] = detail::step_at<K, M>(c, block, s, src[i]...);
		}
	}

	/// @brief One `step` per vector of q31 samples: @p n samples of @p src into @p dst, the partial last vector
	/// included.
	template <auto M, typename... Src>
	[[gnu::always_inline]] static void vectorLoop(const Coefficients& c, const Block& block, State& s, q31* dst,
	                                              std::ptrdiff_t n, const Src*... src) {
		argon::vectorize::for_each<q31>(n, [&] [[gnu::always_inline]] (auto step) {
			step.Store(dst, vectorStepAt<M>(c, block, s, step, step.Load(src)...));
		});
	}

	/// @brief One `step` per vector of stereo frames: @p n frames of the interleaved @p src into the interleaved
	/// @p dst, the partial last vector included.
	template <auto M, typename... Src>
	[[gnu::always_inline]] static void interleavedLoop(const Coefficients& c, const Block& block, State& s, q31* dst,
	                                                   std::ptrdiff_t n, const Src*... src) {
		argon::vectorize::for_each_interleaved<q31, 2>(n, [&] [[gnu::always_inline]] (auto step) {
			const StereoVector frames = vectorStepAt<M>(c, block, s, step, toStereo(step.Load(src))...);
			step.Store(dst, {frames.l, frames.r});
		});
	}

	Coefficients coefficients_{}; ///< K's coefficients for the configured settings.
	State state_{};               ///< K's state, carried from block to block.
};

} // namespace deluge::dsp
