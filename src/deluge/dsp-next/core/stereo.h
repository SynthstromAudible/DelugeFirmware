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
#include "dsp-next/core/mono.h"
#include "dsp-next/core/types.h"
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>

/// @file
/// @brief Stereo frames over the app's interleaved buffers, and Stereo<K>: a mono kernel run on both channels.
namespace deluge::dsp {

/// @brief An interleaved L/R q31 buffer viewed as frames: `size() / 2` frames; an odd trailing sample is outside.
/// @details Any contiguous range of q31 (a span, an array, a vector). A const range gives a
/// @ref fixed::StereoSignal, a mutable one a @ref fixed::StereoBuffer. The view aliases @p interleaved: a frame is
/// its two consecutive q31 samples, left first, which the layout assert in stereo_sample.h guarantees. The app's
/// buffers are int32 storage, so no object of another type is created. A range that owns its storage must be an
/// lvalue, since a view of a temporary would dangle.
template <std::ranges::contiguous_range R>
requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<R>>, q31> && std::ranges::sized_range<R>
         && (std::is_lvalue_reference_v<R> || std::ranges::borrowed_range<R>)
[[nodiscard]] auto as_frames(R&& interleaved) {
	using Frame = std::conditional_t<std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R>>>,
	                                 const StereoSample, StereoSample>;
	return std::span<Frame>{reinterpret_cast<Frame*>(std::ranges::data(interleaved)),
	                        std::ranges::size(interleaved) / 2};
}

/// @brief A mono q31 kernel run on both channels of a stereo frame: one Coefficients shared by both channels, one State
/// per channel, left rendered fully before right on every frame.
/// @tparam K The kernel: a scalar q31 kernel. Stereo<K> is a @ref processor; K must be a processor kernel.
/// @details
/// **Shared coefficients.** `configure` calls `K::coefficients` once; both channels step with the result.
///
/// **Per block.** `renderBlock` calls K's `prepare(coefficients, state, n)` once per channel, when K declares one,
/// since a Block may depend on the State; then, for a moded kernel, dispatches once on `coefficients().mode`
/// through with_mode. Inside that one dispatch a single raw-pointer loop runs every frame in the block's mode, so a
/// mode change set by `configure` takes effect at the next block, and both States carry across it untouched.
///
/// **Order.** Each frame steps the left channel into a local, then the right, then stores the frame. A kernel
/// drawing from a stream shared by both channels (noise) therefore draws for left before right on every frame, and
/// the input and output blocks may be the same buffer.
///
/// **Inlining.** As in @ref Mono: the app builds with `-fno-inline-functions`, so `render`, `renderBlock` and the
/// loop are `[[gnu::always_inline]]`, and the loop runs on local copies of the coefficients and both States, taken
/// after `prepare` and written back after the block, so that they stay in registers even though the output may
/// alias the component.
template <processor_kernel K>
requires std::same_as<typename K::value_type, q31> && std::same_as<typename K::step_type, q31>
class Stereo {
	using Coefficients = typename K::Coefficients; ///< K's coefficients, computed on configure, shared by L and R.
	using State = typename K::State;               ///< K's state, one per channel, carried from block to block.
	using Block = kernel_block_t<K>;               ///< K's per-block values, one per channel, or EmptyBlock.

public:
	using value_type = StereoSample; ///< A block's frame: a left/right pair of q31 samples.

	/// @brief Value-initialised Coefficients and State{} on both channels.
	Stereo() = default;

	/// @brief Configured from @p settings, with State{} on both channels.
	explicit Stereo(const typename K::Settings& settings) : coefficients_{K::coefficients(settings)} {}

	/// @brief Store the coefficients for @p settings, computed once for both channels; they apply from the next
	/// block, both States untouched.
	void configure(const typename K::Settings& settings) { coefficients_ = K::coefficients(settings); }

	/// @brief The coefficients from the last configure, including any owner-facing values the kernel exposes.
	[[nodiscard]] const Coefficients& coefficients() const { return coefficients_; }

	/// @brief Return both channels to the reset State: State{}, or whatever K's `reset(State&)` quirk sets when K
	/// declares one.
	void reset() {
		if constexpr (requires(State& s) { K::reset(s); }) {
			K::reset(left_);
			K::reset(right_);
		}
		else {
			left_ = State{};
			right_ = State{};
		}
	}

	/// @brief The left channel's state between blocks.
	[[nodiscard]] const State& left() const { return left_; }

	/// @brief The right channel's state between blocks.
	[[nodiscard]] const State& right() const { return right_; }

	/// @brief Process one frame: a one-frame block, so one `prepare(…, 1)` per channel and one mode dispatch per
	/// call.
	[[gnu::always_inline]] StereoSample render(StereoSample frame) {
		StereoSample out{};
		renderBlock(Signal<StereoSample>{&frame, 1}, Buffer<StereoSample>{&out, 1});
		return out;
	}

	/// @brief Process `out.size()` frames of @p in into @p out, left before right on every frame.
	/// @param in At least `out.size()` frames; may be the same buffer as @p out.
	/// @param out The block to fill, every frame of it.
	[[gnu::always_inline]] void renderBlock(Signal<StereoSample> in, Buffer<StereoSample> out) {
		const std::size_t n = out.size();
		const Block left_block = detail::prepare_block<K>(coefficients_, left_, n);
		const Block right_block = detail::prepare_block<K>(coefficients_, right_, n);
		const Coefficients c = coefficients_;
		State l = left_;
		State r = right_;
		const StereoSample* src = in.data();
		StereoSample* dst = out.data();
		detail::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () {
			loop<M>(c, left_block, right_block, l, r, dst, static_cast<std::ptrdiff_t>(n), src);
		});
		left_ = l;
		right_ = r;
	}

private:
	/// @brief One `step` per channel per frame at mode @p M: @p n frames of @p src into @p dst, the left channel
	/// stepped into a local before the right.
	template <auto M>
	[[gnu::always_inline]] static void loop(const Coefficients& c, const Block& left_block, const Block& right_block,
	                                        State& l, State& r, StereoSample* dst, std::ptrdiff_t n,
	                                        const StereoSample* src) {
		for (std::ptrdiff_t i = 0; i < n; ++i) {
			const StereoSample x = src[i];
			const q31 left = detail::step_at<K, M>(c, left_block, l, x.l);
			const q31 right = detail::step_at<K, M>(c, right_block, r, x.r);
			dst[i] = {.l = left, .r = right};
		}
	}

	Coefficients coefficients_{}; ///< K's coefficients for the configured settings, shared by both channels.
	State left_{};                ///< The left channel's state, carried from block to block.
	State right_{};               ///< The right channel's state, carried from block to block.
};

} // namespace deluge::dsp
