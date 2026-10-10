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
#include <cstddef>
#include <utility>

/// @file
/// @brief Mono<K>: a kernel as a mono component, and the only loop a kernel runs in.
namespace deluge::dsp {

namespace detail {

/// @brief Whether the Argon loop step @p S is the partial last step of its block. Full steps are a distinct type.
template <typename S>
inline constexpr bool is_partial_step = false;

/// @brief A partial step of `argon::vectorize::for_each`.
template <typename T>
inline constexpr bool is_partial_step<argon::vectorize::Step<T, false>> = true;

/// @brief A partial step of `argon::vectorize::for_each_interleaved`.
template <typename T, std::size_t Stride>
inline constexpr bool is_partial_step<argon::vectorize::InterleavedStep<T, Stride, false>> = true;

} // namespace detail

/// @brief A kernel as a mono component: one Coefficients, one State; the only loop.
/// @tparam K The kernel. Mono<K> is a @ref generator, @ref processor or @ref mixer to match K's step arity; a staged
/// kernel makes a @ref block_processor only. A control kernel is rejected at the class: it has no per-sample loop,
/// and `Control` wraps it.
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
/// `process<M>` through with_mode when the kernel is moded, through @ref kernel_ops::process_at. A staged kernel does
/// its per-block work in `process`, so one that declares `prepare` is rejected. `process` keeps the in-place promise
/// itself: @ref staged_kernel requires it to give the same result when `in` and `out` are the same buffer.
///
/// **Single samples.** `render(…)` is a one-sample block: it builds one-sample spans and runs a whole block's
/// `prepare`, dispatch and loop, which under the shipping `-fno-inline-functions` costs several out-of-line
/// `std::span` calls per call. It is a convenience for tests and for control-rate values, not for per-sample use in
/// shipping code: a component renders blocks, and a kernel that needs another kernel per sample nests it and calls
/// it through @ref kernel_ops.
///
/// **Inlining.** The app builds with `-fno-inline-functions`, which marks every definition noinline unless it says
/// otherwise, so `render`, `renderBlock`, the loop and every lambda handed to Argon are `[[gnu::always_inline]]`:
/// nothing sits between the block loop and the kernel's own always-inline `step`.
template <kernel K>
requires(!control_kernel<K>)
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

	/// @brief Run the event @p Event on the state: with the coefficients when it takes them, else without.
	/// @tparam Event A function taking K's State by non-const reference, and optionally its Coefficients first, as
	/// @ref Control::on accepts. A staged kernel's `on` is the same.
	/// @param args The event's own arguments, after the coefficients and state.
	/// @return What @p Event returns, a reference included.
	template <auto Event, typename... A>
	[[gnu::always_inline]] decltype(auto) on(A&&... args) {
		return detail::run_event<K, Event>(coefficients_, state_, std::forward<A>(args)...);
	}

	/// @brief Generate one sample: a one-sample block, so one `prepare(…, 1)` and one mode dispatch per call.
	/// @details A convenience for tests and control rate, not for per-sample use in shipping code (see **Single
	/// samples** in the class documentation); a vector kernel renders it as the partial step of a block of one,
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
	/// @details A convenience for tests and control rate, not for per-sample use in shipping code (see **Single
	/// samples** in the class documentation); a vector kernel renders it as the partial step of a block of one,
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
			kernel_ops::dispatch_mode<K>(coefficients_, [&]<auto M> [[gnu::always_inline]] () {
				kernel_ops::process_at<K, M>(coefficients_, state_, in.first(out.size()), out);
			});
		}
		else {
			runBlock(out, in);
		}
	}

	/// @brief Mix one pair of samples: a one-sample block, so one `prepare(…, 1)` and one mode dispatch per call.
	/// @details A convenience for tests and control rate, not for per-sample use in shipping code (see **Single
	/// samples** in the class documentation); a vector kernel renders it as the partial step of a block of one.
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
		const Block block = kernel_ops::prepare_block<K>(coefficients_, state_, out.size());
		const Coefficients c = coefficients_;
		State s = state_;
		kernel_ops::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () { loop<M>(c, block, s, out, in...); });
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
			return kernel_ops::step_at<K, M>(c, block, s, x...,
			                                 ActiveLanes{.n = static_cast<std::size_t>(step.count())});
		}
		else {
			return kernel_ops::step_at<K, M>(c, block, s, x...);
		}
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
			dst[i] = kernel_ops::step_at<K, M>(c, block, s, src[i]...);
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
			const StereoVector frames = vectorStepAt<M>(c, block, s, step, detail::toStereo(step.Load(src))...);
			step.Store(dst, {frames.l, frames.r});
		});
	}

	Coefficients coefficients_{}; ///< K's coefficients for the configured settings.
	State state_{};               ///< K's state, carried from block to block.
};

} // namespace deluge::dsp
